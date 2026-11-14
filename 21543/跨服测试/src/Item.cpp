#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Item.h"

#include <unordered_map>
#include "yaml-cpp/yaml.h"
#include <filesystem>
#include <initializer_list>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
std::unordered_map<UINT, std::unique_ptr<Item>> Items;//hashmap

std::map<UINT, std::vector<CarTreasureInfo>> CommonBoxAwards;

RankConfig g_RankConfig;

static std::string RankUtf8ToAcp(const std::string& input)
{
	if (input.empty()) return input;
	int wideLen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), -1, nullptr, 0);
	if (wideLen <= 0) return input;
	std::vector<WCHAR> wide(static_cast<size_t>(wideLen));
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.c_str(), -1, wide.data(), wideLen) <= 0) return input;
	int acpLen = WideCharToMultiByte(CP_ACP, 0, wide.data(), -1, nullptr, 0, nullptr, nullptr);
	if (acpLen <= 0) return input;
	std::string out(static_cast<size_t>(acpLen), '\0');
	if (WideCharToMultiByte(CP_ACP, 0, wide.data(), -1, &out[0], acpLen, nullptr, nullptr) <= 0) return input;
	if (!out.empty() && out.back() == '\0') out.pop_back();
	return out;
}

static std::string RankTrim(std::string value)
{
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
	while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
	return value;
}

static YAML::Node RankRequireNode(const YAML::Node& parent, const char* key, const char* path)
{
	if (!parent || !parent[key]) throw std::runtime_error(std::string("Rank.yml missing: ") + path);
	return parent[key];
}

template<typename T>
static T RankRequireValue(const YAML::Node& parent, const char* key, const char* path)
{
	return RankRequireNode(parent, key, path).as<T>();
}

static std::string RankRequireWireText(const YAML::Node& parent, const char* key, const char* path)
{
	return RankUtf8ToAcp(RankRequireValue<std::string>(parent, key, path));
}

static INT RankReadPeriod(const YAML::Node& node)
{
	if (!node) throw std::runtime_error("Rank.yml missing item period");
	try
	{
		std::string s = RankTrim(node.Scalar());
		if (s == "\xE6\xB0\xB8\xE4\xB9\x85" || s == "\xD3\xC0\xBE\xC3") return -1; // permanent, UTF-8 or GBK
		std::string lower = s;
		std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lower == "permanent" || lower == "forever" || lower == "perm") return -1;
		char* end = nullptr;
		unsigned long long value = std::strtoull(s.c_str(), &end, 10);
		if (end && *end == '\0')
		{
			if (value >= 0xFFFFFFFFull) return -1;
			return static_cast<INT>(value);
		}
	}
	catch (const std::exception&) { throw; }
	return node.as<INT>();
}

static RankAwardInfo RankReadAward(const YAML::Node& node, const std::string& path)
{
	if (!node || !node.IsMap()) throw std::runtime_error("Rank.yml award map required: " + path);
	RankAwardInfo award;
	award.SuperMoney = RankRequireValue<INT>(node, "super_money", (path + ".super_money").c_str());
	award.Coupons = RankRequireValue<INT>(node, "coupons", (path + ".coupons").c_str());
	if (award.SuperMoney < 0 || award.Coupons < 0)
		throw std::runtime_error("Rank.yml award currency cannot be negative: " + path);

	YAML::Node items = RankRequireNode(node, "items", (path + ".items").c_str());
	if (!items.IsSequence()) throw std::runtime_error("Rank.yml award items must be a sequence: " + path);
	if (items.size() > 0x7FFF) throw std::runtime_error("Rank.yml award item count exceeds current signed-short field: " + path);
	for (const auto& item : items)
	{
		RankAwardItemInfo row;
		row.ItemId = RankRequireValue<UINT>(item, "item", (path + ".items[].item").c_str());
		row.ItemNum = RankRequireValue<INT>(item, "count", (path + ".items[].count").c_str());
		row.AvailPeriod = RankReadPeriod(RankRequireNode(item, "period", (path + ".items[].period").c_str()));
		if (row.ItemId == 0 || row.ItemNum <= 0)
			throw std::runtime_error("Rank.yml award item id/count is invalid: " + path);
		award.Items.push_back(row);
	}
	return award;
}

static std::vector<RankPromotionAwardInfo> RankReadPromotionAwards(const YAML::Node& node)
{
	if (!node || !node.IsSequence() || node.size() == 0)
		throw std::runtime_error("Rank.yml rewards.promotion must be a non-empty sequence");
	if (node.size() > 0x7FFF) throw std::runtime_error("Rank.yml promotion award count exceeds current signed-short field");
	std::vector<RankPromotionAwardInfo> rows;
	rows.reserve(node.size());
	for (const auto& item : node)
	{
		RankPromotionAwardInfo row;
		row.GradeLevel = RankRequireValue<UINT>(item, "grade", "rank.rewards.promotion[].grade");
		row.ChildLevel = RankRequireValue<UINT>(item, "child", "rank.rewards.promotion[].child");
		row.AwardNeedFinishNums = RankRequireValue<UINT>(item, "finish_matches", "rank.rewards.promotion[].finish_matches");
		row.Award = RankReadAward(RankRequireNode(item, "award", "rank.rewards.promotion[].award"), "rank.rewards.promotion[].award");
		if (row.GradeLevel == 0 || row.ChildLevel == 0)
			throw std::runtime_error("Rank.yml promotion grade/child cannot be zero");
		rows.push_back(std::move(row));
	}
	return rows;
}

static std::vector<RankSeasonAwardInfo> RankReadSeasonAwards(const YAML::Node& node)
{
	if (!node || !node.IsSequence() || node.size() == 0)
		throw std::runtime_error("Rank.yml rewards.season must be a non-empty sequence");
	if (node.size() > 0x7FFF) throw std::runtime_error("Rank.yml season award count exceeds current signed-short field");
	std::vector<RankSeasonAwardInfo> rows;
	rows.reserve(node.size());
	for (const auto& item : node)
	{
		RankSeasonAwardInfo row;
		row.GradeLevel = RankRequireValue<UINT>(item, "grade", "rank.rewards.season[].grade");
		row.ChildLevel = RankRequireValue<UINT>(item, "child", "rank.rewards.season[].child");
		row.Award = RankReadAward(RankRequireNode(item, "award", "rank.rewards.season[].award"), "rank.rewards.season[].award");
		if (row.GradeLevel == 0 || row.ChildLevel == 0)
			throw std::runtime_error("Rank.yml season grade/child cannot be zero");
		rows.push_back(std::move(row));
	}
	return rows;
}

static std::vector<RankTopHonorAwardInfo> RankReadTopHonorAwards(const YAML::Node& node)
{
	if (!node || !node.IsSequence() || node.size() == 0)
		throw std::runtime_error("Rank.yml rewards.top_honor must be a non-empty sequence");
	if (node.size() > 0x7FFF) throw std::runtime_error("Rank.yml top-honor award count exceeds current signed-short field");
	std::vector<RankTopHonorAwardInfo> rows;
	rows.reserve(node.size());
	for (const auto& item : node)
	{
		RankTopHonorAwardInfo row;
		row.GradeLevel = RankRequireValue<USHORT>(item, "grade", "rank.rewards.top_honor[].grade");
		UINT level = RankRequireValue<UINT>(item, "level", "rank.rewards.top_honor[].level");
		UINT difficulty = RankRequireValue<UINT>(item, "difficulty", "rank.rewards.top_honor[].difficulty");
		if (level > 0xFF || difficulty > 0xFF) throw std::runtime_error("Rank.yml top-honor byte field exceeds 255");
		row.Level = static_cast<BYTE>(level);
		row.Difficulty = static_cast<BYTE>(difficulty);
		row.AppellationType = RankRequireValue<USHORT>(item, "appellation_type", "rank.rewards.top_honor[].appellation_type");
		YAML::Node range = RankRequireNode(item, "rank_range", "rank.rewards.top_honor[].rank_range");
		if (!range.IsSequence() || range.size() != 2)
			throw std::runtime_error("Rank.yml top-honor rank_range must contain exactly two values");
		row.RankBegin = range[0].as<USHORT>();
		row.RankEnd = range[1].as<USHORT>();
		if (row.RankBegin > row.RankEnd) throw std::runtime_error("Rank.yml top-honor rank_range is reversed");
		row.Award = RankReadAward(RankRequireNode(item, "award", "rank.rewards.top_honor[].award"), "rank.rewards.top_honor[].award");
		if (row.GradeLevel == 0 || row.Level == 0)
			throw std::runtime_error("Rank.yml top-honor grade/level cannot be zero");
		rows.push_back(std::move(row));
	}
	return rows;
}

static std::vector<UINT> RankReadUIntSequence(const YAML::Node& node, const char* path, bool allowEmpty = true)
{
	if (!node || !node.IsSequence()) throw std::runtime_error(std::string("Rank.yml sequence required: ") + path);
	if (!allowEmpty && node.size() == 0) throw std::runtime_error(std::string("Rank.yml sequence is empty: ") + path);
	std::vector<UINT> values;
	values.reserve(node.size());
	std::set<UINT> seen;
	for (const auto& item : node)
	{
		UINT value = item.as<UINT>();
		if (value == 0) throw std::runtime_error(std::string("Rank.yml zero ID: ") + path);
		if (!seen.insert(value).second) throw std::runtime_error(std::string("Rank.yml duplicate ID: ") + path);
		values.push_back(value);
	}
	return values;
}

static std::vector<USHORT> RankReadUShortSequence(const YAML::Node& node, const char* path, bool allowEmpty = true)
{
	if (!node || !node.IsSequence()) throw std::runtime_error(std::string("Rank.yml sequence required: ") + path);
	if (!allowEmpty && node.size() == 0) throw std::runtime_error(std::string("Rank.yml sequence is empty: ") + path);
	std::vector<USHORT> values;
	values.reserve(node.size());
	for (const auto& item : node) values.push_back(item.as<USHORT>());
	return values;
}

static std::vector<UINT> RankReadUIntValues(const YAML::Node& node, const char* path)
{
	if (!node || !node.IsSequence()) throw std::runtime_error(std::string("Rank.yml sequence required: ") + path);
	std::vector<UINT> values;
	values.reserve(node.size());
	for (const auto& item : node) values.push_back(item.as<UINT>());
	return values;
}

static bool RankClockIsValid(UINT value)
{
	if (value == 240000) return true;
	UINT hour = value / 10000;
	UINT minute = (value / 100) % 100;
	UINT second = value % 100;
	return hour < 24 && minute < 60 && second < 60;
}

static UINT RankAutoItemType(UINT itemId, UINT configured)
{
	if (configured != static_cast<UINT>(-1)) return configured;
	UINT detected = GetItemType(itemId);
	return detected;
}

static void RankFinalizeShopRows(RankConfig& cfg)
{
	for (size_t i = 0; i < cfg.ShopItems.size(); ++i)
	{
		CarTreasureInfo& row = cfg.ShopItems[i];
		if (row.ItemNum == 0) throw std::runtime_error("Rank.yml shop item count must be greater than zero");
		if (row.ItemType == static_cast<UINT>(-1)) row.ItemType = RankAutoItemType(row.ItemId, static_cast<UINT>(-1));
		row.PosKey = static_cast<UINT>(i);
		row.AwardIdx = static_cast<UINT>(i);
		row.ResultID = 11498;
		row.LabelStatus = 0;
	}
}
static void RankPublishCommonBoxAwards()
{
	if (g_RankConfig.LoadedFromYaml && g_RankConfig.Enabled && !g_RankConfig.ShopItems.empty())
	{
		CommonBoxAwards[g_RankConfig.ShopType] = g_RankConfig.ShopItems;
	}
	else
	{
		CommonBoxAwards.erase(g_RankConfig.ShopType);
	}
}

void LoadRankConfig()
{
	RankConfig cfg;
	std::filesystem::path cfgPath;
	try
	{
		cfgPath = ".\\Config\\Rank.yml";
		if (!std::filesystem::exists(cfgPath)) cfgPath = "..\\..\\Config\\Rank.yml";
		if (!std::filesystem::exists(cfgPath)) throw std::runtime_error("Rank.yml not found");

		YAML::Node file = YAML::LoadFile(cfgPath.string());
		YAML::Node root = RankRequireNode(file, "rank", "rank");
		cfg.Enabled = RankRequireValue<bool>(root, "enabled", "rank.enabled");

		YAML::Node season = RankRequireNode(root, "season", "rank.season");
		cfg.SeasonId = RankRequireValue<UINT>(season, "id", "rank.season.id");
		cfg.OpenDate = RankRequireValue<UINT>(season, "open_date", "rank.season.open_date");
		cfg.CloseDate = RankRequireValue<UINT>(season, "close_date", "rank.season.close_date");
		cfg.SeasonName = RankRequireWireText(season, "name", "rank.season.name");
		cfg.BackgroundSceneId = RankRequireValue<UINT>(season, "background_scene_id", "rank.season.background_scene_id");
		if (cfg.SeasonId == 0 || cfg.SeasonId > 0xFFFF || cfg.OpenDate == 0 || cfg.CloseDate < cfg.OpenDate)
			throw std::runtime_error("Rank.yml invalid season id/date range");
		if (cfg.SeasonName.size() > 20) throw std::runtime_error("Rank.yml season.name exceeds current msg25000 fixed 20-byte field");

		YAML::Node openTimes = RankRequireNode(season, "open_times", "rank.season.open_times");
		if (!openTimes.IsSequence() || openTimes.size() == 0) throw std::runtime_error("Rank.yml season.open_times must not be empty");
		for (const auto& item : openTimes)
		{
			if (!item.IsSequence() || item.size() != 2) throw std::runtime_error("Rank.yml each open_times row must be [open, close]");
			RankOpenTimeInfo value{ item[0].as<UINT>(), item[1].as<UINT>() };
			if (!RankClockIsValid(value.OpenTime) || !RankClockIsValid(value.CloseTime) || value.OpenTime >= value.CloseTime)
				throw std::runtime_error("Rank.yml invalid HHMMSS open time interval");
			cfg.OpenTimes.push_back(value);
		}
		if (cfg.OpenTimes.size() > 0xFFFF) throw std::runtime_error("Rank.yml open time count exceeds current word field");

		YAML::Node maps = RankRequireNode(root, "maps", "rank.maps");
		cfg.MatchmakingGrade = RankRequireValue<USHORT>(maps, "matchmaking_grade", "rank.maps.matchmaking_grade");
		YAML::Node grades = RankRequireNode(maps, "grades", "rank.maps.grades");
		if (!grades.IsSequence() || grades.size() == 0) throw std::runtime_error("Rank.yml maps.grades must not be empty");
		std::set<USHORT> seenGrades;
		for (const auto& item : grades)
		{
			RankGradeMapPool pool;
			pool.GradeLevel = RankRequireValue<USHORT>(item, "grade", "rank.maps.grades[].grade");
			if (!seenGrades.insert(pool.GradeLevel).second) throw std::runtime_error("Rank.yml duplicate map grade");
			pool.NormalMaps = RankReadUIntSequence(RankRequireNode(item, "normal", "rank.maps.grades[].normal"), "rank.maps.grades[].normal", false);
			pool.SuperRaceMaps = RankReadUIntSequence(RankRequireNode(item, "super", "rank.maps.grades[].super"), "rank.maps.grades[].super");
			pool.CarFactoryMaps = RankReadUIntSequence(RankRequireNode(item, "factory", "rank.maps.grades[].factory"), "rank.maps.grades[].factory");
			pool.NewMaps = RankReadUIntSequence(RankRequireNode(item, "new", "rank.maps.grades[].new"), "rank.maps.grades[].new");
			if (pool.NormalMaps.size() > 0xFFFF || pool.SuperRaceMaps.size() > 0xFFFF ||
				pool.CarFactoryMaps.size() > 0xFFFF || pool.NewMaps.size() > 0xFFFF)
				throw std::runtime_error("Rank.yml grade map count exceeds current word field");
			cfg.GradeMapPools.push_back(std::move(pool));
		}
		if (cfg.GradeMapPools.size() > 0xFFFF) throw std::runtime_error("Rank.yml grade row count exceeds current word field");
		const auto selectedPool = std::find_if(cfg.GradeMapPools.begin(), cfg.GradeMapPools.end(), [&](const RankGradeMapPool& pool) { return pool.GradeLevel == cfg.MatchmakingGrade; });
		if (selectedPool == cfg.GradeMapPools.end()) throw std::runtime_error("Rank.yml matchmaking_grade has no matching grade map row");
		if (selectedPool->NormalMaps.size() < 8) throw std::runtime_error("Rank.yml matchmaking map pool requires at least 8 normal maps");

		YAML::Node display = RankRequireNode(root, "display", "rank.display");
		cfg.FeiShiId = RankRequireValue<UINT>(display, "feishi_id", "rank.display.feishi_id");
		cfg.FeiShiDesc = RankRequireWireText(display, "feishi_desc", "rank.display.feishi_desc");
		YAML::Node previewItems = RankRequireNode(display, "shop_preview", "rank.display.shop_preview");
		if (!previewItems.IsSequence()) throw std::runtime_error("Rank.yml display.shop_preview must be a sequence");
		for (const auto& item : previewItems)
		{
			RankShopPreviewInfo row;
			row.Label = RankRequireValue<UINT>(item, "label", "rank.display.shop_preview[].label");
			row.ItemId = RankRequireValue<UINT>(item, "item", "rank.display.shop_preview[].item");
			if (row.ItemId == 0) throw std::runtime_error("Rank.yml shop preview item cannot be zero");
			cfg.ShopPreview.push_back(row);
		}

		YAML::Node protocol = RankRequireNode(root, "protocol", "rank.protocol");
		cfg.WeeklyRankCoinLimit = RankRequireValue<UINT>(protocol, "weekly_rank_coin_limit", "rank.protocol.weekly_rank_coin_limit");
		cfg.WeeklyFactoryCoinLimit = RankRequireValue<UINT>(protocol, "weekly_factory_coin_limit", "rank.protocol.weekly_factory_coin_limit");
		cfg.SpecialTip = RankRequireWireText(protocol, "special_tip", "rank.protocol.special_tip");
		cfg.NewSeasonMapAddDate = RankRequireValue<UINT>(protocol, "new_map_add_date", "rank.protocol.new_map_add_date");
		cfg.SeasonNewMapIds = RankReadUIntSequence(RankRequireNode(protocol, "season_new_map_ids", "rank.protocol.season_new_map_ids"), "rank.protocol.season_new_map_ids");
		if (cfg.SpecialTip.size() > 0xFFFF || cfg.SeasonNewMapIds.size() > 0xFFFF)
			throw std::runtime_error("Rank.yml protocol text/map count exceeds current word field");
		cfg.SuperRaceGradeLimit = RankRequireValue<USHORT>(protocol, "super_race_grade_limit", "rank.protocol.super_race_grade_limit");
		cfg.NextGradeSeasonTask = RankRequireValue<USHORT>(protocol, "next_grade_season_task", "rank.protocol.next_grade_season_task");
		cfg.TopRaceMinOpenSeasonId = RankRequireValue<UINT>(protocol, "top_race_min_open_season_id", "rank.protocol.top_race_min_open_season_id");
		cfg.TopRaceOpenDate = RankRequireValue<UINT>(protocol, "top_race_open_date", "rank.protocol.top_race_open_date");

		YAML::Node mission = RankRequireNode(root, "mission", "rank.mission");
		cfg.MissionSeasonTasks = RankReadUIntSequence(RankRequireNode(mission, "season_tasks", "rank.mission.season_tasks"), "rank.mission.season_tasks");
		cfg.MissionRankTasks = RankReadUIntSequence(RankRequireNode(mission, "rank_tasks", "rank.mission.rank_tasks"), "rank.mission.rank_tasks");
		if (cfg.MissionSeasonTasks.size() > 255 || cfg.MissionRankTasks.size() > 255) throw std::runtime_error("Rank.yml mission task count exceeds current byte field");

		YAML::Node milestone = RankRequireNode(root, "milestone", "rank.milestone");
		cfg.MilestoneTotalMatches = RankRequireValue<USHORT>(milestone, "total_matches", "rank.milestone.total_matches");
		cfg.MilestoneInterval = RankRequireValue<USHORT>(milestone, "interval", "rank.milestone.interval");
		cfg.MilestoneAwardCounts = RankReadUShortSequence(RankRequireNode(milestone, "award_counts", "rank.milestone.award_counts"), "rank.milestone.award_counts", false);
		if (cfg.MilestoneAwardCounts.size() > 255) throw std::runtime_error("Rank.yml milestone award count exceeds current byte field");

		YAML::Node testDrive = RankRequireNode(root, "test_drive", "rank.test_drive");
		cfg.TestDriveKartIds = RankReadUIntSequence(RankRequireNode(testDrive, "kart_ids", "rank.test_drive.kart_ids"), "rank.test_drive.kart_ids", false);
		if (cfg.TestDriveKartIds.size() > 255) throw std::runtime_error("Rank.yml test drive kart count exceeds current byte field");
		UINT testDriveShowInterval = RankRequireValue<UINT>(testDrive, "show_interval", "rank.test_drive.show_interval");
		UINT testDriveRefitLevel = RankRequireValue<UINT>(testDrive, "refit_level", "rank.test_drive.refit_level");
		if (testDriveShowInterval > 255 || testDriveRefitLevel > 255) throw std::runtime_error("Rank.yml test drive byte field exceeds 255");
		cfg.TestDriveShowInterval = static_cast<BYTE>(testDriveShowInterval);
		cfg.TestDriveRefitLevel = static_cast<BYTE>(testDriveRefitLevel);
		cfg.TestDrivePlayerItemIds = RankReadUIntValues(RankRequireNode(testDrive, "player_item_ids", "rank.test_drive.player_item_ids"), "rank.test_drive.player_item_ids");
		cfg.TestDriveUnlockSuitNums = RankReadUShortSequence(RankRequireNode(testDrive, "unlock_suit_nums", "rank.test_drive.unlock_suit_nums"), "rank.test_drive.unlock_suit_nums");
		if (cfg.TestDrivePlayerItemIds.size() != 3 || cfg.TestDriveUnlockSuitNums.size() != 3)
			throw std::runtime_error("Rank.yml current test-drive structure requires exactly 3 player_item_ids and 3 unlock_suit_nums");

		YAML::Node dailyFirstWin = RankRequireNode(root, "daily_first_win", "rank.daily_first_win");
		UINT scoreIncrement = RankRequireValue<UINT>(dailyFirstWin, "score_increment", "rank.daily_first_win.score_increment");
		UINT dailyScoreIncrement = RankRequireValue<UINT>(dailyFirstWin, "daily_score_increment", "rank.daily_first_win.daily_score_increment");
		if (scoreIncrement > 0xFFFF || dailyScoreIncrement > 0xFFFF)
			throw std::runtime_error("Rank.yml daily first-win score field exceeds 65535");
		cfg.DailyFirstWin.ScoreIncrement = static_cast<USHORT>(scoreIncrement);
		cfg.DailyFirstWin.DailyScoreIncrement = static_cast<USHORT>(dailyScoreIncrement);
		cfg.DailyFirstWin.SuperMoney = RankRequireValue<UINT>(dailyFirstWin, "super_money", "rank.daily_first_win.super_money");
		cfg.DailyFirstWin.Coupons = RankRequireValue<UINT>(dailyFirstWin, "coupons", "rank.daily_first_win.coupons");
		cfg.DailyFirstWin.CommonBoxType = RankRequireValue<USHORT>(dailyFirstWin, "common_box_type", "rank.daily_first_win.common_box_type");
		if (cfg.DailyFirstWin.CommonBoxType == 0) throw std::runtime_error("Rank.yml daily first-win common_box_type cannot be zero");
		YAML::Node boxRewards = RankRequireNode(dailyFirstWin, "box_rewards", "rank.daily_first_win.box_rewards");
		if (!boxRewards.IsSequence() || boxRewards.size() == 0 || boxRewards.size() > 0xFF)
			throw std::runtime_error("Rank.yml daily first-win box_rewards must contain 1..255 rows");
		for (const auto& item : boxRewards)
		{
			RankCommonBoxRewardInfo row;
			row.StartScore = RankRequireValue<UINT>(item, "start_score", "rank.daily_first_win.box_rewards[].start_score");
			row.Award = RankReadAward(RankRequireNode(item, "award", "rank.daily_first_win.box_rewards[].award"), "rank.daily_first_win.box_rewards[].award");
			YAML::Node extraAwards = RankRequireNode(item, "extra_awards", "rank.daily_first_win.box_rewards[].extra_awards");
			if (!extraAwards.IsSequence() || extraAwards.size() > 0xFF)
				throw std::runtime_error("Rank.yml daily first-win extra_awards must be a sequence with at most 255 rows");
			for (const auto& award : extraAwards)
				row.ExtraAwards.push_back(RankReadAward(award, "rank.daily_first_win.box_rewards[].extra_awards[]"));
			cfg.DailyFirstWin.BoxRewards.push_back(std::move(row));
		}

		YAML::Node dailyTasks = RankRequireNode(root, "daily_rank_tasks", "rank.daily_rank_tasks");
		if (!dailyTasks.IsSequence() || dailyTasks.size() == 0)
			throw std::runtime_error("Rank.yml daily_rank_tasks must be a non-empty sequence");
		for (const auto& item : dailyTasks)
		{
			RankDailyTaskInfo row;
			row.Desc = RankRequireWireText(item, "desc", "rank.daily_rank_tasks[].desc");
			row.DailyNum = RankRequireValue<UINT>(item, "daily_num", "rank.daily_rank_tasks[].daily_num");
			row.Score = RankRequireValue<UINT>(item, "score", "rank.daily_rank_tasks[].score");
			row.RankedCoinNums = RankRequireValue<UINT>(item, "ranked_coin", "rank.daily_rank_tasks[].ranked_coin");
			row.ExtraTaskId = RankRequireValue<UINT>(item, "extra_task_id", "rank.daily_rank_tasks[].extra_task_id");
			YAML::Node taskInfos = RankRequireNode(item, "task_infos", "rank.daily_rank_tasks[].task_infos");
			if (!taskInfos.IsSequence() || taskInfos.size() == 0)
				throw std::runtime_error("Rank.yml daily task task_infos must be a non-empty sequence");
			for (const auto& taskInfoNode : taskInfos)
			{
				if (!taskInfoNode.IsSequence() || taskInfoNode.size() == 0)
					throw std::runtime_error("Rank.yml daily task condition row must be a non-empty sequence");
				RankTaskInfo taskInfo;
				for (const auto& conditionNode : taskInfoNode)
				{
					RankTaskConditionInfo condition;
					condition.Type = RankRequireValue<UINT>(conditionNode, "type", "rank.daily_rank_tasks[].task_infos[][].type");
					condition.Condition = RankRequireValue<UINT>(conditionNode, "condition", "rank.daily_rank_tasks[].task_infos[][].condition");
					taskInfo.Conditions.push_back(condition);
				}
				row.TaskInfos.push_back(std::move(taskInfo));
			}
			cfg.DailyRankTasks.push_back(std::move(row));
		}

		YAML::Node rewards = RankRequireNode(root, "rewards", "rank.rewards");
		cfg.PromotionAwards = RankReadPromotionAwards(RankRequireNode(rewards, "promotion", "rank.rewards.promotion"));
		cfg.SeasonAwards = RankReadSeasonAwards(RankRequireNode(rewards, "season", "rank.rewards.season"));
		cfg.TopHonorAwards = RankReadTopHonorAwards(RankRequireNode(rewards, "top_honor", "rank.rewards.top_honor"));

		YAML::Node suit = RankRequireNode(root, "suit", "rank.suit");
		cfg.SuitName = RankRequireWireText(suit, "name", "rank.suit.name");
		YAML::Node parts = RankRequireNode(suit, "parts", "rank.suit.parts");
		if (!parts.IsSequence()) throw std::runtime_error("Rank.yml suit.parts must be a sequence");
		for (const auto& item : parts)
		{
			RankSuitPartInfo part;
			part.Name = RankRequireWireText(item, "name", "rank.suit.parts[].name");
			if (item["item"])
			{
				part.MaleItemId = item["item"].as<UINT>();
				part.FemaleItemId = part.MaleItemId;
			}
			else
			{
				part.MaleItemId = RankRequireValue<UINT>(item, "male", "rank.suit.parts[].male");
				part.FemaleItemId = RankRequireValue<UINT>(item, "female", "rank.suit.parts[].female");
			}
			if (part.MaleItemId == 0 || part.FemaleItemId == 0) throw std::runtime_error("Rank.yml suit item cannot be zero");
			part.Show = RankRequireValue<bool>(item, "show", "rank.suit.parts[].show");
			part.Tip = RankRequireWireText(item, "tip", "rank.suit.parts[].tip");
			cfg.SuitParts.push_back(std::move(part));
		}

		YAML::Node shop = RankRequireNode(root, "shop", "rank.shop");
		cfg.ShopType = RankRequireValue<USHORT>(shop, "type", "rank.shop.type");
		YAML::Node shopItems = RankRequireNode(shop, "items", "rank.shop.items");
		if (!shopItems.IsSequence()) throw std::runtime_error("Rank.yml shop.items must be a sequence");
		for (const auto& item : shopItems)
		{
			CarTreasureInfo row;
			row.ItemId = RankRequireValue<UINT>(item, "item", "rank.shop.items[].item");
			row.ItemNum = RankRequireValue<UINT>(item, "count", "rank.shop.items[].count");
			row.AvailPeriod = RankReadPeriod(RankRequireNode(item, "period", "rank.shop.items[].period"));
			row.PieceItemID = RankRequireValue<UINT>(item, "cost_item", "rank.shop.items[].cost_item");
			row.ExchangePieceNum = RankRequireValue<UINT>(item, "cost", "rank.shop.items[].cost");
			row.ItemType = item["item_type"] ? item["item_type"].as<UINT>() : static_cast<UINT>(-1);
			if (row.ItemId == 0) throw std::runtime_error("Rank.yml shop item cannot be zero");
			cfg.ShopItems.push_back(row);
		}
		RankFinalizeShopRows(cfg);
		cfg.LoadedFromYaml = true;
	}
	catch (const std::exception& e)
	{
		printf("Load Rank.yml Error, rank disabled: %s\n", e.what());
		cfg = RankConfig();
	}
	USHORT previousShopType = g_RankConfig.ShopType;
	if (previousShopType != 0) CommonBoxAwards.erase(previousShopType);
	g_RankConfig = std::move(cfg);
	RankPublishCommonBoxAwards();
	if (g_RankConfig.LoadedFromYaml)
	{
		printf("Load Rank.yml Success: %s, season=%u, grade maps=%zu, shop items=%zu\n",
			cfgPath.string().c_str(), g_RankConfig.SeasonId, g_RankConfig.GradeMapPools.size(), g_RankConfig.ShopItems.size());
	}
	else
	{
		printf("Load Rank.yml Failed: no fallback configuration applied\n");
	}
}

const RankConfig& GetRankConfig()
{
	return g_RankConfig;
}

const RankGradeMapPool* GetRankGradeMapPool(USHORT gradeLevel)
{
	const auto it = std::find_if(g_RankConfig.GradeMapPools.begin(), g_RankConfig.GradeMapPools.end(),
		[gradeLevel](const RankGradeMapPool& pool) { return pool.GradeLevel == gradeLevel; });
	return it == g_RankConfig.GradeMapPools.end() ? nullptr : &*it;
}

const RankGradeMapPool* GetRankMatchmakingMapPool()
{
	if (!g_RankConfig.LoadedFromYaml || !g_RankConfig.Enabled) return nullptr;
	return GetRankGradeMapPool(g_RankConfig.MatchmakingGrade);
}

static std::string RankFormatClock(UINT value)
{
	std::ostringstream out;
	out << std::setfill('0') << std::setw(2) << (value / 10000) << ':' << std::setw(2) << ((value / 100) % 100);
	return out.str();
}

bool IsRankOpenNow(std::string& reason)
{
	reason.clear();
	const RankConfig& cfg = GetRankConfig();
	if (!cfg.LoadedFromYaml)
	{
		reason = "Rank.yml加载失败，排位已关闭。";
		return false;
	}
	if (!cfg.Enabled)
	{
		reason = "Rank.yml已关闭排位。";
		return false;
	}

	std::time_t now = std::time(nullptr);
	struct tm localNow{};
	localtime_s(&localNow, &now);
	UINT date = static_cast<UINT>((localNow.tm_year + 1900) * 10000 + (localNow.tm_mon + 1) * 100 + localNow.tm_mday);
	UINT clock = static_cast<UINT>(localNow.tm_hour * 10000 + localNow.tm_min * 100 + localNow.tm_sec);
	if (date < cfg.OpenDate || date > cfg.CloseDate)
	{
		reason = "当前不在Rank.yml配置的排位赛季日期内。";
		return false;
	}
	for (const auto& interval : cfg.OpenTimes)
	{
		if (clock >= interval.OpenTime && clock < interval.CloseTime) return true;
	}

	reason = "排位开启时间：";
	for (size_t i = 0; i < cfg.OpenTimes.size(); ++i)
	{
		if (i != 0) reason += "，";
		reason += RankFormatClock(cfg.OpenTimes[i].OpenTime);
		reason += '-';
		reason += RankFormatClock(cfg.OpenTimes[i].CloseTime);
	}
	reason += "。";
	return false;
}

void LoadItemConfig()
{
	Items.clear();
	try
	{
		YAML::Node Config = YAML::LoadFile(".\\Config\\Item.yml");
		YAML::Node item;
		for (size_t i = 0; i < Config.size(); i++)
		{
			item = Config[i];
			Item* pItem = new Item;
			pItem->ID = item["ID"].as<UINT>();
			//ItemsName[item["ID"].as<UINT>()] = item["Name"].as<std::string>();

			std::string Type = item["Type"].as<std::string>();
			pItem->Type = EAIT_Unknown;
			if (Type == "EAIT_CAR")
			{
				pItem->Type = EAIT_CAR;
			}
			else if (Type == "EAIT_SKIN")
			{
				pItem->Type = EAIT_SKIN;
			}
			else if (Type == "EAIT_CLOTHES")
			{
				pItem->Type = EAIT_CLOTHES;
			}
			else if (Type == "EAIT_PROP")
			{
				pItem->Type = EAIT_PROP;
			}
			else if (Type == "EAIT_PET")
			{
				pItem->Type = EAIT_PET;
			}
			else if (Type == "EAIT_STONE")
			{
				pItem->Type = EAIT_STONE;
			}
			Items[pItem->ID] = std::unique_ptr<Item>(pItem);
			//item.reset();
		}
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load Item Error!\n");
		//exit(0);
	}
	//printf("Load Item Success! [GetItemType%d]\n", GetItemType(127618));

	printf("Load Item Success!\n");
}
void LoadCommonBoxAwards()
{
	try
	{
		CommonBoxAwards.clear();
		std::filesystem::path path(".\\GameTreasureAward\\CommonBoxAward");
		if (exists(path))
		{
			auto begin = std::filesystem::recursive_directory_iterator(path); // 获取文件系统迭代器
			auto end = std::filesystem::recursive_directory_iterator();		  // end迭代器
			for (auto& it = begin; it != end; it++)
			{
				auto& entry = *it;
				if (std::filesystem::is_regular_file(entry))
				{ // 是否是普通文件
					auto& PathInfo = entry.path();
					if (PathInfo.extension() == ".yml")
					{
						UINT Type;
						Type = atof(PathInfo.filename().string().c_str());
						auto path = PathInfo.string();
						const YAML::Node CommonBoxAward = YAML::LoadFile(path);
						for (size_t i = 0; i < CommonBoxAward.size(); i++) {
							CarTreasureInfo CarT;
							CarT.ItemId = CommonBoxAward[i]["ItemId"].as<UINT>();
							CarT.AvailPeriod = CommonBoxAward[i]["AvailPeriod"].as<INT>();
							CarT.ItemNum = CommonBoxAward[i]["ItemNum"].as<UINT>();
							CarT.ExchangePieceNum = CommonBoxAward[i]["PieceNum"].as<UINT>();
							CarT.ResultID = 11498;
							CarT.PieceItemID = 94880;
							CarT.PosKey = static_cast<UINT>(i);
							CarT.AwardIdx = static_cast<UINT>(i);
							CarT.LabelStatus = 0;
							CommonBoxAwards[Type].push_back(CarT);
						}

					}
				}
			}
		}
		auto ReadUIntAny = [](const YAML::Node& node, std::initializer_list<const char*> keys, UINT def) -> UINT
		{
			if (!node) return def;
			for (const char* key : keys)
			{
				try
				{
					if (node[key]) return node[key].as<UINT>();
				}
				catch (const std::exception&) {}
			}
			return def;
		};
		auto ReadIntAny = [](const YAML::Node& node, std::initializer_list<const char*> keys, INT def) -> INT
		{
			if (!node) return def;
			for (const char* key : keys)
			{
				try
				{
					if (node[key]) return node[key].as<INT>();
				}
				catch (const std::exception&) {}
			}
			return def;
		};
		auto LoadDisplayRows = [&](YAML::Node Rows, UINT Type)
		{
			if (!Rows) return;
			CommonBoxAwards[Type].clear();
			for (size_t i = 0; i < Rows.size(); i++)
			{
				YAML::Node Row = Rows[i];
				CarTreasureInfo CarT;
				CarT.ItemId = ReadUIntAny(Row, { "item_id", "ItemId" }, 0);
				if (CarT.ItemId == 0) continue;
				CarT.AvailPeriod = ReadIntAny(Row, { "period", "avail_period", "AvailPeriod" }, 0);
				CarT.ItemNum = ReadUIntAny(Row, { "item_num", "ItemNum" }, 1);
				UINT DetectedItemType = GetItemType(CarT.ItemId);
				CarT.ItemType = ReadUIntAny(Row, { "item_type", "ItemType" }, DetectedItemType);
				CarT.ExchangePieceNum = ReadUIntAny(Row, { "cost", "start_score", "PieceNum" }, 0);
				CarT.ResultID = ReadUIntAny(Row, { "result_id" }, (Type == 365) ? 11503 : 11498);
				CarT.PieceItemID = ReadUIntAny(Row, { "piece_item_id" }, 94880);
				UINT DefaultPosKey = (Type == 125) ? static_cast<UINT>(i + 1) : static_cast<UINT>(i);
				CarT.PosKey = ReadUIntAny(Row, { "pos", "pos_key" }, DefaultPosKey);
				CarT.AwardIdx = ReadUIntAny(Row, { "award_idx" }, CarT.PosKey);
				CarT.LabelStatus = ReadUIntAny(Row, { "label_status" }, 0);
				CommonBoxAwards[Type].push_back(CarT);
			}
		};

		const char* carTreasurePaths[] = {
			".\\24251\\CarTreasure.yml",
			".\\Config\\CarTreasure.yml"
		};
		for (const char* cfgPath : carTreasurePaths)
		{
			std::filesystem::path carTreasureCfg(cfgPath);
			if (!std::filesystem::exists(carTreasureCfg)) continue;
			YAML::Node Config = YAML::LoadFile(carTreasureCfg.string());
			YAML::Node RootCfg = Config["car_treasure"];
			if (!RootCfg) continue;

			YAML::Node ExchangeShop = RootCfg["exchange_shop"];
			if (ExchangeShop)
			{
				UINT ExchangeShopType = ReadUIntAny(ExchangeShop, { "type" }, 125);
				LoadDisplayRows(ExchangeShop["items"], ExchangeShopType);
			}

			YAML::Node SelectDisplay = RootCfg["select_car_display"];
			if (SelectDisplay)
			{
				UINT SelectCarType = ReadUIntAny(SelectDisplay, { "type" }, 365);
				LoadDisplayRows(SelectDisplay["items"], SelectCarType);
			}

			YAML::Node DisplayCfg = RootCfg["cmd24465_display"];
			if (DisplayCfg)
			{
				UINT ExchangeShopType = ReadUIntAny(DisplayCfg, { "exchange_shop_type" }, 125);
				UINT SelectCarType = ReadUIntAny(DisplayCfg, { "select_car_type" }, 365);
				LoadDisplayRows(DisplayCfg["exchange_shop"], ExchangeShopType);
				LoadDisplayRows(DisplayCfg["select_car_display"], SelectCarType);
			}
			break;
		}

		LoadRankConfig();

		printf("Load CommonBoxAward Success!\n");
	}
	catch (const std::exception&)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load CommonBoxAward Error!\n");
	}
}

UINT GetItemType(UINT ID)
{
	std::unique_ptr<Item>& pItem = Items[ID];
	UINT Type = EAIT_Unknown;
	if (pItem)
	{
		Type = pItem->Type;
	}
	//printf("Type:%d\n", Type);
	return Type;
}


UINT GetItemGameType(UINT ID)
{
	std::unique_ptr<Item>& pItem = Items[ID];
	UINT GameType = 0;
	if (pItem)
	{
		GameType = pItem->GameType;
	}
	return GameType;
}


std::string GetItemMess(UINT ID)
{
	std::unique_ptr<Item>& pItem = Items[ID];
	std::string Mess = "";
	if (pItem)
	{
		Mess = pItem->Mess;
	}
	return Mess;
}

int countItemsWithMess( const std::string& targetMess) {
	int count = 0;

	for (const auto& pair : Items) {
		if (pair.second->Mess == targetMess) {
			++count;
		}
	}

	return count;
}
