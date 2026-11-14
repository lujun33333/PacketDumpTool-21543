#pragma once

#include <windows.h>

constexpr ULONG_PTR kNativeManualMapPreferredBase = 0x0000000188000000ULL;
constexpr SIZE_T kNativeManualMapReservationSize = 16 * 1024 * 1024;
constexpr DWORD kNativeManualMapTlsInitialValue = 0x13579BDF;
constexpr DWORD kNativeManualMapTlsCallbackValue = 0x2468ACE0;
constexpr DWORD kNativeManualMapRelocationMarker = 0xA17E5EED;

enum NativeManualMapLifecycleFlags : LONG
{
	NativeManualMapTlsCallbackRan = 1 << 0,
	NativeManualMapDllMainRan = 1 << 1,
	NativeManualMapImportResolved = 1 << 2,
	NativeManualMapRelocationApplied = 1 << 3,
	NativeManualMapExceptionTableRegistered = 1 << 4,
	NativeManualMapTlsContinuity = 1 << 5,
	NativeManualMapAttachRejected = 1 << 6,
};

constexpr LONG kNativeManualMapRequiredLifecycleFlags =
	NativeManualMapTlsCallbackRan |
	NativeManualMapDllMainRan |
	NativeManualMapImportResolved |
	NativeManualMapRelocationApplied |
	NativeManualMapExceptionTableRegistered |
	NativeManualMapTlsContinuity;

struct NativeManualMapFixtureState
{
	volatile LONG Ready;
	volatile LONG ExitRequested;
	volatile LONG RejectAttach;
	volatile LONG LifecycleFlags;
	DWORD TargetProcessId;
	DWORD TlsValueSeenByDllMain;
	DWORD TlsIndexSeenByDllMain;
	DWORD Reserved;
	ULONG_PTR ReservedPreferredBase;
	ULONG_PTR TlsCallbackModuleBase;
	ULONG_PTR AcceptedModuleBase;
	ULONG_PTR RejectedModuleBase;
};

template <typename T, SIZE_T N>
constexpr SIZE_T NativeManualMapCountOf(const T (&)[N])
{
	return N;
}

inline bool BuildNativeManualMapFixtureName(DWORD processId, wchar_t* output, SIZE_T capacity)
{
	static constexpr wchar_t prefix[] = L"Local\\NativeManualMapFixture-";
	if (!output || capacity < NativeManualMapCountOf(prefix) + 10)
	{
		return false;
	}

	SIZE_T length = 0;
	for (SIZE_T index = 0; index + 1 < NativeManualMapCountOf(prefix); ++index)
	{
		output[length++] = prefix[index];
	}

	wchar_t digits[10] = {};
	SIZE_T digitCount = 0;
	do
	{
		digits[digitCount++] = static_cast<wchar_t>(L'0' + (processId % 10));
		processId /= 10;
	} while (processId && digitCount < NativeManualMapCountOf(digits));

	if (length + digitCount + 1 > capacity)
	{
		return false;
	}
	while (digitCount)
	{
		output[length++] = digits[--digitCount];
	}
	output[length] = L'\0';
	return true;
}
