#include "FixtureShared.h"

extern "C" void WINAPI NativeManualMapChildMain()
{
	const DWORD processId = GetCurrentProcessId();
	wchar_t name[64] = {};
	if (!BuildNativeManualMapFixtureName(processId, name, NativeManualMapCountOf(name)))
	{
		ExitProcess(10);
	}

	HANDLE mapping = nullptr;
	for (DWORD attempt = 0; attempt < 500 && !mapping; ++attempt)
	{
		mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
		if (!mapping)
		{
			Sleep(10);
		}
	}
	if (!mapping)
	{
		ExitProcess(11);
	}

	auto* state = static_cast<NativeManualMapFixtureState*>(MapViewOfFile(
		mapping,
		FILE_MAP_ALL_ACCESS,
		0,
		0,
		sizeof(NativeManualMapFixtureState)));
	if (!state)
	{
		CloseHandle(mapping);
		ExitProcess(12);
	}

	LPVOID reservation = VirtualAlloc(
		reinterpret_cast<LPVOID>(kNativeManualMapPreferredBase),
		kNativeManualMapReservationSize,
		MEM_RESERVE,
		PAGE_NOACCESS);
	state->TargetProcessId = processId;
	state->ReservedPreferredBase = reinterpret_cast<ULONG_PTR>(reservation);
	InterlockedExchange(&state->Ready, reservation ? 1 : -1);

	while (reservation && InterlockedCompareExchange(&state->ExitRequested, 0, 0) == 0)
	{
		Sleep(10);
	}
	if (reservation)
	{
		VirtualFree(reservation, 0, MEM_RELEASE);
	}
	UnmapViewOfFile(state);
	CloseHandle(mapping);
	ExitProcess(reservation ? 0 : 13);
}
