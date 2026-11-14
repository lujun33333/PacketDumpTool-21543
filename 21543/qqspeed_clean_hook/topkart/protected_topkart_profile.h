#pragma once

#include <cstddef>
#include <cstdint>

#if defined(QQSPEED_VMP_PROTECTION)
namespace qqspeed::topkart
{
struct ResolvedTopKartCoreProfile
{
	std::uintptr_t antiCheckerExecuteRva = 0;
	std::uintptr_t antiCheckerExecuteSlotRva = 0;
	std::uintptr_t aceInterfaceGlobalRva = 0;
};

bool ResolveProtectedTopKartCoreProfile(
	std::size_t imageSize,
	ResolvedTopKartCoreProfile& profile);
void ClearResolvedTopKartCoreProfile(ResolvedTopKartCoreProfile& profile) noexcept;
}
#endif
