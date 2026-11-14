#pragma once
#include "framework.h"
#include <cstddef>
#include <cstdint>

#ifndef ZingSpeed
#ifndef _WIN64
#define QQSpeed_x64
#endif
#endif

static const DWORD kKartMapSharedSize = 0xE9C;

#pragma pack(push,1)
struct SharedData
{
	HWND hWnd;
#ifdef QQSpeed_x64
	int hWnd_;
#endif
	DWORD Uin;
	char PwdMD5[16];
	char Unknown1[24];
	DWORD WorldID;
	char UserDir[MAX_PATH];
	DWORD CommodityListCRC;
	char CommodityListURL[128];
	char Key[16];
	int SignKeyLen;
	char SignKey[128];
	int ClientKeyLen;
	char ClientKey[128];
	char QQNickNameLength;
	char QQNickName[37];
#if defined(ZingSpeed) && !defined(_WIN64)
	char Unknown2[2978];
#else
	char Unknown2[2974];
#endif
};

struct NotifyLoginResult
{
	HWND	hWnd;
#ifdef QQSpeed_x64
	int hWnd_;
#endif
	DWORD	Entity;
	INT		LoginResult;
	CHAR	ErrorMessage[100];
};
#pragma pack(pop)

static_assert(sizeof(SharedData) == kKartMapSharedSize, "SharedData layout must match KartMap_Shared");
static_assert(offsetof(SharedData, hWnd) == 0x00, "SharedData.hWnd offset must match KartMap_Shared");
static_assert(offsetof(SharedData, Uin) == 0x08, "SharedData.Uin offset must match KartMap_Shared");
static_assert(offsetof(SharedData, PwdMD5) == 0x0C, "SharedData.PwdMD5 offset must match KartMap_Shared");
static_assert(offsetof(SharedData, WorldID) == 0x34, "SharedData.WorldID offset must match KartMap_Shared");
static_assert(offsetof(SharedData, UserDir) == 0x38, "SharedData.UserDir offset must match KartMap_Shared");
static_assert(offsetof(SharedData, Key) == 0x1C0, "SharedData.Key offset must match KartMap_Shared");
static_assert(offsetof(SharedData, SignKeyLen) == 0x1D0, "SharedData.SignKeyLen offset must match KartMap_Shared");
static_assert(offsetof(SharedData, SignKey) == 0x1D4, "SharedData.SignKey offset must match KartMap_Shared");
static_assert(offsetof(SharedData, ClientKeyLen) == 0x254, "SharedData.ClientKeyLen offset must match KartMap_Shared");
static_assert(offsetof(SharedData, ClientKey) == 0x258, "SharedData.ClientKey offset must match KartMap_Shared");
static_assert(offsetof(SharedData, Unknown2) == 0x2FE, "SharedData.Unknown2 offset must match KartMap_Shared");
static_assert(sizeof(NotifyLoginResult) == 0x74, "NotifyLoginResult layout must match GameApp WM_COPYDATA");
static_assert(offsetof(NotifyLoginResult, Entity) == 0x08, "NotifyLoginResult.Entity offset must match GameApp");
static_assert(offsetof(NotifyLoginResult, LoginResult) == 0x0C, "NotifyLoginResult.LoginResult offset must match GameApp");
static_assert(offsetof(NotifyLoginResult, ErrorMessage) == 0x10, "NotifyLoginResult.ErrorMessage offset must match GameApp");


enum ServerEntity
{
	eQQServer = 0,
	eDirServer = 1,
	eGameServer = 2,
	eShopServer = 3,
};

enum LoginResult
{
	eLoginSuccessed = 0,
	eLoginFailed = 1,
	eLoginTimeout = 2,
	eLoginRequireReg = 3,
	eLoginCanceled = 4,
	eLoginIdentityLimit = 5,
	eLoginVersionLimit = 6,
	eLoginVersionRecommend = 7,
	eLoginFileCracked = 8,
};

// Stable C ABI consumed by the Electron utility process. The bridge accepts
// already-authenticated GameApp session material only. Private-server network
// protocol and credential persistence stay in JS; the legacy SignKey is passed
// through solely because KartMap_Shared requires it.
//
// BeginSession is a blocking session owner and should be invoked with Koffi's
// async form. Return codes: 0=ready, 1=invalid input, 2=busy, 3=native failure,
// 4=cancelled. ApplyAuth/StartGame return 0=accepted, 1=invalid input,
// 2=wrong state, 3=Windows operation failed. GetState returns the stable phase
// numbers 0 idle, 1 preparing, 2 preloaded, 3 applying auth, 4 authenticated,
// 5 starting, 6 ready, 7 failed, 8 cancelled.
#ifdef QQSPEED_NATIVE_BRIDGE_DLL
extern "C"
{
	__declspec(dllexport) int __stdcall QQSpeedNativeBridge_SetHookPayload(
		const void* bytes,
		uint32_t size);
	__declspec(dllexport) int __stdcall QQSpeedNativeBridge_BeginSession(
		const wchar_t* gameRoot,
		int server,
		const char* hookHost,
		uint16_t hookPort);
	__declspec(dllexport) int __stdcall QQSpeedNativeBridge_ApplyAuth(
		uint32_t uin,
		const char* gameKeyHex,
		const char* signKey);
	__declspec(dllexport) int __stdcall QQSpeedNativeBridge_StartGame();
	__declspec(dllexport) int __stdcall QQSpeedNativeBridge_Cancel();
	__declspec(dllexport) int __stdcall QQSpeedNativeBridge_GetState();
	__declspec(dllexport) int __stdcall QQSpeedNativeBridge_GetProgress();
	__declspec(dllexport) int __stdcall QQSpeedNativeBridge_IsBusy();
	__declspec(dllexport) uint32_t __stdcall QQSpeedNativeBridge_GetGameProcessId();
	__declspec(dllexport) const wchar_t* __stdcall QQSpeedNativeBridge_GetLastError();
}
#endif
