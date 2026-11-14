#pragma once

#include "hook_config.h"

namespace qqspeed::topkart
{
struct KartPhys551HandlerRetryResult
{
	bool called = false;
	bool exception = false;
	__int64 result = 0;
	unsigned int exceptionCode = 0;
	std::uintptr_t exceptionAddress = 0;
	std::uintptr_t exceptionRva = 0;
	std::uintptr_t exceptionRip = 0;
	std::uintptr_t exceptionRipRva = 0;
	std::uintptr_t exceptionInfo0 = 0;
	std::uintptr_t exceptionInfo1 = 0;
	long handlerEntryDelta = 0;
	long notifyLuaDelta = 0;
	long skipGateDelta = 0;
	long enterBranchDelta = 0;
	long nxEnterDelta = 0;
	long asyncEnterDelta = 0;
	long asyncEnterBypassDelta = 0;
	long asyncEnterOriginalDelta = 0;
};

struct KartPhys551FeedResult
{
	bool attempted = false;
	bool ok = false;
	bool copied = false;
	bool decoded = false;
	bool decodeException = false;
	bool buildCalled = false;
	bool buildException = false;
	bool handlerCalled = false;
	bool handlerException = false;
	std::uintptr_t topKartBase = 0;
	std::uintptr_t compactCache = 0;
	std::uintptr_t runtimeCache = 0;
	std::uintptr_t decodeFn = 0;
	std::uintptr_t buildFn = 0;
	std::uintptr_t handlerFn = 0;
	std::uintptr_t consumerFn = 0;
	std::uintptr_t raceContext = 0;
	std::uintptr_t kartMgr = 0;
	std::uintptr_t luaUi = 0;
	std::uintptr_t gameStatusMgr = 0;
	std::uintptr_t gameStatusApp = 0;
	std::uintptr_t gameStatusScene = 0;
	std::uintptr_t srcGlobalValue = 0;
	unsigned int offsetBefore = 0;
	unsigned int offsetAfter = 0;
	unsigned int runtimeHeaderBefore = 0;
	unsigned int runtimeHeaderAfterBuild = 0;
	unsigned char decodeResult = 0;
	unsigned char buildResult = 0;
	__int64 handlerResult = 0;
	unsigned int handlerExceptionCode = 0;
	std::uintptr_t handlerExceptionAddress = 0;
	std::uintptr_t handlerExceptionRva = 0;
	std::uintptr_t handlerExceptionRip = 0;
	std::uintptr_t handlerExceptionRipRva = 0;
	std::uintptr_t handlerExceptionInfo0 = 0;
	std::uintptr_t handlerExceptionInfo1 = 0;
	KartPhys551HandlerRetryResult handlerRetryImmediate;
	bool handlerRetryDelayedScheduled = false;
	bool handlerRetryDelayedCreateFailed = false;
	bool handlerRetryDelayedSkippedActive = false;
	unsigned int handlerRetryDelayedDelayMs = 0;
	bool routeProbeInstallAttempted = false;
	bool routeProbeInstalled = false;
	std::size_t routeProbePatchedCalls = 0;
	bool consumerFallbackAttempted = false;
	bool consumerContextReady = false;
	bool consumerCalled = false;
	bool consumerException = false;
	bool promiseContextPresent = false;
	char consumerFallbackReason[80] = "not_attempted";
	long handlerEntryDelta = 0;
	long notifyLuaDelta = 0;
	long skipGateDelta = 0;
	long enterBranchDelta = 0;
	long nxEnterDelta = 0;
	long asyncEnterDelta = 0;
	long asyncEnterBypassDelta = 0;
	long asyncEnterOriginalDelta = 0;
	char reason[80] = "not_attempted";
};

bool FeedKartPhys551CompactBody(
	HMODULE topKartModule,
	const config::HookConfig& config,
	const unsigned char* compactBody,
	int compactLen,
	KartPhys551FeedResult& result);
}
