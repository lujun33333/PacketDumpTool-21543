#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace QQSpeedAuroraOfficial
{
inline constexpr std::uint32_t kKissCreatorUin = 0x4B495353u;

struct Work
{
    std::uint64_t workId = 0;
    std::uint32_t ownerUin = 0;
    std::uint32_t mapId = 0;
    std::uint32_t version = 0;
    std::string displayName;
    std::string renderUrl;
    std::string md5;
};

struct BrowseRequest
{
    std::uint32_t uin = 0;
    std::uint32_t timestamp = 0;
    std::uint8_t operateType = 0;
    std::uint64_t workId = 0;
    std::uint16_t pageNo = 0;
    std::uint8_t pageSize = 0;
    std::uint8_t orderBy = 0;
    std::vector<std::uint32_t> filterStyleTagIds;
    std::uint64_t targetUin = 0;
    std::uint32_t targetAreaId = 0;
    std::string keyword;
    std::vector<std::uint64_t> workIds;
    std::uint8_t playHistoryFilter = 0;
    std::uint8_t activityRequestType = 0;
    std::uint8_t notifyTabFilter = 0;
    std::uint32_t recommendSnapshotVersion = 0;
    std::uint8_t detailDataType = 0;
};

struct StartGameRequest
{
    std::uint32_t uin = 0;
    std::uint16_t startType = 0;
    std::uint32_t uiPara0 = 0;
    std::uint32_t iPara0 = 0;
    std::uint32_t mapId = 0;
    std::uint32_t iPara1 = 0;
    std::uint64_t workId = 0;
};

struct MatchResultInfo
{
    std::uint16_t gameServerId = 0;
    std::uint32_t serverIp = 0;
    std::vector<std::uint16_t> tcpPorts;
    std::vector<std::uint16_t> udpPorts;
    std::vector<std::uint32_t> multiServerIps;
    std::string domain;
    std::string serverIdentity;
    std::uint64_t routeId = 0;
    std::uint32_t roomId = 0;
    std::uint32_t pvpKey = 0;
    std::uint32_t gameSequenceHigh = 0;
    std::uint32_t gameSequenceLow = 0;
};

struct RoomAuroraInfo
{
    std::uint64_t workId = 0;
    std::string auroraNameRaw;
    std::uint32_t version = 0;
    std::uint8_t difficulty = 0;
    std::uint8_t maxPlayers = 0;
    std::vector<std::uint32_t> tags;
    std::string coverUrl;
    std::string workNameRaw;
    std::uint32_t totalPlayCount = 0;
    std::uint32_t totalLikeCount = 0;
    std::uint32_t totalFavoriteCount = 0;
};

struct RoomChangeRequest
{
    std::uint32_t uin = 0;
    std::uint32_t timestamp = 0;
    std::int32_t newMapId = 0;
    std::int8_t season = -1;
    std::int8_t timeslot = -1;
    std::int8_t weather = -1;
    std::int32_t mapSubId = 0;
    bool hasAuroraInfo = false;
    RoomAuroraInfo auroraInfo;
};

inline constexpr std::uint8_t kCreatorCreateDraft = 0;
inline constexpr std::uint8_t kCreatorSaveDraft = 1;
inline constexpr std::uint8_t kCreatorUploadSettingsJson = 11;
inline constexpr std::uint8_t kCosCommandRender = 35;
inline constexpr std::uint8_t kCosCommandSettingsJson = 42;

struct CreatorRequest
{
    std::uint32_t uin = 0;
    std::uint32_t timestamp = 0;
    std::uint8_t operateType = 0;
    std::uint64_t workId = 0;
    std::string clientRequestId;
    std::string nameRaw;
    std::uint8_t difficulty = 0;
    std::uint8_t maxPlayers = 0;
    std::uint8_t needCover = 0;
    std::string coverMd5;
    std::uint8_t needRender = 0;
    std::string renderMd5;
    std::string descriptionRaw;
    std::vector<std::uint32_t> themeIds;
    std::vector<std::uint32_t> styleIds;
    std::int32_t componentCount = 0;
};

struct ModuleBrowseRequest
{
    std::uint32_t uin = 0;
    std::uint32_t timestamp = 0;
};

struct UploadReportRequest
{
    std::uint32_t uin = 0;
    std::uint32_t timestamp = 0;
    std::uint16_t resultId = 0;
    std::uint8_t command = 0;
    std::string uuid;
    std::string authInfo;
    std::uint32_t sequence = 0;
    std::uint64_t workId = 0;
    std::vector<std::string> objectKeys;
};

struct RenderUploadNotify
{
    std::uint64_t workId = 0;
    std::uint32_t ownerUin = 0;
    std::string nameRaw;
    std::uint64_t createdAtMs = 0;
    std::uint64_t updatedAtMs = 0;
    std::string renderUrl;
};

// MSGHEAD::BodyLen includes its own two-byte length word, while the Body
// pointer passed to message handlers starts at the first application field.
std::size_t PayloadLengthFromWireBodyLength(
    std::size_t wireBodyLength) noexcept;

bool ParseRoomChangeRequest(
    // Application payload only; exclude MSGHEAD::BodyLen's two-byte overhead.
    const std::uint8_t* body,
    std::size_t bodyLength,
    RoomChangeRequest& out,
    std::string* error = nullptr);

// Resolves the exact room-card metadata from the same captured detail fixture
// exposed by operation 4. Runtime works without complete room metadata fail
// instead of receiving fabricated zero/default fields.
bool ResolveRoomAuroraInfo(
    std::uint64_t workId,
    RoomAuroraInfo& out,
    std::string* error = nullptr);

// Builds the nested AuroraInfo class, including its two-byte class-size word.
bool BuildRoomAuroraInfo(
    const RoomAuroraInfo& info,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);

const Work* FindWork(std::uint64_t workId) noexcept;
std::size_t WorkCount() noexcept;
const Work* WorkAt(std::size_t index) noexcept;
void RegisterRuntimeWork(const Work& work);

bool ParseBrowseRequest(
    // Application payload only; exclude MSGHEAD::BodyLen's two-byte overhead.
    const std::uint8_t* body,
    std::size_t bodyLength,
    BrowseRequest& out,
    std::string* error = nullptr);

// Returns fields after the leading class-size word, ready for SendToClient.
// Supported official operations: 0, 4, 10, 12, 15 and 16.
bool BuildBrowseResponse(
    const BrowseRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);

bool ParseStartGameRequest(
    // Application payload only; exclude MSGHEAD::BodyLen's two-byte overhead.
    const std::uint8_t* body,
    std::size_t bodyLength,
    StartGameRequest& out,
    std::string* error = nullptr);

// Builds CResponseStartGameLogic fields after its class-size word.
bool BuildStartGameResponse(
    const StartGameRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);

// Builds the Aurora queue-8 Msg20081 application payload. The endpoint is
// deployment-specific; queue/match fields follow packet_dumps_6752 exactly.
bool BuildMatchResultResponse(
    const MatchResultInfo& info,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);

// Builds the active Aurora suffix of CNotifyGameBeginPara:
// WorkID, empty WorkName, version, RenderDataCosURL length and URL.
bool BuildGameBeginTail(
    std::uint64_t workId,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);

// Build323 CRequestAuroraCreatorOpPara application fields. The caller must
// remove the two-byte class-size word before parsing.
bool ParseCreatorRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    CreatorRequest& out,
    std::string* error = nullptr);

// Validate the operation's security and persistence invariants. Optional
// metadata fields may vary between compatible clients.
bool IsValidCreateDraftRequest(
    const CreatorRequest& request,
    std::uint32_t sessionUin) noexcept;
bool IsValidSaveDraftRequest(
    const CreatorRequest& request,
    std::uint32_t sessionUin) noexcept;
bool IsValidSettingsJsonRequest(
    const CreatorRequest& request,
    std::uint32_t sessionUin) noexcept;

// CResponseAuroraCreatorOpPara fields after the class-size word.
bool BuildCreatorResponse(
    std::uint32_t uin,
    std::uint8_t operation,
    std::uint64_t workId,
    std::uint8_t mapState,
    std::uint8_t qualification,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);

// Structurally compatible 28480 ModuleBrowse request/response.
bool ParseModuleBrowseRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    ModuleBrowseRequest& out,
    std::string* error = nullptr);
bool BuildModuleBrowseResponse(
    std::uint32_t uin,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);

// 28414/28415 COS coordination fields for commands 35 and 42. UUID and
// sequence metadata in compatible 28415 reports are accepted.
bool BuildCosAuthorizationNotify(
    std::uint32_t uin,
    std::uint8_t command,
    std::string_view host,
    std::string_view authInfo,
    std::uint64_t workId,
    std::string_view objectKey,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);
bool ParseUploadReportRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    UploadReportRequest& out,
    std::string* error = nullptr);

// Successful post-upload notifications. Bytes outside the declared class
// sizes in the hook dump are excluded.
bool BuildRenderUploadNotify(
    std::uint32_t uin,
    const RenderUploadNotify& notify,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);
bool BuildSettingsJsonUploadNotify(
    std::uint32_t uin,
    std::uint32_t timestamp,
    std::uint64_t workId,
    std::string_view settingsUrl,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);

bool ComputeSha256Hex(
    std::string_view value,
    std::string& out,
    std::string* error = nullptr);
}
