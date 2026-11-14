#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace qqspeed::config
{
#if !defined(QQSPEED_VMP_PROTECTION)
template <std::size_t Count>
struct RvaList
{
	std::array<std::uintptr_t, Count> values{};
	std::size_t count = 0;
};
#endif

struct NetworkFingerprint
{
	const char* name = nullptr;
	std::size_t imageSize = 0;
	const char* md5 = nullptr;
};

struct NetworkIdentity
{
	// Keep this profile tied to the exact current binary so an unrelated
	// Network.dll cannot pass preflight on image size alone.
	std::array<NetworkFingerprint, 1> acceptedFingerprints = { {
		{ "official-auto-97d25c1c", 0x968000, "97d25c1c4cf7730f26c226cdc6e8bb3c" }
	} };
	std::size_t maxKnownImageSize = 0x968000;
};

#if !defined(QQSPEED_VMP_PROTECTION)
struct NetworkOffsets : NetworkIdentity
{
	// 21932 direct Msg100 compatibility path.  Only the two enabled functions
	// below are populated; disabled Pack/Serialize/Special paths stay empty so
	// stale profiles cannot be activated accidentally.
	std::uintptr_t unpack = 0x42D8C0;
	std::uintptr_t pack = 0;
	std::uintptr_t serializeBody = 0;
	std::uintptr_t safePacketBuilder = 0;
	std::uintptr_t special28346Decode = 0;
	std::uintptr_t innerDispatch = 0x421670;
	std::uintptr_t filterProtocolInfo = 0;
	std::uintptr_t gameSocketOnMyReceiveUnpackCall = 0x42D77B;
	std::uintptr_t directInnerDispatchCall = 0x42DF50;
	std::uintptr_t socketCachedDstIdStateOffset = 0;
	std::uintptr_t unpackMsgGateTargetTable = 0;
	std::uintptr_t unpackMsgGateIndexTable = 0;
	std::uintptr_t unpackMsgGateReturn0 = 0;
	RvaList<8> unpackCallSites = { { 0x42D77B }, 1 };
	RvaList<8> packCallSites = {};
	RvaList<4> filterProtocolInfoCallSites = {};
	RvaList<4> innerDispatchCallSites = { { 0x42DF50 }, 1 };
	RvaList<4> serializeBodyCallSites = {};
	RvaList<4> safePacketBuilderCallSites = {};
	RvaList<4> special28346DecodeCallSites = {};
};
#endif

struct TopKartOffsets
{
	// Optional 21482 probes are disabled and their stale RVAs must not be
	// reused on 21681. Conditional AntiChecker has its own verified profile.
	std::uintptr_t kartPhysCompactCacheGlobal = 0;
	std::uintptr_t kartPhysRuntimeCache = 0;
	std::uintptr_t compactDecode551 = 0;
	std::uintptr_t build551RuntimeCache = 0;
	std::uintptr_t procNotifyKartPhysAsync = 0;
	std::uintptr_t consumeKartPhysRuntimeCache = 0;
	std::uintptr_t getCurrentPromiseContext = 0;
	std::uintptr_t raceContextGlobal = 0;
	std::uintptr_t kartMgrGlobal = 0;
	std::uintptr_t luaUiGlobal = 0;
	std::uintptr_t gameStatusMgrGlobal = 0;
	std::uintptr_t srcGlobal = 0;
};

struct FeatureFlags
{
	bool enableProcessExceptionTrace = false; // Stable profile: VEH stack scanning/file I/O is diagnostic-only.
	bool enableConnectRedirect = true;
	bool enableNetworkHooks = true;
	bool enableNetworkUnpackMsg100ConnIdPatch = true;
	bool enableNetworkFilterProtocolConnIdAlign = false;
	bool enableNetworkFilterProtocolCachedDstAlign = false;
	bool enableNetworkInnerDispatchPlainExpand = true;
	bool enableNetworkPackSerializerBodyRepair = false;
	bool enableNetworkSerializeBodyRepair = false;
	bool enableNetworkSpecial28346DecodeHook = false;
	bool enableKartRefitShadowCache = true; // 21932: publish Msg318 records beyond Network.dll's fixed 1500-entry cache.
	bool enablePacketFlowLog = false; // Avoid synchronous file I/O on every Pack/Unpack call.
	// 21932: preserve the valid-ACE-Feed conditional execution chain.
	bool enableTopKartAntiCheckerConditional = true;

	bool enablePlainExpandMsg100 = true;
	bool enableTopKart551CompactFeed = false;
	bool enableTopKart551ConsumerFallback = false;
	bool enableTopKart551HandlerImmediateRetry = false;
	bool enableTopKart551HandlerDelayedRetry = false;
	bool enableTopKartExitTraceProbe = false; // Stability baseline: this is observation-only Lua/UI tracing; legacy logs show TK+0x116DD0 AV after topkart_lua_dostring breadcrumbs.
	bool enableTopKartHandlerUiTrace = false; // Disabled: entry inline hooks on fixed Top-Kart RVAs caused startup/lobby crash.
	bool enableTopKartWorldBoundGuard = false; // Official/stability baseline: do not patch Top-Kart fixed RVAs during login.
	bool enableTopKartLuaTableGuard = false;
	bool enableTopKartChangeItemProbe = false;
	bool enableTopKartMapIdProbe = false;
	bool enableTopKartChangeItemForce130GateOpen = false;
	bool enableTopKartChangeItemForceAmModelId = false;
	bool enableTopKartChangeItemClearAmGlobalSeed = false;
	bool enableCandidate21210 = false;
	bool enableCandidate82To99 = false;
	bool enableVerboseProbeLogs = false; // Stable run: avoid patching the full change-item/model/effect trace surface during frame updates.
};

struct HookConfig
{
	const wchar_t* networkModuleName = L"Network.dll";
	const wchar_t* topKartModuleName = L"Top-Kart.dll";
#if defined(QQSPEED_VMP_PROTECTION)
	NetworkIdentity network;
#else
	NetworkOffsets network;
#endif
	TopKartOffsets topKart;
	FeatureFlags features;
};

inline constexpr HookConfig kDefaultHookConfig{};

inline constexpr const HookConfig& ActiveConfig()
{
	return kDefaultHookConfig;
}
}
