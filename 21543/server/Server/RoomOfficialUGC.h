#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace QQSpeedRoomUGCOfficial
{
    struct BrowseRequest
    {
        std::uint32_t uin = 0;
        std::uint32_t timestamp = 0;
        std::uint8_t operation = 0;
        std::uint8_t category = 0;
        std::uint8_t sortType = 0;
        std::uint16_t pageIndex = 0;
        std::uint8_t pageSize = 0;
        std::uint64_t workId = 0;
        std::string keyword;
        std::uint32_t creatorUin = 0;
        std::uint32_t creatorAreaId = 0;
        std::uint8_t notifyTypeFilter = 0;
        std::uint8_t themeTagFilter = 0;
        std::uint8_t timeRange = 0;
        std::vector<std::int32_t> tagFilters;
    };

    struct InteractRequest
    {
        std::uint32_t uin = 0;
        std::uint32_t timestamp = 0;
        std::uint8_t operation = 0;
        std::uint64_t workId = 0;
        std::uint8_t discountType = 0;
        std::uint32_t targetCreatorUin = 0;
        std::uint32_t targetCreatorAreaId = 0;
        std::uint8_t category = 0;
        std::int32_t reportType = 0;
        std::string reportDescription;
        std::vector<std::uint64_t> viewWorkIds;
    };

    struct PlayerInfoRequest
    {
        std::uint32_t uin = 0;
        std::uint32_t timestamp = 0;
        std::uint8_t operation = 0;
        std::uint32_t targetUin = 0;
        std::uint32_t targetAreaId = 0;
    };

    struct OwnedWork
    {
        std::uint64_t workId = 0;
        std::uint32_t purchaseTime = 0;
        std::uint8_t ownerSideStatus = 0;
        std::uint8_t purchaseSource = 0;
    };

    struct RoomSkinInfo
    {
        std::uint64_t workId = 0;
        std::string workName;
        std::string coverCosUrl;
        std::string viewCosUrl;
        std::string cosPrefix;
    };

    std::size_t PayloadLengthFromWireBodyLength(std::size_t wireBodyLength) noexcept;

    bool ParseBrowseRequest(
        const std::uint8_t* body,
        std::size_t bodyLength,
        BrowseRequest& out,
        std::string* error = nullptr);

    bool BuildBrowseResponse(
        const BrowseRequest& request,
        std::uint32_t sessionUin,
        std::vector<std::uint8_t>& out,
        std::string* error = nullptr);

    bool BuildOwnedBrowseResponse(
        const BrowseRequest& request,
        std::uint32_t sessionUin,
        const std::vector<OwnedWork>& ownedWorks,
        std::uint64_t usingWorkId,
        std::vector<std::uint8_t>& out,
        std::string* error = nullptr);

    bool ParseInteractRequest(
        const std::uint8_t* body,
        std::size_t bodyLength,
        InteractRequest& out,
        std::string* error = nullptr);

    bool BuildInteractResponse(
        const InteractRequest& request,
        std::uint32_t sessionUin,
        std::vector<std::uint8_t>& out,
        std::string* error = nullptr);

    bool GetCapturedPurchasePrice(
        const InteractRequest& request,
        std::uint32_t& price,
        std::string* error = nullptr);

    bool BuildPurchaseResponse(
        const InteractRequest& request,
        std::uint32_t sessionUin,
        std::uint32_t paidPrice,
        std::uint32_t remainingBalance,
        std::vector<std::uint8_t>& out,
        std::string* error = nullptr);

    bool BuildUseStateResponse(
        const InteractRequest& request,
        std::uint32_t sessionUin,
        std::vector<std::uint8_t>& out,
        std::string* error = nullptr);

    bool BuildDeleteOwnedWorkResponse(
        const InteractRequest& request,
        std::uint32_t sessionUin,
        std::vector<std::uint8_t>& out,
        std::string* error = nullptr);

    bool GetRoomSkinInfo(
        std::uint64_t workId,
        RoomSkinInfo& out,
        std::string* error = nullptr);

    bool BuildRoomSkinRefreshNotify(
        std::uint32_t recipientUin,
        std::uint32_t equipperUin,
        const RoomSkinInfo& roomSkin,
        std::vector<std::uint8_t>& out,
        std::string* error = nullptr);

    bool MarkDetailResponseOwned(
        std::vector<std::uint8_t>& response,
        std::uint64_t workId,
        std::string* error = nullptr);

    bool ParsePlayerInfoRequest(
        const std::uint8_t* body,
        std::size_t bodyLength,
        PlayerInfoRequest& out,
        std::string* error = nullptr);

    bool BuildPlayerInfoResponse(
        const PlayerInfoRequest& request,
        std::uint32_t sessionUin,
        std::vector<std::uint8_t>& out,
        std::string* error = nullptr);
}
