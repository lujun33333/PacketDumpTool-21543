#include "pch.h"

#include "network/network_hooks.h"

#include "logging/hook_log.h"
#include "network/code_patch.h"
#include "network/msg100_login_plain.h"
#if defined(QQSPEED_VMP_PROTECTION)
#include "network/protected_network_profile.h"
#endif
#include "network/serializer_body.h"
#include "topkart/topkart_551_feed.h"
#include "vmp_protection.h"

#include <intrin.h>
#include <vector>

namespace
{
constexpr std::size_t kClientMsgHeadSize = 14;
constexpr std::size_t kClientCsHeadSize = 18;
constexpr unsigned short kSerializeBodyDefaultLen = 0x7E90;
constexpr unsigned short kNotifySvrConfig3MsgId = 25066;
constexpr std::size_t kNotifySvrConfig3MinFrameRateOffset = 0xACB1;
constexpr std::size_t kNotifySvrConfig3MaxFrameRateOffset = 0xACB5;
constexpr std::size_t kNotifySvrConfig3FrameStepOffset = 0xACB9;
constexpr std::size_t kNotifySvrConfig3CurrentFrameRateOffset = 0xACBD;
constexpr std::size_t kNotifySvrConfig3ReportFrameRateValueOffset = 0xACC1;
constexpr std::size_t kNotifySvrConfig3FrameOpSpanOffset = 0xACC2;
constexpr std::size_t kNotifySvrConfig3VideoSwitchOffset = 0xACC4;
constexpr std::size_t kNotifySvrConfig3KartRefitCardTypeNumOffset = 0xACC5;
constexpr std::size_t kNotifySvrConfig3MinObjectLen = kNotifySvrConfig3KartRefitCardTypeNumOffset + 1;
constexpr std::uintptr_t kGameAppInterfaceGlobalOffset = 0x5E40D0;
constexpr std::size_t kGameAppNativeGateVtableOffset = 0x170;
constexpr std::size_t kGameAppNativeGateCodeDumpSize = 0x400;
constexpr std::size_t kGameAppNativeGateObjectDumpSize = 0x100;

INIT_ONCE g_performanceFrequencyOnce = INIT_ONCE_STATIC_INIT;
LARGE_INTEGER g_performanceFrequency = {};

BOOL CALLBACK InitializePerformanceFrequency(PINIT_ONCE, void*, void**)
{
	QueryPerformanceFrequency(&g_performanceFrequency);
	return TRUE;
}

unsigned long long ElapsedMicrosSince(const LARGE_INTEGER& started)
{
	InitOnceExecuteOnce(&g_performanceFrequencyOnce, &InitializePerformanceFrequency, nullptr, nullptr);
	LARGE_INTEGER finished = {};
	QueryPerformanceCounter(&finished);
	if (g_performanceFrequency.QuadPart <= 0 || finished.QuadPart < started.QuadPart) {
		return 0;
	}
	return static_cast<unsigned long long>(
		(finished.QuadPart - started.QuadPart) * 1000000ULL /
		static_cast<unsigned long long>(g_performanceFrequency.QuadPart));
}

#if defined(QQSPEED_VMP_PROTECTION)
using NetworkUnpackFn = qqspeed::network::ResolvedNetworkUnpackFn;
using NetworkPackFn = qqspeed::network::ResolvedNetworkPackFn;
using NetworkSerializeBodyFn = qqspeed::network::ResolvedNetworkSerializeBodyFn;
using NetworkSpecialDecodeFn = qqspeed::network::ResolvedNetworkSpecialDecodeFn;
using GameSocketFilterProtocolInfoFn = qqspeed::network::ResolvedFilterProtocolInfoFn;
using NetworkInnerDispatchFn = qqspeed::network::ResolvedNetworkInnerDispatchFn;
#else
using NetworkUnpackFn = int(__fastcall*)(
	const unsigned char*, unsigned int, const void*, const void*, void*, void*, unsigned char*, int*,
	unsigned char, unsigned char, void*, void*, int);
using NetworkPackFn = int(__fastcall*)(
	unsigned char*, int*, unsigned int, unsigned int, unsigned char*, int, const void*, void*,
	unsigned char, unsigned short, short, char, char, short, short, const void*, int, int, char);
using NetworkSerializeBodyFn = int(__fastcall*)(unsigned int, const void*, unsigned char*, unsigned short*);
using NetworkSpecialDecodeFn = int(__fastcall*)(void*, const void*, unsigned char*, int*, void*);
using GameSocketFilterProtocolInfoFn = unsigned char(__fastcall*)(
	void*, void*, unsigned short*, unsigned char*, int);
using NetworkInnerDispatchFn = int(__fastcall*)(
	short, const unsigned char*, int, void*, void*, void*, int, int, void*, int);
#endif

struct MsgHeadSnapshot
{
	unsigned short msgId = 0;
	short msgType = 0;
	int msgSeq = 0;
	unsigned char srcFe = 0;
	unsigned char dstFe = 0;
	short srcId = 0;
	short dstId = 0;
	bool valid = false;
};

struct CsHeadSnapshot
{
	int totalLength = 0;
	short version = 0;
	short dialogId = 0;
	int seq = 0;
	unsigned int uin = 0;
	unsigned char bodyFlag = 0;
	unsigned char optLength = 0;
	bool valid = false;
};

struct NotifySvrConfig3Snapshot
{
	int minFrameRate = 0;
	int maxFrameRate = 0;
	int frameStep = 0;
	int currentFrameRate = 0;
	unsigned char reportFrameRateValue = 0;
	unsigned short frameOpSpan = 0;
	unsigned char videoSwitch = 0;
	unsigned char kartRefitCardTypeNum = 0;
	bool readable = false;
};

std::uintptr_t g_networkBase = 0;
#if defined(QQSPEED_VMP_PROTECTION)
qqspeed::config::FeatureFlags g_features = {};
qqspeed::config::TopKartOffsets g_topKartOffsets = {};
const wchar_t* g_topKartModuleName = L"Top-Kart.dll";
std::uintptr_t g_gameSocketOnMyReceiveUnpackCallAddress = 0;
std::uintptr_t g_directInnerDispatchCallAddress = 0;
std::uintptr_t g_socketCachedDstIdStateOffset = 0;
#else
const qqspeed::config::HookConfig* g_config = nullptr;
#endif
NetworkUnpackFn g_originalUnpack = nullptr;
NetworkPackFn g_originalPack = nullptr;
NetworkSerializeBodyFn g_originalSerializeBody = nullptr;
NetworkSpecialDecodeFn g_originalSpecialDecode = nullptr;
GameSocketFilterProtocolInfoFn g_originalFilterProtocolInfo = nullptr;
NetworkInnerDispatchFn g_originalInnerDispatch = nullptr;
// 0 = not started, 1 = installing, 2 = installed exactly, 3 = failed/rolled back.
volatile LONG g_installState = 0;
volatile LONG g_msg103NativeGateDumpCount = 0;

const qqspeed::config::FeatureFlags& RuntimeFeatures()
{
#if defined(QQSPEED_VMP_PROTECTION)
	return g_features;
#else
	return g_config ? g_config->features : qqspeed::config::kDefaultHookConfig.features;
#endif
}

std::uintptr_t RuntimeSocketCachedDstIdStateOffset()
{
#if defined(QQSPEED_VMP_PROTECTION)
	return g_socketCachedDstIdStateOffset;
#else
	return g_config ? g_config->network.socketCachedDstIdStateOffset : 0;
#endif
}

bool IsGameSocketOnMyReceiveCall(std::uintptr_t callAddress, std::uintptr_t callRva)
{
#if defined(QQSPEED_VMP_PROTECTION)
	(void)callRva;
	return callAddress == g_gameSocketOnMyReceiveUnpackCallAddress;
#else
	(void)callAddress;
	return g_config && callRva == g_config->network.gameSocketOnMyReceiveUnpackCall;
#endif
}

bool IsDirectInnerDispatchCall(std::uintptr_t callAddress, std::uintptr_t callRva)
{
#if defined(QQSPEED_VMP_PROTECTION)
	(void)callRva;
	return callAddress == g_directInnerDispatchCallAddress;
#else
	(void)callAddress;
	return g_config && callRva == g_config->network.directInnerDispatchCall;
#endif
}

bool VerboseProbeLogsEnabled()
{
	return RuntimeFeatures().enableVerboseProbeLogs;
}

unsigned short Swap16(unsigned short value)
{
	return static_cast<unsigned short>((value >> 8) | (value << 8));
}

bool IsLoginChainMsg(unsigned short msgId)
{
	switch (msgId) {
	case 1:
	case 90:
	case 100:
	case 223:
	case 28346:
		return true;
	default:
		return false;
	}
}

bool IsNotifySvrConfig3Msg(unsigned short msgId)
{
	return msgId == kNotifySvrConfig3MsgId || Swap16(msgId) == kNotifySvrConfig3MsgId;
}

unsigned short NormalizeMsgId(unsigned short rawMsgId)
{
	if (IsLoginChainMsg(rawMsgId)) {
		return rawMsgId;
	}

	const unsigned short swapped = Swap16(rawMsgId);
	if (IsLoginChainMsg(swapped)) {
		return swapped;
	}

	return rawMsgId;
}

int ReadIntSafe(const int* ptr, int fallback)
{
	int value = fallback;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned short ReadU16Safe(const unsigned short* ptr, unsigned short fallback)
{
	unsigned short value = fallback;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned int ReadU32Safe(const void* ptr, unsigned int fallback)
{
	unsigned int value = fallback;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

void LogMemoryHex(const char* name, const void* source, std::size_t size)
{
	constexpr std::size_t kChunkSize = 32;
	constexpr char kHexDigits[] = "0123456789ABCDEF";
	const auto* sourceBytes = static_cast<const unsigned char*>(source);
	for (std::size_t offset = 0; offset < size; offset += kChunkSize) {
		const std::size_t chunkSize = (size - offset < kChunkSize) ? (size - offset) : kChunkSize;
		unsigned char bytes[kChunkSize] = {};
		if (!qqspeed::network::patch::SafeCopyRaw(bytes, sourceBytes + offset, chunkSize)) {
			qqspeed::logging::Logf(
				"msg103_native_gate %s read_failed offset=0x%llX size=0x%llX",
				name,
				static_cast<unsigned long long>(offset),
				static_cast<unsigned long long>(chunkSize));
			return;
		}

		char hex[kChunkSize * 2 + 1] = {};
		for (std::size_t i = 0; i < chunkSize; ++i) {
			hex[i * 2] = kHexDigits[bytes[i] >> 4];
			hex[i * 2 + 1] = kHexDigits[bytes[i] & 0x0F];
		}
		qqspeed::logging::Logf(
			"msg103_native_gate %s offset=0x%llX bytes=%s",
			name,
			static_cast<unsigned long long>(offset),
			hex);
	}
}

void DumpMsg103NativeGate()
{
	const LONG sample = InterlockedIncrement(&g_msg103NativeGateDumpCount);
	if (sample != 1 || !g_networkBase) {
		return;
	}

	std::uintptr_t gameAppInterface = 0;
	std::uintptr_t vtable = 0;
	std::uintptr_t gateTarget = 0;
	const bool interfaceRead = qqspeed::network::patch::SafeCopyRaw(
		&gameAppInterface,
		reinterpret_cast<const void*>(g_networkBase + kGameAppInterfaceGlobalOffset),
		sizeof(gameAppInterface));
	const bool vtableRead = interfaceRead && gameAppInterface &&
		qqspeed::network::patch::SafeCopyRaw(
			&vtable,
			reinterpret_cast<const void*>(gameAppInterface),
			sizeof(vtable));
	const bool targetRead = vtableRead && vtable &&
		qqspeed::network::patch::SafeCopyRaw(
			&gateTarget,
			reinterpret_cast<const void*>(vtable + kGameAppNativeGateVtableOffset),
			sizeof(gateTarget));

	const HMODULE gameAppModule = GetModuleHandleW(nullptr);
	MEMORY_BASIC_INFORMATION memoryInfo = {};
	const SIZE_T memoryInfoSize = targetRead && gateTarget
		? VirtualQuery(reinterpret_cast<const void*>(gateTarget), &memoryInfo, sizeof(memoryInfo))
		: 0;
	const std::uintptr_t gameAppBase = reinterpret_cast<std::uintptr_t>(gameAppModule);
	const unsigned long long targetOffset =
		gameAppBase && gateTarget >= gameAppBase
		? static_cast<unsigned long long>(gateTarget - gameAppBase)
		: 0;

	qqspeed::logging::Logf(
		"msg103_native_gate begin sample=%ld interfaceRead=%d vtableRead=%d targetRead=%d gameApp=%p interface=%p vtable=%p target=%p targetOffset=0x%llX regionBase=%p regionSize=0x%llX state=0x%lX protect=0x%lX type=0x%lX",
		sample,
		interfaceRead ? 1 : 0,
		vtableRead ? 1 : 0,
		targetRead ? 1 : 0,
		gameAppModule,
		reinterpret_cast<void*>(gameAppInterface),
		reinterpret_cast<void*>(vtable),
		reinterpret_cast<void*>(gateTarget),
		targetOffset,
		memoryInfoSize ? memoryInfo.BaseAddress : nullptr,
		static_cast<unsigned long long>(memoryInfoSize ? memoryInfo.RegionSize : 0),
		memoryInfoSize ? memoryInfo.State : 0,
		memoryInfoSize ? memoryInfo.Protect : 0,
		memoryInfoSize ? memoryInfo.Type : 0);

	if (targetRead && gateTarget) {
		LogMemoryHex(
			"code",
			reinterpret_cast<const void*>(gateTarget),
			kGameAppNativeGateCodeDumpSize);
	}
	if (interfaceRead && gameAppInterface) {
		LogMemoryHex(
			"object",
			reinterpret_cast<const void*>(gameAppInterface),
			kGameAppNativeGateObjectDumpSize);
	}
	qqspeed::logging::Logf("msg103_native_gate end sample=%ld", sample);
}

NotifySvrConfig3Snapshot ReadNotifySvrConfig3Snapshot(const void* outData, int outLen)
{
	NotifySvrConfig3Snapshot snapshot = {};
	if (!outData || outLen < static_cast<int>(kNotifySvrConfig3MinObjectLen)) {
		return snapshot;
	}

	const unsigned char* bytes = static_cast<const unsigned char*>(outData);
	const bool ok =
		qqspeed::network::patch::SafeCopyRaw(
			&snapshot.minFrameRate,
			bytes + kNotifySvrConfig3MinFrameRateOffset,
			sizeof(snapshot.minFrameRate)) &&
		qqspeed::network::patch::SafeCopyRaw(
			&snapshot.maxFrameRate,
			bytes + kNotifySvrConfig3MaxFrameRateOffset,
			sizeof(snapshot.maxFrameRate)) &&
		qqspeed::network::patch::SafeCopyRaw(
			&snapshot.frameStep,
			bytes + kNotifySvrConfig3FrameStepOffset,
			sizeof(snapshot.frameStep)) &&
		qqspeed::network::patch::SafeCopyRaw(
			&snapshot.currentFrameRate,
			bytes + kNotifySvrConfig3CurrentFrameRateOffset,
			sizeof(snapshot.currentFrameRate)) &&
		qqspeed::network::patch::SafeCopyRaw(
			&snapshot.reportFrameRateValue,
			bytes + kNotifySvrConfig3ReportFrameRateValueOffset,
			sizeof(snapshot.reportFrameRateValue)) &&
		qqspeed::network::patch::SafeCopyRaw(
			&snapshot.frameOpSpan,
			bytes + kNotifySvrConfig3FrameOpSpanOffset,
			sizeof(snapshot.frameOpSpan)) &&
		qqspeed::network::patch::SafeCopyRaw(
			&snapshot.videoSwitch,
			bytes + kNotifySvrConfig3VideoSwitchOffset,
			sizeof(snapshot.videoSwitch)) &&
		qqspeed::network::patch::SafeCopyRaw(
			&snapshot.kartRefitCardTypeNum,
			bytes + kNotifySvrConfig3KartRefitCardTypeNumOffset,
			sizeof(snapshot.kartRefitCardTypeNum));
	snapshot.readable = ok;
	return snapshot;
}

MsgHeadSnapshot ReadMsgHeadSnapshot(const void* msgHead)
{
	MsgHeadSnapshot snapshot = {};
	if (msgHead) {
		snapshot.valid = qqspeed::network::patch::SafeCopyRaw(&snapshot, msgHead, kClientMsgHeadSize);
	}
	return snapshot;
}

bool WriteMsgHeadSnapshot(void* msgHead, const MsgHeadSnapshot& snapshot)
{
	return msgHead && qqspeed::network::patch::SafeCopyRaw(msgHead, &snapshot, kClientMsgHeadSize);
}

CsHeadSnapshot ReadCsHeadSnapshot(const void* csHead)
{
	CsHeadSnapshot snapshot = {};
	if (csHead) {
		snapshot.valid = qqspeed::network::patch::SafeCopyRaw(&snapshot, csHead, kClientCsHeadSize);
	}
	return snapshot;
}

bool ReadObfuscatedU16(const void* state, unsigned short& value)
{
	value = 0;
	if (!state) {
		return false;
	}

	const unsigned char* bytes = static_cast<const unsigned char*>(state);
	unsigned char key[2] = {};
	std::uintptr_t encodedPtr = 0;
	unsigned short encoded = 0;
	if (!qqspeed::network::patch::SafeCopyRaw(key, bytes, sizeof(key)) ||
		!qqspeed::network::patch::SafeCopyRaw(&encodedPtr, bytes + 8, sizeof(encodedPtr)) ||
		!encodedPtr ||
		!qqspeed::network::patch::SafeCopyRaw(&encoded, reinterpret_cast<const void*>(encodedPtr), sizeof(encoded))) {
		return false;
	}

	const unsigned char encodedLow = static_cast<unsigned char>(encoded & 0xFF);
	const unsigned char encodedHigh = static_cast<unsigned char>((encoded >> 8) & 0xFF);
	const unsigned char low = static_cast<unsigned char>(key[0] ^ encodedHigh);
	const unsigned char high = static_cast<unsigned char>(key[1] ^ encodedLow);
	value = static_cast<unsigned short>((static_cast<unsigned short>(high) << 8) | low);
	return true;
}

bool ReadSocketCachedDstId(const void* socket, unsigned short& value)
{
	const std::uintptr_t offset = RuntimeSocketCachedDstIdStateOffset();
	if (!offset) {
		return false;
	}
	const unsigned char* socketBytes = static_cast<const unsigned char*>(socket);
	return ReadObfuscatedU16(
		socketBytes ? socketBytes + offset : nullptr,
		value);
}

bool ShouldAlignFilterProtocolDstId(unsigned short msgId)
{
	return msgId != 100;
}

const char* DescribeUnpackCallSite(std::uintptr_t callAddress, std::uintptr_t callRva)
{
	if (IsGameSocketOnMyReceiveCall(callAddress, callRva)) {
		return "GameSocket::OnMyReceive";
	}
	return "unknown";
}

std::uintptr_t AddressToRva(const void* address)
{
	const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
	if (!g_networkBase || value < g_networkBase) {
		return 0;
	}
	return value - g_networkBase;
}

std::uintptr_t GetCallSiteRvaFromReturnAddress(const void* returnAddress)
{
	const std::uintptr_t returnRva = AddressToRva(returnAddress);
	return returnRva >= 5 ? returnRva - 5 : 0;
}

std::uintptr_t GetCallSiteAddressFromReturnAddress(const void* returnAddress)
{
	const std::uintptr_t returnAddressValue = reinterpret_cast<std::uintptr_t>(returnAddress);
	return returnAddressValue >= 5 ? returnAddressValue - 5 : 0;
}

unsigned short ReadPacketMsgId(const unsigned char* packet, int packetLen)
{
	if (!packet || packetLen < 18) {
		return 0;
	}

	unsigned char optLen = 0;
	if (!qqspeed::network::patch::SafeCopyRaw(&optLen, packet + 17, sizeof(optLen))) {
		return 0;
	}

	const std::size_t msgHeadOffset = 18u + optLen;
	if (static_cast<std::size_t>(packetLen) < msgHeadOffset + sizeof(unsigned short)) {
		return 0;
	}

	unsigned short netMsgId = 0;
	if (!qqspeed::network::patch::SafeCopyRaw(&netMsgId, packet + msgHeadOffset, sizeof(netMsgId))) {
		return 0;
	}
	return Swap16(netMsgId);
}

#if defined(QQSPEED_VMP_PROTECTION)
struct Msg100UnpackApplyResult
{
	int finalResult = 0;
	bool forced = false;
	bool wroteHead = false;
};

QQSPEED_VMP_NOINLINE Msg100UnpackApplyResult ApplyDirectMsg100UnpackProtected(
	std::uintptr_t callSiteAddress,
	int originalResult,
	void* msgHead,
	const MsgHeadSnapshot& head,
	unsigned short msgId,
	int actualOutLen,
	const qqspeed::network::LoginPlainSnapshot& loginPlain)
{
	QQSPEED_VMP_BEGIN_ULTRA("Login.Msg100.UnpackApply");
	Msg100UnpackApplyResult apply = {};
	apply.finalResult = originalResult;
	if (g_features.enableNetworkUnpackMsg100ConnIdPatch &&
		originalResult == 0 &&
		callSiteAddress == g_gameSocketOnMyReceiveUnpackCallAddress &&
		head.valid &&
		msgId == 100 &&
		NormalizeMsgId(head.msgId) == msgId &&
		head.msgType == 1 &&
		actualOutLen >= qqspeed::network::kLoginPlainMinLen &&
		loginPlain.shapeOk)
	{
		MsgHeadSnapshot patchedHead = head;
		patchedHead.msgId = 100;
		patchedHead.msgType = 1;
		patchedHead.dstId = static_cast<short>(loginPlain.connId);
		apply.wroteHead = WriteMsgHeadSnapshot(msgHead, patchedHead);
		apply.finalResult = 1;
		apply.forced = true;
	}
	QQSPEED_VMP_END();
	return apply;
}
#endif

int __fastcall HookedUnpack(
	const unsigned char* packet,
	unsigned int packetLen,
	const void* key1,
	const void* key2,
	void* csHead,
	void* msgHead,
	unsigned char* outData,
	int* outLen,
	unsigned char arg9,
	unsigned char arg10,
	void* arg11,
	void* arg12,
	int arg13)
{
	void* returnAddress = _ReturnAddress();
	const std::uintptr_t callSiteAddress = GetCallSiteAddressFromReturnAddress(returnAddress);
	const std::uintptr_t callSiteRva = GetCallSiteRvaFromReturnAddress(returnAddress);
#if defined(QQSPEED_VMP_PROTECTION)
	int result = g_originalUnpack(packet, packetLen, key1, key2, csHead, msgHead, outData, outLen);
#else
	int result = g_originalUnpack(
		packet,
		packetLen,
		key1,
		key2,
		csHead,
		msgHead,
		outData,
		outLen,
		arg9,
		arg10,
		arg11,
		arg12,
		arg13);
#endif
	const int originalResult = result;

	MsgHeadSnapshot head = ReadMsgHeadSnapshot(msgHead);
	unsigned short msgId = NormalizeMsgId(head.msgId);
	const unsigned short packetMsgId = ReadPacketMsgId(packet, static_cast<int>(packetLen));
	if (!msgId) {
		msgId = packetMsgId;
	}

	const int actualOutLen = ReadIntSafe(outLen, -1);
	const qqspeed::network::LoginPlainSnapshot loginPlain =
		qqspeed::network::ReadLoginPlainSnapshot(outData, actualOutLen);
#if defined(QQSPEED_VMP_PROTECTION)
	Msg100UnpackApplyResult apply = {};
	apply.finalResult = result;
	if (msgId == 100) {
		apply = ApplyDirectMsg100UnpackProtected(
			callSiteAddress,
			originalResult,
			msgHead,
			head,
			msgId,
			actualOutLen,
			loginPlain);
	}
	result = apply.finalResult;
	const bool forcedAllowMsg100 = apply.forced;
	const bool wroteHead = apply.wroteHead;
#else
	const bool forcedAllowMsg100 =
		g_config &&
		g_config->features.enableNetworkUnpackMsg100ConnIdPatch &&
		originalResult == 0 &&
		callSiteRva == g_config->network.gameSocketOnMyReceiveUnpackCall &&
		head.valid &&
		msgId == 100 &&
		NormalizeMsgId(head.msgId) == msgId &&
		head.msgType == 1 &&
		actualOutLen >= qqspeed::network::kLoginPlainMinLen &&
		loginPlain.shapeOk;
	bool wroteHead = false;
	if (forcedAllowMsg100) {
		MsgHeadSnapshot patchedHead = head;
		patchedHead.msgId = 100;
		patchedHead.msgType = 1;
		patchedHead.dstId = static_cast<short>(loginPlain.connId);
		wroteHead = WriteMsgHeadSnapshot(msgHead, patchedHead);
		result = 1;
	}
#endif
	const bool logMsg100Probe =
		VerboseProbeLogsEnabled() &&
		IsGameSocketOnMyReceiveCall(callSiteAddress, callSiteRva) &&
		(msgId == 100 || packetMsgId == 100 || loginPlain.readable);

	if (forcedAllowMsg100) {
		qqspeed::logging::Logf(
			"network_unpack force_msg100 callRva=0x%llX caller=%s outLen=%d connId=%u headWrite=%d uin=%u identity=0x%08X",
			static_cast<unsigned long long>(callSiteRva),
			DescribeUnpackCallSite(callSiteAddress, callSiteRva),
			actualOutLen,
			static_cast<unsigned int>(loginPlain.connId),
			wroteHead ? 1 : 0,
			loginPlain.uin,
			loginPlain.identity);
	}

	if (logMsg100Probe) {
		const MsgHeadSnapshot finalHead = ReadMsgHeadSnapshot(msgHead);
		qqspeed::logging::Logf(
			"network_unpack msg100_probe callRva=0x%llX caller=%s originalResult=%d finalResult=%d abiArgs=8 packetMsg=%u rawHeadMsg=%u normMsg=%u headValid=%d headType=%d headDst=%d outLen=%d plainReadable=%d shapeOk=%d resultId=%u connId=%u uin=%u identity=0x%08X forced=%d headWrite=%d finalMsg=%u finalType=%d finalDst=%d",
			static_cast<unsigned long long>(callSiteRva),
			DescribeUnpackCallSite(callSiteAddress, callSiteRva),
			originalResult,
			result,
			static_cast<unsigned int>(packetMsgId),
			static_cast<unsigned int>(head.msgId),
			static_cast<unsigned int>(msgId),
			head.valid ? 1 : 0,
			static_cast<int>(head.msgType),
			static_cast<int>(head.dstId),
			actualOutLen,
			loginPlain.readable ? 1 : 0,
			loginPlain.shapeOk ? 1 : 0,
			static_cast<unsigned int>(loginPlain.resultId),
			static_cast<unsigned int>(loginPlain.connId),
			loginPlain.uin,
			loginPlain.identity,
			forcedAllowMsg100 ? 1 : 0,
			wroteHead ? 1 : 0,
			static_cast<unsigned int>(NormalizeMsgId(finalHead.msgId)),
			static_cast<int>(finalHead.msgType),
			static_cast<int>(finalHead.dstId));
	}

	if (RuntimeFeatures().enablePacketFlowLog) {
		const MsgHeadSnapshot finalHead = ReadMsgHeadSnapshot(msgHead);
		const CsHeadSnapshot finalCs = ReadCsHeadSnapshot(csHead);
		const unsigned short finalMsg = finalHead.valid ? NormalizeMsgId(finalHead.msgId) : msgId;
		qqspeed::logging::PacketFlowf(
		"RECV callRva=0x%llX caller=%s result=%d originalResult=%d packetLen=%u packetMsg=%u rawHeadMsg=%u swappedHeadMsg=%u msg=%u msgSeq=%d msgType=%d srcFe=%u dstFe=%u srcId=%d dstId=%d outLen=%d csValid=%d csSeq=%d csUin=%u bodyFlag=%u forced100=%d",
		static_cast<unsigned long long>(callSiteRva),
		DescribeUnpackCallSite(callSiteAddress, callSiteRva),
		result,
		originalResult,
		packetLen,
		static_cast<unsigned int>(packetMsgId),
		static_cast<unsigned int>(finalHead.msgId),
		static_cast<unsigned int>(Swap16(finalHead.msgId)),
		static_cast<unsigned int>(finalMsg),
		finalHead.msgSeq,
		static_cast<int>(finalHead.msgType),
		static_cast<unsigned int>(finalHead.srcFe),
		static_cast<unsigned int>(finalHead.dstFe),
		static_cast<int>(finalHead.srcId),
		static_cast<int>(finalHead.dstId),
		actualOutLen,
		finalCs.valid ? 1 : 0,
		finalCs.seq,
		finalCs.uin,
		static_cast<unsigned int>(finalCs.bodyFlag),
		forcedAllowMsg100 ? 1 : 0);
	}

	return result;
}

int __fastcall HookedPack(
	unsigned char* outPacket,
	int* outPacketLen,
	unsigned int seq,
	unsigned int cmd,
	unsigned char* srcObject,
	int arg6,
	const void* key,
	void* extraData,
	unsigned char extraLen,
	unsigned short msgId,
	short msgType,
	char flag1,
	char flag2,
	short arg14,
	short arg15,
	const void* plainBody,
	int plainBodyLen,
	int arg18,
	char arg19)
{
	void* returnAddress = _ReturnAddress();
	const std::uintptr_t callSiteRva = GetCallSiteRvaFromReturnAddress(returnAddress);
	const int beforeOutPacketLen = ReadIntSafe(outPacketLen, -1);
	const unsigned short normalizedMsgId = NormalizeMsgId(msgId);
	if (normalizedMsgId == 103) {
		DumpMsg103NativeGate();
	}
	int result = g_originalPack(outPacket, outPacketLen, seq, cmd, srcObject, arg6, key, extraData, extraLen,
		msgId, msgType, flag1, flag2, arg14, arg15, plainBody, plainBodyLen, arg18, arg19);
	const int originalResult = result;
	if (normalizedMsgId == 103) {
		qqspeed::logging::Logf(
			"msg103_native_gate pack_result result=%d outLenBefore=%d outLen=%d plainBodyLen=%d",
			result,
			beforeOutPacketLen,
			ReadIntSafe(outPacketLen, -1),
			plainBodyLen);
	}

	if (RuntimeFeatures().enableNetworkPackSerializerBodyRepair &&
		originalResult == 0 &&
		plainBody == nullptr &&
		plainBodyLen == 0 &&
		qqspeed::network::IsSimulatedSerializerBodyMsg(normalizedMsgId)) {
		std::vector<unsigned char> fallbackBody;
		int fallbackBodyLen = 0;
		if (qqspeed::network::BuildSimulatedSerializerBody(normalizedMsgId, srcObject, seq, fallbackBody, fallbackBodyLen)) {
			const int retryCapacity = beforeOutPacketLen > 0 ? beforeOutPacketLen : 20480;
			static const unsigned char kEmptyFallbackBody[1] = {};
			const void* fallbackPlainBody = fallbackBodyLen > 0 ? fallbackBody.data() : kEmptyFallbackBody;
			qqspeed::network::patch::SafeCopyRaw(outPacketLen, &retryCapacity, sizeof(retryCapacity));
			const int retryResult = g_originalPack(outPacket, outPacketLen, seq, cmd, srcObject, arg6, key, extraData, extraLen,
				msgId, msgType, flag1, flag2, arg14, arg15, fallbackPlainBody, fallbackBodyLen, arg18, arg19);
			const int retryOutPacketLen = ReadIntSafe(outPacketLen, -1);
			if (retryResult) {
				result = retryResult;
			}
			qqspeed::logging::Logf(
				"pack_repair msg=%u cmd=%u original=%d retry=%d fallbackLen=%d outLen=%d capacity=%d",
				static_cast<unsigned int>(normalizedMsgId),
				cmd,
				originalResult,
				retryResult,
				fallbackBodyLen,
				retryOutPacketLen,
				retryCapacity);
		}
		else {
			qqspeed::logging::Logf(
				"pack_repair build_failed msg=%u cmd=%u seq=%u srcObject=%p",
				static_cast<unsigned int>(normalizedMsgId),
				cmd,
				seq,
				srcObject);
		}
	}

	const int finalOutPacketLen = ReadIntSafe(outPacketLen, -1);
	if (RuntimeFeatures().enablePacketFlowLog) {
		qqspeed::logging::PacketFlowf(
		"SEND callRva=0x%llX result=%d originalResult=%d seq=%u cmd=%u msg=%u rawMsg=%u swappedMsg=%u msgType=%d outLenBefore=%d outLen=%d plainBodyLen=%d arg6=%d extraLen=%u flags=%d/%d arg14=%d arg15=%d arg18=%d arg19=%d srcObject=%p plainBody=%p",
		static_cast<unsigned long long>(callSiteRva),
		result,
		originalResult,
		seq,
		cmd,
		static_cast<unsigned int>(normalizedMsgId),
		static_cast<unsigned int>(msgId),
		static_cast<unsigned int>(Swap16(msgId)),
		static_cast<int>(msgType),
		beforeOutPacketLen,
		finalOutPacketLen,
		plainBodyLen,
		arg6,
		static_cast<unsigned int>(extraLen),
		static_cast<int>(flag1),
		static_cast<int>(flag2),
		static_cast<int>(arg14),
		static_cast<int>(arg15),
		arg18,
		static_cast<int>(arg19),
		srcObject,
		plainBody);
	}

	return result;
}

int __fastcall HookedSerializeBody(
	unsigned int msgId,
	const void* srcObject,
	unsigned char* outBody,
	unsigned short* outBodyLen,
	void* aux)
{
	(void)aux;
	int result = g_originalSerializeBody(msgId, srcObject, outBody, outBodyLen);
	const unsigned short normalizedMsgId = NormalizeMsgId(static_cast<unsigned short>(msgId));
	const unsigned short originalBodyLen = ReadU16Safe(outBodyLen, 0);

	if (RuntimeFeatures().enableNetworkSerializeBodyRepair &&
		result == 1 &&
		originalBodyLen == kSerializeBodyDefaultLen &&
		outBody &&
		outBodyLen &&
		qqspeed::network::IsSimulatedSerializerBodyMsg(normalizedMsgId)) {
		std::vector<unsigned char> schemaBody;
		int schemaBodyLen = 0;
		const unsigned int seqFallback = ReadU32Safe(srcObject, 0);
		if (qqspeed::network::BuildSimulatedSerializerBody(normalizedMsgId, srcObject, seqFallback, schemaBody, schemaBodyLen) &&
			schemaBodyLen >= 0 &&
			schemaBodyLen <= static_cast<int>(originalBodyLen)) {
			const unsigned short fixedBodyLen = static_cast<unsigned short>(schemaBodyLen);
			const bool bodyCopied =
				(schemaBodyLen == 0 || qqspeed::network::patch::SafeCopyRaw(outBody, schemaBody.data(), static_cast<std::size_t>(schemaBodyLen))) &&
				qqspeed::network::patch::SafeCopyRaw(outBodyLen, &fixedBodyLen, sizeof(fixedBodyLen));
			if (VerboseProbeLogsEnabled() || !bodyCopied) {
				qqspeed::logging::Logf(
					"serialize_repair msg=%u originalLen=%u bodyLen=%d copied=%d",
					static_cast<unsigned int>(normalizedMsgId),
					static_cast<unsigned int>(originalBodyLen),
					schemaBodyLen,
					bodyCopied ? 1 : 0);
			}
		}
		else {
			qqspeed::logging::Logf(
				"serialize_repair build_failed msg=%u originalLen=%u srcObject=%p",
				static_cast<unsigned int>(normalizedMsgId),
				static_cast<unsigned int>(originalBodyLen),
				srcObject);
		}
	}

	return result;
}

int __fastcall HookedSpecialDecode(
	void* msgHead,
	const void* csHead,
	unsigned char* outData,
	int* outLen,
	void* aux)
{
	MsgHeadSnapshot beforeHead = ReadMsgHeadSnapshot(msgHead);
	const int beforeOutLen = ReadIntSafe(outLen, -1);
	unsigned char mode = 0;
	unsigned short innerMsg = 0;
	unsigned short innerLen = 0;
	const bool modeSafe = outData && qqspeed::network::patch::SafeCopyRaw(&mode, outData + 0x7DD4, sizeof(mode));
	const bool innerMsgSafe = outData && qqspeed::network::patch::SafeCopyRaw(&innerMsg, outData + 0x08, sizeof(innerMsg));
	const bool innerLenSafe = outData && qqspeed::network::patch::SafeCopyRaw(&innerLen, outData + 0x0A, sizeof(innerLen));
	const bool innerRangeOk = innerLenSafe && beforeOutLen >= static_cast<int>(0x0C + innerLen);
	std::vector<unsigned char> preInner551Body;
	bool preInner551BodyCopied = false;
	if (RuntimeFeatures().enableTopKart551CompactFeed &&
		modeSafe &&
		mode == 1 &&
		innerMsgSafe &&
		innerMsg == 551 &&
		innerRangeOk &&
		innerLen > 0 &&
		outData) {
		preInner551Body.resize(static_cast<std::size_t>(innerLen));
		preInner551BodyCopied = qqspeed::network::patch::SafeCopyRaw(
			preInner551Body.data(),
			outData + 0x0C,
			preInner551Body.size());
		if (!preInner551BodyCopied) {
			preInner551Body.clear();
		}
	}

	int result = g_originalSpecialDecode(msgHead, csHead, outData, outLen, aux);
	const int originalResult = result;

	MsgHeadSnapshot afterHead = ReadMsgHeadSnapshot(msgHead);
	unsigned short afterMsg = NormalizeMsgId(afterHead.msgId);
	int afterOutLen = ReadIntSafe(outLen, -1);

	if (RuntimeFeatures().enableTopKart551CompactFeed &&
		modeSafe &&
		innerMsgSafe &&
		mode == 1 &&
		innerMsg == 551 &&
		innerRangeOk &&
		innerLen > 0 &&
		outData) {
		qqspeed::topkart::KartPhys551FeedResult feed = {};
#if defined(QQSPEED_VMP_PROTECTION)
		qqspeed::config::HookConfig topKartConfig = {};
		topKartConfig.topKartModuleName = g_topKartModuleName;
		topKartConfig.topKart = g_topKartOffsets;
		topKartConfig.features = g_features;
		HMODULE topKartModule = GetModuleHandleW(g_topKartModuleName);
		const qqspeed::config::HookConfig* feedConfig = &topKartConfig;
#else
		const qqspeed::config::HookConfig* feedConfig = g_config;
		HMODULE topKartModule = feedConfig ? GetModuleHandleW(feedConfig->topKartModuleName) : nullptr;
#endif
		const bool ok = feedConfig && qqspeed::topkart::FeedKartPhys551CompactBody(
			topKartModule,
			*feedConfig,
			preInner551BodyCopied ? preInner551Body.data() : nullptr,
			preInner551BodyCopied ? static_cast<int>(preInner551Body.size()) : 0,
			feed);
		if (ok) {
			result = 1;
		}
		qqspeed::logging::Logf(
			"topkart_551_feed ok=%d reason=%s originalResult=%d finalResult=%d beforeMsg=%u afterMsg=%u mode=%u innerLen=%u afterOutLen=%d copied=%d decoded=%d decodeResult=%u decodeException=%d offsetAfter=%u buildCalled=%d buildResult=%u buildException=%d runtimeHeaderBefore=%u runtimeHeaderAfter=%u handlerCalled=%d handlerResult=%lld handlerException=%d handlerExceptionCode=0x%08X handlerExceptionAddress=%p handlerExceptionRva=0x%llX handlerExceptionRip=%p handlerExceptionRipRva=0x%llX handlerExceptionInfo0=0x%llX handlerExceptionInfo1=%p retryImmediateCalled=%d retryImmediateResult=%lld retryImmediateException=%d retryImmediateExceptionCode=0x%08X retryImmediateExceptionAddress=%p retryImmediateExceptionRva=0x%llX retryImmediateExceptionRip=%p retryImmediateExceptionRipRva=0x%llX retryImmediateExceptionInfo0=0x%llX retryImmediateExceptionInfo1=%p retryImmediateHandlerEntryDelta=%ld retryImmediateNotifyLuaDelta=%ld retryImmediateSkipGateDelta=%ld retryImmediateEnterBranchDelta=%ld retryImmediateNxEnterDelta=%ld retryImmediateAsyncEnterDelta=%ld retryImmediateAsyncEnterBypassDelta=%ld retryImmediateAsyncEnterOriginalDelta=%ld delayedRetryScheduled=%d delayedRetryCreateFailed=%d delayedRetrySkippedActive=%d delayedRetryDelayMs=%u routeProbeAttempted=%d routeProbeInstalled=%d routeProbePatchedCalls=%llu handlerEntryDelta=%ld notifyLuaDelta=%ld skipGateDelta=%ld enterBranchDelta=%ld nxEnterDelta=%ld asyncEnterDelta=%ld asyncEnterBypassDelta=%ld asyncEnterOriginalDelta=%ld fallbackAttempted=%d fallbackReason=%s consumerReady=%d consumerCalled=%d consumerException=%d promiseContext=%d topKartBase=%p compactCache=%p runtimeCache=%p decodeFn=%p buildFn=%p handlerFn=%p consumerFn=%p raceContext=%p kartMgr=%p luaUi=%p gameStatusMgr=%p gameStatusApp=%p gameStatusScene=%p srcGlobal=%p",
			ok ? 1 : 0,
			feed.reason,
			originalResult,
			result,
			static_cast<unsigned int>(NormalizeMsgId(beforeHead.msgId)),
			static_cast<unsigned int>(afterMsg),
			static_cast<unsigned int>(mode),
			static_cast<unsigned int>(innerLen),
			afterOutLen,
			feed.copied ? 1 : 0,
			feed.decoded ? 1 : 0,
			static_cast<unsigned int>(feed.decodeResult),
			feed.decodeException ? 1 : 0,
			feed.offsetAfter,
			feed.buildCalled ? 1 : 0,
			static_cast<unsigned int>(feed.buildResult),
			feed.buildException ? 1 : 0,
			feed.runtimeHeaderBefore,
			feed.runtimeHeaderAfterBuild,
			feed.handlerCalled ? 1 : 0,
			static_cast<long long>(feed.handlerResult),
			feed.handlerException ? 1 : 0,
			feed.handlerExceptionCode,
			reinterpret_cast<void*>(feed.handlerExceptionAddress),
			static_cast<unsigned long long>(feed.handlerExceptionRva),
			reinterpret_cast<void*>(feed.handlerExceptionRip),
			static_cast<unsigned long long>(feed.handlerExceptionRipRva),
			static_cast<unsigned long long>(feed.handlerExceptionInfo0),
			reinterpret_cast<void*>(feed.handlerExceptionInfo1),
			feed.handlerRetryImmediate.called ? 1 : 0,
			static_cast<long long>(feed.handlerRetryImmediate.result),
			feed.handlerRetryImmediate.exception ? 1 : 0,
			feed.handlerRetryImmediate.exceptionCode,
			reinterpret_cast<void*>(feed.handlerRetryImmediate.exceptionAddress),
			static_cast<unsigned long long>(feed.handlerRetryImmediate.exceptionRva),
			reinterpret_cast<void*>(feed.handlerRetryImmediate.exceptionRip),
			static_cast<unsigned long long>(feed.handlerRetryImmediate.exceptionRipRva),
			static_cast<unsigned long long>(feed.handlerRetryImmediate.exceptionInfo0),
			reinterpret_cast<void*>(feed.handlerRetryImmediate.exceptionInfo1),
			feed.handlerRetryImmediate.handlerEntryDelta,
			feed.handlerRetryImmediate.notifyLuaDelta,
			feed.handlerRetryImmediate.skipGateDelta,
			feed.handlerRetryImmediate.enterBranchDelta,
			feed.handlerRetryImmediate.nxEnterDelta,
			feed.handlerRetryImmediate.asyncEnterDelta,
			feed.handlerRetryImmediate.asyncEnterBypassDelta,
			feed.handlerRetryImmediate.asyncEnterOriginalDelta,
			feed.handlerRetryDelayedScheduled ? 1 : 0,
			feed.handlerRetryDelayedCreateFailed ? 1 : 0,
			feed.handlerRetryDelayedSkippedActive ? 1 : 0,
			feed.handlerRetryDelayedDelayMs,
			feed.routeProbeInstallAttempted ? 1 : 0,
			feed.routeProbeInstalled ? 1 : 0,
			static_cast<unsigned long long>(feed.routeProbePatchedCalls),
			feed.handlerEntryDelta,
			feed.notifyLuaDelta,
			feed.skipGateDelta,
			feed.enterBranchDelta,
			feed.nxEnterDelta,
			feed.asyncEnterDelta,
			feed.asyncEnterBypassDelta,
			feed.asyncEnterOriginalDelta,
			feed.consumerFallbackAttempted ? 1 : 0,
			feed.consumerFallbackReason,
			feed.consumerContextReady ? 1 : 0,
			feed.consumerCalled ? 1 : 0,
			feed.consumerException ? 1 : 0,
			feed.promiseContextPresent ? 1 : 0,
			reinterpret_cast<void*>(feed.topKartBase),
			reinterpret_cast<void*>(feed.compactCache),
			reinterpret_cast<void*>(feed.runtimeCache),
			reinterpret_cast<void*>(feed.decodeFn),
			reinterpret_cast<void*>(feed.buildFn),
			reinterpret_cast<void*>(feed.handlerFn),
			reinterpret_cast<void*>(feed.consumerFn),
			reinterpret_cast<void*>(feed.raceContext),
			reinterpret_cast<void*>(feed.kartMgr),
			reinterpret_cast<void*>(feed.luaUi),
			reinterpret_cast<void*>(feed.gameStatusMgr),
			reinterpret_cast<void*>(feed.gameStatusApp),
			reinterpret_cast<void*>(feed.gameStatusScene),
			reinterpret_cast<void*>(feed.srcGlobalValue));
	}

	return result;
}

QQSPEED_VMP_NOINLINE unsigned char __fastcall HookedFilterProtocolInfo(
	void* socket,
	void* csHead,
	unsigned short* msgHead,
	unsigned char* plain,
	int plainLen)
{
	MsgHeadSnapshot beforeHead = ReadMsgHeadSnapshot(msgHead);
	const unsigned short beforeMsg = NormalizeMsgId(beforeHead.msgId);
	unsigned char result = g_originalFilterProtocolInfo(socket, csHead, msgHead, plain, plainLen);
	const unsigned char originalResult = result;
	QQSPEED_VMP_BEGIN_ULTRA("Login.Msg100_223.FilterProtocol");

	MsgHeadSnapshot afterHead = ReadMsgHeadSnapshot(msgHead);
	CsHeadSnapshot afterCs = ReadCsHeadSnapshot(csHead);
	unsigned short afterMsg = NormalizeMsgId(afterHead.msgId);
	const qqspeed::network::LoginPlainSnapshot loginAfter =
		qqspeed::network::ReadLoginPlainSnapshot(plain, plainLen);

	// Build323 intentionally returns 0 for the initial Msg100 and every
	// Msg223 page whose HasNext is 1.  It caches/appends those pages and only
	// publishes the merged Msg100 when the final HasNext=0 page arrives.
	// Never turn that 0 into 1 here: doing so exposes only the first 200 items.

	unsigned short cachedDstId = 0;
	const bool cachedDstIdValid = ReadSocketCachedDstId(socket, cachedDstId);
	if (VerboseProbeLogsEnabled() && (beforeMsg == 100 || afterMsg == 100 || loginAfter.readable)) {
		qqspeed::logging::Logf(
			"filter_protocol msg100_probe originalResult=%u finalResult=%u beforeMsg=%u beforeType=%d beforeDst=%d afterMsg=%u afterType=%d afterDst=%d csValid=%d bodyFlag=%u plainLen=%d plainReadable=%d shapeOk=%d resultId=%u connId=%u uin=%u identity=0x%08X cachedValid=%d cachedDst=%u",
			static_cast<unsigned int>(originalResult),
			static_cast<unsigned int>(result),
			static_cast<unsigned int>(beforeMsg),
			static_cast<int>(beforeHead.msgType),
			static_cast<int>(beforeHead.dstId),
			static_cast<unsigned int>(afterMsg),
			static_cast<int>(afterHead.msgType),
			static_cast<int>(afterHead.dstId),
			afterCs.valid ? 1 : 0,
			static_cast<unsigned int>(afterCs.bodyFlag),
			plainLen,
			loginAfter.readable ? 1 : 0,
			loginAfter.shapeOk ? 1 : 0,
			static_cast<unsigned int>(loginAfter.resultId),
			static_cast<unsigned int>(loginAfter.connId),
			loginAfter.uin,
			loginAfter.identity,
			cachedDstIdValid ? 1 : 0,
			static_cast<unsigned int>(cachedDstId));
	}
	bool alignedCachedDstId = false;
	if (RuntimeFeatures().enableNetworkFilterProtocolConnIdAlign &&
		RuntimeFeatures().enableNetworkFilterProtocolCachedDstAlign &&
		ShouldAlignFilterProtocolDstId(afterMsg) &&
		result != 0 &&
		afterCs.valid &&
		((afterCs.bodyFlag & 1) != 0) &&
		afterHead.valid &&
		cachedDstIdValid &&
		cachedDstId != 0 &&
		cachedDstId != static_cast<unsigned short>(afterHead.dstId)) {
		const short oldDstId = afterHead.dstId;
		MsgHeadSnapshot patchedHead = afterHead;
		patchedHead.dstId = static_cast<short>(cachedDstId);
		alignedCachedDstId = WriteMsgHeadSnapshot(msgHead, patchedHead);
		if (alignedCachedDstId) {
			afterHead = ReadMsgHeadSnapshot(msgHead);
			afterMsg = NormalizeMsgId(afterHead.msgId);
			if (VerboseProbeLogsEnabled()) {
				qqspeed::logging::Logf(
					"filter_protocol align_dst msg=%u oldDst=%d cachedDst=%u bodyFlag=%u",
					static_cast<unsigned int>(afterMsg),
					static_cast<int>(oldDstId),
					static_cast<unsigned int>(cachedDstId),
					static_cast<unsigned int>(afterCs.bodyFlag));
			}
		}
	}

	if (VerboseProbeLogsEnabled() && (beforeMsg == 100 || afterMsg == 100 || loginAfter.readable)) {
		const MsgHeadSnapshot finalHead = ReadMsgHeadSnapshot(msgHead);
		const unsigned short finalMsg = NormalizeMsgId(finalHead.msgId);
		const bool finalHeadConnOk =
			finalHead.valid &&
			loginAfter.connId != 0 &&
			static_cast<unsigned short>(finalHead.dstId) == loginAfter.connId;
		const bool cachedConnOk =
			cachedDstIdValid &&
			loginAfter.connId != 0 &&
			cachedDstId == loginAfter.connId;
		qqspeed::logging::Logf(
			"filter_protocol msg100_probe_final finalResult=%u finalMsg=%u finalType=%d finalDst=%d aligned=%d cachedValid=%d cachedDst=%u",
			static_cast<unsigned int>(result),
			static_cast<unsigned int>(finalMsg),
			static_cast<int>(finalHead.msgType),
			static_cast<int>(finalHead.dstId),
			alignedCachedDstId ? 1 : 0,
			cachedDstIdValid ? 1 : 0,
			static_cast<unsigned int>(cachedDstId));
		qqspeed::logging::Logf(
			"filter_protocol msg100_result ok=%d finalPass=%u finalMsg=%u finalType=%d finalDst=%d connId=%u headConnOk=%d cachedDst=%u cachedConnOk=%d plainShape=%d resultId=%u aligned=%d",
			(result != 0 && finalMsg == 100 && finalHeadConnOk) ? 1 : 0,
			static_cast<unsigned int>(result),
			static_cast<unsigned int>(finalMsg),
			static_cast<int>(finalHead.msgType),
			static_cast<int>(finalHead.dstId),
			static_cast<unsigned int>(loginAfter.connId),
			finalHeadConnOk ? 1 : 0,
			static_cast<unsigned int>(cachedDstId),
			cachedConnOk ? 1 : 0,
			loginAfter.shapeOk ? 1 : 0,
			static_cast<unsigned int>(loginAfter.resultId),
			alignedCachedDstId ? 1 : 0);
	}

	QQSPEED_VMP_END();
	return result;
}

int __fastcall HookedInnerDispatch(
	short msgId,
	const unsigned char* body,
	int bodyLen,
	void* outData,
	void* outLen,
	void* aux,
	int arg7
#if !defined(QQSPEED_VMP_PROTECTION)
	, int arg8,
	void* arg9,
	int arg10
#endif
	)
{
	void* returnAddress = _ReturnAddress();
	const std::uintptr_t callSiteAddress = GetCallSiteAddressFromReturnAddress(returnAddress);
	const std::uintptr_t callSiteRva = GetCallSiteRvaFromReturnAddress(returnAddress);
	const unsigned short normalizedMsgId = NormalizeMsgId(static_cast<unsigned short>(msgId));
	int result = g_originalInnerDispatch(
		msgId,
		body,
		bodyLen,
		outData,
		outLen,
		aux,
		arg7
#if !defined(QQSPEED_VMP_PROTECTION)
		, arg8,
		arg9,
		arg10
#endif
	);
	const int originalResult = result;

	if (IsNotifySvrConfig3Msg(static_cast<unsigned short>(msgId))) {
		const int actualOutLen = ReadIntSafe(static_cast<const int*>(outLen), -1);
		const NotifySvrConfig3Snapshot frameCfg = ReadNotifySvrConfig3Snapshot(outData, actualOutLen);
		qqspeed::logging::Logf(
			"inner_dispatch notify_svr_config3 callRva=0x%llX rawMsg=%u normalizedMsg=%u originalResult=%d bodyLen=%d outLen=%d readable=%d min=%d max=%d step=%d current=%d report=%u span=%u videoSwitch=%u kartRefitCardTypeNum=%u",
			static_cast<unsigned long long>(callSiteRva),
			static_cast<unsigned int>(static_cast<unsigned short>(msgId)),
			static_cast<unsigned int>(normalizedMsgId),
			originalResult,
			bodyLen,
			actualOutLen,
			frameCfg.readable ? 1 : 0,
			frameCfg.minFrameRate,
			frameCfg.maxFrameRate,
			frameCfg.frameStep,
			frameCfg.currentFrameRate,
			static_cast<unsigned int>(frameCfg.reportFrameRateValue),
			static_cast<unsigned int>(frameCfg.frameOpSpan),
			static_cast<unsigned int>(frameCfg.videoSwitch),
			static_cast<unsigned int>(frameCfg.kartRefitCardTypeNum));
	}

	if (RuntimeFeatures().enableNetworkInnerDispatchPlainExpand &&
		RuntimeFeatures().enablePlainExpandMsg100 &&
		originalResult == 0 &&
		IsDirectInnerDispatchCall(callSiteAddress, callSiteRva) &&
		normalizedMsgId == 100 &&
		body &&
		bodyLen > 0 &&
		outData &&
		outLen) {
		qqspeed::network::Msg100ExpandSnapshot expand = {};
		LARGE_INTEGER expandStarted = {};
		QueryPerformanceCounter(&expandStarted);
		const bool expanded = qqspeed::network::TryExpandDirectMsg100LoginPlain(body, bodyLen, outData, outLen, expand);
		const unsigned long long expandUs = ElapsedMicrosSince(expandStarted);
		if (expanded) {
			result = 1;
			const int actualOutLen = ReadIntSafe(static_cast<const int*>(outLen), -1);
			qqspeed::logging::Logf(
				"inner_dispatch expand_msg100 ok callRva=0x%llX originalResult=%d bodyLen=%d streamLen=%u outLen=%d expandUs=%llu resultId=%u keyLen=%u connId=%u uin=%u identity=0x%08X loginType=%u gameSvrIdentity=0x%08X items=%u/%u tasks=%u maps=%u/%u pets=%u overdue=%u hasMore=%u curSvrId=%u finalStreamPos=0x%llX reason=%s",
				static_cast<unsigned long long>(callSiteRva),
				originalResult,
				bodyLen,
				static_cast<unsigned int>(expand.streamLen),
				actualOutLen,
				expandUs,
				static_cast<unsigned int>(expand.resultId),
				static_cast<unsigned int>(expand.keyLen),
				static_cast<unsigned int>(expand.connId),
				expand.uin,
				expand.identity,
				expand.loginType,
				expand.gameSvrIdentity,
				static_cast<unsigned int>(expand.itemWritten),
				static_cast<unsigned int>(expand.itemNum),
				static_cast<unsigned int>(expand.taskNum),
				static_cast<unsigned int>(expand.mapWritten),
				static_cast<unsigned int>(expand.mapNum),
				static_cast<unsigned int>(expand.petNum),
				static_cast<unsigned int>(expand.overdueNum),
				static_cast<unsigned int>(expand.hasMoreInfo),
				static_cast<unsigned int>(expand.curSvrId),
				static_cast<unsigned long long>(expand.finalStreamPos),
				expand.reason);
		}
		else if (expand.attempted) {
			qqspeed::logging::Logf(
				"inner_dispatch expand_msg100 failed callRva=0x%llX originalResult=%d bodyLen=%d streamLen=%u expandUs=%llu resultId=%u keyLen=%u connId=%u uin=%u identity=0x%08X finalStreamPos=0x%llX reason=%s",
				static_cast<unsigned long long>(callSiteRva),
				originalResult,
				bodyLen,
				static_cast<unsigned int>(expand.streamLen),
				expandUs,
				static_cast<unsigned int>(expand.resultId),
				static_cast<unsigned int>(expand.keyLen),
				static_cast<unsigned int>(expand.connId),
				expand.uin,
				expand.identity,
				static_cast<unsigned long long>(expand.finalStreamPos),
				expand.reason);
		}
	}
	return result;
}

#if defined(QQSPEED_VMP_PROTECTION)
template <std::size_t Capacity>
std::size_t PatchResolvedCalls(
	std::uintptr_t base,
	const qqspeed::network::ResolvedAddressList<Capacity>& list,
	std::uintptr_t targetAddress,
	void* detour,
	const char* name)
{
	return qqspeed::network::patch::PatchResolvedDirectCalls(
		base,
		list.values.data(),
		list.count,
		targetAddress,
		detour,
		name);
}
#else
template <std::size_t Capacity>
std::size_t PatchConfiguredCalls(
	std::uintptr_t base,
	const qqspeed::config::RvaList<Capacity>& list,
	std::uintptr_t targetRva,
	void* detour,
	const char* name)
{
	return qqspeed::network::patch::PatchDirectCalls(
		base,
		list.values.data(),
		list.count,
		targetRva,
		detour,
		name);
}
#endif
}

namespace qqspeed::network
{
#if defined(QQSPEED_VMP_PROTECTION)
bool InstallNetworkHooks(
	HMODULE networkModule,
	const config::HookConfig& config,
	const ResolvedNetworkProfile& profile)
{
	if (!networkModule ||
		!profile.unpackFunction ||
		!profile.packFunction ||
		!profile.serializeBodyFunction ||
		!profile.specialDecodeFunction ||
		!profile.filterProtocolInfoFunction ||
		!profile.innerDispatchFunction) {
		return false;
	}
	const LONG previousState = InterlockedCompareExchange(&g_installState, 1, 0);
	if (previousState != 0) {
		return previousState == 2;
	}

	const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(networkModule);
	g_networkBase = base;
	g_features = config.features;
	g_topKartOffsets = config.topKart;
	g_topKartModuleName = config.topKartModuleName;
	g_gameSocketOnMyReceiveUnpackCallAddress = profile.gameSocketOnMyReceiveUnpackCallAddress;
	g_directInnerDispatchCallAddress = profile.directInnerDispatchCallAddress;
	g_socketCachedDstIdStateOffset = profile.socketCachedDstIdStateOffset;
	g_originalUnpack = profile.unpackFunction;
	g_originalPack = profile.packFunction;
	g_originalSerializeBody = profile.serializeBodyFunction;
	g_originalSpecialDecode = profile.specialDecodeFunction;
	g_originalFilterProtocolInfo = profile.filterProtocolInfoFunction;
	g_originalInnerDispatch = profile.innerDispatchFunction;

	std::size_t patchedUnpack = 0;
	std::size_t patchedPack = 0;
	std::size_t patchedSerialize = 0;
	std::size_t patchedSpecialDecode = 0;
	std::size_t patchedFilter = 0;
	std::size_t patchedInner = 0;

	if (config.features.enableNetworkUnpackMsg100ConnIdPatch) {
		patchedUnpack = PatchResolvedCalls(
			base,
			profile.unpackCallSites,
			reinterpret_cast<std::uintptr_t>(profile.unpackFunction),
			reinterpret_cast<void*>(&HookedUnpack),
			"CEncryptUtil::Unpack");
	}
	if (config.features.enableNetworkPackSerializerBodyRepair || config.features.enablePacketFlowLog) {
		patchedPack = PatchResolvedCalls(
			base,
			profile.packCallSites,
			reinterpret_cast<std::uintptr_t>(profile.packFunction),
			reinterpret_cast<void*>(&HookedPack),
			"CEncryptUtil::Pack");
	}
	if (config.features.enableNetworkSerializeBodyRepair) {
		patchedSerialize = PatchResolvedCalls(
			base,
			profile.serializeBodyCallSites,
			reinterpret_cast<std::uintptr_t>(profile.serializeBodyFunction),
			reinterpret_cast<void*>(&HookedSerializeBody),
			"Network body serializer");
	}
	if (config.features.enableNetworkSpecial28346DecodeHook) {
		patchedSpecialDecode = PatchResolvedCalls(
			base,
			profile.specialDecodeCallSites,
			reinterpret_cast<std::uintptr_t>(profile.specialDecodeFunction),
			reinterpret_cast<void*>(&HookedSpecialDecode),
			"Network special 28346 decode");
	}
	if (config.features.enableNetworkFilterProtocolConnIdAlign) {
		patchedFilter = PatchResolvedCalls(
			base,
			profile.filterProtocolInfoCallSites,
			reinterpret_cast<std::uintptr_t>(profile.filterProtocolInfoFunction),
			reinterpret_cast<void*>(&HookedFilterProtocolInfo),
			"CGameSocket::FilterProtocolInfo");
	}
	if (config.features.enableNetworkInnerDispatchPlainExpand) {
		patchedInner = PatchResolvedCalls(
			base,
			profile.innerDispatchCallSites,
			reinterpret_cast<std::uintptr_t>(profile.innerDispatchFunction),
			reinterpret_cast<void*>(&HookedInnerDispatch),
			"Network inner dispatch");
	}

	const auto hasExactCount = [](bool enabled, std::size_t actual, std::size_t expected) {
		return enabled ? (expected != 0 && actual == expected) : actual == 0;
	};
	const bool installPack =
		config.features.enableNetworkPackSerializerBodyRepair || config.features.enablePacketFlowLog;
	const bool installedExactly =
		hasExactCount(config.features.enableNetworkUnpackMsg100ConnIdPatch, patchedUnpack, profile.unpackCallSites.count) &&
		hasExactCount(installPack, patchedPack, profile.packCallSites.count) &&
		hasExactCount(config.features.enableNetworkSerializeBodyRepair, patchedSerialize, profile.serializeBodyCallSites.count) &&
		hasExactCount(config.features.enableNetworkSpecial28346DecodeHook, patchedSpecialDecode, profile.specialDecodeCallSites.count) &&
		hasExactCount(config.features.enableNetworkFilterProtocolConnIdAlign, patchedFilter, profile.filterProtocolInfoCallSites.count) &&
		hasExactCount(config.features.enableNetworkInnerDispatchPlainExpand, patchedInner, profile.innerDispatchCallSites.count);

	qqspeed::logging::Logf(
		"network_hooks install_result exact=%d base=%p unpack=%llu/%llu pack=%llu/%llu serialize=%llu/%llu special=%llu/%llu filter=%llu/%llu inner=%llu/%llu",
		installedExactly ? 1 : 0,
		networkModule,
		static_cast<unsigned long long>(patchedUnpack),
		static_cast<unsigned long long>(profile.unpackCallSites.count),
		static_cast<unsigned long long>(patchedPack),
		static_cast<unsigned long long>(profile.packCallSites.count),
		static_cast<unsigned long long>(patchedSerialize),
		static_cast<unsigned long long>(profile.serializeBodyCallSites.count),
		static_cast<unsigned long long>(patchedSpecialDecode),
		static_cast<unsigned long long>(profile.specialDecodeCallSites.count),
		static_cast<unsigned long long>(patchedFilter),
		static_cast<unsigned long long>(profile.filterProtocolInfoCallSites.count),
		static_cast<unsigned long long>(patchedInner),
		static_cast<unsigned long long>(profile.innerDispatchCallSites.count));
	if (!installedExactly) {
		const std::size_t rolledBack = qqspeed::network::patch::RollbackAllCallSites();
		qqspeed::logging::Logf(
			"network_hooks install_failed rollback=%llu",
			static_cast<unsigned long long>(rolledBack));
	}
	InterlockedExchange(&g_installState, installedExactly ? 2 : 3);
	return installedExactly;
}
#else
bool InstallNetworkHooks(HMODULE networkModule, const config::HookConfig& config)
{
	if (!networkModule) {
		return false;
	}
	const LONG previousState = InterlockedCompareExchange(&g_installState, 1, 0);
	if (previousState != 0) {
		return previousState == 2;
	}

	g_config = &config;
	const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(networkModule);
	g_networkBase = base;
	g_originalUnpack = reinterpret_cast<NetworkUnpackFn>(base + config.network.unpack);
	g_originalPack = reinterpret_cast<NetworkPackFn>(base + config.network.pack);
	g_originalSerializeBody = reinterpret_cast<NetworkSerializeBodyFn>(base + config.network.serializeBody);
	g_originalSpecialDecode = reinterpret_cast<NetworkSpecialDecodeFn>(base + config.network.special28346Decode);
	g_originalFilterProtocolInfo = reinterpret_cast<GameSocketFilterProtocolInfoFn>(base + config.network.filterProtocolInfo);
	g_originalInnerDispatch = reinterpret_cast<NetworkInnerDispatchFn>(base + config.network.innerDispatch);

	std::size_t patchedUnpack = 0;
	std::size_t patchedPack = 0;
	std::size_t patchedSerialize = 0;
	std::size_t patchedSpecialDecode = 0;
	std::size_t patchedFilter = 0;
	std::size_t patchedInner = 0;

	if (config.features.enableNetworkUnpackMsg100ConnIdPatch) {
		patchedUnpack = PatchConfiguredCalls(
			base,
			config.network.unpackCallSites,
			config.network.unpack,
			reinterpret_cast<void*>(&HookedUnpack),
			"CEncryptUtil::Unpack");
	}
	if (config.features.enableNetworkPackSerializerBodyRepair || config.features.enablePacketFlowLog) {
		patchedPack = PatchConfiguredCalls(
			base,
			config.network.packCallSites,
			config.network.pack,
			reinterpret_cast<void*>(&HookedPack),
			"CEncryptUtil::Pack");
	}
	if (config.features.enableNetworkSerializeBodyRepair) {
		patchedSerialize = PatchConfiguredCalls(
			base,
			config.network.serializeBodyCallSites,
			config.network.serializeBody,
			reinterpret_cast<void*>(&HookedSerializeBody),
			"Network body serializer");
	}
	if (config.features.enableNetworkSpecial28346DecodeHook) {
		patchedSpecialDecode = PatchConfiguredCalls(
			base,
			config.network.special28346DecodeCallSites,
			config.network.special28346Decode,
			reinterpret_cast<void*>(&HookedSpecialDecode),
			"Network special 28346 decode");
	}
	if (config.features.enableNetworkFilterProtocolConnIdAlign) {
		patchedFilter = PatchConfiguredCalls(
			base,
			config.network.filterProtocolInfoCallSites,
			config.network.filterProtocolInfo,
			reinterpret_cast<void*>(&HookedFilterProtocolInfo),
			"CGameSocket::FilterProtocolInfo");
	}
	if (config.features.enableNetworkInnerDispatchPlainExpand) {
		patchedInner = PatchConfiguredCalls(
			base,
			config.network.innerDispatchCallSites,
			config.network.innerDispatch,
			reinterpret_cast<void*>(&HookedInnerDispatch),
			"Network inner dispatch");
	}

	const auto hasExactCount = [](bool enabled, std::size_t actual, std::size_t expected) {
		return enabled ? (expected != 0 && actual == expected) : actual == 0;
	};
	const bool installPack =
		config.features.enableNetworkPackSerializerBodyRepair || config.features.enablePacketFlowLog;
	const bool installedExactly =
		hasExactCount(config.features.enableNetworkUnpackMsg100ConnIdPatch, patchedUnpack, config.network.unpackCallSites.count) &&
		hasExactCount(installPack, patchedPack, config.network.packCallSites.count) &&
		hasExactCount(config.features.enableNetworkSerializeBodyRepair, patchedSerialize, config.network.serializeBodyCallSites.count) &&
		hasExactCount(config.features.enableNetworkSpecial28346DecodeHook, patchedSpecialDecode, config.network.special28346DecodeCallSites.count) &&
		hasExactCount(config.features.enableNetworkFilterProtocolConnIdAlign, patchedFilter, config.network.filterProtocolInfoCallSites.count) &&
		hasExactCount(config.features.enableNetworkInnerDispatchPlainExpand, patchedInner, config.network.innerDispatchCallSites.count);

	qqspeed::logging::Logf(
		"network_hooks install_result exact=%d base=%p unpack=%llu/%llu pack=%llu/%llu serialize=%llu/%llu special=%llu/%llu filter=%llu/%llu inner=%llu/%llu",
		installedExactly ? 1 : 0,
		networkModule,
		static_cast<unsigned long long>(patchedUnpack),
		static_cast<unsigned long long>(config.network.unpackCallSites.count),
		static_cast<unsigned long long>(patchedPack),
		static_cast<unsigned long long>(config.network.packCallSites.count),
		static_cast<unsigned long long>(patchedSerialize),
		static_cast<unsigned long long>(config.network.serializeBodyCallSites.count),
		static_cast<unsigned long long>(patchedSpecialDecode),
		static_cast<unsigned long long>(config.network.special28346DecodeCallSites.count),
		static_cast<unsigned long long>(patchedFilter),
		static_cast<unsigned long long>(config.network.filterProtocolInfoCallSites.count),
		static_cast<unsigned long long>(patchedInner),
		static_cast<unsigned long long>(config.network.innerDispatchCallSites.count));
	if (!installedExactly) {
		const std::size_t rolledBack = qqspeed::network::patch::RollbackAllCallSites();
		qqspeed::logging::Logf(
			"network_hooks install_failed rollback=%llu",
			static_cast<unsigned long long>(rolledBack));
	}
	InterlockedExchange(&g_installState, installedExactly ? 2 : 3);
	return installedExactly;
}
#endif
}
