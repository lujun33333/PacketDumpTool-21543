#define _CRT_SECURE_NO_WARNINGS
#include "NativeBridgeIpc.h"

#include "NativeBridgeLog.h"
#include "NativeBridgeState.h"

#include <cctype>
#include <cstdio>
#include <cstring>

static const DWORD kSpeedHackAuthTokenSize = 0x100;
static const DWORD kPreDownloadSharedSize = 0x19D2;
static const DWORD kCopyDataBridgeSize = 0x615;
static const DWORD kGLauncherPDLSharedSize = 0x19DA;

struct SpeedHookConfig
{
	char Host[64];
	USHORT Port;
};

static_assert(sizeof(SpeedHookConfig) == 0x42, "SpeedHookCfg layout must match hook");

static HANDLE hSpeedHackAuthTokenMap = NULL;
static LPVOID pSpeedHackAuthTokenView = NULL;
static HANDLE hSpeedHookCfgMap = NULL;
static SpeedHookConfig gSpeedHookTarget = {};

static HANDLE hPreDownloadMap = NULL;
static LPVOID pPreDownloadView = NULL;
static HANDLE hCopyDataBridgeMap = NULL;
static LPVOID pCopyDataBridgeView = NULL;
static HANDLE hCopyDataNamedMap = NULL;
static LPVOID pCopyDataNamedView = NULL;
static HANDLE hGLauncherPDLMap = NULL;
static LPVOID pGLauncherPDLView = NULL;
static HANDLE hCopyDataBridgeThread = NULL;
static volatile LONG gCopyDataBridgeStop = 0;

void CloseSpeedHackAuthTokenMapping()
{
	if (pSpeedHackAuthTokenView)
	{
		UnmapViewOfFile(pSpeedHackAuthTokenView);
		pSpeedHackAuthTokenView = NULL;
	}
	if (hSpeedHackAuthTokenMap)
	{
		CloseHandle(hSpeedHackAuthTokenMap);
		hSpeedHackAuthTokenMap = NULL;
	}
}

void CloseSpeedHookConfigMapping()
{
	if (hSpeedHookCfgMap)
	{
		LauncherLog("Close SpeedHookCfg handle=%p", hSpeedHookCfgMap);
		CloseHandle(hSpeedHookCfgMap);
		hSpeedHookCfgMap = NULL;
	}
}

HANDLE GetSpeedHookConfigMappingHandle()
{
	return hSpeedHookCfgMap;
}

BOOL WriteSpeedHackAuthToken(const char* label)
{
	if (!label || !label[0])
	{
		LauncherLog("SpeedHackAuthToken write skipped empty label");
		return FALSE;
	}

	CloseSpeedHackAuthTokenMapping();

	hSpeedHackAuthTokenMap = CreateFileMappingA(
		INVALID_HANDLE_VALUE,
		NULL,
		PAGE_READWRITE,
		0,
		kSpeedHackAuthTokenSize,
		"SpeedHackAuthToken");
	if (!hSpeedHackAuthTokenMap)
	{
		LauncherLog("SpeedHackAuthToken CreateFileMappingA failed labelLen=%zu error=%lu", strlen(label), GetLastError());
		return FALSE;
	}
	DWORD createError = GetLastError();

	pSpeedHackAuthTokenView = MapViewOfFile(
		hSpeedHackAuthTokenMap,
		FILE_MAP_WRITE,
		0,
		0,
		kSpeedHackAuthTokenSize);
	if (!pSpeedHackAuthTokenView)
	{
		LauncherLog("SpeedHackAuthToken MapViewOfFile failed labelLen=%zu error=%lu", strlen(label), GetLastError());
		CloseHandle(hSpeedHackAuthTokenMap);
		hSpeedHackAuthTokenMap = NULL;
		return FALSE;
	}

	char token[kSpeedHackAuthTokenSize] = { 0 };
	int written = snprintf(
		token,
		sizeof(token),
		"VALID_%s_%llu",
		label,
		(unsigned long long)GetTickCount64());
	if (written <= 0 || written >= (int)sizeof(token))
	{
		LauncherLog("SpeedHackAuthToken token format failed labelLen=%zu written=%d", strlen(label), written);
		SecureZeroMemory(token, sizeof(token));
		CloseSpeedHackAuthTokenMapping();
		return FALSE;
	}

	ZeroMemory(pSpeedHackAuthTokenView, kSpeedHackAuthTokenSize);
	memcpy(pSpeedHackAuthTokenView, token, (size_t)written);
	LauncherLog("SpeedHackAuthToken write labelLen=%zu tokenLen=%d existed=%d map=%p view=%p",
		strlen(label),
		written,
		createError == ERROR_ALREADY_EXISTS ? 1 : 0,
		hSpeedHackAuthTokenMap,
		pSpeedHackAuthTokenView);
	SecureZeroMemory(token, sizeof(token));
	return TRUE;
}

BOOL ConfigureSpeedHookTarget(const char* host, USHORT port)
{
	if (!host || !host[0] || port == 0)
	{
		return FALSE;
	}

	const size_t hostLength = strnlen_s(host, sizeof(gSpeedHookTarget.Host));
	if (hostLength == 0 || hostLength >= sizeof(gSpeedHookTarget.Host))
	{
		return FALSE;
	}
	for (size_t index = 0; index < hostLength; ++index)
	{
		const unsigned char value = static_cast<unsigned char>(host[index]);
		if (!(std::isalnum(value) || value == '.' || value == '-' || value == '_' || value == ':'))
		{
			return FALSE;
		}
	}

	ZeroMemory(&gSpeedHookTarget, sizeof(gSpeedHookTarget));
	memcpy(gSpeedHookTarget.Host, host, hostLength);
	gSpeedHookTarget.Port = port;
	return TRUE;
}

void ClearSpeedHookTarget()
{
	SecureZeroMemory(&gSpeedHookTarget, sizeof(gSpeedHookTarget));
}

HANDLE CreateSpeedHookConfigMapping(DWORD gameProcessId)
{
	CloseSpeedHookConfigMapping();

	if (!gSpeedHookTarget.Host[0] || gSpeedHookTarget.Port == 0)
	{
		LauncherLog("CreateSpeedHookConfigMapping rejected: hook target is not configured");
		return NULL;
	}
	SpeedHookConfig cfg = gSpeedHookTarget;

	wchar_t name[64] = { 0 };
	wsprintfW(name, L"SpeedHookCfg_%lu", gameProcessId);
	LauncherLog("CreateSpeedHookConfigMapping name=%s host=%s port=%u size=%zu",
		WideToUtf8Log(name).c_str(), cfg.Host, cfg.Port, sizeof(cfg));

	HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(cfg), name);
	if (!hMap)
	{
		LauncherLog("CreateFileMappingW SpeedHookCfg failed error=%lu", GetLastError());
		return NULL;
	}

	LPVOID view = MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, sizeof(cfg));
	if (!view)
	{
		LauncherLog("MapViewOfFile SpeedHookCfg failed error=%lu", GetLastError());
		CloseHandle(hMap);
		return NULL;
	}

	memcpy(view, &cfg, sizeof(cfg));
	UnmapViewOfFile(view);
	hSpeedHookCfgMap = hMap;
	LauncherLog("SpeedHookCfg ready handle=%p", hSpeedHookCfgMap);
	return hSpeedHookCfgMap;
}

static void BuildLoaderSharedName(char* out, size_t outSize, const char* baseName)
{
	snprintf(out, outSize, "%s_%u", baseName, GetCurrentProcessId());
}

static BOOL CreateLoaderSharedMapping(const char* label, const char* baseName, DWORD size, HANDLE* handle, LPVOID* view, BOOL zeroMemory)
{
	char name[260] = { 0 };
	BuildLoaderSharedName(name, sizeof(name), baseName);
	LauncherLog("Create loader shared mapping label=%s name=%s size=%lu", label, name, size);

	HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, size, name);
	if (!map)
	{
		LauncherLog("Create loader shared mapping failed label=%s error=%lu", label, GetLastError());
		return FALSE;
	}

	LPVOID mapped = MapViewOfFile(map, FILE_MAP_WRITE, 0, 0, size);
	if (!mapped)
	{
		LauncherLog("Map loader shared mapping failed label=%s error=%lu", label, GetLastError());
		CloseHandle(map);
		return FALSE;
	}

	if (zeroMemory)
	{
		ZeroMemory(mapped, size);
	}
	*handle = map;
	*view = mapped;
	return TRUE;
}

static void CloseLoaderSharedMapping(const char* label, HANDLE* handle, LPVOID* view)
{
	if (*view)
	{
		LauncherLog("Unmap loader shared mapping label=%s view=%p", label, *view);
		UnmapViewOfFile(*view);
		*view = NULL;
	}
	if (*handle)
	{
		LauncherLog("Close loader shared mapping label=%s handle=%p", label, *handle);
		CloseHandle(*handle);
		*handle = NULL;
	}
}

static BOOL DispatchCopyDataBridgeMessage(LPBYTE view, const char* label)
{
	if (!view)
	{
		return FALSE;
	}

	DWORD dwData = *(DWORD*)view;
	if (!dwData)
	{
		return FALSE;
	}

	COPYDATASTRUCT copyData = {};
	copyData.dwData = dwData;
	if (dwData == 1)
	{
		copyData.cbData = 1041;
		copyData.lpData = view + sizeof(DWORD);
	}
	else
	{
		char* text = (char*)view + 1045;
		size_t len = 0;
		while (len < 512 && text[len])
		{
			++len;
		}
		copyData.cbData = (DWORD)len + 1;
		copyData.lpData = text;
	}

	HWND launcherHwnd = pMapData ? pMapData->hWnd : NULL;
	LauncherLog("Copydata bridge dispatch label=%s hwnd=%p dwData=%lu cbData=%lu lpData=%p",
		label, launcherHwnd, dwData, copyData.cbData, copyData.lpData);
	if (launcherHwnd && IsWindow(launcherHwnd))
	{
		SendMessageA(launcherHwnd, WM_COPYDATA, 0, (LPARAM)&copyData);
	}
	else
	{
		LauncherLog("Copydata bridge skipped send: launcher hwnd unavailable");
	}

	ZeroMemory(view, kCopyDataBridgeSize);
	return TRUE;
}

static DWORD WINAPI CopyDataBridgeThread(LPVOID)
{
	LauncherLog("Copydata bridge worker started");
	while (InterlockedCompareExchange(&gCopyDataBridgeStop, 0, 0) == 0)
	{
		BOOL dispatched = FALSE;
		dispatched |= DispatchCopyDataBridgeMessage((LPBYTE)pCopyDataBridgeView, "PreDownloadAlias");
		dispatched |= DispatchCopyDataBridgeMessage((LPBYTE)pCopyDataNamedView, "CopyDataNamed");
		Sleep(dispatched ? 10 : 50);
	}
	LauncherLog("Copydata bridge worker stopped");
	return 0;
}

static BOOL StartCopyDataBridgeWorker()
{
	if (hCopyDataBridgeThread)
	{
		return TRUE;
	}
	InterlockedExchange(&gCopyDataBridgeStop, 0);
	hCopyDataBridgeThread = CreateThread(NULL, 0, CopyDataBridgeThread, NULL, 0, NULL);
	if (!hCopyDataBridgeThread)
	{
		LauncherLog("Copydata bridge worker create failed error=%lu", GetLastError());
		return FALSE;
	}
	return TRUE;
}

static void StopCopyDataBridgeWorker()
{
	if (!hCopyDataBridgeThread)
	{
		return;
	}
	InterlockedExchange(&gCopyDataBridgeStop, 1);
	WaitForSingleObject(hCopyDataBridgeThread, 2000);
	CloseHandle(hCopyDataBridgeThread);
	hCopyDataBridgeThread = NULL;
}

BOOL CreateLoaderAuxiliaryMappings()
{
	if (!CreateLoaderSharedMapping("PreDownload", "QQSpeedLoader2PreDownLoadMap_Shared",
		kPreDownloadSharedSize, &hPreDownloadMap, &pPreDownloadView, TRUE))
	{
		return FALSE;
	}

	// IDA shows the copydata bridge view is formatted with the PreDownload base name and size 0x615.
	if (!CreateLoaderSharedMapping("CopyDataBridgePreDownloadAlias", "QQSpeedLoader2PreDownLoadMap_Shared",
		kCopyDataBridgeSize, &hCopyDataBridgeMap, &pCopyDataBridgeView, FALSE))
	{
		return FALSE;
	}

	if (!CreateLoaderSharedMapping("CopyDataNamed", "QQSpeedCopyData_MSG_Shared",
		kCopyDataBridgeSize, &hCopyDataNamedMap, &pCopyDataNamedView, TRUE))
	{
		return FALSE;
	}

	if (!CreateLoaderSharedMapping("GLauncherPDL", "QQSpeedLoader2GLauncherPDLMap_Shared",
		kGLauncherPDLSharedSize, &hGLauncherPDLMap, &pGLauncherPDLView, TRUE))
	{
		return FALSE;
	}

	return StartCopyDataBridgeWorker();
}

void CloseLoaderAuxiliaryMappings()
{
	StopCopyDataBridgeWorker();
	CloseLoaderSharedMapping("CopyDataNamed", &hCopyDataNamedMap, &pCopyDataNamedView);
	CloseLoaderSharedMapping("CopyDataBridgePreDownloadAlias", &hCopyDataBridgeMap, &pCopyDataBridgeView);
	CloseLoaderSharedMapping("GLauncherPDL", &hGLauncherPDLMap, &pGLauncherPDLView);
	CloseLoaderSharedMapping("PreDownload", &hPreDownloadMap, &pPreDownloadView);
}
