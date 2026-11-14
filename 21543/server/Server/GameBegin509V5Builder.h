#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace QQSpeedGameBegin509V5
{
    struct StoneGroove
    {
        std::uint32_t useOccasionType = 0;
        std::uint32_t skillStoneId = 0;
    };

    struct Skill
    {
        std::uint32_t id = 0;
        std::uint32_t level = 0;
    };

    struct EffectBuff
    {
        std::uint16_t buffId = 0;
        std::uint16_t featureId = 0;
        std::uint8_t level = 0;
        std::uint8_t showIcon = 0;
    };

    struct SpecialEffect
    {
        std::uint32_t type = 0;
        std::uint32_t parameter0 = 0;
    };

    struct MapCheckDivision
    {
        std::uint16_t beginIndex = 0;
        std::uint16_t endIndex = 0;
    };

    struct CarCollectPlayer
    {
        std::uint32_t uin = 0;
        std::uint32_t carId = 0;
        std::uint32_t taskId = 0;
    };

    struct Player
    {
        std::uint32_t uin = 0;
        std::uint32_t kartId = 0;
        std::uint32_t stoneKartId = 0;
        std::uint32_t chumCircleId = 0;
        std::uint32_t worldEscapeTaskId = 0;
        std::uint32_t starsFightingStar = 0;
        std::uint32_t equipSkillId = 0;
        std::uint8_t professionBigLevel = 6;
        std::uint8_t professionSmallLevel = 5;
        std::uint8_t gender = 0;
        std::vector<StoneGroove> stoneGrooves;
        std::vector<Skill> skills;
        std::vector<EffectBuff> effectBuffs;
        std::vector<std::uint32_t> parameters;
        std::vector<SpecialEffect> specialEffects;
    };

    struct ShuttleMap
    {
        std::uint32_t mapId = 0;
        std::uint32_t beginIndex = 0xFFFFFFFFu;
        std::uint32_t endIndex = 0xFFFFFFFFu;
        std::uint8_t reverse = 0;
        std::uint16_t beginCheckPointIndex = 0;
        std::uint32_t probability = 0;
        std::vector<std::uint32_t> tags;
        std::uint8_t shuttleType = 1;
    };

    struct CustomScheme
    {
        std::uint32_t id = 0;
        std::array<std::uint8_t, 128> name{};
        std::uint32_t createTime = 0;
        std::uint32_t updateTime = 0;
        std::uint16_t baseMode = 0;
        std::uint16_t subMode = 0;
        std::uint8_t allocServerType = 0;
        std::uint8_t eliminationInterval = 0;
        std::uint8_t eliminationRanking = 0;
        std::uint8_t roundCount = 0;
        std::uint8_t n2oMode = 0;
        std::int32_t n2oEffect = 0;
        std::uint8_t smallSpray = 0;
        std::uint8_t collision = 0;
        std::uint8_t gem = 0;
        std::uint8_t costume = 0;
        std::uint8_t modification = 0;
        std::uint8_t mapCount = 0;
        std::array<std::uint32_t, 3> mapIds{};
        std::uint8_t noCollisionSeconds = 0;
    };

    struct Aurora
    {
        std::uint64_t workId = 0;
        std::vector<std::uint8_t> workName;
        std::uint32_t version = 0;
        std::vector<std::uint8_t> renderDataUrl;
    };

    struct Fields
    {
        std::uint32_t normalLoadTime = 0;
        std::uint32_t mapId = 0;
        std::array<std::uint8_t, 32> mapHash{};
        std::uint32_t gameId = 0;
        std::uint32_t seed = 0;
        std::array<std::uint32_t, 20> raceTrackOrders{};
        std::uint16_t totalMapRound = 0;
        std::uint32_t record = 0;
        std::vector<Player> players;
        std::uint8_t reportDataFlag = 0;
        std::vector<MapCheckDivision> mapCheckDivisions;
        std::uint8_t p2pMode = 0;
        std::uint8_t tcpFrequency = 0;
        std::vector<std::uint8_t> multiInfo;
        std::uint8_t feedbackEnabled = 0;
        std::uint8_t cliReserveFlag = 0;
        std::vector<std::uint16_t> speedSections;
        std::vector<std::uint16_t> normalSpeedSections;
        std::uint32_t hideTaskId = 0;
        std::uint8_t hideTaskType = 0;
        std::uint32_t hideTaskParameter = 0;
        std::uint8_t enableAntiDriftCheat = 0;
        std::uint32_t gameSequence = 0;
        std::vector<ShuttleMap> shuttleMaps;
        std::uint32_t specialMapId = 0;
        std::uint32_t gameSequenceIdHigh = 0;
        std::uint32_t gameSequenceIdLow = 0;
        std::uint16_t monitorCheckPointBegin = 0;
        std::uint16_t monitorCheckPointEnd = 0;
        std::uint8_t monitorLapCount = 0;
        std::uint8_t gameTypeBeforePoint = 0;
        std::uint16_t baseMode = 0;
        std::uint16_t subMode = 0;
        std::uint8_t baseGameModeEx = 0;
        std::vector<std::uint32_t> baseGameModeExParameters;
        bool hasGameStageInfo = false;
        std::uint8_t gameStageIndex = 0;
        std::uint8_t totalStageNum = 0;
        std::vector<CarCollectPlayer> carCollectPlayers;
        std::uint16_t reportAntiCollisionDataTime = 0;
        std::uint32_t moveFrequency = 0;
        std::uint8_t anonymousMode = 0;
        std::uint32_t anonymousKartId = 0;
        std::uint8_t maxPropEffectNum = 0;
        std::uint8_t crossNoCollision = 0;
        // Zero means the profile has no record-check list.  Non-zero is the
        // first map-specific threshold in the three-record official list.
        std::uint32_t recordCheckFirstLimit = 0;
        std::uint32_t gameBeginSwitchFlag = 0;
        std::uint16_t borderRaceType = 0;
        std::array<std::uint32_t, 4> raceSyncParameters{};
        std::uint32_t specialEffectSwitchFlag = 0;
        std::uint32_t specialEffectParameter = 0;
        std::uint32_t clientPidIntegrity = 0;
        std::uint8_t originTeamId = 0;
        std::uint8_t ladderChallenge = 0;
        bool hasCustomScheme = false;
        CustomScheme customScheme;
        bool hasAurora = false;
        Aurora aurora;
    };

    bool Build(
        const Fields& fields,
        std::vector<std::uint8_t>& payload,
        std::string* error = nullptr);
}
