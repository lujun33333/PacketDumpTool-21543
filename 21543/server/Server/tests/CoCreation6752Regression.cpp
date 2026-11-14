#include "../AuroraOfficialUGC.h"
#include "../Msg28346Router.h"
#include "../RoomOfficialUGC.h"
#include "../UGCQuickRpc.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr std::uint32_t kCapturedUin = 924496061u;
    constexpr std::uint32_t kCapturedUin17188 = 1030921579u;

    void Require(bool condition, const std::string& message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot open fixture: " + path.string());
        stream.seekg(0, std::ios::end);
        const auto length = stream.tellg();
        Require(length >= 0, "cannot determine fixture size: " + path.string());
        stream.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        stream.read(reinterpret_cast<char*>(bytes.data()), length);
        Require(stream.good(), "cannot read fixture: " + path.string());
        return bytes;
    }

    std::uint16_t ReadBE16(const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        Require(offset + 2u <= bytes.size(), "BE16 offset outside payload");
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[offset]) << 8u) |
            static_cast<std::uint16_t>(bytes[offset + 1u]));
    }

    std::uint32_t ReadBE32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        Require(offset + 4u <= bytes.size(), "BE32 offset outside payload");
        return (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
            (static_cast<std::uint32_t>(bytes[offset + 1u]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 2u]) << 8u) |
            static_cast<std::uint32_t>(bytes[offset + 3u]);
    }

    std::uint64_t ReadBE64(const std::vector<std::uint8_t>& bytes, std::size_t offset)
    {
        Require(offset + 8u <= bytes.size(), "BE64 offset outside payload");
        std::uint64_t value = 0u;
        for (std::size_t index = 0; index < 8u; ++index)
            value = (value << 8u) | bytes[offset + index];
        return value;
    }

    std::string ReadString8(
        const std::vector<std::uint8_t>& bytes,
        std::size_t lengthOffset)
    {
        Require(lengthOffset < bytes.size(), "uint8 string length outside payload");
        const std::size_t length = bytes[lengthOffset];
        Require(lengthOffset + 1u + length <= bytes.size(),
            "uint8 string outside payload");
        return std::string(
            bytes.begin() + lengthOffset + 1u,
            bytes.begin() + lengthOffset + 1u + length);
    }

    const std::string& ExpectedDescriptionRaw()
    {
        static const std::string value(
            "\xBB\xB6\xD3\xAD\xCC\xE5\xD1\xE9", 8u); // 欢迎体验 in GB18030
        return value;
    }

    std::vector<std::uint64_t> RoomBrowseWorkIds(
        const std::vector<std::uint8_t>& response)
    {
        Require(response.size() >= 11u, "RoomUGC browse response too short");
        std::vector<std::uint64_t> workIds;
        std::size_t cursor = 11u;
        workIds.reserve(response[10]);
        for (std::size_t index = 0; index < response[10]; ++index)
        {
            const std::size_t recordLength = ReadBE16(response, cursor);
            Require(recordLength >= 10u && cursor + recordLength <= response.size(),
                "RoomUGC browse record crosses response boundary");
            workIds.push_back(ReadBE64(response, cursor + 2u));
            cursor += recordLength;
        }
        return workIds;
    }

    void WriteBE32(
        std::vector<std::uint8_t>& bytes,
        std::size_t offset,
        std::uint32_t value)
    {
        Require(offset + 4u <= bytes.size(), "BE32 write outside payload");
        bytes[offset] = static_cast<std::uint8_t>(value >> 24u);
        bytes[offset + 1u] = static_cast<std::uint8_t>(value >> 16u);
        bytes[offset + 2u] = static_cast<std::uint8_t>(value >> 8u);
        bytes[offset + 3u] = static_cast<std::uint8_t>(value);
    }

    void WriteBE64(
        std::vector<std::uint8_t>& bytes,
        std::size_t offset,
        std::uint64_t value)
    {
        Require(offset + 8u <= bytes.size(), "BE64 write outside payload");
        for (std::size_t index = 0u; index < 8u; ++index)
            bytes[offset + index] = static_cast<std::uint8_t>(
                value >> (56u - index * 8u));
    }

    void WriteBE16(
        std::vector<std::uint8_t>& bytes,
        std::size_t offset,
        std::uint16_t value)
    {
        Require(offset + 2u <= bytes.size(), "BE16 write outside payload");
        bytes[offset] = static_cast<std::uint8_t>(value >> 8u);
        bytes[offset + 1u] = static_cast<std::uint8_t>(value);
    }

    void ReplaceString8(
        std::vector<std::uint8_t>& bytes,
        std::size_t lengthOffset,
        std::string_view value)
    {
        Require(value.size() <= 0xFFu && lengthOffset < bytes.size(),
            "replacement uint8 string is invalid");
        const std::size_t oldLength = bytes[lengthOffset];
        Require(lengthOffset + 1u + oldLength <= bytes.size(),
            "replacement uint8 string crosses payload");
        const auto first = bytes.begin() + lengthOffset;
        bytes.erase(first, first + 1u + oldLength);
        bytes.insert(bytes.begin() + lengthOffset,
            static_cast<std::uint8_t>(value.size()));
        bytes.insert(bytes.begin() + lengthOffset + 1u, value.begin(), value.end());
    }

    std::size_t AuroraDescriptionLengthOffset(
        const std::vector<std::uint8_t>& response,
        std::size_t briefOffset)
    {
        std::size_t cursor = briefOffset + ReadBE16(response, briefOffset);
        for (int index = 0; index < 2; ++index)
            cursor += 2u + ReadBE16(response, cursor);
        cursor += 32u + 37u;
        Require(cursor < response.size(), "Aurora description is outside payload");
        return cursor;
    }

    void NormalizeAuroraWorkMetadata(std::vector<std::uint8_t>& response)
    {
        constexpr std::size_t kDetailSizeOffset = 14u;
        constexpr std::size_t kBriefOffset = 16u;
        constexpr std::size_t kNameLengthInBrief = 10u;
        const std::uint16_t oldDetailSize = ReadBE16(response, kDetailSizeOffset);
        const std::uint16_t oldBriefSize = ReadBE16(response, kBriefOffset);
        const std::size_t nameLengthOffset = kBriefOffset + kNameLengthInBrief;
        const std::size_t oldNameLength = response[nameLengthOffset];
        ReplaceString8(response, nameLengthOffset, "Kiss");
        const auto nameDelta = static_cast<std::ptrdiff_t>(4u) -
            static_cast<std::ptrdiff_t>(oldNameLength);
        WriteBE16(response, kBriefOffset, static_cast<std::uint16_t>(
            static_cast<std::ptrdiff_t>(oldBriefSize) + nameDelta));

        const std::size_t descriptionOffset =
            AuroraDescriptionLengthOffset(response, kBriefOffset);
        const std::size_t oldDescriptionLength = response[descriptionOffset];
        ReplaceString8(response, descriptionOffset, ExpectedDescriptionRaw());
        const auto descriptionDelta =
            static_cast<std::ptrdiff_t>(ExpectedDescriptionRaw().size()) -
            static_cast<std::ptrdiff_t>(oldDescriptionLength);
        WriteBE16(response, kDetailSizeOffset, static_cast<std::uint16_t>(
            static_cast<std::ptrdiff_t>(oldDetailSize) + nameDelta +
            descriptionDelta));
    }

    std::vector<std::uint8_t> ClassApplication(const std::filesystem::path& path)
    {
        const auto bytes = ReadBytes(path);
        Require(bytes.size() >= 2u, "class fixture shorter than its size word");
        const std::size_t declared = ReadBE16(bytes, 0u);
        Require(declared >= 2u && declared <= bytes.size(), "invalid class size word");
        return { bytes.begin() + 2, bytes.begin() + declared };
    }

    std::vector<std::uint8_t> Slice(
        const std::filesystem::path& path,
        std::size_t offset,
        std::size_t length)
    {
        const auto bytes = ReadBytes(path);
        Require(offset + length <= bytes.size(), "slice outside fixture");
        return { bytes.begin() + offset, bytes.begin() + offset + length };
    }

    bool ContainsText(
        const std::vector<std::uint8_t>& bytes,
        std::string_view text)
    {
        return std::search(
            bytes.begin(), bytes.end(), text.begin(), text.end()) != bytes.end();
    }

    void RequireEqual(
        const std::vector<std::uint8_t>& actual,
        const std::vector<std::uint8_t>& expected,
        const char* message)
    {
        if (actual == expected) return;
        const std::size_t common = (std::min)(actual.size(), expected.size());
        std::size_t firstDifference = 0u;
        while (firstDifference < common &&
               actual[firstDifference] == expected[firstDifference])
        {
            ++firstDifference;
        }
        throw std::runtime_error(
            std::string(message) + " (actual=" + std::to_string(actual.size()) +
            ", expected=" + std::to_string(expected.size()) +
            ", first-difference=" + std::to_string(firstDifference) +
            ", actual-byte=" +
                (firstDifference < actual.size()
                    ? std::to_string(actual[firstDifference]) : std::string("eof")) +
            ", expected-byte=" +
                (firstDifference < expected.size()
                    ? std::to_string(expected[firstDifference]) : std::string("eof")) +
            ")");
    }

    void AppendLuaNumber(std::vector<std::uint8_t>& bytes, double value)
    {
        bytes.push_back(0xF6u);
        const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
        bytes.insert(bytes.end(), raw, raw + sizeof(value));
    }

    std::vector<std::uint8_t> MakeQuickRequest(
        std::string_view command,
        double context,
        const std::vector<double>& arguments = {})
    {
        std::vector<std::uint8_t> request(command.begin(), command.end());
        request.push_back(0u);
        request.push_back(0u);
        AppendLuaNumber(request, context);
        for (const double value : arguments) AppendLuaNumber(request, value);
        return request;
    }

    double ResponseContext(const std::vector<std::uint8_t>& response)
    {
        constexpr std::string_view command = "DispatchCoResponse";
        Require(response.size() >= command.size() + 11u, "QuickRPC response too short");
        Require(std::equal(command.begin(), command.end(), response.begin()),
            "QuickRPC response command mismatch");
        Require(response[command.size()] == 0u &&
                response[command.size() + 1u] == 0u &&
                response[command.size() + 2u] == 0xF6u,
            "QuickRPC response context marker mismatch");
        double context = 0.0;
        std::memcpy(&context, response.data() + command.size() + 3u, sizeof(context));
        return context;
    }

    void TestRoomBrowse(const std::filesystem::path& payloads)
    {
        using namespace QQSpeedRoomUGCOfficial;
        const auto requestBytes = ClassApplication(
            payloads / "network_bin_6752_10106_pack_before_body_msg28471_len37.bin");
        BrowseRequest request;
        std::string error;
        Require(ParseBrowseRequest(
            requestBytes.data(), requestBytes.size(), request, &error), error);
        Require(request.uin == kCapturedUin && request.operation == 1u &&
                request.category == 1u && request.sortType == 3u &&
                request.pageIndex == 0u && request.pageSize == 6u,
            "official RoomUGC gallery request parsed incorrectly");

        std::vector<std::uint8_t> response;
        Require(BuildBrowseResponse(request, kCapturedUin, response, &error), error);
        const std::vector<std::uint64_t> capturedCatalogWorkIds = {
            1073796097ull,
            1073746945ull,
            1101394945ull,
            1101250561ull,
            1101243393ull,
            1101244417ull,
            1080501249ull,
            1101247489ull,
            1101395969ull,
            1073757185ull,
            1073758209ull,
            1073761281ull,
            1073750017ull,
            1073751041ull,
            1073763329ull,
            1073776641ull,
            1074709505ull,
            1093775361ull,
        };
        const std::vector<std::string> capturedCatalogWorkNames = {
            std::string("\xC1\xFA\xBE\xB3", 4),
            std::string("\xC4\xDE\xBA\xE7\xBE\xC6\xB0\xC9", 8),
            std::string("\xB6\xAF\xB8\xD0\xCE\xE8\xCC\xA8", 8),
            std::string("\xD4\xC2\xD2\xB9\xC3\xA8\xD3\xEF", 8),
            std::string("\xBC\xAB\xCB\xD9\xB2\xB9\xB8\xF8\xD5\xBE", 10),
            std::string("\xD4\xC2\xC2\xE4\xC9\xAD\xC1\xD6", 8),
        };
        Require(ReadBE16(response, 8u) == capturedCatalogWorkIds.size() &&
                response[10] == 6u,
            "RoomUGC captured catalog total/count fields are wrong");
        const auto firstPageIds = RoomBrowseWorkIds(response);
        Require(std::equal(
                firstPageIds.begin(), firstPageIds.end(),
                capturedCatalogWorkIds.begin()),
            "RoomUGC first page does not follow the captured catalog");
        std::size_t roomRecordCursor = 11u;
        for (std::size_t index = 0; index < response[10]; ++index)
        {
            Require(ReadString8(response, roomRecordCursor + 14u) ==
                    capturedCatalogWorkNames[index],
                "RoomUGC browse work name drifted from the official capture");
            roomRecordCursor += ReadBE16(response, roomRecordCursor);
        }

        for (std::size_t index = 0u; index < firstPageIds.size(); ++index)
        {
            const auto workId = firstPageIds[index];
            BrowseRequest listedDetail;
            listedDetail.uin = kCapturedUin;
            listedDetail.operation = 3u;
            listedDetail.workId = workId;
            std::vector<std::uint8_t> detailResponse;
            Require(BuildBrowseResponse(
                    listedDetail, kCapturedUin, detailResponse, &error), error);
            Require(detailResponse.size() >= 24u &&
                    ReadBE64(detailResponse, 16u) == workId &&
                    ReadString8(detailResponse, 28u) ==
                        capturedCatalogWorkNames[index],
                "RoomUGC listed work has no matching captured detail");
        }

        BrowseRequest secondPage = request;
        secondPage.pageIndex = 1u;
        Require(BuildBrowseResponse(
                secondPage, kCapturedUin, response, &error), error);
        Require(ReadBE16(response, 8u) == capturedCatalogWorkIds.size() &&
                response[10] == 6u &&
                RoomBrowseWorkIds(response) == std::vector<std::uint64_t>(
                    capturedCatalogWorkIds.begin() + 6u,
                    capturedCatalogWorkIds.begin() + 12u),
            "RoomUGC second page did not expose the next captured works");

        BrowseRequest finalPage = request;
        finalPage.pageIndex = 2u;
        Require(BuildBrowseResponse(
                finalPage, kCapturedUin, response, &error), error);
        Require(ReadBE16(response, 8u) == capturedCatalogWorkIds.size() &&
                response[10] == 6u &&
                RoomBrowseWorkIds(response) == std::vector<std::uint64_t>(
                    capturedCatalogWorkIds.begin() + 12u,
                    capturedCatalogWorkIds.end()),
            "RoomUGC final page did not expose the remaining captured works");

        BrowseRequest pastLastPage = request;
        pastLastPage.pageIndex = 3u;
        Require(BuildBrowseResponse(
                pastLastPage, kCapturedUin, response, &error), error);
        Require(ReadBE16(response, 8u) == capturedCatalogWorkIds.size() &&
                response[10] == 0u && RoomBrowseWorkIds(response).empty(),
            "RoomUGC page past the captured catalog must be an empty success");

        BrowseRequest threePerPage = request;
        threePerPage.pageIndex = 1u;
        threePerPage.pageSize = 3u;
        Require(BuildBrowseResponse(
            threePerPage, kCapturedUin, response, &error), error);
        Require(RoomBrowseWorkIds(response) == std::vector<std::uint64_t>({
                capturedCatalogWorkIds[3],
                capturedCatalogWorkIds[4],
                capturedCatalogWorkIds[5],
            }),
            "RoomUGC pagination did not honor the client page size");

        BrowseRequest latest = request;
        latest.operation = 13u;
        latest.sortType = 0u;
        Require(BuildBrowseResponse(latest, kCapturedUin, response, &error), error);
        Require(ReadBE16(response, 8u) == capturedCatalogWorkIds.size() &&
                RoomBrowseWorkIds(response) == firstPageIds,
            "RoomUGC operation-13 did not expose the captured catalog");

        const auto marketRequestBytes = ClassApplication(
            payloads / "network_bin_6752_9834_serialize_body_plain_msg28471_len37.bin");
        BrowseRequest market;
        Require(ParseBrowseRequest(
            marketRequestBytes.data(), marketRequestBytes.size(), market, &error), error);
        Require(market.operation == 5u && market.pageIndex == 0u &&
                market.pageSize == 150u,
            "official RoomUGC market-header request parsed incorrectly");
        Require(BuildBrowseResponse(market, kCapturedUin, response, &error), error);
        RequireEqual(
            response,
            ClassApplication(payloads /
                "network_bin_6752_9836_parser_queue_dispatch_body_msg28471_len290.bin"),
            "RoomUGC operation-5 market-header response drifted from dump6752");

        market.pageSize = 0u;
        Require(BuildBrowseResponse(market, kCapturedUin, response, &error), error);
        RequireEqual(
            response,
            ClassApplication(payloads /
                "network_bin_6752_9836_parser_queue_dispatch_body_msg28471_len290.bin"),
            "RoomUGC market header became dependent on an incidental page size");

        const auto clickedRequestBytes = ClassApplication(
            payloads / "network_bin_6752_9855_serialize_body_plain_msg28471_len37.bin");
        BrowseRequest clicked;
        Require(ParseBrowseRequest(
            clickedRequestBytes.data(), clickedRequestBytes.size(), clicked, &error), error);
        Require(clicked.operation == 3u && clicked.pageIndex == 0u &&
                clicked.pageSize == 1u && clicked.workId == 1080501249ull,
            "official RoomUGC clicked-work request parsed incorrectly");
        Require(BuildBrowseResponse(clicked, kCapturedUin, response, &error), error);
        const auto expectedClicked = ClassApplication(payloads /
            "network_bin_6752_9857_parser_queue_dispatch_body_msg28471_len418.bin");
        RequireEqual(
            response,
            expectedClicked,
            "RoomUGC clicked-work detail drifted from dump6752");

        BrowseRequest detail;
        detail.uin = kCapturedUin;
        detail.operation = 3u;
        detail.workId = 1101244417ull;
        Require(BuildBrowseResponse(detail, kCapturedUin, response, &error), error);
        const auto expectedDetail = ClassApplication(
            payloads / "network_bin_6752_9335_parser_queue_dispatch_body_msg28471_len530.bin");
        RequireEqual(response, expectedDetail,
            "RoomUGC detail drifted from dump6752");
        Require(ContainsText(response, "room_display.zip") &&
                ContainsText(response, "runtimeugc/1101244417/"),
            "RoomUGC detail omitted captured display/runtime assets");

        detail.workId = 0x1122334455667788ull;
        Require(!BuildBrowseResponse(detail, kCapturedUin, response, &error),
            "uncaptured RoomUGC detail was fabricated");
        detail.workId = 1073746945ull;
        Require(BuildBrowseResponse(detail, kCapturedUin, response, &error), error);
        Require(ReadBE64(response, 16u) == detail.workId &&
                ReadString8(response, 28u) ==
                    std::string("\xC4\xDE\xBA\xE7\xBE\xC6\xB0\xC9", 8),
            "newly captured RoomUGC detail is unavailable");
    }

    void TestRoomPurchaseEquipAndTheme17188(
        const std::filesystem::path& payloads)
    {
        using namespace QQSpeedRoomUGCOfficial;
        constexpr std::uint64_t kWorkId = 0x4000D401ull;
        std::string error;
        std::vector<std::uint8_t> response;

        const auto purchaseBytes = ClassApplication(payloads /
            "network_bin_17188_6145_pack_before_body_msg28472_len36.bin");
        InteractRequest purchase;
        Require(ParseInteractRequest(
            purchaseBytes.data(), purchaseBytes.size(), purchase, &error), error);
        Require(purchase.uin == kCapturedUin17188 && purchase.operation == 0u &&
                purchase.workId == kWorkId,
            "official dump17188 purchase request parsed incorrectly");
        std::uint32_t price = 0u;
        Require(GetCapturedPurchasePrice(purchase, price, &error), error);
        Require(price == 640u, "official dump17188 purchase price is wrong");
        Require(BuildPurchaseResponse(
            purchase, kCapturedUin17188, price, 360u, response, &error), error);
        RequireEqual(response, ClassApplication(payloads /
            "network_bin_17188_6198_parser_queue_dispatch_body_msg28472_len66.bin"),
            "RoomUGC purchase response drifted from dump17188");

        const auto equipBytes = ClassApplication(payloads /
            "network_bin_17188_6219_pack_before_body_msg28472_len36.bin");
        InteractRequest equip;
        Require(ParseInteractRequest(
            equipBytes.data(), equipBytes.size(), equip, &error), error);
        Require(equip.uin == kCapturedUin17188 && equip.operation == 7u &&
                equip.workId == kWorkId,
            "official dump17188 equip request parsed incorrectly");
        Require(BuildUseStateResponse(
            equip, kCapturedUin17188, response, &error), error);
        RequireEqual(response, ClassApplication(payloads /
            "network_bin_17188_6221_parser_queue_dispatch_body_msg28472_len66.bin"),
            "RoomUGC equip response drifted from dump17188");

        // Equip is a state operation. Runtime display assets are consumed later
        // by Msg716 and must not make the op7 response depend on a local catalog.
        equip.workId = 1080501249ull;
        Require(BuildUseStateResponse(
            equip, kCapturedUin17188, response, &error), error);
        Require(response.size() == 49u && ReadBE64(response, 8u) == equip.workId,
            "RoomUGC equip incorrectly requires local runtime-display assets");

        InteractRequest removeOwned = equip;
        removeOwned.operation = 10u;
        removeOwned.category = 1u;
        Require(BuildDeleteOwnedWorkResponse(
            removeOwned, kCapturedUin17188, response, &error), error);
        Require(response.size() == 49u &&
                ReadBE32(response, 0u) == kCapturedUin17188 &&
                response[7u] == 10u && ReadBE64(response, 8u) == removeOwned.workId &&
                response[48u] == removeOwned.category,
            "RoomUGC delete-owned-work response does not match the client schema");

        RoomSkinInfo roomSkin;
        Require(GetRoomSkinInfo(kWorkId, roomSkin, &error), error);
        Require(roomSkin.workId == kWorkId &&
                roomSkin.workName == std::string("\xC1\xFA\xBE\xB3", 4) &&
                roomSkin.coverCosUrl ==
                    "https://image.speed.qq.com/roomugc/1074444308/cover_1785161021781" &&
                roomSkin.viewCosUrl ==
                    "https://image.speed.qq.com/roomugc/1073796097/1785369892848/room_display.zip" &&
                roomSkin.cosPrefix ==
                    "https://image.speed.qq.com/runtimeugc/1073796097/",
            "RoomUGC captured room-skin runtime fields are wrong");
        Require(BuildRoomSkinRefreshNotify(
            kCapturedUin17188, kCapturedUin17188, roomSkin, response, &error), error);
        const auto expectedNotify = ClassApplication(payloads /
            "network_bin_17188_7854_inner_dispatch_body_msg716_len250.bin");
        RequireEqual(response, expectedNotify,
            "RoomUGC Msg716 room-skin refresh drifted from dump17188");

        const auto ownedBytes = ClassApplication(payloads /
            "network_bin_17188_6196_pack_before_body_msg28471_len37.bin");
        BrowseRequest ownedRequest;
        Require(ParseBrowseRequest(
            ownedBytes.data(), ownedBytes.size(), ownedRequest, &error), error);
        Require(ownedRequest.uin == kCapturedUin17188 &&
                ownedRequest.operation == 23u && ownedRequest.pageIndex == 0u &&
                ownedRequest.pageSize == 50u,
            "official dump17188 owned-work request parsed incorrectly");

        Require(BuildOwnedBrowseResponse(
            ownedRequest, kCapturedUin17188, {}, 0u, response, &error), error);
        const auto expectedBaseOwned = ClassApplication(payloads /
            "network_bin_17188_1225_parser_queue_dispatch_body_msg28471_len338.bin");
        RequireEqual(response, expectedBaseOwned,
            "RoomUGC base owned-work response drifted from dump17188");

        const std::vector<OwnedWork> purchased = {{
            kWorkId, 1786090671u, 0u, 0u,
        }};
        Require(BuildOwnedBrowseResponse(
            ownedRequest, kCapturedUin17188, purchased, 0u,
            response, &error), error);
        const auto expectedPurchased = ClassApplication(payloads /
            "network_bin_17188_6212_parser_queue_dispatch_body_msg28471_len554.bin");
        RequireEqual(response, expectedPurchased,
            "RoomUGC purchased owned-work response drifted from dump17188");

        Require(BuildOwnedBrowseResponse(
            ownedRequest, kCapturedUin17188, purchased, kWorkId,
            response, &error), error);
        const auto expectedEquipped = ClassApplication(payloads /
            "network_bin_17188_7522_parser_queue_dispatch_body_msg28471_len554.bin");
        RequireEqual(response, expectedEquipped,
            "RoomUGC equipped owned-work response drifted from dump17188");
    }

    void TestRoomUnequip20932(const std::filesystem::path& payloads)
    {
        using namespace QQSpeedRoomUGCOfficial;
        std::string error;
        std::vector<std::uint8_t> response;

        const auto unequipBytes = ClassApplication(payloads /
            "network_bin_20932_5283_pack_before_body_msg28472_len36.bin");
        InteractRequest unequip;
        Require(ParseInteractRequest(
            unequipBytes.data(), unequipBytes.size(), unequip, &error), error);
        Require(unequip.uin == kCapturedUin17188 &&
                unequip.operation == 8u && unequip.workId == 0u &&
                unequip.discountType == 0u &&
                unequip.targetCreatorUin == 0u &&
                unequip.targetCreatorAreaId == 0u &&
                unequip.category == 0u && unequip.reportType == 0 &&
                unequip.reportDescription.empty() &&
                unequip.viewWorkIds.empty(),
            "official dump20932 unequip request parsed incorrectly");
        Require(BuildUseStateResponse(
            unequip, kCapturedUin17188, response, &error), error);
        RequireEqual(response, ClassApplication(payloads /
            "network_bin_20932_5285_parser_queue_dispatch_body_msg28472_len66.bin"),
            "RoomUGC unequip response drifted from dump20932");
    }

    void TestRoomSkinCreate20932(const std::filesystem::path& payloads)
    {
        std::string error;
        std::vector<std::uint8_t> plain;
        QQSpeedMsg103::RequestFields fields;

        const auto createWithoutSkin = ReadBytes(payloads /
            "network_bin_20932_7466_safe28346_builder_inner_body_at12_msg103_len137.bin");
        Require(QQSpeedMsg103::DecodeSafeRequestToPlain(
            createWithoutSkin.data(), createWithoutSkin.size(), plain, &fields, &error),
            error);
        Require(fields.uin == kCapturedUin17188 &&
                fields.roomUGCSkinWorkId == 0u &&
                fields.extendedRoomInfoCount == 0u && plain.size() == 72u,
            "official RoomUGC create-without-skin fields decoded incorrectly");

        const auto createWithSkin = ReadBytes(payloads /
            "network_bin_20932_8096_safe28346_builder_inner_body_at12_msg103_len134.bin");
        Require(QQSpeedMsg103::DecodeSafeRequestToPlain(
            createWithSkin.data(), createWithSkin.size(), plain, &fields, &error),
            error);
        Require(fields.uin == kCapturedUin17188 &&
                fields.roomUGCSkinWorkId == 1073796097ull &&
                fields.extendedRoomInfoCount == 0u && plain.size() == 72u,
            "official RoomUGC create-with-skin work ID decoded incorrectly");
    }

    void TestRoomInteractAndProfiles(const std::filesystem::path& payloads)
    {
        using namespace QQSpeedRoomUGCOfficial;
        std::string error;
        std::vector<std::uint8_t> response;

        const auto interactBytes = ClassApplication(
            payloads / "network_bin_6752_9383_pack_before_body_msg28472_len44.bin");
        InteractRequest interact;
        Require(ParseInteractRequest(
            interactBytes.data(), interactBytes.size(), interact, &error), error);
        Require(interact.operation == 12u && interact.workId == 0u &&
                interact.viewWorkIds == std::vector<std::uint64_t>{ 1101244417ull },
            "official RoomUGC interaction request parsed incorrectly");
        Require(BuildInteractResponse(interact, kCapturedUin, response, &error), error);
        const auto officialInteractResponse = ClassApplication(payloads /
            "network_bin_6752_9385_parser_queue_dispatch_body_msg28472_len66.bin");
        RequireEqual(response, officialInteractResponse,
            "RoomUGC interaction response drifted from dump6752");

        auto batchViewBytes = interactBytes;
        batchViewBytes[33u] = 6u;
        for (std::uint64_t workId : {
                1073761281ull, 1073794049ull, 1080501249ull,
                1073746945ull, 1073750017ull })
        {
            const std::size_t offset = batchViewBytes.size();
            batchViewBytes.resize(offset + 8u);
            WriteBE64(batchViewBytes, offset, workId);
        }
        InteractRequest batchView;
        Require(ParseInteractRequest(
            batchViewBytes.data(), batchViewBytes.size(), batchView, &error), error);
        Require(batchView.operation == 12u && batchView.workId == 0u &&
                batchView.viewWorkIds.size() == 6u,
            "RoomUGC batched valid-view request parsed incorrectly");
        Require(BuildInteractResponse(
            batchView, kCapturedUin, response, &error), error);
        RequireEqual(response, officialInteractResponse,
            "RoomUGC batched valid-view response drifted from official schema");

        std::vector<std::uint8_t> purchaseBytes(34u, 0u);
        WriteBE32(purchaseBytes, 0u, kCapturedUin);
        purchaseBytes[8u] = 0u;
        WriteBE64(purchaseBytes, 9u, 1073761281ull);
        purchaseBytes[17u] = 0u;
        InteractRequest purchase;
        Require(ParseInteractRequest(
            purchaseBytes.data(), purchaseBytes.size(), purchase, &error), error);
        std::uint32_t price = 0u;
        Require(GetCapturedPurchasePrice(purchase, price, &error), error);
        Require(price == 600u, "RoomUGC captured purchase price is wrong");
        Require(BuildPurchaseResponse(
            purchase, kCapturedUin, price, 99400u, response, &error), error);
        Require(response.size() == 49u && ReadBE32(response, 0u) == kCapturedUin &&
                ReadBE16(response, 4u) == 0u && response[6u] == 0u &&
                response[7u] == 0u && ReadBE64(response, 8u) == 1073761281ull &&
                ReadBE32(response, 16u) == 600u &&
                ReadBE32(response, 20u) == 99400u &&
                ReadBE32(response, 40u) == 0u &&
                ReadBE32(response, 44u) == 0u && response[48u] == 0u,
            "RoomUGC purchase response does not match the client response schema");

        BrowseRequest ownedDetail;
        ownedDetail.uin = kCapturedUin;
        ownedDetail.operation = 3u;
        ownedDetail.workId = purchase.workId;
        Require(BuildBrowseResponse(
            ownedDetail, kCapturedUin, response, &error), error);
        const std::size_t interactCountOffset = 12u + ReadBE16(response, 12u);
        Require(interactCountOffset < response.size() &&
                response[interactCountOffset] == 1u &&
                ReadBE16(response, interactCountOffset + 1u) == 13u &&
                ReadBE64(response, interactCountOffset + 3u) == purchase.workId &&
                response[interactCountOffset + 13u] == 0u,
            "captured RoomUGC ownership marker drifted from dump6752");
        Require(MarkDetailResponseOwned(response, purchase.workId, &error), error);
        Require(response[interactCountOffset] == 1u &&
                ReadBE16(response, interactCountOffset + 1u) == 13u &&
                ReadBE64(response, interactCountOffset + 3u) == purchase.workId &&
                response[interactCountOffset + 13u] == 1u,
            "RoomUGC purchased ownership marker is malformed");
        const std::size_t ownedDetailSize = response.size();
        Require(MarkDetailResponseOwned(response, purchase.workId, &error), error);
        Require(response.size() == ownedDetailSize,
            "RoomUGC duplicate ownership marker was appended");

        const auto profileBytes = ClassApplication(
            payloads / "network_bin_6752_9333_pack_before_body_msg28473_len25.bin");
        PlayerInfoRequest profile;
        Require(ParsePlayerInfoRequest(
            profileBytes.data(), profileBytes.size(), profile, &error), error);
        Require(profile.operation == 0u && profile.targetUin == 1507781713u,
            "official RoomUGC creator profile request parsed incorrectly");
        Require(BuildPlayerInfoResponse(profile, kCapturedUin, response, &error), error);
        RequireEqual(
            response,
            ClassApplication(payloads /
                "network_bin_6752_9451_parser_queue_dispatch_body_msg28473_len306.bin"),
            "RoomUGC creator profile response drifted from dump6752");

        const auto operation6Bytes = ClassApplication(
            payloads / "network_bin_6752_9284_pack_before_body_msg28473_len25.bin");
        PlayerInfoRequest operation6;
        Require(ParsePlayerInfoRequest(
            operation6Bytes.data(), operation6Bytes.size(), operation6, &error), error);
        Require(operation6.operation == 6u,
            "official RoomUGC operation-6 request parsed incorrectly");
        Require(BuildPlayerInfoResponse(operation6, kCapturedUin, response, &error), error);
        RequireEqual(
            response,
            ClassApplication(payloads /
                "network_bin_6752_9315_parser_queue_dispatch_body_msg28473_len42.bin"),
            "RoomUGC operation-6 response drifted from dump6752");
    }

    void TestRoomQuickRpc(const std::filesystem::path& payloads)
    {
        using namespace QQSpeedUGCQuickRpc;
        std::string error;
        std::vector<std::uint8_t> response;
        ResponseInfo info;

        const auto expectedConfig = Slice(
            payloads /
                "network_bin_6752_11450_special28346_after_body_msg24251_co.DispatchCoResponse_len32258.bin",
            254u, 1250u);
        const double configContext = ResponseContext(expectedConfig);

        const auto safeConfigRequest = ReadBytes(
            payloads /
                "network_bin_6752_11437_safe28346_builder_inner_body_at12_msg24251_co.RoomUGCGetCfg_len76.bin");
        std::vector<std::uint8_t> plainConfigRequest;
        Require(QQSpeedMsg24251::DecodeSafeRequestToPlain(
                    safeConfigRequest.data(), safeConfigRequest.size(),
                    plainConfigRequest, &error),
            error);
        Require(plainConfigRequest.size() >= 17u &&
                ReadBE16(plainConfigRequest, 0u) == 13u,
            "RoomUGC config safe request decoded to an invalid plain header");
        const std::size_t configHeadLength = ReadBE16(plainConfigRequest, 0u);
        const std::size_t configMessageLength =
            ReadBE16(plainConfigRequest, configHeadLength + 2u);
        Require(configHeadLength + 4u + configMessageLength ==
                plainConfigRequest.size(),
            "RoomUGC config safe request plain length is inconsistent");
        const std::vector<std::uint8_t> capturedConfigMessage(
            plainConfigRequest.begin() + configHeadLength + 4u,
            plainConfigRequest.end());
        Require(BuildOfficialResponse(
                    capturedConfigMessage.data(), capturedConfigMessage.size(),
                    response, &info, &error, kCapturedUin) == BuildResult::Built,
            error);
        RequireEqual(response, expectedConfig,
            "RoomUGC config response for captured UIN request drifted from dump6752");

        const std::array<const char*, 5> configRequestFixtures = {{
            "network_bin_6752_3279_safe28346_builder_inner_body_at12_msg24251_co.RoomUGCGetCfg_len67.bin",
            "network_bin_6752_6912_safe28346_builder_inner_body_at12_msg24251_co.RoomUGCGetCfg_len71.bin",
            "network_bin_6752_9659_safe28346_builder_inner_body_at12_msg24251_co.RoomUGCGetCfg_len75.bin",
            "network_bin_6752_11437_safe28346_builder_inner_body_at12_msg24251_co.RoomUGCGetCfg_len76.bin",
            "network_bin_6752_9191_safe28346_builder_inner_body_at12_msg24251_co.RoomUGCGetCfg_len80.bin",
        }};
        for (const char* fixtureName : configRequestFixtures)
        {
            const auto safeRequest = ReadBytes(payloads / fixtureName);
            std::vector<std::uint8_t> plainRequest;
            Require(QQSpeedMsg24251::DecodeSafeRequestToPlain(
                        safeRequest.data(), safeRequest.size(), plainRequest, &error),
                std::string(fixtureName) + ": " + error);
            Require(plainRequest.size() >= 17u && ReadBE16(plainRequest, 0u) == 13u,
                std::string(fixtureName) + ": invalid plain QuickRPC header");
            const std::size_t headLength = ReadBE16(plainRequest, 0u);
            const std::size_t messageLength = ReadBE16(plainRequest, headLength + 2u);
            Require(headLength + 4u + messageLength == plainRequest.size(),
                std::string(fixtureName) + ": inconsistent plain QuickRPC length");
            const std::vector<std::uint8_t> requestMessage(
                plainRequest.begin() + headLength + 4u, plainRequest.end());
            Require(BuildOfficialResponse(
                        requestMessage.data(), requestMessage.size(), response,
                        &info, &error, kCapturedUin) == BuildResult::Built,
                std::string(fixtureName) + ": " + error);
            auto expectedForContext = expectedConfig;
            std::memcpy(expectedForContext.data() + 21u,
                &info.context, sizeof(info.context));
            RequireEqual(response, expectedForContext,
                (std::string(fixtureName) +
                    ": RoomUGC config response drifted").c_str());
        }

        const auto configRequest = MakeQuickRequest("Co.RoomUGCGetCfg", configContext);
        Require(BuildOfficialResponse(
                    configRequest.data(), configRequest.size(), response, &info, &error) ==
                BuildResult::Built,
            error);
        RequireEqual(response, expectedConfig, "RoomUGC config response drifted from dump6752");
        Require(info.command == "Co.RoomUGCGetCfg" && info.context == configContext,
            "RoomUGC config response metadata is wrong");
        Require(ContainsText(response, "Exchange") &&
                ContainsText(response, "PlayerInfo") &&
                ContainsText(response, "themeId") &&
                ContainsText(response, "2026-07-30 0-0-0") &&
                ContainsText(response, "2026-09-30 23-59-59"),
            "RoomUGC seasonal/background config fields are incomplete");

        const double otherConfigContext = configContext + 1000.0;
        const auto otherUinConfigRequest = MakeQuickRequest(
            "Co.RoomUGCGetCfg", otherConfigContext,
            { static_cast<double>(kCapturedUin + 1u) });
        Require(BuildOfficialResponse(
                    otherUinConfigRequest.data(), otherUinConfigRequest.size(),
                    response, nullptr, &error, kCapturedUin) ==
                BuildResult::Built,
            "RoomUGC config incorrectly bound the RPC argument to the transport session");
        Require(ResponseContext(response) == otherConfigContext,
            "RoomUGC config did not correlate a dynamic request context");

        const auto malformedConfigRequest = MakeQuickRequest(
            "Co.RoomUGCGetCfg", configContext, { 10001.5 });
        Require(BuildOfficialResponse(
                    malformedConfigRequest.data(), malformedConfigRequest.size(),
                    response, nullptr, &error, kCapturedUin) ==
                BuildResult::InvalidRequest,
            "RoomUGC config accepted a non-integral UIN argument");

        const auto expectedProfile = Slice(
            payloads /
                "network_bin_6752_11535_special28346_after_body_msg24251_co.DispatchCoResponse_len32258.bin",
            254u, 542u);
        const double profileContext = ResponseContext(expectedProfile);
        const auto profileRequest = MakeQuickRequest(
            "Co.RoomUGCGetPlayerProfileSnapshot",
            profileContext,
            { 1507781713.0 });
        Require(BuildOfficialResponse(
                    profileRequest.data(), profileRequest.size(), response, &info, &error) ==
                BuildResult::Built,
            error);
        RequireEqual(response, expectedProfile,
            "RoomUGC QuickRPC profile response drifted from dump6752");
        Require(ContainsText(response, "RoomUGCArtistLevelName") &&
                ContainsText(response, "RoomUGCArtistLevel"),
            "RoomUGC QuickRPC profile omitted artist fields");

        const auto unknownRequest = MakeQuickRequest(
            "Co.RoomUGCGetPlayerProfileSnapshot",
            profileContext,
            { 123456789.0 });
        Require(BuildOfficialResponse(
                    unknownRequest.data(), unknownRequest.size(), response, nullptr, &error) ==
                BuildResult::InvalidRequest,
            "uncaptured RoomUGC QuickRPC profile was fabricated");
    }

    void TestAuroraTogetherCreate(const std::filesystem::path& payloads)
    {
        const auto safeCreate = ReadBytes(
            payloads /
                "network_bin_6752_5661_safe28346_builder_inner_body_at12_msg103_len240.bin");
        Require(safeCreate.size() == 240u,
            "official Aurora together Msg103 fixture length is wrong");

        QQSpeedMsg103::RequestFields fields;
        std::vector<std::uint8_t> plain;
        std::string error;
        Require(QQSpeedMsg103::DecodeSafeRequestToPlain(
                safeCreate.data(), safeCreate.size(), plain, &fields, &error),
            error);
        Require(fields.uin == kCapturedUin && fields.baseGameMode == 1u &&
                fields.subGameMode == 1u && fields.seatNum == 6u &&
                fields.isNewerOpen == 1u && fields.fbSeason == 5u &&
                fields.specialFlag == 10u,
            "official Aurora together create-room fields decoded incorrectly");
        Require(fields.extendedRoomInfoCount == 1u &&
                fields.auroraRoomInfo.workId == 1128046849ull &&
                fields.auroraRoomInfo.tags == std::vector<std::uint32_t>({ 15u }) &&
                fields.auroraRoomInfo.maxPlayers == 6u,
            "official Msg103 AuroraInfo identity fields decoded incorrectly");
        Require(fields.auroraRoomInfo.coverUrl.empty() &&
                fields.auroraRoomInfo.workName.empty() &&
                fields.auroraRoomInfo.version == 0u &&
                fields.auroraRoomInfo.difficulty == 0u &&
                fields.auroraRoomInfo.playCount == 0u &&
                fields.auroraRoomInfo.likeCount == 0u &&
                fields.auroraRoomInfo.favoriteCount == 0u &&
                fields.auroraRoomInfo.auroraName[0] != 0u,
            "official Msg103 AuroraInfo optional fields decoded incorrectly");
        Require(plain.size() == 72u && ReadBE32(plain, 0u) == kCapturedUin &&
                ReadBE16(plain, 8u) == 1u && ReadBE16(plain, 10u) == 1u &&
                plain[36] == 6u,
            "Aurora together Msg103 did not preserve the legacy create-room body");

        auto truncated = safeCreate;
        truncated.pop_back();
        Require(!QQSpeedMsg103::DecodeSafeRequestToPlain(
                truncated.data(), truncated.size(), plain, nullptr, &error),
            "truncated Aurora together Msg103 was accepted");

        const auto officialCreateResponse = ClassApplication(
            payloads /
                "network_bin_6752_5668_inner_dispatch_body_msg103_len162.bin");
        Require(officialCreateResponse.size() == 148u &&
                ReadBE64(officialCreateResponse, 140u) == 1128046849ull,
            "official Aurora create-room response tail has the wrong work ID");
    }

    void TestAuroraPrepareReady(const std::filesystem::path& payloads)
    {
        const auto safePrepare = ReadBytes(
            payloads /
                "network_bin_6752_7915_safe28346_builder_inner_body_at12_msg120_len79.bin");
        Require(safePrepare.size() == 79u,
            "official Aurora Msg120 fixture length is wrong");

        std::vector<std::uint8_t> plain;
        std::string error;
        Require(QQSpeedMsg120::DecodeSafeRequestToPlain(
                safePrepare.data(), safePrepare.size(), plain, &error),
            error);
        Require(plain.size() == 0x2Eu &&
                ReadBE32(plain, 0u) == kCapturedUin &&
                ReadBE32(plain, 8u) == 0u &&
                std::string(plain.begin() + 12u, plain.begin() + 44u) ==
                    "86AF5BB8F5B0A5773D07091171229BC2" &&
                plain[44u] == 0u && plain[45u] == 0u,
            "official Aurora Msg120 decoded fields are wrong");
    }

    void TestAuroraMatchResult(const std::filesystem::path& payloads)
    {
        const auto officialDetailRequest = ClassApplication(
            payloads /
                "network_bin_6752_5529_pack_before_body_msg28476_len46.bin");
        QQSpeedAuroraOfficial::BrowseRequest detailRequest;
        std::string error;
        Require(QQSpeedAuroraOfficial::ParseBrowseRequest(
                officialDetailRequest.data(), officialDetailRequest.size(),
                detailRequest, &error),
            error);
        Require(detailRequest.operateType == 4u &&
                detailRequest.workId == 1128046849ull,
            "official dump6752 Aurora op=4 request parsed incorrectly");
        std::vector<std::uint8_t> response;
        Require(QQSpeedAuroraOfficial::BuildBrowseResponse(
                detailRequest, kCapturedUin, response, &error),
            error);
        auto expectedDetail = ClassApplication(payloads /
            "network_bin_6752_5542_parser_queue_dispatch_body_msg28476_len642.bin");
        NormalizeAuroraWorkMetadata(expectedDetail);
        const std::size_t nameEnd = 27u + expectedDetail[26u];
        const std::size_t briefTail = nameEnd + 2u + ReadBE16(expectedDetail, nameEnd);
        WriteBE32(expectedDetail, briefTail + 28u,
            QQSpeedAuroraOfficial::kKissCreatorUin);
        WriteBE32(expectedDetail, briefTail + 40u, 1u);
        RequireEqual(
            response,
            expectedDetail,
            "Aurora op=4 detail changed beyond normalized metadata and creator");
        Require(ReadString8(response, 26u) == "Kiss" &&
                ReadString8(response,
                    AuroraDescriptionLengthOffset(response, 16u)) ==
                    ExpectedDescriptionRaw(),
            "Aurora normalized work metadata is missing");

        const auto officialMatch = ClassApplication(
            payloads /
                "network_bin_6752_7837_parser_queue_dispatch_body_msg20081_len130.bin");
        Require(officialMatch.size() == 116u && officialMatch[4] == 8u &&
                officialMatch[5] == 1u && ReadBE16(officialMatch, 6u) == 84u &&
                ReadBE32(officialMatch, 90u) == 0u &&
                ReadBE16(officialMatch, 94u) == 0u &&
                ReadBE16(officialMatch, 96u) == 19u &&
                officialMatch[98] == 1u &&
                ReadBE16(officialMatch, 113u) == 19u &&
                officialMatch[115] == 0u,
            "official dump6752 Msg20081 queue-8 fields were decoded incorrectly");

        const auto officialEnterRequest = ClassApplication(
            payloads /
                "network_bin_6752_7849_pack_before_body_msg20082_len14.bin");
        const auto officialEnterResponse = ClassApplication(
            payloads /
                "network_bin_6752_7851_parser_queue_dispatch_body_msg20082_len18.bin");
        Require(officialEnterRequest.size() == 12u &&
                ReadBE32(officialEnterRequest, 0u) == kCapturedUin &&
                ReadBE32(officialEnterRequest, 8u) == 253u,
            "official dump6752 Msg20082 request fields were decoded incorrectly");
        Require(officialEnterResponse == std::vector<std::uint8_t>({ 0u, 0u, 0u, 0u }),
            "official dump6752 Msg20082 response fields were decoded incorrectly");

        QQSpeedAuroraOfficial::MatchResultInfo info;
        info.gameServerId = 0x024Fu;
        info.serverIp = 0xC75C6C71u;
        info.tcpPorts = { 10095u, 10096u, 10097u };
        info.udpPorts = { 10095u };
        info.multiServerIps = {
            0xC75C6C71u, 0x1837F13Du, 0x92033570u, 0xE6101BAFu
        };
        info.domain = "dxpvp.speed.qq.com";
        info.serverIdentity = "1.0.75.591";
        info.routeId = 0x3D746E756F436C61ull;
        info.roomId = 253u;
        info.pvpKey = 0x3BAA4B73u;
        info.gameSequenceHigh = 0x009BF9F2u;
        info.gameSequenceLow = 0x7335C9F8u;

        response.clear();
        Require(QQSpeedAuroraOfficial::BuildMatchResultResponse(
            info, response, &error), error);
        RequireEqual(response, officialMatch,
            "Aurora Msg20081 full endpoint schema drifted from dump6752");

        QQSpeedAuroraOfficial::MatchResultInfo local;
        local.gameServerId = 0x024Fu;
        local.serverIp = 0x7F000001u;
        local.tcpPorts = { 10095u };
        local.udpPorts = { 10096u };
        local.roomId = 0x12345678u;
        local.pvpKey = 0x3BAA4B73u;
        local.gameSequenceHigh = 0x009BF9F2u;
        local.gameSequenceLow = 0x7335C9F8u;
        Require(QQSpeedAuroraOfficial::BuildMatchResultResponse(
            local, response, &error), error);
        Require(response.size() == 64u, "Aurora local Msg20081 payload size is wrong");
        Require(ReadBE16(response, 0u) == 0u && ReadBE16(response, 2u) == 0u &&
                response[4] == 8u && response[5] == 1u,
            "Aurora Msg20081 result/queue/enter header is wrong");
        Require(ReadBE16(response, 6u) == 32u && ReadBE16(response, 8u) == 24u &&
                ReadBE16(response, 10u) == local.gameServerId &&
                ReadBE32(response, 12u) == 0x7F000001u &&
                response[16] == 1u && ReadBE16(response, 17u) == local.tcpPorts[0] &&
                response[19] == 1u && ReadBE16(response, 20u) == local.udpPorts[0] &&
                response[22] == 0u && response[23] == 0u &&
                ReadBE32(response, 24u) == 0u && ReadBE32(response, 28u) == 0u,
            "Aurora Msg20081 endpoint block is wrong");
        Require(ReadBE32(response, 32u) == local.roomId && ReadBE16(response, 36u) == 0u &&
                ReadBE32(response, 38u) == 0u && ReadBE16(response, 42u) == 0u &&
                ReadBE16(response, 44u) == 19u && response[46] == 1u &&
                ReadBE32(response, 47u) == local.pvpKey &&
                ReadBE32(response, 51u) == local.gameSequenceHigh &&
                ReadBE32(response, 55u) == local.gameSequenceLow &&
                ReadBE16(response, 59u) == 0u && ReadBE16(response, 61u) == 19u &&
                response[63] == 0u,
            "Aurora Msg20081 official queue-8 match fields are wrong");

        QQSpeedAuroraOfficial::MatchResultInfo incomplete;
        Require(!QQSpeedAuroraOfficial::BuildMatchResultResponse(
            incomplete, response, &error),
            "incomplete Aurora match endpoint was accepted");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4,
            "usage: CoCreation6752Regression <packet_dumps_6752> <packet_dumps_17188> <packet_dumps_20932>");
        const std::filesystem::path payloads =
            std::filesystem::path(argv[1]) / "payloads";
        const std::filesystem::path payloads17188 =
            std::filesystem::path(argv[2]) / "payloads";
        const std::filesystem::path payloads20932 =
            std::filesystem::path(argv[3]) / "payloads";
        Require(std::filesystem::is_directory(payloads),
            "packet_dumps_6752 payload directory not found");
        Require(std::filesystem::is_directory(payloads17188),
            "packet_dumps_17188 payload directory not found");
        Require(std::filesystem::is_directory(payloads20932),
            "packet_dumps_20932 payload directory not found");

        TestRoomBrowse(payloads);
        TestRoomInteractAndProfiles(payloads);
        TestRoomQuickRpc(payloads);
        TestAuroraTogetherCreate(payloads);
        TestAuroraPrepareReady(payloads);
        TestAuroraMatchResult(payloads);
        TestRoomPurchaseEquipAndTheme17188(payloads17188);
        TestRoomUnequip20932(payloads20932);
        TestRoomSkinCreate20932(payloads20932);
        std::cout << "CoCreation official-dump regression passed" << std::endl;
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "CoCreation official-dump regression failed: "
                  << exception.what() << std::endl;
        return 1;
    }
}
