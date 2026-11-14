#include "UGCQuickRpc.h"
#include "AuroraOfficialUGC.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
    constexpr std::string_view kDispatchResponse = "DispatchCoResponse";
    constexpr std::string_view kRoomRedPoint = "Co.RoomUGCCheckNotifyRedPointOnLogin";
    constexpr std::string_view kRoomConfig = "Co.RoomUGCGetCfg";
    constexpr std::string_view kRoomPlayerProfile = "Co.RoomUGCGetPlayerProfileSnapshot";
    constexpr std::string_view kAuroraConfig = "Co.AuroraUGCGetCfg";
    constexpr std::string_view kAuroraRedPoint = "Co.AuroraUGCCheckNotifyRedPointOnLogin";
    constexpr std::string_view kAuroraShowOff = "Co.GetAuroraShowOffCfg";
    constexpr std::string_view kBatchPlayerProfile = "Co.BatchGetPlayerProfileSnapshot";

#include "UGCQuickRpcOfficialFixtures.inc"
#include "UGCQuickRpcRoom6752Fixtures.inc"
#include "UGCKissProfileFixture.inc"
#include "UGCQuickRpc35184Fixtures.inc"

    bool SetError(std::string* error, const char* value)
    {
        if (error) *error = value;
        return false;
    }

    std::uint8_t HexNibble(char value)
    {
        if (value >= '0' && value <= '9') return static_cast<std::uint8_t>(value - '0');
        if (value >= 'a' && value <= 'f') return static_cast<std::uint8_t>(value - 'a' + 10);
        if (value >= 'A' && value <= 'F') return static_cast<std::uint8_t>(value - 'A' + 10);
        return 0xFF;
    }

    std::vector<std::uint8_t> DecodeHex(std::string_view hex)
    {
        std::vector<std::uint8_t> bytes;
        if ((hex.size() % 2u) != 0u)
            return bytes;
        bytes.reserve(hex.size() / 2u);
        for (std::size_t index = 0; index < hex.size(); index += 2u)
        {
            const std::uint8_t high = HexNibble(hex[index]);
            const std::uint8_t low = HexNibble(hex[index + 1u]);
            if (high > 0x0Fu || low > 0x0Fu)
                return {};
            bytes.push_back(static_cast<std::uint8_t>((high << 4u) | low));
        }
        return bytes;
    }

    const std::vector<std::uint8_t>& RoomConfigTemplate()
    {
        static const std::vector<std::uint8_t> value =
            DecodeHex(kRoomConfig6752ResponseHex);
        return value;
    }

    const std::vector<std::uint8_t>& RoomProfilePrimaryTemplate()
    {
        static const std::vector<std::uint8_t> value =
            DecodeHex(kRoomProfilePrimaryResponseHex);
        return value;
    }

    const std::vector<std::uint8_t>& RoomProfileLatestSecondaryTemplate()
    {
        static const std::vector<std::uint8_t> value =
            DecodeHex(kRoomProfileLatestSecondaryResponseHex);
        return value;
    }

    const std::vector<std::uint8_t>& RoomProfileGallerySecondaryTemplate()
    {
        static const std::vector<std::uint8_t> value =
            DecodeHex(kRoomProfileGallerySecondaryResponseHex);
        return value;
    }

    const std::vector<std::uint8_t>& RoomProfileGalleryExtraTemplate()
    {
        static const std::vector<std::uint8_t> value =
            DecodeHex(kRoomProfileGalleryExtraResponseHex);
        return value;
    }

    const std::vector<std::uint8_t>& RoomProfileLegacySecondaryTemplate()
    {
        static const std::vector<std::uint8_t> value =
            DecodeHex(kRoomProfileLegacySecondaryResponseHex);
        return value;
    }

    const std::vector<std::uint8_t>& AuroraConfigTemplate()
    {
        static const std::vector<std::uint8_t> value =
            DecodeHex(kAuroraConfigResponseHex);
        return value;
    }

    struct DecodedCapturedProfileFixture
    {
        std::string_view command;
        std::vector<std::uint8_t> normalizedRequest;
        std::vector<std::uint8_t> response;
    };

    const std::vector<DecodedCapturedProfileFixture>& CapturedProfileFixtures()
    {
        static const std::vector<DecodedCapturedProfileFixture> value = [] {
            std::vector<DecodedCapturedProfileFixture> decoded;
            decoded.reserve(std::size(kProfile35184Fixtures));
            for (const auto& fixture : kProfile35184Fixtures)
            {
                auto request = DecodeHex(fixture.normalizedRequestHex);
                auto response = DecodeHex(fixture.responseHex);
                if (request.empty() || response.empty()) continue;
                decoded.push_back({fixture.command, std::move(request), std::move(response)});
            }
            return decoded;
        }();
        return value;
    }

    bool BuildCapturedProfileResponse(
        std::string_view command,
        const std::uint8_t* request,
        std::size_t requestLength,
        std::vector<std::uint8_t>& response)
    {
        if (!request || requestLength < command.size() + 11u)
            return false;

        std::vector<std::uint8_t> normalized(request, request + requestLength);
        const std::size_t contextOffset = command.size() + 3u;
        std::fill(
            normalized.begin() + contextOffset,
            normalized.begin() + contextOffset + sizeof(double),
            static_cast<std::uint8_t>(0u));

        for (const auto& fixture : CapturedProfileFixtures())
        {
            if (fixture.command == command &&
                fixture.normalizedRequest == normalized)
            {
                response = fixture.response;
                return true;
            }
        }
        return false;
    }

    const std::vector<std::uint8_t>& AuroraShowOffTemplate()
    {
        static const std::vector<std::uint8_t> value = DecodeHex(kAuroraShowOffResponseHex);
        return value;
    }

    const std::vector<std::uint8_t>& KissPlayerProfileTemplate()
    {
        static const std::vector<std::uint8_t> value =
            DecodeHex(kKissPlayerProfileResponseHex);
        return value;
    }

    bool IsSupported(std::string_view command)
    {
        return command == kRoomRedPoint || command == kRoomConfig ||
            command == kRoomPlayerProfile ||
            command == kAuroraConfig || command == kAuroraRedPoint ||
            command == kAuroraShowOff || command == kBatchPlayerProfile;
    }

    bool ContainsLuaNumber(
        const std::uint8_t* request,
        std::size_t begin,
        std::size_t length,
        std::uint32_t expectedUin)
    {
        const double expected = static_cast<double>(expectedUin);
        for (std::size_t offset = begin; offset + 9u <= length; ++offset)
        {
            if (request[offset] != 0xF6) continue;
            double value = 0.0;
            std::memcpy(&value, request + offset + 1u, sizeof(value));
            if (value == expected) return true;
        }
        return false;
    }

    bool IsExactLuaUinArgument(
        const std::uint8_t* request,
        std::size_t begin,
        std::size_t length)
    {
        if (!request || begin > length || length - begin != 9u ||
            request[begin] != 0xF6)
            return false;

        double value = 0.0;
        std::memcpy(&value, request + begin + 1u, sizeof(value));
        return std::isfinite(value) && value >= 0.0 &&
            value <= static_cast<double>((std::numeric_limits<std::uint32_t>::max)()) &&
            std::floor(value) == value;
    }

    bool ContainsKissCreatorUin(
        const std::uint8_t* request,
        std::size_t begin,
        std::size_t length)
    {
        return ContainsLuaNumber(
            request, begin, length, QQSpeedAuroraOfficial::kKissCreatorUin);
    }

    bool ParseOfficialRequest(
        const std::uint8_t* request,
        std::size_t length,
        std::string_view& command,
        double& context,
        std::string* error)
    {
        if (!request)
            return SetError(error, "request pointer null");
        const auto* terminator = static_cast<const std::uint8_t*>(std::memchr(request, 0, length));
        if (!terminator)
            return SetError(error, "request command not terminated");

        const std::size_t commandLength = static_cast<std::size_t>(terminator - request);
        command = std::string_view(reinterpret_cast<const char*>(request), commandLength);
        if (!IsSupported(command))
            return SetError(error, "unsupported");

        // Every entry/config request starts with one Lua-number coroutine
        // context: command NUL, argument-list marker 0x00, number marker 0xF6,
        // and one little-endian IEEE-754 double. Dump6752 additionally contains
        // RoomUGCGetCfg requests with exactly one more Lua-number argument: the
        // current session UIN.
        constexpr std::size_t kSuffixLength = 11;
        const std::size_t commonLength = commandLength + kSuffixLength;
        if (length < commonLength)
            return SetError(error, "request length mismatch");
        if (terminator[1] != 0 || terminator[2] != 0xF6)
            return SetError(error, "request context marker mismatch");

        static_assert(sizeof(double) == 8, "QuickRPC requires 64-bit IEEE-754 double");
        std::memcpy(&context, terminator + 3, sizeof(context));
        if (!std::isfinite(context))
            return SetError(error, "request context is not finite");

        if (command == kBatchPlayerProfile)
        {
            if (length <= commonLength)
                return SetError(error, "request length mismatch");
        }
        else if (command == kRoomPlayerProfile)
        {
            // The current gallery sends a Lua table after the context.  Its
            // creator UINs select one of the captured response batches below.
            if (length <= commonLength)
                return SetError(error, "request length mismatch");
        }
        else if (command == kRoomConfig)
        {
            if (length != commonLength &&
                !IsExactLuaUinArgument(request, commonLength, length))
                return SetError(error, "request UIN argument schema mismatch");
        }
        else if (length != commonLength)
        {
            return SetError(error, "request length mismatch");
        }
        if (error) error->clear();
        return true;
    }

    void AppendContext(std::vector<std::uint8_t>& out, double context)
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&context);
        out.insert(out.end(), bytes, bytes + sizeof(context));
    }

    std::vector<std::uint8_t> BuildEmptySuccess(double context)
    {
        std::vector<std::uint8_t> response;
        response.reserve(kDispatchResponse.size() + 20u);
        response.insert(response.end(), kDispatchResponse.begin(), kDispatchResponse.end());
        response.push_back(0);
        response.push_back(0);
        response.push_back(0xF6);
        AppendContext(response, context);
        response.push_back(0xF6);
        AppendContext(response, 0.0);
        return response;
    }

    bool PatchResponseContext(std::vector<std::uint8_t>& response, double context, std::string* error)
    {
        if (response.size() < kDispatchResponse.size() + 11u ||
            !std::equal(kDispatchResponse.begin(), kDispatchResponse.end(), response.begin()))
            return SetError(error, "fixture dispatch command mismatch");
        const std::size_t marker = kDispatchResponse.size();
        if (response[marker] != 0 || response[marker + 1u] != 0 || response[marker + 2u] != 0xF6)
            return SetError(error, "fixture context marker mismatch");
        std::memcpy(response.data() + marker + 3u, &context, sizeof(context));
        return true;
    }
}

QQSpeedUGCQuickRpc::BuildResult QQSpeedUGCQuickRpc::BuildOfficialResponse(
    const std::uint8_t* requestMessage,
    std::size_t requestMessageLength,
    std::vector<std::uint8_t>& responseMessage,
    ResponseInfo* info,
    std::string* error,
    std::uint32_t /*sessionUin*/)
{
    responseMessage.clear();
    if (info) *info = {};

    std::string_view command;
    double context = 0.0;
    std::string parseError;
    if (!ParseOfficialRequest(
            requestMessage, requestMessageLength,
            command, context, &parseError))
    {
        if (parseError == "unsupported")
        {
            if (error) *error = parseError;
            return BuildResult::NotHandled;
        }
        if (error) *error = parseError;
        return BuildResult::InvalidRequest;
    }

    const bool capturedProfile =
        (command == kRoomPlayerProfile || command == kBatchPlayerProfile) &&
        BuildCapturedProfileResponse(
            command, requestMessage, requestMessageLength, responseMessage);

    if (capturedProfile)
    {
        // Exact current-client capture matched after normalizing only the
        // coroutine ContextID. The response context is patched below.
    }
    else if (command == kRoomRedPoint || command == kAuroraRedPoint)
    {
        responseMessage = BuildEmptySuccess(context);
    }
    else if (command == kRoomConfig)
    {
        responseMessage = RoomConfigTemplate();
    }
    else if (command == kRoomPlayerProfile)
    {
        const std::size_t argumentsBegin = command.size() + 11u;
        if (ContainsLuaNumber(
                requestMessage, argumentsBegin, requestMessageLength, 873411793u))
        {
            responseMessage = RoomProfileLatestSecondaryTemplate();
        }
        else if (ContainsLuaNumber(
                     requestMessage, argumentsBegin, requestMessageLength, 1433136760u) ||
                 ContainsLuaNumber(
                     requestMessage, argumentsBegin, requestMessageLength, 1524697784u))
        {
            responseMessage = RoomProfileGallerySecondaryTemplate();
        }
        else if (ContainsLuaNumber(
                     requestMessage, argumentsBegin, requestMessageLength, 1538814586u))
        {
            responseMessage = RoomProfileGalleryExtraTemplate();
        }
        else if (ContainsLuaNumber(
                     requestMessage, argumentsBegin, requestMessageLength, 1530194656u))
        {
            responseMessage = RoomProfileLegacySecondaryTemplate();
        }
        else if (ContainsLuaNumber(
                     requestMessage, argumentsBegin, requestMessageLength, 1507781713u))
        {
            responseMessage = RoomProfilePrimaryTemplate();
        }
        else
        {
            if (error) *error = "room profile batch is not captured";
            return BuildResult::InvalidRequest;
        }
    }
    else if (command == kAuroraConfig)
    {
        responseMessage = AuroraConfigTemplate();
    }
    else if (command == kAuroraShowOff)
    {
        responseMessage = AuroraShowOffTemplate();
    }
    else if (command == kBatchPlayerProfile)
    {
        if (ContainsKissCreatorUin(
                requestMessage, command.size() + 11u, requestMessageLength))
        {
            responseMessage = KissPlayerProfileTemplate();
        }
        else
        {
            if (error) *error = "unsupported";
            return BuildResult::NotHandled;
        }
    }

    if (responseMessage.empty())
    {
        if (error) *error = "official response fixture empty";
        return BuildResult::InvalidRequest;
    }
    if (!PatchResponseContext(responseMessage, context, error))
    {
        responseMessage.clear();
        return BuildResult::InvalidRequest;
    }

    if (info)
    {
        info->command.assign(command.begin(), command.end());
        info->context = context;
    }
    if (error) *error = "ok";
    return BuildResult::Built;
}
