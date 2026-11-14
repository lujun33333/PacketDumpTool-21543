#pragma once

#include <windows.h>
#include <string>

struct GameProcessMonitorContext
{
	HANDLE Process;
	HANDLE Event;
	const char* Stage;
	DWORD StartTick;
	LONG Stop;
};

DWORD GetGameSharedReadyValue();
void SetGameSharedReadyValue(DWORD value);
HWND FindGameTopLevelWindow(DWORD gameProcessId, BOOL requireVisible);
BOOL WaitForGameReady(DWORD gameProcessId, DWORD timeoutMs);

void CloseGameProcessHandle();
void TerminateGameProcess();
BOOL LogGameProcessState(const char* stage, HANDLE hProcess);

HANDLE StartGameProcessMonitor(GameProcessMonitorContext* ctx, HANDLE process, HANDLE eventHandle, const char* stage);
void StopGameProcessMonitor(HANDLE thread, GameProcessMonitorContext* ctx);

std::wstring WriteProcessMemoryInTargetProcess(DWORD processId);
BOOL LaunchGameProcess(HWND hWnd, TCHAR* buffer);
