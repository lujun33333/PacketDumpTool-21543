#include "../NotifySvrConfigProfile.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    std::uint64_t Fnv1a64(const std::vector<std::uint8_t>& bytes)
    {
        std::uint64_t hash = 0xcbf29ce484222325ULL;
        for (std::uint8_t value : bytes)
        {
            hash ^= value;
            hash *= 0x100000001b3ULL;
        }
        return hash;
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot open NotifySvrConfig.yml");
        return std::string(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>());
    }

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot create invalid selector fixture");
        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        Require(stream.good(), "cannot write invalid selector fixture");
    }

    void WriteBinary(
        const std::filesystem::path& path,
        const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot create serialized tail fixture");
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        Require(stream.good(), "cannot write serialized tail fixture");
    }

    void VerifyInvalidSelectorIsRejected(
        const std::filesystem::path& configPath,
        const std::filesystem::path& tempRoot)
    {
        std::string yaml = ReadText(configPath);
        const std::string marker = "SelectedVersion: 4";
        const std::size_t markerPos = yaml.find(marker);
        Require(markerPos != std::string::npos, "selector marker is missing");
        yaml.replace(markerPos, marker.size(), "SelectedVersion: 5");

        const std::filesystem::path invalidPath = tempRoot / "invalid-selector.yml";
        WriteText(invalidPath, yaml);

        bool rejected = false;
        try
        {
            (void)QQSpeedNotifySvrConfig::LoadFile(invalidPath.string());
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "invalid selector silently fell back to another profile");
    }

    void VerifyMissingFieldIsRejected(
        const std::filesystem::path& configPath,
        const std::filesystem::path& tempRoot)
    {
        std::string yaml = ReadText(configPath);
        const std::string marker = "    ItemExtendLimit: 5800\n";
        const std::size_t markerPos = yaml.find(marker);
        Require(markerPos != std::string::npos, "Config4 ItemExtendLimit marker is missing");
        yaml.erase(markerPos, marker.size());

        const std::filesystem::path invalidPath = tempRoot / "missing-field.yml";
        WriteText(invalidPath, yaml);

        bool rejected = false;
        try
        {
            (void)QQSpeedNotifySvrConfig::LoadFile(invalidPath.string());
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        Require(rejected, "missing Config4 field silently used a default value");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 3, "usage: NotifySvrConfigProfileRegression <config.yml> <temp-dir>");
        const std::filesystem::path configPath = argv[1];
        const std::filesystem::path tempRoot = argv[2];

        const QQSpeedNotifySvrConfig::Settings settings =
            QQSpeedNotifySvrConfig::LoadFile(configPath.string());

        Require(settings.selectedVersion == 4, "YAML must explicitly select Config4");
        Require(settings.config3.refitCards.size() == 12, "legacy Config3 card count changed");
        Require(settings.config4.clientItemOpenConfigs.size() == 13, "official ClientItemOpenTypeNum changed");
        Require(settings.config4.itemExtendLimit == 5800, "official ItemExtendLimit changed");
        Require(settings.config4.clientItemIcons.size() == 8, "official ClientItemIconNum changed");
        Require(settings.config4.tdcBlackCars.size() == 3, "official TDCBlackCarNum changed");
        Require(settings.config4.refitCards.size() == 25, "official KartRefitCardTypeNum changed");
        Require(settings.config4.needShowOrderItems.size() == 4, "official NeedShowOrderItemNum changed");
        Require(settings.config4.sceneSettingTypes ==
                std::vector<std::uint32_t>({ 0, 0, 0, 0, 0 }),
            "official SceneSettingTypes changed");
        Require(settings.config4.clientFlag == std::vector<std::uint8_t>({ 0xF2, 0x01 }),
            "official ClientFlag changed");

        const std::vector<std::uint8_t> config3Tail =
            QQSpeedNotifySvrConfig::SerializeTail(settings.config3, 3);
        Require(config3Tail.size() == 633, "Config3 tail wire length changed");
        Require(Fnv1a64(config3Tail) == 0x621b0e2c5d6b9756ULL,
            "Config3 structured serializer no longer matches the preserved legacy wire");

        const std::vector<std::uint8_t> config4Tail =
            QQSpeedNotifySvrConfig::SerializeTail(settings.config4, 4);
        Require(config4Tail.size() == 2731, "Config4 tail wire length changed");
        Require(Fnv1a64(config4Tail) == 0x3857ba115077be95ULL,
            "Config4 structured serializer no longer matches the latest official 25880 capture");
        WriteBinary(tempRoot / "config4-tail.bin", config4Tail);

        VerifyInvalidSelectorIsRejected(configPath, tempRoot);
        VerifyMissingFieldIsRejected(configPath, tempRoot);
        std::cout << "NotifySvrConfig profiles passed: explicit selector, strict validation, "
                     "legacy Config3 wire and official Config4 wire verified."
                  << std::endl;
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << std::endl;
        return 1;
    }
}
