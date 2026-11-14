#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "Message.h"
#include "Room.h"
#include "Map.h"
#include "oicq/tea.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// 大奖赛入口链路实现。
// 本文件集中处理：400/401/402/815/816/24617 以及本次两人进图测试用的 381/382/383/384/803/810/811。
// 当前目标：两人报名后创建真实 RoomNode，按官服链路 815 -> 客户端真实 381 -> 810/811/803/804
// -> 382 status=2 -> 382/803 status=5 -> 客户端 120 prepare-ready -> 509/551/511。
// 官服赛制是 32 人大奖赛；当前私服只降低报名触发人数，包序/结构/阶段仍按官服抓包推进。

extern std::string IP;
extern std::string BindIP;
extern USHORT TcpPort;
extern USHORT UdpPort;
extern USHORT BindTcpPort;
extern USHORT BindUdpPort;
extern void RecycleRoomID(int id);

void ScheduleLuckyMatchOfficialMiddle(DWORD roomId);
void ScheduleLuckyMatchNextElimRound(DWORD roomId, BYTE nextRound);

namespace
{
constexpr BYTE   kLuckyOfficialNeedPlayers = 32;
constexpr BYTE   kLuckyNeedPlayers = 2;       // 本地两开触发人数；官服字段参考 kLuckyOfficialNeedPlayers(32)
constexpr UINT   kLuckyMatchTimeoutSec = 300; // wait timeout shown by cmd 400
constexpr USHORT kLuckySingleServerDefaultId = 113; // single-server default game server id
constexpr int    kLuckyMaxMapNum = 500;
constexpr int    kLuckyMaxOpenTimeNum = 5;
constexpr int    kLuckyBillboardMax = 10;
constexpr size_t kLuckyCreateRoomReasonMax = 0x800;
constexpr UINT   kLuckyDefaultMapId = 200;    // 当前 Map.yml 已存在的普通地图，避免随机池导致进图失败
constexpr DWORD  kLuckyEnterRoomCountdownSec = 73; // 官服抓包：382 status=2 leftTime=73
constexpr DWORD  kLuckyCountdownToRacingDelayMs = 3000; // 官服抓包：status=2 后约 3 秒推进到 status=5
constexpr DWORD  kLuckyRacingStatusLeftSec = 70; // 官服抓包：382/803 status=5 leftTime=70
constexpr DWORD  kLuckyTop16AdvanceDelayMs = 3000; // 本地调度等待；包里的 leftTime 仍按官服 70，不把显示字段当墙钟 sleep
constexpr DWORD  kLuckyRaceWaitLeftSec = 230; // 官服抓包：首次 803 status=8 left=230，后续轮次也保持官服等待语义
constexpr DWORD  kLuckyRaceWaitAdvanceDelayMs = 3000; // 本地调度等待；包里的 leftTime 仍按官服 230
constexpr DWORD  kLuckyRollLeftSec = 10; // 官服抓包：status=9/10 left=10
constexpr DWORD  kLuckyRollAdvanceDelayMs = 3000; // 本地调度等待；包里的 leftTime 仍按官服 10
constexpr DWORD  kLuckyWinnerLuckMoney = 3000; // v1 两人决赛冠军奖励展示值；不落库
constexpr DWORD  kLuckyWinnerMatchPoint = 100; // v1 两人决赛冠军积分展示值；不落库
constexpr BYTE   kLuckyMatchOverReasonNormal = 1; // 805 里 0 会被客户端当“网络异常(0)”终止弹窗；1 作为正常完成收尾
constexpr BYTE   kLuckyUiRoundNum = 2; // 两人 v1 只缩玩家数，UI/ScoreInfo 仍按官服 2 轮语义推进
constexpr USHORT kLuckyCreateRoomScoreMinNum = 16; // 官服 381 尾部 ScoreMin 默认值
constexpr bool   kLuckySingleServerLocalEnter = false; // 不能本地直发 381/804；客户端必须先收 815 才会从匹配界面切进大奖赛房
constexpr bool   kLuckyRetry815 = false;               // 815 是切服指令，不能重复轰炸；只保留手动开关
constexpr bool   kLuckySynthesize381From439 = false;   // 381 已由重登录链真实触发，439 只 attach；禁止重复合成 381/804 干扰 Top 处理
constexpr bool   kLuckySendOfficial804 = true;         // 381 ok 后恢复 804；body 内 0x0389 结构长度必须原样保留

enum LuckyStatus : BYTE
{
    LMT_STATUS_WAIT_ENTER = 1,
    LMT_STATUS_COUNTDOWN  = 2,
    LMT_STATUS_RACING     = 5,
    LMT_STATUS_ELIM_WAIT   = 6,
    LMT_STATUS_ELIM_COUNT  = 7,
    LMT_STATUS_RACE_WAIT   = 8,
    LMT_STATUS_ROLL_READY  = 9,
    LMT_STATUS_ROLLING     = 10,
    LMT_STATUS_TOP16       = 12,
};

struct LuckyJoinEntry
{
    DWORD uin = 0;
    DWORD connId = 0; // 本地两开/同号测试时用连接号区分排队实体，避免第二个客户端把第一个刷新掉
    std::weak_ptr<ClientNode> client;
    DWORD roomId = 0;
    std::chrono::steady_clock::time_point joinTime;
    bool matched = false;
};

struct LuckyCreateRoomEntry
{
    DWORD ownerUin = 0;
    DWORD roomNum = 0;
    BYTE createRoomType = 1;
    BYTE roundNum = kLuckyUiRoundNum;
    BYTE playerNum = 0;
    std::chrono::steady_clock::time_point createTime;
};

struct LuckyIPv4Octets
{
    BYTE octet[4]{};
};

bool ParseLuckyIPv4Text(const char* text, LuckyIPv4Octets& out)
{
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (!text || sscanf_s(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail, 1) != 4)
        return false;
    if (a > 255 || b > 255 || c > 255 || d > 255)
        return false;
    out.octet[0] = static_cast<BYTE>(a);
    out.octet[1] = static_cast<BYTE>(b);
    out.octet[2] = static_cast<BYTE>(c);
    out.octet[3] = static_cast<BYTE>(d);
    return true;
}

UINT LuckyIPv4WireLogValue(const LuckyIPv4Octets& ip)
{
    // 官服 815 抓包证明：body 里 IP 按 IPv4 数字小端写。
    // 例如官服 body=3F 4B 0E 79，客户端实际连接 121.14.75.63。
    // 所以源码文本 "43.226.61.100" 的 815 wire 应为 64 3D E2 2B。
    return (static_cast<UINT>(ip.octet[3]) << 24) |
        (static_cast<UINT>(ip.octet[2]) << 16) |
        (static_cast<UINT>(ip.octet[1]) << 8) |
        static_cast<UINT>(ip.octet[0]);
}

void WriteLuckyIPv4Raw(BYTE*& p, const LuckyIPv4Octets& ip)
{
    // 815 里 IP 是 IPv4 数字的小端原始字节；源码保持 "43.226.61.100" 可读。
    Write8(p, ip.octet[3]);
    Write8(p, ip.octet[2]);
    Write8(p, ip.octet[1]);
    Write8(p, ip.octet[0]);
}

struct LuckyRoomState
{
    DWORD roomId = 0;
    DWORD ownerUin = 0;
    UINT mapId = kLuckyDefaultMapId;
    BYTE status = LMT_STATUS_WAIT_ENTER;
    DWORD leftTime = 0;
    BYTE roundNum = kLuckyUiRoundNum;
    bool countdownNotified = false;
    bool startQueued = false;      // 509 已按官服 120 ready 链发送
    bool raceBeginSent = false;    // 551/511 已发送
    bool officialMiddleQueued = false; // status=5 后的官服 810/811/24602/24607/384/809/804 中间链只排一次
    bool officialMiddleSent = false;
    BYTE elimRound = 3;                 // 官服 384/809 淘汰树轮次：3 -> 2 -> 1 -> 0
    bool roundAdvanceQueued = false;    // 513 后下一轮 803/384/809/804/509 链只排一次
    std::set<DWORD> raceOverUins;       // 当前淘汰轮已收到 RaceOver 的玩家
    std::vector<std::weak_ptr<ClientNode>> players;
    std::set<DWORD> enteredUins;
    std::set<DWORD> enteredConnIds;
    std::set<DWORD> prepareReadyConnIds;
    // 815 会让客户端先断旧连接、再用新 ConnID 重新登录同一 GameServer。
    // 这里记录“切服重连保护窗口”，避免旧 ConnID 的 OnClose 被当成普通离房。
    std::set<DWORD> switchPendingUins;
    std::set<DWORD> switchPendingConnIds;
    bool matchOverStarted = false;              // 已进入 LMT 专用赛后结算，后续普通 514 不再直接接管
    DWORD winnerUin = 0;
    std::set<DWORD> matchOverNotifiedUins;
};

std::mutex g_luckyMutex;
std::mutex g_luckyTraceMutex;
std::vector<LuckyJoinEntry> g_waitingPlayers;
std::map<DWORD, DWORD> g_uinRoomId;
std::map<DWORD, DWORD> g_connRoomId;
std::map<DWORD, LuckyRoomState> g_luckyRooms;
std::vector<LuckyCreateRoomEntry> g_createdRooms;
std::atomic<DWORD> g_nextCreateRoomNum{ 0x0037EC9A }; // old create-room token seed, now dynamic
std::atomic<int> g_lucky815MsgSeq{ 1 }; // 815 官方抓包 MsgSeq 非 -1；测试用递增序列排除客户端忽略 -1
DWORD g_lastChampionUin = 0;
std::string g_lastChampionName;

constexpr const char* kLuckyTracePath = "F:\\new1\\21043 ver\\lucky_match_trace.log";

bool CanRead(const BYTE* begin, const BYTE* end, size_t n)
{
    return begin && end && begin <= end && static_cast<size_t>(end - begin) >= n;
}

DWORD Read32Safe(BYTE*& p, const BYTE* end, DWORD def = 0)
{
    if (!CanRead(p, end, 4)) return def;
    return Read32(p);
}

WORD Read16Safe(BYTE*& p, const BYTE* end, WORD def = 0)
{
    if (!CanRead(p, end, 2)) return def;
    return Read16(p);
}

BYTE Read8Safe(BYTE*& p, const BYTE* end, BYTE def = 0)
{
    if (!CanRead(p, end, 1)) return def;
    return Read8(p);
}

BYTE* BeginLen(BYTE*& p)
{
    BYTE* start = p;
    Write16(p, 0);
    return start;
}

void FinishLen(BYTE* start, BYTE* p)
{
    if (!start || !p || p < start) return;
    Set16(start, static_cast<WORD>(p - start));
}

bool TrySkipWireTopLen(BYTE*& p, const BYTE* end, size_t minPayload = 0, WORD* outWireLen = nullptr)
{
    if (!CanRead(p, end, 2))
        return false;

    BYTE* saved = p;
    const WORD wireLen = Read16(p);
    const size_t available = static_cast<size_t>(end - saved);
    if (wireLen >= 2 && wireLen <= available && static_cast<size_t>(wireLen - 2) >= minPayload)
    {
        if (outWireLen)
            *outWireLen = wireLen;
        return true;
    }

    p = saved;
    return false;
}

void HexBytesUpper(const BYTE* data, DWORD len, char* out, size_t outLen)
{
    if (!out || outLen == 0)
        return;
    out[0] = '\0';
    if (!data)
        return;

    size_t pos = 0;
    for (DWORD i = 0; i < len && pos + 2 < outLen; ++i)
    {
        const int written = std::snprintf(out + pos, outLen - pos, "%02X", static_cast<unsigned>(data[i]));
        if (written != 2)
            break;
        pos += 2;
    }
    out[(pos < outLen) ? pos : (outLen - 1)] = '\0';
}

void LmtLog(const char* fmt, ...)
{
    char msg[8192]{};
    va_list args;
    va_start(args, fmt);
    const int written = std::vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (written < 0)
        strcpy_s(msg, sizeof(msg), "format-error");
    else
        msg[sizeof(msg) - 1] = '\0';

    std::printf("[LMT] %s\n", msg);
    std::fflush(stdout);

    char exePath[MAX_PATH]{};
    DWORD exeLen = GetModuleFileNameA(nullptr, exePath, static_cast<DWORD>(sizeof(exePath)));
    if (exeLen == 0 || exeLen >= sizeof(exePath))
        strcpy_s(exePath, sizeof(exePath), "unknown");

    std::time_t now = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &now);
    char ts[32]{};
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    std::lock_guard<std::mutex> traceLock(g_luckyTraceMutex);
    FILE* fp = nullptr;
    if (fopen_s(&fp, kLuckyTracePath, "ab") == 0 && fp)
    {
        std::fprintf(fp, "%s pid=%lu exe=%s %s\n",
            ts,
            static_cast<unsigned long>(GetCurrentProcessId()),
            exePath,
            msg);
        std::fclose(fp);
    }
}

bool IsLuckyContestMsgHead14(short msgId)
{
    switch (msgId)
    {
    case 381:
    case 382:
    case 383:
    case 384:
    case 385:
    case 400:
    case 401:
    case 402:
    case 801:
    case 802:
    case 803:
    case 804:
    case 805:
    case 806:
    case 807:
    case 808:
    case 809:
    case 810:
    case 811:
    case 815:
    case 816:
    case 24607:
    case 24617:
        return true;
    default:
        return false;
    }
}

BOOL SendLuckyToClient(std::shared_ptr<ClientNode> client, short msgId, const BYTE* data, DWORD length,
    short srcId, char srcFe, short dstId, short msgType, int msgSeq = -1, char encrypt = true)
{
    if (!client || client->TransportConnID <= 0 || !client->Server || client->ProtocolVersion == 0)
        return FALSE;

    // 官服 LuckyContest/LMT 表里的 381/382/383/80x/815 等包外层都是 14 字节 MSGHEAD，
    // 不带旧 MSGHEAD.BodyLen。body 自己的前 2 字节才是 wire StructLen。
    // 如果走 16 字节旧头，客户端会把 BodyLen 当成 body 头，381 会把真正的 StructLen(如 265)
    // 当 ResultID，表现为“进入大奖赛房间出错 265”。
    if (IsLuckyContestMsgHead14(msgId))
    {
#pragma pack(push, 1)
        struct LuckyMsgHead14
        {
            unsigned short MsgID;
            short MsgType;
            int MsgSeq;
            char SrcFE;
            char DstFE;
            short SrcID;
            short DstID;
        };
#pragma pack(pop)
        static_assert(sizeof(LuckyMsgHead14) == 14, "LuckyContest packets must use official 14-byte msg head");

        const int msgLen = static_cast<int>(sizeof(LuckyMsgHead14) + length);
        LuckyMsgHead14* msgHead = static_cast<LuckyMsgHead14*>(std::malloc(msgLen));
        if (!msgHead)
            return FALSE;

        msgHead->MsgID = htons(msgId);
        msgHead->MsgType = htons(msgType);
        msgHead->MsgSeq = htonl(msgSeq);
        msgHead->SrcFE = srcFe;
        msgHead->DstFE = FE_CLIENT;
        msgHead->SrcID = htons(srcId);
        msgHead->DstID = htons(dstId > 0 ? dstId : 1);
        if (length > 0 && data)
            memcpy(msgHead + 1, data, length);

        char msgHeadHex[64]{};
        HexBytesUpper(reinterpret_cast<const BYTE*>(msgHead), static_cast<DWORD>(sizeof(LuckyMsgHead14)), msgHeadHex, sizeof(msgHeadHex));

        CSHEAD* proto = nullptr;
        int totalLength = 0;
        if (encrypt)
        {
            int encryptLen = oi_symmetry_encrypt2_len(msgLen);
            totalLength = static_cast<int>(sizeof(CSHEAD) + encryptLen);
            proto = static_cast<CSHEAD*>(std::malloc(totalLength));
            if (!proto)
            {
                std::free(msgHead);
                return FALSE;
            }
            oi_symmetry_encrypt2(reinterpret_cast<BYTE*>(msgHead), msgLen, client->Key, reinterpret_cast<BYTE*>(proto + 1), &encryptLen);
            totalLength = static_cast<int>(sizeof(CSHEAD) + encryptLen);
        }
        else
        {
            totalLength = static_cast<int>(sizeof(CSHEAD) + msgLen);
            proto = static_cast<CSHEAD*>(std::malloc(totalLength));
            if (!proto)
            {
                std::free(msgHead);
                return FALSE;
            }
            memcpy(proto + 1, msgHead, msgLen);
        }
        std::free(msgHead);

        proto->TotalLength = htonl(totalLength);
        proto->Ver = static_cast<short>(htons(client->ProtocolVersion));
        proto->DialogID = htons(srcId);
        proto->Seq = htonl(-1);
        proto->Uin = htonl(client->Uin);
        proto->BodyFlag = encrypt;
        proto->OptLength = 0;

        char csHeadHex[64]{};
        HexBytesUpper(reinterpret_cast<const BYTE*>(proto), static_cast<DWORD>(std::min<int>(static_cast<int>(sizeof(CSHEAD)), totalLength)), csHeadHex, sizeof(csHeadHex));
        LmtLog("SEND %d HEAD14 TransportConnID=%u Uin=%u MsgID=%d MsgType=%d MsgSeq=%d SrcFE=%d DstFE=%d SrcID=%d DstIDArg=%d WireDstID=%d BodyLenField=none BodyStructLen=%u CSVer=%u CSDialogID=%d CSSeq=%d CSTotal=%d BodyFlag=%d OptLen=%u CSHeadHex=%s MsgHeadHex=%s",
            (int)msgId,
            client->TransportConnID,
            client->Uin,
            (int)msgId,
            (int)msgType,
            msgSeq,
            (int)srcFe,
            (int)FE_CLIENT,
            (int)srcId,
            (int)dstId,
            (int)(dstId > 0 ? dstId : 1),
            (unsigned)length,
            (unsigned)client->ProtocolVersion,
            (int)srcId,
            -1,
            totalLength,
            encrypt ? 1 : 0,
            0u,
            csHeadHex,
            msgHeadHex);

        client->Server->Send(client->TransportConnID, reinterpret_cast<BYTE*>(proto), totalLength);
        std::free(proto);
        return TRUE;
    }

    const int msgLen = static_cast<int>(sizeof(MSGHEAD) + length);
    MSGHEAD* msgHead = static_cast<MSGHEAD*>(std::malloc(msgLen));
    if (!msgHead)
        return FALSE;

    msgHead->MsgID = htons(msgId);
    msgHead->MsgType = htons(msgType);
    msgHead->MsgSeq = htonl(msgSeq);
    msgHead->SrcFE = srcFe;
    msgHead->DstFE = FE_CLIENT;
    msgHead->SrcID = htons(srcId);
    // 与全局 SendToClient 保持一致：客户端侧普通下发 DstID 为 1，
    // BodyLen 采用“实际 body + 2”的老协议口径。否则 815 虽然 body 正确，
    // 但客户端调度层可能因头字段不匹配而不进入 CNotifyLuckyMatchReadyPara，自然不会发 381。
    msgHead->DstID = htons(1);
    msgHead->BodyLen = htons(static_cast<USHORT>(sizeof(USHORT) + USHORT(length)));
    if (length > 0 && data)
        memcpy(msgHead + 1, data, length);

    char msgHeadHex[64]{};
    if (msgId == 815)
        HexBytesUpper(reinterpret_cast<const BYTE*>(msgHead), static_cast<DWORD>(std::min<int>(msgLen, 16)), msgHeadHex, sizeof(msgHeadHex));

    CSHEAD* proto = nullptr;
    int totalLength = 0;
    if (encrypt)
    {
        int encryptLen = oi_symmetry_encrypt2_len(msgLen);
        totalLength = static_cast<int>(sizeof(CSHEAD) + encryptLen);
        proto = static_cast<CSHEAD*>(std::malloc(totalLength));
        if (!proto)
        {
            std::free(msgHead);
            return FALSE;
        }
        oi_symmetry_encrypt2(reinterpret_cast<BYTE*>(msgHead), msgLen, client->Key, reinterpret_cast<BYTE*>(proto + 1), &encryptLen);
        totalLength = static_cast<int>(sizeof(CSHEAD) + encryptLen);
    }
    else
    {
        totalLength = static_cast<int>(sizeof(CSHEAD) + msgLen);
        proto = static_cast<CSHEAD*>(std::malloc(totalLength));
        if (!proto)
        {
            std::free(msgHead);
            return FALSE;
        }
        memcpy(proto + 1, msgHead, msgLen);
    }
    std::free(msgHead);

    proto->TotalLength = htonl(totalLength);
    proto->Ver = static_cast<short>(htons(client->ProtocolVersion));
    proto->DialogID = htons(srcId);
    proto->Seq = htonl(-1);
    proto->Uin = htonl(client->Uin);
    proto->BodyFlag = encrypt;
    proto->OptLength = 0;

    if (msgId == 815)
    {
        char csHeadHex[64]{};
        HexBytesUpper(reinterpret_cast<const BYTE*>(proto), static_cast<DWORD>(std::min<int>(static_cast<int>(sizeof(CSHEAD)), totalLength)), csHeadHex, sizeof(csHeadHex));
        LmtLog("SEND 815 HEAD TransportConnID=%u Uin=%u MsgID=%d MsgType=%d MsgSeq=%d SrcFE=%d DstFE=%d SrcID=%d DstIDArg=%d WireDstID=1 BodyLen=%u WireBodyLen=%u CSVer=%u CSDialogID=%d CSSeq=%d CSTotal=%d BodyFlag=%d OptLen=%u CSHeadHex=%s MsgHeadHex=%s",
            client->TransportConnID,
            client->Uin,
            (int)msgId,
            (int)msgType,
            msgSeq,
            (int)srcFe,
            (int)FE_CLIENT,
            (int)srcId,
            (int)dstId,
            (unsigned)length,
            (unsigned)(sizeof(USHORT) + USHORT(length)),
            (unsigned)client->ProtocolVersion,
            (int)srcId,
            -1,
            totalLength,
            encrypt ? 1 : 0,
            0u,
            csHeadHex,
            msgHeadHex);
    }

    client->Server->Send(client->TransportConnID, reinterpret_cast<BYTE*>(proto), totalLength);
    std::free(proto);
    return TRUE;
}

BOOL SendLuckyPlayerPacket(std::shared_ptr<ClientNode> client, short msgId, const BYTE* data, DWORD length, short msgType)
{
    if (!client) return FALSE;
    const short id = GetClientWirePlayerHeaderID(client);
    return SendLuckyToClient(client, msgId, data, length, id, FE_PLAYER, 1, msgType, -1, true);
}

BOOL SendLuckyMatchReadyPacket(std::shared_ptr<ClientNode> client, const BYTE* data, DWORD length)
{
    if (!client) return FALSE;
    const short id = GetClientWirePlayerHeaderID(client);
    const int msgSeq = g_lucky815MsgSeq.fetch_add(1);
    return SendLuckyToClient(client, 815, data, length, id, FE_PLAYER, 1, Notify, msgSeq, true);
}

BOOL SendLuckyRoomPacket(std::shared_ptr<ClientNode> client, short msgId, const BYTE* data, DWORD length, DWORD roomId, short msgType)
{
    if (!client) return FALSE;
    return SendLuckyToClient(client, msgId, data, length, static_cast<short>(roomId), FE_LUCKYMATCHROOM,
        1, msgType, 0, true);
}

DWORD LuckyQueueKey(DWORD uin, DWORD connId)
{
    return connId != 0 ? connId : uin;
}

bool IsUsableIpText(const std::string& v)
{
    return !v.empty() && v != "0.0.0.0" && v != "::" && v != "localhost";
}

const char* ChooseLuckySingleServerIpText()
{
    if (IsUsableIpText(IP)) return IP.c_str();
    if (IsUsableIpText(BindIP)) return BindIP.c_str();
    return "127.0.0.1";
}

DWORD ClientUinOrBody(std::shared_ptr<ClientNode> client, DWORD bodyUin)
{
    if (client && client->Uin != 0) return client->Uin;
    return bodyUin;
}

USHORT GetLuckySingleServerId(std::shared_ptr<ClientNode> client)
{
    // Private server runs LuckyMatch on the current game server, not match server 300.
    // If 815 points to another server id, the client treats it as a server switch and may not send 381.
    if (client && client->ServerID != 0)
        return static_cast<USHORT>(client->ServerID);
    return kLuckySingleServerDefaultId;
}

UINT ChooseLuckyMapID()
{
    const UINT candidates[] = { kLuckyDefaultMapId, 320, 381, 400, 100, 11 };
    for (UINT id : candidates)
    {
        if (GetMap(id))
            return id;
    }
    return kLuckyDefaultMapId;
}

std::string BuildWaitingSnapshotLocked();

void PruneDeadWaitingLocked()
{
    const size_t before = g_waitingPlayers.size();
    const auto now = std::chrono::steady_clock::now();
    g_waitingPlayers.erase(
        std::remove_if(g_waitingPlayers.begin(), g_waitingPlayers.end(), [&](const LuckyJoinEntry& e) {
            if (e.client.expired()) return true;
            if (std::chrono::duration_cast<std::chrono::seconds>(now - e.joinTime).count() > (kLuckyMatchTimeoutSec + 60))
                return true;
            return false;
        }),
        g_waitingPlayers.end());
    if (g_waitingPlayers.size() != before)
    {
        const std::string snapshot = BuildWaitingSnapshotLocked();
        LmtLog("QUEUE prune before=%zu after=%zu queue=%s", before, g_waitingPlayers.size(), snapshot.c_str());
    }
}

size_t CountRecentJoinsPerMinuteLocked()
{
    const auto now = std::chrono::steady_clock::now();
    const auto cutoff = now - std::chrono::seconds(60);
    return static_cast<size_t>(std::count_if(g_waitingPlayers.begin(), g_waitingPlayers.end(), [&](const LuckyJoinEntry& e) {
        return !e.client.expired() && e.joinTime >= cutoff;
    }));
}

std::string BuildWaitingSnapshotLocked()
{
    std::string out;
    char tmp[192]{};
    for (const auto& e : g_waitingPlayers)
    {
        auto c = e.client.lock();
        snprintf(tmp, sizeof(tmp), "[uin=%u conn=%u login=%u del=%d server=%u room=%u matched=%u]",
            e.uin,
            e.connId,
            c ? (c->IsLogin ? 1U : 0U) : 0U,
            c ? static_cast<int>(c->deleteflag) : -1,
            c ? static_cast<unsigned>(c->ServerID) : 0U,
            c ? static_cast<unsigned>(c->RoomID) : 0U,
            e.matched ? 1U : 0U);
        out += tmp;
    }
    if (out.empty())
        out = "[]";
    return out;
}

void AddOrRefreshWaitingLocked(DWORD uin, DWORD connId, std::shared_ptr<ClientNode> client)
{
    const DWORD key = LuckyQueueKey(uin, connId);
    const size_t before = g_waitingPlayers.size();
    for (auto& e : g_waitingPlayers)
    {
        if (LuckyQueueKey(e.uin, e.connId) == key)
        {
            e.uin = uin;
            e.connId = connId;
            e.client = client;
            e.joinTime = std::chrono::steady_clock::now();
            const std::string snapshot = BuildWaitingSnapshotLocked();
            LmtLog("QUEUE refresh key=%u uin=%u conn=%u before=%zu after=%zu queue=%s",
                key, uin, connId, before, g_waitingPlayers.size(), snapshot.c_str());
            return;
        }
    }

    LuckyJoinEntry e;
    e.uin = uin;
    e.connId = connId;
    e.client = client;
    e.joinTime = std::chrono::steady_clock::now();
    g_waitingPlayers.push_back(e);
    const std::string snapshot = BuildWaitingSnapshotLocked();
    LmtLog("QUEUE add key=%u uin=%u conn=%u before=%zu after=%zu queue=%s",
        key, uin, connId, before, g_waitingPlayers.size(), snapshot.c_str());
}

void RemoveWaitingLocked(DWORD uin, DWORD connId)
{
    const DWORD key = LuckyQueueKey(uin, connId);
    g_waitingPlayers.erase(
        std::remove_if(g_waitingPlayers.begin(), g_waitingPlayers.end(), [&](const LuckyJoinEntry& e) {
            return LuckyQueueKey(e.uin, e.connId) == key;
        }),
        g_waitingPlayers.end());

    DWORD roomId = 0;
    auto cit = g_connRoomId.find(connId);
    if (cit != g_connRoomId.end())
    {
        roomId = cit->second;
        g_connRoomId.erase(cit);
    }

    auto it = g_uinRoomId.find(uin);
    if (it != g_uinRoomId.end())
    {
        if (roomId == 0) roomId = it->second;
        g_uinRoomId.erase(it);
    }

    if (roomId != 0)
    {
        auto rit = g_luckyRooms.find(roomId);
        if (rit != g_luckyRooms.end() && rit->second.enteredConnIds.empty() && !rit->second.startQueued)
        {
            bool stillReferenced = false;
            for (const auto& kv : g_connRoomId)
            {
                if (kv.second == roomId)
                {
                    stillReferenced = true;
                    break;
                }
            }
            if (!stillReferenced)
                g_luckyRooms.erase(rit);
        }
    }
}

std::vector<std::shared_ptr<ClientNode>> CollectPlayers(const LuckyRoomState& state)
{
    std::vector<std::shared_ptr<ClientNode>> out;
    for (const auto& w : state.players)
    {
        auto c = w.lock();
        if (c)
            out.push_back(c);
    }
    return out;
}

bool IsLuckyLiveClientLocked(const std::shared_ptr<ClientNode>& client, DWORD roomId)
{
    if (!client || roomId == 0 || client->Uin == 0 || client->TransportConnID == 0)
        return false;
    if (!client->Server || !client->IsLogin || client->deleteflag)
        return false;

    auto cit = g_connRoomId.find(client->TransportConnID);
    if (cit != g_connRoomId.end() && cit->second == roomId)
        return true;

    auto uit = g_uinRoomId.find(client->Uin);
    return uit != g_uinRoomId.end() && uit->second == roomId;
}

std::vector<std::shared_ptr<ClientNode>> CollectLiveRoomPlayersLocked(std::shared_ptr<RoomNode> room, DWORD roomId)
{
    std::vector<std::shared_ptr<ClientNode>> out;
    if (!room || roomId == 0)
        return out;

    std::set<DWORD> seenConns;
    for (auto& client : room->Player)
    {
        if (!IsLuckyLiveClientLocked(client, roomId))
            continue;
        if (seenConns.find(client->TransportConnID) != seenConns.end())
            continue;
        seenConns.insert(client->TransportConnID);
        out.push_back(client);
    }
    return out;
}

bool IsLuckyParticipantLocked(const LuckyRoomState& state, std::shared_ptr<ClientNode> client, DWORD uin)
{
    if (state.roomId == 0)
        return false;

    if (uin != 0)
    {
        if (state.switchPendingUins.find(uin) != state.switchPendingUins.end())
            return true;

        auto uit = g_uinRoomId.find(uin);
        if (uit != g_uinRoomId.end() && uit->second == state.roomId)
            return true;
    }

    if (client)
    {
        auto cit = g_connRoomId.find(client->TransportConnID);
        if (cit != g_connRoomId.end() && cit->second == state.roomId)
            return true;

        if (state.switchPendingConnIds.find(client->TransportConnID) != state.switchPendingConnIds.end())
            return true;
    }

    for (const auto& w : state.players)
    {
        auto p = w.lock();
        if (p && uin != 0 && p->Uin == uin)
            return true;
    }
    return false;
}

DWORD FindLuckyRoomIdLocked(std::shared_ptr<ClientNode> client, DWORD reqRoomId = 0)
{
    if (reqRoomId != 0 && g_luckyRooms.find(reqRoomId) != g_luckyRooms.end())
        return reqRoomId;

    if (client)
    {
        auto cit = g_connRoomId.find(client->TransportConnID);
        if (cit != g_connRoomId.end() && cit->second != 0)
            return cit->second;

        auto it = g_uinRoomId.find(client->Uin);
        if (it != g_uinRoomId.end() && it->second != 0)
            return it->second;

        if (client->RoomID != 0)
        {
            DWORD rid = static_cast<DWORD>(client->RoomID);
            if (g_luckyRooms.find(rid) != g_luckyRooms.end())
                return rid;
        }
    }
    return 0;
}

void ResetLuckyClientForLobby(std::shared_ptr<ClientNode> client)
{
    if (!client) return;
    client->RoomID = 0;
    client->CurrentRoom = NULL;
    client->BorderRoomID = 0;
    client->ChatRoomID = -1;
    client->SeatID = 0;
    client->ReadyState = 1;
    client->IsReady = FALSE;
    ResetNormalRaceWireState(client);
    client->GameID = 0;
    client->isRace = 0;
}

struct LuckyCancelResult
{
    DWORD roomId = 0;
    bool hadRoom = false;
    bool roomDestroyed = false;
    size_t waitingCount = 0;
    std::vector<std::shared_ptr<ClientNode>> requeuedPlayers;
};

LuckyCancelResult CancelLuckyParticipationLocked(std::shared_ptr<ClientNode> client, DWORD uin)
{
    LuckyCancelResult result;
    if (!client)
        return result;

    const DWORD connId = client->TransportConnID;
    const DWORD key = LuckyQueueKey(uin, connId);

    g_waitingPlayers.erase(
        std::remove_if(g_waitingPlayers.begin(), g_waitingPlayers.end(), [&](const LuckyJoinEntry& e) {
            return LuckyQueueKey(e.uin, e.connId) == key;
        }),
        g_waitingPlayers.end());

    result.roomId = FindLuckyRoomIdLocked(client);
    result.hadRoom = result.roomId != 0;

    auto cit = g_connRoomId.find(connId);
    if (cit != g_connRoomId.end())
        g_connRoomId.erase(cit);

    auto uit = g_uinRoomId.find(uin);
    if (uit != g_uinRoomId.end() && (result.roomId == 0 || uit->second == result.roomId))
        g_uinRoomId.erase(uit);

    if (result.roomId != 0)
    {
        auto rit = g_luckyRooms.find(result.roomId);
        if (rit != g_luckyRooms.end())
        {
            const bool canDestroyTestRoom = !rit->second.startQueued &&
                !rit->second.raceBeginSent &&
                rit->second.enteredConnIds.empty();

            if (canDestroyTestRoom)
            {
                const auto players = CollectPlayers(rit->second);
                for (auto& p : players)
                {
                    if (!p) continue;

                    g_connRoomId.erase(p->TransportConnID);
                    if (p->Uin != 0)
                    {
                        auto pit = g_uinRoomId.find(p->Uin);
                        if (pit != g_uinRoomId.end() && pit->second == result.roomId)
                            g_uinRoomId.erase(pit);
                    }

                    ResetLuckyClientForLobby(p);
                    if (p->TransportConnID != connId)
                    {
                        AddOrRefreshWaitingLocked(p->Uin, p->TransportConnID, p);
                        result.requeuedPlayers.push_back(p);
                    }
                }

                g_luckyRooms.erase(rit);
                result.roomDestroyed = true;
            }
            else
            {
                rit->second.enteredConnIds.erase(connId);
                rit->second.enteredUins.erase(uin);
            }
        }
    }

    ResetLuckyClientForLobby(client);
    result.waitingCount = g_waitingPlayers.size();
    return result;
}

DWORD CreateLuckyRoomLocked(const std::vector<std::shared_ptr<ClientNode>>& clients)
{
    if (clients.empty())
        return 0;

    std::shared_ptr<RoomNode> room = NewRoom();
    if (!room)
        return 0;

    const UINT mapId = ChooseLuckyMapID();

    memset(room->Name, 0, sizeof(room->Name));
    strncpy_s(room->Name, sizeof(room->Name), "LuckyMatch-2P", _TRUNCATE);
    room->ServerID = GetLuckySingleServerId(clients[0]);
    // 官服大奖赛 509 解包：m_ushBaseMode=1, m_ushSubMode=1, m_ucHasCustomScheme=0。
    // Room/Client 状态也保持 1/1，避免 Top-Kart 后续按普通单人/未知模式分支处理。
    room->BaseGameMode = 1;
    room->SubGameMode = 1;
    room->SeatNum = kLuckyNeedPlayers;
    room->CurrentPlayerNum = static_cast<BYTE>(std::min<size_t>(clients.size(), kLuckyNeedPlayers));
    room->Flag = 0x18C;
    room->ChatRoomType = 0;
    room->SpeFlag = 0;
    room->CreateRoomType = 1;
    room->LuckyMatchInternalRoom = true;
    room->AllowAutoStart = 1;
    room->MapID = mapId;
    room->RaceStartPlayerNum = room->CurrentPlayerNum;
    room->RaceActiveMask = 0;
    room->startStatus = 1;
    room->Israce = 0;
    room->isSkip = 0;

    for (size_t i = 0; i < room->Player.size(); ++i)
        room->Player[i].reset();
    for (size_t i = 0; i < 6; ++i)
        room->SeatState[i] = 1;

    LuckyRoomState state;
    state.roomId = room->ID;
    state.ownerUin = clients[0] ? clients[0]->Uin : 0;
    state.mapId = mapId;
    state.status = LMT_STATUS_WAIT_ENTER;
    state.leftTime = 0;
    state.roundNum = kLuckyUiRoundNum;

    BYTE seat = 0;
    for (auto& c : clients)
    {
        if (!c || seat >= kLuckyNeedPlayers)
            continue;

        room->Player[seat] = c;
        room->SeatState[seat] = 0;
        room->RaceActiveMask |= static_cast<BYTE>(1 << seat);

        c->RoomID = static_cast<USHORT>(room->ID);
        c->CurrentRoom = room.get();
        c->BorderRoomID = 0;
        c->ChatRoomID = -1;
        c->SeatID = static_cast<char>(seat);
        c->ReadyState = 0;
        c->IsReady = FALSE;
        c->MapID = mapId;
        c->BaseGameMode = room->BaseGameMode;
        c->SubGameMode = room->SubGameMode;

        if (c->Uin != 0)
            g_uinRoomId[c->Uin] = room->ID;
        g_connRoomId[c->TransportConnID] = room->ID;
        state.players.push_back(c);
        ++seat;
    }

    room->RoomOwnerUin = state.ownerUin;
    room->RoomOwnerID = clients[0] ? GetRoomConnPlayerID(room, clients[0]) : 0;

    g_luckyRooms[room->ID] = state;
    LmtLog("CREATE roomId=%u map=%u players=%zu need=%u ownerUin=%u ownerConn=%u ownerWire=%u serverId=%u singleServer=1",
        room->ID,
        mapId,
        state.players.size(),
        (unsigned)kLuckyNeedPlayers,
        state.ownerUin,
        clients[0] ? clients[0]->TransportConnID : 0,
        (unsigned)room->RoomOwnerID,
        (unsigned)room->ServerID);
    return room->ID;
}

std::vector<std::shared_ptr<ClientNode>> TryMakeReadyGroupLocked()
{
    std::vector<std::shared_ptr<ClientNode>> readyClients;
    std::set<DWORD> seenKeys;
    PruneDeadWaitingLocked();

    if (g_waitingPlayers.size() < kLuckyNeedPlayers)
    {
        const std::string snapshot = BuildWaitingSnapshotLocked();
        LmtLog("MATCH skip reason=need-more waiting=%zu/%u queue=%s",
            g_waitingPlayers.size(), (unsigned)kLuckyNeedPlayers, snapshot.c_str());
        return readyClients;
    }

    for (auto& e : g_waitingPlayers)
    {
        if (readyClients.size() >= kLuckyNeedPlayers)
            break;
        auto c = e.client.lock();
        if (!c) continue;
        const DWORD key = LuckyQueueKey(e.uin, e.connId);
        if (!seenKeys.insert(key).second)
            continue;
        readyClients.push_back(c);
    }

    if (readyClients.size() < kLuckyNeedPlayers)
    {
        const std::string snapshot = BuildWaitingSnapshotLocked();
        LmtLog("MATCH skip reason=not-enough-live ready=%zu waiting=%zu/%u queue=%s",
            readyClients.size(), g_waitingPlayers.size(), (unsigned)kLuckyNeedPlayers, snapshot.c_str());
        readyClients.clear();
        return readyClients;
    }

    const DWORD roomId = CreateLuckyRoomLocked(readyClients);
    if (roomId == 0)
    {
        const std::string snapshot = BuildWaitingSnapshotLocked();
        LmtLog("MATCH fail reason=create-room-failed ready=%zu waiting=%zu queue=%s",
            readyClients.size(), g_waitingPlayers.size(), snapshot.c_str());
        readyClients.clear();
        return readyClients;
    }

    for (auto& e : g_waitingPlayers)
    {
        for (auto& c : readyClients)
        {
            if (c && LuckyQueueKey(e.uin, e.connId) == LuckyQueueKey(c->Uin, c->TransportConnID))
            {
                e.roomId = roomId;
                e.matched = true;
                break;
            }
        }
    }

    g_waitingPlayers.erase(
        std::remove_if(g_waitingPlayers.begin(), g_waitingPlayers.end(), [&](const LuckyJoinEntry& e) {
            return e.roomId == roomId && e.matched;
        }),
        g_waitingPlayers.end());

    LmtLog("MATCH ready roomId=%u ready=%zu waitingAfterErase=%zu", roomId, readyClients.size(), g_waitingPlayers.size());
    return readyClients;
}

DWORD TodayYmdPlus(int days)
{
    std::time_t t = std::time(nullptr) + static_cast<std::time_t>(days) * 24 * 60 * 60;
    std::tm tmv{};
    localtime_s(&tmv, &t);
    return static_cast<DWORD>((tmv.tm_year + 1900) * 10000 + (tmv.tm_mon + 1) * 100 + tmv.tm_mday);
}

UINT GetExternalIp()
{
    const char* s = nullptr;
    if (IsUsableIpText(IP)) s = IP.c_str();
    else if (IsUsableIpText(BindIP)) s = BindIP.c_str();
    else s = "127.0.0.1";

    UINT ip = inet_addr(s);
    if (ip == INADDR_NONE || ip == 0)
        ip = inet_addr("127.0.0.1");
    return ip;
}

USHORT GetTcpPortBase()
{
    if (TcpPort != 0) return TcpPort;
    if (BindTcpPort != 0) return BindTcpPort;
    return 10140;
}

USHORT GetUdpPortBase()
{
    if (UdpPort != 0) return UdpPort;
    if (BindUdpPort != 0) return BindUdpPort;
    return GetTcpPortBase();
}

std::vector<UINT> BuildLuckyMapList(std::shared_ptr<ClientNode> client)
{
    std::vector<UINT> maps;
    if (client && client->MapID != 0 && GetMap(client->MapID))
        maps.push_back(client->MapID);

    const UINT defaults[] = { kLuckyDefaultMapId, 320, 381, 400, 100, 11 };
    for (UINT id : defaults)
    {
        if (GetMap(id) && std::find(maps.begin(), maps.end(), id) == maps.end())
            maps.push_back(id);
    }
    return maps;
}

void WriteFixedString(BYTE*& p, const char* text, size_t fixedLen)
{
    memset(p, 0, fixedLen);
    if (text && fixedLen > 0)
    {
        const size_t n = std::min(strlen(text), fixedLen);
        memcpy(p, text, n);
    }
    p += fixedLen;
}

void WriteBillboardRecord(BYTE*& p, BYTE type, const char* name, const char* matchType, const char* award,
    const char* areaName, BYTE roomType, BYTE createRoomType, const char* ownerName)
{
    (void)roomType;
    (void)createRoomType;
    (void)ownerName;

    BYTE* rec = p;
    Write16(rec, 0); // official 402 entry: len=0x74, payload=0x72

    Write8(rec, type);
    WriteFixedString(rec, name, 0x11);
    WriteFixedString(rec, matchType, 0x1E);
    WriteFixedString(rec, award, 0x1E);
    WriteFixedString(rec, areaName, 0x1E);
    memset(rec, 0, 6); // TailHex in official samples
    rec += 6;

    const size_t len = static_cast<size_t>(rec - p);
    Set16(p, static_cast<WORD>(len));
    p += len;
}

void WriteLmtPlayerRecord(BYTE*& p, std::shared_ptr<ClientNode> player)
{
    BYTE* rec = p;
    Write16(rec, 0);

    Write32(rec, player ? player->Uin : 0);
    WriteFixedString(rec, player ? player->NickName : "", MaxNickName);
    Write32(rec, 0);                                      // Identify
    Write8(rec, player ? static_cast<BYTE>(player->Gender) : 0);
    Write32(rec, 0);                                      // EquipedIconID
    WriteUn(rec, player ? player->Experience : 0);

    {
        BYTE* sub = rec;
        Write16(sub, 0);
        Write32(sub, 0);                                  // MatchNum
        Write8(sub, 0);                                   // BestRank
        Write32(sub, 0);                                  // ChamNum
        Write32(sub, 0);                                  // WinNum
        const size_t len = static_cast<size_t>(sub - rec);
        Set16(rec, static_cast<WORD>(len));
        rec += len;
    }

    Write16(rec, player ? player->VipFlag : 0);            // VipFlag，LMT brief 用 WORD；写 DWORD 会让无 LoverVip 记录从 0x79 偏到 0x7B

    {
        BYTE* sub = rec;
        Write16(sub, 0);
        Write32(sub, player ? player->NobleID : 0);
        Write8(sub, player ? static_cast<BYTE>(player->NobleLevel) : 0);
        Write32(sub, player ? player->NoblePoint : 0);
        Write32(sub, 0xFFFFFFFF);                         // NobleLeftDays
        const size_t len = static_cast<size_t>(sub - rec);
        Set16(rec, static_cast<WORD>(len));
        rec += len;
    }

    {
        BYTE* sub = rec;
        Write16(sub, 0);
        Write8(sub, player ? static_cast<BYTE>(player->GuildVipLevel) : 0);
        Write32(sub, player ? player->GuildVipPoint : 0);
        const size_t len = static_cast<size_t>(sub - rec);
        Set16(rec, static_cast<WORD>(len));
        rec += len;
    }

    const UINT loverVipLevel = player ? player->LoverVipLevel : 0;
    const UINT loverVipPoint = player ? player->LoverVipPoint : 0;
    const bool hasLoverVip = loverVipLevel > 0;
    Write8(rec, hasLoverVip ? 1 : 0);                     // HasLoverVip
    // LmtPlayerInfoBrief 是 recordLen 变长结构；无 LoverVip 时不能继续塞 LoverVipInfo，
    // 否则后续 Garden/Emperor/ConsumeVip/下一条玩家或房间尾部会整体错位。
    if (hasLoverVip)
    {
        BYTE* sub = rec;
        Write16(sub, 0);
        Write8(sub, static_cast<BYTE>(std::min<UINT>(loverVipLevel, 255)));
        Write32(sub, loverVipPoint);
        Write8(sub, 10);                                  // LoverVipGrowRate
        const size_t len = static_cast<size_t>(sub - rec);
        Set16(rec, static_cast<WORD>(len));
        rec += len;
    }
    Write8(rec, player ? static_cast<BYTE>(player->GardenLevel) : 0);

    {
        BYTE* sub = rec;
        Write16(sub, 0);
        Write8(sub, player ? static_cast<BYTE>(player->EmperorLevel) : 0);
        Write32(sub, player ? player->EmperorPoint : 0);
        Write32(sub, 0);
        Write8(sub, 0);
        const size_t len = static_cast<size_t>(sub - rec);
        Set16(rec, static_cast<WORD>(len));
        rec += len;
    }

    {
        BYTE* sub = rec;
        Write16(sub, 0);
        WriteUn(sub, 0);
        WriteUn(sub, 0);
        WriteUn(sub, 0);
        WriteUn(sub, 0);
        Write32(sub, 10);
        Write8(sub, 0);
        const size_t len = static_cast<size_t>(sub - rec);
        Set16(rec, static_cast<WORD>(len));
        rec += len;
    }

    Write32(rec, 0);                                      // BeLikedNums
    Write8(rec, 0);                                       // AreaID
    Write32(rec, player ? static_cast<DWORD>(player->GuildID) : 0);

    const size_t len = static_cast<size_t>(rec - p);
    Set16(p, static_cast<WORD>(len));
    p += len;
}

void SendParticipateResponse(std::shared_ptr<ClientNode> client, BYTE curPlayers, BYTE needPlayers, BYTE newPerMinute,
    USHORT resultId, const char* reason)
{
    BYTE buf[4096]{};
    BYTE* p = buf;

    // cmd 400 当前客户端按 Network 反编译的 CResponseParticipateLuckyMatchPara 固定偏移读取：
    // ResultID + Cur/Need/New + u16 NeedMapNum + i32 MapID[500] + timeout/date/openTime[5] + reason。
    // 注意这里不是 381/402 那种 u16 StructLen 对象，body 仍直接从 ResultID 开始。
    Write16(p, resultId);              // ResultID
    Write8(p, curPlayers);             // CurPlayerNum
    Write8(p, needPlayers);            // NeedPlayerNum
    Write8(p, newPerMinute);           // NewPlayerNumPerMinute
    Write16(p, 0);                     // NeedMapNum = 0，表示不要求客户端额外下载地图

    memset(p, 0, kLuckyMaxMapNum * sizeof(UINT)); // MapID[500] fixed slots
    p += kLuckyMaxMapNum * sizeof(UINT);
    Write32(p, kLuckyMatchTimeoutSec); // MatchTimeOutTime
    Write32(p, TodayYmdPlus(-1));       // OpenDate
    Write32(p, TodayYmdPlus(7));        // CloseDate

    Write16(p, 1);                     // OpenTimeInfoNums
    for (int i = 0; i < kLuckyMaxOpenTimeNum; ++i)
    {
        Write32(p, 0);                              // OpenTime
        Write32(p, i == 0 ? 24 * 60 * 60 : 0);      // CloseTime
    }

    const BYTE reasonLen = reason ? static_cast<BYTE>(std::min<size_t>(strlen(reason), 255)) : 0;
    Write8(p, reasonLen);              // ReasonLen
    if (reasonLen != 0)
    {
        memcpy(p, reason, reasonLen);
        p += reasonLen;
    }

    const DWORD len = static_cast<DWORD>(p - buf);
    char bodyHex[256]{};
    HexBytesUpper(buf, std::min<DWORD>(len, 64), bodyHex, sizeof(bodyHex));
    LmtLog("SEND 400 TransportConnID=%u Uin=%u len=%u cur=%u need=%u newPerMin=%u result=%u reasonLen=%u schema=decompiled400-fixed-noWireLen bodyHex64=%s",
        client ? client->TransportConnID : 0,
        client ? client->Uin : 0,
        len,
        (unsigned)curPlayers,
        (unsigned)needPlayers,
        (unsigned)newPerMinute,
        (unsigned)resultId,
        (unsigned)reasonLen,
        bodyHex);
    SendToClient(client, 400, buf, len, client->ConnID, FE_PLAYER, 1, Response);
}

void SendLmtEnterRoomResponse(std::shared_ptr<ClientNode> client, const LuckyRoomState& state, USHORT resultId)
{
    if (!client) return;

    BYTE buf[8192]{};
    BYTE* p = buf;
    const auto players = CollectPlayers(state);
    BYTE* topLen = BeginLen(p);

    // cmd 381 官服 wire：u16 StructLen + ResultID + ReasonLen + PlayerNum + PlayerRecord[] + room tail。
    Write16(p, resultId); // ResultID
    Write8(p, 0);         // ReasonLen
    const WORD snapshotPlayerNum = resultId == 0
        ? static_cast<WORD>(std::min<size_t>(players.size(), 0xFFFF))
        : 0;
    Write16(p, snapshotPlayerNum); // PlayerNum

    if (resultId == 0)
    {
        // 官服 381 的 PlayerNum 只对应真实玩家记录；不能用空槽补齐，
        // 否则客户端右侧参赛列表会渲染 UIN=0，后续 Top LMT 对象链也会出现空对象。
        for (WORD i = 0; i < snapshotPlayerNum; ++i)
            WriteLmtPlayerRecord(p, players[i]);

        // 官方 381 在玩家列表后还有房间概要字段；只缩报名触发人数，不缩 UI/淘汰树语义。
        Write8(p, 0);                                       // RoomType
        Write8(p, 0);                                       // CreateRoomType
        Write32(p, state.roomId);                           // RoomNum/RoomID
        Write32(p, state.ownerUin);                         // RoomOwnerUin
        Write16(p, kLuckyOfficialNeedPlayers);              // CreateRoomMaxPlayerNum
        Write16(p, kLuckyCreateRoomScoreMinNum);            // CreateRoomScoreMinNum
        Write8(p, kLuckyUiRoundNum);                        // ScoreRoundNum
        Write8(p, 1);                                       // CanBet
    }

    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - buf);
    char bodyHex[256]{};
    HexBytesUpper(buf, std::min<DWORD>(len, 64), bodyHex, sizeof(bodyHex));
    LmtLog("SEND 381 TransportConnID=%u Uin=%u len=%u result=%u roomId=%u players=%zu snapshotPlayers=%u status=%u maxPlayer=%u scoreMin=%u round=%u canBet=%u schema=wireTopLen bodyHex64=%s",
        client->TransportConnID,
        client->Uin,
        len,
        (unsigned)resultId,
        state.roomId,
        players.size(),
        (unsigned)snapshotPlayerNum,
        (unsigned)state.status,
        (unsigned)kLuckyOfficialNeedPlayers,
        (unsigned)kLuckyCreateRoomScoreMinNum,
        (unsigned)kLuckyUiRoundNum,
        1u,
        bodyHex);
    SendLuckyPlayerPacket(client, 381, buf, len, Response);
}

void SendLmtEnterNotify(std::shared_ptr<ClientNode> target, std::shared_ptr<ClientNode> entrant)
{
    if (!target || !entrant) return;

    BYTE buf[2048]{};
    BYTE* p = buf;
    BYTE* topLen = BeginLen(p);
    WriteLmtPlayerRecord(p, entrant);
    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - buf);
    LmtLog("SEND 801 TransportConnID=%u Uin=%u len=%u entrantUin=%u schema=wireTopLen", target->TransportConnID, target->Uin, len, entrant->Uin);
    SendLuckyRoomPacket(target, 801, buf, len, target->RoomID, Notify);
}

void WriteScoreInfoPayload(BYTE*& p, const LuckyRoomState& state, std::shared_ptr<ClientNode> client)
{
    BYTE* score = p;
    Write16(score, 0);

    DWORD scoreValue = 0;
    WORD rank = 0;
    WORD scoreMin = static_cast<WORD>(kLuckyOfficialNeedPlayers);
    BYTE rr1 = 0;
    BYTE rr2 = 0;
    DWORD scoreChg1 = 0;
    DWORD scoreChg2 = 0;
    DWORD matchNum = 0;
    BYTE bestRank = 0;
    DWORD chamNum = 0;
    DWORD winNum = 0;
    const auto players = CollectPlayers(state);
    const WORD actualPlayerNum = static_cast<WORD>(std::max<size_t>(players.size(), 1));
    WORD actualRank = 1;
    for (size_t i = 0; i < players.size(); ++i)
    {
        auto& p = players[i];
        if (!p || !client)
            continue;
        if ((p->TransportConnID != 0 && p->TransportConnID == client->TransportConnID) ||
            (p->Uin != 0 && p->Uin == client->Uin))
        {
            actualRank = static_cast<WORD>(std::min<size_t>(i + 1, 0xFFFF));
            break;
        }
    }

    // 官服 810/383 抓包结构：
    // topLen -> Score(u32), Rank(u16), ScoreMinNum(u16), RoundNum(u8),
    // RoundResultInfo[len=7]*2, HistoryInfo[len=15]。
    // 当前两开联调只缩参与人数，不改官服 UI/阶段字段。
    switch (state.status)
    {
    case LMT_STATUS_WAIT_ENTER:
        scoreValue = 1000;
        rank = 10;
        scoreMin = 4;
        rr1 = 3;
        rr2 = 3;
        matchNum = 0xA3;
        bestRank = 1;
        chamNum = 3;
        winNum = 0xF3;
        break;
    case LMT_STATUS_COUNTDOWN:
        scoreValue = 1700;
        rank = 12;
        scoreMin = 0x1F;
        rr1 = 0;
        rr2 = 0;
        scoreChg1 = 700;
        scoreChg2 = 0;
        matchNum = 0xA4;
        bestRank = 1;
        chamNum = 3;
        winNum = 0xF4;
        break;
    case LMT_STATUS_RACING:
    case LMT_STATUS_TOP16:
    case LMT_STATUS_ELIM_WAIT:
    case LMT_STATUS_ELIM_COUNT:
    case LMT_STATUS_RACE_WAIT:
    case LMT_STATUS_ROLL_READY:
    case LMT_STATUS_ROLLING:
        scoreValue = 3300;
        rank = 4;
        scoreMin = 0x19;
        rr1 = 0;
        rr2 = 0;
        scoreChg1 = 700;
        scoreChg2 = 1100;
        matchNum = 0xA4;
        bestRank = 1;
        chamNum = 3;
        winNum = 0xF5;
        break;
    default:
        break;
    }

    // 本地两号联调不能继续使用官服 32 人样本里的 rank/totalPlayerNum，
    // 否则 UI 会出现 4/2、25/2 这种自相矛盾状态。
    rank = actualRank;
    scoreMin = actualPlayerNum;
    Write32(score, scoreValue);
    Write16(score, rank);
    Write16(score, scoreMin);
    const BYTE scoreRoundNum = state.roundNum >= kLuckyUiRoundNum ? state.roundNum : kLuckyUiRoundNum;
    Write8(score, scoreRoundNum);

    for (BYTE i = 0; i < scoreRoundNum; ++i)
    {
        BYTE* rr = score;
        Write16(rr, 0);
        Write8(rr, i == 0 ? rr1 : rr2);
        Write32(rr, i == 0 ? scoreChg1 : scoreChg2);
        const size_t len = static_cast<size_t>(rr - score);
        Set16(score, static_cast<WORD>(len));
        score += len;
    }

    {
        BYTE* rec = score;
        Write16(rec, 0);
        Write32(rec, matchNum);
        Write8(rec, bestRank);
        Write32(rec, chamNum);
        Write32(rec, winNum);
        const size_t len = static_cast<size_t>(rec - score);
        Set16(score, static_cast<WORD>(len));
        score += len;
    }

    const size_t len = static_cast<size_t>(score - p);
    Set16(p, static_cast<WORD>(len));
    p += len;
}

void SendLmtSummaryInfo(std::shared_ptr<ClientNode> client, const LuckyRoomState& state)
{
    if (!client) return;

    BYTE buf[2048]{};
    BYTE* p = buf;
    const auto players = CollectPlayers(state);

    struct LuckySummaryRow
    {
        DWORD uin;
        WORD score;
        BYTE rank;
        BYTE status;
        WORD extraScore;
    };

    // 811 的单条结构来自官服 0x012f/0x0177 抓包：u16 len=12,
    // u32 Uin, u16 Score, u8 Rank, u8 PlayerStatus, u16 ExtraScore。
    // 官服 31/25 行是因为真实参赛人数还在；本地只有两个真实号时不能填假人/空人。

    std::vector<LuckySummaryRow> rows;
    std::set<DWORD> seenUins;
    BYTE localRank = 1;
    for (auto& player : players)
    {
        if (!player)
            continue;
        const DWORD uin = player->Uin;
        if (uin == 0 || seenUins.count(uin))
            continue;
        seenUins.insert(uin);
        rows.push_back({ uin,
            state.status >= LMT_STATUS_RACING ? static_cast<WORD>(3300) : static_cast<WORD>(1700),
            localRank++,
            state.status >= LMT_STATUS_RACING ? static_cast<BYTE>(5) : static_cast<BYTE>(4),
            0 });
    }

    BYTE* topLen = BeginLen(p);
    Write8(p, static_cast<BYTE>(std::min<size_t>(rows.size(), 255))); // PlayerNum
    for (const auto& row : rows)
    {
        BYTE* rec = p;
        Write16(rec, 0);
        Write32(rec, row.uin);
        Write16(rec, row.score);
        Write8(rec, row.rank);
        Write8(rec, row.status);
        Write16(rec, row.extraScore);
        const size_t len = static_cast<size_t>(rec - p);
        Set16(p, static_cast<WORD>(len));
        p += len;
    }

    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - buf);
    LmtLog("SEND 811 TransportConnID=%u Uin=%u len=%u roomId=%u players=%zu rows=%zu status=%u schema=wireTopLen",
        client->TransportConnID, client->Uin, len, state.roomId, players.size(), rows.size(), (unsigned)state.status);
    SendLuckyRoomPacket(client, 811, buf, len, state.roomId ? state.roomId : client->RoomID, Notify);
}

void SendLmtStatusNotify(std::shared_ptr<ClientNode> client, const LuckyRoomState& state)
{
    if (!client) return;

    BYTE buf[16]{};
    BYTE* p = buf;
    BYTE* topLen = BeginLen(p);
    Write8(p, state.status);
    Write32(p, state.leftTime);
    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - buf);
    LmtLog("SEND 803 TransportConnID=%u Uin=%u len=%u roomId=%u status=%u left=%u schema=wireTopLen",
        client->TransportConnID, client->Uin, len, state.roomId, (unsigned)state.status, state.leftTime);
    SendLuckyRoomPacket(client, 803, buf, len, state.roomId ? state.roomId : client->RoomID, Notify);
}

int LuckyHexNibble(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::vector<BYTE> LuckyHexLiteralToBytes(const char* hex)
{
    std::vector<BYTE> out;
    int high = -1;
    if (!hex)
        return out;

    for (const char* p = hex; *p; ++p)
    {
        const int n = LuckyHexNibble(*p);
        if (n < 0)
            continue;
        if (high < 0)
        {
            high = n;
            continue;
        }
        out.push_back(static_cast<BYTE>((high << 4) | n));
        high = -1;
    }
    return out;
}

void SendLuckyOfficialHexPacket(std::shared_ptr<ClientNode> client, const LuckyRoomState& state, short msgId, const char* bodyHex, short msgType, const char* tag)
{
    if (!client || !bodyHex)
        return;

    const std::vector<BYTE> body = LuckyHexLiteralToBytes(bodyHex);
    if (body.empty())
    {
        LmtLog("SKIP %d TransportConnID=%u Uin=%u roomId=%u reason=empty-official-hex tag=%s",
            (int)msgId, client->TransportConnID, client->Uin, state.roomId, tag ? tag : "");
        return;
    }

    LmtLog("SEND %d TransportConnID=%u Uin=%u len=%u roomId=%u tag=%s schema=official-capture",
        (int)msgId,
        client->TransportConnID,
        client->Uin,
        static_cast<unsigned>(body.size()),
        state.roomId ? state.roomId : client->RoomID,
        tag ? tag : "");
    SendLuckyRoomPacket(client, msgId, body.data(), static_cast<DWORD>(body.size()),
        state.roomId ? state.roomId : client->RoomID, msgType);
}

void SendLucky24602(std::shared_ptr<ClientNode> client, const LuckyRoomState& state)
{
    // 官服 cmd=24602 / NotifyClientLuckMatchBeLikedInfo，pkt=426/431/439 等反复出现，bodyLen=42。
    // packetstruct_luckmatch_246xx_chunk.txt 只还原出结构入口，字段名不完整；这里保留官服 body 形状，不改成猜测结构。
    SendLuckyOfficialHexPacket(client, state, 24602,
        "002A00000000000000020003000A3890B22900000000000A33CC623400000000000A2DA6659400000000",
        Notify,
        "official-24602-liked");
}

void SendLucky24607(std::shared_ptr<ClientNode> client, const LuckyRoomState& state)
{
    // 官服 cmd=24607 / NotifyClientLuckMatchBetInfo，pkt=507，bodyLen=530：
    // top fields + 16 * len=0x20 的候选玩家记录 + BetTimeRemain。
    SendLuckyOfficialHexPacket(client, state, 24607,
        "02123E18EEFF0000000200000CE4001000203E18EEFFB6C4C9F10000000000000000000000000000000E1014000000320020"
        "1BF912E7C1ECC5DCCEC8480000000000000000000000000E1014000000320020327811D2B0F8CDED0000000000000000000000000000000E1014000000320020"
        "7343DC02CAC9B9DAD8BC323300000000000000000000000E10140000003200203890B229536170706869726556474300000000000000000D4818000000320020"
        "2DA66594CAC9B9DAD8BCBCAACBFBCAD6C4CFB7E70000000CE41A00000032002030464799D0BBC3CC0000000000000000000000000000000B5423000000320020"
        "5E9A8E96D1BECDB7D7D3C3B400000000000000000000000AF02500000032002033CC6234C9F1CEEE0000000000000000000000000000000AF025000000320020"
        "57059C70CCFCC3C30000000000000000000000000000000A8C28000000320020227F4B74CAC9B9DA0000000000000000000000000000000A282A000000320020"
        "4C6FF65ACAC9B9DAD8BCC3DCC8A60000000000000000000A282A000000320020B1EC37F2D0FEDAA4D6AEBEBA00000000000000000000000A282A000000320020"
        "1D3B11BDCAC9B9DAD8BCB4A8D3E5CADE0000000000000009C42C00000032002062420A99CEE1C6DEDCB7D0ED000000000000000000000009602E000000320020"
        "395BEC985373757575000000000000000000000000000008FC30000000324E20",
        Notify,
        "official-24607-bet");
}

void SendLmtOfficial804(std::shared_ptr<ClientNode> client, const LuckyRoomState& state)
{
    if (!client) return;

    if (!kLuckySendOfficial804)
    {
        LmtLog("SKIP 804 TransportConnID=%u Uin=%u roomId=%u reason=official-template-disabled",
            client->TransportConnID, client->Uin, state.roomId);
        return;
    }

    // 官服 pkt=634 的 cmd 804 / NotifyLmtOpponentInfo 模板。
    // LuckyContest 14-byte msg head 不带 BodyLen 字段；body.hex 的前 2 字节 0x0389 是
    // 804 自身结构长度，必须原样发送。剥掉它会让客户端从 0x0385 开始错位解析，
    // 后续进图时 Top-Kart 的 LMT 对象链为空并在 +0xEE0 崩溃。
    static const BYTE kOfficial804Body[] = {
        0x03,0x89,0x03,0x85,0x2D,0xA6,0x65,0x94,0x00,0x00,0x00,0x00,0x00,0x00,0x2F,0x45,
        0x00,0x01,0x23,0xCA,0xC9,0xB9,0xDA,0xD8,0xBC,0xBC,0xAA,0xCB,0xFB,0xCA,0xD6,0xC4,
        0xCF,0xB7,0xE7,0x00,0x01,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x2E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x0A,0x00,0x19,0x00,0x00,0x27,0x26,0x00,0x00,0x00,0x01,0xFF,0xFF,
        0xFF,0xFF,0x01,0x69,0xFB,0x55,0xE5,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x19,0x00,
        0x00,0x27,0x29,0x00,0x00,0x00,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x69,0xFB,0x55,0xE5,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x19,0x00,0x00,0x27,0x7A,0x00,0x00,0x00,0x01,
        0xFF,0xFF,0xFF,0xFF,0x01,0x69,0xFB,0x55,0xE5,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x19,0x00,0x00,0x27,0x3B,0x00,0x00,0x00,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x69,0xFB,
        0x55,0xE5,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x19,0x00,0x00,0x27,0x3D,0x00,0x00,
        0x00,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x69,0xFB,0x55,0xE5,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x19,0x00,0x00,0x27,0x3C,0x00,0x00,0x00,0x01,0xFF,0xFF,0xFF,0xFF,0x01,
        0x69,0xFB,0x55,0xE5,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x19,0x00,0x00,0x27,0x24,
        0x00,0x00,0x00,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x69,0xFB,0x55,0xE5,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x19,0x00,0x00,0x27,0x1E,0x00,0x00,0x00,0x01,0xFF,0xFF,0xFF,
        0xFF,0x01,0x69,0xFB,0x55,0xE5,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x19,0x00,0x00,
        0x27,0x75,0x00,0x00,0x00,0x01,0xFF,0xFF,0xFF,0xFF,0x01,0x69,0xFB,0x55,0xE5,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x19,0x00,0x00,0x27,0x73,0x00,0x00,0x00,0x01,0xFF,
        0xFF,0xFF,0xFF,0x01,0x69,0xFB,0x55,0xE5,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x0E,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x0F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x17,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x35,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x0C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x12,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x0B,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x35,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    };

    static_assert(sizeof(kOfficial804Body) == 905, "official 804 template must be full captured body");

    // 804 的整体 wire layout 继续沿用官服模板，但模板里的玩家身份不能照抄官服抓包。
    // 加载页用这里的 UIN/昵称建立对手卡；如果仍是抓包里的 0x2DA66594/“噬冠丶吉他手南风”，
    // 客户端进图后会把大奖赛对手对象建成官服样本玩家，而不是当前房间两人。
    BYTE body[sizeof(kOfficial804Body)]{};
    memcpy(body, kOfficial804Body, sizeof(body));

    std::shared_ptr<ClientNode> opponent;
    const auto players = CollectPlayers(state);
    for (auto& p : players)
    {
        if (p && p->TransportConnID != client->TransportConnID && p->Uin != client->Uin)
        {
            opponent = p;
            break;
        }
    }
    if (!opponent)
    {
        for (auto& p : players)
        {
            if (p && p->TransportConnID != client->TransportConnID)
            {
                opponent = p;
                break;
            }
        }
    }
    if (!opponent)
        opponent = client;

    constexpr size_t kOfficial804UinOffset = 4;
    constexpr size_t kOfficial804NickOffset = 19;
    constexpr size_t kOfficial804GenderOffset = 36;
    Set32(body + kOfficial804UinOffset, opponent ? opponent->Uin : 0);
    memset(body + kOfficial804NickOffset, 0, MaxNickName);
    if (opponent)
    {
        size_t nickLen = 0;
        while (nickLen < MaxNickName && opponent->NickName[nickLen] != '\0')
            ++nickLen;
        if (nickLen > 0)
            memcpy(body + kOfficial804NickOffset, opponent->NickName, nickLen);
        body[kOfficial804GenderOffset] = static_cast<BYTE>(opponent->Gender);
    }

    const BYTE* data = body;
    const DWORD len = static_cast<DWORD>(sizeof(kOfficial804Body));
    LmtLog("SEND 804 TransportConnID=%u Uin=%u dataLen=%u roomId=%u template=official-patched opponentUin=%u opponentTransportConn=%u opponentNick=%s bodyPrefix=%02X%02X%02X%02X",
        client->TransportConnID,
        client->Uin,
        len,
        state.roomId,
        opponent ? opponent->Uin : 0,
        opponent ? opponent->TransportConnID : 0,
        opponent ? opponent->NickName : "",
        (unsigned)data[0],
        (unsigned)data[1],
        (unsigned)data[2],
        (unsigned)data[3]);
    SendLuckyRoomPacket(client, 804, data, len, state.roomId ? state.roomId : client->RoomID, Notify);
}

struct LuckyRankRow
{
    DWORD uin = 0;
    DWORD connId = 0;
    DWORD finishRankValue = 0xFFFFFFFE;
    std::shared_ptr<ClientNode> client;
};

DWORD LuckyFinishRankValue(std::shared_ptr<ClientNode> c)
{
    if (!c)
        return 0xFFFFFFFF;
    const DWORD ft = static_cast<DWORD>(c->FinishTime);
    if (ft == 0 || ft == 0xFFFFFFFF)
        return 0xFFFFFFFE; // 未完成排在真实成绩后、空位前
    return ft;
}

std::vector<LuckyRankRow> BuildLuckyRankRows(std::shared_ptr<RoomNode> room, const LuckyRoomState& state)
{
    std::vector<LuckyRankRow> rows;
    if (room)
    {
        const size_t limit = std::min<size_t>(room->Player.size(), kLuckyNeedPlayers);
        for (size_t i = 0; i < limit; ++i)
        {
            auto c = room->Player[i];
            if (!c || c->Uin == 0)
                continue;
            LuckyRankRow row;
            row.uin = c->Uin;
            row.connId = c->TransportConnID;
            row.finishRankValue = LuckyFinishRankValue(c);
            row.client = c;
            rows.push_back(row);
        }
    }

    if (rows.empty())
    {
        for (auto& c : CollectPlayers(state))
        {
            if (!c || c->Uin == 0)
                continue;
            LuckyRankRow row;
            row.uin = c->Uin;
            row.connId = c->TransportConnID;
            row.finishRankValue = LuckyFinishRankValue(c);
            row.client = c;
            rows.push_back(row);
        }
    }

    std::sort(rows.begin(), rows.end(), [](const LuckyRankRow& a, const LuckyRankRow& b) {
        if (a.finishRankValue != b.finishRankValue)
            return a.finishRankValue < b.finishRankValue;
        return a.uin < b.uin;
    });
    return rows;
}

WORD LuckyRankForUin(const std::vector<LuckyRankRow>& rows, DWORD uin)
{
    if (uin == 0)
        return 0;
    for (size_t i = 0; i < rows.size(); ++i)
    {
        if (rows[i].uin == uin)
            return static_cast<WORD>(i + 1);
    }
    return 0;
}

DWORD LuckyWinnerFromRows(const std::vector<LuckyRankRow>& rows)
{
    return rows.empty() ? 0 : rows.front().uin;
}

void SendLmtMatchOverNotify(std::shared_ptr<ClientNode> client, const LuckyRoomState& state, WORD rank, DWORD winnerUin)
{
    if (!client)
        return;

    const bool isWinner = (rank == 1 && client->Uin == winnerUin);
    BYTE buf[128]{};
    BYTE* p = buf;
    BYTE* topLen = BeginLen(p);

    // CNotifyLmtMatchOverPara，来自抓包反编译结构：
    // reason + rank + awardItemNum + fixed award[5] + luckMoney + luckPoint + winNum + room/createType + superMoney + itemNum.
    Write8(p, kLuckyMatchOverReasonNormal);            // OverReason: normal finish；0 是客户端“网络异常(0)”
    Write16(p, rank == 0 ? 1 : rank);                  // Rank
    Write8(p, 0);                                      // AwardItemNum
    for (int i = 0; i < 5; ++i)
        Write32(p, 0);                                 // AwardItem[5]
    Write32(p, isWinner ? kLuckyWinnerLuckMoney : 0);  // AwardLuckMoney
    Write32(p, isWinner ? kLuckyWinnerMatchPoint : 0); // AwardLuckyMatchPoint
    Write16(p, isWinner ? 1 : 0);                      // WinNum
    Write8(p, 0);                                      // RoomType
    Write8(p, 1);                                      // CreateRoomType
    Write32(p, 0);                                     // AwardSuperMoney
    Write8(p, 0);                                      // ItemNum

    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - buf);
    char bodyHex[256]{};
    HexBytesUpper(buf, std::min<DWORD>(len, 64), bodyHex, sizeof(bodyHex));
    LmtLog("SEND 805 TransportConnID=%u Uin=%u len=%u roomId=%u overReason=%u rank=%u winner=%u luckMoney=%u luckPoint=%u schema=wireTopLen bodyHex64=%s",
        client->TransportConnID,
        client->Uin,
        len,
        state.roomId,
        (unsigned)kLuckyMatchOverReasonNormal,
        (unsigned)(rank == 0 ? 1 : rank),
        winnerUin,
        isWinner ? (unsigned)kLuckyWinnerLuckMoney : 0u,
        isWinner ? (unsigned)kLuckyWinnerMatchPoint : 0u,
        bodyHex);
    SendLuckyRoomPacket(client, 805, buf, len, state.roomId ? state.roomId : client->RoomID, Notify);
}

void BroadcastLuckyInit(DWORD roomId)
{
    LuckyRoomState state;
    std::vector<std::shared_ptr<ClientNode>> players;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return;
        state = it->second;
        players = CollectPlayers(state);
    }

    for (auto& c : players)
    {
        NotifyLmtGetScoreInfo(c);
        SendLmtSummaryInfo(c, state);
        SendLmtStatusNotify(c, state);
    }
}

void AdvanceLuckyMatchToRacingStatus(DWORD roomId)
{
    bool shouldBroadcast = false;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return;

        LuckyRoomState& state = it->second;
        if (state.status != LMT_STATUS_COUNTDOWN || state.startQueued || state.raceBeginSent)
            return;

        state.status = LMT_STATUS_RACING;
        state.leftTime = kLuckyRacingStatusLeftSec;
        shouldBroadcast = true;
    }

    if (shouldBroadcast)
    {
        LmtLog("COUNTDOWN_ADVANCE roomId=%u status=5 left=%u official=382-status5-wait-client120",
            roomId,
            kLuckyRacingStatusLeftSec);
        BroadcastLuckyInit(roomId);
        ScheduleLuckyMatchOfficialMiddle(roomId);
    }
}

void ScheduleLuckyMatchCountdownAdvance(DWORD roomId)
{
    if (roomId == 0)
        return;

    std::thread([roomId]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(kLuckyCountdownToRacingDelayMs));
        AdvanceLuckyMatchToRacingStatus(roomId);
    }).detach();
}

bool PrepareLuckyRaceRoom(std::shared_ptr<RoomNode> room, LuckyRoomState& state)
{
	if (!room)
		return false;

	const UINT mapId = state.mapId;
	if (mapId == 0 || !GetMap(mapId))
		return false;

    room->MapID = mapId;
    room->RaceStartPlayerNum = 0;
    room->RaceActiveMask = 0;
    room->Israce = 1;
    room->startStatus = 1;
    if (!BeginRoomGameLogicSession(room, "lucky-start"))
        return false;

    Map* pMap = GetMap(mapId);
    for (BYTE i = 0; i < room->Player.size(); ++i)
    {
        auto c = room->Player[i];
        if (!c) continue;

        ++room->RaceStartPlayerNum;
        room->RaceActiveMask |= static_cast<BYTE>(1 << i);

        ResetNormalRaceWireState(c);
        c->GameID = room->RaceGameID;
        c->IsReady = FALSE;
        c->IsFinish = FALSE;
        c->IsRefitItem = false;
        c->RoomID = static_cast<USHORT>(room->ID);
        c->CurrentRoom = room.get();
        c->BorderRoomID = 0;
        c->SeatID = static_cast<char>(i);
        c->ReadyState = 0;

        if (pMap)
        {
            c->MapCompatibilityMode = false;
            c->EndCheckPoint = pMap->End;
            c->TotalRound = pMap->Round;
            c->Loop = pMap->Loop;
        }
        else
        {
            c->MapCompatibilityMode = true;
            c->EndCheckPoint = 0;
            c->TotalRound = 1;
            c->Loop = false;
        }

        c->MapID = mapId;
        c->FinishTime = 0;
        c->Round = 0;
        c->PassedCheckPoint = 0;
        c->MsgSequence = -1;
        c->CustomRaceStageIndex = 0;
	}
	return CaptureRoomRaceStartSnapshot(room, "lucky-start");
}

void SendLuckyMatchRaceBegin(std::shared_ptr<RoomNode> room)
{
    if (!room) return;

    std::vector<std::shared_ptr<ClientNode>> players;
    size_t readyNum = 0;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(room->ID);
        if (it == g_luckyRooms.end())
            return;

        players = CollectLiveRoomPlayersLocked(room, room->ID);
        readyNum = it->second.prepareReadyConnIds.size();
        if (players.size() < kLuckyNeedPlayers)
        {
            it->second.status = LMT_STATUS_COUNTDOWN;
            it->second.leftTime = 3;
            LmtLog("BEGIN blocked roomId=%u active=%zu/%u ready=%zu reason=need-two-live-before-551",
                room->ID,
                players.size(),
                (unsigned)kLuckyNeedPlayers,
                it->second.prepareReadyConnIds.size());
            return;
        }
        if (it->second.raceBeginSent)
        {
            LmtLog("BEGIN skip roomId=%u active=%zu ready=%zu reason=already-sent-551",
                room->ID,
                players.size(),
                readyNum);
            return;
        }
        it->second.status = LMT_STATUS_RACING;
        it->second.leftTime = 180;
        it->second.raceBeginSent = true;
        it->second.players.clear();
        for (auto& c : players)
            it->second.players.push_back(c);
    }

    for (auto& c : players)
    {
        if (!c) continue;
        c->IsReady = TRUE;
        LmtLog("SEND 551 TransportConnID=%u Uin=%u roomId=%u", c->TransportConnID, c->Uin, room->ID);
        NotifyKartPhysParam(c, room->Player);              // 551
    }

    room->dwStart = GetTickCount();
    for (auto& c : players)
    {
        if (!c) continue;
        LmtLog("SEND 511 TransportConnID=%u Uin=%u roomId=%u", c->TransportConnID, c->Uin, room->ID);
        NotifyRaceBegin(c);                                // 511
    }
}

void StartLuckyMatchRace(DWORD roomId, const char* trigger = "client120");

void BeginLuckyMatchCountdown(DWORD roomId)
{
    bool shouldBroadcast = false;
    std::shared_ptr<RoomNode> room = GetRoom(roomId);
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end() || it->second.countdownNotified || it->second.startQueued)
            return;
        it->second.status = LMT_STATUS_COUNTDOWN;
        it->second.leftTime = kLuckyEnterRoomCountdownSec;
        it->second.countdownNotified = true;
        shouldBroadcast = true;
    }

    if (shouldBroadcast)
    {
        LmtLog("COUNTDOWN roomId=%u status=2 seconds=%u waitClient120=1 official=382-status2",
            roomId,
            kLuckyEnterRoomCountdownSec);
        BroadcastLuckyInit(roomId);
        ScheduleLuckyMatchCountdownAdvance(roomId);
    }
}

void StartLuckyMatchRace(DWORD roomId, const char* trigger)
{
    std::shared_ptr<RoomNode> room = GetRoom(roomId);
    if (!room)
        return;

    std::vector<std::shared_ptr<ClientNode>> players;
    size_t readyNum = 0;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end() || it->second.startQueued)
            return;

        players = CollectLiveRoomPlayersLocked(room, roomId);
        readyNum = it->second.prepareReadyConnIds.size();
        if (players.size() < kLuckyNeedPlayers)
        {
            it->second.status = LMT_STATUS_COUNTDOWN;
            it->second.leftTime = kLuckyEnterRoomCountdownSec;
            it->second.countdownNotified = true;
            LmtLog("START blocked roomId=%u active=%zu/%u ready=%zu entered=%zu reason=need-two-live-before-509",
                roomId,
                players.size(),
                (unsigned)kLuckyNeedPlayers,
                readyNum,
                it->second.enteredConnIds.size());
            return;
        }
		if (!PrepareLuckyRaceRoom(room, it->second))
		{
			LmtLog("START rejected roomId=%u reason=race-snapshot-invalid", roomId);
			return;
		}
		const bool officialAfter804 = trigger && strcmp(trigger, "official-after-804") == 0;
		it->second.status = officialAfter804 ? LMT_STATUS_RACE_WAIT : LMT_STATUS_COUNTDOWN;
        it->second.leftTime = officialAfter804 ? kLuckyRaceWaitLeftSec : 3;
        it->second.countdownNotified = true;
        it->second.startQueued = true;
        it->second.players.clear();
        for (auto& c : players)
            it->second.players.push_back(c);
	}

    for (auto& c : players)
    {
        if (!c) continue;
        LmtLog("SEND 509 TransportConnID=%u Uin=%u roomId=%u map=%u players=%zu ready=%zu trigger=%s",
            c->TransportConnID, c->Uin, roomId, room->MapID, players.size(), readyNum, trigger ? trigger : "unknown");
        NotifyGameBegin(c, room->Player);                  // 509
    }

    // 官服首局抓包：28381 -> 804 -> 509 -> 551 -> 511；120 在进入图后才出现。
    // 不能把 551/511 强行后移到 120 之后，否则链路和官服不一致。
    SendLuckyMatchRaceBegin(room);
}


bool HandleLuckyMatchPrepareReady(std::shared_ptr<ClientNode> client)
{
    if (!client) return false;

    DWORD roomId = 0;
    size_t readyNum = 0;
    size_t playerNum = 0;
    bool shouldSend509 = false;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        roomId = FindLuckyRoomIdLocked(client);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return false;

        it->second.prepareReadyConnIds.insert(client->TransportConnID);
        readyNum = it->second.prepareReadyConnIds.size();
        playerNum = CollectLiveRoomPlayersLocked(GetRoom(roomId), roomId).size();
        const bool allEntered = playerNum > 0 && it->second.enteredConnIds.size() >= playerNum;
        if (playerNum >= kLuckyNeedPlayers && allEntered && readyNum >= playerNum)
        {
            shouldSend509 = !it->second.startQueued;
        }
    }

    LmtLog("RECV 120 TransportConnID=%u Uin=%u roomId=%u ready=%zu/%zu send509=%u official551By509=1",
        client->TransportConnID, client->Uin, roomId, readyNum, playerNum, shouldSend509 ? 1 : 0);

    if (shouldSend509)
    {
        StartLuckyMatchRace(roomId, "client120");
    }
    return true;
}

bool HandleLuckyMatchSwitchDisconnect(std::shared_ptr<ClientNode> client)
{
    if (!client)
        return false;

    std::lock_guard<std::mutex> lock(g_luckyMutex);
    const DWORD roomId = FindLuckyRoomIdLocked(client);
    auto it = g_luckyRooms.find(roomId);
    if (it == g_luckyRooms.end())
        return false;

    LuckyRoomState& state = it->second;
    const bool pendingSwitch =
        state.switchPendingConnIds.find(client->TransportConnID) != state.switchPendingConnIds.end() ||
        (client->Uin != 0 && state.switchPendingUins.find(client->Uin) != state.switchPendingUins.end());

    const bool protectPreRace =
        state.status == LMT_STATUS_WAIT_ENTER ||
        state.status == LMT_STATUS_COUNTDOWN ||
        (state.status == LMT_STATUS_RACING && !state.raceBeginSent) ||
        (state.startQueued && !state.raceBeginSent);

    // 815 后客户端会断旧连接；进入 LMT 等待/倒计时/加载期后，也不能让系统 OnClose 直走普通 LeaveRoom。
    // 真正 511 后的比赛断线仍交给原离房链。
    if ((!pendingSwitch && !protectPreRace) || state.raceBeginSent)
        return false;

	g_connRoomId.erase(client->TransportConnID);
	state.enteredConnIds.erase(client->TransportConnID);
	state.prepareReadyConnIds.erase(client->TransportConnID);
	if (client->Uin != 0)
		g_uinRoomId[client->Uin] = roomId; // 保留 Uin->RoomID，给新 ConnID 回挂用。
	state.players.erase(
		std::remove_if(state.players.begin(), state.players.end(), [&](const std::weak_ptr<ClientNode>& w) {
			auto p = w.lock();
			if (!p)
				return true;
			if (p == client)
				return true;
			if (client->TransportConnID != 0 && p->TransportConnID == client->TransportConnID)
				return true;
			return client->Uin != 0 && p->Uin == client->Uin;
		}),
		state.players.end());
	if (auto room = GetRoom(roomId))
	{
		// 只把旧连接从 LuckyMatch 的发送快照里移除，Room->Player 座位暂留作装备/座位快照；
		// 新连接 AttachLuckyMatchReconnect 后会替换该座位并刷新 RoomOwnerID 为新的协议 ConnID。
		if (room->RoomOwnerUin == client->Uin && room->RoomOwnerID == 0)
			room->RoomOwnerID = GetClientWirePlayerID16(client);
	}

	LmtLog("%s disconnect keep room TransportConnID=%u Uin=%u roomId=%u status=%u players=%zu entered=%zu ready=%zu pendingUin=%zu pendingTransportConn=%zu",
		pendingSwitch ? "SWITCH" : "LMT",
		client->TransportConnID,
		client->Uin,
		roomId,
		(unsigned)state.status,
		state.players.size(),
		state.enteredConnIds.size(),
		state.prepareReadyConnIds.size(),
		state.switchPendingUins.size(),
        state.switchPendingConnIds.size());
    return true;
}

bool AttachLuckyMatchReconnect(std::shared_ptr<ClientNode> client, const char* reason)
{
    if (!client || client->Uin == 0)
        return false;

    std::lock_guard<std::mutex> lock(g_luckyMutex);

    DWORD roomId = 0;
    auto uit = g_uinRoomId.find(client->Uin);
    if (uit != g_uinRoomId.end() && uit->second != 0)
        roomId = uit->second;
    if (roomId == 0)
    {
        auto cit = g_connRoomId.find(client->TransportConnID);
        if (cit != g_connRoomId.end() && cit->second != 0)
            roomId = cit->second;
    }
    // 不再仅凭 Client->RoomID/请求里的 RoomID 把任意连接挂进大奖赛房间。
    // 815 切服后的合法重连一定会保留 Uin->RoomID 或 ConnID->RoomID 映射。
    if (roomId == 0)
        return false;

    auto it = g_luckyRooms.find(roomId);
    if (it == g_luckyRooms.end())
        return false;

    auto room = GetRoom(roomId);
    if (!room)
        return false;

    LuckyRoomState& state = it->second;
    if (!IsLuckyParticipantLocked(state, client, client->Uin))
        return false;

    int seat = -1;
    DWORD oldConnId = 0;
    const size_t seatLimit = std::min<size_t>(room->Player.size(), kLuckyNeedPlayers);

    for (size_t i = 0; i < seatLimit; ++i)
    {
        auto& p = room->Player[i];
        if (p && p->TransportConnID == client->TransportConnID)
        {
            seat = static_cast<int>(i);
            oldConnId = p->TransportConnID;
            break;
        }
    }

    // 官服语义：同 Uin 新连接会顶掉旧连接；协议房间座位按 Uin/座位回挂到新 ConnID。
    // 815 重连时优先把新连接绑定到仍处于 switchPending 的旧连接座位。
    for (size_t i = 0; seat < 0 && i < seatLimit; ++i)
    {
        auto& p = room->Player[i];
        if (p && p->Uin == client->Uin &&
            state.switchPendingConnIds.find(p->TransportConnID) != state.switchPendingConnIds.end())
        {
            seat = static_cast<int>(i);
            oldConnId = p->TransportConnID;
            break;
        }
    }

    for (size_t i = 0; seat < 0 && i < seatLimit; ++i)
    {
        auto& p = room->Player[i];
        if (p && p->Uin == client->Uin)
        {
            seat = static_cast<int>(i);
            oldConnId = p->TransportConnID;
            break;
        }
    }

    if (seat < 0 && client->SeatID >= 0 && static_cast<size_t>(client->SeatID) < seatLimit)
        seat = static_cast<int>(client->SeatID);

    if (seat < 0)
    {
        for (size_t i = 0; i < seatLimit; ++i)
        {
            if (!room->Player[i])
            {
                seat = static_cast<int>(i);
                break;
            }
        }
    }

    if (seat < 0 && seatLimit > 0)
        seat = 0;
    if (seat < 0)
        return false;

    auto oldSeatClient = room->Player[seat];
    if (oldSeatClient && oldSeatClient.get() != client.get())
    {
        const UINT beforeKart = client->KartID;
        const UINT beforeSkin = client->KartSkinID;
        if (client->KartID == 0 && oldSeatClient->KartID != 0)
            client->KartID = oldSeatClient->KartID;
        if (client->KartSkinID == 0 && oldSeatClient->KartSkinID != 0)
            client->KartSkinID = oldSeatClient->KartSkinID;
        if (oldSeatClient->ClientKartRefitItemInfo.KartId != 0)
            client->ClientKartRefitItemInfo = oldSeatClient->ClientKartRefitItemInfo;
        if (client->OrgKartId == 0 && oldSeatClient->OrgKartId != 0)
            client->OrgKartId = oldSeatClient->OrgKartId;
        if (client->Pet && oldSeatClient->Pet && client->Pet->ID == 0 && oldSeatClient->Pet->ID != 0)
        {
            client->Pet = oldSeatClient->Pet;
            client->PetID = oldSeatClient->PetID;
        }
        LmtLog("RECONNECT preserve-equip reason=%s TransportConnID=%u Uin=%u roomId=%u seat=%d oldTransportConn=%u beforeKart=%u beforeSkin=%u oldKart=%u oldSkin=%u newKart=%u newSkin=%u",
            reason ? reason : "unknown",
            client->TransportConnID,
            client->Uin,
            roomId,
            seat,
            oldConnId,
            beforeKart,
            beforeSkin,
            oldSeatClient->KartID,
            oldSeatClient->KartSkinID,
            client->KartID,
            client->KartSkinID);
    }

    room->Player[seat] = client;
    room->SeatState[seat] = 0;
    room->SeatNum = kLuckyNeedPlayers;
    room->MapID = state.mapId;
    if (state.ownerUin == client->Uin)
    {
        room->RoomOwnerUin = state.ownerUin;
        room->RoomOwnerID = GetRoomConnPlayerID(room, client);
    }

    BYTE activeMask = 0;
    BYTE playerCount = 0;
    for (size_t i = 0; i < seatLimit; ++i)
    {
        if (room->Player[i])
        {
            room->SeatState[i] = 0;
            activeMask |= static_cast<BYTE>(1 << i);
            ++playerCount;
        }
        else
        {
            room->SeatState[i] = 1;
        }
    }
    room->CurrentPlayerNum = playerCount;
    room->RaceStartPlayerNum = playerCount;
    room->RaceActiveMask = activeMask;

    client->RoomID = static_cast<USHORT>(roomId);
    client->CurrentRoom = room.get();
    client->BorderRoomID = 0;
    client->ChatRoomID = -1;
    client->SeatID = static_cast<char>(seat);
    client->ReadyState = 0;
    client->IsReady = FALSE;
    client->MapID = state.mapId;
    client->BaseGameMode = room->BaseGameMode;
    client->SubGameMode = room->SubGameMode;

    if (oldConnId != 0 && oldConnId != client->TransportConnID)
    {
        g_connRoomId.erase(oldConnId);
        state.enteredConnIds.erase(oldConnId);
        state.prepareReadyConnIds.erase(oldConnId);
        state.switchPendingConnIds.erase(oldConnId);
    }

    g_uinRoomId[client->Uin] = roomId;
    g_connRoomId[client->TransportConnID] = roomId;
    state.switchPendingUins.erase(client->Uin);
    state.switchPendingConnIds.erase(client->TransportConnID);

    // 用 RoomNode 的当前座位重建 LuckyRoomState.players，确保后续 381/120/509 发给新 ConnID。
    state.players.clear();
    for (size_t i = 0; i < seatLimit; ++i)
    {
        if (room->Player[i])
            state.players.push_back(room->Player[i]);
    }

    LmtLog("RECONNECT attach reason=%s TransportConnID=%u Uin=%u roomId=%u seat=%d oldTransportConn=%u players=%zu activeMask=0x%02X",
        reason ? reason : "unknown",
        client->TransportConnID,
        client->Uin,
        roomId,
        seat,
        oldConnId,
        state.players.size(),
        (unsigned)room->RaceActiveMask);
    return true;
}

bool HandleLuckyMatchEnterAlias(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
    (void)body;
    if (!client)
        return false;

    const bool attached = AttachLuckyMatchReconnect(client, "439");
    if (!attached)
        return false;

    DWORD roomId = 0;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        roomId = FindLuckyRoomIdLocked(client);
    }

    LmtLog("RECV 439 alias TransportConnID=%u Uin=%u bodyLen=%zu roomId=%u -> attach-only synth381=%u",
        client->TransportConnID,
        client->Uin,
        bodyLen,
        roomId,
        kLuckySynthesize381From439 ? 1u : 0u);

    if (kLuckySynthesize381From439)
        ResponseLmtEnterChatRoom(client);

    return true;
}

bool HandleLuckyMatchLeaveRoom(std::shared_ptr<ClientNode> client, short leaveType)
{
    if (!client)
        return false;

    std::lock_guard<std::mutex> lock(g_luckyMutex);
    const DWORD roomId = FindLuckyRoomIdLocked(client);
    auto it = g_luckyRooms.find(roomId);
    if (it == g_luckyRooms.end())
        return false;

    const bool isAutoLeave = (leaveType == 0);
    const bool protectPreRace =
        it->second.status == LMT_STATUS_WAIT_ENTER ||
        it->second.status == LMT_STATUS_COUNTDOWN ||
        (it->second.status == LMT_STATUS_RACING && !it->second.raceBeginSent) ||
        (it->second.startQueued && !it->second.raceBeginSent);

    if (!isAutoLeave || !protectPreRace)
        return false;

    // 815 切服/381 入房/等待 120 阶段的连接抖动不能被普通 105 拆房。
    // 511 后进入比赛阶段不再吞 105，退赛/断线交给正常比赛链路处理。
    LmtLog("SKIP 105 leave-room TransportConnID=%u Uin=%u roomId=%u status=%u left=%u startQueued=%u raceBegin=%u reason=lmt-pre-race-protect",
        client->TransportConnID,
        client->Uin,
        roomId,
        (unsigned)it->second.status,
        it->second.leftTime,
        it->second.startQueued ? 1 : 0,
        it->second.raceBeginSent ? 1 : 0);
    return true;
}

bool HandleLuckyMatchRaceOver(std::shared_ptr<ClientNode> client, std::shared_ptr<RoomNode> room, bool first)
{
    if (!client || !room)
        return false;

    BYTE currentRound = 0;
    BYTE nextRound = 0;
    bool scheduleNextRound = false;
    size_t finishedNum = 0;
    size_t liveNum = 0;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(room->ID);
        if (it == g_luckyRooms.end())
            return false;

        LuckyRoomState& st = it->second;
        currentRound = st.elimRound <= 3 ? st.elimRound : 3;
        if (currentRound > 0)
        {
            if (client->Uin != 0)
                st.raceOverUins.insert(client->Uin);
            finishedNum = st.raceOverUins.size();
            liveNum = CollectLiveRoomPlayersLocked(room, room->ID).size();
            const size_t needFinish = std::max<size_t>(1, std::min<size_t>(liveNum ? liveNum : kLuckyNeedPlayers, kLuckyNeedPlayers));

            if (!st.roundAdvanceQueued && (first || finishedNum >= needFinish))
            {
                nextRound = static_cast<BYTE>(currentRound - 1);
                st.roundAdvanceQueued = true;
                scheduleNextRound = true;
            }
        }
    }

    if (currentRound > 0)
    {
        LmtLog("RACEOVER round-progress TransportConnID=%u Uin=%u roomId=%u first=%u currentRound=%u nextRound=%u finished=%zu/%zu schedule=%u",
            client->TransportConnID,
            client->Uin,
            room->ID,
            first ? 1u : 0u,
            (unsigned)currentRound,
            (unsigned)nextRound,
            finishedNum,
            liveNum,
            scheduleNextRound ? 1u : 0u);
        if (scheduleNextRound)
            ScheduleLuckyMatchNextElimRound(room->ID, nextRound);
        return true;
    }

    LuckyRoomState state;
    WORD rank = 1;
    DWORD winnerUin = 0;
    bool alreadySent = false;

    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(room->ID);
        if (it == g_luckyRooms.end())
            return false;

        auto rows = BuildLuckyRankRows(room, it->second);
        winnerUin = it->second.winnerUin ? it->second.winnerUin : LuckyWinnerFromRows(rows);
        if (winnerUin == 0)
            winnerUin = client->Uin;

        it->second.matchOverStarted = true;
        it->second.winnerUin = winnerUin;
        it->second.leftTime = 0;

        rank = LuckyRankForUin(rows, client->Uin);
        if (rank == 0)
            rank = static_cast<WORD>(client->Uin == winnerUin ? 1 : 2);

        alreadySent = it->second.matchOverNotifiedUins.find(client->Uin) != it->second.matchOverNotifiedUins.end();
        if (!alreadySent)
            it->second.matchOverNotifiedUins.insert(client->Uin);

        if (winnerUin != 0 && g_lastChampionUin != winnerUin)
        {
            g_lastChampionUin = winnerUin;
            g_lastChampionName.clear();
            for (const auto& row : rows)
            {
                if (row.uin == winnerUin && row.client)
                {
                    g_lastChampionName = row.client->NickName;
                    break;
                }
            }
            if (g_lastChampionName.empty() && client->Uin == winnerUin)
                g_lastChampionName = client->NickName;
        }

        state = it->second;
    }

    LmtLog("RACEOVER captured TransportConnID=%u Uin=%u roomId=%u first=%u rank=%u winner=%u already805=%u",
        client->TransportConnID,
        client->Uin,
        room->ID,
        first ? 1 : 0,
        (unsigned)rank,
        winnerUin,
        alreadySent ? 1 : 0);

    if (!alreadySent)
        SendLmtMatchOverNotify(client, state, rank, winnerUin);
    return true;
}

bool HandleLuckyMatchGameOver(std::shared_ptr<ClientNode> client, USHORT leaveGameType)
{
    if (!client)
        return false;

    std::lock_guard<std::mutex> lock(g_luckyMutex);
    const DWORD roomId = FindLuckyRoomIdLocked(client);
    auto it = g_luckyRooms.find(roomId);
    if (it == g_luckyRooms.end())
        return false;

    LuckyRoomState& state = it->second;
    if (!state.matchOverStarted && state.matchOverNotifiedUins.find(client->Uin) == state.matchOverNotifiedUins.end())
        return false;

    // 513/RaceOver 后大奖赛应该由 805 收尾；普通房间 514 会把客户端拉回普通房间/大厅语义，
    // 在 LMT 房间里容易触发后续 501/500/504/105 误链，表现为闪退或仍停大奖赛界面报错。
    LmtLog("SKIP 514 game-over TransportConnID=%u Uin=%u roomId=%u leaveType=%u reason=lmt-match-over",
        client->TransportConnID,
        client->Uin,
        roomId,
        (unsigned)leaveGameType);
    return true;
}

bool HandleLuckyMatchLeaveGame(std::shared_ptr<ClientNode> client, USHORT leaveGameType)
{
    if (!client)
        return false;

    DWORD roomId = 0;
    bool canHandle = false;
    bool roomEmpty = false;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        roomId = FindLuckyRoomIdLocked(client);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return false;

        LuckyRoomState& state = it->second;
        canHandle = state.matchOverStarted ||
            state.matchOverNotifiedUins.find(client->Uin) != state.matchOverNotifiedUins.end();
        if (!canHandle)
            return false;

        const DWORD uin = client->Uin;
        const DWORD connId = client->TransportConnID;
        g_connRoomId.erase(connId);
        if (uin != 0)
        {
            auto uit = g_uinRoomId.find(uin);
            if (uit != g_uinRoomId.end() && uit->second == roomId)
                g_uinRoomId.erase(uit);
        }
        state.enteredConnIds.erase(connId);
        state.enteredUins.erase(uin);
        state.prepareReadyConnIds.erase(connId);

        if (auto room = GetRoom(roomId))
        {
            for (auto& p : room->Player)
            {
                if (p && (p->TransportConnID == connId || (uin != 0 && p->Uin == uin)))
                    p.reset();
            }

            BYTE activeCount = 0;
            for (auto& p : room->Player)
                if (p)
                    ++activeCount;
            room->CurrentPlayerNum = activeCount;
            roomEmpty = (activeCount == 0);
        }

        ResetLuckyClientForLobby(client);
        if (roomEmpty)
        {
            g_luckyRooms.erase(it);
            RecycleRoomID(static_cast<int>(roomId));
        }
    }

    LmtLog("SKIP 424 leave-game TransportConnID=%u Uin=%u roomId=%u leaveType=%u roomEmpty=%u reason=lmt-match-over-return-hall",
        client->TransportConnID,
        client->Uin,
        roomId,
        (unsigned)leaveGameType,
        roomEmpty ? 1 : 0);
    return true;
}

} // namespace

bool TryHandleLuckyMatchPrepareReady(std::shared_ptr<ClientNode> Client)
{
    return HandleLuckyMatchPrepareReady(Client);
}

bool TryHandleLuckyMatchSwitchDisconnect(std::shared_ptr<ClientNode> Client)
{
    return HandleLuckyMatchSwitchDisconnect(Client);
}

bool TryAttachLuckyMatchReconnect(std::shared_ptr<ClientNode> Client)
{
    return AttachLuckyMatchReconnect(Client, "external");
}

bool TryIsLuckyMatchLoginPending(std::shared_ptr<ClientNode> Client)
{
    if (!Client || Client->Uin == 0)
        return false;

    std::lock_guard<std::mutex> lock(g_luckyMutex);
    const DWORD roomId = FindLuckyRoomIdLocked(Client);
    auto it = g_luckyRooms.find(roomId);
    if (it == g_luckyRooms.end())
        return false;

    const LuckyRoomState& state = it->second;
    const bool preRace =
        state.status == LMT_STATUS_WAIT_ENTER ||
        state.status == LMT_STATUS_COUNTDOWN ||
        (state.status == LMT_STATUS_RACING && !state.raceBeginSent) ||
        (state.startQueued && !state.raceBeginSent);
    return preRace && IsLuckyParticipantLocked(state, Client, Client->Uin);
}

bool TryHandleLuckyMatchLoginFastPath(std::shared_ptr<ClientNode> Client)
{
    if (!Client || Client->Uin == 0)
        return false;

    DWORD roomId = 0;
    BYTE status = 0;
    size_t pendingUin = 0;
    size_t pendingConn = 0;
    bool canFastPath = false;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        roomId = FindLuckyRoomIdLocked(Client);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return false;

        const LuckyRoomState& state = it->second;
        const bool preRace =
            state.status == LMT_STATUS_WAIT_ENTER ||
            state.status == LMT_STATUS_COUNTDOWN ||
            (state.status == LMT_STATUS_RACING && !state.raceBeginSent) ||
            (state.startQueued && !state.raceBeginSent);
        canFastPath = preRace && IsLuckyParticipantLocked(state, Client, Client->Uin);
        status = state.status;
        pendingUin = state.switchPendingUins.size();
        pendingConn = state.switchPendingConnIds.size();
    }

    if (!canFastPath)
        return false;

    if (!AttachLuckyMatchReconnect(Client, "login-fast-path"))
        return false;

    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        roomId = FindLuckyRoomIdLocked(Client);
        auto it = g_luckyRooms.find(roomId);
        if (it != g_luckyRooms.end())
        {
            status = it->second.status;
            pendingUin = it->second.switchPendingUins.size();
            pendingConn = it->second.switchPendingConnIds.size();
        }
    }

    LmtLog("LMT login fast-path TransportConnID=%u Uin=%u roomId=%u status=%u pendingUin=%zu pendingTransportConn=%zu skipHall=1",
        Client->TransportConnID,
        Client->Uin,
        roomId,
        (unsigned)status,
        pendingUin,
        pendingConn);
    ResponseLmtEnterChatRoom(Client);
    return true;
}

bool TryHandleLuckyMatchEnterAlias(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
    return HandleLuckyMatchEnterAlias(Client, Body, BodyLen);
}

bool TryHandleLuckyMatchLeaveRoom(std::shared_ptr<ClientNode> Client, short LeaveType)
{
    return HandleLuckyMatchLeaveRoom(Client, LeaveType);
}

bool TryHandleLuckyMatchRaceOver(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, bool First)
{
    return HandleLuckyMatchRaceOver(Client, Room, First);
}

bool TryHandleLuckyMatchGameOver(std::shared_ptr<ClientNode> Client, USHORT LeaveGameType)
{
    return HandleLuckyMatchGameOver(Client, LeaveGameType);
}

bool TryHandleLuckyMatchLeaveGame(std::shared_ptr<ClientNode> Client, USHORT LeaveGameType)
{
    return HandleLuckyMatchLeaveGame(Client, LeaveGameType);
}

void ScheduleLuckyMatchReadyRetry(DWORD roomId)
{
    if (roomId == 0)
        return;

    std::thread([roomId]() {
        const int delaysMs[] = { 700, 1800 };
        for (int delayMs : delaysMs)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));

            std::vector<std::shared_ptr<ClientNode>> targets;
            size_t entered = 0;
            size_t pendingUin = 0;
            size_t pendingConn = 0;
            BYTE status = 0;
            {
                std::lock_guard<std::mutex> lock(g_luckyMutex);
                auto it = g_luckyRooms.find(roomId);
                if (it == g_luckyRooms.end())
                    return;

                LuckyRoomState& state = it->second;
                entered = state.enteredConnIds.size();
                pendingUin = state.switchPendingUins.size();
                pendingConn = state.switchPendingConnIds.size();
                status = state.status;

                if (entered >= kLuckyNeedPlayers || state.startQueued || state.raceBeginSent)
                    return;

                for (const auto& w : state.players)
                {
                    auto c = w.lock();
                    if (!IsLuckyLiveClientLocked(c, roomId))
                        continue;
                    if (state.enteredConnIds.find(c->TransportConnID) != state.enteredConnIds.end())
                        continue;
                    if (c->Uin != 0 && state.enteredUins.find(c->Uin) != state.enteredUins.end())
                        continue;
                    targets.push_back(c);
                }
            }

            if (targets.empty())
            {
                LmtLog("RETRY 815 skip roomId=%u status=%u entered=%zu/%u pendingUin=%zu pendingConn=%zu reason=no-live-target",
                    roomId, (unsigned)status, entered, (unsigned)kLuckyNeedPlayers, pendingUin, pendingConn);
                continue;
            }

            LmtLog("RETRY 815 roomId=%u targets=%zu status=%u entered=%zu/%u pendingUin=%zu pendingConn=%zu delayMs=%d",
                roomId, targets.size(), (unsigned)status, entered, (unsigned)kLuckyNeedPlayers, pendingUin, pendingConn, delayMs);

            for (auto& c : targets)
            {
                if (!c)
                    continue;
                SendParticipateResponse(c, kLuckyNeedPlayers, kLuckyNeedPlayers, 1, 0, nullptr);
                NotifyLuckyMatchReady(c);
            }
        }
    }).detach();
}

void ResponseParticipateLuckyMatch(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
    if (!Client) return;

    BYTE* pBody = Body;
    const BYTE* end = Body ? (Body + BodyLen) : nullptr;
    WORD wireLen = 0;
    const bool skippedTopLen = TrySkipWireTopLen(pBody, end, 8, &wireLen);

    const DWORD reqUin = Read32Safe(pBody, end, Client->Uin);
    (void)Read32Safe(pBody, end, 0); // client tick/time
    const DWORD uin = ClientUinOrBody(Client, reqUin);

    BYTE curPlayers = 1;
    BYTE newPerMinute = 1;
    DWORD matchRoomId = 0;
    std::vector<std::shared_ptr<ClientNode>> readyClients;
    std::string waitSnapshot;

    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        PruneDeadWaitingLocked();
        AddOrRefreshWaitingLocked(uin, Client->TransportConnID, Client);

        curPlayers = static_cast<BYTE>(std::min<size_t>(g_waitingPlayers.size(), 255));
        newPerMinute = static_cast<BYTE>(std::min<size_t>(CountRecentJoinsPerMinuteLocked(), 255));
        readyClients = TryMakeReadyGroupLocked();
        if (!readyClients.empty())
            matchRoomId = FindLuckyRoomIdLocked(readyClients.front());
        waitSnapshot = BuildWaitingSnapshotLocked();
    }

    LmtLog("RECV 400 TransportConnID=%u Uin=%u reqUin=%u bodyLen=%zu reqWireTopLen=%u reqWireLen=%u waiting=%u/%u ready=%zu roomId=%u queue=%s",
        Client->TransportConnID,
        uin,
        reqUin,
        BodyLen,
        skippedTopLen ? 1u : 0u,
        (unsigned)wireLen,
        (unsigned)curPlayers,
        (unsigned)kLuckyNeedPlayers,
        readyClients.size(),
        matchRoomId,
        waitSnapshot.c_str());

    if (readyClients.empty())
    {
        SendParticipateResponse(Client, curPlayers, kLuckyNeedPlayers, newPerMinute, 0, nullptr);
        return;
    }

    // When full, push a final 400 to both clients so the first UI leaves 1/2 wait state.
    for (auto& c : readyClients)
    {
        if (c)
            SendParticipateResponse(c, kLuckyNeedPlayers, kLuckyNeedPlayers, newPerMinute, 0, nullptr);
    }

    if (kLuckySingleServerLocalEnter)
    {
        LmtLog("LOCAL enter roomId=%u players=%zu reason=single-server-no-815", matchRoomId, readyClients.size());
        for (auto& c : readyClients)
        {
            if (!c)
                continue;
            ResponseLmtEnterChatRoom(c);
        }
    }
    else
    {
        for (auto& c : readyClients)
        {
            if (c)
                NotifyLuckyMatchReady(c);
        }

        if (kLuckyRetry815)
            ScheduleLuckyMatchReadyRetry(matchRoomId);
    }
}

void ResponseCancelParticipateLuckyMatch(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
    if (!Client) return;

    BYTE* pBody = Body;
    const BYTE* end = Body ? (Body + BodyLen) : nullptr;
    WORD wireLen = 0;
    const bool skippedTopLen = TrySkipWireTopLen(pBody, end, 4, &wireLen);

    const DWORD reqUin = Read32Safe(pBody, end, Client->Uin);
    const DWORD uin = ClientUinOrBody(Client, reqUin);

    LuckyCancelResult cancel;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        cancel = CancelLuckyParticipationLocked(Client, uin);
    }

    if (cancel.roomDestroyed && cancel.roomId != 0)
    {
        if (auto room = GetRoom(cancel.roomId))
        {
            for (auto& p : room->Player)
                p.reset();
            room->CurrentPlayerNum = 0;
        }
        RecycleRoomID(static_cast<int>(cancel.roomId));
    }

    LmtLog("RECV 401 TransportConnID=%u Uin=%u reqUin=%u bodyLen=%zu reqWireTopLen=%u reqWireLen=%u hadRoom=%u roomDestroyed=%u roomId=%u waiting=%zu requeued=%zu",
        Client->TransportConnID,
        uin,
        reqUin,
        BodyLen,
        skippedTopLen ? 1u : 0u,
        (unsigned)wireLen,
        cancel.hadRoom ? 1 : 0,
        cancel.roomDestroyed ? 1 : 0,
        cancel.roomId,
        cancel.waitingCount,
        cancel.requeuedPlayers.size());

    BYTE buf[2]{};
    BYTE* p = buf;
    Write16(p, 0); // ResultID
    const DWORD len401 = static_cast<DWORD>(p - buf);
    LmtLog("SEND 401 TransportConnID=%u Uin=%u len=%u result=0", Client->TransportConnID, Client->Uin, len401);
    SendToClient(Client, 401, buf, len401, Client->ConnID, FE_PLAYER, 1, Response);

    BYTE notifyBuf[2]{};
    BYTE* n = notifyBuf;
    Write16(n, 0); // ResultID
    const DWORD len816 = static_cast<DWORD>(n - notifyBuf);
    LmtLog("SEND 816 TransportConnID=%u Uin=%u len=%u result=0 reason=cancel", Client->TransportConnID, Client->Uin, len816);
    SendToClient(Client, 816, notifyBuf, len816, Client->ConnID, FE_PLAYER, 1, Notify);

    for (auto& requeued : cancel.requeuedPlayers)
    {
        if (requeued)
            SendParticipateResponse(requeued, static_cast<BYTE>(std::min<size_t>(cancel.waitingCount, 255)), kLuckyNeedPlayers, 1, 0, nullptr);
    }
}

void ResponseLuckyMatchBillboard(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
    if (!Client) return;
    (void)Body;
    (void)BodyLen;

    DWORD championUin = 0;
    std::string championName;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        championUin = g_lastChampionUin;
        championName = g_lastChampionName;
    }

    // 402 会被客户端渲染到右侧“冠军/夺冠公告”列表；无真实冠军前返回 0 条，有本地冠军后返回 1 条官方 116 字节记录。
    BYTE buf[512]{};
    BYTE* p = buf;
    BYTE* topLen = BeginLen(p);
    if (championUin != 0 && !championName.empty())
    {
        Write8(p, 1);
        WriteBillboardRecord(p, 0, championName.c_str(), "幸运大奖赛", "冠军", "", 0, 1, "");
    }
    else
    {
        Write8(p, 0); // recordCount = 0
    }
    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - buf);
    LmtLog("SEND 402 TransportConnID=%u Uin=%u len=%u records=%u championUin=%u schema=wireTopLen",
        Client->TransportConnID,
        Client->Uin,
        len,
        (championUin != 0 && !championName.empty()) ? 1u : 0u,
        championUin);
    SendToClient(Client, 402, buf, len, Client->ConnID, FE_PLAYER, 1, Response);
}

void NotifyLuckyMatchReady(std::shared_ptr<ClientNode> Client)
{
    if (!Client) return;

    DWORD roomId = 0;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        roomId = FindLuckyRoomIdLocked(Client);
        auto it = g_luckyRooms.find(roomId);
        if (it != g_luckyRooms.end())
        {
            if (Client->Uin != 0)
                it->second.switchPendingUins.insert(Client->Uin);
            it->second.switchPendingConnIds.insert(Client->TransportConnID);
        }
    }
    if (roomId == 0)
        roomId = Client->RoomID;

    const USHORT clientServerId = GetLuckySingleServerId(Client);
    USHORT roomServerId = 0;
    if (auto room = GetRoom(roomId))
        roomServerId = static_cast<USHORT>(room->ServerID);
    const USHORT serverId = roomServerId != 0 ? roomServerId : clientServerId;
    const char* ipText = ChooseLuckySingleServerIpText();
    LuckyIPv4Octets externalIp{};
    if (!ParseLuckyIPv4Text(ipText, externalIp))
    {
        // 配置不是可解析 IPv4 时兜底，不让 815 写出 0.0.0.0。
        ParseLuckyIPv4Text("127.0.0.1", externalIp);
        ipText = "127.0.0.1";
    }
    const UINT externalIpWire = LuckyIPv4WireLogValue(externalIp);
    const USHORT tcp = GetTcpPortBase();
    const USHORT udp = GetUdpPortBase();

    BYTE buf[256]{};
    BYTE* p = buf;
    BYTE* topLen = BeginLen(p);

    // pcaps\10 官服 815 body 第一字段是 u16 StructLen，之后才是 CNotifyLuckyMatchReadyPara：
    // ServerID + IP + tcpNum(3) + 3*tcp + udpNum(1) + udp + RoomID + MultiIPInfo。
    Write16(p, serverId);       // Single-server: point back to current game server, not MatchServerInfo(300)
    WriteLuckyIPv4Raw(p, externalIp); // ExtenalIP: source uses "43.226.61.100", official wire writes 64 3D E2 2B

    // 私服当前只监听一个 TCP 端口；仍保留官服 3-port wire 形态，但三个候选都指向同一端口。
    // 否则客户端可能选择第二/第三候选端口 28881/28882，导致切服失败，不会自然发 381。
    Write8(p, 3);               // ExtTcpPortNum
    Write16(p, tcp);
    Write16(p, tcp);
    Write16(p, tcp);

    Write8(p, 1);               // ExtUdpPortNum
    Write16(p, udp);

    Write32(p, roomId);         // 真实 RoomNode->ID

    Write8(p, 1);               // MultiIPInfo count
    {
        BYTE* rec = p;
        Write16(rec, 0);        // nested len，官服样例为 0x14
        Write8(rec, 4);         // IPCount，固定给 4 格，私服单机时 4 个都写同一个可达 IP
        for (int i = 0; i < 4; ++i)
            WriteLuckyIPv4Raw(rec, externalIp);
        Write8(rec, 0);         // DomainLen

        const size_t len = static_cast<size_t>(rec - p);
        Set16(p, static_cast<WORD>(len));
        p += len;
    }

    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - buf);
    char bodyHex[512]{};
    HexBytesUpper(buf, len, bodyHex, sizeof(bodyHex));
    LmtLog("SEND 815 TransportConnID=%u Uin=%u len=%u roomId=%u serverId=%u clientServerId=%u roomServerId=%u notifyServerId=%u singleServer=1 ipText=%s ipWire=%08X tcp=%u/%u/%u udp=%u dstId=%u schema=wireTopLen bodyHex=%s",
        Client->TransportConnID,
        Client->Uin,
        len,
        roomId,
        (unsigned)serverId,
        (unsigned)clientServerId,
        (unsigned)roomServerId,
        (unsigned)serverId,
        ipText,
        externalIpWire,
        (unsigned)tcp,
        (unsigned)tcp,
        (unsigned)tcp,
        (unsigned)udp,
        Client->TransportConnID,
        bodyHex);
    SendLuckyMatchReadyPacket(Client, buf, len);
}

void NotifyParticipateLuckyMatchTimeOut(std::shared_ptr<ClientNode> Client)
{
    if (!Client) return;

    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        RemoveWaitingLocked(Client->Uin, Client->TransportConnID);
    }

    BYTE buf[2]{};
    BYTE* p = buf;
    Write16(p, 0); // ResultID
    const DWORD len = static_cast<DWORD>(p - buf);
    LmtLog("SEND 816 TransportConnID=%u Uin=%u len=%u result=0 reason=timeout", Client->TransportConnID, Client->Uin, len);
    SendToClient(Client, 816, buf, len, Client->ConnID, FE_PLAYER, 1, Notify);
}

void Request24617(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
    if (!Client) return;

    BYTE* pBody = Body;
    const BYTE* end = Body ? (Body + BodyLen) : nullptr;
    WORD wireLen = 0;
    const bool skippedTopLen = TrySkipWireTopLen(pBody, end, 8, &wireLen);

    const DWORD reqUin = Read32Safe(pBody, end, Client->Uin);
    (void)Read32Safe(pBody, end, 0); // client tick/time
    const WORD createRoomTypeReq = Read16Safe(pBody, end, 1);
    const BYTE roundNum = Read8Safe(pBody, end, 0);
    const BYTE playerNum = Read8Safe(pBody, end, kLuckyNeedPlayers);
    const BYTE effectiveRoundNum = roundNum >= kLuckyUiRoundNum ? roundNum : kLuckyUiRoundNum;

    const DWORD uin = ClientUinOrBody(Client, reqUin);
    const BYTE createRoomType = static_cast<BYTE>(std::max<WORD>(1, std::min<WORD>(createRoomTypeReq, 255)));

    DWORD roomNum = g_nextCreateRoomNum.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        LuckyCreateRoomEntry e;
        e.ownerUin = uin;
        e.roomNum = roomNum;
        e.createRoomType = createRoomType;
        e.roundNum = effectiveRoundNum;
        e.playerNum = playerNum;
        e.createTime = std::chrono::steady_clock::now();
        g_createdRooms.push_back(e);
        if (g_createdRooms.size() > 64)
            g_createdRooms.erase(g_createdRooms.begin(), g_createdRooms.begin() + (g_createdRooms.size() - 64));
    }

    BYTE buf[4096]{};
    BYTE* p = buf;

    Write32(p, uin);                // Uin
    Write16(p, 0);                  // ResultID
    Write16(p, 0);                  // ReasonLen
    memset(p, 0, kLuckyCreateRoomReasonMax);
    p += kLuckyCreateRoomReasonMax; // Reason[0x800]
    Write8(p, createRoomType);      // CreateRoomType from request
    Write32(p, roomNum);            // dynamic RoomNum/token

    const DWORD len = static_cast<DWORD>(p - buf);
    LmtLog("SEND 24617 TransportConnID=%u Uin=%u len=%u roomNum=%u createType=%u round=%u reqRound=%u playerNum=%u reqWireTopLen=%u reqWireLen=%u",
        Client->TransportConnID, Client->Uin, len, roomNum, (unsigned)createRoomType, (unsigned)effectiveRoundNum, (unsigned)roundNum, (unsigned)playerNum,
        skippedTopLen ? 1u : 0u, (unsigned)wireLen);
    SendToClient(Client, 24617, buf, len, Client->ConnID, FE_PLAYER, 1, Response);
}

void RequestLmtEnterChatRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
    if (!Client) return;

    BYTE* pBody = Body;
    const BYTE* end = Body ? (Body + BodyLen) : nullptr;
    WORD wireLen = 0;
    const bool skippedTopLen = TrySkipWireTopLen(pBody, end, 12, &wireLen);

    const DWORD reqUin = Read32Safe(pBody, end, Client->Uin);
    (void)Read32Safe(pBody, end, 0); // Time
    const DWORD reqRoomId = Read32Safe(pBody, end, Client->RoomID);
    const DWORD uin = ClientUinOrBody(Client, reqUin);

    LmtLog("RECV 381 TransportConnID=%u Uin=%u reqUin=%u reqRoomId=%u bodyLen=%zu curRoom=%u reqWireTopLen=%u reqWireLen=%u",
        Client->TransportConnID,
        uin,
        reqUin,
        reqRoomId,
        BodyLen,
        Client->RoomID,
        skippedTopLen ? 1u : 0u,
        (unsigned)wireLen);

    // 815 后部分客户端不直接沿用旧连接发 381，而是先 101 断开，再 100 新登录。
    // 如果新连接随后发 381，这里先按 Uin 回挂到原 LuckyRoom，避免后续 players 仍指向旧 ConnID。
    AttachLuckyMatchReconnect(Client, "381");

    DWORD roomId = 0;
    LuckyRoomState state;
    bool allEntered = false;
    std::vector<std::shared_ptr<ClientNode>> players;

    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        roomId = FindLuckyRoomIdLocked(Client, reqRoomId);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
        {
            LmtLog("RECV 381 fail TransportConnID=%u Uin=%u reqRoomId=%u resolvedRoomId=%u reason=no-lucky-room",
                Client->TransportConnID, uin, reqRoomId, roomId);
            LuckyRoomState empty;
            SendLmtEnterRoomResponse(Client, empty, 1);
            return;
        }

        if (!IsLuckyParticipantLocked(it->second, Client, uin))
        {
            LmtLog("RECV 381 reject TransportConnID=%u Uin=%u reqRoomId=%u resolvedRoomId=%u reason=not-lucky-participant",
                Client->TransportConnID, uin, reqRoomId, roomId);
            LuckyRoomState empty;
            SendLmtEnterRoomResponse(Client, empty, 1);
            return;
        }

        Client->RoomID = static_cast<USHORT>(roomId);
        Client->CurrentRoom = GetRoom(roomId).get();
        Client->BorderRoomID = 0;
        Client->ChatRoomID = -1;
        if (uin != 0)
            g_uinRoomId[uin] = roomId;
        g_connRoomId[Client->TransportConnID] = roomId;

        it->second.enteredUins.insert(uin);
        it->second.enteredConnIds.insert(Client->TransportConnID);
        state = it->second;
        players = CollectPlayers(state);
        allEntered = players.size() >= kLuckyNeedPlayers && it->second.enteredConnIds.size() >= players.size();
    }

    LmtLog("RECV 381 ok TransportConnID=%u Uin=%u roomId=%u entered=%zu/%zu allEntered=%u",
        Client->TransportConnID,
        uin,
        roomId,
        state.enteredConnIds.size(),
        players.size(),
        allEntered ? 1 : 0);

    SendLmtEnterRoomResponse(Client, state, 0);

    for (auto& target : players)
    {
        if (target && target->TransportConnID != Client->TransportConnID)
            SendLmtEnterNotify(target, Client);
    }

    NotifyLmtGetScoreInfo(Client);
    SendLmtSummaryInfo(Client, state);
    SendLmtStatusNotify(Client, state);

    if (allEntered)
        BeginLuckyMatchCountdown(roomId);
}

void ResponseLmtEnterChatRoom(std::shared_ptr<ClientNode> Client)
{
    if (!Client) return;

    DWORD roomId = 0;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        roomId = FindLuckyRoomIdLocked(Client);
    }

    BYTE body[14]{};
    BYTE* p = body;
    BYTE* topLen = BeginLen(p);
    Write32(p, Client->Uin);
    Write32(p, static_cast<DWORD>(time(nullptr)));
    Write32(p, roomId ? roomId : Client->RoomID);
    FinishLen(topLen, p);
    RequestLmtEnterChatRoom(Client, body, static_cast<size_t>(p - body));
}

void SendLmtStatusResponse(std::shared_ptr<ClientNode> Client, const LuckyRoomState& state, const char* reason)
{
    if (!Client)
        return;

    BYTE buf[32]{};
    BYTE* p = buf;
    BYTE* topLen = BeginLen(p);
    Write16(p, 0);                 // ResultID
    Write8(p, 0);                  // ReasonLen
    Write8(p, state.status);       // PlayerStatus
    Write32(p, state.leftTime);    // LeftTime
    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - buf);
    LmtLog("SEND 382 TransportConnID=%u Uin=%u len=%u status=%u left=%u reason=%s schema=wireTopLen",
        Client->TransportConnID,
        Client->Uin,
        len,
        (unsigned)state.status,
        state.leftTime,
        reason ? reason : "");
    SendLuckyRoomPacket(Client, 382, buf, len, state.roomId ? state.roomId : Client->RoomID, Response);
}

void ResponseLmtUpdateStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
    if (!Client) return;
    (void)Body;
    LmtLog("RECV 382 TransportConnID=%u Uin=%u bodyLen=%zu roomId=%u", Client->TransportConnID, Client->Uin, BodyLen, Client->RoomID);

    LuckyRoomState state;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        const DWORD roomId = FindLuckyRoomIdLocked(Client);
        auto it = g_luckyRooms.find(roomId);
        if (it != g_luckyRooms.end())
            state = it->second;
        else
        {
            state.status = LMT_STATUS_WAIT_ENTER;
            state.leftTime = 0;
        }
    }

    SendLmtStatusResponse(Client, state, "client-request");
}

void NotifyLmtGetScoreInfo(std::shared_ptr<ClientNode> Client)
{
    if (!Client) return;

    LuckyRoomState state;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        const DWORD roomId = FindLuckyRoomIdLocked(Client);
        auto it = g_luckyRooms.find(roomId);
        if (it != g_luckyRooms.end())
            state = it->second;
        else
        {
            state.roomId = Client->RoomID;
            state.roundNum = kLuckyUiRoundNum;
        }
    }

    BYTE buf[2048]{};
    BYTE* p = buf;
    BYTE* topLen = BeginLen(p);
    WriteScoreInfoPayload(p, state, Client);
    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - buf);
    LmtLog("SEND 810 TransportConnID=%u Uin=%u len=%u roomId=%u round=%u schema=wireTopLen", Client->TransportConnID, Client->Uin, len, state.roomId, (unsigned)state.roundNum);
    SendLuckyRoomPacket(Client, 810, buf, len, state.roomId ? state.roomId : Client->RoomID, Notify);
}

void SendLmtScoreInfoResponse(std::shared_ptr<ClientNode> Client, const LuckyRoomState& state, const char* reason)
{
    if (!Client)
        return;

    BYTE rspBuf[2048];
    memset(rspBuf, 0, sizeof(rspBuf));
    BYTE* p = rspBuf;
    BYTE* topLen = BeginLen(p);
    Write16(p, 0); // ResultID
    Write8(p, 0);  // ReasonLen
    WriteScoreInfoPayload(p, state, Client);
    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - rspBuf);
    LmtLog("SEND 383 TransportConnID=%u Uin=%u len=%u roomId=%u round=%u reason=%s schema=wireTopLen",
        Client->TransportConnID,
        Client->Uin,
        len,
        state.roomId,
        (unsigned)state.roundNum,
        reason ? reason : "");
    SendLuckyRoomPacket(Client, 383, rspBuf, len, state.roomId ? state.roomId : Client->RoomID, Response);
}

void ResponseLmtGetScoreInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
    if (!Client) return;
    (void)Body;
    LmtLog("RECV 383 TransportConnID=%u Uin=%u bodyLen=%zu roomId=%u", Client->TransportConnID, Client->Uin, BodyLen, Client->RoomID);

    LuckyRoomState state;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        const DWORD roomId = FindLuckyRoomIdLocked(Client);
        auto it = g_luckyRooms.find(roomId);
        if (it != g_luckyRooms.end())
            state = it->second;
        else
        {
            state.roomId = Client->RoomID;
            state.roundNum = kLuckyUiRoundNum;
        }
    }

    SendLmtScoreInfoResponse(Client, state, "client-request");
    NotifyLmtGetScoreInfo(Client);
}

void WriteLmtElimMatchRecord(BYTE*& p, DWORD uinA, DWORD uinB, BYTE result)
{
    BYTE* rec = BeginLen(p);
    Write32(p, uinA);
    Write32(p, uinB);
    Write8(p, result);
    FinishLen(rec, p);
}

BYTE LuckyElimRoundFromState(const LuckyRoomState& state)
{
    return state.elimRound <= 3 ? state.elimRound : 3;
}

void WriteLmtElimInfoPayload(BYTE*& p, const LuckyRoomState& state, bool responseHead, BYTE forcedRound = 0)
{
    if (responseHead)
    {
        Write16(p, 0); // ResultID
        Write8(p, 0);  // ReasonLen
    }

    const auto players = CollectPlayers(state);
    const DWORD uinA = (!players.empty() && players[0]) ? players[0]->Uin : 0;
    const DWORD uinB = (players.size() > 1 && players[1]) ? players[1]->Uin : 0;
    const BYTE round = forcedRound ? forcedRound : LuckyElimRoundFromState(state);

    const bool twoPlayerFinal = players.size() <= kLuckyNeedPlayers;
    const BYTE effectiveRound = twoPlayerFinal ? 0 : round;
    const BYTE matchNum = twoPlayerFinal ? 1 : 3;

    // pcaps\10 的 384/809 wire 每条 match 前都有 u16 recordLen，样例每条 0x000B。
    // 本地两人局是直接 1v1 决赛；不能继续发送三条淘汰面板记录，
    // 否则 UI 会固定落到“四强席位”并保留空槽。
    Write8(p, effectiveRound);
    Write8(p, matchNum);
    WriteLmtElimMatchRecord(p, uinA, uinB, 3);
    if (matchNum > 1)
    {
        WriteLmtElimMatchRecord(p, 0, 0, 3);
        WriteLmtElimMatchRecord(p, 0, 0, 3);
    }
}

void NotifyLmtGetElimInfo(std::shared_ptr<ClientNode> client, const LuckyRoomState& state, BYTE forcedRound = 0)
{
    if (!client)
        return;

    BYTE buf[128]{};
    BYTE* p = buf;
    BYTE* topLen = BeginLen(p);
    WriteLmtElimInfoPayload(p, state, false, forcedRound);
    FinishLen(topLen, p);

    const DWORD len = static_cast<DWORD>(p - buf);
    const BYTE round = forcedRound ? forcedRound : LuckyElimRoundFromState(state);
    const auto players = CollectPlayers(state);
    const BYTE effectiveRound = players.size() <= kLuckyNeedPlayers ? 0 : round;
    const BYTE matchNum = players.size() <= kLuckyNeedPlayers ? 1 : 3;
    LmtLog("SEND 809 TransportConnID=%u Uin=%u len=%u roomId=%u round=%u matchNum=%u schema=wireTopLen",
        client->TransportConnID, client->Uin, len, state.roomId ? state.roomId : client->RoomID, (unsigned)effectiveRound, (unsigned)matchNum);
    SendLuckyRoomPacket(client, 809, buf, len, state.roomId ? state.roomId : client->RoomID, Notify);
}

void SendLmtElimInfoResponse(std::shared_ptr<ClientNode> Client, const LuckyRoomState& state, BYTE forcedRound, const char* reason)
{
    if (!Client)
        return;

    BYTE elimBuf[128]{};
    BYTE* p = elimBuf;
    BYTE* topLen = BeginLen(p);
    WriteLmtElimInfoPayload(p, state, true, forcedRound);
    FinishLen(topLen, p);
    const DWORD len = static_cast<DWORD>(p - elimBuf);
    const BYTE round = forcedRound ? forcedRound : LuckyElimRoundFromState(state);
    const auto players = CollectPlayers(state);
    const BYTE effectiveRound = players.size() <= kLuckyNeedPlayers ? 0 : round;
    const BYTE matchNum = players.size() <= kLuckyNeedPlayers ? 1 : 3;
    LmtLog("SEND 384 TransportConnID=%u Uin=%u len=%u roomId=%u round=%u matchNum=%u reason=%s schema=wireTopLen",
        Client->TransportConnID,
        Client->Uin,
        len,
        state.roomId ? state.roomId : Client->RoomID,
        (unsigned)effectiveRound,
        (unsigned)matchNum,
        reason ? reason : "");
    SendLuckyRoomPacket(Client, 384, elimBuf, len, state.roomId ? state.roomId : Client->RoomID, Response);
}

void ResponseLmtGetElimInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
    if (!Client) return;
    (void)Body;
    (void)BodyLen;

    LuckyRoomState state;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        const DWORD roomId = FindLuckyRoomIdLocked(Client);
        auto it = g_luckyRooms.find(roomId);
        if (it != g_luckyRooms.end())
            state = it->second;
        else
        {
            state.roomId = Client->RoomID;
            state.roundNum = kLuckyUiRoundNum;
        }
    }

    SendLmtElimInfoResponse(Client, state, 0, "client-request");
    NotifyLmtGetElimInfo(Client, state);
}

bool SetLuckyOfficialStage(DWORD roomId, BYTE status, DWORD leftTime, LuckyRoomState& state, std::vector<std::shared_ptr<ClientNode>>& players, const char* tag)
{
    std::lock_guard<std::mutex> lock(g_luckyMutex);
    auto it = g_luckyRooms.find(roomId);
    if (it == g_luckyRooms.end())
        return false;

    LuckyRoomState& roomState = it->second;
    if (roomState.startQueued || roomState.raceBeginSent)
    {
        LmtLog("OFFICIAL_STAGE skip roomId=%u status=%u left=%u tag=%s reason=race-already-started",
            roomId,
            (unsigned)status,
            leftTime,
            tag ? tag : "");
        return false;
    }

    roomState.status = status;
    roomState.leftTime = leftTime;
    state = roomState;
    players = CollectPlayers(roomState);
    LmtLog("OFFICIAL_STAGE roomId=%u status=%u left=%u players=%zu tag=%s",
        roomId,
        (unsigned)status,
        leftTime,
        players.size(),
        tag ? tag : "");
    return true;
}

void BroadcastLuckyOfficialScoreTriplet(DWORD roomId, BYTE status, DWORD leftTime, bool sendStatusResponse, bool sendScoreResponse, bool sendLikeInfo, const char* tag)
{
    LuckyRoomState state;
    std::vector<std::shared_ptr<ClientNode>> players;
    if (!SetLuckyOfficialStage(roomId, status, leftTime, state, players, tag))
        return;

    for (auto& c : players)
    {
        NotifyLmtGetScoreInfo(c);
        SendLmtSummaryInfo(c, state);
        if (sendLikeInfo)
            SendLucky24602(c, state);
        if (sendStatusResponse)
            SendLmtStatusResponse(c, state, tag);
        if (sendScoreResponse)
            SendLmtScoreInfoResponse(c, state, tag);
    }
}

void BroadcastLuckyOfficialStatus(DWORD roomId, BYTE status, DWORD leftTime, bool sendBetInfo, const char* tag)
{
    LuckyRoomState state;
    std::vector<std::shared_ptr<ClientNode>> players;
    if (!SetLuckyOfficialStage(roomId, status, leftTime, state, players, tag))
        return;

    for (auto& c : players)
    {
        SendLmtStatusNotify(c, state);
        if (sendBetInfo)
            SendLucky24607(c, state);
    }
}

void BroadcastLuckyOfficialElim(DWORD roomId, BYTE round, bool sendResponse, bool sendNotify, const char* tag)
{
    LuckyRoomState state;
    std::vector<std::shared_ptr<ClientNode>> players;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return;
        it->second.elimRound = round <= 3 ? round : 3;
        state = it->second;
        players = CollectPlayers(it->second);
    }

    LmtLog("OFFICIAL_ELIM roomId=%u round=%u players=%zu response=%u notify=%u tag=%s",
        roomId,
        (unsigned)round,
        players.size(),
        sendResponse ? 1u : 0u,
        sendNotify ? 1u : 0u,
        tag ? tag : "");
    for (auto& c : players)
    {
        if (sendResponse)
            SendLmtElimInfoResponse(c, state, round, tag);
        if (sendNotify)
            NotifyLmtGetElimInfo(c, state, round);
    }
}

void BroadcastLuckyOfficial804(DWORD roomId, const char* tag)
{
    LuckyRoomState state;
    std::vector<std::shared_ptr<ClientNode>> players;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return;
        state = it->second;
        players = CollectPlayers(it->second);
    }

    LmtLog("OFFICIAL_804 roomId=%u players=%zu tag=%s", roomId, players.size(), tag ? tag : "");
    for (auto& c : players)
        SendLmtOfficial804(c, state);
}

void BroadcastLuckyOfficialCommonEvent(DWORD roomId, const char* tag)
{
    LuckyRoomState state;
    std::vector<std::shared_ptr<ClientNode>> players;
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return;
        state = it->second;
        players = CollectPlayers(it->second);
    }

    LmtLog("OFFICIAL_28381 roomId=%u players=%zu tag=%s", roomId, players.size(), tag ? tag : "");
    for (auto& c : players)
    {
        // 官服 pkt=633，紧贴 804 前：cmd=28381 / NotifyClientCommonEvent，bodyLen=12。
        SendLuckyOfficialHexPacket(c, state, 28381, "000C00120000000000000000", Notify, tag ? tag : "before-804");
    }
}

void MarkLuckyOfficialMiddleSent(DWORD roomId)
{
    std::lock_guard<std::mutex> lock(g_luckyMutex);
    auto it = g_luckyRooms.find(roomId);
    if (it != g_luckyRooms.end())
        it->second.officialMiddleSent = true;
}

void PrepareLuckyBetweenRounds(DWORD roomId, BYTE nextRound, const char* tag)
{
    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return;

        LuckyRoomState& state = it->second;
        state.elimRound = nextRound <= 3 ? nextRound : 3;
        state.startQueued = false;
        state.raceBeginSent = false;
        state.prepareReadyConnIds.clear();
        state.matchOverStarted = false;
        state.matchOverNotifiedUins.clear();
        state.status = LMT_STATUS_ELIM_WAIT;
        state.leftTime = 0;
    }

    if (auto room = GetRoom(roomId))
    {
        room->Israce = 0;
        room->startStatus = 0;
        room->RaceStartPlayerNum = 0;
        room->RaceActiveMask = 0;
        for (auto& c : room->Player)
        {
            if (!c) continue;
            c->IsReady = FALSE;
            c->ReadyState = 0;
            c->FinishTime = 0;
            c->Round = 0;
            c->PassedCheckPoint = 0;
            c->MsgSequence = -1;
        }
    }

    LmtLog("ROUND_RESET roomId=%u nextRound=%u tag=%s", roomId, (unsigned)nextRound, tag ? tag : "");
}

void ArmLuckyNextRace(DWORD roomId, BYTE nextRound, const char* tag)
{
    std::lock_guard<std::mutex> lock(g_luckyMutex);
    auto it = g_luckyRooms.find(roomId);
    if (it == g_luckyRooms.end())
        return;

    LuckyRoomState& state = it->second;
    state.elimRound = nextRound <= 3 ? nextRound : 3;
    state.startQueued = false;
    state.raceBeginSent = false;
    state.roundAdvanceQueued = false;
    state.raceOverUins.clear();
    state.prepareReadyConnIds.clear();
    LmtLog("ROUND_ARM_START roomId=%u round=%u tag=%s", roomId, (unsigned)state.elimRound, tag ? tag : "");
}

void ScheduleLuckyMatchNextElimRound(DWORD roomId, BYTE nextRound)
{
    if (roomId == 0)
        return;

    nextRound = nextRound <= 3 ? nextRound : 3;
    std::thread([roomId, nextRound]() {
        PrepareLuckyBetweenRounds(roomId, nextRound, "post-race-next-round");

        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ROLL_READY, kLuckyRollLeftSec, false, "post-race-status9-roll-ready");

        std::this_thread::sleep_for(std::chrono::milliseconds(kLuckyRollAdvanceDelayMs));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ROLLING, kLuckyRollLeftSec, false, "post-race-status10-rolling");

        std::this_thread::sleep_for(std::chrono::milliseconds(kLuckyRollAdvanceDelayMs));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ELIM_WAIT, 0, false, "post-race-status6-elim-wait");
        BroadcastLuckyOfficialElim(roomId, nextRound, true, true, "post-race-384-809");

        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ELIM_COUNT, 12, false, "post-race-status7-elim-count");
        BroadcastLuckyOfficialElim(roomId, nextRound, true, false, "post-race-final-384");

        BroadcastLuckyOfficialCommonEvent(roomId, "post-race-before-804");
        BroadcastLuckyOfficial804(roomId, "post-race-before-509");

        ArmLuckyNextRace(roomId, nextRound, "post-race-before-509");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        StartLuckyMatchRace(roomId, "official-after-804");
    }).detach();
}

void ScheduleLuckyMatchOfficialMiddle(DWORD roomId)
{
    if (roomId == 0)
        return;

    {
        std::lock_guard<std::mutex> lock(g_luckyMutex);
        auto it = g_luckyRooms.find(roomId);
        if (it == g_luckyRooms.end())
            return;
        if (it->second.officialMiddleQueued)
            return;
        it->second.officialMiddleQueued = true;
    }

    std::thread([roomId]() {
        // 官服 status=5 后会持续刷新 810/811/24602，并给 382/383 当前状态响应。
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        BroadcastLuckyOfficialScoreTriplet(roomId, LMT_STATUS_RACING, kLuckyRacingStatusLeftSec,
            true, true, true, "status5-score-refresh");

        // leftTime 是客户端展示/状态字段；本地测试调度不能按 70 秒墙钟等待。
        std::this_thread::sleep_for(std::chrono::milliseconds(kLuckyTop16AdvanceDelayMs));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_TOP16, 0, true, "status12-top16-bet");

        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ELIM_WAIT, 0, false, "status6-elim-wait");

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ELIM_COUNT, 12, false, "status7-elim-count");
        BroadcastLuckyOfficialElim(roomId, 3, true, false, "round3-384");

        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_RACE_WAIT, kLuckyRaceWaitLeftSec, false, "status8-race-wait");

        // 继续保留官服 wire 字段 left=230，但本地调度短等待后推进下一段。
        std::this_thread::sleep_for(std::chrono::milliseconds(kLuckyRaceWaitAdvanceDelayMs));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ROLL_READY, kLuckyRollLeftSec, false, "status9-roll-ready");

        std::this_thread::sleep_for(std::chrono::milliseconds(kLuckyRollAdvanceDelayMs));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ROLLING, kLuckyRollLeftSec, false, "status10-rolling");

        std::this_thread::sleep_for(std::chrono::milliseconds(kLuckyRollAdvanceDelayMs));
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ELIM_WAIT, 0, false, "status6-after-roll");
        BroadcastLuckyOfficialElim(roomId, 2, true, true, "round2-384-809");
        BroadcastLuckyOfficialStatus(roomId, LMT_STATUS_ELIM_COUNT, 12, false, "status7-final-ready");
        BroadcastLuckyOfficialElim(roomId, 2, true, false, "round2-final-384");
        BroadcastLuckyOfficialCommonEvent(roomId, "before-804");
        BroadcastLuckyOfficial804(roomId, "before-client-120-or-official-509");
        MarkLuckyOfficialMiddleSent(roomId);

        // 官服首局抓包：pkt633=28381, pkt634=804, pkt635=28346/509, pkt636=28346/551。
        // 120 出现在后续轮次切换，不是首局 804 后的必经客户端请求。
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        StartLuckyMatchRace(roomId, "official-after-804");
    }).detach();
}
