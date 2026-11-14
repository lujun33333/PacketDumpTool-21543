#include "pch.h"

#include "logging/hook_log.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
constexpr wchar_t kLogDirectoryName[] = L"qqspeed_clean_hook_logs";
constexpr wchar_t kLogFilePrefix[] = L"qqspeed_clean_hook_";
constexpr wchar_t kPacketFlowLogFilePrefix[] = L"qqspeed_packet_flow_";

std::mutex g_logMutex;
std::wstring g_logDirectory;
std::wstring g_logPath;
std::wstring g_packetFlowLogPath;
HANDLE g_logFile = INVALID_HANDLE_VALUE;
HANDLE g_packetFlowLogFile = INVALID_HANDLE_VALUE;
ULONGLONG g_logStartTick = 0;

bool ShouldSuppressProtectedAddressLog(const char* format)
{
#if defined(QQSPEED_VMP_PROTECTION)
	if (!format || strcmp(format, "hook_config protected_profile=1") == 0) {
		return false;
	}
	if (strstr(format, "hook_config ") ||
		strstr(format, "callRva") ||
		strstr(format, "rva=") ||
		strstr(format, "Rva=") ||
		strstr(format, "RVA=")) {
		return true;
	}
	// Cover compound spellings such as executeRVA or caller_rva without
	// maintaining a fragile list of every diagnostic format string.
	for (const char* cursor = format; cursor[0] && cursor[1] && cursor[2]; ++cursor) {
		const char a = static_cast<char>(cursor[0] | 0x20);
		const char b = static_cast<char>(cursor[1] | 0x20);
		const char c = static_cast<char>(cursor[2] | 0x20);
		if (a == 'r' && b == 'v' && c == 'a') {
			return true;
		}
	}
#else
	(void)format;
#endif
	return false;
}

std::wstring EnsureTrailingSlash(const std::wstring& path)
{
	if (path.empty()) {
		return path;
	}

	const wchar_t last = path[path.size() - 1];
	if (last == L'\\' || last == L'/') {
		return path;
	}
	return path + L"\\";
}

std::wstring GetModuleDirectory(HMODULE module)
{
	std::vector<wchar_t> path(MAX_PATH);
	for (;;) {
		const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0) {
			return {};
		}
		if (length < path.size() - 1) {
			std::wstring fullPath(path.data(), length);
			const size_t slashPos = fullPath.find_last_of(L"\\/");
			if (slashPos == std::wstring::npos) {
				return {};
			}
			return fullPath.substr(0, slashPos + 1);
		}
		path.resize(path.size() * 2);
	}
}

std::wstring BuildLogPathUnlocked(const wchar_t* prefix)
{
	if (g_logDirectory.empty() || prefix == nullptr) {
		return {};
	}

	return EnsureTrailingSlash(g_logDirectory) +
		prefix +
		std::to_wstring(GetCurrentProcessId()) +
		L".log";
}

std::wstring BuildLogPath(const wchar_t* prefix)
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	return BuildLogPathUnlocked(prefix);
}

void CloseLogFile(HANDLE& file)
{
	if (file != INVALID_HANDLE_VALUE) {
		CloseHandle(file);
		file = INVALID_HANDLE_VALUE;
	}
}

void AppendBytes(const wchar_t* prefix, const char* bytes, size_t byteCount)
{
	if (prefix == nullptr || bytes == nullptr || byteCount == 0) {
		return;
	}

	std::lock_guard<std::mutex> lock(g_logMutex);
	const bool packetFlow = wcscmp(prefix, kPacketFlowLogFilePrefix) == 0;
	const std::wstring& path = packetFlow ? g_packetFlowLogPath : g_logPath;
	HANDLE& file = packetFlow ? g_packetFlowLogFile : g_logFile;
	if (path.empty()) {
		return;
	}
	if (file == INVALID_HANDLE_VALUE) {
		file = CreateFileW(
			path.c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE) {
			return;
		}
	}

	DWORD written = 0;
	WriteFile(file, bytes, static_cast<DWORD>(byteCount), &written, nullptr);
}

void WriteFormattedLog(const wchar_t* prefix, const char* channel, const char* format, va_list args)
{
	if (ShouldSuppressProtectedAddressLog(format)) {
		return;
	}
	if (format == nullptr) {
		return;
	}

	char message[8192] = {};
	vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);

	char line[8704] = {};
	const ULONGLONG elapsedMs = g_logStartTick == 0 ? 0 : GetTickCount64() - g_logStartTick;
	snprintf(
		line,
		sizeof(line),
		"[%s:%lu +%llums] %s\r\n",
		channel ? channel : "qqspeed_clean_hook",
		GetCurrentProcessId(),
		static_cast<unsigned long long>(elapsedMs),
		message);

	OutputDebugStringA(line);
	AppendBytes(prefix, line, strnlen_s(line, sizeof(line)));
}
}

namespace qqspeed::logging
{
void Initialize(HMODULE module)
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	CloseLogFile(g_logFile);
	CloseLogFile(g_packetFlowLogFile);
	g_logStartTick = GetTickCount64();

	const std::wstring moduleDirectory = GetModuleDirectory(module);
	if (moduleDirectory.empty()) {
		g_logDirectory.clear();
		g_logPath.clear();
		g_packetFlowLogPath.clear();
		return;
	}

	g_logDirectory = EnsureTrailingSlash(moduleDirectory) + kLogDirectoryName + L"\\";
	CreateDirectoryW(g_logDirectory.c_str(), nullptr);
	g_logPath = BuildLogPathUnlocked(kLogFilePrefix);
	g_packetFlowLogPath = BuildLogPathUnlocked(kPacketFlowLogFilePrefix);
}

std::wstring LogDirectory()
{
	std::lock_guard<std::mutex> lock(g_logMutex);
	return g_logDirectory;
}

std::wstring LogPath()
{
	return BuildLogPath(kLogFilePrefix);
}

void Logf(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	WriteFormattedLog(kLogFilePrefix, "qqspeed_clean_hook", format, args);
	va_end(args);
}

std::wstring PacketFlowLogPath()
{
	return BuildLogPath(kPacketFlowLogFilePrefix);
}

void PacketFlowf(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	WriteFormattedLog(kPacketFlowLogFilePrefix, "qqspeed_packet_flow", format, args);
	va_end(args);
}
}
