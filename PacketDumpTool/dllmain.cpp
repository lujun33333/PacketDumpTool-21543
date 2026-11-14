#include "pch.h"

#include "MinHook.h"
#include "hook_log.h"
#include "network_dump.h"
#ifdef PACKETDUMP_OP4_COLLECTOR
#include "op4_collector.h"
#endif

#ifdef _WIN64
#pragma comment(lib, "libMinHook.x64.lib")
#endif

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	switch (reason) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		HookLog("loaded owner=%p", hModule);
		{
			MH_STATUS status = MH_Initialize();
			HookLog("MinHook initialize status=%s", MH_StatusToString(status));
			if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) {
				StartNetworkDumpWorker(hModule);
#ifdef PACKETDUMP_OP4_COLLECTOR
				StartOp4Collector(hModule);
#endif
			}
		}
		break;
	case DLL_PROCESS_DETACH:
#ifdef PACKETDUMP_OP4_COLLECTOR
		StopOp4Collector();
#endif
		StopNetworkDumpWorker();
		MH_Uninitialize();
		HookLog("unloaded");
		break;
	default:
		break;
	}

	return TRUE;
}
