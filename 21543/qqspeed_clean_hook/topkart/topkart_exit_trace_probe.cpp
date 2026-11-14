#include "pch.h"

#include "topkart/topkart_exit_trace_probe.h"

#include "logging/hook_log.h"
#include "network/code_patch.h"

#include <intrin.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace
{
constexpr DWORD kTopKartWaitMs = 120000;
constexpr DWORD kTopKartPollMs = 250;
constexpr std::uintptr_t kTopKartKnownImageSize = 0x3101000;
constexpr std::size_t kImportCallSize = 6;

constexpr std::uintptr_t kPostMessageAIatRva = 0x1EDE700;
constexpr std::uintptr_t kSendMessageAIatRva = 0x1EDE6F8;
constexpr std::uintptr_t kPostMessageWIatRva = 0x1EDE7D0;
constexpr std::uintptr_t kSendMessageWIatRva = 0x1EDE7A0;
constexpr std::uintptr_t kPostThreadMessageAIatRva = 0x1EDE7F0;
constexpr std::uintptr_t kPostQuitMessageIatRva = 0x1EDE7A8;

constexpr std::uintptr_t kAppEventDispatchRva = 0x0C5FD0;
constexpr std::uintptr_t kAppEventDispatchCallRvas[] = {
	0x180AF0,
	0x1C73FA,
	0x1C8267,
	0x1E262E,
	0x1E2639,
	0x1E2682,
	0x1E459C,
	0x1E5075,
	0x21530E,
	0x215681,
	0x27961A,
	0x35A82E,
	0x35CB34,
	0x502CDD,
	0x523388,
	0x52A0E5,
	0x52A125,
	0x52AB7F,
	0x5686A1,
	0x56E944,
	0x56ED40,
	0x56EDBE,
	0x7129D6,
	0x71A27F,
	0x7486C2,
	0x76676A,
	0x776C6B,
	0x776E07,
	0x776E5F,
	0x7771D8,
	0x777823,
	0x777ABF,
	0x842CD2,
	0x842CF1,
	0x844583,
	0x849915,
	0x84AA8A,
	0x84AD14,
	0x84B54C,
	0x84BAC1,
	0x84BBDC,
	0x84BEB7,
	0x85167F,
	0x85C631,
	0x86991E,
	0x869BD3,
	0x869CB9,
	0x869FB7,
	0x869FE2,
	0x86AA08,
	0x86C789,
	0x86D31F,
	0x86D3C9,
	0x86D4A5,
	0x86D670,
	0x86E4DC,
	0x86F4C1,
	0x86F5A3,
	0x86F73D,
	0x870EFE,
	0x8733CC,
	0x87367B,
	0x875941,
	0x875B14,
	0x875D71,
	0x875FE5,
	0x8760A1,
	0x8768FB,
	0x876B83,
	0x877598,
	0x8777BD,
	0x877A2D,
	0x877FF2,
	0x879218,
	0x87990A,
	0x87A0C9,
	0x87A375,
	0x87A489,
	0x87A702,
	0x87AA9C,
	0x87AB96,
	0x87B28D,
	0x87B62F,
	0x87BC6F,
	0x87BE10,
	0x87D5F8,
	0x87D80A,
	0x87D8EC,
	0x87DA21,
	0x87E446,
	0x87E664,
	0x87E6F7,
	0x87E9D4,
	0x87EAF4,
	0x87EB53,
	0x87EBAD,
	0x87ECD8,
	0x87EDD4,
	0x880C9E,
	0x880D8B,
	0x880F5F,
	0x917E60,
	0xAB586F,
	0xAB5EDC,
	0xB0F876,
	0xB10275,
	0xB103F7,
	0xB1192C,
	0xB11BC5,
	0xB11C22,
	0xB11C8C,
	0xB11FEF,
	0xB13375,
	0xB13407,
	0xB140ED,
	0xB43498,
	0xB5A656,
	0xB5F1A9,
	0xB5F590,
	0xB5F5AC,
	0xB65A25,
	0xB6A949,
	0xB6B20C,
	0xB71C75,
	0xBAFC4B,
	0xBC5937,
	0xC76731,
	0xC767E1,
	0xC84676,
	0xC99DD8,
	0xC9A1A6,
	0xC9A361,
	0xCB3C01,
	0xCB3C75,
	0xCB3CE9,
	0xCB3D5D,
	0xCB3DD1,
	0xCB3E4E,
	0xCB3EC4,
	0xCB3F38,
	0xCB3FAC,
	0xCB4020,
	0xCB4094,
	0xCB418A,
	0xCB50F5,
	0xCB5500,
	0xCCB806,
	0xDE1418,
	0xDE14A8,
	0xDE14E2,
	0xF065E7,
	0xF06834,
	0xF06A22,
	0xF06B67,
	0xF0767A,
	0xF097EB,
	0xF09992,
	0xF09A5E,
	0xF09E7F,
	0xF0A2E9,
	0xF0AC6B,
	0xF0DEB9,
	0xFE29E4,
	0xFE3640,
	0xFF615F,
	0x1004D57,
	0x1031AE9,
	0x1032AE3,
	0x1032E78,
	0x1032F27,
	0x1032F62,
	0x1032F9D,
	0x1032FD8,
	0x1033013,
	0x1033140,
	0x103317B,
	0x1033257,
	0x1033292,
	0x10332CD,
	0x10333A9,
	0x1033582,
	0x1033AC5,
	0x1033BA6,
	0x1063C61,
	0x10DABC1,
	0x1181135,
	0x118D9FB,
	0x1199C9B,
	0x12077B4,
	0x1208183,
	0x120B947,
	0x120E0D3,
	0x1387CD6,
	0x1398021,
	0x139882C,
	0x13D9D66,
	0x13D9F75,
	0x13DA1AC,
	0x13DA202,
	0x14AB79A,
	0x15077B8,
	0x1507848,
	0x150AC55,
	0x150B00C,
	0x150B170,
	0x150BE19,
	0x150BF85,
	0x150CDE4,
	0x150D236,
	0x150D658,
	0x150E1B7,
	0x150E9B8,
	0x1587320,
};

constexpr std::uintptr_t kUiLuaCallbackRva = 0x153F6BC;
constexpr std::uintptr_t kUiLuaCallbackCallRvas[] = {
	0x1535B93,
	0x153604D,
	0x1536519,
	0x153F38B,
	0x153F5F2,
};

constexpr std::uintptr_t kLuaDoStringRva = 0x15C5940;
constexpr std::uintptr_t kLuaDoStringCallRvas[] = {
	0x216CF0,
	0x219E04,
	0x21A139,
	0xD0E7B5,
	0x1478BD8,
	0x1495566,
	0x1523B40,
	0x152FDDB,
	0x15300B3,
	0x1530552,
	0x153E72C,
	0x153F7D0,
	0x1542F36,
	0x15474E6,
	0x15476DB,
	0x1547935,
	0x1547BDF,
	0x1547CDB,
	0x1558C89,
	0x15655A7,
	0x157F249,
	0x157F8A6,
	0x157FA4B,
	0x1587AA0,
	0x1587D31,
	0x2DE0178,
};

constexpr std::uintptr_t kPostMessageACallRvas[] = {
	0x55B049,
	0x55B2AA,
	0x55B3E1,
	0x55B4BA,
	0x55B909,
	0x55C0DD,
	0x55C21D,
	0x55E98F,
	0x55EA0C,
	0x55F9A5,
	0x56F860,
	0x5787A6,
	0xD2E3B3,
	0xED148B,
	0xF4B380,
	0x162E66F,
	0x162E6D1,
	0x162E719,
	0x162E761,
	0x162E7A9,
	0x162E7F4,
	0x162E83F,
	0x162E88A,
	0x162E8D5,
};

constexpr std::uintptr_t kPostMessageWCallRvas[] = {
	0x73E0DA,
};

constexpr std::uintptr_t kSendMessageACallRvas[] = {
	0x5559A4,
	0x73CB75,
	0x73CB9E,
	0x73CBD5,
	0x73CC11,
	0x73CC36,
	0x73CC57,
	0x73E935,
	0x73E956,
	0x820026,
	0xC8152A,
	0xC82B93,
	0xC82E29,
	0xC82ECD,
	0xC84E64,
	0xECADDB,
	0xECADF2,
	0xF4AA76,
	0xF4AA8A,
	0xF4AB77,
	0xF4AB8E,
	0x15BF36C,
	0x15BF4CC,
	0x15BF522,
	0x15BF575,
	0x15BFB1D,
	0x15C19FE,
	0x15C1D67,
	0x15C1D9D,
	0x15C1DD3,
	0x15C1E09,
	0x15C1E3F,
	0x15C1EE2,
	0x15C3342,
	0x162E410,
	0x162E480,
	0x162E494,
	0x162E550,
	0x16302E5,
	0x1630309,
};

constexpr std::uintptr_t kSendMessageWCallRvas[] = {
	0x73CBF0,
};

constexpr std::uintptr_t kPostThreadMessageACallRvas[] = {
	0x56E3E7,
	0x56E421,
	0x75B34F,
	0x75B37C,
	0x75B3C7,
	0xECA867,
	0x15C39AE,
};

constexpr std::uintptr_t kPostQuitMessageCallRvas[] = {
	0x75CC13,
	0xC71C56,
	0xF4B1C0,
};

using AppEventDispatchFn = double(__fastcall*)(void* scriptState, const char* eventName, const char* args);
using PostMessageAFn = BOOL(WINAPI*)(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
using PostMessageWFn = BOOL(WINAPI*)(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
using SendMessageAFn = LRESULT(WINAPI*)(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
using SendMessageWFn = LRESULT(WINAPI*)(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
using PostThreadMessageAFn = BOOL(WINAPI*)(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam);
using PostQuitMessageFn = void(WINAPI*)(int exitCode);
using UiLuaCallbackFn = __int64(__fastcall*)(void* widget, const char* funcName, const char* args);
using LuaDoStringFn = __int64(__fastcall*)(void* scriptHost, const char* code, const char* name);

struct ProbeThreadContext
{
	qqspeed::config::HookConfig config;
};

std::uintptr_t g_topKartBase = 0;
volatile LONG g_installState = 0;
std::size_t g_patchedCalls = 0;
AppEventDispatchFn g_originalAppEventDispatch = nullptr;
UiLuaCallbackFn g_originalUiLuaCallback = nullptr;
LuaDoStringFn g_originalLuaDoString = nullptr;

std::uintptr_t TopKartCallRvaFromReturnAddress(void* returnAddress)
{
	const std::uintptr_t ret = reinterpret_cast<std::uintptr_t>(returnAddress);
	if (!g_topKartBase || ret < g_topKartBase) {
		return 0;
	}
	const std::uintptr_t retRva = ret - g_topKartBase;
	return retRva >= 5 ? retRva - 5 : 0;
}

bool IsTopKartAddress(std::uintptr_t address)
{
	return g_topKartBase &&
		address >= g_topKartBase &&
		address < g_topKartBase + kTopKartKnownImageSize;
}

const char* CloseMessageName(UINT msg, WPARAM wParam)
{
	if (msg == WM_CLOSE) {
		return "WM_CLOSE";
	}
	if (msg == WM_QUIT) {
		return "WM_QUIT";
	}
	if (msg == WM_SYSCOMMAND && (wParam & 0xFFF0u) == SC_CLOSE) {
		return "WM_SYSCOMMAND/SC_CLOSE";
	}
	return nullptr;
}

const char* CallSiteName(std::uintptr_t callRva)
{
	switch (callRva) {
	case 0x56E944:
		return "wndproc_dispatch_OnClose";
	case 0xF4B380:
		return "ExitApp_PostMessage_WM_CLOSE";
	case 0xF4B1C0:
		return "ConfirmExit_PostQuitMessage";
	case 0x153F38B:
		return "widget_event_lua_callback";
	case 0x153F5F2:
		return "widget_event_lua_callback_fallback";
	case 0x153F7D0:
		return "ui_lua_callback_DoString";
	default:
		return "unknown";
	}
}

void CopyWindowTextSafe(HWND hWnd, char* className, std::size_t classNameSize, char* title, std::size_t titleSize)
{
	if (className && classNameSize) {
		className[0] = 0;
	}
	if (title && titleSize) {
		title[0] = 0;
	}
	if (!hWnd) {
		return;
	}

	__try
	{
		if (className && classNameSize) {
			GetClassNameA(hWnd, className, static_cast<int>(classNameSize));
			className[classNameSize - 1] = 0;
		}
		if (title && titleSize) {
			GetWindowTextA(hWnd, title, static_cast<int>(titleSize));
			title[titleSize - 1] = 0;
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		if (className && classNameSize) {
			strcpy_s(className, classNameSize, "<window-read-exception>");
		}
		if (title && titleSize) {
			title[0] = 0;
		}
	}
}

void AppendText(char* out, std::size_t outSize, std::size_t& used, const char* text)
{
	if (!out || outSize == 0 || used >= outSize || !text) {
		return;
	}
	const int written = std::snprintf(out + used, outSize - used, "%s", text);
	if (written > 0) {
		const std::size_t delta = static_cast<std::size_t>(written);
		used = (delta < outSize - used) ? used + delta : outSize - 1;
	}
}

void AppendFrameText(char* out, std::size_t outSize, std::size_t& used, void* frame)
{
	if (!out || outSize == 0 || used >= outSize) {
		return;
	}

	const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(frame);
	char frameText[64] = {};
	if (IsTopKartAddress(address)) {
		std::snprintf(
			frameText,
			sizeof(frameText),
			"TK+0x%llX",
			static_cast<unsigned long long>(address - g_topKartBase));
	}
	else {
		std::snprintf(frameText, sizeof(frameText), "%p", frame);
	}
	AppendText(out, outSize, used, frameText);
}

void CaptureStackText(char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}
	out[0] = 0;

	void* frames[16] = {};
	const USHORT count = CaptureStackBackTrace(1, static_cast<ULONG>(_countof(frames)), frames, nullptr);
	std::size_t used = 0;
	for (USHORT i = 0; i < count; ++i) {
		if (i != 0) {
			AppendText(out, outSize, used, ">");
		}
		AppendFrameText(out, outSize, used, frames[i]);
	}
}

void LogWindowCloseMessage(
	const char* stage,
	std::uintptr_t callRva,
	HWND hWnd,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam,
	long long resultValue,
	bool hasResult)
{
	const char* messageName = CloseMessageName(msg, wParam);
	if (!messageName) {
		return;
	}

	const DWORD senderTid = GetCurrentThreadId();
	DWORD targetPid = 0;
	const DWORD targetTid = hWnd ? GetWindowThreadProcessId(hWnd, &targetPid) : 0;
	char className[128] = {};
	char title[128] = {};
	char stack[1024] = {};
	CopyWindowTextSafe(hWnd, className, sizeof(className), title, sizeof(title));
	CaptureStackText(stack, sizeof(stack));

	qqspeed::logging::Logf(
		"topkart_exit_trace stage=%s callRva=0x%llX site=%s senderTid=%lu targetTid=%lu targetPid=%lu "
		"crossThread=%d hwnd=%p msg=0x%X msgName=%s wParam=0x%llX lParam=0x%llX resultKnown=%d result=%lld "
		"class=%s title=%s stack=%s",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		senderTid,
		targetTid,
		targetPid,
		(targetTid != 0 && senderTid != targetTid) ? 1 : 0,
		hWnd,
		msg,
		messageName,
		static_cast<unsigned long long>(wParam),
		static_cast<unsigned long long>(lParam),
		hasResult ? 1 : 0,
		resultValue,
		className[0] ? className : "<empty>",
		title[0] ? title : "<empty>",
		stack[0] ? stack : "<empty>");
}

void LogThreadMessage(
	const char* stage,
	std::uintptr_t callRva,
	DWORD targetThreadId,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam,
	long long resultValue,
	bool hasResult)
{
	const char* messageName = CloseMessageName(msg, wParam);
	if (!messageName) {
		return;
	}

	char stack[1024] = {};
	CaptureStackText(stack, sizeof(stack));

	qqspeed::logging::Logf(
		"topkart_exit_trace stage=%s callRva=0x%llX site=%s senderTid=%lu targetTid=%lu crossThread=%d "
		"msg=0x%X msgName=%s wParam=0x%llX lParam=0x%llX resultKnown=%d result=%lld stack=%s",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		GetCurrentThreadId(),
		targetThreadId,
		targetThreadId != 0 && GetCurrentThreadId() != targetThreadId ? 1 : 0,
		msg,
		messageName,
		static_cast<unsigned long long>(wParam),
		static_cast<unsigned long long>(lParam),
		hasResult ? 1 : 0,
		resultValue,
		stack[0] ? stack : "<empty>");
}

void LogPostQuitMessage(std::uintptr_t callRva, int exitCode)
{
	char stack[1024] = {};
	CaptureStackText(stack, sizeof(stack));

	qqspeed::logging::Logf(
		"topkart_exit_trace stage=PostQuitMessage callRva=0x%llX site=%s senderTid=%lu exitCode=%d stack=%s",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		GetCurrentThreadId(),
		exitCode,
		stack[0] ? stack : "<empty>");
}

void CopyCStringSafeBounded(const char* src, char* out, std::size_t outSize, std::size_t maxTextBytes)
{
	if (!out || outSize == 0) {
		return;
	}
	out[0] = 0;
	if (!src) {
		strcpy_s(out, outSize, "<null>");
		return;
	}

	const std::size_t limit = (outSize - 1) < maxTextBytes ? (outSize - 1) : maxTextBytes;
	__try
	{
		for (std::size_t i = 0; i < limit; ++i) {
			const char ch = src[i];
			out[i] = ch;
			if (ch == 0) {
				return;
			}
		}
		out[limit] = 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		std::snprintf(out, outSize, "<unreadable:%p>", src);
		return;
	}
}

void CopyCStringSafe(const char* src, char* out, std::size_t outSize)
{
	CopyCStringSafeBounded(src, out, outSize, 128);
}

bool ContainsInterestingExitToken(const char* text)
{
	if (!text || !*text) {
		return false;
	}

	return std::strstr(text, "ConfirmExit") ||
		std::strstr(text, "OnClose") ||
		std::strstr(text, "ExitApp") ||
		std::strstr(text, "PostQuit") ||
		std::strstr(text, "GotoUIScreen") ||
		std::strstr(text, "AppEventHandle") ||
		std::strstr(text, "RaceOver") ||
		std::strstr(text, "ProcNotifyRaceOver") ||
		std::strstr(text, "HideGansterModeTips");
}

void LogAppEventDispatch(std::uintptr_t callRva, void* scriptState, const char* eventName, const char* args)
{
	char safeEvent[160] = {};
	char safeArgs[160] = {};
	CopyCStringSafe(eventName, safeEvent, sizeof(safeEvent));
	CopyCStringSafe(args, safeArgs, sizeof(safeArgs));
	if (!ContainsInterestingExitToken(safeEvent) && !ContainsInterestingExitToken(safeArgs)) {
		return;
	}

	char stack[1024] = {};
	CaptureStackText(stack, sizeof(stack));

	qqspeed::logging::Logf(
		"topkart_exit_trace stage=AppEventDispatch callRva=0x%llX site=%s senderTid=%lu scriptState=%p event=%s args=%s stack=%s",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		GetCurrentThreadId(),
		scriptState,
		safeEvent,
		safeArgs,
		stack[0] ? stack : "<empty>");
}

BOOL WINAPI HookedPostMessageA(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	const BOOL result = ::PostMessageA(hWnd, msg, wParam, lParam);
	LogWindowCloseMessage("PostMessageA", callRva, hWnd, msg, wParam, lParam, result, true);
	return result;
}

BOOL WINAPI HookedPostMessageW(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	const BOOL result = ::PostMessageW(hWnd, msg, wParam, lParam);
	LogWindowCloseMessage("PostMessageW", callRva, hWnd, msg, wParam, lParam, result, true);
	return result;
}

LRESULT WINAPI HookedSendMessageA(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	LogWindowCloseMessage("SendMessageA_before", callRva, hWnd, msg, wParam, lParam, 0, false);
	const LRESULT result = ::SendMessageA(hWnd, msg, wParam, lParam);
	LogWindowCloseMessage("SendMessageA_after", callRva, hWnd, msg, wParam, lParam, result, true);
	return result;
}

LRESULT WINAPI HookedSendMessageW(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	LogWindowCloseMessage("SendMessageW_before", callRva, hWnd, msg, wParam, lParam, 0, false);
	const LRESULT result = ::SendMessageW(hWnd, msg, wParam, lParam);
	LogWindowCloseMessage("SendMessageW_after", callRva, hWnd, msg, wParam, lParam, result, true);
	return result;
}

BOOL WINAPI HookedPostThreadMessageA(DWORD threadId, UINT msg, WPARAM wParam, LPARAM lParam)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	const BOOL result = ::PostThreadMessageA(threadId, msg, wParam, lParam);
	LogThreadMessage("PostThreadMessageA", callRva, threadId, msg, wParam, lParam, result, true);
	return result;
}

void WINAPI HookedPostQuitMessage(int exitCode)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	LogPostQuitMessage(callRva, exitCode);
	::PostQuitMessage(exitCode);
}

double __fastcall HookedAppEventDispatch(void* scriptState, const char* eventName, const char* args)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	LogAppEventDispatch(callRva, scriptState, eventName, args);
	AppEventDispatchFn original = g_originalAppEventDispatch;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<AppEventDispatchFn>(g_topKartBase + kAppEventDispatchRva);
	}
	return original ? original(scriptState, eventName, args) : 0.0;
}

__int64 __fastcall HookedUiLuaCallback(void* widget, const char* funcName, const char* args)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	char safeFunc[256] = {};
	char safeArgs[256] = {};
	CopyCStringSafeBounded(funcName, safeFunc, sizeof(safeFunc), 220);
	CopyCStringSafeBounded(args, safeArgs, sizeof(safeArgs), 220);
	const bool shouldLog = ContainsInterestingExitToken(safeFunc) || ContainsInterestingExitToken(safeArgs);

	if (shouldLog) {
		char stack[1024] = {};
		CaptureStackText(stack, sizeof(stack));
		qqspeed::logging::Logf(
			"topkart_exit_trace stage=UiLuaCallback_before callRva=0x%llX site=%s senderTid=%lu widget=%p func=%s args=%s stack=%s",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			GetCurrentThreadId(),
			widget,
			safeFunc,
			safeArgs,
			stack[0] ? stack : "<empty>");
	}

	UiLuaCallbackFn original = g_originalUiLuaCallback;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<UiLuaCallbackFn>(g_topKartBase + kUiLuaCallbackRva);
	}
	const __int64 result = original ? original(widget, funcName, args) : 0;

	if (shouldLog) {
		qqspeed::logging::Logf(
			"topkart_exit_trace stage=UiLuaCallback_after callRva=0x%llX site=%s senderTid=%lu widget=%p func=%s result=%lld",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			GetCurrentThreadId(),
			widget,
			safeFunc,
			static_cast<long long>(result));
	}

	return result;
}

__int64 __fastcall HookedLuaDoString(void* scriptHost, const char* code, const char* name)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	char safeCode[768] = {};
	char safeName[256] = {};
	CopyCStringSafeBounded(code, safeCode, sizeof(safeCode), 720);
	CopyCStringSafeBounded(name, safeName, sizeof(safeName), 220);
	const bool shouldLog = ContainsInterestingExitToken(safeCode) || ContainsInterestingExitToken(safeName);

	if (shouldLog) {
		char stack[1024] = {};
		CaptureStackText(stack, sizeof(stack));
		qqspeed::logging::Logf(
			"topkart_exit_trace stage=LuaDoString_before callRva=0x%llX site=%s senderTid=%lu scriptHost=%p name=%s code=%s stack=%s",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			GetCurrentThreadId(),
			scriptHost,
			safeName,
			safeCode,
			stack[0] ? stack : "<empty>");
	}

	LuaDoStringFn original = g_originalLuaDoString;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LuaDoStringFn>(g_topKartBase + kLuaDoStringRva);
	}
	const __int64 result = original ? original(scriptHost, code, name) : 0;

	if (shouldLog) {
		qqspeed::logging::Logf(
			"topkart_exit_trace stage=LuaDoString_after callRva=0x%llX site=%s senderTid=%lu scriptHost=%p name=%s result=%lld",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			GetCurrentThreadId(),
			scriptHost,
			safeName,
			static_cast<long long>(result));
	}

	return result;
}

bool BuildImportCallExpected(std::uintptr_t moduleBase, std::uintptr_t callRva, std::uintptr_t iatRva, unsigned char* out)
{
	if (!moduleBase || !out) {
		return false;
	}

	const std::uintptr_t call = moduleBase + callRva;
	const std::uintptr_t iat = moduleBase + iatRva;
	const std::int64_t delta =
		static_cast<std::int64_t>(iat) - static_cast<std::int64_t>(call + kImportCallSize);
	if (delta < INT32_MIN || delta > INT32_MAX) {
		return false;
	}

	out[0] = 0xFF;
	out[1] = 0x15;
	const std::int32_t rel = static_cast<std::int32_t>(delta);
	memcpy(out + 2, &rel, sizeof(rel));
	return true;
}

std::size_t PatchImportCallSite(
	std::uintptr_t topKartBase,
	std::uintptr_t callRva,
	std::uintptr_t iatRva,
	void* detour,
	const char* name)
{
	unsigned char expected[kImportCallSize] = {};
	if (!BuildImportCallExpected(topKartBase, callRva, iatRva, expected)) {
		qqspeed::logging::Logf(
			"topkart_exit_trace patch expected build failed name=%s callRva=0x%llX iatRva=0x%llX",
			name ? name : "unknown",
			static_cast<unsigned long long>(callRva),
			static_cast<unsigned long long>(iatRva));
		return 0;
	}

	return qqspeed::network::patch::PatchCallSitesWithRelay(
		topKartBase,
		&callRva,
		1,
		expected,
		sizeof(expected),
		detour,
		name);
}

template <std::size_t Count>
std::size_t PatchImportCallSites(
	std::uintptr_t topKartBase,
	const std::uintptr_t(&callRvas)[Count],
	std::uintptr_t iatRva,
	void* detour,
	const char* name)
{
	std::size_t patched = 0;
	for (std::uintptr_t callRva : callRvas) {
		patched += PatchImportCallSite(topKartBase, callRva, iatRva, detour, name);
	}
	return patched;
}

std::size_t PatchExitTraceCalls(std::uintptr_t topKartBase)
{
	std::size_t patched = 0;
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kAppEventDispatchCallRvas,
		_countof(kAppEventDispatchCallRvas),
		kAppEventDispatchRva,
		reinterpret_cast<void*>(&HookedAppEventDispatch),
		"TopKartExitTrace AppEventDispatch");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kUiLuaCallbackCallRvas,
		_countof(kUiLuaCallbackCallRvas),
		kUiLuaCallbackRva,
		reinterpret_cast<void*>(&HookedUiLuaCallback),
		"TopKartExitTrace UiLuaCallback");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kLuaDoStringCallRvas,
		_countof(kLuaDoStringCallRvas),
		kLuaDoStringRva,
		reinterpret_cast<void*>(&HookedLuaDoString),
		"TopKartExitTrace LuaDoString");
	patched += PatchImportCallSites(
		topKartBase,
		kPostMessageACallRvas,
		kPostMessageAIatRva,
		reinterpret_cast<void*>(&HookedPostMessageA),
		"TopKartExitTrace PostMessageA");
	patched += PatchImportCallSites(
		topKartBase,
		kPostMessageWCallRvas,
		kPostMessageWIatRva,
		reinterpret_cast<void*>(&HookedPostMessageW),
		"TopKartExitTrace PostMessageW");
	patched += PatchImportCallSites(
		topKartBase,
		kSendMessageACallRvas,
		kSendMessageAIatRva,
		reinterpret_cast<void*>(&HookedSendMessageA),
		"TopKartExitTrace SendMessageA");
	patched += PatchImportCallSites(
		topKartBase,
		kSendMessageWCallRvas,
		kSendMessageWIatRva,
		reinterpret_cast<void*>(&HookedSendMessageW),
		"TopKartExitTrace SendMessageW");
	patched += PatchImportCallSites(
		topKartBase,
		kPostThreadMessageACallRvas,
		kPostThreadMessageAIatRva,
		reinterpret_cast<void*>(&HookedPostThreadMessageA),
		"TopKartExitTrace PostThreadMessageA");
	patched += PatchImportCallSites(
		topKartBase,
		kPostQuitMessageCallRvas,
		kPostQuitMessageIatRva,
		reinterpret_cast<void*>(&HookedPostQuitMessage),
		"TopKartExitTrace PostQuitMessage");
	return patched;
}

DWORD WINAPI ExitTraceProbeThread(void* param)
{
	std::unique_ptr<ProbeThreadContext> context(static_cast<ProbeThreadContext*>(param));
	if (!context) {
		return 0;
	}

	DWORD waited = 0;
	while (waited <= kTopKartWaitMs) {
		HMODULE topKartModule = GetModuleHandleW(context->config.topKartModuleName);
		if (topKartModule) {
			qqspeed::topkart::InstallExitTraceProbe(topKartModule, context->config);
			return 0;
		}
		Sleep(kTopKartPollMs);
		waited += kTopKartPollMs;
	}

	qqspeed::logging::Logf("topkart_exit_trace probe skipped: Top-Kart.dll not loaded after %lu ms", kTopKartWaitMs);
	return 0;
}
}

namespace qqspeed::topkart
{
bool InstallExitTraceProbe(HMODULE topKartModule, const config::HookConfig& config)
{
	if (!config.features.enableTopKartExitTraceProbe) {
		qqspeed::logging::Logf("topkart_exit_trace probe disabled");
		return false;
	}
	if (!topKartModule) {
		return false;
	}
	if (InterlockedCompareExchange(&g_installState, 0, 0) == 2) {
		return g_patchedCalls > 0;
	}
	if (InterlockedCompareExchange(&g_installState, 1, 0) != 0) {
		return InterlockedCompareExchange(&g_installState, 0, 0) == 2 && g_patchedCalls > 0;
	}

	g_topKartBase = reinterpret_cast<std::uintptr_t>(topKartModule);
	g_originalAppEventDispatch = reinterpret_cast<AppEventDispatchFn>(g_topKartBase + kAppEventDispatchRva);
	g_originalUiLuaCallback = reinterpret_cast<UiLuaCallbackFn>(g_topKartBase + kUiLuaCallbackRva);
	g_originalLuaDoString = reinterpret_cast<LuaDoStringFn>(g_topKartBase + kLuaDoStringRva);
	g_patchedCalls = PatchExitTraceCalls(g_topKartBase);
	InterlockedExchange(&g_installState, 2);

	qqspeed::logging::Logf(
		"topkart_exit_trace probe installed=%d patchedCalls=%llu base=%p",
		g_patchedCalls > 0 ? 1 : 0,
		static_cast<unsigned long long>(g_patchedCalls),
		reinterpret_cast<void*>(g_topKartBase));
	return g_patchedCalls > 0;
}

void StartExitTraceProbeThread(const config::HookConfig& config)
{
	if (!config.features.enableTopKartExitTraceProbe) {
		qqspeed::logging::Logf("topkart_exit_trace probe disabled");
		return;
	}

	auto* context = new (std::nothrow) ProbeThreadContext();
	if (!context) {
		qqspeed::logging::Logf("topkart_exit_trace probe thread create skipped: alloc failed");
		return;
	}
	context->config = config;

	HANDLE thread = CreateThread(nullptr, 0, &ExitTraceProbeThread, context, 0, nullptr);
	if (!thread) {
		delete context;
		qqspeed::logging::Logf("topkart_exit_trace probe thread create failed error=%lu", GetLastError());
		return;
	}
	CloseHandle(thread);
}
}
