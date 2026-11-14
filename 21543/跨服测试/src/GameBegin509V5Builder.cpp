#include "GameBegin509V5Builder.h"

#include <algorithm>
#include <limits>
#include <unordered_set>

namespace
{
    using namespace QQSpeedGameBegin509V5;

    void SetError(std::string* error, const char* text)
    {
        if (error)
            *error = text;
    }

    class Writer
    {
    public:
        explicit Writer(std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

        void U8(std::uint8_t value)
        {
            bytes_.push_back(value);
        }

        void U16(std::uint16_t value)
        {
            bytes_.push_back(static_cast<std::uint8_t>(value >> 8));
            bytes_.push_back(static_cast<std::uint8_t>(value));
        }

        void U32(std::uint32_t value)
        {
            bytes_.push_back(static_cast<std::uint8_t>(value >> 24));
            bytes_.push_back(static_cast<std::uint8_t>(value >> 16));
            bytes_.push_back(static_cast<std::uint8_t>(value >> 8));
            bytes_.push_back(static_cast<std::uint8_t>(value));
        }

        void U64(std::uint64_t value)
        {
            U32(static_cast<std::uint32_t>(value >> 32));
            U32(static_cast<std::uint32_t>(value));
        }

        template <typename Container>
        void Bytes(const Container& bytes)
        {
            bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
        }

        std::size_t BeginNode()
        {
            const std::size_t offset = bytes_.size();
            U16(0);
            return offset;
        }

        bool EndNode(std::size_t offset)
        {
            if (offset + 2 > bytes_.size())
                return false;
            const std::size_t size = bytes_.size() - offset;
            if (size > 0x7FFFu)
                return false;
            bytes_[offset] = static_cast<std::uint8_t>(size >> 8);
            bytes_[offset + 1] = static_cast<std::uint8_t>(size);
            return true;
        }

    private:
        std::vector<std::uint8_t>& bytes_;
    };

    bool Validate(const Fields& fields, std::string* error)
    {
        if (fields.normalLoadTime == 0 || fields.mapId == 0 || fields.gameId == 0 ||
            fields.seed == 0 ||
            fields.totalMapRound == 0 || fields.gameSequence == 0 ||
            fields.gameSequenceIdHigh == 0 || fields.gameSequenceIdLow == 0 ||
            fields.baseMode == 0 || fields.subMode == 0 ||
            fields.reportAntiCollisionDataTime == 0 || fields.moveFrequency == 0 ||
            fields.gameBeginSwitchFlag == 0 || fields.clientPidIntegrity == 0)
        {
            SetError(error, "missing required race-start scalar");
            return false;
        }
        if (fields.players.empty() || fields.players.size() > 12)
        {
            SetError(error, "player count outside 1..12");
            return false;
        }
        if (fields.multiInfo.size() != 74)
        {
            SetError(error, "MultiInfo must contain the current 74-byte authority value");
            return false;
        }
        if (fields.mapCheckDivisions.size() > 10 ||
            fields.speedSections.size() > 10 || fields.normalSpeedSections.size() > 10 ||
            fields.baseGameModeExParameters.size() > 20)
        {
            SetError(error, "variable scalar list exceeds client capacity");
            return false;
        }
        if (fields.reportDataFlag > 1 ||
            fields.reportDataFlag != (fields.mapCheckDivisions.empty() ? 0 : 1))
        {
            SetError(error, "map-check report flag and division list differ");
            return false;
        }
        std::uint16_t previousDivisionEnd = 0;
        for (const auto& division : fields.mapCheckDivisions)
        {
            if (division.beginIndex == 0 || division.beginIndex > division.endIndex ||
                division.beginIndex <= previousDivisionEnd)
            {
                SetError(error, "invalid map-check division sequence");
                return false;
            }
            previousDivisionEnd = division.endIndex;
        }
        if ((fields.hideTaskId == 0 &&
             (fields.hideTaskType != 0 || fields.hideTaskParameter != 0)) ||
            (fields.hideTaskId != 0 && fields.hideTaskType == 0))
        {
            SetError(error, "invalid hide-task state");
            return false;
        }
        if (fields.shuttleMaps.size() > 5)
        {
            SetError(error, "shuttle map count exceeds client capacity");
            return false;
        }
        if ((fields.hasGameStageInfo &&
             (fields.totalStageNum == 0 || fields.gameStageIndex >= fields.totalStageNum)) ||
            (!fields.hasGameStageInfo &&
             (fields.gameStageIndex != 0 || fields.totalStageNum != 0)))
        {
            SetError(error, "invalid GameStageInfo state");
            return false;
        }

        std::unordered_set<std::uint32_t> playerUins;
        std::unordered_set<std::uint32_t> trackUins;
        for (const auto& player : fields.players)
        {
            if (player.uin == 0 || player.kartId == 0 ||
                !playerUins.insert(player.uin).second ||
                player.stoneGrooves.size() > 5 || player.skills.size() > 6 ||
                player.effectBuffs.size() > 64 || player.parameters.size() > 20 ||
                player.specialEffects.size() > 4)
            {
                SetError(error, "invalid player record");
                return false;
            }
        }
        for (const auto uin : fields.raceTrackOrders)
        {
            if (uin != 0 && !trackUins.insert(uin).second)
            {
                SetError(error, "duplicate race-track UIN");
                return false;
            }
        }
        if (trackUins != playerUins)
        {
            SetError(error, "race-track snapshot and player roster differ");
            return false;
        }
        for (const auto& map : fields.shuttleMaps)
        {
            if (map.mapId == 0 || map.tags.size() > 4)
            {
                SetError(error, "invalid shuttle map record");
                return false;
            }
        }
        if (fields.hasCustomScheme)
        {
            const auto& scheme = fields.customScheme;
            if (fields.hasAurora || scheme.id == 0 || scheme.baseMode == 0 ||
                scheme.subMode == 0 || scheme.roundCount == 0 ||
                scheme.mapCount == 0 || scheme.mapCount > 3 ||
                fields.shuttleMaps.size() != scheme.mapCount)
            {
                SetError(error, "invalid custom scheme");
                return false;
            }
            for (std::size_t index = 0; index < scheme.mapCount; ++index)
            {
                if (scheme.mapIds[index] == 0 ||
                    scheme.mapIds[index] != fields.shuttleMaps[index].mapId)
                {
                    SetError(error, "custom map order is not one frozen sequence");
                    return false;
                }
            }
            const auto currentMap = std::find_if(
                fields.shuttleMaps.begin(),
                fields.shuttleMaps.end(),
                [&fields](const ShuttleMap& map) { return map.mapId == fields.specialMapId; });
            if (fields.specialMapId != fields.mapId || currentMap == fields.shuttleMaps.end())
            {
                SetError(error, "custom current map is not in the frozen sequence");
                return false;
            }
        }
        else if (!fields.shuttleMaps.empty() || fields.specialMapId != 0)
        {
            SetError(error, "non-custom profile contains custom map state");
            return false;
        }
        if (fields.hasAurora)
        {
            if (fields.aurora.workId == 0 || fields.aurora.version == 0 ||
                fields.aurora.workName.size() > 128 ||
                fields.aurora.renderDataUrl.empty() ||
                fields.aurora.renderDataUrl.size() > std::numeric_limits<std::uint16_t>::max())
            {
                SetError(error, "invalid Aurora state");
                return false;
            }
        }
        if (fields.anonymousMode != 0 && fields.anonymousMode != fields.players.size())
        {
            SetError(error, "anonymous roster must cover the complete race roster");
            return false;
        }
        if (fields.carCollectPlayers.size() > 20)
        {
            SetError(error, "car-collect roster exceeds client capacity");
            return false;
        }
        std::unordered_set<std::uint32_t> carCollectUins;
        for (const auto& player : fields.carCollectPlayers)
        {
            if (player.uin == 0 || player.carId == 0 || player.taskId == 0 ||
                playerUins.find(player.uin) == playerUins.end() ||
                !carCollectUins.insert(player.uin).second)
            {
                SetError(error, "invalid car-collect player record");
                return false;
            }
        }
        return true;
    }

    bool WriteSyncCar(Writer& writer, const Player& player)
    {
        const auto node = writer.BeginNode();
        writer.U32(player.uin);
        writer.U32(player.kartId);
        writer.U8(player.stoneGrooves.empty() ? 0 : 1);
        if (!player.stoneGrooves.empty())
        {
            const auto stone = writer.BeginNode();
            writer.U32(player.stoneKartId == 0 ? player.kartId : player.stoneKartId);
            writer.U32(static_cast<std::uint32_t>(player.stoneGrooves.size()));
            for (const auto& groove : player.stoneGrooves)
            {
                const auto item = writer.BeginNode();
                writer.U32(groove.useOccasionType);
                writer.U32(groove.skillStoneId);
                if (!writer.EndNode(item))
                    return false;
            }
            if (!writer.EndNode(stone))
                return false;
        }
        return writer.EndNode(node);
    }

    bool WritePlayer(Writer& writer, const Player& player)
    {
        const auto node = writer.BeginNode();
        writer.U32(player.uin);
        writer.U32(player.chumCircleId);
        writer.U8(static_cast<std::uint8_t>(player.skills.size()));
        for (const auto& skill : player.skills)
        {
            const auto item = writer.BeginNode();
            writer.U32(skill.id);
            writer.U32(skill.level);
            if (!writer.EndNode(item))
                return false;
        }
        writer.U32(player.worldEscapeTaskId);
        const auto license = writer.BeginNode();
        writer.U8(player.professionBigLevel);
        writer.U8(player.professionSmallLevel);
        if (!writer.EndNode(license))
            return false;
        writer.U8(static_cast<std::uint8_t>(player.effectBuffs.size()));
        for (const auto& buff : player.effectBuffs)
        {
            const auto item = writer.BeginNode();
            writer.U16(buff.buffId);
            writer.U16(buff.featureId);
            writer.U8(buff.level);
            writer.U8(buff.showIcon);
            if (!writer.EndNode(item))
                return false;
        }
        writer.U32(player.starsFightingStar);
        writer.U8(static_cast<std::uint8_t>(player.parameters.size()));
        for (const auto value : player.parameters)
            writer.U32(value);
        writer.U32(player.equipSkillId);
        writer.U8(static_cast<std::uint8_t>(player.specialEffects.size()));
        for (const auto& effect : player.specialEffects)
        {
            const auto item = writer.BeginNode();
            writer.U32(effect.type);
            writer.U32(effect.parameter0);
            if (!writer.EndNode(item))
                return false;
        }
        return writer.EndNode(node);
    }

    bool WriteShuttleMap(Writer& writer, const ShuttleMap& map)
    {
        const auto node = writer.BeginNode();
        writer.U32(map.mapId);
        writer.U32(map.beginIndex);
        writer.U32(map.endIndex);
        writer.U8(map.reverse);
        writer.U16(map.beginCheckPointIndex);
        writer.U32(map.probability);
        writer.U8(static_cast<std::uint8_t>(map.tags.size()));
        for (const auto tag : map.tags)
            writer.U32(tag);
        writer.U8(map.shuttleType);
        return writer.EndNode(node);
    }

    bool WriteAnonymousPlayer(Writer& writer, const Player& player)
    {
        const bool firstAvatarSet = player.gender != 0;
        const std::uint32_t hair = firstAvatarSet ? 10025u : 10042u;
        const std::uint32_t coat = firstAvatarSet ? 11700u : 11732u;
        const std::uint32_t trousers = firstAvatarSet ? 11701u : 11733u;
        const std::uint32_t face = firstAvatarSet ? 10106u : 10103u;

        const auto node = writer.BeginNode();
        writer.U32(player.uin);
        writer.U32(hair);
        writer.U32(coat);
        writer.U32(trousers);
        writer.U32(0);
        writer.U32(0);
        writer.U8(1);
        const auto item = writer.BeginNode();
        for (std::size_t index = 0; index < 17; ++index)
            writer.U32(index == 2 ? face : 0);
        if (!writer.EndNode(item))
            return false;
        return writer.EndNode(node);
    }

    bool WriteOfficialRecordChecks(Writer& writer, std::uint32_t firstLimit)
    {
        const std::uint32_t first0[] = { firstLimit, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 3000u };
        static const std::uint32_t first1[] = { 300u, 0xFFFFFFFFu, 0u, 1000u, 1000u };
        static const std::uint32_t second0[] = { 0u, 50u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u };
        static const std::uint32_t second1[] = { 0u, 40000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 10000u, 0u };
        static const std::uint32_t third0[] = { 10u };
        struct Condition { std::uint16_t type; const std::uint32_t* values; std::uint8_t count; };
        const Condition conditions[][2] = {
            { { 0, first0, 5 }, { 0, first1, 5 } },
            { { 0, second0, 5 }, { 1, second1, 6 } },
            { { 5, third0, 1 }, { 0, nullptr, 0 } }
        };
        static const std::uint16_t relations[] = { 0, 1, 0 };
        static const std::uint8_t counts[] = { 2, 2, 1 };

        writer.U8(3);
        for (std::size_t recordIndex = 0; recordIndex < 3; ++recordIndex)
        {
            const auto record = writer.BeginNode();
            writer.U16(relations[recordIndex]);
            writer.U8(counts[recordIndex]);
            for (std::size_t conditionIndex = 0; conditionIndex < counts[recordIndex]; ++conditionIndex)
            {
                const auto& condition = conditions[recordIndex][conditionIndex];
                const auto item = writer.BeginNode();
                writer.U16(condition.type);
                writer.U8(condition.count);
                for (std::size_t valueIndex = 0; valueIndex < condition.count; ++valueIndex)
                    writer.U32(condition.values[valueIndex]);
                if (!writer.EndNode(item))
                    return false;
            }
            if (!writer.EndNode(record))
                return false;
        }
        return true;
    }

    bool WriteCustomScheme(Writer& writer, const CustomScheme& scheme)
    {
        const auto node = writer.BeginNode();
        writer.U32(scheme.id);
        writer.Bytes(scheme.name);
        writer.U32(scheme.createTime);
        writer.U32(scheme.updateTime);
        writer.U16(scheme.baseMode);
        writer.U16(scheme.subMode);
        writer.U8(scheme.allocServerType);
        writer.U8(scheme.eliminationInterval);
        writer.U8(scheme.eliminationRanking);
        writer.U8(scheme.roundCount);
        writer.U8(scheme.n2oMode);
        writer.U32(static_cast<std::uint32_t>(scheme.n2oEffect));
        writer.U8(scheme.smallSpray);
        writer.U8(scheme.collision);
        writer.U8(scheme.gem);
        writer.U8(scheme.costume);
        writer.U8(scheme.modification);
        writer.U8(scheme.mapCount);
        for (const auto mapId : scheme.mapIds)
            writer.U32(mapId);
        writer.U8(scheme.noCollisionSeconds);
        return writer.EndNode(node);
    }
}

bool QQSpeedGameBegin509V5::Build(
    const Fields& fields,
    std::vector<std::uint8_t>& payload,
    std::string* error)
{
    payload.clear();
    if (!Validate(fields, error))
        return false;

    payload.reserve(2048);
    Writer writer(payload);
    writer.U32(fields.normalLoadTime);
    writer.U32(fields.mapId);
    writer.Bytes(fields.mapHash);
    writer.U32(fields.gameId);
    writer.U32(fields.seed);
    writer.U8(static_cast<std::uint8_t>(fields.players.size()));
    for (const auto uin : fields.raceTrackOrders)
        writer.U32(uin);
    writer.U16(fields.totalMapRound);
    writer.U32(0); // PropUpdateInterval
    writer.U32(fields.record);
    writer.U8(0); // NianShouAwardInfo

    writer.U8(static_cast<std::uint8_t>(fields.players.size()));
    for (const auto& player : fields.players)
    {
        if (!WriteSyncCar(writer, player))
        {
            SetError(error, "SyncCar nested record overflow");
            payload.clear();
            return false;
        }
    }

    writer.U8(fields.reportDataFlag);
    writer.U8(static_cast<std::uint8_t>(fields.mapCheckDivisions.size()));
    for (const auto& division : fields.mapCheckDivisions)
    {
        const auto item = writer.BeginNode();
        writer.U16(division.beginIndex);
        writer.U16(division.endIndex);
        if (!writer.EndNode(item))
        {
            SetError(error, "MapCheckDivision nested record overflow");
            payload.clear();
            return false;
        }
    }
    writer.U8(fields.p2pMode); // P2PMode
    writer.U8(fields.tcpFrequency); // TcpFrequence
    writer.U8(static_cast<std::uint8_t>(fields.multiInfo.size()));
    writer.Bytes(fields.multiInfo);
    writer.U8(fields.feedbackEnabled);
    writer.U8(static_cast<std::uint8_t>(fields.speedSections.size()));
    for (const auto value : fields.speedSections)
        writer.U16(value);
    writer.U8(static_cast<std::uint8_t>(fields.normalSpeedSections.size()));
    for (const auto value : fields.normalSpeedSections)
        writer.U16(value);
    writer.U8(0); // MemCheckInfoNum
    writer.U8(4); // ExtraInfoInterval
    writer.U16(0xFFFFu); // OffsetThreshold
    writer.U32(200); // SpeedRatioThreshold1
    writer.U32(200); // SpeedRatioThreshold2
    writer.U32(fields.hideTaskId);
    writer.U8(fields.hideTaskType);
    writer.U32(fields.hideTaskParameter);
    writer.U8(0); // ForceReportCPNum
    writer.U8(0); // CliReserveFlag
    writer.U8(fields.enableAntiDriftCheat);
    if (fields.enableAntiDriftCheat)
    {
        const auto antiDrift = writer.BeginNode();
        writer.U32(0);
        writer.U32(0);
        writer.U32(0);
        writer.U32(220);
        writer.U32(15);
        writer.U32(0);
        writer.U32(0);
        if (!writer.EndNode(antiDrift))
        {
            SetError(error, "AntiDrift nested record overflow");
            payload.clear();
            return false;
        }
    }
    writer.U8(0); // HasCrashModePara
    writer.U32(0); // FizzStarTaskId
    writer.U8(0); // FizzStarTaskType
    writer.U32(0); // FizzStarTaskParam1
    writer.U32(0); // LDMRecordID
    writer.U32(fields.gameSequence);

    writer.U8(static_cast<std::uint8_t>(fields.players.size()));
    for (const auto& player : fields.players)
    {
        if (!WritePlayer(writer, player))
        {
            SetError(error, "GameBeginPlayer nested record overflow");
            payload.clear();
            return false;
        }
    }
    writer.U8(0); // PlayerCheerNums
    writer.U8(static_cast<std::uint8_t>(fields.shuttleMaps.size()));
    for (const auto& map : fields.shuttleMaps)
    {
        if (!WriteShuttleMap(writer, map))
        {
            SetError(error, "ShuttleMap nested record overflow");
            payload.clear();
            return false;
        }
    }
    writer.U32(fields.specialMapId);
    writer.U8(0); // NPCNum
    writer.U8(0); // MapInfoNum
    for (std::size_t index = 0; index < 20; ++index)
        writer.U32(0); // NPCRaceTrackOrders
    writer.U8(0); // PlayerExtraInfoNum
    writer.U8(0); // HasTowerInfo
    writer.U8(0); // HasWeRelayGameBeginInfo
    writer.U8(0); // ChangeCar
    writer.U32(fields.gameSequenceIdHigh);
    writer.U32(fields.gameSequenceIdLow);
    writer.U32(0); // KubiBigCoinReplaceItem
    writer.U32(0); // TimerChallengeJumpLevel
    writer.U8(0); // ShadowRunDelay
    writer.U16(0); // ShadowCatchUpContinuesTime
    writer.U8(0); // ArrestPlayerNums
    writer.U16(fields.monitorCheckPointBegin);
    writer.U16(fields.monitorCheckPointEnd);
    writer.U8(fields.monitorLapCount);
    writer.U8(fields.gameTypeBeforePoint);
    writer.U16(0); // PointID
    writer.U16(fields.baseMode);
    writer.U16(fields.subMode);
    writer.U8(0); // GameType
    writer.U16(0); // ReportPosSpan
    writer.U16(0); // PropID
    writer.U32(0); // PropIndex
    writer.U16(0); // PropNum
    writer.U8(fields.baseGameModeEx);
    writer.U8(static_cast<std::uint8_t>(fields.baseGameModeExParameters.size()));
    for (const auto parameter : fields.baseGameModeExParameters)
        writer.U32(parameter);
    writer.U8(0); // MapCheckpointFileIndex
    writer.U8(0); // HasTimerChallenge2ndGameBeginInfo
    writer.U8(fields.hasGameStageInfo ? 1 : 0);
    if (fields.hasGameStageInfo)
    {
        const auto gameStageInfo = writer.BeginNode();
        writer.U8(fields.gameStageIndex);
        writer.U8(fields.totalStageNum);
        if (!writer.EndNode(gameStageInfo))
        {
            SetError(error, "GameStageInfo nested record overflow");
            payload.clear();
            return false;
        }
    }
    writer.U8(fields.carCollectPlayers.empty() ? 0 : 1);
    if (!fields.carCollectPlayers.empty())
    {
        const auto carCollect = writer.BeginNode();
        writer.U16(static_cast<std::uint16_t>(fields.carCollectPlayers.size()));
        for (const auto& player : fields.carCollectPlayers)
        {
            const auto item = writer.BeginNode();
            writer.U32(player.uin);
            writer.U32(player.carId);
            writer.U32(player.taskId);
            if (!writer.EndNode(item))
            {
                SetError(error, "CarCollectPlayer nested record overflow");
                payload.clear();
                return false;
            }
        }
        if (!writer.EndNode(carCollect))
        {
            SetError(error, "CarCollectInfo nested record overflow");
            payload.clear();
            return false;
        }
    }
    writer.U16(fields.reportAntiCollisionDataTime);
    writer.U32(0); // Duration
    writer.U32(0); // BeginCDTime
    writer.U32(0); // PropInterval
    writer.U32(fields.moveFrequency);
    writer.U8(0); // RankedMatchGameLogicTaskInfo
    writer.U16(0); // RankedMatchBeginTipInfoType
    writer.U16(0); // BeginTipLen
    writer.U8(0); // EffectBuffInfoNum
    writer.U8(0); // MapChallengeInfoNum
    writer.U8(0); // IsChangeCar
    writer.U8(fields.anonymousMode);
    if (fields.anonymousMode)
    {
        for (const auto& player : fields.players)
        {
            if (!WriteAnonymousPlayer(writer, player))
            {
                SetError(error, "anonymous player nested record overflow");
                payload.clear();
                return false;
            }
        }
    }
    writer.U8(0); // IsTimeShuttleGen
    writer.U8(0); // HalloweenAdventureInfoNum
    writer.U8(fields.maxPropEffectNum);
    writer.U8(fields.crossNoCollision);
    if (fields.recordCheckFirstLimit != 0)
    {
        if (!WriteOfficialRecordChecks(writer, fields.recordCheckFirstLimit))
        {
            SetError(error, "record-check nested record overflow");
            payload.clear();
            return false;
        }
    }
    else
    {
        writer.U8(0);
    }
    writer.U32(fields.gameBeginSwitchFlag);
    writer.U8(0); // TriggerVeggieDogTask
    writer.U8(0); // HasQSpeedCrystalInfo
    writer.U16(fields.borderRaceType);
    writer.U8(static_cast<std::uint8_t>(fields.raceSyncParameters.size()));
    for (const auto parameter : fields.raceSyncParameters)
        writer.U32(parameter);
    writer.U8(0); // QingHuaMysteryGroundDataNum
    writer.U32(fields.specialEffectSwitchFlag);
    writer.U32(fields.specialEffectParameter);
    writer.U32(0); // YiGuaJiCheckType
    writer.U8(0); // YiGuaJiCheckParaNum
    writer.U32(fields.clientPidIntegrity);
    writer.U8(fields.originTeamId);
    writer.U8(fields.ladderChallenge);
    writer.U8(fields.hasCustomScheme ? 1 : 0);
    if (fields.hasCustomScheme && !WriteCustomScheme(writer, fields.customScheme))
    {
        SetError(error, "CustomScheme nested record overflow");
        payload.clear();
        return false;
    }
    if (fields.hasAurora)
    {
        writer.U64(fields.aurora.workId);
        writer.U8(static_cast<std::uint8_t>(fields.aurora.workName.size()));
        writer.Bytes(fields.aurora.workName);
        writer.U32(fields.aurora.version);
        writer.U16(static_cast<std::uint16_t>(fields.aurora.renderDataUrl.size()));
        writer.Bytes(fields.aurora.renderDataUrl);
    }

    SetError(error, "ok");
    return true;
}
