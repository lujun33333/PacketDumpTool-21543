#define _CRT_SECURE_NO_WARNINGS
#include "NativeBridgeLog.h"

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <shlwapi.h>

#pragma comment(lib, "Shlwapi.lib")

static std::once_flag gLauncherConsoleOnce;
static std::once_flag gLauncherLogPathOnce;
static std::mutex gLauncherLogMutex;
static bool gLauncherConsoleReady = false;
static WCHAR gLauncherLogPath[MAX_PATH] = {};

static bool IsEnvironmentFlagEnabled(LPCWSTR name)
{
	WCHAR value[16] = {};
	DWORD copied = GetEnvironmentVariableW(name, value, _countof(value));
	return copied > 0 && copied < _countof(value) && lstrcmpiW(value, L"1") == 0;
}

bool IsLauncherDebugEnabled()
{
	return IsEnvironmentFlagEnabled(L"QQSPEED_LAUNCHER_DEBUG");
}

static void InitLauncherConsole()
{
	std::call_once(gLauncherConsoleOnce, []()
		{
			if (!IsLauncherDebugEnabled() || !IsEnvironmentFlagEnabled(L"QQSPEED_LAUNCHER_DEBUG_CONSOLE"))
			{
				return;
			}

			if (!AttachConsole(ATTACH_PARENT_PROCESS) && !AllocConsole())
			{
				return;
			}

			SetConsoleTitleW(L"QQSpeed Launcher Debug Log");
			SetConsoleOutputCP(CP_UTF8);
			SetConsoleCP(CP_UTF8);

			FILE* fp = nullptr;
			freopen_s(&fp, "CONOUT$", "w", stdout);
			freopen_s(&fp, "CONOUT$", "w", stderr);
			freopen_s(&fp, "CONIN$", "r", stdin);
			std::ios::sync_with_stdio(true);
			gLauncherConsoleReady = true;
		});
}

static void InitLauncherLogPath()
{
	std::call_once(gLauncherLogPathOnce, []()
		{
			WCHAR localAppData[MAX_PATH] = {};
			DWORD copied = GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, _countof(localAppData));
			if (copied == 0 || copied >= _countof(localAppData))
			{
				return;
			}

			WCHAR appDirectory[MAX_PATH] = {};
			WCHAR logDirectory[MAX_PATH] = {};
			if (!PathCombineW(appDirectory, localAppData, L"QQSpeedLauncher") ||
				!PathCombineW(logDirectory, appDirectory, L"logs") ||
				!PathCombineW(gLauncherLogPath, logDirectory, L"launcher.log"))
			{
				gLauncherLogPath[0] = L'\0';
				return;
			}

			CreateDirectoryW(appDirectory, nullptr);
			CreateDirectoryW(logDirectory, nullptr);
		});
}

void LauncherLog(const char* format, ...)
{
	// Production builds are silent by default. Debug logging is an explicit opt-in
	// and never writes beside the packaged native binaries.
	if (!IsLauncherDebugEnabled())
	{
		return;
	}

	InitLauncherConsole();
	InitLauncherLogPath();

	char message[2048] = {};
	va_list args;
	va_start(args, format);
	vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
	va_end(args);

	SYSTEMTIME now = {};
	GetLocalTime(&now);

	char line[2300] = {};
	snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] [Launcher] %s\r\n",
		now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, message);

	{
		std::lock_guard<std::mutex> lock(gLauncherLogMutex);
		if (gLauncherConsoleReady)
		{
			printf("%s", line);
			fflush(stdout);
		}

		if (gLauncherLogPath[0] != L'\0')
		{
			FILE* fp = nullptr;
			if (_wfopen_s(&fp, gLauncherLogPath, L"ab") == 0 && fp)
			{
				fputs(line, fp);
				fclose(fp);
			}
		}
	}

	OutputDebugStringA(line);
}

std::string WideToUtf8Log(LPCWSTR value)
{
	if (!value)
	{
		return std::string();
	}
	int len = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
	if (len <= 0)
	{
		return std::string();
	}
	std::string result((size_t)len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value, -1, &result[0], len, nullptr, nullptr);
	if (!result.empty() && result.back() == '\0')
	{
		result.pop_back();
	}
	return result;
}
