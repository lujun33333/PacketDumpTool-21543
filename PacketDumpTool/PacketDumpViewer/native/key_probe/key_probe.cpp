#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <TlHelp32.h>

#include <cstdint>
#include <cstdarg>
#include <cstdlib>
#include <cwchar>
#include <cstdio>
#include <cstring>

#ifndef _countof
#define _countof(array) (sizeof(array) / sizeof((array)[0]))
#endif

namespace
{
constexpr uint32_t SharedMagic = 0x4B505651u; // "QVPK" little-endian marker for PacketDumpViewer KeyProbe.
constexpr uint32_t SharedVersion = 2u;
constexpr wchar_t SharedMappingName[] = L"Local\\PacketDumpViewer_KeyProbe_SessionKey_v2";
constexpr uint32_t KeySourceNone = 0u;
constexpr uint32_t KeySourceManualTest = 1u;
constexpr uint32_t KeySourceLauncherSharedMap = 2u;
constexpr uint32_t KeySourceRuntimeMemoryScan = 3u;
constexpr uint32_t KeySourceQqSpeedGameSocketLayout = 4u;
constexpr uint32_t KeySourceNetworkUnpackHook = 5u;
constexpr uint32_t KeySourceQqSpeedGameSocketInitHook = 6u;
constexpr uint32_t SessionKeyLength = 16u;
constexpr uint32_t SessionKeyPlainObfDistance = 0xC8u;
constexpr uint8_t SessionKeyXorByte = 0x3Au;
constexpr SIZE_T MaxRuntimeScanRegionBytes = 128ull * 1024ull * 1024ull;
constexpr SIZE_T ClientMsgHeadSize = 14u;
constexpr SIZE_T MaxInternalPayloadBytes = 4ull * 1024ull * 1024ull;
constexpr LONG InternalQueueWarnItems = 4096;
constexpr SIZE_T InternalQueueWarnBytes = 256ull * 1024ull * 1024ull;
constexpr uintptr_t NetworkUnpackRva = 0x2F66F0u;         // sub_1802F66F0
constexpr uintptr_t NetworkPackRva = 0x2F5750u;           // CEncryptUtil::Pack
constexpr uintptr_t NetworkSpecialDecodeRva = 0x346520u;  // CNotifyEncryptedMsgPara / 28346 special decode
constexpr uintptr_t NetworkInnerDispatchRva = 0x1F88E0u;  // inner plaintext dispatch after wrapper decode
constexpr uintptr_t NetworkSafePacketBuilderRva = 0x322CF0u; // CEncryptUtil::Pack internal 28346 builder
constexpr uintptr_t NetworkSerializeBodyRva = 0x2EECA0u;  // CEncryptUtil::Pack body serializer
constexpr uintptr_t NetworkGameSocketInitRva = 0x3B2C50u; // sub_1803B2C50
constexpr SIZE_T NetworkUnpackPatchBytes = 13u;           // push rbp/rbx/rsi/rdi/r12/r13/r14/r15
constexpr SIZE_T NetworkGameSocketInitPatchBytes = 18u;   // sub rsp + mov + lea, no RIP-relative bytes
constexpr SIZE_T Safe28346ModeOffset = 0x7DD4u;
constexpr SIZE_T Safe28346PayloadSize = Safe28346ModeOffset + 1u;
constexpr SIZE_T Safe28346InnerBodyOffset = 0x0Cu;
constexpr uintptr_t NetworkPackCallRvas[] = {
    0x1EECD2u,
    0x3B420Eu,
    0x3B4484u,
    0x3B8D69u,
    0x3B8F69u,
    0x3B948Du,
    0x3F379Eu,
    0x3F5A05u,
};
constexpr uintptr_t NetworkSpecialDecodeCallRvas[] = {
    0x2F6DAEu,
};
constexpr uintptr_t NetworkInnerDispatchCallRvas[] = {
    0x2F6D3Au, // direct CEncryptUtil::Unpack after MSGHEAD decode
    0x3466BBu, // mode0 28346 decoded inner dispatch
};
constexpr uintptr_t NetworkSafePacketBuilderCallRvas[] = {
    0x2F58F2u, // CEncryptUtil::Pack internal safe 28346 builder
};
constexpr uintptr_t NetworkSerializeBodyCallRvas[] = {
    0x2F5930u, // CEncryptUtil::Pack internal body serializer
};
constexpr uintptr_t TopKartRequestGameServerRva = 0xBDD400u; // sub_180BDD400 / GlobalInterface::RequestGameServer
constexpr SIZE_T TopKartRequestGameServerPatchBytes = 16u;   // pushes + sub rsp, stops before RIP-relative mov.
constexpr SIZE_T TopKartSourcePreviewBytes = 512u;           // Source object preview, not wire body length.
constexpr uint8_t TopKartRequestGameServerPrologue[TopKartRequestGameServerPatchBytes] = {
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41,
    0x57, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00,
};
// Network.dll 21273 IDA evidence:
// sub_1803B2C50 / CGameSocket::Init copies the plain key at a1+0x7D10C,
// then writes an obfuscated mirror at a1+0x7D1D4 by XORing byte_1805BB09A (0x3A).
// sub_1803B7540 / CGameSocket::OnMyReceive passes both pointers to sub_1802F66F0.
// Top-Kart.dll 21273 IDA evidence:
// sub_1807E19B0 builds the CreateRoom source buffer and calls sub_180BDD400(103, v26, ...).
// sub_180BDD400 validates and forwards msgId/sourceObject to GlobalInterface::RequestGameServer.
constexpr SIZE_T QqSpeedGameSocketPlainKeyOffset = 512268u;      // 0x7D10C
constexpr SIZE_T QqSpeedGameSocketObfuscatedKeyOffset = 512468u; // 0x7D1D4
constexpr SIZE_T QqSpeedGameSocketMinObjectSize = QqSpeedGameSocketObfuscatedKeyOffset + SessionKeyLength;
constexpr SIZE_T QqSpeedGameSocketScanStride = sizeof(void*);
constexpr DWORD WorkerPollMs = 1000;
constexpr DWORD WorkerMaxPolls = 180; // 180s: wait for launcher map / login runtime key to appear.

struct SharedKeyState
{
    uint32_t magic;
    uint32_t version;
    volatile LONG hasSessionKey;
    uint8_t sessionKey[SessionKeyLength];
    volatile LONG keySource;
    volatile LONG workerState;
    volatile LONG scanAttempts;
    volatile LONG remotePid;
    volatile LONG lastError;
};

SRWLOCK g_keyLock = SRWLOCK_INIT;
uint8_t g_sessionKey[16] = {};
bool g_hasSessionKey = false;
HANDLE g_sharedMapping = nullptr;
SharedKeyState* g_sharedState = nullptr;
volatile LONG g_workerState = 0; // 0=not started, 1=running, 2=found, 3=not found, 4=start failed
volatile LONG g_scanAttempts = 0;
volatile LONG g_networkHooksInstalling = 0;
volatile LONG g_networkHooksInstalled = 0;
volatile LONG g_networkHookInstallAttempts = 0;
volatile LONG g_networkModuleSeen = 0;
volatile LONG g_unpackHookCalls = 0;
volatile LONG g_packHookCalls = 0;
volatile LONG g_specialDecodeHookCalls = 0;
volatile LONG g_innerDispatchHookCalls = 0;
volatile LONG g_safePacketBuilderHookCalls = 0;
volatile LONG g_serializeBodyHookCalls = 0;
volatile LONG g_gameSocketInitHookCalls = 0;
volatile LONG g_topKartHooksInstalling = 0;
volatile LONG g_topKartHooksInstalled = 0;
volatile LONG g_topKartHookInstallAttempts = 0;
volatile LONG g_topKartModuleSeen = 0;
volatile LONG g_topKartHookDisabled = 0;
volatile LONG g_topKartRequestCalls = 0;
volatile LONG g_internalPacketSequence = 0;
volatile LONG g_internalPacketDrops = 0;

struct CallSitePatch
{
    void* address = nullptr;
    unsigned char original[5] = {};
    bool installed = false;
};

CallSitePatch g_callPatches[24] = {};
size_t g_callPatchCount = 0;
unsigned char* g_relayPage = nullptr;
size_t g_relayOffset = 0;

#pragma pack(push, 1)
struct LauncherSharedDataLegacy
{
    HWND hWnd;
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
    char Unknown2[2974];
};

struct LauncherSharedDataQqSpeedX64
{
    HWND hWnd;
    DWORD hWndPad;
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
    char Unknown2[1817];
};

struct MsgHeadSnapshot
{
    unsigned short msgId = 0;
    short msgType = 0;
    int msgSeq = 0;
    unsigned char srcFe = 0;
    unsigned char dstFe = 0;
    short srcId = 0;
    short dstId = 0;
    bool valid = false;
};
#pragma pack(pop)

struct InternalPacketEvent
{
    InternalPacketEvent* next = nullptr;
    char direction[128] = {};
    MsgHeadSnapshot head = {};
    int dataLen = 0;
    SIZE_T copyLen = 0;
    uint8_t* bytes = nullptr;
};

SRWLOCK g_internalQueueLock = SRWLOCK_INIT;
CONDITION_VARIABLE g_internalQueueCv = CONDITION_VARIABLE_INIT;
InternalPacketEvent* g_internalQueueHead = nullptr;
InternalPacketEvent* g_internalQueueTail = nullptr;
LONG g_internalQueueLength = 0;
SIZE_T g_internalQueueBytes = 0;
volatile LONG g_internalWriterStarted = 0;
volatile LONG g_internalWriterStop = 0;

static_assert(sizeof(LauncherSharedDataLegacy) == 0xE9C, "legacy launcher shared data layout mismatch");
static_assert(sizeof(LauncherSharedDataQqSpeedX64) == 2587, "QQSpeed x64 launcher shared data layout mismatch");

SharedKeyState* EnsureSharedState()
{
    if (g_sharedState != nullptr)
    {
        return g_sharedState;
    }

    auto mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(SharedKeyState)),
        SharedMappingName);
    if (mapping == nullptr)
    {
        return nullptr;
    }

    auto view = static_cast<SharedKeyState*>(
        MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SharedKeyState)));
    if (view == nullptr)
    {
        CloseHandle(mapping);
        return nullptr;
    }

    if (view->magic != SharedMagic || view->version != SharedVersion)
    {
        ZeroMemory(view, sizeof(SharedKeyState));
        view->magic = SharedMagic;
        view->version = SharedVersion;
        view->keySource = KeySourceNone;
        view->workerState = 0;
        view->scanAttempts = 0;
        view->remotePid = 0;
        view->lastError = 0;
    }

    g_sharedMapping = mapping;
    g_sharedState = view;
    return g_sharedState;
}

bool ReadSharedSessionKey(uint8_t* outKey)
{
    auto state = EnsureSharedState();
    if (state == nullptr || state->hasSessionKey == 0)
    {
        return false;
    }

    MemoryBarrier();
    for (uint32_t i = 0; i < SessionKeyLength; ++i)
    {
        outKey[i] = state->sessionKey[i];
    }

    return true;
}

void WriteSharedSessionKey(const uint8_t* key, uint32_t keySource)
{
    auto state = EnsureSharedState();
    if (state == nullptr)
    {
        return;
    }

    InterlockedExchange(&state->hasSessionKey, 0);
    MemoryBarrier();
    for (uint32_t i = 0; i < SessionKeyLength; ++i)
    {
        state->sessionKey[i] = key[i];
    }

    InterlockedExchange(&state->keySource, static_cast<LONG>(keySource));
    MemoryBarrier();
    InterlockedExchange(&state->hasSessionKey, 1);
}

void WriteSharedWorkerStatus(LONG workerState, LONG remotePid = 0, LONG lastError = 0)
{
    auto state = EnsureSharedState();
    if (state == nullptr)
    {
        return;
    }

    InterlockedExchange(&state->workerState, workerState);
    if (remotePid != 0)
    {
        InterlockedExchange(&state->remotePid, remotePid);
    }

    if (lastError != 0)
    {
        InterlockedExchange(&state->lastError, lastError);
    }
}

void IncrementSharedScanAttempts()
{
    auto state = EnsureSharedState();
    if (state != nullptr)
    {
        InterlockedIncrement(&state->scanAttempts);
    }
}

void ClearSharedSessionKey()
{
    auto state = EnsureSharedState();
    if (state == nullptr)
    {
        return;
    }

    InterlockedExchange(&state->hasSessionKey, 0);
    SecureZeroMemory(state->sessionKey, sizeof(state->sessionKey));
    InterlockedExchange(&state->keySource, KeySourceNone);
}

const wchar_t* WorkerStateText(LONG state)
{
    switch (state)
    {
    case 0:
        return L"not_started";
    case 1:
        return L"running";
    case 2:
        return L"found";
    case 3:
        return L"not_found";
    case 4:
        return L"start_failed";
    default:
        return L"unknown";
    }
}

const wchar_t* KeySourceText(LONG source)
{
    switch (source)
    {
    case KeySourceManualTest:
        return L"manual_test";
    case KeySourceLauncherSharedMap:
        return L"launcher_shared_map";
    case KeySourceRuntimeMemoryScan:
        return L"runtime_memory_scan";
    case KeySourceQqSpeedGameSocketLayout:
        return L"qqspeed_gamesocket_layout";
    case KeySourceNetworkUnpackHook:
        return L"network_unpack_hook";
    case KeySourceQqSpeedGameSocketInitHook:
        return L"qqspeed_gamesocket_init_hook";
    default:
        return L"none";
    }
}

const wchar_t* TopKartStateText()
{
    if (InterlockedCompareExchange(&g_topKartHooksInstalled, 0, 0) != 0)
    {
        return L"hook_installed";
    }

    if (InterlockedCompareExchange(&g_topKartHookDisabled, 0, 0) != 0)
    {
        return L"hook_disabled";
    }

    if (InterlockedCompareExchange(&g_topKartModuleSeen, 0, 0) != 0)
    {
        return L"module_seen";
    }

    return L"module_missing";
}

bool GetDiagFilePathForPid(DWORD pid, wchar_t* outPath, size_t outPathChars)
{
    if (outPath == nullptr || outPathChars == 0)
    {
        return false;
    }

    outPath[0] = L'\0';

    wchar_t localAppData[MAX_PATH] = {};
    const DWORD localAppDataChars = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        localAppData,
        static_cast<DWORD>(_countof(localAppData)));
    if (localAppDataChars == 0 || localAppDataChars >= _countof(localAppData))
    {
        return false;
    }

    wchar_t rootDir[MAX_PATH * 2] = {};
    wchar_t nativeDir[MAX_PATH * 2] = {};
    _snwprintf_s(rootDir, _TRUNCATE, L"%s\\PacketDumpViewer", localAppData);
    CreateDirectoryW(rootDir, nullptr);

    _snwprintf_s(nativeDir, _TRUNCATE, L"%s\\native", rootDir);
    CreateDirectoryW(nativeDir, nullptr);

    _snwprintf_s(
        outPath,
        outPathChars,
        _TRUNCATE,
        L"%s\\key_probe_diag_%lu.txt",
        nativeDir,
        static_cast<unsigned long>(pid));
    return outPath[0] != L'\0';
}

void WriteDiagLine(const wchar_t* format, ...)
{
    wchar_t path[MAX_PATH * 2] = {};
    if (!GetDiagFilePathForPid(GetCurrentProcessId(), path, _countof(path)))
    {
        return;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"a, ccs=UTF-8") != 0 || file == nullptr)
    {
        return;
    }

    SYSTEMTIME time = {};
    GetLocalTime(&time);
    std::fwprintf(
        file,
        L"%04u-%02u-%02u %02u:%02u:%02u.%03u pid=%lu ",
        time.wYear,
        time.wMonth,
        time.wDay,
        time.wHour,
        time.wMinute,
        time.wSecond,
        time.wMilliseconds,
        static_cast<unsigned long>(GetCurrentProcessId()));

    va_list args;
    va_start(args, format);
    std::vfwprintf(file, format, args);
    va_end(args);

    std::fwprintf(file, L"\n");
    std::fclose(file);
}

bool SafeCopyRaw(void* destination, const void* source, SIZE_T size)
{
    if (destination == nullptr || source == nullptr || size == 0)
    {
        return false;
    }

    __try
    {
        CopyMemory(destination, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeReadInt32(const int* source, int* value)
{
    if (source == nullptr || value == nullptr)
    {
        return false;
    }

    __try
    {
        *value = *source;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SafeReadUInt8(const void* source, uint8_t* value)
{
    if (source == nullptr || value == nullptr)
    {
        return false;
    }

    return SafeCopyRaw(value, source, sizeof(*value));
}

bool SafeReadUInt16Native(const void* source, unsigned short* value)
{
    if (source == nullptr || value == nullptr)
    {
        return false;
    }

    return SafeCopyRaw(value, source, sizeof(*value));
}

bool SafeReadMsgHead(const void* source, MsgHeadSnapshot* snapshot)
{
    if (source == nullptr || snapshot == nullptr)
    {
        return false;
    }

    ZeroMemory(snapshot, sizeof(*snapshot));
    if (!SafeCopyRaw(snapshot, source, ClientMsgHeadSize))
    {
        return false;
    }

    snapshot->valid = true;
    return true;
}

unsigned short Swap16(unsigned short value)
{
    return static_cast<unsigned short>(((value & 0x00FFu) << 8) | ((value & 0xFF00u) >> 8));
}

unsigned short ReadU16BeRaw(const uint8_t* data)
{
    return static_cast<unsigned short>((static_cast<unsigned short>(data[0]) << 8) | data[1]);
}

uint32_t ReadU32BeRaw(const uint8_t* data)
{
    return (static_cast<uint32_t>(data[0]) << 24) |
        (static_cast<uint32_t>(data[1]) << 16) |
        (static_cast<uint32_t>(data[2]) << 8) |
        static_cast<uint32_t>(data[3]);
}

bool TryReadPacketMsgHead(const unsigned char* packet, unsigned int packetLen, MsgHeadSnapshot* snapshot)
{
    if (packet == nullptr || snapshot == nullptr || packetLen < 18)
    {
        return false;
    }

    unsigned char optLen = 0;
    if (!SafeCopyRaw(&optLen, packet + 17, sizeof(optLen)))
    {
        return false;
    }

    const SIZE_T msgHeadOffset = 18u + optLen;
    if (packetLen < msgHeadOffset + ClientMsgHeadSize)
    {
        return false;
    }

    uint8_t rawHead[ClientMsgHeadSize] = {};
    if (!SafeCopyRaw(rawHead, packet + msgHeadOffset, sizeof(rawHead)))
    {
        return false;
    }

    ZeroMemory(snapshot, sizeof(*snapshot));
    snapshot->msgId = ReadU16BeRaw(rawHead);
    snapshot->msgType = static_cast<short>(ReadU16BeRaw(rawHead + 2));
    snapshot->msgSeq = static_cast<int>(ReadU32BeRaw(rawHead + 4));
    snapshot->srcFe = rawHead[8];
    snapshot->dstFe = rawHead[9];
    snapshot->srcId = static_cast<short>(ReadU16BeRaw(rawHead + 10));
    snapshot->dstId = static_cast<short>(ReadU16BeRaw(rawHead + 12));
    snapshot->valid = true;
    return true;
}

bool IsKnownLoginChainMsg(unsigned short msgId)
{
    switch (msgId)
    {
    case 1:
    case 90:
    case 100:
    case 223:
    case 28346:
        return true;
    default:
        return false;
    }
}

unsigned short NormalizeMsgIdForEvent(unsigned short rawMsgId)
{
    if (rawMsgId == 0 || IsKnownLoginChainMsg(rawMsgId))
    {
        return rawMsgId;
    }

    const auto swapped = Swap16(rawMsgId);
    if (IsKnownLoginChainMsg(swapped))
    {
        return swapped;
    }

    return rawMsgId;
}

bool GetInternalHookSessionDirectory(wchar_t* outPath, size_t outPathChars)
{
    if (outPath == nullptr || outPathChars == 0)
    {
        return false;
    }

    outPath[0] = L'\0';

    wchar_t localAppData[MAX_PATH] = {};
    const DWORD localAppDataChars = GetEnvironmentVariableW(
        L"LOCALAPPDATA",
        localAppData,
        static_cast<DWORD>(_countof(localAppData)));
    if (localAppDataChars == 0 || localAppDataChars >= _countof(localAppData))
    {
        return false;
    }

    wchar_t rootDir[MAX_PATH * 2] = {};
    wchar_t internalDir[MAX_PATH * 2] = {};
    _snwprintf_s(rootDir, _TRUNCATE, L"%s\\PacketDumpViewer", localAppData);
    CreateDirectoryW(rootDir, nullptr);

    _snwprintf_s(internalDir, _TRUNCATE, L"%s\\internal_hook", rootDir);
    CreateDirectoryW(internalDir, nullptr);

    _snwprintf_s(
        outPath,
        outPathChars,
        _TRUNCATE,
        L"%s\\session_%lu",
        internalDir,
        static_cast<unsigned long>(GetCurrentProcessId()));
    CreateDirectoryW(outPath, nullptr);
    return outPath[0] != L'\0';
}

bool EnsureInternalPayloadDirectory(wchar_t* sessionDir, size_t sessionDirChars, wchar_t* payloadDir, size_t payloadDirChars)
{
    if (!GetInternalHookSessionDirectory(sessionDir, sessionDirChars))
    {
        return false;
    }

    _snwprintf_s(payloadDir, payloadDirChars, _TRUNCATE, L"%s\\payloads", sessionDir);
    CreateDirectoryW(payloadDir, nullptr);
    return payloadDir[0] != L'\0';
}

void FreeInternalPacketEvent(InternalPacketEvent* event)
{
    if (event == nullptr)
    {
        return;
    }

    if (event->bytes != nullptr)
    {
        HeapFree(GetProcessHeap(), 0, event->bytes);
        event->bytes = nullptr;
    }

    HeapFree(GetProcessHeap(), 0, event);
}

void WriteInternalPlainPacketSync(const InternalPacketEvent* event)
{
    if (event == nullptr || event->bytes == nullptr || event->copyLen == 0)
    {
        return;
    }

    wchar_t sessionDir[MAX_PATH * 2] = {};
    wchar_t payloadDir[MAX_PATH * 2] = {};
    if (!EnsureInternalPayloadDirectory(sessionDir, _countof(sessionDir), payloadDir, _countof(payloadDir)))
    {
        return;
    }

    const LONG sequence = InterlockedIncrement(&g_internalPacketSequence);
    const unsigned short msgId = NormalizeMsgIdForEvent(event->head.msgId);
    char fileName[160] = {};
    std::snprintf(
        fileName,
        sizeof(fileName),
        "internal_packet_%ld_%s_msg%u_len%llu.bin",
        sequence,
        event->direction[0] != '\0' ? event->direction : "unknown",
        static_cast<unsigned int>(msgId),
        static_cast<unsigned long long>(event->copyLen));

    wchar_t wideFileName[160] = {};
    MultiByteToWideChar(CP_UTF8, 0, fileName, -1, wideFileName, static_cast<int>(_countof(wideFileName)));

    wchar_t payloadPath[MAX_PATH * 2] = {};
    _snwprintf_s(payloadPath, _countof(payloadPath), _TRUNCATE, L"%s\\%s", payloadDir, wideFileName);

    FILE* payload = nullptr;
    if (_wfopen_s(&payload, payloadPath, L"wb") != 0 || payload == nullptr)
    {
        return;
    }

    bool wrotePayload = false;
    wrotePayload = std::fwrite(event->bytes, 1, event->copyLen, payload) == event->copyLen;

    std::fclose(payload);
    if (!wrotePayload)
    {
        DeleteFileW(payloadPath);
        return;
    }

    wchar_t indexPath[MAX_PATH * 2] = {};
    _snwprintf_s(indexPath, _countof(indexPath), _TRUNCATE, L"%s\\packets.tsv", sessionDir);

    FILE* index = nullptr;
    if (_wfopen_s(&index, indexPath, L"a") != 0 || index == nullptr)
    {
        return;
    }

    std::fprintf(
        index,
        "%ld\t%llu\t%s\t%u\t%d\t%d\t%d\t%llu\t%s\n",
        sequence,
        static_cast<unsigned long long>(GetTickCount64()),
        event->direction[0] != '\0' ? event->direction : "unknown",
        static_cast<unsigned int>(msgId),
        static_cast<int>(event->head.msgType),
        event->head.msgSeq,
        event->dataLen,
        static_cast<unsigned long long>(event->copyLen),
        fileName);
    std::fclose(index);
}

DWORD WINAPI InternalPacketWriterThread(void*)
{
    for (;;)
    {
        InternalPacketEvent* event = nullptr;

        AcquireSRWLockExclusive(&g_internalQueueLock);
        while (g_internalQueueHead == nullptr &&
               InterlockedCompareExchange(&g_internalWriterStop, 0, 0) == 0)
        {
            SleepConditionVariableSRW(&g_internalQueueCv, &g_internalQueueLock, 1000, 0);
        }

        if (g_internalQueueHead == nullptr &&
            InterlockedCompareExchange(&g_internalWriterStop, 0, 0) != 0)
        {
            ReleaseSRWLockExclusive(&g_internalQueueLock);
            break;
        }

        event = g_internalQueueHead;
        if (event != nullptr)
        {
            g_internalQueueHead = event->next;
            if (g_internalQueueHead == nullptr)
            {
                g_internalQueueTail = nullptr;
            }

            event->next = nullptr;
            if (g_internalQueueLength > 0)
            {
                --g_internalQueueLength;
            }

            if (g_internalQueueBytes >= event->copyLen)
            {
                g_internalQueueBytes -= event->copyLen;
            }
            else
            {
                g_internalQueueBytes = 0;
            }
        }

        ReleaseSRWLockExclusive(&g_internalQueueLock);

        if (event != nullptr)
        {
            WriteInternalPlainPacketSync(event);
            FreeInternalPacketEvent(event);
        }
    }

    InterlockedExchange(&g_internalWriterStarted, 0);
    return 0;
}

bool EnsureInternalWriterThread()
{
    if (InterlockedCompareExchange(&g_internalWriterStarted, 1, 0) != 0)
    {
        return true;
    }

    InterlockedExchange(&g_internalWriterStop, 0);
    HANDLE thread = CreateThread(nullptr, 0, InternalPacketWriterThread, nullptr, 0, nullptr);
    if (thread == nullptr)
    {
        InterlockedExchange(&g_internalWriterStarted, 0);
        return false;
    }

    CloseHandle(thread);
    return true;
}

void AppendInternalPlainPacket(
    const char* direction,
    const void* data,
    int dataLen,
    const MsgHeadSnapshot& head)
{
    if (data == nullptr || dataLen <= 0)
    {
        return;
    }

    const SIZE_T copyLen = dataLen > static_cast<int>(MaxInternalPayloadBytes)
        ? MaxInternalPayloadBytes
        : static_cast<SIZE_T>(dataLen);

    InternalPacketEvent* event = static_cast<InternalPacketEvent*>(
        HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(InternalPacketEvent)));
    if (event == nullptr)
    {
        InterlockedIncrement(&g_internalPacketDrops);
        return;
    }

    event->bytes = static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, copyLen));
    if (event->bytes == nullptr)
    {
        InterlockedIncrement(&g_internalPacketDrops);
        FreeInternalPacketEvent(event);
        return;
    }

    __try
    {
        std::memcpy(event->bytes, data, copyLen);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        InterlockedIncrement(&g_internalPacketDrops);
        FreeInternalPacketEvent(event);
        return;
    }

    event->head = head;
    event->dataLen = dataLen;
    event->copyLen = copyLen;
    strncpy_s(
        event->direction,
        _countof(event->direction),
        direction != nullptr ? direction : "unknown",
        _TRUNCATE);

    if (!EnsureInternalWriterThread())
    {
        InterlockedIncrement(&g_internalPacketDrops);
        FreeInternalPacketEvent(event);
        return;
    }

    AcquireSRWLockExclusive(&g_internalQueueLock);
    // Lossless by default: do not drop packets just because the writer is behind.
    // If disk is slow during game start, the queue grows and the writer drains it
    // asynchronously. A drop is counted only when memory/thread startup actually
    // fails, because silently losing protocol evidence is worse than a temporary
    // queue backlog.
    if (g_internalQueueTail != nullptr)
    {
        g_internalQueueTail->next = event;
    }
    else
    {
        g_internalQueueHead = event;
    }

    g_internalQueueTail = event;
    ++g_internalQueueLength;
    g_internalQueueBytes += copyLen;
    ReleaseSRWLockExclusive(&g_internalQueueLock);

    WakeConditionVariable(&g_internalQueueCv);
}

MsgHeadSnapshot BuildInternalMsgHead(unsigned short rawMsgId, short msgType = -1, int msgSeq = -1)
{
    MsgHeadSnapshot head = {};
    head.msgId = NormalizeMsgIdForEvent(rawMsgId);
    head.msgType = msgType;
    head.msgSeq = msgSeq;
    head.valid = true;
    return head;
}

void AppendSafe28346InnerBody(
    const char* direction,
    const uint8_t* safePayload,
    int safePayloadLen,
    unsigned short fallbackMsgId,
    int msgSeq = -1)
{
    if (safePayload == nullptr || safePayloadLen < static_cast<int>(Safe28346InnerBodyOffset))
    {
        return;
    }

    unsigned short innerMsg = 0;
    unsigned short innerLen = 0;
    if (!SafeReadUInt16Native(safePayload + 0x08, &innerMsg) ||
        !SafeReadUInt16Native(safePayload + 0x0A, &innerLen) ||
        innerLen == 0)
    {
        return;
    }

    if (safePayloadLen < static_cast<int>(Safe28346InnerBodyOffset + innerLen))
    {
        return;
    }

    const auto dumpMsgId = innerMsg != 0 ? innerMsg : fallbackMsgId;
    AppendInternalPlainPacket(
        direction,
        safePayload + Safe28346InnerBodyOffset,
        static_cast<int>(innerLen),
        BuildInternalMsgHead(dumpMsgId, -1, msgSeq));
}

void FormatCodeBytes(const uint8_t* address, wchar_t* outText, size_t outTextChars)
{
    if (outText == nullptr || outTextChars == 0)
    {
        return;
    }

    outText[0] = L'\0';
    __try
    {
        size_t used = 0;
        for (uint32_t i = 0; i < 16; ++i)
        {
            if (used + 4 >= outTextChars)
            {
                break;
            }

            const int written = _snwprintf_s(
                outText + used,
                outTextChars - used,
                _TRUNCATE,
                i == 0 ? L"%02X" : L" %02X",
                address[i]);
            if (written <= 0)
            {
                break;
            }

            used += static_cast<size_t>(written);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        wcscpy_s(outText, outTextChars, L"<read-failed>");
    }
}

bool CodePrefixMatches(const void* address, const uint8_t* expected, SIZE_T expectedLen)
{
    if (address == nullptr || expected == nullptr || expectedLen == 0)
    {
        return false;
    }

    uint8_t actual[32] = {};
    if (expectedLen > sizeof(actual) || !SafeCopyRaw(actual, address, expectedLen))
    {
        return false;
    }

    return std::memcmp(actual, expected, expectedLen) == 0;
}

bool ReadDiagTailForPid(DWORD pid, wchar_t* outText, size_t outTextChars)
{
    if (outText == nullptr || outTextChars == 0 || pid == 0)
    {
        return false;
    }

    outText[0] = L'\0';

    wchar_t path[MAX_PATH * 2] = {};
    if (!GetDiagFilePathForPid(pid, path, _countof(path)))
    {
        return false;
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, path, L"r, ccs=UTF-8") != 0 || file == nullptr)
    {
        return false;
    }

    wchar_t line[512] = {};
    while (std::fgetws(line, _countof(line), file) != nullptr)
    {
        const size_t length = std::wcslen(line);
        if (length == 0)
        {
            continue;
        }

        for (size_t i = length; i > 0; --i)
        {
            if (line[i - 1] == L'\r' || line[i - 1] == L'\n')
            {
                line[i - 1] = L'\0';
            }
            else
            {
                break;
            }
        }

        if (line[0] != L'\0')
        {
            wcscpy_s(outText, outTextChars, line);
        }
    }

    std::fclose(file);
    return outText[0] != L'\0';
}

HMODULE FindLoadedModuleByName(const wchar_t* moduleName)
{
    if (moduleName == nullptr || moduleName[0] == L'\0')
    {
        return nullptr;
    }

    HMODULE module = GetModuleHandleW(moduleName);
    if (module != nullptr)
    {
        return module;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return nullptr;
    }

    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szModule, moduleName) == 0)
            {
                module = entry.hModule;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return module;
}

void WriteModuleSummaryForDiagnostics()
{
    HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
        GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        WriteDiagLine(L"module summary failed snapshot gle=%lu", GetLastError());
        return;
    }

    DWORD count = 0;
    MODULEENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry))
    {
        do
        {
            ++count;
            if (count <= 48)
            {
                WriteDiagLine(
                    L"module[%lu] name=%s base=%p size=%lu path=%s",
                    static_cast<unsigned long>(count),
                    entry.szModule,
                    entry.modBaseAddr,
                    entry.modBaseSize,
                    entry.szExePath);
            }
        } while (Module32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    WriteDiagLine(L"module summary count=%lu", static_cast<unsigned long>(count));
}

int32_t CopyStatusText(wchar_t* buffer, uint32_t bufferChars, const wchar_t* text)
{
    if (buffer == nullptr || bufferChars == 0)
    {
        return -1;
    }

    const auto length = static_cast<uint32_t>(std::wcslen(text));
    const auto copyChars = (length < (bufferChars - 1)) ? length : (bufferChars - 1);
    if (copyChars > 0)
    {
        std::wmemcpy(buffer, text, copyChars);
    }

    buffer[copyChars] = L'\0';
    return static_cast<int32_t>(copyChars);
}

template <typename TSharedData>
bool LooksLikeLauncherSharedData(const TSharedData& snapshot)
{
    return snapshot.Uin != 0 &&
        snapshot.WorldID < 1000 &&
        snapshot.SignKeyLen >= 0 &&
        snapshot.SignKeyLen <= static_cast<int>(sizeof(snapshot.SignKey)) &&
        snapshot.ClientKeyLen >= 0 &&
        snapshot.ClientKeyLen <= static_cast<int>(sizeof(snapshot.ClientKey));
}

bool LooksLikeSessionKey(const char* key)
{
    bool seen[256] = {};
    uint32_t nonZero = 0;
    uint32_t unique = 0;

    for (uint32_t i = 0; i < 16; ++i)
    {
        const auto value = static_cast<uint8_t>(key[i]);
        if (value != 0)
        {
            ++nonZero;
        }

        if (!seen[value])
        {
            seen[value] = true;
            ++unique;
        }
    }

    // Session key can contain zero bytes, but a completely zero or near-constant
    // buffer is not a real key and should not overwrite UI state.
    return nonZero > 0 && unique >= 4;
}

bool LooksLikeRuntimeSessionKey(const uint8_t* key)
{
    bool seen[256] = {};
    uint32_t nonZero = 0;
    uint32_t unique = 0;

    for (uint32_t i = 0; i < SessionKeyLength; ++i)
    {
        const auto value = key[i];
        if (value != 0)
        {
            ++nonZero;
        }

        if (!seen[value])
        {
            seen[value] = true;
            ++unique;
        }
    }

    // Stable memdump.py signal:
    // plain key and obfuscated key at +0xC8 match XOR 0x3A.
    // Extra entropy filter avoids all-zero/repeated padding false positives.
    return (nonZero >= 8u) && (unique >= 8u);
}

template <typename TSharedData>
bool TryReadLauncherMapAsLayout(void* view, size_t viewSize, uint8_t* outKey)
{
    if (viewSize < sizeof(TSharedData))
    {
        return false;
    }

    TSharedData snapshot = {};
    CopyMemory(&snapshot, view, sizeof(snapshot));

    if (!LooksLikeLauncherSharedData(snapshot) || !LooksLikeSessionKey(snapshot.Key))
    {
        return false;
    }

    CopyMemory(outKey, snapshot.Key, 16);
    return true;
}

bool TryReadLauncherMap(const char* mapName, uint8_t* outKey)
{
    HANDLE map = OpenFileMappingA(FILE_MAP_READ, FALSE, mapName);
    if (map == nullptr)
    {
        return false;
    }

    void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr)
    {
        CloseHandle(map);
        return false;
    }

    MEMORY_BASIC_INFORMATION mbi = {};
    const size_t viewSize = VirtualQuery(view, &mbi, sizeof(mbi)) != 0
        ? static_cast<size_t>(mbi.RegionSize)
        : sizeof(LauncherSharedDataQqSpeedX64);

    const bool found =
        TryReadLauncherMapAsLayout<LauncherSharedDataQqSpeedX64>(view, viewSize, outKey) ||
        TryReadLauncherMapAsLayout<LauncherSharedDataLegacy>(view, viewSize, outKey);

    UnmapViewOfFile(view);
    CloseHandle(map);
    return found;
}

bool TryReadLauncherMapsForPid(DWORD pid, uint8_t* outKey)
{
    if (pid == 0)
    {
        return false;
    }

    char mapName[96] = {};
    const char* prefixes[] = {
        "KartMap_Shared_%lu",
        "Local\\KartMap_Shared_%lu",
        "Global\\KartMap_Shared_%lu",
        "ZingSpeedMap_Shared_%lu",
        "Local\\ZingSpeedMap_Shared_%lu",
        "Global\\ZingSpeedMap_Shared_%lu",
    };

    for (const char* prefix : prefixes)
    {
        if (std::snprintf(mapName, sizeof(mapName), prefix, static_cast<unsigned long>(pid)) <= 0)
        {
            continue;
        }

        if (TryReadLauncherMap(mapName, outKey))
        {
            return true;
        }
    }

    return false;
}

bool TryReadBaseLauncherMaps(uint8_t* outKey)
{
    const char* mapNames[] = {
        "KartMap_Shared",
        "Local\\KartMap_Shared",
        "Global\\KartMap_Shared",
        "ZingSpeedMap_Shared",
        "Local\\ZingSpeedMap_Shared",
        "Global\\ZingSpeedMap_Shared",
    };

    for (const char* mapName : mapNames)
    {
        if (TryReadLauncherMap(mapName, outKey))
        {
            return true;
        }
    }

    return false;
}

DWORD GetParentProcessId(DWORD pid)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    DWORD parentPid = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (entry.th32ProcessID == pid)
            {
                parentPid = entry.th32ParentProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return parentPid;
}

bool TryReadAnyLauncherSharedKey(uint8_t* outKey)
{
    if (TryReadBaseLauncherMaps(outKey))
    {
        return true;
    }

    const DWORD currentPid = GetCurrentProcessId();
    const DWORD parentPid = GetParentProcessId(currentPid);
    if (TryReadLauncherMapsForPid(parentPid, outKey) ||
        TryReadLauncherMapsForPid(currentPid, outKey))
    {
        return true;
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    bool found = false;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            if (TryReadLauncherMapsForPid(entry.th32ProcessID, outKey))
            {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return found;
}

bool IsReadableMemoryProtect(DWORD protect)
{
    if ((protect & PAGE_GUARD) != 0 || (protect & PAGE_NOACCESS) != 0)
    {
        return false;
    }

    const DWORD baseProtect = protect & 0xFFu;
    return baseProtect == PAGE_READONLY ||
        baseProtect == PAGE_READWRITE ||
        baseProtect == PAGE_WRITECOPY ||
        baseProtect == PAGE_EXECUTE_READ ||
        baseProtect == PAGE_EXECUTE_READWRITE ||
        baseProtect == PAGE_EXECUTE_WRITECOPY;
}

bool IsLikelyHeapMemory(const MEMORY_BASIC_INFORMATION& mbi)
{
    if (mbi.Type != MEM_PRIVATE)
    {
        return false;
    }

    const DWORD baseProtect = mbi.Protect & 0xFFu;
    return baseProtect == PAGE_READWRITE || baseProtect == PAGE_WRITECOPY;
}

bool TryCopyQqSpeedGameSocketKeyPair(const uint8_t* plain, const uint8_t* obfuscated, uint8_t* outKey)
{
    if (plain == nullptr || obfuscated == nullptr || outKey == nullptr)
    {
        return false;
    }

    uint8_t candidate[SessionKeyLength] = {};
    bool seen[256] = {};
    uint32_t nonZero = 0;
    uint32_t unique = 0;

    __try
    {
        for (uint32_t i = 0; i < SessionKeyLength; ++i)
        {
            const uint8_t plainByte = plain[i];
            const uint8_t obfuscatedByte = obfuscated[i];
            if (static_cast<uint8_t>(plainByte ^ SessionKeyXorByte) != obfuscatedByte)
            {
                return false;
            }

            candidate[i] = plainByte;
            if (plainByte != 0)
            {
                ++nonZero;
            }

            if (!seen[plainByte])
            {
                seen[plainByte] = true;
                ++unique;
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    // The layout evidence is strong, but reject obvious empty/template buffers.
    if (nonZero < 4 || unique < 4)
    {
        SecureZeroMemory(candidate, sizeof(candidate));
        return false;
    }

    CopyMemory(outKey, candidate, SessionKeyLength);
    SecureZeroMemory(candidate, sizeof(candidate));
    return true;
}

bool TryScanQqSpeedGameSocketRegion(uint8_t* base, SIZE_T size, uint8_t* outKey)
{
    if (base == nullptr || outKey == nullptr || size < QqSpeedGameSocketMinObjectSize)
    {
        return false;
    }

    const SIZE_T limit = size - QqSpeedGameSocketMinObjectSize;
    for (SIZE_T offset = 0; offset <= limit; offset += QqSpeedGameSocketScanStride)
    {
        const uint8_t* candidateBase = base + offset;
        const uint8_t* plain = candidateBase + QqSpeedGameSocketPlainKeyOffset;
        const uint8_t* obfuscated = candidateBase + QqSpeedGameSocketObfuscatedKeyOffset;
        if (TryCopyQqSpeedGameSocketKeyPair(plain, obfuscated, outKey))
        {
            return true;
        }
    }

    return false;
}

bool TryScanRuntimeRegion(uint8_t* base, SIZE_T size, uint8_t* outKey)
{
    if (base == nullptr || size < SessionKeyPlainObfDistance + SessionKeyLength)
    {
        return false;
    }

    const SIZE_T limit = size - SessionKeyPlainObfDistance - SessionKeyLength;
    __try
    {
        for (SIZE_T offset = 0; offset <= limit; ++offset)
        {
            const uint8_t* plain = base + offset;
            const uint8_t* obfuscated = plain + SessionKeyPlainObfDistance;
            if (!LooksLikeRuntimeSessionKey(plain))
            {
                continue;
            }

            bool matched = true;
            for (uint32_t i = 0; i < SessionKeyLength; ++i)
            {
                if (static_cast<uint8_t>(plain[i] ^ SessionKeyXorByte) != obfuscated[i])
                {
                    matched = false;
                    break;
                }
            }

            if (!matched)
            {
                continue;
            }

            CopyMemory(outKey, plain, SessionKeyLength);
            return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    return false;
}

bool TryReadQqSpeedGameSocketSessionKey(uint8_t* outKey)
{
    uint8_t* address = nullptr;
    MEMORY_BASIC_INFORMATION mbi = {};
    while (VirtualQuery(address, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        auto* base = static_cast<uint8_t*>(mbi.BaseAddress);
        const SIZE_T regionSize = mbi.RegionSize;
        auto* next = base + regionSize;

        if (mbi.State == MEM_COMMIT &&
            IsReadableMemoryProtect(mbi.Protect) &&
            IsLikelyHeapMemory(mbi) &&
            TryScanQqSpeedGameSocketRegion(base, regionSize, outKey))
        {
            return true;
        }

        if (next <= address)
        {
            break;
        }

        address = next;
    }

    return false;
}

bool TryReadRuntimeSessionKey(uint8_t* outKey)
{
    uint8_t* address = nullptr;
    MEMORY_BASIC_INFORMATION mbi = {};
    while (VirtualQuery(address, &mbi, sizeof(mbi)) == sizeof(mbi))
    {
        auto* base = static_cast<uint8_t*>(mbi.BaseAddress);
        const SIZE_T regionSize = mbi.RegionSize;
        auto* next = base + regionSize;

        if (mbi.State == MEM_COMMIT &&
            regionSize <= MaxRuntimeScanRegionBytes &&
            IsReadableMemoryProtect(mbi.Protect))
        {
            if (TryScanRuntimeRegion(base, regionSize, outKey))
            {
                return true;
            }
        }

        if (next <= address)
        {
            break;
        }

        address = next;
    }

    return false;
}

void PublishFoundKey(const uint8_t* key, uint32_t source)
{
    AcquireSRWLockExclusive(&g_keyLock);
    CopyMemory(g_sessionKey, key, sizeof(g_sessionKey));
    g_hasSessionKey = true;
    ReleaseSRWLockExclusive(&g_keyLock);

    WriteSharedSessionKey(key, source);
    InterlockedExchange(&g_workerState, 2);
    WriteSharedWorkerStatus(2, static_cast<LONG>(GetCurrentProcessId()));
    WriteDiagLine(L"session_key published source=%s", KeySourceText(static_cast<LONG>(source)));
}

using NetworkUnpackFn = int(__fastcall *)(
    const uint8_t* packet,
    unsigned int packetLength,
    const void* plainKey,
    const void* obfuscatedKey,
    void* csHead,
    void* msgHead,
    uint8_t* outPlain,
    int* outLength,
    bool allowSpecialDecode);

using NetworkPackFn = int(__fastcall *)(
    uint8_t* outPacket,
    int* outPacketLen,
    unsigned int seq,
    unsigned int cmd,
    uint8_t* srcObject,
    int arg6,
    const void* key,
    void* extraData,
    unsigned char extraLen,
    unsigned short msgId,
    short msgType,
    char flag1,
    char flag2,
    short arg14,
    short arg15,
    const void* plainBody,
    int plainBodyLen,
    int arg18,
    char arg19);

using NetworkSpecialDecodeFn = int(__fastcall *)(
    void* msgHead,
    const void* csHead,
    uint8_t* outData,
    int* outLen,
    void* aux);

using NetworkInnerDispatchFn = int(__fastcall *)(
    short msgId,
    const uint8_t* body,
    int bodyLen,
    void* outData,
    void* outLen,
    void* aux,
    int arg7);

using NetworkSafePacketBuilderFn = int(__fastcall *)(
    short msgId,
    int seq,
    const void* srcObject,
    uint8_t* outSafePayload);

using NetworkSerializeBodyFn = int(__fastcall *)(
    unsigned int msgId,
    const void* srcObject,
    uint8_t* outBody,
    unsigned short* outBodyLen,
    void* aux);

using NetworkGameSocketInitFn = uint8_t(__fastcall *)(uint8_t* gameSocket, void* initStruct, void* logger);
using TopKartRequestGameServerFn = __int64(__fastcall *)(
    unsigned int msgId,
    uint8_t* sourceObject,
    unsigned int arg3,
    char arg4,
    short arg5,
    int arg6);

NetworkUnpackFn g_originalNetworkUnpack = nullptr;
NetworkPackFn g_originalNetworkPack = nullptr;
NetworkSpecialDecodeFn g_originalNetworkSpecialDecode = nullptr;
NetworkInnerDispatchFn g_originalNetworkInnerDispatch = nullptr;
NetworkSafePacketBuilderFn g_originalNetworkSafePacketBuilder = nullptr;
NetworkSerializeBodyFn g_originalNetworkSerializeBody = nullptr;
NetworkGameSocketInitFn g_originalGameSocketInit = nullptr;
TopKartRequestGameServerFn g_originalTopKartRequestGameServer = nullptr;

bool TryCopyPlainSessionKey(const uint8_t* plain, uint8_t* outKey)
{
    if (plain == nullptr || outKey == nullptr)
    {
        return false;
    }

    __try
    {
        if (!LooksLikeRuntimeSessionKey(plain))
        {
            return false;
        }

        CopyMemory(outKey, plain, SessionKeyLength);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void TryPublishPlainSessionKey(const uint8_t* plain, uint32_t source)
{
    uint8_t key[SessionKeyLength] = {};
    if (TryCopyPlainSessionKey(plain, key))
    {
        PublishFoundKey(key, source);
    }

    SecureZeroMemory(key, sizeof(key));
}

void TryPublishSessionKeyPair(const uint8_t* plain, const uint8_t* obfuscated, uint32_t source)
{
    uint8_t key[SessionKeyLength] = {};
    if (TryCopyQqSpeedGameSocketKeyPair(plain, obfuscated, key))
    {
        PublishFoundKey(key, source);
    }

    SecureZeroMemory(key, sizeof(key));
}

int __fastcall HookedNetworkUnpack(
    const uint8_t* packet,
    unsigned int packetLength,
    const void* plainKey,
    const void* obfuscatedKey,
    void* csHead,
    void* msgHead,
    uint8_t* outPlain,
    int* outLength,
    bool allowSpecialDecode)
{
    const LONG calls = InterlockedIncrement(&g_unpackHookCalls);
    if (calls <= 3)
    {
        WriteDiagLine(
            L"network_unpack_hook called call=%ld packet=%p packetLength=%u plainKeyPtr=%p obfuscatedKeyPtr=%p csHead=%p msgHead=%p outPlain=%p outLength=%p allowSpecial=%d",
            calls,
            packet,
            packetLength,
            plainKey,
            obfuscatedKey,
            csHead,
            msgHead,
            outPlain,
            outLength,
            allowSpecialDecode ? 1 : 0);
    }

    if (obfuscatedKey != nullptr)
    {
        TryPublishSessionKeyPair(
            static_cast<const uint8_t*>(plainKey),
            static_cast<const uint8_t*>(obfuscatedKey),
            KeySourceNetworkUnpackHook);
    }
    else
    {
        TryPublishPlainSessionKey(static_cast<const uint8_t*>(plainKey), KeySourceNetworkUnpackHook);
    }

    auto original = g_originalNetworkUnpack;
    if (original == nullptr)
    {
        return 0;
    }

    const int result = original(
        packet,
        packetLength,
        plainKey,
        obfuscatedKey,
        csHead,
        msgHead,
        outPlain,
        outLength,
        allowSpecialDecode);

    int actualOutLen = 0;
    MsgHeadSnapshot head = {};
    MsgHeadSnapshot packetHead = {};
    const bool packetHeadValid = TryReadPacketMsgHead(packet, packetLength, &packetHead);
    if (result != 0 &&
        outPlain != nullptr &&
        SafeReadInt32(outLength, &actualOutLen) &&
        actualOutLen > 0 &&
        SafeReadMsgHead(msgHead, &head))
    {
        if (packetHeadValid && NormalizeMsgIdForEvent(packetHead.msgId) == 28346)
        {
            AppendInternalPlainPacket("recv_raw_28346_packet", packet, static_cast<int>(packetLength), packetHead);
        }

        AppendInternalPlainPacket(
            packetHeadValid && NormalizeMsgIdForEvent(packetHead.msgId) == 28346
                ? "recv_28346_plain_full"
                : "recv_plain_full",
            outPlain,
            actualOutLen,
            head);
        if (calls <= 3)
        {
            WriteDiagLine(
                L"network_unpack_hook captured call=%ld result=%d packetMsg=%u msg=%u type=%d seq=%d outLen=%d",
                calls,
                result,
                static_cast<unsigned int>(packetHeadValid ? NormalizeMsgIdForEvent(packetHead.msgId) : 0),
                static_cast<unsigned int>(NormalizeMsgIdForEvent(head.msgId)),
                static_cast<int>(head.msgType),
                head.msgSeq,
                actualOutLen);
        }
    }

    return result;
}

MsgHeadSnapshot BuildSendMsgHead(unsigned short rawMsgId, short msgType, unsigned int seq)
{
    MsgHeadSnapshot head = {};
    head.msgId = NormalizeMsgIdForEvent(rawMsgId);
    head.msgType = msgType;
    head.msgSeq = static_cast<int>(seq);
    head.valid = true;
    return head;
}

int __fastcall HookedNetworkPack(
    uint8_t* outPacket,
    int* outPacketLen,
    unsigned int seq,
    unsigned int cmd,
    uint8_t* srcObject,
    int arg6,
    const void* key,
    void* extraData,
    unsigned char extraLen,
    unsigned short msgId,
    short msgType,
    char flag1,
    char flag2,
    short arg14,
    short arg15,
    const void* plainBody,
    int plainBodyLen,
    int arg18,
    char arg19)
{
    UNREFERENCED_PARAMETER(cmd);
    UNREFERENCED_PARAMETER(srcObject);
    UNREFERENCED_PARAMETER(arg6);
    UNREFERENCED_PARAMETER(key);
    UNREFERENCED_PARAMETER(extraData);
    UNREFERENCED_PARAMETER(extraLen);
    UNREFERENCED_PARAMETER(flag1);
    UNREFERENCED_PARAMETER(flag2);
    UNREFERENCED_PARAMETER(arg14);
    UNREFERENCED_PARAMETER(arg15);
    UNREFERENCED_PARAMETER(arg18);
    UNREFERENCED_PARAMETER(arg19);

    const LONG calls = InterlockedIncrement(&g_packHookCalls);
    const MsgHeadSnapshot sendHead = BuildSendMsgHead(msgId, msgType, seq);
    if (plainBody != nullptr && plainBodyLen > 0)
    {
        AppendInternalPlainPacket("send_plain_body", plainBody, plainBodyLen, sendHead);
    }

    auto original = g_originalNetworkPack;
    if (original == nullptr)
    {
        return 0;
    }

    const int result = original(
        outPacket,
        outPacketLen,
        seq,
        cmd,
        srcObject,
        arg6,
        key,
        extraData,
        extraLen,
        msgId,
        msgType,
        flag1,
        flag2,
        arg14,
        arg15,
        plainBody,
        plainBodyLen,
        arg18,
        arg19);

    if (calls <= 5)
    {
        int actualOutPacketLen = 0;
        SafeReadInt32(outPacketLen, &actualOutPacketLen);
        WriteDiagLine(
            L"network_pack_hook captured call=%ld result=%d msg=%u type=%d seq=%u bodyLen=%d outLen=%d",
            calls,
            result,
            static_cast<unsigned int>(sendHead.msgId),
            static_cast<int>(sendHead.msgType),
            seq,
            plainBodyLen,
            actualOutPacketLen);
    }

    return result;
}

int __fastcall HookedNetworkSpecialDecode(
    void* msgHead,
    const void* csHead,
    uint8_t* outData,
    int* outLen,
    void* aux)
{
    UNREFERENCED_PARAMETER(csHead);
    UNREFERENCED_PARAMETER(aux);

    const LONG calls = InterlockedIncrement(&g_specialDecodeHookCalls);
    MsgHeadSnapshot beforeHead = {};
    SafeReadMsgHead(msgHead, &beforeHead);

    int beforeOutLen = 0;
    SafeReadInt32(outLen, &beforeOutLen);

    uint8_t mode = 0;
    unsigned short innerMsg = 0;
    unsigned short innerLen = 0;
    const bool modeReadable = outData != nullptr && SafeReadUInt8(outData + Safe28346ModeOffset, &mode);
    const bool innerReadable = outData != nullptr &&
        SafeReadUInt16Native(outData + 0x08, &innerMsg) &&
        SafeReadUInt16Native(outData + 0x0A, &innerLen);

    if (outData != nullptr && beforeOutLen > 0)
    {
        AppendInternalPlainPacket(
            "recv_special28346_before_full",
            outData,
            beforeOutLen,
            BuildInternalMsgHead(28346, beforeHead.msgType, beforeHead.msgSeq));
        AppendSafe28346InnerBody(
            "recv_special28346_before_inner_body_at12",
            outData,
            beforeOutLen,
            28346,
            beforeHead.msgSeq);
    }

    auto original = g_originalNetworkSpecialDecode;
    const int result = original != nullptr ? original(msgHead, csHead, outData, outLen, aux) : 0;

    MsgHeadSnapshot afterHead = {};
    SafeReadMsgHead(msgHead, &afterHead);
    int afterOutLen = 0;
    SafeReadInt32(outLen, &afterOutLen);
    const auto afterMsg = NormalizeMsgIdForEvent(afterHead.msgId);
    if (outData != nullptr && afterOutLen > 0)
    {
        AppendInternalPlainPacket(
            "recv_special28346_after_full",
            outData,
            afterOutLen,
            BuildInternalMsgHead(afterMsg != 0 ? afterMsg : 28346, afterHead.msgType, afterHead.msgSeq));
    }

    if (calls <= 8)
    {
        WriteDiagLine(
            L"special28346_hook call=%ld result=%d beforeMsg=%u beforeLen=%d afterMsg=%u afterLen=%d modeReadable=%d mode=%u innerReadable=%d innerMsg=%u innerLen=%u",
            calls,
            result,
            static_cast<unsigned int>(NormalizeMsgIdForEvent(beforeHead.msgId)),
            beforeOutLen,
            static_cast<unsigned int>(afterMsg),
            afterOutLen,
            modeReadable ? 1 : 0,
            static_cast<unsigned int>(mode),
            innerReadable ? 1 : 0,
            static_cast<unsigned int>(innerMsg),
            static_cast<unsigned int>(innerLen));
    }

    return result;
}

int __fastcall HookedNetworkInnerDispatch(
    short msgId,
    const uint8_t* body,
    int bodyLen,
    void* outData,
    void* outLen,
    void* aux,
    int arg7)
{
    UNREFERENCED_PARAMETER(aux);
    UNREFERENCED_PARAMETER(arg7);

    const LONG calls = InterlockedIncrement(&g_innerDispatchHookCalls);
    const unsigned short normalizedMsgId = NormalizeMsgIdForEvent(static_cast<unsigned short>(msgId));

    auto original = g_originalNetworkInnerDispatch;
    const int result = original != nullptr
        ? original(msgId, body, bodyLen, outData, outLen, aux, arg7)
        : 0;

    int actualOutLen = 0;
    SafeReadInt32(static_cast<const int*>(outLen), &actualOutLen);

    if (body != nullptr && bodyLen > 0)
    {
        AppendInternalPlainPacket(
            "recv_inner_dispatch_body",
            body,
            bodyLen,
            BuildInternalMsgHead(normalizedMsgId));
    }

    if (result != 0 && outData != nullptr && actualOutLen > 0)
    {
        AppendInternalPlainPacket(
            "recv_inner_dispatch_out_full",
            outData,
            actualOutLen,
            BuildInternalMsgHead(normalizedMsgId));
    }

    if (calls <= 12 || normalizedMsgId == 551 || normalizedMsgId == 28346)
    {
        WriteDiagLine(
            L"inner_dispatch_hook call=%ld result=%d msg=%u bodyLen=%d outLen=%d body=%p outData=%p",
            calls,
            result,
            static_cast<unsigned int>(normalizedMsgId),
            bodyLen,
            actualOutLen,
            body,
            outData);
    }

    return result;
}

int __fastcall HookedNetworkSafePacketBuilder(
    short msgId,
    int seq,
    const void* srcObject,
    uint8_t* outSafePayload)
{
    const LONG calls = InterlockedIncrement(&g_safePacketBuilderHookCalls);
    auto original = g_originalNetworkSafePacketBuilder;
    const int result = original != nullptr ? original(msgId, seq, srcObject, outSafePayload) : 0;

    const unsigned short normalizedMsgId = NormalizeMsgIdForEvent(static_cast<unsigned short>(msgId));
    uint8_t mode = 0;
    unsigned short innerMsg = 0;
    unsigned short innerLen = 0;
    if (outSafePayload != nullptr)
    {
        SafeReadUInt8(outSafePayload + Safe28346ModeOffset, &mode);
        SafeReadUInt16Native(outSafePayload + 0x08, &innerMsg);
        SafeReadUInt16Native(outSafePayload + 0x0A, &innerLen);
    }

    if (result != 0 && outSafePayload != nullptr)
    {
        AppendInternalPlainPacket(
            "send_safe28346_builder_payload_full",
            outSafePayload,
            static_cast<int>(Safe28346PayloadSize),
            BuildInternalMsgHead(28346, -1, seq));
        AppendSafe28346InnerBody(
            "send_safe28346_builder_inner_body_at12",
            outSafePayload,
            static_cast<int>(Safe28346PayloadSize),
            normalizedMsgId,
            seq);
    }

    if (calls <= 12 || innerMsg == 551 || normalizedMsgId == 551)
    {
        WriteDiagLine(
            L"safe28346_builder_hook call=%ld result=%d msg=%u seq=%d payload=%p mode=%u innerMsg=%u innerLen=%u",
            calls,
            result,
            static_cast<unsigned int>(normalizedMsgId),
            seq,
            outSafePayload,
            static_cast<unsigned int>(mode),
            static_cast<unsigned int>(innerMsg),
            static_cast<unsigned int>(innerLen));
    }

    return result;
}

int __fastcall HookedNetworkSerializeBody(
    unsigned int msgId,
    const void* srcObject,
    uint8_t* outBody,
    unsigned short* outBodyLen,
    void* aux)
{
    UNREFERENCED_PARAMETER(srcObject);
    UNREFERENCED_PARAMETER(aux);

    const LONG calls = InterlockedIncrement(&g_serializeBodyHookCalls);
    auto original = g_originalNetworkSerializeBody;
    const int result = original != nullptr ? original(msgId, srcObject, outBody, outBodyLen, aux) : 0;

    unsigned short bodyLen = 0;
    SafeReadUInt16Native(outBodyLen, &bodyLen);
    const unsigned short normalizedMsgId = NormalizeMsgIdForEvent(static_cast<unsigned short>(msgId & 0xFFFFu));

    if (result != 0 && outBody != nullptr && bodyLen > 0)
    {
        AppendInternalPlainPacket(
            "send_serialize_body_plain",
            outBody,
            static_cast<int>(bodyLen),
            BuildInternalMsgHead(normalizedMsgId));

        if (normalizedMsgId == 28346)
        {
            AppendSafe28346InnerBody(
                "send_serialize_body_28346_inner_body_at12",
                outBody,
                static_cast<int>(bodyLen),
                28346);
        }
    }

    if (calls <= 12 || normalizedMsgId == 28346 || normalizedMsgId == 551)
    {
        WriteDiagLine(
            L"serialize_body_hook call=%ld result=%d msg=%u bodyLen=%u outBody=%p",
            calls,
            result,
            static_cast<unsigned int>(normalizedMsgId),
            static_cast<unsigned int>(bodyLen),
            outBody);
    }

    return result;
}

uint8_t __fastcall HookedGameSocketInit(uint8_t* gameSocket, void* initStruct, void* logger)
{
    const LONG calls = InterlockedIncrement(&g_gameSocketInitHookCalls);
    if (calls <= 3)
    {
        WriteDiagLine(
            L"gamesocket_init_hook called call=%ld gameSocket=%p initStruct=%p logger=%p",
            calls,
            gameSocket,
            initStruct,
            logger);
    }

    // sub_1803B2C50 copies a2[0..] to gameSocket+0x7D108; the session key starts
    // at gameSocket+0x7D10C, therefore initStruct+4 before the original call.
    if (initStruct != nullptr)
    {
        TryPublishPlainSessionKey(static_cast<const uint8_t*>(initStruct) + 4, KeySourceQqSpeedGameSocketInitHook);
    }

    auto original = g_originalGameSocketInit;
    if (original == nullptr)
    {
        return 0;
    }

    const auto result = original(gameSocket, initStruct, logger);

    if (gameSocket != nullptr)
    {
        TryPublishSessionKeyPair(
            gameSocket + QqSpeedGameSocketPlainKeyOffset,
            gameSocket + QqSpeedGameSocketObfuscatedKeyOffset,
            KeySourceQqSpeedGameSocketInitHook);
    }

    return result;
}

__int64 __fastcall HookedTopKartRequestGameServer(
    unsigned int msgId,
    uint8_t* sourceObject,
    unsigned int arg3,
    char arg4,
    short arg5,
    int arg6)
{
    const LONG calls = InterlockedIncrement(&g_topKartRequestCalls);
    auto original = g_originalTopKartRequestGameServer;
    if (original == nullptr)
    {
        return 1;
    }

    const auto result = original(msgId, sourceObject, arg3, arg4, arg5, arg6);

    if (sourceObject != nullptr && msgId > 0)
    {
        MsgHeadSnapshot head = {};
        head.msgId = NormalizeMsgIdForEvent(static_cast<unsigned short>(msgId & 0xFFFFu));
        head.msgType = -1; // Top-Kart source object is pre-MSGHEAD; real msgtype comes from Network.dll Pack.
        head.msgSeq = -1;
        head.valid = true;
        AppendInternalPlainPacket(
            "send_topkart_source_object_after",
            sourceObject,
            static_cast<int>(TopKartSourcePreviewBytes),
            head);
    }

    if (calls <= 8)
    {
        WriteDiagLine(
            L"topkart_request_hook captured call=%ld msg=%u arg3=%u arg4=%d arg5=%d arg6=%d src=%p result=%lld preview=%llu",
            calls,
            msgId,
            arg3,
            static_cast<int>(arg4),
            static_cast<int>(arg5),
            arg6,
            sourceObject,
            static_cast<long long>(result),
            static_cast<unsigned long long>(TopKartSourcePreviewBytes));
    }

    return result;
}

bool InstallAbsoluteJumpHook(void* target, void* hook, SIZE_T stolenBytes, void** original)
{
    if (target == nullptr || hook == nullptr || original == nullptr || stolenBytes < 12)
    {
        WriteDiagLine(
            L"hook install rejected target=%p hook=%p stolenBytes=%llu",
            target,
            hook,
            static_cast<unsigned long long>(stolenBytes));
        return false;
    }

    wchar_t beforeBytes[128] = {};
    FormatCodeBytes(static_cast<const uint8_t*>(target), beforeBytes, _countof(beforeBytes));
    WriteDiagLine(
        L"hook install attempt target=%p hook=%p stolenBytes=%llu before=%s",
        target,
        hook,
        static_cast<unsigned long long>(stolenBytes),
        beforeBytes);

    auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(
        nullptr,
        stolenBytes + 12,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr)
    {
        WriteDiagLine(L"hook install failed target=%p reason=VirtualAlloc gle=%lu", target, GetLastError());
        return false;
    }

    *original = trampoline;

    auto* targetBytes = static_cast<uint8_t*>(target);
    CopyMemory(trampoline, targetBytes, stolenBytes);
    trampoline[stolenBytes] = 0x48;     // mov rax, imm64
    trampoline[stolenBytes + 1] = 0xB8;
    *reinterpret_cast<uint64_t*>(trampoline + stolenBytes + 2) =
        reinterpret_cast<uint64_t>(targetBytes + stolenBytes);
    trampoline[stolenBytes + 10] = 0xFF; // jmp rax
    trampoline[stolenBytes + 11] = 0xE0;

    DWORD oldProtect = 0;
    if (!VirtualProtect(targetBytes, stolenBytes, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteDiagLine(L"hook install failed target=%p reason=VirtualProtect gle=%lu", target, GetLastError());
        VirtualFree(trampoline, 0, MEM_RELEASE);
        *original = nullptr;
        return false;
    }

    targetBytes[0] = 0x48; // mov rax, imm64
    targetBytes[1] = 0xB8;
    *reinterpret_cast<uint64_t*>(targetBytes + 2) = reinterpret_cast<uint64_t>(hook);
    targetBytes[10] = 0xFF; // jmp rax
    targetBytes[11] = 0xE0;
    for (SIZE_T i = 12; i < stolenBytes; ++i)
    {
        targetBytes[i] = 0x90;
    }

    FlushInstructionCache(GetCurrentProcess(), targetBytes, stolenBytes);
    DWORD ignored = 0;
    VirtualProtect(targetBytes, stolenBytes, oldProtect, &ignored);

    WriteDiagLine(L"hook installed target=%p trampoline=%p stolenBytes=%llu", target, trampoline, static_cast<unsigned long long>(stolenBytes));
    return true;
}

bool IsRel32Reachable(uintptr_t source, uintptr_t destination)
{
    const int64_t delta = static_cast<int64_t>(destination) - static_cast<int64_t>(source + 5);
    return delta >= (-2147483647LL - 1LL) && delta <= 2147483647LL;
}

unsigned char* AllocateRelayPageNear(uintptr_t base)
{
    SYSTEM_INFO sysInfo = {};
    GetSystemInfo(&sysInfo);
    const uintptr_t granularity = sysInfo.dwAllocationGranularity ? sysInfo.dwAllocationGranularity : 0x10000;
    const uintptr_t minAddress = reinterpret_cast<uintptr_t>(sysInfo.lpMinimumApplicationAddress);
    const uintptr_t maxAddress = reinterpret_cast<uintptr_t>(sysInfo.lpMaximumApplicationAddress);

    for (uintptr_t distance = 0; distance < 0x70000000ULL; distance += granularity)
    {
        const uintptr_t down = base > distance ? (base - distance) & ~(granularity - 1) : 0;
        if (down >= minAddress && IsRel32Reachable(base, down))
        {
            void* mem = VirtualAlloc(reinterpret_cast<void*>(down), granularity, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (mem != nullptr)
            {
                return static_cast<unsigned char*>(mem);
            }
        }

        const uintptr_t up = (base + distance + granularity - 1) & ~(granularity - 1);
        if (up >= base && up <= maxAddress && IsRel32Reachable(base, up))
        {
            void* mem = VirtualAlloc(reinterpret_cast<void*>(up), granularity, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
            if (mem != nullptr)
            {
                return static_cast<unsigned char*>(mem);
            }
        }
    }

    return nullptr;
}

void* CreateRelayStub(uintptr_t networkBase, void* detour, const wchar_t* name)
{
    constexpr size_t RelayPageSize = 0x10000;
    constexpr size_t RelayStubSize = 14;
    if (g_relayPage == nullptr)
    {
        g_relayPage = AllocateRelayPageNear(networkBase);
        if (g_relayPage == nullptr)
        {
            WriteDiagLine(L"Network call-site relay allocation failed base=%p", reinterpret_cast<void*>(networkBase));
            return nullptr;
        }

        WriteDiagLine(L"Network call-site relay page=%p base=%p", g_relayPage, reinterpret_cast<void*>(networkBase));
    }

    if (g_relayOffset + RelayStubSize > RelayPageSize)
    {
        WriteDiagLine(L"Network %s relay page exhausted", name);
        return nullptr;
    }

    unsigned char* stub = g_relayPage + g_relayOffset;
    g_relayOffset += RelayStubSize;

    // jmp qword ptr [rip+0]; dq detour
    stub[0] = 0xFF;
    stub[1] = 0x25;
    stub[2] = 0x00;
    stub[3] = 0x00;
    stub[4] = 0x00;
    stub[5] = 0x00;
    *reinterpret_cast<uintptr_t*>(stub + 6) = reinterpret_cast<uintptr_t>(detour);
    FlushInstructionCache(GetCurrentProcess(), stub, RelayStubSize);

    WriteDiagLine(L"Network %s relay stub=%p detour=%p", name, stub, detour);
    return stub;
}

bool PatchDirectCall(uintptr_t networkBase, uintptr_t callRva, uintptr_t expectedTargetRva, void* relay, const wchar_t* name)
{
    if (g_callPatchCount >= _countof(g_callPatches))
    {
        WriteDiagLine(L"Network %s call-site patch table full", name);
        return false;
    }

    auto* call = reinterpret_cast<unsigned char*>(networkBase + callRva);
    unsigned char original[5] = {};
    if (!SafeCopyRaw(original, call, sizeof(original)) || original[0] != 0xE8)
    {
        WriteDiagLine(L"Network %s call-site invalid rva=0x%llX address=%p first=%02X",
            name,
            static_cast<unsigned long long>(callRva),
            call,
            static_cast<unsigned int>(original[0]));
        return false;
    }

    int32_t oldRel = 0;
    CopyMemory(&oldRel, original + 1, sizeof(oldRel));
    const uintptr_t oldTarget = reinterpret_cast<uintptr_t>(call + 5 + oldRel);
    const uintptr_t expectedTarget = networkBase + expectedTargetRva;
    if (oldTarget != expectedTarget)
    {
        WriteDiagLine(L"Network %s call-site target mismatch rva=0x%llX got=%p expected=%p",
            name,
            static_cast<unsigned long long>(callRva),
            reinterpret_cast<void*>(oldTarget),
            reinterpret_cast<void*>(expectedTarget));
        return false;
    }

    const uintptr_t relayAddress = reinterpret_cast<uintptr_t>(relay);
    if (!IsRel32Reachable(reinterpret_cast<uintptr_t>(call), relayAddress))
    {
        WriteDiagLine(L"Network %s call-site relay out of rel32 range call=%p relay=%p", name, call, relay);
        return false;
    }

    const int64_t delta = static_cast<int64_t>(relayAddress) - static_cast<int64_t>(reinterpret_cast<uintptr_t>(call) + 5);
    const int32_t newRel = static_cast<int32_t>(delta);
    unsigned char patch[5] = { 0xE8, 0, 0, 0, 0 };
    CopyMemory(patch + 1, &newRel, sizeof(newRel));

    DWORD oldProtect = 0;
    if (!VirtualProtect(call, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteDiagLine(L"Network %s call-site protect failed rva=0x%llX gle=%lu",
            name,
            static_cast<unsigned long long>(callRva),
            GetLastError());
        return false;
    }

    CopyMemory(call, patch, sizeof(patch));
    DWORD ignoredProtect = 0;
    VirtualProtect(call, sizeof(patch), oldProtect, &ignoredProtect);
    FlushInstructionCache(GetCurrentProcess(), call, sizeof(patch));

    CallSitePatch& saved = g_callPatches[g_callPatchCount++];
    saved.address = call;
    CopyMemory(saved.original, original, sizeof(saved.original));
    saved.installed = true;

    WriteDiagLine(L"Network %s call-site patched rva=0x%llX address=%p relay=%p",
        name,
        static_cast<unsigned long long>(callRva),
        call,
        relay);
    return true;
}

template <size_t Count>
size_t PatchDirectCalls(uintptr_t networkBase, const uintptr_t(&callRvas)[Count], uintptr_t targetRva, void* detour, const wchar_t* name)
{
    void* relay = CreateRelayStub(networkBase, detour, name);
    if (relay == nullptr)
    {
        return 0;
    }

    size_t patched = 0;
    for (uintptr_t callRva : callRvas)
    {
        if (PatchDirectCall(networkBase, callRva, targetRva, relay, name))
        {
            ++patched;
        }
    }

    WriteDiagLine(L"Network %s call-site patch complete patched=%llu total=%llu",
        name,
        static_cast<unsigned long long>(patched),
        static_cast<unsigned long long>(Count));
    return patched;
}

bool TryInstallNetworkHooks()
{
    if (InterlockedCompareExchange(&g_networkHooksInstalled, 0, 0) != 0)
    {
        return true;
    }

    if (InterlockedCompareExchange(&g_networkHooksInstalling, 1, 0) != 0)
    {
        return false;
    }

    bool installed = false;
    const LONG attempt = InterlockedIncrement(&g_networkHookInstallAttempts);
    auto network = FindLoadedModuleByName(L"Network.dll");
    if (network != nullptr)
    {
        InterlockedExchange(&g_networkModuleSeen, 1);
        auto* networkBase = reinterpret_cast<uint8_t*>(network);
        if (attempt == 1 || (attempt % 10) == 0)
        {
            WriteDiagLine(
                L"Network.dll visible base=%p attempt=%ld unpackTarget=%p packTarget=%p gamesocketInitTarget=%p",
                networkBase,
                attempt,
                networkBase + NetworkUnpackRva,
                networkBase + NetworkPackRva,
                networkBase + NetworkGameSocketInitRva);
        }

        if (g_originalNetworkUnpack == nullptr)
        {
            if (InstallAbsoluteJumpHook(
                networkBase + NetworkUnpackRva,
                reinterpret_cast<void*>(&HookedNetworkUnpack),
                NetworkUnpackPatchBytes,
                reinterpret_cast<void**>(&g_originalNetworkUnpack)))
            {
                installed = true;
            }
            else
            {
                WriteDiagLine(L"network_unpack_hook install failed target=%p", networkBase + NetworkUnpackRva);
            }
        }

        if (g_originalNetworkPack == nullptr)
        {
            auto* packTarget = networkBase + NetworkPackRva;
            const size_t patched = PatchDirectCalls(
                reinterpret_cast<uintptr_t>(networkBase),
                NetworkPackCallRvas,
                NetworkPackRva,
                reinterpret_cast<void*>(&HookedNetworkPack),
                L"CEncryptUtil::Pack");
            if (patched > 0)
            {
                g_originalNetworkPack = reinterpret_cast<NetworkPackFn>(packTarget);
                installed = true;
            }
            else
            {
                WriteDiagLine(L"network_pack_hook install failed target=%p patched=0", packTarget);
            }
        }

        if (g_originalNetworkSpecialDecode == nullptr)
        {
            auto* specialDecodeTarget = networkBase + NetworkSpecialDecodeRva;
            const size_t patched = PatchDirectCalls(
                reinterpret_cast<uintptr_t>(networkBase),
                NetworkSpecialDecodeCallRvas,
                NetworkSpecialDecodeRva,
                reinterpret_cast<void*>(&HookedNetworkSpecialDecode),
                L"Network special 28346 decode");
            if (patched > 0)
            {
                g_originalNetworkSpecialDecode = reinterpret_cast<NetworkSpecialDecodeFn>(specialDecodeTarget);
                installed = true;
            }
            else
            {
                WriteDiagLine(L"special28346_hook install skipped target=%p patched=0", specialDecodeTarget);
            }
        }

        if (g_originalNetworkInnerDispatch == nullptr)
        {
            auto* innerDispatchTarget = networkBase + NetworkInnerDispatchRva;
            const size_t patched = PatchDirectCalls(
                reinterpret_cast<uintptr_t>(networkBase),
                NetworkInnerDispatchCallRvas,
                NetworkInnerDispatchRva,
                reinterpret_cast<void*>(&HookedNetworkInnerDispatch),
                L"Network inner dispatch");
            if (patched > 0)
            {
                g_originalNetworkInnerDispatch = reinterpret_cast<NetworkInnerDispatchFn>(innerDispatchTarget);
                installed = true;
            }
            else
            {
                WriteDiagLine(L"inner_dispatch_hook install skipped target=%p patched=0", innerDispatchTarget);
            }
        }

        if (g_originalNetworkSafePacketBuilder == nullptr)
        {
            auto* safeBuilderTarget = networkBase + NetworkSafePacketBuilderRva;
            const size_t patched = PatchDirectCalls(
                reinterpret_cast<uintptr_t>(networkBase),
                NetworkSafePacketBuilderCallRvas,
                NetworkSafePacketBuilderRva,
                reinterpret_cast<void*>(&HookedNetworkSafePacketBuilder),
                L"Network safe 28346 builder");
            if (patched > 0)
            {
                g_originalNetworkSafePacketBuilder = reinterpret_cast<NetworkSafePacketBuilderFn>(safeBuilderTarget);
                installed = true;
            }
            else
            {
                WriteDiagLine(L"safe28346_builder_hook install skipped target=%p patched=0", safeBuilderTarget);
            }
        }

        if (g_originalNetworkSerializeBody == nullptr)
        {
            auto* serializeBodyTarget = networkBase + NetworkSerializeBodyRva;
            const size_t patched = PatchDirectCalls(
                reinterpret_cast<uintptr_t>(networkBase),
                NetworkSerializeBodyCallRvas,
                NetworkSerializeBodyRva,
                reinterpret_cast<void*>(&HookedNetworkSerializeBody),
                L"Network body serializer");
            if (patched > 0)
            {
                g_originalNetworkSerializeBody = reinterpret_cast<NetworkSerializeBodyFn>(serializeBodyTarget);
                installed = true;
            }
            else
            {
                WriteDiagLine(L"serialize_body_hook install skipped target=%p patched=0", serializeBodyTarget);
            }
        }

        if (g_originalGameSocketInit == nullptr)
        {
            if (InstallAbsoluteJumpHook(
                networkBase + NetworkGameSocketInitRva,
                reinterpret_cast<void*>(&HookedGameSocketInit),
                NetworkGameSocketInitPatchBytes,
                reinterpret_cast<void**>(&g_originalGameSocketInit)))
            {
                installed = true;
            }
            else
            {
                WriteDiagLine(L"gamesocket_init_hook install failed target=%p", networkBase + NetworkGameSocketInitRva);
            }
        }
    }
    else if (attempt == 1 || (attempt % 10) == 0)
    {
        WriteDiagLine(L"Network.dll not loaded attempt=%ld", attempt);
        if (attempt == 1)
        {
            WriteModuleSummaryForDiagnostics();
        }
    }

    if (g_originalNetworkUnpack != nullptr ||
        g_originalNetworkPack != nullptr ||
        g_originalNetworkSpecialDecode != nullptr ||
        g_originalNetworkInnerDispatch != nullptr ||
        g_originalNetworkSafePacketBuilder != nullptr ||
        g_originalNetworkSerializeBody != nullptr ||
        g_originalGameSocketInit != nullptr)
    {
        InterlockedExchange(&g_networkHooksInstalled, 1);
        installed = true;
        WriteDiagLine(
            L"network hooks active unpack=%s pack=%s special28346=%s inner_dispatch=%s safe_builder=%s serialize_body=%s gamesocket_init=%s",
            g_originalNetworkUnpack != nullptr ? L"yes" : L"no",
            g_originalNetworkPack != nullptr ? L"yes" : L"no",
            g_originalNetworkSpecialDecode != nullptr ? L"yes" : L"no",
            g_originalNetworkInnerDispatch != nullptr ? L"yes" : L"no",
            g_originalNetworkSafePacketBuilder != nullptr ? L"yes" : L"no",
            g_originalNetworkSerializeBody != nullptr ? L"yes" : L"no",
            g_originalGameSocketInit != nullptr ? L"yes" : L"no");
    }

    InterlockedExchange(&g_networkHooksInstalling, 0);
    return installed;
}

bool TryInstallTopKartHooks()
{
    if (InterlockedCompareExchange(&g_topKartHooksInstalled, 0, 0) != 0)
    {
        return true;
    }

    if (InterlockedCompareExchange(&g_topKartHookDisabled, 0, 0) != 0)
    {
        return false;
    }

    if (InterlockedCompareExchange(&g_topKartHooksInstalling, 1, 0) != 0)
    {
        return false;
    }

    bool installed = false;
    const LONG attempt = InterlockedIncrement(&g_topKartHookInstallAttempts);
    auto topKart = FindLoadedModuleByName(L"Top-Kart.dll");
    if (topKart != nullptr)
    {
        InterlockedExchange(&g_topKartModuleSeen, 1);
        auto* topKartBase = reinterpret_cast<uint8_t*>(topKart);
        auto* target = topKartBase + TopKartRequestGameServerRva;
        if (attempt == 1 || (attempt % 10) == 0)
        {
            WriteDiagLine(
                L"Top-Kart.dll visible base=%p attempt=%ld requestGameServerTarget=%p",
                topKartBase,
                attempt,
                target);
        }

        if (!CodePrefixMatches(target, TopKartRequestGameServerPrologue, sizeof(TopKartRequestGameServerPrologue)))
        {
            wchar_t bytes[128] = {};
            FormatCodeBytes(target, bytes, _countof(bytes));
            WriteDiagLine(
                L"TopKart RequestGameServer hook disabled: prologue mismatch target=%p got=%s",
                target,
                bytes);
            InterlockedExchange(&g_topKartHookDisabled, 1);
        }
        else if (g_originalTopKartRequestGameServer == nullptr)
        {
            if (InstallAbsoluteJumpHook(
                target,
                reinterpret_cast<void*>(&HookedTopKartRequestGameServer),
                TopKartRequestGameServerPatchBytes,
                reinterpret_cast<void**>(&g_originalTopKartRequestGameServer)))
            {
                InterlockedExchange(&g_topKartHooksInstalled, 1);
                installed = true;
                WriteDiagLine(L"topkart request hook active target=%p", target);
            }
            else
            {
                WriteDiagLine(L"topkart request hook install failed target=%p", target);
            }
        }
    }
    else if (attempt == 1 || (attempt % 10) == 0)
    {
        WriteDiagLine(L"Top-Kart.dll not loaded attempt=%ld", attempt);
    }

    InterlockedExchange(&g_topKartHooksInstalling, 0);
    return installed;
}

DWORD WINAPI KeyProbeWorker(LPVOID)
{
    if (InterlockedCompareExchange(&g_workerState, 1, 0) != 0)
    {
        return 0;
    }

    EnsureSharedState();
    ClearSharedSessionKey();
    WriteSharedWorkerStatus(1, static_cast<LONG>(GetCurrentProcessId()));
    WriteDiagLine(L"worker started build=2026-06-18-network-topkart-hook");

    bool publishedSessionKey = false;
    for (DWORD i = 0; i < WorkerMaxPolls; ++i)
    {
        InterlockedIncrement(&g_scanAttempts);
        IncrementSharedScanAttempts();
        TryInstallNetworkHooks();
        TryInstallTopKartHooks();

        uint8_t key[16] = {};
        if (!publishedSessionKey && TryReadAnyLauncherSharedKey(key))
        {
            PublishFoundKey(key, KeySourceLauncherSharedMap);
            publishedSessionKey = true;
            SecureZeroMemory(key, sizeof(key));
        }

        // If key_probe is injected immediately when GameApp.exe appears, Network.dll
        // may not be mapped yet. Do not run full-process memory scans in that state:
        // they are expensive and can delay the next module poll long enough to miss
        // the real Network load/install window. Keep polling cheaply until the module
        // is actually visible in the target process.
        if (InterlockedCompareExchange(&g_networkModuleSeen, 0, 0) == 0)
        {
            Sleep(WorkerPollMs);
            continue;
        }

        if (!publishedSessionKey && TryReadQqSpeedGameSocketSessionKey(key))
        {
            PublishFoundKey(key, KeySourceQqSpeedGameSocketLayout);
            publishedSessionKey = true;
            SecureZeroMemory(key, sizeof(key));
        }

        if (!publishedSessionKey && TryReadRuntimeSessionKey(key))
        {
            PublishFoundKey(key, KeySourceRuntimeMemoryScan);
            publishedSessionKey = true;
            SecureZeroMemory(key, sizeof(key));
        }

        Sleep(WorkerPollMs);
    }

    if (!publishedSessionKey)
    {
        uint8_t existingKey[16] = {};
        if (ReadSharedSessionKey(existingKey))
        {
            publishedSessionKey = true;
        }

        SecureZeroMemory(existingKey, sizeof(existingKey));
    }

    if (!publishedSessionKey)
    {
        InterlockedExchange(&g_workerState, 3);
        WriteSharedWorkerStatus(3, static_cast<LONG>(GetCurrentProcessId()));
    }

    WriteDiagLine(L"worker finished publishedKey=%s attempts=%ld hookAttempts=%ld topKartAttempts=%ld unpackCalls=%ld packCalls=%ld initCalls=%ld topKartCalls=%ld",
        publishedSessionKey ? L"yes" : L"no",
        InterlockedCompareExchange(&g_scanAttempts, 0, 0),
        InterlockedCompareExchange(&g_networkHookInstallAttempts, 0, 0),
        InterlockedCompareExchange(&g_topKartHookInstallAttempts, 0, 0),
        InterlockedCompareExchange(&g_unpackHookCalls, 0, 0),
        InterlockedCompareExchange(&g_packHookCalls, 0, 0),
        InterlockedCompareExchange(&g_gameSocketInitHookCalls, 0, 0),
        InterlockedCompareExchange(&g_topKartRequestCalls, 0, 0));
    return 0;
}

void StartKeyProbeWorker()
{
    HANDLE thread = CreateThread(nullptr, 0, KeyProbeWorker, nullptr, 0, nullptr);
    if (thread == nullptr)
    {
        InterlockedExchange(&g_workerState, 4);
        WriteDiagLine(L"worker start failed gle=%lu", GetLastError());
        return;
    }

    CloseHandle(thread);
}

bool IsTargetGameProcess()
{
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0]))) == 0)
    {
        return false;
    }

    const wchar_t* fileName = std::wcsrchr(path, L'\\');
    fileName = fileName != nullptr ? fileName + 1 : path;
    return _wcsicmp(fileName, L"GameApp.exe") == 0;
}
}

extern "C" __declspec(dllexport) uint32_t __stdcall KeyProbe_Version()
{
    return 0x00010000u;
}

extern "C" __declspec(dllexport) int32_t __stdcall KeyProbe_GetStatus(wchar_t* buffer, uint32_t bufferChars)
{
    uint8_t sharedKey[16] = {};
    const bool sharedReady = EnsureSharedState() != nullptr;
    const bool sharedHasKey = sharedReady && ReadSharedSessionKey(sharedKey);
    const LONG sharedSource = (sharedReady && sharedHasKey && g_sharedState != nullptr)
        ? g_sharedState->keySource
        : KeySourceNone;
    const LONG sharedWorkerState = (sharedReady && g_sharedState != nullptr)
        ? g_sharedState->workerState
        : InterlockedCompareExchange(&g_workerState, 0, 0);
    const LONG sharedScanAttempts = (sharedReady && g_sharedState != nullptr)
        ? g_sharedState->scanAttempts
        : InterlockedCompareExchange(&g_scanAttempts, 0, 0);
    const LONG sharedRemotePid = (sharedReady && g_sharedState != nullptr)
        ? g_sharedState->remotePid
        : 0;

    AcquireSRWLockShared(&g_keyLock);
    const bool hasSessionKey = g_hasSessionKey || sharedHasKey;
    ReleaseSRWLockShared(&g_keyLock);

    wchar_t diagTail[256] = {};
    const bool hasDiagTail = ReadDiagTailForPid(static_cast<DWORD>(sharedRemotePid), diagTail, _countof(diagTail));

    LONG internalQueueLength = 0;
    SIZE_T internalQueueBytes = 0;
    AcquireSRWLockShared(&g_internalQueueLock);
    internalQueueLength = g_internalQueueLength;
    internalQueueBytes = g_internalQueueBytes;
    ReleaseSRWLockShared(&g_internalQueueLock);

    wchar_t text[1024] = {};
    _snwprintf_s(
        text,
        _TRUNCATE,
        L"key_probe.dll loaded; shared_map=%s; worker=%s; attempts=%ld; remote_pid=%ld; session_key=%s; source=%s; topkart=%s; topkart_calls=%ld; internal_queue=%ld/%llu; queue_warn=%s; internal_drops=%ld; diag_tail=%s",
        sharedReady ? L"ready" : L"failed",
        WorkerStateText(sharedWorkerState),
        sharedScanAttempts,
        sharedRemotePid,
        hasSessionKey ? L"present" : L"missing",
        KeySourceText(sharedSource),
        TopKartStateText(),
        InterlockedCompareExchange(&g_topKartRequestCalls, 0, 0),
        internalQueueLength,
        static_cast<unsigned long long>(internalQueueBytes),
        (internalQueueLength >= InternalQueueWarnItems || internalQueueBytes >= InternalQueueWarnBytes) ? L"yes" : L"no",
        InterlockedCompareExchange(&g_internalPacketDrops, 0, 0),
        hasDiagTail ? diagTail : L"none");

    return CopyStatusText(buffer, bufferChars, text);
}

extern "C" __declspec(dllexport) int32_t __stdcall KeyProbe_SetSessionKeyForTest(const uint8_t* key, uint32_t keyLen)
{
    if (key == nullptr || keyLen != SessionKeyLength)
    {
        return -1;
    }

    AcquireSRWLockExclusive(&g_keyLock);
    for (uint32_t i = 0; i < SessionKeyLength; ++i)
    {
        g_sessionKey[i] = key[i];
    }

    g_hasSessionKey = true;
    ReleaseSRWLockExclusive(&g_keyLock);
    WriteSharedSessionKey(key, KeySourceManualTest);
    return 16;
}

extern "C" __declspec(dllexport) int32_t __stdcall KeyProbe_GetSessionKey(uint8_t* outKey, uint32_t outKeyLen)
{
    if (outKey == nullptr || outKeyLen < SessionKeyLength)
    {
        return -1;
    }

    if (ReadSharedSessionKey(outKey))
    {
        return 16;
    }

    AcquireSRWLockShared(&g_keyLock);
    if (!g_hasSessionKey)
    {
        ReleaseSRWLockShared(&g_keyLock);
        return 0;
    }

    for (uint32_t i = 0; i < SessionKeyLength; ++i)
    {
        outKey[i] = g_sessionKey[i];
    }

    ReleaseSRWLockShared(&g_keyLock);
    return 16;
}

extern "C" __declspec(dllexport) void __stdcall KeyProbe_ClearSessionKey()
{
    AcquireSRWLockExclusive(&g_keyLock);
    SecureZeroMemory(g_sessionKey, sizeof(g_sessionKey));
    g_hasSessionKey = false;
    ReleaseSRWLockExclusive(&g_keyLock);
    ClearSharedSessionKey();
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(module);
        EnsureSharedState();
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, static_cast<DWORD>(_countof(path)));
        WriteDiagLine(L"dll attach module=%p process=%s targetGame=%s", module, path, IsTargetGameProcess() ? L"yes" : L"no");

        // Non-GameApp processes load this DLL only to call exported status/key
        // functions. Do not start a background thread there: the caller may
        // unload the library after probing, and an active thread would execute
        // from an unloaded image. The worker is only needed inside GameApp.exe.
        if (IsTargetGameProcess())
        {
            StartKeyProbeWorker();
        }
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        InterlockedExchange(&g_internalWriterStop, 1);
        WakeAllConditionVariable(&g_internalQueueCv);

        if (g_sharedState != nullptr)
        {
            UnmapViewOfFile(g_sharedState);
            g_sharedState = nullptr;
        }

        if (g_sharedMapping != nullptr)
        {
            CloseHandle(g_sharedMapping);
            g_sharedMapping = nullptr;
        }
    }

    return TRUE;
}
