//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Video Overlay support implementation
//---------------------------------------------------------------------------


#include "tjsCommHead.h"

#include <algorithm>
#include "MsgIntf.h"
#include "VideoOvlImpl.h"
#include "DebugIntf.h"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "SysInitIntf.h"
#include "StorageImpl.h"
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
#include "krmovie.h"
#endif
#include "PluginImpl.h"
#include "WaveImpl.h"  // for DirectSound attenuate <-> TVP volume
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
#include <evcode.h>
#endif

#include "Application.h"
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
#include "TVPVideoOverlay.h"
#else
#define TVPDSAttenuateToPan(x) x
#define TVPDSAttenuateToVolume(x) x
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/val.h>
#include <emscripten/bind.h>
#include <string>
#include "CharacterSet.h"

// Heap-allocated state for the HTML5 <video> overlay. Kept out of the header
// so emscripten headers do not leak into VideoOvlImpl.h.
struct tVPVideoEmscState
{
	emscripten::val video = emscripten::val::null();
	emscripten::val onEnded = emscripten::val::null();
	emscripten::val onError = emscripten::val::null();
	emscripten::val onCanPlay = emscripten::val::null();
	std::string blobUrl;
	bool wantPlay = false;
	bool ready = false;
	bool failed = false;
};

static inline bool TVP_EmscValOk(const emscripten::val &v)
{
	return !v.isUndefined() && !v.isNull();
}

// Extensions browsers typically cannot decode in <video>; these are routed
// through the server-side transcode cache (video_cache/...).
static bool TVP_EmscIsUnsupportedExt(const std::string &e)
{
	return e == "wmv" || e == "asf" || e == "avi" || e == "mpg" ||
		e == "mpeg" || e == "mpe" || e == "flv" || e == "rm" ||
		e == "rmvb" || e == "vob" || e == "3gp";
}

static std::string TVP_EmscMimeForExt(const std::string &e)
{
	if(e == "webm") return "video/webm";
	if(e == "ogv" || e == "ogg") return "video/ogg";
	if(e == "mp4" || e == "m4v") return "video/mp4";
	if(e == "mov") return "video/quicktime";
	if(e == "mkv") return "video/x-matroska";
	if(!e.empty()) return std::string("video/") + e;
	return "video/mp4";
}

// Build the server-side transcode cache URL for an unsupported-format video.
// gamePath: e.g. "games/Data.xp3" (the ?data= value).
// videoPath: in-archive path, e.g. "video/spp1.wmv".
// -> "video_cache/games/Data.xp3.d/video/spp1.wmv.mp4"
static std::string TVP_EmscCacheRelPath(const std::string &gamePath,
	const std::string &videoPath)
{
	// 保留原始文件名（含扩展名）再追加 .mp4，避免同目录下仅扩展名不同的
	// 视频在缓存里撞名（如 a.wmv 与 a.avi 都映射到 a.mp4）。归档内条目名
	// 唯一，故 <videoPath>.mp4 在单个游戏缓存内必然唯一；跨游戏由 .d 隔离。
	return "video_cache/" + gamePath + ".d/" + videoPath + ".mp4";
}

static bool TVP_EmscVideoHelpersInstalled = false;
static void TVP_EmscVideoEnsureHelpers()
{
	if(TVP_EmscVideoHelpersInstalled) return;
	TVP_EmscVideoHelpersInstalled = true;
	static const char *code = R"js(
(function(){
  if(window.__tvpVideo) return;
  var H = {
    canvasId: 'canvas',
    makeHandler: function(name){
      return function(ev){
        var v = ev && ev.target;
        if(v && v._tvpPtr != null){
          try { if(typeof Module !== 'undefined' && Module[name]) Module[name](v._tvpPtr); }
          catch(e){ if(typeof console !== 'undefined') console.error('[tvpVideo]', e); }
        }
      };
    },
    play: function(v){
      try {
        var p = v.play();
        if(p && p.then){
          p.then(function(){}, function(err){
            if(v._tvpMutedRetry) return;
            v._tvpMutedRetry = true;
            try { v.muted = true; var p2 = v.play(); if(p2 && p2.then){ p2.then(function(){}, function(e){ if(console) console.warn('[tvpVideo] play rejected', e); }); } }
            catch(e2){ if(console) console.warn('[tvpVideo]', e2); }
          });
        }
      } catch(e){ if(console) console.warn('[tvpVideo]', e); }
    },
    pause: function(v){ try { v.pause(); } catch(e){} },
    seek0: function(v){ try { v.currentTime = 0; } catch(e){} },
    fetchCachedVideo: function(rel, ptr){
      var self = this;
      try {
        fetch(rel).then(function(resp){
          if(!resp.ok){ self._fetchFail(ptr); return null; }
          return resp.arrayBuffer();
        }).then(function(buf){
          if(!buf) return;
          var blob = new Blob([buf], {type:'video/mp4'});
          var u = URL.createObjectURL(blob);
          if(Module && Module.tvpVideoCbFetched) Module.tvpVideoCbFetched(ptr, u);
        }).catch(function(e){ if(console) console.warn('[tvpVideo] cache fetch failed', e); self._fetchFail(ptr); });
      } catch(e){ if(console) console.warn('[tvpVideo]', e); self._fetchFail(ptr); }
    },
    _fetchFail: function(ptr){ if(Module && Module.tvpVideoCbFetchFail) Module.tvpVideoCbFetchFail(ptr); },
    reposition: function(v){
      var c = document.getElementById(this.canvasId);
      if(!c || !v) return;
      var r = c.getBoundingClientRect();
      var W = v._tvpW, Hh = v._tvpH;
      if(!(W > 0 && Hh > 0)){ W = c.width || r.width; Hh = c.height || r.height; }
      var s = Math.min(r.width / W, r.height / Hh);
      var dw = W * s, dh = Hh * s;
      var ox = r.left + (r.width - dw) / 2, oy = r.top + (r.height - dh) / 2;
      var st = v.style;
      st.position = 'absolute';
      st.left = (ox + v._tvpLx * s) + 'px';
      st.top = (oy + v._tvpLy * s) + 'px';
      st.width = (v._tvpLw * s) + 'px';
      st.height = (v._tvpLh * s) + 'px';
    },
    repositionAll: function(){
      var a = document.querySelectorAll('video[data-tvp-video="1"]');
      for(var i = 0; i < a.length; i++) this.reposition(a[i]);
    }
  };
  window.__tvpVideo = H;
  window.addEventListener('resize', function(){ if(window.__tvpVideo) window.__tvpVideo.repositionAll(); });
  window.addEventListener('orientationchange', function(){ if(window.__tvpVideo) window.__tvpVideo.repositionAll(); });
})();
)js";
	emscripten_run_script(code);
}
#endif // __EMSCRIPTEN__


//---------------------------------------------------------------------------
static std::vector<tTJSNI_VideoOverlay *> TVPVideoOverlayVector;
//---------------------------------------------------------------------------
static void TVPAddVideOverlay(tTJSNI_VideoOverlay *ovl)
{
	TVPVideoOverlayVector.push_back(ovl);
}
//---------------------------------------------------------------------------
static void TVPRemoveVideoOverlay(tTJSNI_VideoOverlay *ovl)
{
	std::vector<tTJSNI_VideoOverlay*>::iterator i;
	i = std::find(TVPVideoOverlayVector.begin(), TVPVideoOverlayVector.end(), ovl);
	if(i != TVPVideoOverlayVector.end())
		TVPVideoOverlayVector.erase(i);
}
//---------------------------------------------------------------------------
static void TVPShutdownVideoOverlay()
{
	// shutdown all overlay object and release krmovie.dll / krflash.dll
	std::vector<tTJSNI_VideoOverlay*>::iterator i;
	for(i = TVPVideoOverlayVector.begin(); i != TVPVideoOverlayVector.end(); i++)
	{
		(*i)->Shutdown();
	}
}
static tTVPAtExit TVPShutdownVideoOverlayAtExit
	(TVP_ATEXIT_PRI_PREPARE, TVPShutdownVideoOverlay);
//---------------------------------------------------------------------------




//---------------------------------------------------------------------------
// tTJSNI_VideoOverlay
//---------------------------------------------------------------------------
tTJSNI_VideoOverlay::tTJSNI_VideoOverlay()
: EventQueue(this,&tTJSNI_VideoOverlay::WndProc)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	VideoOverlay = NULL;
#endif
	Rect.left = 0;
	Rect.top = 0;
	Rect.right = 320;
	Rect.bottom = 240;
	Visible = false;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	OwnerWindow = NULL;
#endif
	LocalTempStorageHolder = NULL;

	EventQueue.Allocate();

	Layer1 = NULL;
	Layer2 = NULL;
	Mode = vomOverlay;
	Loop = false;
	IsPrepare = false;
	SegLoopStartFrame = -1;
	SegLoopEndFrame = -1;
	IsEventPast = false;
	EventFrame = -1;

#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	Bitmap[0] = Bitmap[1] = NULL;
	BmpBits[0] = BmpBits[1] = NULL;
#endif
#ifdef __EMSCRIPTEN__
	EmscVideoState = nullptr;
	EmscVolume = 100000; // full (TVP volume scale: 0..100000)
#endif
}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD
tTJSNI_VideoOverlay::Construct(tjs_int numparams, tTJSVariant **param,
		iTJSDispatch2 *tjs_obj)
{
	tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
	if(TJS_FAILED(hr)) return hr;

	return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_VideoOverlay::Invalidate()
{
	inherited::Invalidate();

	Close();

	EventQueue.Deallocate();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Open(const ttstr &_name)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// open

	// first, close
	Close();


	// check window
	if(!Window) TVPThrowExceptionMessage(TVPWindowAlreadyMissing);

	// open target storage
	ttstr name(_name);
	ttstr param;

	const tjs_char * param_pos;
	int param_pos_ind;
	param_pos = TJS_strchr(name.c_str(), TJS_W('?'));
	param_pos_ind = (int)(param_pos - name.c_str());
	if(param_pos != NULL)
	{
		param = param_pos;
		name = ttstr(name, param_pos_ind);
	}

	IStream *istream = NULL;
	long size;
	ttstr ext = TVPExtractStorageExt(name).c_str();
	ext.ToLowerCase();

	{
		// prepate IStream
		tTJSBinaryStream *stream0 = NULL;
		try
		{
			stream0 = TVPCreateStream(name);
			size = (long)stream0->GetSize();
		}
		catch(...)
		{
			if(stream0) delete stream0;
			throw;
		}

		istream = new tTVPIStreamAdapter(stream0);
	}

	// 'istream' is an IStream instance at this point

	// create video overlay object
	try
	{
		{
			if(Mode == vomLayer)
				GetVideoLayerObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(), size, &VideoOverlay);
			else if(Mode == vomMixer)
				GetMixingVideoOverlayObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(), size, &VideoOverlay);
			else if(Mode == vomMFEVR)
				GetMFVideoOverlayObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(), size, &VideoOverlay);
			else
				GetVideoOverlayObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(), size, &VideoOverlay);
		}

		if( (Mode == vomOverlay) || (Mode == vomMixer) || (Mode == vomMFEVR) )
		{
			ResetOverlayParams();
		}
		else
		{	// set font and back buffer to layerVideo
			long	width, height;
			long			size;
			VideoOverlay->GetVideoSize( &width, &height );
			
			if( width <= 0 || height <= 0 )
				TVPThrowExceptionMessage(TVPErrorInKrMovieDLL, (const tjs_char*)TVPInvalidVideoSize);

			size = width * height * 4;
			if( Bitmap[0] != NULL )
				delete Bitmap[0];
			if( Bitmap[1] != NULL )
				delete Bitmap[1];
			Bitmap[0] = new tTVPBaseBitmap( width, height, 32 );
			Bitmap[1] = new tTVPBaseBitmap( width, height, 32 );

			BmpBits[0] = static_cast<BYTE*>(Bitmap[0]->GetBitmap()->GetScanLine( Bitmap[0]->GetBitmap()->GetHeight()-1 ));
			BmpBits[1] = static_cast<BYTE*>(Bitmap[1]->GetBitmap()->GetScanLine( Bitmap[1]->GetBitmap()->GetHeight()-1 ));
			//BmpBits[0] = static_cast<BYTE*>(Bitmap[0]->GetBitmap()->GetScanLine( 0 ));
			//BmpBits[1] = static_cast<BYTE*>(Bitmap[1]->GetBitmap()->GetScanLine( 0 ));

			VideoOverlay->SetVideoBuffer( BmpBits[0], BmpBits[1], size );
		}
	}
	catch(...)
	{
		if(istream) istream->Release();
		Close();
		throw;
	}
	if(istream) istream->Release();

	// set Status
	ClearWndProcMessages();
	SetStatus(tTVPVideoOverlayStatus::Stop);
#elif defined(__EMSCRIPTEN__)
	EmscOpen(_name);
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Close()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// close
	// release VideoOverlay object
	if(VideoOverlay)
	{
		VideoOverlay->Release(), VideoOverlay = NULL;
		::SetFocus(Window->GetWindowHandle());
	}
	if(LocalTempStorageHolder)
		delete LocalTempStorageHolder, LocalTempStorageHolder = NULL;
	ClearWndProcMessages();
	SetStatus(tTVPVideoOverlayStatus::Unload);

	if( Bitmap[0] )
		delete Bitmap[0];
	if( Bitmap[1] )
		delete Bitmap[1];

	Bitmap[0] = Bitmap[1] = NULL;
	BmpBits[0] = BmpBits[1] = NULL;
#elif defined(__EMSCRIPTEN__)
	EmscClose();
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Shutdown()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// shutdown the system
	// this functions closes the overlay object, but must not fire any events.
	bool c = CanDeliverEvents;
	ClearWndProcMessages();
	SetStatus(tTVPVideoOverlayStatus::Unload);
	try
	{
		if(VideoOverlay) VideoOverlay->Release(), VideoOverlay = NULL;
	}
	catch(...)
	{
		CanDeliverEvents = c;
		throw;
	}
	CanDeliverEvents = c;
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Disconnect()
{
	// disconnect the object
	Shutdown();

	Window = NULL;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Play()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// start playing
	if(VideoOverlay)
	{
		VideoOverlay->Play();
		ClearWndProcMessages();
		if( Mode != vomMFEVR ) SetStatus(tTVPVideoOverlayStatus::Play);
	}
#elif defined(__EMSCRIPTEN__)
	EmscPlay();
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Stop()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// stop playing
	if(VideoOverlay)
	{
		VideoOverlay->Stop();
		ClearWndProcMessages();
		if( Mode != vomMFEVR ) SetStatus(tTVPVideoOverlayStatus::Stop);
	}
#elif defined(__EMSCRIPTEN__)
	EmscStop();
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Pause()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// pause playing
	if(VideoOverlay)
	{
		VideoOverlay->Pause();
//		ClearWndProcMessages();
		if( Mode != vomMFEVR ) SetStatus(tTVPVideoOverlayStatus::Pause);
	}
#elif defined(__EMSCRIPTEN__)
	EmscPause();
#endif
}
void tTJSNI_VideoOverlay::Rewind()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// rewind playing
	if(VideoOverlay)
	{
		VideoOverlay->Rewind();
		ClearWndProcMessages();

		if( EventFrame >= 0 && IsEventPast )
			IsEventPast = false;
	}
#endif
}
void tTJSNI_VideoOverlay::Prepare()
{	// prepare movie
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if( VideoOverlay && (Mode == vomLayer) )
	{
		Pause();
		Rewind();
		IsPrepare = true;
		Play();
	}
#endif
}
void tTJSNI_VideoOverlay::SetSegmentLoop( int comeFrame, int goFrame )
{
	SegLoopStartFrame = comeFrame;
	SegLoopEndFrame = goFrame;
}
void tTJSNI_VideoOverlay::SetPeriodEvent( int eventFrame )
{
	EventFrame = eventFrame;

	if( eventFrame <= GetFrame() )
		IsEventPast = true;
	else
		IsEventPast = false;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetRectangleToVideoOverlay()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// set Rectangle to video overlay
	if(VideoOverlay && OwnerWindow)
	{
		tjs_int ofsx, ofsy;
		Window->GetVideoOffset(ofsx, ofsy);
		tjs_int l = Rect.left;
		tjs_int t = Rect.top;
		tjs_int r = Rect.right;
		tjs_int b = Rect.bottom;
		TVPAddLog(TJS_W("Video zoom: (") + ttstr(l) + TJS_W(",") + ttstr(t) + TJS_W(")-(") +
			ttstr(r) + TJS_W(",") + ttstr(b) + TJS_W(") ->"));
		Window->ZoomRectangle(l, t, r, b);
		TVPAddLog(TJS_W("(") + ttstr(l) + TJS_W(",") + ttstr(t) + TJS_W(")-(") +
			ttstr(r) + TJS_W(",") + ttstr(b) + TJS_W(")"));
		RECT rect = {l + ofsx, t + ofsy, r + ofsx, b + ofsy};
		VideoOverlay->SetRect(&rect);
	}
#elif defined(__EMSCRIPTEN__)
	EmscUpdateRect();
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetPosition(tjs_int left, tjs_int top)
{
	if( Mode == vomLayer )
	{
		if( Layer1 != NULL ) Layer1->SetPosition( left, top );
		if( Layer2 != NULL ) Layer2->SetPosition( left, top );
	}
	else
	{
		Rect.set_offsets(left, top);
		SetRectangleToVideoOverlay();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetSize(tjs_int width, tjs_int height)
{
	if( Mode == vomLayer ) return;

	Rect.set_size(width, height);
	SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetBounds(const tTVPRect & rect)
{
	if( Mode == vomLayer ) return;

	Rect = rect;
	SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetLeft(tjs_int l)
{
	if( Mode == vomLayer )
	{
		if( Layer1 != NULL ) Layer1->SetLeft( l );
		if( Layer2 != NULL ) Layer2->SetLeft( l );
	}
	else
	{
		Rect.set_offsets(l, Rect.top);
		SetRectangleToVideoOverlay();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetTop(tjs_int t)
{
	if( Mode == vomLayer )
	{
		if( Layer1 != NULL ) Layer1->SetTop( t );
		if( Layer2 != NULL ) Layer2->SetTop( t );
	}
	else
	{
		Rect.set_offsets(Rect.left, t);
		SetRectangleToVideoOverlay();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetWidth(tjs_int w)
{
	if( Mode == vomLayer ) return;

	Rect.right = Rect.left + w;
	SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetHeight(tjs_int h)
{
	if( Mode == vomLayer ) return;

	Rect.bottom = Rect.top + h;
	SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetVisible(bool b)
{
	Visible = b;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		if( Mode == vomLayer )
		{
			if( Layer1 != NULL ) Layer1->SetVisible( Visible );
			if( Layer2 != NULL ) Layer2->SetVisible( Visible );
		}
		else
		{
			VideoOverlay->SetVisible(Visible);
		}
	}
#endif
#ifdef __EMSCRIPTEN__
	EmscSetVisible(b);
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::ResetOverlayParams()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// retrieve new window information from owner window and
	// set video owner window / message drain window.
	// also sets rectangle and visible state.
	if(VideoOverlay && Window && (Mode == vomOverlay || Mode == vomMixer || Mode == vomMFEVR) )
	{
		OwnerWindow = Window->GetWindowHandle();
		VideoOverlay->SetWindow(OwnerWindow);

		VideoOverlay->SetMessageDrainWindow(Window->GetSurfaceWindowHandle());

		// set Rectangle
		SetRectangleToVideoOverlay();

		// set Visible
		VideoOverlay->SetVisible(Visible);
	}
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::DetachVideoOverlay()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay && Window && (Mode == vomOverlay || Mode == vomMixer || Mode == vomMFEVR) )
	{
		VideoOverlay->SetWindow(NULL);
		VideoOverlay->SetMessageDrainWindow(EventQueue.GetOwner());
			// once set to util window
	}
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetRectOffset(tjs_int ofsx, tjs_int ofsy)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		RECT r = {Rect.left + ofsx, Rect.top + ofsy,
			Rect.right + ofsx, Rect.bottom + ofsy};
		VideoOverlay->SetRect(&r);
	}
#endif
}
//---------------------------------------------------------------------------
//void __fastcall tTJSNI_VideoOverlay::WndProc(Messages::TMessage &Msg)
void tTJSNI_VideoOverlay::WndProc( NativeEvent& ev )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// EventQueue's message procedure
	if(VideoOverlay)
	{
		switch(ev.Message) {
		case WM_GRAPHNOTIFY:
		{
			long evcode;
			LONG_PTR p1, p2;
			bool got;
			do {
				VideoOverlay->GetEvent(&evcode, &p1, &p2, &got);
				if( got == false)
					return;

				switch( evcode )
				{
					case EC_COMPLETE:
						if( Status == tTVPVideoOverlayStatus::Play )
						{
							if( Loop )
							{
								Rewind();
								FirePeriodEvent(perLoop); // fire period event by loop rewind
							}
							else
							{
								// Graph manager seems not to complete playing
								// at this point (rewinding the movie at the event
								// handler called asynchronously from SetStatusAsync
								// makes continuing playing, but the graph seems to
								// be unstable).
								// We manually stop the manager anyway.
								VideoOverlay->Stop();
								SetStatusAsync(tTVPVideoOverlayStatus::Stop); // All data has been rendered
							}
						}
						break;
					case EC_UPDATE:
						if( Mode == vomLayer && Status == tTVPVideoOverlayStatus::Play )
						{
							int		curFrame = (int)p1;
							if( Layer1 == NULL && Layer2 == NULL )	// nothing to do.
								return;

							// 2フレーム以上差があるときはGetFrame() を現在のフレームとする
							int frame = GetFrame();
							if( (frame+1) < curFrame || (frame-1) > curFrame )
								curFrame = frame;

							if( (!IsPrepare) && (SegLoopEndFrame > 0) && (frame >= SegLoopEndFrame) ) {
								SetFrame( SegLoopStartFrame > 0 ? SegLoopStartFrame : 0 );
								FirePeriodEvent(perSegLoop); // fire period event by segment loop rewind
								return; // Updateを行わない
							}

							// get video image size
							long	width, height;
							VideoOverlay->GetVideoSize( &width, &height );

							tTJSNI_BaseLayer	*l1 = Layer1;
							tTJSNI_BaseLayer	*l2 = Layer2;

							// Check layer image size
							if( l1 != NULL )
							{
								if( (long)l1->GetImageWidth() != width || (long)l1->GetImageHeight() != height )
									l1->SetImageSize( width, height );
								if( (long)l1->GetWidth() != width || (long)l1->GetHeight() != height )
									l1->SetSize( width, height );
							}
							if( l2 != NULL )
							{
								if( (long)l2->GetImageWidth() != width || (long)l2->GetImageHeight() != height )
									l2->SetImageSize( width, height );
								if( (long)l2->GetWidth() != width || (long)l2->GetHeight() != height )
									l2->SetSize( width, height );
							}
							BYTE *buff;
							VideoOverlay->GetFrontBuffer( &buff );
							if( buff == BmpBits[0] )
							{
								if( l1 ) l1->AssignMainImage( Bitmap[0] );
								if( l2 ) l2->AssignMainImage( Bitmap[0] );
							}
							else	// 0じゃなかったら、1とみなす。
							{
								if( l1 ) l1->AssignMainImage( Bitmap[1] );
								if( l2 ) l2->AssignMainImage( Bitmap[1] );
							}
							if( l1 ) l1->Update();
							if( l2 ) l2->Update();
							FireFrameUpdateEvent( curFrame );

							// ! Prepare mode ?
							if( !IsPrepare )
							{
								// Send period event ?
								if( EventFrame >= 0 && !IsEventPast && curFrame >= EventFrame )
								{
									EventFrame = -1;
									FirePeriodEvent(perPeriod); // fire period event by setPeriodEvent()
								}
							}
							else
							{	// Prepare mode
								FirePeriodEvent(perPrepare); // fire period event by prepare()
								Pause();
								Rewind();
								IsPrepare = false;
							}
						}
						else if( Mode == vomMixer && Status == tTVPVideoOverlayStatus::Play )
						{
							int frame = GetFrame();
							if( (!IsPrepare) && (SegLoopEndFrame > 0) && (frame >= SegLoopEndFrame) ) {
								SetFrame( SegLoopStartFrame > 0 ? SegLoopStartFrame : 0 );
								FirePeriodEvent(perSegLoop); // fire period event by segment loop rewind
								return;
							}
							VideoOverlay->PresentVideoImage();
							FireFrameUpdateEvent( frame );
							// Send period event ?
							if( EventFrame >= 0 && !IsEventPast && frame >= EventFrame )
							{
								EventFrame = -1;
								FirePeriodEvent(perPeriod); // fire period event by setPeriodEvent()
							}
						}
						break;
				}
				VideoOverlay->FreeEventParams( evcode, p1, p2 );
			} while( got );
			return;
		}
		case WM_CALLBACKCMD:
		{
			// wparam : command
			// lparam : argument
			FireCallbackCommand((tjs_char*)ev.WParam, (tjs_char*)ev.LParam);
			return;
		}
		case WM_STATE_CHANGE:
			{
				switch( ev.WParam ) {
				case vsStopped:
					SetStatusAsync( tTVPVideoOverlayStatus::Stop );
					break;
				case vsPlaying:
					SetStatusAsync( tTVPVideoOverlayStatus::Play );
					break;
				case vsPaused:
					SetStatusAsync( tTVPVideoOverlayStatus::Pause );
					break;
				case vsReady:
					SetStatusAsync( tTVPVideoOverlayStatus::Ready );
					break;
				case vsEnded:
					if( Status == tTVPVideoOverlayStatus::Play )
					{
						if( Loop )
						{
							VideoOverlay->Play();
							FirePeriodEvent(perLoop); // fire period event by loop rewind
						}
						else
						{
							VideoOverlay->Stop();
							SetStatusAsync(tTVPVideoOverlayStatus::Stop); // All data has been rendered
						}
					}
					break;
				}
				return;
			}
		}
	}

	EventQueue.HandlerDefault(ev);
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetTimePosition( tjs_uint64 p )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetPosition( p );
	}
#endif
}
tjs_uint64 tTJSNI_VideoOverlay::GetTimePosition()
{
	tjs_uint64	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetPosition( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetFrame( tjs_int f )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetFrame( f );

		if( EventFrame >= f && IsEventPast )
			IsEventPast = false;
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetFrame()
{
	tjs_int	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetFrame( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetStopFrame( tjs_int f )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetStopFrame( f );
	}
#endif
}
void tTJSNI_VideoOverlay::SetDefaultStopFrame()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetDefaultStopFrame();
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetStopFrame()
{
	tjs_int	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetStopFrame( &result );
	}
#endif
	return result;
}
tjs_real tTJSNI_VideoOverlay::GetFPS()
{
	tjs_real	result = 0.0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetFPS( &result );
	}
#endif
	return result;
}
tjs_int tTJSNI_VideoOverlay::GetNumberOfFrame()
{
	tjs_int	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetNumberOfFrame( &result );
	}
#endif
	return result;
}
tjs_int64 tTJSNI_VideoOverlay::GetTotalTime()
{
	tjs_int64	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetTotalTime( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetLoop( bool b )
{
	Loop = b;
}
void tTJSNI_VideoOverlay::SetLayer1( tTJSNI_BaseLayer *l )
{
	Layer1 = l;
}
void tTJSNI_VideoOverlay::SetLayer2( tTJSNI_BaseLayer *l )
{
	Layer2 = l;
}
void tTJSNI_VideoOverlay::SetMode( tTVPVideoOverlayMode m )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// ビデオオープン後のモード変更は禁止
	if( !VideoOverlay )
	{
		Mode = m;
	}
#elif defined(__EMSCRIPTEN__)
	if(!EmscVideoState) Mode = m;
#endif
}

tjs_real tTJSNI_VideoOverlay::GetPlayRate()
{
	tjs_real	result = 0.0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetPlayRate( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetPlayRate(tjs_real r)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetPlayRate( r );
	}
#endif
}

tjs_int tTJSNI_VideoOverlay::GetAudioBalance()
{
	long	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetAudioBalance( &result );
	}
#endif
	return TVPDSAttenuateToPan( result );
}
void tTJSNI_VideoOverlay::SetAudioBalance(tjs_int b)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetAudioBalance( TVPPanToDSAttenuate( b ) );
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetAudioVolume()
{
	long	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetAudioVolume( &result );
	}
#endif
	return TVPDSAttenuateToVolume( result );
}
void tTJSNI_VideoOverlay::SetAudioVolume(tjs_int b)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetAudioVolume( TVPVolumeToDSAttenuate( b ) );
	}
#elif defined(__EMSCRIPTEN__)
	EmscVolume = b;
	EmscSetVolume(b);
#endif
}
tjs_uint tTJSNI_VideoOverlay::GetNumberOfAudioStream()
{
	unsigned long	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetNumberOfAudioStream( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SelectAudioStream(tjs_uint n)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SelectAudioStream( n );
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetEnabledAudioStream()
{
	long		result = -1;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetEnableAudioStreamNum( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::DisableAudioStream()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->DisableAudioStream();
	}
#endif
}

tjs_uint tTJSNI_VideoOverlay::GetNumberOfVideoStream()
{
	unsigned long	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetNumberOfVideoStream( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SelectVideoStream(tjs_uint n)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SelectVideoStream( n );
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetEnabledVideoStream()
{
	long		result = -1;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetEnableVideoStreamNum( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetMixingLayer( tTJSNI_BaseLayer *l )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		if( l )
		{
			if( l->GetVisible() )
			{
				float	alpha = static_cast<float>(l->GetOpacity()) / 255.0f;
				RECT	dest;
				dest.left = l->GetLeft() + l->GetImageLeft();
				dest.top = l->GetTop() + l->GetImageTop();
				dest.right = dest.left + l->GetImageWidth();
				dest.bottom = dest.top + l->GetImageHeight();

				// tTVPBaseBitmap->tTVPBitmap
				tTVPBitmap *bmp = l->GetMainImage()->GetBitmap();
				if( bmp )
				{
					// 自前でDCを作る
					HDC hdc;
					HDC			ref = GetDC(0);
					HBITMAP		myDIB = CreateDIBitmap( ref, bmp->GetBITMAPINFOHEADER(), CBM_INIT, bmp->GetBits(), bmp->GetBITMAPINFO(), bmp->Is8bit() ? DIB_PAL_COLORS : DIB_RGB_COLORS );
					hdc = CreateCompatibleDC( NULL );
					HGDIOBJ		hOldBmp = SelectObject( hdc, myDIB );

					VideoOverlay->SetMixingBitmap( hdc, &dest, alpha );

					SelectObject( hdc, hOldBmp );
					DeleteObject( myDIB );
					DeleteDC( hdc );
				}
			}
			else
			{
				VideoOverlay->ResetMixingBitmap();
			}
		}
		else
		{
			VideoOverlay->ResetMixingBitmap();
		}
	}
#endif
}
void tTJSNI_VideoOverlay::ResetMixingBitmap()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->ResetMixingBitmap();
	}
#endif
}
void tTJSNI_VideoOverlay::SetMixingMovieAlpha( tjs_real a )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetMixingMovieAlpha( static_cast<float>(a) );
	}
#endif
}
tjs_real tTJSNI_VideoOverlay::GetMixingMovieAlpha()
{
	float	ret = 0.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetMixingMovieAlpha( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetMixingMovieBGColor( tjs_uint col )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetMixingMovieBGColor( col );
	}
#endif
}
tjs_uint tTJSNI_VideoOverlay::GetMixingMovieBGColor()
{
	unsigned long	ret;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetMixingMovieBGColor( &ret );
	}
#endif
	return static_cast<tjs_uint>(ret);
}



tjs_real tTJSNI_VideoOverlay::GetContrastRangeMin()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrastRangeMin( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrastRangeMax()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrastRangeMax( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrastDefaultValue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrastDefaultValue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrastStepSize()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrastStepSize( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrast()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrast( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetContrast( tjs_real v )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetContrast( static_cast<float>(v) );
	}
#endif
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessRangeMin()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightnessRangeMin( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessRangeMax()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightnessRangeMax( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessDefaultValue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightnessDefaultValue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessStepSize()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightnessStepSize( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightness()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightness( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetBrightness( tjs_real v )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetBrightness( static_cast<float>(v) );
	}
#endif
}

tjs_real tTJSNI_VideoOverlay::GetHueRangeMin()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHueRangeMin( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHueRangeMax()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHueRangeMax( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHueDefaultValue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHueDefaultValue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHueStepSize()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHueStepSize( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetHue( tjs_real v )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetHue( static_cast<float>(v) );
	}
#endif
}

tjs_real tTJSNI_VideoOverlay::GetSaturationRangeMin()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturationRangeMin( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturationRangeMax()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturationRangeMax( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturationDefaultValue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturationDefaultValue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturationStepSize()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturationStepSize( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturation()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturation( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetSaturation( tjs_real v )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetSaturation( static_cast<float>(v) );
	}
#endif
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_VideoOverlay::GetOriginalWidth()
{
	// retrieve original (coded in the video stream) width size
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(!VideoOverlay) return 0;
#endif

	long	width, height;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	VideoOverlay->GetVideoSize( &width, &height );
#else
	width = 0;
#endif

	return (tjs_int)width;
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_VideoOverlay::GetOriginalHeight()
{
	// retrieve original (coded in the video stream) height size

	long	width, height;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	VideoOverlay->GetVideoSize( &width, &height );
#else
	height = 0;
#endif

	return (tjs_int)height;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::ClearWndProcMessages()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// clear WndProc's message queue
	MSG msg;
	while(PeekMessage(&msg, EventQueue.GetOwner(), WM_GRAPHNOTIFY, WM_GRAPHNOTIFY+2, PM_REMOVE))
	{
		if(VideoOverlay)
		{
			long evcode;
			LONG_PTR p1, p2;
			bool got;
			VideoOverlay->GetEvent(&evcode, &p1, &p2, &got); // dummy call
			if( got )
				VideoOverlay->FreeEventParams( evcode, p1, p2 );
		}
	}
#endif
}
//---------------------------------------------------------------------------



//---------------------------------------------------------------------------
// tTJSNC_VideoOverlay::CreateNativeInstance : returns proper instance object
//---------------------------------------------------------------------------
tTJSNativeInstance *tTJSNC_VideoOverlay::CreateNativeInstance()
{
	return new tTJSNI_VideoOverlay();
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPCreateNativeClass_VideoOverlay
//---------------------------------------------------------------------------
tTJSNativeClass * TVPCreateNativeClass_VideoOverlay()
{
	return new tTJSNC_VideoOverlay();
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// Emscripten HTML5 <video> overlay implementation
//---------------------------------------------------------------------------
#ifdef __EMSCRIPTEN__
// callbacks invoked from the HTML5 <video> event listeners (JS -> C++)
static void TVP_EmscVideo_OnEnded(double self)
{
	auto *o = reinterpret_cast<tTJSNI_VideoOverlay*>(static_cast<intptr_t>(self));
	if(o) o->EmscHandleEnded();
}
static void TVP_EmscVideo_OnError(double self)
{
	auto *o = reinterpret_cast<tTJSNI_VideoOverlay*>(static_cast<intptr_t>(self));
	if(o) o->EmscHandleError();
}
static void TVP_EmscVideo_OnCanPlay(double self)
{
	auto *o = reinterpret_cast<tTJSNI_VideoOverlay*>(static_cast<intptr_t>(self));
	if(o) o->EmscHandleCanPlay();
}
// cache-fetch callbacks (server-side transcode cache for unsupported formats)
static void TVP_EmscVideo_OnFetched(double self, std::string url)
{
	auto *o = reinterpret_cast<tTJSNI_VideoOverlay*>(static_cast<intptr_t>(self));
	if(o) o->EmscHandleFetched(url);
}
static void TVP_EmscVideo_OnFetchFail(double self)
{
	auto *o = reinterpret_cast<tTJSNI_VideoOverlay*>(static_cast<intptr_t>(self));
	if(o) o->EmscHandleFetchFail();
}

EMSCRIPTEN_BINDINGS(tvp_video_overlay_emsc)
{
	emscripten::function("tvpVideoCbEnded", &TVP_EmscVideo_OnEnded);
	emscripten::function("tvpVideoCbError", &TVP_EmscVideo_OnError);
	emscripten::function("tvpVideoCbCanPlay", &TVP_EmscVideo_OnCanPlay);
	emscripten::function("tvpVideoCbFetched", &TVP_EmscVideo_OnFetched);
	emscripten::function("tvpVideoCbFetchFail", &TVP_EmscVideo_OnFetchFail);
}

// Create the <video> element for an already-resolved blob URL and wire up
// listeners / geometry. Used both for archive-backed (supported formats) and
// cache-backed (transcoded mp4) playback.
void tTJSNI_VideoOverlay::EmscSetupVideoElement(const std::string &blobUrl,
	const std::string &mime)
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st) return;
	st->blobUrl = blobUrl;

	emscripten::val document = emscripten::val::global("document");
	emscripten::val video = document.call<emscripten::val>("createElement", std::string("video"));
	st->video = video;

	video.set("src", blobUrl);
	video.set("preload", std::string("auto"));
	video.set("playsInline", true);
	video.set("loop", false);
	video.set("controls", false);
	video.set("disablePictureInPicture", true);
	emscripten::val style = video["style"];
	style.set("position", std::string("absolute"));
	style.set("objectFit", std::string("fill"));
	style.set("backgroundColor", std::string("black"));
	style.set("zIndex", std::string("60"));
	style.set("display", Visible ? std::string("block") : std::string("none"));
	style.set("pointerEvents", std::string("none"));
	style.set("margin", std::string("0"));
	style.set("border", std::string("0"));
	video.call<void>("setAttribute", std::string("data-tvp-video"), std::string("1"));

	video.set("_tvpPtr", (double)reinterpret_cast<intptr_t>(this));

	emscripten::val helpers = emscripten::val::global("window")["__tvpVideo"];
	if(TVP_EmscValOk(helpers))
	{
		st->onEnded = helpers["makeHandler"](std::string("tvpVideoCbEnded"));
		st->onError = helpers["makeHandler"](std::string("tvpVideoCbError"));
		st->onCanPlay = helpers["makeHandler"](std::string("tvpVideoCbCanPlay"));
		if(TVP_EmscValOk(st->onEnded))
			video.call<void>("addEventListener", std::string("ended"), st->onEnded);
		if(TVP_EmscValOk(st->onError))
			video.call<void>("addEventListener", std::string("error"), st->onError);
		if(TVP_EmscValOk(st->onCanPlay))
			video.call<void>("addEventListener", std::string("canplay"), st->onCanPlay);
	}

	document["body"].call<void>("appendChild", video);

	EmscSetVolume(EmscVolume);
	EmscUpdateRect();
}

void tTJSNI_VideoOverlay::EmscOpen(const ttstr &_name)
{
	EmscClose();

	if(!Window) TVPThrowExceptionMessage(TVPWindowAlreadyMissing);

	ttstr name(_name);
	const tjs_char *param_pos = TJS_strchr(name.c_str(), TJS_W('?'));
	if(param_pos != NULL)
		name = ttstr(name, (int)(param_pos - name.c_str()));

	ttstr ext = TVPExtractStorageExt(name).c_str();
	ext.ToLowerCase();
	std::string extAscii;
	for(tjs_uint i = 0; i < ext.length(); i++)
	{
		tjs_char c = ext[i];
		if(c < 128) extAscii += (char)c;
	}

	TVP_EmscVideoEnsureHelpers();
	tVPVideoEmscState *st = new tVPVideoEmscState();
	EmscVideoState = st;

	if(TVP_EmscIsUnsupportedExt(extAscii))
	{
		// Browser cannot decode this format -> request the server-side
		// transcode cache (video_cache/<game>.d/<path>.mp4). Playback starts
		// asynchronously in EmscHandleFetched once the cached mp4 arrives.
		emscripten::val gp = emscripten::val::global("window")["__tvpGameDataPath"];
		std::string videoPathUtf8;
		TVPUtf16ToUtf8(videoPathUtf8, name.AsStdString());
		bool safe = videoPathUtf8.find("..") == std::string::npos;
		if(TVP_EmscValOk(gp) && safe)
		{
			std::string gamePath = gp.as<std::string>();
			std::string rel = TVP_EmscCacheRelPath(gamePath, videoPathUtf8);
			SetStatus(tTVPVideoOverlayStatus::Stop);
			emscripten::val helpers = emscripten::val::global("window")["__tvpVideo"];
			intptr_t selfp = reinterpret_cast<intptr_t>(this);
			if(TVP_EmscValOk(helpers))
				helpers.call<void>("fetchCachedVideo", rel, (double)selfp);
			else
				EmscHandleFetchFail();
			return;
		}
		// No game path / unsafe path -> cannot use the cache; skip gracefully.
		st->failed = true;
		SetStatus(tTVPVideoOverlayStatus::Stop);
		return;
	}

	// Browser-supported format -> read bytes straight from the archive.
	std::string mime = TVP_EmscMimeForExt(extAscii);
	tTJSBinaryStream *stream = NULL;
	tjs_uint8 *buf = NULL;
	tjs_uint size = 0;
	emscripten::val urlVal = emscripten::val::undefined();
	try
	{
		stream = TVPCreateStream(name);
		size = (tjs_uint)stream->GetSize();
		buf = new tjs_uint8[size];
		stream->ReadBuffer(buf, size);
		delete stream; stream = NULL;

		emscripten::val u8 = emscripten::val::global("Uint8Array").new_(
			emscripten::typed_memory_view(size, buf));
		emscripten::val arr = emscripten::val::array();
		arr.set(0, u8);
		emscripten::val opts = emscripten::val::object();
		opts.set("type", mime);
		emscripten::val blob = emscripten::val::global("Blob").new_(arr, opts);
		urlVal = emscripten::val::global("URL").call<emscripten::val>("createObjectURL", blob);
	}
	catch(...)
	{
		if(buf) delete[] buf;
		if(stream) delete stream;
		EmscClose();
		throw;
	}
	delete[] buf; buf = NULL;

	EmscSetupVideoElement(urlVal.as<std::string>(), mime);
	ClearWndProcMessages();
	SetStatus(tTVPVideoOverlayStatus::Stop);
}

void tTJSNI_VideoOverlay::EmscClose()
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st) return;
	EmscVideoState = nullptr;
	emscripten::val video = st->video;
	if(TVP_EmscValOk(video))
	{
		try
		{
			if(TVP_EmscValOk(st->onEnded))
				video.call<void>("removeEventListener", std::string("ended"), st->onEnded);
			if(TVP_EmscValOk(st->onError))
				video.call<void>("removeEventListener", std::string("error"), st->onError);
			if(TVP_EmscValOk(st->onCanPlay))
				video.call<void>("removeEventListener", std::string("canplay"), st->onCanPlay);
			video.call<void>("pause");
			video.set("src", emscripten::val::null());
			video.call<void>("removeAttribute", std::string("src"));
			video.call<void>("load");
			emscripten::val parent = video["parentNode"];
			if(TVP_EmscValOk(parent))
				parent.call<void>("removeChild", video);
		}
		catch(...) {}
	}
	if(!st->blobUrl.empty())
	{
		try { emscripten::val::global("URL").call<void>("revokeObjectURL", st->blobUrl); }
		catch(...) {}
	}
	delete st;
}

void tTJSNI_VideoOverlay::EmscPlay()
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st || st->failed)
	{
		SetStatusAsync(tTVPVideoOverlayStatus::Stop);
		return;
	}
	st->wantPlay = true;
	SetStatus(tTVPVideoOverlayStatus::Play);
	if(!TVP_EmscValOk(st->video)) return; // waiting for cache fetch
	emscripten::val helpers = emscripten::val::global("window")["__tvpVideo"];
	if(TVP_EmscValOk(helpers))
		helpers.call<void>("play", st->video);
}

void tTJSNI_VideoOverlay::EmscStop()
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(st)
	{
		st->wantPlay = false;
		if(TVP_EmscValOk(st->video))
		{
			emscripten::val helpers = emscripten::val::global("window")["__tvpVideo"];
			if(TVP_EmscValOk(helpers))
			{
				helpers.call<void>("pause", st->video);
				helpers.call<void>("seek0", st->video);
			}
		}
	}
	SetStatus(tTVPVideoOverlayStatus::Stop);
}

void tTJSNI_VideoOverlay::EmscPause()
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(st)
	{
		st->wantPlay = false;
		if(TVP_EmscValOk(st->video))
		{
			emscripten::val helpers = emscripten::val::global("window")["__tvpVideo"];
			if(TVP_EmscValOk(helpers))
				helpers.call<void>("pause", st->video);
		}
	}
	SetStatus(tTVPVideoOverlayStatus::Pause);
}

void tTJSNI_VideoOverlay::EmscSetVisible(bool b)
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st) return;
	st->video["style"].set("display", b ? std::string("block") : std::string("none"));
}

void tTJSNI_VideoOverlay::EmscUpdateRect()
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st) return;
	tjs_int ww = 0, wh = 0;
	if(Window) { ww = Window->GetWidth(); wh = Window->GetHeight(); }
	st->video.set("_tvpW", (double)ww);
	st->video.set("_tvpH", (double)wh);
	st->video.set("_tvpLx", (double)Rect.left);
	st->video.set("_tvpLy", (double)Rect.top);
	st->video.set("_tvpLw", (double)Rect.get_width());
	st->video.set("_tvpLh", (double)Rect.get_height());
	emscripten::val helpers = emscripten::val::global("window")["__tvpVideo"];
	if(TVP_EmscValOk(helpers))
		helpers.call<void>("reposition", st->video);
}

void tTJSNI_VideoOverlay::EmscSetVolume(tjs_int v)
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st) return;
	double vol;
	if(v >= 0) vol = v / 100000.0;
	else vol = (v + 10000.0) / 10000.0;
	if(vol < 0.0) vol = 0.0;
	if(vol > 1.0) vol = 1.0;
	try
	{
		st->video.set("volume", vol);
		st->video.set("muted", vol <= 0.0);
	}
	catch(...) {}
}

void tTJSNI_VideoOverlay::EmscHandleEnded()
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st) return;
	if(Loop)
	{
		emscripten::val helpers = emscripten::val::global("window")["__tvpVideo"];
		if(TVP_EmscValOk(helpers))
		{
			helpers.call<void>("seek0", st->video);
			helpers.call<void>("play", st->video);
		}
		FirePeriodEvent(perLoop);
	}
	else
	{
		st->wantPlay = false;
		SetStatusAsync(tTVPVideoOverlayStatus::Stop);
	}
}

void tTJSNI_VideoOverlay::EmscHandleError()
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st) return;
	st->failed = true;
	st->wantPlay = false;
	TVPAddLog(ttstr("VideoOverlay: HTML5 video failed to load (unsupported codec or decode error); skipping."));
	SetStatusAsync(tTVPVideoOverlayStatus::Stop);
}

void tTJSNI_VideoOverlay::EmscHandleCanPlay()
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st) return;
	st->ready = true;
	if(st->wantPlay)
	{
		emscripten::val helpers = emscripten::val::global("window")["__tvpVideo"];
		if(TVP_EmscValOk(helpers))
			helpers.call<void>("play", st->video);
	}
}

void tTJSNI_VideoOverlay::EmscHandleFetched(std::string url)
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st)
	{
		// overlay closed before the cache fetch resolved; release the blob.
		try { emscripten::val::global("URL").call<void>("revokeObjectURL", url); }
		catch(...) {}
		return;
	}
	EmscSetupVideoElement(url, std::string("video/mp4"));
	if(st->wantPlay)
	{
		emscripten::val helpers = emscripten::val::global("window")["__tvpVideo"];
		if(TVP_EmscValOk(helpers))
			helpers.call<void>("play", st->video);
	}
}

void tTJSNI_VideoOverlay::EmscHandleFetchFail()
{
	tVPVideoEmscState *st = (tVPVideoEmscState*)EmscVideoState;
	if(!st) return;
	st->failed = true;
	if(st->wantPlay)
		SetStatusAsync(tTVPVideoOverlayStatus::Stop);
}

#endif // __EMSCRIPTEN__

