// 最小 side-module：仅导出 V2Link/V2Unlink，用于验证 side-module 加载链路。
#define KRKRSDL2_ENABLE_PLUGINS
#include "tp_stub.h"
#include "FuncStubs.h"

#ifdef _WIN32
#define DLL_EXPORT __declspec(dllexport)
#define STDCALL __stdcall
#else
typedef tjs_error HRESULT;
#define DLL_EXPORT
#define STDCALL
#endif

#if defined(__EMSCRIPTEN__)
#define KRKR_EXPORT_NAME(n) __attribute__((export_name(n)))
#else
#define KRKR_EXPORT_NAME(n)
#endif
extern "C" DLL_EXPORT HRESULT STDCALL KRKR_EXPORT_NAME("V2Link") V2Link(iTVPFunctionExporter *exporter)
{
	// 最小验证：仅确认 V2Link 符号可被 SDL_LoadFunction 找到并调用。
	// 不依赖 TVP 导出函数，规避 stub 生成机制。
	(void)exporter;
	return TJS_S_OK;
}

extern "C" DLL_EXPORT HRESULT STDCALL KRKR_EXPORT_NAME("V2Unlink") V2Unlink()
{
	return TJS_S_OK;
}
