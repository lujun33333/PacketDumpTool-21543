#pragma once

#include <string>
#include <windows.h>

void LauncherLog(const char* format, ...);
std::string WideToUtf8Log(LPCWSTR value);
bool IsLauncherDebugEnabled();
