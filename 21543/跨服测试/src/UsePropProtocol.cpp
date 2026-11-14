#include "UsePropProtocol.h"

#include <limits>
#include <utility>

namespace qqspeed::useprop
{
namespace
{
constexpr std::size_t kNotify517BasePayloadLength = 53;
constexpr std::size_t kMaxPayloadLength =
    static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)()) - 2u;
constexpr std::size_t kMaxCount =
    static_cast<std::size_t>((std::numeric_limits<std::uint8_t>::max)());

bool Fail(std::string* error, const char* message)
{
    if (error != nullptr)
    {
        *error = message;
    }
    return false;
}

class Reader
{
public:
    Reader(const std::uint8_t* data, std::size_t length)
        : current_(data), remaining_(length)
    {
    }

    bool Read8(std::uint8_t& value)
    {
        if (remaining_ < 1)
        {
            return false;
        }
        value = *current_++;
        --remaining_;
        return true;
    }

    bool Read16(std::uint16_t& value)
    {
        if (remaining_ < 2)
        {
            return false;
        }
        value = static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(current_[0]) << 8) |
            static_cast<std::uint16_t>(current_[1]));
        current_ += 2;
        remaining_ -= 2;
        return true;
    }

    bool Read32(std::uint32_t& value)
    {
        if (remaining_ < 4)
        {
            return false;
        }
        value =
            (static_cast<std::uint32_t>(current_[0]) << 24) |
            (static_cast<std::uint32_t>(current_[1]) << 16) |
            (static_cast<std::uint32_t>(current_[2]) << 8) |
            static_cast<std::uint32_t>(current_[3]);
        current_ += 4;
        remaining_ -= 4;
        return true;
    }

    bool ReadSigned32(std::int32_t& value)
    {
        std::uint32_t raw = 0;
        if (!Read32(raw))
        {
            return false;
        }
        value = raw <= static_cast<std::uint32_t>(
                          (std::numeric_limits<std::int32_t>::max)())
            ? static_cast<std::int32_t>(raw)
            : -1 - static_cast<std::int32_t>(~raw);
        return true;
    }

    std::size_t Remaining() const
    {
        return remaining_;
    }

private:
    const std::uint8_t* current_;
    std::size_t remaining_;
};

bool ReadUint32List(
    Reader& reader,
    std::vector<std::uint32_t>& values,
    const char* truncatedMessage,
    std::string* error)
{
    std::uint8_t count = 0;
    if (!reader.Read8(count))
    {
        return Fail(error, truncatedMessage);
    }
    if (reader.Remaining() < static_cast<std::size_t>(count) * 4u)
    {
        return Fail(error, truncatedMessage);
    }

    values.clear();
    values.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
    {
        std::uint32_t value = 0;
        if (!reader.Read32(value))
        {
            return Fail(error, truncatedMessage);
        }
        values.push_back(value);
    }
    return true;
}

bool ReadOtherPropPositions(
    Reader& reader,
    std::vector<OtherPropPosition>& records,
    std::string* error)
{
    std::uint8_t count = 0;
    if (!reader.Read8(count))
    {
        return Fail(error, "Msg165 is truncated at OtherPropPosNum");
    }

    records.clear();
    records.reserve(count);
    for (std::size_t recordIndex = 0; recordIndex < count; ++recordIndex)
    {
        std::uint16_t recordLength = 0;
        if (!reader.Read16(recordLength))
        {
            return Fail(error, "Msg165 is truncated at OtherPropPos length");
        }
        if (recordLength < 6 || ((recordLength - 6u) % 4u) != 0)
        {
            return Fail(error, "Msg165 has an invalid OtherPropPos length");
        }
        if (reader.Remaining() < static_cast<std::size_t>(recordLength) - 2u)
        {
            return Fail(error, "Msg165 has a truncated OtherPropPos record");
        }

        OtherPropPosition record;
        if (!reader.Read32(record.index))
        {
            return Fail(error, "Msg165 is truncated at OtherPropPos index");
        }
        const std::size_t positionCount =
            (static_cast<std::size_t>(recordLength) - 6u) / 4u;
        record.positions.reserve(positionCount);
        for (std::size_t positionIndex = 0;
             positionIndex < positionCount;
             ++positionIndex)
        {
            std::int32_t position = 0;
            if (!reader.ReadSigned32(position))
            {
                return Fail(error, "Msg165 is truncated in OtherPropPos positions");
            }
            record.positions.push_back(position);
        }
        records.push_back(std::move(record));
    }
    return true;
}

void Write8(std::vector<std::uint8_t>& out, std::uint8_t value)
{
    out.push_back(value);
}

void Write16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void Write32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void WriteSigned32(std::vector<std::uint8_t>& out, std::int32_t value)
{
    Write32(out, static_cast<std::uint32_t>(value));
}

bool AddSize(std::size_t amount, std::size_t& total, std::string* error)
{
    if (amount > kMaxPayloadLength - total)
    {
        return Fail(error, "Msg517 payload exceeds the 16-bit wire BodyLen");
    }
    total += amount;
    return true;
}

bool CheckCount(std::size_t count, const char* message, std::string* error)
{
    if (count > kMaxCount)
    {
        return Fail(error, message);
    }
    return true;
}
} // namespace

bool ParseRequest165(
    const std::uint8_t* payload,
    std::size_t payloadLength,
    Request165Flavor flavor,
    Request165& out,
    std::string* error)
{
    if (error != nullptr)
    {
        error->clear();
    }
    if (payload == nullptr && payloadLength != 0)
    {
        return Fail(error, "Msg165 payload pointer is null");
    }

    Request165 parsed;
    Reader reader(payload, payloadLength);
    if (!reader.Read32(parsed.uin) ||
        !reader.Read32(parsed.time) ||
        !reader.Read16(parsed.propId) ||
        !reader.Read32(parsed.dstUin) ||
        !reader.ReadSigned32(parsed.currentPosition[0]) ||
        !reader.ReadSigned32(parsed.currentPosition[1]) ||
        !reader.ReadSigned32(parsed.currentPosition[2]) ||
        !reader.Read32(parsed.propIndex) ||
        !reader.Read16(parsed.newId) ||
        !reader.Read8(parsed.isClearProp) ||
        !reader.Read8(parsed.dstType) ||
        !reader.Read32(parsed.myUseItemId) ||
        !reader.Read32(parsed.dstUseItemId) ||
        !reader.Read8(parsed.isConvertMode) ||
        !reader.ReadSigned32(parsed.stoneSkillType) ||
        !reader.Read32(parsed.lapTime))
    {
        return Fail(error, "Msg165 is shorter than its fixed prefix");
    }

    if (!ReadUint32List(
            reader, parsed.dstUins, "Msg165 has a truncated DstUin list", error) ||
        !reader.Read8(parsed.propUseMode) ||
        !ReadUint32List(
            reader, parsed.queryUins, "Msg165 has a truncated QueryUin list", error))
    {
        if (error != nullptr && error->empty())
        {
            *error = "Msg165 is truncated at PropUseMode";
        }
        return false;
    }

    if (flavor == Request165Flavor::QqSpeed21487)
    {
        if (!reader.Read8(parsed.currentSelfRank) ||
            !reader.Read8(parsed.currentTeammateRank) ||
            !reader.Read8(parsed.position) ||
            !reader.Read8(parsed.propSecType) ||
            !reader.Read8(parsed.propPosIdx) ||
            !reader.Read32(parsed.npcUin) ||
            !reader.Read8(parsed.usePropProtoMode) ||
            !reader.Read16(parsed.subPropId) ||
            !ReadOtherPropPositions(reader, parsed.otherPropPositions, error))
        {
            if (error != nullptr && error->empty())
            {
                *error = "Msg165 is shorter than its QQSpeed 21487 tail";
            }
            return false;
        }
    }

    if (reader.Remaining() != 0)
    {
        return Fail(error, "Msg165 contains trailing bytes");
    }

    out = std::move(parsed);
    return true;
}

Notify517 MakeNotify517(
    const Request165& request,
    const AuthoritativeUsePropResult& result)
{
    Notify517 notify;
    notify.uin = request.uin;
    notify.propId = request.propId;
    notify.dstUin = request.dstUin;
    notify.propPosition = request.currentPosition;
    notify.propIndex = request.propIndex;
    notify.newId = request.newId;
    notify.itemId = request.myUseItemId;
    notify.stoneSkillType = request.stoneSkillType;
    notify.lapTime = request.lapTime;
    notify.dstUins = request.dstUins;
    notify.propUseMode = request.propUseMode;
    notify.queryUins = request.queryUins;
    notify.dstType = request.dstType;
    notify.position = request.position;
    notify.parameters = result.parameters;
    notify.status = result.status;
    notify.propPosIdx = request.propPosIdx;
    notify.propSecType = request.propSecType;
    notify.passedCheckPointIds = result.passedCheckPointIds;
    notify.subPropId = request.subPropId;
    notify.otherPropPositions = request.otherPropPositions;
    return notify;
}

bool SerializeNotify517(
    const Notify517& notify,
    std::vector<std::uint8_t>& out,
    std::string* error)
{
    out.clear();
    if (error != nullptr)
    {
        error->clear();
    }

    if (!CheckCount(notify.dstUins.size(), "Msg517 DstUin count exceeds 255", error) ||
        !CheckCount(notify.queryUins.size(), "Msg517 QueryUin count exceeds 255", error) ||
        !CheckCount(notify.parameters.size(), "Msg517 parameter count exceeds 255", error) ||
        !CheckCount(
            notify.passedCheckPointIds.size(),
            "Msg517 checkpoint count exceeds 255",
            error) ||
        !CheckCount(
            notify.otherPropPositions.size(),
            "Msg517 OtherPropPos count exceeds 255",
            error))
    {
        return false;
    }

    std::size_t payloadLength = kNotify517BasePayloadLength;
    if (!AddSize(notify.dstUins.size() * 4u, payloadLength, error) ||
        !AddSize(notify.queryUins.size() * 4u, payloadLength, error) ||
        !AddSize(notify.parameters.size() * 4u, payloadLength, error) ||
        !AddSize(notify.passedCheckPointIds.size() * 2u, payloadLength, error))
    {
        return false;
    }

    for (const OtherPropPosition& record : notify.otherPropPositions)
    {
        constexpr std::size_t kRecordHeaderLength = 6;
        const std::size_t maxPositions =
            (static_cast<std::size_t>((std::numeric_limits<std::uint16_t>::max)()) -
                kRecordHeaderLength) /
            4u;
        if (record.positions.size() > maxPositions)
        {
            return Fail(error, "Msg517 OtherPropPos record exceeds 16-bit length");
        }
        const std::size_t recordLength =
            kRecordHeaderLength + record.positions.size() * 4u;
        if (!AddSize(recordLength, payloadLength, error))
        {
            return false;
        }
    }

    std::vector<std::uint8_t> encoded;
    encoded.reserve(payloadLength);
    Write32(encoded, notify.uin);
    Write16(encoded, notify.propId);
    Write32(encoded, notify.dstUin);
    for (std::int32_t position : notify.propPosition)
    {
        WriteSigned32(encoded, position);
    }
    Write32(encoded, notify.propIndex);
    Write16(encoded, notify.newId);
    Write32(encoded, notify.itemId);
    WriteSigned32(encoded, notify.stoneSkillType);
    Write32(encoded, notify.lapTime);

    Write8(encoded, static_cast<std::uint8_t>(notify.dstUins.size()));
    for (std::uint32_t uin : notify.dstUins)
    {
        Write32(encoded, uin);
    }
    Write8(encoded, notify.propUseMode);
    Write8(encoded, static_cast<std::uint8_t>(notify.queryUins.size()));
    for (std::uint32_t uin : notify.queryUins)
    {
        Write32(encoded, uin);
    }
    Write8(encoded, notify.dstType);
    Write8(encoded, notify.position);
    Write8(encoded, static_cast<std::uint8_t>(notify.parameters.size()));
    for (std::int32_t parameter : notify.parameters)
    {
        WriteSigned32(encoded, parameter);
    }
    Write8(encoded, notify.status);
    Write8(encoded, notify.propPosIdx);
    Write8(encoded, notify.propSecType);
    Write8(encoded, static_cast<std::uint8_t>(notify.passedCheckPointIds.size()));
    for (std::uint16_t checkpointId : notify.passedCheckPointIds)
    {
        Write16(encoded, checkpointId);
    }
    Write16(encoded, notify.subPropId);
    Write8(encoded, static_cast<std::uint8_t>(notify.otherPropPositions.size()));
    for (const OtherPropPosition& record : notify.otherPropPositions)
    {
        const std::size_t recordLength = 6u + record.positions.size() * 4u;
        Write16(encoded, static_cast<std::uint16_t>(recordLength));
        Write32(encoded, record.index);
        for (std::int32_t position : record.positions)
        {
            WriteSigned32(encoded, position);
        }
    }

    if (encoded.size() != payloadLength)
    {
        return Fail(error, "Msg517 serializer length invariant failed");
    }
    out.swap(encoded);
    return true;
}
} // namespace qqspeed::useprop
