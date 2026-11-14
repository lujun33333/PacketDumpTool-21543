#include "../Msg28346Router.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma pack(push, 1)
struct PdumpHeader
{
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t direction;
    std::uint32_t sequence;
    std::uint64_t tick;
    std::uint32_t threadId;
    std::uint16_t msgId;
    std::uint16_t reserved;
    std::uint32_t originalLength;
    std::uint32_t capturedLength;
};
#pragma pack(pop)

static std::uint16_t ReadBE16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) << 8U | data[1]);
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
            throw std::runtime_error(
                "usage: OfficialMsg123CurrentRegression <pdump> [pdump ...]");

        std::size_t accepted = 0;
        for (int argument = 1; argument < argc; ++argument)
        {
        std::ifstream input(argv[argument], std::ios::binary);
        if (!input)
            throw std::runtime_error(
                "unable to open pdump: " + std::string(argv[argument]));
        std::size_t acceptedFile = 0;
        while (true)
        {
            PdumpHeader header{};
            input.read(reinterpret_cast<char*>(&header), sizeof(header));
            if (input.gcount() == 0)
                break;
            if (input.gcount() != sizeof(header))
                break;
            if (header.magic != 0x31504450u || header.version != 1 ||
                header.capturedLength > 0x400000u)
                throw std::runtime_error("invalid pdump header");
            std::vector<std::uint8_t> body(header.capturedLength);
            input.read(reinterpret_cast<char*>(body.data()), body.size());
            if (static_cast<std::size_t>(input.gcount()) != body.size())
                break;
            if (header.direction != 2 || header.msgId != 28346 ||
                body.size() < 15 || ReadBE16(body.data() + 10) != 123)
                continue;

            Msg28346Envelope envelope;
            Msg28346ParseDiag diag;
            if (!TryParseOfficialMsg28346Envelope(
                    body.data() + 2, body.size() - 2, 123, envelope, &diag))
                throw std::runtime_error("envelope rejected: " + diag.Reason);
            std::vector<std::uint8_t> legacy;
            QQSpeedMsg123::LegacyGameplaySummary summary;
            std::string error;
            if (!QQSpeedMsg123::DecodeSafeRequestToLegacy(
                    envelope.InnerBody, envelope.InnerBodyLen, envelope.OuterUin,
                    legacy, &summary, &error))
                throw std::runtime_error(
                    "record " + std::to_string(header.sequence) + ": " + error);
            if (summary.uin != envelope.OuterUin ||
                summary.bytesConsumed != legacy.size())
                throw std::runtime_error("decoded summary mismatch");
            if (summary.lapTime < 6000 && summary.distanceToEnd == 0)
                throw std::runtime_error(
                    "record " + std::to_string(header.sequence) +
                    " decoded zero DistanceToEnd, lapTime=" +
                    std::to_string(summary.lapTime));
            ++accepted;
            ++acceptedFile;
        }
        std::cout << "ok file=" << argv[argument]
                  << " current_inner123=" << acceptedFile << "\n";
        }
        if (accepted < 28)
            throw std::runtime_error(
                "expected at least 28 current official samples, got " +
                std::to_string(accepted));
        std::cout << "ok total_current_inner123=" << accepted << "\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "failed: " << exception.what() << "\n";
        return 1;
    }
}
