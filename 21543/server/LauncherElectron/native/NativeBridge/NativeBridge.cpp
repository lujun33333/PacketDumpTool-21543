// Win32 compatibility bridge for GameApp shared memory, events, windows and Hook injection.
#define _CRT_SECURE_NO_WARNINGS
#include "NativeBridge.h"
#include "NativeBridgeHookPayload.h"
#include "NativeBridgeIpc.h"
#include "NativeBridgeLog.h"
#include "NativeBridgeProcess.h"
#include "NativeBridgeState.h"
#include <shlobj_core.h>
#include <shlwapi.h>
#pragma comment (lib,"Shlwapi.lib")
#include <string>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <exception>
#pragma comment (lib,"Shell32.lib")

// Global state.
HINSTANCE hInst;									// Current instance.
WCHAR szTitle[MAX_LOADSTRING];						// Title bar text.
WCHAR szWindowClass[MAX_LOADSTRING] = L"QQSpeed";	// Main window class name.
HDC hMemDC;
HANDLE hLoaderBG;
HANDLE hLoaderProgressBack = NULL;
HANDLE hLoaderProgressFore = NULL;
static HANDLE gLoaderCancel = NULL;
static HWND gLoaderWindow = NULL;
static HFONT gLoaderPercentFont = NULL;
static HFONT gLoaderTipFont = NULL;
static WCHAR gLoaderTip[256] = L"按F7可以切换全屏或者窗口显示";
static COLORREF gLoaderTextColor = RGB(253, 253, 253);
static bool gClientLoadingUIReady = false;
HANDLE hRunSpeedGameEvent = NULL;
HANDLE hFileMap = NULL;
SharedData* pMapData = NULL;
HANDLE hGameProcess = NULL;
TCHAR GamePath[MAX_PATH] = {};
TCHAR GameWorkDir[MAX_PATH] = {};
TCHAR GameAppFile[MAX_PATH] = {};

static volatile LONG gLoaderProgress = 0;
static volatile LONG gLoaderProgressStages = 0;
static UINT gLoaderDpi = USER_DEFAULT_SCREEN_DPI;
static const int kLoaderLogicalWidth = 300;
static const int kLoaderLogicalHeight = 200;
static const int kLoaderProgressStageCount = 10;

static std::wstring gSelectedGameRoot;
static HMODULE gLauncherModuleHandle = NULL;
static volatile LONG gQQSpeedNativeBridgeBusy = 0;
static volatile LONG gQQSpeedNativeBridgeGameReady = 0;
static volatile LONG gQQSpeedNativeBridgeGameProcessId = 0;
static volatile LONG gQQSpeedNativeBridgeFailure = 0;
static volatile LONG gQQSpeedNativeBridgeState = 0;
static SRWLOCK gQQSpeedNativeBridgeErrorLock = SRWLOCK_INIT;
static SRWLOCK gQQSpeedNativeBridgeAuthLock = SRWLOCK_INIT;
static WCHAR gQQSpeedNativeBridgeErrorBuffers[2][1024] = {};
static volatile LONG gQQSpeedNativeBridgeErrorIndex = 0;
static HANDLE gQQSpeedNativeBridgeAuthEvent = NULL;
static HANDLE gQQSpeedNativeBridgeStartEvent = NULL;
static HANDLE gQQSpeedNativeBridgeCancelEvent = NULL;
static volatile LONG gQQSpeedNativeBridgeAuthSubmitted = 0;
static const DWORD kGameReadyTimeoutMs = 120000;

struct NativeAuthMaterial
{
	uint32_t Uin;
	BYTE GameKey[16];
	char SignKey[128];
	uint32_t SignKeyLength;
};

static NativeAuthMaterial gQQSpeedNativeBridgeAuth = {};

enum QQSpeedNativeBridgeSessionState
{
	eSessionIdle = 0,
	eSessionPreparing = 1,
	eSessionPreloaded = 2,
	eSessionApplyingAuth = 3,
	eSessionAuthenticated = 4,
	eSessionStarting = 5,
	eSessionReady = 6,
	eSessionFailed = 7,
	eSessionCancelled = 8,
};

static void ClearSessionAuthMaterial()
{
	AcquireSRWLockExclusive(&gQQSpeedNativeBridgeAuthLock);
	SecureZeroMemory(&gQQSpeedNativeBridgeAuth, sizeof(gQQSpeedNativeBridgeAuth));
	InterlockedExchange(&gQQSpeedNativeBridgeAuthSubmitted, 0);
	ReleaseSRWLockExclusive(&gQQSpeedNativeBridgeAuthLock);
}

bool IsQQSpeedNativeBridgeCancelRequested()
{
	return gQQSpeedNativeBridgeCancelEvent &&
		WaitForSingleObject(gQQSpeedNativeBridgeCancelEvent, 0) == WAIT_OBJECT_0;
}

static void SetQQSpeedNativeBridgeState(QQSpeedNativeBridgeSessionState state)
{
	InterlockedExchange(&gQQSpeedNativeBridgeState, (LONG)state);
	LauncherLog("Native bridge session state=%ld", (LONG)state);
}

static void PumpNativeBridgeMessages()
{
	MSG msg = {};
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

// 0=command, 1=cancel, 2=game exited, 3=wait failure.
static int WaitForSessionGate(HANDLE gate, const char* gateName)
{
	LauncherLog("Waiting for native session gate=%s", gateName ? gateName : "unknown");
	for (;;)
	{
		HANDLE handles[3] = {};
		DWORD handleCount = 0;
		DWORD cancelIndex = MAXDWORD;
		DWORD processIndex = MAXDWORD;
		DWORD gateIndex = MAXDWORD;

		if (gQQSpeedNativeBridgeCancelEvent)
		{
			cancelIndex = handleCount;
			handles[handleCount++] = gQQSpeedNativeBridgeCancelEvent;
		}
		if (hGameProcess)
		{
			processIndex = handleCount;
			handles[handleCount++] = hGameProcess;
		}
		if (gate)
		{
			gateIndex = handleCount;
			handles[handleCount++] = gate;
		}

		DWORD waitResult = MsgWaitForMultipleObjects(handleCount, handles, FALSE, 250, QS_ALLINPUT);
		if (waitResult >= WAIT_OBJECT_0 && waitResult < WAIT_OBJECT_0 + handleCount)
		{
			DWORD index = waitResult - WAIT_OBJECT_0;
			if (index == cancelIndex)
			{
				LauncherLog("Native session gate cancelled gate=%s", gateName ? gateName : "unknown");
				return 1;
			}
			if (index == processIndex)
			{
				DWORD exitCode = 0;
				GetExitCodeProcess(hGameProcess, &exitCode);
				LauncherLog("GameApp exited while waiting gate=%s exitCode=%lu", gateName ? gateName : "unknown", exitCode);
				SetQQSpeedNativeBridgeLastError(L"GameApp exited while waiting for the launcher action.");
				return 2;
			}
			if (index == gateIndex)
			{
				LauncherLog("Native session gate received gate=%s", gateName ? gateName : "unknown");
				return 0;
			}
		}
		else if (waitResult == WAIT_OBJECT_0 + handleCount)
		{
			PumpNativeBridgeMessages();
		}
		else if (waitResult == WAIT_FAILED)
		{
			LauncherLog("Native session gate wait failed gate=%s error=%lu", gateName ? gateName : "unknown", GetLastError());
			SetQQSpeedNativeBridgeLastError(L"Failed while waiting for a launcher action.");
			return 3;
		}
	}
}

void SetQQSpeedNativeBridgeLastError(const wchar_t* message)
{
	AcquireSRWLockExclusive(&gQQSpeedNativeBridgeErrorLock);
	LONG current = InterlockedCompareExchange(&gQQSpeedNativeBridgeErrorIndex, 0, 0);
	LONG next = current == 0 ? 1 : 0;
	lstrcpynW(gQQSpeedNativeBridgeErrorBuffers[next], message ? message : L"", _countof(gQQSpeedNativeBridgeErrorBuffers[next]));
	MemoryBarrier();
	InterlockedExchange(&gQQSpeedNativeBridgeErrorIndex, next);
	ReleaseSRWLockExclusive(&gQQSpeedNativeBridgeErrorLock);
}

void SetQQSpeedNativeBridgeLastErrorUtf8(const char* message)
{
	WCHAR wideText[1024] = {};
	if (message && message[0])
	{
		if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, message, -1, wideText, _countof(wideText)))
		{
			MultiByteToWideChar(CP_ACP, 0, message, -1, wideText, _countof(wideText));
		}
	}
	SetQQSpeedNativeBridgeLastError(wideText[0] ? wideText : L"Launcher operation failed.");
}

static const wchar_t* GetQQSpeedNativeBridgeLastErrorPointer()
{
	LONG index = InterlockedCompareExchange(&gQQSpeedNativeBridgeErrorIndex, 0, 0);
	return gQQSpeedNativeBridgeErrorBuffers[index];
}

void SetQQSpeedNativeBridgeGameProcessId(DWORD processId)
{
	InterlockedExchange(&gQQSpeedNativeBridgeGameProcessId, (LONG)processId);
}

void SetQQSpeedNativeBridgeFailure(bool failed)
{
	InterlockedExchange(&gQQSpeedNativeBridgeFailure, failed ? 1 : 0);
}

bool HasQQSpeedNativeBridgeFailure()
{
	return InterlockedCompareExchange(&gQQSpeedNativeBridgeFailure, 0, 0) != 0;
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

static void BuildGamePaths()
{
	LPCTSTR subDir = TEXT("Releasephysx27_64");
	wsprintf(GameWorkDir, TEXT("%s\\%s"), GamePath, subDir);
	wsprintf(GameAppFile, TEXT("%s\\GameApp.exe"), GameWorkDir);
	LauncherLog("BuildGamePaths forced64 workDir=%s game=%s",
		WideToUtf8Log(GameWorkDir).c_str(),
		WideToUtf8Log(GameAppFile).c_str());
}

void SetLoaderProgress(int progress)
{
	if (progress < 0) progress = 0;
	if (progress > 100) progress = 100;
	LONG current = InterlockedCompareExchange(&gLoaderProgress, 0, 0);
	while (progress > current)
	{
		LONG observed = InterlockedCompareExchange(&gLoaderProgress, progress, current);
		if (observed == current)
		{
			break;
		}
		current = observed;
	}
	HWND loaderWindow = gLoaderWindow;
	if (loaderWindow && IsWindow(loaderWindow) && IsWindowVisible(loaderWindow))
	{
		InvalidateRect(loaderWindow, NULL, FALSE);
	}
}

void ResetLoaderProgressStages()
{
	InterlockedExchange(&gLoaderProgressStages, 0);
	InterlockedExchange(&gLoaderProgress, 0);
}

void MarkLoaderProgressStage(LoaderProgressStage stage)
{
	LONG stageMask = InterlockedOr(&gLoaderProgressStages, static_cast<LONG>(stage));
	stageMask |= static_cast<LONG>(stage);
	int completed = 0;
	for (LONG remaining = stageMask; remaining != 0; remaining >>= 1)
	{
		completed += remaining & 1L;
	}
	const int progress = stage == eLoaderStageReady
		? 100
		: completed * 100 / kLoaderProgressStageCount;
	SetLoaderProgress(progress);
	LauncherLog("Loader progress stage=0x%lX completed=%d progress=%d", (LONG)stage, completed, progress);
}

static UINT QueryLoaderDpi()
{
	typedef UINT(WINAPI* GetDpiForSystemFn)();
	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	GetDpiForSystemFn getDpiForSystem = user32
		? reinterpret_cast<GetDpiForSystemFn>(GetProcAddress(user32, "GetDpiForSystem"))
		: nullptr;
	const UINT dpi = getDpiForSystem ? getDpiForSystem() : USER_DEFAULT_SCREEN_DPI;
	return dpi >= USER_DEFAULT_SCREEN_DPI ? dpi : USER_DEFAULT_SCREEN_DPI;
}

static int ScaleLoaderMetric(int logicalValue)
{
	return MulDiv(logicalValue, static_cast<int>(gLoaderDpi), USER_DEFAULT_SCREEN_DPI);
}

static void DrawBitmap(HDC hdc, HANDLE bitmap, int x, int y, int width = 0, int height = 0)
{
	if (!bitmap)
	{
		return;
	}
	BITMAP bm = {};
	if (!GetObject((HBITMAP)bitmap, sizeof(bm), &bm))
	{
		return;
	}
	HDC memDC = CreateCompatibleDC(hdc);
	HGDIOBJ old = SelectObject(memDC, (HBITMAP)bitmap);
	int drawWidth = ScaleLoaderMetric(width > 0 ? width : bm.bmWidth);
	int drawHeight = ScaleLoaderMetric(height > 0 ? height : bm.bmHeight);
	SetStretchBltMode(hdc, HALFTONE);
	StretchBlt(
		hdc,
		ScaleLoaderMetric(x),
		ScaleLoaderMetric(y),
		drawWidth,
		drawHeight,
		memDC,
		0,
		0,
		bm.bmWidth,
		bm.bmHeight,
		SRCCOPY);
	SelectObject(memDC, old);
	DeleteDC(memDC);
}

static void DrawProgressBitmap(HDC hdc, int x, int y, int progress)
{
	BITMAP back = {};
	BITMAP fore = {};
	if (!hLoaderProgressBack || !GetObject((HBITMAP)hLoaderProgressBack, sizeof(back), &back))
	{
		return;
	}
	DrawBitmap(hdc, hLoaderProgressBack, x, y);
	if (!hLoaderProgressFore || !GetObject((HBITMAP)hLoaderProgressFore, sizeof(fore), &fore))
	{
		return;
	}
	int sourceWidth = back.bmWidth * progress / 100;
	if (sourceWidth <= 0)
	{
		return;
	}
	HDC memDC = CreateCompatibleDC(hdc);
	HGDIOBJ old = SelectObject(memDC, (HBITMAP)hLoaderProgressFore);
	SetStretchBltMode(hdc, HALFTONE);
	StretchBlt(
		hdc,
		ScaleLoaderMetric(x),
		ScaleLoaderMetric(y),
		ScaleLoaderMetric(sourceWidth),
		ScaleLoaderMetric(fore.bmHeight),
		memDC,
		0,
		0,
		sourceWidth,
		fore.bmHeight,
		SRCCOPY);
	SelectObject(memDC, old);
	DeleteDC(memDC);
}

static HANDLE LoadClientLoaderBitmap(LPCWSTR fileName)
{
	WCHAR path[MAX_PATH] = {};
	if (!PathCombineW(path, GamePath, L"Releasephysx27_64\\LoadingGameUI") || !PathAppendW(path, fileName))
	{
		return NULL;
	}
	return LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
}

static BOOL InitializeClientLoadingUI()
{
	if (gClientLoadingUIReady)
	{
		return TRUE;
	}

	hLoaderBG = LoadClientLoaderBitmap(L"LoaderBG.bmp");
	hLoaderProgressBack = LoadClientLoaderBitmap(L"Progress_Back.bmp");
	hLoaderProgressFore = LoadClientLoaderBitmap(L"Progress_Fore.bmp");
	gLoaderCancel = LoadClientLoaderBitmap(L"cancel_normal.bmp");
	if (!hLoaderBG || !hLoaderProgressBack || !hLoaderProgressFore || !gLoaderCancel)
	{
		LauncherLog("Official client loading UI bitmap load failed bg=%p back=%p fore=%p cancel=%p",
			hLoaderBG, hLoaderProgressBack, hLoaderProgressFore, gLoaderCancel);
		return FALSE;
	}

	hMemDC = CreateCompatibleDC(NULL);
	if (!hMemDC || !SelectObject(hMemDC, hLoaderBG))
	{
		LauncherLog("Official client loading UI memory DC initialization failed error=%lu", GetLastError());
		return FALSE;
	}

	WCHAR tipsPath[MAX_PATH] = {};
	if (!PathCombineW(tipsPath, GamePath, L"loadertipstext.ini"))
	{
		return FALSE;
	}
	const int tipCount = max(1, GetPrivateProfileIntW(L"Setting", L"Num", 1, tipsPath));
	WCHAR tipSection[32] = {};
	wsprintfW(tipSection, L"Tip%d", (int)(GetTickCount64() % (ULONGLONG)tipCount));
	GetPrivateProfileStringW(
		tipSection,
		L"tips",
		L"按F7可以切换全屏或者窗口显示",
		gLoaderTip,
		_countof(gLoaderTip),
		tipsPath);
	const int red = GetPrivateProfileIntW(L"Loader", L"Red", 253, tipsPath);
	const int green = GetPrivateProfileIntW(L"Loader", L"Green", 253, tipsPath);
	const int blue = GetPrivateProfileIntW(L"Loader", L"Blue", 253, tipsPath);
	gLoaderTextColor = RGB(
		max(0, min(255, red)),
		max(0, min(255, green)),
		max(0, min(255, blue)));

	gLoaderDpi = QueryLoaderDpi();
	gLoaderPercentFont = CreateFontW(
		-ScaleLoaderMetric(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
	gLoaderTipFont = CreateFontW(
		-ScaleLoaderMetric(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
	gClientLoadingUIReady = true;
	LauncherLog("Official client loading UI initialized tip=%s", WideToUtf8Log(gLoaderTip).c_str());
	return TRUE;
}

static void PaintNativeLoadingWindow(HWND hWnd)
{
	PAINTSTRUCT paint = {};
	HDC hdc = BeginPaint(hWnd, &paint);
	if (!hdc)
	{
		return;
	}

	// LoaderBG is already selected into hMemDC during initialization. Selecting the
	// same HBITMAP into DrawBitmap's second memory DC makes SelectObject fail and
	// leaves the popup showing stale pixels from the GameApp window underneath.
	// Paint it exactly like the working legacy launcher instead.
	SetStretchBltMode(hdc, HALFTONE);
	StretchBlt(
		hdc,
		0,
		0,
		ScaleLoaderMetric(kLoaderLogicalWidth),
		ScaleLoaderMetric(kLoaderLogicalHeight),
		hMemDC,
		0,
		0,
		kLoaderLogicalWidth,
		kLoaderLogicalHeight,
		SRCCOPY);
	SetBkMode(hdc, TRANSPARENT);
	SetTextColor(hdc, gLoaderTextColor);

	const int progress = (int)InterlockedCompareExchange(&gLoaderProgress, 0, 0);
	WCHAR percentText[16] = {};
	wsprintfW(percentText, L"%d%%", progress);
	RECT percentRect = {
		0,
		ScaleLoaderMetric(8),
		ScaleLoaderMetric(kLoaderLogicalWidth),
		ScaleLoaderMetric(30)
	};
	HGDIOBJ oldFont = SelectObject(hdc, gLoaderPercentFont ? gLoaderPercentFont : GetStockObject(DEFAULT_GUI_FONT));
	DrawTextW(hdc, percentText, -1, &percentRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

	RECT tipRect = {
		ScaleLoaderMetric(10),
		ScaleLoaderMetric(142),
		ScaleLoaderMetric(290),
		ScaleLoaderMetric(171)
	};
	SelectObject(hdc, gLoaderTipFont ? gLoaderTipFont : GetStockObject(DEFAULT_GUI_FONT));
	DrawTextW(hdc, gLoaderTip, -1, &tipRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
	SelectObject(hdc, oldFont);

	DrawProgressBitmap(hdc, 21, 181, progress);
	DrawBitmap(hdc, gLoaderCancel, 277, 0);
	EndPaint(hWnd, &paint);
}

static BOOL ShowNativeLoadingWindow(HWND hWnd)
{
	if (!gClientLoadingUIReady || !IsWindow(hWnd))
	{
		return FALSE;
	}
	gLoaderWindow = hWnd;
	InvalidateRect(hWnd, NULL, FALSE);
	ShowWindow(hWnd, SW_SHOWNORMAL);
	UpdateWindow(hWnd);
	SetForegroundWindow(hWnd);
	LauncherLog("Official client loading window shown hwnd=%p progress=%ld", hWnd, InterlockedCompareExchange(&gLoaderProgress, 0, 0));
	return TRUE;
}

void OnCopyData(HWND hWnd, COPYDATASTRUCT* pCopyDataStruct)
{
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
		const size_t reportedErrorLength = strnlen_s(pLoginResult->ErrorMessage, sizeof(pLoginResult->ErrorMessage));
		LauncherLog("NotifyLoginResult hwnd=%p entity=%lu result=%d errorLength=%zu",
			pLoginResult->hWnd,
			pLoginResult->Entity,
			pLoginResult->LoginResult,
			reportedErrorLength);

		LPCSTR pstrMsg = NULL;
		char reportedError[sizeof(pLoginResult->ErrorMessage) + 1] = {};
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
		if (reportedErrorLength > 0)
		{
			memcpy(reportedError, pLoginResult->ErrorMessage, reportedErrorLength);
			pstrMsg = reportedError;
		}
		if (pstrMsg)
		{
			LauncherLog("NotifyLoginResult failure entity=%lu result=%d serverMessage=%d",
				pLoginResult->Entity, pLoginResult->LoginResult, reportedErrorLength > 0 ? 1 : 0);
			SetQQSpeedNativeBridgeLastErrorUtf8(pstrMsg);
			SetQQSpeedNativeBridgeFailure(true);
			if (pMapData && pMapData->hWnd)
			{
				DestroyWindow(pMapData->hWnd);
			}
		}
		else
		{
			switch (pLoginResult->Entity)
			{
			case eQQServer:
				MarkLoaderProgressStage(eLoaderStageQQServerAuthenticated);
				break;
			case eDirServer:
				MarkLoaderProgressStage(eLoaderStageDirServerAuthenticated);
				break;
			case eGameServer:
				MarkLoaderProgressStage(eLoaderStageGameServerAuthenticated);
				break;
			default:
				break;
			}
			if (pLoginResult->Entity == eGameServer)
			{
				SetGameSharedReadyValue(1);
				MarkLoaderProgressStage(eLoaderStageReady);
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
		LauncherLog("Status text update len=%zu", len);
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
void Cleanup()
{
	LauncherLog("Cleanup start map=%p fileMap=%p event=%p gameProcess=%p speedHookCfg=%p loaderBG=%p memDC=%p",
		pMapData, hFileMap, hRunSpeedGameEvent, hGameProcess, GetSpeedHookConfigMappingHandle(), hLoaderBG, hMemDC);
	CloseLoaderAuxiliaryMappings();
	if (pMapData)
	{
		SecureZeroMemory(pMapData, kSharedDataMapSize);
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
	ClearHookPayload();
	if (hMemDC)
	{
		DeleteDC(hMemDC);
		hMemDC = NULL;
	}
	if (hLoaderBG)
	{
		DeleteObject(hLoaderBG);
		hLoaderBG = NULL;
	}
	if (hLoaderProgressBack)
	{
		DeleteObject(hLoaderProgressBack);
		hLoaderProgressBack = NULL;
	}
	if (hLoaderProgressFore)
	{
		DeleteObject(hLoaderProgressFore);
		hLoaderProgressFore = NULL;
	}
	if (gLoaderCancel)
	{
		DeleteObject(gLoaderCancel);
		gLoaderCancel = NULL;
	}
	if (gLoaderPercentFont)
	{
		DeleteObject(gLoaderPercentFont);
		gLoaderPercentFont = NULL;
	}
	if (gLoaderTipFont)
	{
		DeleteObject(gLoaderTipFont);
		gLoaderTipFont = NULL;
	}
	gLoaderWindow = NULL;
	gClientLoadingUIReady = false;
	ClearSessionAuthMaterial();
	LauncherLog("Cleanup finish");
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance;
	LauncherLog("InitInstance start hInstance=%p nCmdShow=%d", hInstance, nCmdShow);

	if (gSelectedGameRoot.empty())
	{
		LauncherLog("Native bridge missing selected game root");
		SetQQSpeedNativeBridgeLastError(L"No QQSpeed directory was selected. Please select the main folder first.");
		return FALSE;
	}
	lstrcpynW(GamePath, gSelectedGameRoot.c_str(), MAX_PATH);
	LauncherLog("Native bridge game root=%s", WideToUtf8Log(GamePath).c_str());

	BuildGamePaths();
	if (!HasHookPayload())
	{
		SetQQSpeedNativeBridgeLastError(L"The authenticated in-memory Hook payload has not been supplied.");
		return FALSE;
	}

	if (!PathFileExists(GameAppFile))
	{
		LauncherLog("Selected game root does not contain GameApp path=%s", WideToUtf8Log(GameAppFile).c_str());
		SetQQSpeedNativeBridgeLastError(L"GameApp.exe was not found under the selected QQSpeed directory. Please reselect the main folder that contains Releasephysx27_64\\GameApp.exe.");
		return FALSE;
	}
	LauncherLog("GameApp found path=%s", WideToUtf8Log(GameAppFile).c_str());
	if (!InitializeClientLoadingUI())
	{
		SetQQSpeedNativeBridgeLastError(L"The official client loading UI resources could not be initialized.");
		return FALSE;
	}


	RECT workArea = { 0, 0, 0, 0 };
	SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
	const int protocolWindowWidth = ScaleLoaderMetric(kLoaderLogicalWidth);
	const int protocolWindowHeight = ScaleLoaderMetric(kLoaderLogicalHeight);
	const int protocolWindowX = workArea.left + ((workArea.right - workArea.left) - protocolWindowWidth) / 2;
	const int protocolWindowY = workArea.top + ((workArea.bottom - workArea.top) - protocolWindowHeight) / 2;
	HWND hWnd = CreateWindowExW(
		0,
		szWindowClass,
		szTitle,
		WS_POPUP,
		protocolWindowX, protocolWindowY, protocolWindowWidth, protocolWindowHeight,
		nullptr,
		nullptr,
		hInstance,
		nullptr);
	if (!hWnd)
	{
		LauncherLog("CreateWindowExW hidden protocol window failed error=%lu", GetLastError());
		SetQQSpeedNativeBridgeLastError(L"Failed to create the native bridge protocol window.");
		return FALSE;
	}
	gLoaderWindow = hWnd;
	ShowWindow(hWnd, SW_HIDE);
	UpdateWindow(hWnd);
	LauncherLog("Native bridge hidden protocol window created hwnd=%p", hWnd);


	char eventName[260] = { 0 };
	snprintf(eventName, sizeof(eventName), "RunSpeedGameEvent_%u", GetCurrentProcessId());
	hRunSpeedGameEvent = CreateEventA(NULL, TRUE, FALSE, eventName);
	if (!hRunSpeedGameEvent)
	{
		LauncherLog("CreateEvent failed name=%s error=%lu", eventName, GetLastError());
		SetQQSpeedNativeBridgeLastError(L"Failed to create the launch event.");
		DestroyWindow(hWnd);
		return FALSE;
	}
	LauncherLog("RunSpeedGameEvent created name=%s handle=%p", eventName, hRunSpeedGameEvent);

	LauncherLog("GameApp preload starts before auth material is applied; RunSpeedGameEvent remains unsignaled");
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
		SetQQSpeedNativeBridgeLastError(L"Failed to create shared memory. The client cannot start.");
		DestroyWindow(hWnd);
		return FALSE;
	}
	pMapData = (SharedData*)MapViewOfFile(hFileMap, FILE_MAP_WRITE, 0, 0, kSharedDataMapSize);
	if (!pMapData)
	{
		LauncherLog("MapViewOfFile shared memory failed error=%lu", GetLastError());
		CloseHandle(hFileMap);
		hFileMap = NULL;
		SetQQSpeedNativeBridgeLastError(L"Failed to map shared memory. The client cannot start.");
		DestroyWindow(hWnd);
		return FALSE;
	}
	else
	{
		LauncherLog("Shared memory mapped view=%p", pMapData);
		LauncherLog("Shared memory mapped before GameApp launch");
		if (!CreateLoaderAuxiliaryMappings())
		{
			SetQQSpeedNativeBridgeLastError(L"Failed to create loader auxiliary shared memory. The client cannot start.");
			DestroyWindow(hWnd);
			return FALSE;
		}
		ZeroMemory(pMapData, kSharedDataMapSize);
		SetSharedDataHwnd(pMapData, hWnd);
		SetGameSharedReadyValue(0);
		MarkLoaderProgressStage(eLoaderStageSharedMemoryReady);

		char launcherTokenLabel[64] = { 0 };
		snprintf(launcherTokenLabel, sizeof(launcherTokenLabel), "launcher_%lu", GetCurrentProcessId());
		if (!WriteSpeedHackAuthToken(launcherTokenLabel))
		{
			SetQQSpeedNativeBridgeLastError(L"Failed to create the SpeedHackAuthToken shared memory.");
			DestroyWindow(hWnd);
			return FALSE;
		}

		if (!LaunchGameProcess())
		{
			DestroyWindow(hWnd);
			return FALSE;
		}
		LogGameProcessState("after GameApp preload", hGameProcess);
		SetQQSpeedNativeBridgeState(eSessionPreloaded);

		int authGateResult = WaitForSessionGate(gQQSpeedNativeBridgeAuthEvent, "auth-material");
		if (authGateResult != 0)
		{
			SetQQSpeedNativeBridgeState(authGateResult == 1 ? eSessionCancelled : eSessionFailed);
			TerminateGameProcess();
			DestroyWindow(hWnd);
			return FALSE;
		}

		SetQQSpeedNativeBridgeState(eSessionApplyingAuth);
		NativeAuthMaterial auth = {};
		AcquireSRWLockExclusive(&gQQSpeedNativeBridgeAuthLock);
		memcpy(&auth, &gQQSpeedNativeBridgeAuth, sizeof(auth));
		SecureZeroMemory(&gQQSpeedNativeBridgeAuth, sizeof(gQQSpeedNativeBridgeAuth));
		ReleaseSRWLockExclusive(&gQQSpeedNativeBridgeAuthLock);
		if (auth.Uin == 0 || auth.SignKeyLength == 0 || auth.SignKeyLength > 64)
		{
			SecureZeroMemory(&auth, sizeof(auth));
			SetQQSpeedNativeBridgeLastError(L"The supplied authentication material is invalid.");
			SetQQSpeedNativeBridgeState(eSessionFailed);
			TerminateGameProcess();
			DestroyWindow(hWnd);
			return FALSE;
		}

		char MyDocuments[MAX_PATH] = {};
		ZeroMemory(pMapData, kSharedDataMapSize);
		SetSharedDataHwnd(pMapData, hWnd);
		pMapData->Uin = auth.Uin;
		pMapData->WorldID = 1;
		SetGameSharedReadyValue(0);

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
		memcpy(pMapData->Key, auth.GameKey, sizeof(auth.GameKey));
		memcpy(pMapData->SignKey, auth.SignKey, auth.SignKeyLength);
		pMapData->SignKeyLen = static_cast<int>(auth.SignKeyLength);
		LauncherLog("SharedData filled hwnd=%p uinPresent=%d worldId=%lu userDirConfigured=%d signKeyLen=%d readyFlag=%lu",
			pMapData->hWnd,
			pMapData->Uin != 0 ? 1 : 0,
			pMapData->WorldID,
			pMapData->UserDir[0] != '\0' ? 1 : 0,
			pMapData->SignKeyLen,
			GetGameSharedReadyValue());

		char uinTokenLabel[64] = { 0 };
		snprintf(uinTokenLabel, sizeof(uinTokenLabel), "%u", auth.Uin);
		SecureZeroMemory(&auth, sizeof(auth));
		if (!WriteSpeedHackAuthToken(uinTokenLabel))
		{
			SetQQSpeedNativeBridgeLastError(L"Failed to update the SpeedHackAuthToken shared memory.");
			TerminateGameProcess();
			DestroyWindow(hWnd);
			return FALSE;
		}
		LogGameProcessState("after authenticated SpeedHackAuthToken write", hGameProcess);
		MarkLoaderProgressStage(eLoaderStageAuthApplied);

	}
	LauncherLog("Injected hook owns Network.dll compatibility; legacy NativeBridge patcher is not used");
	SetQQSpeedNativeBridgeState(eSessionAuthenticated);
	LauncherLog("Account authenticated and GameApp prepared; waiting for explicit Start Game click");
	int startGateResult = WaitForSessionGate(gQQSpeedNativeBridgeStartEvent, "start-game");
	if (startGateResult != 0)
	{
		SetQQSpeedNativeBridgeState(startGateResult == 1 ? eSessionCancelled : eSessionFailed);
		TerminateGameProcess();
		DestroyWindow(hWnd);
		return FALSE;
	}
	SetQQSpeedNativeBridgeState(eSessionStarting);
	if (!ShowNativeLoadingWindow(hWnd))
	{
		SetQQSpeedNativeBridgeLastError(L"The official client loading window could not be shown.");
		SetQQSpeedNativeBridgeFailure(true);
		SetQQSpeedNativeBridgeState(eSessionFailed);
		TerminateGameProcess();
		DestroyWindow(hWnd);
		return FALSE;
	}
	LauncherLog("SetEvent RunSpeedGameEvent handle=%p", hRunSpeedGameEvent);
	if (!SetEvent(hRunSpeedGameEvent))
	{
		LauncherLog("SetEvent failed error=%lu", GetLastError());
		SetQQSpeedNativeBridgeLastError(L"Failed to signal the game launch event.");
		SetQQSpeedNativeBridgeFailure(true);
		SetQQSpeedNativeBridgeState(eSessionFailed);
	}
	else
	{
		LauncherLog("SetEvent succeeded");
		MarkLoaderProgressStage(eLoaderStageStartSignaled);
	}
	BOOL gameReady = WaitForGameReady(GetProcessId(hGameProcess), kGameReadyTimeoutMs);
	InterlockedExchange(&gQQSpeedNativeBridgeGameReady, gameReady ? 1 : 0);
	if (!gameReady)
	{
		DWORD exitCode = 0;
		DWORD gamePid = GetProcessId(hGameProcess);
		BOOL gameAlive = hGameProcess && GetExitCodeProcess(hGameProcess, &exitCode) && exitCode == STILL_ACTIVE;
		LauncherLog("WaitForGameReady ended before ready pid=%lu alive=%d exitCode=%lu",
			gamePid, gameAlive ? 1 : 0, exitCode);
	}
	LauncherLog("Post-start wait completed ready=%d", gameReady ? 1 : 0);
	if (IsWindow(hWnd))
	{
		LauncherLog("Destroy loader window after game ready hwnd=%p", hWnd);
		DestroyWindow(hWnd);
	}
	if (!gameReady)
	{
		if (IsQQSpeedNativeBridgeCancelRequested())
		{
			SetQQSpeedNativeBridgeState(eSessionCancelled);
		}
		else
		{
			SetQQSpeedNativeBridgeState(eSessionFailed);
		}
		if (GetQQSpeedNativeBridgeLastErrorPointer()[0] == L'\0')
		{
			SetQQSpeedNativeBridgeLastError(L"The game exited before reaching the ready state.");
		}
		TerminateGameProcess();
		return FALSE;
	}

	SetQQSpeedNativeBridgeState(eSessionReady);
	LauncherLog("InitInstance finish success");
	return TRUE;
}




LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_COPYDATA:
		OnCopyData((HWND)wParam, (COPYDATASTRUCT*)lParam);
		return 0;
	case WM_PAINT:
		PaintNativeLoadingWindow(hWnd);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_LBUTTONDOWN:
	{
		const int x = static_cast<short>(LOWORD(lParam));
		const int y = static_cast<short>(HIWORD(lParam));
		if (x >= ScaleLoaderMetric(277) && x < ScaleLoaderMetric(300) &&
			y >= 0 && y < ScaleLoaderMetric(23))
		{
			LauncherLog("Official client loading window cancel clicked hwnd=%p", hWnd);
			if (gQQSpeedNativeBridgeCancelEvent)
			{
				SetEvent(gQQSpeedNativeBridgeCancelEvent);
			}
			return 0;
		}
		SendMessageW(hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
		return 0;
	}
	case WM_DESTROY:
		LauncherLog("Native bridge hidden protocol window destroyed hwnd=%p", hWnd);
		if (gLoaderWindow == hWnd)
		{
			gLoaderWindow = NULL;
		}
		return 0;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex = {};
	wcex.cbSize = sizeof(WNDCLASSEXW);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursorW(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
	wcex.lpszClassName = szWindowClass;
	return RegisterClassExW(&wcex);
}

#ifdef QQSPEED_NATIVE_BRIDGE_DLL
static void CloseSessionControlEvents()
{
	if (gQQSpeedNativeBridgeAuthEvent)
	{
		CloseHandle(gQQSpeedNativeBridgeAuthEvent);
		gQQSpeedNativeBridgeAuthEvent = NULL;
	}
	if (gQQSpeedNativeBridgeStartEvent)
	{
		CloseHandle(gQQSpeedNativeBridgeStartEvent);
		gQQSpeedNativeBridgeStartEvent = NULL;
	}
	if (gQQSpeedNativeBridgeCancelEvent)
	{
		CloseHandle(gQQSpeedNativeBridgeCancelEvent);
		gQQSpeedNativeBridgeCancelEvent = NULL;
	}
}

static bool CreateSessionControlEvents()
{
	CloseSessionControlEvents();
	gQQSpeedNativeBridgeAuthEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	gQQSpeedNativeBridgeStartEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
	gQQSpeedNativeBridgeCancelEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
	if (!gQQSpeedNativeBridgeAuthEvent || !gQQSpeedNativeBridgeStartEvent || !gQQSpeedNativeBridgeCancelEvent)
	{
		SetQQSpeedNativeBridgeLastError(L"Failed to create native session control events.");
		CloseSessionControlEvents();
		return false;
	}
	return true;
}

static int QQSpeedNativeBridgeBeginSessionImpl(
	const wchar_t* gameRoot,
	int server,
	const char* hookHost,
	uint16_t hookPort) noexcept
{
	if (InterlockedCompareExchange(&gQQSpeedNativeBridgeBusy, 1, 0) != 0)
	{
		return 2;
	}

	int result = 3;
	bool registeredHere = false;
	try
	{
		do
		{
			if (!gameRoot || gameRoot[0] == L'\0' || wcslen(gameRoot) >= MAX_PATH ||
				server != 64 || !ConfigureSpeedHookTarget(hookHost, hookPort))
			{
				SetQQSpeedNativeBridgeLastError(L"Invalid native session parameters.");
				SetQQSpeedNativeBridgeState(eSessionFailed);
				result = 1;
				break;
			}

			SetQQSpeedNativeBridgeLastError(L"");
			SetQQSpeedNativeBridgeGameProcessId(0);
			InterlockedExchange(&gQQSpeedNativeBridgeGameReady, 0);
			SetQQSpeedNativeBridgeFailure(false);
			ResetLoaderProgressStages();
			SetGameSharedReadyValue(0);
			ClearSessionAuthMaterial();

			gSelectedGameRoot.assign(gameRoot);
			SetQQSpeedNativeBridgeState(eSessionPreparing);
			if (!CreateSessionControlEvents())
			{
				SetQQSpeedNativeBridgeState(eSessionFailed);
				break;
			}

			if (!gLauncherModuleHandle)
			{
				HMODULE module = NULL;
				GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&QQSpeedNativeBridgeBeginSessionImpl),
					&module);
				gLauncherModuleHandle = module;
			}
			if (!gLauncherModuleHandle)
			{
				SetQQSpeedNativeBridgeLastError(L"Failed to resolve QQSpeedNativeBridge.dll.");
				SetQQSpeedNativeBridgeState(eSessionFailed);
				break;
			}

			hInst = reinterpret_cast<HINSTANCE>(gLauncherModuleHandle);
			// GameApp's launcher handshake expects the legacy top-level window identity.
			// Keep that identity; the same window later paints the client loading bitmaps.
			lstrcpynW(szTitle, L"Speed", MAX_LOADSTRING);

			SetLastError(ERROR_SUCCESS);
			ATOM classAtom = MyRegisterClass(hInst);
			if (classAtom)
			{
				registeredHere = true;
			}
			else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
			{
				SetQQSpeedNativeBridgeLastError(L"Failed to register the native launcher window class.");
				SetQQSpeedNativeBridgeState(eSessionFailed);
				break;
			}

			BOOL initialized = InitInstance(hInst, SW_HIDE);
			LONG finalState = InterlockedCompareExchange(&gQQSpeedNativeBridgeState, 0, 0);
			if (!initialized || finalState != eSessionReady || InterlockedCompareExchange(&gQQSpeedNativeBridgeGameReady, 0, 0) == 0)
			{
				if (finalState == eSessionCancelled)
				{
					result = 4;
					break;
				}
				if (GetQQSpeedNativeBridgeLastErrorPointer()[0] == L'\0')
				{
					SetQQSpeedNativeBridgeLastError(L"The game did not reach the ready state.");
				}
				SetQQSpeedNativeBridgeState(eSessionFailed);
				break;
			}

			SetQQSpeedNativeBridgeLastError(L"");
			result = 0;
		} while (false);
	}
	catch (const std::exception&)
	{
		SetQQSpeedNativeBridgeLastError(L"Unexpected native launcher exception.");
		SetQQSpeedNativeBridgeState(eSessionFailed);
		result = 3;
	}
	catch (...)
	{
		SetQQSpeedNativeBridgeLastError(L"Unknown native launcher exception.");
		SetQQSpeedNativeBridgeState(eSessionFailed);
		result = 3;
	}

	Cleanup();
	if (registeredHere)
	{
		UnregisterClassW(szWindowClass, hInst);
	}
	ClearSessionAuthMaterial();
	std::wstring().swap(gSelectedGameRoot);
	CloseSessionControlEvents();
	ClearSpeedHookTarget();
	InterlockedExchange(&gQQSpeedNativeBridgeBusy, 0);
	return result;
}

extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_BeginSession(
	const wchar_t* gameRoot,
	int server,
	const char* hookHost,
	uint16_t hookPort)
{
	__try
	{
		return QQSpeedNativeBridgeBeginSessionImpl(gameRoot, server, hookHost, hookPort);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		// A hardware fault bypasses the C++ implementation's normal epilogue.
		// Contain a second cleanup fault as well; the utility process remains the
		// final isolation boundary and will exit after this failure result.
		__try
		{
			TerminateGameProcess();
			Cleanup();
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			// Best effort only after corrupted native state.
		}
		SetQQSpeedNativeBridgeLastError(L"A native launcher fault was contained.");
		SetQQSpeedNativeBridgeState(eSessionFailed);
		InterlockedExchange(&gQQSpeedNativeBridgeBusy, 0);
		return 3;
	}
}

static int DecodeHexNibble(char value)
{
	if (value >= '0' && value <= '9')
	{
		return value - '0';
	}
	if (value >= 'a' && value <= 'f')
	{
		return value - 'a' + 10;
	}
	if (value >= 'A' && value <= 'F')
	{
		return value - 'A' + 10;
	}
	return -1;
}

extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_ApplyAuth(
	uint32_t uin,
	const char* gameKeyHex,
	const char* signKey)
{
	const size_t signKeyLength = signKey ? strnlen_s(signKey, 65) : 0;
	if (uin == 0 || !gameKeyHex || !signKey ||
		strnlen_s(gameKeyHex, 33) != 32 || signKeyLength == 0 || signKeyLength > 64)
	{
		SetQQSpeedNativeBridgeLastError(L"Invalid authentication material.");
		return 1;
	}
	if (InterlockedCompareExchange(&gQQSpeedNativeBridgeState, 0, 0) != eSessionPreloaded || !gQQSpeedNativeBridgeAuthEvent)
	{
		SetQQSpeedNativeBridgeLastError(L"GameApp is not waiting for authentication material.");
		return 2;
	}
	if (InterlockedCompareExchange(&gQQSpeedNativeBridgeAuthSubmitted, 1, 0) != 0)
	{
		SetQQSpeedNativeBridgeLastError(L"Authentication material was already submitted.");
		return 2;
	}

	NativeAuthMaterial auth = {};
	auth.Uin = uin;
	auth.SignKeyLength = static_cast<uint32_t>(signKeyLength);
	for (size_t index = 0; index < 16; ++index)
	{
		const int high = DecodeHexNibble(gameKeyHex[index * 2]);
		const int low = DecodeHexNibble(gameKeyHex[index * 2 + 1]);
		if (high < 0 || low < 0)
		{
			SecureZeroMemory(&auth, sizeof(auth));
			InterlockedExchange(&gQQSpeedNativeBridgeAuthSubmitted, 0);
			SetQQSpeedNativeBridgeLastError(L"The game key must be exactly 16 bytes of hexadecimal data.");
			return 1;
		}
		auth.GameKey[index] = static_cast<BYTE>((high << 4) | low);
	}
	memcpy(auth.SignKey, signKey, auth.SignKeyLength);

	AcquireSRWLockExclusive(&gQQSpeedNativeBridgeAuthLock);
	memcpy(&gQQSpeedNativeBridgeAuth, &auth, sizeof(auth));
	ReleaseSRWLockExclusive(&gQQSpeedNativeBridgeAuthLock);
	SecureZeroMemory(&auth, sizeof(auth));
	if (!SetEvent(gQQSpeedNativeBridgeAuthEvent))
	{
		ClearSessionAuthMaterial();
		SetQQSpeedNativeBridgeLastError(L"Failed to submit authentication material.");
		return 3;
	}
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_StartGame()
{
	if (InterlockedCompareExchange(&gQQSpeedNativeBridgeState, 0, 0) != eSessionAuthenticated || !gQQSpeedNativeBridgeStartEvent)
	{
		SetQQSpeedNativeBridgeLastError(L"The account is not authenticated or GameApp is not ready.");
		return 2;
	}
	if (!SetEvent(gQQSpeedNativeBridgeStartEvent))
	{
		SetQQSpeedNativeBridgeLastError(L"Failed to submit the Start Game command.");
		return 3;
	}
	return 0;
}

extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_Cancel()
{
	if (!gQQSpeedNativeBridgeCancelEvent)
	{
		return InterlockedCompareExchange(&gQQSpeedNativeBridgeBusy, 0, 0) ? 2 : 0;
	}
	return SetEvent(gQQSpeedNativeBridgeCancelEvent) ? 0 : 3;
}

extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_GetState()
{
	return (int)InterlockedCompareExchange(&gQQSpeedNativeBridgeState, 0, 0);
}

extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_GetProgress()
{
	return (int)InterlockedCompareExchange(&gLoaderProgress, 0, 0);
}

extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_IsBusy()
{
	return (int)InterlockedCompareExchange(&gQQSpeedNativeBridgeBusy, 0, 0);
}

extern "C" __declspec(dllexport) uint32_t __stdcall QQSpeedNativeBridge_GetGameProcessId()
{
	return (uint32_t)InterlockedCompareExchange(&gQQSpeedNativeBridgeGameProcessId, 0, 0);
}

extern "C" __declspec(dllexport) const wchar_t* __stdcall QQSpeedNativeBridge_GetLastError()
{
	return GetQQSpeedNativeBridgeLastErrorPointer();
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
	UNREFERENCED_PARAMETER(reserved);
	if (reason == DLL_PROCESS_ATTACH)
	{
		gLauncherModuleHandle = instance;
	}
	return TRUE;
}
#endif
