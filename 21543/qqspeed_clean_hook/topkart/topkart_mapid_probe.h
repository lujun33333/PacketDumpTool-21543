#pragma once

#include "hook_config.h"

namespace qqspeed::topkart
{
void StartMapIdProbeThread(const config::HookConfig& config);
bool InstallMapIdProbe(HMODULE topKartModule, const config::HookConfig& config);
}
