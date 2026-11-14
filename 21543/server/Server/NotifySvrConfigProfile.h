#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace QQSpeedNotifySvrConfig
{
    struct ClientItemOpenConfig
    {
        std::uint16_t openType = 0;
        std::vector<std::uint32_t> itemIds;
    };

    struct CollisionPowerOtherInfo
    {
        std::uint32_t power = 0;
        std::uint32_t ratio = 0;
        std::uint32_t add = 0;
    };

    struct ClientItemIconConfig
    {
        std::uint32_t itemId = 0;
        std::string iconPath;
    };

    struct KartRefitEffect
    {
        std::uint8_t refitId = 0;
        std::uint8_t modifyValue = 0;
    };

    struct KartRefitCardConfig
    {
        std::uint32_t refitCardId = 0;
        std::vector<KartRefitEffect> effects;
        std::vector<std::uint32_t> kartIds;
        std::vector<std::uint32_t> kartTypes;
    };

    struct Profile
    {
        std::uint32_t switchFlag1 = 0;
        std::uint16_t maxQuickLerpThreshold = 0;
        std::uint16_t quickLerpStepCounts = 0;
        std::uint16_t lerpSyncFrequency = 0;

        std::vector<ClientItemOpenConfig> clientItemOpenConfigs;
        std::vector<CollisionPowerOtherInfo> collisionPowerOtherInfos;
        std::uint32_t collisionMinPower = 0;
        std::uint32_t itemExtendLimit = 0;
        std::uint32_t chattingTalkLevelLimit = 0;
        std::uint32_t chattingSpecialOp = 0;
        std::vector<std::uint32_t> forbidMaps;
        std::uint32_t maxBuySpeedNum = 0;

        std::vector<ClientItemIconConfig> clientItemIcons;
        std::vector<std::uint32_t> tdcBlackCars;
        std::vector<std::uint32_t> tdcBlackCarTypes;
        std::vector<std::uint32_t> tdcBlackCarTypes2nd;

        std::uint32_t minFrameRate = 0;
        std::uint32_t maxFrameRate = 0;
        std::uint32_t frameStep = 0;
        std::uint32_t currentFrameRate = 0;
        std::uint8_t reportFrameRateValue = 0;
        std::uint16_t frameOpSpan = 0;
        std::uint8_t videoSwitch = 0;

        std::vector<KartRefitCardConfig> refitCards;
        std::uint16_t maxHostSpeedAfterCollision = 0;
        std::vector<std::uint32_t> needShowOrderItems;
        std::vector<std::uint32_t> sceneSettingTypes;
        std::vector<std::uint8_t> clientFlag;
    };

    struct Settings
    {
        int selectedVersion = 0;
        Profile config3;
        Profile config4;
    };

    Settings LoadFile(const std::string& path);
    std::vector<std::uint8_t> SerializeTail(const Profile& profile, int version);

    void InstallFile(const std::string& path);
    std::shared_ptr<const Settings> Current();
}
