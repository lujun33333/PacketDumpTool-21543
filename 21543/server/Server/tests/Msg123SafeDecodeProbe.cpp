#include "../Msg28346Router.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    std::ifstream input(argv[1], std::ios::binary);
    std::vector<std::uint8_t> safe{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    const std::uint32_t uin =
        static_cast<std::uint32_t>(std::stoul(argv[2]));
    std::vector<std::uint8_t> legacy;
    QQSpeedMsg123::LegacyGameplaySummary summary;
    std::string error;
    if (!QQSpeedMsg123::DecodeSafeRequestToLegacy(
            safe.data(), safe.size(), uin, legacy, &summary, &error))
    {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << "ok uin=" << summary.uin
              << " lap=" << summary.lapTime
              << " distance=" << summary.distanceToEnd
              << " checkpoints=" << static_cast<unsigned>(summary.checkpointCount)
              << " legacy=" << legacy.size() << "\n";
    return 0;
}
