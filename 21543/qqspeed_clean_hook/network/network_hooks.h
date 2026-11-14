#pragma once

#include "hook_config.h"
#if defined(QQSPEED_VMP_PROTECTION)
#include "network/protected_network_profile.h"
#endif

namespace qqspeed::network
{
#if defined(QQSPEED_VMP_PROTECTION)
bool InstallNetworkHooks(
	HMODULE networkModule,
	const config::HookConfig& config,
	const ResolvedNetworkProfile& profile);
#else
bool InstallNetworkHooks(HMODULE networkModule, const config::HookConfig& config);
#endif
}
