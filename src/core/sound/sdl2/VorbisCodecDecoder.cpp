// SPDX-License-Identifier: MIT
// Built-in Ogg Vorbis wave decoder for web and platforms without wuvorbis.

#include "tjsCommHead.h"
#include "StorageIntf.h"
#include "WaveIntf.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <limits>
#include <memory>
#include <vector>

#ifndef FAUDIOAPI
#define FAUDIOAPI
#endif
#ifndef FAudio_alloca
#define FAudio_alloca(size) std::malloc(size)
#endif
#ifndef FAudio_dealloca
#define FAudio_dealloca(ptr) std::free(ptr)
#endif
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_NO_FAST_SCALED_FLOAT
#include "stb_vorbis.h"

class tTVPWD_Vorbis : public tTVPWaveDecoder
{
	std::vector<unsigned char> Data;
	stb_vorbis *InputFile;
	tTVPWaveFormat Format;
	tjs_uint CurrentPos;

public:
	tTVPWD_Vorbis(std::vector<unsigned char> &&data, stb_vorbis *inputfile)
		: Data(std::move(data)), InputFile(inputfile), CurrentPos(0)
	{
		stb_vorbis_info info = stb_vorbis_get_info(InputFile);
		memset(&Format, 0, sizeof(Format));
		Format.SamplesPerSec = info.sample_rate;
		Format.Channels = info.channels;
		Format.BitsPerSample = 16;
		Format.BytesPerSample = 2;
		Format.TotalSamples = stb_vorbis_stream_length_in_samples(InputFile);
		Format.TotalTime = Format.SamplesPerSec
			? Format.TotalSamples * 1000 / Format.SamplesPerSec
			: 0;
		Format.SpeakerConfig = 0;
		Format.IsFloat = false;
		Format.Seekable = true;
	}

	~tTVPWD_Vorbis()
	{
		if (InputFile) stb_vorbis_close(InputFile);
	}

	void GetFormat(tTVPWaveFormat &format) override
	{
		format = Format;
	}

	bool Render(void *buf, tjs_uint bufsamplelen, tjs_uint &rendered) override
	{
		if (!InputFile || !buf || bufsamplelen == 0)
		{
			rendered = 0;
			return false;
		}

		rendered = 0;
		short *out = static_cast<short *>(buf);
		const int channels = static_cast<int>(Format.Channels);
		const int requested = static_cast<int>(bufsamplelen);

		while (rendered < bufsamplelen)
		{
			int got = stb_vorbis_get_samples_short_interleaved(
				InputFile,
				channels,
				out + rendered * channels,
				(requested - static_cast<int>(rendered)) * channels);
			if (got <= 0) break;
			rendered += static_cast<tjs_uint>(got);
		}

		CurrentPos += rendered;
		return rendered == bufsamplelen;
	}

	bool SetPosition(tjs_uint64 samplepos) override
	{
		if (!InputFile) return false;
		if (samplepos > static_cast<tjs_uint64>(std::numeric_limits<unsigned int>::max())) return false;
		if (!stb_vorbis_seek(InputFile, static_cast<unsigned int>(samplepos))) return false;
		CurrentPos = static_cast<tjs_uint>(samplepos);
		return true;
	}
};

class tTVPWDC_Vorbis : public tTVPWaveDecoderCreator
{
public:
	tTVPWaveDecoder *Create(const ttstr &storagename, const ttstr &extension) override
	{
		if (extension != TJS_W(".ogg")) return nullptr;

		try
		{
			std::unique_ptr<tTJSBinaryStream> stream(TVPCreateStream(storagename));
			if (!stream) return nullptr;

			tjs_uint64 size64 = stream->GetSize();
			if (size64 == 0 || size64 > static_cast<tjs_uint64>(std::numeric_limits<int>::max())) return nullptr;

			std::vector<unsigned char> data(static_cast<size_t>(size64));
			tjs_uint read = stream->Read(data.data(), static_cast<tjs_uint>(data.size()));
			if (read != static_cast<tjs_uint>(data.size())) return nullptr;

			int error = 0;
			stb_vorbis *inputfile = stb_vorbis_open_memory(
				data.data(),
				static_cast<int>(data.size()),
				&error,
				nullptr);
			if (!inputfile) return nullptr;

			return new tTVPWD_Vorbis(std::move(data), inputfile);
		}
		catch (...)
		{
			return nullptr;
		}
	}
};

static tTVPWDC_Vorbis VorbisDecoderCreator;

void TVPRegisterVorbisDecoderCreator()
{
	static bool registered = false;
	if (!registered)
	{
		TVPRegisterWaveDecoderCreator(&VorbisDecoderCreator);
		registered = true;
	}
}
