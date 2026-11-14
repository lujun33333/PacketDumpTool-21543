#pragma once

#include <Windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// 官服 native-safe / 28346 壳的公共描述。
// 这里只负责剥壳与校验 innerMsg/innerLen，不解析业务字段。
struct Msg28346Envelope
{
	UINT OuterUin = 0;
	UINT Context = 0;
	USHORT InnerMsg = 0;
	USHORT InnerLen = 0;
	const BYTE* InnerBody = nullptr;
	size_t InnerBodyLen = 0;
	size_t HeaderLen = 0;
	BYTE Mode = 0;
};

struct Msg28346ParseDiag
{
	std::string ParseMode = "official-28346";
	std::string Reason = "not parsed";
	UINT OuterUin = 0;
	UINT Context = 0;
	USHORT InnerMsg = 0;
	USHORT InnerLen = 0;
	size_t BodyLen = 0;
	size_t HeaderLen = 0;
	BYTE Mode = 0;
};

// 官服线上序列化形态固定为 body[0] 起始、BE 字段：
// +0x00 outerUin, +0x04 context, +0x08 innerMsg, +0x0A innerLen, +0x0C innerBody。
// expectedInnerMsg 为 0 时只剥壳不限制 innerMsg。
bool TryParseOfficialMsg28346Envelope(
	const BYTE* body,
	size_t bodyLen,
	USHORT expectedInnerMsg,
	Msg28346Envelope& out,
	Msg28346ParseDiag* diag = nullptr);

namespace QQSpeedMsg103
{
	// Network.dll 21487 的 CSSafeCRequestCreateRoomPara 字段。
	// 不能映射回旧 RequestCreateRoom 的扩展字段也保留在这里，以便严格拒绝，
	// 而不是在服务端静默丢弃。
	struct RequestFields
	{
		std::uint32_t uin = 0;
		std::uint32_t timestamp = 0;
		std::uint16_t baseGameMode = 0;
		std::uint16_t subGameMode = 0;
		std::uint16_t flag = 0;
		std::array<std::uint8_t, 22> roomName{};
		std::uint8_t seatNum = 0;
		std::array<std::uint8_t, 16> password{};
		std::uint8_t isNewerOpen = 0;
		std::uint8_t fbSeason = 0;
		std::uint8_t context = 0;
		std::uint8_t fbLevel = 0;
		std::uint8_t specialFlag = 0;
		std::uint8_t moreInfoCount = 0;
		std::uint8_t extendedRoomInfoCount = 0;
		std::uint16_t createRoomType = 0;
		std::uint16_t serverMatchType = 0;
		std::uint16_t teamMatchType = 0;
		std::uint8_t chatRoomType = 0;
		std::uint16_t sceneId = 0;
		std::uint8_t presetObNum = 0;
		std::uint8_t allowAutoStart = 0;
		std::uint8_t talkRoomBgId = 0;
		std::vector<std::uint32_t> enterRoomValues;
		std::uint8_t extension88 = 0;
		std::uint8_t extension89 = 0;
		std::uint8_t extension90 = 0;
		std::uint64_t reserved64 = 0;
	};

	// 只接受 outer 28346 指定 innerLen 内的 Msg103 安全编码。
	bool DecodeSafeRequest(
		const std::uint8_t* safe,
		std::size_t safeLength,
		RequestFields& out,
		std::string* error = nullptr);

	// 生成 Room.cpp::RequestCreateRoom 当前消费的网络序普通请求体。
	// 不可表示的嵌套字段、扩展字段和尾随字节一律拒绝。
	bool BuildPlainRequest(
		const RequestFields& fields,
		std::vector<std::uint8_t>& out,
		std::string* error = nullptr);

	bool DecodeSafeRequestToPlain(
		const std::uint8_t* safe,
		std::size_t safeLength,
		std::vector<std::uint8_t>& out,
		RequestFields* decoded = nullptr,
		std::string* error = nullptr);
}

namespace QQSpeedMsg110
{
	// CSSafeCRequestStartGamePara -> RequestStartGame 的 9 字节普通请求体。
	bool DecodeSafeRequestToPlain(
		const std::uint8_t* safe,
		std::size_t safeLength,
		std::vector<std::uint8_t>& out,
		std::string* error = nullptr);
}

namespace QQSpeedMsg112
{
	// CSSafeCRequestHelloPara -> RequestHello 的 Uin(BE32) + Time(BE32)。
	bool DecodeSafeRequestToPlain(
		const std::uint8_t* safe,
		std::size_t safeLength,
		std::vector<std::uint8_t>& out,
		std::string* error = nullptr);
}

namespace QQSpeedMsg120
{
	// CSSafeCRequestPrepareReadyPara -> RequestPrepareReady 的 0x2E 字节普通请求体。
	// 不可表示的 extension_46、非零 padding、非规范 varint 和尾随字节一律拒绝。
	bool DecodeSafeRequestToPlain(
		const std::uint8_t* safe,
		std::size_t safeLength,
		std::vector<std::uint8_t>& out,
		std::string* error = nullptr);
}

namespace QQSpeedMsg123
{
	struct LegacyGameplaySummary
	{
		std::uint32_t uin = 0;
		std::uint32_t timestamp = 0;
		std::uint32_t lapTime = 0;
		std::uint8_t checkpointCount = 0;
		std::uint8_t messageSequence = 0;
		std::size_t bytesConsumed = 0;
	};

	// 精确消费 Build323/当前 21487 的 CSSafeCRequestReportCurrentInfoPara，
	// 再构造并自校验 RequestReportCurrentInfo 当前消费的有界旧格式请求体。
	bool DecodeSafeRequestToLegacy(
		const std::uint8_t* safe,
		std::size_t safeLength,
		std::uint32_t expectedUin,
		std::vector<std::uint8_t>& out,
		LegacyGameplaySummary* summary = nullptr,
	    std::string* error = nullptr);
}

namespace QQSpeedMsg138
{
	struct RequestFields
	{
		std::uint32_t uin = 0;
		std::uint32_t timestamp = 0;
		std::uint32_t taskId = 0;
		std::uint32_t mapId = 0;
		std::uint8_t level = 0;
		std::uint64_t extension17 = 0;
		std::vector<std::uint8_t> arrayValues;
		std::array<std::uint8_t, 3> paddingCounts{};
	};

	// 当前 21487 CSSafeCRequestStartSingleTaskPara。
	// UIN 的 terminal/continuation 变换并非单表双射，必须用 outer UIN
	// 消歧；所有字段、三组 padding 与 EOF 都验证后才构造普通请求体。
	bool DecodeSafeRequestToPlain(
		const std::uint8_t* safe,
		std::size_t safeLength,
		std::uint32_t expectedUin,
		std::vector<std::uint8_t>& out,
		RequestFields* decoded = nullptr,
		std::string* error = nullptr);
}

namespace QQSpeedMsg271
{
	// CSSafeCRequestKartRefitInfoPara -> RequestKartRefitInfo 的
	// Uin(BE32) + Time(BE32) 普通请求体。
	bool DecodeSafeRequestToPlain(
		const std::uint8_t* safe,
		std::size_t safeLength,
		std::vector<std::uint8_t>& out,
		std::string* error = nullptr);
}

namespace QQSpeedMsg24251
{
	// CSSafeCRequestQuickRPCPara（含 Body/Head/Request 三层）-> 普通 QuickRPC 请求体。
	bool DecodeSafeRequestToPlain(
		const std::uint8_t* safe,
		std::size_t safeLength,
		std::vector<std::uint8_t>& out,
		std::string* error = nullptr);
}
