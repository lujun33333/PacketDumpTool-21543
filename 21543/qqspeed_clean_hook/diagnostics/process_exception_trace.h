#pragma once

#include "hook_config.h"
#include "pch.h"

namespace qqspeed::diagnostics
{
void StartProcessExceptionTrace(HMODULE selfModule, const qqspeed::config::HookConfig& config);
}
