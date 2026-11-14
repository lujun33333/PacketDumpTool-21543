#include "../Msg28346Router.h"

#include <cstdint>
#include <cstdlib>
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

namespace
{
    std::uint16_t ReadBE16(const std::uint8_t* data)
    {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(data[0]) << 8u) |
            static_cast<std::uint16_t>(data[1]));
    }

    std::uint32_t ReadBE32(const std::uint8_t* data)
    {
        return (static_cast<std::uint32_t>(data[0]) << 24u) |
            (static_cast<std::uint32_t>(data[1]) << 16u) |
            (static_cast<std::uint32_t>(data[2]) << 8u) |
            static_cast<std::uint32_t>(data[3]);
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
            throw std::runtime_error(
                "usage: Msg112CurrentRegression <pdump> [pdump ...]");

        std::size_t total = 0;
        for (int argument = 1; argument < argc; ++argument)
        {
            std::ifstream input(argv[argument], std::ios::binary);
            if (!input)
                throw std::runtime_error(
                    "unable to open pdump: " + std::string(argv[argument]));

            std::size_t accepted = 0;
            while (true)
            {
                PdumpHeader header{};
                input.read(reinterpret_cast<char*>(&header), sizeof(header));
                if (input.gcount() == 0)
                    break;
                if (input.gcount() != sizeof(header))
                    break;
                if (header.magic != 0x31504450u || header.version != 1u ||
                    header.capturedLength > 0x400000u)
                    throw std::runtime_error("invalid pdump header");

                std::vector<std::uint8_t> body(header.capturedLength);
                input.read(reinterpret_cast<char*>(body.data()), body.size());
                if (static_cast<std::size_t>(input.gcount()) != body.size())
                    break;
                if (header.direction != 2u || header.msgId != 28346u ||
                    body.size() < 15u || ReadBE16(body.data() + 10u) != 112u)
                    continue;

                Msg28346Envelope envelope;
                Msg28346ParseDiag diag;
                if (!TryParseOfficialMsg28346Envelope(
                        body.data() + 2u, body.size() - 2u, 112u,
                        envelope, &diag))
                    throw std::runtime_error(
                        "record " + std::to_string(header.sequence) +
                        " envelope rejected: " + diag.Reason);

                std::vector<std::uint8_t> plain;
                std::string error;
                if (!QQSpeedMsg112::DecodeSafeRequestToPlain(
                        envelope.InnerBody, envelope.InnerBodyLen, plain,
                        envelope.OuterUin, &error))
                    throw std::runtime_error(
                        "record " + std::to_string(header.sequence) +
                        " decode failed: " + error);
                if (plain.size() != 8u ||
                    ReadBE32(plain.data()) != envelope.OuterUin)
                    throw std::runtime_error("decoded Uin mismatch");

                const std::uint32_t timestamp = ReadBE32(plain.data() + 4u);
                const std::uint32_t tick = static_cast<std::uint32_t>(header.tick);
                const std::int64_t delta = static_cast<std::int32_t>(timestamp - tick);
                if (std::llabs(delta) > 250)
                    throw std::runtime_error(
                        "record " + std::to_string(header.sequence) +
                        " timestamp/tick mismatch: timestamp=" +
                        std::to_string(timestamp) + " tick=" + std::to_string(tick));
                ++accepted;
                ++total;
            }
            std::cout << "ok file=" << argv[argument]
                      << " current_inner112=" << accepted << "\n";
        }

        if (total < 100u)
            throw std::runtime_error(
                "expected at least 100 current inner112 samples, got " +
                std::to_string(total));
        std::cout << "ok total_current_inner112=" << total << "\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "failed: " << exception.what() << "\n";
        return 1;
    }
}
