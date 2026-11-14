#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>
#include <windows.h>

namespace
{
wchar_t gLastManualMapError[512] = {};
}

void LauncherLog(const char* format, ...)
{
	if (!format)
	{
		return;
	}
	va_list arguments;
	va_start(arguments, format);
	fprintf(stderr, "MAPPER ");
	vfprintf(stderr, format, arguments);
	fprintf(stderr, "\n");
	va_end(arguments);
}

std::string WideToUtf8Log(LPCWSTR value)
{
	if (!value)
	{
		return std::string();
	}
	const int length = WideCharToMultiByte(
		CP_UTF8,
		0,
		value,
		-1,
		nullptr,
		0,
		nullptr,
		nullptr);
	if (length <= 0)
	{
		return std::string();
	}
	std::string result(static_cast<SIZE_T>(length), '\0');
	WideCharToMultiByte(
		CP_UTF8,
		0,
		value,
		-1,
		result.data(),
		length,
		nullptr,
		nullptr);
	if (!result.empty() && result.back() == '\0')
	{
		result.pop_back();
	}
	return result;
}

void SetQQSpeedNativeBridgeLastError(const wchar_t* message)
{
	if (!message)
	{
		gLastManualMapError[0] = L'\0';
		return;
	}
	wcsncpy_s(gLastManualMapError, message, _TRUNCATE);
	fwprintf(stderr, L"MAPPER %ls\n", gLastManualMapError);
}
