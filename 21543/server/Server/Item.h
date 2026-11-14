#pragma once
#include <iostream>
#include <unordered_map>
#include <map>
#include <string>
#include <vector>
enum ItemType
{
	EAIT_Unknown,
	EAIT_CAR,
	EAIT_SKIN,
	EAIT_CLOTHES,
	EAIT_PROP,
	EAIT_PET,
	EAIT_STONE
};
struct CarTreasureInfo {
	UINT ItemId = 0;
	UINT MaleItemID = 0;
	UINT FemaleItemID = 0;
	UINT ItemNum = 0;
	INT AvailPeriod = 0;
	INT OtherAttribute = 0;
	UINT iProbability = 0;
	UINT ItemType = 0;
	UINT ExchangePieceNum = 0;
	UINT CanExchange = 0;
	UINT ResultID = 0;
	UINT PieceItemID = 0;
	UINT PosKey = 0;
	UINT AwardIdx = 0;
	UINT LabelStatus = 0;
	UINT GroupIndex = 0; // 自选车组索引，新版赛车夺宝 iGroupIndex
	UINT SelectCarType = 0; // 自选车类型，新版赛车夺宝 iSelectCarType：1=A，2=T，3=S

	bool HasGenderItem() const { return MaleItemID != 0 || FemaleItemID != 0; }
	UINT ResolveItemIdByGender(UINT Gender) const
	{
		if (!HasGenderItem()) return ItemId;
		return (Gender == 0) ? FemaleItemID : MaleItemID; // 当前工程：0=女，1=男
	}
};

struct RankOpenTimeInfo {
	UINT OpenTime = 0;
	UINT CloseTime = 0;
};

// CResponseGetRankedMatchShowInfo -> STRankedMatchedGradeMapInfo.
// Field order and widths were recovered from the current 21543 client Lua 5.4 decoder.
struct RankGradeMapPool {
	USHORT GradeLevel = 0;
	std::vector<UINT> NormalMaps;
	std::vector<UINT> SuperRaceMaps;
	std::vector<UINT> CarFactoryMaps;
	std::vector<UINT> NewMaps;
};

enum class RankPromotionMode : BYTE {
	PromotionMatch = 0,
	Direct = 1,
	Leaderboard = 2,
	LeaderboardMember = 3
};

struct RankGradeProgressionInfo {
	USHORT GradeLevel = 0;
	UINT ChildLevel = 0;
	UINT PromotionScore = 0;
	RankPromotionMode Mode = RankPromotionMode::PromotionMatch;
	USHORT NextGradeLevel = 0;
	UINT NextChildLevel = 0;
	bool HasNext = false;
};

struct RankMasterLeaderboardInfo {
	std::string ScoreField;
	UINT TopRank = 0;
	bool DemoteWhenOut = false;
};

struct RankScoreFormulaInfo {
	INT Base = 0;
	INT PerRank = 0;
};

struct RankRaceScoringInfo {
	UINT MinimumPlayersForPoints = 0;
	INT FinishBonus = 0;
	RankScoreFormulaInfo Finished;
	RankScoreFormulaInfo Unfinished;
};

struct RankShopPreviewInfo {
	UINT Label = 0;
	UINT ItemId = 0;
};

struct RankSuitPartInfo {
	std::string Name;
	UINT MaleItemId = 0;
	UINT FemaleItemId = 0;
	bool Show = true;
	std::string Tip;
};

// The field sets below follow the current 21543 Lua 5.4 wire structures.
// Fields that are constant zero in the S56 packet are written by the serializer
// and are intentionally not exposed as configuration.
struct RankAwardItemInfo {
	UINT ItemId = 0;
	INT ItemNum = 0;
	INT AvailPeriod = 0;
};

struct RankAwardInfo {
	INT SuperMoney = 0;
	INT Coupons = 0;
	std::vector<RankAwardItemInfo> Items;
};

struct RankTaskConditionInfo {
	UINT Type = 0;
	UINT Condition = 0;
};

struct RankTaskInfo {
	std::vector<RankTaskConditionInfo> Conditions;
};

struct RankDailyTaskInfo {
	std::string Desc;
	UINT DailyNum = 0;
	UINT Score = 0;
	UINT RankedCoinNums = 0;
	UINT ExtraTaskId = 0;
	std::vector<RankTaskInfo> TaskInfos;
};

struct RankDailyTaskAwardInfo {
	UINT TaskId = 0;
	USHORT FinishNeed = 0;
	RankAwardInfo Award;
};

struct RankSeasonChallengeTaskInfo {
	UINT Index = 0;
	UINT Type = 0;
	UINT FinishNeedValue = 0;
};

struct RankSeasonChallengeInfo {
	UINT NeedStarNums = 0;
	UINT AwardHonorDefendNums = 0;
	UINT DefendAddScore = 0;
	std::vector<RankSeasonChallengeTaskInfo> Tasks;
};

struct RankSeasonMapChallengeLevelInfo {
	USHORT Level = 0;
	UINT FinishTime = 0;
	UINT AddRankedScore = 0;
	UINT AddRankedCoin = 0;
};

struct RankSeasonMapChallengeMapInfo {
	UINT MapId = 0;
	UINT MapIndex = 0;
	UINT MaxFinishTime = 0;
	BYTE PromotionExtraScore = 0;
	std::vector<RankSeasonMapChallengeLevelInfo> Levels;
};

struct RankSeasonMapChallengeInfo {
	UINT SeasonId = 0;
	UINT OpenDate = 0;
	UINT CloseDate = 0;
	UINT OpenTime = 0;
	UINT CloseTime = 0;
	std::vector<RankSeasonMapChallengeMapInfo> Maps;
};

struct RankTimeRaceMapInfo {
	BYTE Index = 0;
	UINT MapId = 0;
	BYTE Grade = 0;
	std::vector<UINT> TargetTimes;
};

struct RankTimeRaceGradeScoreInfo {
	BYTE Grade = 0;
	std::vector<INT> Scores;
};

struct RankPromotionAwardInfo {
	UINT GradeLevel = 0;
	UINT ChildLevel = 0;
	UINT AwardNeedFinishNums = 0;
	RankAwardInfo Award;
};

struct RankSeasonAwardInfo {
	UINT GradeLevel = 0;
	UINT ChildLevel = 0;
	RankAwardInfo Award;
};

struct RankTopHonorAwardInfo {
	USHORT GradeLevel = 0;
	BYTE Level = 0;
	USHORT RankBegin = 0;
	USHORT RankEnd = 0;
	USHORT AppellationType = 0;
	BYTE Difficulty = 0;
	RankAwardInfo Award;
};

struct RankCommonBoxRewardInfo {
	UINT StartScore = 0;
	RankAwardInfo Award;
	std::vector<RankAwardInfo> ExtraAwards;
};

struct RankDailyFirstWinInfo {
	USHORT ScoreIncrement = 0;
	USHORT DailyScoreIncrement = 0;
	UINT SuperMoney = 0;
	UINT Coupons = 0;
	USHORT CommonBoxType = 0;
	std::vector<RankCommonBoxRewardInfo> BoxRewards;
};

struct RankConfig {
	bool Enabled = false;
	UINT MinimumMatchPlayers = 0;
	bool ManMachineEnabled = false;
	UINT ManMachineWaitSeconds = 0;
	UINT SeasonId = 0;
	UINT OpenDate = 0;
	UINT CloseDate = 0;
	std::vector<RankOpenTimeInfo> OpenTimes;
	std::string SeasonName;
	UINT BackgroundSceneId = 0;

	std::vector<RankGradeMapPool> GradeMapPools;
	std::vector<RankGradeProgressionInfo> GradeProgression;
	RankMasterLeaderboardInfo MasterLeaderboard;
	RankRaceScoringInfo RaceScoring;

	UINT FeiShiId = 0;
	std::string FeiShiDesc;
	std::vector<RankShopPreviewInfo> ShopPreview;
	UINT WeeklyRankCoinLimit = 0;
	UINT RankedCoinItemId = 0;
	UINT WeeklyFactoryCoinLimit = 0;
	std::string SpecialTip;
	UINT NewSeasonMapAddDate = 0;
	std::vector<UINT> SeasonNewMapIds;
	USHORT SuperRaceGradeLimit = 0;
	USHORT NextGradeSeasonTask = 0;
	std::vector<UINT> MissionSeasonTasks;
	std::vector<UINT> MissionRankTasks;
	USHORT MilestoneTotalMatches = 0;
	USHORT MilestoneInterval = 0;
	std::vector<USHORT> MilestoneAwardCounts;
	std::vector<UINT> TestDriveKartIds;
	BYTE TestDriveShowInterval = 0;
	BYTE TestDriveRefitLevel = 0;
	std::vector<UINT> TestDrivePlayerItemIds;
	std::vector<USHORT> TestDriveUnlockSuitNums;
	UINT TopRaceMinOpenSeasonId = 0;
	UINT TopRaceOpenDate = 0;
	RankDailyFirstWinInfo DailyFirstWin;
	std::vector<RankDailyTaskInfo> DailyRankTasks;
	UINT DailyTaskAwardType = 0;
	std::string DailyTaskAwardGroup;
	std::vector<RankDailyTaskAwardInfo> DailyTaskAwards;
	RankSeasonChallengeInfo SeasonChallenge;
	RankSeasonMapChallengeInfo SeasonMapChallenge;
	std::vector<RankTimeRaceMapInfo> TimeRaceMaps;
	std::vector<RankTimeRaceGradeScoreInfo> TimeRaceGradeScores;
	std::vector<RankPromotionAwardInfo> PromotionAwards;
	std::vector<RankSeasonAwardInfo> SeasonAwards;
	std::vector<RankTopHonorAwardInfo> TopHonorAwards;

	std::string SuitName;
	std::vector<RankSuitPartInfo> SuitParts;

	USHORT ShopType = 0;
	std::vector<CarTreasureInfo> ShopItems;
	bool LoadedFromYaml = false;
};


struct CarTreasure {
	UINT MaxRate = 0;
	UINT MaxRate2 = 0;
	std::vector<CarTreasureInfo> Treasures;
	std::vector<CarTreasureInfo> JPTreasures;
	std::map<UINT, std::vector<CarTreasureInfo>> TreasureMap;
	std::map<UINT, std::vector<CarTreasureInfo>> PtTreasureMap;
};
class Item
{
public:
	UINT ID;
	UINT Type;
	UINT buyflag;
	UINT lock;
	UINT GameType; // 游戏模式（0普通 1 滑板 2 轮滑）
	std::string Mess;	   // 详细类型
	std::string Name;	   // 物品名称
};

void LoadItemConfig();
void LoadRankConfig();
const RankConfig& GetRankConfig();
const RankGradeMapPool* GetRankGradeMapPool(USHORT gradeLevel);
const RankGradeProgressionInfo* GetRankGradeProgression(USHORT gradeLevel, UINT childLevel);
const RankGradeProgressionInfo* GetPreviousRankGradeProgression(USHORT gradeLevel, UINT childLevel);
bool IsRankOpenNow(std::string& reason);
UINT GetItemType(UINT ID);
int countItemsWithMess(const std::string& targetMess);
