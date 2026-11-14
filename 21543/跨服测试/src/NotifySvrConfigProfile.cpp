#include "NotifySvrConfigProfile.h"

#include "yaml-cpp/yaml.h"

#include <atomic>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace QQSpeedNotifySvrConfig
{
    namespace
    {
        std::shared_ptr<const Settings> g_currentSettings;

        [[noreturn]] void Fail(const std::string& path, const std::string& reason)
        {
            throw std::runtime_error("NotifySvrConfig.yml: " + path + " " + reason);
        }

        YAML::Node RequiredNode(const YAML::Node& parent, const char* key, const std::string& path)
        {
            const YAML::Node node = parent[key];
            if (!node.IsDefined())
                Fail(path + "." + key, "is required");
            return node;
        }

        YAML::Node RequiredMap(const YAML::Node& parent, const char* key, const std::string& path)
        {
            const YAML::Node node = RequiredNode(parent, key, path);
            if (!node.IsMap())
                Fail(path + "." + key, "must be a map");
            return node;
        }

        YAML::Node RequiredSequence(const YAML::Node& parent, const char* key, const std::string& path)
        {
            const YAML::Node node = RequiredNode(parent, key, path);
            if (!node.IsSequence())
                Fail(path + "." + key, "must be a sequence");
            return node;
        }

        template <typename T>
        T RequiredScalar(const YAML::Node& parent, const char* key, const std::string& path)
        {
            const YAML::Node node = RequiredNode(parent, key, path);
            if (!node.IsScalar())
                Fail(path + "." + key, "must be a scalar");
            try
            {
                return node.as<T>();
            }
            catch (const std::exception& exception)
            {
                Fail(path + "." + key, std::string("is invalid: ") + exception.what());
            }
        }

        std::uint8_t RequiredByte(const YAML::Node& parent, const char* key, const std::string& path)
        {
            const std::uint32_t value = RequiredScalar<std::uint32_t>(parent, key, path);
            if (value > std::numeric_limits<std::uint8_t>::max())
                Fail(path + "." + key, "must fit in uint8");
            return static_cast<std::uint8_t>(value);
        }

        std::uint16_t RequiredWord(const YAML::Node& parent, const char* key, const std::string& path)
        {
            const std::uint32_t value = RequiredScalar<std::uint32_t>(parent, key, path);
            if (value > std::numeric_limits<std::uint16_t>::max())
                Fail(path + "." + key, "must fit in uint16");
            return static_cast<std::uint16_t>(value);
        }

        void RequireCount(
            std::size_t count,
            std::size_t maximum,
            const std::string& path)
        {
            if (count > maximum)
            {
                std::ostringstream message;
                message << "has " << count << " entries; maximum is " << maximum;
                Fail(path, message.str());
            }
        }

        std::vector<std::uint32_t> ReadUIntList(
            const YAML::Node& parent,
            const char* key,
            const std::string& path,
            std::size_t maximum)
        {
            const YAML::Node list = RequiredSequence(parent, key, path);
            RequireCount(list.size(), maximum, path + "." + key);
            std::vector<std::uint32_t> values;
            values.reserve(list.size());
            for (std::size_t index = 0; index < list.size(); ++index)
            {
                try
                {
                    values.push_back(list[index].as<std::uint32_t>());
                }
                catch (const std::exception& exception)
                {
                    Fail(
                        path + "." + key + "[" + std::to_string(index) + "]",
                        std::string("is invalid: ") + exception.what());
                }
            }
            return values;
        }

        std::vector<std::uint8_t> ReadByteList(
            const YAML::Node& parent,
            const char* key,
            const std::string& path,
            std::size_t maximum)
        {
            const YAML::Node list = RequiredSequence(parent, key, path);
            RequireCount(list.size(), maximum, path + "." + key);
            std::vector<std::uint8_t> values;
            values.reserve(list.size());
            for (std::size_t index = 0; index < list.size(); ++index)
            {
                std::uint32_t value = 0;
                try
                {
                    value = list[index].as<std::uint32_t>();
                }
                catch (const std::exception& exception)
                {
                    Fail(
                        path + "." + key + "[" + std::to_string(index) + "]",
                        std::string("is invalid: ") + exception.what());
                }
                if (value > std::numeric_limits<std::uint8_t>::max())
                    Fail(path + "." + key + "[" + std::to_string(index) + "]", "must fit in uint8");
                values.push_back(static_cast<std::uint8_t>(value));
            }
            return values;
        }

        std::vector<ClientItemOpenConfig> ReadClientItemOpenConfigs(
            const YAML::Node& profile,
            const std::string& path)
        {
            const YAML::Node list = RequiredSequence(profile, "ClientItemOpenConfigs", path);
            RequireCount(list.size(), 20, path + ".ClientItemOpenConfigs");
            std::vector<ClientItemOpenConfig> configs;
            configs.reserve(list.size());
            for (std::size_t index = 0; index < list.size(); ++index)
            {
                const std::string itemPath =
                    path + ".ClientItemOpenConfigs[" + std::to_string(index) + "]";
                const YAML::Node node = list[index];
                if (!node.IsMap())
                    Fail(itemPath, "must be a map");
                ClientItemOpenConfig config;
                config.openType = RequiredWord(node, "OpenType", itemPath);
                config.itemIds = ReadUIntList(node, "Items", itemPath, 128);
                configs.push_back(std::move(config));
            }
            return configs;
        }

        std::vector<CollisionPowerOtherInfo> ReadCollisionPowerOtherInfos(
            const YAML::Node& profile,
            const std::string& path)
        {
            const YAML::Node list = RequiredSequence(profile, "CollisionPowerOtherInfos", path);
            RequireCount(list.size(), 32, path + ".CollisionPowerOtherInfos");
            std::vector<CollisionPowerOtherInfo> configs;
            configs.reserve(list.size());
            for (std::size_t index = 0; index < list.size(); ++index)
            {
                const std::string itemPath =
                    path + ".CollisionPowerOtherInfos[" + std::to_string(index) + "]";
                const YAML::Node node = list[index];
                if (!node.IsMap())
                    Fail(itemPath, "must be a map");
                CollisionPowerOtherInfo config;
                config.power = RequiredScalar<std::uint32_t>(node, "Power", itemPath);
                config.ratio = RequiredScalar<std::uint32_t>(node, "Ratio", itemPath);
                config.add = RequiredScalar<std::uint32_t>(node, "Add", itemPath);
                configs.push_back(config);
            }
            return configs;
        }

        std::vector<ClientItemIconConfig> ReadClientItemIcons(
            const YAML::Node& profile,
            const std::string& path)
        {
            const YAML::Node list = RequiredSequence(profile, "ClientItemIcons", path);
            RequireCount(list.size(), 8, path + ".ClientItemIcons");
            std::vector<ClientItemIconConfig> configs;
            configs.reserve(list.size());
            for (std::size_t index = 0; index < list.size(); ++index)
            {
                const std::string itemPath =
                    path + ".ClientItemIcons[" + std::to_string(index) + "]";
                const YAML::Node node = list[index];
                if (!node.IsMap())
                    Fail(itemPath, "must be a map");
                ClientItemIconConfig config;
                config.itemId = RequiredScalar<std::uint32_t>(node, "ItemID", itemPath);
                config.iconPath = RequiredScalar<std::string>(node, "IconPath", itemPath);
                RequireCount(config.iconPath.size(), 64, itemPath + ".IconPath");
                configs.push_back(std::move(config));
            }
            return configs;
        }

        std::vector<KartRefitEffect> ReadEffects(
            const YAML::Node& card,
            const std::string& path)
        {
            const YAML::Node list = RequiredSequence(card, "Effects", path);
            RequireCount(list.size(), 16, path + ".Effects");
            std::vector<KartRefitEffect> effects;
            effects.reserve(list.size());
            for (std::size_t index = 0; index < list.size(); ++index)
            {
                const std::string itemPath = path + ".Effects[" + std::to_string(index) + "]";
                const YAML::Node node = list[index];
                if (!node.IsMap())
                    Fail(itemPath, "must be a map");
                KartRefitEffect effect;
                effect.refitId = RequiredByte(node, "RefitID", itemPath);
                effect.modifyValue = RequiredByte(node, "ModifyValue", itemPath);
                effects.push_back(effect);
            }
            return effects;
        }

        std::vector<KartRefitCardConfig> ReadRefitCards(
            const YAML::Node& profile,
            const std::string& path)
        {
            const YAML::Node list = RequiredSequence(profile, "KartRefitCards", path);
            RequireCount(list.size(), 32, path + ".KartRefitCards");
            std::vector<KartRefitCardConfig> cards;
            cards.reserve(list.size());
            for (std::size_t index = 0; index < list.size(); ++index)
            {
                const std::string itemPath =
                    path + ".KartRefitCards[" + std::to_string(index) + "]";
                const YAML::Node node = list[index];
                if (!node.IsMap())
                    Fail(itemPath, "must be a map");
                KartRefitCardConfig card;
                card.refitCardId = RequiredScalar<std::uint32_t>(node, "RefitCardID", itemPath);
                card.effects = ReadEffects(node, itemPath);
                card.kartIds = ReadUIntList(node, "Karts", itemPath, 64);
                card.kartTypes = ReadUIntList(node, "KartTypes", itemPath, 16);
                cards.push_back(std::move(card));
            }
            return cards;
        }

        Profile ReadProfile(const YAML::Node& profiles, const char* key, int version)
        {
            const std::string path = std::string("Profiles.") + key;
            const YAML::Node node = RequiredMap(profiles, key, "Profiles");
            Profile profile;
            profile.switchFlag1 = RequiredScalar<std::uint32_t>(node, "SwitchFlag1", path);
            profile.maxQuickLerpThreshold = RequiredWord(node, "MaxQuickLerpThreshold", path);
            profile.quickLerpStepCounts = RequiredWord(node, "QuickLerpStepCounts", path);
            profile.lerpSyncFrequency = RequiredWord(node, "LerpSyncFrequency", path);
            profile.clientItemOpenConfigs = ReadClientItemOpenConfigs(node, path);
            profile.collisionPowerOtherInfos = ReadCollisionPowerOtherInfos(node, path);
            profile.collisionMinPower = RequiredScalar<std::uint32_t>(node, "CollisionMinPower", path);
            profile.itemExtendLimit = RequiredScalar<std::uint32_t>(node, "ItemExtendLimit", path);
            profile.chattingTalkLevelLimit =
                RequiredScalar<std::uint32_t>(node, "ChattingTalkLevelLimit", path);
            profile.chattingSpecialOp =
                RequiredScalar<std::uint32_t>(node, "ChattingSpecialOp", path);
            profile.forbidMaps = ReadUIntList(node, "ForbidMaps", path, 32);
            profile.maxBuySpeedNum = RequiredScalar<std::uint32_t>(node, "MaxBuySpeedNum", path);
            profile.clientItemIcons = ReadClientItemIcons(node, path);
            profile.tdcBlackCars = ReadUIntList(node, "TDCBlackCars", path, 64);
            profile.tdcBlackCarTypes = ReadUIntList(node, "TDCBlackCarTypes", path, 16);
            profile.tdcBlackCarTypes2nd = ReadUIntList(node, "TDCBlackCarTypes2nd", path, 16);

            const YAML::Node frameRate = RequiredMap(node, "ClientFrameRate", path);
            profile.minFrameRate =
                RequiredScalar<std::uint32_t>(frameRate, "MinFrameRate", path + ".ClientFrameRate");
            profile.maxFrameRate =
                RequiredScalar<std::uint32_t>(frameRate, "MaxFrameRate", path + ".ClientFrameRate");
            profile.frameStep =
                RequiredScalar<std::uint32_t>(frameRate, "FrameStep", path + ".ClientFrameRate");
            profile.currentFrameRate =
                RequiredScalar<std::uint32_t>(frameRate, "CurrentFrameRate", path + ".ClientFrameRate");
            profile.reportFrameRateValue = RequiredByte(node, "ReportFrameRateValue", path);
            profile.frameOpSpan = RequiredWord(node, "FrameOpSpan", path);
            profile.videoSwitch = RequiredByte(node, "VideoSwitch", path);

            profile.refitCards = ReadRefitCards(node, path);
            profile.maxHostSpeedAfterCollision =
                RequiredWord(node, "MaxHostSpeedAfterCollision", path);
            profile.needShowOrderItems = ReadUIntList(node, "NeedShowOrderItems", path, 32);
            profile.sceneSettingTypes = ReadUIntList(node, "SceneSettingTypes", path, 5);
            profile.clientFlag = ReadByteList(node, "ClientFlag", path, 128);

            if (version == 3)
            {
                for (std::size_t index = 0; index < profile.refitCards.size(); ++index)
                {
                    if (!profile.refitCards[index].kartTypes.empty())
                    {
                        Fail(
                            path + ".KartRefitCards[" + std::to_string(index) + "].KartTypes",
                            "must be empty for the Config3 wire layout");
                    }
                }
                if (!profile.needShowOrderItems.empty())
                    Fail(path + ".NeedShowOrderItems", "must be empty for the Config3 wire layout");
                if (!profile.sceneSettingTypes.empty())
                    Fail(path + ".SceneSettingTypes", "must be empty for the Config3 wire layout");
                if (!profile.clientFlag.empty())
                    Fail(path + ".ClientFlag", "must be empty for the Config3 wire layout");
            }
            return profile;
        }

        class WireWriter
        {
        public:
            void Write8(std::uint8_t value)
            {
                bytes_.push_back(value);
            }

            void Write16(std::uint16_t value)
            {
                bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
                bytes_.push_back(static_cast<std::uint8_t>(value & 0xFF));
            }

            void Write32(std::uint32_t value)
            {
                bytes_.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
                bytes_.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
                bytes_.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
                bytes_.push_back(static_cast<std::uint8_t>(value & 0xFF));
            }

            void WriteBytes(const std::uint8_t* data, std::size_t size)
            {
                if (size == 0)
                    return;
                bytes_.insert(bytes_.end(), data, data + size);
            }

            std::size_t BeginRecord()
            {
                const std::size_t offset = bytes_.size();
                Write16(0);
                return offset;
            }

            void EndRecord(std::size_t offset)
            {
                const std::size_t length = bytes_.size() - offset;
                if (length > std::numeric_limits<std::uint16_t>::max())
                    throw std::runtime_error("NotifySvrConfig record length exceeds uint16");
                bytes_[offset] = static_cast<std::uint8_t>((length >> 8) & 0xFF);
                bytes_[offset + 1] = static_cast<std::uint8_t>(length & 0xFF);
            }

            std::vector<std::uint8_t> Take()
            {
                return std::move(bytes_);
            }

        private:
            std::vector<std::uint8_t> bytes_;
        };

        template <typename T>
        std::uint8_t Count8(const std::vector<T>& values)
        {
            if (values.size() > std::numeric_limits<std::uint8_t>::max())
                throw std::runtime_error("NotifySvrConfig uint8 list count overflow");
            return static_cast<std::uint8_t>(values.size());
        }

        template <typename T>
        std::uint16_t Count16(const std::vector<T>& values)
        {
            if (values.size() > std::numeric_limits<std::uint16_t>::max())
                throw std::runtime_error("NotifySvrConfig uint16 list count overflow");
            return static_cast<std::uint16_t>(values.size());
        }
    }

    Settings LoadFile(const std::string& path)
    {
        const YAML::Node root = YAML::LoadFile(path);
        if (!root.IsMap())
            Fail("<root>", "must be a map");

        Settings settings;
        settings.selectedVersion = RequiredScalar<int>(root, "SelectedVersion", "<root>");
        if (settings.selectedVersion != 3 && settings.selectedVersion != 4)
            Fail("SelectedVersion", "must be exactly 3 or 4");

        const YAML::Node profiles = RequiredMap(root, "Profiles", "<root>");
        settings.config3 = ReadProfile(profiles, "Config3", 3);
        settings.config4 = ReadProfile(profiles, "Config4", 4);
        return settings;
    }

    std::vector<std::uint8_t> SerializeTail(const Profile& profile, int version)
    {
        if (version != 3 && version != 4)
            throw std::invalid_argument("NotifySvrConfig version must be 3 or 4");

        WireWriter writer;
        writer.Write32(profile.switchFlag1);
        writer.Write16(profile.maxQuickLerpThreshold);
        writer.Write16(profile.quickLerpStepCounts);
        writer.Write16(profile.lerpSyncFrequency);

        writer.Write16(Count16(profile.clientItemOpenConfigs));
        for (const ClientItemOpenConfig& config : profile.clientItemOpenConfigs)
        {
            const std::size_t record = writer.BeginRecord();
            writer.Write16(config.openType);
            writer.Write8(Count8(config.itemIds));
            for (std::uint32_t itemId : config.itemIds)
                writer.Write32(itemId);
            writer.EndRecord(record);
        }

        writer.Write8(Count8(profile.collisionPowerOtherInfos));
        for (const CollisionPowerOtherInfo& config : profile.collisionPowerOtherInfos)
        {
            const std::size_t record = writer.BeginRecord();
            writer.Write32(config.power);
            writer.Write32(config.ratio);
            writer.Write32(config.add);
            writer.EndRecord(record);
        }

        writer.Write32(profile.collisionMinPower);
        writer.Write32(profile.itemExtendLimit);
        writer.Write32(profile.chattingTalkLevelLimit);
        writer.Write32(profile.chattingSpecialOp);

        writer.Write8(Count8(profile.forbidMaps));
        for (std::uint32_t mapId : profile.forbidMaps)
            writer.Write32(mapId);

        writer.Write32(profile.maxBuySpeedNum);

        writer.Write8(Count8(profile.clientItemIcons));
        for (const ClientItemIconConfig& config : profile.clientItemIcons)
        {
            const std::size_t record = writer.BeginRecord();
            writer.Write32(config.itemId);
            writer.Write8(static_cast<std::uint8_t>(config.iconPath.size()));
            writer.WriteBytes(
                reinterpret_cast<const std::uint8_t*>(config.iconPath.data()),
                config.iconPath.size());
            writer.EndRecord(record);
        }

        writer.Write8(Count8(profile.tdcBlackCars));
        for (std::uint32_t itemId : profile.tdcBlackCars)
            writer.Write32(itemId);

        writer.Write8(Count8(profile.tdcBlackCarTypes));
        for (std::uint32_t type : profile.tdcBlackCarTypes)
            writer.Write32(type);

        writer.Write8(Count8(profile.tdcBlackCarTypes2nd));
        for (std::uint32_t type : profile.tdcBlackCarTypes2nd)
            writer.Write32(type);

        {
            const std::size_t record = writer.BeginRecord();
            writer.Write32(profile.minFrameRate);
            writer.Write32(profile.maxFrameRate);
            writer.Write32(profile.frameStep);
            writer.Write32(profile.currentFrameRate);
            writer.EndRecord(record);
        }

        writer.Write8(profile.reportFrameRateValue);
        writer.Write16(profile.frameOpSpan);
        writer.Write8(profile.videoSwitch);

        writer.Write8(Count8(profile.refitCards));
        for (const KartRefitCardConfig& card : profile.refitCards)
        {
            const std::size_t cardRecord = writer.BeginRecord();
            writer.Write32(card.refitCardId);
            writer.Write8(Count8(card.effects));
            for (const KartRefitEffect& effect : card.effects)
            {
                const std::size_t effectRecord = writer.BeginRecord();
                writer.Write8(effect.refitId);
                writer.Write8(effect.modifyValue);
                writer.EndRecord(effectRecord);
            }
            writer.Write8(Count8(card.kartIds));
            for (std::uint32_t kartId : card.kartIds)
                writer.Write32(kartId);

            if (version == 4)
            {
                writer.Write8(Count8(card.kartTypes));
                for (std::uint32_t kartType : card.kartTypes)
                    writer.Write32(kartType);
            }
            writer.EndRecord(cardRecord);
        }

        writer.Write16(profile.maxHostSpeedAfterCollision);

        if (version == 4)
        {
            writer.Write16(Count16(profile.needShowOrderItems));
            for (std::uint32_t itemId : profile.needShowOrderItems)
                writer.Write32(itemId);

            writer.Write8(Count8(profile.sceneSettingTypes));
            for (std::uint32_t sceneType : profile.sceneSettingTypes)
                writer.Write32(sceneType);

            writer.Write16(Count16(profile.clientFlag));
            writer.WriteBytes(profile.clientFlag.data(), profile.clientFlag.size());
        }

        return writer.Take();
    }

    void InstallFile(const std::string& path)
    {
        std::shared_ptr<const Settings> parsed = std::make_shared<const Settings>(LoadFile(path));
        std::atomic_store_explicit(&g_currentSettings, std::move(parsed), std::memory_order_release);
    }

    std::shared_ptr<const Settings> Current()
    {
        return std::atomic_load_explicit(&g_currentSettings, std::memory_order_acquire);
    }
}
