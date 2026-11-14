#pragma once

#include <windows.h>

DWORD GetGameSharedReadyValue();
void SetGameSharedReadyValue(DWORD value);
HWND FindGameTopLevelWindow(DWORD gameProcessId, BOOL requireVisible);
BOOL WaitForGameReady(DWORD gameProcessId, DWORD timeoutMs);

void CloseGameProcessHandle();
void TerminateGameProcess();
BOOL LogGameProcessState(const char* stage, HANDLE hProcess);

BOOL LaunchGameProcess();
