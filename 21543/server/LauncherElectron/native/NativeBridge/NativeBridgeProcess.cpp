#define _CRT_SECURE_NO_WARNINGS
#include "NativeBridgeProcess.h"

#include "NativeBridgeHookPayload.h"
#include "NativeBridgeIpc.h"
#include "NativeBridgeLog.h"
#include "NativeBridgeManualMap.h"
#include "NativeBridgeState.h"

#include <shellapi.h>
#include <tlhelp32.h>

#pragma comment (lib,"Shell32.lib")

static const DWORD kSharedDataMapSize = kKartMapSharedSize;
static const DWORD kGameReplacementProbeMs = 5000;
static const DWORD kGameTerminationWaitMs = 5000;
static const int kMaxGameProcessMigrations = 3;
static const LONG kMinimumReadyWindowWidth = 640;
static const LONG kMinimumReadyWindowHeight = 480;
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

static BOOL CALLBACK EnumGameMainWindowProc(HWND hWnd, LPARAM lParam)
{
	GameWindowSearch* search = (GameWindowSearch*)lParam;
	DWORD pid = 0;
	GetWindowThreadProcessId(hWnd, &pid);
	if (pid != search->ProcessId || GetWindow(hWnd, GW_OWNER) != NULL || !IsWindowVisible(hWnd) || IsIconic(hWnd))
	{
		return TRUE;
	}

	RECT clientRect = {};
	if (!GetClientRect(hWnd, &clientRect))
	{
		return TRUE;
	}
	const LONG width = clientRect.right - clientRect.left;
	const LONG height = clientRect.bottom - clientRect.top;
	if (width < kMinimumReadyWindowWidth || height < kMinimumReadyWindowHeight)
	{
		// GameApp's official LoaderBG window is about 300x200 and must remain visible
		// until the real client window is ready.
		return TRUE;
	}

	search->Found = TRUE;
	search->Hwnd = hWnd;
	return FALSE;
}

static BOOL IsGameMainWindowVisible(DWORD gameProcessId)
{
	GameWindowSearch search = { gameProcessId, FALSE, NULL, TRUE };
	EnumWindows(EnumGameMainWindowProc, (LPARAM)&search);
	return search.Found;
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
	const BOOL waitForever = timeoutMs == INFINITE;
	BOOL readyByFlag = FALSE;
	BOOL readyByWindow = FALSE;
	BOOL processExited = FALSE;
	DWORD exitCode = STILL_ACTIVE;
	LauncherLog("WaitForGameReady start pid=%lu timeoutMs=%lu map=%p mapSize=%lu",
		gameProcessId, timeoutMs, pMapData, kSharedDataMapSize);

	while (waitForever || GetTickCount64() - started < timeoutMs)
	{
		if (IsQQSpeedNativeBridgeCancelRequested())
		{
			LauncherLog("WaitForGameReady stopped by session cancellation");
			break;
		}
		if (HasQQSpeedNativeBridgeFailure())
		{
			LauncherLog("WaitForGameReady stopped by native failure state");
			break;
		}
		DWORD readyFlag = GetGameSharedReadyValue();
		readyByFlag = readyFlag != 0;
		readyByWindow = IsGameMainWindowVisible(gameProcessId);
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

		PumpLauncherMessages();
		Sleep(150);
	}

	if (!HasQQSpeedNativeBridgeFailure() && !readyByFlag && !readyByWindow && !processExited)
	{
		readyByFlag = IsGameSharedReady();
		readyByWindow = IsGameMainWindowVisible(gameProcessId);
	}

	BOOL nativeFailure = HasQQSpeedNativeBridgeFailure();
	BOOL ready = !nativeFailure && (readyByFlag || readyByWindow);
	if (!ready && !nativeFailure)
	{
		if (processExited)
		{
			LauncherLog("WaitForGameReady failed: process exited pid=%lu exitCode=%lu",
				gameProcessId, exitCode);
			WCHAR message[160] = { 0 };
			swprintf_s(message, L"The game exited before reaching the ready state (exit code %lu).", exitCode);
			SetQQSpeedNativeBridgeLastError(message);
			SetQQSpeedNativeBridgeFailure(true);
		}
		else if (!waitForever)
		{
			LauncherLog("WaitForGameReady timeout pid=%lu timeoutMs=%lu readyFlag=%lu windowVisible=%d",
				gameProcessId, timeoutMs, GetGameSharedReadyValue(), readyByWindow ? 1 : 0);
			SetQQSpeedNativeBridgeLastError(L"Game startup timed out after 120 seconds.");
			SetQQSpeedNativeBridgeFailure(true);
		}
	}
	LauncherLog("WaitForGameReady finish ready=%d readyFlag=%lu windowVisible=%d processExited=%d exitCode=%lu",
		ready ? 1 : 0, GetGameSharedReadyValue(), readyByWindow ? 1 : 0, processExited ? 1 : 0, exitCode);

	if (ready)
	{
		MarkLoaderProgressStage(eLoaderStageReady);
		PumpLauncherMessages();
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
		if (TerminateProcess(hGameProcess, 0))
		{
			const DWORD waitResult = WaitForSingleObject(hGameProcess, kGameTerminationWaitMs);
			if (waitResult != WAIT_OBJECT_0)
			{
				LauncherLog("TerminateGameProcess wait incomplete pid=%lu result=%lu error=%lu",
					pid, waitResult, waitResult == WAIT_FAILED ? GetLastError() : 0);
			}
		}
		else
		{
			LauncherLog("TerminateGameProcess failed pid=%lu error=%lu", pid, GetLastError());
		}
		CloseGameProcessHandle();
		SetQQSpeedNativeBridgeGameProcessId(0);
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

static HANDLE FindReplacementGameProcess(DWORD parentProcessId, DWORD timeoutMs, DWORD* replacementProcessId)
{
	if (replacementProcessId)
	{
		*replacementProcessId = 0;
	}

	const ULONGLONG deadline = GetTickCount64() + timeoutMs;
	for (;;)
	{
		HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
		if (snapshot != INVALID_HANDLE_VALUE)
		{
			PROCESSENTRY32W entry = {};
			entry.dwSize = sizeof(entry);
			if (Process32FirstW(snapshot, &entry))
			{
				do
				{
					if (entry.th32ParentProcessID != parentProcessId ||
						_wcsicmp(entry.szExeFile, L"GameApp.exe") != 0)
					{
						continue;
					}

					const DWORD desiredAccess = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
						PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE;
					HANDLE process = OpenProcess(desiredAccess, FALSE, entry.th32ProcessID);
					if (!process)
					{
						LauncherLog("GameApp replacement candidate open failed parentPid=%lu childPid=%lu error=%lu",
							parentProcessId, entry.th32ProcessID, GetLastError());
						continue;
					}

					WCHAR imagePath[32768] = {};
					DWORD imagePathLength = _countof(imagePath);
					DWORD exitCode = 0;
					const BOOL pathRead = QueryFullProcessImageNameW(process, 0, imagePath, &imagePathLength);
					const BOOL pathMatches = !pathRead || _wcsicmp(imagePath, GameAppFile) == 0;
					const BOOL active = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
					if (pathMatches && active)
					{
						CloseHandle(snapshot);
						if (replacementProcessId)
						{
							*replacementProcessId = entry.th32ProcessID;
						}
						return process;
					}
					LauncherLog("GameApp replacement candidate rejected parentPid=%lu childPid=%lu pathRead=%d path=%s active=%d exitCode=%lu error=%lu",
						parentProcessId,
						entry.th32ProcessID,
						pathRead ? 1 : 0,
						pathRead ? WideToUtf8Log(imagePath).c_str() : "[unavailable]",
						active ? 1 : 0,
						exitCode,
						GetLastError());
					CloseHandle(process);
				} while (Process32NextW(snapshot, &entry));
			}
			CloseHandle(snapshot);
		}

		if (GetTickCount64() >= deadline)
		{
			return NULL;
		}
		PumpLauncherMessages();
		Sleep(100);
	}
}

static BOOL FollowReplacementGameProcesses()
{
	for (int migration = 0; migration < kMaxGameProcessMigrations; ++migration)
	{
		const DWORD parentProcessId = hGameProcess ? GetProcessId(hGameProcess) : 0;
		if (!parentProcessId)
		{
			SetQQSpeedNativeBridgeLastError(L"The active game process handle became invalid.");
			return FALSE;
		}

		DWORD replacementProcessId = 0;
		HANDLE replacement = FindReplacementGameProcess(
			parentProcessId,
			kGameReplacementProbeMs,
			&replacementProcessId);
		if (!replacement)
		{
			return TRUE;
		}

		LauncherLog("GameApp replacement detected parentPid=%lu childPid=%lu migration=%d",
			parentProcessId, replacementProcessId, migration + 1);
		CloseSpeedHookConfigMapping();
		CloseHandle(hGameProcess);
		hGameProcess = replacement;
		SetQQSpeedNativeBridgeGameProcessId(replacementProcessId);

		if (!CreateSpeedHookConfigMapping(replacementProcessId))
		{
			SetQQSpeedNativeBridgeLastError(L"Failed to create SpeedHookCfg for the replacement GameApp process.");
			return FALSE;
		}
		HookPayloadSnapshot replacementHook = {};
		if (!GetHookPayloadSnapshot(&replacementHook))
		{
			SetQQSpeedNativeBridgeLastError(L"The authenticated in-memory Hook payload is unavailable.");
			return FALSE;
		}
		const BOOL replacementMapped = ManualMapHookImage(
			hGameProcess,
			replacementHook.Bytes,
			replacementHook.Size);
		ReleaseHookPayloadSnapshot();
		if (!replacementMapped)
		{
			LauncherLog("Replacement GameApp in-memory Hook mapping failed pid=%lu", replacementProcessId);
			return FALSE;
		}
		LogGameProcessState("after replacement ManualMapHookImage", hGameProcess);
	}

	SetQQSpeedNativeBridgeLastError(L"GameApp restarted too many times during launch.");
	return FALSE;
}

BOOL LaunchGameProcess()
{
	TCHAR launcherPid[32] = {};
	wsprintf(launcherPid, TEXT("%u"), GetCurrentProcessId());
	SHELLEXECUTEINFO ShExecInfo{};
	ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
	ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
	ShExecInfo.hwnd = 0;
	//ShExecInfo.lpVerb = L"runas";
	ShExecInfo.lpVerb = NULL;
	ShExecInfo.lpFile = TEXT("GameApp.exe");
	ShExecInfo.lpDirectory = GameWorkDir;
	ShExecInfo.lpParameters = launcherPid;
	ShExecInfo.nShow = SW_SHOWDEFAULT;
	LauncherLog("ShellExecuteEx start file=%s dir=%s params=%s",
		WideToUtf8Log(ShExecInfo.lpFile).c_str(),
		WideToUtf8Log(ShExecInfo.lpDirectory).c_str(),
		WideToUtf8Log(ShExecInfo.lpParameters).c_str());

	if (!ShellExecuteEx(&ShExecInfo))
	{
		LauncherLog("ShellExecuteEx failed error=%lu", GetLastError());
		SetQQSpeedNativeBridgeLastError(L"Failed to launch GameApp.exe.");
		return FALSE;
	}

	DWORD processId = GetProcessId(ShExecInfo.hProcess);
	hGameProcess = ShExecInfo.hProcess;
	SetQQSpeedNativeBridgeGameProcessId(processId);
	MarkLoaderProgressStage(eLoaderStageGameProcessCreated);
	LauncherLog("ShellExecuteEx success process=%p pid=%lu", hGameProcess, processId);
	LogGameProcessState("after ShellExecuteEx", hGameProcess);

	CloseSpeedHookConfigMapping();
	HANDLE speedHookCfgMap = CreateSpeedHookConfigMapping(processId);
	if (!speedHookCfgMap)
	{
		SetQQSpeedNativeBridgeLastError(L"Failed to create the SpeedHookCfg shared config.");
		TerminateGameProcess();
		return FALSE;
	}
	LogGameProcessState("after SpeedHookCfg", hGameProcess);

	HookPayloadSnapshot initialHook = {};
	if (!GetHookPayloadSnapshot(&initialHook))
	{
		SetQQSpeedNativeBridgeLastError(L"The authenticated in-memory Hook payload is unavailable.");
		CloseSpeedHookConfigMapping();
		TerminateGameProcess();
		return FALSE;
	}
	const BOOL hookMapped = ManualMapHookImage(hGameProcess, initialHook.Bytes, initialHook.Size);
	ReleaseHookPayloadSnapshot();
	if (!hookMapped)
	{
		CloseSpeedHookConfigMapping();
		LauncherLog("In-memory Hook mapping failed");
		TerminateGameProcess();
		return FALSE;
	}
	MarkLoaderProgressStage(eLoaderStageHookMapped);
	LogGameProcessState("after ManualMapHookImage", hGameProcess);

	LauncherLog("SpeedHookCfg handle kept alive after injection handle=%p", GetSpeedHookConfigMappingHandle());
	LogGameProcessState("after SpeedHookCfg keepalive", hGameProcess);
	if (!FollowReplacementGameProcesses())
	{
		TerminateGameProcess();
		return FALSE;
	}
	MarkLoaderProgressStage(eLoaderStageProcessStabilized);
	LauncherLog("GameApp process identity stabilized pid=%lu", GetProcessId(hGameProcess));
	return TRUE;
}
