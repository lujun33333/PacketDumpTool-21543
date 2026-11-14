#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace QQSpeedUGCQuickRpc
{
    enum class BuildResult
    {
        NotHandled,
        Built,
        InvalidRequest,
    };

    struct ResponseInfo
    {
        std::string command;
        double context = 0.0;
    };

    // Builds the application message carried by STQuickRPCBody. The caller
    // must pass it through the normal Response24251 -> CSSafe24251 ->
    // outer28346(mode1) route; this function deliberately does not create or
    // replay transport headers, UINs, timestamps, padding, or hook trailers.
    BuildResult BuildOfficialResponse(
        const std::uint8_t* requestMessage,
        std::size_t requestMessageLength,
        std::vector<std::uint8_t>& responseMessage,
        ResponseInfo* info = nullptr,
        std::string* error = nullptr,
        std::uint32_t sessionUin = 0);
}
