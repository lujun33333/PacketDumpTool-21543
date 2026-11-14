#pragma once

#include "hook_config.h"

namespace qqspeed::topkart
{
void StartExitTraceProbeThread(const config::HookConfig& config);
bool InstallExitTraceProbe(HMODULE topKartModule, const config::HookConfig& config);
}
