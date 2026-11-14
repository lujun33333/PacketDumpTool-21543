#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "yaml-cpp/yaml.h"

namespace QQSPetConfig
{
struct Skill
{
	std::uint16_t SkillID = 0;
	std::uint8_t Status = 0;
	std::uint8_t Active = 0;
	std::uint32_t Value = 0;
};

struct StaticPacket
{
	std::uint16_t MsgID = 0;
	std::vector<std::uint8_t> Body;
};

bool Load(const std::string& petDirectory, std::string& error);
bool IsReady();
bool HasPet(std::uint32_t petID);
bool HasShow(std::uint32_t petID);
bool HasStrength(std::uint32_t petID);
bool HasHuanHua(std::uint32_t petID);
bool HasPK(std::uint32_t petID);
YAML::Node Base(std::uint32_t petID);
YAML::Node SkillConfig(std::uint32_t petID);
YAML::Node Strength(std::uint32_t petID);
YAML::Node HuanHua(std::uint32_t petID);
YAML::Node PK(std::uint32_t petID);
YAML::Node PKGlobal();
const std::vector<std::uint32_t>& BaseOrder();
const std::vector<std::uint32_t>& ShowOrder();
const std::vector<std::uint32_t>& StrengthOrder();
const std::vector<std::uint32_t>& StrengthDetailOrder();
const std::vector<std::uint32_t>& HuanHuaOrder();
const std::vector<std::uint32_t>& PKOrder();
bool GetName(std::uint32_t petID, std::string& utf8Name);
bool GetBaseSkills(std::uint32_t petID, std::vector<Skill>& skills);
bool GetLevelForExperience(std::uint32_t experience, std::uint32_t& level);
bool GetMaxExperience(std::uint32_t level, std::uint32_t& maxExperience);
std::uint32_t MaxLevel();
bool GetProtocolCapabilities(std::uint32_t petID, std::uint8_t& canHuanHua, std::uint8_t& canDrive);
bool GetStrengthFullLevel(std::uint32_t petID, std::uint32_t& fullLevel);
bool GetStrengthLevelRule(
	std::uint32_t petID,
	std::uint32_t level,
	std::uint32_t& succProb,
	std::uint32_t& consCardNum,
	std::uint32_t& consSuperMoney);
std::uint32_t StrengthenCardID();
bool GetStrengthRuntimeRules(
	std::uint32_t& cardID,
	std::uint32_t& maxBatch,
	std::uint32_t& failAwardExp);
bool GetStrengthBonuses(std::uint32_t petID, std::uint32_t level, std::map<std::uint32_t, std::uint32_t>& bonuses);
const std::vector<std::uint8_t>& StrengthHeaderBody();
const std::vector<std::vector<std::uint8_t>>& StrengthDetailBodies();
const std::vector<std::uint8_t>& StrengthPetIDBody();
const std::vector<std::uint8_t>& HuanHuaBody();
const std::vector<std::uint8_t>& SkillListBody();
const std::vector<std::vector<std::uint8_t>>& PKMoreBodies();
bool BuildShowPackets(std::uint32_t uin, std::uint32_t petID, std::vector<StaticPacket>& packets, std::string& error);
bool BuildPKGlobalBody(std::uint32_t uin, std::vector<std::uint8_t>& body, std::string& error);
}

inline bool QQS_IsPetFormStatusAllowed(std::uint32_t status, std::uint32_t strengthLevel)
{
	if (status < 1 || status > 5)
		return false;
	return status != 5 || strengthLevel >= 10;
}

// Official 551: pet SkillID=2 protects the remaining unprotected power.
inline int QQS_ApplyOfficialPetCrashProtection(int prePetCrashPower, int petCrashProtectRaw)
{
	if (petCrashProtectRaw <= 0)
		return prePetCrashPower;

	if (prePetCrashPower < 0)
		prePetCrashPower = 0;
	else if (prePetCrashPower > 1000)
		prePetCrashPower = 1000;

	if (petCrashProtectRaw > 1000)
		petCrashProtectRaw = 1000;

	const long long remainingPower =
		static_cast<long long>(1000 - prePetCrashPower) * (1000 - petCrashProtectRaw);
	return 1000 - static_cast<int>(remainingPower / 1000);
}

inline YAML::Node QQS_PetCrashCompensatePowerNode(YAML::Node tail)
{
	const YAML::Node current = tail;
	if (current["m_iCrashCompensatePower"])
		return tail["m_iCrashCompensatePower"];
	if (current["iCrashCompensatePower"])
		return tail["iCrashCompensatePower"];
	return tail["CrashCompensatePower"];
}

inline YAML::Node QQS_551CollisionLoseRateNode(YAML::Node tail)
{
	const YAML::Node current = tail;
	if (current["m_ushCollisionLoseRate"])
		return tail["m_ushCollisionLoseRate"];
	if (current["ushCollisionLoseRate"])
		return tail["ushCollisionLoseRate"];
	return tail["CollisionLoseRate"];
}

inline int QQS_OfficialCollisionLoseRateFromProfile(const YAML::Node& plain, int antiCollisionWeight)
{
	if (antiCollisionWeight < 0)
		antiCollisionWeight = 0;

	const YAML::Node refitCalibration = plain["RefitCalibration"];
	if (refitCalibration && refitCalibration.IsMap())
	{
		const YAML::Node calibration = refitCalibration["CollisionLoseRate"];
		if (calibration && calibration.IsMap())
		{
			try
			{
				const int zeroValue = calibration["ZeroModValue"].as<int>();
				const int observedWeight = calibration["ObservedAntiWeight"].as<int>();
				const int observedValue = calibration["ObservedValue"].as<int>();
				if (observedWeight > 0)
				{
					const long long scaled =
						static_cast<long long>(observedValue - zeroValue) * antiCollisionWeight;
					return zeroValue + static_cast<int>((scaled + observedWeight / 2) / observedWeight);
				}
			}
			catch (...)
			{
			}
		}
	}

	return (antiCollisionWeight * 13) / 10;
}

void LoadPetLevelConfig();
void GetPetInfo(std::shared_ptr<ClientNode> Client);
UINT GetPetLevel(UINT Exp);
UINT GetPetMaxExp(UINT Level);
std::map<UINT, UINT> GetPetStrengLevel(UINT PetID, UINT StrengLevel);
void RequestGetPetInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //24528
void ResponseGetPetInfo(std::shared_ptr<ClientNode> Client); //24528
void RequestGetPetShowInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //24426
void ResponseGetPetShowInfo(std::shared_ptr<ClientNode> Client, UINT PETID); //24426
void ResponseGetPetShowMoreInfo(std::shared_ptr<ClientNode> Client, UINT PETID); //24527
void RequestGetPKPetCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //21140
void ResponseGetPKPetCfg(std::shared_ptr<ClientNode> Client); //21140
void NotifyGetPKPetMoreCfg(std::shared_ptr<ClientNode> Client); //21149
void RequestEmbattle(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //21120
void RequestChangePetEquipStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //15398
void ResponseChangePetEquipStatus(std::shared_ptr<ClientNode> Client, int NewStatus, USHORT ResultID); //15398
void RequestUpdatePetName(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); // 178
void ResponseUpdatePetName(std::shared_ptr<ClientNode> Client);                            // 178
void NotifyPetUpdateName(std::shared_ptr<ClientNode> Client, UINT PetID);                 // 590
void NotifyPetEvolution(std::shared_ptr<ClientNode> Client, UINT Result, UCHAR PreStatus); //584
void NotifyBroadcastPetEvolution(std::shared_ptr<ClientNode> Client, UINT PetID, UINT Status);
void NotifyPetStrengCfgInfo(std::shared_ptr<ClientNode> Client); //20100
void NotifyPetStrengCfgMoreInfo(std::shared_ptr<ClientNode> Client); //28228
void NotifyPetStrengPetID(std::shared_ptr<ClientNode> Client); //24074
PetInfo* GetPetItem(UINT PetID, UINT Uin);
bool QQS_CompletePetInfo(PetInfo& Pet);
bool QQS_LoadPetInfo(UINT Uin, UINT PetID, PetInfo& Pet);
bool QQS_WriteFullPetInfo(BYTE*& p, UINT Uin, const PetInfo& Pet);
bool QQS_WriteVisiblePetInfo(BYTE*& p, UINT Uin, const PetInfo& Pet);
