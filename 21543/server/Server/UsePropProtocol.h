#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace qqspeed::useprop
{
enum class Request165Flavor
{
    QqSpeed21487,
    ZingSpeed,
};

struct OtherPropPosition
{
    std::uint32_t index = 0;
    std::vector<std::int32_t> positions;
};

// Decoded CRequestUseProp2Para (Msg165). All counted fields are retained so
// the read cursor always follows the client wire schema instead of assuming
// the common zero-count boost packet.
struct Request165
{
    std::uint32_t uin = 0;
    std::uint32_t time = 0;
    std::uint16_t propId = 0;
    std::uint32_t dstUin = 0;
    std::array<std::int32_t, 3> currentPosition{};
    std::uint32_t propIndex = 0;
    std::uint16_t newId = 0;
    std::uint8_t isClearProp = 0;
    std::uint8_t dstType = 0;
    std::uint32_t myUseItemId = 0;
    std::uint32_t dstUseItemId = 0;
    std::uint8_t isConvertMode = 0;
    std::int32_t stoneSkillType = 0;
    std::uint32_t lapTime = 0;
    std::vector<std::uint32_t> dstUins;
    std::uint8_t propUseMode = 0;
    std::vector<std::uint32_t> queryUins;

    // QQSpeed 21487 tail. ZingSpeed ends immediately after queryUins.
    std::uint8_t currentSelfRank = 0;
    std::uint8_t currentTeammateRank = 0;
    std::uint8_t position = 0;
    std::uint8_t propSecType = 0;
    std::uint8_t propPosIdx = 0;
    std::uint32_t npcUin = 0;
    std::uint8_t usePropProtoMode = 0;
    std::uint16_t subPropId = 0;
    std::vector<OtherPropPosition> otherPropPositions;
};

// Values in Msg517 that are decided by game logic rather than copied from
// Msg165. Keeping this separate prevents request bytes from masquerading as an
// authoritative server result.
struct AuthoritativeUsePropResult
{
    std::vector<std::int32_t> parameters;
    std::uint8_t status = 0;
    std::vector<std::uint16_t> passedCheckPointIds;
};

struct Notify517
{
    std::uint32_t uin = 0;
    std::uint16_t propId = 0;
    std::uint32_t dstUin = 0;
    std::array<std::int32_t, 3> propPosition{};
    std::uint32_t propIndex = 0;
    std::uint16_t newId = 0;
    std::uint32_t itemId = 0;
    std::int32_t stoneSkillType = 0;
    std::uint32_t lapTime = 0;
    std::vector<std::uint32_t> dstUins;
    std::uint8_t propUseMode = 0;
    std::vector<std::uint32_t> queryUins;
    std::uint8_t dstType = 0;
    std::uint8_t position = 0;
    std::vector<std::int32_t> parameters;
    std::uint8_t status = 0;
    std::uint8_t propPosIdx = 0;
    std::uint8_t propSecType = 0;
    std::vector<std::uint16_t> passedCheckPointIds;
    std::uint16_t subPropId = 0;
    std::vector<OtherPropPosition> otherPropPositions;
};

bool ParseRequest165(
    const std::uint8_t* payload,
    std::size_t payloadLength,
    Request165Flavor flavor,
    Request165& out,
    std::string* error = nullptr);

Notify517 MakeNotify517(
    const Request165& request,
    const AuthoritativeUsePropResult& result);

// Produces only the Msg517 payload. The outer sender owns BodyLen and the
// message header. Count or length overflow is rejected; no value is truncated.
bool SerializeNotify517(
    const Notify517& notify,
    std::vector<std::uint8_t>& out,
    std::string* error = nullptr);
} // namespace qqspeed::useprop
