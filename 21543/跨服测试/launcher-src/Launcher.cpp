// Launcher.cpp : application entry point.
//
#define _CRT_SECURE_NO_WARNINGS
#include "Launcher.h"
#include "LauncherIpc.h"
#include "LauncherLog.h"
#include "LauncherProcess.h"
//#include "MetaString.h"
#include <shlobj_core.h>
#include <shlwapi.h>
#pragma comment (lib,"Shlwapi.lib")
#include <shellapi.h>
#include <string>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <wincrypt.h>
#include <Psapi.h>
#include <tlhelp32.h>
#pragma comment (lib,"Shell32.lib")
#pragma comment (lib,"Advapi32.lib")

#include <windows.h>
#define MAX_LOADSTRING 100

// Global state.
HINSTANCE hInst;									// Current instance.
WCHAR szTitle[MAX_LOADSTRING];						// Title bar text.
WCHAR szWindowClass[MAX_LOADSTRING] = L"QQSpeed";	// Main window class name.
HDC hMemDC;
HANDLE hLoaderBG;
HANDLE hRunSpeedGameEvent = NULL;
HANDLE hFileMap = NULL;
SharedData* pMapData = NULL;
HANDLE hGameProcess = NULL;
TCHAR GamePath[MAX_PATH] = L".";
TCHAR LauncherDir[MAX_PATH] = L".";
TCHAR GameWorkDir[MAX_PATH] = L".";
TCHAR GameAppFile[MAX_PATH];

#ifndef QQSPEED_HOOK_DLL_NAME
#define QQSPEED_HOOK_DLL_NAME L"qqspeed_clean_hook_64.dll"
#endif

TCHAR HookDllFile[MAX_PATH] = L".\\" QQSPEED_HOOK_DLL_NAME;

UINT LoginUin;
char LoginPwd[128];
BYTE GameKey[16];
wchar_t* Msg = NULL;
extern char NewIP[64];
extern USHORT Port;
extern bool ExportRes(const std::wstring& exportPath, DWORD resourceId, LPCTSTR resourceType);
extern LPCTSTR resourceTypedb; // Resource type.
extern int Bit;

bool fileExists(const std::string& filePath) {
	std::ifstream file(filePath);
	return file.good();
}

static const DWORD kSharedDataMapSize = kKartMapSharedSize;
static const char kQQSpeedUserDirNameA[] = "\x51\x51\xB7\xC9\xB3\xB5";

static void LogSharedDataLayout()
{
#ifdef QQSpeed_x64
	unsigned long hWndHighOffset = (unsigned long)offsetof(SharedData, hWnd_);
#else
	unsigned long hWndHighOffset = 0xFFFFFFFFUL;
#endif
	LauncherLog(
		"SharedData layout size=0x%lX hWnd=0x%lX hWndHigh=0x%lX uin=0x%lX pwdMD5=0x%lX worldId=0x%lX userDir=0x%lX crc=0x%lX url=0x%lX key=0x%lX signKeyLen=0x%lX signKey=0x%lX clientKeyLen=0x%lX clientKey=0x%lX nickLen=0x%lX nick=0x%lX unknown2=0x%lX",
		(unsigned long)sizeof(SharedData),
		(unsigned long)offsetof(SharedData, hWnd),
		hWndHighOffset,
		(unsigned long)offsetof(SharedData, Uin),
		(unsigned long)offsetof(SharedData, PwdMD5),
		(unsigned long)offsetof(SharedData, WorldID),
		(unsigned long)offsetof(SharedData, UserDir),
		(unsigned long)offsetof(SharedData, CommodityListCRC),
		(unsigned long)offsetof(SharedData, CommodityListURL),
		(unsigned long)offsetof(SharedData, Key),
		(unsigned long)offsetof(SharedData, SignKeyLen),
		(unsigned long)offsetof(SharedData, SignKey),
		(unsigned long)offsetof(SharedData, ClientKeyLen),
		(unsigned long)offsetof(SharedData, ClientKey),
		(unsigned long)offsetof(SharedData, QQNickNameLength),
		(unsigned long)offsetof(SharedData, QQNickName),
		(unsigned long)offsetof(SharedData, Unknown2));
}

static void SetSharedDataHwnd(SharedData* sharedData, HWND hWnd)
{
	sharedData->hWnd = hWnd;
#ifdef QQSpeed_x64
	sharedData->hWnd_ = ((LONG)(LONG_PTR)hWnd < 0) ? -1 : 0;
#endif
}

static BOOL ResolveLauncherDirectory()
{
	DWORD len = GetModuleFileNameW(NULL, LauncherDir, MAX_PATH);
	if (len == 0 || len >= MAX_PATH)
	{
		LauncherLog("ResolveLauncherDirectory failed len=%lu error=%lu", len, GetLastError());
		return FALSE;
	}

	PathRemoveFileSpecW(LauncherDir);
	SetCurrentDirectoryW(LauncherDir);
	TCHAR configuredGameRoot[MAX_PATH] = {};
	GetPrivateProfileStringW(
		TEXT("ServerInfo1"), TEXT("GameRoot"), TEXT(""),
		configuredGameRoot, MAX_PATH, TEXT(".\\DirSvrInfo1.ini"));
	if (configuredGameRoot[0] != TEXT('\0'))
		lstrcpynW(GamePath, configuredGameRoot, MAX_PATH);
	else
		lstrcpynW(GamePath, LauncherDir, MAX_PATH);
	LauncherLog("LauncherDir=%s", WideToUtf8Log(LauncherDir).c_str());
	LauncherLog("GameRoot=%s", WideToUtf8Log(GamePath).c_str());
	return TRUE;
}

static void BuildGamePaths()
{
	LPCTSTR subDir = (Bit == 64) ? TEXT("Releasephysx27_64") : TEXT("Releasephysx27");
	wsprintf(GameWorkDir, TEXT("%s\\%s"), GamePath, subDir);
	wsprintf(GameAppFile, TEXT("%s\\GameApp.exe"), GameWorkDir);
	wsprintf(HookDllFile, TEXT("%s\\%s"), LauncherDir, QQSPEED_HOOK_DLL_NAME);
	LauncherLog("BuildGamePaths bit=%d workDir=%s game=%s hook=%s",
		Bit,
		WideToUtf8Log(GameWorkDir).c_str(),
		WideToUtf8Log(GameAppFile).c_str(),
		WideToUtf8Log(HookDllFile).c_str());
}

static void MaintainConfiguredGameWindowTitle(DWORD gameProcessId)
{
	wchar_t configuredTitle[128] = {};
	GetPrivateProfileStringW(
		L"ServerInfo1", L"WindowTitle", L"",
		configuredTitle, static_cast<DWORD>(_countof(configuredTitle)), L".\\DirSvrInfo1.ini");
	if (configuredTitle[0] == L'\0')
		return;

	LauncherLog("WindowTitle keeper start pid=%lu title=%s",
		gameProcessId, WideToUtf8Log(configuredTitle).c_str());
	for (;;)
	{
		HWND gameHwnd = FindGameTopLevelWindow(gameProcessId, TRUE);
		if (gameHwnd)
		{
			wchar_t currentTitle[256] = {};
			GetWindowTextW(gameHwnd, currentTitle, static_cast<int>(_countof(currentTitle)));
			if (lstrcmpW(currentTitle, configuredTitle) != 0)
			{
				const BOOL changed = SetWindowTextW(gameHwnd, configuredTitle);
				LauncherLog("WindowTitle corrected pid=%lu hwnd=%p changed=%d",
					gameProcessId, gameHwnd, changed ? 1 : 0);
			}
		}

		if (!hGameProcess || WaitForSingleObject(hGameProcess, 500) != WAIT_TIMEOUT)
			break;
	}
	LauncherLog("WindowTitle keeper finish pid=%lu", gameProcessId);
}

static BOOL EnsureBundledGameFiles()
{
	LauncherLog("Bundled game dll restore skipped; using external game files workDir=%s",
		WideToUtf8Log(GameWorkDir).c_str());
	return TRUE;
}

static BOOL EnsureHookDllPresent()
{
	wsprintf(HookDllFile, TEXT("%s\\%s"), LauncherDir, QQSPEED_HOOK_DLL_NAME);
	LauncherLog("External hook dll check name=%s path=%s",
		WideToUtf8Log(QQSPEED_HOOK_DLL_NAME).c_str(),
		WideToUtf8Log(HookDllFile).c_str());
	BOOL ok = PathFileExistsW(HookDllFile);
	LauncherLog("External hook dll check result=%d path=%s error=%lu", ok ? 1 : 0, WideToUtf8Log(HookDllFile).c_str(), GetLastError());
	return ok;
}

wchar_t* UTF8ToW(char* str, int len)
{
	wchar_t* result = new wchar_t[len + 1];
	len = MultiByteToWideChar(CP_UTF8, 0, str, len, result, len);
	result[len] = 0;
	return result;
}

char* WToUTF8(wchar_t* str, int len)
{
	int len2 = len * 6; // UTF-8 uses 1-6 bytes per character here.
	char* result = new char[len2 + 1];
	len2 = WideCharToMultiByte(CP_UTF8, 0, str, len, result, len2, 0, NULL);
	result[len2] = 0;
	return result;
}

wchar_t* AToW(char* str, int len)
{
	wchar_t* result = new wchar_t[len + 1];
	len = MultiByteToWideChar(CP_ACP, 0, str, len, result, len);
	result[len] = 0;
	return result;
}

char* WToA(wchar_t* str, int len)
{
	int len2 = len * 2; // ANSI uses 1-2 bytes per character here.
	char* result = new char[len2 + 1];
	len2 = WideCharToMultiByte(CP_ACP, 0, str, len, result, len2, 0, NULL);
	result[len2] = 0;
	return result;
}


VOID myprintf(const char* format, ...)
{
	va_list ap;
	va_start(ap, format);
	char bufer[1024];
	memset(bufer, 0, sizeof(bufer));
	int length = wvsprintfA(bufer, format, ap);
	va_end(ap);

	OutputDebugStringA(bufer);
}

void OnCopyData(HWND hWnd, COPYDATASTRUCT* pCopyDataStruct)
{
	//printf("pCopyDataStruct->lpData:%d\n", pCopyDataStruct->lpData);
	if (!pCopyDataStruct)
	{
		LauncherLog("OnCopyData skipped: null COPYDATASTRUCT");
		return;
	}

	LauncherLog("OnCopyData hwnd=%p dwData=%llu cbData=%lu lpData=%p",
		hWnd,
		(unsigned long long)pCopyDataStruct->dwData,
		pCopyDataStruct->cbData,
		pCopyDataStruct->lpData);
	
	switch (pCopyDataStruct->dwData)
	{
	case 1: //EDT_NotifyLoginResult 
	{
		if (!pCopyDataStruct->lpData || pCopyDataStruct->cbData < sizeof(NotifyLoginResult))
		{
			LauncherLog("OnCopyData login result invalid payload cbData=%lu expected=%zu",
				pCopyDataStruct->cbData, sizeof(NotifyLoginResult));
			break;
		}

		NotifyLoginResult* pLoginResult = (NotifyLoginResult*)pCopyDataStruct->lpData;
		LauncherLog("NotifyLoginResult hwnd=%p entity=%lu result=%d error=%s",
			pLoginResult->hWnd,
			pLoginResult->Entity,
			pLoginResult->LoginResult,
			pLoginResult->ErrorMessage);

		LPCSTR pstrMsg = NULL;
		switch (pLoginResult->LoginResult)
		{
		case eLoginSuccessed:
		case eLoginRequireReg:
		case eLoginVersionRecommend:
			break;
		case eLoginFailed:
			pstrMsg = "Login failed. Check the user name and password.";
			break;
		case eLoginTimeout:
			pstrMsg = "Login timed out. Check the network connection.";
			break;
		case eLoginCanceled:
			pstrMsg = "Login was canceled.";
			break;
		case eLoginIdentityLimit:
			pstrMsg = "Login is blocked by identity restrictions.";
			break;
		case eLoginVersionLimit:
			pstrMsg = "Login is blocked by version restrictions.";
			break;
		case eLoginFileCracked:
			pstrMsg = "Client files are damaged. Login is not available.";
			break;
		default:
			pstrMsg = "Unknown error.";
			break;
		}
		if (strlen(pLoginResult->ErrorMessage) > 0)
		{
			pstrMsg = pLoginResult->ErrorMessage;
		}
		if (pstrMsg)
		{
			LauncherLog("NotifyLoginResult message=%s", pstrMsg);
			MessageBoxA(pMapData ? pMapData->hWnd : hWnd, pstrMsg, nullptr, MB_OK | MB_ICONHAND);
			if (pMapData && pMapData->hWnd)
			{
				DestroyWindow(pMapData->hWnd);
			}
		}
		else
		{
			if (pLoginResult->Entity == eGameServer)
			{
				SetGameSharedReadyValue(1);
				LauncherLog("GameServer login success; ready flag set to %lu and showing game hwnd=%p",
					GetGameSharedReadyValue(), pLoginResult->hWnd);
				ShowWindow(pLoginResult->hWnd, SW_SHOW);
				if (pMapData && pMapData->hWnd)
				{
					DestroyWindow(pMapData->hWnd);
				}
			}
			else
			{
				LauncherLog("NotifyLoginResult success entity=%lu; waiting for GameServer result", pLoginResult->Entity);
			}
		}
		break;
	}
	case 2: // Status text.
	{
		size_t len = pCopyDataStruct->cbData;
		char* str = (char*)pCopyDataStruct->lpData;
		if (str)
		{
			LauncherLog("Status text update len=%zu text=%.*s", len, (int)len, str);
			wchar_t* temp = Msg;
			Msg = NULL;
			if (temp)
			{
				delete[] temp;
			}
#ifdef ZingSpeed
			temp = new wchar_t[len + 1];
			memcpy(temp, str, len);
			temp[len] = 0;
			Msg = temp;
#else
			Msg = AToW(str, (int)len);
#endif
			//InvalidateRect(pMapData->hWnd, NULL, FALSE);
			//MessageBoxA(pMapData->hWnd, , nullptr, MB_OK | MB_ICONHAND);
		}
		break;
	}
	case 3:
		LauncherLog("OnCopyData type=3 ignored");
		
		break;
	case 5: //?
		LauncherLog("OnCopyData type=5 ignored");
		break;
	case 6:
		LauncherLog("OnCopyData type=6 ignored");
		break;
	default:
		LauncherLog("OnCopyData unknown type=%llu", (unsigned long long)pCopyDataStruct->dwData);
		break;
	}
}

static BOOL ForceNotifyLoginResultFromGameWindow(HWND launcherHwnd, DWORD gameProcessId)
{
	HWND gameHwnd = FindGameTopLevelWindow(gameProcessId, FALSE);
	LauncherLog("ForceNotifyLoginResult probe pid=%lu hwnd=%p visible=%d iconic=%d",
		gameProcessId,
		gameHwnd,
		gameHwnd ? (IsWindowVisible(gameHwnd) ? 1 : 0) : 0,
		gameHwnd ? (IsIconic(gameHwnd) ? 1 : 0) : 0);
	if (!gameHwnd)
	{
		return FALSE;
	}

	NotifyLoginResult loginResult = {};
	loginResult.hWnd = gameHwnd;
#ifdef QQSpeed_x64
	loginResult.hWnd_ = 0;
#endif
	loginResult.Entity = eGameServer;
	loginResult.LoginResult = eLoginSuccessed;

	COPYDATASTRUCT copyData = {};
	copyData.dwData = 1;
	copyData.cbData = sizeof(loginResult);
	copyData.lpData = &loginResult;

	LauncherLog("ForceNotifyLoginResult synthesizing dwData=1 hwnd=%p cbData=%lu",
		gameHwnd, copyData.cbData);
	OnCopyData(launcherHwnd, &copyData);
	return TRUE;
}

void Cleanup()
{
	LauncherLog("Cleanup start map=%p fileMap=%p event=%p gameProcess=%p speedHookCfg=%p loaderBG=%p memDC=%p",
		pMapData, hFileMap, hRunSpeedGameEvent, hGameProcess, GetSpeedHookConfigMappingHandle(), hLoaderBG, hMemDC);
	CloseLoaderAuxiliaryMappings();
	if (pMapData)
	{
		UnmapViewOfFile(pMapData);
		pMapData = NULL;
	}
	if (hFileMap)
	{
		CloseHandle(hFileMap);
		hFileMap = NULL;
	}
	if (hRunSpeedGameEvent)
	{
		CloseHandle(hRunSpeedGameEvent);
		hRunSpeedGameEvent = NULL;
	}
	CloseSpeedHookConfigMapping();
	CloseSpeedHackAuthTokenMapping();
	CloseGameProcessHandle();
	if (hLoaderBG)
	{
		DeleteObject(hLoaderBG);
		hLoaderBG = NULL;
	}
	if (hMemDC)
	{
		DeleteDC(hMemDC);
		hMemDC = NULL;
	}
	LauncherLog("Cleanup finish");
}

// Write new file content and preserve the original modification time.
void writeFileContent(const std::string& filename, const std::string& newContent) {
	std::wstring wideFilename(filename.begin(), filename.end());
	HANDLE hFile = CreateFileW(wideFilename.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	FILETIME originalModifiedTime;
	GetFileTime(hFile, NULL, NULL, &originalModifiedTime);
	CloseHandle(hFile);

	std::ofstream fout(filename);
	fout << newContent;
	fout.close();

	hFile = CreateFileW(wideFilename.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	SetFileTime(hFile, NULL, NULL, &originalModifiedTime);
	CloseHandle(hFile);
}

std::string recordFileContent(const std::string& filename) {
	std::ifstream fin(filename);
	std::string content;
	getline(fin, content, '\0');
	fin.close();
	return content;
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance;
	TCHAR Buffer[1024];
	LauncherLog("InitInstance start hInstance=%p nCmdShow=%d", hInstance, nCmdShow);
	GameProcessMonitorContext postLaunchMonitor = {};
	HANDLE hPostLaunchMonitor = NULL;

	if (!ResolveLauncherDirectory())
	{
		MessageBox(nullptr, TEXT("Failed to initialize the launcher directory."), szTitle, MB_ICONERROR);
		return FALSE;
	}

	// User.db is managed by the installed client; do not overwrite it on startup.
	LauncherLog("User.db resource export skipped");
	BuildGamePaths();
	if (!EnsureHookDllPresent())
	{
		TCHAR message[256] = {};
		wsprintf(message, TEXT("Missing external hook dll: %s."), QQSPEED_HOOK_DLL_NAME);
		MessageBox(nullptr, message, szTitle, MB_ICONERROR);
		return FALSE;
	}

	if (!EnsureBundledGameFiles())
	{
		MessageBox(nullptr, TEXT("Failed to prepare external game files."), szTitle, MB_ICONERROR);
		return FALSE;
	}
	
	if (!PathFileExists(GameAppFile))
	{
		LauncherLog("GameApp not found at initial path=%s", WideToUtf8Log(GameAppFile).c_str());
		DWORD sizo = MAX_PATH;
		if (ERROR_SUCCESS == RegGetValue(HKEY_LOCAL_MACHINE,
#ifdef ZingSpeed
			TEXT("Software\\VinaGame\\Zing Speed\\SYS"), TEXT("PathRoot")
#else
			TEXT("SOFTWARE\\Tencent\\QQ\u98DE\u8F66\\SYS"), TEXT("PathRoot")
#endif
			, RRF_RT_REG_SZ | RRF_ZEROONFAILURE, NULL, &GamePath, &sizo))
		{

			BuildGamePaths();
			if (!EnsureBundledGameFiles())
			{
				MessageBox(nullptr, TEXT("Failed to prepare external game files."), szTitle, MB_ICONERROR);
				return FALSE;
			}
			if (!PathFileExists(GameAppFile))
			{
				LauncherLog("GameApp still missing after registry path=%s", WideToUtf8Log(GameAppFile).c_str());
				MessageBox(nullptr, TEXT("GameApp.exe was not found. Put this launcher in the game directory and try again."), szTitle, MB_ICONERROR);
				return FALSE;
			}
			LauncherLog("GameApp found through registry path=%s", WideToUtf8Log(GameAppFile).c_str());
			wsprintf(Buffer, TEXT("The launch directory is not correct, but the game directory was found:\n%s\n\nLaunch the game from this directory?"), GamePath);
			if (MessageBox(nullptr, Buffer, szTitle, MB_ICONINFORMATION | MB_YESNO) == IDNO)
			{
				LauncherLog("User declined registry game path");
				return FALSE;
			}
		}
		else
		{
			LauncherLog("GameApp missing and registry lookup failed error=%lu", GetLastError());
			MessageBox(nullptr, TEXT("GameApp.exe was not found. Put this launcher in the game directory and try again."), szTitle, MB_ICONERROR);
			return FALSE;
		}
	}
	else
	{
		LauncherLog("GameApp found path=%s", WideToUtf8Log(GameAppFile).c_str());
	}


	wsprintf(Buffer, TEXT("%s\\Loader\\LoaderBG.bmp"), GamePath);
	hLoaderBG = LoadImage(nullptr, Buffer, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
	hMemDC = CreateCompatibleDC(NULL);
	SelectObject(hMemDC, hLoaderBG);
	LauncherLog("Loader background path=%s image=%p memDC=%p", WideToUtf8Log(Buffer).c_str(), hLoaderBG, hMemDC);


	INT x = 0; INT y = 0; INT w = 300; INT h = 200;
	RECT WorkAreaRect = { 0,0,0,0 };
	SystemParametersInfo(SPI_GETWORKAREA, 0, &WorkAreaRect, 0);
	x = (WorkAreaRect.right - w) / 2 + x;
	y = (WorkAreaRect.bottom - h) / 2 + y;
	HWND hWnd = CreateWindowW(szWindowClass, szTitle, WS_POPUP, x, y, w, h, nullptr, nullptr, hInstance, nullptr);
	if (!hWnd)
	{
		LauncherLog("CreateWindowW loader failed error=%lu", GetLastError());
		return FALSE;
	}
	LauncherLog("Loader window created hwnd=%p", hWnd);



	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);


	char eventName[260] = { 0 };
	snprintf(eventName, sizeof(eventName), "RunSpeedGameEvent_%u", GetCurrentProcessId());
	hRunSpeedGameEvent = CreateEventA(NULL, TRUE, FALSE, eventName);
	if (!hRunSpeedGameEvent)
	{
		LauncherLog("CreateEvent failed name=%s error=%lu", eventName, GetLastError());
		MessageBox(hWnd, TEXT("Failed to create the launch event."), szTitle, MB_ICONERROR);
		DestroyWindow(hWnd);
		return FALSE;
	}
	LauncherLog("RunSpeedGameEvent created name=%s handle=%p", eventName, hRunSpeedGameEvent);

	LauncherLog("GameApp launch delayed until KartMap_Shared is filled");
	LogSharedDataLayout();

	const char* mapBaseName =
#ifdef ZingSpeed
		"ZingSpeedMap_Shared";
#else
		"KartMap_Shared";
#endif
	char mapName[260] = { 0 };
	snprintf(mapName, sizeof(mapName), "%s_%u", mapBaseName, GetCurrentProcessId());
	LauncherLog("Create shared memory name=%s size=%lu", mapName, kSharedDataMapSize);
	hFileMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, kSharedDataMapSize, mapName);
	if (!hFileMap)
	{
		LauncherLog("CreateFileMapping shared memory failed error=%lu", GetLastError());
		MessageBox(hWnd, L"Failed to create shared memory. The client cannot start.", NULL, MB_ICONHAND | MB_OK);
		DestroyWindow(hWnd);
		return FALSE;
	}
	pMapData = (SharedData*)MapViewOfFile(hFileMap, FILE_MAP_WRITE, 0, 0, kSharedDataMapSize);
	if (!pMapData)
	{
		LauncherLog("MapViewOfFile shared memory failed error=%lu", GetLastError());
		CloseHandle(hFileMap);
		MessageBox(hWnd, L"Failed to map shared memory. The client cannot start.", NULL, MB_ICONHAND | MB_OK);
		DestroyWindow(hWnd);
		return FALSE;
	}
	else
	{
		LauncherLog("Shared memory mapped view=%p", pMapData);
		LauncherLog("Shared memory mapped before GameApp launch");
		if (!CreateLoaderAuxiliaryMappings())
		{
			MessageBox(hWnd, L"Failed to create loader auxiliary shared memory. The client cannot start.", NULL, MB_ICONHAND | MB_OK);
			DestroyWindow(hWnd);
			return FALSE;
		}
		LauncherLog("LoginDialog start");
		GameProcessMonitorContext loginMonitor = {};
		HANDLE hLoginMonitor = hGameProcess ? StartGameProcessMonitor(&loginMonitor, hGameProcess, hRunSpeedGameEvent, "LoginDialog") : NULL;
		LoginDialog(hWnd);
		StopGameProcessMonitor(hLoginMonitor, &loginMonitor);
		LauncherLog("LoginDialog returned LoginUin=%u", LoginUin);
		LauncherLog("LoginDialog completed before GameApp launch");

		if (LoginUin == 0)
		{
			LauncherLog("LoginUin is zero; terminating GameApp");
			TerminateGameProcess();
			DestroyWindow(hWnd);
			return FALSE;
		}

		char MyDocuments[MAX_PATH];
		ZeroMemory(pMapData, kSharedDataMapSize);
		SetSharedDataHwnd(pMapData, hWnd);
		pMapData->Uin = LoginUin;
		pMapData->WorldID = 1;
		SetGameSharedReadyValue(0);
#ifdef _WIN64
#else
		//std::string contents = "[ServerInfo1]\nIP=";
		//const std::string port = std::to_string(Port) + std::string(",") + std::to_string(Port) + std::string(",") + std::to_string(Port);
		//contents.append(NewIP).append("\nDomain =").append(NewIP).append("\nPorts =").append(port).append("\nServerID = 2");
		////const std::string contents = "[ServerInfo1]\nIP=" + ip + "\nDomain = 183.131.83.194\nPorts = 27000, 27000, 27000\nServerID = 2";
		//writeFileContent(".\\DirSvrInfo1.ini", contents);
#endif

		if (SHGetSpecialFolderPathA(NULL, MyDocuments, CSIDL_MYDOCUMENTS, false))
		{
			wsprintfA(pMapData->UserDir,
#ifdef ZingSpeed
				"%s\\Zing Speed\\%d"
#else
				"%s\\%s\\%d"
#endif
				, MyDocuments,
#ifdef ZingSpeed
				pMapData->Uin
#else
				kQQSpeedUserDirNameA, pMapData->Uin
#endif
			);
		}
		else
		{
			LauncherLog("SHGetSpecialFolderPathA(CSIDL_MYDOCUMENTS) failed error=%lu", GetLastError());
		}
		pMapData->CommodityListCRC = 0;
		//lstrcpyA(pMapData->CommodityListURL, "http://update.zing.vcdn.vn/update/zs/commoditylist.201703231746.save.spd");
		memcpy(pMapData->Key, GameKey, 16);
		pMapData->SignKeyLen = (int)strlen(lstrcpyA(pMapData->SignKey, LoginPwd));
		LauncherLog("SharedData filled hwnd=%p uin=%lu worldId=%lu userDir=%s signKeyLen=%d readyFlag=%lu",
			pMapData->hWnd,
			pMapData->Uin,
			pMapData->WorldID,
			pMapData->UserDir,
			pMapData->SignKeyLen,
			GetGameSharedReadyValue());

		char launcherTokenLabel[64] = { 0 };
		snprintf(launcherTokenLabel, sizeof(launcherTokenLabel), "launcher_%lu", GetCurrentProcessId());
		if (!WriteSpeedHackAuthToken(launcherTokenLabel))
		{
			MessageBox(hWnd, TEXT("Failed to create the SpeedHackAuthToken shared memory."), szTitle, MB_ICONERROR);
			DestroyWindow(hWnd);
			return FALSE;
		}

		if (!LaunchGameProcess(hWnd, Buffer))
		{
			DestroyWindow(hWnd);
			return FALSE;
		}
		hPostLaunchMonitor = StartGameProcessMonitor(&postLaunchMonitor, hGameProcess, hRunSpeedGameEvent, "PostLaunchGameReady");
		LogGameProcessState("after delayed GameApp launch", hGameProcess);

		char uinTokenLabel[64] = { 0 };
		snprintf(uinTokenLabel, sizeof(uinTokenLabel), "%u", LoginUin);
		if (!WriteSpeedHackAuthToken(uinTokenLabel))
		{
			MessageBox(hWnd, TEXT("Failed to update the SpeedHackAuthToken shared memory."), szTitle, MB_ICONERROR);
			StopGameProcessMonitor(hPostLaunchMonitor, &postLaunchMonitor);
			TerminateGameProcess();
			DestroyWindow(hWnd);
			return FALSE;
		}
		LogGameProcessState("after SpeedHackAuthToken uin write", hGameProcess);

	}
	LogGameProcessState("before Network patch", hGameProcess);
	std::wstring patchError = WriteProcessMemoryInTargetProcess(GetProcessId(hGameProcess));
	if (!patchError.empty())
	{
		LauncherLog("Patch failed message=%s", WideToUtf8Log(patchError.c_str()).c_str());
		MessageBoxW(hWnd, patchError.c_str(), szTitle, MB_ICONERROR);
		StopGameProcessMonitor(hPostLaunchMonitor, &postLaunchMonitor);
		TerminateGameProcess();
		DestroyWindow(hWnd);
		return FALSE;
	}
	LauncherLog("SetEvent RunSpeedGameEvent handle=%p", hRunSpeedGameEvent);
	if (!SetEvent(hRunSpeedGameEvent))
	{
		LauncherLog("SetEvent failed error=%lu", GetLastError());
	}
	else
	{
		LauncherLog("SetEvent succeeded");
	}
	BOOL gameReady = WaitForGameReady(GetProcessId(hGameProcess), INFINITE);
	if (!gameReady)
	{
		DWORD exitCode = 0;
		DWORD gamePid = GetProcessId(hGameProcess);
		BOOL gameAlive = hGameProcess && GetExitCodeProcess(hGameProcess, &exitCode) && exitCode == STILL_ACTIVE;
		LauncherLog("WaitForGameReady ended before ready pid=%lu alive=%d exitCode=%lu",
			gamePid, gameAlive ? 1 : 0, exitCode);
	}
	StopGameProcessMonitor(hPostLaunchMonitor, &postLaunchMonitor);
	LauncherLog("Post-login wait completed ready=%d", gameReady ? 1 : 0);
	if (gameReady)
		MaintainConfiguredGameWindowTitle(GetProcessId(hGameProcess));
	if (IsWindow(hWnd))
	{
		LauncherLog("Destroy loader window after game ready hwnd=%p", hWnd);
		DestroyWindow(hWnd);
	}

	LauncherLog("InitInstance finish success");
	return TRUE;
}




LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		switch (wmId)
		{
		case IDM_ABOUT:
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;
	}
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);
		BitBlt(hdc, 0, 0, 300, 200, hMemDC, 0, 0, SRCCOPY | CAPTUREBLT);
		if (Msg)
		{
			RECT Rect = { 0,0,300,200 };
			SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
			SetBkColor(hdc, RGB(0, 0, 0));
			SetTextColor(hdc, RGB(255, 255, 255));
			DrawTextExW(hdc, Msg, -1, &Rect, DT_NOPREFIX | DT_NOCLIP | DT_EDITCONTROL, NULL);
		}
		EndPaint(hWnd, &ps);
		break;
	}
	case WM_COPYDATA:
		OnCopyData((HWND)wParam, (COPYDATASTRUCT*)lParam);
		break;
	case WM_DESTROY:
		LauncherLog("WM_DESTROY hwnd=%p map=%p mapHwnd=%p", hWnd, pMapData, pMapData ? pMapData->hWnd : NULL);
		if (pMapData && pMapData->hWnd && pMapData->hWnd != hWnd)
		{
			LauncherLog("WM_DESTROY destroying mapped hwnd=%p", pMapData->hWnd);
			DestroyWindow(pMapData->hWnd);
		}
		PostQuitMessage(0);
		LauncherLog("WM_DESTROY posted quit");
		break;
	case WM_LBUTTONDOWN:
		SendMessage(hWnd, WM_SYSCOMMAND, SC_MOVE | HTCAPTION, 0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}



ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_LAUNCHER));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = nullptr;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = nullptr;

	return RegisterClassExW(&wcex);
}



int APIENTRY wWinMain(
	_In_ HINSTANCE		hInstance,
	_In_opt_ HINSTANCE	hPrevInstance,
	_In_ LPWSTR			lpCmdLine,
	_In_ int			nCmdShow
)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);




	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LauncherLog("wWinMain start");

	MyRegisterClass(hInstance);

	char server[127] = { 0 };
	GetPrivateProfileStringA("ServerInfo1", "server", "64", server, 127, ".\\ASpeed.ini");
	Bit = atoi(server) == 32 ? 32 : 64;
	LauncherLog("Bit selected=%d rawServerConfig=%s", Bit, server);
	//MessageBox(nullptr, std::to_wstring(Bit).c_str(), szTitle, MB_ICONERROR);
	//std::string filename = ".\\DirSvrInfo1.ini";
	//std::string originalContent = recordFileContent(filename);

	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_LAUNCHER));

	MSG msg;

	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	LauncherLog("Message loop exited wParam=%lld", (long long)msg.wParam);
	//writeFileContent(filename, originalContent);
	Cleanup();
	/*remove(".//User.db");
	remove(".//" QQSPEED_HOOK_DLL_NAME);
	ExportRes(L".//User.db", IDR_DB3, resourceTypedb);*/
	return (int)msg.wParam;
}
