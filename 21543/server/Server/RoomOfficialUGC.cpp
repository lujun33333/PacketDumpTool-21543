#include "RoomOfficialUGC.h"

#include <algorithm>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::size_t kBrowseBasePayloadLength = 35u;
    constexpr std::size_t kInteractBasePayloadLength = 34u;
    constexpr std::size_t kPlayerInfoPayloadLength = 23u;
    constexpr std::size_t kMaxTagCount = 32u;
    constexpr std::size_t kMaxReportDescriptionLength = 512u;
    constexpr std::size_t kMaxViewWorkCount = 20u;
    constexpr std::uint16_t kRoomSkinRefreshAction = 73u;

    struct DetailFixture
    {
        std::uint64_t workId;
        const char* responseHex;
    };

    struct PlayerFixture
    {
        std::uint32_t targetUin;
        const char* responseHex;
    };

    struct BrowseRecordFixture
    {
        std::uint64_t workId;
        const char* recordHex;
    };

#include "RoomOfficialUGCFixtures.inc"

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

        bool String(std::string& value)
        {
            std::uint8_t length = 0;
            if (!U8(length) || Remaining() < length) return false;
            value.assign(
                reinterpret_cast<const char*>(data_ + position_),
                reinterpret_cast<const char*>(data_ + position_ + length));
            position_ += length;
            return true;
        }

        bool Bytes(std::size_t length, std::string& value)
        {
            if (Remaining() < length) return false;
            value.assign(
                reinterpret_cast<const char*>(data_ + position_),
                reinterpret_cast<const char*>(data_ + position_ + length));
            position_ += length;
            return true;
        }

        bool Skip(std::size_t length)
        {
            if (Remaining() < length) return false;
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
        return 0xFFu;
    }

    std::vector<std::uint8_t> DecodeHex(std::string_view hex)
    {
        if ((hex.size() % 2u) != 0u) return {};
        std::vector<std::uint8_t> bytes;
        bytes.reserve(hex.size() / 2u);
        for (std::size_t index = 0; index < hex.size(); index += 2u)
        {
            const auto high = HexNibble(hex[index]);
            const auto low = HexNibble(hex[index + 1u]);
            if (high > 0x0Fu || low > 0x0Fu) return {};
            bytes.push_back(static_cast<std::uint8_t>((high << 4u) | low));
        }
        return bytes;
    }

    void PatchBE32(std::vector<std::uint8_t>& value, std::size_t offset, std::uint32_t encoded)
    {
        value[offset] = static_cast<std::uint8_t>(encoded >> 24u);
        value[offset + 1u] = static_cast<std::uint8_t>(encoded >> 16u);
        value[offset + 2u] = static_cast<std::uint8_t>(encoded >> 8u);
        value[offset + 3u] = static_cast<std::uint8_t>(encoded);
    }

    void AppendBE16(std::vector<std::uint8_t>& value, std::uint16_t encoded)
    {
        value.push_back(static_cast<std::uint8_t>(encoded >> 8u));
        value.push_back(static_cast<std::uint8_t>(encoded));
    }

    void AppendBE32(std::vector<std::uint8_t>& value, std::uint32_t encoded)
    {
        value.push_back(static_cast<std::uint8_t>(encoded >> 24u));
        value.push_back(static_cast<std::uint8_t>(encoded >> 16u));
        value.push_back(static_cast<std::uint8_t>(encoded >> 8u));
        value.push_back(static_cast<std::uint8_t>(encoded));
    }

    void AppendBE64(std::vector<std::uint8_t>& value, std::uint64_t encoded)
    {
        for (int shift = 56; shift >= 0; shift -= 8)
            value.push_back(static_cast<std::uint8_t>(encoded >> shift));
    }

    bool ReadBE16At(
        const std::vector<std::uint8_t>& value,
        std::size_t offset,
        std::uint16_t& encoded)
    {
        if (offset > value.size() || value.size() - offset < 2u) return false;
        encoded = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(value[offset]) << 8u) |
            static_cast<std::uint16_t>(value[offset + 1u]));
        return true;
    }

    bool ReadBE32At(
        const std::vector<std::uint8_t>& value,
        std::size_t offset,
        std::uint32_t& encoded)
    {
        if (offset > value.size() || value.size() - offset < 4u) return false;
        encoded = (static_cast<std::uint32_t>(value[offset]) << 24u) |
            (static_cast<std::uint32_t>(value[offset + 1u]) << 16u) |
            (static_cast<std::uint32_t>(value[offset + 2u]) << 8u) |
            static_cast<std::uint32_t>(value[offset + 3u]);
        return true;
    }

    bool ReadBE64At(
        const std::vector<std::uint8_t>& value,
        std::size_t offset,
        std::uint64_t& encoded)
    {
        if (offset > value.size() || value.size() - offset < 8u) return false;
        encoded = 0u;
        for (std::size_t index = 0; index < 8u; ++index)
            encoded = (encoded << 8u) | value[offset + index];
        return true;
    }

    bool PatchOuterUinAndValidate(
        std::vector<std::uint8_t>& response,
        std::uint32_t sessionUin,
        std::uint8_t expectedOperation,
        std::string* error)
    {
        if (response.size() < 8u || response[6] != 0u || response[7] != expectedOperation)
        {
            response.clear();
            SetError(error, "captured response fixture header mismatch");
            return false;
        }
        PatchBE32(response, 0u, sessionUin);
        return true;
    }

    struct CapturedBrowseRecord
    {
        std::uint64_t workId = 0u;
        std::vector<std::uint8_t> bytes;
    };

    bool ExtractCapturedDetailRecord(
        const DetailFixture& fixture,
        CapturedBrowseRecord& record)
    {
        constexpr std::size_t kDetailRecordOffset = 14u;
        record = {};
        const auto detail = DecodeHex(fixture.responseHex);
        std::uint16_t recordLength = 0u;
        std::uint64_t recordWorkId = 0u;
        if (detail.size() < kDetailRecordOffset || detail[7] != 3u ||
            !ReadBE16At(detail, kDetailRecordOffset, recordLength) ||
            recordLength < 10u ||
            kDetailRecordOffset > detail.size() ||
            recordLength > detail.size() - kDetailRecordOffset ||
            !ReadBE64At(detail, kDetailRecordOffset + 2u, recordWorkId) ||
            recordWorkId != fixture.workId)
        {
            return false;
        }
        record.workId = fixture.workId;
        record.bytes.assign(
            detail.begin() + kDetailRecordOffset,
            detail.begin() + kDetailRecordOffset + recordLength);
        return true;
    }

    bool ExtractCapturedBrowseRecord(
        const BrowseRecordFixture& fixture,
        CapturedBrowseRecord& record)
    {
        record = {};
        auto bytes = DecodeHex(fixture.recordHex);
        std::uint16_t recordLength = 0u;
        std::uint64_t recordWorkId = 0u;
        if (!ReadBE16At(bytes, 0u, recordLength) ||
            recordLength != bytes.size() || recordLength < 10u ||
            !ReadBE64At(bytes, 2u, recordWorkId) ||
            recordWorkId != fixture.workId)
        {
            return false;
        }
        record.workId = fixture.workId;
        record.bytes = std::move(bytes);
        return true;
    }

    bool CollectCapturedBrowseRecords(
        std::vector<CapturedBrowseRecord>& records,
        std::string* error)
    {
        records.clear();
        records.reserve(
            sizeof(kRugcDetailFixtures) / sizeof(kRugcDetailFixtures[0]) +
            sizeof(kRugcBrowseOnlyFixtures) / sizeof(kRugcBrowseOnlyFixtures[0]));
        for (const auto& fixture : kRugcDetailFixtures)
        {
            CapturedBrowseRecord record;
            if (!ExtractCapturedDetailRecord(fixture, record))
            {
                SetError(error, "captured work detail cannot form a browse record");
                return false;
            }
            records.push_back(std::move(record));
        }
        for (const auto& fixture : kRugcBrowseOnlyFixtures)
        {
            CapturedBrowseRecord record;
            if (!ExtractCapturedBrowseRecord(fixture, record) ||
                std::any_of(records.begin(), records.end(), [&](const auto& candidate) {
                    return candidate.workId == record.workId;
                }))
            {
                records.clear();
                SetError(error, "captured browse-only work record is invalid or duplicated");
                return false;
            }
            records.push_back(std::move(record));
        }
        return true;
    }

    const CapturedBrowseRecord* FindCapturedBrowseRecord(
        const std::vector<CapturedBrowseRecord>& records,
        std::uint64_t workId)
    {
        const auto found = std::find_if(
            records.begin(), records.end(), [&](const CapturedBrowseRecord& record) {
                return record.workId == workId;
            });
        return found == records.end() ? nullptr : &*found;
    }

    bool AppendCapturedBrowseTail(
        std::uint8_t operation,
        std::size_t recordCount,
        const std::vector<CapturedBrowseRecord>& records,
        std::size_t firstRecord,
        std::vector<std::uint8_t>& out,
        std::string* error)
    {
        const char* tailFixtureHex = operation == 1u
            ? kRugcBrowseOperation1Hex
            : kRugcBrowseOperation13Hex;
        const auto tailFixture = DecodeHex(tailFixtureHex);
        if (tailFixture.size() < 13u)
        {
            SetError(error, "captured browse tail fixture is truncated");
            return false;
        }

        std::size_t cursor = 11u;
        for (std::size_t index = 0; index < tailFixture[10]; ++index)
        {
            std::uint16_t nodeLength = 0u;
            if (!ReadBE16At(tailFixture, cursor, nodeLength) ||
                nodeLength < 2u || cursor > tailFixture.size() ||
                nodeLength > tailFixture.size() - cursor)
            {
                SetError(error, "captured browse record fixture is invalid");
                return false;
            }
            cursor += nodeLength;
        }

        std::uint16_t auxiliaryCount = 0u;
        if (!ReadBE16At(tailFixture, cursor, auxiliaryCount))
        {
            SetError(error, "captured browse auxiliary list is truncated");
            return false;
        }
        cursor += 2u;
        for (std::size_t index = 0; index < auxiliaryCount; ++index)
        {
            std::uint16_t nodeLength = 0u;
            if (!ReadBE16At(tailFixture, cursor, nodeLength) ||
                nodeLength < 2u || cursor > tailFixture.size() ||
                nodeLength > tailFixture.size() - cursor)
            {
                SetError(error, "captured browse auxiliary node is invalid");
                return false;
            }
            cursor += nodeLength;
        }

        if ((recordCount != 0u &&
                (firstRecord > records.size() ||
                    recordCount > records.size() - firstRecord)) ||
            recordCount > std::numeric_limits<std::uint16_t>::max())
        {
            SetError(error, "captured browse page range is invalid");
            return false;
        }
        AppendBE16(out, static_cast<std::uint16_t>(recordCount));
        for (std::size_t index = 0; index < recordCount; ++index)
        {
            AppendBE16(out, 13u);
            AppendBE64(out, records[firstRecord + index].workId);
            out.insert(out.end(), 3u, 0u);
        }
        out.insert(out.end(), tailFixture.begin() + cursor, tailFixture.end());
        return true;
    }

    bool BuildCapturedCatalogPage(
        const QQSpeedRoomUGCOfficial::BrowseRequest& request,
        std::uint32_t sessionUin,
        std::vector<std::uint8_t>& out,
        std::string* error)
    {
        std::vector<CapturedBrowseRecord> records;
        if (!CollectCapturedBrowseRecords(records, error))
        {
            out.clear();
            return false;
        }
        if (records.size() > std::numeric_limits<std::uint16_t>::max())
        {
            SetError(error, "captured RoomUGC catalog exceeds wire total count");
            return false;
        }

        const std::size_t pageSize = static_cast<std::size_t>(request.pageSize);
        const std::size_t firstRecord =
            static_cast<std::size_t>(request.pageIndex) * pageSize;
        const std::size_t recordCount = firstRecord < records.size()
            ? std::min(pageSize, records.size() - firstRecord)
            : 0u;
        if (recordCount > std::numeric_limits<std::uint8_t>::max())
        {
            SetError(error, "RoomUGC page count exceeds wire field");
            return false;
        }

        out.clear();
        AppendBE32(out, sessionUin);
        AppendBE32(out, request.operation);
        AppendBE16(out, static_cast<std::uint16_t>(records.size()));
        out.push_back(static_cast<std::uint8_t>(recordCount));
        for (std::size_t index = 0; index < recordCount; ++index)
        {
            const auto& record = records[firstRecord + index];
            out.insert(out.end(), record.bytes.begin(), record.bytes.end());
        }
        if (!AppendCapturedBrowseTail(
                request.operation, recordCount, records, firstRecord, out, error))
        {
            out.clear();
            return false;
        }
        SetError(error, "ok");
        return true;
    }

    const char* BrowseFixture(std::uint8_t operation)
    {
        switch (operation)
        {
        case 1: return kRugcBrowseOperation1Hex;
        case 2: return kRugcBrowseOperation2Hex;
        case 5: return kRugcBrowseOperation5Hex;
        case 13: return kRugcBrowseOperation13Hex;
        case 15: return kRugcBrowseOperation15Hex;
        default: return nullptr;
        }
    }

    const DetailFixture* FindDetailFixture(std::uint64_t workId)
    {
        for (const auto& fixture : kRugcDetailFixtures)
        {
            if (fixture.workId == workId)
                return &fixture;
        }
        return nullptr;
    }

    bool SkipCapturedNodes(
        const std::vector<std::uint8_t>& value,
        std::size_t& cursor,
        std::size_t count,
        std::size_t limit)
    {
        for (std::size_t index = 0u; index < count; ++index)
        {
            std::uint16_t nodeLength = 0u;
            if (!ReadBE16At(value, cursor, nodeLength) || nodeLength < 2u ||
                cursor > limit || nodeLength > limit - cursor)
            {
                return false;
            }
            cursor += nodeLength;
        }
        return true;
    }

    bool ReadCapturedString8(
        const std::vector<std::uint8_t>& value,
        std::size_t& cursor,
        std::size_t limit,
        std::string& out)
    {
        if (cursor >= limit) return false;
        const std::size_t length = value[cursor++];
        if (cursor > limit || length > limit - cursor) return false;
        out.assign(
            reinterpret_cast<const char*>(value.data() + cursor), length);
        cursor += length;
        return true;
    }

    bool ReadCapturedString16(
        const std::vector<std::uint8_t>& value,
        std::size_t& cursor,
        std::size_t limit,
        std::string& out)
    {
        std::uint16_t length = 0u;
        if (!ReadBE16At(value, cursor, length)) return false;
        cursor += 2u;
        if (cursor > limit || length > limit - cursor) return false;
        out.assign(
            reinterpret_cast<const char*>(value.data() + cursor), length);
        cursor += length;
        return true;
    }

    bool ExtractRoomSkinInfo(
        const DetailFixture& fixture,
        QQSpeedRoomUGCOfficial::RoomSkinInfo& out,
        std::string* error)
    {
        out = {};
        const auto response = DecodeHex(fixture.responseHex);
        if (response.size() < 13u || response[7] != 3u)
        {
            SetError(error, "captured room-skin detail header is invalid");
            return false;
        }

        std::size_t cursor = 11u;
        const std::uint8_t workCount = response[10u];
        if (!SkipCapturedNodes(response, cursor, workCount, response.size()) ||
            cursor >= response.size() || response[cursor++] != 1u)
        {
            SetError(error, "captured room-skin detail node is missing");
            return false;
        }

        const std::size_t detailOffset = cursor;
        std::uint16_t detailLength = 0u;
        if (!ReadBE16At(response, detailOffset, detailLength) || detailLength < 4u ||
            detailOffset > response.size() ||
            detailLength > response.size() - detailOffset)
        {
            SetError(error, "captured room-skin detail envelope is invalid");
            return false;
        }
        const std::size_t detailEnd = detailOffset + detailLength;
        const std::size_t briefOffset = detailOffset + 2u;
        std::uint16_t briefLength = 0u;
        std::uint64_t workId = 0u;
        if (!ReadBE16At(response, briefOffset, briefLength) || briefLength < 15u ||
            briefOffset > detailEnd || briefLength > detailEnd - briefOffset ||
            !ReadBE64At(response, briefOffset + 2u, workId) ||
            workId != fixture.workId)
        {
            SetError(error, "captured room-skin brief envelope is invalid");
            return false;
        }
        const std::size_t briefEnd = briefOffset + briefLength;
        cursor = briefOffset + 14u;
        std::string workName;
        std::string coverCosUrl;
        if (!ReadCapturedString8(response, cursor, briefEnd, workName) ||
            !ReadCapturedString16(response, cursor, briefEnd, coverCosUrl))
        {
            SetError(error, "captured room-skin name or cover URL is invalid");
            return false;
        }

        cursor = detailEnd;
        if (cursor >= response.size())
        {
            SetError(error, "captured room-skin interaction list is missing");
            return false;
        }
        const std::uint8_t interactCount = response[cursor++];
        if (!SkipCapturedNodes(response, cursor, interactCount, response.size()) ||
            cursor > response.size() || response.size() - cursor < 10u)
        {
            SetError(error, "captured room-skin interaction list is invalid");
            return false;
        }
        cursor += 10u; // followed, category, last-fetch-time

        if (cursor >= response.size())
        {
            SetError(error, "captured notification list is missing");
            return false;
        }
        const std::uint8_t notifyCount = response[cursor++];
        if (!SkipCapturedNodes(response, cursor, notifyCount, response.size()) ||
            cursor > response.size() || response.size() - cursor < 16u)
        {
            SetError(error, "captured notification list is invalid");
            return false;
        }
        cursor += 16u;

        if (cursor >= response.size())
        {
            SetError(error, "captured theme-contest flag is missing");
            return false;
        }
        const std::uint8_t themeContestCount = response[cursor++];
        if (!SkipCapturedNodes(response, cursor, themeContestCount, response.size()) ||
            cursor >= response.size())
        {
            SetError(error, "captured theme-contest node is invalid");
            return false;
        }
        const std::uint8_t creatorCount = response[cursor++];
        if (!SkipCapturedNodes(response, cursor, creatorCount, response.size()) ||
            cursor > response.size() || response.size() - cursor < 3u)
        {
            SetError(error, "captured creator list is invalid");
            return false;
        }
        cursor += 2u; // sort type, time range
        const std::uint8_t tagCount = response[cursor++];
        if (cursor > response.size() ||
            static_cast<std::size_t>(tagCount) * 4u > response.size() - cursor)
        {
            SetError(error, "captured tag list is invalid");
            return false;
        }
        cursor += static_cast<std::size_t>(tagCount) * 4u;

        if (cursor >= response.size())
        {
            SetError(error, "captured owner list is missing");
            return false;
        }
        const std::uint8_t ownerCount = response[cursor++];
        if (!SkipCapturedNodes(response, cursor, ownerCount, response.size()) ||
            cursor >= response.size())
        {
            SetError(error, "captured owner list is invalid");
            return false;
        }
        const std::uint8_t activatePriceCount = response[cursor++];
        if (!SkipCapturedNodes(response, cursor, activatePriceCount, response.size()) ||
            cursor > response.size() || response.size() - cursor < 17u)
        {
            SetError(error, "captured activation-price tail is invalid");
            return false;
        }
        cursor += 16u; // using-work ID, draft limit, followed-creator total
        const std::uint8_t statisticsCount = response[cursor++];
        if (!SkipCapturedNodes(response, cursor, statisticsCount, response.size()))
        {
            SetError(error, "captured statistics node is invalid");
            return false;
        }

        std::string viewCosUrl;
        std::string cosPrefix;
        if (!ReadCapturedString16(response, cursor, response.size(), viewCosUrl) ||
            !ReadCapturedString16(response, cursor, response.size(), cosPrefix) ||
            workName.empty() || coverCosUrl.empty() ||
            viewCosUrl.empty() || cosPrefix.empty())
        {
            SetError(error, "captured room-skin runtime assets are incomplete");
            return false;
        }

        out.workId = workId;
        out.workName = std::move(workName);
        out.coverCosUrl = std::move(coverCosUrl);
        out.viewCosUrl = std::move(viewCosUrl);
        out.cosPrefix = std::move(cosPrefix);
        return true;
    }

    bool ReadCapturedPurchasePrice(
        const DetailFixture& fixture,
        std::uint8_t requestedDiscountType,
        std::uint32_t& price,
        std::string* error)
    {
        constexpr std::size_t kDetailOffset = 12u;
        auto response = DecodeHex(fixture.responseHex);
        std::uint16_t detailLength = 0u;
        std::uint16_t briefLength = 0u;
        if (response.size() <= kDetailOffset || response[7] != 3u ||
            !ReadBE16At(response, kDetailOffset, detailLength) ||
            detailLength < 2u || detailLength > response.size() - kDetailOffset ||
            !ReadBE16At(response, kDetailOffset + 2u, briefLength) ||
            briefLength < 2u ||
            briefLength > detailLength - 2u)
        {
            SetError(error, "captured work detail price envelope is invalid");
            return false;
        }

        const std::size_t detailEnd = kDetailOffset + detailLength;
        std::size_t cursor = kDetailOffset + 2u + briefLength;
        std::uint16_t workCosUrlLength = 0u;
        if (!ReadBE16At(response, cursor, workCosUrlLength))
        {
            SetError(error, "captured work detail COS URL length is missing");
            return false;
        }
        cursor += 2u;
        if (cursor > detailEnd || workCosUrlLength > detailEnd - cursor)
        {
            SetError(error, "captured work detail COS URL is truncated");
            return false;
        }
        cursor += workCosUrlLength;
        if (cursor >= detailEnd)
        {
            SetError(error, "captured work detail price list count is missing");
            return false;
        }

        const std::uint8_t priceDetailCount = response[cursor++];
        for (std::uint8_t index = 0u; index < priceDetailCount; ++index)
        {
            std::uint16_t nodeLength = 0u;
            if (!ReadBE16At(response, cursor, nodeLength) || nodeLength < 2u ||
                cursor > detailEnd || nodeLength > detailEnd - cursor)
            {
                SetError(error, "captured work detail price node is invalid");
                return false;
            }
            cursor += nodeLength;
        }

        constexpr std::size_t kPriceScalarLength =
            4u + 4u + 1u + 4u + 4u + 4u + 4u + 4u;
        if (cursor > detailEnd || kPriceScalarLength > detailEnd - cursor)
        {
            SetError(error, "captured work detail price scalars are truncated");
            return false;
        }
        cursor += 8u;
        const std::uint8_t capturedDiscountType = response[cursor++];
        cursor += 4u;
        std::uint32_t totalPrice = 0u;
        std::uint32_t discountPrice = 0u;
        if (!ReadBE32At(response, cursor, totalPrice) || totalPrice == 0u ||
            !ReadBE32At(response, cursor + 12u, discountPrice) ||
            capturedDiscountType != requestedDiscountType)
        {
            SetError(error, "purchase discount does not match captured work detail");
            return false;
        }
        if (discountPrice == 0u)
        {
            SetError(error, "captured work detail has no final purchase price");
            return false;
        }
        price = discountPrice;
        return true;
    }
}

std::size_t QQSpeedRoomUGCOfficial::PayloadLengthFromWireBodyLength(
    std::size_t wireBodyLength) noexcept
{
    return wireBodyLength >= sizeof(std::uint16_t)
        ? wireBodyLength - sizeof(std::uint16_t)
        : 0u;
}

bool QQSpeedRoomUGCOfficial::ParseBrowseRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    BrowseRequest& out,
    std::string* error)
{
    out = {};
    if (!body || bodyLength < kBrowseBasePayloadLength)
    {
        SetError(error, "browse request pointer null or payload too short");
        return false;
    }

    Reader reader(body, bodyLength);
    BrowseRequest parsed;
    std::uint8_t tagCount = 0;
    if (!reader.U32(parsed.uin) ||
        !reader.U32(parsed.timestamp) ||
        !reader.U8(parsed.operation) ||
        !reader.U8(parsed.category) ||
        !reader.U8(parsed.sortType) ||
        !reader.U16(parsed.pageIndex) ||
        !reader.U8(parsed.pageSize) ||
        !reader.U64(parsed.workId) ||
        !reader.String(parsed.keyword) ||
        !reader.U32(parsed.creatorUin) ||
        !reader.U32(parsed.creatorAreaId) ||
        !reader.U8(parsed.notifyTypeFilter) ||
        !reader.U8(parsed.themeTagFilter) ||
        !reader.U8(parsed.timeRange) ||
        !reader.U8(tagCount) ||
        tagCount > kMaxTagCount ||
        reader.Remaining() != static_cast<std::size_t>(tagCount) * 4u)
    {
        SetError(error, "browse request schema truncated or residual length invalid");
        return false;
    }

    parsed.tagFilters.reserve(tagCount);
    for (std::uint8_t index = 0; index < tagCount; ++index)
    {
        std::int32_t value = 0;
        if (!reader.I32(value))
        {
            SetError(error, "browse tag filter array truncated");
            return false;
        }
        parsed.tagFilters.push_back(value);
    }
    if (reader.Remaining() != 0u)
    {
        SetError(error, "browse request has residual bytes");
        return false;
    }

    out = std::move(parsed);
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::BuildBrowseResponse(
    const BrowseRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (request.uin != sessionUin)
    {
        SetError(error, "browse request UIN does not match session");
        return false;
    }

    const char* fixture = nullptr;
    if (request.operation == 3u)
    {
        if (request.workId == 0u)
        {
            SetError(error, "detail request has no work ID");
            return false;
        }
        for (const auto& candidate : kRugcDetailFixtures)
        {
            if (candidate.workId == request.workId)
            {
                fixture = candidate.responseHex;
                break;
            }
        }
        if (!fixture)
        {
            SetError(error, "work detail is not present in the captured playable catalog");
            return false;
        }
    }
    else if (request.operation == 1u || request.operation == 13u)
    {
        if (request.workId != 0u)
        {
            SetError(error, "browse request carries a detail work ID");
            return false;
        }
        return BuildCapturedCatalogPage(request, sessionUin, out, error);
    }
    else
    {
        if (request.workId != 0u)
        {
            SetError(error, "non-detail browse request carries a work ID");
            return false;
        }
        fixture = BrowseFixture(request.operation);
    }

    if (!fixture)
    {
        SetError(error, "unsupported browse operation");
        return false;
    }
    out = DecodeHex(fixture);
    if (out.empty() || !PatchOuterUinAndValidate(out, sessionUin, request.operation, error))
        return false;
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::BuildOwnedBrowseResponse(
    const BrowseRequest& request,
    std::uint32_t sessionUin,
    const std::vector<OwnedWork>& ownedWorks,
    std::uint64_t usingWorkId,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (request.uin != sessionUin || request.operation != 23u ||
        request.workId != 0u)
    {
        SetError(error, "owned-work browse request identity or operation mismatch");
        return false;
    }

    std::vector<CapturedBrowseRecord> capturedRecords;
    if (!CollectCapturedBrowseRecords(capturedRecords, error))
        return false;

    struct OwnedRecord
    {
        OwnedWork owner;
        std::vector<std::uint8_t> record;
    };
    std::vector<OwnedRecord> records;
    records.reserve(ownedWorks.size() + 1u);
	const bool hasCapturedPurchasedState =
		std::any_of(ownedWorks.begin(), ownedWorks.end(), [](const OwnedWork& owned) {
			return owned.workId == kRugcCapturedPurchasedWorkId;
		});
    for (const auto& owned : ownedWorks)
    {
        if (owned.workId == 0u || owned.workId == kRugcDefaultOwnedWorkId ||
            owned.purchaseTime == 0u ||
            std::any_of(records.begin(), records.end(), [&](const OwnedRecord& record) {
                return record.owner.workId == owned.workId;
            }))
        {
            SetError(error, "owned-work database row is invalid or duplicated");
            return false;
        }
        std::vector<std::uint8_t> recordBytes;
        if (owned.workId == kRugcCapturedPurchasedWorkId)
        {
            CapturedBrowseRecord capturedOwned;
            const BrowseRecordFixture purchasedFixture = {
                owned.workId,
                kRugcPurchased1073796097OwnedWorkRecordHex,
            };
            if (ExtractCapturedBrowseRecord(purchasedFixture, capturedOwned))
                recordBytes = std::move(capturedOwned.bytes);
        }
        else
        {
            const auto* captured = FindCapturedBrowseRecord(capturedRecords, owned.workId);
            if (captured)
                recordBytes = captured->bytes;
        }
        if (recordBytes.empty())
        {
            SetError(error, "owned work is absent from the captured catalog");
            return false;
        }
        records.push_back({ owned, std::move(recordBytes) });
    }

    CapturedBrowseRecord defaultRecord;
    const BrowseRecordFixture defaultFixture = {
        kRugcDefaultOwnedWorkId,
        hasCapturedPurchasedState
			? kRugcPurchasedDefaultOwnedWorkRecordHex
			: kRugcDefaultOwnedWorkRecordHex,
    };
    if (!ExtractCapturedBrowseRecord(defaultFixture, defaultRecord))
    {
        SetError(error, "captured default owned-work record is invalid");
        return false;
    }
    records.push_back({
        { kRugcDefaultOwnedWorkId, kRugcDefaultOwnedPurchaseTime, 0u, 0u },
        std::move(defaultRecord.bytes),
    });

    if (records.size() > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "owned-work total exceeds the wire field");
        return false;
    }
    if (usingWorkId != 0u &&
        std::none_of(ownedWorks.begin(), ownedWorks.end(), [&](const OwnedWork& owned) {
            return owned.workId == usingWorkId;
        }))
    {
        SetError(error, "equipped work is not in the owned-work database rows");
        return false;
    }

    const std::size_t pageSize = static_cast<std::size_t>(request.pageSize);
    const std::size_t firstRecord =
        static_cast<std::size_t>(request.pageIndex) * pageSize;
    const std::size_t recordCount = firstRecord < records.size()
        ? std::min(pageSize, records.size() - firstRecord)
        : 0u;
    if (recordCount > std::numeric_limits<std::uint8_t>::max())
    {
        SetError(error, "owned-work page count exceeds the wire field");
        return false;
    }

    AppendBE32(out, sessionUin);
    AppendBE16(out, 0u);
    out.push_back(0u);
    out.push_back(request.operation);
    AppendBE16(out, static_cast<std::uint16_t>(records.size()));
    out.push_back(static_cast<std::uint8_t>(recordCount));
    for (std::size_t index = 0u; index < recordCount; ++index)
    {
        const auto& record = records[firstRecord + index];
        out.insert(out.end(), record.record.begin(), record.record.end());
    }

    out.push_back(0u); // work detail
    out.push_back(0u); // interaction status
    out.push_back(0u); // followed
    out.push_back(0u); // response category
    AppendBE64(out, 0u); // last fetch time
    out.push_back(0u); // notifications
    for (int index = 0; index < 4; ++index) AppendBE32(out, 0u);
    out.push_back(0u); // theme contest
    out.push_back(0u); // creators
    out.push_back(0u); // sort type
    out.push_back(0u); // time range
    out.push_back(0u); // tags

    out.push_back(static_cast<std::uint8_t>(recordCount));
    for (std::size_t index = 0u; index < recordCount; ++index)
    {
        const auto& owner = records[firstRecord + index].owner;
        AppendBE16(out, 16u);
        AppendBE64(out, owner.workId);
        AppendBE32(out, owner.purchaseTime);
        out.push_back(owner.ownerSideStatus);
        out.push_back(owner.purchaseSource);
    }

    out.push_back(0u); // activation price
    AppendBE64(out, usingWorkId);
    AppendBE32(out, 0u); // draft limit
    AppendBE32(out, 0u); // followed creator total
    out.push_back(0u); // statistics
    AppendBE16(out, 0u); // view COS URL
    AppendBE16(out, 0u); // COS prefix
    AppendBE64(out, 0u); // last subscription view time
    AppendBE32(out, 0u); // self-activate discount rate
    AppendBE32(out, 0u); // listed limit
    AppendBE32(out, 0u); // delisted limit
    out.push_back(0u); // active flags
    AppendBE32(out, 0u); // 21543 response tail

    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::ParseInteractRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    InteractRequest& out,
    std::string* error)
{
    out = {};
    if (!body || bodyLength < kInteractBasePayloadLength)
    {
        SetError(error, "interact request pointer null or payload too short");
        return false;
    }
    Reader reader(body, bodyLength);
    InteractRequest parsed;
    std::uint16_t reportDescriptionLength = 0u;
    std::uint8_t viewWorkCount = 0u;
    if (!reader.U32(parsed.uin) || !reader.U32(parsed.timestamp) ||
        !reader.U8(parsed.operation) || !reader.U64(parsed.workId) ||
        !reader.U8(parsed.discountType) ||
        !reader.U32(parsed.targetCreatorUin) ||
        !reader.U32(parsed.targetCreatorAreaId) ||
        !reader.U8(parsed.category) || !reader.I32(parsed.reportType) ||
        !reader.U16(reportDescriptionLength) ||
        reportDescriptionLength > kMaxReportDescriptionLength ||
        !reader.Bytes(reportDescriptionLength, parsed.reportDescription) ||
        !reader.U8(viewWorkCount) || viewWorkCount > kMaxViewWorkCount ||
        reader.Remaining() != static_cast<std::size_t>(viewWorkCount) * 8u)
    {
        SetError(error, "interact request schema truncated or residual length invalid");
        return false;
    }

    parsed.viewWorkIds.reserve(viewWorkCount);
    for (std::uint8_t index = 0u; index < viewWorkCount; ++index)
    {
        std::uint64_t workId = 0u;
        if (!reader.U64(workId) || workId == 0u)
        {
            SetError(error, "interact viewed-work array is invalid");
            return false;
        }
        parsed.viewWorkIds.push_back(workId);
    }
    if (reader.Remaining() != 0u)
    {
        SetError(error, "interact request has residual bytes");
        return false;
    }
    out = parsed;
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::BuildInteractResponse(
    const InteractRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (request.uin != sessionUin || request.operation != 12u ||
        request.workId != 0u || request.discountType != 0u ||
        request.targetCreatorUin != 0u || request.targetCreatorAreaId != 0u ||
        request.reportType != 0 ||
        !request.reportDescription.empty() || request.viewWorkIds.empty() ||
        request.viewWorkIds.size() > kMaxViewWorkCount)
    {
        SetError(error, "interact request identity/operation mismatch");
        return false;
    }
    out = DecodeHex(kRugcInteractOperation12Hex);
    if (out.empty() || !PatchOuterUinAndValidate(out, sessionUin, request.operation, error))
        return false;
    out[48u] = request.category;
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::GetCapturedPurchasePrice(
    const InteractRequest& request,
    std::uint32_t& price,
    std::string* error)
{
    price = 0u;
    if (request.operation != 0u || request.workId == 0u ||
        request.targetCreatorUin != 0u || request.targetCreatorAreaId != 0u ||
        request.category != 0u || request.reportType != 0 ||
        !request.reportDescription.empty() || !request.viewWorkIds.empty())
    {
        SetError(error, "purchase request fields do not match the official schema");
        return false;
    }
    const DetailFixture* fixture = FindDetailFixture(request.workId);
    if (!fixture)
    {
        SetError(error, "purchase work is not present in the captured playable catalog");
        return false;
    }
    if (!ReadCapturedPurchasePrice(*fixture, request.discountType, price, error))
        return false;
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::BuildPurchaseResponse(
    const InteractRequest& request,
    std::uint32_t sessionUin,
    std::uint32_t paidPrice,
    std::uint32_t remainingBalance,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    std::uint32_t capturedPrice = 0u;
    if (request.uin != sessionUin || paidPrice == 0u ||
        !GetCapturedPurchasePrice(request, capturedPrice, error))
    {
        return false;
    }
    if (capturedPrice != paidPrice)
    {
        SetError(error, "purchase response price does not match captured detail");
        return false;
    }

    out.reserve(49u);
    AppendBE32(out, sessionUin);
    AppendBE16(out, 0u);
    out.push_back(0u);
    out.push_back(request.operation);
    AppendBE64(out, request.workId);
    AppendBE32(out, paidPrice);
    AppendBE32(out, remainingBalance);
    AppendBE32(out, 0u);
    AppendBE32(out, 0u);
    AppendBE64(out, 0u);
    AppendBE32(out, request.targetCreatorUin);
    AppendBE32(out, request.targetCreatorAreaId);
    out.push_back(request.category);
    if (out.size() != 49u)
    {
        out.clear();
        SetError(error, "purchase response schema length mismatch");
        return false;
    }
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::BuildUseStateResponse(
    const InteractRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    const bool equip = request.operation == 7u;
    const bool unequip = request.operation == 8u;
    if (request.uin != sessionUin || (!equip && !unequip) ||
        request.discountType != 0u || request.targetCreatorUin != 0u ||
        request.targetCreatorAreaId != 0u || request.category != 0u ||
        request.reportType != 0 ||
        !request.reportDescription.empty() || !request.viewWorkIds.empty() ||
        (equip && request.workId == 0u) || (unequip && request.workId != 0u))
    {
        SetError(error, "room-skin use-state request fields do not match the official schema");
        return false;
    }

    out.reserve(49u);
    AppendBE32(out, sessionUin);
    AppendBE16(out, 0u);
    out.push_back(0u);
    out.push_back(request.operation);
    AppendBE64(out, request.workId);
    AppendBE32(out, 0u);
    AppendBE32(out, 0u);
    AppendBE32(out, 0u);
    AppendBE32(out, 0u);
    AppendBE64(out, 0u);
    AppendBE32(out, request.targetCreatorUin);
    AppendBE32(out, request.targetCreatorAreaId);
    out.push_back(request.category);
    if (out.size() != 49u)
    {
        out.clear();
        SetError(error, "room-skin use-state response schema length mismatch");
        return false;
    }
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::BuildDeleteOwnedWorkResponse(
    const InteractRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (request.uin != sessionUin || request.operation != 10u ||
        request.workId == 0u || request.discountType != 0u ||
        request.targetCreatorUin != 0u || request.targetCreatorAreaId != 0u ||
        request.reportType != 0 || !request.reportDescription.empty() ||
        !request.viewWorkIds.empty())
    {
        SetError(error, "delete-owned-work request fields do not match the client schema");
        return false;
    }

    out.reserve(49u);
    AppendBE32(out, sessionUin);
    AppendBE16(out, 0u);
    out.push_back(0u);
    out.push_back(request.operation);
    AppendBE64(out, request.workId);
    AppendBE32(out, 0u);
    AppendBE32(out, 0u);
    AppendBE32(out, 0u);
    AppendBE32(out, 0u);
    AppendBE64(out, 0u);
    AppendBE32(out, request.targetCreatorUin);
    AppendBE32(out, request.targetCreatorAreaId);
    out.push_back(request.category);
    if (out.size() != 49u)
    {
        out.clear();
        SetError(error, "delete-owned-work response schema length mismatch");
        return false;
    }
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::GetRoomSkinInfo(
    std::uint64_t workId,
    RoomSkinInfo& out,
    std::string* error)
{
    out = {};
    const DetailFixture* fixture = FindDetailFixture(workId);
    if (!fixture)
    {
        SetError(error, "room skin is absent from the captured detail catalog");
        return false;
    }
    if (!ExtractRoomSkinInfo(*fixture, out, error))
        return false;
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::BuildRoomSkinRefreshNotify(
    std::uint32_t recipientUin,
    std::uint32_t equipperUin,
    const RoomSkinInfo& roomSkin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (recipientUin == 0u || equipperUin == 0u || roomSkin.workId == 0u ||
        roomSkin.workName.empty() || roomSkin.coverCosUrl.empty() ||
        roomSkin.viewCosUrl.empty() || roomSkin.cosPrefix.empty() ||
        roomSkin.workName.size() > std::numeric_limits<std::uint8_t>::max() ||
        roomSkin.coverCosUrl.size() > std::numeric_limits<std::uint16_t>::max() ||
        roomSkin.viewCosUrl.size() > std::numeric_limits<std::uint16_t>::max() ||
        roomSkin.cosPrefix.size() > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "room-skin refresh fields are invalid");
        return false;
    }

    std::vector<std::uint8_t> node;
    AppendBE16(node, 0u);
    AppendBE64(node, roomSkin.workId);
    AppendBE32(node, equipperUin);
    AppendBE16(node, static_cast<std::uint16_t>(roomSkin.coverCosUrl.size()));
    node.insert(node.end(), roomSkin.coverCosUrl.begin(), roomSkin.coverCosUrl.end());
    node.push_back(static_cast<std::uint8_t>(roomSkin.workName.size()));
    node.insert(node.end(), roomSkin.workName.begin(), roomSkin.workName.end());
    AppendBE16(node, static_cast<std::uint16_t>(roomSkin.viewCosUrl.size()));
    node.insert(node.end(), roomSkin.viewCosUrl.begin(), roomSkin.viewCosUrl.end());
    AppendBE16(node, static_cast<std::uint16_t>(roomSkin.cosPrefix.size()));
    node.insert(node.end(), roomSkin.cosPrefix.begin(), roomSkin.cosPrefix.end());
    if (node.size() > std::numeric_limits<std::uint16_t>::max())
    {
        SetError(error, "room-skin refresh node exceeds the wire field");
        return false;
    }
    node[0u] = static_cast<std::uint8_t>(node.size() >> 8u);
    node[1u] = static_cast<std::uint8_t>(node.size());

    AppendBE32(out, recipientUin);
    AppendBE16(out, 0u);
    AppendBE16(out, kRoomSkinRefreshAction);
    AppendBE32(out, 0u);
    AppendBE32(out, 0u);
    AppendBE16(out, 1u);
    out.insert(out.end(), node.begin(), node.end());
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::MarkDetailResponseOwned(
    std::vector<std::uint8_t>& response,
    std::uint64_t workId,
    std::string* error)
{
    constexpr std::size_t kDetailOffset = 12u;
    std::uint16_t detailLength = 0u;
    std::uint64_t responseWorkId = 0u;
    if (response.size() <= kDetailOffset || response[7] != 3u ||
        !ReadBE16At(response, kDetailOffset, detailLength) ||
        detailLength < 4u || detailLength > response.size() - kDetailOffset ||
        !ReadBE64At(response, kDetailOffset + 4u, responseWorkId) ||
        responseWorkId != workId)
    {
        SetError(error, "detail response envelope does not match purchased work");
        return false;
    }

    const std::size_t interactCountOffset = kDetailOffset + detailLength;
    if (interactCountOffset >= response.size())
    {
        SetError(error, "detail response interaction list count is missing");
        return false;
    }
    const std::uint8_t interactCount = response[interactCountOffset];
    std::size_t cursor = interactCountOffset + 1u;
    for (std::uint8_t index = 0u; index < interactCount; ++index)
    {
        std::uint16_t nodeLength = 0u;
        std::uint64_t nodeWorkId = 0u;
        if (!ReadBE16At(response, cursor, nodeLength) || nodeLength < 13u ||
            cursor > response.size() || nodeLength > response.size() - cursor ||
            !ReadBE64At(response, cursor + 2u, nodeWorkId))
        {
            SetError(error, "detail response interaction node is invalid");
            return false;
        }
        if (nodeWorkId == workId)
        {
            response[cursor + 12u] = 1u;
            SetError(error, "ok");
            return true;
        }
        cursor += nodeLength;
    }
    if (interactCount == std::numeric_limits<std::uint8_t>::max())
    {
        SetError(error, "detail response interaction list is full");
        return false;
    }

    std::vector<std::uint8_t> ownedStatus;
    ownedStatus.reserve(13u);
    AppendBE16(ownedStatus, 13u);
    AppendBE64(ownedStatus, workId);
    ownedStatus.push_back(0u);
    ownedStatus.push_back(0u);
    ownedStatus.push_back(1u);
    response.insert(response.begin() + static_cast<std::ptrdiff_t>(cursor),
        ownedStatus.begin(), ownedStatus.end());
    response[interactCountOffset] = static_cast<std::uint8_t>(interactCount + 1u);
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::ParsePlayerInfoRequest(
    const std::uint8_t* body,
    std::size_t bodyLength,
    PlayerInfoRequest& out,
    std::string* error)
{
    out = {};
    if (!body || bodyLength != kPlayerInfoPayloadLength)
    {
        SetError(error, "player-info request payload length mismatch");
        return false;
    }

    Reader reader(body, bodyLength);
    PlayerInfoRequest parsed;
    std::uint8_t entryCount = 0;
    std::uint32_t reserved = 0;
    std::uint8_t tail = 0;
    if (!reader.U32(parsed.uin) || !reader.U32(parsed.timestamp) ||
        !reader.U8(parsed.operation) || !reader.U8(entryCount) ||
        !reader.U32(reserved) || !reader.U32(parsed.targetUin) ||
        !reader.U32(parsed.targetAreaId) || !reader.U8(tail) ||
        reader.Remaining() != 0u)
    {
        SetError(error, "player-info request schema truncated");
        return false;
    }
    if (parsed.operation == 0u && entryCount != 1u)
    {
        SetError(error, "player-info operation-0 entry count mismatch");
        return false;
    }
    out = parsed;
    SetError(error, "ok");
    return true;
}

bool QQSpeedRoomUGCOfficial::BuildPlayerInfoResponse(
    const PlayerInfoRequest& request,
    std::uint32_t sessionUin,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (request.uin != sessionUin)
    {
        SetError(error, "player-info request UIN does not match session");
        return false;
    }

    const char* fixture = nullptr;
    if (request.operation == 6u)
    {
        fixture = kRugcPlayerOperation6Hex;
    }
    else if (request.operation == 0u)
    {
        for (const auto& candidate : kRugcPlayerFixtures)
        {
            if (candidate.targetUin == request.targetUin)
            {
                fixture = candidate.responseHex;
                break;
            }
        }
    }

    if (!fixture)
    {
        SetError(error, "player-info target/operation is not captured");
        return false;
    }
    out = DecodeHex(fixture);
    if (out.empty() || !PatchOuterUinAndValidate(out, sessionUin, request.operation, error))
        return false;

    SetError(error, "ok");
    return true;
}
