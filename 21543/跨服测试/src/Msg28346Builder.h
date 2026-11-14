#pragma once

#include <Windows.h>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct ClientNode;
struct ItemInfo;
struct PetInfo;

namespace qqspeed::msg511
{
// 21487 Network.dll::CNotifyRaceBeginPara
// (sub_1800DCFE0/sub_1800FE660), packed size 0x30.
#pragma pack(push, 1)
struct CNotifyRaceBeginPara
{
    DWORD CountDownTime = 0;
    WORD DelayTime = 0;
    BYTE StageIndex = 0;
    DWORD RaceCountDownTime = 0;
    DWORD RaceCountDownDis = 0;
    BYTE UseNewCountDownTime = 0;
    DWORD NewCountDownTime = 0;
    DWORD ServerSecond = 0;
    DWORD ServerMicroSecond = 0;
    DWORD RaceStartTime = 0;
    DWORD RaceStartMicroSecond = 0;
    DWORD RaceBeginSecond = 0;
    DWORD RaceBeginMicroSecond = 0;
    DWORD Para1 = 0;
};
#pragma pack(pop)

inline constexpr DWORD kClientPidCheckXor = 0x3A3A3A3Au;

static_assert(sizeof(CNotifyRaceBeginPara) == 0x30);
static_assert(offsetof(CNotifyRaceBeginPara, StageIndex) == 0x06);
static_assert(offsetof(CNotifyRaceBeginPara, RaceCountDownTime) == 0x07);
static_assert(offsetof(CNotifyRaceBeginPara, UseNewCountDownTime) == 0x0F);
static_assert(offsetof(CNotifyRaceBeginPara, ServerSecond) == 0x14);
static_assert(offsetof(CNotifyRaceBeginPara, ServerMicroSecond) == 0x18);
static_assert(offsetof(CNotifyRaceBeginPara, RaceStartTime) == 0x1C);
static_assert(offsetof(CNotifyRaceBeginPara, RaceBeginMicroSecond) == 0x28);
static_assert(offsetof(CNotifyRaceBeginPara, Para1) == 0x2C);
}

// 将服务端现有普通 Msg100 紧凑 body 按当前 Network.dll 的 native-safe
// CSSafeCResponseLoginPara schema 编码，并封装为 mode=1 的 28346(inner100)。
// compactBody 只用于还原固定布局；物品/宠物动态子对象按结构重新编码，
// 不复制官服抓包字节，也不在失败时回退直发 Msg100。
bool BuildOfficialSafeLogin100(
    UINT outerUin,
    UINT requestTime,
    DWORD clientPid,
    const BYTE* compactBody,
    size_t compactBodyLen,
    const std::vector<ItemInfo>& items,
    const std::vector<PetInfo>& pets,
    std::vector<BYTE>& outBody,
    std::string* diag = nullptr);

// 构造 Network.dll 官服 native-safe 28346(inner 223) mode-1 body：
// +0x00 Uin BE32，+0x04 Time BE32，+0x08 innerMsg BE16(223)，+0x0A innerLen BE16，
// +0x0C safe CSSafeCNotifyLoginMoreInfoPara stream，随后紧跟 mode byte 1。
// 32213 是客户端解码 workspace 容量，不是线上固定 body 长度。
//
// 21487 官服抓包与当前 Network.dll 反编译证据：
// - msg100 发装备中物品和前 50 只宠物；
// - 223 的 ItemNum 为 0，只承载后续宠物详情，50/page；
// - 背包全量另由 437/438 响应客户端请求。
// 返回的 outBody 是直接传给 SendToClient(..., 28346, outBody.data(), outBody.size(), ...) 的 body。
bool BuildOfficialSafeLoginMoreInfo223(
    UINT outerUin,
    UINT requestTime,
    DWORD clientPid,
    const std::vector<PetInfo>& pets,
    bool hasNext,
    std::vector<BYTE>& outBody,
    std::string* diag = nullptr);

// 当前客户端背包链路：437 为首个 1000 件分页，438 为后续分页。
// 两者都放在 mode=1 的 28346 壳内；ItemType 按官服 expanded 数据写 0。
bool BuildOfficialSafePlayerItems437(
    UINT outerUin,
    UINT requestTime,
    DWORD clientPid,
    const std::vector<ItemInfo>& items,
    bool hasMore,
    USHORT totalItemNum,
    std::vector<BYTE>& outBody,
    std::string* diag = nullptr);

bool BuildOfficialSafePlayerItems438(
    UINT outerUin,
    UINT requestTime,
    DWORD clientPid,
    const std::vector<ItemInfo>& items,
    bool hasNext,
    std::vector<BYTE>& outBody,
    std::string* diag = nullptr);

enum class OfficialKartRefit271BuildResult
{
    Failed,
    Safe28346,
    Direct271Required,
};

// 官服首个 271 页在 Build323 内层工作区可容纳时使用 mode=1 的 28346；
// 编码后的内层超过工作区上限时改发原生 compact 271。
OfficialKartRefit271BuildResult BuildOfficialSafeKartRefit271(
    UINT outerUin,
    UINT responseContext,
    DWORD clientPid,
    const BYTE* plainBody,
    size_t plainBodyLen,
    std::vector<BYTE>& outBody,
    std::string* diag = nullptr);

// 官服 Build323 的 318 S->C 使用 mode=1 的 28346 外壳。
bool BuildOfficialSafeKartRefit318(
    UINT outerUin,
    UINT responseContext,
    DWORD clientPid,
    const BYTE* plainBody,
    size_t plainBodyLen,
    std::vector<BYTE>& outBody,
    std::string* diag = nullptr);

// 官服 21543 的 509 S->C 使用 mode=1 的 28346 外壳。
// 已按官服样本恢复 Base1/Sub1、排位 Base40/Sub1 与自定义
// Base42/Sub27（含 1..12P、自定义方案、穿梭地图、匿名玩家和 GL 特效）。
// 未由官服样本确认的可选分支仍严格失败。
// 不发送普通 509，也不使用抓包模板或兼容兜底。
bool BuildOfficialSafeGameBegin509(
    UINT outerUin,
    UINT context,
    DWORD clientPid,
    const BYTE* plainBody,
    size_t plainBodyLen,
    std::vector<BYTE>& outBody,
    std::string* diag = nullptr);

// 官服 Build323 的 511 S->C：完整 CNotifyRaceBeginPara 先按
// CSSafeCNotifyRaceBeginPara::ReadFromStream 的逆变换写入 safe stream，
// 再封装为 mode=1 的 28346(inner511)。outer context 与结构内
// ServerSecond 是官服先后独立取时；Para1 必须为 PID ^ 0x3A3A3A3A。
// 不发送普通 511，也不提供失败回退。
bool BuildOfficialSafeRaceBegin511(
    UINT outerUin,
    UINT context,
    DWORD clientPid,
    const qqspeed::msg511::CNotifyRaceBeginPara& plain,
    std::vector<BYTE>& outBody,
    std::string* diag = nullptr);
