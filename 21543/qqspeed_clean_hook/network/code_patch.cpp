#include "pch.h"

#include "network/code_patch.h"

#include "logging/hook_log.h"

#include <cstring>
#include <limits>
#include <mutex>

namespace
{
constexpr std::size_t kRelayPageSize = 0x10000;
constexpr std::size_t kRelayStubSize = 14;
constexpr std::size_t kMaxPatchBytes = 16;

struct CallSitePatch
{
	void* Address = nullptr;
	unsigned char Original[kMaxPatchBytes] = {};
	std::size_t Size = 0;
	bool Installed = false;
};

CallSitePatch g_callPatches[1024] = {};
std::size_t g_callPatchCount = 0;
unsigned char* g_relayPage = nullptr;
std::size_t g_relayOffset = 0;
std::mutex g_patchMutex;

bool IsRel32Reachable(std::uintptr_t source, std::uintptr_t destination)
{
	const std::int64_t delta =
		static_cast<std::int64_t>(destination) - static_cast<std::int64_t>(source + 5);
	return delta >= (std::numeric_limits<std::int32_t>::min)() &&
		delta <= (std::numeric_limits<std::int32_t>::max)();
}

unsigned char* AllocateRelayPageNear(std::uintptr_t base)
{
	SYSTEM_INFO sysInfo = {};
	GetSystemInfo(&sysInfo);
	const std::uintptr_t granularity = sysInfo.dwAllocationGranularity ? sysInfo.dwAllocationGranularity : 0x10000;
	const std::uintptr_t minAddress = reinterpret_cast<std::uintptr_t>(sysInfo.lpMinimumApplicationAddress);
	const std::uintptr_t maxAddress = reinterpret_cast<std::uintptr_t>(sysInfo.lpMaximumApplicationAddress);

	for (std::uintptr_t distance = 0; distance < 0x70000000ULL; distance += granularity) {
		const std::uintptr_t down = base > distance ? (base - distance) & ~(granularity - 1) : 0;
		if (down >= minAddress && IsRel32Reachable(base, down)) {
			void* mem = VirtualAlloc(reinterpret_cast<void*>(down), granularity, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (mem) {
				return static_cast<unsigned char*>(mem);
			}
		}

		const std::uintptr_t up = (base + distance + granularity - 1) & ~(granularity - 1);
		if (up >= base && up <= maxAddress && IsRel32Reachable(base, up)) {
			void* mem = VirtualAlloc(reinterpret_cast<void*>(up), granularity, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (mem) {
				return static_cast<unsigned char*>(mem);
			}
		}
	}

	return nullptr;
}

void* CreateRelayStub(std::uintptr_t moduleBase, void* detour, const char* name)
{
	if (!g_relayPage || !IsRel32Reachable(moduleBase, reinterpret_cast<std::uintptr_t>(g_relayPage))) {
		g_relayPage = AllocateRelayPageNear(moduleBase);
		g_relayOffset = 0;
		if (!g_relayPage) {
			qqspeed::logging::Logf("Network relay allocation failed base=%p", reinterpret_cast<void*>(moduleBase));
			return nullptr;
		}
		qqspeed::logging::Logf("Network relay page=%p base=%p", g_relayPage, reinterpret_cast<void*>(moduleBase));
	}

	if (g_relayOffset + kRelayStubSize > kRelayPageSize) {
		qqspeed::logging::Logf("Network relay page exhausted for %s", name ? name : "unknown");
		return nullptr;
	}

	unsigned char* stub = g_relayPage + g_relayOffset;
	g_relayOffset += kRelayStubSize;

	stub[0] = 0xFF;
	stub[1] = 0x25;
	stub[2] = 0x00;
	stub[3] = 0x00;
	stub[4] = 0x00;
	stub[5] = 0x00;
	*reinterpret_cast<std::uintptr_t*>(stub + 6) = reinterpret_cast<std::uintptr_t>(detour);
	FlushInstructionCache(GetCurrentProcess(), stub, kRelayStubSize);

	qqspeed::logging::Logf("Network %s relay stub=%p detour=%p", name ? name : "unknown", stub, detour);
	return stub;
}

bool PatchResolvedDirectCall(
	std::uintptr_t callAddress,
	std::uintptr_t expectedTarget,
	void* relay,
	const char* name)
{
	if (g_callPatchCount >= _countof(g_callPatches)) {
		qqspeed::logging::Logf("Network %s call patch table full", name ? name : "unknown");
		return false;
	}

	unsigned char* call = reinterpret_cast<unsigned char*>(callAddress);
	unsigned char original[5] = {};
	if (!qqspeed::network::patch::SafeCopyRaw(original, call, sizeof(original)) || original[0] != 0xE8) {
		qqspeed::logging::Logf(
			"Network %s call-site invalid address=%p first=%02X",
			name ? name : "unknown",
			call,
			original[0]);
		return false;
	}

	std::int32_t oldRel = 0;
	memcpy(&oldRel, original + 1, sizeof(oldRel));
	const std::uintptr_t oldTarget = reinterpret_cast<std::uintptr_t>(call + 5 + oldRel);
	if (oldTarget != expectedTarget) {
		qqspeed::logging::Logf(
			"Network %s call-site target mismatch address=%p got=%p expected=%p",
			name ? name : "unknown",
			call,
			reinterpret_cast<void*>(oldTarget),
			reinterpret_cast<void*>(expectedTarget));
		return false;
	}

	const std::uintptr_t relayAddress = reinterpret_cast<std::uintptr_t>(relay);
	if (!IsRel32Reachable(reinterpret_cast<std::uintptr_t>(call), relayAddress)) {
		qqspeed::logging::Logf("Network %s relay out of rel32 range call=%p relay=%p", name ? name : "unknown", call, relay);
		return false;
	}

	const std::int64_t delta =
		static_cast<std::int64_t>(relayAddress) - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(call) + 5);
	const std::int32_t newRel = static_cast<std::int32_t>(delta);
	unsigned char patch[5] = { 0xE8, 0, 0, 0, 0 };
	memcpy(patch + 1, &newRel, sizeof(newRel));

	DWORD oldProtect = 0;
	if (!VirtualProtect(call, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		qqspeed::logging::Logf("Network %s call protect failed address=%p error=%lu",
			name ? name : "unknown",
			call,
			GetLastError());
		return false;
	}

	memcpy(call, patch, sizeof(patch));
	DWORD ignoredProtect = 0;
	VirtualProtect(call, sizeof(patch), oldProtect, &ignoredProtect);
	FlushInstructionCache(GetCurrentProcess(), call, sizeof(patch));

	CallSitePatch& saved = g_callPatches[g_callPatchCount++];
	saved.Address = call;
	memcpy(saved.Original, original, sizeof(original));
	saved.Size = sizeof(original);
	saved.Installed = true;
	qqspeed::logging::Logf(
		"Network %s call patched address=%p relay=%p",
		name ? name : "unknown",
		call,
		relay);
	return true;
}

bool PatchCallSiteWithRelay(
	std::uintptr_t moduleBase,
	std::uintptr_t callRva,
	const unsigned char* expectedBytes,
	std::size_t patchSize,
	void* relay,
	const char* name)
{
	if (g_callPatchCount >= _countof(g_callPatches)) {
		qqspeed::logging::Logf("Network %s callsite patch table full", name ? name : "unknown");
		return false;
	}
	if (!expectedBytes || patchSize < 5 || patchSize > kMaxPatchBytes) {
		qqspeed::logging::Logf(
			"Network %s callsite patch bad size rva=0x%llX size=%llu",
			name ? name : "unknown",
			static_cast<unsigned long long>(callRva),
			static_cast<unsigned long long>(patchSize));
		return false;
	}

	unsigned char* call = reinterpret_cast<unsigned char*>(moduleBase + callRva);
	unsigned char original[kMaxPatchBytes] = {};
	if (!qqspeed::network::patch::SafeCopyRaw(original, call, patchSize) ||
		memcmp(original, expectedBytes, patchSize) != 0) {
		qqspeed::logging::Logf(
			"Network %s callsite bytes mismatch rva=0x%llX address=%p got=%02X %02X %02X %02X %02X %02X",
			name ? name : "unknown",
			static_cast<unsigned long long>(callRva),
			call,
			patchSize > 0 ? original[0] : 0,
			patchSize > 1 ? original[1] : 0,
			patchSize > 2 ? original[2] : 0,
			patchSize > 3 ? original[3] : 0,
			patchSize > 4 ? original[4] : 0,
			patchSize > 5 ? original[5] : 0);
		return false;
	}

	const std::uintptr_t relayAddress = reinterpret_cast<std::uintptr_t>(relay);
	if (!IsRel32Reachable(reinterpret_cast<std::uintptr_t>(call), relayAddress)) {
		qqspeed::logging::Logf("Network %s relay out of rel32 range call=%p relay=%p", name ? name : "unknown", call, relay);
		return false;
	}

	unsigned char patch[kMaxPatchBytes] = {};
	memset(patch, 0x90, patchSize);
	patch[0] = 0xE8;
	const std::int64_t delta =
		static_cast<std::int64_t>(relayAddress) - static_cast<std::int64_t>(reinterpret_cast<std::uintptr_t>(call) + 5);
	const std::int32_t newRel = static_cast<std::int32_t>(delta);
	memcpy(patch + 1, &newRel, sizeof(newRel));

	DWORD oldProtect = 0;
	if (!VirtualProtect(call, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
		qqspeed::logging::Logf(
			"Network %s callsite protect failed rva=0x%llX error=%lu",
			name ? name : "unknown",
			static_cast<unsigned long long>(callRva),
			GetLastError());
		return false;
	}

	memcpy(call, patch, patchSize);
	DWORD ignoredProtect = 0;
	VirtualProtect(call, patchSize, oldProtect, &ignoredProtect);
	FlushInstructionCache(GetCurrentProcess(), call, patchSize);

	CallSitePatch& saved = g_callPatches[g_callPatchCount++];
	saved.Address = call;
	memcpy(saved.Original, original, patchSize);
	saved.Size = patchSize;
	saved.Installed = true;
	qqspeed::logging::Logf(
		"Network %s callsite patched rva=0x%llX address=%p size=%llu relay=%p",
		name ? name : "unknown",
		static_cast<unsigned long long>(callRva),
		call,
		static_cast<unsigned long long>(patchSize),
		relay);
	return true;
}
}

namespace qqspeed::network::patch
{
bool SafeCopyRaw(void* dst, const void* src, std::size_t size)
{
	if (!dst || (!src && size != 0)) {
		return false;
	}

	__try
	{
		memcpy(dst, src, size);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

std::size_t PatchDirectCalls(
	std::uintptr_t moduleBase,
	const std::uintptr_t* callRvas,
	std::size_t callCount,
	std::uintptr_t targetRva,
	void* detour,
	const char* name)
{
	std::lock_guard<std::mutex> lock(g_patchMutex);

	if (!moduleBase || !callRvas || callCount == 0 || !targetRva || !detour) {
		qqspeed::logging::Logf("Network %s call patch skipped: bad args", name ? name : "unknown");
		return 0;
	}

	void* relay = CreateRelayStub(moduleBase, detour, name);
	if (!relay) {
		return 0;
	}

	std::size_t patched = 0;
	const std::uintptr_t expectedTarget = moduleBase + targetRva;
	for (std::size_t i = 0; i < callCount; ++i) {
		if (PatchResolvedDirectCall(moduleBase + callRvas[i], expectedTarget, relay, name)) {
			++patched;
		}
	}

	qqspeed::logging::Logf(
		"Network %s call patch complete patched=%llu total=%llu",
		name ? name : "unknown",
		static_cast<unsigned long long>(patched),
		static_cast<unsigned long long>(callCount));
	return patched;
}

std::size_t PatchResolvedDirectCalls(
	std::uintptr_t relayBase,
	const std::uintptr_t* callAddresses,
	std::size_t callCount,
	std::uintptr_t expectedTarget,
	void* detour,
	const char* name)
{
	std::lock_guard<std::mutex> lock(g_patchMutex);

	if (!relayBase || !callAddresses || callCount == 0 || !expectedTarget || !detour) {
		qqspeed::logging::Logf("Network %s resolved call patch skipped: bad args", name ? name : "unknown");
		return 0;
	}

	void* relay = CreateRelayStub(relayBase, detour, name);
	if (!relay) {
		return 0;
	}

	std::size_t patched = 0;
	for (std::size_t i = 0; i < callCount; ++i) {
		if (PatchResolvedDirectCall(callAddresses[i], expectedTarget, relay, name)) {
			++patched;
		}
	}

	qqspeed::logging::Logf(
		"Network %s resolved call patch complete patched=%llu total=%llu",
		name ? name : "unknown",
		static_cast<unsigned long long>(patched),
		static_cast<unsigned long long>(callCount));
	return patched;
}

std::size_t PatchCallSitesWithRelay(
	std::uintptr_t moduleBase,
	const std::uintptr_t* callRvas,
	std::size_t callCount,
	const unsigned char* expectedBytes,
	std::size_t patchSize,
	void* detour,
	const char* name)
{
	std::lock_guard<std::mutex> lock(g_patchMutex);

	if (!moduleBase || !callRvas || callCount == 0 || !expectedBytes || patchSize < 5 || !detour) {
		qqspeed::logging::Logf("Network %s callsite patch skipped: bad args", name ? name : "unknown");
		return 0;
	}

	void* relay = CreateRelayStub(moduleBase, detour, name);
	if (!relay) {
		return 0;
	}

	std::size_t patched = 0;
	for (std::size_t i = 0; i < callCount; ++i) {
		if (PatchCallSiteWithRelay(moduleBase, callRvas[i], expectedBytes, patchSize, relay, name)) {
			++patched;
		}
	}

	qqspeed::logging::Logf(
		"Network %s callsite patch complete patched=%llu total=%llu",
		name ? name : "unknown",
		static_cast<unsigned long long>(patched),
		static_cast<unsigned long long>(callCount));
	return patched;
}

std::size_t RollbackAllCallSites()
{
	std::lock_guard<std::mutex> lock(g_patchMutex);

	std::size_t restored = 0;
	std::size_t failed = 0;
	for (std::size_t i = g_callPatchCount; i > 0; --i) {
		CallSitePatch& saved = g_callPatches[i - 1];
		if (!saved.Installed || !saved.Address || saved.Size == 0 || saved.Size > kMaxPatchBytes) {
			continue;
		}

		DWORD oldProtect = 0;
		if (!VirtualProtect(saved.Address, saved.Size, PAGE_EXECUTE_READWRITE, &oldProtect)) {
			++failed;
			continue;
		}

		// A five-byte direct CALL can be made rollback-safe by restoring its
		// rel32 operand first while keeping the E8 opcode valid throughout.
		unsigned char currentOpcode = 0;
		const bool directCall =
			saved.Size == 5 &&
			SafeCopyRaw(&currentOpcode, saved.Address, sizeof(currentOpcode)) &&
			currentOpcode == 0xE8 &&
			saved.Original[0] == 0xE8;
		if (directCall) {
			memcpy(static_cast<unsigned char*>(saved.Address) + 1, saved.Original + 1, 4);
		}
		else {
			memcpy(saved.Address, saved.Original, saved.Size);
		}

		DWORD ignoredProtect = 0;
		VirtualProtect(saved.Address, saved.Size, oldProtect, &ignoredProtect);
		FlushInstructionCache(GetCurrentProcess(), saved.Address, saved.Size);
		saved.Installed = false;
		++restored;
	}

	qqspeed::logging::Logf(
		"Network install rollback complete restored=%llu failed=%llu tracked=%llu",
		static_cast<unsigned long long>(restored),
		static_cast<unsigned long long>(failed),
		static_cast<unsigned long long>(g_callPatchCount));
	return restored;
}
}
