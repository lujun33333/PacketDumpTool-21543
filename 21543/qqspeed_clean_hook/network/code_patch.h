#pragma once

#include <cstddef>
#include <cstdint>

namespace qqspeed::network::patch
{
bool SafeCopyRaw(void* dst, const void* src, std::size_t size);
std::size_t PatchDirectCalls(
	std::uintptr_t moduleBase,
	const std::uintptr_t* callRvas,
	std::size_t callCount,
	std::uintptr_t targetRva,
	void* detour,
	const char* name);
std::size_t PatchResolvedDirectCalls(
	std::uintptr_t relayBase,
	const std::uintptr_t* callAddresses,
	std::size_t callCount,
	std::uintptr_t expectedTarget,
	void* detour,
	const char* name);
std::size_t PatchCallSitesWithRelay(
	std::uintptr_t moduleBase,
	const std::uintptr_t* callRvas,
	std::size_t callCount,
	const unsigned char* expectedBytes,
	std::size_t patchSize,
	void* detour,
	const char* name);
std::size_t RollbackAllCallSites();
}
