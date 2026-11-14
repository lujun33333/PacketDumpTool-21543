#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include"Message.h"
#include "AuroraOfficialUGC.h"
#include <map>
#include <string>
#include <vector>

static constexpr BYTE SPEED_LADDER_RECORD_SOURCE_LEGACY = 0;
static constexpr BYTE SPEED_LADDER_RECORD_SOURCE_MAP_CHALLENGE = 1;
static constexpr BYTE SPEED_LADDER_RECORD_SOURCE_SPEED_LADDER = 2;



struct CharmVipLevelNeed {
	INT VipLevel = 1;
	INT NeedScore = 0;
};

struct CharmVipPlayerStateRow {
	UINT Uin = 0;
	INT CurVipLevel = 1;
	INT BaseVipLevel = 1;
	INT AccuVipLevel = 1;
	INT CharmValueOfMonth = 0;
	INT CharmMonthSum = 0;
	INT CharmYearSum = 0;
	INT CharmAccuValue = 0;
	INT GrowthAccuValue = 0;
	INT TotalBrightScore = 0;
	INT CharmGrowthRemainder = 0;
	UINT UpdateTime = 0;
};

// 生涯魅力达人展示态。官服将 V1-V10 的基础等级与 V10 绝影/璀璨
// 两档至尊身份分开传输，不能复用最高只有 9 级的月消费贵族等级。
struct CharmVipCareerDisplayState {
	INT CurVipLevel = 0;
	INT BaseVipLevel = 0;
	INT AccuVipLevel = 0;
	INT SupremeVipLevel = 0;
	INT CharmCareerRank = 0;
	INT TotalBrightScore = 0;
};

struct SpeedLadderPlayerMapRow {
	UINT Uin = 0;
	UINT SeasonID = 1;
	UINT MapID = 0;
	UINT BestTime = 0;
	BYTE TrophyLevel = 0;
	BYTE TrophyCount = 0;
	BYTE Star1Done = 0;
	BYTE Star2Done = 0;
	BYTE Star3Done = 0;
	BYTE AssessDone = 0;
	BYTE RecordSource = SPEED_LADDER_RECORD_SOURCE_LEGACY;
	std::string ReplayMd5;
	std::string ReplayPath;
	UINT UpdatedAt = 0;
};

enum class AuroraActivityDbResult
{
	Ok,
	NotReady,
	NotFound,
	Forbidden,
	Conflict,
	InvalidState,
	DatabaseError,
};

struct AuroraActivityWorkRow
{
	std::uint64_t WorkID = 0;
	std::uint32_t OwnerUin = 0;
	std::string NameRaw;
	std::uint8_t LifecycleState = 0;
	std::uint8_t QualificationStatus = 0;
	std::uint32_t CurrentVersion = 0;
	std::int32_t ComponentCount = 0;
	std::string RenderMd5;
	std::string RenderObjectKey;
	std::string RenderUrl;
	std::string SettingsMd5;
	std::string SettingsObjectKey;
	std::string SettingsUrl;
	std::uint64_t CreatedAtMs = 0;
	std::uint64_t UpdatedAtMs = 0;
};

struct AuroraActivityUploadRow
{
	std::uint64_t UploadID = 0;
	std::uint64_t WorkID = 0;
	std::uint32_t OwnerUin = 0;
	std::uint8_t Operation = 0;
	std::uint8_t Command = 0;
	std::uint32_t RequestTimestamp = 0;
	std::uint32_t WorkVersion = 0;
	std::string ClientRequestID;
	std::string ExpectedMd5;
	std::uint64_t ObjectID = 0;
	std::string ObjectKey;
	std::string PublicUrl;
	std::string AuthHash;
	std::uint8_t Status = 0;
	std::uint64_t CreatedAtMs = 0;
	std::uint64_t ReportedAtMs = 0;
};

struct CarTreasureComItemRow {
	UINT ItemId = 0;
	UINT OpNum = 0;
	INT AvailPeriod = 0;
	UINT ItemType = 0;
};

struct SummerCarTreasureStateRow {
	UINT Uin = 0;
	UINT ActivityId = 0;
	INT LuckValue = 0;
	INT CrystalProgress = 0;
	INT CrystalGotNum = 0;
	INT DiscountUsedNum = 0;
};

struct SummerCarTreasureDbAwardRow {
	UINT ItemId = 0;
	UINT ItemNum = 1;
	INT AvailPeriod = -1;
	UINT ExchangePieceNum = 0;
	bool DirectToBackpack = false;
	INT FinalItemNum = 0;
};

struct SummerCarTreasureProgressAwards {
	SummerCarTreasureDbAwardRow LuckAward;
	SummerCarTreasureDbAwardRow CrystalAward;
	bool LuckGranted = false;
	bool CrystalGranted = false;
};

enum class SummerCarTreasureDrawDbResult {
	Success,
	InsufficientTicket,
	DatabaseError,
};

struct FourBeautiesStateRow {
	UINT CurDraw = 0;
	UINT DrawItemCnt = 0;
	UINT NextBuff = 5;
	UINT BestAwardFlag = 0;
};

struct FourBeautiesTempItemRow {
	UINT Id = 0;
	UINT ItemId = 0;
	UINT ItemNum = 1;
	UINT ItemType = 1;
	INT AvailPeriod = -1;
	UINT FenjieNum = 0;
	UINT Status = 0;
};

struct FourBeautiesBuffRow {
	UINT BuffIdx = 0;
	UINT Count = 0;
	UINT UsedCount = 0;
};

struct GodWarBoxActivityRow {
	UINT ActivityId = 20260417;
	INT SeasonId = 25;
	INT ThemeIdx = 7;
	UINT CallonItemId = 115241;
	INT NumB = 12;
	INT IOpenDate = 20260417;
	INT IOpenTime = 0;
	INT ICloseDate = 20260517;
	INT ICloseTime = 235959;
	INT RoundTriggerA = 10;
	INT TriggerNumN = 4;
	INT DefaultCostItemNum = 1;
	INT CurrentMulAwardIndex = 0;
	INT GodWarBoxReceiveStatus = 0;
	INT TreasureAwardHasGotStatus = 0;
	INT GodWarBoxTriggerNum = 0;
	INT TotalTreasureAwardNum = 0;
	INT CurrRoundNum = 1;
};

struct GodWarBoxAwardRow {
	INT ItemIndex = 0;
	UINT ItemID = 0;
	INT ItemNum = 1;
	INT ItemType = 0;
	INT AvailPeriod = 0;
	INT FenjieNum = 0;
	INT ConpousNum = 0;
	INT SuperMoneyNum = 0;
	INT AlreadyGotDefault = 0;
};

struct GodWarBoxStateRow {
	UINT Uin = 0;
	UINT ActivityId = 20260417;
	INT CurrentMulAwardIndex = 0;
	INT GodWarBoxReceiveStatus = 0;
	INT TreasureAwardHasGotStatus = 0;
	INT GodWarBoxTriggerNum = 0;
	INT TreasureAwardNum = 0;
	INT DrawCount = 0;
	INT CurrRoundNum = 1;
	INT CurrCostItemNum = 1;
};

struct GodWarBoxTempItemRow {
	UINT Id = 0;
	INT ItemIndex = 0;
	UINT ItemID = 0;
	INT ItemNum = 1;
	INT ItemType = 0;
	INT AvailPeriod = 0;
	INT FenjieNum = 0;
	UINT Status = 0;
};

struct SupremeTreasureStateRow {
	UINT Uin = 0;
	UINT ActivityId = 20260501;
	INT DrawNum = 0;
	INT LuckScore = 0;
	INT ScoreDrawDayCount = 0;
	INT ScoreDrawWeekCount = 0;
	UINT AccuClaimMask = 0;
};

struct SecretCompassStateRow {
	UINT Uin = 0;
	UINT ActivityId = 20260501;
	INT PieceScore = 0;
	INT DrawCount = 0;
};

struct SpeedPassStateRow {
	UINT Uin = 0;
	UINT ActivityId = 0;
	INT CurLevel = 0;
	INT CurExp = 0;
	INT PremiumUnlocked = 0;
	INT CollectionUnlocked = 0;
	INT WeekTaskCurScore = 0;
	INT LastWeeklyReset = 0;
};

struct SpeedPassTaskStateRow {
	UINT Uin = 0;
	UINT ActivityId = 0;
	INT TaskId = 0;
	INT Progress = 0;
	INT AwardCount = 0;
	std::string PeriodKey;
};

struct SpeedPassExchangeStateRow {
	UINT Uin = 0;
	UINT ActivityId = 0;
	UINT ItemId = 0;
	INT ExchangeCount = 0;
	std::string PeriodKey = "season";
};

enum class SpeedPassPurchaseDbResult
{
	Success,
	InsufficientFunds,
	CurrencyUnavailable,
	DatabaseError
};

struct HuimengDreamStateRow {
	UINT Uin = 0;
	UINT ActivityId = 20260501;
	INT DrawCount = 0;
	INT GuaranteeCount = 0;
	std::string ExchangeData;
	std::string GiftData;
	std::string ExtraData;
};

struct FinalMagicHouseStateRow {
	UINT Uin = 0;
	UINT ActivityId = 20260501;
	INT DrawCount = 0;
	INT CurrentRound = 1;
	std::string GotItemData;
};

struct ZhiYaoGodsStateRow {
	UINT Uin = 0;
	UINT ActivityId = 20260518;
	INT DrawCount = 0;
	INT AccuScore = 0;
	UINT ClaimMask = 0;
};

struct HuimengDreamTempItemRow {
	UINT Id = 0;
	INT AwardIdx = 0;
	UINT ItemID = 0;
	INT ItemNum = 1;
	INT ItemType = 0;
	INT AvailPeriod = 0;
	INT FenjieNum = 0;
	UINT Status = 0;
};

struct GodHorseStateRow {
	UINT Uin = 0;
	INT ActivityType = 1;
	INT DrawCount = 0;
	UINT ClaimMask = 0;
};

struct WastelandStateRow {
	UINT Uin = 0;
	INT ActivityType = 1;
	INT DrawCount = 0;
	UINT ClaimMask = 0;
};

struct WastelandTempItemRow {
	UINT Id = 0;
	INT ActivityType = 1;
	INT AwardIdx = 0;
	UINT ItemID = 0;
	INT ItemNum = 1;
	INT ItemType = 0;
	INT AvailPeriod = 0;
	INT FenjieNum = 0;
	UINT Status = 0;
};

struct TarotStateRow {
	UINT Uin = 0;
	UINT ActivityId = 20260516;
	INT DrawCount = 0;
	INT CurDraw = 0;
	INT CurScore = 0;
	INT MaxScore = 0;
	INT DrawItemCnt = 0;
	UINT AccuClaimMask = 0;
	UINT Page1CardMask = 0;
	UINT Page2CardMask = 0;
	UINT Page3CardMask = 0;
	UINT Page4CardMask = 0;
};

struct TarotTempItemRow {
	UINT Id = 0;
	INT AwardIdx = 0;
	UINT ItemID = 0;
	INT ItemNum = 1;
	INT ItemType = 0;
	INT AvailPeriod = 0;
	INT FenjieNum = 0;
	INT GetNum = 0;
	UINT Status = 0;
};

struct AnnualRacingStateRow {
	UINT Uin = 0;
	UINT SeasonID = 1;
	UINT OpenDate = 20260711;
	INT StarLevel = 0;
	INT OpenStatus = 0;
	INT DrawCount = 0;
	INT BaoDiLeftNum = 2;
	INT BaoDiType = 0;
	INT SageEggGetNum = 0;
	UINT StarRewardMask = 0;
	UINT RowVersion = 0;
	UINT UpdateTime = 0;
};

struct AnnualRacingTempItemRow {
	std::uint64_t Id = 0;
	UINT Uin = 0;
	UINT SeasonID = 1;
	UINT OpenDate = 20260711;
	INT AwardIndex = 0;
	UINT ItemID = 0;
	UINT ItemID1 = 0;
	INT ItemNum = 1;
	INT ItemType = 0;
	INT AvailPeriod = 0;
	INT ExchangePieceNum = 0;
	UINT Status = 0;
	UINT CreateTime = 0;
	UINT UpdateTime = 0;
};


enum class FieldType {
	INT,
	VARCHAR,
	// 可根据需要添加更多数据类型
};
void InitDB();
void CloseDB();
bool ActivityDB_IsReady(std::string* Error = nullptr);
std::string ActivityDB_QualifiedTableName(const char* TableName);
bool ActivityDB_InitializeAnnualRacing(std::string& Error);
bool ActivityDB_AnnualRacingIsReady();
bool ActivityDB_AnnualRacingLoadState(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	AnnualRacingStateRow& State);
bool ActivityDB_AnnualRacingSaveState(const AnnualRacingStateRow& State);
void ActivityDB_AnnualRacingLoadTempItems(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	std::vector<AnnualRacingTempItemRow>& Rows,
	INT Status = 0);
bool ActivityDB_AnnualRacingAddTempItem(const AnnualRacingTempItemRow& Row);
bool ActivityDB_AnnualRacingTakeTempItem(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	const AnnualRacingTempItemRow& Request,
	UINT NewStatus,
	AnnualRacingTempItemRow& Taken);
INT ActivityDB_AnnualRacingGetPackageCount(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	INT SpecialFlag);
bool ActivityDB_AnnualRacingTryReservePackage(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	INT SpecialFlag,
	INT MaxGetNum,
	INT& NewGetNum);
bool ActivityDB_AnnualRacingReleasePackage(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	INT SpecialFlag);
INT ActivityDB_AnnualRacingGetExchangeCount(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	USHORT ShopType,
	INT AwardIndex);
bool ActivityDB_AnnualRacingTryReserveExchange(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	USHORT ShopType,
	INT AwardIndex,
	INT AddCount,
	INT MaxGetNum,
	INT& NewGetNum);
bool ActivityDB_AnnualRacingReleaseExchange(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	USHORT ShopType,
	INT AwardIndex,
	INT Count);
AuroraActivityDbResult ActivityDB_CreateAuroraDraft(
	const QQSpeedAuroraOfficial::CreatorRequest& Request,
	AuroraActivityWorkRow& Work,
	std::string& Error);
AuroraActivityDbResult ActivityDB_BeginAuroraUpload(
	const QQSpeedAuroraOfficial::CreatorRequest& Request,
	std::uint8_t Command,
	const std::string& PublicBaseUrl,
	const std::string& AuthInfo,
	AuroraActivityWorkRow& Work,
	AuroraActivityUploadRow& Upload,
	std::string& Error);
AuroraActivityDbResult ActivityDB_CompleteAuroraUpload(
	const QQSpeedAuroraOfficial::UploadReportRequest& Request,
	const std::string& AuthHash,
	AuroraActivityWorkRow& Work,
	AuroraActivityUploadRow& Upload,
	std::string& Error);
AuroraActivityDbResult ActivityDB_GetAuroraWork(
	std::uint64_t WorkID,
	AuroraActivityWorkRow& Work,
	std::string& Error);
void InitTaskInfo();
BOOL UserDB_Register(const char* Username, const char* Password, const char* cdk);
BOOL UserDB_RegisterNew(const char* Username, const char* Password, const char* yqm);
DWORD UserDB_GetUin(const char* ip, const char* Username, const char* Pwd);
BOOL UserDB_GetUin(UINT Uin);
void UserDB_GetAllUin(std::vector<UINT>& Uins);
void PlayerDB_GetAllBaseInfoUin(std::vector<UINT>& Uins);
void UserDB_UpdateMD5();
void UserDB_UpdateMD5(const char* Username);
std::string UserDB_GetLoginKey(UINT Uin);
void UserDB_UpdateLoginKey(UINT Uin, const char* key);
BOOL UserDB_CmpIP(const char* IP);
BOOL UserDB_SelectIP(const char* IP);
std::string UserDB_GetIP(UINT Uin);
BOOL UserDB_SelectIPNoTime(const char* IP);
void UserDB_SetTransportConnID(DWORD Uin, DWORD TransportConnID);
void UserDB_SelectTransportConnIDs(std::vector<UINT>& TransportConnIDs);
void UserDB_ClearTransportConnID(DWORD Uin);
void UserDB_ClearTransportConnIDIfMatch(DWORD Uin, DWORD TransportConnID);
void UserDB_ClearTransportConnID();
UINT UserDB_SelectTransportConnID(DWORD Uin);
UINT UserDB_SelectUsrByTransportConnID(DWORD TransportConnID);

BOOL UserDB_CmpPassword(DWORD Uin, const char* Password);
int PlayerDB_AddItem1(std::shared_ptr<ClientNode> Client, DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status, sql::Connection* conn);

int PlayerDB_AddItem2(std::shared_ptr<ClientNode> Client, DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status);
int PlayerDB_AddItem3(std::shared_ptr<ClientNode> Client, DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status, bool Skip);
int PlayerDB_AddItem4(DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status);
int PlayerDB_AddItem5(DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status, sql::Connection* conn, bool* Success = nullptr);
int PlayerDB_AddItem5Batch(const std::vector<UINT>& Uins, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status);

int PlayerDB_AddStoneCar(std::shared_ptr<ClientNode> Client);
int PlayerDB_GetStone(UINT Uin, std::vector<UINT>& vecItem);
int PlayerDB_GetItemAvailPeriod(UINT Uin, UINT Itemid);
int PlayerDB_GetKartRefit(UINT Uin, std::vector<UINT>& vecItem);
int PlayerDB_AddKartStoneGroove(UINT Uin, std::vector<UINT> vecItem);
int PlayerDB_AddKartRefit(UINT Uin, std::vector<UINT> vecItem);
DWORD PlayerDB_GetMapRecord(DWORD Uin, int MapID, std::shared_ptr<ClientNode> Client);
bool PlayerDB_SetMapRecord(DWORD Uin, int MapID, DWORD Record, std::shared_ptr<ClientNode> Client);
bool PlayerDB_GetSpeedLadderPlayerMap(DWORD Uin, UINT SeasonID, UINT MapID, SpeedLadderPlayerMapRow& Row);
bool PlayerDB_GetSpeedLadderPlayerMapBySource(DWORD Uin, UINT SeasonID, UINT MapID, BYTE RecordSource, SpeedLadderPlayerMapRow& Row);
bool PlayerDB_GetSpeedLadderPlayerMapsBySource(DWORD Uin, UINT SeasonID, BYTE RecordSource, std::vector<SpeedLadderPlayerMapRow>& Rows);
bool PlayerDB_UpsertSpeedLadderPlayerMap(DWORD Uin, UINT SeasonID, UINT MapID, UINT BestTime, BYTE TrophyLevel, BYTE TrophyCount, BYTE Star1Done, BYTE Star2Done, BYTE Star3Done, BYTE AssessDone, BYTE RecordSource, const std::string& ReplayMd5, const std::string& ReplayPath);
UINT PlayerDB_GetSpeedLadderTotalTrophyCount(UINT Uin, UINT SeasonID);
bool PlayerDB_GetSpeedLadderRewardClaimed(UINT Uin, UINT SeasonID, UINT PosKey);
bool PlayerDB_SetSpeedLadderRewardClaimed(UINT Uin, UINT SeasonID, UINT PosKey);
bool PlayerDB_ResetSpeedLadderRewardClaims(UINT Uin, UINT SeasonID);
bool PlayerDB_AddBaseInfoRewardCurrency(UINT Uin, UINT Exp, UINT Money, UINT SuperMoney, UINT Coupons, UINT SpeedCoin);
void UserDB_ban(UINT Uin);
void PlayerDB_AddMagicCupTempBox(std::shared_ptr<ClientNode> Client, DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, int ItemType);
BOOL PlayerDB_SelectClientInfo(UINT Uin, ClientInfo*& clientInfo);
BOOL PlayerDB_SelectClientInfo(std::shared_ptr<ClientNode> Client);
void PlayerDB_LoadClientSavedFlags(UINT Uin, BYTE* Flags, size_t FlagLen);
bool PlayerDB_UpdateClientSavedFlagHex(UINT Uin, const BYTE* Flags, size_t FlagLen);
void PlayerDB_LoadExRightFlags(UINT Uin, BYTE* Flags, size_t FlagLen);
bool PlayerDB_UpdateExRightFlagHex(UINT Uin, const BYTE* Flags, size_t FlagLen);

int PlayerDB_GetItemNum(DWORD Uin, DWORD ItemID);
bool PlayerDB_TryConsumeItem(DWORD Uin, DWORD ItemID, INT ItemNum, INT& Remaining);
UINT PlayerDB_GetSpeedCoin(DWORD Uin);
bool PlayerDB_IsSpeedCoinReady();
bool PlayerDB_TrySpendSpeedCoin(DWORD Uin, UINT Cost, UINT& Remaining);
bool PlayerDB_RefundSpeedCoin(DWORD Uin, UINT Amount, UINT& Current);
BOOL PlayerDB_HasItem(DWORD Uin, DWORD ItemID);
BOOL PlayerDB_IsItemExpired(DWORD Uin, DWORD ItemID);
void PlayerDB_ClearExpiredSkinBindings(DWORD Uin);
BOOL PlayerDB_DeleteItem(DWORD Uin, int ItemID);
void PlayerDB_Reduce(UINT Uin, UINT ItemID, int Num);
bool PlayerDB_TryReduceItem(UINT Uin, UINT ItemID, int Num, int& Remaining);
void PlayerDB_UpdateItemNum(UINT Uin, UINT ItemID, int Num);
void PlayerDB_UpdateItemWareHouse(UINT Uin, UINT ItemID, int OpType);
void PlayerDB_UpdateItemOtherAttribute(UINT Uin, UINT ItemID, int type);
int PlayerDB_SelectWareHouseItem(UINT Uin, std::vector<ItemInfo>& Items);
int PlayerDB_SelectItems(UINT Uin, std::vector<ItemInfo>& vecItem);
int PlayerDB_SelectItemsStatus(UINT Uin, std::vector<ItemInfo>& vecItem);
int PlayerDB_LoginSelectItems(UINT Uin, std::vector<ItemInfo>& vecItem);
int PlayerDB_LoginSelectOverItems(UINT Uin, std::vector<ItemInfo>& vecItem);

BOOL PlayerDB_IsHasFriend(UINT Uin, UINT FriendUin);
void PlayerDB_InsertFriend(UINT Uin, UINT FriendUin);
void PlayerDB_UpdateEngageInfo(UINT Uin, UINT EngageUin);
void PlayerDB_UpdateEngageInfoStat(std::shared_ptr<ClientNode> Client);
void PlayerDB_InsertEngageInfo(UINT Uin, UINT EngageUin);
void PlayerDB_DeleteEngageInfo(UINT Uin, UINT EngageUin);
void PlayerDB_UpdateHead(std::shared_ptr<ClientNode> Client);
void PlayerDB_UpdateNobel(UINT Uin, UINT NobleID);
void PlayerDB_InsertStoneNew();
int PlayerDB_UpdateKartStone(UINT Uin, UINT KartID, UINT StoneGrooveID, UINT SkillID);
void PlayerDB_SelectKartStone(UINT Uin, UINT KartID, std::vector<KartStoneItemInfo>& kartStoneItemInfo);


void PlayerDB_UpdatMagicLampInfo(UINT Uin, UINT ItemID, int LightOn, int LuckyValue);
void PlayerDB_DeleteMagicCupTempBox(UINT Uin, UINT ItemID);
int PlayerDB_GetMagicLampNum(DWORD Uin, DWORD ItemID);
void PlayerDB_InsertMagicLamp(DWORD Uin, DWORD ItemID);
void PlayerDB_UpdateBugletTalk(std::shared_ptr<ClientNode> Client);

UINT PlayerDB_SelectGuildID(UINT GuildID, std::string& NickName);
void PlayerDB_SelectGuildInfo(UINT GuildID, Guidinfo*& guidinfo);
void PlayerDB_SelectGuildInfoByUin(UINT Uin, Guidinfo*& guidinfo);
UINT PlayerDB_SelectGuildInfoAll(Guidinfo*& guidinfo);
UINT PlayerDB_SelectGuildMember(UINT GuildID, Guidinfo*& guidinfo);
void PlayerDB_InsertGuildList(UINT Uin, char* newGuildname);
void PlayerDB_UpdateGuildList(UINT ID);
void PlayerDB_UpdateGuildListEnlistType(UINT ID);
void PlayerDB_UpdateGuildListCurrentMember(UINT ID);
void PlayerDB_InsertGuildInfo(DWORD Uin, UINT GuildID, UINT Duty);
void PlayerDB_DeleteGuildInfo(DWORD Uin);
void PlayerDB_UpdateGuildInfoDuty(UINT Uin, UINT Duty);
void PlayerDB_UpdateGuildInfoDuty1(UINT Uin, UINT Duty, bool isTransferLeader = false);

void PlayerDB_UpdateGuildListScore(UINT GuildID, UINT Score, UINT Prosperity);
void PlayerDB_UpdateGuildInfoScore(UINT Uin, UINT GuildID, UINT MeProsperity);

void PlayerDB_InsertKartRefit(DWORD Uin, UINT KartItemID);
void PlayerDB_UpdateKartRefit(DWORD Uin, UINT* RefitItemID, UINT ShapeSuitID, UINT ShapeLegendSuitID, UINT SkinSuitID, UINT KartItemID);
void PlayerDB_UpNickName(DWORD Uin, std::string& NickName);
int PlayerDB_SelectNickName(UINT Uin, std::string& NickName);
int PlayerDB_SelectMapInfo(UINT Uin, std::vector<MapInfo>& mapInfo);
int PlayerDB_SelectPetInfo(UINT Uin, std::vector<PetInfo>& petInfo);
int PlayerDB_SelectRankInfo(std::shared_ptr<ClientNode> Client);

int PlayerDB_GetMagicCupTempBoxNum(DWORD Uin, DWORD ItemID);
int PlayerDB_GetMagicCupTempBox(DWORD Uin, std::vector<ItemInfo>& Items);

void PlayerDB_GetMagicFormationNum(DWORD Uin, std::unordered_map<int, int>& Items);
void PlayerDB_InsertMagicFormation(DWORD Uin, UINT ItemID);
void PlayerDB_GetMagicShopInfo(DWORD Uin, std::unordered_map<int, int>& Items);
void PlayerDB_InsertMagicShopInfo(DWORD Uin, UINT ItemID);

int PlayerDB_GetPetLuck(DWORD Uin, UINT Petid);
void PlayerDB_UpdatePetLuck(DWORD Uin, UINT Petid, UINT Luck);
void PlayerDB_UpdatePetStrengLevel(DWORD Uin, UINT Petid, UINT StrengLevel);

enum PetStrengthenApplyCode : UINT
{
	PET_STRENGTHEN_APPLY_OK = 0,
	PET_STRENGTHEN_APPLY_INVALID = 1,
	PET_STRENGTHEN_APPLY_PET_ROW = 2,
	PET_STRENGTHEN_APPLY_STALE_LEVEL = 3,
	PET_STRENGTHEN_APPLY_BASE_ROW = 4,
	PET_STRENGTHEN_APPLY_MONEY = 5,
	PET_STRENGTHEN_APPLY_ITEM_ROW = 6,
	PET_STRENGTHEN_APPLY_ITEM_NUM = 7,
	PET_STRENGTHEN_APPLY_DATABASE = 8,
};

struct PetStrengthenApplyResult
{
	UINT Code = PET_STRENGTHEN_APPLY_INVALID;
	UINT OldLuckValue = 0;
	UINT NewLuckValue = 0;
	UINT NewStrengLevel = 0;
	UINT NewSuperMoney = 0;
	UINT NewExperience = 0;
	INT CardNumAfter = -1;
};

bool PlayerDB_ApplyPetStrengthenAttempt(
	DWORD Uin,
	UINT Petid,
	UINT ExpectedStrengLevel,
	UINT ItemID,
	UINT UseItemNum,
	UINT CostSuperMoney,
	bool StrengthenSucceeded,
	UINT FailAwardExp,
	PetStrengthenApplyResult& Result);

void PlayerDB_InsertAuctionUnPayInfo(DWORD Uin, UINT BidPrice, AuctionItemInfo AuctionItems);
void PlayerDB_DeleteAuctionInfo(UINT CommodityId);
void PlayerDB_DeleteAuctionUnPayInfo(UINT Uin, UINT CommodityId);
UINT PlayerDB_SelectAuctionUnPayInfo(DWORD Uin, std::vector<AuctionItemInfo>& auction);

int PlayerDB_SelectPlayerInfo(std::shared_ptr<ClientNode> Client, std::vector<PlayerInfo>& playerInfo);
int PlayerDB_SelectPlayerInfo(UINT Uin, std::vector<PlayerInfo>& playerInfo);
int PlayerDB_UpdatePlayerInfo(std::shared_ptr<ClientNode> Client, char* CurLoginTm);
int PlayerDB_InsertPlayer(UINT Uin);
int PlayerDB_InsertBaseInfo(UINT Uin, char* newname, UINT Gender, UINT Country);
int PlayerDB_UpdateBaseInfo(UINT Uin, std::string qqNumber);
int PlayerDB_UpdateBaseInfoFps(std::shared_ptr<ClientNode> Client);
int PlayerDB_SelectBaseInfoKartLibaray(UINT Uin, std::vector<UINT>& myKartLibrary);
int PlayerDB_UpdateBaseInfoKartLibaray(std::shared_ptr<ClientNode> Client);
int PlayerDB_SeleteBaseInfoHead(UINT Uin, std::vector<PlayerInfo>& playerInfo);

int PlayerDB_SelectEngageInfo(std::shared_ptr<ClientNode> Client);


void PlayerDB_AddComItemTable(std::shared_ptr<ClientNode> Client, UINT ItemId, int Aviable, int num);
void PlayerDB_UpdateComItemTable(std::shared_ptr<ClientNode> Client, UINT ItemId, int Aviable, int num);
void PlayerDB_SelectComItemTable(std::shared_ptr<ClientNode> Client, BYTE*& pData, double& intValue);
int PlayerDB_SelectComItemTableNum(std::shared_ptr<ClientNode> Client);
void PlayerDB_GetComItemTableRows(std::shared_ptr<ClientNode> Client, std::vector<CarTreasureComItemRow>& Rows);
bool PlayerDB_LoadCarTreasureSelectedGroups(UINT Uin, std::vector<int>& Groups, UINT ActivityId = 202605);
void PlayerDB_SaveCarTreasureSelectedGroups(UINT Uin, const std::vector<int>& Groups, UINT ActivityId = 202605);
bool PlayerDB_SaveCarTreasureSelectedGroupsOnce(UINT Uin, const std::vector<int>& Groups, UINT ActivityId);
int PlayerDB_GetExchangePieceNum(std::shared_ptr<ClientNode> Client, UINT ItemId, int Aviable, UINT ItemNum);
bool PlayerDB_LoadSummerCarTreasureState(UINT Uin, UINT ActivityId, SummerCarTreasureStateRow& State);
SummerCarTreasureDrawDbResult PlayerDB_ApplySummerCarTreasureDraw(
	UINT Uin,
	UINT ActivityId,
	UINT TicketItemId,
	INT TicketCost,
	INT LuckAdd,
	INT LuckLimit,
	INT CrystalAdd,
	INT CrystalLimit,
	INT CrystalTimes,
	INT DiscountUsedAdd,
	SummerCarTreasureProgressAwards& ProgressAwards,
	std::vector<SummerCarTreasureDbAwardRow>& Awards,
	SummerCarTreasureStateRow& State,
	INT& RemainingTicket);
bool PlayerDB_ApplySummerCarTreasureTempOperation(
	UINT Uin,
	INT Operation,
	UINT PieceItemId,
	std::vector<SummerCarTreasureDbAwardRow>& Items,
	INT& AddedPieceNum,
	INT& FinalPieceNum);
void PlayerDB_EnsureFourBeautiesTables();
bool PlayerDB_FBLoadState(UINT Uin, UINT ActivityId, FourBeautiesStateRow& State);
void PlayerDB_FBSaveState(UINT Uin, UINT ActivityId, const FourBeautiesStateRow& State);
void PlayerDB_FBLoadTempItems(UINT Uin, UINT ActivityId, std::vector<FourBeautiesTempItemRow>& Rows);
void PlayerDB_FBAddTempItem(UINT Uin, UINT ActivityId, const FourBeautiesTempItemRow& Row);
bool PlayerDB_FBRemoveTempItem(UINT Uin, UINT ActivityId, const FourBeautiesTempItemRow& Row, UINT NewStatus = 1);
void PlayerDB_FBLoadGotItems(UINT Uin, UINT ActivityId, std::vector<UINT>& ItemIds);
void PlayerDB_FBMarkGotItem(UINT Uin, UINT ActivityId, UINT ItemId);
void PlayerDB_FBLoadBuffs(UINT Uin, UINT ActivityId, std::vector<FourBeautiesBuffRow>& Rows);
void PlayerDB_FBAddBuff(UINT Uin, UINT ActivityId, UINT BuffIdx, UINT Count = 1);
bool PlayerDB_FBConsumeBuff(UINT Uin, UINT ActivityId, UINT BuffIdx, UINT Count = 1);
void PlayerDB_EnsureGodWarBoxTables();
void PlayerDB_EnsureSupremeTreasureTables();
bool PlayerDB_STLoadState(UINT Uin, UINT ActivityId, SupremeTreasureStateRow& State);
void PlayerDB_STSaveState(UINT Uin, UINT ActivityId, const SupremeTreasureStateRow& State);
void PlayerDB_EnsureSecretCompassTables();
bool PlayerDB_SCLoadState(UINT Uin, UINT ActivityId, SecretCompassStateRow& State);
int PlayerDB_SCAddPieces(UINT Uin, UINT ActivityId, INT AddPieces, INT AddDrawCount = 0);
bool PlayerDB_SCTryConsumePieces(UINT Uin, UINT ActivityId, INT NeedPieces, INT* RemainPieces = nullptr);
void PlayerDB_EnsureSpeedPassTables();
bool PlayerDB_SPLoadState(UINT Uin, UINT ActivityId, SpeedPassStateRow& State);
bool PlayerDB_SPSaveState(UINT Uin, UINT ActivityId, const SpeedPassStateRow& State);
bool PlayerDB_SPCommitStateAndTasks(
	UINT Uin,
	UINT ActivityId,
	const SpeedPassStateRow& State,
	const std::vector<SpeedPassTaskStateRow>& TaskStates);
SpeedPassPurchaseDbResult PlayerDB_SPTryPurchase(
	UINT Uin,
	UINT ActivityId,
	UINT Cost,
	const SpeedPassStateRow& State,
	UINT& Remaining);
bool PlayerDB_SPIsClaimed(UINT Uin, UINT ActivityId, const std::string& Tier, INT Level);
void PlayerDB_SPMarkClaimed(UINT Uin, UINT ActivityId, const std::string& Tier, INT Level);
bool PlayerDB_SPLoadTaskState(UINT Uin, UINT ActivityId, INT TaskId, const std::string& PeriodKey, SpeedPassTaskStateRow& State);
int PlayerDB_SPGetExchangeCount(UINT Uin, UINT ActivityId, UINT ItemId, const std::string& PeriodKey);
void PlayerDB_SPSetExchangeCount(UINT Uin, UINT ActivityId, UINT ItemId, const std::string& PeriodKey, INT ExchangeCount);
bool PlayerDB_GWBLoadActivity(UINT ActivityId, GodWarBoxActivityRow& Row);
void PlayerDB_GWBLoadAwards(UINT ActivityId, std::vector<GodWarBoxAwardRow>& Rows);
bool PlayerDB_GWBLoadState(UINT Uin, UINT ActivityId, GodWarBoxStateRow& State);
void PlayerDB_GWBSaveState(UINT Uin, UINT ActivityId, const GodWarBoxStateRow& State);
void PlayerDB_GWBLoadTempItems(UINT Uin, UINT ActivityId, std::vector<GodWarBoxTempItemRow>& Rows);
void PlayerDB_GWBAddTempItem(UINT Uin, UINT ActivityId, const GodWarBoxTempItemRow& Row);
bool PlayerDB_GWBRemoveTempItem(UINT Uin, UINT ActivityId, const GodWarBoxTempItemRow& Row, UINT NewStatus = 1);
void PlayerDB_GWBAddDrawLog(UINT Uin, UINT ActivityId, INT RoundNum, INT AwardIndex, UINT ItemID, INT ItemNum, UINT CostItemId, INT CostItemNum);
void PlayerDB_EnsureFinalMagicHouseTables();
bool PlayerDB_FMH_LoadSelectedGroups(UINT Uin, UINT ActivityId, std::vector<int>& Groups);
void PlayerDB_FMH_SaveSelectedGroups(UINT Uin, UINT ActivityId, const std::vector<int>& Groups);
bool PlayerDB_FMH_LoadState(UINT Uin, UINT ActivityId, FinalMagicHouseStateRow& State);
void PlayerDB_FMH_SaveState(UINT Uin, UINT ActivityId, const FinalMagicHouseStateRow& State);
void PlayerDB_EnsureHuimengDreamTables();
bool PlayerDB_HM_LoadState(UINT Uin, UINT ActivityId, HuimengDreamStateRow& State);
void PlayerDB_HM_SaveState(UINT Uin, UINT ActivityId, const HuimengDreamStateRow& State);
void PlayerDB_HM_LoadTempItems(UINT Uin, UINT ActivityId, std::vector<HuimengDreamTempItemRow>& Rows, INT Status = 0);
void PlayerDB_HM_AddTempItem(UINT Uin, UINT ActivityId, const HuimengDreamTempItemRow& Row);
bool PlayerDB_HM_RemoveTempItem(UINT Uin, UINT ActivityId, const HuimengDreamTempItemRow& Row, UINT NewStatus = 1);
void PlayerDB_EnsureZhiYaoGodsTables();
bool PlayerDB_ZY_LoadState(UINT Uin, UINT ActivityId, ZhiYaoGodsStateRow& State);
void PlayerDB_ZY_SaveState(UINT Uin, UINT ActivityId, const ZhiYaoGodsStateRow& State);
void PlayerDB_EnsureGodHorseTables();
void PlayerDB_EnsureWastelandTables();
void PlayerDB_EnsureCharmVipTables();
void PlayerDB_EnsureWelfareTables();
bool PlayerDB_WFTryMarkClaimed(UINT Uin, UINT EntryID, INT SeasonID, INT ClaimKind, INT ClaimIndex, UINT ClaimTime);
void PlayerDB_WFLoadClaims(UINT Uin, UINT EntryID, INT SeasonID, INT ClaimKind, std::vector<INT>& ClaimIndexes, UINT& LastClaimTime);
bool PlayerDB_CVIsClaimed(UINT Uin, UINT ActivityId, const std::string& ClaimKey);
bool PlayerDB_CVTryMarkClaimed(UINT Uin, UINT ActivityId, const std::string& ClaimKey, INT Opt, INT VipLevel);
void PlayerDB_CVLoadClaimedKeys(UINT Uin, UINT ActivityId, std::vector<std::string>& ClaimKeys);
bool PlayerDB_CVLoadPlayerState(UINT Uin, CharmVipPlayerStateRow& State, INT DefaultVipLevel = 1);
bool PlayerDB_CVAddCharmAndGrowth(UINT Uin, INT CharmDelta, INT CharmToGrowthRatio, const std::vector<CharmVipLevelNeed>& LevelNeeds, INT DefaultVipLevel, CharmVipPlayerStateRow& State, UINT& NewCharm);
CharmVipCareerDisplayState PlayerDB_CVResolveCareerDisplay(const CharmVipPlayerStateRow& State, INT BaseVipMaxLevel = 10, INT BaseBrightScore = 5000, INT SupremeVip2BaseRank = 100);
bool PlayerDB_GHLoadState(UINT Uin, INT ActivityType, GodHorseStateRow& State);
void PlayerDB_GHSaveState(UINT Uin, INT ActivityType, const GodHorseStateRow& State);
void PlayerDB_GHAddDrawCount(UINT Uin, INT ActivityType, INT AddCount);
bool PlayerDB_GHMarkScoreClaimed(UINT Uin, INT ActivityType, INT SpecialFlag);
int PlayerDB_GHGetExchangeCount(UINT Uin, INT ActivityType, INT AwardIdx);
void PlayerDB_GHAddExchangeCount(UINT Uin, INT ActivityType, INT AwardIdx, INT AddCount = 1);
bool PlayerDB_WSTLoadState(UINT Uin, INT ActivityType, WastelandStateRow& State);
void PlayerDB_WSTSaveState(UINT Uin, INT ActivityType, const WastelandStateRow& State);
bool PlayerDB_WSTMarkScoreClaimed(UINT Uin, INT ActivityType, INT SpecialFlag);
int PlayerDB_WSTGetExchangeCount(UINT Uin, INT ActivityType, INT AwardIdx);
void PlayerDB_WSTAddExchangeCount(UINT Uin, INT ActivityType, INT AwardIdx, INT AddCount = 1);
void PlayerDB_WSTLoadTempItems(UINT Uin, INT ActivityType, std::vector<WastelandTempItemRow>& Rows, INT Status = 0);
void PlayerDB_WSTAddTempItem(UINT Uin, INT ActivityType, const WastelandTempItemRow& Row);
bool PlayerDB_WSTRemoveTempItem(UINT Uin, INT ActivityType, const WastelandTempItemRow& Row, UINT NewStatus = 1);
void PlayerDB_EnsureTarotTables();
bool PlayerDB_TarotLoadState(UINT Uin, UINT ActivityId, TarotStateRow& State);
void PlayerDB_TarotSaveState(UINT Uin, UINT ActivityId, const TarotStateRow& State);
void PlayerDB_TarotLoadTempItems(UINT Uin, UINT ActivityId, std::vector<TarotTempItemRow>& Rows, INT Status = 0);
void PlayerDB_TarotAddTempItem(UINT Uin, UINT ActivityId, const TarotTempItemRow& Row);
bool PlayerDB_TarotRemoveTempItem(UINT Uin, UINT ActivityId, const TarotTempItemRow& Row, UINT NewStatus = 1);
void PlayerDB_TarotAddDrawLog(UINT Uin, UINT ActivityId, INT RoundNum, INT Page, INT CardIdx, UINT ItemID, INT ItemNum, INT ItemType, INT AvailPeriod, UINT CostItemID, INT CostItemNum, INT ZancunFlag);
void PlayerDB_SetRankedInfo(std::shared_ptr<ClientNode> Client);
BOOL isDBtHasItem(std::shared_ptr<ClientNode> Client, int ItemID, int count);
void UpdateClientBase(std::shared_ptr<ClientNode> Client);
void SelectClientExp(std::shared_ptr<ClientNode> Client);
UINT SelectClientExp(UINT Uin);
BOOLEAN SelectSuperItem(std::shared_ptr<ClientNode> Client, UINT Itemid);
void InsertSign3Info(std::shared_ptr<ClientNode> Client);
void UpdateSign3Info(std::shared_ptr<ClientNode> Client, char NextSignDay, char* CurTm, int SignWeek);
void UpdateSign3InfoWeek(std::shared_ptr<ClientNode> Client);
void UpdateBuglet(std::shared_ptr<ClientNode> Client);
std::vector<int> executeQueryRow(const std::string& sqlQuery, const std::string& queryName, const std::vector<int>& conditionValues);
std::vector<int> executeQueryCol(const std::string& sqlQuery, const std::string& queryName, const std::vector<int>& conditionValues);
std::vector<int> executeQueryRow(const std::string& sqlQuery, const std::string& queryName, const std::vector<std::string>& conditionValues);
void UpdateClientKart(std::shared_ptr<ClientNode> Client, int Speed2Weight, int DriftVecWeight, int AdditionalZSpeedWeight, int KartItemID);

void SelectTopList(const std::string& sqlQuery, std::vector<UINT>& List);
UINT SelectKartRefitItemInfo(UINT Uin, KartRefitItemInfo*& KartRefit);
UINT SelectKartRefitItemInfo(UINT Uin, std::vector<KartRefitItemInfo>& KartRefit);
void SelectOneKartRefitItemInfo(UINT Uin, UINT KartID, KartRefitItemInfo*& KartRefit);
void UpdateOneKartRefitItemInfo(UINT Uin, UINT KartID, KartRefitItemInfo*& KartRefit);

void InsertBattleRecord(PlayerRaceInfo* ArrPlayerRaceInfo, UINT Mapid, UINT GameMode);
void UpdateAppellationType(UINT Uin, int type);
int SelectAppellationType(UINT Uin);
void SelectGmTime(int Time);
int SelectChallengeMap(UINT Mapid, UINT RankID, std::vector<ChallengeMap>& ChallengeMaps);
int SelectChallengeMap(std::vector<ChallengeMap>& ChallengeMaps);
int SelectChallengeMap(std::shared_ptr<ClientNode> Client, std::vector<ChallengeMap>& ChallengeMaps);
int UpdateChallengeMapCNT(UINT Mapid, UINT RankID);
int SelectChallengeMapTime(std::shared_ptr<ClientNode> Client);
int SelectChallengeAllMap(std::vector<ChallengeMap>& ChallengeAllMaps, std::vector<ChallengeMapsInfo> ChallengeMaps);
int InSertChallengeMap(std::shared_ptr<ClientNode> Client, ChallengeMap* CM, std::string FilePath);
int SelectBaseInfoEffect(UINT Uin);
int SelectMyKartLibarayNum(UINT Uin, char* Mess);
int SelectMyKartLibarayAllNum(char* Mess);

int Selectmechskinaward(UINT Uin, std::vector<MechTreasureInfo>& MechTreasureInfos);
int SelectmechskinawardGetNum(UINT Uin, UINT Itemid, int Available);
int Insertmechskinaward(UINT Uin, MechTreasureInfo mechTreasureInfo);
int Updatemechskinaward(UINT Uin, std::string Col, UINT Itemid, int itemnum, int Available);
