#pragma once

#include "NativeBridge.h"

#ifndef MAX_LOADSTRING
#define MAX_LOADSTRING 100
#endif

extern HINSTANCE hInst;
extern WCHAR szTitle[MAX_LOADSTRING];
extern WCHAR szWindowClass[MAX_LOADSTRING];
extern HDC hMemDC;
extern HANDLE hLoaderBG;
extern HANDLE hLoaderProgressBack;
extern HANDLE hLoaderProgressFore;
extern HANDLE hRunSpeedGameEvent;
extern HANDLE hFileMap;
extern SharedData* pMapData;
extern HANDLE hGameProcess;
extern TCHAR GamePath[MAX_PATH];
extern TCHAR GameWorkDir[MAX_PATH];
extern TCHAR GameAppFile[MAX_PATH];

enum LoaderProgressStage : LONG
{
	eLoaderStageSharedMemoryReady = 1L << 0,
	eLoaderStageGameProcessCreated = 1L << 1,
	eLoaderStageHookMapped = 1L << 2,
	eLoaderStageProcessStabilized = 1L << 3,
	eLoaderStageAuthApplied = 1L << 4,
	eLoaderStageStartSignaled = 1L << 5,
	eLoaderStageQQServerAuthenticated = 1L << 6,
	eLoaderStageDirServerAuthenticated = 1L << 7,
	eLoaderStageGameServerAuthenticated = 1L << 8,
	eLoaderStageReady = 1L << 9,
};

void SetLoaderProgress(int progress);
void ResetLoaderProgressStages();
void MarkLoaderProgressStage(LoaderProgressStage stage);

// Shared helpers used by the DLL entry points and GameApp compatibility flow.
bool IsQQSpeedNativeBridgeCancelRequested();
void SetQQSpeedNativeBridgeLastError(const wchar_t* message);
void SetQQSpeedNativeBridgeLastErrorUtf8(const char* message);
void SetQQSpeedNativeBridgeGameProcessId(DWORD processId);
void SetQQSpeedNativeBridgeFailure(bool failed);
bool HasQQSpeedNativeBridgeFailure();
