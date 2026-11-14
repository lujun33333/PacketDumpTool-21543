#pragma once

#include "Launcher.h"

#ifndef MAX_LOADSTRING
#define MAX_LOADSTRING 100
#endif

extern HINSTANCE hInst;
extern WCHAR szTitle[MAX_LOADSTRING];
extern WCHAR szWindowClass[MAX_LOADSTRING];
extern HDC hMemDC;
extern HANDLE hLoaderBG;
extern HANDLE hRunSpeedGameEvent;
extern HANDLE hFileMap;
extern SharedData* pMapData;
extern HANDLE hGameProcess;
extern TCHAR GamePath[MAX_PATH];
extern TCHAR LauncherDir[MAX_PATH];
extern TCHAR GameWorkDir[MAX_PATH];
extern TCHAR GameAppFile[MAX_PATH];
extern TCHAR HookDllFile[MAX_PATH];
extern UINT LoginUin;
extern char LoginPwd[128];
extern BYTE GameKey[16];
extern wchar_t* Msg;

extern char NewIP[64];
extern USHORT Port;
extern int Bit;
extern LPCTSTR resourceTypedb;

extern bool ExportRes(const std::wstring& exportPath, DWORD resourceId, LPCTSTR resourceType);
