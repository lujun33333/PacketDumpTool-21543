#include "pch.h"

#include "hook_log.h"

#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <string>

std::string GetCurrentDllDirectory()
{
	char path[MAX_PATH] = { 0 };
	HMODULE hModule = nullptr;
	if (!GetModuleHandleExA(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCSTR>(&GetCurrentDllDirectory),
		&hModule))
	{
		return "";
	}

	DWORD pathLen = GetModuleFileNameA(hModule, path, MAX_PATH);
	if (pathLen == 0 || pathLen == MAX_PATH) {
		return "";
	}

	std::string fullPath(path);
	size_t lastSlashPos = fullPath.find_last_of("\\/");
	if (lastSlashPos != std::string::npos) {
		return fullPath.substr(0, lastSlashPos + 1);
	}
	return "";
}

std::string GetPacketDumpOutputDirectory()
{
	std::string dir = GetCurrentDllDirectory()
		+ "packet_dumps_"
		+ std::to_string(GetCurrentProcessId())
		+ "\\";
	CreateDirectoryA(dir.c_str(), nullptr);
	return dir;
}

std::string GetPacketPayloadOutputDirectory()
{
	std::string dir = GetPacketDumpOutputDirectory() + "payloads\\";
	CreateDirectoryA(dir.c_str(), nullptr);
	return dir;
}

void HookLog(const char* format, ...)
{
	char message[1024] = { 0 };
	va_list args;
	va_start(args, format);
	vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
	va_end(args);

	char line[1200] = { 0 };
	snprintf(line, sizeof(line), "[PacketDumpHook:%lu] %s\r\n", GetCurrentProcessId(), message);

	OutputDebugStringA(line);

	std::string logPath = GetPacketDumpOutputDirectory() + "PacketDumpHook_" + std::to_string(GetCurrentProcessId()) + ".log";
	std::ofstream logFile(logPath, std::ios::app);
	if (logFile) {
		logFile << line;
	}
}
