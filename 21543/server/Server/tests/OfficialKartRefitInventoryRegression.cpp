#include "../Msg28346Builder.h"
#include "../pet.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace QQSPetConfig
{
    bool GetBaseSkills(std::uint32_t, std::vector<Skill>& skills)
    {
        skills.clear();
        return false;
    }
}

namespace QQSpeedMsg271Build323
{
    bool ExpandCurrentServerBody(
        const std::uint8_t* in,
        std::size_t size,
        std::vector<std::uint8_t>& out,
        std::string* error);
}

namespace
{
    constexpr std::uint32_t kClientPidCheckXor = 0x3A3A3A3Au;
    constexpr std::array<std::uint32_t, 30> kLuckyValueByLevel = {
        20, 20, 20, 20, 20, 20,
        10, 10, 10, 10,
        5, 5,
        4, 4,
        3, 3, 3, 3, 3, 3, 3, 3,
        3, 3, 3, 3, 3, 3, 3, 3
    };

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(static_cast<bool>(stream), "failed to open fixture");
        return {
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        };
    }

    std::uint16_t ReadBE16(const std::uint8_t* data)
    {
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(data[0]) << 8) |
            static_cast<std::uint16_t>(data[1]));
    }

    std::uint32_t ReadBE32(const std::uint8_t* data)
    {
        return
            (static_cast<std::uint32_t>(data[0]) << 24) |
            (static_cast<std::uint32_t>(data[1]) << 16) |
            (static_cast<std::uint32_t>(data[2]) << 8) |
            static_cast<std::uint32_t>(data[3]);
    }

    std::uint16_t ReadLE16(const std::uint8_t* data)
    {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(data[0]) |
            (static_cast<std::uint16_t>(data[1]) << 8));
    }

    std::uint32_t ReadLE32(const std::uint8_t* data)
    {
        return
            static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24);
    }

    void AppendBE16(std::vector<std::uint8_t>& out, std::uint16_t value)
    {
        out.push_back(static_cast<std::uint8_t>(value >> 8));
        out.push_back(static_cast<std::uint8_t>(value));
    }

    void AppendBE32(std::vector<std::uint8_t>& out, std::uint32_t value)
    {
        out.push_back(static_cast<std::uint8_t>(value >> 24));
        out.push_back(static_cast<std::uint8_t>(value >> 16));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
        out.push_back(static_cast<std::uint8_t>(value));
    }

    std::vector<std::uint8_t> MakeCompact318(
        const std::vector<std::uint8_t>& expanded)
    {
        Require(expanded.size() == 135011, "official expanded Msg318 fixture size changed");
        const std::uint16_t kartCount = ReadLE16(expanded.data() + 4);
        Require(kartCount <= 1500, "official expanded Msg318 KartNum is invalid");

        std::vector<std::uint8_t> body;
        body.reserve(11u + static_cast<std::size_t>(kartCount) * 94u);
        AppendBE32(body, ReadLE32(expanded.data()));
        AppendBE16(body, kartCount);
        for (std::size_t index = 0; index < kartCount; ++index)
        {
            const std::uint8_t* item = expanded.data() + 6 + index * 90;
            AppendBE16(body, 94);
            AppendBE32(body, ReadLE32(item));
            AppendBE32(body, ReadLE32(item + 4));
            AppendBE32(body, ReadLE32(item + 8));
            for (std::size_t offset : { 12u, 14u, 16u, 18u, 20u, 22u })
                AppendBE16(body, ReadLE16(item + offset));
            for (std::size_t offset : { 24u, 28u, 32u, 36u, 40u })
                AppendBE32(body, ReadLE32(item + offset));
            AppendBE16(body, 10);
            body.insert(body.end(), item + 44, item + 52);
            AppendBE32(body, ReadLE32(item + 52));
            for (std::size_t offset : { 56u, 58u, 60u, 62u, 64u, 66u })
                AppendBE16(body, ReadLE16(item + offset));
            AppendBE32(body, ReadLE32(item + 68));
            body.push_back(item[72]);
            AppendBE32(body, ReadLE32(item + 73));
            AppendBE32(body, ReadLE32(item + 77));
            body.push_back(item[81]);
            AppendBE32(body, ReadLE32(item + 82));
            AppendBE32(body, ReadLE32(item + 86));
        }
        body.push_back(expanded[135006]);
        AppendBE32(body, ReadLE32(expanded.data() + 135007));
        return body;
    }

    void VerifyOfficialCompact271(const std::vector<std::uint8_t>& body)
    {
        Require(body.size() == 28526, "official direct Msg271 business size changed");
        Require(ReadBE16(body.data()) == 0, "official Msg271 ResultID mismatch");
        Require(ReadBE16(body.data() + 6) == 300, "official Msg271 KartNum mismatch");

        std::size_t pos = 8;
        for (std::size_t index = 0; index < 300; ++index)
        {
            Require(ReadBE16(body.data() + pos) == 94, "official Msg271 item length mismatch");
            const std::uint8_t* item = body.data() + pos + 2;
            const std::array<std::uint16_t, 6> weights = {
                ReadBE16(item + 14),
                ReadBE16(item + 16),
                ReadBE16(item + 18),
                ReadBE16(item + 20),
                ReadBE16(item + 22),
                ReadBE16(item + 64)
            };
            Require(
                ReadBE16(item + 68) == *std::max_element(weights.begin(), weights.end()),
                "official RefitLuckyValueMaxWeight formula mismatch");
            pos += 94;
        }

        Require(ReadBE32(body.data() + pos) == 16, "official MaxLuckyWeight mismatch");
        pos += 4;
        Require(ReadBE32(body.data() + pos) == 0, "official LuckyValue mismatch");
        pos += 4;
        Require(ReadBE32(body.data() + pos) == 100, "official MaxLuckyValue mismatch");
        pos += 4;
        Require(body[pos++] == kLuckyValueByLevel.size(), "official LevelNum mismatch");
        for (std::size_t level = 0; level < kLuckyValueByLevel.size(); ++level)
        {
            Require(ReadBE16(body.data() + pos) == 10, "official LuckyValueInfo length mismatch");
            Require(ReadBE32(body.data() + pos + 2) == level, "official LuckyValueInfo level mismatch");
            Require(
                ReadBE32(body.data() + pos + 6) == kLuckyValueByLevel[level],
                "official LuckyValueInfo value mismatch");
            pos += 10;
        }
        Require(body[pos++] == 1, "official direct Msg271 HasMore mismatch");
        pos += 4;
        Require(pos == body.size(), "official direct Msg271 tail size mismatch");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(argc == 4, "usage: regression <direct-raw> <expanded271> <expanded318>");
        const std::vector<std::uint8_t> raw = ReadFile(argv[1]);
        const std::vector<std::uint8_t> officialExpanded = ReadFile(argv[2]);
        const std::vector<std::uint8_t> officialExpanded318 = ReadFile(argv[3]);
        Require(raw.size() == 28538, "official raw Msg271 fixture size changed");
        Require(officialExpanded.size() == 135282, "official expanded Msg271 fixture size changed");

        const std::uint16_t declaredBodyLen = ReadBE16(raw.data());
        Require(declaredBodyLen == 28528, "official direct Msg271 BodyLen changed");
        Require(declaredBodyLen <= raw.size(), "official direct Msg271 BodyLen is invalid");
        std::vector<std::uint8_t> body(raw.begin() + 2, raw.begin() + declaredBodyLen);
        VerifyOfficialCompact271(body);

        std::vector<std::uint8_t> expanded;
        std::string error;
        Require(
            QQSpeedMsg271Build323::ExpandCurrentServerBody(
                body.data(), body.size(), expanded, &error),
            error.c_str());
        Require(expanded == officialExpanded, "current compact-to-expanded Msg271 differs from official fixture");

        const std::uint32_t uin = ReadBE32(body.data() + 2);
        const std::uint32_t clientPid =
            ReadBE32(body.data() + body.size() - 4) ^ kClientPidCheckXor;
        constexpr std::uint32_t responseContext = 0x10203040u;
        std::vector<std::uint8_t> body28346;
        std::srand(1);
        Require(
            BuildOfficialSafeKartRefit271(
                uin,
                responseContext,
                clientPid,
                body.data(),
                body.size(),
                body28346,
                &error) == OfficialKartRefit271BuildResult::Direct271Required,
            "300-item official Msg271 must select the direct overflow path");
        Require(body28346.empty(), "direct overflow path must not return a partial 28346 body");

        constexpr std::uint16_t safeKartCount = 20;
        std::vector<std::uint8_t> safeBody;
        safeBody.insert(safeBody.end(), body.begin(), body.begin() + 6);
        AppendBE16(safeBody, safeKartCount);
        safeBody.insert(
            safeBody.end(),
            body.begin() + 8,
            body.begin() + 8 + static_cast<std::size_t>(safeKartCount) * 94);
        const std::size_t officialTail = 8 + 300u * 94u;
        safeBody.insert(safeBody.end(), body.begin() + officialTail, body.end());
        safeBody[safeBody.size() - 5] = 0;

        std::srand(1);
        Require(
            BuildOfficialSafeKartRefit271(
                uin,
                responseContext,
                clientPid,
                safeBody.data(),
                safeBody.size(),
                body28346,
                &error) == OfficialKartRefit271BuildResult::Safe28346,
            error.c_str());
        Require(body28346.size() >= 13, "safe Msg271 outer body is truncated");
        Require(ReadBE32(body28346.data()) == uin, "safe Msg271 outer Uin mismatch");
        Require(ReadBE32(body28346.data() + 4) == responseContext, "safe Msg271 outer context mismatch");
        Require(ReadBE16(body28346.data() + 8) == 271, "safe Msg271 inner ID mismatch");
        const std::uint16_t innerLen = ReadBE16(body28346.data() + 10);
        Require(body28346.size() == 12u + innerLen + 1u, "safe Msg271 outer length mismatch");
        Require(body28346.back() == 1, "safe Msg271 mode mismatch");

        const std::vector<std::uint8_t> moreBody =
            MakeCompact318(officialExpanded318);
        const std::uint32_t moreUin = ReadBE32(moreBody.data());
        const std::uint32_t moreClientPid =
            ReadBE32(moreBody.data() + moreBody.size() - 4) ^ kClientPidCheckXor;
        std::srand(1);
        Require(
            BuildOfficialSafeKartRefit318(
                moreUin,
                responseContext,
                moreClientPid,
                moreBody.data(),
                moreBody.size(),
                body28346,
                &error),
            error.c_str());
        Require(ReadBE32(body28346.data()) == moreUin, "safe Msg318 outer Uin mismatch");
        Require(ReadBE32(body28346.data() + 4) == responseContext, "safe Msg318 outer context mismatch");
        Require(ReadBE16(body28346.data() + 8) == 318, "safe Msg318 inner ID mismatch");
        const std::uint16_t moreInnerLen = ReadBE16(body28346.data() + 10);
        Require(body28346.size() == 12u + moreInnerLen + 1u, "safe Msg318 outer length mismatch");
        Require(body28346.back() == 1, "safe Msg318 mode mismatch");
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
