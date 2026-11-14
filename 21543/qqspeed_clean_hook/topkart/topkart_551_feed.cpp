#include "pch.h"

#include "topkart/topkart_551_feed.h"

#include "logging/hook_log.h"
#include "network/code_patch.h"
#include "vmp_protection.h"

#include <intrin.h>
#include <cstdio>
#include <memory>
#include <new>
#include <vector>

namespace
{
constexpr int kMaxCompact551BodyLen = 0x400000;
constexpr std::uintptr_t kKartPhysHandlerEntryMarkerRva = 0x1679990;
constexpr std::uintptr_t kKartPhysHandlerEntryMarkerCallRvas[] = { 0x1200E9C };
constexpr std::uintptr_t kKartPhysSkipEnterGateRva = 0x114D970;
constexpr std::uintptr_t kKartPhysSkipEnterGateCallRvas[] = { 0x1201E56 };
constexpr std::uintptr_t kKartPhysEnterBranchRva = 0x120A270;
constexpr std::uintptr_t kKartPhysEnterBranchCallRvas[] = { 0x1201E5F };
constexpr std::uintptr_t kKartPhysShortcutGateRva = 0x272BD0;
constexpr std::uintptr_t kKartPhysShortcutGateCallRvas[] = { 0x1201E30 };
constexpr std::uintptr_t kKartPhysObjectFlagGateRva = 0x9ED020;
constexpr std::uintptr_t kKartPhysObjectFlagGateCallRvas[] = { 0x1201CA6 };
constexpr std::uintptr_t kNxPlayerEnterPhySceneImplRva = 0x9CC870;
constexpr std::uintptr_t kNxPlayerEnterPhySceneImplCallRvas[] = { 0x9A81C0, 0xA54E86 };
constexpr std::uintptr_t kLuaEventTriggerRva = 0x0C6040;
constexpr std::uintptr_t kLuaEventTriggerCallRvas[] = { 0x1202139 };
constexpr std::uintptr_t kAsyncPromiseEnterRva = 0x280DD0;
constexpr std::uintptr_t kKartPhysAsyncPromiseEnterCallRvas[] = { 0x1200EE5 };
constexpr std::uintptr_t kGameModeGlobalRva = 0x28276F8;
constexpr std::uintptr_t kRoadMapGlobalRva = 0x27CB3E8;
constexpr std::uintptr_t kRaceStatusMgrGlobalRva = 0x2827EE0;
constexpr std::size_t kGameModeModeOffset = 0x2C80E;
constexpr std::size_t kGameModeSubModeOffset = 0x2C810;
constexpr std::size_t kRaceContextOffset = 0x388;
constexpr std::size_t kRaceRoadMapOffset = 0x638;
constexpr std::size_t kShortcutGateProgressVectorOffset = 0x8;
constexpr std::size_t kShortcutGateObjectVectorOffset = 0x20;
constexpr std::size_t kShortcutGateIdVectorOffset = 0x158;
constexpr std::size_t kShortcutGateDoneFlagOffset = 0x1A0;
constexpr std::size_t kShortcutGateThresholdOffset = 0x1A4;
constexpr std::size_t kShortcutGateTempFlagOffset = 0x240;
constexpr std::size_t kShortcutGateNameOffset = 0x3B8;
constexpr std::size_t kShortcutGateProgressRecordSize = 0x1C;
constexpr unsigned int kKartPhysHandlerDelayedRetryDelayMs = 250;

using CompactDecode551Fn = unsigned char(__fastcall*)(void* object, unsigned __int64* base, unsigned __int64 end, unsigned int* offset);
using Build551RuntimeCacheFn = unsigned char(__fastcall*)(__int64 arg1, __int64 arg2);
using ProcNotifyKartPhysAsyncFn = __int64(__fastcall*)();
using ConsumeKartPhysRuntimeCacheFn = void(__fastcall*)(__int64 self, void* runtimeCache);
using GetCurrentPromiseContextFn = __int64(__fastcall*)();
using NoArgInt64Fn = __int64(__fastcall*)();
using SkipEnterGateFn = unsigned char(__fastcall*)();
using EnterBranchFn = void(__fastcall*)();
using NxPlayerEnterPhySceneFn = void(__fastcall*)(void* player, const float* transform);
using LuaEventTriggerFn = __int64(__fastcall*)(void* scriptState, const char* eventName, const char* format, std::uintptr_t arg1, std::uintptr_t arg2, std::uintptr_t arg3);
using AsyncPromiseEnterFn = __int64(__fastcall*)(__int64 asyncObject, char* file, char* functionName, int line);
using PointerBoolGateFn = unsigned char(__fastcall*)(void* arg);

std::uintptr_t g_topKartBase = 0;
NoArgInt64Fn g_originalHandlerEntryMarker = nullptr;
SkipEnterGateFn g_originalSkipEnterGate = nullptr;
EnterBranchFn g_originalEnterBranch = nullptr;
PointerBoolGateFn g_originalShortcutGate = nullptr;
PointerBoolGateFn g_originalObjectFlagGate = nullptr;
NxPlayerEnterPhySceneFn g_originalNxPlayerEnterPhyScene = nullptr;
LuaEventTriggerFn g_originalLuaEventTrigger = nullptr;
AsyncPromiseEnterFn g_originalAsyncPromiseEnter = nullptr;
volatile LONG g_routeProbeInstallState = 0;
std::size_t g_routeProbePatchedCalls = 0;
volatile LONG g_handlerEntryHits = 0;
volatile LONG g_notifyLuaHits = 0;
volatile LONG g_skipGateHits = 0;
volatile LONG g_enterBranchHits = 0;
volatile LONG g_nxEnterHits = 0;
volatile LONG g_asyncEnterHits = 0;
volatile LONG g_asyncEnterBypassHits = 0;
volatile LONG g_asyncEnterOriginalHits = 0;
volatile LONG g_delayedRetryActive = 0;

struct RouteProbeCounterSnapshot
{
	LONG handlerEntry = 0;
	LONG notifyLua = 0;
	LONG skipGate = 0;
	LONG enterBranch = 0;
	LONG nxEnter = 0;
	LONG asyncEnter = 0;
	LONG asyncEnterBypass = 0;
	LONG asyncEnterOriginal = 0;
};

RouteProbeCounterSnapshot SnapshotRouteProbeCounters();
void CaptureRouteProbeCounters(
	qqspeed::topkart::KartPhys551FeedResult& result,
	const RouteProbeCounterSnapshot& baseline);
void CaptureRouteProbeCounters(
	qqspeed::topkart::KartPhys551HandlerRetryResult& result,
	const RouteProbeCounterSnapshot& baseline);

void SetReason(qqspeed::topkart::KartPhys551FeedResult& result, const char* reason)
{
	strncpy_s(result.reason, reason ? reason : "unknown", _TRUNCATE);
}

void SetConsumerFallbackReason(qqspeed::topkart::KartPhys551FeedResult& result, const char* reason)
{
	strncpy_s(result.consumerFallbackReason, reason ? reason : "unknown", _TRUNCATE);
}

std::uintptr_t ReadPtrSafe(const void* ptr)
{
	std::uintptr_t value = 0;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned int ReadU32Safe(const void* ptr)
{
	unsigned int value = 0;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned short ReadU16Safe(const void* ptr)
{
	unsigned short value = 0;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned char ReadU8Safe(const void* ptr)
{
	unsigned char value = 0;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

void CopyCStringSafe(const char* src, char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}
	out[0] = 0;
	if (!src) {
		strcpy_s(out, outSize, "<null>");
		return;
	}

	constexpr std::size_t kMaxTextBytes = 160;
	const std::size_t copySize = (outSize - 1) < kMaxTextBytes ? (outSize - 1) : kMaxTextBytes;
	if (!qqspeed::network::patch::SafeCopyRaw(out, src, copySize)) {
		std::snprintf(out, outSize, "<unreadable:%p>", src);
		return;
	}
	out[copySize] = 0;
}

std::size_t CountVectorEntries(std::uintptr_t begin, std::uintptr_t end, std::size_t stride)
{
	if (!begin || end < begin || stride == 0) {
		return 0;
	}
	return static_cast<std::size_t>((end - begin) / stride);
}

std::uintptr_t CurrentTopKartReturnRva()
{
	const std::uintptr_t ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
	if (!g_topKartBase || ret < g_topKartBase) {
		return 0;
	}
	return ret - g_topKartBase;
}

std::uintptr_t CurrentTopKartCallRva()
{
	const std::uintptr_t retRva = CurrentTopKartReturnRva();
	return retRva >= 5 ? retRva - 5 : 0;
}

const char* RouteCallSiteName(std::uintptr_t callRva)
{
	switch (callRva) {
	case 0x1200E9C:
		return "handler_entry";
	case 0x1200EE5:
		return "async_promise_enter";
	case 0x1201CA6:
		return "object_flag_before_enter";
	case 0x1201E30:
		return "shortcut_gate_before_skip";
	case 0x1201E56:
		return "skip_enter_gate";
	case 0x1201E5F:
		return "enter_branch";
	case 0x1202139:
		return "notify_lua";
	case 0x9A81C0:
		return "nx_enter_phy_scene_1";
	case 0xA54E86:
		return "nx_enter_phy_scene_2";
	default:
		return "unknown";
	}
}

void LogKartPhysGate(const char* stage, std::uintptr_t callRva, void* arg, int result)
{
	const std::uintptr_t gameMode = ReadPtrSafe(reinterpret_cast<const void*>(g_topKartBase + kGameModeGlobalRva));
	const unsigned short mode = ReadU16Safe(reinterpret_cast<const void*>(gameMode + kGameModeModeOffset));
	const unsigned short subMode = ReadU16Safe(reinterpret_cast<const void*>(gameMode + kGameModeSubModeOffset));
	const std::uintptr_t roadMapGlobal = ReadPtrSafe(reinterpret_cast<const void*>(g_topKartBase + kRoadMapGlobalRva));
	const std::uintptr_t raceStatusMgr = ReadPtrSafe(reinterpret_cast<const void*>(g_topKartBase + kRaceStatusMgrGlobalRva));
	const std::uintptr_t raceContext = ReadPtrSafe(reinterpret_cast<const void*>(raceStatusMgr + kRaceContextOffset));
	const std::uintptr_t raceRoadMap = ReadPtrSafe(reinterpret_cast<const void*>(raceContext + kRaceRoadMapOffset));

	qqspeed::logging::Logf(
		"topkart_551_gate stage=%s callRva=0x%llX site=%s arg=%p result=%d gameMode=%p mode=%u subMode=%u "
		"roadMapGlobal=%p roadMapCore=%p raceStatusMgr=%p raceContext=%p raceRoadMap=%p",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		RouteCallSiteName(callRva),
		arg,
		result,
		reinterpret_cast<void*>(gameMode),
		static_cast<unsigned int>(mode),
		static_cast<unsigned int>(subMode),
		reinterpret_cast<void*>(roadMapGlobal),
		roadMapGlobal ? reinterpret_cast<void*>(roadMapGlobal + 0x208) : nullptr,
		reinterpret_cast<void*>(raceStatusMgr),
		reinterpret_cast<void*>(raceContext),
		reinterpret_cast<void*>(raceRoadMap));
}

void LogKartPhysShortcutDetail(std::uintptr_t callRva, void* arg, int result)
{
	if (!arg) {
		return;
	}

	const auto base = reinterpret_cast<std::uintptr_t>(arg);
	const unsigned int threshold = ReadU32Safe(reinterpret_cast<const void*>(base + kShortcutGateThresholdOffset));
	const unsigned char doneFlag = ReadU8Safe(reinterpret_cast<const void*>(base + kShortcutGateDoneFlagOffset));
	const unsigned char tempFlag = ReadU8Safe(reinterpret_cast<const void*>(base + kShortcutGateTempFlagOffset));

	const std::uintptr_t progressBegin = ReadPtrSafe(reinterpret_cast<const void*>(base + kShortcutGateProgressVectorOffset));
	const std::uintptr_t progressEnd = ReadPtrSafe(reinterpret_cast<const void*>(base + kShortcutGateProgressVectorOffset + sizeof(std::uintptr_t)));
	const std::uintptr_t objectBegin = ReadPtrSafe(reinterpret_cast<const void*>(base + kShortcutGateObjectVectorOffset));
	const std::uintptr_t objectEnd = ReadPtrSafe(reinterpret_cast<const void*>(base + kShortcutGateObjectVectorOffset + sizeof(std::uintptr_t)));
	const std::uintptr_t idBegin = ReadPtrSafe(reinterpret_cast<const void*>(base + kShortcutGateIdVectorOffset));
	const std::uintptr_t idEnd = ReadPtrSafe(reinterpret_cast<const void*>(base + kShortcutGateIdVectorOffset + sizeof(std::uintptr_t)));

	const std::size_t progressCount = CountVectorEntries(progressBegin, progressEnd, kShortcutGateProgressRecordSize);
	const std::size_t objectCount = CountVectorEntries(objectBegin, objectEnd, sizeof(std::uintptr_t));
	const std::size_t idCount = CountVectorEntries(idBegin, idEnd, sizeof(unsigned int));

	unsigned int firstScore = 0;
	unsigned int firstObjectId = 0;
	unsigned char firstFlag = 0;
	unsigned int lastScore = 0;
	unsigned int lastScore2 = 0;
	unsigned int lastObjectId = 0;
	unsigned char lastFlag = 0;
	unsigned int lastMarker = 0;
	std::uintptr_t lastExtra = 0;
	if (progressCount > 0 && progressCount < 0x10000) {
		const std::uintptr_t firstRecord = progressBegin;
		const std::uintptr_t lastRecord = progressBegin + ((progressCount - 1) * kShortcutGateProgressRecordSize);
		firstScore = ReadU32Safe(reinterpret_cast<const void*>(firstRecord));
		firstObjectId = ReadU32Safe(reinterpret_cast<const void*>(firstRecord + 8));
		firstFlag = ReadU8Safe(reinterpret_cast<const void*>(firstRecord + 0xC));
		lastScore = ReadU32Safe(reinterpret_cast<const void*>(lastRecord));
		lastScore2 = ReadU32Safe(reinterpret_cast<const void*>(lastRecord + 4));
		lastObjectId = ReadU32Safe(reinterpret_cast<const void*>(lastRecord + 8));
		lastFlag = ReadU8Safe(reinterpret_cast<const void*>(lastRecord + 0xC));
		lastMarker = ReadU32Safe(reinterpret_cast<const void*>(lastRecord + 0x10));
		lastExtra = ReadPtrSafe(reinterpret_cast<const void*>(lastRecord + 0x14));
	}

	char shortcutName[192] = {};
	CopyCStringSafe(reinterpret_cast<const char*>(base + kShortcutGateNameOffset), shortcutName, sizeof(shortcutName));
	qqspeed::logging::Logf(
		"topkart_551_shortcut_detail callRva=0x%llX site=%s arg=%p result=%d threshold=%u reached=%d "
		"doneFlag=%u tempFlag=%u progressBegin=%p progressEnd=%p progressCount=%llu objectCount=%llu idCount=%llu "
		"firstScore=%u firstObjectId=%u firstFlag=%u lastScore=%u lastScore2=%u lastObjectId=%u lastFlag=%u "
		"lastMarker=0x%08X lastExtra=%p name=%s",
		static_cast<unsigned long long>(callRva),
		RouteCallSiteName(callRva),
		arg,
		result,
		threshold,
		lastScore >= threshold ? 1 : 0,
		static_cast<unsigned int>(doneFlag),
		static_cast<unsigned int>(tempFlag),
		reinterpret_cast<void*>(progressBegin),
		reinterpret_cast<void*>(progressEnd),
		static_cast<unsigned long long>(progressCount),
		static_cast<unsigned long long>(objectCount),
		static_cast<unsigned long long>(idCount),
		firstScore,
		firstObjectId,
		static_cast<unsigned int>(firstFlag),
		lastScore,
		lastScore2,
		lastObjectId,
		static_cast<unsigned int>(lastFlag),
		lastMarker,
		reinterpret_cast<void*>(lastExtra),
		shortcutName);
}

void LogKartPhysLua(
	std::uintptr_t callRva,
	void* scriptState,
	const char* eventName,
	const char* format,
	std::uintptr_t arg1,
	std::uintptr_t arg2,
	std::uintptr_t arg3,
	__int64 result)
{
	char safeEvent[192] = {};
	char safeFormat[64] = {};
	CopyCStringSafe(eventName, safeEvent, sizeof(safeEvent));
	CopyCStringSafe(format, safeFormat, sizeof(safeFormat));
	qqspeed::logging::Logf(
		"topkart_551_gate stage=notify_lua callRva=0x%llX site=%s scriptState=%p event=%s format=%s "
		"arg1=0x%llX arg2=0x%llX arg3=0x%llX result=%lld",
		static_cast<unsigned long long>(callRva),
		RouteCallSiteName(callRva),
		scriptState,
		safeEvent,
		safeFormat,
		static_cast<unsigned long long>(arg1),
		static_cast<unsigned long long>(arg2),
		static_cast<unsigned long long>(arg3),
		static_cast<long long>(result));
}

unsigned char SafeCallCompactDecode(
	CompactDecode551Fn fn,
	void* object,
	unsigned __int64* base,
	unsigned __int64 end,
	unsigned int* offset,
	bool& exception)
{
	exception = false;
	if (!fn || !object || !base || !offset) {
		return 0;
	}

	__try
	{
		return fn(object, base, end, offset);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		exception = true;
		return 0;
	}
}

unsigned char SafeCallBuildRuntimeCache(Build551RuntimeCacheFn fn, __int64 arg1, __int64 arg2, bool& exception)
{
	exception = false;
	if (!fn) {
		return 0;
	}

	__try
	{
		return fn(arg1, arg2);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		exception = true;
		return 0;
	}
}

std::uintptr_t ToTopKartRva(std::uintptr_t topKartBase, std::uintptr_t address)
{
	if (!topKartBase || address < topKartBase) {
		return 0;
	}
	return address - topKartBase;
}

int CaptureHandlerException(
	EXCEPTION_POINTERS* info,
	qqspeed::topkart::KartPhys551FeedResult* result,
	std::uintptr_t topKartBase)
{
	if (!info || !result) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	const EXCEPTION_RECORD* record = info->ExceptionRecord;
	if (record) {
		const std::uintptr_t exceptionAddress = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
		result->handlerExceptionCode = record->ExceptionCode;
		result->handlerExceptionAddress = exceptionAddress;
		result->handlerExceptionRva = ToTopKartRva(topKartBase, exceptionAddress);
		if (record->NumberParameters > 0) {
			result->handlerExceptionInfo0 = static_cast<std::uintptr_t>(record->ExceptionInformation[0]);
		}
		if (record->NumberParameters > 1) {
			result->handlerExceptionInfo1 = static_cast<std::uintptr_t>(record->ExceptionInformation[1]);
		}
	}

#if defined(_M_X64)
	if (info->ContextRecord) {
		result->handlerExceptionRip = static_cast<std::uintptr_t>(info->ContextRecord->Rip);
		result->handlerExceptionRipRva = ToTopKartRva(topKartBase, result->handlerExceptionRip);
	}
#endif
	return EXCEPTION_EXECUTE_HANDLER;
}

int CaptureRetryException(
	EXCEPTION_POINTERS* info,
	qqspeed::topkart::KartPhys551HandlerRetryResult* result,
	std::uintptr_t topKartBase)
{
	if (!info || !result) {
		return EXCEPTION_EXECUTE_HANDLER;
	}

	const EXCEPTION_RECORD* record = info->ExceptionRecord;
	if (record) {
		const std::uintptr_t exceptionAddress = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
		result->exceptionCode = record->ExceptionCode;
		result->exceptionAddress = exceptionAddress;
		result->exceptionRva = ToTopKartRva(topKartBase, exceptionAddress);
		if (record->NumberParameters > 0) {
			result->exceptionInfo0 = static_cast<std::uintptr_t>(record->ExceptionInformation[0]);
		}
		if (record->NumberParameters > 1) {
			result->exceptionInfo1 = static_cast<std::uintptr_t>(record->ExceptionInformation[1]);
		}
	}

#if defined(_M_X64)
	if (info->ContextRecord) {
		result->exceptionRip = static_cast<std::uintptr_t>(info->ContextRecord->Rip);
		result->exceptionRipRva = ToTopKartRva(topKartBase, result->exceptionRip);
	}
#endif
	return EXCEPTION_EXECUTE_HANDLER;
}

__int64 SafeCallProcNotifyKartPhysAsync(
	ProcNotifyKartPhysAsyncFn fn,
	qqspeed::topkart::KartPhys551FeedResult& result,
	std::uintptr_t topKartBase)
{
	result.handlerException = false;
	result.handlerExceptionCode = 0;
	result.handlerExceptionAddress = 0;
	result.handlerExceptionRva = 0;
	result.handlerExceptionRip = 0;
	result.handlerExceptionRipRva = 0;
	result.handlerExceptionInfo0 = 0;
	result.handlerExceptionInfo1 = 0;
	if (!fn) {
		return 0;
	}

	__try
	{
		return fn();
	}
	__except (CaptureHandlerException(GetExceptionInformation(), &result, topKartBase))
	{
		result.handlerException = true;
		return 0;
	}
}

bool SafeCallProcNotifyKartPhysAsyncRetry(
	ProcNotifyKartPhysAsyncFn fn,
	std::uintptr_t topKartBase,
	qqspeed::topkart::KartPhys551HandlerRetryResult& result)
{
	result = {};
	if (!fn) {
		return false;
	}

	const RouteProbeCounterSnapshot baseline = SnapshotRouteProbeCounters();
	result.called = true;
	__try
	{
		result.result = fn();
	}
	__except (CaptureRetryException(GetExceptionInformation(), &result, topKartBase))
	{
		result.exception = true;
	}
	CaptureRouteProbeCounters(result, baseline);
	return !result.exception;
}

std::uintptr_t SafeCallGetCurrentPromiseContext(GetCurrentPromiseContextFn fn, bool& exception)
{
	exception = false;
	if (!fn) {
		return 0;
	}

	__try
	{
		return static_cast<std::uintptr_t>(fn());
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		exception = true;
		return 0;
	}
}

bool SafeCallConsumeKartPhysRuntimeCache(ConsumeKartPhysRuntimeCacheFn fn, void* runtimeCache, bool& exception)
{
	exception = false;
	if (!fn || !runtimeCache) {
		return false;
	}

	__try
	{
		fn(0, runtimeCache);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		exception = true;
		return false;
	}
}

const char* CaptureConsumerContext(
	std::uintptr_t topKartBase,
	const qqspeed::config::HookConfig& config,
	GetCurrentPromiseContextFn getPromiseContextFn,
	qqspeed::topkart::KartPhys551FeedResult& result)
{
	constexpr std::uintptr_t kInvalidRaceContext = static_cast<std::uintptr_t>(-472);

	result.raceContext = ReadPtrSafe(reinterpret_cast<const void*>(topKartBase + config.topKart.raceContextGlobal));
	result.kartMgr = ReadPtrSafe(reinterpret_cast<const void*>(topKartBase + config.topKart.kartMgrGlobal));
	result.luaUi = ReadPtrSafe(reinterpret_cast<const void*>(topKartBase + config.topKart.luaUiGlobal));
	result.gameStatusMgr = ReadPtrSafe(reinterpret_cast<const void*>(topKartBase + config.topKart.gameStatusMgrGlobal));
	result.srcGlobalValue = ReadPtrSafe(reinterpret_cast<const void*>(topKartBase + config.topKart.srcGlobal));

	if (!result.raceContext || result.raceContext == kInvalidRaceContext) {
		return "fallback_race_context_missing";
	}
	if (!result.kartMgr) {
		return "fallback_kart_mgr_missing";
	}
	if (!result.luaUi) {
		return "fallback_lua_ui_missing";
	}
	if (!result.gameStatusMgr) {
		return "fallback_game_status_mgr_missing";
	}

	bool promiseException = false;
	result.promiseContextPresent = SafeCallGetCurrentPromiseContext(getPromiseContextFn, promiseException) != 0;
	if (promiseException) {
		return "fallback_promise_context_exception";
	}

	const unsigned char statusAllowsNoPromise =
		ReadU8Safe(reinterpret_cast<const void*>(result.gameStatusMgr + 1504));
	if (!result.promiseContextPresent && !statusAllowsNoPromise && result.srcGlobalValue) {
		return "fallback_promise_context_missing";
	}

	result.gameStatusApp = ReadPtrSafe(reinterpret_cast<const void*>(result.gameStatusMgr + (111 * sizeof(std::uintptr_t))));
	if (!result.gameStatusApp) {
		return "fallback_game_status_app_missing";
	}

	result.gameStatusScene = ReadPtrSafe(reinterpret_cast<const void*>(result.gameStatusApp + 1592));
	if (!result.gameStatusScene) {
		return "fallback_game_status_scene_missing";
	}

	result.consumerContextReady = true;
	return nullptr;
}

struct DelayedHandlerRetryContext
{
	ProcNotifyKartPhysAsyncFn handlerFn = nullptr;
	std::uintptr_t topKartBase = 0;
	qqspeed::config::HookConfig config = {};
	unsigned int delayMs = 0;
	unsigned int firstExceptionCode = 0;
	std::uintptr_t firstExceptionRva = 0;
	std::uintptr_t firstExceptionInfo0 = 0;
	std::uintptr_t firstExceptionInfo1 = 0;
};

DWORD WINAPI DelayedHandlerRetryThread(void* param)
{
	std::unique_ptr<DelayedHandlerRetryContext> context(static_cast<DelayedHandlerRetryContext*>(param));
	if (!context) {
		InterlockedExchange(&g_delayedRetryActive, 0);
		return 0;
	}

	Sleep(context->delayMs);

	qqspeed::topkart::KartPhys551FeedResult contextSnapshot = {};
	const auto getPromiseContextFn =
		reinterpret_cast<GetCurrentPromiseContextFn>(context->topKartBase + context->config.topKart.getCurrentPromiseContext);
	const char* contextReason = CaptureConsumerContext(
		context->topKartBase,
		context->config,
		getPromiseContextFn,
		contextSnapshot);
	if (!contextReason) {
		contextReason = "context_ready";
	}

	qqspeed::topkart::KartPhys551HandlerRetryResult retry = {};
	SafeCallProcNotifyKartPhysAsyncRetry(context->handlerFn, context->topKartBase, retry);

	qqspeed::logging::Logf(
		"topkart_551_delayed_retry delayMs=%u called=%d result=%lld exception=%d exceptionCode=0x%08X exceptionAddress=%p exceptionRva=0x%llX exceptionRip=%p exceptionRipRva=0x%llX exceptionInfo0=0x%llX exceptionInfo1=%p handlerEntryDelta=%ld notifyLuaDelta=%ld skipGateDelta=%ld enterBranchDelta=%ld nxEnterDelta=%ld asyncEnterDelta=%ld asyncEnterBypassDelta=%ld asyncEnterOriginalDelta=%ld contextReady=%d contextReason=%s promiseContext=%d firstExceptionCode=0x%08X firstExceptionRva=0x%llX firstExceptionInfo0=0x%llX firstExceptionInfo1=%p topKartBase=%p raceContext=%p kartMgr=%p luaUi=%p gameStatusMgr=%p gameStatusApp=%p gameStatusScene=%p srcGlobal=%p",
		context->delayMs,
		retry.called ? 1 : 0,
		static_cast<long long>(retry.result),
		retry.exception ? 1 : 0,
		retry.exceptionCode,
		reinterpret_cast<void*>(retry.exceptionAddress),
		static_cast<unsigned long long>(retry.exceptionRva),
		reinterpret_cast<void*>(retry.exceptionRip),
		static_cast<unsigned long long>(retry.exceptionRipRva),
		static_cast<unsigned long long>(retry.exceptionInfo0),
		reinterpret_cast<void*>(retry.exceptionInfo1),
		retry.handlerEntryDelta,
		retry.notifyLuaDelta,
		retry.skipGateDelta,
		retry.enterBranchDelta,
		retry.nxEnterDelta,
		retry.asyncEnterDelta,
		retry.asyncEnterBypassDelta,
		retry.asyncEnterOriginalDelta,
		contextSnapshot.consumerContextReady ? 1 : 0,
		contextReason,
		contextSnapshot.promiseContextPresent ? 1 : 0,
		context->firstExceptionCode,
		static_cast<unsigned long long>(context->firstExceptionRva),
		static_cast<unsigned long long>(context->firstExceptionInfo0),
		reinterpret_cast<void*>(context->firstExceptionInfo1),
		reinterpret_cast<void*>(context->topKartBase),
		reinterpret_cast<void*>(contextSnapshot.raceContext),
		reinterpret_cast<void*>(contextSnapshot.kartMgr),
		reinterpret_cast<void*>(contextSnapshot.luaUi),
		reinterpret_cast<void*>(contextSnapshot.gameStatusMgr),
		reinterpret_cast<void*>(contextSnapshot.gameStatusApp),
		reinterpret_cast<void*>(contextSnapshot.gameStatusScene),
		reinterpret_cast<void*>(contextSnapshot.srcGlobalValue));

	InterlockedExchange(&g_delayedRetryActive, 0);
	return 0;
}

bool ScheduleDelayedHandlerRetry(
	ProcNotifyKartPhysAsyncFn handlerFn,
	std::uintptr_t topKartBase,
	const qqspeed::config::HookConfig& config,
	const qqspeed::topkart::KartPhys551FeedResult& source,
	qqspeed::topkart::KartPhys551FeedResult& result)
{
	result.handlerRetryDelayedDelayMs = kKartPhysHandlerDelayedRetryDelayMs;
	if (!handlerFn || !topKartBase) {
		result.handlerRetryDelayedCreateFailed = true;
		return false;
	}
	if (InterlockedCompareExchange(&g_delayedRetryActive, 1, 0) != 0) {
		result.handlerRetryDelayedSkippedActive = true;
		return false;
	}

	auto* context = new (std::nothrow) DelayedHandlerRetryContext();
	if (!context) {
		InterlockedExchange(&g_delayedRetryActive, 0);
		result.handlerRetryDelayedCreateFailed = true;
		return false;
	}

	context->handlerFn = handlerFn;
	context->topKartBase = topKartBase;
	context->config = config;
	context->delayMs = kKartPhysHandlerDelayedRetryDelayMs;
	context->firstExceptionCode = source.handlerExceptionCode;
	context->firstExceptionRva = source.handlerExceptionRva;
	context->firstExceptionInfo0 = source.handlerExceptionInfo0;
	context->firstExceptionInfo1 = source.handlerExceptionInfo1;

	HANDLE thread = CreateThread(nullptr, 0, &DelayedHandlerRetryThread, context, 0, nullptr);
	if (!thread) {
		delete context;
		InterlockedExchange(&g_delayedRetryActive, 0);
		result.handlerRetryDelayedCreateFailed = true;
		return false;
	}

	CloseHandle(thread);
	result.handlerRetryDelayedScheduled = true;
	return true;
}

bool TryConsumerFallback(
	std::uintptr_t topKartBase,
	const qqspeed::config::HookConfig& config,
	qqspeed::topkart::KartPhys551FeedResult& result)
{
	result.consumerFallbackAttempted = true;
	if (!config.features.enableTopKart551ConsumerFallback) {
		SetConsumerFallbackReason(result, "fallback_disabled");
		return false;
	}
	if (!result.consumerFn || !result.runtimeCache) {
		SetConsumerFallbackReason(result, "fallback_consumer_fn_missing");
		return false;
	}

	const auto consumeFn = reinterpret_cast<ConsumeKartPhysRuntimeCacheFn>(result.consumerFn);
	const auto getPromiseContextFn =
		reinterpret_cast<GetCurrentPromiseContextFn>(topKartBase + config.topKart.getCurrentPromiseContext);
	if (!consumeFn || !getPromiseContextFn) {
		SetConsumerFallbackReason(result, "fallback_consumer_fn_missing");
		return false;
	}

	if (const char* contextReason = CaptureConsumerContext(topKartBase, config, getPromiseContextFn, result)) {
		SetConsumerFallbackReason(result, contextReason);
		return false;
	}

	result.consumerCalled = true;
	if (!SafeCallConsumeKartPhysRuntimeCache(
		consumeFn,
		reinterpret_cast<void*>(result.runtimeCache),
		result.consumerException)) {
		SetConsumerFallbackReason(result, result.consumerException ? "fallback_consumer_exception" : "fallback_consumer_failed");
		return false;
	}

	SetConsumerFallbackReason(result, "fallback_consumer_triggered");
	return true;
}

RouteProbeCounterSnapshot SnapshotRouteProbeCounters()
{
	RouteProbeCounterSnapshot snapshot = {};
	snapshot.handlerEntry = InterlockedCompareExchange(&g_handlerEntryHits, 0, 0);
	snapshot.notifyLua = InterlockedCompareExchange(&g_notifyLuaHits, 0, 0);
	snapshot.skipGate = InterlockedCompareExchange(&g_skipGateHits, 0, 0);
	snapshot.enterBranch = InterlockedCompareExchange(&g_enterBranchHits, 0, 0);
	snapshot.nxEnter = InterlockedCompareExchange(&g_nxEnterHits, 0, 0);
	snapshot.asyncEnter = InterlockedCompareExchange(&g_asyncEnterHits, 0, 0);
	snapshot.asyncEnterBypass = InterlockedCompareExchange(&g_asyncEnterBypassHits, 0, 0);
	snapshot.asyncEnterOriginal = InterlockedCompareExchange(&g_asyncEnterOriginalHits, 0, 0);
	return snapshot;
}

void CaptureRouteProbeCounters(
	qqspeed::topkart::KartPhys551FeedResult& result,
	const RouteProbeCounterSnapshot& baseline)
{
	result.handlerEntryDelta = InterlockedCompareExchange(&g_handlerEntryHits, 0, 0) - baseline.handlerEntry;
	result.notifyLuaDelta = InterlockedCompareExchange(&g_notifyLuaHits, 0, 0) - baseline.notifyLua;
	result.skipGateDelta = InterlockedCompareExchange(&g_skipGateHits, 0, 0) - baseline.skipGate;
	result.enterBranchDelta = InterlockedCompareExchange(&g_enterBranchHits, 0, 0) - baseline.enterBranch;
	result.nxEnterDelta = InterlockedCompareExchange(&g_nxEnterHits, 0, 0) - baseline.nxEnter;
	result.asyncEnterDelta = InterlockedCompareExchange(&g_asyncEnterHits, 0, 0) - baseline.asyncEnter;
	result.asyncEnterBypassDelta = InterlockedCompareExchange(&g_asyncEnterBypassHits, 0, 0) - baseline.asyncEnterBypass;
	result.asyncEnterOriginalDelta = InterlockedCompareExchange(&g_asyncEnterOriginalHits, 0, 0) - baseline.asyncEnterOriginal;
}

void CaptureRouteProbeCounters(
	qqspeed::topkart::KartPhys551HandlerRetryResult& result,
	const RouteProbeCounterSnapshot& baseline)
{
	result.handlerEntryDelta = InterlockedCompareExchange(&g_handlerEntryHits, 0, 0) - baseline.handlerEntry;
	result.notifyLuaDelta = InterlockedCompareExchange(&g_notifyLuaHits, 0, 0) - baseline.notifyLua;
	result.skipGateDelta = InterlockedCompareExchange(&g_skipGateHits, 0, 0) - baseline.skipGate;
	result.enterBranchDelta = InterlockedCompareExchange(&g_enterBranchHits, 0, 0) - baseline.enterBranch;
	result.nxEnterDelta = InterlockedCompareExchange(&g_nxEnterHits, 0, 0) - baseline.nxEnter;
	result.asyncEnterDelta = InterlockedCompareExchange(&g_asyncEnterHits, 0, 0) - baseline.asyncEnter;
	result.asyncEnterBypassDelta = InterlockedCompareExchange(&g_asyncEnterBypassHits, 0, 0) - baseline.asyncEnterBypass;
	result.asyncEnterOriginalDelta = InterlockedCompareExchange(&g_asyncEnterOriginalHits, 0, 0) - baseline.asyncEnterOriginal;
}

__int64 __fastcall HookedHandlerEntryMarker()
{
	InterlockedIncrement(&g_handlerEntryHits);
	NoArgInt64Fn original = g_originalHandlerEntryMarker;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<NoArgInt64Fn>(g_topKartBase + kKartPhysHandlerEntryMarkerRva);
	}
	return original ? original() : 0;
}

unsigned char __fastcall HookedSkipEnterGate()
{
	InterlockedIncrement(&g_skipGateHits);
	const std::uintptr_t callRva = CurrentTopKartCallRva();
	SkipEnterGateFn original = g_originalSkipEnterGate;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<SkipEnterGateFn>(g_topKartBase + kKartPhysSkipEnterGateRva);
	}
	const unsigned char result = original ? original() : 1;
	LogKartPhysGate("skip_enter_gate", callRva, nullptr, result);
	return result;
}

void __fastcall HookedEnterBranch()
{
	InterlockedIncrement(&g_enterBranchHits);
	const std::uintptr_t callRva = CurrentTopKartCallRva();
	LogKartPhysGate("enter_branch_begin", callRva, nullptr, 0);
	EnterBranchFn original = g_originalEnterBranch;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<EnterBranchFn>(g_topKartBase + kKartPhysEnterBranchRva);
	}
	if (original) {
		original();
	}
	LogKartPhysGate("enter_branch_end", callRva, nullptr, 0);
}

unsigned char __fastcall HookedShortcutGate(void* arg)
{
	const std::uintptr_t callRva = CurrentTopKartCallRva();
	PointerBoolGateFn original = g_originalShortcutGate;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<PointerBoolGateFn>(g_topKartBase + kKartPhysShortcutGateRva);
	}
	const unsigned char result = original ? original(arg) : 0;
	LogKartPhysGate("shortcut_gate", callRva, arg, result);
	LogKartPhysShortcutDetail(callRva, arg, result);
	return result;
}

unsigned char __fastcall HookedObjectFlagGate(void* arg)
{
	const std::uintptr_t callRva = CurrentTopKartCallRva();
	PointerBoolGateFn original = g_originalObjectFlagGate;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<PointerBoolGateFn>(g_topKartBase + kKartPhysObjectFlagGateRva);
	}
	const unsigned char result = original ? original(arg) : 0;
	LogKartPhysGate("object_flag_gate", callRva, arg, result);
	return result;
}

void __fastcall HookedNxPlayerEnterPhyScene(void* player, const float* transform)
{
	InterlockedIncrement(&g_nxEnterHits);
	const std::uintptr_t callRva = CurrentTopKartCallRva();
	LogKartPhysGate("nx_enter_phy_scene", callRva, player, transform ? 1 : 0);
	NxPlayerEnterPhySceneFn original = g_originalNxPlayerEnterPhyScene;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<NxPlayerEnterPhySceneFn>(g_topKartBase + kNxPlayerEnterPhySceneImplRva);
	}
	if (original) {
		original(player, transform);
	}
}

__int64 __fastcall HookedLuaEventTrigger(
	void* scriptState,
	const char* eventName,
	const char* format,
	std::uintptr_t arg1,
	std::uintptr_t arg2,
	std::uintptr_t arg3)
{
	InterlockedIncrement(&g_notifyLuaHits);
	const std::uintptr_t callRva = CurrentTopKartCallRva();

	LuaEventTriggerFn original = g_originalLuaEventTrigger;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LuaEventTriggerFn>(g_topKartBase + kLuaEventTriggerRva);
	}
	const __int64 result = original ? original(scriptState, eventName, format, arg1, arg2, arg3) : 0;
	LogKartPhysLua(callRva, scriptState, eventName, format, arg1, arg2, arg3, result);
	return result;
}

__int64 __fastcall HookedAsyncPromiseEnter(__int64 asyncObject, char* file, char* functionName, int line)
{
	InterlockedIncrement(&g_asyncEnterHits);

	bool promiseException = false;
	const auto getPromiseContextFn = g_topKartBase ?
		reinterpret_cast<GetCurrentPromiseContextFn>(g_topKartBase + qqspeed::config::ActiveConfig().topKart.getCurrentPromiseContext) :
		nullptr;
	const bool hasPromiseContext = SafeCallGetCurrentPromiseContext(getPromiseContextFn, promiseException) != 0;
	if (!hasPromiseContext) {
		InterlockedIncrement(&g_asyncEnterBypassHits);
		return 0;
	}

	InterlockedIncrement(&g_asyncEnterOriginalHits);
	AsyncPromiseEnterFn original = g_originalAsyncPromiseEnter;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<AsyncPromiseEnterFn>(g_topKartBase + kAsyncPromiseEnterRva);
	}
	return original ? original(asyncObject, file, functionName, line) : 0;
}

std::size_t PatchTopKartRouteCalls(std::uintptr_t topKartBase)
{
	std::size_t patched = 0;
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kKartPhysAsyncPromiseEnterCallRvas,
		_countof(kKartPhysAsyncPromiseEnterCallRvas),
		kAsyncPromiseEnterRva,
		reinterpret_cast<void*>(&HookedAsyncPromiseEnter),
		"TopKart551 async_promise_enter");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kKartPhysHandlerEntryMarkerCallRvas,
		_countof(kKartPhysHandlerEntryMarkerCallRvas),
		kKartPhysHandlerEntryMarkerRva,
		reinterpret_cast<void*>(&HookedHandlerEntryMarker),
		"TopKart551 handler_entry");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kKartPhysSkipEnterGateCallRvas,
		_countof(kKartPhysSkipEnterGateCallRvas),
		kKartPhysSkipEnterGateRva,
		reinterpret_cast<void*>(&HookedSkipEnterGate),
		"TopKart551 skip_gate");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kKartPhysEnterBranchCallRvas,
		_countof(kKartPhysEnterBranchCallRvas),
		kKartPhysEnterBranchRva,
		reinterpret_cast<void*>(&HookedEnterBranch),
		"TopKart551 enter_branch");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kKartPhysShortcutGateCallRvas,
		_countof(kKartPhysShortcutGateCallRvas),
		kKartPhysShortcutGateRva,
		reinterpret_cast<void*>(&HookedShortcutGate),
		"TopKart551 shortcut_gate");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kKartPhysObjectFlagGateCallRvas,
		_countof(kKartPhysObjectFlagGateCallRvas),
		kKartPhysObjectFlagGateRva,
		reinterpret_cast<void*>(&HookedObjectFlagGate),
		"TopKart551 object_flag_gate");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kNxPlayerEnterPhySceneImplCallRvas,
		_countof(kNxPlayerEnterPhySceneImplCallRvas),
		kNxPlayerEnterPhySceneImplRva,
		reinterpret_cast<void*>(&HookedNxPlayerEnterPhyScene),
		"TopKart551 nx_enter_phy_scene");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kLuaEventTriggerCallRvas,
		_countof(kLuaEventTriggerCallRvas),
		kLuaEventTriggerRva,
		reinterpret_cast<void*>(&HookedLuaEventTrigger),
		"TopKart551 notify_lua");
	return patched;
}

bool EnsureRouteProbeInstalled(std::uintptr_t topKartBase, qqspeed::topkart::KartPhys551FeedResult& result)
{
	result.routeProbeInstallAttempted = true;
	result.routeProbePatchedCalls = g_routeProbePatchedCalls;

	if (InterlockedCompareExchange(&g_routeProbeInstallState, 0, 0) == 2) {
		result.routeProbeInstalled = g_routeProbePatchedCalls > 0;
		return result.routeProbeInstalled;
	}
	if (!topKartBase) {
		return false;
	}
	if (InterlockedCompareExchange(&g_routeProbeInstallState, 1, 0) != 0) {
		result.routeProbeInstalled = InterlockedCompareExchange(&g_routeProbeInstallState, 0, 0) == 2;
		result.routeProbePatchedCalls = g_routeProbePatchedCalls;
		return result.routeProbeInstalled;
	}

	g_topKartBase = topKartBase;
	g_originalHandlerEntryMarker = reinterpret_cast<NoArgInt64Fn>(topKartBase + kKartPhysHandlerEntryMarkerRva);
	g_originalSkipEnterGate = reinterpret_cast<SkipEnterGateFn>(topKartBase + kKartPhysSkipEnterGateRva);
	g_originalEnterBranch = reinterpret_cast<EnterBranchFn>(topKartBase + kKartPhysEnterBranchRva);
	g_originalShortcutGate = reinterpret_cast<PointerBoolGateFn>(topKartBase + kKartPhysShortcutGateRva);
	g_originalObjectFlagGate = reinterpret_cast<PointerBoolGateFn>(topKartBase + kKartPhysObjectFlagGateRva);
	g_originalNxPlayerEnterPhyScene = reinterpret_cast<NxPlayerEnterPhySceneFn>(topKartBase + kNxPlayerEnterPhySceneImplRva);
	g_originalLuaEventTrigger = reinterpret_cast<LuaEventTriggerFn>(topKartBase + kLuaEventTriggerRva);
	g_originalAsyncPromiseEnter = reinterpret_cast<AsyncPromiseEnterFn>(topKartBase + kAsyncPromiseEnterRva);

	g_routeProbePatchedCalls = PatchTopKartRouteCalls(topKartBase);
	InterlockedExchange(&g_routeProbeInstallState, 2);
	result.routeProbeInstalled = g_routeProbePatchedCalls > 0;
	result.routeProbePatchedCalls = g_routeProbePatchedCalls;
	qqspeed::logging::Logf(
		"topkart_551_route_probe installed=%d patchedCalls=%llu base=%p",
		result.routeProbeInstalled ? 1 : 0,
		static_cast<unsigned long long>(g_routeProbePatchedCalls),
		reinterpret_cast<void*>(topKartBase));
	return result.routeProbeInstalled;
}
}

namespace qqspeed::topkart
{
QQSPEED_VMP_NOINLINE bool FeedKartPhys551CompactBody(
	HMODULE topKartModule,
	const config::HookConfig& config,
	const unsigned char* compactBody,
	int compactLen,
	KartPhys551FeedResult& result)
{
	QQSPEED_VMP_BEGIN_ULTRA("Login.Msg551.CompactFeed");
	result = {};
	result.attempted = true;
	SetReason(result, "precheck_failed");

	if (!compactBody || compactLen <= 0 || compactLen > kMaxCompact551BodyLen) {
		SetReason(result, "bad_inner_body");
		return false;
	}
	if (!topKartModule) {
		SetReason(result, "missing_topkart_base");
		return false;
	}

	const std::uintptr_t topKartBase = reinterpret_cast<std::uintptr_t>(topKartModule);
	result.topKartBase = topKartBase;
	result.runtimeCache = topKartBase + config.topKart.kartPhysRuntimeCache;
	result.compactCache = ReadPtrSafe(reinterpret_cast<const void*>(topKartBase + config.topKart.kartPhysCompactCacheGlobal));
	result.decodeFn = topKartBase + config.topKart.compactDecode551;
	result.buildFn = topKartBase + config.topKart.build551RuntimeCache;
	result.handlerFn = topKartBase + config.topKart.procNotifyKartPhysAsync;
	result.consumerFn = topKartBase + config.topKart.consumeKartPhysRuntimeCache;
	EnsureRouteProbeInstalled(topKartBase, result);

	if (!result.compactCache) {
		SetReason(result, "compact_cache_null");
		return false;
	}

	const auto decodeFn = reinterpret_cast<CompactDecode551Fn>(result.decodeFn);
	const auto buildFn = reinterpret_cast<Build551RuntimeCacheFn>(result.buildFn);
	const auto handlerFn = reinterpret_cast<ProcNotifyKartPhysAsyncFn>(result.handlerFn);
	if (!decodeFn || !buildFn || !handlerFn) {
		SetReason(result, "missing_topkart_fn");
		return false;
	}

	std::vector<unsigned char> bodyCopy(static_cast<std::size_t>(compactLen));
	if (!qqspeed::network::patch::SafeCopyRaw(bodyCopy.data(), compactBody, bodyCopy.size())) {
		SetReason(result, "inner_copy_failed");
		return false;
	}
	result.copied = true;

	unsigned __int64 base = reinterpret_cast<unsigned __int64>(bodyCopy.data());
	const unsigned __int64 end = base + static_cast<unsigned __int64>(bodyCopy.size());
	unsigned int offset = 0;
	result.offsetBefore = offset;
	result.decodeResult = SafeCallCompactDecode(
		decodeFn,
		reinterpret_cast<void*>(result.compactCache),
		&base,
		end,
		&offset,
		result.decodeException);
	result.offsetAfter = offset;
	if (result.decodeException) {
		SetReason(result, "topkart_compact_decode_exception");
		return false;
	}
	if (!result.decodeResult) {
		SetReason(result, "topkart_compact_decode_failed");
		return false;
	}
	result.decoded = true;

	result.runtimeHeaderBefore = ReadU32Safe(reinterpret_cast<const void*>(result.runtimeCache));
	result.buildCalled = true;
	result.buildResult = SafeCallBuildRuntimeCache(
		buildFn,
		static_cast<__int64>(result.compactCache),
		0,
		result.buildException);
	result.runtimeHeaderAfterBuild = ReadU32Safe(reinterpret_cast<const void*>(result.runtimeCache));
	if (result.buildException) {
		SetReason(result, "topkart_runtime_build_exception");
		return false;
	}

	const RouteProbeCounterSnapshot baseline = SnapshotRouteProbeCounters();
	result.handlerCalled = true;
	result.handlerResult = SafeCallProcNotifyKartPhysAsync(handlerFn, result, topKartBase);
	CaptureRouteProbeCounters(result, baseline);
	if (result.handlerException) {
		if (TryConsumerFallback(topKartBase, config, result)) {
			const bool shouldRetryImmediate = config.features.enableTopKart551HandlerImmediateRetry;
			if (shouldRetryImmediate) {
				SafeCallProcNotifyKartPhysAsyncRetry(handlerFn, topKartBase, result.handlerRetryImmediate);
			}
			if ((!shouldRetryImmediate || result.handlerRetryImmediate.exception) &&
				config.features.enableTopKart551HandlerDelayedRetry) {
				ScheduleDelayedHandlerRetry(handlerFn, topKartBase, config, result, result);
			}
			result.ok = true;
			if (result.handlerRetryImmediate.called && !result.handlerRetryImmediate.exception) {
				SetReason(result, "topkart_handler_exception_consumer_retry_ok");
			}
			else if (result.handlerRetryDelayedScheduled) {
				SetReason(result, "topkart_handler_exception_consumer_delayed_retry");
			}
			else if (result.handlerRetryImmediate.called) {
				SetReason(result, "topkart_handler_exception_consumer_retry_exception");
			}
			else {
				SetReason(result, "topkart_handler_exception_consumer_fallback");
			}
			return true;
		}
		SetReason(result, "topkart_handler_exception");
		return false;
	}

	result.ok = true;
	SetReason(result, "ok_handler_triggered");
	QQSPEED_VMP_END();
	return true;
}
}
