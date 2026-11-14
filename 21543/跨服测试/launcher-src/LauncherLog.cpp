#define _CRT_SECURE_NO_WARNINGS
#include "LauncherLog.h"

#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <shlwapi.h>

static std::once_flag gLauncherConsoleOnce;
static std::mutex gLauncherLogMutex;

static void InitLauncherConsole()
{
	std::call_once(gLauncherConsoleOnce, []()
		{
			if (!AttachConsole(ATTACH_PARENT_PROCESS))
			{
				AllocConsole();
			}

			SetConsoleTitleW(L"Launcher Debug Log");
			SetConsoleOutputCP(CP_UTF8);
			SetConsoleCP(CP_UTF8);

			FILE* fp = nullptr;
			freopen_s(&fp, "CONOUT$", "w", stdout);
			freopen_s(&fp, "CONOUT$", "w", stderr);
			freopen_s(&fp, "CONIN$", "r", stdin);
			std::ios::sync_with_stdio(true);

			printf("[Launcher] console initialized\r\n");
			fflush(stdout);
		});
}

void LauncherLog(const char* format, ...)
{
	InitLauncherConsole();

	char message[2048] = { 0 };
	va_list args;
	va_start(args, format);
	vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
	va_end(args);

	SYSTEMTIME now;
	GetLocalTime(&now);

	char line[2300] = { 0 };
	snprintf(line, sizeof(line), "[%02u:%02u:%02u.%03u] [Launcher] %s\r\n",
		now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, message);

	{
		std::lock_guard<std::mutex> lock(gLauncherLogMutex);
		printf("%s", line);
		fflush(stdout);

		char logPath[MAX_PATH] = "QQSpeedLauncher_debug.log";
		char modulePath[MAX_PATH] = { 0 };
		if (GetModuleFileNameA(NULL, modulePath, MAX_PATH))
		{
			PathRemoveFileSpecA(modulePath);
			PathCombineA(logPath, modulePath, "QQSpeedLauncher_debug.log");
		}

		FILE* fp = nullptr;
		if (fopen_s(&fp, logPath, "ab") == 0 && fp)
		{
			fputs(line, fp);
			fclose(fp);
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
