#include "pch.h"

#include "hook_runtime.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH) {
		DisableThreadLibraryCalls(module);
		qqspeed::runtime::Start(module);
	}
	return TRUE;
}

