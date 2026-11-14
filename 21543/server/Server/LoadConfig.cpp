#include "LoadConfig.h"
#include "yaml-cpp/yaml.h"
#include "Message.h"
#include "GmPermission.h"
#include "DBConnection.h"
#include "Item.h"
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern std::shared_ptr<MAPPOOL> AllMap;
extern std::shared_ptr<MAPPOOL> BorderMap;
extern std::shared_ptr<MAPPOOL> EventMap;
extern std::shared_ptr<MAPPOOL> RankMap;
extern std::map<long, std::shared_ptr<RANDOMMAPPOOL>> RandomMapPools;
extern std::shared_ptr<CarTreasure> CarTreasures;//夺宝配置
extern std::unordered_map<long, std::unique_ptr<TREASUREBOX>> TreasureBoxs;//hashmap
//extern TREASUREBOX* GameEndBox;
extern std::vector<ItemInfo> GameEndBox;
extern int GameEndBoxTime[4];
extern std::vector<ItemInfo> MagicLampItems;
extern std::shared_ptr<AuctionCfg> AuctionConfig;
extern std::unordered_map<long, std::shared_ptr<ItemInfo>> RegisterBoxs;//hashmap
extern std::unordered_map<long, std::shared_ptr<ItemInfo>> DayRaffleBoxs;//hashmap
extern std::unordered_map<UINT, std::unique_ptr<Item>> Items;
extern std::unordered_map<long, std::shared_ptr<ItemInfo>> MagicFormationBoxs;
extern std::unordered_map<long, std::shared_ptr<ItemInfo>> MagicExchangeShopCarBoxs;//hashmap
extern std::unordered_map<long, std::shared_ptr<ItemInfo>> MagicExchangeShopPetBoxs;//hashmap
extern std::unordered_map<long, std::shared_ptr<ItemInfo>> MagicExchangeShopSeatBoxs;//hashmap
extern std::map<UINT, std::shared_ptr<OpenBoxDef>> GameEndBox2Uin;
extern std::shared_ptr<MIRACLECFG> MiracleCfg;
extern int MagicCupExchange[7];
extern std::map<UINT, std::shared_ptr<AppellationInfo>> Appellations;

static bool LC_YamlTryIntNoThrow(const YAML::Node& node, int& out)
{
	if (!node || !node.IsScalar()) return false;
	return YAML::convert<int>::decode(node, out);
}

static bool LC_YamlTryUIntNoThrow(const YAML::Node& node, UINT& out)
{
	if (!node || !node.IsScalar()) return false;
	return YAML::convert<UINT>::decode(node, out);
}

static bool LC_YamlTryBoolNoThrow(const YAML::Node& node, bool& out)
{
	if (!node || !node.IsScalar()) return false;
	if (YAML::convert<bool>::decode(node, out)) return true;

	int intValue = 0;
	if (YAML::convert<int>::decode(node, intValue))
	{
		out = (intValue != 0);
		return true;
	}
	return false;
}
extern std::set<UINT> ClothesGMUins;  //只能刷衣服
extern std::set<UINT> CarGMUins;  //只能刷车
extern std::set<UINT> NewClothesGMUins;  //新版只刷衣服
extern std::set<UINT> NewCarGMUins;  //新版只刷赛车
extern std::set<UINT> NewClothesCarGMUins;
extern std::set<UINT> AdminUins;//赌注模式管理 
extern std::string ClothesGMMess;
extern std::string CarGMMess;
extern std::string NewClothesGMMess;
extern std::string NewCarGMMess;
extern std::string NewClothesCarGMMess;
extern UINT ClothesGMMaxItemID;
extern UINT CarGMMaxItemID;
extern UINT NewClothesGMMaxItemID;
extern UINT NewCarGMMaxItemID;
extern UINT NewClothesCarGMMaxItemID;
extern std::map<UINT, GmUinLimit> ClothesGMLimits;
extern std::map<UINT, GmUinLimit> CarGMLimits;
extern std::map<UINT, GmUinLimit> NewClothesGMLimits;
extern std::map<UINT, GmUinLimit> NewCarGMLimits;
extern std::map<UINT, GmUinLimit> NewClothesCarGMLimits;
extern std::vector<MechTreasureInfo> mechExchangeTreasureInfo;
extern std::vector<MechTreasureInfo> mechTreasureInfo;
extern std::set<UINT> SpecialLoginUins; // 定义特殊登录用户集合
extern std::vector<ActiveHeadItemList> gActiveHeadItemListCfg;
extern std::vector<ActiveHeadItemList> gActiveHeadShokItemListCfg;
extern std::vector<ActiveHeadItemList> gActiveBackPlateItemListCfg;

std::string removeSpaces(const std::string& str) {
	std::string noSpaces = str;
	noSpaces.erase(std::remove_if(noSpaces.begin(), noSpaces.end(), [](unsigned char c) { return ::isspace(c); }), noSpaces.end());
	return noSpaces;
}

static bool IsValidUtf8Bytes(const std::string& str)
{
	size_t i = 0;
	while (i < str.size())
	{
		unsigned char c = static_cast<unsigned char>(str[i]);
		if (c <= 0x7F)
		{
			i++;
			continue;
		}

		size_t extra = 0;
		if ((c & 0xE0) == 0xC0)
		{
			if (c < 0xC2) return false;
			extra = 1;
		}
		else if ((c & 0xF0) == 0xE0)
		{
			extra = 2;
		}
		else if ((c & 0xF8) == 0xF0)
		{
			if (c > 0xF4) return false;
			extra = 3;
		}
		else
		{
			return false;
		}

		if (i + extra >= str.size()) return false;
		for (size_t j = 1; j <= extra; j++)
		{
			unsigned char cc = static_cast<unsigned char>(str[i + j]);
			if ((cc & 0xC0) != 0x80) return false;
		}
		i += extra + 1;
	}
	return true;
}

static std::string BuildPermissionMessForMatch(const std::string& messText)
{
	// 兼容两种现场配置：
	// 1) 旧版服务器常见 GBK/ANSI AUserPermissions.yml：必须保留原始字节，不能强制 UTF2GB。
	// 2) 新保存的 UTF-8 AUserPermissions.yml：额外追加一份 UTF2GB 后的文本用于匹配数据库 itemMess。
	std::string rawMess = removeSpaces(messText);
	if (!IsValidUtf8Bytes(messText))
	{
		return rawMess;
	}

	std::string gbMess = removeSpaces(UTF2GB(messText.c_str()));
	if (gbMess.empty() || gbMess == rawMess)
	{
		return rawMess;
	}
	return rawMess + "," + gbMess;
}

static std::string YamlKeyToString(const YAML::Node& node)
{
	if (!node) return "";
	try
	{
		return node.as<std::string>();
	}
	catch (...)
	{
	}
	int intValue = 0;
	if (YAML::convert<int>::decode(node, intValue))
		return std::to_string(intValue);
	return "";
}

static void LoadGmDenyItems(const YAML::Node& node, std::set<UINT>& deny)
{
	if (!node) return;
	if (node.IsScalar())
	{
		UINT id = 0;
		if (LC_YamlTryUIntNoThrow(node, id) && id)
			deny.insert(id);
		return;
	}
	if (!node.IsSequence()) return;
	for (size_t i = 0; i < node.size(); i++)
	{
		const YAML::Node item = node[i];
		UINT id = 0;
		if (item.IsScalar())
		{
			LC_YamlTryUIntNoThrow(item, id);
		}
		else if (item.IsMap())
		{
			if (item["ItemID"]) LC_YamlTryUIntNoThrow(item["ItemID"], id);
			else if (item["ItemId"]) LC_YamlTryUIntNoThrow(item["ItemId"], id);
			else if (item["id"]) LC_YamlTryUIntNoThrow(item["id"], id);
		}
		if (id)
			deny.insert(id);
	}
}

static void LoadGmUinList(const YAML::Node& uinsNode, std::set<UINT>& allUins, std::map<UINT, GmUinLimit>& limits, const GmUinLimit& limitTemplate)
{
	if (!uinsNode || !uinsNode.IsSequence()) return;
	for (size_t i = 0; i < uinsNode.size(); i++)
	{
		UINT uin = 0;
		if (uinsNode[i] && uinsNode[i]["Uin"])
			LC_YamlTryUIntNoThrow(uinsNode[i]["Uin"], uin);
		if (!uin) continue;
		allUins.insert(uin);
		limits[uin] = limitTemplate;
	}
}

static void LoadGmRoleNode(
	const YAML::Node& node,
	std::string& mess,
	std::set<UINT>& uins,
	UINT& fallbackMaxItemID,
	std::map<UINT, GmUinLimit>& limits)
{
	if (!node) return;
	if (node["Mess"] && node["Mess"].IsScalar())
		mess = BuildPermissionMessForMatch(node["Mess"].as<std::string>());
	fallbackMaxItemID = 0;
	if (node["MaxItemID"])
		LC_YamlTryUIntNoThrow(node["MaxItemID"], fallbackMaxItemID);

	// 旧写法：组顶层 Uins + MaxItemID 仍然有效。
	if (node["Uins"])
	{
		GmUinLimit lim;
		lim.MaxItemID = fallbackMaxItemID;
		lim.VersionName = "default";
		LoadGmDenyItems(node["DenyItems"], lim.DenyItems);
		LoadGmUinList(node["Uins"], uins, limits, lim);
	}

	// 新写法：Versions.<版本号> 下独立 Uins / MaxItemID / DenyItems，可无限加块。
	const YAML::Node versions = node["Versions"];
	if (versions && versions.IsMap())
	{
		for (auto it = versions.begin(); it != versions.end(); ++it)
		{
			const std::string verName = YamlKeyToString(it->first);
			if (verName.empty()) continue;
			const YAML::Node ver = it->second;
			if (!ver || !ver.IsMap()) continue;
			GmUinLimit lim;
			lim.VersionName = verName;
			lim.MaxItemID = fallbackMaxItemID;
			if (ver["MaxItemID"])
				LC_YamlTryUIntNoThrow(ver["MaxItemID"], lim.MaxItemID);
			LoadGmDenyItems(ver["DenyItems"], lim.DenyItems);
			LoadGmUinList(ver["Uins"], uins, limits, lim);
		}
	}
}

void LoadMechTreasureInfoConfig()
{
	try
	{
		mechExchangeTreasureInfo.clear();
		YAML::Node MechExchangeTreasureInfoNode = YAML::LoadFile(".\\GameTreasureAward\\MechTreasure\\MechExchangeTreasure.yml");
		for (size_t i = 0; i < MechExchangeTreasureInfoNode.size(); i++)
		{
			MechTreasureInfo MT;
			MT.ItemId = MechExchangeTreasureInfoNode[i]["itemid"].as<UINT>();
			MT.AvailPeriod = MechExchangeTreasureInfoNode[i]["AvailPeriod"].as<int>();
			MT.ItemNum = MechExchangeTreasureInfoNode[i]["ItemNum"].as<UINT>();
			MT.exchangeNeedNum = MechExchangeTreasureInfoNode[i]["exchangeNeedNum"].as<UINT>();
			MT.maxGetNum = MechExchangeTreasureInfoNode[i]["maxGetNum"].as<UINT>();
			MT.award_idx = MechExchangeTreasureInfoNode[i]["award_idx"].as<UINT>();
			MT.errid = MechExchangeTreasureInfoNode[i]["errid"].as<UINT>();

			MT.item_type = MechExchangeTreasureInfoNode[i]["item_type"].as<UINT>();
			MT.status = MechExchangeTreasureInfoNode[i]["status"].as<UINT>();
			MT.getnum = MechExchangeTreasureInfoNode[i]["getnum"].as<UINT>();
			mechExchangeTreasureInfo.push_back(MT);
		}

		mechTreasureInfo.clear();
		YAML::Node MechTreasureInfoNode = YAML::LoadFile(".\\GameTreasureAward\\MechTreasure\\MechTreasure.yml");
		for (size_t i = 0; i < MechTreasureInfoNode.size(); i++)
		{
			MechTreasureInfo MT;
			MT.ItemId = MechTreasureInfoNode[i]["itemid"].as<UINT>();
			MT.AvailPeriod = MechTreasureInfoNode[i]["AvailPeriod"].as<int>();
			MT.alreadyGot = MechTreasureInfoNode[i]["alreadyGot"].as<UINT>();
			MT.ItemNum = MechTreasureInfoNode[i]["itemnum"].as<UINT>();
			MT.exchangeNeedNum = MechTreasureInfoNode[i]["exchangeNeedNum"].as<UINT>();
			MT.exchangePieceNum = MechTreasureInfoNode[i]["exchangePieceNum"].as<UINT>();
			MT.item_type = MechTreasureInfoNode[i]["item_type"].as<UINT>();
			MT.award_idx = MechTreasureInfoNode[i]["award_idx"].as<UINT>();
			MT.coupons = MechTreasureInfoNode[i]["coupons"].as<UINT>();
			MT.supermoney = MechTreasureInfoNode[i]["supermoney"].as<UINT>();
			MT.merge = MechTreasureInfoNode[i]["merge"].as<UINT>();
			MT.seed = MechTreasureInfoNode[i]["seed"].as<UINT>();

			mechTreasureInfo.push_back(MT);
		}

		printf("Load MechTreasureInfo Success!\n");
	}
	catch (const std::exception&)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load MechTreasureInfo Error\n");
		//exit(0);
	}
}

void LoadAppellationConfig()
{
	try
	{
		YAML::Node Config = YAML::LoadFile(".\\Config\\Appellations.yml");
		for (size_t i = 0; i < Config.size(); i++)
		{
			YAML::Node item = Config[i];
			std::shared_ptr<AppellationInfo> pAppellation = std::make_shared<AppellationInfo>();
			pAppellation->Type = item["Type"].as<UINT>();
			pAppellation->Level = item["Level"].as<UINT>();
			pAppellation->Difficulty = item["Difficulty"].as<UINT>();
			pAppellation->MapId = item["MapId"].as<UINT>();
			pAppellation->Value = item["Value"].as<UINT>();
			Appellations[pAppellation->Type] = pAppellation;
		}
		printf("Load AppellationConfig Success!\n");
	}
	catch (const std::exception&)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load AppellationConfig Error\n");
		//exit(0);
	}
}

void LoadRandomMapPool()
{
	try
	{
		RandomMapPools.clear();
		YAML::Node Config = YAML::LoadFile(".\\Config\\RandomMaps.yml");
		for (size_t i = 0; i < Config.size(); i++)
		{
			YAML::Node MapConfig = Config[i];
			int PoolID = MapConfig["ID"].as<int>();
			std::shared_ptr<RANDOMMAPPOOL> RandomMapPool = std::make_shared<RANDOMMAPPOOL>();
	
			YAML::Node PoolConfig = MapConfig["Maps"];
			int poolsize = PoolConfig.size();
			if (poolsize <= 0)
			{
				continue;
			}
			RandomMapPool->MapID.resize(poolsize);

			for (size_t j = 0; j < poolsize; j++)
			{
				RandomMapPool->MapID[j] = PoolConfig[j].as<int>();
			}
			RandomMapPools[PoolID] = RandomMapPool;
			PoolConfig.reset();
			MapConfig.reset();
		}
		printf("Load RandomMaps Success 全随0 速随1 人气随2 边随5 赛事8 排位16 经典13\n");
	}
	catch (const std::exception& e)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load RandomMaps Error\n");
	}
}

void LoadMapPool()
{
	
	int result;
	AllMap = std::make_shared<MAPPOOL>();
	AllMap->MapID.resize(500);
	AllMap->Size = 0;
	BorderMap = std::make_shared<MAPPOOL>();
	BorderMap->MapID.resize(500);
	BorderMap->Size = 0;
	EventMap = std::make_shared<MAPPOOL>();
	EventMap->MapID.resize(500);
	EventMap->Size = 0;
	RankMap = std::make_shared<MAPPOOL>();
	RankMap->MapID.resize(500);
	RankMap->Size = 0;
	USHORT AllMapID = 0, BorderMapID = 0, EventMapID = 0, RankMapID = 0;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT * FROM MapRand;"));
		prep_stmt->clearParameters();
		// 执行查询
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// 处理结果
		while (res->next()) {
			AllMapID = res->getUInt(1);
			if (AllMapID != 0)
			{
				AllMap->MapID[AllMap->Size] = AllMapID;
				AllMap->Size++;
			}
			BorderMapID = res->getUInt(2);
			if (BorderMapID != 0)
			{
				BorderMap->MapID[BorderMap->Size] = BorderMapID;
				BorderMap->Size++;
			}
			EventMapID = res->getUInt(3);
			if (EventMapID != 0)
			{
				EventMap->MapID[EventMap->Size] = EventMapID;
				EventMap->Size++;
			}
			RankMapID = res->getUInt(4);
			if (RankMapID != 0)
			{
				RankMap->MapID[RankMap->Size] = RankMapID;
				RankMap->Size++;
			}
		}

		res.reset();

	}
	catch (const std::exception& e)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf(e.what());
		printf("LoadMapPool error\n");
	}
}

static UINT LC_YUIntAny(const YAML::Node& node, std::initializer_list<const char*> keys, UINT def)
{
	if (!node) return def;
	for (const char* key : keys)
	{
		try
		{
			if (!node[key]) continue;
			UINT uintValue = 0;
			if (LC_YamlTryUIntNoThrow(node[key], uintValue)) return uintValue;
			bool boolValue = false;
			if (LC_YamlTryBoolNoThrow(node[key], boolValue)) return boolValue ? 1U : 0U;
		}
		catch (const std::exception&) {}
	}
	return def;
}

static INT LC_YIntAny(const YAML::Node& node, std::initializer_list<const char*> keys, INT def)
{
	if (!node) return def;
	for (const char* key : keys)
	{
		try
		{
			if (!node[key]) continue;
			int intValue = 0;
			if (LC_YamlTryIntNoThrow(node[key], intValue)) return intValue;
			bool boolValue = false;
			if (LC_YamlTryBoolNoThrow(node[key], boolValue)) return boolValue ? 1 : 0;
		}
		catch (const std::exception&) {}
	}
	return def;
}

static YAML::Node LC_LoadCarTreasureRoot()
{
	const char* paths[] = {
		".\\24251\\CarTreasure.yml",
		".\\Config\\CarTreasure.yml"
	};
	for (const char* path : paths)
	{
		try
		{
			YAML::Node cfg = YAML::LoadFile(path);
			YAML::Node root = cfg["car_treasure"];
			if (root) return root;
		}
		catch (const std::exception&) {}
	}
	return YAML::Node();
}

void LoadCarTreasure()
{
	try
	{
		CarTreasures = std::make_shared<CarTreasure>();

		YAML::Node RootCfg = LC_LoadCarTreasureRoot();
		YAML::Node ItemsCfg;
		YAML::Node Defaults;
		YAML::Node LegacyConfig;

		if (RootCfg)
		{
			YAML::Node AwardPool = RootCfg["award_pool"];
			if (AwardPool && AwardPool.IsMap())
			{
				Defaults = AwardPool["defaults"];
				ItemsCfg = AwardPool["items"];
			}
			else
			{
				ItemsCfg = AwardPool;
			}
		}

		if (!ItemsCfg)
		{
			LegacyConfig = YAML::LoadFile(".\\Config\\CarTreasure.yml");
			ItemsCfg = LegacyConfig["Items"];
		}

		if (ItemsCfg && ItemsCfg.size() > 0) {
			UINT AutoSelectGroup = 1;
			std::map<std::pair<UINT, UINT>, UINT> AutoSelectGroupByItem;
			for (size_t i = 0; i < ItemsCfg.size(); i++)
			{
				YAML::Node Cfg = ItemsCfg[i];
				CarTreasureInfo Treasure;
				Treasure.ItemId = LC_YUIntAny(Cfg, { "item_id", "ItemId" }, 0);
				if (Treasure.ItemId == 0) continue;
				Treasure.ItemNum = LC_YUIntAny(Cfg, { "item_num", "ItemNum" }, LC_YUIntAny(Defaults, { "item_num" }, 1));
				Treasure.AvailPeriod = LC_YIntAny(Cfg, { "period", "avail_period", "AvailPeriod" }, LC_YIntAny(Defaults, { "period", "avail_period" }, 0));
				Treasure.SelectCarType = LC_YUIntAny(Cfg, { "select_type", "select_car_type", "SelectCarType" }, LC_YUIntAny(Defaults, { "select_type", "select_car_type" }, 0));

				UINT DetectedItemType = GetItemType(Treasure.ItemId);
				UINT AutoItemType = (DetectedItemType != EAIT_Unknown) ? DetectedItemType : ((Treasure.SelectCarType > 0) ? EAIT_CAR : EAIT_Unknown);
				UINT AutoOtherAttribute = (AutoItemType == EAIT_CAR) ? 1 : 0;
				Treasure.ItemType = LC_YUIntAny(Cfg, { "item_type", "ItemType" }, LC_YUIntAny(Defaults, { "item_type" }, AutoItemType));
				Treasure.ExchangePieceNum = LC_YUIntAny(Cfg, { "piece", "exchange_piece_num", "ExchangePieceNum" }, LC_YUIntAny(Defaults, { "piece", "exchange_piece_num" }, 0));
				Treasure.CanExchange = LC_YUIntAny(Cfg, { "can_exchange", "CanExchange" }, LC_YUIntAny(Defaults, { "can_exchange" }, 0));
				Treasure.iProbability = LC_YUIntAny(Cfg, { "probability", "weight", "Probability" }, LC_YUIntAny(Defaults, { "probability", "weight" }, 1));
				Treasure.OtherAttribute = LC_YUIntAny(Cfg, { "other_attribute", "OtherAttribute" }, LC_YUIntAny(Defaults, { "other_attribute" }, AutoOtherAttribute));

				UINT DefaultGroupIndex = LC_YUIntAny(Defaults, { "group", "group_index" }, 0);
				Treasure.GroupIndex = LC_YUIntAny(Cfg, { "group", "group_index", "GroupIndex" }, DefaultGroupIndex);
				if (Treasure.SelectCarType > 0) {
					std::pair<UINT, UINT> groupKey(Treasure.SelectCarType, Treasure.ItemId);
					if (Treasure.GroupIndex == 0) {
						auto it = AutoSelectGroupByItem.find(groupKey);
						if (it != AutoSelectGroupByItem.end()) {
							Treasure.GroupIndex = it->second;
						}
						else {
							Treasure.GroupIndex = AutoSelectGroup++;
							AutoSelectGroupByItem[groupKey] = Treasure.GroupIndex;
						}
					}
					else {
						AutoSelectGroupByItem[groupKey] = Treasure.GroupIndex;
						if (Treasure.GroupIndex >= AutoSelectGroup) AutoSelectGroup = Treasure.GroupIndex + 1;
					}
				}

				if (Treasure.iProbability > CarTreasures->MaxRate) {
					CarTreasures->MaxRate = Treasure.iProbability;
				}
				CarTreasures->TreasureMap[Treasure.iProbability].push_back(Treasure);
				CarTreasures->Treasures.push_back(Treasure);
				if (Treasure.ItemType == 1) {
					CarTreasures->JPTreasures.push_back(Treasure);
				}
				else {
					if (Treasure.iProbability > CarTreasures->MaxRate2) {
						CarTreasures->MaxRate2 = Treasure.iProbability;
					}
					CarTreasures->PtTreasureMap[Treasure.iProbability].push_back(Treasure);
				}
				Cfg.reset();
			}
		}
		printf("Load CarTreasure Success!\n");
	}
	catch (const std::exception&)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load CarTreasure Error!\n");
	}
}

void DumpTreasureServerLimit(UINT boxId)
{
	int n = 0;
	auto dumpOne = [&](long id, TREASUREBOX* box) {
		if (!box) return;
		for (const auto& pool : box->TreasureAwards)
		{
			for (const auto& item : pool.AwardItems)
			{
				if (item.ServerLimit > 0)
				{
					printf("[TreasureLimit] mem box=%ld item=%u period=%d limit=%d\n",
						id, item.ItemID, item.AvailPeriod, item.ServerLimit);
					n++;
				}
			}
		}
	};
	if (boxId != 0)
	{
		auto it = TreasureBoxs.find(static_cast<long>(boxId));
		if (it == TreasureBoxs.end() || !it->second)
		{
			printf("[TreasureLimit] box %u not loaded\n", boxId);
			return;
		}
		dumpOne(static_cast<long>(boxId), it->second.get());
		if (n == 0) printf("[TreasureLimit] box %u has no ServerLimit in memory\n", boxId);
		return;
	}
	for (const auto& kv : TreasureBoxs)
		dumpOne(kv.first, kv.second.get());
	if (n == 0) printf("[TreasureLimit] no ServerLimit in memory\n");
}

int LoadTreasureBox()
{
	//目标文件夹路径
	std::string inPath = ".\\Treasure\\*.yml";//遍历文件夹下的所有.yml文件 加载宝箱
	TreasureBoxs.clear();
	//用于查找的句柄
	intptr_t handle;
	struct _finddata_t fileinfo;
	//第一次查找
	handle = _findfirst(inPath.c_str(), &fileinfo);
	if (handle == -1)
		return -1;
	YAML::Node config;
	YAML::Node PoolConfig;
	YAML::Node SeedCfg;
	YAML::Node TreasureCfg;
	YAML::Node ItemCfg;
	UINT ConfigSize = 0;
	UINT ItemNum = 0;
	try {
		do
		{
			try
			{
			std::string outPath(".\\Treasure\\");
			outPath.append(fileinfo.name);

			config = YAML::LoadFile(outPath);
			//printf("TreasureID:%d\n", atoi(fileinfo.name));
			TREASUREBOX* TreasureBox = new TREASUREBOX;
			TreasureBoxs[atoi(fileinfo.name)] = std::unique_ptr<TREASUREBOX>(TreasureBox);
			TreasureBox->LuckyValue = config["LuckyValue"].as<int>();
			YAML::Node PreviewCfg = config["Preview"] ? config["Preview"] : config["preview"];
			if (PreviewCfg && PreviewCfg.IsMap())
			{
				if (PreviewCfg["ExtraItemID"]) TreasureBox->ExtraItemID = PreviewCfg["ExtraItemID"].as<UINT>();
				if (PreviewCfg["ExtraItemNum"]) TreasureBox->ExtraItemNum = PreviewCfg["ExtraItemNum"].as<UINT>();
				if (PreviewCfg["ExtraItemPeriod"]) TreasureBox->ExtraItemPeriod = PreviewCfg["ExtraItemPeriod"].as<int>();
				if (PreviewCfg["ExtraItemType"]) TreasureBox->ExtraItemType = PreviewCfg["ExtraItemType"].as<USHORT>();
			}
			PoolConfig = config["Pool"];
			ConfigSize = PoolConfig.size();
			TreasureBox->TreasureAwards.resize(ConfigSize);
			for (int i = 0; i < ConfigSize; i++)
			{
				SeedCfg = PoolConfig[i];
				TreasureBox->TreasureAwards[i].Seed = SeedCfg["Seed"].as<int>();
				TreasureBox->TreasureAwards[i].ItemType = SeedCfg["ItemType"].as<int>();

				//printf("Seed:%d\n", TreasureBox->TreasureAwards[i].Seed);
				TreasureCfg = SeedCfg["Items"];

				ItemNum = TreasureCfg.size();
				
				TreasureBox->TreasureAwards[i].AwardItems.resize(ItemNum);
				for (int j = 0; j < ItemNum; j++)
				{
					ItemCfg = TreasureCfg[j];

					TreasureBox->TreasureAwards[i].AwardItems[j].ItemID = ItemCfg["ItemId"].as<int>();
					TreasureBox->TreasureAwards[i].AwardItems[j].ItemNum = ItemCfg["ItemNum"].as<int>();
					TreasureBox->TreasureAwards[i].AwardItems[j].AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
					int serverLimit = 0;
					if (ItemCfg["ServerLimit"].IsDefined() && !ItemCfg["ServerLimit"].IsNull())
						serverLimit = ItemCfg["ServerLimit"].as<int>(0);
					if (serverLimit < 0) serverLimit = 0;
					TreasureBox->TreasureAwards[i].AwardItems[j].ServerLimit = serverLimit;
					if (serverLimit > 0)
					{
						printf("[TreasureLimit] load box=%d item=%u period=%d limit=%d\n",
							atoi(fileinfo.name),
							TreasureBox->TreasureAwards[i].AwardItems[j].ItemID,
							TreasureBox->TreasureAwards[i].AwardItems[j].AvailPeriod,
							serverLimit);
					}
					ItemCfg.reset();
					//printf("ItemId:%d\n", TreasureBox->TreasureAwards[i].AwardItems[j].ItemID);
				}
				
				TreasureCfg.reset();
				SeedCfg.reset();
			}
			}
			catch (const std::exception& fileErr)
			{
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
				printf("[Treasure] load fail %s: %s\n", fileinfo.name, fileErr.what());
				SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED);
			}

		} while (!_findnext(handle, &fileinfo));
	}
	catch (const std::exception& e)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load TreasureBox Exception! %s\n", e.what());
	}

	_findclose(handle);
	printf("Load TreasureBox Success!\n");
	DumpTreasureServerLimit(0);


	GameEndBox.clear();
	GameEndBox2Uin.clear();
	try
	{
		YAML::Node Config = YAML::LoadFile(".\\Config\\Treasure.yml");
		GameEndBoxTime[0] = Config["on"].as<int>();
		GameEndBoxTime[1] = Config["multiple"].as<int>();
		GameEndBoxTime[2] = Config["start"].as<int>();
		GameEndBoxTime[3] = Config["end"].as<int>();
		YAML::Node ItemList = Config["Items"];
		for (size_t i = 0; i < ItemList.size(); i++)
		{
			ItemInfo IT;
			IT.ItemID = ItemList[i]["ItemId"].as<INT>();
			IT.ItemNum = ItemList[i]["ItemNum"].as<INT>();
			IT.AvailPeriod = ItemList[i]["AvailPeriod"].as<INT>();
			IT.ItemType = ItemList[i]["ItemType"].as<INT>();
			GameEndBox.push_back(IT);
		}


		YAML::Node UinList = Config["Uins"];
		for (size_t i = 0; i < UinList.size(); i++)
		{
			YAML::Node Node = UinList[i];
			std::shared_ptr<OpenBoxDef> Box = std::make_shared<OpenBoxDef>();
			Box->Uin = Node["Uin"].as<UINT>();
			Box->BoxID = Node["ItemId"].as<UINT>();
			Box->ItemNum = Node["ItemNum"].as<UINT>();
			Box->BoxType = Node["ItemType"].as<UINT>();
			GameEndBox2Uin[Node["Uin"].as<UINT>()] = Box;
		}
	}
	catch (const std::exception& e)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load GameEndBox Exception!\n");
	}
	printf("Load GameEndBox Success!\n");
	return 0;
}

void LoadMagicLampCfg()
{
	int allitem;
	try
	{
		YAML::Node Config = YAML::LoadFile(".\\Config\\MagicLampCfg.yml");
		allitem = Config.size();
		MagicLampItems.resize(allitem);
		for (int j = 0; j < allitem; j++)
		{
			YAML::Node ItemCfg = Config[j];//0开头
			MagicLampItems[j].ItemID = ItemCfg["ItemId"].as<int>();
			MagicLampItems[j].ItemNum = ItemCfg["ItemNum"].as<int>();
			MagicLampItems[j].AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
			MagicLampItems[j].ItemType = ItemCfg["ItemType"].as<int>();
			ItemCfg.reset();
		}
		printf("Load MagicLampCfg Success!\n");
	}
	catch (const std::exception& e)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load MagicLampCfg Error!\n");
	}
}

void LoadAuctionCfg()
{
	int allitem = 0;
	try
	{
		AuctionConfig = std::make_shared<AuctionCfg>();
		YAML::Node Config = YAML::LoadFile(".\\Config\\AuctionCfg.yml");
		AuctionConfig->Open = Config["Open"].as<int>();
		AuctionConfig->Content = Config["Content"].as<std::string>();

		YAML::Node AuctionCfgItems = Config["Items"];
		allitem = AuctionCfgItems.size();
		AuctionConfig->AuctionItemNum = allitem;
		AuctionConfig->AuctionItems.resize(allitem);

		for (int j = 0; j < allitem; j++)
		{
			YAML::Node ItemCfg = AuctionCfgItems[j];
			AuctionConfig->AuctionItems[j].ItemID = ItemCfg["ItemId"].as<int>();
			AuctionConfig->AuctionItems[j].ItemNum = ItemCfg["ItemNum"].as<int>();
			AuctionConfig->AuctionItems[j].AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
			AuctionConfig->AuctionItems[j].StartingPrice = ItemCfg["StartingPrice"].as<int>();
			AuctionConfig->AuctionItems[j].IncreasePrice = ItemCfg["IncreasePrice"].as<int>();
			AuctionConfig->AuctionItems[j].CommodityId = ItemCfg["CommodityId"].as<int>();
			AuctionConfig->AuctionItems[j].Time = ItemCfg["Time"].as<std::string>();
			ItemCfg.reset();
		}
		printf("Load AuctionCfg Success!\n");
	}
	catch (const std::exception&)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load AuctionCfg Error!\n");
	}
}

void LoadRegisterBox()
{
	size_t allitem;
	try
	{
		RegisterBoxs.clear();
		YAML::Node Config = YAML::LoadFile(".\\Config\\RegisterBox.yml");
		allitem = Config.size();
		for (int j = 0; j < allitem; j++)
		{
			YAML::Node ItemCfg = Config[j];//0开头
			std::shared_ptr<ItemInfo> it = std::make_shared<ItemInfo>(); 
			it->ItemID = ItemCfg["ItemId"].as<int>();
			it->ItemNum = ItemCfg["ItemNum"].as<int>();
			it->ObtainTime = ItemCfg["ObtainTime"].as<int>();
			it->AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
			RegisterBoxs[j] = it;
			ItemCfg.reset();
		}
		printf("Load RegisterBox Success!\n");
	}
	catch (const std::exception& e)
	{
		/*if (Items)
		{
			delete[] Items;
		}*/
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load RegisterBox Error!\n");
	}

	Items.clear();
	try
	{
		int result;
		DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ID,buyflag,Name,type,Gametype,Mess,lockid FROM ItemAllNew  where id > 0 "));
		prep_stmt->clearParameters();
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		while (res->next()) {
			UINT ID = res->getUInt("ID");
			UINT buyflag = res->getUInt("buyflag");
			UINT GameType = res->getUInt("Gametype");
			UINT lock = res->getUInt("lockid");

			std::string name = UTF2GB(res->getString("Name").c_str());
			std::string type = UTF2GB(res->getString("type").c_str());
			std::string Mess = UTF2GB(res->getString("Mess").c_str());
	
			Item* pItem = new Item;
			pItem->ID = ID;
			pItem->buyflag = buyflag;
			pItem->lock = lock;
			pItem->Type = EAIT_Unknown;
			pItem->Mess = removeSpaces(Mess);

			if (type == "EAIT_CAR")
			{
				pItem->Type = EAIT_CAR;
				if (Mess == "K0级轮滑" || Mess == "K1级轮滑" || Mess == "K2级轮滑") {
					pItem->GameType = 2;
				}
				else if (Mess == "L0级滑板" || Mess == "L1级滑板" || Mess == "L2级滑板" || Mess == "L3级滑板") {

					pItem->GameType = 1;
				}
				else
				{
					pItem->GameType = 0;
				}
			}
			else if (type == "EAIT_SKIN")
			{
				pItem->Type = EAIT_SKIN;
			}
			else if (type == "EAIT_CLOTHES")
			{
				pItem->Type = EAIT_CLOTHES;
			}
			else if (type == "EAIT_PROP")
			{
				pItem->Type = EAIT_PROP;
			}
			else if (type == "EAIT_PET")
			{
				pItem->Type = EAIT_PET;
			}
			else if (type == "EAIT_STONE")
			{
				pItem->Type = EAIT_STONE;
			}
			pItem->Name = name;
			Items[pItem->ID] = std::unique_ptr<Item>(pItem);
		}

		printf("Load ItemAll Success!\n");
	}
	catch (const std::exception& e)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load ItemAll Error!\n");
	}
}

void LoadDayRaffleBox()
{
	size_t allitem;
	try
	{
		DayRaffleBoxs.clear();
		YAML::Node Config = YAML::LoadFile(".\\Config\\DayRaffle.yml");
		allitem = Config.size();
		for (int j = 0; j < allitem; j++)
		{
			YAML::Node ItemCfg = Config[j];//0开头
			std::shared_ptr<ItemInfo> it = std::make_shared<ItemInfo>();
			it->ItemID = ItemCfg["ItemId"].as<int>();
			it->ItemNum = ItemCfg["ItemNum"].as<int>();
			it->ObtainTime = ItemCfg["ObtainTime"].as<int>();
			it->AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
			DayRaffleBoxs[j] = it;
			ItemCfg.reset();
		}
		printf("Load DayRaffle Success!\n");
	}
	catch (const std::exception& e)
	{
		/*if (Items)
		{
			delete[] Items;
		}*/
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load DayRaffle Error!\n");
	}


}

void LoadMagicFormationBoxs()
{
	size_t allitem;
	try
	{
		MagicFormationBoxs.clear();
		YAML::Node Config = YAML::LoadFile(".\\Config\\MagicFormation.yml");
		allitem = Config.size();
		for (int j = 0; j < allitem; j++)
		{
			YAML::Node ItemCfg = Config[j];//0开头
			std::shared_ptr<ItemInfo> it = std::make_shared<ItemInfo>();
			it->ItemID = ItemCfg["ItemId"].as<int>();
			it->ItemNum = ItemCfg["ItemNum"].as<int>();
			it->ObtainTime = ItemCfg["ObtainTime"].as<int>();
			it->AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
			MagicFormationBoxs[j] = it;
			ItemCfg.reset();
		}
		printf("Load MagicFormationBoxs Success!\n");
	}
	catch (const std::exception& e)
	{
		/*if (Items)
		{
			delete[] Items;
		}*/
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load MagicFormationBoxs Error!\n");
	}


}

void LoadMagicExchangeShopBoxs()
{
	size_t allitem;

	try
	{
		MagicExchangeShopCarBoxs.clear();
		MagicExchangeShopPetBoxs.clear();
		MagicExchangeShopSeatBoxs.clear();
		YAML::Node Config = YAML::LoadFile(".\\Config\\MagicExchangeShop.yml");

		YAML::Node Car = Config["EAIT_CAR"];
		allitem = Car.size();
		for (int j = 0; j < allitem; j++)
		{
			YAML::Node ItemCfg = Car[j];//0开头
			std::shared_ptr<ItemInfo> it = std::make_shared<ItemInfo>();
			it->ItemID = ItemCfg["ItemId"].as<int>();
			it->ItemNum = ItemCfg["ItemNum"].as<int>();
			it->AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
			//it->ComsumeItemId = ItemCfg["ComsumeItemId"].as<int>();
			it->ComsumeItemNum = ItemCfg["ComsumeItemNum"].as<int>();
			MagicExchangeShopCarBoxs[j] = it;
			ItemCfg.reset();
		}

		YAML::Node Pet = Config["EAIT_PET"];
		allitem = Pet.size();
		for (int j = 0; j < allitem; j++)
		{
			YAML::Node ItemCfg = Pet[j];//0开头
			std::shared_ptr<ItemInfo> it = std::make_shared<ItemInfo>();
			it->ItemID = ItemCfg["ItemId"].as<int>();
			it->ItemNum = ItemCfg["ItemNum"].as<int>();
			it->AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
			//it->ComsumeItemId = ItemCfg["ComsumeItemId"].as<int>();
			it->ComsumeItemNum = ItemCfg["ComsumeItemNum"].as<int>();
			MagicExchangeShopPetBoxs[j] = it;
			ItemCfg.reset();
		}

		YAML::Node Seat = Config["EAIT_SEAT"];
		allitem = Seat.size();
		for (int j = 0; j < allitem; j++)
		{
			YAML::Node ItemCfg = Seat[j];//0开头
			std::shared_ptr<ItemInfo> it = std::make_shared<ItemInfo>();
			it->ItemID = ItemCfg["ItemId"].as<int>();
			it->ItemNum = ItemCfg["ItemNum"].as<int>();
			it->AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
			//it->ComsumeItemId = ItemCfg["ComsumeItemId"].as<int>();
			it->ComsumeItemNum = ItemCfg["ComsumeItemNum"].as<int>();
			MagicExchangeShopSeatBoxs[j] = it;
			ItemCfg.reset();
		}
		printf("Load MagicExchangeShopBoxs Success!\n");
	}
	catch (const std::exception& e)
	{
		/*if (Items)
		{
			delete[] Items;
		}*/
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf("Load MagicExchangeShopBoxs Error!\n");
	}

}

void LoadMiracleCfg()
{
	int allitem;
	try
	{
		YAML::Node Config = YAML::LoadFile(".\\Config\\MiracleCfg.yml");
		YAML::Node MagicCupExchangeConfig = Config["MagicCupExchange"];
		MagicCupExchange[EAIT_CAR] = MagicCupExchangeConfig["EAIT_CAR"].as<int>();
		MagicCupExchange[EAIT_SKIN] = MagicCupExchangeConfig["EAIT_SKIN"].as<int>();
		MagicCupExchange[EAIT_CLOTHES] = MagicCupExchangeConfig["EAIT_CLOTHES"].as<int>();
		MagicCupExchange[EAIT_PROP] = MagicCupExchangeConfig["EAIT_PROP"].as<int>();
		MagicCupExchange[EAIT_PET] = MagicCupExchangeConfig["EAIT_PET"].as<int>();
		MagicCupExchange[EAIT_STONE] = MagicCupExchangeConfig["EAIT_STONE"].as<int>();
		printf("Load MagicCupExchangeConfig Success!\n");

		YAML::Node MiracleConfig = Config["MiracleCfg"];
		allitem = MiracleConfig.size();
		MiracleCfg = std::make_shared<MIRACLECFG>();
		MiracleCfg->MiracleItems.resize(allitem);
		for (int j = 0; j < allitem; j++)
		{
			YAML::Node ItemCfg = MiracleConfig[j];//0开头
			MiracleCfg->MiracleItems[j].ItemID = ItemCfg["ItemId"].as<int>();
			MiracleCfg->MiracleItems[j].ItemNum = ItemCfg["ItemNum"].as<int>();
			MiracleCfg->MiracleItems[j].AvailPeriod = ItemCfg["AvailPeriod"].as<int>();
			MiracleCfg->MiracleItems[j].ItemType = ItemCfg["ItemType"].as<int>();
			MiracleCfg->MiracleItems[j].ComsumeItemId = ItemCfg["ComsumeItemId"].as<int>();
			MiracleCfg->MiracleItems[j].ComsumeItemNum = ItemCfg["ComsumeItemNum"].as<int>();
			const int minBuyNum = ItemCfg["MinBuyNum"] ? ItemCfg["MinBuyNum"].as<int>() : 1;
			if (minBuyNum < 1)
			{
				throw std::runtime_error("MiracleCfg MinBuyNum must be greater than zero");
			}
			MiracleCfg->MiracleItems[j].MinBuyNum = static_cast<UINT>(minBuyNum);
			ItemCfg.reset();
		}
		printf("Load MiracleCfg Success!\n");
	}
	catch (const std::exception& e)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf(e.what());
		printf("Load MiracleCfg Error!\n");
	}
}

void LoadUserPermissionsConfig()
{
	ClothesGMUins.clear();
	CarGMUins.clear();
	NewClothesGMUins.clear();
	NewCarGMUins.clear();
	NewClothesCarGMUins.clear();
	AdminUins.clear(); // 清空管理员集合
	SpecialLoginUins.clear();
	ClothesGMLimits.clear();
	CarGMLimits.clear();
	NewClothesGMLimits.clear();
	NewCarGMLimits.clear();
	NewClothesCarGMLimits.clear();
	ClothesGMMaxItemID = 0;
	CarGMMaxItemID = 0;
	NewClothesGMMaxItemID = 0;
	NewCarGMMaxItemID = 0;
	NewClothesCarGMMaxItemID = 0;
	ClothesGMMess.clear();
	CarGMMess.clear();
	NewClothesGMMess.clear();
	NewCarGMMess.clear();
	NewClothesCarGMMess.clear();

	try
	{
		YAML::Node config = YAML::LoadFile(".\\Config\\AUserPermissions.yml");
		YAML::Node UserPermissionsList = config["UserPermissionsList"];
		if (UserPermissionsList)
		{
			LoadGmRoleNode(UserPermissionsList["ClothesGMUins"], ClothesGMMess, ClothesGMUins, ClothesGMMaxItemID, ClothesGMLimits);
			LoadGmRoleNode(UserPermissionsList["CarGMUins"], CarGMMess, CarGMUins, CarGMMaxItemID, CarGMLimits);
			// 兼容旧键 NewClothes / NewCar：若 yml 里还有，并进衣服组 / 赛车组。
			LoadGmRoleNode(UserPermissionsList["NewClothesGMUins"], ClothesGMMess, ClothesGMUins, ClothesGMMaxItemID, ClothesGMLimits);
			LoadGmRoleNode(UserPermissionsList["NewCarGMUins"], CarGMMess, CarGMUins, CarGMMaxItemID, CarGMLimits);
			YAML::Node clothesCarNode = UserPermissionsList["ClothesCarGMUins"];
			if (!clothesCarNode)
				clothesCarNode = UserPermissionsList["NewClothesCarGMUins"];
			LoadGmRoleNode(clothesCarNode, NewClothesCarGMMess, NewClothesCarGMUins, NewClothesCarGMMaxItemID, NewClothesCarGMLimits);
			// 加载管理员权限用户 ID
			YAML::Node AdminUinsNode = UserPermissionsList["AdminUins"];
			if (AdminUinsNode)
			{
				for (size_t i = 0; AdminUinsNode["Uins"] && i < AdminUinsNode["Uins"].size(); i++)
				{
					AdminUins.insert(AdminUinsNode["Uins"][i]["Uin"].as<UINT>());
				}
			}
			// 加载 SpecialLoginUins
			YAML::Node SpecialLoginUinsNode = UserPermissionsList["SpecialLoginUins"];
			if (SpecialLoginUinsNode)
			{
				for (size_t i = 0; SpecialLoginUinsNode["Uins"] && i < SpecialLoginUinsNode["Uins"].size(); i++)
				{
					SpecialLoginUins.insert(SpecialLoginUinsNode["Uins"][i]["Uin"].as<UINT>());
				}
			}
		}


		// UserPermDBG dump disabled: keep console quiet unless explicitly debugging 10001.

		printf("Load UserPermissionsConfig Success! Clothes=%llu Car=%llu NewClothes=%llu NewCar=%llu NewClothesCar=%llu\n",
			(unsigned long long)ClothesGMUins.size(),
			(unsigned long long)CarGMUins.size(),
			(unsigned long long)NewClothesGMUins.size(),
			(unsigned long long)NewCarGMUins.size(),
			(unsigned long long)NewClothesCarGMUins.size());
	}
	catch (const std::exception& e)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf(e.what());
		printf("Load UserPermissionsConfig Error!\n");
	}
}

void LoadHeadAndHeadShokConfig()
{

	gActiveHeadItemListCfg.clear();
	gActiveHeadShokItemListCfg.clear();
	gActiveBackPlateItemListCfg.clear();
	try
	{
		YAML::Node HeadConfig = YAML::LoadFile(".\\24251\\HeadCfg.yml");
		YAML::Node HeadItemList = HeadConfig["HeadItem"];
		for (int i = 0; i < HeadItemList.size(); i++)
		{
			ActiveHeadItemList item;
			item.ActivePeriod = HeadItemList[i]["ActivePeriod"].as<int>();
			item.ActiveHeadId = HeadItemList[i]["ActiveHeadId"].as<int>();
			item.ActiveItemId = HeadItemList[i]["ActiveItemId"].as<int>();
			item.Method = HeadItemList[i]["Method"].as<std::string>();
			item.Desc = HeadItemList[i]["Desc"].as<std::string>();
			item.Info = HeadItemList[i]["Info"].as<std::string>();
			gActiveHeadItemListCfg.push_back(item);
		}
		printf("Load HeadConfig Success!\n");

		YAML::Node HeadShokConfig = YAML::LoadFile(".\\24251\\HeadShokCfg.yml");
		YAML::Node HeadShokItemList = HeadShokConfig["HeadShokItem"];
		for (int i = 0; i < HeadShokItemList.size(); i++)
		{
			ActiveHeadItemList item;
			item.ActivePeriod = HeadShokItemList[i]["ActivePeriod"].as<int>();
			item.ActiveHeadId = HeadShokItemList[i]["ActiveHeadId"].as<int>();
			item.ActiveItemId = HeadShokItemList[i]["ActiveItemId"].as<int>();
			item.Method = HeadShokItemList[i]["Method"].as<std::string>();
			item.Desc = HeadShokItemList[i]["Desc"].as<std::string>();
			item.Info = HeadShokItemList[i]["Info"].as<std::string>();
			gActiveHeadShokItemListCfg.push_back(item);
		}
		printf("Load HeadShokConfig Success!\n");

		YAML::Node BackPlateConfig = YAML::LoadFile(".\\24251\\BackPlateCfg.yml");
		YAML::Node BackPlateItemList = BackPlateConfig["BackPlateItem"];
		for (int i = 0; i < BackPlateItemList.size(); i++)
		{
			ActiveHeadItemList item;
			item.ActiveHeadId = BackPlateItemList[i]["ActiveHeadId"].as<int>();
			item.Method = BackPlateItemList[i]["Method"].as<std::string>();
			item.Desc = BackPlateItemList[i]["Desc"].as<std::string>();
			item.Info = BackPlateItemList[i]["Info"].as<std::string>();
			gActiveBackPlateItemListCfg.push_back(item);
		}
		printf("Load BackPlateConfig Success!\n");
	}
	catch (const std::exception& e)
	{
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_RED);
		printf(e.what());
		printf("Load HeadAndHeadShokConfig Error!\n");
	}
}
