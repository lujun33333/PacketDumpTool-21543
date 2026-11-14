#pragma once

#include "hook_config.h"

namespace qqspeed::topkart
{
void StartChangeItemProbeThread(const config::HookConfig& config);
bool InstallChangeItemProbe(HMODULE topKartModule, const config::HookConfig& config);
}
