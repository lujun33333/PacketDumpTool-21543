#pragma once

#include "pch.h"

#include <string>

namespace qqspeed::logging
{
void Initialize(HMODULE module);
std::wstring LogDirectory();
std::wstring LogPath();
void Logf(const char* format, ...);
std::wstring PacketFlowLogPath();
void PacketFlowf(const char* format, ...);
}
