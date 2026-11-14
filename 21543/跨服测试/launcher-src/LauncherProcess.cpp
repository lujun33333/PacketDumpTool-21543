#define _CRT_SECURE_NO_WARNINGS
#include "LauncherProcess.h"

#include "LauncherIpc.h"
#include "LauncherLog.h"
#include "LauncherState.h"

#include <Psapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <cstring>

#pragma comment (lib,"Shlwapi.lib")
#pragma comment (lib,"Shell32.lib")

static const DWORD kSharedDataMapSize = kKartMapSharedSize;
static const BOOL kDisableHookInjectionForTest = FALSE;
static volatile LONG gGameReadyFlag = 0;

struct GameWindowSearch
{
	DWORD ProcessId;
	BOOL Found;
	HWND Hwnd;
	BOOL RequireVisible;
};

static void PumpLauncherMessages()
{
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		if (msg.message == WM_QUIT)
		{
			PostQuitMessage((int)msg.wParam);
			break;
		}
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}

static BOOL CALLBACK EnumGameWindowProc(HWND hWnd, LPARAM lParam)
{
	GameWindowSearch* search = (GameWindowSearch*)lParam;
	DWORD pid = 0;
	GetWindowThreadProcessId(hWnd, &pid);
	if (pid != search->ProcessId || GetWindow(hWnd, GW_OWNER) != NULL)
	{
		return TRUE;
	}
	if (search->RequireVisible && (!IsWindowVisible(hWnd) || IsIconic(hWnd)))
	{
		return TRUE;
	}

	if (!search->RequireVisible || (IsWindowVisible(hWnd) && !IsIconic(hWnd)))
	{
		search->Found = TRUE;
		search->Hwnd = hWnd;
		return FALSE;
	}
	return TRUE;
}

HWND FindGameTopLevelWindow(DWORD gameProcessId, BOOL requireVisible)
{
	GameWindowSearch search = { gameProcessId, FALSE, NULL, requireVisible };
	EnumWindows(EnumGameWindowProc, (LPARAM)&search);
	return search.Hwnd;
}

static BOOL IsGameWindowVisible(DWORD gameProcessId)
{
	return FindGameTopLevelWindow(gameProcessId, TRUE) != NULL;
}

static BOOL IsGameSharedReady()
{
	return InterlockedCompareExchange(&gGameReadyFlag, 0, 0) != 0;
}

DWORD GetGameSharedReadyValue()
{
	return (DWORD)InterlockedCompareExchange(&gGameReadyFlag, 0, 0);
}

void SetGameSharedReadyValue(DWORD value)
{
	InterlockedExchange(&gGameReadyFlag, (LONG)value);
}

BOOL WaitForGameReady(DWORD gameProcessId, DWORD timeoutMs)
{
	ULONGLONG started = GetTickCount64();
	ULONGLONG nextLog = 0;
	int progress = 58;
	const BOOL waitForever = timeoutMs == INFINITE;
	BOOL readyByFlag = FALSE;
	BOOL readyByWindow = FALSE;
	BOOL processExited = FALSE;
	DWORD exitCode = STILL_ACTIVE;
	LauncherLog("WaitForGameReady start pid=%lu timeoutMs=%lu map=%p mapSize=%lu",
		gameProcessId, timeoutMs, pMapData, kSharedDataMapSize);

	while (waitForever || GetTickCount64() - started < timeoutMs)
	{
		DWORD readyFlag = GetGameSharedReadyValue();
		readyByFlag = readyFlag != 0;
		readyByWindow = IsGameWindowVisible(gameProcessId);
		if (readyByFlag || readyByWindow)
		{
			LauncherLog("WaitForGameReady satisfied elapsedMs=%llu readyFlag=%lu windowVisible=%d",
				(unsigned long long)(GetTickCount64() - started), readyFlag, readyByWindow ? 1 : 0);
			break;
		}

		if (hGameProcess && GetExitCodeProcess(hGameProcess, &exitCode) && exitCode != STILL_ACTIVE)
		{
			processExited = TRUE;
			LauncherLog("WaitForGameReady process exited pid=%lu exitCode=%lu elapsedMs=%llu",
				gameProcessId, exitCode, (unsigned long long)(GetTickCount64() - started));
			break;
		}

		DWORD elapsed = (DWORD)(GetTickCount64() - started);
		if ((ULONGLONG)elapsed >= nextLog)
		{
			// LauncherLog("Waiting for game resource initialization elapsedMs=%lu readyFlag=%lu windowVisible=0",
			// 	elapsed, readyFlag);
			nextLog = (ULONGLONG)elapsed + 1000;
		}

		int nextProgress = waitForever ? 58 + (int)(elapsed / 1000) : 58 + (int)(32ULL * elapsed / timeoutMs);
		if (nextProgress > 92)
		{
			nextProgress = 92;
		}
		if (nextProgress != progress)
		{
			progress = nextProgress;
			if (Msg)
			{
				InvalidateRect(pMapData ? pMapData->hWnd : NULL, NULL, FALSE);
			}
		}
		PumpLauncherMessages();
		Sleep(50);
	}

	if (!readyByFlag && !readyByWindow && !processExited)
	{
		readyByFlag = IsGameSharedReady();
		readyByWindow = IsGameWindowVisible(gameProcessId);
	}

	BOOL ready = readyByFlag || readyByWindow;
	if (!ready && !processExited && !waitForever)
	{
		LauncherLog("WaitForGameReady timeout pid=%lu timeoutMs=%lu readyFlag=%lu windowVisible=%d",
			gameProcessId, timeoutMs, GetGameSharedReadyValue(), readyByWindow ? 1 : 0);
	}
	LauncherLog("WaitForGameReady finish ready=%d readyFlag=%lu windowVisible=%d processExited=%d exitCode=%lu",
		ready ? 1 : 0, GetGameSharedReadyValue(), readyByWindow ? 1 : 0, processExited ? 1 : 0, exitCode);

	for (int p = progress + 1; p <= 100; p += ready ? 8 : 4)
	{
		PumpLauncherMessages();
		Sleep(ready ? 25 : 40);
	}
	for (int i = 0; i < 20; ++i)
	{
		PumpLauncherMessages();
		Sleep(30);
	}

	return ready;
}

void CloseGameProcessHandle()
{
	if (hGameProcess)
	{
		LauncherLog("CloseGameProcessHandle handle=%p", hGameProcess);
		CloseHandle(hGameProcess);
		hGameProcess = NULL;
	}
}

void TerminateGameProcess()
{
	if (hGameProcess)
	{
		DWORD pid = GetProcessId(hGameProcess);
		LauncherLog("TerminateGameProcess pid=%lu handle=%p", pid, hGameProcess);
		TerminateProcess(hGameProcess, 0);
		CloseGameProcessHandle();
	}
}

BOOL LogGameProcessState(const char* stage, HANDLE hProcess)
{
	if (!hProcess)
	{
		LauncherLog("GameApp state stage=%s handle=NULL", stage);
		return FALSE;
	}

	DWORD pid = GetProcessId(hProcess);
	DWORD exitCode = 0;
	if (!GetExitCodeProcess(hProcess, &exitCode))
	{
		LauncherLog("GameApp state stage=%s pid=%lu GetExitCodeProcess failed error=%lu",
			stage, pid, GetLastError());
		return FALSE;
	}

	LauncherLog("GameApp state stage=%s pid=%lu active=%d exitCode=%lu exitCodeHex=0x%08lX",
		stage,
		pid,
		exitCode == STILL_ACTIVE ? 1 : 0,
		exitCode,
		exitCode);
	return exitCode == STILL_ACTIVE;
}

static const char* WaitResultName(DWORD waitResult)
{
	switch (waitResult)
	{
	case WAIT_OBJECT_0:
		return "signaled";
	case WAIT_TIMEOUT:
		return "timeout";
	case WAIT_FAILED:
		return "failed";
	default:
		return "other";
	}
}

static DWORD WINAPI GameProcessMonitorThread(LPVOID param)
{
	GameProcessMonitorContext* ctx = static_cast<GameProcessMonitorContext*>(param);
	DWORD pid = ctx->Process ? GetProcessId(ctx->Process) : 0;
	DWORD lastExitCode = STILL_ACTIVE;

	while (InterlockedCompareExchange(&ctx->Stop, 0, 0) == 0)
	{
		DWORD elapsed = GetTickCount() - ctx->StartTick;
		DWORD exitCode = 0;
		BOOL gotExitCode = ctx->Process && GetExitCodeProcess(ctx->Process, &exitCode);
		DWORD eventState = ctx->Event ? WaitForSingleObject(ctx->Event, 0) : WAIT_FAILED;

		if (!gotExitCode)
		{
			LauncherLog("GameApp monitor stage=%s pid=%lu elapsedMs=%lu GetExitCodeProcess failed error=%lu event=%s",
				ctx->Stage, pid, elapsed, GetLastError(), WaitResultName(eventState));
			break;
		}

		// LauncherLog("GameApp monitor stage=%s pid=%lu elapsedMs=%lu active=%d exitCode=%lu exitCodeHex=0x%08lX event=%s",
		// 	ctx->Stage,
		// 	pid,
		// 	elapsed,
		// 	exitCode == STILL_ACTIVE ? 1 : 0,
		// 	exitCode,
		// 	exitCode,
		// 	WaitResultName(eventState));

		lastExitCode = exitCode;
		if (exitCode != STILL_ACTIVE)
		{
			break;
		}

		Sleep(500);
	}

	LauncherLog("GameApp monitor finish stage=%s pid=%lu elapsedMs=%lu lastExitCode=%lu lastExitCodeHex=0x%08lX stop=%ld",
		ctx->Stage,
		pid,
		GetTickCount() - ctx->StartTick,
		lastExitCode,
		lastExitCode,
		InterlockedCompareExchange(&ctx->Stop, 0, 0));
	return 0;
}

HANDLE StartGameProcessMonitor(GameProcessMonitorContext* ctx, HANDLE process, HANDLE eventHandle, const char* stage)
{
	ctx->Process = process;
	ctx->Event = eventHandle;
	ctx->Stage = stage;
	ctx->StartTick = GetTickCount();
	ctx->Stop = 0;

	HANDLE thread = CreateThread(nullptr, 0, GameProcessMonitorThread, ctx, 0, nullptr);
	if (!thread)
	{
		LauncherLog("GameApp monitor create failed stage=%s error=%lu", stage, GetLastError());
	}
	return thread;
}

void StopGameProcessMonitor(HANDLE thread, GameProcessMonitorContext* ctx)
{
	if (!thread)
	{
		return;
	}

	InterlockedExchange(&ctx->Stop, 1);
	WaitForSingleObject(thread, 2000);
	CloseHandle(thread);
}

static BOOL InjectDLLEx(HANDLE hProcess, LPCWSTR lpDllPath)
{
	LauncherLog("InjectDLLEx start process=%p dll=%s", hProcess, WideToUtf8Log(lpDllPath).c_str());
	SIZE_T dllPathSize = (wcslen(lpDllPath) + 1) * sizeof(WCHAR);
	LPVOID lpBaseAddress = VirtualAllocEx(hProcess, NULL, dllPathSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!lpBaseAddress) {
		LauncherLog("InjectDLLEx VirtualAllocEx failed size=%llu error=%lu",
			(unsigned long long)dllPathSize, GetLastError());
		return FALSE;
	}
	LauncherLog("InjectDLLEx remote buffer=%p size=%llu", lpBaseAddress, (unsigned long long)dllPathSize);

	if (!WriteProcessMemory(hProcess, lpBaseAddress, (LPVOID)lpDllPath, dllPathSize, NULL)) {
		LauncherLog("InjectDLLEx WriteProcessMemory failed error=%lu", GetLastError());
		VirtualFreeEx(hProcess, lpBaseAddress, 0, MEM_RELEASE);
		return FALSE;
	}
	LauncherLog("InjectDLLEx wrote dll path");

	LPVOID lpLoadLibrary = (LPVOID)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
	if (!lpLoadLibrary) {
		LauncherLog("InjectDLLEx GetProcAddress(LoadLibraryW) failed error=%lu", GetLastError());
		VirtualFreeEx(hProcess, lpBaseAddress, 0, MEM_RELEASE);
		return FALSE;
	}
	LauncherLog("InjectDLLEx LoadLibraryW=%p", lpLoadLibrary);

	HANDLE hRemoteThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)lpLoadLibrary, lpBaseAddress, 0, NULL);
	if (!hRemoteThread) {
		LauncherLog("InjectDLLEx CreateRemoteThread failed error=%lu", GetLastError());
		VirtualFreeEx(hProcess, lpBaseAddress, 0, MEM_RELEASE);
		return FALSE;
	}
	LauncherLog("InjectDLLEx remote thread=%p", hRemoteThread);
	LogGameProcessState("InjectDLLEx before remote thread wait", hProcess);

	WaitForSingleObject(hRemoteThread, INFINITE);
	DWORD exitCode = 0;
	if (!GetExitCodeThread(hRemoteThread, &exitCode))
	{
		LauncherLog("InjectDLLEx GetExitCodeThread failed error=%lu", GetLastError());
	}
	else
	{
		LauncherLog("InjectDLLEx remote thread exitCode=%lu", exitCode);
	}
	BOOL processAliveAfterLoad = LogGameProcessState("InjectDLLEx after remote thread wait", hProcess);

	CloseHandle(hRemoteThread);
	VirtualFreeEx(hProcess, lpBaseAddress, 0, MEM_RELEASE);

	BOOL ok = processAliveAfterLoad && exitCode != 0;
	LauncherLog("InjectDLLEx finish success=%d", ok ? 1 : 0);
	return ok;
}

static BOOL FindProcessModuleByName(HANDLE hProcess, LPCWSTR lpszModuleName, LPMODULEINFO lpmodinfo, DWORD dwTimeout)
{
	DWORD pid = GetProcessId(hProcess);
	if (pid == 0)
	{
		LauncherLog("FindProcessModuleByName failed: GetProcessId returned 0 error=%lu", GetLastError());
		return FALSE;
	}

	DWORD dwStartTime = GetTickCount();
	DWORD nextLogTime = dwStartTime;
	DWORD snapshotFailures = 0;
	DWORD lastSnapshotError = ERROR_SUCCESS;
	DWORD lastModuleCount = 0;
	char lastModuleSample[1024] = { 0 };
	char lastNetworkLikeModules[512] = { 0 };
	LauncherLog("Waiting for module %s pid=%lu timeoutMs=%lu",
		WideToUtf8Log(lpszModuleName).c_str(), pid, dwTimeout);

	while (TRUE)
	{
		DWORD exitCode = STILL_ACTIVE;
		if (!GetExitCodeProcess(hProcess, &exitCode))
		{
			LauncherLog("GetExitCodeProcess failed while waiting module pid=%lu error=%lu",
				pid, GetLastError());
		}
		else if (exitCode != STILL_ACTIVE)
		{
			LauncherLog("Process exited while waiting module=%s pid=%lu exitCode=%lu elapsedMs=%lu",
				WideToUtf8Log(lpszModuleName).c_str(), pid, exitCode, GetTickCount() - dwStartTime);
			return FALSE;
		}

		HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
		if (hSnap == INVALID_HANDLE_VALUE)
		{
			++snapshotFailures;
			lastSnapshotError = GetLastError();
			DWORD elapsed = GetTickCount() - dwStartTime;
			if (GetTickCount() >= nextLogTime)
			{
				LauncherLog("Waiting for module snapshot pid=%lu elapsedMs=%lu error=%lu",
					pid, elapsed, lastSnapshotError);
				nextLogTime = GetTickCount() + 1000;
			}
			if (elapsed >= dwTimeout) break;
			Sleep(100);
			continue;
		}

		MODULEENTRY32W me = { 0 };
		me.dwSize = sizeof(me);

		BOOL ret = FALSE;
		DWORD moduleCount = 0;
		char moduleSample[1024] = { 0 };
		char networkLikeModules[512] = { 0 };

		if (Module32FirstW(hSnap, &me)) {
			do {
				++moduleCount;
				std::string moduleName = WideToUtf8Log(me.szModule);
				if (moduleCount <= 12)
				{
					if (moduleSample[0])
					{
						strncat_s(moduleSample, ", ", _TRUNCATE);
					}
					strncat_s(moduleSample, moduleName.c_str(), _TRUNCATE);
				}
				if (StrStrIW(me.szModule, L"network") || StrStrIW(me.szModule, L"net"))
				{
					if (networkLikeModules[0])
					{
						strncat_s(networkLikeModules, ", ", _TRUNCATE);
					}
					strncat_s(networkLikeModules, moduleName.c_str(), _TRUNCATE);
				}
				if (_wcsicmp(me.szModule, lpszModuleName) == 0) {
					lpmodinfo->lpBaseOfDll = me.modBaseAddr;
					lpmodinfo->SizeOfImage = me.modBaseSize;
					lpmodinfo->EntryPoint = NULL; // Toolhelp does not expose EntryPoint here.
					ret = TRUE;
					LauncherLog("Module found name=%s base=%p size=%lu elapsedMs=%lu",
						WideToUtf8Log(me.szModule).c_str(),
						me.modBaseAddr,
						me.modBaseSize,
						GetTickCount() - dwStartTime);
					break;
				}
			} while (Module32NextW(hSnap, &me));

			lastModuleCount = moduleCount;
			lstrcpynA(lastModuleSample, moduleSample, sizeof(lastModuleSample));
			lstrcpynA(lastNetworkLikeModules, networkLikeModules, sizeof(lastNetworkLikeModules));
		}
		else
		{
			LauncherLog("Module32FirstW failed pid=%lu error=%lu", pid, GetLastError());
		}

		CloseHandle(hSnap);

		if (ret) return TRUE;

		DWORD elapsed = GetTickCount() - dwStartTime;
		if (GetTickCount() >= nextLogTime)
		{
			LauncherLog("Module scan pid=%lu target=%s elapsedMs=%lu count=%lu firstModules=[%s] networkLike=[%s]",
				pid,
				WideToUtf8Log(lpszModuleName).c_str(),
				elapsed,
				moduleCount,
				moduleSample,
				networkLikeModules);
			nextLogTime = GetTickCount() + 1000;
		}

		if (elapsed >= dwTimeout) break;

		Sleep(100);
	}

	LauncherLog("FindProcessModuleByName timeout module=%s pid=%lu timeoutMs=%lu snapshotFailures=%lu lastSnapshotError=%lu lastModuleCount=%lu lastModules=[%s] lastNetworkLike=[%s]",
		WideToUtf8Log(lpszModuleName).c_str(),
		pid,
		dwTimeout,
		snapshotFailures,
		lastSnapshotError,
		lastModuleCount,
		lastModuleSample,
		lastNetworkLikeModules);
	return FALSE;
}

// Retry process memory access after relaxing page protection.
#define AccessProcessMemoryEx(func)                                                                                                                               \
	BOOL Ex##func(HANDLE hProcess, LPVOID lpBaseAddress, LPVOID lpBuffer, SIZE_T nSize, SIZE_T *lpNumberOfBytesAccess)                                        \
	{                                                                                                                                                         \
		DWORD oldProtec;                                                                                                                                  \
		return func(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesAccess) || \
			   (ERROR_NOACCESS == GetLastError() && \
			    VirtualProtectEx(hProcess, lpBaseAddress, nSize, PAGE_EXECUTE_READWRITE, &oldProtec) && \
			    func(hProcess, lpBaseAddress, lpBuffer, nSize, lpNumberOfBytesAccess)); \
	}

AccessProcessMemoryEx(ReadProcessMemory);
AccessProcessMemoryEx(WriteProcessMemory);

#undef AccessProcessMemoryEx

static LPCWSTR GAME_MODULE_NAME = L"Network.dll";
static const int GAME_DATA_OFFSET = 0x348E8D;

DWORD GetProcessIdByName(const std::wstring& processName) {
	HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (hSnapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	PROCESSENTRY32 pe32;
	pe32.dwSize = sizeof(PROCESSENTRY32);

	if (!Process32First(hSnapshot, &pe32)) {
		CloseHandle(hSnapshot);
		return 0;
	}

	do {
		if (processName == pe32.szExeFile) {
			CloseHandle(hSnapshot);
			return pe32.th32ProcessID;
		}
	} while (Process32Next(hSnapshot, &pe32));

	CloseHandle(hSnapshot);
	return 0;
}

void enableDebugPriv()
{
	HANDLE hToken;
	LUID sedebugnameValue;
	TOKEN_PRIVILEGES tkp;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
	{
		return;
	}

	if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &sedebugnameValue))
	{
		CloseHandle(hToken);
		return;
	}
	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Luid = sedebugnameValue;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof tkp, NULL, NULL))
	{
		CloseHandle(hToken);
		return;
	}
}

std::wstring WriteProcessMemoryInTargetProcess(DWORD processId) {


	//HANDLE pHandle = OpenProcess(PROCESS_ALL_ACCESS, false, processId);
	LauncherLog("Network patch routine start pid=%lu targetModule=%s offset=0x%X",
		processId, WideToUtf8Log(GAME_MODULE_NAME).c_str(), GAME_DATA_OFFSET);
	HANDLE pHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, FALSE, processId);
	if (!pHandle)
	{
		LauncherLog("OpenProcess for patch failed pid=%lu error=%lu", processId, GetLastError());
		return L"Insufficient permissions. Run the launcher as administrator.";
	}
	LauncherLog("OpenProcess for patch succeeded handle=%p", pHandle);

	MODULEINFO moduleInfo;
	//WriteLog(L"Attempting to get module info...");
	LauncherLog("Waiting for %s before patch write", WideToUtf8Log(GAME_MODULE_NAME).c_str());
	if (!FindProcessModuleByName(pHandle, GAME_MODULE_NAME, &moduleInfo, 30000))
	{
		LauncherLog("Patch failed: module not found name=%s", WideToUtf8Log(GAME_MODULE_NAME).c_str());
		CloseHandle(pHandle);
		return  L"Failed to find Network.dll. Restart the launcher and try again.";
	}

	char* targetAddress = (char*)moduleInfo.lpBaseOfDll + GAME_DATA_OFFSET;
	LauncherLog("Patch target base=%p size=%lu address=%p",
		moduleInfo.lpBaseOfDll, moduleInfo.SizeOfImage, targetAddress);

	// Replace the target 6-byte instruction with NOPs, matching the IDA flow.
	BYTE nopBytes[6] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

	SIZE_T bytesWritten = 0;
	LauncherLog("Patch Network.dll write start address=%p size=%llu",
		targetAddress, (unsigned long long)sizeof(nopBytes));
	if (ExWriteProcessMemory(pHandle, targetAddress, nopBytes, sizeof(nopBytes), &bytesWritten))
	{
		LauncherLog("Patch Network.dll success bytesWritten=%llu", (unsigned long long)bytesWritten);
		CloseHandle(pHandle);
		return L"";
	}
	else
	{
		LauncherLog("Patch Network.dll failed error=%lu bytesWritten=%llu",
			GetLastError(), (unsigned long long)bytesWritten);
		CloseHandle(pHandle);
		return L"Failed to patch Network.dll.";
	}
}

BOOL LaunchGameProcess(HWND hWnd, TCHAR* Buffer)
{
	wsprintf(Buffer, TEXT("%u"), GetCurrentProcessId());
	SHELLEXECUTEINFO ShExecInfo{};
	ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
	ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	ShExecInfo.hwnd = 0;
	//ShExecInfo.lpVerb = L"runas";
	ShExecInfo.lpVerb = NULL;
	ShExecInfo.lpFile = TEXT("GameApp.exe");
	ShExecInfo.lpDirectory = GameWorkDir;
	ShExecInfo.lpParameters = Buffer;
	ShExecInfo.nShow = SW_SHOWDEFAULT;
	LauncherLog("ShellExecuteEx start file=%s dir=%s params=%s",
		WideToUtf8Log(ShExecInfo.lpFile).c_str(),
		WideToUtf8Log(ShExecInfo.lpDirectory).c_str(),
		WideToUtf8Log(ShExecInfo.lpParameters).c_str());

	if (!ShellExecuteEx(&ShExecInfo))
	{
		LauncherLog("ShellExecuteEx failed error=%lu", GetLastError());
		MessageBox(hWnd, TEXT("Failed to launch the game."), szTitle, MB_ICONERROR);
		return FALSE;
	}

	DWORD processId = GetProcessId(ShExecInfo.hProcess);
	hGameProcess = ShExecInfo.hProcess;
	LauncherLog("ShellExecuteEx success process=%p pid=%lu", hGameProcess, processId);
	LogGameProcessState("after ShellExecuteEx", hGameProcess);

	if (kDisableHookInjectionForTest)
	{
		LauncherLog("Hook injection disabled for this test; skipping SpeedHookCfg and InjectDLLEx");
		return TRUE;
	}

	CloseSpeedHookConfigMapping();
	HANDLE speedHookCfgMap = CreateSpeedHookConfigMapping(processId);
	if (!speedHookCfgMap)
	{
		MessageBox(hWnd, TEXT("Failed to create the SpeedHookCfg shared config."), szTitle, MB_ICONERROR);
		TerminateGameProcess();
		return FALSE;
	}
	LogGameProcessState("after SpeedHookCfg", hGameProcess);

	if (!PathFileExistsW(HookDllFile) || !InjectDLLEx(hGameProcess, HookDllFile))
	{
		CloseSpeedHookConfigMapping();
		LauncherLog("Inject hook failed hookExists=%d path=%s",
			PathFileExistsW(HookDllFile) ? 1 : 0, WideToUtf8Log(HookDllFile).c_str());
		wsprintf(Buffer, TEXT("Failed to inject DLL:\n%s"), HookDllFile);
		MessageBox(hWnd, Buffer, szTitle, MB_ICONERROR);
		TerminateGameProcess();
		return FALSE;
	}
	LogGameProcessState("after InjectDLLEx", hGameProcess);

	LauncherLog("SpeedHookCfg handle kept alive after injection handle=%p", GetSpeedHookConfigMappingHandle());
	LogGameProcessState("after SpeedHookCfg keepalive", hGameProcess);
	return TRUE;
}
