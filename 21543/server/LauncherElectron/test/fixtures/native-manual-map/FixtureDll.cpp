#include "FixtureShared.h"

extern "C" DWORD _tls_index = 0;
extern "C" __declspec(thread) DWORD gNativeManualMapTlsValue =
	kNativeManualMapTlsInitialValue;

namespace
{
DWORD gRelocationMarker = kNativeManualMapRelocationMarker;
DWORD* gRelocatedMarkerPointer = &gRelocationMarker;

NativeManualMapFixtureState* OpenFixtureState()
{
	wchar_t name[64] = {};
	if (!BuildNativeManualMapFixtureName(
		GetCurrentProcessId(),
		name,
		NativeManualMapCountOf(name)))
	{
		return nullptr;
	}
	HANDLE mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, name);
	if (!mapping)
	{
		return nullptr;
	}
	auto* state = static_cast<NativeManualMapFixtureState*>(MapViewOfFile(
		mapping,
		FILE_MAP_ALL_ACCESS,
		0,
		0,
		sizeof(NativeManualMapFixtureState)));
	CloseHandle(mapping);
	return state;
}

__declspec(noinline) bool RunExceptionTableProbe(HINSTANCE module)
{
	DWORD64 registeredImageBase = 0;
	PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(
		reinterpret_cast<DWORD64>(&RunExceptionTableProbe),
		&registeredImageBase,
		nullptr);
	return function != nullptr &&
		registeredImageBase == reinterpret_cast<DWORD64>(module) &&
		function->BeginAddress < function->EndAddress;
}
}

extern "C" void NTAPI NativeManualMapTlsCallback(PVOID module, DWORD reason, PVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
	{
		return;
	}
	NativeManualMapFixtureState* state = OpenFixtureState();
	if (!state)
	{
		return;
	}
	state->TlsCallbackModuleBase = reinterpret_cast<ULONG_PTR>(module);
	if (gNativeManualMapTlsValue == kNativeManualMapTlsInitialValue)
	{
		InterlockedOr(&state->LifecycleFlags, NativeManualMapTlsCallbackRan);
	}
	gNativeManualMapTlsValue = kNativeManualMapTlsCallbackValue;
	UnmapViewOfFile(state);
}

#pragma section(".tls", read, write)
extern "C" __declspec(allocate(".tls")) const BYTE _tls_start = 0;
#pragma section(".tls$ZZZ", read, write)
extern "C" __declspec(allocate(".tls$ZZZ")) const BYTE _tls_end = 0;

using NativeManualMapTlsCallbackPointer = void (NTAPI*)(PVOID, DWORD, PVOID);
#pragma section(".CRT$XLB", read)
extern "C" __declspec(allocate(".CRT$XLB"))
	const NativeManualMapTlsCallbackPointer gNativeManualMapTlsCallbacks[] = {
		NativeManualMapTlsCallback,
		nullptr,
	};

#pragma section(".rdata$T", read)
extern "C" __declspec(allocate(".rdata$T")) const IMAGE_TLS_DIRECTORY64 _tls_used = {
	reinterpret_cast<ULONGLONG>(&_tls_start),
	reinterpret_cast<ULONGLONG>(&_tls_end),
	reinterpret_cast<ULONGLONG>(&_tls_index),
	reinterpret_cast<ULONGLONG>(gNativeManualMapTlsCallbacks),
	0,
	0,
};

#pragma comment(linker, "/INCLUDE:_tls_used")
#pragma comment(linker, "/INCLUDE:gNativeManualMapTlsCallbacks")

extern "C" BOOL WINAPI FixtureDllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
	{
		return TRUE;
	}

	NativeManualMapFixtureState* state = OpenFixtureState();
	if (!state)
	{
		return FALSE;
	}

	const bool reject = InterlockedCompareExchange(&state->RejectAttach, 0, 0) != 0;
	if (reject)
	{
		state->RejectedModuleBase = reinterpret_cast<ULONG_PTR>(module);
	}
	else
	{
		state->AcceptedModuleBase = reinterpret_cast<ULONG_PTR>(module);
	}

	state->TlsValueSeenByDllMain = gNativeManualMapTlsValue;
	state->TlsIndexSeenByDllMain = _tls_index;
	if (gNativeManualMapTlsValue == kNativeManualMapTlsCallbackValue)
	{
		InterlockedOr(&state->LifecycleFlags, NativeManualMapTlsContinuity);
	}
	if (GetCurrentProcessId() == state->TargetProcessId)
	{
		InterlockedOr(&state->LifecycleFlags, NativeManualMapImportResolved);
	}
	if (gRelocatedMarkerPointer == &gRelocationMarker &&
		*gRelocatedMarkerPointer == kNativeManualMapRelocationMarker)
	{
		InterlockedOr(&state->LifecycleFlags, NativeManualMapRelocationApplied);
	}
	if (RunExceptionTableProbe(module))
	{
		InterlockedOr(
			&state->LifecycleFlags,
			NativeManualMapExceptionTableRegistered);
	}
	InterlockedOr(&state->LifecycleFlags, NativeManualMapDllMainRan);
	if (reject)
	{
		InterlockedOr(&state->LifecycleFlags, NativeManualMapAttachRejected);
	}

	UnmapViewOfFile(state);
	return reject ? FALSE : TRUE;
}
