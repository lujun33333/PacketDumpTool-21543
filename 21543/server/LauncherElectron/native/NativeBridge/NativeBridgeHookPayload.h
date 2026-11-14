#pragma once

#include <windows.h>
#include <cstdint>

struct HookPayloadSnapshot
{
	const BYTE* Bytes;
	DWORD Size;
};

// The utility process supplies authenticated bundle bytes before BeginSession.
// The Bridge owns a private copy so the JS buffer can be wiped immediately.
extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_SetHookPayload(
	const void* bytes,
	std::uint32_t size);

bool GetHookPayloadSnapshot(HookPayloadSnapshot* snapshot);
void ReleaseHookPayloadSnapshot();
bool HasHookPayload();
void ClearHookPayload();
