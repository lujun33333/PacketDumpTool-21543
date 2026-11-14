#include "AuroraOfficialUGC.h"

#include <algorithm>
#include <array>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <string_view>
#include <utility>

namespace
{
using QQSpeedAuroraOfficial::BrowseRequest;
using QQSpeedAuroraOfficial::StartGameRequest;
using QQSpeedAuroraOfficial::Work;

constexpr std::uint8_t kMaxFilterStyleTagCount = 3;
constexpr std::uint8_t kMaxWorkIdCount = 20;
constexpr std::size_t kMaxKeywordLength = 64;
constexpr std::uint16_t kAuroraStartType = 4;
constexpr std::size_t kCreatorMinimumLength = 32;
constexpr std::size_t kMaxClientRequestIdLength = 40;
constexpr std::size_t kMaxCreatorNameLength = 64;
constexpr std::size_t kMaxCreatorMd5Length = 64;
constexpr std::size_t kMaxCreatorDescriptionLength = 255;
constexpr std::uint8_t kMaxCreatorThemeCount = 5;
constexpr std::uint8_t kMaxCreatorStyleCount = 3;
constexpr std::uint8_t kUploadObjectCount = 1;
constexpr std::string_view kCreatorQuestionnaireUrl =
    "https://in.weisurvey.com/v2/?sid=6a2b80a776ccfe178b0e6884";

struct CapturedDetailFixture
{
    std::uint64_t workId;
    const char* responseHex;
};

#include "AuroraOfficialUGCFixtures.inc"

std::shared_mutex gRuntimeWorkMutex;
std::map<std::uint64_t, Work> gRuntimeWorks;

void SetError(std::string* error, const char* value)
{
    if (error) *error = value;
}

class Reader
{
public:
    Reader(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size)
    {
    }

    bool U8(std::uint8_t& value)
    {
        if (Remaining() < 1u) return false;
        value = data_[position_++];
        return true;
    }

    bool U16(std::uint16_t& value)
    {
        if (Remaining() < 2u) return false;
        value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(data_[position_]) << 8u) |
            static_cast<std::uint16_t>(data_[position_ + 1u]));
        position_ += 2u;
        return true;
    }

    bool U32(std::uint32_t& value)
    {
        if (Remaining() < 4u) return false;
        value = (static_cast<std::uint32_t>(data_[position_]) << 24u) |
            (static_cast<std::uint32_t>(data_[position_ + 1u]) << 16u) |
            (static_cast<std::uint32_t>(data_[position_ + 2u]) << 8u) |
            static_cast<std::uint32_t>(data_[position_ + 3u]);
        position_ += 4u;
        return true;
    }

    bool I32(std::int32_t& value)
    {
        std::uint32_t encoded = 0;
        if (!U32(encoded)) return false;
        value = static_cast<std::int32_t>(encoded);
        return true;
    }

    bool U64(std::uint64_t& value)
    {
        if (Remaining() < 8u) return false;
        value = 0;
        for (int index = 0; index < 8; ++index)
            value = (value << 8u) | data_[position_ + static_cast<std::size_t>(index)];
        position_ += 8u;
        return true;
    }

    bool Bytes(std::string& value, std::size_t length)
    {
        if (Remaining() < length) return false;
        value.assign(
            reinterpret_cast<const char*>(data_ + position_),
            reinterpret_cast<const char*>(data_ + position_ + length));
        position_ += length;
        return true;
    }

    bool String(std::string& value)
    {
        return String(value, kMaxKeywordLength);
    }

    bool String(std::string& value, std::size_t maximumLength)
    {
        std::uint8_t length = 0;
        if (!U8(length) || length > maximumLength || Remaining() < length)
            return false;
        value.assign(
            reinterpret_cast<const char*>(data_ + position_),
            reinterpret_cast<const char*>(data_ + position_ + length));
        position_ += length;
        return true;
    }

    bool String16(std::string& value, std::size_t maximumLength)
    {
        std::uint16_t length = 0;
        if (!U16(length) || length > maximumLength || Remaining() < length)
            return false;
        value.assign(
            reinterpret_cast<const char*>(data_ + position_),
            reinterpret_cast<const char*>(data_ + position_ + length));
        position_ += length;
        return true;
    }

    std::size_t Remaining() const
    {
        return position_ <= size_ ? size_ - position_ : 0u;
    }

private:
    const std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t position_ = 0;
};

std::uint8_t HexNibble(char value)
{
    if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
    return 0xFF;
}

std::vector<std::uint8_t> DecodeHex(std::string_view hex)
{
    if ((hex.size() % 2u) != 0u) return {};
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2u);
    for (std::size_t index = 0; index < hex.size(); index += 2u)
    {
        const auto high = HexNibble(hex[index]);
        const auto low = HexNibble(hex[index + 1u]);
        if (high > 0x0Fu || low > 0x0Fu) return {};
        out.push_back(static_cast<std::uint8_t>((high << 4u) | low));
    }
    return out;
}

void AppendBE16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 8u));
    out.push_back(static_cast<std::uint8_t>(value));
}

void AppendBE32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24u));
    out.push_back(static_cast<std::uint8_t>(value >> 16u));
    out.push_back(static_cast<std::uint8_t>(value >> 8u));
    out.push_back(static_cast<std::uint8_t>(value));
}

void AppendBE64(std::vector<std::uint8_t>& out, std::uint64_t value)
{
    AppendBE32(out, static_cast<std::uint32_t>(value >> 32u));
    AppendBE32(out, static_cast<std::uint32_t>(value));
}

bool AppendStringU8(
    std::vector<std::uint8_t>& out,
    std::string_view value,
    std::string* error)
{
    if (value.size() > std::numeric_limits<std::uint8_t>::max())
    {
        SetError(error, "string exceeds uint8 wire length");
        return false;
    }
    out.push_back(static_cast<std::uint8_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

bool AppendStringU16(
    std::vector<std::uint8_t>& out,
    std::string_view value,
    std::string* error)
{
    if (value.size() > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "string exceeds uint16 wire length");
        return false;
    }
    AppendBE16(out, static_cast<std::uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

void PatchBE16(
    std::vector<std::uint8_t>& out,
    std::size_t offset,
    std::uint16_t value)
{
    out[offset] = static_cast<std::uint8_t>(value >> 8u);
    out[offset + 1u] = static_cast<std::uint8_t>(value);
}

bool IsHexMd5(std::string_view value)
{
    if (value.size() != 32u) return false;
    return std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'f') ||
            (ch >= 'A' && ch <= 'F');
    });
}

std::string BytesToLowerHex(const std::uint8_t* bytes, std::size_t size)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string value;
    value.resize(size * 2u);
    for (std::size_t index = 0; index < size; ++index)
    {
        value[index * 2u] = kDigits[bytes[index] >> 4u];
        value[index * 2u + 1u] = kDigits[bytes[index] & 0x0Fu];
    }
    return value;
}

bool PatchResponseUin(
    std::vector<std::uint8_t>& response,
    std::uint32_t uin,
    std::string* error)
{
    if (response.size() < 8u)
    {
        SetError(error, "captured response fixture too short");
        return false;
    }
    response[0] = static_cast<std::uint8_t>(uin >> 24u);
    response[1] = static_cast<std::uint8_t>(uin >> 16u);
    response[2] = static_cast<std::uint8_t>(uin >> 8u);
    response[3] = static_cast<std::uint8_t>(uin);
    return true;
}

struct BriefSpan
{
    std::size_t offset;
    std::size_t size;
};

const std::vector<std::uint8_t>& RecommendationBriefs()
{
    static const std::vector<std::uint8_t> value = [] {
        auto captured = DecodeHex(kBrowseRecommendationBriefsHex);
        const auto additional = DecodeHex(kAdditionalRecommendationBriefsHex);
        captured.insert(captured.end(), additional.begin(), additional.end());
        return captured;
    }();
    return value;
}

const std::vector<BriefSpan>& RecommendationBriefSpans()
{
    static const std::vector<BriefSpan> value = [] {
        const auto& briefs = RecommendationBriefs();
        std::vector<BriefSpan> spans;
        spans.reserve(kCapturedRecommendationCount);
        std::size_t offset = 0;
        while (offset + 2u <= briefs.size())
        {
            const std::size_t size =
                (static_cast<std::size_t>(briefs[offset]) << 8u) |
                static_cast<std::size_t>(briefs[offset + 1u]);
            if (size < 13u || offset + size > briefs.size()) return std::vector<BriefSpan>{};
            spans.push_back({offset, size});
            offset += size;
        }
        if (offset != briefs.size() || spans.size() != kCapturedRecommendationCount)
            return std::vector<BriefSpan>{};
        return spans;
    }();
    return value;
}

const std::vector<std::uint8_t>& RecommendationTail()
{
    static const auto value = DecodeHex(kBrowseRecommendationTailHex);
    return value;
}

bool BuildRecommendationResponse(
    const BrowseRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    const auto& briefs = RecommendationBriefs();
    const auto& spans = RecommendationBriefSpans();
    const auto& tail = RecommendationTail();
    if (briefs.empty() || spans.size() != kCapturedRecommendationCount || tail.empty())
    {
        SetError(error, "captured recommendation fixtures are invalid");
        return false;
    }

    const std::size_t pageSize = request.pageSize != 0u
        ? static_cast<std::size_t>(request.pageSize)
        : (request.pageNo == 0u ? kRecommendationFirstPageSize : 9u);
    const std::size_t start = request.pageNo == 0u
        ? 0u
        : kRecommendationFirstPageSize +
            (static_cast<std::size_t>(request.pageNo) - 1u) * pageSize;
    const std::size_t count = start < spans.size()
        ? std::min(pageSize, spans.size() - start)
        : 0u;
    if (count > std::numeric_limits<std::uint8_t>::max())
    {
        SetError(error, "recommendation page count exceeds wire field");
        return false;
    }

    out.clear();
    AppendBE32(out, sessionUin);
    AppendBE32(out, 10u);
    AppendBE32(out, static_cast<std::uint32_t>(kCapturedRecommendationCount));
    out.push_back(static_cast<std::uint8_t>(count));
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& span = spans[start + index];
        out.insert(
            out.end(),
            briefs.begin() + span.offset,
            briefs.begin() + span.offset + span.size);
    }
    out.insert(out.end(), tail.begin(), tail.end());
    SetError(error, "ok");
    return true;
}

bool BuildQualifiedDraftListResponse(
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    AppendBE32(out, sessionUin);
    AppendBE16(out, 0u); // ResultID
    out.push_back(0u);   // ReasonLen
    out.push_back(0u);   // ENABO_ListMyDraft
    AppendBE32(out, 0u); // TotalCount
    out.push_back(0u);   // BriefCount
    out.push_back(0u);   // HasDetail
    out.push_back(0u);   // AuthorBriefCount
    AppendBE32(out, 0u); // LastFetchTime
    out.push_back(0u);   // NotificationCount
    for (int index = 0; index < 4; ++index)
        AppendBE32(out, 0u); // UnreadCountByType
    out.push_back(0u);   // ShowWeeklyTab
    out.push_back(0u);   // WeeklyChallengeNum
    out.push_back(0u);   // ShowActivityTab
    out.push_back(0u);   // ActivityNum
    AppendBE32(out, 0u); // ParticipantCount
    AppendBE32(out, 0u); // TotalPlayCount
    AppendBE32(out, 0u); // TotalLikeCount
    out.push_back(0u);   // CraftsmanLevelNum
    out.push_back(0u);   // StyleTagCfgNum
    AppendBE32(out, 1u); // AreaID
    out.push_back(0u);   // ShowOffTagNum
    AppendBE32(out, 30u); // DraftLimit
    AppendBE32(out, 10u); // PublishLimit
    out.push_back(0u);    // PlayHistoryNum
    AppendBE32(out, 0u);  // RecommendSnapshotVersion
    out.push_back(0u);    // ShowOffDynamicNum
    AppendBE32(out, 0u);  // LightFriendPlayCount
    out.push_back(0u);    // MapDesignQualifyStatus::Qualified
    if (!AppendStringU16(out, kCreatorQuestionnaireUrl, error))
        return false;
    AppendBE16(out, 0u); // SettingsJsonUrlLen
    AppendBE32(out, 0u); // FollowedCreatorTotal
    AppendBE32(out, 0u); // FollowedCreatorLimit
    AppendBE32(out, 0u); // FavoriteWorkLimit
    AppendBE32(out, 0u); // AuthorWorkTotalCount
    AppendBE32(out, 0u); // NextIndex
    if (out.size() != 159u)
    {
        out.clear();
        SetError(error, "qualified draft-list response size drifted");
        return false;
    }
    SetError(error, "ok");
    return true;
}

bool BuildDetailResponse(std::uint64_t workId, std::vector<std::uint8_t>& out)
{
    for (const auto& fixture : kDetailFixtures)
    {
        if (fixture.workId != workId) continue;
        out = DecodeHex(fixture.responseHex);
        return !out.empty();
    }
    return false;
}

bool ReadBE16At(
    const std::vector<std::uint8_t>& data,
    std::size_t offset,
    std::uint16_t& value)
{
    if (offset > data.size() || data.size() - offset < 2u)
        return false;
    value = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[offset]) << 8u) |
        static_cast<std::uint16_t>(data[offset + 1u]));
    return true;
}

bool ReadBE32At(
    const std::vector<std::uint8_t>& data,
    std::size_t offset,
    std::uint32_t& value)
{
    if (offset > data.size() || data.size() - offset < 4u)
        return false;
    value = (static_cast<std::uint32_t>(data[offset]) << 24u) |
        (static_cast<std::uint32_t>(data[offset + 1u]) << 16u) |
        (static_cast<std::uint32_t>(data[offset + 2u]) << 8u) |
        static_cast<std::uint32_t>(data[offset + 3u]);
    return true;
}

bool ReadBE64At(
    const std::vector<std::uint8_t>& data,
    std::size_t offset,
    std::uint64_t& value)
{
    if (offset > data.size() || data.size() - offset < 8u)
        return false;
    value = 0u;
    for (std::size_t index = 0; index < 8u; ++index)
        value = (value << 8u) | data[offset + index];
    return true;
}

bool CapturedTagDetailsMatch(
    const std::vector<std::uint8_t>& detail,
    std::size_t offset,
    const std::vector<std::uint32_t>& tags)
{
    for (const auto tag : tags)
    {
        std::uint16_t nestedSize = 0;
        std::uint32_t nestedTag = 0;
        if (!ReadBE16At(detail, offset, nestedSize) ||
            nestedSize < 7u ||
            offset > detail.size() ||
            nestedSize > detail.size() - offset ||
            !ReadBE32At(detail, offset + 2u, nestedTag) ||
            nestedTag != tag)
        {
            return false;
        }
        offset += nestedSize;
    }
    return true;
}

bool DecodeCapturedRoomAuroraInfo(
    const Work& work,
    const std::vector<std::uint8_t>& detail,
    QQSpeedAuroraOfficial::RoomAuroraInfo& out,
    std::string* error)
{
    constexpr std::size_t kDetailBriefOffset = 16u;
    constexpr std::size_t kBriefFixedPrefixSize = 11u;
    constexpr std::size_t kBriefTailBeforeTagsSize = 46u;
    constexpr std::size_t kBriefTailAfterTagsSize = 13u;
    constexpr std::size_t kStatsBlockSize = 24u;

    out = {};
    std::uint16_t briefSize = 0;
    std::uint64_t detailWorkId = 0;
    if (!ReadBE16At(detail, kDetailBriefOffset, briefSize) ||
        briefSize < kBriefFixedPrefixSize ||
        kDetailBriefOffset > detail.size() ||
        briefSize > detail.size() - kDetailBriefOffset ||
        !ReadBE64At(detail, kDetailBriefOffset + 2u, detailWorkId) ||
        detailWorkId != work.workId)
    {
        SetError(error, "captured Aurora detail brief is invalid");
        return false;
    }

    const std::size_t briefEnd = kDetailBriefOffset + briefSize;
    const std::size_t nameLengthOffset = kDetailBriefOffset + 10u;
    if (nameLengthOffset >= briefEnd)
    {
        SetError(error, "captured Aurora detail name is missing");
        return false;
    }
    const std::size_t nameLength = detail[nameLengthOffset];
    const std::size_t nameOffset = nameLengthOffset + 1u;
    if (nameLength > 64u ||
        nameOffset > briefEnd ||
        nameLength > briefEnd - nameOffset)
    {
        SetError(error, "captured Aurora detail name is invalid");
        return false;
    }

    const std::size_t coverLengthOffset = nameOffset + nameLength;
    std::uint16_t coverLength = 0;
    if (!ReadBE16At(detail, coverLengthOffset, coverLength) ||
        coverLength > 512u)
    {
        SetError(error, "captured Aurora detail cover length is invalid");
        return false;
    }
    const std::size_t coverOffset = coverLengthOffset + 2u;
    if (coverOffset > briefEnd || coverLength > briefEnd - coverOffset)
    {
        SetError(error, "captured Aurora detail cover is truncated");
        return false;
    }

    const std::size_t briefTailOffset = coverOffset + coverLength;
    if (briefTailOffset > briefEnd ||
        briefEnd - briefTailOffset <
            kBriefTailBeforeTagsSize + kBriefTailAfterTagsSize)
    {
        SetError(error, "captured Aurora detail brief tail is truncated");
        return false;
    }
    std::uint16_t tagCount = 0;
    if (!ReadBE16At(detail, briefTailOffset + 44u, tagCount) ||
        tagCount > 5u)
    {
        SetError(error, "captured Aurora detail tag count is invalid");
        return false;
    }
    const std::size_t tagsOffset =
        briefTailOffset + kBriefTailBeforeTagsSize;
    const std::size_t tagsBytes = static_cast<std::size_t>(tagCount) * 4u;
    if (tagsOffset > briefEnd ||
        tagsBytes > briefEnd - tagsOffset ||
        briefEnd - tagsOffset - tagsBytes < kBriefTailAfterTagsSize)
    {
        SetError(error, "captured Aurora detail tags are truncated");
        return false;
    }

    out.workId = work.workId;
    out.auroraNameRaw.assign(
        reinterpret_cast<const char*>(detail.data() + nameOffset),
        nameLength);
    out.version = work.version;
    out.difficulty = detail[briefTailOffset];
    out.maxPlayers = detail[briefTailOffset + 1u];
    out.tags.reserve(tagCount);
    for (std::size_t index = 0; index < tagCount; ++index)
    {
        std::uint32_t tag = 0;
        if (!ReadBE32At(detail, tagsOffset + index * 4u, tag))
        {
            SetError(error, "captured Aurora detail tag is truncated");
            return false;
        }
        out.tags.push_back(tag);
    }
    out.coverUrl.assign(
        reinterpret_cast<const char*>(detail.data() + coverOffset),
        coverLength);
    out.workNameRaw = out.auroraNameRaw;

    const std::size_t briefCountersOffset = tagsOffset + tagsBytes;
    std::uint32_t briefLikeCount = 0;
    std::uint32_t briefPlayCount = 0;
    if (!ReadBE32At(detail, briefCountersOffset, briefLikeCount) ||
        !ReadBE32At(detail, briefCountersOffset + 4u, briefPlayCount))
    {
        SetError(error, "captured Aurora detail brief counters are invalid");
        return false;
    }

    std::size_t statsOffset = detail.size();
    for (std::size_t offset = briefEnd;
         offset <= detail.size() &&
         detail.size() - offset >= kStatsBlockSize;
         ++offset)
    {
        std::uint32_t playCount = 0;
        std::uint32_t likeCount = 0;
        std::uint32_t fullTagCount = 0;
        if (!ReadBE32At(detail, offset, playCount) ||
            !ReadBE32At(detail, offset + 8u, likeCount) ||
            !ReadBE32At(detail, offset + 20u, fullTagCount) ||
            playCount != briefPlayCount ||
            likeCount != briefLikeCount ||
            fullTagCount != tagCount ||
            !CapturedTagDetailsMatch(
                detail, offset + kStatsBlockSize, out.tags))
        {
            continue;
        }
        if (statsOffset != detail.size())
        {
            SetError(error, "captured Aurora detail counters are ambiguous");
            return false;
        }
        statsOffset = offset;
    }
    if (statsOffset == detail.size())
    {
        SetError(error, "captured Aurora detail counters are missing");
        return false;
    }
    if (!ReadBE32At(detail, statsOffset, out.totalPlayCount) ||
        !ReadBE32At(detail, statsOffset + 8u, out.totalLikeCount) ||
        !ReadBE32At(detail, statsOffset + 12u, out.totalFavoriteCount))
    {
        out = {};
        SetError(error, "captured Aurora detail counters are truncated");
        return false;
    }
    SetError(error, "ok");
    return true;
}

const std::vector<std::uint8_t>& Fixture(std::uint8_t operation)
{
    static const auto op12 = DecodeHex(kBrowseOp12ResponseHex);
    static const auto op15 = DecodeHex(kBrowseOp15ResponseHex);
    static const auto op16 = DecodeHex(kBrowseOp16ResponseHex);
    static const std::vector<std::uint8_t> empty;

    switch (operation)
    {
    case 12: return op12;
    case 15: return op15;
    case 16: return op16;
    default:
        return empty;
    }
}
}

const QQSpeedAuroraOfficial::Work* QQSpeedAuroraOfficial::FindWork(
    std::uint64_t workId) noexcept
{
    for (const auto& work : kWorks)
    {
        if (work.workId == workId) return &work;
    }
    thread_local Work runtimeCopy;
    std::shared_lock<std::shared_mutex> lock(gRuntimeWorkMutex);
    const auto runtime = gRuntimeWorks.find(workId);
    if (runtime != gRuntimeWorks.end())
    {
        runtimeCopy = runtime->second;
        return &runtimeCopy;
    }
    return nullptr;
}

std::size_t QQSpeedAuroraOfficial::WorkCount() noexcept
{
    return sizeof(kWorks) / sizeof(kWorks[0]);
}

const QQSpeedAuroraOfficial::Work* QQSpeedAuroraOfficial::WorkAt(
    std::size_t index) noexcept
{
    return index < WorkCount() ? &kWorks[index] : nullptr;
}

void QQSpeedAuroraOfficial::RegisterRuntimeWork(const Work& work)
{
    if (work.workId == 0u || work.ownerUin == 0u ||
        work.mapId == 0u || work.version == 0u ||
        work.renderUrl.empty() || work.md5.size() != 32u)
    {
        return;
    }
    std::unique_lock<std::shared_mutex> lock(gRuntimeWorkMutex);
    gRuntimeWorks[work.workId] = work;
}

std::size_t QQSpeedAuroraOfficial::PayloadLengthFromWireBodyLength(
    std::size_t wireBodyLength) noexcept
{
    constexpr std::size_t kWireLengthWordSize = sizeof(std::uint16_t);
    return wireBodyLength >= kWireLengthWordSize
        ? wireBodyLength - kWireLengthWordSize
        : 0u;
}

bool QQSpeedAuroraOfficial::ParseRoomChangeRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    RoomChangeRequest& out,
    std::string* error)
{
    constexpr std::size_t kNormalPayloadSize = 22u;
    constexpr std::size_t kDynamicInfoSize = 5u;
    constexpr std::size_t kFixedAuroraNameSize = 64u;
    constexpr std::size_t kMinimumAuroraInfoSize = 96u;
    constexpr std::size_t kMaximumAuroraInfoSize = 692u;

    out = {};
    if (!body)
    {
        SetError(error, "room-change request pointer null");
        return false;
    }
    if (bodyLength < kNormalPayloadSize ||
        bodyLength > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "room-change request length outside schema bounds");
        return false;
    }

    Reader reader(body, bodyLength);
    std::uint16_t dynamicSize = 0;
    std::uint8_t season = 0;
    std::uint8_t timeslot = 0;
    std::uint8_t weather = 0;
    std::uint8_t hasAuroraInfo = 0;
    if (!reader.U32(out.uin) ||
        !reader.U32(out.timestamp) ||
        !reader.I32(out.newMapId) ||
        !reader.U16(dynamicSize) ||
        dynamicSize != kDynamicInfoSize ||
        !reader.U8(season) ||
        !reader.U8(timeslot) ||
        !reader.U8(weather) ||
        !reader.I32(out.mapSubId) ||
        !reader.U8(hasAuroraInfo) ||
        hasAuroraInfo > 1u)
    {
        out = {};
        SetError(error, "room-change request fixed fields are invalid");
        return false;
    }
    out.season = static_cast<std::int8_t>(season);
    out.timeslot = static_cast<std::int8_t>(timeslot);
    out.weather = static_cast<std::int8_t>(weather);
    out.hasAuroraInfo = hasAuroraInfo != 0u;

    if (out.hasAuroraInfo)
    {
        const std::size_t nestedBytesBefore = reader.Remaining();
        std::uint16_t nestedSize = 0;
        std::string fixedName;
        std::uint8_t tagCount = 0;
        if (!reader.U16(nestedSize) ||
            nestedSize < kMinimumAuroraInfoSize ||
            nestedSize > kMaximumAuroraInfoSize ||
            nestedSize > nestedBytesBefore ||
            !reader.U64(out.auroraInfo.workId) ||
            !reader.Bytes(fixedName, kFixedAuroraNameSize) ||
            !reader.U32(out.auroraInfo.version) ||
            !reader.U8(out.auroraInfo.difficulty) ||
            !reader.U8(out.auroraInfo.maxPlayers) ||
            !reader.U8(tagCount) ||
            tagCount > 5u)
        {
            out = {};
            SetError(error, "room-change AuroraInfo header is invalid");
            return false;
        }
        const auto terminator = fixedName.find('\0');
        out.auroraInfo.auroraNameRaw.assign(
            fixedName.data(),
            terminator == std::string::npos
                ? fixedName.size()
                : terminator);
        out.auroraInfo.tags.reserve(tagCount);
        for (std::size_t index = 0; index < tagCount; ++index)
        {
            std::uint32_t tag = 0;
            if (!reader.U32(tag))
            {
                out = {};
                SetError(error, "room-change AuroraInfo tags are truncated");
                return false;
            }
            out.auroraInfo.tags.push_back(tag);
        }
        if (!reader.String16(out.auroraInfo.coverUrl, 512u) ||
            !reader.String(out.auroraInfo.workNameRaw, 64u) ||
            !reader.U32(out.auroraInfo.totalPlayCount) ||
            !reader.U32(out.auroraInfo.totalLikeCount) ||
            !reader.U32(out.auroraInfo.totalFavoriteCount) ||
            nestedBytesBefore - reader.Remaining() != nestedSize)
        {
            out = {};
            SetError(error, "room-change AuroraInfo body is invalid");
            return false;
        }
    }
    if (reader.Remaining() != 0u)
    {
        out = {};
        SetError(error, "room-change request contains residual bytes");
        return false;
    }
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::ResolveRoomAuroraInfo(
    std::uint64_t workId,
    RoomAuroraInfo& out,
    std::string* error)
{
    out = {};
    const auto* work = FindWork(workId);
    if (!work)
    {
        SetError(error, "Aurora room work is not in the playable catalog");
        return false;
    }
    std::vector<std::uint8_t> detail;
    if (!BuildDetailResponse(workId, detail))
    {
        SetError(
            error,
            "Aurora room metadata is unavailable for this runtime work");
        return false;
    }
    return DecodeCapturedRoomAuroraInfo(*work, detail, out, error);
}

bool QQSpeedAuroraOfficial::BuildRoomAuroraInfo(
    const RoomAuroraInfo& info,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (info.workId == 0u ||
        info.auroraNameRaw.size() > 64u ||
        info.tags.size() > 5u ||
        info.coverUrl.size() > 512u ||
        info.workNameRaw.size() > 64u)
    {
        SetError(error, "Aurora room metadata exceeds wire bounds");
        return false;
    }

    AppendBE16(out, 0u);
    AppendBE64(out, info.workId);
    out.insert(
        out.end(), info.auroraNameRaw.begin(), info.auroraNameRaw.end());
    out.resize(out.size() + 64u - info.auroraNameRaw.size(), 0u);
    AppendBE32(out, info.version);
    out.push_back(info.difficulty);
    out.push_back(info.maxPlayers);
    out.push_back(static_cast<std::uint8_t>(info.tags.size()));
    for (const auto tag : info.tags)
        AppendBE32(out, tag);
    if (!AppendStringU16(out, info.coverUrl, error) ||
        !AppendStringU8(out, info.workNameRaw, error))
    {
        out.clear();
        return false;
    }
    AppendBE32(out, info.totalPlayCount);
    AppendBE32(out, info.totalLikeCount);
    AppendBE32(out, info.totalFavoriteCount);
    if (out.size() > std::numeric_limits<std::uint16_t>::max())
    {
        out.clear();
        SetError(error, "Aurora room metadata class size overflow");
        return false;
    }
    PatchBE16(out, 0u, static_cast<std::uint16_t>(out.size()));
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::ParseBrowseRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    BrowseRequest& out,
    std::string* error)
{
    out = {};
    if (!body)
    {
        SetError(error, "request pointer null");
        return false;
    }
    if (bodyLength < 44u || bodyLength > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "request length outside schema bounds");
        return false;
    }

    Reader reader(body, bodyLength);
    std::uint8_t filterCount = 0;
    std::uint8_t workIdCount = 0;
    if (!reader.U32(out.uin) ||
        !reader.U32(out.timestamp) ||
        !reader.U8(out.operateType) ||
        !reader.U64(out.workId) ||
        !reader.U16(out.pageNo) ||
        !reader.U8(out.pageSize) ||
        !reader.U8(out.orderBy) ||
        !reader.U8(filterCount) ||
        filterCount > kMaxFilterStyleTagCount)
    {
        out = {};
        SetError(error, "request header truncated");
        return false;
    }

    out.filterStyleTagIds.reserve(filterCount);
    for (std::uint8_t index = 0; index < filterCount; ++index)
    {
        std::uint32_t value = 0;
        if (!reader.U32(value))
        {
            out = {};
            SetError(error, "style tag array truncated");
            return false;
        }
        out.filterStyleTagIds.push_back(value);
    }

    if (!reader.U64(out.targetUin) ||
        !reader.U32(out.targetAreaId) ||
        !reader.String(out.keyword) ||
        !reader.U8(workIdCount) ||
        workIdCount > kMaxWorkIdCount)
    {
        out = {};
        SetError(error, "request target/string/work-id header truncated");
        return false;
    }

    out.workIds.reserve(workIdCount);
    for (std::uint8_t index = 0; index < workIdCount; ++index)
    {
        std::uint64_t value = 0;
        if (!reader.U64(value))
        {
            out = {};
            SetError(error, "work-id array truncated");
            return false;
        }
        out.workIds.push_back(value);
    }

    if (!reader.U8(out.playHistoryFilter) ||
        !reader.U8(out.activityRequestType) ||
        !reader.U8(out.notifyTabFilter) ||
        !reader.U32(out.recommendSnapshotVersion) ||
        !reader.U8(out.detailDataType) ||
        reader.Remaining() != 0u)
    {
        out = {};
        SetError(error, "request tail invalid or has residual bytes");
        return false;
    }

    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::BuildBrowseResponse(
    const BrowseRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (request.uin != sessionUin)
    {
        SetError(error, "request UIN does not match session");
        return false;
    }
    if (request.operateType == 4 && !FindWork(request.workId))
    {
        SetError(error, "work is not present in the captured playable catalog");
        return false;
    }

    if (request.operateType == 10)
        return BuildRecommendationResponse(request, sessionUin, out, error);

    if (request.operateType == 0)
        return BuildQualifiedDraftListResponse(sessionUin, out, error);

    if (request.operateType == 4)
    {
        if (!BuildDetailResponse(request.workId, out))
        {
            SetError(error, "captured detail fixture is invalid");
            return false;
        }
    }
    else
    {
        const auto& fixture = Fixture(request.operateType);
        if (fixture.empty())
        {
            SetError(error, "unsupported Aurora browse operation");
            return false;
        }
        out = fixture;
    }
    if (out.empty())
    {
        SetError(error, "captured Aurora response fixture empty");
        return false;
    }
    if (!PatchResponseUin(out, sessionUin, error))
    {
        out.clear();
        return false;
    }
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::ParseStartGameRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    StartGameRequest& out,
    std::string* error)
{
    out = {};
    if (!body)
    {
        SetError(error, "request pointer null");
        return false;
    }

    if (bodyLength != 30u)
    {
        SetError(error, "28437 request length mismatch");
        return false;
    }

    Reader reader(body, bodyLength);
    if (!reader.U32(out.uin) ||
        !reader.U16(out.startType) ||
        !reader.U32(out.uiPara0) ||
        !reader.U32(out.iPara0) ||
        !reader.U32(out.mapId) ||
        !reader.U32(out.iPara1) ||
        !reader.U64(out.workId) ||
        reader.Remaining() != 0u)
    {
        out = {};
        SetError(error, "28437 request fields truncated or residual bytes present");
        return false;
    }
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::BuildStartGameResponse(
    const StartGameRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    const auto* work = FindWork(request.workId);
    if (request.uin != sessionUin)
    {
        SetError(error, "request UIN does not match session");
        return false;
    }
    if (request.startType != kAuroraStartType || !work)
    {
        SetError(error, "unsupported StartGameLogic profile or work ID");
        return false;
    }

    out.reserve(21u);
    AppendBE32(out, sessionUin);
    AppendBE16(out, 0); // ResultID
    out.push_back(0);   // ReasonLen
    AppendBE16(out, kAuroraStartType);
    AppendBE32(out, request.uiPara0);
    AppendBE32(out, request.iPara0);
    // All three class-size-bounded official 28437 responses carry zero here.
    // Earlier nonzero values were bytes read past the serialized object by the
    // capture hook and must never be replayed.
    AppendBE32(out, 0u);
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::BuildMatchResultResponse(
    const MatchResultInfo& info,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (info.gameServerId == 0u || info.serverIp == 0u ||
        info.tcpPorts.empty() || info.udpPorts.empty() || info.roomId == 0u ||
        info.pvpKey == 0u || info.gameSequenceHigh == 0u ||
        info.gameSequenceLow == 0u ||
        std::any_of(info.tcpPorts.begin(), info.tcpPorts.end(),
            [](std::uint16_t port) { return port == 0u; }) ||
        std::any_of(info.udpPorts.begin(), info.udpPorts.end(),
            [](std::uint16_t port) { return port == 0u; }))
    {
        SetError(error, "Aurora match result requires a complete endpoint and race authority");
        return false;
    }
    if (info.tcpPorts.size() > std::numeric_limits<std::uint8_t>::max() ||
        info.udpPorts.size() > std::numeric_limits<std::uint8_t>::max() ||
        info.multiServerIps.size() > std::numeric_limits<std::uint8_t>::max() ||
        info.domain.size() > std::numeric_limits<std::uint8_t>::max() ||
        info.serverIdentity.size() > std::numeric_limits<std::uint8_t>::max())
    {
        SetError(error, "Aurora match endpoint exceeds wire count or string limits");
        return false;
    }

    out.reserve(128u);
    AppendBE16(out, 0u); // ResultID
    AppendBE16(out, 0u); // ReasonLen
    out.push_back(8u);   // MatchQueueType
    out.push_back(1u);   // HasEnterInfo

    const std::size_t enterLengthOffset = out.size();
    AppendBE16(out, 0u);
    const std::size_t addressLengthOffset = out.size();
    AppendBE16(out, 0u);
    AppendBE16(out, info.gameServerId);
    AppendBE32(out, info.serverIp);
    out.push_back(static_cast<std::uint8_t>(info.tcpPorts.size()));
    for (const auto port : info.tcpPorts) AppendBE16(out, port);
    out.push_back(static_cast<std::uint8_t>(info.udpPorts.size()));
    for (const auto port : info.udpPorts) AppendBE16(out, port);

    const bool hasMultiIpInfo = !info.multiServerIps.empty() || !info.domain.empty();
    out.push_back(hasMultiIpInfo ? 1u : 0u);
    if (hasMultiIpInfo)
    {
        const std::size_t multiLengthOffset = out.size();
        AppendBE16(out, 0u);
        out.push_back(static_cast<std::uint8_t>(info.multiServerIps.size()));
        for (const auto address : info.multiServerIps) AppendBE32(out, address);
        if (!AppendStringU8(out, info.domain, error))
        {
            out.clear();
            return false;
        }
        PatchBE16(
            out, multiLengthOffset,
            static_cast<std::uint16_t>(out.size() - multiLengthOffset));
    }
    if (!AppendStringU8(out, info.serverIdentity, error))
    {
        out.clear();
        return false;
    }
    AppendBE64(out, info.routeId);
    PatchBE16(
        out, addressLengthOffset,
        static_cast<std::uint16_t>(out.size() - addressLengthOffset));
    AppendBE32(out, info.roomId);
    AppendBE16(out, 0u); // SceneID
    PatchBE16(
        out, enterLengthOffset,
        static_cast<std::uint16_t>(out.size() - enterLengthOffset));

    // dump6752 Msg20081: map is carried by 1718/509, while the match routing
    // tuple identifies Aurora's queue-8 solo room as type 19.
    AppendBE32(out, 0u);  // MapID
    AppendBE16(out, 0u);  // MatchedRoomType
    AppendBE16(out, 19u); // TeamMatchType
    out.push_back(1u);    // PVPType
    AppendBE32(out, info.pvpKey);
    AppendBE32(out, info.gameSequenceHigh);
    AppendBE32(out, info.gameSequenceLow);
    AppendBE16(out, 0u);  // ForceTransCount
    AppendBE16(out, 19u); // PlayerMatchingType
    out.push_back(0u);    // ParaNum

    if (out.size() > std::numeric_limits<std::uint16_t>::max())
    {
        out.clear();
        SetError(error, "Aurora match-result payload exceeds class-size wire limit");
        return false;
    }
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::BuildGameBeginTail(
    std::uint64_t workId,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    const auto* work = FindWork(workId);
    if (!work)
    {
        SetError(error, "work is not present in the captured playable catalog");
        return false;
    }
    const std::string_view url(work->renderUrl);
    if (url.size() > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "render URL too long");
        return false;
    }

    out.reserve(15u + url.size());
    AppendBE64(out, workId);
    out.push_back(0); // m_ucAuroraWorkNameLen: zero in both official 509 captures
    AppendBE32(out, work->version);
    AppendBE16(out, static_cast<std::uint16_t>(url.size()));
    out.insert(out.end(), url.begin(), url.end());
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::ParseCreatorRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    CreatorRequest& out,
    std::string* error)
{
    out = {};
    if (!body)
    {
        SetError(error, "creator request pointer null");
        return false;
    }
    if (bodyLength < kCreatorMinimumLength ||
        bodyLength > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "creator request length outside schema bounds");
        return false;
    }

    Reader reader(body, bodyLength);
    CreatorRequest parsed;
    if (!reader.U32(parsed.uin) ||
        !reader.U32(parsed.timestamp) ||
        !reader.U8(parsed.operateType) ||
        !reader.U64(parsed.workId) ||
        !reader.String(parsed.clientRequestId, kMaxClientRequestIdLength) ||
        !reader.String(parsed.nameRaw, kMaxCreatorNameLength) ||
        !reader.U8(parsed.difficulty) ||
        !reader.U8(parsed.maxPlayers) ||
        !reader.U8(parsed.needCover) ||
        !reader.String(parsed.coverMd5, kMaxCreatorMd5Length) ||
        !reader.U8(parsed.needRender) ||
        !reader.String(parsed.renderMd5, kMaxCreatorMd5Length) ||
        !reader.String(parsed.descriptionRaw, kMaxCreatorDescriptionLength))
    {
        SetError(error, "creator request header or string truncated");
        return false;
    }

    std::uint8_t themeCount = 0;
    if (!reader.U8(themeCount) || themeCount > kMaxCreatorThemeCount)
    {
        SetError(error, "creator theme count invalid");
        return false;
    }
    parsed.themeIds.reserve(themeCount);
    for (std::uint8_t index = 0; index < themeCount; ++index)
    {
        std::uint32_t value = 0;
        if (!reader.U32(value))
        {
            SetError(error, "creator theme array truncated");
            return false;
        }
        parsed.themeIds.push_back(value);
    }

    std::uint8_t styleCount = 0;
    if (!reader.U8(styleCount) || styleCount > kMaxCreatorStyleCount)
    {
        SetError(error, "creator style count invalid");
        return false;
    }
    parsed.styleIds.reserve(styleCount);
    for (std::uint8_t index = 0; index < styleCount; ++index)
    {
        std::uint32_t value = 0;
        if (!reader.U32(value))
        {
            SetError(error, "creator style array truncated");
            return false;
        }
        parsed.styleIds.push_back(value);
    }

    if (!reader.I32(parsed.componentCount) || reader.Remaining() != 0u)
    {
        SetError(error, "creator request tail invalid");
        return false;
    }

    out = std::move(parsed);
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::IsValidCreateDraftRequest(
    const CreatorRequest& request,
    std::uint32_t sessionUin) noexcept
{
    return request.uin == sessionUin &&
        request.operateType == kCreatorCreateDraft &&
        request.workId == 0u &&
        !request.nameRaw.empty() &&
        request.componentCount >= 0;
}

bool QQSpeedAuroraOfficial::IsValidSaveDraftRequest(
    const CreatorRequest& request,
    std::uint32_t sessionUin) noexcept
{
    return request.uin == sessionUin &&
        request.operateType == kCreatorSaveDraft &&
        request.workId != 0u &&
        !request.clientRequestId.empty() &&
        IsHexMd5(request.renderMd5) &&
        request.componentCount >= 0;
}

bool QQSpeedAuroraOfficial::IsValidSettingsJsonRequest(
    const CreatorRequest& request,
    std::uint32_t sessionUin) noexcept
{
    return request.uin == sessionUin &&
        request.operateType == kCreatorUploadSettingsJson &&
        request.workId != 0u &&
        !request.clientRequestId.empty() &&
        IsHexMd5(request.renderMd5);
}

bool QQSpeedAuroraOfficial::BuildCreatorResponse(
    std::uint32_t uin,
    std::uint8_t operation,
    std::uint64_t workId,
    std::uint8_t mapState,
    std::uint8_t qualification,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (workId == 0u ||
        (operation != kCreatorCreateDraft &&
            operation != kCreatorSaveDraft &&
            operation != kCreatorUploadSettingsJson))
    {
        SetError(error, "creator response arguments invalid");
        return false;
    }

    out.reserve(18u);
    AppendBE32(out, uin);
    AppendBE16(out, 0u);
    out.push_back(0u);
    out.push_back(operation);
    AppendBE64(out, workId);
    out.push_back(mapState);
    out.push_back(qualification);
    if (out.size() != 18u)
    {
        out.clear();
        SetError(error, "creator response size drifted");
        return false;
    }
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::ParseModuleBrowseRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    ModuleBrowseRequest& out,
    std::string* error)
{
    out = {};
    if (!body || bodyLength != 20u)
    {
        SetError(error, "28480 request length mismatch");
        return false;
    }

    Reader reader(body, bodyLength);
    ModuleBrowseRequest parsed;
    if (!reader.U32(parsed.uin) || !reader.U32(parsed.timestamp))
    {
        SetError(error, "28480 request header truncated");
        return false;
    }
    for (int index = 0; index < 11; ++index)
    {
        std::uint8_t value = 0;
        if (!reader.U8(value))
        {
            SetError(error, "28480 request options truncated");
            return false;
        }
    }
    std::uint8_t pageSize = 0;
    if (!reader.U8(pageSize) || pageSize == 0u || pageSize > 100u ||
        reader.Remaining() != 0u)
    {
        SetError(error, "28480 request page size invalid");
        return false;
    }
    out = parsed;
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::BuildModuleBrowseResponse(
    std::uint32_t uin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    // packet_dumps_4828 declares a 119-byte class. SendToClient adds the
    // two-byte class-size word, so this application payload is 117 bytes.
    out.assign(117u, 0u);
    out[0] = static_cast<std::uint8_t>(uin >> 24u);
    out[1] = static_cast<std::uint8_t>(uin >> 16u);
    out[2] = static_cast<std::uint8_t>(uin >> 8u);
    out[3] = static_cast<std::uint8_t>(uin);
    out[17] = 0x65u;
    out[19] = 0x63u;
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::BuildCosAuthorizationNotify(
    std::uint32_t uin,
    std::uint8_t command,
    std::string_view host,
    std::string_view authInfo,
    std::uint64_t workId,
    std::string_view objectKey,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if ((command != kCosCommandRender &&
            command != kCosCommandSettingsJson) ||
        host.empty() || authInfo.empty() || workId == 0u || objectKey.empty())
    {
        SetError(error, "28414 authorization arguments invalid");
        return false;
    }
    if (host.size() > std::numeric_limits<std::uint8_t>::max() ||
        authInfo.size() > std::numeric_limits<std::uint16_t>::max() ||
        objectKey.size() > std::numeric_limits<std::uint8_t>::max())
    {
        SetError(error, "28414 authorization string exceeds wire field");
        return false;
    }

    AppendBE32(out, uin);
    out.push_back(command);
    if (!AppendStringU8(out, host, error)) return false;
    AppendBE16(out, 0u); // UUID is empty in both captured profiles.
    if (!AppendStringU16(out, authInfo, error)) return false;
    AppendBE32(out, 0u); // Captured sequence.
    // The COS coordination profile serializes the two WorkID words low-first.
    AppendBE32(out, static_cast<std::uint32_t>(workId));
    AppendBE32(out, static_cast<std::uint32_t>(workId >> 32u));
    out.push_back(0u);
    out.push_back(kUploadObjectCount);
    if (!AppendStringU8(out, objectKey, error)) return false;
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::ParseUploadReportRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    UploadReportRequest& out,
    std::string* error)
{
    out = {};
    if (!body || bodyLength < 22u ||
        bodyLength > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "28415 request length outside schema bounds");
        return false;
    }

    Reader reader(body, bodyLength);
    UploadReportRequest parsed;
    std::uint32_t workLow = 0;
    std::uint32_t workHigh = 0;
    std::uint8_t objectCount = 0;
    if (!reader.U32(parsed.uin) ||
        !reader.U32(parsed.timestamp) ||
        !reader.U16(parsed.resultId) ||
        !reader.U8(parsed.command) ||
        !reader.String16(parsed.uuid, 255u) ||
        !reader.String16(parsed.authInfo, 4096u) ||
        !reader.U32(parsed.sequence) ||
        !reader.U32(workLow) ||
        !reader.U32(workHigh) ||
        !reader.U8(objectCount) ||
        objectCount != kUploadObjectCount)
    {
        SetError(error, "28415 request header invalid");
        return false;
    }
    if (parsed.command != kCosCommandRender &&
        parsed.command != kCosCommandSettingsJson)
    {
        SetError(error, "28415 command unsupported");
        return false;
    }
    if (parsed.authInfo.empty())
    {
        SetError(error, "28415 authorization missing");
        return false;
    }

    parsed.workId =
        (static_cast<std::uint64_t>(workHigh) << 32u) | workLow;
    parsed.objectKeys.reserve(objectCount);
    for (std::uint8_t index = 0; index < objectCount; ++index)
    {
        std::string objectKey;
        if (!reader.String(objectKey, 255u) || objectKey.empty())
        {
            SetError(error, "28415 object key invalid");
            return false;
        }
        parsed.objectKeys.push_back(std::move(objectKey));
    }
    if (parsed.workId == 0u || reader.Remaining() != 0u)
    {
        SetError(error, "28415 request tail invalid");
        return false;
    }

    out = std::move(parsed);
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::BuildRenderUploadNotify(
    std::uint32_t uin,
    const RenderUploadNotify& notify,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (notify.workId == 0u || notify.ownerUin == 0u ||
        notify.nameRaw.empty() ||
        notify.nameRaw.size() > kMaxCreatorNameLength ||
        notify.renderUrl.empty() ||
        notify.renderUrl.size() > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "28487 notify arguments invalid");
        return false;
    }

    AppendBE32(out, uin);
    AppendBE32(out, 1u);
    AppendBE64(out, notify.workId);
    out.push_back(0u);
    out.push_back(1u);

    const std::size_t detailStart = out.size();
    AppendBE16(out, 0u);
    AppendBE64(out, notify.workId);
    if (!AppendStringU8(out, notify.nameRaw, error)) return false;
    AppendBE32(out, 1u);
    AppendBE16(out, 0u);
    AppendBE64(out, notify.updatedAtMs);
    out.insert(out.end(), 16u, 0u);
    AppendBE32(out, notify.ownerUin);
    AppendBE16(out, 0u);
    AppendBE64(out, notify.createdAtMs);
    AppendBE32(out, 1u);
    AppendBE64(out, 0u);
    out.push_back(1u);
    AppendBE32(out, 0u);

    const std::size_t detailSize = out.size() - detailStart;
    if (detailSize > std::numeric_limits<std::uint16_t>::max())
    {
        out.clear();
        SetError(error, "28487 detail exceeds wire size");
        return false;
    }
    PatchBE16(out, detailStart, static_cast<std::uint16_t>(detailSize));
    if (!AppendStringU16(out, notify.renderUrl, error)) return false;
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::BuildSettingsJsonUploadNotify(
    std::uint32_t uin,
    std::uint32_t timestamp,
    std::uint64_t workId,
    std::string_view settingsUrl,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (workId == 0u || settingsUrl.empty() ||
        settingsUrl.size() > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "28483 notify arguments invalid");
        return false;
    }

    AppendBE32(out, uin);
    AppendBE32(out, timestamp);
    AppendBE64(out, workId);
    if (!AppendStringU16(out, settingsUrl, error)) return false;
    out.push_back(kCosCommandSettingsJson);
    SetError(error, "ok");
    return true;
}

bool QQSpeedAuroraOfficial::ComputeSha256Hex(
    std::string_view value,
    std::string& out,
    std::string* error)
{
    out.clear();
    if (value.size() > std::numeric_limits<ULONG>::max())
    {
        SetError(error, "SHA-256 input exceeds BCrypt limit");
        return false;
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD propertyLength = 0;
    std::vector<std::uint8_t> hashObject;
    std::array<std::uint8_t, 32> digest{};

    const auto cleanup = [&]() {
        if (hash) BCryptDestroyHash(hash);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    };

    if (BCryptOpenAlgorithmProvider(
            &algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(
            algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
            &propertyLength, 0) < 0 ||
        objectLength == 0u)
    {
        cleanup();
        SetError(error, "BCrypt SHA-256 initialization failed");
        return false;
    }
    hashObject.resize(objectLength);
    if (BCryptCreateHash(
            algorithm, &hash, hashObject.data(), objectLength,
            nullptr, 0, 0) < 0 ||
        BCryptHashData(
            hash,
            reinterpret_cast<PUCHAR>(
                const_cast<char*>(value.data())),
            static_cast<ULONG>(value.size()), 0) < 0 ||
        BCryptFinishHash(
            hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
    {
        cleanup();
        SetError(error, "BCrypt SHA-256 operation failed");
        return false;
    }

    cleanup();
    out = BytesToLowerHex(digest.data(), digest.size());
    SetError(error, "ok");
    return true;
}
