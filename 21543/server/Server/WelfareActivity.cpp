#include "Message.h"
#include "Utils.h"
#include "Database.h"
#include "Item.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// 福利中心（Beta99/Beta100/Beta102）使用官服抓包中的字段结构动态序列化。
// 配置数据与玩家状态分离：配置来自 2026-07-17 官服链路，进度、背包数量和领取状态来自本服。
namespace
{
constexpr int kWFClaimSign = 1;
constexpr int kWFClaimProcess = 2;

struct WFItem
{
	int ItemID = 0;
	int ItemNum = 0;
	int AvailPeriod = 0;
	int ItemType = 0;
};

struct WFRewardPart
{
	bool HasItem = false;
	WFItem Item;
	int SuperMoney = 0;
};

struct WFAward
{
	int Exp = 0;
	int SuperMoney = 0;
	int Coupons = 0;
	int SpeedCoin = 0;
	std::vector<WFItem> Items;
};

struct WFActiveTask
{
	int TaskID = 0;
	int OpenDate = 0;
	int CloseDate = 0;
	int FinishNums = 0;
	int ExecutePeriod = 0;
	int ShowTimeStamp = 0;
	int ShowDetail = -1;
	int ActivityAward = 0;
	int WireLayout = 0; // 0=普通任务，1=带详情任务，2=登录任务（官服省略玩家进度字段）
	WFAward Award;
};

struct WFProcessReward
{
	int NeedScore = 0;
	std::string Desc;
	std::vector<WFRewardPart> Parts;
};

struct WFBackground
{
	int ShowType = 1;
	std::string Resource;
	std::string CdnResource;
};

struct WFActiveConfig
{
	int EntryID = 0;
	int SeasonID = 0;
	std::int64_t BeginTime = 0;
	std::int64_t EndTime = 0;
	int ProcessItemID = 0;
	int ProcessType = 0;
	std::string DetailUrl;
	WFBackground Background;
	std::vector<WFActiveTask> Tasks;
	std::vector<WFProcessReward> ProcessRewards;
};

struct WFSignDay
{
	int Multiple = 1;
	std::vector<WFRewardPart> Parts;
};

struct WFSignConfig
{
	int EntryID = 0;
	int SeasonID = 0;
	std::int64_t BeginTime = 0;
	std::int64_t EndTime = 0;
	std::string DetailUrl;
	WFBackground Background;
	std::vector<WFSignDay> Days;
};

struct WFBenefitEntry
{
	int NewTag = 0;
	int Index = 0;
	std::string ActName;
	int ActID = 0;
	int UseTemplateType = 0;
	bool HasUseTemplateType = false;
	int WarmUpFuncID = 0;
	int Priority = 0;
	int EntryID = 0;
	std::string EntryUrl;
	int ActType = 1;
};

struct WFBenefitTab
{
	int Index = 0;
	std::string Background;
	std::string Name;
	int Priority = 0;
	int TabID = 0;
	std::vector<WFBenefitEntry> Entries;
};

struct WFHallJump
{
	int Tab2 = 0;
	int Tab1 = 0;
	std::string Url;
	int BuyCommodityID = 0;
	std::string ShopTab1;
	int Type = 0;
	int ShopTab2 = 0;
	int EntryID = 0;
};

struct WFHallEntry
{
	int Index = 0;
	WFHallJump Jump;
	int ActID = 0;
	std::string EntryDesc;
	std::string Desc;
	std::string Background;
	int Priority = 0;
	std::string EntryPic;
};

struct WFWarmUpConfig
{
	int EntryID = 0;
	int FuncID = 0;
	std::string JumpEntryLink;
	std::string BackgroundResource;
	int JumpEntryType = 0;
	std::int64_t EndTime = 0;
	std::int64_t BeginTime = 0;
	std::string BackgroundResourceCdn;
};

struct WFRedDemonTask
{
	int SlotID = 0;
	int TaskID = 0;
	int OpenDate = 0;
	int CloseDate = 0;
	int FinishNums = 0;
	std::vector<WFItem> Awards;
	std::string AwardDesc;
};

struct WFRedDemonStage
{
	int StageID = 0;
	std::int64_t UnlockTime = 0;
	std::vector<WFRedDemonTask> Tasks;
};

struct WFRedDemonConfig
{
	int EntryID = 0;
	int SeasonID = 0;
	std::int64_t BeginTime = 0;
	std::int64_t EndTime = 0;
	std::string DetailUrl;
	int PreviewItemID = 0;
	bool Permanent = false;
	std::string TipText;
	std::int64_t WelfareUnlockTime = 0;
	WFItem WelfareAward;
	std::string WelfareLockTip;
	std::vector<WFRedDemonStage> Stages;
	int OpType = 0;
};

struct WFAnnualTask
{
	int TaskID = 0;
	int AwardMoveNums = 0;
	int CloseDate = 0;
	int FinishNums = 0;
	int OpenDate = 0;
	int ExecutePeriod = 0;
};

struct WFAnnualBox
{
	int ItemNum = 0;
	std::string PictureUrl;
	int Probability = 0;
	int BoxType = 0;
	std::string Description;
};

struct WFAnnualConfig
{
	int ExchangePieceID = 0;
	std::vector<WFAnnualTask> Tasks;
	std::vector<WFAnnualBox> Boxes;
};

struct WFStrengthenConfig
{
	int DegreeNum = 0;
	int DayDegreeBagNum = 0;
	std::vector<WFItem> Awards;
};

struct WFSpeedCoinBubbleConfig
{
	std::string LimitSpeedCoinEndStr;
	std::string DateEndStr;
	std::string JumpUrl;
	std::int64_t LimitSpeedCoinEndTime = 0;
	std::string RemindText;
	std::int64_t BeginTime = 0;
	std::int64_t EndTime = 0;
	std::string DateBeginStr;
};

static size_t WF_FindPayload(const BYTE* msg, size_t msgLen)
{
	size_t pos = 0;
	while (pos < msgLen && msg[pos] != 0) ++pos;
	while (pos < msgLen && msg[pos] == 0) ++pos;
	return pos;
}

static double WF_ReadDouble(const BYTE* p)
{
	double value = 0;
	std::memcpy(&value, p, sizeof(value));
	return value;
}

static double WF_ReadContext(BYTE* msg, size_t msgLen)
{
	const size_t pos = WF_FindPayload(msg, msgLen);
	return (pos + 9 <= msgLen && msg[pos] == 0xF6) ? WF_ReadDouble(msg + pos + 1) : 0;
}

static std::vector<std::int64_t> WF_ParseNumbers(BYTE* msg, size_t msgLen)
{
	std::vector<std::int64_t> out;
	for (size_t pos = WF_FindPayload(msg, msgLen); pos + 9 <= msgLen;)
	{
		if (msg[pos] == 0xF6)
		{
			out.push_back(static_cast<std::int64_t>(std::llround(WF_ReadDouble(msg + pos + 1))));
			pos += 9;
		}
		else
		{
			++pos;
		}
	}
	return out;
}

static std::string WF_Utf8ToGbk(const std::string& text)
{
	if (text.empty()) return text;
	const int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
	if (wideLen <= 0) return text;
	std::wstring wide(static_cast<size_t>(wideLen), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &wide[0], wideLen);
	const int gbkLen = WideCharToMultiByte(936, 0, wide.data(), wideLen, nullptr, 0, nullptr, nullptr);
	if (gbkLen <= 0) return text;
	std::string out(static_cast<size_t>(gbkLen), '\0');
	WideCharToMultiByte(936, 0, wide.data(), wideLen, &out[0], gbkLen, nullptr, nullptr);
	return out;
}

static thread_local std::unordered_map<std::string, BYTE> g_WFKeyDict;

static void WF_ResetDictionary()
{
	g_WFKeyDict.clear();
}

static void WF_WriteTextToken(BYTE*& p, const std::string& text)
{
	auto it = g_WFKeyDict.find(text);
	if (it != g_WFKeyDict.end())
	{
		WriteKeyIndex(p, it->second);
		return;
	}
	if (g_WFKeyDict.size() < 0xFF)
	{
		g_WFKeyDict.emplace(text, static_cast<BYTE>(g_WFKeyDict.size()));
	}
	WriteText(p, text.c_str(), static_cast<DWORD>(text.size()));
}

static void WF_WriteKey(BYTE*& p, const char* key)
{
	auto it = g_WFKeyDict.find(key);
	if (it != g_WFKeyDict.end())
	{
		WriteKeyIndex(p, it->second);
		return;
	}
	if (g_WFKeyDict.size() < 0xFF)
	{
		g_WFKeyDict.emplace(key, static_cast<BYTE>(g_WFKeyDict.size()));
	}
	WriteText(p, key, static_cast<DWORD>(std::strlen(key)));
}

static void WF_WriteNumber(BYTE*& p, std::int64_t value)
{
	double wireValue = static_cast<double>(value);
	WriteValue(p, wireValue);
}

static void WF_WriteNumber(BYTE*& p, const char* key, std::int64_t value)
{
	WF_WriteKey(p, key);
	WF_WriteNumber(p, value);
}

static void WF_WriteTextValue(BYTE*& p, const char* key, const std::string& utf8)
{
	WF_WriteKey(p, key);
	const std::string wire = WF_Utf8ToGbk(utf8);
	WF_WriteTextToken(p, wire);
}

static BYTE* WF_BeginTable(BYTE*& p)
{
	Write8(p, 0xFD);
	BYTE* lenPos = p;
	Write32(p, 0);
	return lenPos;
}

static void WF_EndTable(BYTE* lenPos, BYTE* begin, BYTE* end)
{
	Set32(lenPos, static_cast<DWORD>(end - begin));
}

template <typename Writer>
static void WF_WriteObject(BYTE*& p, const char* key, Writer writer)
{
	WF_WriteKey(p, key);
	BYTE* lenPos = WF_BeginTable(p);
	BYTE* begin = p;
	writer(p);
	WF_EndTable(lenPos, begin, p);
}

template <typename Writer>
static void WF_WriteIndexed(BYTE*& p, int index, Writer writer)
{
	WF_WriteNumber(p, index);
	BYTE* lenPos = WF_BeginTable(p);
	BYTE* begin = p;
	writer(p);
	WF_EndTable(lenPos, begin, p);
}

static void WF_WriteEmptyTable(BYTE*& p, const char* key)
{
	WF_WriteKey(p, key);
	BYTE* lenPos = WF_BeginTable(p);
	BYTE* begin = p;
	WF_EndTable(lenPos, begin, p);
}

template <typename Writer>
static void WF_Send(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen, int resultCode, bool hasData, Writer writer)
{
	if (!client) return;
	std::vector<BYTE> msg(131072);
	BYTE* p = msg.data();
	std::memset(p, 0, 20);
	std::memcpy(p, "DispatchCoResponse", 18);
	p += 20;
	const double context = WF_ReadContext(body, bodyLen);
	WriteValue(p, context);
	WriteValue(p, resultCode);
	if (hasData)
	{
		WF_ResetDictionary();
		BYTE* lenPos = WF_BeginTable(p);
		BYTE* begin = p;
		writer(p);
		WF_EndTable(lenPos, begin, p);
	}
	msg.resize(static_cast<size_t>(p - msg.data()));
	MyResponseQuickRPC(client, msg.data(), msg.size());
}

static void WF_SendNumberResponse(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen,
	int resultCode, std::int64_t value)
{
	if (!client) return;
	std::vector<BYTE> msg(1024);
	BYTE* p = msg.data();
	std::memset(p, 0, 20);
	std::memcpy(p, "DispatchCoResponse", 18);
	p += 20;
	const double context = WF_ReadContext(body, bodyLen);
	WriteValue(p, context);
	WriteValue(p, resultCode);
	WF_WriteNumber(p, value);
	msg.resize(static_cast<size_t>(p - msg.data()));
	MyResponseQuickRPC(client, msg.data(), msg.size());
}

static void WF_SendError(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen, const char* method, const char* reason)
{
	QQS_DebugPrintf10001("[Welfare] reject method=%s Uin=%u reason=%s\n", method, client ? client->Uin : 0, reason);
	WF_Send(client, body, bodyLen, 1, false, [](BYTE*&) {});
}

static WFItem WF_I(int id, int num, int period = 0, int type = 0)
{
	return WFItem{ id, num, period, type };
}

static WFRewardPart WF_PI(int id, int num, int period = 0, int type = 0)
{
	WFRewardPart part;
	part.HasItem = true;
	part.Item = WF_I(id, num, period, type);
	return part;
}

static WFRewardPart WF_PM(int superMoney)
{
	WFRewardPart part;
	part.SuperMoney = superMoney;
	return part;
}

static WFActiveTask WF_Task(int id, int openDate, int closeDate, int finish, int period, int showTime,
	std::vector<WFItem> items, int layout = 0, int showDetail = -1, int activityAward = 0)
{
	WFActiveTask task;
	task.TaskID = id;
	task.OpenDate = openDate;
	task.CloseDate = closeDate;
	task.FinishNums = finish;
	task.ExecutePeriod = period;
	task.ShowTimeStamp = showTime;
	task.WireLayout = layout;
	task.ShowDetail = showDetail;
	task.ActivityAward = activityAward;
	task.Award.Items = std::move(items);
	return task;
}

static int WF_LocalDate(std::time_t value)
{
	std::tm local{};
	localtime_s(&local, &value);
	return (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
}

static bool WF_ActivityOpen(std::int64_t begin, std::int64_t end)
{
	const std::int64_t now = static_cast<std::int64_t>(time(nullptr));
	return now >= begin && now <= end;
}

static int WF_ItemCount(std::shared_ptr<ClientNode> client, int itemID)
{
	return (client && itemID > 0) ? PlayerDB_GetItemNum(client->Uin, itemID) : 0;
}

static void WF_WriteItem(BYTE*& p, const WFItem& item, bool itemIdBeforeNum = false)
{
	WF_WriteNumber(p, "m_iAvailPeriod", item.AvailPeriod);
	WF_WriteNumber(p, "m_ushItemType", item.ItemType);
	if (itemIdBeforeNum)
	{
		WF_WriteNumber(p, "m_iItemID", item.ItemID);
		WF_WriteNumber(p, "m_iItemNum", item.ItemNum);
	}
	else
	{
		WF_WriteNumber(p, "m_iItemNum", item.ItemNum);
		WF_WriteNumber(p, "m_iItemID", item.ItemID);
	}
}

static void WF_WriteItemTable(BYTE*& p, const char* key, const std::vector<WFItem>& items, bool itemIdBeforeNum = false)
{
	WF_WriteObject(p, key, [&](BYTE*& table) {
		for (size_t i = 0; i < items.size(); ++i)
		{
			WF_WriteIndexed(table, static_cast<int>(i + 1), [&](BYTE*& row) { WF_WriteItem(row, items[i], itemIdBeforeNum); });
		}
	});
}

static void WF_WriteAward(BYTE*& p, const WFAward& award, bool includeItemCount = false, bool itemIdBeforeNum = false)
{
	WF_WriteNumber(p, "m_iExp", award.Exp);
	WF_WriteNumber(p, "m_iSuperMoney", award.SuperMoney);
	if (includeItemCount) WF_WriteNumber(p, "m_sItemNum", static_cast<int>(award.Items.size()));
	WF_WriteNumber(p, "m_iCoupons", award.Coupons);
	WF_WriteNumber(p, "m_iSpeedCoin", award.SpeedCoin);
	WF_WriteItemTable(p, "m_astItemInfo", award.Items, itemIdBeforeNum);
}

static void WF_GrantAward(std::shared_ptr<ClientNode> client, const WFAward& award)
{
	if (!client) return;
	if (award.Exp > 0 || award.SuperMoney > 0 || award.Coupons > 0 || award.SpeedCoin > 0)
	{
		PlayerDB_AddBaseInfoRewardCurrency(client->Uin, static_cast<UINT>((std::max)(0, award.Exp)), 0,
			static_cast<UINT>((std::max)(0, award.SuperMoney)), static_cast<UINT>((std::max)(0, award.Coupons)),
			static_cast<UINT>((std::max)(0, award.SpeedCoin)));
	}
	std::vector<ItemInfo> notify;
	for (const WFItem& source : award.Items)
	{
		if (source.ItemID <= 0 || source.ItemNum <= 0) continue;
		ItemInfo item;
		item.ItemID = static_cast<UINT>(source.ItemID);
		item.ItemNum = PlayerDB_AddItem2(client, client->Uin, static_cast<DWORD>(source.ItemID),
			static_cast<DWORD>(source.ItemNum), source.AvailPeriod, false);
		item.AvailPeriod = source.AvailPeriod;
		item.ItemType = source.ItemType;
		notify.push_back(item);
	}
	if (!notify.empty()) NotifyClientAddItem(client, static_cast<UINT>(notify.size()), notify);
}

static WFAward WF_AwardFromParts(const std::vector<WFRewardPart>& parts, int multiple = 1)
{
	WFAward award;
	multiple = (std::max)(1, multiple);
	for (const WFRewardPart& part : parts)
	{
		if (part.HasItem)
		{
			WFItem item = part.Item;
			item.ItemNum *= multiple;
			award.Items.push_back(item);
		}
		award.SuperMoney += part.SuperMoney * multiple;
	}
	return award;
}
}

namespace
{
static const std::vector<WFHallEntry>& WF_HallEntries()
{
	static const std::vector<WFHallEntry> entries = {
		{ 1, { 1, 6, "", 0, "", 2, 0, 478 }, 1, "送6666限时飞车币！", "",
			"Res/uires/LobbyUI/Config/RecommandMenu/Res/NewBigButton/fuli2_2.png", 999,
			"Res/uires/LobbyUI/Config/RecommandMenu/Res/NewBigButton/fuli.gsa" },
		{ 2, { 0, 0, "", 0, "", 1, 0, 442 }, 2, "", "参与100%必得永久A车！",
			"Res/uires/LobbyUI/Config/RecommandMenu/Res/NewBigButton/tianmi_2.png", 998,
			"Res/uires/LobbyUI/Config/RecommandMenu/Res/NewBigButton/tongxingzheng.gsa" },
		{ 3, { 0, 2, "", 0, "", 2, 0, 465 }, 3, "联动款四驱车首发", "奥迪双钻四驱车联动中",
			"Res/uires/LobbyUI/Config/RecommandMenu/Res/NewBigButton/Entrance27_2.png", 101,
			"Res/uires/LobbyUI/Config/RecommandMenu/Res/NewBigButton/remai.gsa" },
		{ 4, { 0, 118, "", 0, "", 7, 0, 465 }, 13, "夏日福利特惠", "至尊夺宝 狂欢夺宝热卖中",
			"Res/uires/LobbyUI/Config/RecommandMenu/Res/NewBigButton/Entrance1_2.png", 11,
			"Res/uires/LobbyUI/Config/RecommandMenu/Res/NewBigButton/tehui.gsa" }
	};
	return entries;
}

static const std::vector<WFBenefitTab>& WF_BenefitTabs()
{
	static const std::vector<WFBenefitTab> tabs = {
		{ 5, "Xiantiao.png", "线条小狗", 50, 6, {
			{ 0, 1, "送限时飞车币", 1, 0, false, 0, 10, 474, "", 1 },
			{ 0, 2, "限时飞车币商城", 2, 0, false, 0, 10, 421, "", 1 },
			{ 1, 3, "夏日之旅", 3, 2, true, 0, 10, 540, "", 1 },
			{ 1, 5, "清凉一夏", 5, 3, true, 0, 10, 542, "", 1 },
			{ 0, 6, "登录送好礼", 6, 2, true, 0, 10, 483, "", 1 }
		} },
		{ 3, "WonderfulActivities.png", "精彩活动", 10, 3, {
			{ 0, 27, "活动日历", 48, 4, true, 2, 29, 497, "", 1 },
			{ 0, 1, "赛季寻秘·UGC", 2, 2, true, 0, 28, 484, "", 1 },
			{ 1, 15, "红魔鬼来袭", 31, 0, true, 0, 25, 570, "", 1 },
			{ 0, 21, "起源系列-四驱", 42, 2, true, 0, 22, 582, "", 1 },
			{ 1, 23, "签到送永久", 44, 3, true, 0, 19, 583, "", 1 },
			{ 0, 17, "绿茵赛场", 38, 2, true, 0, 13, 508, "", 1 },
			{ 0, 7, "每周签到", 16, 3, true, 0, 6, 487, "", 1 },
			{ 0, 28, "盛夏回归福利", 49, 0, false, 0, 5, 0, "https://speed.qq.com/cp/a20260610sxhgfl/nq.html", 2 },
			{ 0, 2, "天天跑图", 4, 2, true, 0, 4, 476, "", 1 },
			{ 0, 4, "强化福袋", 8, 0, false, 0, 3, 498, "", 1 },
			{ 0, 12, "映色贺新月", 26, 0, false, 0, 2, 0, "https://speed.qq.com/cp/a20260317xinyue/index.html", 2 },
			{ 0, 13, "关注我们", 27, 4, true, 1, 1, 497, "", 1 }
		} }
	};
	return tabs;
}

static const std::vector<WFActiveConfig>& WF_ActiveConfigs()
{
	static const std::vector<WFActiveConfig> configs = [] {
		std::vector<WFActiveConfig> out;

		WFActiveConfig summer;
		summer.EntryID = 540;
		summer.SeasonID = 1;
		summer.BeginTime = 1783699200LL;
		summer.EndTime = 1787759999LL;
		summer.ProcessItemID = 174679;
		summer.ProcessType = 2;
		summer.DetailUrl = "https://speed.qq.com/webplat/info/news_version3/147/14551/14572/14578/m22624/202606/988208.shtml";
		summer.Background = { 1, "Res/uires/ActivityTaskTemplate/Res/AC_1001/Img/ActiveSummerjourney.png", "" };
		summer.Tasks = {
			WF_Task(5754, 20260711, 20260826, 1, 2, 1783699200, { WF_I(174679, 1) }),
			WF_Task(5755, 20260711, 20260826, 3, 2, 1783699200, { WF_I(174679, 1) }),
			WF_Task(5756, 20260711, 20260826, 10, 2, 1783699200, { WF_I(174679, 1) }),
			WF_Task(5757, 20260711, 20260826, 3, 2, 1783699200, { WF_I(174679, 1) }),
			WF_Task(5758, 20260711, 20260826, 500, 2, 1783699200, { WF_I(174679, 1) })
		};
		summer.ProcessRewards = {
			{ 1, "蛋糕小狗头像框（1个）", { WF_PI(174699, 1) } },
			{ 3, "初级改装宝箱（50个）", { WF_PI(138147, 50) } },
			{ 5, "马尔济斯角色套装（永久）", { WF_PI(173741, 1, -1) } },
			{ 8, "小金毛角色套装（永久）", { WF_PI(173781, 1, -1) } },
			{ 10, "线条小狗（永久）", { WF_PI(174771, 1, -1) } }
		};
		out.push_back(std::move(summer));

		WFActiveConfig login;
		login.EntryID = 483;
		login.SeasonID = 1;
		login.BeginTime = 1782921600LL;
		login.EndTime = 1785081599LL;
		login.DetailUrl = "https://speed.qq.com/home/texing/99denglusonghaoli.html";
		login.Background = { 1, "Res/uires/ActivityTaskTemplate/Res/AC_1001/Img/ActiveHourlyTask4.png", "" };
		const std::vector<WFItem> commonLogin = { WF_I(160464, 5), WF_I(100879, 1), WF_I(160275, 2) };
		login.Tasks = {
			WF_Task(5644, 20260704, 20260704, 1, 1, 1782921600, commonLogin, 2),
			WF_Task(5645, 20260705, 20260705, 1, 1, 1782921600, commonLogin, 2),
			WF_Task(5646, 20260711, 20260711, 1, 1, 1782921600, { WF_I(173692, 1, -1), WF_I(173700, 1, -1), WF_I(174384, 1, -1), WF_I(160275, 5) }, 2),
			WF_Task(5647, 20260712, 20260712, 1, 1, 1782921600, { WF_I(174349, 1, -1), WF_I(174385, 1, -1), WF_I(160275, 5) }, 2),
			WF_Task(5648, 20260718, 20260718, 1, 1, 1782921600, commonLogin, 2),
			WF_Task(5649, 20260719, 20260719, 1, 1, 1782921600, commonLogin, 2),
			WF_Task(5650, 20260725, 20260725, 1, 1, 1782921600, commonLogin, 2),
			WF_Task(5651, 20260726, 20260726, 1, 1, 1782921600, commonLogin, 2)
		};
		out.push_back(std::move(login));

		WFActiveConfig ugc;
		ugc.EntryID = 484;
		ugc.SeasonID = 4;
		ugc.BeginTime = 1782921600LL;
		ugc.EndTime = 1787846399LL;
		ugc.Background = { 1, "Res/uires/ActivityTaskTemplate/Res/AC_1001/Img/bg-ugc-sjxm.png", "" };
		ugc.Tasks = {
			WF_Task(5711, 20260702, 20260826, 1, 2, 1782921600, { WF_I(138147, 5) }, 1, 1),
			WF_Task(5712, 20260702, 20260826, 1, 0, 1782921600, { WF_I(165092, 2), WF_I(160275, 1) }, 1, 1),
			WF_Task(5713, 20260702, 20260826, 2, 0, 1782921600, { WF_I(165092, 2), WF_I(160275, 1) }, 1, 1),
			WF_Task(5714, 20260702, 20260826, 3, 0, 1782921600, { WF_I(165092, 3), WF_I(160275, 1) }, 1, 1),
			WF_Task(5715, 20260702, 20260826, 5, 0, 1782921600, { WF_I(165092, 3), WF_I(160275, 1) }, 1, 1),
			WF_Task(5716, 20260702, 20260826, 7, 0, 1782921600, { WF_I(174503, 1, -1), WF_I(165092, 5), WF_I(160275, 1) }, 1, 1),
			WF_Task(5717, 20260702, 20260826, 5, 0, 1782921600, { WF_I(165092, 2), WF_I(160275, 2) }, 1, 1),
			WF_Task(5718, 20260702, 20260826, 10, 0, 1782921600, { WF_I(165092, 2), WF_I(160275, 2) }, 1, 1),
			WF_Task(5719, 20260702, 20260826, 15, 0, 1782921600, { WF_I(165092, 3), WF_I(160275, 2) }, 1, 1),
			WF_Task(5720, 20260702, 20260826, 20, 0, 1782921600, { WF_I(165092, 3), WF_I(160275, 2) }, 1, 1),
			WF_Task(5721, 20260702, 20260826, 30, 0, 1782921600, { WF_I(165092, 5), WF_I(160275, 2) }, 1, 1)
		};
		out.push_back(std::move(ugc));

		WFActiveConfig origin;
		origin.EntryID = 582;
		origin.SeasonID = 1;
		origin.BeginTime = 1783094400LL;
		origin.EndTime = 1787759999LL;
		origin.ProcessItemID = 168529;
		origin.ProcessType = 2;
		origin.DetailUrl = "https://speed.qq.com/webplat/info/news_version3/147/14551/14572/14578/m11645/202606/987962.shtml";
		origin.Background = { 1, "Res/uires/ActivityTaskTemplate/Res/AC_1001/Img/ActiveOriginGifts.png", "" };
		origin.Tasks = {
			WF_Task(5828, 20260704, 20260826, 5, 0, 1783094400, { WF_I(103081, 1) }),
			WF_Task(5829, 20260704, 20260826, 1, 0, 1783094400, { WF_I(103081, 1) }),
			WF_Task(5820, 20260704, 20260826, 500, 2, 1783094400, { WF_I(174788, 5), WF_I(158211, 1) }),
			WF_Task(5821, 20260704, 20260826, 800, 2, 1783094400, { WF_I(174788, 5), WF_I(158211, 1) }),
			WF_Task(5822, 20260704, 20260826, 5, 2, 1783094400, { WF_I(174788, 5), WF_I(158211, 1) }),
			WF_Task(5823, 20260704, 20260826, 10, 2, 1783094400, { WF_I(174788, 5), WF_I(158211, 1) }),
			WF_Task(5824, 20260704, 20260826, 3, 2, 1783094400, { WF_I(174788, 5), WF_I(158211, 1) }),
			WF_Task(5825, 20260704, 20260826, 5, 2, 1783094400, { WF_I(174788, 5), WF_I(158211, 1) }),
			WF_Task(5826, 20260704, 20260826, 1000, 2, 1783094400, { WF_I(174788, 8) }),
			WF_Task(5827, 20260704, 20260826, 2000, 2, 1783094400, { WF_I(174788, 8) })
		};
		out.push_back(std::move(origin));

		WFActiveConfig football;
		football.EntryID = 508;
		football.SeasonID = 2;
		football.BeginTime = 1781193600LL;
		football.EndTime = 1784822399LL;
		football.DetailUrl = "https://speed.qq.com/webplat/info/news_version3/147/14551/14572/14578/m22624/202606/986914.shtml";
		football.Background = { 1, "", "https://down.speed.qq.com/CDNRes/lysc_95aeb2e9.png" };
		football.Tasks = {
			WF_Task(5230, 20260612, 20260723, 3000, 0, 1781193600, { WF_I(88789, 1, -1), WF_I(88799, 1, -1), WF_I(165093, 1) }, 1, 1),
			WF_Task(5231, 20260612, 20260723, 1500, 0, 1781193600, { WF_I(88781, 1, -1), WF_I(88791, 1, -1), WF_I(165093, 1) }, 1, 1),
			WF_Task(5232, 20260612, 20260723, 2, 2, 1781193600, { WF_I(160275, 1), WF_I(88979, 1) }, 1, 1),
			WF_Task(5233, 20260612, 20260723, 2, 2, 1781193600, { WF_I(160275, 1), WF_I(88979, 1) }, 1, 1),
			WF_Task(5234, 20260612, 20260723, 50, 2, 1781193600, { WF_I(160275, 1), WF_I(88979, 1) }, 1, 1)
		};
		out.push_back(std::move(football));

		WFActiveConfig weekly;
		weekly.EntryID = 476;
		weekly.SeasonID = 1;
		weekly.BeginTime = 1768406400LL;
		weekly.EndTime = 4093171199LL;
		weekly.ProcessItemID = 67;
		weekly.ProcessType = 1;
		weekly.DetailUrl = "https://speed.qq.com/home/texing/99tiantianpaotu.html";
		weekly.Background = { 1, "Res/uires/ActivityTaskTemplate/Res/AC_1001/Img/WeeklyActive.png", "" };
		weekly.Tasks = {
			WF_Task(5370, 20260115, 20990915, 1, 1, 1768406400, {}, 1, 1, 30),
			WF_Task(5643, 20260702, 20260826, 1, 1, 1768406400, {}, 1, 1, 80),
			WF_Task(5371, 20260115, 20990915, 1, 1, 1768406400, {}, 1, 1, 50),
			WF_Task(5372, 20260115, 20990915, 2, 1, 1768406400, {}, 1, 1, 50),
			WF_Task(5373, 20260115, 20990915, 3, 1, 1768406400, {}, 1, 1, 60),
			WF_Task(5374, 20260115, 20990915, 4, 1, 1768406400, {}, 1, 1, 60),
			WF_Task(5375, 20260115, 20990915, 5, 1, 1768406400, {}, 1, 1, 80)
		};
		weekly.ProcessRewards = {
			{ 50, "5000点券", { WF_PM(5000) } },
			{ 100, "改装自选宝箱*5", { WF_PI(138147, 5) } },
			{ 150, "QS晶匣钥匙*2+8000点券", { WF_PI(160275, 2), WF_PM(8000) } },
			{ 300, "改装自选宝箱*10", { WF_PI(138147, 10) } },
			{ 500, "QS晶匣钥匙*2+15000点券", { WF_PI(160275, 2), WF_PM(15000) } }
		};
		out.push_back(std::move(weekly));

		return out;
	}();
	return configs;
}

static const WFActiveConfig* WF_FindActive(int entryID)
{
	const auto& configs = WF_ActiveConfigs();
	auto it = std::find_if(configs.begin(), configs.end(), [&](const WFActiveConfig& cfg) { return cfg.EntryID == entryID; });
	return it == configs.end() ? nullptr : &*it;
}

static const std::vector<WFSignConfig>& WF_SignConfigs()
{
	static const std::vector<WFSignConfig> configs = {
		{ 542, 1, 1783699200LL, 1787759999LL,
			"https://speed.qq.com/webplat/info/news_version3/147/14551/14572/14578/m22624/202606/988209.shtml",
			{ 1, "Res/uires/ActivitySignInTemplate/Res/AC_1001/Img/SignCoolsummer.png", "" },
			{
				{ 1, { WF_PI(138147, 10) } }, { 1, { WF_PI(162550, 2) } },
				{ 1, { WF_PI(174662, 1, -1) } }, { 1, { WF_PI(138147, 20) } },
				{ 1, { WF_PI(174636, 1, -1) } }, { 1, { WF_PI(138147, 20) } },
				{ 1, { WF_PI(174557, 1, -1) } }
			} },
		{ 583, 1, 1784044800LL, 1787759999LL,
			"https://speed.qq.com/webplat/info/news_version3/147/14551/14572/14578/m11645/202606/987963.shtml",
			{ 1, "Res/uires/ActivitySignInTemplate/Res/AC_1001/Img/SignFunction.png", "" },
			{
				{ 1, { WF_PI(174983, 1, -1), WF_PI(155787, 1) } },
				{ 1, { WF_PI(155788, 1) } }, { 1, { WF_PI(155788, 1) } },
				{ 1, { WF_PI(174981, 1, -1), WF_PI(155787, 1) } },
				{ 1, { WF_PI(155788, 1) } }, { 1, { WF_PI(155788, 1) } },
				{ 1, { WF_PI(174982, 1, -1), WF_PI(155787, 1) } }
			} },
		{ 487, 2, 1774800000LL, 2114351999LL,
			"https://speed.qq.com/webplat/info/news_version3/147/14551/14572/14585/m22624/202603/984019.shtml",
			{ 1, "Res/uires/ActivitySignInTemplate/Res/AC_1001/Img/weeksign.png", "" },
			{
				{ 1, { WF_PI(138147, 10), WF_PM(1000) } },
				{ 2, { WF_PI(138147, 10), WF_PI(74273, 1000) } },
				{ 1, { WF_PI(138147, 10), WF_PI(160275, 2) } },
				{ 2, { WF_PI(21983, 2), WF_PI(74273, 1000) } },
				{ 1, { WF_PI(138147, 10), WF_PI(160275, 2) } },
				{ 1, { WF_PI(138147, 10), WF_PI(74731, 1) } },
				{ 2, { WF_PI(160464, 3), WF_PI(160440, 1) } }
			} }
	};
	return configs;
}

static const WFSignConfig* WF_FindSign(int entryID)
{
	const auto& configs = WF_SignConfigs();
	auto it = std::find_if(configs.begin(), configs.end(), [&](const WFSignConfig& cfg) { return cfg.EntryID == entryID; });
	return it == configs.end() ? nullptr : &*it;
}

static const std::vector<WFWarmUpConfig>& WF_WarmUpConfigs()
{
	static const std::vector<WFWarmUpConfig> configs = {
		{ 497, 1, "", "Res/uires/ActivityTaskTemplate/Res/AC_1001/Img/guanmei.png", 1,
			3060950400LL, 1769011200LL, "" },
		{ 497, 2, "", "", 1, 1785513599LL, 1782921600LL,
			"https://down.speed.qq.com/CDNRes/shengxia_39e3f88d.png" }
	};
	return configs;
}

static const WFWarmUpConfig* WF_FindWarmUp(int entryID, int funcID)
{
	const auto& configs = WF_WarmUpConfigs();
	auto it = std::find_if(configs.begin(), configs.end(), [&](const WFWarmUpConfig& cfg) {
		return cfg.EntryID == entryID && cfg.FuncID == funcID;
	});
	return it == configs.end() ? nullptr : &*it;
}

static const WFRedDemonConfig& WF_RedDemonConfig()
{
	static const WFRedDemonConfig config = {
		570, 1, 1783699200LL, 1787759999LL,
		"https://speed.qq.com/webplat/info/news_version3/147/14551/14572/14578/m22624/202606/988207.shtml",
		171045, false, "活动分两阶段开启，每阶段3个任务，完成任务即可领取奖励。",
		1785513600LL, WF_I(171045, 1, 168, 1), "8月1日开放领取，敬请期待！",
		{
			{ 1, 1783699200LL, {
				{ 1, 5743, 20260711, 20260826, 1, { WF_I(174739, 1), WF_I(138147, 20) },
					"初级改装宝箱（20个）+魔鬼召唤宝箱（1个）" },
				{ 2, 5744, 20260711, 20260826, 5, { WF_I(174739, 1), WF_I(138147, 30) },
					"初级改装宝箱（30个）+魔鬼召唤宝箱（1个）" },
				{ 3, 5745, 20260711, 20260826, 5, { WF_I(174739, 1), WF_I(138147, 50) },
					"初级改装宝箱（50个）+魔鬼召唤宝箱（1个）" }
			} },
			{ 2, 1785513600LL, {
				{ 1, 5746, 20260801, 20260826, 1, { WF_I(174739, 2), WF_I(138147, 20) },
					"初级改装宝箱（20个）+红鬼召唤宝箱（2个）" },
				{ 2, 5747, 20260801, 20260826, 5, { WF_I(174739, 2), WF_I(138147, 20) },
					"初级改装宝箱（20个）+红鬼召唤宝箱（2个）" },
				{ 3, 5748, 20260801, 20260826, 7, { WF_I(174739, 3), WF_I(138147, 20) },
					"初级改装宝箱（20个）+红鬼召唤宝箱（3个）" }
			} }
		},
		1
	};
	return config;
}

static const WFAnnualConfig& WF_AnnualConfig()
{
	static const WFAnnualConfig config = {
		156052,
		{
			{ 5751, 5, 20260826, 1, 20260702, 1 },
			{ 5752, 5, 20260826, 3, 20260702, 1 },
			{ 5753, 10, 20260826, 1, 20260702, 1 }
		},
		{
			{ 100, "1", 40, 1, "高级飞车币宝箱逐梦" },
			{ 50, "2", 860, 2, "中级飞车币宝箱逐梦" },
			{ 30, "3", 100, 3, "低级飞车币宝箱逐梦" }
		}
	};
	return config;
}

static const WFStrengthenConfig& WF_StrengthenConfig()
{
	static const WFStrengthenConfig config = {
		60, 2,
		{
			WF_I(12380, 10), WF_I(12376, 10), WF_I(96597, 10), WF_I(12377, 10), WF_I(12378, 10),
			WF_I(21977, 5), WF_I(21983, 5), WF_I(22013, 5), WF_I(21989, 5), WF_I(22025, 5),
			WF_I(22031, 5), WF_I(22037, 5), WF_I(22043, 5)
		}
	};
	return config;
}

static const WFSpeedCoinBubbleConfig& WF_SpeedCoinBubbleConfig()
{
	static const WFSpeedCoinBubbleConfig config = {
		"2026-08-26 23-59-59",
		"2026-07-11 23-59-59",
		"Actions.BenefitsActivityCenter.OpenActivity(5,2,{})",
		1787759999LL,
		"赛车手拥有%d限时飞车币，该道具将在%d月%d日过期",
		1783699200LL,
		1783785599LL,
		"2026-07-11 00-00-00"
	};
	return config;
}

static void WF_LoadClaims(std::shared_ptr<ClientNode> client, int entryID, int seasonID, int kind,
	std::vector<INT>& indexes, UINT& lastClaimTime)
{
	indexes.clear();
	lastClaimTime = 0;
	if (client) PlayerDB_WFLoadClaims(client->Uin, static_cast<UINT>(entryID), seasonID, kind, indexes, lastClaimTime);
}

static bool WF_IsClaimed(const std::vector<INT>& claims, int index)
{
	return std::find(claims.begin(), claims.end(), index) != claims.end();
}

static void WF_WriteBackground(BYTE*& p, const WFBackground& cfg)
{
	WF_WriteNumber(p, "showType", cfg.ShowType);
	WF_WriteTextValue(p, "resource", cfg.Resource);
	if (!cfg.CdnResource.empty()) WF_WriteTextValue(p, "backgroundResCDN", cfg.CdnResource);
}

static void WF_WriteProcessParts(BYTE*& p, const std::vector<WFRewardPart>& parts)
{
	WF_WriteObject(p, "items", [&](BYTE*& table) {
		for (size_t i = 0; i < parts.size(); ++i)
		{
			WF_WriteIndexed(table, static_cast<int>(i + 1), [&](BYTE*& row) {
				const WFRewardPart& part = parts[i];
				if (part.HasItem)
				{
					WF_WriteNumber(row, "m_iAvailPeriod", part.Item.AvailPeriod);
					WF_WriteNumber(row, "m_ushItemType", part.Item.ItemType);
					WF_WriteNumber(row, "m_iItemNum", part.Item.ItemNum);
					WF_WriteNumber(row, "award_type", 1);
					WF_WriteNumber(row, "m_iItemID", part.Item.ItemID);
				}
				else
				{
					WF_WriteNumber(row, "award_type", 1);
					WF_WriteNumber(row, "m_iSuperMoney", part.SuperMoney);
				}
			});
		}
	});
}

static void WF_WriteActiveTask(BYTE*& p, const WFActiveTask& task)
{
	WF_WriteNumber(p, "m_iOpenDate", task.OpenDate);
	if (task.WireLayout == 1)
	{
		WF_WriteNumber(p, "m_ucTaskID", task.TaskID);
		WF_WriteNumber(p, "m_uiAlreadyAwardNums", 0);
		if (task.ActivityAward > 0) WF_WriteNumber(p, "m_iActivityAward", task.ActivityAward);
		WF_WriteNumber(p, "m_uiReceiveStatus", 0);
		WF_WriteNumber(p, "m_iShowDetail", task.ShowDetail < 0 ? 0 : task.ShowDetail);
		WF_WriteNumber(p, "m_iShowTimeStamp", task.ShowTimeStamp);
		WF_WriteNumber(p, "m_uiProcessRate", 0);
		WF_WriteNumber(p, "m_iType", 1);
		WF_WriteNumber(p, "m_shExecutePeriod", task.ExecutePeriod);
		WF_WriteNumber(p, "m_uiTaskFinishNums", task.FinishNums);
		WF_WriteNumber(p, "m_iCloseDate", task.CloseDate);
		WF_WriteNumber(p, "m_shActiveReceiveAwardFlag", 0);
	}
	else
	{
		WF_WriteNumber(p, "m_shActiveReceiveAwardFlag", 0);
		if (task.WireLayout != 2)
		{
			WF_WriteNumber(p, "m_uiAlreadyAwardNums", 0);
			WF_WriteNumber(p, "m_uiReceiveStatus", 0);
		}
		WF_WriteNumber(p, "m_iShowTimeStamp", task.ShowTimeStamp);
		if (task.WireLayout != 2) WF_WriteNumber(p, "m_uiProcessRate", 0);
		WF_WriteNumber(p, "m_iType", 1);
		WF_WriteNumber(p, "m_shExecutePeriod", task.ExecutePeriod);
		WF_WriteNumber(p, "m_uiTaskFinishNums", task.FinishNums);
		WF_WriteNumber(p, "m_iCloseDate", task.CloseDate);
		WF_WriteNumber(p, "m_ucTaskID", task.TaskID);
	}
	WF_WriteNumber(p, "m_unIdentityLimit", 0);
	WF_WriteObject(p, "m_stAwardToClient", [&](BYTE*& award) { WF_WriteAward(award, task.Award); });
}

static void WF_WriteActiveMain(BYTE*& p, std::shared_ptr<ClientNode> client, const WFActiveConfig& cfg)
{
	std::vector<INT> claims;
	UINT lastClaim = 0;
	WF_LoadClaims(client, cfg.EntryID, cfg.SeasonID, kWFClaimProcess, claims, lastClaim);
	const int current = WF_ItemCount(client, cfg.ProcessItemID);

	WF_WriteNumber(p, "entryID", cfg.EntryID);
	WF_WriteObject(p, "taskStatusArray", [&](BYTE*& table) {
		for (size_t i = 0; i < cfg.Tasks.size(); ++i)
		{
			WF_WriteIndexed(table, static_cast<int>(i + 1), [&](BYTE*& row) { WF_WriteActiveTask(row, cfg.Tasks[i]); });
		}
	});
	WF_WriteObject(p, "processRewardInfo", [&](BYTE*& table) {
		for (size_t i = 0; i < cfg.ProcessRewards.size(); ++i)
		{
			WF_WriteIndexed(table, static_cast<int>(i + 1), [&](BYTE*& row) {
				const WFProcessReward& reward = cfg.ProcessRewards[i];
				WF_WriteProcessParts(row, reward.Parts);
				const int status = WF_IsClaimed(claims, static_cast<int>(i + 1)) ? 2 : (current >= reward.NeedScore ? 1 : 0);
				WF_WriteNumber(row, "status", status);
				WF_WriteNumber(row, "needScore", reward.NeedScore);
				WF_WriteTextValue(row, "desc", reward.Desc);
			});
		}
	});
	WF_WriteNumber(p, "currentProcessValue", current);
	WF_WriteObject(p, "backgroundCfg", [&](BYTE*& bg) { WF_WriteBackground(bg, cfg.Background); });
	WF_WriteObject(p, "baseInfo", [&](BYTE*& base) {
		if (!cfg.DetailUrl.empty()) WF_WriteTextValue(base, "detailUrl", cfg.DetailUrl);
		WF_WriteNumber(base, "endTime", cfg.EndTime);
		if (cfg.ProcessItemID > 0) WF_WriteNumber(base, "processItemID", cfg.ProcessItemID);
		WF_WriteNumber(base, "seasonID", cfg.SeasonID);
		if (cfg.ProcessType > 0) WF_WriteNumber(base, "processType", cfg.ProcessType);
		WF_WriteNumber(base, "beginTime", cfg.BeginTime);
	});
}

static void WF_WriteSignParts(BYTE*& p, const std::vector<WFRewardPart>& parts)
{
	WF_WriteObject(p, "awardItemInfo", [&](BYTE*& table) {
		for (size_t i = 0; i < parts.size(); ++i)
		{
			WF_WriteIndexed(table, static_cast<int>(i + 1), [&](BYTE*& row) {
				if (parts[i].HasItem) WF_WriteItem(row, parts[i].Item);
				else WF_WriteNumber(row, "m_iSuperMoney", parts[i].SuperMoney);
			});
		}
	});
}

static void WF_WriteSignMain(BYTE*& p, std::shared_ptr<ClientNode> client, const WFSignConfig& cfg)
{
	std::vector<INT> claims;
	UINT lastClaim = 0;
	WF_LoadClaims(client, cfg.EntryID, cfg.SeasonID, kWFClaimSign, claims, lastClaim);
	const int todayStatus = lastClaim > 0 && WF_LocalDate(static_cast<time_t>(lastClaim)) == WF_LocalDate(time(nullptr)) ? 1 : 0;

	WF_WriteObject(p, "backgroundCfg", [&](BYTE*& bg) { WF_WriteBackground(bg, cfg.Background); });
	WF_WriteObject(p, "signInInfo", [&](BYTE*& table) {
		for (size_t i = 0; i < cfg.Days.size(); ++i)
		{
			WF_WriteIndexed(table, static_cast<int>(i + 1), [&](BYTE*& row) {
				WF_WriteNumber(row, "status", WF_IsClaimed(claims, static_cast<int>(i + 1)) ? 1 : 0);
				WF_WriteSignParts(row, cfg.Days[i].Parts);
				WF_WriteNumber(row, "multiple", cfg.Days[i].Multiple);
			});
		}
	});
	WF_WriteObject(p, "baseInfo", [&](BYTE*& base) {
		WF_WriteNumber(base, "seasonID", cfg.SeasonID);
		WF_WriteTextValue(base, "detailUrl", cfg.DetailUrl);
		WF_WriteNumber(base, "endTime", cfg.EndTime);
		WF_WriteNumber(base, "beginTime", cfg.BeginTime);
	});
	WF_WriteObject(p, "playerInfo", [&](BYTE*& player) {
		WF_WriteNumber(player, "lastSignInTime", lastClaim);
		WF_WriteNumber(player, "days", static_cast<int>(claims.size()));
		WF_WriteNumber(player, "todayStatus", todayStatus);
	});
	WF_WriteNumber(p, "entryID", cfg.EntryID);
}

static void WF_WriteRedDemonTask(BYTE*& p, const WFRedDemonTask& task, bool stageOpen)
{
	WF_WriteNumber(p, "m_iTaskSlotID", task.SlotID);
	WF_WriteNumber(p, "m_iAwardState", 0);
	WF_WriteObject(p, "m_stTaskInfo", [&](BYTE*& info) {
		WF_WriteNumber(info, "m_iOpenDate", task.OpenDate);
		WF_WriteNumber(info, "m_ucTaskID", task.TaskID);
		if (stageOpen)
		{
			WF_WriteNumber(info, "m_uiAlreadyAwardNums", 0);
			WF_WriteNumber(info, "m_uiProcessRate", 0);
		}
		WF_WriteNumber(info, "m_uiTaskFinishNums", task.FinishNums);
		WF_WriteNumber(info, "m_iCloseDate", task.CloseDate);
		WF_WriteNumber(info, "m_shExecutePeriod", 0);
		if (stageOpen) WF_WriteNumber(info, "m_iStatus", 0);
	});
	WF_WriteItemTable(p, "m_astAwards", task.Awards);
	WF_WriteTextValue(p, "m_strAwardDesc", task.AwardDesc);
}

static void WF_WriteRedDemonMain(BYTE*& p, const WFRedDemonConfig& cfg)
{
	const std::int64_t now = static_cast<std::int64_t>(time(nullptr));
	WF_WriteObject(p, "m_astBaseInfo", [&](BYTE*& base) {
		WF_WriteNumber(base, "m_iEndTime", cfg.EndTime);
		WF_WriteTextValue(base, "m_strDetailUrl", cfg.DetailUrl);
		WF_WriteNumber(base, "m_iBeginTime", cfg.BeginTime);
		WF_WriteNumber(base, "m_iSeasonID", cfg.SeasonID);
		WF_WriteNumber(base, "m_iPreviewItemID", cfg.PreviewItemID);
		WF_WriteNumber(base, "m_bPermanent", cfg.Permanent ? 1 : 0);
		WF_WriteTextValue(base, "m_strTipText", cfg.TipText);
	});
	WF_WriteNumber(p, "m_iEntryID", cfg.EntryID);
	WF_WriteObject(p, "m_stWelfareInfo", [&](BYTE*& welfare) {
		WF_WriteNumber(welfare, "m_iUnlockDate", cfg.WelfareUnlockTime);
		// 官服状态由活动进度决定；本服没有该任务进度来源时保持未领取/未解锁，不伪造可领奖状态。
		WF_WriteNumber(welfare, "m_iWelfareState", 0);
		WF_WriteObject(welfare, "m_astAwards", [&](BYTE*& awards) {
			WF_WriteIndexed(awards, 1, [&](BYTE*& item) { WF_WriteItem(item, cfg.WelfareAward); });
		});
		WF_WriteTextValue(welfare, "m_strLockTipText", cfg.WelfareLockTip);
	});
	WF_WriteObject(p, "m_astStageInfo", [&](BYTE*& stages) {
		for (size_t i = 0; i < cfg.Stages.size(); ++i)
		{
			const WFRedDemonStage& stage = cfg.Stages[i];
			const bool stageOpen = now >= stage.UnlockTime && now <= cfg.EndTime;
			WF_WriteIndexed(stages, static_cast<int>(i + 1), [&](BYTE*& row) {
				WF_WriteNumber(row, "m_iStageState", stageOpen ? 1 : 0);
				WF_WriteNumber(row, "m_iStageID", stage.StageID);
				WF_WriteObject(row, "m_astTaskInfo", [&](BYTE*& tasks) {
					for (size_t j = 0; j < stage.Tasks.size(); ++j)
					{
						WF_WriteIndexed(tasks, static_cast<int>(j + 1), [&](BYTE*& task) {
							WF_WriteRedDemonTask(task, stage.Tasks[j], stageOpen);
						});
					}
				});
				WF_WriteNumber(row, "m_iUnlockDate", stage.UnlockTime);
			});
		}
	});
	WF_WriteNumber(p, "m_iOpType", cfg.OpType);
}

static void WF_WriteAnnualMain(BYTE*& p, std::shared_ptr<ClientNode> client, const WFAnnualConfig& cfg)
{
	WF_WriteObject(p, "PlayerInfo", [&](BYTE*& player) {
		WF_WriteNumber(player, "AwardFlag", 0);
		WF_WriteNumber(player, "OwnerBoxType", 0);
		WF_WriteNumber(player, "ExchangeItemNum", WF_ItemCount(client, cfg.ExchangePieceID));
		WF_WriteNumber(player, "HadGotNum", 0);
		WF_WriteNumber(player, "HadBindNum", 0);
		WF_WriteNumber(player, "ShareFlag", 0);
		WF_WriteNumber(player, "BindBoxType", 0);
		WF_WriteTextValue(player, "OwnerUniqueCode", "");
	});
	WF_WriteObject(p, "taskTable", [&](BYTE*& tasks) {
		for (size_t i = 0; i < cfg.Tasks.size(); ++i)
		{
			const WFAnnualTask& task = cfg.Tasks[i];
			WF_WriteIndexed(tasks, static_cast<int>(i + 1), [&](BYTE*& row) {
				WF_WriteNumber(row, "m_ucTaskID", task.TaskID);
				WF_WriteNumber(row, "m_uiAwardMoveNums", task.AwardMoveNums);
				WF_WriteNumber(row, "closeDate", task.CloseDate);
				WF_WriteNumber(row, "m_uiProcessRate", 0);
				WF_WriteNumber(row, "m_uiTaskFinishNums", task.FinishNums);
				WF_WriteNumber(row, "openDate", task.OpenDate);
				WF_WriteNumber(row, "ExecutePeriod", task.ExecutePeriod);
				WF_WriteNumber(row, "receiveStatus", 0);
				WF_WriteNumber(row, "m_uiAlreadyAwardNums", 0);
			});
		}
	});
	WF_WriteObject(p, "baseCfg", [&](BYTE*& base) {
		WF_WriteTextValue(base, "LimitSpeedCoinEndStr", "2026-08-30 23-59-59");
		WF_WriteNumber(base, "ShareCodeAwardMoveNums", 5);
		WF_WriteNumber(base, "RemindBeginTime", 1785599999LL);
		WF_WriteNumber(base, "ExchangePieceID", cfg.ExchangePieceID);
		WF_WriteNumber(base, "BindMaxNumLimit", 5);
		WF_WriteNumber(base, "AwardMaxNum", 3267);
		WF_WriteTextValue(base, "DateBeginStr", "2026-07-02 00-00-00");
		WF_WriteNumber(base, "VerificationBeginTime", 1782957600LL);
		WF_WriteNumber(base, "EndTime", 1787759999LL);
		WF_WriteNumber(base, "LimitSpeedCoinEndTime", 1788105599LL);
		WF_WriteTextValue(base, "ShareCodeDesc", "每日分享飞车币宝箱");
		WF_WriteTextValue(base, "VerificationBeginStr", "2026-07-02 10-00-00");
		WF_WriteNumber(base, "BeginTime", 1782921600LL);
		WF_WriteNumber(base, "ShowItemId", 171050);
		WF_WriteTextValue(base, "DateEndStr", "2026-08-26 23-59-59");
		WF_WriteNumber(base, "seasonId", 2);
		WF_WriteTextValue(base, "RemindBeginStr", "2026-08-01 23-59-59");
	});
	WF_WriteObject(p, "BoxTable", [&](BYTE*& boxes) {
		for (size_t i = 0; i < cfg.Boxes.size(); ++i)
		{
			const WFAnnualBox& box = cfg.Boxes[i];
			WF_WriteIndexed(boxes, static_cast<int>(i + 1), [&](BYTE*& row) {
				WF_WriteNumber(row, "ItemNum", box.ItemNum);
				WF_WriteTextValue(row, "PictureUrl", box.PictureUrl);
				WF_WriteNumber(row, "prob", box.Probability);
				WF_WriteNumber(row, "BoxType", box.BoxType);
				WF_WriteTextValue(row, "BoxDesc", box.Description);
			});
		}
	});
}

static int WF_StrengthenAwardIndex(const StrengthenBagPlayerState& state, const WFStrengthenConfig& cfg, size_t slot)
{
	if (slot >= state.AwardIndexes.size()) return 0;
	const int awardIndex = state.AwardIndexes[slot];
	return awardIndex >= 1 && awardIndex <= static_cast<int>(cfg.Awards.size()) ? awardIndex : 0;
}

static int WF_StrengthenPendingCount(const StrengthenBagPlayerState& state, const WFStrengthenConfig& cfg)
{
	int count = 0;
	for (size_t i = 0; i < state.AwardIndexes.size(); ++i)
	{
		if (WF_StrengthenAwardIndex(state, cfg, i) > 0) ++count;
	}
	return count;
}

static void WF_WriteStrengthenMain(BYTE*& p, const WFStrengthenConfig& cfg, const StrengthenBagPlayerState& state)
{
	WF_WriteObject(p, "BaseCfg", [&](BYTE*& base) {
		WF_WriteNumber(base, "degreeNum", cfg.DegreeNum);
		WF_WriteNumber(base, "dayDegreeBagNum", cfg.DayDegreeBagNum);
	});
	WF_WriteObject(p, "Award", [&](BYTE*& awards) {
		for (size_t i = 0; i < cfg.Awards.size(); ++i)
		{
			const WFItem& award = cfg.Awards[i];
			WF_WriteIndexed(awards, static_cast<int>(i + 1), [&](BYTE*& row) {
				WF_WriteNumber(row, "type", 1);
				WF_WriteObject(row, "item", [&](BYTE*& items) {
					WF_WriteIndexed(items, 1, [&](BYTE*& item) {
						WF_WriteNumber(item, "m_iAvailPeriod", award.AvailPeriod);
						WF_WriteNumber(item, "m_ushItemType", award.ItemType);
						WF_WriteNumber(item, "m_iItemNum", award.ItemNum);
						WF_WriteObject(item, "m_aiClientParas", [&](BYTE*& params) {
							WF_WriteNumber(params, 1);
							WF_WriteNumber(params, 0);
						});
						WF_WriteNumber(item, "m_iItemID", award.ItemID);
					});
				});
				WF_WriteNumber(row, "m_iSuperMoney", 0);
				WF_WriteNumber(row, "m_iCoupons", 0);
			});
		}
	});
	WF_WriteObject(p, "playerInfo", [&](BYTE*& player) {
		// 客户端固定访问 awardInfo[1..5]；官服空状态也会下发 5 个数字槽位。
		WF_WriteObject(player, "awardInfo", [&](BYTE*& awardInfo) {
			for (size_t i = 0; i < state.AwardIndexes.size(); ++i)
			{
				WF_WriteNumber(awardInfo, static_cast<int>(i + 1));
				WF_WriteNumber(awardInfo, WF_StrengthenAwardIndex(state, cfg, i));
			}
		});
		WF_WriteNumber(player, "dayActivityBagNum", (std::max)(0, state.DayActivityBagNum));
		WF_WriteNumber(player, "dayOfflineBagNum", (std::max)(0, state.DayOfflineBagNum));
	});
}

static void WF_WriteStrengthenAwardResult(BYTE*& p, const std::vector<WFItem>& items)
{
	WF_WriteObject(p, "awardToReturn", [&](BYTE*& award) {
		WF_WriteNumber(award, "m_sItemNum", static_cast<int>(items.size()));
		WF_WriteNumber(award, "m_iSuperMoney", 0);
		WF_WriteNumber(award, "m_iCoupons", 0);
		WF_WriteItemTable(award, "m_astItemInfo", items);
	});
}
}

void Request_Beta99HallGetEntryCfg(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) {
		WF_WriteObject(p, "EntryCfg", [&](BYTE*& table) {
			const auto& entries = WF_HallEntries();
			for (size_t i = 0; i < entries.size(); ++i)
			{
				WF_WriteIndexed(table, static_cast<int>(i + 1), [&](BYTE*& row) {
					const WFHallEntry& entry = entries[i];
					WF_WriteNumber(row, "Index", entry.Index);
					WF_WriteObject(row, "Jump", [&](BYTE*& jump) {
						WF_WriteNumber(jump, "JumpTab2", entry.Jump.Tab2);
						WF_WriteNumber(jump, "JumpTab1", entry.Jump.Tab1);
						WF_WriteTextValue(jump, "JumpURL", entry.Jump.Url);
						WF_WriteNumber(jump, "JumpBuyCommodityID", entry.Jump.BuyCommodityID);
						WF_WriteTextValue(jump, "JumpShopTab1", entry.Jump.ShopTab1);
						WF_WriteNumber(jump, "JumpType", entry.Jump.Type);
						WF_WriteNumber(jump, "JumpShopTab2", entry.Jump.ShopTab2);
						WF_WriteNumber(jump, "JumpEntryID", entry.Jump.EntryID);
					});
					WF_WriteNumber(row, "ActID", entry.ActID);
					WF_WriteTextValue(row, "EntryDesc", entry.EntryDesc);
					WF_WriteTextValue(row, "Desc", entry.Desc);
					WF_WriteTextValue(row, "CDNBackGround_2", "");
					WF_WriteTextValue(row, "BackGround", entry.Background);
					WF_WriteNumber(row, "Priority", entry.Priority);
					WF_WriteTextValue(row, "EntryPic", entry.EntryPic);
					WF_WriteTextValue(row, "CDNBackGround", "");
				});
			}
		});
	});
}

void Request_Beta99ActivityManagerNewMapEntry(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) {
		WF_WriteTextValue(p, "newMapEntryFunc", "Actions.BenefitsActivityCenter.OpenActivity(3,6,{})");
	});
}

void Request_Beta99ActivityManagerBenefitsMainDlg(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) {
		WF_WriteObject(p, "ActivityCfg", [&](BYTE*& tabTable) {
			const auto& tabs = WF_BenefitTabs();
			for (size_t i = 0; i < tabs.size(); ++i)
			{
				WF_WriteIndexed(tabTable, static_cast<int>(i + 1), [&](BYTE*& tab) {
					const WFBenefitTab& cfg = tabs[i];
					WF_WriteNumber(tab, "Index", cfg.Index);
					WF_WriteTextValue(tab, "TabBackGround", cfg.Background);
					WF_WriteTextValue(tab, "TabName", cfg.Name);
					WF_WriteNumber(tab, "Priority", cfg.Priority);
					WF_WriteObject(tab, "TabCfg", [&](BYTE*& entries) {
						for (size_t j = 0; j < cfg.Entries.size(); ++j)
						{
							WF_WriteIndexed(entries, static_cast<int>(j + 1), [&](BYTE*& row) {
								const WFBenefitEntry& entry = cfg.Entries[j];
								WF_WriteNumber(row, "NewTag", entry.NewTag);
								WF_WriteNumber(row, "Index", entry.Index);
								WF_WriteTextValue(row, "ActName", entry.ActName);
								WF_WriteNumber(row, "ActID", entry.ActID);
								if (entry.HasUseTemplateType) WF_WriteNumber(row, "UseTemplateType", entry.UseTemplateType);
								if (entry.WarmUpFuncID > 0) WF_WriteNumber(row, "WarmUpFuncID", entry.WarmUpFuncID);
								WF_WriteTextValue(row, "ActBackGround", "");
								WF_WriteNumber(row, "Priority", entry.Priority);
								if (!entry.EntryUrl.empty()) WF_WriteTextValue(row, "EntryID", entry.EntryUrl);
								else WF_WriteNumber(row, "EntryID", entry.EntryID);
								WF_WriteNumber(row, "ActType", entry.ActType);
							});
						}
					});
					WF_WriteNumber(tab, "TabID", cfg.TabID);
				});
			}
		});
	});
}

void Request_Beta99ActiveActivityMainDlg(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const std::vector<std::int64_t> nums = WF_ParseNumbers(body, bodyLen);
	if (nums.size() < 2)
	{
		WF_SendError(client, body, bodyLen, "ActiveActivity.MainDlg", "missing EntryID");
		return;
	}
	const WFActiveConfig* cfg = WF_FindActive(static_cast<int>(nums[1]));
	if (!cfg)
	{
		WF_SendError(client, body, bodyLen, "ActiveActivity.MainDlg", "unknown EntryID");
		return;
	}
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) { WF_WriteActiveMain(p, client, *cfg); });
}

void Request_Beta99ActiveActivityProcessReward(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const std::vector<std::int64_t> nums = WF_ParseNumbers(body, bodyLen);
	if (!client || nums.size() < 3)
	{
		WF_SendError(client, body, bodyLen, "ActiveActivity.ProcessReward", "missing arguments");
		return;
	}
	const int entryID = static_cast<int>(nums[1]);
	const int rewardIndex = static_cast<int>(nums[2]);
	const WFActiveConfig* cfg = WF_FindActive(entryID);
	if (!cfg || rewardIndex < 1 || rewardIndex > static_cast<int>(cfg->ProcessRewards.size()))
	{
		WF_SendError(client, body, bodyLen, "ActiveActivity.ProcessReward", "unknown reward");
		return;
	}
	const WFProcessReward& reward = cfg->ProcessRewards[static_cast<size_t>(rewardIndex - 1)];
	if (!WF_ActivityOpen(cfg->BeginTime, cfg->EndTime) || WF_ItemCount(client, cfg->ProcessItemID) < reward.NeedScore)
	{
		WF_SendError(client, body, bodyLen, "ActiveActivity.ProcessReward", "requirement not met");
		return;
	}
	const UINT now = static_cast<UINT>(time(nullptr));
	if (!PlayerDB_WFTryMarkClaimed(client->Uin, static_cast<UINT>(entryID), cfg->SeasonID, kWFClaimProcess, rewardIndex, now))
	{
		WF_SendError(client, body, bodyLen, "ActiveActivity.ProcessReward", "already claimed");
		return;
	}
	const WFAward award = WF_AwardFromParts(reward.Parts);
	WF_GrantAward(client, award);
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) {
		WF_WriteNumber(p, "entryID", entryID);
		WF_WriteObject(p, "collectedIndexes", [&](BYTE*& indexes) {
			WF_WriteNumber(indexes, 1);
			WF_WriteNumber(indexes, rewardIndex);
		});
		WF_WriteObject(p, "award", [&](BYTE*& result) { WF_WriteAward(result, award, true, true); });
	});
}

void Request_Beta99SignActivityMainDlg(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const std::vector<std::int64_t> nums = WF_ParseNumbers(body, bodyLen);
	if (nums.size() < 2)
	{
		WF_SendError(client, body, bodyLen, "SignActivity.MainDlg", "missing EntryID");
		return;
	}
	const WFSignConfig* cfg = WF_FindSign(static_cast<int>(nums[1]));
	if (!cfg)
	{
		WF_SendError(client, body, bodyLen, "SignActivity.MainDlg", "unknown EntryID");
		return;
	}
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) { WF_WriteSignMain(p, client, *cfg); });
}

void Request_Beta99SignActivityReward(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const std::vector<std::int64_t> nums = WF_ParseNumbers(body, bodyLen);
	if (!client || nums.size() < 3)
	{
		WF_SendError(client, body, bodyLen, "SignActivity.GetReward", "missing arguments");
		return;
	}
	const int entryID = static_cast<int>(nums[1]);
	const int dayIndex = static_cast<int>(nums[2]);
	const WFSignConfig* cfg = WF_FindSign(entryID);
	if (!cfg || dayIndex < 1 || dayIndex > static_cast<int>(cfg->Days.size()))
	{
		WF_SendError(client, body, bodyLen, "SignActivity.GetReward", "unknown sign day");
		return;
	}
	std::vector<INT> claims;
	UINT lastClaim = 0;
	WF_LoadClaims(client, entryID, cfg->SeasonID, kWFClaimSign, claims, lastClaim);
	const int today = WF_LocalDate(time(nullptr));
	if (!WF_ActivityOpen(cfg->BeginTime, cfg->EndTime) || dayIndex != static_cast<int>(claims.size() + 1) ||
		(lastClaim > 0 && WF_LocalDate(static_cast<time_t>(lastClaim)) == today))
	{
		WF_SendError(client, body, bodyLen, "SignActivity.GetReward", "sign order or daily limit");
		return;
	}
	const UINT now = static_cast<UINT>(time(nullptr));
	if (!PlayerDB_WFTryMarkClaimed(client->Uin, static_cast<UINT>(entryID), cfg->SeasonID, kWFClaimSign, dayIndex, now))
	{
		WF_SendError(client, body, bodyLen, "SignActivity.GetReward", "already claimed");
		return;
	}
	const WFSignDay& day = cfg->Days[static_cast<size_t>(dayIndex - 1)];
	const WFAward award = WF_AwardFromParts(day.Parts, day.Multiple);
	WF_GrantAward(client, award);
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) {
		WF_WriteNumber(p, "entryID", entryID);
		WF_WriteNumber(p, "multiple", day.Multiple);
		WF_WriteObject(p, "award", [&](BYTE*& result) { WF_WriteAward(result, award, true, true); });
		WF_WriteNumber(p, "dayIndex", dayIndex);
	});
}

void Request_Beta100WarmUpActivityMainDlg(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const std::vector<std::int64_t> nums = WF_ParseNumbers(body, bodyLen);
	if (nums.size() < 3)
	{
		WF_SendError(client, body, bodyLen, "WarmUpActivity.MainDlg", "missing EntryID or FuncID");
		return;
	}
	const WFWarmUpConfig* cfg = WF_FindWarmUp(static_cast<int>(nums[1]), static_cast<int>(nums[2]));
	if (!cfg)
	{
		WF_SendError(client, body, bodyLen, "WarmUpActivity.MainDlg", "unknown EntryID or FuncID");
		return;
	}
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) {
		WF_WriteNumber(p, "entryID", cfg->EntryID);
		WF_WriteNumber(p, "id", cfg->FuncID);
		WF_WriteObject(p, "warmUpInfo", [&](BYTE*& info) {
			WF_WriteTextValue(info, "jumpEntryLink", cfg->JumpEntryLink);
			WF_WriteTextValue(info, "backgroundRes", cfg->BackgroundResource);
			WF_WriteNumber(info, "jumpEntryType", cfg->JumpEntryType);
			WF_WriteNumber(info, "endTime", cfg->EndTime);
			WF_WriteNumber(info, "beginTime", cfg->BeginTime);
			if (!cfg->BackgroundResourceCdn.empty())
				WF_WriteTextValue(info, "backgroundResCDN", cfg->BackgroundResourceCdn);
		});
	});
}

void Request_Beta102RedDemonMainDlg(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const std::vector<std::int64_t> nums = WF_ParseNumbers(body, bodyLen);
	const WFRedDemonConfig& cfg = WF_RedDemonConfig();
	if (nums.size() > 1 && nums[1] != cfg.EntryID)
	{
		WF_SendError(client, body, bodyLen, "RedDemon.MainDlg", "unknown EntryID");
		return;
	}
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) { WF_WriteRedDemonMain(p, cfg); });
}

void Request_Beta99AnnualSkinMainDlg(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const WFAnnualConfig& cfg = WF_AnnualConfig();
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) { WF_WriteAnnualMain(p, client, cfg); });
}

void Request_Beta99AnnualSkinAwardOpt(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	// 当前服务端没有官服年度皮肤任务进度源，拒绝领奖比读取抓包中的完成态并强制发奖更安全。
	WF_SendError(client, body, bodyLen, "AnnualSkin.AwardOpt", "task progress unavailable");
}

void Request_Beta102SpeedCoinTimeLimitBubble(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const WFSpeedCoinBubbleConfig& cfg = WF_SpeedCoinBubbleConfig();
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) {
		WF_WriteTextValue(p, "LimitSpeedCoinEndStr", cfg.LimitSpeedCoinEndStr);
		WF_WriteTextValue(p, "DateEndStr", cfg.DateEndStr);
		WF_WriteTextValue(p, "JumpUrl", cfg.JumpUrl);
		WF_WriteNumber(p, "LimitSpeedCoinEndTime", cfg.LimitSpeedCoinEndTime);
		WF_WriteTextValue(p, "RemindText", cfg.RemindText);
		WF_WriteNumber(p, "BeginTime", cfg.BeginTime);
		WF_WriteNumber(p, "EndTime", cfg.EndTime);
		WF_WriteTextValue(p, "DateBeginStr", cfg.DateBeginStr);
	});
}

void Request_Beta97StrengthenBagMainDlg(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const WFStrengthenConfig& cfg = WF_StrengthenConfig();
	const StrengthenBagPlayerState state = client ? client->StrengthenBagState : StrengthenBagPlayerState{};
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) { WF_WriteStrengthenMain(p, cfg, state); });
}

void Request_Beta97StrengthenBagRedPoint(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	const WFStrengthenConfig& cfg = WF_StrengthenConfig();
	const StrengthenBagPlayerState state = client ? client->StrengthenBagState : StrengthenBagPlayerState{};
	const int redPoint = (std::min)(3, WF_StrengthenPendingCount(state, cfg));
	WF_SendNumberResponse(client, body, bodyLen, 0, redPoint);
}

void Request_Beta97StrengthenBagGetAward(std::shared_ptr<ClientNode> client, BYTE* body, size_t bodyLen)
{
	if (!client) return;
	const WFStrengthenConfig& cfg = WF_StrengthenConfig();
	std::vector<WFItem> items;
	for (size_t i = 0; i < client->StrengthenBagState.AwardIndexes.size(); ++i)
	{
		const int awardIndex = WF_StrengthenAwardIndex(client->StrengthenBagState, cfg, i);
		if (awardIndex > 0) items.push_back(cfg.Awards[static_cast<size_t>(awardIndex - 1)]);
	}

	if (items.empty())
	{
		// 没有本服待领状态时不伪造奖励；仍完成 RPC，避免客户端协程超时报“未知错误”。
		WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) { WF_WriteEmptyTable(p, "awardToReturn"); });
		return;
	}

	// 先消耗待领槽位，避免同一会话内重复点击领取；不生成任何保底或强制奖励。
	client->StrengthenBagState.AwardIndexes.fill(0);
	WFAward award;
	award.Items = items;
	WF_GrantAward(client, award);
	WF_Send(client, body, bodyLen, 0, true, [&](BYTE*& p) { WF_WriteStrengthenAwardResult(p, items); });
}
