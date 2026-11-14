#pragma once

#include <string>

std::string GetCurrentDllDirectory();
std::string GetPacketDumpOutputDirectory();
std::string GetPacketPayloadOutputDirectory();
void HookLog(const char* format, ...);
