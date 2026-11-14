#include "../AuroraOfficialUGC.h"
#include "../UGCQuickRpc.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace
{
using QQSpeedAuroraOfficial::BrowseRequest;
using QQSpeedAuroraOfficial::RoomAuroraInfo;
using QQSpeedAuroraOfficial::RoomChangeRequest;
using QQSpeedAuroraOfficial::StartGameRequest;

[[noreturn]] void Fail(const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void Check(bool value, const char* message)
{
    if (!value) Fail(message);
}

std::uint16_t ReadBE16(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    Check(offset + 2 <= data.size(), "ReadBE16 out of range");
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[offset]) << 8u) | data[offset + 1u]);
}

std::uint32_t ReadBE32(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    Check(offset + 4 <= data.size(), "ReadBE32 out of range");
    return (static_cast<std::uint32_t>(data[offset]) << 24u) |
        (static_cast<std::uint32_t>(data[offset + 1u]) << 16u) |
        (static_cast<std::uint32_t>(data[offset + 2u]) << 8u) |
        static_cast<std::uint32_t>(data[offset + 3u]);
}

std::uint64_t ReadBE64(const std::vector<std::uint8_t>& data, std::size_t offset)
{
    Check(offset + 8 <= data.size(), "ReadBE64 out of range");
    std::uint64_t result = 0;
    for (std::size_t index = 0; index < 8; ++index)
        result = (result << 8u) | data[offset + index];
    return result;
}

std::string ReadString8(
    const std::vector<std::uint8_t>& data,
    std::size_t lengthOffset)
{
    Check(lengthOffset < data.size(), "uint8 string length out of range");
    const std::size_t length = data[lengthOffset];
    Check(lengthOffset + 1u + length <= data.size(), "uint8 string out of range");
    return std::string(
        data.begin() + lengthOffset + 1u,
        data.begin() + lengthOffset + 1u + length);
}

const std::string& ExpectedDescriptionRaw()
{
    static const std::string value(
        "\xBB\xB6\xD3\xAD\xCC\xE5\xD1\xE9", 8u); // 欢迎体验 in GB18030
    return value;
}

std::size_t BriefTailOffset(
    const std::vector<std::uint8_t>& data,
    std::size_t briefStart)
{
    const std::size_t nameLengthOffset = briefStart + 10u;
    Check(nameLengthOffset < data.size(), "brief name length out of range");
    const std::size_t coverLengthOffset =
        nameLengthOffset + 1u + data[nameLengthOffset];
    return coverLengthOffset + 2u + ReadBE16(data, coverLengthOffset);
}

std::size_t DetailDescriptionLengthOffset(
    const std::vector<std::uint8_t>& data,
    std::size_t briefStart)
{
    std::size_t cursor = briefStart + ReadBE16(data, briefStart);
    for (int index = 0; index < 2; ++index)
        cursor += 2u + ReadBE16(data, cursor);
    cursor += 32u + 37u;
    Check(cursor < data.size(), "detail description length out of range");
    return cursor;
}

std::vector<std::uint8_t> Hex(const char* text)
{
    std::vector<std::uint8_t> value;
    for (std::size_t index = 0; text[index] != '\0'; index += 2)
    {
        const auto nibble = [](char ch) -> std::uint8_t {
            if (ch >= '0' && ch <= '9') return static_cast<std::uint8_t>(ch - '0');
            if (ch >= 'a' && ch <= 'f') return static_cast<std::uint8_t>(ch - 'a' + 10);
            if (ch >= 'A' && ch <= 'F') return static_cast<std::uint8_t>(ch - 'A' + 10);
            Fail("invalid hex fixture");
        };
        Check(text[index + 1] != '\0', "odd hex fixture");
        value.push_back(static_cast<std::uint8_t>(
            (nibble(text[index]) << 4u) | nibble(text[index + 1])));
    }
    return value;
}

void AppendBE16(std::vector<std::uint8_t>& data, std::uint16_t value)
{
    data.push_back(static_cast<std::uint8_t>(value >> 8u));
    data.push_back(static_cast<std::uint8_t>(value));
}

void AppendBE32(std::vector<std::uint8_t>& data, std::uint32_t value)
{
    data.push_back(static_cast<std::uint8_t>(value >> 24u));
    data.push_back(static_cast<std::uint8_t>(value >> 16u));
    data.push_back(static_cast<std::uint8_t>(value >> 8u));
    data.push_back(static_cast<std::uint8_t>(value));
}

void AppendBE64(std::vector<std::uint8_t>& data, std::uint64_t value)
{
    AppendBE32(data, static_cast<std::uint32_t>(value >> 32u));
    AppendBE32(data, static_cast<std::uint32_t>(value));
}

void AppendString8(std::vector<std::uint8_t>& data, const std::string& value)
{
    Check(value.size() <= 0xFFu, "test uint8 string too long");
    data.push_back(static_cast<std::uint8_t>(value.size()));
    data.insert(data.end(), value.begin(), value.end());
}

void AppendString16(std::vector<std::uint8_t>& data, const std::string& value)
{
    Check(value.size() <= 0xFFFFu, "test uint16 string too long");
    AppendBE16(data, static_cast<std::uint16_t>(value.size()));
    data.insert(data.end(), value.begin(), value.end());
}

BrowseRequest ParseBrowse(const char* framedHex)
{
    auto framed = Hex(framedHex);
    Check(framed.size() >= 2, "browse fixture too short");
    BrowseRequest request;
    std::string error;
    const bool parsed = QQSpeedAuroraOfficial::ParseBrowseRequest(
        framed.data() + 2, framed.size() - 2, request, &error);
    Check(parsed, error.c_str());
    return request;
}

void TestBrowseRecommendationAndDetails()
{
    constexpr std::uint32_t kSessionUin = 0x10203040u;
    constexpr std::size_t kExpectedPlayableWorkCount = 77u;
    constexpr std::size_t kExpectedRecommendationCount = 88u;
    auto recommend = ParseBrowse(
        "002e371ab0bd0471f2350a0000000000000000000006000000000000000000000000000000000000000000000000");
    Check(recommend.operateType == 10, "recommend operation mismatch");
    recommend.uin = kSessionUin;

    std::vector<std::uint8_t> response;
    std::string error;
    bool built = QQSpeedAuroraOfficial::BuildBrowseResponse(
        recommend, kSessionUin, response, &error);
    Check(built, error.c_str());
    Check(response.size() > 418, "captured recommendation payload was not expanded");
    Check(ReadBE32(response, 0) == kSessionUin, "recommend response UIN not patched");
    Check(response[7] == 10, "recommend response operation mismatch");
    Check(ReadBE32(response, 8) == kExpectedRecommendationCount,
        "recommend total must expose every captured card");
    Check(response[12] == 6, "first recommendation page must contain six briefs");

    std::size_t brief = 13;
    Check(ReadBE16(response, brief) >= 13u, "first recommendation brief is truncated");
    Check(ReadBE64(response, brief + 2) == 1074294017ULL, "first dump10264 work ID mismatch");
    Check(ReadString8(response, brief + 10u) == "Kiss",
        "recommendation work name was not normalized to Kiss");
    Check(ReadBE32(response, BriefTailOffset(response, brief) + 28u) ==
        QQSpeedAuroraOfficial::kKissCreatorUin,
        "recommendation brief must use the synthetic Kiss creator UIN");
    Check(ReadBE32(response, BriefTailOffset(response, brief) + 40u) == 1u,
        "recommendation brief must use the Kiss profile area");

    BrowseRequest newDetail = recommend;
    newDetail.operateType = 4;
    newDetail.workId = 1074294017ULL;
    response.clear();
    built = QQSpeedAuroraOfficial::BuildBrowseResponse(
        newDetail, kSessionUin, response, &error);
    Check(built, error.c_str());
    Check(ReadBE64(response, 18) == newDetail.workId,
        "dump10264 detail work ID mismatch");
    const std::size_t nestedBrief = 16u;
    const std::size_t briefTailOffset = BriefTailOffset(response, nestedBrief);
    Check(ReadString8(response, nestedBrief + 10u) == "Kiss",
        "detail work name was not normalized to Kiss");
    Check(ReadString8(response,
            DetailDescriptionLengthOffset(response, nestedBrief)) ==
            ExpectedDescriptionRaw(),
        "detail description was not normalized to 欢迎体验");
    Check(ReadBE32(response, briefTailOffset + 28) == 0x4B495353u,
        "detail response must use the synthetic Kiss creator UIN");
    Check(ReadBE32(response, briefTailOffset + 40) == 1u,
        "detail response must use the Kiss profile area");

    Check(QQSpeedAuroraOfficial::WorkCount() == kExpectedPlayableWorkCount,
        "captured catalog must expose all unique op=4 works");
    Check(QQSpeedAuroraOfficial::WorkAt(kExpectedPlayableWorkCount) == nullptr,
        "catalog index past the end must be rejected");
    for (std::size_t index = 0; index < QQSpeedAuroraOfficial::WorkCount(); ++index)
    {
        const auto* work = QQSpeedAuroraOfficial::WorkAt(index);
        Check(work != nullptr, "catalog work missing by index");
        Check(QQSpeedAuroraOfficial::FindWork(work->workId) == work,
            "catalog lookup and index lookup differ");
        Check(work->ownerUin == QQSpeedAuroraOfficial::kKissCreatorUin,
            "catalog owner must be the synthetic Kiss creator");
        Check(work->mapId == 499u && work->version != 0u,
            "catalog work lacks playable 509 metadata");
        Check(std::string(work->md5).size() == 32u,
            "catalog work MD5 must fill MapHash[32]");

        BrowseRequest detail = recommend;
        detail.operateType = 4u;
        detail.workId = work->workId;
        detail.uin = kSessionUin;
        response.clear();
        built = QQSpeedAuroraOfficial::BuildBrowseResponse(
            detail, kSessionUin, response, &error);
        Check(built, error.c_str());
        Check(ReadBE32(response, 0) == kSessionUin, "detail response UIN not patched");
        Check(response[7] == 4 && ReadBE32(response, 8) == 1,
            "detail response common header mismatch");
        Check(response[12] == 0 && response[13] == 1,
            "detail response must contain one detail and no brief list");
        Check(ReadBE64(response, 18) == work->workId,
            "detail response nested work ID mismatch");

        const std::string bytes(response.begin(), response.end());
        Check(bytes.find(work->renderUrl) != std::string::npos,
            "detail response missing official render package URL");
        Check(bytes.find(work->md5) != std::string::npos,
            "detail response missing official package MD5");
        const auto tailOffset = BriefTailOffset(response, 16u);
        Check(ReadString8(response, 26u) == "Kiss",
            "captured detail carries a non-Kiss work name");
        Check(ReadString8(response,
                DetailDescriptionLengthOffset(response, 16u)) ==
                ExpectedDescriptionRaw(),
            "captured detail carries a non-normalized description");
        Check(ReadBE32(response, tailOffset + 28u) ==
            QQSpeedAuroraOfficial::kKissCreatorUin,
            "captured detail did not patch its creator UIN");
        Check(ReadBE32(response, tailOffset + 40u) == 1u,
            "captured detail did not patch its creator area");

        RoomAuroraInfo roomInfo;
        Check(ResolveRoomAuroraInfo(work->workId, roomInfo, &error),
            error.c_str());
        Check(roomInfo.workId == work->workId &&
                roomInfo.version == work->version &&
                roomInfo.auroraNameRaw == "Kiss" &&
                roomInfo.workNameRaw == "Kiss" &&
                !roomInfo.coverUrl.empty(),
            "captured work lacks multiplayer room metadata");
        std::vector<std::uint8_t> roomInfoBytes;
        Check(BuildRoomAuroraInfo(roomInfo, roomInfoBytes, &error),
            error.c_str());
        Check(roomInfoBytes.size() >= 96u &&
                ReadBE16(roomInfoBytes, 0u) == roomInfoBytes.size() &&
                ReadBE64(roomInfoBytes, 2u) == work->workId,
            "captured multiplayer room metadata failed to serialize");
    }

    std::set<std::uint64_t> pagedWorkIds;
    const std::size_t laterWorkCount =
        kExpectedRecommendationCount > 6u ? kExpectedRecommendationCount - 6u : 0u;
    const std::uint16_t pageCount = static_cast<std::uint16_t>(
        1u + (laterWorkCount + 8u) / 9u);
    for (std::uint16_t pageNo = 0; pageNo < pageCount; ++pageNo)
    {
        BrowseRequest page = recommend;
        page.pageNo = pageNo;
        page.pageSize = pageNo == 0u ? 6u : 9u;
        response.clear();
        built = QQSpeedAuroraOfficial::BuildBrowseResponse(
            page, kSessionUin, response, &error);
        Check(built, error.c_str());
        const std::size_t start = pageNo == 0u
            ? 0u
            : 6u + (static_cast<std::size_t>(pageNo) - 1u) * 9u;
        const std::size_t expectedCount = std::min<std::size_t>(
            pageNo == 0u ? 6u : 9u,
            kExpectedRecommendationCount - start);
        Check(response[12] == expectedCount,
            "recommendation page count does not cover the captured catalog");
        std::size_t cursor = 13u;
        for (std::size_t item = 0; item < expectedCount; ++item)
        {
            const auto briefSize = ReadBE16(response, cursor);
            Check(briefSize >= 13u && cursor + briefSize <= response.size(),
                "paged recommendation brief crosses response boundary");
            const auto workId = ReadBE64(response, cursor + 2u);
            Check(pagedWorkIds.insert(workId).second,
                "recommendation pages contain a duplicate work ID");
            const auto tailOffset = BriefTailOffset(response, cursor);
            Check(ReadString8(response, cursor + 10u) == "Kiss",
                "paged recommendation carries a non-Kiss work name");
            Check(ReadBE32(response, tailOffset + 28u) ==
                QQSpeedAuroraOfficial::kKissCreatorUin,
                "paged recommendation carries a non-Kiss creator UIN");
            Check(ReadBE32(response, tailOffset + 40u) == 1u,
                "paged recommendation carries a non-Kiss creator area");
            cursor += briefSize;
        }
    }
    Check(pagedWorkIds.size() == kExpectedRecommendationCount,
        "recommendation pages do not expose every captured card");
    const std::set<std::uint64_t> browseOnlyWorkIds = {
        1073840385ULL,
        1074044161ULL,
        1074128129ULL,
        1074138370ULL,
        1074171137ULL,
        1074246914ULL,
        1074247937ULL,
        1088589057ULL,
        1111024897ULL,
        1128047873ULL,
        1128048897ULL,
    };
    for (const auto workId : browseOnlyWorkIds)
    {
        Check(pagedWorkIds.count(workId) == 1u,
            "captured browse-only card is missing from recommendation pages");
        BrowseRequest detail = recommend;
        detail.operateType = 4u;
        detail.workId = workId;
        Check(!QQSpeedAuroraOfficial::BuildBrowseResponse(
                detail, kSessionUin, response, &error),
            "browse-only Aurora card fabricated an uncaptured detail");
    }
}

void TestBrowseConfigAndShowOff()
{
    constexpr std::uint32_t kSessionUin = 0x55667788u;
    std::string error;
    std::vector<std::uint8_t> response;
    for (const auto operation : {12u, 15u, 0u})
    {
        std::string hex = "002e371ab0bd0471f235";
        const char digits[] = "0123456789abcdef";
        hex += digits[(operation >> 4u) & 0x0Fu];
        hex += digits[operation & 0x0Fu];
        hex += "0000000000000000000000000000000000000000000000000000000000000000000000";
        auto request = ParseBrowse(hex.c_str());
        request.uin = kSessionUin;
        const bool built = QQSpeedAuroraOfficial::BuildBrowseResponse(
            request, kSessionUin, response, &error);
        Check(built, error.c_str());
        Check(ReadBE32(response, 0) == kSessionUin, "aux browse UIN not patched");
        Check(response[7] == operation, "aux browse operation mismatch");
        if (operation == 0u)
        {
            Check(response.size() == 159u,
                "qualified draft-list payload size mismatch");
            Check(response[77] == 0u,
                "draft-list response did not grant official qualification");
            Check(ReadBE16(response, 78u) == 57u,
                "draft-list questionnaire URL length mismatch");
        }
    }
}

void TestStartGameAndGameBeginTail()
{
    constexpr std::uint32_t kSessionUin = 0x12345678u;
    auto framed = Hex(
        "0020371ab0bd000400000000000000000000000000000000000000004003fd14");
    StartGameRequest parsedRequest;
    std::string error;
    Check(QQSpeedAuroraOfficial::ParseStartGameRequest(
        framed.data() + 2, framed.size() - 2, parsedRequest, &error),
        error.c_str());
    Check(parsedRequest.startType == 4u && parsedRequest.workId == 1074003220ULL,
        "Aurora start request parse mismatch");

    for (std::size_t index = 0; index < QQSpeedAuroraOfficial::WorkCount(); ++index)
    {
        const auto* work = QQSpeedAuroraOfficial::WorkAt(index);
        Check(work != nullptr, "start-game catalog entry missing");
        StartGameRequest request{};
        request.uin = kSessionUin;
        request.startType = 4u;
        request.mapId = work->mapId;
        request.workId = work->workId;

        std::vector<std::uint8_t> response;
        bool built = QQSpeedAuroraOfficial::BuildStartGameResponse(
            request, kSessionUin, response, &error);
        Check(built, error.c_str());
        Check(response.size() == 21, "28437 response payload length mismatch");
        Check(ReadBE32(response, 0) == kSessionUin, "28437 response UIN mismatch");
        Check(ReadBE16(response, 4) == 0 && response[6] == 0,
            "28437 response result mismatch");
        Check(ReadBE16(response, 7) == 4, "28437 response start type mismatch");
        Check(ReadBE32(response, 17) == 0,
            "28437 iPara1 must exclude capture-hook over-read bytes");

        std::vector<std::uint8_t> tail;
        built = QQSpeedAuroraOfficial::BuildGameBeginTail(work->workId, tail, &error);
        Check(built, error.c_str());
        Check(ReadBE64(tail, 0) == work->workId, "509 Aurora tail work ID mismatch");
        Check(tail[8] == 0, "official 509 capture has empty work-name field");
        Check(ReadBE32(tail, 9) == work->version, "509 Aurora version mismatch");
        const auto urlLength = ReadBE16(tail, 13);
        Check(urlLength == std::string(work->renderUrl).size(), "509 URL length mismatch");
        Check(std::string(tail.begin() + 15, tail.end()) == work->renderUrl,
            "509 official render URL mismatch");
        Check(std::string(work->md5).size() == 32, "official MD5 must fill MapHash[32]");
        Check(work->mapId == 499, "official decoded 509 MapID mismatch");
    }
}

void TestEntryQuickRpcResponses()
{
    constexpr double kContext = 271.0;
    for (const char* command : {
        "Co.RoomUGCCheckNotifyRedPointOnLogin",
        "Co.RoomUGCGetCfg",
        "Co.AuroraUGCGetCfg",
        "Co.AuroraUGCCheckNotifyRedPointOnLogin",
        "Co.GetAuroraShowOffCfg",
    })
    {
        std::vector<std::uint8_t> request(command, command + std::string(command).size());
        request.push_back(0);
        request.push_back(0);
        request.push_back(0xF6);
        const auto* contextBytes = reinterpret_cast<const std::uint8_t*>(&kContext);
        request.insert(request.end(), contextBytes, contextBytes + sizeof(kContext));

        std::vector<std::uint8_t> response;
        QQSpeedUGCQuickRpc::ResponseInfo info;
        std::string error;
        const auto result = QQSpeedUGCQuickRpc::BuildOfficialResponse(
            request.data(), request.size(), response, &info, &error);
        Check(result == QQSpeedUGCQuickRpc::BuildResult::Built, error.c_str());
        Check(info.command == command && info.context == kContext,
            "QuickRPC response metadata mismatch");
        const std::string bytes(response.begin(), response.end());
        Check(bytes.rfind("DispatchCoResponse", 0) == 0,
            "QuickRPC response dispatch command mismatch");
        if (std::string(command) == "Co.AuroraUGCGetCfg")
        {
            Check(bytes.find("DefaultCoverUrl") != std::string::npos &&
                    bytes.find("aurora_template_map_20260625.png") !=
                        std::string::npos &&
                    bytes.find("BannerSlots") != std::string::npos,
                "Aurora config is missing the captured cover/background fields");
        }
    }
}

void TestKissCreatorProfileResponse()
{
    constexpr double kContext = 9001.0;
    constexpr double kKissCreatorUin = static_cast<double>(0x4B495353u);
    const char* command = "Co.BatchGetPlayerProfileSnapshot";
    std::vector<std::uint8_t> request(command, command + std::string(command).size());
    request.push_back(0);
    request.push_back(0);
    request.push_back(0xF6);
    const auto* contextBytes = reinterpret_cast<const std::uint8_t*>(&kContext);
    request.insert(request.end(), contextBytes, contextBytes + sizeof(kContext));
    request.push_back(0xF6);
    const auto* uinBytes = reinterpret_cast<const std::uint8_t*>(&kKissCreatorUin);
    request.insert(request.end(), uinBytes, uinBytes + sizeof(kKissCreatorUin));

    std::vector<std::uint8_t> response;
    QQSpeedUGCQuickRpc::ResponseInfo info;
    std::string error;
    const auto result = QQSpeedUGCQuickRpc::BuildOfficialResponse(
        request.data(), request.size(), response, &info, &error);
    Check(result == QQSpeedUGCQuickRpc::BuildResult::Built, error.c_str());
    Check(info.command == command && info.context == kContext,
        "Kiss profile response metadata mismatch");
    const std::string bytes(response.begin(), response.end());
    Check(bytes.find("Kiss") != std::string::npos,
        "Kiss profile response missing author name");
    const auto uinField = bytes.find("Uin");
    Check(uinField != std::string::npos && uinField + 12u <= response.size(),
        "Kiss profile response missing Uin field");
    Check(response[uinField + 3u] == 0xF6,
        "Kiss profile Uin field is not a Lua number");
    double responseUin = 0.0;
    std::memcpy(&responseUin, response.data() + uinField + 4u, sizeof(responseUin));
    Check(responseUin == kKissCreatorUin,
        "Kiss profile response carries the wrong synthetic UIN");

    constexpr double kUnrelatedUin = 123456.0;
    std::memcpy(request.data() + request.size() - sizeof(double),
        &kUnrelatedUin, sizeof(kUnrelatedUin));
    response.clear();
    Check(QQSpeedUGCQuickRpc::BuildOfficialResponse(
        request.data(), request.size(), response, nullptr, &error) ==
        QQSpeedUGCQuickRpc::BuildResult::NotHandled,
        "unrelated player-profile queries must remain on the legacy path");
}

void TestRejectedProfiles()
{
    std::string error;
    BrowseRequest browse;
    const std::uint8_t shortBrowse[] = {0, 1, 2, 3};
    Check(!QQSpeedAuroraOfficial::ParseBrowseRequest(
        shortBrowse, sizeof(shortBrowse), browse, &error),
        "truncated browse request must be rejected");

    browse = ParseBrowse(
        "002e371ab0bd04741f4604000000004003fd14000000000000000000000000000000000000000000000000000000");
    std::vector<std::uint8_t> output;
    Check(!QQSpeedAuroraOfficial::BuildBrowseResponse(
        browse, browse.uin + 1u, output, &error),
        "cross-session browse UIN must be rejected");
    browse.workId = 0x12345678ULL;
    Check(!QQSpeedAuroraOfficial::BuildBrowseResponse(
        browse, browse.uin, output, &error),
        "uncaptured detail work must be rejected");

    StartGameRequest start;
    auto framed = Hex(
        "0020371ab0bd000400000000000000000000000000000000000000004003fd14");
    Check(QQSpeedAuroraOfficial::ParseStartGameRequest(
        framed.data() + 2, framed.size() - 2, start, &error),
        "valid start fixture failed before negative assertions");
    start.startType = 1;
    Check(!QQSpeedAuroraOfficial::BuildStartGameResponse(
        start, start.uin, output, &error),
        "non-Aurora start type must be rejected");
    Check(!QQSpeedAuroraOfficial::BuildGameBeginTail(
        0x12345678ULL, output, &error),
        "unknown work must not produce a 509 tail");

    const char* unsupported = "Co.NotAurora";
    std::vector<std::uint8_t> rpc(
        unsupported, unsupported + std::string(unsupported).size());
    rpc.insert(rpc.end(), {0, 0, 0xF6, 0, 0, 0, 0, 0, 0, 0, 0});
    Check(QQSpeedUGCQuickRpc::BuildOfficialResponse(
        rpc.data(), rpc.size(), output, nullptr, &error) ==
        QQSpeedUGCQuickRpc::BuildResult::NotHandled,
        "unrelated QuickRPC must remain available to the legacy dispatcher");
}

void TestWireBodyLengthNormalization()
{
    Check(QQSpeedAuroraOfficial::PayloadLengthFromWireBodyLength(46u) == 44u,
        "28476 wire body length must exclude the MSGHEAD length word");
    Check(QQSpeedAuroraOfficial::PayloadLengthFromWireBodyLength(32u) == 30u,
        "28437 wire body length must exclude the MSGHEAD length word");
    Check(QQSpeedAuroraOfficial::PayloadLengthFromWireBodyLength(1u) == 0u,
        "undersized wire body length must be rejected as empty");

    auto browseWire = Hex(
        "002e371ab0bd0471f2350a0000000000000000000006000000000000000000000000000000000000000000000000");
    std::vector<std::uint8_t> browsePayload(
        browseWire.begin() + 2, browseWire.end());
    // The parser receives application fields only. A UIN whose high word is
    // numerically equal to the payload length must not be mistaken for an
    // embedded class-size word.
    browsePayload[0] = 0x00;
    browsePayload[1] = 0x2C;
    BrowseRequest browse;
    std::string error;
    Check(QQSpeedAuroraOfficial::ParseBrowseRequest(
        browsePayload.data(),
        QQSpeedAuroraOfficial::PayloadLengthFromWireBodyLength(browseWire.size()),
        browse, &error),
        "browse UIN high word must not be treated as an embedded length");
    Check(browse.uin == 0x002CB0BDu,
        "browse UIN changed during wire-length normalization");

    auto startWire = Hex(
        "0020371ab0bd000400000000000000000000000000000000000000004003fd14");
    StartGameRequest start;
    Check(QQSpeedAuroraOfficial::ParseStartGameRequest(
        startWire.data() + 2,
        QQSpeedAuroraOfficial::PayloadLengthFromWireBodyLength(startWire.size()),
        start, &error),
        "28437 wire-length-normalized payload must parse");
    Check(start.workId == 1074003220ULL,
        "28437 work ID changed during wire-length normalization");
}

void TestCreatorCompatibleProfiles()
{
    constexpr std::uint32_t kUin = 0x3D729D6Bu;
    constexpr std::uint64_t kWorkId = 0x413F3101ULL;
    std::string error;

    std::vector<std::uint8_t> createPayload;
    AppendBE32(createPayload, kUin);
    AppendBE32(createPayload, 0x0AC9D154u);
    createPayload.push_back(QQSpeedAuroraOfficial::kCreatorCreateDraft);
    AppendBE64(createPayload, 0u);
    AppendString8(createPayload, "");
    AppendString8(createPayload,
        std::string("\xCE\xD2\xB5\xC4\xD7\xF7\xC6\xB7\x31", 9u));
    createPayload.insert(createPayload.end(), {0u, 0u, 0u});
    AppendString8(createPayload, "");
    createPayload.push_back(0u);
    AppendString8(createPayload, "");
    AppendString8(createPayload, "");
    createPayload.push_back(0u);
    createPayload.push_back(0u);
    AppendBE32(createPayload, 0u);
    Check(createPayload.size() == 41u,
        "captured create request size mismatch");
    QQSpeedAuroraOfficial::CreatorRequest create;
    Check(QQSpeedAuroraOfficial::ParseCreatorRequest(
        createPayload.data(), createPayload.size(), create, &error),
        error.c_str());
    Check(QQSpeedAuroraOfficial::IsValidCreateDraftRequest(create, kUin),
        "create request validation mismatch");
    Check(create.nameRaw.size() == 9u && create.workId == 0u,
        "captured create request fields mismatch");
    create.clientRequestId = "optional";
    create.difficulty = 3u;
    create.maxPlayers = 6u;
    create.needRender = 1u;
    create.descriptionRaw = "metadata";
    create.themeIds.push_back(7u);
    create.styleIds.push_back(8u);
    create.componentCount = 2;
    Check(QQSpeedAuroraOfficial::IsValidCreateDraftRequest(create, kUin),
        "create request must allow optional metadata");
    create.nameRaw.clear();
    Check(!QQSpeedAuroraOfficial::IsValidCreateDraftRequest(create, kUin),
        "create request must still require a name");
    auto createWithTail = createPayload;
    createWithTail.push_back(0u);
    Check(!QQSpeedAuroraOfficial::ParseCreatorRequest(
        createWithTail.data(), createWithTail.size(), create, &error),
        "creator parser must reject residual bytes");

    std::vector<std::uint8_t> save;
    AppendBE32(save, kUin);
    AppendBE32(save, 0x6A5C8DA0u);
    save.push_back(QQSpeedAuroraOfficial::kCreatorSaveDraft);
    AppendBE64(save, kWorkId);
    AppendString8(save, "atu-6a5c8da0-413f3101-5c18");
    AppendString8(save, "");
    save.insert(save.end(), {0u, 0u, 0u});
    AppendString8(save, "");
    save.push_back(1u);
    AppendString8(save, "5535EF13A57ECDC57713EC51EB4261E9");
    AppendString8(save, "");
    save.push_back(0u);
    save.push_back(0u);
    AppendBE32(save, 0u);
    Check(save.size() == 90u, "captured SaveDraft payload size mismatch");
    QQSpeedAuroraOfficial::CreatorRequest saveRequest;
    Check(QQSpeedAuroraOfficial::ParseCreatorRequest(
        save.data(), save.size(), saveRequest, &error), error.c_str());
    Check(QQSpeedAuroraOfficial::IsValidSaveDraftRequest(saveRequest, kUin),
        "SaveDraft validation mismatch");
    saveRequest.needRender = 0u;
    saveRequest.descriptionRaw = "compatible metadata";
    saveRequest.componentCount = 4;
    Check(QQSpeedAuroraOfficial::IsValidSaveDraftRequest(
        saveRequest, kUin), "SaveDraft must allow optional metadata flags");
    saveRequest.renderMd5 = "invalid";
    Check(!QQSpeedAuroraOfficial::IsValidSaveDraftRequest(
        saveRequest, kUin), "SaveDraft must reject an invalid MD5");

    std::vector<std::uint8_t> settings = save;
    settings.clear();
    AppendBE32(settings, kUin);
    AppendBE32(settings, 0x6A5C8DBDu);
    settings.push_back(QQSpeedAuroraOfficial::kCreatorUploadSettingsJson);
    AppendBE64(settings, kWorkId);
    AppendString8(settings, "aju-6a5c8dbd-11-26da");
    AppendString8(settings, "");
    settings.insert(settings.end(), {0u, 0u, 0u});
    AppendString8(settings, "");
    settings.push_back(1u);
    AppendString8(settings, "B65DDF4A91FB42E897A6DE929CBF7DA0");
    AppendString8(settings, "");
    settings.push_back(0u);
    settings.push_back(0u);
    AppendBE32(settings, 0u);
    Check(settings.size() == 84u,
        "captured settings-json payload size mismatch");
    QQSpeedAuroraOfficial::CreatorRequest settingsRequest;
    Check(QQSpeedAuroraOfficial::ParseCreatorRequest(
        settings.data(), settings.size(), settingsRequest, &error),
        error.c_str());
    Check(QQSpeedAuroraOfficial::IsValidSettingsJsonRequest(
        settingsRequest, kUin), "settings-json validation mismatch");
    settingsRequest.needRender = 0u;
    settingsRequest.descriptionRaw = "compatible metadata";
    Check(QQSpeedAuroraOfficial::IsValidSettingsJsonRequest(
        settingsRequest, kUin),
        "settings-json must allow optional metadata flags");
    settings.push_back(0u);
    Check(!QQSpeedAuroraOfficial::ParseCreatorRequest(
        settings.data(), settings.size(), settingsRequest, &error),
        "creator parser must reject residual bytes");

    std::vector<std::uint8_t> response;
    Check(QQSpeedAuroraOfficial::BuildCreatorResponse(
        kUin, QQSpeedAuroraOfficial::kCreatorCreateDraft,
        kWorkId, 0u, 0u, response, &error), error.c_str());
    Check(response.size() == 18u && response[7] == 0u &&
        ReadBE64(response, 8u) == kWorkId &&
        response[16] == 0u && response[17] == 0u,
        "captured CreateDraft response mismatch");
    Check(QQSpeedAuroraOfficial::BuildCreatorResponse(
        kUin, QQSpeedAuroraOfficial::kCreatorSaveDraft,
        kWorkId, 1u, 0u, response, &error), error.c_str());
    Check(response[7] == 1u && response[16] == 1u,
        "captured SaveDraft response state mismatch");
    Check(QQSpeedAuroraOfficial::BuildCreatorResponse(
        kUin, QQSpeedAuroraOfficial::kCreatorUploadSettingsJson,
        kWorkId, 0u, 0u, response, &error), error.c_str());
    Check(response[7] == 11u && response[16] == 0u,
        "captured settings response state mismatch");
}

void TestModuleBrowseAndCosCoordination()
{
    constexpr std::uint32_t kUin = 0x3D729D6Bu;
    constexpr std::uint64_t kWorkId = 0x413F3101ULL;
    std::string error;

    const auto moduleWire = Hex(
        "00163d729d6b6a5c8d9e000000000000000000000032");
    Check(moduleWire.size() == 22u, "captured 28480 request size mismatch");
    QQSpeedAuroraOfficial::ModuleBrowseRequest module;
    Check(QQSpeedAuroraOfficial::ParseModuleBrowseRequest(
        moduleWire.data() + 2u, moduleWire.size() - 2u, module, &error),
        error.c_str());
    Check(module.uin == kUin && module.timestamp == 0x6A5C8D9Eu,
        "captured 28480 request fields mismatch");
    std::vector<std::uint8_t> moduleResponse;
    Check(QQSpeedAuroraOfficial::BuildModuleBrowseResponse(
        kUin, moduleResponse, &error), error.c_str());
    Check(moduleResponse.size() == 117u &&
        ReadBE32(moduleResponse, 0u) == kUin &&
        moduleResponse[17] == 0x65u && moduleResponse[19] == 0x63u,
        "captured 28480 response mismatch");
    auto invalidModule = moduleWire;
    invalidModule[10] = 1u;
    Check(QQSpeedAuroraOfficial::ParseModuleBrowseRequest(
        invalidModule.data() + 2u, invalidModule.size() - 2u,
        module, &error), "28480 parser must allow compatible option bytes");
    for (std::size_t index = 4u; index < moduleResponse.size(); ++index)
    {
        if (index == 17u || index == 19u) continue;
        Check(moduleResponse[index] == 0u,
            "28480 response contains non-captured residual data");
    }
    auto invalidModuleWire = moduleWire;
    invalidModuleWire.back() = 0u;
    Check(!QQSpeedAuroraOfficial::ParseModuleBrowseRequest(
        invalidModuleWire.data() + 2u, invalidModuleWire.size() - 2u,
        module, &error), "28480 parser must reject an empty page size");

    const std::string host =
        "speed-image-1258344700.cos.ap-nanjing.myqcloud.com";
    const std::string auth = "q-sign-algorithm=sha1";
    const std::string renderKey =
        "aurora/1094660353/2/render/1784450463";
    std::vector<std::uint8_t> authorization;
    Check(QQSpeedAuroraOfficial::BuildCosAuthorizationNotify(
        kUin, QQSpeedAuroraOfficial::kCosCommandRender,
        host, auth, kWorkId, renderKey, authorization, &error),
        error.c_str());
    Check(ReadBE32(authorization, 0u) == kUin &&
        authorization[4] == QQSpeedAuroraOfficial::kCosCommandRender &&
        authorization[5] == host.size(),
        "28414 authorization header mismatch");

    std::vector<std::uint8_t> report;
    AppendBE32(report, kUin);
    AppendBE32(report, 0x0AC9DC50u);
    AppendBE16(report, 0u);
    report.push_back(QQSpeedAuroraOfficial::kCosCommandRender);
    AppendString16(report, "");
    AppendString16(report, auth);
    AppendBE32(report, 0u);
    AppendBE32(report, static_cast<std::uint32_t>(kWorkId));
    AppendBE32(report, 0u);
    report.push_back(1u);
    AppendString8(report, renderKey);
    QQSpeedAuroraOfficial::UploadReportRequest parsedReport;
    Check(QQSpeedAuroraOfficial::ParseUploadReportRequest(
        report.data(), report.size(), parsedReport, &error), error.c_str());
    Check(parsedReport.uin == kUin &&
        parsedReport.workId == kWorkId &&
        parsedReport.command == QQSpeedAuroraOfficial::kCosCommandRender &&
        parsedReport.objectKeys.size() == 1u &&
        parsedReport.objectKeys[0] == renderKey,
        "28415 report parse mismatch");

    std::vector<std::uint8_t> compatibleReport;
    AppendBE32(compatibleReport, kUin);
    AppendBE32(compatibleReport, 0x0AC9DC50u);
    AppendBE16(compatibleReport, 0u);
    compatibleReport.push_back(QQSpeedAuroraOfficial::kCosCommandRender);
    AppendString16(compatibleReport, "client-uuid");
    AppendString16(compatibleReport, auth);
    AppendBE32(compatibleReport, 7u);
    AppendBE32(compatibleReport, static_cast<std::uint32_t>(kWorkId));
    AppendBE32(compatibleReport, 0u);
    compatibleReport.push_back(1u);
    AppendString8(compatibleReport, renderKey);
    Check(QQSpeedAuroraOfficial::ParseUploadReportRequest(
        compatibleReport.data(), compatibleReport.size(),
        parsedReport, &error),
        "28415 parser must allow UUID and sequence metadata");

    report.push_back(0u);
    Check(!QQSpeedAuroraOfficial::ParseUploadReportRequest(
        report.data(), report.size(), parsedReport, &error),
        "28415 parser must reject residual bytes");

}

void TestUploadCompletionNotifications()
{
    constexpr std::uint32_t kUin = 0x3D729D6Bu;
    constexpr std::uint64_t kWorkId = 0x413F3101ULL;
    const std::string renderUrl =
        "https://image.speed.qq.com/aurora/1094660353/2/render/1784450463";
    const std::string settingsUrl =
        "https://image.speed.qq.com/aurora/1094660353/settings_json/1784450493";
    const std::string nameRaw(
        "\xCE\xD2\xB5\xC4\xD7\xF7\xC6\xB7\x31", 9u);
    std::string error;

    QQSpeedAuroraOfficial::RenderUploadNotify render;
    render.workId = kWorkId;
    render.ownerUin = kUin;
    render.nameRaw = nameRaw;
    render.createdAtMs = 0x0000019F79892FE3ULL;
    render.updatedAtMs = 0x0000019F798939C3ULL;
    render.renderUrl = renderUrl;
    std::vector<std::uint8_t> response;
    Check(QQSpeedAuroraOfficial::BuildRenderUploadNotify(
        kUin, render, response, &error), error.c_str());
    Check(response.size() == 165u &&
        ReadBE32(response, 0u) == kUin &&
        ReadBE64(response, 8u) == kWorkId &&
        ReadBE16(response, 18u) == 81u &&
        ReadBE16(response, 99u) == renderUrl.size() &&
        std::string(response.end() - renderUrl.size(), response.end()) ==
            renderUrl,
        "28487 bounded notification mismatch");

    Check(QQSpeedAuroraOfficial::BuildSettingsJsonUploadNotify(
        kUin, 0x6A5C8DBDu, kWorkId, settingsUrl, response, &error),
        error.c_str());
    Check(response.size() == 88u &&
        ReadBE32(response, 0u) == kUin &&
        ReadBE32(response, 4u) == 0x6A5C8DBDu &&
        ReadBE64(response, 8u) == kWorkId &&
        ReadBE16(response, 16u) == settingsUrl.size() &&
        response.back() == QQSpeedAuroraOfficial::kCosCommandSettingsJson,
        "28483 bounded notification mismatch");

    std::string digest;
    Check(QQSpeedAuroraOfficial::ComputeSha256Hex("abc", digest, &error),
        error.c_str());
    Check(digest ==
        "ba7816bf8f01cfea414140de5dae2223"
        "b00361a396177a9cb410ff61f20015ad",
        "SHA-256 auth digest mismatch");
}

void TestRuntimeWorkRegistration()
{
    QQSpeedAuroraOfficial::Work runtime;
    runtime.workId = 1100000000ULL;
    runtime.ownerUin = 10001u;
    runtime.mapId = 499u;
    runtime.version = 2u;
    runtime.displayName = "runtime";
    runtime.renderUrl =
        "https://ugc.example.test/aurora/1100000000/2/render/1784451000";
    runtime.md5 = "0123456789ABCDEF0123456789ABCDEF";
    QQSpeedAuroraOfficial::RegisterRuntimeWork(runtime);

    const auto* resolved = QQSpeedAuroraOfficial::FindWork(runtime.workId);
    Check(resolved != nullptr &&
        resolved->ownerUin == runtime.ownerUin &&
        resolved->version == runtime.version &&
        resolved->renderUrl == runtime.renderUrl,
        "runtime work registry lookup mismatch");

    std::vector<std::uint8_t> tail;
    std::string error;
    Check(QQSpeedAuroraOfficial::BuildGameBeginTail(
        runtime.workId, tail, &error), error.c_str());
    Check(ReadBE64(tail, 0u) == runtime.workId &&
        ReadBE32(tail, 9u) == runtime.version,
        "runtime work 509 tail mismatch");
}

void TestRoomChangeFlowCodec()
{
    constexpr std::uint32_t kUin = 0x371AB0BDu;
    constexpr std::uint32_t kTimestamp = 0x0AC9D154u;

    std::vector<std::uint8_t> normal;
    AppendBE32(normal, kUin);
    AppendBE32(normal, kTimestamp);
    AppendBE32(normal, 220u);
    AppendBE16(normal, 5u);
    normal.insert(normal.end(), {0xFFu, 0xFFu, 0xFFu});
    AppendBE32(normal, 0u);
    normal.push_back(0u);
    Check(normal.size() == 22u,
        "121 normal application payload size mismatch");

    RoomChangeRequest request;
    std::string error;
    Check(QQSpeedAuroraOfficial::ParseRoomChangeRequest(
        normal.data(), normal.size(), request, &error), error.c_str());
    Check(request.uin == kUin &&
        request.timestamp == kTimestamp &&
        request.newMapId == 220 &&
        request.season == -1 &&
        request.timeslot == -1 &&
        request.weather == -1 &&
        request.mapSubId == 0 &&
        !request.hasAuroraInfo,
        "121 normal request fields mismatch");

    RoomAuroraInfo requestInfo;
    requestInfo.workId = 1074294017ULL;
    requestInfo.auroraNameRaw =
        std::string("\xCC\xAB\xBF\xD5\xD6\xAE\xC2\xC3", 8u);
    std::vector<std::uint8_t> nestedRequest;
    Check(QQSpeedAuroraOfficial::BuildRoomAuroraInfo(
        requestInfo, nestedRequest, &error), error.c_str());
    Check(nestedRequest.size() == 96u &&
        ReadBE16(nestedRequest, 0u) == 96u,
        "121 compact AuroraInfo request size mismatch");

    std::vector<std::uint8_t> aurora;
    AppendBE32(aurora, kUin);
    AppendBE32(aurora, kTimestamp);
    AppendBE32(aurora, 0u);
    AppendBE16(aurora, 5u);
    aurora.insert(aurora.end(), {0xFFu, 0xFFu, 0xFFu});
    AppendBE32(aurora, 0u);
    aurora.push_back(1u);
    aurora.insert(
        aurora.end(), nestedRequest.begin(), nestedRequest.end());
    Check(aurora.size() == 118u,
        "121 Aurora application payload size mismatch");
    Check(QQSpeedAuroraOfficial::ParseRoomChangeRequest(
        aurora.data(), aurora.size(), request, &error), error.c_str());
    Check(request.newMapId == 0 &&
        request.hasAuroraInfo &&
        request.auroraInfo.workId == requestInfo.workId &&
        request.auroraInfo.auroraNameRaw == requestInfo.auroraNameRaw &&
        request.auroraInfo.version == 0u &&
        request.auroraInfo.tags.empty() &&
        request.auroraInfo.coverUrl.empty() &&
        request.auroraInfo.workNameRaw.empty(),
        "121 compact Aurora request fields mismatch");

    auto truncated = aurora;
    truncated.pop_back();
    Check(!QQSpeedAuroraOfficial::ParseRoomChangeRequest(
        truncated.data(), truncated.size(), request, &error),
        "121 truncated AuroraInfo must be rejected");
    auto residual = normal;
    residual.push_back(0u);
    Check(!QQSpeedAuroraOfficial::ParseRoomChangeRequest(
        residual.data(), residual.size(), request, &error),
        "121 residual bytes must be rejected");

    RoomAuroraInfo resolved;
    Check(QQSpeedAuroraOfficial::ResolveRoomAuroraInfo(
        requestInfo.workId, resolved, &error), error.c_str());
    Check(resolved.workId == requestInfo.workId &&
        resolved.auroraNameRaw == "Kiss" &&
        resolved.workNameRaw == "Kiss" &&
        resolved.version == 630u &&
        resolved.difficulty == 0u &&
        resolved.maxPlayers == 3u &&
        resolved.tags == std::vector<std::uint32_t>({1014u, 1021u}) &&
        resolved.coverUrl ==
            "https://image.speed.qq.com/aurora/1074192660/623/cover/1783595293" &&
        resolved.totalPlayCount == 50959u &&
        resolved.totalLikeCount == 833u &&
        resolved.totalFavoriteCount == 615u,
        "captured room-card metadata decode mismatch");

    RoomAuroraInfo runtimeOnly;
    Check(!QQSpeedAuroraOfficial::ResolveRoomAuroraInfo(
        1100000000ULL, runtimeOnly, &error),
        "runtime work without captured room metadata must be rejected");

    std::vector<std::uint8_t> nestedResponse;
    Check(QQSpeedAuroraOfficial::BuildRoomAuroraInfo(
        resolved, nestedResponse, &error), error.c_str());
    Check(nestedResponse.size() == 173u &&
        ReadBE16(nestedResponse, 0u) == nestedResponse.size() &&
        ReadBE64(nestedResponse, 2u) == resolved.workId &&
        ReadBE32(nestedResponse, 74u) == resolved.version &&
        nestedResponse[78] == resolved.difficulty &&
        nestedResponse[79] == resolved.maxPlayers &&
        nestedResponse[80] == 2u &&
        ReadBE32(nestedResponse, 81u) == 1014u &&
        ReadBE32(nestedResponse, 85u) == 1021u &&
        ReadBE16(nestedResponse, 89u) == resolved.coverUrl.size(),
        "121 response AuroraInfo wire layout mismatch");

    for (std::size_t index = 0;
         index < QQSpeedAuroraOfficial::WorkCount();
         ++index)
    {
        const auto* work = QQSpeedAuroraOfficial::WorkAt(index);
        Check(work != nullptr, "room-card work lookup failed");
        RoomAuroraInfo info;
        Check(QQSpeedAuroraOfficial::ResolveRoomAuroraInfo(
            work->workId, info, &error), error.c_str());
        Check(info.workId == work->workId &&
            info.version == work->version &&
            info.tags.size() <= 5u &&
            info.auroraNameRaw == "Kiss" &&
            info.workNameRaw == "Kiss" &&
            info.coverUrl.size() <= 512u,
            "captured room-card metadata bounds mismatch");
        Check(QQSpeedAuroraOfficial::BuildRoomAuroraInfo(
            info, nestedResponse, &error), error.c_str());
        Check(ReadBE16(nestedResponse, 0u) == nestedResponse.size(),
            "captured room-card nested size mismatch");
    }
}
}

int main()
{
    TestBrowseRecommendationAndDetails();
    TestBrowseConfigAndShowOff();
    TestStartGameAndGameBeginTail();
    TestEntryQuickRpcResponses();
    TestKissCreatorProfileResponse();
    TestRejectedProfiles();
    TestWireBodyLengthNormalization();
    TestCreatorCompatibleProfiles();
    TestModuleBrowseAndCosCoordination();
    TestUploadCompletionNotifications();
    TestRuntimeWorkRegistration();
    TestRoomChangeFlowCodec();
    std::cout << "AuroraOfficialUGCTests: PASS\n";
    return 0;
}
