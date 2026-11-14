#include "../GameBegin509V5Builder.h"
#include "../Msg28346Builder.h"
#include "../pet.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

std::string bytes_to_hex(const unsigned char* data, std::size_t length)
{
    static constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.resize(length * 2);
    for (std::size_t index = 0; index < length; ++index)
    {
        output[index * 2] = digits[data[index] >> 4];
        output[index * 2 + 1] = digits[data[index] & 0x0F];
    }
    return output;
}

namespace qqspeed::build323
{
    inline constexpr std::size_t kNotifyGameBegin509FixedRawSize = 0xAB03;

    bool EncodeNotifyGameBegin509(
        const std::uint8_t* fixedRaw,
        std::size_t fixedRawSize,
        std::mt19937_64& paddingRng,
        std::vector<std::uint8_t>& safeOut,
        std::string* error);

    bool DecodeNotifyGameBegin509LegacyBody(
        const std::uint8_t* bodyWithoutRootLength,
        std::size_t bodySize,
        std::array<std::uint8_t, kNotifyGameBegin509FixedRawSize>& fixedRaw,
        std::string* error);
}

// Msg28346Builder.cpp also contains the unrelated safe-pet encoder.  This
// regression only links that translation unit to exercise Msg509, so provide
// the unused pet-config boundary locally instead of pulling in the server.
namespace QQSPetConfig
{
    bool GetBaseSkills(std::uint32_t, std::vector<Skill>& skills)
    {
        skills.clear();
        return false;
    }
}

namespace
{
    using namespace QQSpeedGameBegin509V5;

    constexpr std::uint32_t kClientPid = 0x2864A135u;
    constexpr std::uint32_t kClientPidCheckXor = 0x3A3A3A3Au;
    constexpr std::uint32_t kOuterContext = 0x10203040u;

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    Player MakePlayer(std::uint32_t index)
    {
        Player player;
        player.uin = 10001u + index;
        player.kartId = 170000u + index;
        player.stoneKartId = player.kartId;
        player.gender = static_cast<std::uint8_t>(index & 1u);
        return player;
    }

    Fields MakeBase(std::size_t playerCount)
    {
        Fields fields;
        fields.normalLoadTime = 20000;
        fields.mapId = 142;
        fields.gameId = 2;
        fields.seed = 0x1234567u;
        fields.totalMapRound = 1;
        fields.multiInfo.resize(74);
        for (std::size_t index = 0; index < fields.multiInfo.size(); ++index)
            fields.multiInfo[index] = static_cast<std::uint8_t>(index * 17u + 3u);
        fields.gameSequence = 0x234567u;
        fields.gameSequenceIdHigh = 0x345678u;
        fields.gameSequenceIdLow = 0x45678Au;
        fields.baseMode = 1;
        fields.subMode = 1;
        fields.reportAntiCollisionDataTime = 10;
        fields.moveFrequency = 30;
        fields.gameBeginSwitchFlag = 0x01F08EB3u;
        fields.clientPidIntegrity = kClientPid ^ kClientPidCheckXor;
        fields.raceSyncParameters = { 200, 1000, 3000, 200 };
        for (std::size_t index = 0; index < playerCount; ++index)
        {
            fields.players.push_back(MakePlayer(static_cast<std::uint32_t>(index)));
            fields.raceTrackOrders[index] = fields.players.back().uin;
        }
        return fields;
    }

    void WritePayload(const std::filesystem::path& path, const Fields& fields)
    {
        std::vector<std::uint8_t> payload;
        std::string error;
        Require(Build(fields, payload, &error), error.c_str());
        Require(!payload.empty(), "builder returned an empty payload");
        std::ofstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot create payload fixture");
        stream.write(
            reinterpret_cast<const char*>(payload.data()),
            static_cast<std::streamsize>(payload.size()));
        Require(stream.good(), "cannot write payload fixture");
    }

    void WriteBytes(
        const std::filesystem::path& path,
        const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot create binary fixture");
        stream.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        Require(stream.good(), "cannot write binary fixture");
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        Require(stream.good(), "cannot open binary fixture");
        stream.seekg(0, std::ios::end);
        const std::streamoff length = stream.tellg();
        Require(length >= 0, "cannot determine binary fixture length");
        stream.seekg(0, std::ios::beg);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        stream.read(
            reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        Require(stream.good(), "cannot read binary fixture");
        return bytes;
    }

    void WriteOfficialReencodedSafe(
        const std::filesystem::path& outputDirectory,
        std::size_t fixtureIndex,
        const std::filesystem::path& expandedFixedPath)
    {
        constexpr std::size_t kBuild323FixedRawSize = 0xAAFF;
        constexpr std::size_t kFixedRawSize = 0xAB03;
        std::vector<std::uint8_t> fixedRaw = ReadBytes(expandedFixedPath);
        Require(
            fixedRaw.size() == kBuild323FixedRawSize ||
                fixedRaw.size() == kFixedRawSize,
            "official expanded Msg509 fixture has an unexpected size");
        // 21529 introduced GameBeginSwitchFlag2 and 21543 keeps the same
        // layout. Legacy expanded fixtures map to a zero value for that field.
        fixedRaw.resize(kFixedRawSize, 0);
        std::mt19937_64 paddingRng(0x50900000u + fixtureIndex);
        std::vector<std::uint8_t> safe;
        std::string error;
        if (!qqspeed::build323::EncodeNotifyGameBegin509(
                fixedRaw.data(),
                fixedRaw.size(),
                paddingRng,
                safe,
                &error))
        {
            throw std::runtime_error(
                "official fixture re-encode failed: " + error);
        }
        WriteBytes(
            outputDirectory /
                ("official-reencoded-" + std::to_string(fixtureIndex) +
                 ".inner.bin"),
            safe);
    }

    std::uint16_t ReadBE16(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset)
    {
        Require(offset + 2 <= bytes.size(), "BE16 read exceeds fixture");
        return static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(bytes[offset]) << 8u) |
            bytes[offset + 1]);
    }

    std::uint32_t ReadBE32(
        const std::vector<std::uint8_t>& bytes,
        std::size_t offset)
    {
        Require(offset + 4 <= bytes.size(), "BE32 read exceeds fixture");
        return
            (static_cast<std::uint32_t>(bytes[offset]) << 24u) |
            (static_cast<std::uint32_t>(bytes[offset + 1]) << 16u) |
            (static_cast<std::uint32_t>(bytes[offset + 2]) << 8u) |
            bytes[offset + 3];
    }

    void WriteSafePayloadPair(
        const std::filesystem::path& outputDirectory,
        const std::string& stem,
        const Fields& fields)
    {
        std::vector<std::uint8_t> directPayload;
        std::string error;
        if (!Build(fields, directPayload, &error))
            throw std::runtime_error(stem + " direct build failed: " + error);
        Require(!fields.players.empty(), "safe fixture has no recipient");

        std::array<std::uint8_t,
            qqspeed::build323::kNotifyGameBegin509FixedRawSize> fixedRaw{};
        if (!qqspeed::build323::DecodeNotifyGameBegin509LegacyBody(
                directPayload.data(),
                directPayload.size(),
                fixedRaw,
                &error))
            throw std::runtime_error(stem + " fixed-raw decode failed: " + error);

        std::vector<std::uint8_t> envelope;
        if (!BuildOfficialSafeGameBegin509(
                fields.players.front().uin,
                kOuterContext,
                kClientPid,
                directPayload.data(),
                directPayload.size(),
                envelope,
                &error))
            throw std::runtime_error(stem + " safe build failed: " + error);
        Require(envelope.size() >= 13, "safe 28346 envelope is truncated");
        Require(
            ReadBE32(envelope, 0) == fields.players.front().uin,
            "safe 28346 outer UIN mismatch");
        Require(
            ReadBE32(envelope, 4) == kOuterContext,
            "safe 28346 outer context mismatch");
        Require(ReadBE16(envelope, 8) == 509, "safe 28346 inner message mismatch");
        Require(
            ReadBE16(envelope, 10) == envelope.size() - 13,
            "safe 28346 inner length mismatch");
        Require(envelope.back() == 1, "safe 28346 mode byte is not one");

        WriteBytes(outputDirectory / (stem + ".direct.bin"), directPayload);
        WriteBytes(
            outputDirectory / (stem + ".direct.raw.bin"),
            std::vector<std::uint8_t>(fixedRaw.begin(), fixedRaw.end()));
        WriteBytes(outputDirectory / (stem + ".28346.bin"), envelope);
    }

    Fields MakeObservedBranchFixture()
    {
        Fields fields = MakeBase(2);
        fields.reportDataFlag = 1;
        fields.mapCheckDivisions = {
            { 2, 10 }, { 11, 26 }, { 27, 33 }, { 62, 71 }, { 75, 81 }
        };
        fields.speedSections = { 298, 311, 324, 337, 350, 363, 373, 388 };
        fields.normalSpeedSections = { 200, 220, 240, 260, 280, 300, 320, 340 };
        fields.hideTaskId = 900;
        fields.hideTaskType = 1;
        fields.enableAntiDriftCheat = 1;
        fields.monitorCheckPointBegin = 12;
        fields.monitorCheckPointEnd = 13;
        fields.monitorLapCount = 1;
        fields.hasGameStageInfo = true;
        fields.gameStageIndex = 1;
        fields.totalStageNum = 3;
        fields.carCollectPlayers.push_back({ fields.players[0].uin, 74362u, 3755u });
        fields.anonymousMode = static_cast<std::uint8_t>(fields.players.size());
        fields.recordCheckFirstLimit = 600;
        fields.specialEffectSwitchFlag = 14;
        fields.specialEffectParameter = 30;

        fields.players[0].chumCircleId = 423687;
        fields.players[0].skills = { { 1, 8 }, { 3, 1 } };
        fields.players[0].stoneGrooves = { { 1, 70001 }, { 2, 70002 } };
        fields.players[0].effectBuffs = { { 100, 200, 3, 1 } };
        fields.players[0].parameters = { 7, 8, 9 };
        fields.players[0].equipSkillId = 500;
        fields.players[0].specialEffects = { { 1, 10 }, { 3, 30 } };
        return fields;
    }

    Fields MakeCustomFixture(std::size_t playerCount)
    {
        Fields fields = MakeBase(playerCount);
        fields.mapId = 238;
        fields.totalMapRound = 3;
        fields.baseMode = 42;
        fields.subMode = 27;
        fields.feedbackEnabled = 0;
        fields.gameTypeBeforePoint = 14;
        fields.baseGameModeExParameters = { 200 };
        fields.recordCheckFirstLimit = 0;
        fields.gameBeginSwitchFlag = 0x00B1BBB1u;
        fields.borderRaceType = 40;
        fields.originTeamId = 1;
        fields.shuttleMaps = { { 238 }, { 461 }, { 149 } };
        fields.specialMapId = 238;
        fields.hasCustomScheme = true;
        fields.customScheme.id = 0x11223344u;
        const std::string schemeName = "official-v5-regression";
        std::copy(schemeName.begin(), schemeName.end(), fields.customScheme.name.begin());
        fields.customScheme.createTime = 0x66554433u;
        fields.customScheme.updateTime = 0x66554444u;
        fields.customScheme.baseMode = 1;
        fields.customScheme.subMode = 1;
        fields.customScheme.allocServerType = 14;
        fields.customScheme.eliminationInterval = 10;
        fields.customScheme.roundCount = 3;
        fields.customScheme.n2oEffect = 100;
        fields.customScheme.smallSpray = 1;
        fields.customScheme.collision = 1;
        fields.customScheme.gem = 1;
        fields.customScheme.modification = 1;
        fields.customScheme.mapCount = 3;
        fields.customScheme.mapIds = { 238, 461, 149 };
        fields.customScheme.noCollisionSeconds = 5;
        fields.players[0].specialEffects = { { 1, 0 } };
        if (fields.players.size() >= 3)
            fields.players[2].specialEffects = { { 1, 0 }, { 2, 0 }, { 3, 0 } };
        if (fields.players.size() >= 8)
            fields.players[7].specialEffects = { { 1, 0 }, { 2, 0 } };
        return fields;
    }

    Fields MakeAuroraFixture()
    {
        Fields fields = MakeBase(1);
        fields.mapId = 499;
        fields.baseMode = 42;
        fields.subMode = 29;
        fields.feedbackEnabled = 0;
        fields.moveFrequency = 15;
        fields.gameBeginSwitchFlag = 0x46B93BB0u;
        fields.borderRaceType = 43;
        fields.recordCheckFirstLimit = 600;
        fields.hasAurora = true;
        fields.aurora.workId = 0x1122334455667788ull;
        fields.aurora.version = 7;
        const std::string url = "https://127.0.0.1/aurora-v5.zip";
        fields.aurora.renderDataUrl.assign(url.begin(), url.end());
        return fields;
    }

    Fields MakeRankedFixture(std::size_t playerCount = 4)
    {
        Fields fields = MakeBase(playerCount);
        fields.normalLoadTime = 25000;
        fields.mapId = 396;
        fields.feedbackEnabled = 1;
        fields.enableAntiDriftCheat = 0;
        fields.baseMode = 40;
        fields.subMode = 1;
        fields.moveFrequency = 15;
        fields.maxPropEffectNum = 7;
        fields.crossNoCollision = 1;
        fields.recordCheckFirstLimit = 600;
        fields.gameBeginSwitchFlag = 0x07D88EB3u;
        fields.borderRaceType = 3;
        fields.specialEffectSwitchFlag = 14;
        fields.specialEffectParameter = 15;
        fields.anonymousMode = static_cast<std::uint8_t>(fields.players.size());
        return fields;
    }

    Fields MakePinnacleFixture()
    {
        Fields fields = MakeBase(2);
        fields.normalLoadTime = 15000;
        fields.mapId = 132;
        fields.feedbackEnabled = 0;
        fields.enableAntiDriftCheat = 0;
        fields.baseMode = 40;
        fields.subMode = 5;
        fields.moveFrequency = 15;
        fields.maxPropEffectNum = 7;
        fields.crossNoCollision = 1;
        fields.recordCheckFirstLimit = 600;
        fields.gameBeginSwitchFlag = 0x01D10EB3u;
        fields.borderRaceType = 29;
        fields.specialEffectSwitchFlag = 14;
        fields.specialEffectParameter = 15;
        return fields;
    }

    Fields MakeBorderFixture(std::size_t playerCount = 2)
    {
        Fields fields = MakeBase(playerCount);
        fields.mapId = 320;
        fields.feedbackEnabled = 1;
        fields.enableAntiDriftCheat = 0;
        fields.baseGameModeEx = 101;
        fields.moveFrequency = 15;
        fields.maxPropEffectNum = 7;
        fields.crossNoCollision = 1;
        fields.recordCheckFirstLimit = 600;
        fields.gameBeginSwitchFlag = 0x81F88EB3u;
        fields.borderRaceType = 4;
        fields.specialEffectSwitchFlag = 14;
        fields.specialEffectParameter = 15;
        if (fields.players.size() >= 2)
            fields.players[1].specialEffects = { { 2, 0 } };
        return fields;
    }

    Fields MakeNormalSafeFixture(std::size_t playerCount)
    {
        Fields fields = MakeBase(playerCount);
        fields.feedbackEnabled = 1;
        fields.enableAntiDriftCheat = 1;
        fields.recordCheckFirstLimit = 600;
        fields.gameBeginSwitchFlag = 0x01F08EB3u;
        fields.specialEffectSwitchFlag = 14;
        fields.specialEffectParameter = 30;
        fields.players[0].specialEffects = { { 1, 0 }, { 2, 0 }, { 3, 0 } };
        return fields;
    }

    Fields MakeCarGodSafeFixture()
    {
        static constexpr std::uint8_t kOfficialCarGodMultiInfo[74] = {
            0x00,0x00,0x00,0x01,0x6A,0x92,0xA1,0xEE,0x04,0x03,0xB8,0x02,0x0A,0x01,0x01,0x6A,
            0x92,0xA1,0xEE,0x22,0x6E,0x15,0x38,0x01,0x00,0xC0,0xE9,0x3D,0xB0,0xAA,0x6D,0x95,
            0xC8,0x0E,0xFD,0x14,0x4C,0xB9,0xA4,0x5F,0x79,0x09,0x98,0x26,0xBC,0x2B,0x0A,0xAA,
            0x3B,0x4B,0x75,0x4A,0x99,0x7E,0xE7,0x31,0x94,0x3A,0x3D,0x61,0xCE,0x56,0xC7,0x4D,
            0x0A,0x42,0x3F,0xA3,0xE5,0xE1,0xBE,0x3C,0x7B,0x4F
        };

        Fields fields = MakeBase(1);
        fields.normalLoadTime = 8000;
        fields.mapId = 258;
        fields.gameId = 86;
        fields.seed = 0x3AD80A6Bu;
        fields.totalMapRound = 2;
        fields.record = 116766;
        fields.players[0].uin = 1030921579u;
        fields.players[0].kartId = 12720;
        fields.players[0].stoneKartId = fields.players[0].kartId;
        fields.raceTrackOrders.fill(0);
        fields.raceTrackOrders[2] = fields.players[0].uin;
        fields.multiInfo.assign(
            std::begin(kOfficialCarGodMultiInfo),
            std::end(kOfficialCarGodMultiInfo));
        fields.feedbackEnabled = 0;
        fields.cliReserveFlag = 1;
        fields.enableAntiDriftCheat = 1;
        fields.gameSequence = 185;
        fields.gameSequenceIdHigh = 8553911;
        fields.gameSequenceIdLow = 4136341940u;
        fields.monitorCheckPointBegin = 6;
        fields.monitorCheckPointEnd = 7;
        fields.monitorLapCount = 1;
        fields.baseMode = 10;
        fields.subMode = 1;
        fields.baseGameModeEx = 0;
        fields.reportAntiCollisionDataTime = 10;
        fields.moveFrequency = 15;
        fields.recordCheckFirstLimit = 600;
        fields.gameBeginSwitchFlag = 0x009133F0u;
        fields.raceSyncParameters = { 200, 1000, 3000, 200 };
        fields.specialEffectSwitchFlag = 0;
        fields.specialEffectParameter = 0;
        fields.clientPidIntegrity = kClientPid ^ kClientPidCheckXor;
        return fields;
    }

    void VerifyCarGodSkinIdentity(const std::filesystem::path& outputDirectory)
    {
        Fields base = MakeCarGodSafeFixture();
        base.players[0].kartId = 163043;
        base.players[0].stoneKartId = base.players[0].kartId;
        Fields skin = base;
        skin.players[0].kartId = 163044;
        skin.players[0].stoneKartId = skin.players[0].kartId;

        std::vector<std::uint8_t> basePayload, skinPayload;
        std::string error;
        Require(Build(base, basePayload, &error), error.c_str());
        Require(Build(skin, skinPayload, &error), error.c_str());
        Require(basePayload.size() == skinPayload.size(), "skin changed 509 framing");
        std::vector<std::size_t> differences;
        for (std::size_t i = 0; i < basePayload.size(); ++i)
            if (basePayload[i] != skinPayload[i]) differences.push_back(i);
        // The two IDs differ only in their low byte. With no stone grooves,
        // skin selection must alter exactly the SyncCar ID, no other fields.
        Require(differences.size() == 1 && differences.front() >= 3,
            "skin changed fields outside SyncCar");
        const std::size_t kartOffset = differences.front() - 3;
        Require(ReadBE32(basePayload, kartOffset) == 163043,
            "base kart missing from SyncCar");
        Require(ReadBE32(skinPayload, kartOffset) == 163044,
            "equipped skin missing from SyncCar");
        WriteSafePayloadPair(outputDirectory, "safe-cargod-base-163043", base);
        WriteSafePayloadPair(outputDirectory, "safe-cargod-skin-163044", skin);

        Fields enabled = skin;
        enabled.gameBeginSwitchFlag = 0x009133B0u;
        Require((skin.gameBeginSwitchFlag ^ enabled.gameBeginSwitchFlag) == 0x40u,
            "CarGod override changes flags other than DisableKartSuperJet");
        Require((enabled.gameBeginSwitchFlag & 0x40u) == 0,
            "CarGod still disables kart super jet");
        std::vector<std::uint8_t> enabledPayload;
        Require(Build(enabled, enabledPayload, &error), error.c_str());
        Require(skinPayload.size() == enabledPayload.size(), "super jet flag changed 509 framing");
        differences.clear();
        for (std::size_t i = 0; i < skinPayload.size(); ++i)
            if (skinPayload[i] != enabledPayload[i]) differences.push_back(i);
        Require(differences.size() == 1 &&
            (skinPayload[differences.front()] ^ enabledPayload[differences.front()]) == 0x40,
            "super jet override changed unrelated wire fields");
        WriteSafePayloadPair(outputDirectory, "safe-cargod-superjet-enabled-skin-163044", enabled);
    }

    Fields MakeNormalTelemetrySafeFixture(
        bool mapCheck,
        bool speedSections)
    {
        Fields fields = MakeNormalSafeFixture(1);
        fields.mapId = 112;
        if (mapCheck)
        {
            fields.reportDataFlag = 1;
            fields.mapCheckDivisions = {
                { 2, 10 }, { 11, 26 }, { 27, 33 }, { 62, 71 }, { 75, 81 }
            };
        }
        if (speedSections)
        {
            fields.speedSections = { 292, 302, 312, 322, 332, 342, 352, 367 };
            fields.normalSpeedSections = {
                200, 220, 240, 260, 280, 300, 320, 340
            };
        }
        return fields;
    }

    Fields MakeNormalAnonymousSafeFixture(std::size_t playerCount)
    {
        Fields fields = MakeNormalSafeFixture(playerCount);
        fields.anonymousMode = static_cast<std::uint8_t>(fields.players.size());
        fields.anonymousKartId = 10020u;
        return fields;
    }

    Fields MakeLuckyFixture()
    {
        Fields fields = MakeBase(2);
        fields.normalLoadTime = 10000;
        fields.mapId = 251;
        fields.feedbackEnabled = 1;
        fields.enableAntiDriftCheat = 0;
        fields.moveFrequency = 15;
        fields.maxPropEffectNum = 7;
        fields.crossNoCollision = 1;
        fields.recordCheckFirstLimit = 600;
        fields.gameBeginSwitchFlag = 0x00D13EB3u;
        fields.borderRaceType = 30;
        fields.specialEffectSwitchFlag = 0;
        fields.specialEffectParameter = 15;
        return fields;
    }

    void VerifyRejectedStates()
    {
        std::vector<std::uint8_t> payload;
        std::string error;

        Fields badMapCheck = MakeBase(1);
        badMapCheck.reportDataFlag = 1;
        Require(!Build(badMapCheck, payload, &error), "accepted map-check flag without divisions");

        Fields badTrack = MakeBase(2);
        badTrack.raceTrackOrders[1] = badTrack.raceTrackOrders[0];
        Require(!Build(badTrack, payload, &error), "accepted duplicate race-track UIN");

        Fields badCarCollect = MakeBase(1);
        badCarCollect.carCollectPlayers.push_back({ 999999u, 74362u, 3755u });
        Require(!Build(badCarCollect, payload, &error), "accepted car-collect player outside race roster");

        Fields badStage = MakeBase(1);
        badStage.hasGameStageInfo = true;
        badStage.gameStageIndex = 2;
        badStage.totalStageNum = 2;
        Require(!Build(badStage, payload, &error), "accepted invalid game-stage bounds");

        Fields badGameId = MakeBase(1);
        badGameId.gameId = 0;
        Require(!Build(badGameId, payload, &error), "accepted a missing game ID");
    }

    void VerifyTransportModeFields()
    {
        Fields tcpOnly = MakeBase(2);
        tcpOnly.p2pMode = 0;
        tcpOnly.tcpFrequency = 0;

        Fields p2p = tcpOnly;
        p2p.p2pMode = 1;
        p2p.tcpFrequency = 10;

        std::vector<std::uint8_t> tcpOnlyPayload;
        std::vector<std::uint8_t> p2pPayload;
        std::string error;
        Require(Build(tcpOnly, tcpOnlyPayload, &error), error.c_str());
        Require(Build(p2p, p2pPayload, &error), error.c_str());
        Require(tcpOnlyPayload.size() == p2pPayload.size(), "transport fields changed payload size");

        std::vector<std::size_t> differences;
        for (std::size_t index = 0; index < tcpOnlyPayload.size(); ++index)
        {
            if (tcpOnlyPayload[index] != p2pPayload[index])
                differences.push_back(index);
        }
        Require(differences.size() == 2, "P2PMode/TcpFrequence are not isolated wire fields");
        Require(
            tcpOnlyPayload[differences[0]] == 0 &&
            p2pPayload[differences[0]] == 1,
            "P2PMode is not serialized from Fields");
        Require(
            tcpOnlyPayload[differences[1]] == 0 &&
            p2pPayload[differences[1]] == 10,
            "TcpFrequence is not serialized from Fields");
    }
}

int main(int argc, char** argv)
{
    try
    {
        Require(
            argc >= 2,
            "usage: GameBegin509V5Regression <payload-directory> "
            "[official-expanded-fixed ...]");
        const std::filesystem::path outputDirectory(argv[1]);
        std::filesystem::create_directories(outputDirectory);
        WritePayload(outputDirectory / "observed-branches.bin", MakeObservedBranchFixture());
        WritePayload(outputDirectory / "ranked.bin", MakeRankedFixture());
        WritePayload(outputDirectory / "pinnacle.bin", MakePinnacleFixture());
        WritePayload(outputDirectory / "border.bin", MakeBorderFixture());
        WritePayload(outputDirectory / "lucky.bin", MakeLuckyFixture());
        WritePayload(outputDirectory / "custom-12p-3map.bin", MakeCustomFixture(12));
        WritePayload(outputDirectory / "aurora.bin", MakeAuroraFixture());

        for (std::size_t playerCount = 1; playerCount <= 6; ++playerCount)
        {
            WriteSafePayloadPair(
                outputDirectory,
                "safe-normal-" + std::to_string(playerCount) + "p",
                MakeNormalSafeFixture(playerCount));
        }
        WriteSafePayloadPair(
            outputDirectory,
            "safe-normal-mapcheck-1p",
            MakeNormalTelemetrySafeFixture(true, false));
        WriteSafePayloadPair(
            outputDirectory,
            "safe-normal-speed-1p",
            MakeNormalTelemetrySafeFixture(false, true));
        WriteSafePayloadPair(
            outputDirectory,
            "safe-normal-mapcheck-speed-1p",
            MakeNormalTelemetrySafeFixture(true, true));
        WriteSafePayloadPair(
            outputDirectory, "safe-normal-anonymous-4p", MakeNormalAnonymousSafeFixture(4));
        WriteSafePayloadPair(
            outputDirectory, "safe-normal-anonymous-6p", MakeNormalAnonymousSafeFixture(6));
        WriteSafePayloadPair(
            outputDirectory, "safe-cargod-1p", MakeCarGodSafeFixture());
        VerifyCarGodSkinIdentity(outputDirectory);
        WriteSafePayloadPair(
            outputDirectory, "safe-border-6p", MakeBorderFixture(6));
        WriteSafePayloadPair(
            outputDirectory, "safe-aurora-1p", MakeAuroraFixture());
        for (std::size_t playerCount = 1; playerCount <= 6; ++playerCount)
        {
            WriteSafePayloadPair(
                outputDirectory,
                "safe-ranked-" + std::to_string(playerCount) + "p",
                MakeRankedFixture(playerCount));
        }
        for (std::size_t playerCount = 1; playerCount <= 12; ++playerCount)
        {
            WriteSafePayloadPair(
                outputDirectory,
                "safe-custom-" + std::to_string(playerCount) + "p",
                MakeCustomFixture(playerCount));
        }
        for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
        {
            WriteOfficialReencodedSafe(
                outputDirectory,
                static_cast<std::size_t>(argumentIndex - 2),
                std::filesystem::path(argv[argumentIndex]));
        }
        VerifyRejectedStates();
        VerifyTransportModeFields();
        std::cout << "GameBegin509V5 C++ regression passed\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "GameBegin509V5 regression failed: " << exception.what() << '\n';
        return 1;
    }
}
