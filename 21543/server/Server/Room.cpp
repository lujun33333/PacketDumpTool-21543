#include "Room.h"
#include "GmPermission.h"
#include "Map.h"
#include <time.h>
#include <map>
#include "Database.h"
#include <regex>
#include "DBConnection.h"
#include "Item.h"
#include "AuroraOfficialUGC.h"
#include "RoomOfficialUGC.h"
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <random>
#include <bcrypt.h>

static UINT QQS_ResolveBugletItemId(BYTE bugletType)
{
	switch (bugletType)
	{
	case 0: return 10207;
	case 1: return 14727;
	case 2: return 14728;
	case 3: return 18817;
	case 4: return 23976;
	case 5: return 24923;
	case 7: return 27139;
	case 10: return 31998;
	case 11: return 64294;
	case 12: return 66641;
	case 13: return 68144;
	case 14: return 68143;
	default: return 0;
	}
}

static BYTE QQS_NormalizeNotifyBugletType(BYTE talkType, BYTE requestedBugletType)
{
	return talkType == 1 ? static_cast<BYTE>(0xFF) : requestedBugletType;
}

static ULONGLONG QQS_NextTalkContentIdMilliseconds()
{
	const ULONGLONG now = static_cast<ULONGLONG>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
	static std::atomic<ULONGLONG> last{ 0 };
	ULONGLONG observed = last.load(std::memory_order_relaxed);
	for (;;)
	{
		const ULONGLONG candidate = now > observed ? now : observed + 1;
		if (last.compare_exchange_weak(
			observed, candidate, std::memory_order_relaxed, std::memory_order_relaxed))
		{
			return candidate;
		}
	}
}

static std::array<BYTE, 18> QQS_MakeTalkContentId()
{
	std::array<BYTE, 18> contentId{};
	char* first = reinterpret_cast<char*>(contentId.data());
	const auto result = std::to_chars(
		first, first + contentId.size() - 1, QQS_NextTalkContentIdMilliseconds());
	if (result.ec != std::errc())
		contentId.fill(0);
	return contentId;
}

//std::deque<std::shared_ptr<ClientNode>> RankedMatchQueue;
//std::deque<std::shared_ptr<ClientNode>> BorderMatchQueue;
std::deque<std::pair<std::shared_ptr<ClientNode>, std::chrono::steady_clock::time_point>> RankedMatchQueue;
std::deque<std::pair<std::shared_ptr<ClientNode>, std::chrono::steady_clock::time_point>> BorderMatchQueue;
std::recursive_mutex RankedQueueMutex;
std::recursive_mutex BorderQueueMutex;
void RequestEnterMatchRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
std::map<long, std::shared_ptr<RoomNode>> Rooms;
std::map<long, std::shared_ptr<RoomNode>> BorderRooms;
std::queue<int> recycledRoomIDs;  // 存储可复用的房间ID
HANDLE TimerQueue = CreateTimerQueue();
//BYTE chewangkaoyan[100] = { 0 };
BYTE shumiaoyunshu[100] = { 0 };
BYTE baofengtiaozhan[2048] = { 0 };
int baofengmap[5] = { 137,118,120,108,135 };
extern int obs;
void WINAPI OnDanceTimer(std::shared_ptr<RoomNode> Room);
extern UINT GetRandMap(short MapID);

bool BeginRoomGameLogicSession(const std::shared_ptr<RoomNode>& Room, const char* reason)
{
	if (!Room)
		return false;
	const USHORT previousGameID = Room->RaceGameID;
	const UINT envelopeContext = static_cast<UINT>(time(nullptr));
	if (envelopeContext == 0)
	{
		QQS_DebugPrintf10001("[FLOW][GAMELOGIC-ALLOC-FAIL] Reason=%s RoomID=%u PreviousGameID=%u Error=zero-envelope-context\n",
			reason ? reason : "", Room->ID, (unsigned)previousGameID);
		return false;
	}
	const USHORT gameID = AllocateGameLogicSessionID();
	if (gameID == 0)
	{
		QQS_DebugPrintf10001("[FLOW][GAMELOGIC-ALLOC-FAIL] Reason=%s RoomID=%u PreviousGameID=%u\n",
			reason ? reason : "", Room->ID, (unsigned)previousGameID);
		return false;
	}
	Room->RaceGameID = gameID;
	Room->RaceEnvelopeContext = envelopeContext;
	QQS_DebugPrintf10001("[FLOW][GAMELOGIC-BEGIN] Reason=%s RoomID=%u PreviousGameID=%u GameID=%u EnvelopeContext=%u\n",
		reason ? reason : "", Room->ID, (unsigned)previousGameID, (unsigned)gameID,
		Room->RaceEnvelopeContext);
	return true;
}

void ClearRoomGameLogicSession(const std::shared_ptr<RoomNode>& Room, const char* reason)
{
	if (!Room)
		return;
	const USHORT oldGameID = Room->RaceGameID;
	const UINT oldEnvelopeContext = Room->RaceEnvelopeContext;
	Room->RaceGameID = 0;
	Room->RaceEnvelopeContext = 0;
	if (oldGameID == 0 && oldEnvelopeContext == 0)
		return;
	QQS_DebugPrintf10001("[FLOW][GAMELOGIC-END] Reason=%s RoomID=%u GameID=%u EnvelopeContext=%u\n",
		reason ? reason : "", Room->ID, (unsigned)oldGameID, oldEnvelopeContext);
}
extern bool TryAdvanceCustomStageIfReady(std::shared_ptr<RoomNode> Room, const char* reason);
std::recursive_mutex Mutex;
extern std::recursive_mutex ClientMutex;
long SeqRoomID = 10000;
//extern DWORD WINAPI OnSingleOver(PVOID Client);
void NotifyChangeReadyState(std::shared_ptr<ClientNode> Client, UINT Uin, BYTE ReadyState);
void NotifyChangeSeatState(std::shared_ptr<ClientNode> Client, char SeatID, char SeatState);
void NotifyClientUpdatePlayerID(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
void ResponseStartQuickMatchGame2(std::shared_ptr<ClientNode> Client, USHORT matchQueueType, UINT MapID);
void ResponseStartQuickMatchGame2_Rank(std::shared_ptr<ClientNode> Client, USHORT matchQueueType);
extern std::shared_ptr<ClientNode> GetClient(long Uin);
extern int Borderset[6];
extern int beiginmode;
extern UINT QQS_GetConsumeVipLevelByCharmValue(UINT CharmValue);

static bool IsOfficialReturnRosterClient(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& RoomClient);

static const char* const QQS_BORDER_KART_PHYS_MISSING_REASON = "赛车物理数据缺失，请更换赛车后重试";

static bool PreflightBorderKartPhysEntry(
	const std::shared_ptr<ClientNode>& Client,
	const char* route,
	UINT* OutLoadID = nullptr)
{
	UINT resolvedLoadID = 0;
	if (PreflightKartPhysParamForPlayer(Client, &resolvedLoadID))
	{
		if (OutLoadID)
			*OutLoadID = resolvedLoadID;
		return true;
	}

	if (OutLoadID)
		*OutLoadID = resolvedLoadID;
	QQS_DebugPrintf10001(
		"[FLOW][BORDER-551-PREFLIGHT-ABORT] Route=%s Uin=%u KartID=%u SkinID=%u LoadID=%u -> no queue/no match room\n",
		route ? route : "", Client ? Client->Uin : 0,
		Client ? Client->KartID : 0, Client ? Client->KartSkinID : 0, resolvedLoadID);
	return false;
}

static bool QQS_FixedRoomNameEquals(const char* roomName, const char* expected)
{
	if (!roomName || !expected) return false;
	char safeName[MaxRoomName + 1] = { 0 };
	memcpy(safeName, roomName, MaxRoomName);
	safeName[MaxRoomName] = '\0';
	return strcmp(safeName, expected) == 0;
}

static CharmVipPlayerStateRow QQS_LoadCharmVipState(UINT Uin)
{
	CharmVipPlayerStateRow cvState;
	if (Uin != 0)
	{
		PlayerDB_CVLoadPlayerState(Uin, cvState, 1);
	}
	return cvState;
}

static UINT QQS_LoadRoomCharmValue(UINT Uin, std::shared_ptr<ClientNode> Client)
{
	if (Client && Client->Uin == Uin && Client->Charm > 0)
		return static_cast<UINT>(Client->Charm);

	if (Uin == 0)
		return 0;

	ClientInfo info[1];
	ClientInfo* pInfo = info;
	if (!PlayerDB_SelectClientInfo(Uin, pInfo))
		return 0;
	return info[0].Charm;
}

static UINT QQS_GetRoomConsumeVipLevel(UINT Uin, std::shared_ptr<ClientNode> Client)
{
	return QQS_GetConsumeVipLevelByCharmValue(QQS_LoadRoomCharmValue(Uin, Client));
}

static USHORT QQS_BuildOfficialRoomFlag(
	USHORT requestFlag,
	UINT nobleLevel,
	UINT guildVipLevel,
	UINT loverVipLevel,
	UINT consumeVipLevel)
{
	constexpr USHORT NobleFlag = 0x0004;
	constexpr USHORT GuildVipFlag = 0x0008;
	constexpr USHORT LoverVipFlag = 0x0080;
	constexpr USHORT ConsumeVipFlag = 0x0100;
	constexpr USHORT VipMask = NobleFlag | GuildVipFlag | LoverVipFlag | ConsumeVipFlag;

	// 贵族位只信任服务端登录态；密码、匹配、远程房等非贵族位仍保留请求值。
	USHORT roomFlag = static_cast<USHORT>(
		requestFlag & static_cast<USHORT>(~VipMask));

	if (nobleLevel > 0)
		roomFlag |= NobleFlag;
	if (guildVipLevel > 0)
		roomFlag |= GuildVipFlag;
	if (loverVipLevel > 0)
		roomFlag |= LoverVipFlag;
	if (consumeVipLevel > 0)
		roomFlag |= ConsumeVipFlag;

	return roomFlag;
}

static UINT QQS_GetCharmVipCareerLevel(const CharmVipPlayerStateRow& state)
{
	const CharmVipCareerDisplayState display = PlayerDB_CVResolveCareerDisplay(state);
	return static_cast<UINT>(display.CurVipLevel);
}

static BYTE QQS_GetCharmVipIconPreference(UINT ConsumeVipLevel, UINT CharmCareerLevel)
{
	// 0 优先生涯魅力达人图标，1 优先本月消费贵族图标。
	return CharmCareerLevel > 0 ? 0 : (ConsumeVipLevel > 0 ? 1 : 0);
}

bool IsBorderTeamMatchRoom(const std::shared_ptr<RoomNode>& Room)
{
	return Room &&
		(Room->BaseGameMode == 1 || Room->BaseGameMode == 2) &&
		Room->SubGameMode == 1 &&
		Room->CreateRoomType == 1 &&
		Room->ServerMatchType == 0 &&
		Room->TeamMatchType == 0;
}

static bool IsOfficialBorderTeamCreateRequest(
	USHORT BaseGameMode,
	USHORT SubGameMode,
	BYTE SeatNum)
{
	// 当前官服 Msg103 原始创建形态：Base/Sub=竞速普通、SeatNum=2 的组队壳。
	// create/server/team 三个扩展字段在当前紧凑 wire 中的映射尚未闭合，不能用于分类。
	// 服务端接收后再归一化成内部 6 人等待房状态。
	return (BaseGameMode == 1 || BaseGameMode == 2) &&
		SubGameMode == 1 &&
		SeatNum == 2;
}

static void ApplyOfficial6BorderTeamRoomState(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
	{
		return;
	}
	Room->SeatNum = 6;
	Room->Flag = 0x0600;
	Room->CreateRoomType = 1;
	Room->ServerMatchType = 0;
	Room->TeamMatchType = 0;
}

static void NormalizeOfficial6BorderTeamRoom(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room || !IsBorderTeamMatchRoom(Room))
	{
		return;
	}
	// 保留官服组队匹配身份(Base=1/2 + CreateRoomType/ServerMatchType/TeamMatchType)，
	// 保留 6 人等待房，但 Flag 使用官服边境组队强特征，避免返回 104 后客户端按普通房重建 UI。
	ApplyOfficial6BorderTeamRoomState(Room);
}

static bool IsBorderTeamRoomBusyForEnter(const std::shared_ptr<RoomNode>& Room)
{
	if (!IsBorderTeamMatchRoom(Room))
	{
		return false;
	}
	if (Room->Callback != NULL || Room->startStatus != 1)
	{
		return true;
	}
	for (char i = 0; i < 6; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (!RoomClient)
		{
			continue;
		}
		// Official border-team starts through 20210, which moves members into
		// BorderMatchQueue/BorderRoom but leaves their original RoomID in this
		// team lobby. While any original member is queued or racing, this lobby
		// must reject 104 late-join requests; otherwise FE_ROOM enter/seat/ready
		// notifies are broadcast to racers and can cause mid-race camera snaps.
		if (RoomClient->TaskID == 0xFFFD || RoomClient->BorderRoomID != 0)
		{
			return true;
		}
	}
	return false;
}

static bool IsLiveRoomClientForOwner(const std::shared_ptr<ClientNode>& RoomClient)
{
	if (!RoomClient)
		return false;
	if (RoomClient->Uin == 0 || RoomClient->TransportConnID == 0)
		return false;
	if (!RoomClient->IsLogin || RoomClient->deleteflag)
		return false;
	std::shared_ptr<ClientNode> LiveClient = GetClient(RoomClient->Uin);
	return LiveClient == RoomClient;
}

static void RemoveFromBorderMatchQueue_NoLock(const std::shared_ptr<ClientNode>& Client)
{
	if (!Client)
	{
		return;
	}
	BorderMatchQueue.erase(std::remove_if(BorderMatchQueue.begin(), BorderMatchQueue.end(),
		[Client](const std::pair<std::shared_ptr<ClientNode>, std::chrono::steady_clock::time_point>& entry)
		{
			return entry.first && entry.first->Uin == Client->Uin;
		}), BorderMatchQueue.end());
}

static bool IsClientValidForMatchQueue(const std::shared_ptr<ClientNode>& Client, UINT expectedTaskID)
{
	if (!Client)
	{
		return false;
	}
	if (!Client->IsLogin || Client->deleteflag || Client->Uin == 0 || Client->TransportConnID == 0)
	{
		return false;
	}
	// 队列只能保留当前活动会话。旧连接对象即使 IsLogin 尚未及时清零，
	// 也不能再次被分配进下一局，否则会形成“幽灵”参赛槽位。
	if (GetClient(Client->Uin) != Client)
	{
		return false;
	}
	if (Client->TaskID != expectedTaskID)
	{
		return false;
	}
	// 已经被分配到比赛房的玩家不能继续留在等待队列里。
	if (Client->BorderRoomID != 0)
	{
		return false;
	}
	return true;
}

static bool IsQueuedBorderTeamMember_NoLock(const std::shared_ptr<ClientNode>& Client, USHORT* teamRoomID = nullptr)
{
	if (!IsClientValidForMatchQueue(Client, 0xFFFD))
		return false;
	if (Client->RoomID == 0 || !Client->CurrentRoom)
		return false;

	std::shared_ptr<RoomNode> TeamRoom = GetRoom(Client->RoomID);
	if (!IsBorderTeamMatchRoom(TeamRoom))
		return false;
	if (Client->CurrentRoom != TeamRoom.get())
		return false;
	if (Client->SeatID < 0 || Client->SeatID >= ROOM_SEAT_NORMAL_MAX)
		return false;
	if (TeamRoom->Player[Client->SeatID] != Client)
		return false;

	if (teamRoomID)
		*teamRoomID = (USHORT)TeamRoom->ID;
	return true;
}

static bool BorderRosterContainsClient(const std::vector<std::shared_ptr<ClientNode>>& roster, const std::shared_ptr<ClientNode>& Client)
{
	if (!Client)
		return false;
	for (auto& RosterClient : roster)
	{
		if (RosterClient && RosterClient->Uin == Client->Uin)
			return true;
	}
	return false;
}

static void RemoveBorderRosterFromQueue_NoLock(const std::vector<std::shared_ptr<ClientNode>>& roster)
{
	for (auto& Client : roster)
	{
		RemoveFromBorderMatchQueue_NoLock(Client);
	}
}

static int GetBorderMatchRequiredPlayerNum()
{
	int required = Borderset[5];
	if (required < 1)
		required = ROOM_SEAT_NORMAL_MAX;
	if (required > ROOM_SEAT_NORMAL_MAX)
		required = ROOM_SEAT_NORMAL_MAX;
	return required;
}

static bool TryBuildBorderTeamMixedRoster_NoLock(std::vector<std::shared_ptr<ClientNode>>& outRoster)
{
	outRoster.clear();
	const int requiredPlayers = GetBorderMatchRequiredPlayerNum();

	for (auto& entry : BorderMatchQueue)
	{
		USHORT teamRoomID = 0;
		if (!IsQueuedBorderTeamMember_NoLock(entry.first, &teamRoomID))
			continue;

		std::vector<std::shared_ptr<ClientNode>> teamMembers;
		std::vector<std::shared_ptr<ClientNode>> singles;
		for (auto& scanEntry : BorderMatchQueue)
		{
			std::shared_ptr<ClientNode> scanClient = scanEntry.first;
			if (!IsClientValidForMatchQueue(scanClient, 0xFFFD))
				continue;

			USHORT scanTeamRoomID = 0;
			if (IsQueuedBorderTeamMember_NoLock(scanClient, &scanTeamRoomID))
			{
				if (scanTeamRoomID == teamRoomID && !BorderRosterContainsClient(teamMembers, scanClient))
					teamMembers.push_back(scanClient);
				continue;
			}

			if (!BorderRosterContainsClient(singles, scanClient))
				singles.push_back(scanClient);
		}

		if (teamMembers.size() < 2)
			continue;
		if (teamMembers.size() > ROOM_SEAT_NORMAL_MAX)
			continue;

		std::sort(teamMembers.begin(), teamMembers.end(),
			[](const std::shared_ptr<ClientNode>& a, const std::shared_ptr<ClientNode>& b)
			{
				return (a ? a->SeatID : 99) < (b ? b->SeatID : 99);
			});

		outRoster = teamMembers;
		for (auto& singleClient : singles)
		{
			if ((int)outRoster.size() >= ROOM_SEAT_NORMAL_MAX)
				break;
			outRoster.push_back(singleClient);
		}

		if ((int)outRoster.size() >= requiredPlayers)
		{
			RemoveBorderRosterFromQueue_NoLock(outRoster);
			QQS_DebugPrintf10001("[FLOW][BORDER-MATCH-TEAM-MIX] TeamRoomID=%u TeamMembers=%zu Singles=%zu Roster=%zu Minimum=%d Capacity=%d -> dynamic fill, atomic team preserved\n",
				(unsigned)teamRoomID, teamMembers.size(), outRoster.size() > teamMembers.size() ? outRoster.size() - teamMembers.size() : 0,
				outRoster.size(), requiredPlayers, ROOM_SEAT_NORMAL_MAX);
			return true;
		}

		outRoster.clear();
	}

	return false;
}

static bool TryBuildBorderSingleRoster_NoLock(std::vector<std::shared_ptr<ClientNode>>& outRoster)
{
	outRoster.clear();
	const int requiredPlayers = GetBorderMatchRequiredPlayerNum();
	for (auto& entry : BorderMatchQueue)
	{
		std::shared_ptr<ClientNode> Client = entry.first;
		if (!IsClientValidForMatchQueue(Client, 0xFFFD))
			continue;
		if (IsQueuedBorderTeamMember_NoLock(Client))
			continue;
		if (!BorderRosterContainsClient(outRoster, Client))
			outRoster.push_back(Client);
		if ((int)outRoster.size() >= ROOM_SEAT_NORMAL_MAX)
			break;
	}

	if ((int)outRoster.size() < requiredPlayers)
	{
		outRoster.clear();
		return false;
	}

	RemoveBorderRosterFromQueue_NoLock(outRoster);
	QQS_DebugPrintf10001("[FLOW][BORDER-MATCH-SINGLE] Singles=%zu Minimum=%d Capacity=%d -> dynamic fill\n",
		outRoster.size(), requiredPlayers, ROOM_SEAT_NORMAL_MAX);
	return true;
}

static bool TryBuildBorderMatchRoster_NoLock(std::vector<std::shared_ptr<ClientNode>>& outRoster)
{
	return TryBuildBorderTeamMixedRoster_NoLock(outRoster) || TryBuildBorderSingleRoster_NoLock(outRoster);
}

static void PruneInvalidMatchQueue_NoLock(
	std::deque<std::pair<std::shared_ptr<ClientNode>, std::chrono::steady_clock::time_point>>& queue,
	UINT expectedTaskID,
	const char* tag)
{
	for (auto it = queue.begin(); it != queue.end();)
	{
		if (!IsClientValidForMatchQueue(it->first, expectedTaskID))
		{
			auto Client = it->first;
			QQS_DebugPrintf10001("[FLOW][MATCH-QUEUE-PRUNE] Tag=%s Uin=%u TransportConnID=%u TaskID=%u BorderRoomID=%u\n",
				tag ? tag : "",
				Client ? Client->Uin : 0,
				Client ? Client->TransportConnID : 0,
				Client ? Client->TaskID : 0,
				Client ? Client->BorderRoomID : 0);
			it = queue.erase(it);
		}
		else
		{
			++it;
		}
	}
}

static bool IsValidBorderTeamMemberForStart(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& RoomClient, char seatIndex)
{
	if (!Room || !RoomClient)
		return false;
	if (seatIndex < 0 || seatIndex >= 6)
		return false;
	if (RoomClient->Uin == 0)
		return false;
	if (RoomClient->TransportConnID == 0)
		return false;
	std::shared_ptr<ClientNode> LiveClient = GetClient(RoomClient->Uin);
	if (LiveClient != RoomClient)
		return false;
	if (!RoomClient->IsLogin || RoomClient->deleteflag)
		return false;
	if (RoomClient->OBState)
		return false;
	if (RoomClient->RoomID != Room->ID)
		return false;
	if (RoomClient->BorderRoomID != 0)
		return false;
	if (RoomClient->SeatID < 0 || RoomClient->SeatID >= 6)
		return false;
	if (RoomClient->SeatID != seatIndex)
		return false;
	if (Room->Player[RoomClient->SeatID] != RoomClient)
		return false;
	if (Room->Player[seatIndex] != RoomClient)
		return false;
	return true;
}

static bool IsBorderRacePlayerForRoomLocal(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& RoomClient)
{
	if (!Room || !RoomClient)
		return false;
	if (RoomClient->Uin == 0)
		return false;
	if (RoomClient->TransportConnID == 0)
		return false;
	std::shared_ptr<ClientNode> LiveClient = GetClient(RoomClient->Uin);
	if (LiveClient != RoomClient)
		return false;
	if (!RoomClient->IsLogin || RoomClient->deleteflag)
		return false;
	if (RoomClient->OBState)
		return false;
	if (RoomClient->BorderRoomID != Room->ID)
		return false;
	if (RoomClient->CurrentRoom != Room.get())
		return false;
	if (RoomClient->TaskID != 0xFFFD)
		return false;
	if (RoomClient->GameID == 0)
		return false;
	return true;
}

static bool IsTrackedBorderRaceRoomLocal(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room || Room->matchQueueType != 0)
		return false;
	std::shared_ptr<RoomNode> BorderRoom = GetBorderRoom(Room->ID);
	return BorderRoom && BorderRoom == Room;
}

static void NotifyBorderTeamMatchQueueAck(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room)
{
	if (!Client || !Room)
	{
		return;
	}

	std::vector<BYTE> buf(32); BYTE* p = buf.data(); size_t len;
	Write16(p, (USHORT)Room->ID); // team room id, official pcaps/9 20213 ack first field
	Write16(p, 0);                // official border-team sample uses 0 here
	Write32(p, Client->Uin);       // player identity

	len = p - buf.data();
	QQS_DebugPrintf10001("[FLOW][20213-BORDER-TEAM] ack Uin=%u TeamRoomID=%u PayloadLen=%zu WireBodyLen=%zu\n",
		Client->Uin, Room->ID, len, len + 2);
	// pcaps/9 official 20213 header is FE_PLAYER/ServerID, while the body carries the team room id.
	// Do not route this notify as FE_ROOM, otherwise the client may treat the returned team room incorrectly.
	SendToClient(Client, 20213, buf.data(), len, Client->ServerID, FE_PLAYER, Client->ServerID, Notify);
}

extern void WINAPI OnBegin(std::shared_ptr<RoomNode> Room);
extern int mode28346;
extern int GMTime[4];
extern std::list<UINT> modecar;

extern bool NotifyGameNPCInfo(std::shared_ptr<ClientNode> Client);
extern void RequestEnter3DRoom(std::shared_ptr<ClientNode> Client);
extern void NoticeClient(std::shared_ptr<ClientNode> Client, std::string news);
//void NotifyChangeRoomOwner(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
extern UINT DanceTime;
extern UINT SyncTime;
extern std::map<long, std::shared_ptr<ClientNode>> Clients;
extern UINT NpcNum;
extern int SkipDancetime;
extern std::string week;
extern int Stake[2];
extern void virtualsend(std::shared_ptr<ClientNode> Client, std::vector<ItemInfo> Items, short count, UINT itemid);
extern void NotifyUpdateKartSkillStoneInfoConfig(std::shared_ptr<ClientNode> Client, int KartID);
extern bool isLock(UINT Itemid);
extern std::shared_ptr<ClientNode> GetClient(long Uin);
extern void ResponseChangeOBState(std::shared_ptr<ClientNode> Client, BOOL OBState);
extern void NotifyChangeOBState(std::shared_ptr<ClientNode> Client, BOOL OBState, UINT Uin);
extern int SendAnyItem(std::shared_ptr<ClientNode> Client, char cdk[]);
extern std::string GetItemName(UINT Itemid);
extern int Borderset[6];
extern int BorderManTime;
extern std::set<UINT> ClothesGMUins;  //只能刷衣服，旧版/原有组
extern std::set<UINT> CarGMUins;  //只能刷车，旧版/原有组
extern std::set<UINT> NewClothesGMUins;  //新版只能刷衣服
extern std::set<UINT> NewCarGMUins;  //新版只能刷车
extern std::set<UINT> NewClothesCarGMUins;  //新版只能刷衣服+赛车
extern std::set<UINT> AdminUins;//赌注模式管理 
extern void Reloadracingdata();
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
extern std::string GetItemMess(UINT ID);
extern int IsBottomGame[4];
extern int IsBottomGame4or6[2];
extern int LimitTalk;
extern std::string GMMess;
extern int Mantissa[4];
extern int Uinew;
extern double version;

static std::vector<std::shared_ptr<ClientNode>> GetBottomSuperMoneyRacePlayers(
	const std::shared_ptr<RoomNode>& Room)
{
	std::vector<std::shared_ptr<ClientNode>> players;
	if (!Room)
		return players;
	for (const auto& player : Room->Player)
	{
		if (!player || player->Uin == 0 || player->OBState || player->deleteflag || !player->IsLogin)
			continue;
		players.push_back(player);
	}
	return players;
}

static bool BottomSuperMoneyHasInvalidOrDuplicateIp(
	const std::vector<std::shared_ptr<ClientNode>>& players)
{
	std::unordered_set<std::string> ips;
	for (const auto& player : players)
	{
		const size_t length = strnlen_s(player->Ip, sizeof(player->Ip));
		if (length == 0 || length >= sizeof(player->Ip))
			return true;
		if (!ips.emplace(player->Ip, length).second)
			return true;
	}
	return false;
}

static std::string BuildBottomSuperMoneyNotice(UINT mode)
{
	const BottomSuperMoneyModeConfig* config = GetBottomSuperMoneyModeConfig(mode);
	if (!config)
		return "点券垫底赛配置无效";
	const std::uint32_t fee = static_cast<std::uint32_t>(
		static_cast<std::uint64_t>(config->TotalCost) * g_BottomSuperMoneyConfig.FeePercent / 100u);
	const std::vector<std::uint32_t> rewards =
		SplitBottomGameAmount(config->TotalCost - fee, config->RewardPercent);
	const std::vector<std::uint32_t> costs =
		SplitBottomGameAmount(config->TotalCost, config->LoserPercent);
	if (mode == 4 && rewards.size() == 3 && costs.size() == 1)
	{
		return "已开启四人点券垫底赛！总扣/C01【" + std::to_string(config->TotalCost) +
			"】/C00，手续费/C01【" + std::to_string(fee) + "】/C00；奖励1-3名/C01【" +
			std::to_string(rewards[0]) + "、" + std::to_string(rewards[1]) + "、" +
			std::to_string(rewards[2]) + "】/C00；扣款4名/C01【" + std::to_string(costs[0]) +
			"】/C00；同IP禁赛。";
	}
	if (mode == 6 && rewards.size() == 4 && costs.size() == 2)
	{
		return "已开启六人点券垫底赛！总扣/C01【" + std::to_string(config->TotalCost) +
			"】/C00，手续费/C01【" + std::to_string(fee) + "】/C00；奖励1-4名/C01【" +
			std::to_string(rewards[0]) + "、" + std::to_string(rewards[1]) + "、" +
			std::to_string(rewards[2]) + "、" + std::to_string(rewards[3]) +
			"】/C00；扣款5-6名/C01【" + std::to_string(costs[0]) + "、" +
			std::to_string(costs[1]) + "】/C00；同IP禁赛。";
	}
	return "点券垫底赛配置无效";
}

static void SetBottomSuperMoneyAnonymousState(const std::shared_ptr<RoomNode>& Room, bool enabled)
{
	if (!Room)
		return;
	Room->IsAnonymous = enabled ? 1 : 0;
	for (const auto& player : Room->Player)
	{
		if (!player)
			continue;
		player->IsAnonymous = enabled ? 1 : 0;
		player->IsBottomGame = enabled ? 1 : 0;
	}
}


bool IsCustomRoomFlow(const std::shared_ptr<RoomNode>& Room)
{
	return Room && (Room->HasCustomScheme || Room->SpeFlag == 9);
}

bool IsTwelvePlayerRoom(const std::shared_ptr<RoomNode>& Room)
{
	// 12P 必须是建房时明确打上的房壳标记，不能再用 SeatNum/PresetOBNum 猜。
	// 普通自定义赛后可能被旧链路/客户端字段带出 SeatNum=12、PresetOB=11；
	// 如果这里按 SeatNum==12 误判，就会把普通 6 人自定义回房发成 12P 壳，
	// 结果就是座位错位、自己看不到房主。
	return Room && Room->TwelvePlayerCustomRoomShell;
}

int GetRoomSeatLimitBySeatNum(BYTE SeatNum)
{
	// 不用裸 SeatNum 推断 12P。12P 只由 IsTwelvePlayerRoom(Room) 进入。
	// 这样普通自定义即使残留 SeatNum=12，也会按 6 人房壳恢复。
	return ROOM_SEAT_NORMAL_MAX;
}

int GetRoomSeatLimit(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return ROOM_SEAT_NORMAL_MAX;
	return IsTwelvePlayerRoom(Room) ? ROOM_SEAT_12_MAX : GetRoomSeatLimitBySeatNum(Room->SeatNum);
}

int GetRoomSeatCapacity(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return ROOM_SEAT_NORMAL_MAX;
	if (IsTwelvePlayerRoom(Room))
		return ROOM_SEAT_12_MAX;
	int limit = GetRoomSeatLimit(Room);
	int seatNum = Room->SeatNum ? Room->SeatNum : limit;
	if (seatNum < 1) seatNum = limit;
	if (seatNum > limit) seatNum = limit;
	return seatNum;
}

RoomSeatMask GetRoomSeatBit(int seat)
{
	if (seat < 0 || seat >= ROOM_SEAT_MAX)
		return 0;
	return (RoomSeatMask)(1u << seat);
}

int CountRoomPlayers(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return 0;
	int limit = GetRoomSeatLimit(Room);
	int count = 0;
	for (int i = 0; i < limit && i < (int)Room->Player.size(); i++)
	{
		if (Room->Player[i])
			count++;
	}
	return count;
}

static USHORT GetClientConnID16(const std::shared_ptr<ClientNode>& Client)
{
	return GetClientWirePlayerID16(Client);
}

USHORT GetRoomConnPlayerID(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& Client)
{
	if (!Room || !Client)
		return 0;
	const USHORT clientWireID = GetClientConnID16(Client);
	if (clientWireID == 0)
		return 0;

	int seatLimit = GetRoomSeatLimit(Room);
	for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient == Client)
		{
			return clientWireID;
		}
	}
	return 0;
}

USHORT GetRoomConnOwnerID(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return 0;
	int seatLimit = GetRoomSeatLimit(Room);
	USHORT owner = Room->RoomOwnerID;
	// Protocol-visible RoomOwnerID / PlayerID / NewOwnerID must be non-zero 16-bit ids.
	// Real HPSocket ConnID is still used for network send/close, but it can exceed 65535.
	if (Room->RoomOwnerUin != 0)
	{
		for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); i++)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			if (IsLiveRoomClientForOwner(RoomClient) && RoomClient->Uin == Room->RoomOwnerUin)
				return GetClientConnID16(RoomClient);
		}
		// 官服 LuckyMatch/大奖赛切服重连按 Uin/座位保留业务房主；
		// 旧 TCP 会话关闭期间不能把房主临时转给其它在线玩家。
		if (Room->LuckyMatchInternalRoom)
			return IsValidWireConnID16(owner) ? owner : 0;
	}
	for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (!IsLiveRoomClientForOwner(RoomClient))
			continue;
		USHORT wireID = GetClientConnID16(RoomClient);
		if (owner != 0 && owner == wireID)
			return wireID;
	}
	// Compatibility for rooms already polluted by the previous seat-index owner model.
	if (owner >= 1 && owner <= (USHORT)seatLimit)
	{
		int idx = owner - 1;
		if (idx >= 0 && idx < (int)Room->Player.size() && IsLiveRoomClientForOwner(Room->Player[idx]))
			return GetClientConnID16(Room->Player[idx]);
	}
	for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (IsLiveRoomClientForOwner(RoomClient))
			return GetClientConnID16(RoomClient);
	}
	return 0;
}

bool IsRoomConnOwner(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& Client)
{
	USHORT playerID = GetRoomConnPlayerID(Room, Client);
	if (playerID == 0)
		return false;
	return playerID == GetRoomConnOwnerID(Room);
}


static short GetRoomWireDstIDForClient(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& Client)
{
	(void)Room;
	const USHORT wireID = GetClientConnID16(Client);
	// 官服 103/104/500/504 包头 DstID 走客户端连接路由 ID。
	// 这个值不是座位号；房间内 PlayerID/RoomOwnerID 也统一使用同一个 ConnID16。
	return static_cast<short>(wireID != 0 ? wireID : 1);
}


static bool IsNormalRoomVisible(const std::shared_ptr<RoomNode>& Room)
{
	return Room && !Room->LuckyMatchInternalRoom;
}

void ScrubClientFromRoomsForDisconnect(const std::shared_ptr<ClientNode>& Client, long exceptRoomId, const char* reason)
{
	if (!Client)
	{
		return;
	}

	const DWORD uin = Client->Uin;
	const DWORD connId = Client->TransportConnID;
	const USHORT protocolConnID = Client->ConnID;
	if (connId == 0 && protocolConnID == 0)
	{
		return;
	}

	std::lock_guard<std::recursive_mutex> lg(Mutex);
	for (auto& kv : Rooms)
	{
		std::shared_ptr<RoomNode> Room = kv.second;
		if (!Room || (exceptRoomId != 0 && Room->ID == (UINT)exceptRoomId))
		{
			continue;
		}

		const int seatLimit = GetRoomSeatLimit(Room);
		bool changed = false;
		bool ownerRemoved = false;
		BYTE activeCount = 0;

		for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); ++i)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			const bool exactSession =
				RoomClient == Client ||
				(RoomClient && connId != 0 && protocolConnID != 0 && RoomClient->TransportConnID == connId && RoomClient->ConnID == protocolConnID);
			if (exactSession)
			{
				if (IsRoomConnOwner(Room, RoomClient) || (Room->RoomOwnerUin != 0 && Room->RoomOwnerUin == RoomClient->Uin))
				{
					ownerRemoved = true;
				}
				Room->Player[i].reset();
				if (i < ROOM_SEAT_MAX)
				{
					Room->SeatState[i] = 1;
				}
				changed = true;
				continue;
			}

			if (RoomClient)
			{
				++activeCount;
			}
		}

		if (!changed)
		{
			continue;
		}

		Room->CurrentPlayerNum = activeCount;
		if (ownerRemoved)
		{
			Room->RoomOwnerID = 0;
			Room->RoomOwnerUin = 0;
			for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); ++i)
			{
				if (Room->Player[i])
				{
					Room->RoomOwnerUin = Room->Player[i]->Uin;
					Room->RoomOwnerID = GetRoomConnPlayerID(Room, Room->Player[i]);
					break;
				}
			}
		}

		QQS_DebugPrintf10001("[FLOW][ROOM-DISCONNECT-CLEAN] Reason=%s Uin=%u TransportConnID=%u ProtocolID=%u RoomID=%u Active=%u OwnerUin=%u OwnerID=%u Lucky=%u\n",
			reason ? reason : "", uin, connId, (unsigned)protocolConnID, Room->ID, (unsigned)Room->CurrentPlayerNum,
			Room->RoomOwnerUin, (unsigned)Room->RoomOwnerID, Room->LuckyMatchInternalRoom ? 1u : 0u);
	}
}

static void ScrubClientFromRoomsForNormalFlow(const std::shared_ptr<ClientNode>& Client, long exceptRoomId, const char* reason)
{
	if (!Client)
	{
		return;
	}

	const DWORD uin = Client->Uin;
	const DWORD connId = Client->TransportConnID;
	if (uin == 0 && connId == 0)
	{
		return;
	}

	std::lock_guard<std::recursive_mutex> lg(Mutex);
	for (auto& kv : Rooms)
	{
		std::shared_ptr<RoomNode> Room = kv.second;
		if (!Room || (exceptRoomId != 0 && Room->ID == (UINT)exceptRoomId))
		{
			continue;
		}

		const int seatLimit = GetRoomSeatLimit(Room);
		bool changed = false;
		bool ownerRemoved = false;
		BYTE activeCount = 0;

		for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); ++i)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			if (RoomClient &&
				((connId != 0 && RoomClient->TransportConnID == connId) ||
				 (uin != 0 && RoomClient->Uin == uin)))
			{
				if (IsRoomConnOwner(Room, RoomClient))
				{
					ownerRemoved = true;
				}
				Room->Player[i].reset();
				if (i < ROOM_SEAT_MAX)
				{
					Room->SeatState[i] = 1;
				}
				changed = true;
				continue;
			}

			if (RoomClient)
			{
				++activeCount;
			}
		}

		if (!changed)
		{
			continue;
		}

		Room->CurrentPlayerNum = activeCount;
		if (ownerRemoved)
		{
			Room->RoomOwnerID = 0;
			Room->RoomOwnerUin = 0;
			for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); ++i)
			{
				if (Room->Player[i])
				{
					Room->RoomOwnerUin = Room->Player[i]->Uin;
					Room->RoomOwnerID = GetRoomConnPlayerID(Room, Room->Player[i]);
					break;
				}
			}
		}

		QQS_DebugPrintf10001("[FLOW][ROOM-GHOST-CLEAN] Reason=%s Uin=%u TransportConnID=%u RoomID=%u Active=%u Lucky=%u\n",
			reason ? reason : "", uin, connId, Room->ID, (unsigned)Room->CurrentPlayerNum,
			Room->LuckyMatchInternalRoom ? 1u : 0u);
	}
}

static UINT GetRoomStartDanceDelayMs(const std::shared_ptr<RoomNode>& Room)
{
	if (Room && Room->isSkip == 1)
		return SkipDancetime > 0 ? (UINT)SkipDancetime : 0;
	return DanceTime;
}

static bool IsReturnedRaceClientForCustomStart(const std::shared_ptr<ClientNode>& RoomClient)
{
	if (!RoomClient)
		return true;
	return RoomClient->GameID == 0 &&
		RoomClient->isRace == 0 &&
		RoomClient->LastRoomReturnTick != 0;
}

static bool IsCustomRaceStartEligibleClient(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& RoomClient, char seatIndex)
{
	if (!Room || !RoomClient)
		return false;
	if (!RoomClient->IsLogin || RoomClient->deleteflag || RoomClient->Uin == 0 || RoomClient->OBState)
		return false;
	if (IsReturnedRaceClientForCustomStart(RoomClient))
		return false;
	if (RoomClient->GameID == 0)
		return false;
	if (RoomClient->RoomID != Room->ID || RoomClient->CurrentRoom != Room.get())
		return false;
	int seatLimit = GetRoomSeatLimit(Room);
	if (seatIndex < 0 || seatIndex >= seatLimit)
		return false;
	if (Room->RaceActiveMask != 0 && (Room->RaceActiveMask & GetRoomSeatBit(seatIndex)) == 0)
		return false;
	return true;
}

static bool HasCustomRaceStartEligibleClient(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return false;
	int seatLimit = GetRoomSeatLimit(Room);
	for (char si = 0; si < seatLimit && si < (int)Room->Player.size(); si++)
	{
		if (IsCustomRaceStartEligibleClient(Room, Room->Player[si], si))
			return true;
	}
	return false;
}

static bool IsCustomRaceSnapshotBasicClient(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& RoomClient, char seatIndex)
{
	if (!Room || !RoomClient)
		return false;
	if (!RoomClient->IsLogin || RoomClient->deleteflag || RoomClient->Uin == 0 || RoomClient->OBState)
		return false;
	int seatLimit = GetRoomSeatLimit(Room);
	if (seatIndex < 0 || seatIndex >= seatLimit)
		return false;
	return true;
}

void ClearCustomRaceStartSnapshot(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room)
		return;
	if (Room->CustomRaceSnapshotValid || !Room->CustomRaceStartSnapshot.empty() ||
		Room->CustomRaceKartPhysReadyMask != 0 || Room->CustomRaceKartPhysMissingMask != 0)
	{
		QQS_DebugPrintf10001("[FLOW][CUSTOM-SNAPSHOT-CLEAR] RoomID=%u Reason=%s SnapshotNum=%u SnapshotMask=0x%04X PhysReady=0x%04X PhysMissing=0x%04X\n",
			Room->ID, reason ? reason : "", Room->CustomRaceSnapshotPlayerNum,
			(unsigned)Room->CustomRaceSnapshotMask,
			(unsigned)Room->CustomRaceKartPhysReadyMask,
			(unsigned)Room->CustomRaceKartPhysMissingMask);
	}
	Room->CustomRaceSnapshotValid = false;
	Room->CustomRaceSnapshotRoomID = 0;
	Room->CustomRaceSnapshotPlayerNum = 0;
	Room->CustomRaceSnapshotMask = 0;
	Room->CustomRaceSnapshotTick = 0;
	Room->CustomRaceKartPhysAllReady = false;
	Room->CustomRaceKartPhysReadyMask = 0;
	Room->CustomRaceKartPhysMissingMask = 0;
	Room->CustomRaceStartSnapshot.clear();
}

static BYTE PruneCustomRaceStartSnapshotToMask(std::shared_ptr<RoomNode> Room, RoomSeatMask keepMask, const char* reason)
{
	if (!Room)
		return 0;

	std::vector<CustomRaceStartSnapshotEntry> kept;
	kept.reserve(Room->CustomRaceStartSnapshot.size());
	int seatLimit = GetRoomSeatLimit(Room);
	for (const auto& entry : Room->CustomRaceStartSnapshot)
	{
		if (entry.SeatID < 0 || entry.SeatID >= seatLimit)
			continue;
		RoomSeatMask bit = GetRoomSeatBit(entry.SeatID);
		if ((keepMask & bit) == 0)
			continue;
		kept.push_back(entry);
	}

	BYTE keptCount = (BYTE)kept.size();
	RoomSeatMask oldSnapshotMask = Room->CustomRaceSnapshotMask;
	BYTE oldSnapshotNum = Room->CustomRaceSnapshotPlayerNum;
	Room->CustomRaceStartSnapshot.swap(kept);
	Room->CustomRaceSnapshotMask = keepMask;
	Room->CustomRaceSnapshotPlayerNum = keptCount;
	Room->CustomRaceSnapshotValid = keptCount > 0;
	Room->CustomRaceKartPhysReadyMask = (RoomSeatMask)(Room->CustomRaceKartPhysReadyMask & keepMask);
	Room->CustomRaceKartPhysMissingMask = (RoomSeatMask)(Room->CustomRaceKartPhysMissingMask & keepMask);
	Room->CustomRaceKartPhysAllReady =
		keepMask != 0 &&
		(Room->CustomRaceKartPhysReadyMask & keepMask) == keepMask &&
		Room->CustomRaceKartPhysMissingMask == 0;

	QQS_DebugPrintf10001("[FLOW][CUSTOM-SNAPSHOT-PRUNE] RoomID=%u Reason=%s OldNum=%u NewNum=%u OldMask=0x%04X NewMask=0x%04X PhysReady=0x%04X PhysMissing=0x%04X\n",
		Room->ID, reason ? reason : "", oldSnapshotNum, keptCount,
		(unsigned)oldSnapshotMask, (unsigned)Room->CustomRaceSnapshotMask,
		(unsigned)Room->CustomRaceKartPhysReadyMask,
		(unsigned)Room->CustomRaceKartPhysMissingMask);
	return keptCount;
}

static void AbortCustomRaceStartAfterReturn(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room)
		return;
	QQS_DebugPrintf10001("[FLOW][CUSTOM-START-ABORT-RETURNED] Reason=%s RoomID=%u startStatus=%d ActiveMask=0x%04X LastS2C=%u\n",
		reason ? reason : "", Room->ID, Room->startStatus,
		(unsigned)Room->RaceActiveMask, (unsigned)Room->CustomLastSentMsgID);
	Room->RaceStartPlayerNum = 0;
	Room->RaceActiveMask = 0;
	Room->CustomStageArriveMask = 0;
	Room->CustomStageArriveCount = 0;
	Room->CustomStageArriveNeed = 0;
	Room->CustomLoadReadyMask = 0;
	Room->CustomWaitingPrepareReadyBeforeLoad = false;
	Room->CustomPreloadReadySatisfied = false;
	Room->CustomPreloadReadyMask = 0;
	Room->CustomWaitingRaceBegin = false;
	Room->CustomRaceBeginSent = false;
	Room->CustomLoadingDispatching = false;
	Room->CustomLoadingStartTick = 0;
	ClearCustomRaceStartSnapshot(Room, reason ? reason : "custom-start-abort");
	ClearRoomGameLogicSession(Room, reason ? reason : "custom-start-abort");
	Room->startStatus = 1;
	Room->Callback = NULL;
}

void ScheduleCustomRaceBeginWithDance(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!IsCustomRoomFlow(Room))
		return;
	if (Room->startStatus != 2 || !HasCustomRaceStartEligibleClient(Room))
	{
		AbortCustomRaceStartAfterReturn(Room, reason ? reason : "custom-schedule-511-invalid");
		return;
	}
	if (Room->CustomRaceBeginSent)
	{
		QQS_DebugPrintf10001("[FLOW][CUSTOM-511-DANCE] skip RoomID=%u already scheduled/sent 511 Reason=%s LastS2C=%u\n",
			Room->ID, reason ? reason : "", Room->CustomLastSentMsgID);
		return;
	}
	if (Room->HasCustomScheme)
	{
		RoomSeatMask activeMask = Room->RaceActiveMask ? Room->RaceActiveMask : Room->CustomRaceSnapshotMask;
		RoomSeatMask readyMask = Room->CustomLoadReadyMask & activeMask;
		RoomSeatMask missingMask = (RoomSeatMask)(activeMask & (RoomSeatMask)~readyMask);
		if (activeMask == 0 || missingMask != 0)
		{
			QQS_DebugPrintf10001("[FLOW][CUSTOM-511-BLOCK-120] RoomID=%u Reason=%s ActiveMask=0x%04X ReadyMask=0x%04X MissingMask=0x%04X LastS2C=%u -> no 511\n",
				Room->ID, reason ? reason : "", (unsigned)activeMask,
				(unsigned)readyMask, (unsigned)missingMask,
				Room->CustomLastSentMsgID);
			Room->CustomWaitingRaceBegin = false;
			Room->CustomRaceBeginSent = false;
			Room->CustomLoadingDispatching = false;
			Room->Callback = NULL;
			Room->startStatus = 1;
			return;
		}
	}
	if (Room->HasCustomScheme && (!Room->CustomRaceKartPhysAllReady || Room->CustomRaceKartPhysMissingMask != 0))
	{
		QQS_DebugPrintf10001("[FLOW][CUSTOM-511-BLOCK-551] RoomID=%u Reason=%s SnapshotNum=%u PhysReady=0x%04X PhysMissing=0x%04X LastS2C=%u -> no 511\n",
			Room->ID, reason ? reason : "", Room->CustomRaceSnapshotPlayerNum,
			(unsigned)Room->CustomRaceKartPhysReadyMask,
			(unsigned)Room->CustomRaceKartPhysMissingMask,
			Room->CustomLastSentMsgID);
		Room->CustomWaitingRaceBegin = false;
		Room->CustomRaceBeginSent = false;
		Room->CustomLoadingDispatching = false;
		Room->Callback = NULL;
		Room->startStatus = 1;
		return;
	}

	UINT danceDelayMs = GetRoomStartDanceDelayMs(Room);
	if (Room->Timer)
		CancelRoomTimer(Room, reason ? reason : "custom schedule 511 dance");

	Room->CustomRaceBeginSent = true;
	Room->CustomWaitingRaceBegin = false;
	Room->CustomLoadingDispatching = false;
	Room->CustomLastSentMsgID = 511;

	QQS_DebugPrintf10001("[FLOW][CUSTOM-511-DANCE] RoomID=%u DelayMs=%u IsSkip=%u DanceTime=%u SkipDancetime=%d Reason=%s -> 511 after same yml dance delay as normal room\n",
		Room->ID, danceDelayMs, Room->isSkip, DanceTime, SkipDancetime, reason ? reason : "");

	if (danceDelayMs == 0)
	{
		OnBegin(Room);
		return;
	}
	CreateRoomTimer(Room, (int)danceDelayMs, OnBegin);
}

void CancelRoomTimer(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room)
		return;
	if (Room->Timer)
	{
		HANDLE oldTimer = Room->Timer;
		if (TimerQueue && DeleteTimerQueueTimer(TimerQueue, oldTimer, NULL))
		{
			QQS_DebugPrintf10001("[FLOW][ROOM-TIMER-CANCEL] RoomID=%u Timer=%p Reason=%s\n", Room->ID, oldTimer, reason ? reason : "");
		}
		else
		{
			DWORD err = GetLastError();
			QQS_DebugPrintf10001("[FLOW][ROOM-TIMER-CANCEL-FAIL] RoomID=%u Timer=%p Err=%lu Reason=%s; clear handle/callback\n", Room->ID, oldTimer, err, reason ? reason : "");
		}
		Room->Timer = NULL;
	}
	Room->Callback = NULL;
}

static void ResetCustomRuntimeState(std::shared_ptr<RoomNode> Room)
{
	if (!Room)
		return;
	Room->CustomCurrentStageIndex = 0;
	Room->CustomLastFinishStageIndex = 0xFF;
	Room->CustomLastStageFinishTick = 0;
	Room->CustomStageArriveStageIndex = 0xFF;
	Room->CustomStageArriveMask = 0;
	Room->CustomStageArriveCount = 0;
	Room->CustomStageArriveNeed = 0;
	Room->CustomStageFirstArriveTick = 0;
	Room->CustomWaitingPrepareReadyBeforeLoad = false;
	Room->CustomPreloadReadySatisfied = false;
	Room->CustomPreloadReadyMask = 0;
	Room->CustomWaitingRaceBegin = false;
	Room->CustomRaceBeginSent = false;
	Room->CustomLoadingDispatching = false;
	Room->CustomLoadReadyMask = 0;
	Room->CustomLoadingStartTick = 0;
	Room->CustomLastSentMsgID = 0;
	Room->CustomStageMapOrderNum = 0;
	Room->CustomStageMapSeed = 0;
	Room->CustomStageMapOrder[0] = 0;
	Room->CustomStageMapOrder[1] = 0;
	Room->CustomStageMapOrder[2] = 0;
	ClearCustomRaceStartSnapshot(Room, "reset-custom-runtime");
}


void EnsureRoomOwnerAlive(std::shared_ptr<RoomNode> Room, std::shared_ptr<ClientNode> preferred, const char* reason)
{
	if (!Room)
		return;

	int seatLimit = GetRoomSeatLimit(Room);
	USHORT oldOwner = Room->RoomOwnerID;
	USHORT normalizedOwner = GetRoomConnOwnerID(Room);
	if (normalizedOwner != 0)
	{
		std::shared_ptr<ClientNode> ownerClient = nullptr;
		for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); ++i)
		{
			if (Room->Player[i] && GetRoomConnPlayerID(Room, Room->Player[i]) == normalizedOwner)
			{
				ownerClient = Room->Player[i];
				break;
			}
		}
		Room->RoomOwnerID = normalizedOwner;
		if (ownerClient)
			Room->RoomOwnerUin = ownerClient->Uin;
		if (oldOwner != normalizedOwner)
		{
			QQS_DebugPrintf10001("[FLOW][ROOM-OWNER-FIX] RoomID=%u Reason=%s OldOwner=%u NewOwner=%u PreferredUin=%u NewOwnerUin=%u Seat=%d SeatLimit=%d\n",
				Room->ID, reason ? reason : "", oldOwner, Room->RoomOwnerID,
				preferred ? preferred->Uin : 0, ownerClient ? ownerClient->Uin : 0,
				ownerClient ? ownerClient->SeatID : -1, seatLimit);
		}
		return;
	}

	Room->RoomOwnerID = 0;
	Room->RoomOwnerUin = 0;
	if (oldOwner != 0)
	{
		QQS_DebugPrintf10001("[FLOW][ROOM-OWNER-CLEAR] RoomID=%u Reason=%s OldOwner=%u SeatLimit=%d\n",
			Room->ID, reason ? reason : "", oldOwner, seatLimit);
	}
}

void ClearCustomRoomState(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room)
		return;
	ClearRoomGameLogicSession(Room, reason ? reason : "clear-custom");
	if (!Room->HasCustomScheme && Room->SpeFlag != 9 && Room->CustomMapNum == 0 && Room->CustomSchemeWire.empty())
	{
		ResetCustomRuntimeState(Room);
		return;
	}
	QQS_DebugPrintf10001("[FLOW][CUSTOM-CLEAR] RoomID=%u Reason=%s OldSpeFlag=%u SchemeID=%u MapNum=%u Map=[%u,%u,%u]\n",
		Room->ID, reason ? reason : "", Room->SpeFlag, Room->CustomSchemeID, Room->CustomMapNum,
		Room->CustomMapIDList[0], Room->CustomMapIDList[1], Room->CustomMapIDList[2]);
	CancelRoomTimer(Room, reason ? reason : "clear-custom");
	Room->HasCustomScheme = false;
	Room->SpeFlag = 0;
	Room->CustomAllocSvrType = 14;
	Room->CustomSchemeID = 0;
	Room->CustomBaseGameMode = 0;
	Room->CustomSubGameMode = 0;
	Room->CustomEliminationIntervalTs = 10;
	Room->CustomEliminationRanking = 0;
	Room->CustomRoundNum = 0;
	Room->CustomN2OMode = 0;
	Room->CustomN2OEffect = 100;
	Room->CustomSmallSpray = 1;
	Room->CustomCollision = 0;
	Room->CustomGem = 1;
	Room->CustomCostume = 0;
	Room->CustomModification = 1;
	Room->CustomMapNum = 0;
	Room->CustomMapIDList[0] = 0;
	Room->CustomMapIDList[1] = 0;
	Room->CustomMapIDList[2] = 0;
	Room->CustomNoCollisionSeconds = 5;
	Room->CustomSchemeWire.clear();
	ResetCustomRuntimeState(Room);
}

BYTE BuildCustomStageMapOrder(std::shared_ptr<RoomNode> Room)
{
	if (!Room || !Room->HasCustomScheme)
		return 0;
	NormalizeCustomSchemeForOfficial(Room, "build-stage-order");
	std::vector<UINT> maps;
	BYTE srcNum = Room->CustomMapNum;
	if (srcNum > 3) srcNum = 3;
	for (BYTE i = 0; i < srcNum; i++)
	{
		UINT mapID = Room->CustomMapIDList[i];
		// i < CustomMapNum means this is an active official custom slot.
		// Keep 0 here because it is the all-random map pool.
		maps.push_back(mapID);
	}
	if (maps.empty() && Room->MapID != 0)
		maps.push_back(Room->MapID);
	if (maps.empty())
		maps.push_back(0); // default to all-random instead of an empty scheme
	BYTE stageCount = Room->CustomRoundNum ? Room->CustomRoundNum : (BYTE)maps.size();
	if (stageCount == 0) stageCount = 1;
	if (stageCount > 3) stageCount = 3;
	Room->CustomStageMapSeed = 0;
	for (BYTE i = 0; i < 3; i++)
	{
		UINT stageMapID = 0;
		if (i < stageCount)
		{
			stageMapID = maps[i % maps.size()];
			if (stageMapID < 100)
				stageMapID = GetRandMap((short)stageMapID);
			if (stageMapID == 0)
				stageMapID = Room->MapID ? Room->MapID : 114;
		}
		Room->CustomStageMapOrder[i] = stageMapID;
	}
	Room->CustomStageMapOrderNum = stageCount;
	Room->MapID = Room->CustomStageMapOrder[0];
	QQS_DebugPrintf10001("[FLOW][CUSTOM-MAP-ORDER] RoomID=%u PreserveOrder=1 StageCount=%u SrcMapNum=%u Pool=[%u,%u,%u] StageMap=[%u,%u,%u]\n",
		Room->ID, Room->CustomStageMapOrderNum, Room->CustomMapNum,
		Room->CustomMapIDList[0], Room->CustomMapIDList[1], Room->CustomMapIDList[2],
		Room->CustomStageMapOrder[0], Room->CustomStageMapOrder[1], Room->CustomStageMapOrder[2]);
	return Room->CustomStageMapOrderNum;
}

BYTE GetCustomStageCount(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room || !Room->HasCustomScheme)
		return 0;
	BYTE count = Room->CustomStageMapOrderNum;
	if (count == 0) count = Room->CustomMapNum;
	if (count == 0 && Room->CustomRoundNum) count = Room->CustomRoundNum;
	if (count == 0) count = 1;
	if (count > 3) count = 3;
	return count;
}

UINT GetCustomStageMapID(const std::shared_ptr<RoomNode>& Room, BYTE stageIndex, UINT fallbackMapID)
{
	if (!Room || !Room->HasCustomScheme)
		return fallbackMapID;
	BYTE count = GetCustomStageCount(Room);
	if (count == 0)
		return fallbackMapID;
	if (stageIndex >= count)
		stageIndex = (BYTE)(count - 1);
	UINT mapID = Room->CustomStageMapOrder[stageIndex];
	if (mapID == 0 && stageIndex < 3)
		mapID = Room->CustomMapIDList[stageIndex];
	if (mapID == 0)
		mapID = fallbackMapID;
	return mapID;
}

USHORT GetCustomOfficialTotalRound(const std::shared_ptr<RoomNode>& Room, UINT mapID, USHORT physicalRound)
{
	if (!Room || !IsCustomRoomFlow(Room))
		return physicalRound;
	if (Room->CustomRoundNum == 0 || Room->CustomRoundNum != Room->CustomMapNum ||
		Room->CustomStageMapOrderNum != Room->CustomMapNum)
		return 0;
	for (BYTE index = 0; index < Room->CustomStageMapOrderNum; ++index)
	{
		if (Room->CustomStageMapOrder[index] == 0)
			return 0;
		if (Room->CustomStageMapOrder[index] == mapID)
			return Room->CustomRoundNum;
	}
	return 0;
}

UINT GetCustomDisplayMapID(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return 0;
	if (!Room->HasCustomScheme)
		return Room->MapID;

	// 104/room-list still needs a non-zero display MapID. Returning 0 makes
	// the client rebuild an empty/custom fallback shell after ESC return.
	if (Room->MapID != 0)
		return Room->MapID;
	if (Room->CustomMapIDList[0] != 0)
		return Room->CustomMapIDList[0];
	// Active custom slot 0 means all-random. Room-list/104 display still needs
	// a concrete non-zero map to keep the client UI from falling back to empty.
	return 114;
}


BYTE GetEffectiveRoomSpeFlag(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return 0;
	if (Room->HasCustomScheme)
		return Room->SpeFlag ? Room->SpeFlag : 9;
	return Room->SpeFlag;
}

static void CopyBorderRaceRuntimeToSnapshot(BorderRaceStartRosterEntry& entry, const std::shared_ptr<ClientNode>& Client)
{
	if (!Client)
		return;
	const bool finished = Client->IsFinish == TRUE && Client->FinishTime > 0;
	entry.Finished = entry.Finished || finished;
	if (finished)
		entry.FinishTime = static_cast<UINT>(Client->FinishTime);
	entry.TeamID = Client->TeamID;
	entry.KartID = Client->KartID;
	entry.KartSkinID = Client->KartSkinID;
	entry.HighestSpeed = Client->HighestSpeed;
	entry.CollisionCount = Client->CollisionCount;
	entry.N2OGatherCount = Client->N2OGatherCount;
	entry.DriftCount = Client->DriftCount;
	entry.RaceDetail = Client->RaceDetail;
	entry.GradeLevel = Client->GradeLevel;
	entry.Experience = Client->Experience;
	entry.SuperMoney = Client->SuperMoney;
	entry.TotalHonor = Client->TotalHonor;
}

bool UpdateBorderRaceSnapshotFromClient(std::shared_ptr<RoomNode> Room, const std::shared_ptr<ClientNode>& Client, bool markQuit, const char* reason)
{
	if (!Room || !Client || !Room->BorderRaceSnapshotValid)
		return false;

	for (auto& entry : Room->BorderRaceStartRoster)
	{
		if (entry.Uin != Client->Uin)
			continue;

		CopyBorderRaceRuntimeToSnapshot(entry, Client);
		RoomSeatMask bit = GetRoomSeatBit(entry.SeatID);
		if (entry.Finished && bit != 0)
			Room->BorderRaceFinishedMask = (RoomSeatMask)(Room->BorderRaceFinishedMask | bit);
		if (markQuit)
		{
			entry.Quit = true;
			if (bit != 0)
				Room->BorderRaceQuitMask = (RoomSeatMask)(Room->BorderRaceQuitMask | bit);
		}
		if (bit != 0 && markQuit)
			Room->BorderRaceConnectedMask = (RoomSeatMask)(Room->BorderRaceConnectedMask & (RoomSeatMask)~bit);

		QQS_DebugPrintf10001("[FLOW][BORDER-SNAPSHOT-UPDATE] Reason=%s BorderRoomID=%u Uin=%u Seat=%d Finished=%u Quit=%u StartMask=0x%04X ConnectedMask=0x%04X FinishedMask=0x%04X QuitMask=0x%04X\n",
			reason ? reason : "", Room->ID, entry.Uin, (int)entry.SeatID,
			entry.Finished ? 1u : 0u, entry.Quit ? 1u : 0u,
			(unsigned)Room->BorderRaceStartMask, (unsigned)Room->BorderRaceConnectedMask,
			(unsigned)Room->BorderRaceFinishedMask, (unsigned)Room->BorderRaceQuitMask);
		return true;
	}
	return false;
}

static UINT NextRoomRaceSequence(std::atomic<UINT>& next, UINT initialValue, UINT maximum)
{
	if (initialValue == 0 || initialValue > maximum)
		return 0;
	UINT expected = 0;
	next.compare_exchange_strong(
		expected, initialValue, std::memory_order_relaxed, std::memory_order_relaxed);
	for (;;)
	{
		UINT current = next.load(std::memory_order_relaxed);
		if (current == 0 || current > maximum)
			return 0;
		const UINT following = current == maximum ? 1 : current + 1;
		if (next.compare_exchange_weak(
			current, following, std::memory_order_relaxed, std::memory_order_relaxed))
		{
			return current;
		}
	}
}

static void WriteRaceAuthorityBE16(std::array<BYTE, 74>& bytes, size_t offset, USHORT value)
{
	bytes[offset] = static_cast<BYTE>(value >> 8);
	bytes[offset + 1] = static_cast<BYTE>(value);
}

static void WriteRaceAuthorityBE32(std::array<BYTE, 74>& bytes, size_t offset, UINT value)
{
	bytes[offset] = static_cast<BYTE>(value >> 24);
	bytes[offset + 1] = static_cast<BYTE>(value >> 16);
	bytes[offset + 2] = static_cast<BYTE>(value >> 8);
	bytes[offset + 3] = static_cast<BYTE>(value);
}

static void RollbackRoomRaceStartSnapshot(const std::shared_ptr<RoomNode>& Room, const char* reason)
{
	if (!Room)
		return;
	ClearRoomGameLogicSession(Room, reason ? reason : "race-start-rollback");
	Room->RaceStartPlayerNum = 0;
	Room->RaceActiveMask = 0;
	Room->RaceStartUins.clear();
	Room->RaceTrackOrders.fill(0);
	Room->RaceTrackOrdersValid = false;
	Room->RaceSeed = 0;
	Room->RaceGameSeq = 0;
	Room->RaceMatchSeqHigh = 0;
	Room->RaceMatchSeqLow = 0;
	Room->RaceMultiInfo.fill(0);
	Room->RaceAuthorityValid = false;
	Room->BorderRaceSnapshotValid = false;
	Room->BorderRaceStartMask = 0;
	Room->BorderRaceConnectedMask = 0;
	Room->BorderRaceFinishedMask = 0;
	Room->BorderRaceQuitMask = 0;
	Room->BorderRacePhase = BorderRaceSettlementPhase::Idle;
	Room->BorderRaceStartRoster.clear();
	ClearCustomRaceStartSnapshot(Room, reason ? reason : "race-start-rollback");
}

static UINT ReadRaceAuthorityLE32(const BYTE* bytes)
{
	return static_cast<UINT>(bytes[0]) |
		(static_cast<UINT>(bytes[1]) << 8) |
		(static_cast<UINT>(bytes[2]) << 16) |
		(static_cast<UINT>(bytes[3]) << 24);
}

static bool BuildRoomRaceAuthority(const std::shared_ptr<RoomNode>& Room, BYTE playerCount)
{
	if (!Room || Room->ID == 0 || Room->ID > 0xFFFF ||
		playerCount == 0 || playerCount > ROOM_SEAT_12_MAX)
		return false;

	Room->RaceAuthorityValid = false;
	Room->RaceSeed = 0;
	Room->RaceGameSeq = 0;
	Room->RaceMatchSeqHigh = 0;
	Room->RaceMatchSeqLow = 0;
	Room->RaceMultiInfo.fill(0);

	const bool serviceContext =
		Room->HasCustomScheme || Room->AuroraUGC || Room->LuckyMatchInternalRoom ||
		Room->BaseGameMode == 40 || Room->matchQueueType != 0 ||
		IsTrackedBorderRaceRoomLocal(Room);
	const USHORT outerBaseMode = Room->HasCustomScheme ? 42 : (Room->AuroraUGC ? 42 : Room->BaseGameMode);
	const USHORT outerSubMode = Room->HasCustomScheme ? 27 : (Room->AuroraUGC ? 29 : Room->SubGameMode);
	if (outerBaseMode == 0 || outerBaseMode > 0xFF || outerSubMode == 0 || outerSubMode > 0xFF)
		return false;

	// The client treats bytes 20..73 as opaque PVP authority data.  Generate it
	// once with the OS CSPRNG and freeze it with the room snapshot; never replay a
	// captured value or derive a second value per recipient.
	std::array<BYTE, 69> randomMaterial{};
	if (BCryptGenRandom(
		nullptr,
		randomMaterial.data(),
		static_cast<ULONG>(randomMaterial.size()),
		BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
	{
		return false;
	}

	const UINT seed = ReadRaceAuthorityLE32(randomMaterial.data()) & 0x7FFFFFFFu;
	const UINT matchSeqLow = ReadRaceAuthorityLE32(randomMaterial.data() + 4) & ~1u;
	const UINT matchHighSeed = ReadRaceAuthorityLE32(randomMaterial.data() + 12) & 0x00FFFFFFu;
	if (seed == 0 || matchSeqLow == 0 || matchHighSeed == 0)
		return false;
	static std::atomic<UINT> nextGameSequence{ 0 };
	static std::atomic<UINT> nextMatchSequenceHigh{ 0 };
	const UINT gameSequence = NextRoomRaceSequence(nextGameSequence, 1u, 0x00FFFFFFu);
	const UINT matchSeqHigh = NextRoomRaceSequence(nextMatchSequenceHigh, matchHighSeed, 0x00FFFFFFu);
	if (gameSequence == 0 || matchSeqHigh == 0)
		return false;

	const UINT timestamp = static_cast<UINT>(time(nullptr));
	std::array<BYTE, 74>& multiInfo = Room->RaceMultiInfo;
	WriteRaceAuthorityBE32(multiInfo, 0, serviceContext ? 0u : 1u);
	WriteRaceAuthorityBE32(multiInfo, 4, timestamp);
	multiInfo[8] = serviceContext ? 101 : 4;
	WriteRaceAuthorityBE16(multiInfo, 9, static_cast<USHORT>(Room->ID));
	multiInfo[11] = serviceContext ? 2 : 4;
	multiInfo[12] = static_cast<BYTE>(outerBaseMode);
	multiInfo[13] = static_cast<BYTE>(outerSubMode);
	multiInfo[14] = 1;
	WriteRaceAuthorityBE32(multiInfo, 15, timestamp);
	multiInfo[19] = serviceContext ? 18 : 34;
	std::copy(randomMaterial.begin() + 16, randomMaterial.begin() + 19, multiInfo.begin() + 20);
	multiInfo[23] = playerCount;
	std::copy(randomMaterial.begin() + 19, randomMaterial.end(), multiInfo.begin() + 24);

	Room->RaceSeed = seed;
	Room->RaceGameSeq = gameSequence;
	Room->RaceMatchSeqHigh = matchSeqHigh;
	Room->RaceMatchSeqLow = matchSeqLow;
	Room->RaceAuthorityValid =
		Room->RaceSeed != 0 &&
		Room->RaceGameSeq != 0 &&
		Room->RaceMatchSeqHigh != 0 &&
		Room->RaceMatchSeqLow != 0;
	return Room->RaceAuthorityValid;
}

static bool BuildFrozenRoomRaceTrackOrders(
	const std::shared_ptr<RoomNode>& Room,
	RoomSeatMask activeMask,
	int seatLimit)
{
	if (!Room)
		return false;

	Room->RaceStartUins.clear();
	Room->RaceTrackOrders.fill(0);
	Room->RaceTrackOrdersValid = false;
	Room->RaceAuthorityValid = false;

	for (int seat = 0; seat < seatLimit && seat < static_cast<int>(Room->Player.size()); ++seat)
	{
		const RoomSeatMask bit = GetRoomSeatBit(seat);
		if (bit == 0 || (activeMask & bit) == 0)
			continue;
		const std::shared_ptr<ClientNode>& player = Room->Player[seat];
		if (!player || player->Uin == 0)
			return false;
		Room->RaceStartUins.push_back(player->Uin);
	}

	const size_t playerCount = Room->RaceStartUins.size();
	if (playerCount == 0 || playerCount > Room->RaceTrackOrders.size() ||
		playerCount != static_cast<size_t>(Room->RaceStartPlayerNum))
	{
		return false;
	}

	// Current official fixtures use center-out lanes for 1..6 racers:
	// 1P={2}, 2P={2,3}, 4P={2,3,1,4}, 6P={2,3,1,4,0,5}.
	// The captured 11-player custom race occupies contiguous slots 0..10.
	static constexpr std::array<BYTE, ROOM_SEAT_NORMAL_MAX> kOfficialSixPlayerLaneOrder = { 2, 3, 1, 4, 0, 5 };
	std::vector<BYTE> occupiedSlots;
	occupiedSlots.reserve(playerCount);
	if (playerCount <= kOfficialSixPlayerLaneOrder.size())
	{
		for (size_t i = 0; i < playerCount; ++i)
			occupiedSlots.push_back(kOfficialSixPlayerLaneOrder[i]);
	}
	else
	{
		for (size_t i = 0; i < playerCount; ++i)
			occupiedSlots.push_back(static_cast<BYTE>(i));
	}

	if (!BuildRoomRaceAuthority(Room, static_cast<BYTE>(playerCount)))
		return false;
	std::seed_seq shuffleSeed{
		Room->RaceSeed,
		Room->RaceMatchSeqHigh,
		Room->RaceMatchSeqLow,
		static_cast<UINT>(Room->ID),
		Room->MapID,
		static_cast<UINT>(playerCount)
	};
	std::mt19937 rng(shuffleSeed);

	std::vector<UINT> shuffledUins = Room->RaceStartUins;
	if (shuffledUins.size() > 1)
		std::shuffle(shuffledUins.begin(), shuffledUins.end(), rng);

	for (size_t i = 0; i < shuffledUins.size(); ++i)
	{
		const BYTE slot = occupiedSlots[i];
		if (slot >= Room->RaceTrackOrders.size() || shuffledUins[i] <= 10000)
			return false;
		Room->RaceTrackOrders[slot] = shuffledUins[i];
	}

	Room->RaceTrackOrdersValid = true;
	return true;
}

static BYTE FindFrozenRoomRaceTrackSlot(const std::shared_ptr<RoomNode>& Room, UINT uin)
{
	if (!Room || !Room->RaceTrackOrdersValid || uin == 0)
		return 0xFF;
	for (size_t slot = 0; slot < Room->RaceTrackOrders.size(); ++slot)
	{
		if (Room->RaceTrackOrders[slot] == uin)
			return static_cast<BYTE>(slot);
	}
	return 0xFF;
}

bool CaptureRoomRaceStartSnapshot(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room)
		return false;
	Room->RankedSettlement.reset();
	RoomSeatMask mask = 0;
	BYTE count = 0;
	int skippedSlotNum = 0;
	bool isBorderRaceRoom = IsTrackedBorderRaceRoomLocal(Room);
	int seatLimit = GetRoomSeatLimit(Room);
	for (char si = 0; si < seatLimit; si++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[si];
		if (!RoomClient)
			continue;
		if (isBorderRaceRoom)
		{
			if (!IsBorderRacePlayerForRoomLocal(Room, RoomClient))
			{
				skippedSlotNum++;
				continue;
			}
		}
		else
		{
			if (RoomClient->OBState)
				continue;
			if (RoomClient->deleteflag || !RoomClient->IsLogin || RoomClient->Uin == 0)
				continue;
		}
		mask |= GetRoomSeatBit(si);
		count++;
	}
	Room->RaceStartPlayerNum = count;
	Room->RaceActiveMask = mask;
	if (!BuildFrozenRoomRaceTrackOrders(Room, mask, seatLimit))
	{
		RollbackRoomRaceStartSnapshot(Room, reason ? reason : "race-track-build-failed");
		QQS_DebugPrintf10001("[FLOW][RACE-TRACK-SNAPSHOT-FAIL] Reason=%s RoomID=%u PlayerNum=%u ActiveMask=0x%04X SeatLimit=%d\n",
			reason ? reason : "", Room->ID, static_cast<unsigned>(count), static_cast<unsigned>(mask), seatLimit);
		return false;
	}
	if (isBorderRaceRoom)
	{
		Room->BorderRaceStartRoster.clear();
		Room->BorderRaceStartMask = mask;
		Room->BorderRaceConnectedMask = mask;
		Room->BorderRaceFinishedMask = 0;
		Room->BorderRaceQuitMask = 0;
		Room->BorderRacePhase = BorderRaceSettlementPhase::Racing;
		for (char si = 0; si < seatLimit && si < (char)Room->Player.size(); si++)
		{
			RoomSeatMask bit = GetRoomSeatBit(si);
			if ((mask & bit) == 0)
				continue;
			std::shared_ptr<ClientNode> RoomClient = Room->Player[si];
			if (!RoomClient)
				continue;

			BorderRaceStartRosterEntry entry;
			entry.SeatID = si;
			entry.Uin = RoomClient->Uin;
			entry.ConnID = RoomClient->ConnID;
			entry.OriginalRoomID = RoomClient->RoomID;
			CopyBorderRaceRuntimeToSnapshot(entry, RoomClient);
			Room->BorderRaceStartRoster.push_back(entry);
		}
		Room->RaceStartPlayerNum = (BYTE)Room->BorderRaceStartRoster.size();
		Room->BorderRaceSnapshotValid = !Room->BorderRaceStartRoster.empty();
		QQS_DebugPrintf10001("[FLOW][BORDER-START-SNAPSHOT] Reason=%s BorderRoomID=%u StartNum=%u StartMask=0x%04X ConnectedMask=0x%04X\n",
			reason ? reason : "", Room->ID, (unsigned)Room->RaceStartPlayerNum,
			(unsigned)Room->BorderRaceStartMask, (unsigned)Room->BorderRaceConnectedMask);
	}
	else
	{
		Room->BorderRaceSnapshotValid = false;
		Room->BorderRaceStartMask = 0;
		Room->BorderRaceConnectedMask = 0;
		Room->BorderRaceFinishedMask = 0;
		Room->BorderRaceQuitMask = 0;
		Room->BorderRacePhase = BorderRaceSettlementPhase::Idle;
		Room->BorderRaceStartRoster.clear();
	}
	if (IsCustomRoomFlow(Room))
	{
		Room->CustomRaceStartSnapshot.clear();
		Room->CustomRaceSnapshotRoomID = Room->ID;
		Room->CustomRaceSnapshotMask = mask;
		Room->CustomRaceSnapshotPlayerNum = count;
		Room->CustomRaceSnapshotTick = GetTickCount();
		Room->CustomRaceKartPhysAllReady = false;
		Room->CustomRaceKartPhysReadyMask = 0;
		Room->CustomRaceKartPhysMissingMask = 0;
		for (char si = 0; si < seatLimit && si < (char)Room->Player.size(); si++)
		{
			RoomSeatMask bit = GetRoomSeatBit(si);
			if ((mask & bit) == 0)
				continue;
			std::shared_ptr<ClientNode> RoomClient = Room->Player[si];
			if (!IsCustomRaceSnapshotBasicClient(Room, RoomClient, si))
			{
				RollbackRoomRaceStartSnapshot(Room, "custom-snapshot-client-invalid");
				return false;
			}
			CustomRaceStartSnapshotEntry entry;
			entry.SeatID = si;
			entry.TrackSlot = FindFrozenRoomRaceTrackSlot(Room, RoomClient->Uin);
			if (entry.TrackSlot == 0xFF)
			{
				RollbackRoomRaceStartSnapshot(Room, "custom-snapshot-track-slot-missing");
				return false;
			}
			entry.Uin = RoomClient->Uin;
			entry.ConnID = GetRoomConnPlayerID(Room, RoomClient);
			entry.GameID = RoomClient->GameID;
			entry.MapID = RoomClient->MapID ? RoomClient->MapID : Room->MapID;
			entry.KartID = RoomClient->KartID;
			entry.KartSkinID = RoomClient->KartSkinID;
			entry.VisualKartID = RoomClient->KartSkinID ? RoomClient->KartSkinID : RoomClient->KartID;
			entry.PhysLoadID = (RoomClient->Uin > 10000 && RoomClient->KartSkinID > 0) ? RoomClient->KartSkinID : RoomClient->KartID;
			entry.KartPhysReady = false;
			Room->CustomRaceStartSnapshot.push_back(entry);
		}
		Room->CustomRaceSnapshotPlayerNum = (BYTE)Room->CustomRaceStartSnapshot.size();
		Room->CustomRaceSnapshotValid =
			!Room->CustomRaceStartSnapshot.empty() &&
			Room->CustomRaceStartSnapshot.size() == count;
		if (!Room->CustomRaceSnapshotValid)
		{
			RollbackRoomRaceStartSnapshot(Room, "custom-snapshot-roster-mismatch");
			return false;
		}
		QQS_DebugPrintf10001("[FLOW][CUSTOM-SNAPSHOT] Reason=%s RoomID=%u SnapshotNum=%u SnapshotMask=0x%04X TrackSlots=[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]\n",
			reason ? reason : "", Room->ID, Room->CustomRaceSnapshotPlayerNum,
			(unsigned)Room->CustomRaceSnapshotMask,
			Room->CustomRaceStartSnapshot.size() > 0 ? Room->CustomRaceStartSnapshot[0].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 1 ? Room->CustomRaceStartSnapshot[1].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 2 ? Room->CustomRaceStartSnapshot[2].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 3 ? Room->CustomRaceStartSnapshot[3].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 4 ? Room->CustomRaceStartSnapshot[4].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 5 ? Room->CustomRaceStartSnapshot[5].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 6 ? Room->CustomRaceStartSnapshot[6].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 7 ? Room->CustomRaceStartSnapshot[7].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 8 ? Room->CustomRaceStartSnapshot[8].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 9 ? Room->CustomRaceStartSnapshot[9].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 10 ? Room->CustomRaceStartSnapshot[10].TrackSlot : 0xFF,
			Room->CustomRaceStartSnapshot.size() > 11 ? Room->CustomRaceStartSnapshot[11].TrackSlot : 0xFF);
	}
	else
	{
		ClearCustomRaceStartSnapshot(Room, reason ? reason : "non-custom-snapshot");
	}
	QQS_DebugPrintf10001("[FLOW][RACE-SNAPSHOT] Reason=%s RoomID=%u StartPlayerNum=%u ActiveMask=0x%04X CurrentPlayerNum=%u SeatLimit=%d HasCustom=%d\n",
		reason ? reason : "", Room->ID, Room->RaceStartPlayerNum, (unsigned)Room->RaceActiveMask,
		(unsigned)Room->CurrentPlayerNum, seatLimit, Room->HasCustomScheme ? 1 : 0);
	if (isBorderRaceRoom && (skippedSlotNum > 0 || count != Room->CurrentPlayerNum))
	{
		QQS_DebugPrintf10001("[FLOW][BORDER-BEGIN-ROSTER] Reason=%s BorderRoomID=%u StartPlayerNum=%u CurrentPlayerNum=%u Skipped=%d ActiveMask=0x%04X\n",
				reason ? reason : "", Room->ID, Room->RaceStartPlayerNum, (unsigned)Room->CurrentPlayerNum, skippedSlotNum, (unsigned)Room->RaceActiveMask);
	}
	return true;
}

static USHORT GetRoomListSvrID(std::shared_ptr<ClientNode> Client, const std::shared_ptr<RoomNode>& Room)
{
	// Keep SvrId stable in room list/104. Non-zero SvrId can make the client
	// display a recomputed public room number while custom game is running.
	return 0;
}

static UINT GetRoomPublicNo(std::shared_ptr<ClientNode> Client, const std::shared_ptr<RoomNode>& Room)
{
	// Do not synthesize channel*10000+localNo here. The client shows RoomNo in UI,
	// so custom rooms must keep the original internal Room->ID visible end-to-end.
	return Room ? Room->ID : 0;
}

static void PatchCustomSchemeWire16(BYTE* buf, size_t off, USHORT value)
{
	BYTE* q = buf + off;
	Write16(q, value);
}

static void PatchCustomSchemeWire32(BYTE* buf, size_t off, UINT value)
{
	BYTE* q = buf + off;
	Write32(q, value);
}

static BYTE CountRoomSeatMaskBits(RoomSeatMask mask, int seatLimit)
{
	BYTE count = 0;
	if (seatLimit > ROOM_SEAT_MAX)
		seatLimit = ROOM_SEAT_MAX;
	for (int si = 0; si < seatLimit; si++)
	{
		if (mask & GetRoomSeatBit(si))
			count++;
	}
	return count;
}

static RoomSeatMask BuildCurrentRoomPlayerMask(const std::shared_ptr<RoomNode>& Room)
{
	RoomSeatMask mask = 0;
	if (!Room)
		return mask;
	int seatLimit = GetRoomSeatLimit(Room);
	for (int si = 0; si < seatLimit && si < (int)Room->Player.size(); si++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[si];
		if (RoomClient && !RoomClient->OBState && !RoomClient->deleteflag && RoomClient->IsLogin && RoomClient->Uin != 0)
			mask |= GetRoomSeatBit(si);
	}
	return mask;
}

static void RefreshCustomRaceStartSnapshotRuntimeFields(std::shared_ptr<RoomNode> Room)
{
	if (!Room || !Room->CustomRaceSnapshotValid)
		return;
	for (auto& entry : Room->CustomRaceStartSnapshot)
	{
		if (entry.SeatID < 0 || entry.SeatID >= (char)Room->Player.size())
			continue;
		std::shared_ptr<ClientNode> RoomClient = Room->Player[entry.SeatID];
		if (!RoomClient || RoomClient->Uin != entry.Uin)
			continue;
		entry.ConnID = GetRoomConnPlayerID(Room, RoomClient);
		entry.GameID = RoomClient->GameID;
		entry.MapID = RoomClient->MapID ? RoomClient->MapID : Room->MapID;
		entry.KartID = RoomClient->KartID;
		entry.KartSkinID = RoomClient->KartSkinID;
		entry.VisualKartID = RoomClient->KartSkinID ? RoomClient->KartSkinID : RoomClient->KartID;
		if (entry.PhysLoadID == 0)
			entry.PhysLoadID = (RoomClient->Uin > 10000 && RoomClient->KartSkinID > 0) ? RoomClient->KartSkinID : RoomClient->KartID;
	}
}

bool EnsureCustomRaceStartSnapshot(std::shared_ptr<RoomNode> Room, const char* reason, bool rebuild)
{
	if (!IsCustomRoomFlow(Room))
		return false;
	if (rebuild || !Room->CustomRaceSnapshotValid || Room->CustomRaceStartSnapshot.empty())
	{
		if (!CaptureRoomRaceStartSnapshot(Room, reason ? reason : "ensure-custom-snapshot"))
			return false;
	}
	RefreshCustomRaceStartSnapshotRuntimeFields(Room);
	return Room->CustomRaceSnapshotValid && !Room->CustomRaceStartSnapshot.empty();
}

bool FindCustomRaceStartSnapshotEntry(const std::shared_ptr<RoomNode>& Room, UINT Uin, CustomRaceStartSnapshotEntry* OutEntry)
{
	if (!Room || Uin == 0)
		return false;
	for (const auto& entry : Room->CustomRaceStartSnapshot)
	{
		if (entry.Uin != Uin)
			continue;
		if (OutEntry)
			*OutEntry = entry;
		return true;
	}
	return false;
}

std::vector<std::shared_ptr<ClientNode>> BuildCustomRaceRoster(const std::shared_ptr<RoomNode>& Room)
{
	std::vector<std::shared_ptr<ClientNode>> roster;
	if (!Room)
		return roster;
	int seatLimit = GetRoomSeatLimit(Room);
	if (IsCustomRoomFlow(Room) && EnsureCustomRaceStartSnapshot(Room, "build-custom-roster", false))
	{
		int skipped = 0;
		for (const auto& entry : Room->CustomRaceStartSnapshot)
		{
			if (entry.SeatID < 0 || entry.SeatID >= seatLimit || entry.SeatID >= (char)Room->Player.size())
			{
				skipped++;
				continue;
			}
			std::shared_ptr<ClientNode> RoomClient = Room->Player[entry.SeatID];
			if (!IsCustomRaceSnapshotBasicClient(Room, RoomClient, entry.SeatID) || RoomClient->Uin != entry.Uin)
			{
				skipped++;
				continue;
			}
			roster.push_back(RoomClient);
		}
		if (skipped > 0 || roster.size() != Room->CustomRaceStartSnapshot.size())
		{
			QQS_DebugPrintf10001("[FLOW][CUSTOM-SNAPSHOT-ROSTER] RoomID=%u SnapshotNum=%u RosterNum=%zu Skipped=%d ActiveMask=0x%04X Reason=live-mismatch\n",
				Room->ID, Room->CustomRaceSnapshotPlayerNum, roster.size(), skipped,
				(unsigned)Room->RaceActiveMask);
		}
		return roster;
	}
	RoomSeatMask activeMask = Room->RaceActiveMask ? Room->RaceActiveMask : BuildCurrentRoomPlayerMask(Room);
	for (char si = 0; si < seatLimit && si < (char)Room->Player.size(); si++)
	{
		RoomSeatMask bit = GetRoomSeatBit(si);
		if (activeMask != 0 && (activeMask & bit) == 0)
			continue;
		std::shared_ptr<ClientNode> RoomClient = Room->Player[si];
		if (!RoomClient)
			continue;
		if (RoomClient->OBState || RoomClient->deleteflag || !RoomClient->IsLogin || RoomClient->Uin == 0)
			continue;
		roster.push_back(RoomClient);
	}
	return roster;
}

bool NormalizeCustomSchemeForOfficial(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room)
		return false;
	if (!Room->HasCustomScheme && Room->SpeFlag != 9 && Room->CustomSchemeWire.empty())
		return false;

	BYTE oldMapNum = Room->CustomMapNum;
	BYTE oldRoundNum = Room->CustomRoundNum;
	UINT oldMapList[3] = { Room->CustomMapIDList[0], Room->CustomMapIDList[1], Room->CustomMapIDList[2] };
	UINT oldMapID = Room->MapID;

	UINT normalizedMaps[3] = { 0, 0, 0 };
	BYTE normalizedMapNum = 0;
	BYTE declaredMapNum = Room->CustomMapNum;
	if (declaredMapNum > 3)
		declaredMapNum = 3;
	if (declaredMapNum > 0)
	{
		for (BYTE i = 0; i < declaredMapNum; i++)
		{
			// CustomMapNum marks active official slots. Preserve 0 here because
			// it is the all-random pool, not padding.
			normalizedMaps[normalizedMapNum++] = Room->CustomMapIDList[i];
		}
	}
	else
	{
		for (BYTE i = 0; i < 3; i++)
		{
			UINT mapID = Room->CustomMapIDList[i];
			if (mapID == 0)
				continue;
			normalizedMaps[normalizedMapNum++] = mapID;
			if (normalizedMapNum >= 3)
				break;
		}
	}
	if (normalizedMapNum == 0 && Room->MapID != 0)
	{
		normalizedMaps[0] = Room->MapID;
		normalizedMapNum = 1;
	}
	if (normalizedMapNum == 0)
		normalizedMapNum = 1;

	BYTE normalizedRoundNum = normalizedMapNum;
	if (normalizedRoundNum == 0)
		normalizedRoundNum = 1;
	if (normalizedRoundNum > 3)
		normalizedRoundNum = 3;

	Room->HasCustomScheme = true;
	Room->SpeFlag = 9;
	if (Room->CustomSchemeID == 0)
		Room->CustomSchemeID = 1;
	if (Room->CustomAllocSvrType == 0)
		Room->CustomAllocSvrType = 14;
	if (Room->CustomBaseGameMode == 0)
		Room->CustomBaseGameMode = Room->BaseGameMode ? Room->BaseGameMode : 1;
	if (Room->CustomSubGameMode == 0)
		Room->CustomSubGameMode = Room->SubGameMode ? Room->SubGameMode : 1;
	Room->CustomMapNum = normalizedMapNum;
	Room->CustomRoundNum = normalizedRoundNum;
	for (BYTE i = 0; i < 3; i++)
		Room->CustomMapIDList[i] = normalizedMaps[i];
	if (Room->CustomMapIDList[0] != 0)
		Room->MapID = Room->CustomMapIDList[0];

	if (!Room->CustomSchemeWire.empty())
	{
		if (Room->CustomSchemeWire.size() >= 4)
			PatchCustomSchemeWire32(Room->CustomSchemeWire.data(), 0, Room->CustomSchemeID);
		if (Room->CustomSchemeWire.size() > 147)
			Room->CustomSchemeWire[147] = Room->CustomRoundNum;
		if (Room->CustomSchemeWire.size() > 158)
			Room->CustomSchemeWire[158] = Room->CustomMapNum;
		for (BYTE i = 0; i < 3; i++)
		{
			size_t off = 159 + (i * 4);
			if (Room->CustomSchemeWire.size() >= off + 4)
				PatchCustomSchemeWire32(Room->CustomSchemeWire.data(), off, Room->CustomMapIDList[i]);
		}
		if (Room->CustomSchemeWire.size() > 171)
			Room->CustomSchemeWire[171] = Room->CustomNoCollisionSeconds;
	}

	bool changed = oldMapNum != Room->CustomMapNum || oldRoundNum != Room->CustomRoundNum ||
		oldMapID != Room->MapID || oldMapList[0] != Room->CustomMapIDList[0] ||
		oldMapList[1] != Room->CustomMapIDList[1] || oldMapList[2] != Room->CustomMapIDList[2];
	QQS_DebugPrintf10001("[FLOW][CUSTOM-SCHEME-NORMALIZE] Reason=%s RoomID=%u Changed=%d OldMapNum=%u NewMapNum=%u OldRound=%u NewRound=%u OldMap=[%u,%u,%u] NewMap=[%u,%u,%u] RoomMap=%u SchemeLen=%zu\n",
		reason ? reason : "", Room->ID, changed ? 1 : 0,
		oldMapNum, Room->CustomMapNum, oldRoundNum, Room->CustomRoundNum,
		oldMapList[0], oldMapList[1], oldMapList[2],
		Room->CustomMapIDList[0], Room->CustomMapIDList[1], Room->CustomMapIDList[2],
		Room->MapID, Room->CustomSchemeWire.size());
	return changed;
}

static BYTE GetCustomSchemeMapNum104(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return 0;
	BYTE mapNum = Room->CustomMapNum;
	if (mapNum == 0)
	{
		for (BYTE i = 0; i < 3; i++)
		{
			if (Room->CustomMapIDList[i] != 0)
				mapNum++;
		}
	}
	if (mapNum > 3)
		mapNum = 3;
	return mapNum;
}

static size_t WriteCustomSchemeForEnterRoom104(BYTE*& p, const std::shared_ptr<RoomNode>& Room)
{
	static const size_t SchemePayloadSize104 = 172;
	BYTE scheme[SchemePayloadSize104] = {};

	if (Room && !Room->CustomSchemeWire.empty())
	{
		size_t copyLen = Room->CustomSchemeWire.size() < SchemePayloadSize104 ? Room->CustomSchemeWire.size() : SchemePayloadSize104;
		memcpy(scheme, Room->CustomSchemeWire.data(), copyLen);
	}

	if (Room)
	{
		UINT schemeID = Room->CustomSchemeID ? Room->CustomSchemeID : 1;
		USHORT baseMode = Room->CustomBaseGameMode ? Room->CustomBaseGameMode : Room->BaseGameMode;
		USHORT subMode = Room->CustomSubGameMode ? Room->CustomSubGameMode : Room->SubGameMode;
		BYTE mapNum = GetCustomSchemeMapNum104(Room);
		BYTE roundNum = Room->CustomRoundNum ? Room->CustomRoundNum : (mapNum ? mapNum : 1);

		PatchCustomSchemeWire32(scheme, 0, schemeID);
		PatchCustomSchemeWire16(scheme, 140, baseMode);
		PatchCustomSchemeWire16(scheme, 142, subMode);
		scheme[144] = Room->CustomAllocSvrType ? Room->CustomAllocSvrType : 14;
		scheme[145] = Room->CustomEliminationIntervalTs;
		scheme[146] = Room->CustomEliminationRanking;
		scheme[147] = roundNum;
		scheme[148] = Room->CustomN2OMode;
		PatchCustomSchemeWire32(scheme, 149, (UINT)Room->CustomN2OEffect);
		scheme[153] = Room->CustomSmallSpray;
		scheme[154] = Room->CustomCollision;
		scheme[155] = Room->CustomGem;
		scheme[156] = Room->CustomCostume;
		scheme[157] = Room->CustomModification;
		scheme[158] = mapNum;
		for (BYTE i = 0; i < 3; i++)
		{
			PatchCustomSchemeWire32(scheme, 159 + (i * 4), Room->CustomMapIDList[i]);
		}
		scheme[171] = Room->CustomNoCollisionSeconds;
	}

	Write16(p, (USHORT)(SchemePayloadSize104 + 2)); // nested length: 2B length field + 172B payload
	memcpy(p, scheme, SchemePayloadSize104);
	p += SchemePayloadSize104;
	return SchemePayloadSize104 + 2;
}

static size_t WriteEnterRoom104CustomTail(
	BYTE*& p,
	const std::shared_ptr<RoomNode>& Room,
	UINT RoomOtherTag,
	BYTE& HasCustomScheme,
	size_t& CustomSchemeWireLen)
{
	BYTE* begin = p;
	Write8(p, 0); // m_uchHasNoVipStartGameInfo
	Write8(p, 0); // m_ucRoomLabel
	Write32(p, RoomOtherTag); // m_uiRoomOtherTag
	Write8(p, 0); // m_ucHasSpringPropDaga
	HasCustomScheme = 1;
	Write8(p, HasCustomScheme); // m_ucHasCustomScheme
	CustomSchemeWireLen = WriteCustomSchemeForEnterRoom104(p, Room);
	Write32(p, 0); // m_uiMyWinStreak
	Write8(p, 2); // m_ucEnterRoomReason: official custom enter/return sample uses 2
	Write32(p, 0); // m_uiRoomWinStreak
	return (size_t)(p - begin);
}

static size_t WriteEnterRoom104ThreeDRoomTail(BYTE*& p)
{
	BYTE* begin = p;
	Write8(p, 0);  // m_uchHasNoVipStartGameInfo
	Write8(p, 0);  // m_ucRoomLabel
	Write32(p, 0); // m_uiRoomOtherTag
	Write8(p, 0);  // m_ucHasSpringPropDaga
	Write8(p, 0);  // m_ucHasCustomScheme
	Write32(p, 0); // m_uiMyWinStreak
	Write8(p, 2);  // m_ucEnterRoomReason
	Write32(p, 0); // m_uiRoomWinStreak
	Write8(p, 0);  // m_ucHasAuroraInfo
	return (size_t)(p - begin);
}

static void BuildCustomSchemeWire172(BYTE* scheme, const std::shared_ptr<RoomNode>& Room)
{
	static const size_t SchemeSize102 = 172;
	memset(scheme, 0, SchemeSize102);

	if (Room && !Room->CustomSchemeWire.empty())
	{
		size_t copyLen = Room->CustomSchemeWire.size() < SchemeSize102 ? Room->CustomSchemeWire.size() : SchemeSize102;
		memcpy(scheme, Room->CustomSchemeWire.data(), copyLen);
	}

	if (!Room)
		return;

	UINT schemeID = Room->CustomSchemeID ? Room->CustomSchemeID : 1;
	USHORT baseMode = Room->CustomBaseGameMode ? Room->CustomBaseGameMode : Room->BaseGameMode;
	USHORT subMode = Room->CustomSubGameMode ? Room->CustomSubGameMode : Room->SubGameMode;
	BYTE mapNum = GetCustomSchemeMapNum104(Room);
	BYTE roundNum = Room->CustomRoundNum ? Room->CustomRoundNum : (mapNum ? mapNum : 1);

	PatchCustomSchemeWire32(scheme, 0, schemeID);
	PatchCustomSchemeWire16(scheme, 140, baseMode);
	PatchCustomSchemeWire16(scheme, 142, subMode);
	scheme[144] = Room->CustomAllocSvrType ? Room->CustomAllocSvrType : 14;
	scheme[145] = Room->CustomEliminationIntervalTs;
	scheme[146] = Room->CustomEliminationRanking;
	scheme[147] = roundNum;
	scheme[148] = Room->CustomN2OMode;
	PatchCustomSchemeWire32(scheme, 149, (UINT)Room->CustomN2OEffect);
	scheme[153] = Room->CustomSmallSpray;
	scheme[154] = Room->CustomCollision;
	scheme[155] = Room->CustomGem;
	scheme[156] = Room->CustomCostume;
	scheme[157] = Room->CustomModification;
	scheme[158] = mapNum;
	for (BYTE i = 0; i < 3; i++)
	{
		PatchCustomSchemeWire32(scheme, 159 + (i * 4), Room->CustomMapIDList[i]);
	}
	scheme[171] = Room->CustomNoCollisionSeconds;
}

bool EnsureTwelvePlayerDefaultCustomScheme(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room || !IsTwelvePlayerRoom(Room))
		return false;
	if (Room->HasCustomScheme && !Room->CustomSchemeWire.empty())
		return false;

	UINT mapID = Room->MapID ? Room->MapID : 19;
	Room->HasCustomScheme = true;
	Room->SpeFlag = 9;
	Room->CustomAllocSvrType = 14;
	// 官服 12 人创建时，客户端提交 AT=67 的 SchemeID=0，服务端回包分配的是 3。
	// 这里默认也用 3，避免 12P 壳和自定义方案编号不一致导致客户端按普通房回退。
	Room->CustomSchemeID = Room->CustomSchemeID ? Room->CustomSchemeID : 3;
	Room->CustomBaseGameMode = Room->BaseGameMode ? Room->BaseGameMode : 1;
	Room->CustomSubGameMode = Room->SubGameMode ? Room->SubGameMode : 1;
	Room->CustomEliminationIntervalTs = 10;
	Room->CustomEliminationRanking = 0;
	Room->CustomRoundNum = 2;
	Room->CustomN2OMode = 0;
	Room->CustomN2OEffect = 100;
	Room->CustomSmallSpray = 1;
	Room->CustomCollision = 0;
	Room->CustomGem = 1;
	Room->CustomCostume = 0;
	Room->CustomModification = 1;
	Room->CustomMapNum = 2;
	Room->CustomMapIDList[0] = mapID;
	Room->CustomMapIDList[1] = mapID;
	Room->CustomMapIDList[2] = 0;
	Room->CustomNoCollisionSeconds = 5;
	Room->MapID = mapID;
	Room->CustomStageMapOrder[0] = 0;
	Room->CustomStageMapOrder[1] = 0;
	Room->CustomStageMapOrder[2] = 0;
	Room->CustomStageMapOrderNum = 0;
	Room->CustomStageMapSeed = 0;

	BYTE scheme[172] = {};
	BuildCustomSchemeWire172(scheme, Room);
	Room->CustomSchemeWire.assign(scheme, scheme + sizeof(scheme));
	NormalizeCustomSchemeForOfficial(Room, reason ? reason : "12p-default-normalize");

	QQS_DebugPrintf10001("[FLOW][12P-CUSTOM-DEFAULT] RoomID=%u Reason=%s SeatNum=%u Base=%u Sub=%u SchemeID=%u Map=[%u,%u,%u] SchemeLen=%zu\n",
		Room->ID, reason ? reason : "", (unsigned)Room->SeatNum,
		Room->CustomBaseGameMode, Room->CustomSubGameMode, Room->CustomSchemeID,
		Room->CustomMapIDList[0], Room->CustomMapIDList[1], Room->CustomMapIDList[2],
		Room->CustomSchemeWire.size());
	return true;
}

static BYTE GetConfiguredRoomSeatNum(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return 0;
	if (!IsTwelvePlayerRoom(Room))
		return (BYTE)GetRoomSeatCapacity(Room);

	const int seatLimit = GetRoomSeatLimit(Room);
	int configuredSeatNum = 0;
	for (int i = 0; i < seatLimit && i < ROOM_SEAT_MAX; ++i)
	{
		// SeatNum 是当前开放座位数，不是 12P 房壳类型。已坐人的位置即使
		// 收到异常锁座状态也仍然属于有效座位，线路上的占用态由 Player 写成 2。
		if (Room->SeatState[i] != 0 ||
			(i < (int)Room->Player.size() && Room->Player[i]))
		{
			++configuredSeatNum;
		}
	}

	if (configuredSeatNum == 0)
	{
		configuredSeatNum = Room->SeatNum;
		if (configuredSeatNum < 1 || configuredSeatNum > seatLimit)
			configuredSeatNum = seatLimit;
	}
	return (BYTE)configuredSeatNum;
}

static void NormalizeTwelvePlayerRoomWireState(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room || !IsTwelvePlayerRoom(Room))
		return;

	BYTE oldSeatNum = Room->SeatNum;
	Room->TwelvePlayerCustomRoomShell = true;
	Room->SeatNum = GetConfiguredRoomSeatNum(Room);
	EnsureTwelvePlayerDefaultCustomScheme(Room, reason ? reason : "12p-wire-normalize");
	NormalizeCustomSchemeForOfficial(Room, reason ? reason : "12p-wire-normalize");

	if (oldSeatNum != Room->SeatNum)
	{
		QQS_DebugPrintf10001("[FLOW][12P-WIRE-SEAT-COUNT] Reason=%s RoomID=%u OldSeatNum=%u NewSeatNum=%u PresetOB=%u CurrentPlayerNum=%u CountPlayers=%d Locked=%u\n",
			reason ? reason : "", Room->ID,
			(unsigned)oldSeatNum, (unsigned)Room->SeatNum,
			(unsigned)Room->PresetOBNum, (unsigned)Room->CurrentPlayerNum,
			CountRoomPlayers(Room), (unsigned)(ROOM_SEAT_12_MAX - Room->SeatNum));
	}
}

static void NormalizeNormalCustomRoomWireState(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room || !IsCustomRoomFlow(Room) || IsTwelvePlayerRoom(Room))
		return;

	BYTE oldSeatNum = Room->SeatNum;
	BYTE oldPresetOBNum = Room->PresetOBNum;
	bool oldShell = Room->TwelvePlayerCustomRoomShell;

	Room->TwelvePlayerCustomRoomShell = false;
	if (Room->SeatNum == 0 || Room->SeatNum > ROOM_SEAT_NORMAL_MAX)
		Room->SeatNum = ROOM_SEAT_NORMAL_MAX;
	if (Room->PresetOBNum >= ROOM_SEAT_12_MAX - 1)
		Room->PresetOBNum = 4;

	if (oldSeatNum != Room->SeatNum || oldPresetOBNum != Room->PresetOBNum || oldShell)
	{
		QQS_DebugPrintf10001("[FLOW][104-CUSTOM-6P-SHELL-FIX] Reason=%s RoomID=%u OldSeatNum=%u NewSeatNum=%u OldPresetOB=%u NewPresetOB=%u Old12Shell=%d\n",
			reason ? reason : "", Room->ID,
			(unsigned)oldSeatNum, (unsigned)Room->SeatNum,
			(unsigned)oldPresetOBNum, (unsigned)Room->PresetOBNum,
			oldShell ? 1 : 0);
	}
}

static BYTE GetRoomWireSeatNum(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return 0;
	return GetConfiguredRoomSeatNum(Room);
}

static size_t WriteCustomRoomListTail102(BYTE*& p, const std::shared_ptr<RoomNode>& Room)
{
	static const size_t SchemeSize102 = 172;
	BYTE scheme[SchemeSize102] = {};
	BuildCustomSchemeWire172(scheme, Room);

	BYTE* begin = p;
	memset(p, 0, 0x25);
	p += 0x25;
	Write8(p, 1);                         // CustomSchemeCount / HasCustomScheme
	Write16(p, (USHORT)(SchemeSize102 + 2)); // nested length
	memcpy(p, scheme, SchemeSize102);
	p += SchemeSize102;
	memset(p, 0, 8);
	p += 8;
	return (size_t)(p - begin);
}

static UINT GetBorderTeamRoomListMapID(const std::shared_ptr<RoomNode>& Room)
{
	// 官服边境组队 103/104 抓包里等待房展示 MapID=11。
	// 102 大厅列表也用稳定展示图，避免把运行态/随机图带回大厅卡片。
	return 11;
}

static size_t WriteBorderTeamRoomListTail102(BYTE*& p, const std::shared_ptr<RoomNode>& Room)
{
	// 102 RoomInfo 固定前缀后，官服普通房还有 46 字节版本尾巴；
	// 旧代码边境组队只写 64 字节固定前缀，客户端房间列表侧容易按普通老结构兜底。
	// 这里不伪造自定义 SpeFlag=9，也不改 103/104，只补齐标准列表尾部。
	BYTE* begin = p;

	Write8(p, 0); // ValueNum
	Write8(p, 0); // ParaNum
	Write32(p, Room ? Room->ID : 0); // GSvrRoomNo / stable display key
	memset(p, 0, 20); // OwnerName[20]，当前服务端列表未维护房主名，保持空
	p += 20;
	Write8(p, 0); // RoomLabel
	Write8(p, 0); // RoomGameNum
	Write32(p, (DWORD)time(nullptr)); // LastGameEndTime / refresh timestamp
	Write32(p, 0); // RoomOtherTag
	Write8(p, 0); // HasSpringPropData
	Write8(p, 0); // HasCustomScheme
	Write32(p, 0); // UniqKey high
	Write32(p, 0); // UniqKey low

	return (size_t)(p - begin);
}

static void NotifyGlobalStoneForKartAndSkin(std::shared_ptr<ClientNode> Client);
static void NotifyKartStoneForKartAndSkin(std::shared_ptr<ClientNode> Client);

void NotifyRoomStateAfterEnter104(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, const char* tag)
{
	if (!Client || !Room)
		return;

	EnsureRoomOwnerAlive(Room, nullptr, tag ? tag : "post-104");
	// 该旧链只用于需要 owner-sync 的专用返回流程，不用于普通首次进房。
	NotifyChangeReadyState(Client, Client->Uin, Client->ReadyState);
	NotifyChangeOBState(Client, FALSE, Client->Uin);
	NotifyClientUpdatePlayerID(Client, Room);
	NotifyChangeRoomOwner(Client, Room);
}

static void NotifyGlobalStoneForKartAndSkin(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;
	NotifyUpdateKartSkillStoneInfoConfig(Client, Client->KartID);
	if (Client->KartSkinID != 0 && Client->KartSkinID != Client->KartID)
	{
		NotifyUpdateKartSkillStoneInfoConfig(Client, Client->KartSkinID);
		QQS_DebugPrintf10001("[FLOW][906-CUSTOM-SKIN] send global stone for skin Uin=%u KartID=%u SkinID=%u GlobalItemStoneID=%u\n",
			Client->Uin, Client->KartID, Client->KartSkinID, Client->GlobalItemStoneid);
	}
}

static void NotifyKartStoneForKartAndSkin(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;
	NotifyUpdateKartSkillStoneInfo(Client, Client->KartID);
	if (Client->KartSkinID != 0 && Client->KartSkinID != Client->KartID)
		NotifyUpdateKartSkillStoneInfo(Client, Client->KartSkinID);
	if (Client->GlobalStone == 1)
		NotifyGlobalStoneForKartAndSkin(Client);
}
int GVoice = 0;
static void NotifyBugletTalkInternal(std::shared_ptr<ClientNode> Client, const char* Content,
	short clength, char BugletType, const std::array<BYTE, 18>& contentId, bool CountTalk)
{
	if (!Client || !Content || clength <= 0)
		return;

	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;

	if (CountTalk)
	{
		Client->BugletTalk += 1;
		if (Client->Uin > 10000)
			PlayerDB_UpdateBugletTalk(Client);
	}


	BYTE* pBodyLen = p;
	Write16(p, 0); //BodyLen
	Write8(p, 1); //BugletNum
	for (size_t i = 0; i < 1; i++)
	{ //BugletInfo
		BYTE* pBugletInfo = p;
		Write16(pBugletInfo, 0); //len

		Write32(pBugletInfo, Client->Uin); //SrcUin
		memset(pBugletInfo, 0, 17);

		memcpy(pBugletInfo, Client->NickName, 17);

		pBugletInfo += 17;
		//WriteString(pBugletInfo, 0); //SrcNickName[]

		memset(pBugletInfo, 0, 17);//SrcGuildName[]
		memcpy(pBugletInfo, Client->GuildName, 17);
		pBugletInfo += 17;
		Write8(pBugletInfo, static_cast<BYTE>(Client->Gender | 0x40)); //Gender
		Write16(pBugletInfo, clength); //ContentLength
		memcpy(pBugletInfo, Content, clength);
		pBugletInfo += clength;


		Write8(pBugletInfo, BugletType); //BugletType
		Write32(pBugletInfo, 0); //Identify
		Write16(pBugletInfo, static_cast<USHORT>(Client ? Client->VipFlag : 0)); //VipFlag
		memcpy(pBugletInfo, contentId.data(), contentId.size()); //Contentid[]
		pBugletInfo += contentId.size();
		{ //NobleInfo
			BYTE* pNobleInfo = pBugletInfo;
			Write16(pNobleInfo, 0); //len

			Write32(pNobleInfo, Client->NobleID); //NobleID
			Write8(pNobleInfo, Client->NobleLevel); //NobleLevel
			Write32(pNobleInfo, Client ? Client->NoblePoint : 0); //NoblePoint
			Write32(pNobleInfo, -1); //NobleLeftDays

			len = pNobleInfo - pBugletInfo;
			Set16(pBugletInfo, (WORD)len);
			pBugletInfo += len;
		}
		{ //GuildVipBaseInfo
			BYTE* pGuildVipBaseInfo = pBugletInfo;
			Write16(pGuildVipBaseInfo, 0); //len

			Write8(pGuildVipBaseInfo, Client->GuildVipLevel); //GuildVipLevel
			Write32(pGuildVipBaseInfo, Client->GuildVipPoint); //GuildVipPoint

			len = pGuildVipBaseInfo - pBugletInfo;
			Set16(pBugletInfo, (WORD)len);
			pBugletInfo += len;
		}
		const BYTE hasLoverVip = (Client && Client->LoverVipLevel > 0) ? 1 : 0;
		Write8(pBugletInfo, hasLoverVip); //HasLoverVip
		for (size_t i = 0; i < hasLoverVip; i++)
		{ //LoverVipInfo
			BYTE* pLoverVipInfo = pBugletInfo;
			Write16(pLoverVipInfo, 0); //len

			Write8(pLoverVipInfo, Client->LoverVipLevel); //LoverVipLevel
			Write32(pLoverVipInfo, Client->LoverVipPoint); //LoverVipPoint
			Write8(pLoverVipInfo, 10); //GrowRate

			len = pLoverVipInfo - pBugletInfo;
			Set16(pBugletInfo, (WORD)len);
			pBugletInfo += len;
		}
		CharmVipPlayerStateRow cvState = QQS_LoadCharmVipState(Client ? Client->Uin : 0);
		const UINT consumeVipLevel = QQS_GetRoomConsumeVipLevel(Client ? Client->Uin : 0, Client);
		const UINT charmCareerLevel = QQS_GetCharmVipCareerLevel(cvState);
		Write8(pBugletInfo, Client->GardenLevel); //GardenLevel
		Write8(pBugletInfo, static_cast<BYTE>(std::min<UINT>(consumeVipLevel, 9))); //ConsumeVipLevel
		{ //EmperorInfo
			BYTE* pEmperorInfo = pBugletInfo;
			Write16(pEmperorInfo, 0); //len

			Write8(pEmperorInfo, Client->EmperorLevel); //EmperorLevel
			Write32(pEmperorInfo, Client->EmperorPoint); //EmperorPoint
			Write32(pEmperorInfo, -1); //EmperorLeftDays
			Write8(pEmperorInfo, 10); //EmperorGrowRate

			len = pEmperorInfo - pBugletInfo;
			Set16(pBugletInfo, (WORD)len);
			pBugletInfo += len;
		}
		Write8(pBugletInfo, 0); //HasCarLottoryRecord
		Write32(pBugletInfo, charmCareerLevel); //CharmCareerLevel
		Write8(pBugletInfo, QQS_GetCharmVipIconPreference(consumeVipLevel, charmCareerLevel)); //ShowConsumeVip

		len = pBugletInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	len = p - buf.data();
	Set16(pBodyLen, (WORD)len);
	std::vector<std::shared_ptr<ClientNode>> recipients;
	{
		std::lock_guard<std::recursive_mutex> lg(ClientMutex);
		recipients.reserve(Clients.size());
		for (const auto& entry : Clients) {
			if (entry.second) recipients.push_back(entry.second);
		}
	}
	// Retain client lifetimes, but never hold the global registry lock while
	// serializing/encrypting/sending a broadcast to every online player.
	for (const auto& DstClient : recipients)
	{
		if (DstClient)
		{
#ifdef ZingSpeed
			SendToClient(DstClient, 555, buf.data(), len, DstClient->ConnID, FE_PLAYER, 1, Notify);
#else
			const short bugletSrcId = Client->ConnID > 0 ? Client->ConnID : 1;
			SendToClientOfficial14(DstClient, 555, buf.data(), static_cast<DWORD>(len),
				bugletSrcId, FE_PLAYER, 0, Notify);
#endif
		}
	}
}

void NotifyBugletTalk(std::shared_ptr<ClientNode> Client, const char* Content, short clength, char BugletType)
{
	const auto contentId = QQS_MakeTalkContentId();
	NotifyBugletTalkInternal(Client, Content, clength, BugletType, contentId, true);
}

void NotifyBugletTalkNoCount(std::shared_ptr<ClientNode> Client, const char* Content, short clength, char BugletType)
{
	const auto contentId = QQS_MakeTalkContentId();
	NotifyBugletTalkInternal(Client, Content, clength, BugletType, contentId, false);
}

static BYTE GetQuickbugLeaveGameStageIndex(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return 0;
	if (Room->HasCustomScheme || Room->SpeFlag == 9)
		return Room->CustomCurrentStageIndex;
	return 0;
}

static bool IsQuickbugSinglePlayerRace(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return false;
	BYTE startNum = Room->RaceStartPlayerNum;
	if (startNum == 0)
	{
		if (Room->RaceActiveMask != 0)
		{
			int seatLimit = GetRoomSeatLimit(Room);
			for (char si = 0; si < seatLimit; si++)
			{
				if (Room->RaceActiveMask & GetRoomSeatBit(si))
					startNum++;
			}
		}
		else
		{
			startNum = (BYTE)Room->CurrentPlayerNum;
		}
	}
	return startNum <= 1;
}

static void SendQuickbugLeaveGame514(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, bool singleRace, USHORT LeaveGameType, const char* reason)
{
	if (!Client)
		return;

	// 管理聊天指令 "g" 的服务端快捷退赛，不是客户端 C2S 424；
	// 保持独立，不能标成或复用官服 ESC 短事务状态。
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;
	BYTE returnHall = singleRace ? 0 : 1;
	BYTE waitEnterRoom = singleRace ? 1 : 0;
	BYTE stageIndex = GetQuickbugLeaveGameStageIndex(Room);

	Write16(p, 0); //ResultID
#ifndef ZingSpeed
	Write8(p, stageIndex); //StageIndex
	Write8(p, returnHall); //ReturnHall
	Write8(p, waitEnterRoom); //WaitEnterRoom
	Write16(p, LeaveGameType); //LeaveGameType
	Write8(p, 0); //ParaNum
#endif
	len = p - buf.data();

	QQS_DebugPrintf10001("[FLOW][QUICKBUG-514] Reason=%s Uin=%u RoomID=%u Single=%d StartNum=%u ActiveMask=0x%04X CurrentPlayerNum=%u HasCustom=%d ReturnHall=%u WaitEnterRoom=%u LeaveGameType=%u PayloadLen=%zu\n",
		reason ? reason : "", Client->Uin, Room ? Room->ID : 0, singleRace ? 1 : 0,
		Room ? Room->RaceStartPlayerNum : 0, Room ? (unsigned)Room->RaceActiveMask : 0, Room ? (unsigned)Room->CurrentPlayerNum : 0,
		Room && Room->HasCustomScheme ? 1 : 0, returnHall, waitEnterRoom, LeaveGameType, len);
	SendToClient(Client, 514, buf.data(), len, Client->GameID, FE_GAMELOGIC, 1, Notify);
}

void QuickbugLeaveGame(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;
	std::shared_ptr<RoomNode> Room = NULL;
	if (Client->RoomID != 0)
	{
		Room = GetRoom(Client->RoomID);
	}
	bool singleRace = IsQuickbugSinglePlayerRace(Room);
	SendQuickbugLeaveGame514(Client, Room, singleRace, 0, singleRace ? "quickbug-single-room" : "quickbug-multi-hall");
	if (Room)
	{
		if (singleRace)
		{//单人退出直接到房间
			if (Room->ChatRoomType == 6)
			{
				//进入3d房间
				ResponseCreateRoom(Client, Room, 6);
				RequestEnter3DRoom(Client);
			}
			if (Room->Timer)
			{
				if (DeleteTimerQueueTimer(TimerQueue, Room->Timer, INVALID_HANDLE_VALUE))
				{
					Room->Timer = NULL;
				}
			}
			Room->startStatus = 1;
			Room->Callback = NULL;
			Client->ReadyState = QQS_ROOM_READY_STATE_READY;
			ResetNormalRaceWireState(Client);
			Client->GameID = 0;
			Client->isRace = 0;
		}
		else
		{//多人退出到大厅
			if (Room->ChatRoomType == 6)
			{
				//退出3d房间
				ResponseCreateRoom(Client, Room, 0);
			}
			//LeaveOutdoor(Client);
			LeaveRoom(Client, 0);
		}
	}
	LeaveBorderRoom(Client, 0);
	Client->TaskID = 0;
	Client->IsFinish = FALSE;
	Client->IsReady = FALSE;
}


void NotifyUpdateKartSkillStoneInfo3(std::shared_ptr<ClientNode> Client, int KartID)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;
	UINT skillstoneid[5] = { 21988,21982,21994,22018,21988 };
	//UINT skillstoneid[5] = { 21980,21986,22016,21992,22004 };
	Write32(p, Client->Uin); //Uin

	//KartStoneInfo
	BYTE* pKartStoneInfo = p;
	Write16(pKartStoneInfo, 0); //len
	Write32(pKartStoneInfo, KartID); //KartID
	Write32(pKartStoneInfo, 5); //StoneGrooveNum
	for (int i = 0; i < 5; i++)
	{
		BYTE* pStoneGrooveInfo = pKartStoneInfo;
		Write16(pStoneGrooveInfo, 0); //len

		Write32(pStoneGrooveInfo, 0); //StoneUseOccaType
		Write32(pStoneGrooveInfo, skillstoneid[i]); //SkillStoneID

		len = pStoneGrooveInfo - pKartStoneInfo;
		Set16(pKartStoneInfo, (WORD)len);
		pKartStoneInfo += len;
	}
	len = pKartStoneInfo - p;
	Set16(p, (WORD)len);
	p += len;
	len = p - buf.data();
	SendToClient(Client, 906, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}


void NotifyUpdateKartSkillStoneInfo2(std::shared_ptr<ClientNode> Client, int KartID)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;
	UINT skillstoneid[2] = { 21980,21986 };
	//UINT skillstoneid[5] = { 21980,21986,22016,21992,22004 };
	Write32(p, Client->Uin); //Uin

	//KartStoneInfo
	BYTE* pKartStoneInfo = p;
	Write16(pKartStoneInfo, 0); //len
	Write32(pKartStoneInfo, KartID); //KartID
	Write32(pKartStoneInfo, 2); //StoneGrooveNum
	for (int i = 0; i < 2; i++)
	{
		BYTE* pStoneGrooveInfo = pKartStoneInfo;
		Write16(pStoneGrooveInfo, 0); //len

		Write32(pStoneGrooveInfo, 0); //StoneUseOccaType
		Write32(pStoneGrooveInfo, skillstoneid[i]); //SkillStoneID

		len = pStoneGrooveInfo - pKartStoneInfo;
		Set16(pKartStoneInfo, (WORD)len);
		pKartStoneInfo += len;
	}
	len = pKartStoneInfo - p;
	Set16(p, (WORD)len);
	p += len;
	len = p - buf.data();
	SendToClient(Client, 906, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}

void NotifyClientUpdatePlayerID(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room)
{
	if (!Client || !Room)
		return;
	BYTE buf[8192]; BYTE* p = buf; size_t len;
	BYTE* pPlayerNum = p;
	Write16(p, 0); //PlayerNum
	USHORT playerNum = 0;
	int seatLimit = GetRoomSeatLimit(Room);
	for (char i = 0; i < seatLimit && i < (int)Room->Player.size(); i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			USHORT connPlayerID = GetRoomConnPlayerID(Room, RoomClient);
			if (connPlayerID == 0)
				continue;
			// CNotifyClientUpdatePlayerIDPara uses a length-prefixed PlayerIDInfo
			// array. The client reads each row as:
			// uint16 RowLen(8) + uint32 Uin + uint16 PlayerID.
			BYTE* pPlayerIDInfo = p;
			Write16(pPlayerIDInfo, 0); //len
			Write32(pPlayerIDInfo, RoomClient->Uin); //Uin
			Write16(pPlayerIDInfo, connPlayerID); //PlayerID(ConnID)
			const size_t playerIDInfoLen = pPlayerIDInfo - p;
			Set16(p, static_cast<WORD>(playerIDInfoLen));
			p += playerIDInfoLen;
			playerNum++;
		}
	}
	Set16(pPlayerNum, playerNum);

	len = p - buf;
	const short wireDstID = GetRoomWireDstIDForClient(Room, Client);
	QQS_DebugPrintf10001("[FLOW][PLAYERID-SYNC] Uin=%u RoomID=%u PlayerNum=%u OwnerID=%u SelfWireID=%u WireDstID=%d SeatLimit=%d PayloadLen=%zu WireBodyLen=%zu LengthPrefixed=1\n",
		Client->Uin, Room->ID, playerNum, GetRoomConnOwnerID(Room), GetRoomConnPlayerID(Room, Client), wireDstID, seatLimit, len, len + 2);
	SendToClientWithWireDstIDAndMsgSeq(Client, 0x5F46, buf, len,
		Room->ID, FE_ROOM, wireDstID, 0, Notify);
}

void WINAPI OnPrepareTest(std::shared_ptr<RoomNode> Room)
{
	UINT time = 0;
	char PlayerNum = Room->CurrentPlayerNum;
	std::shared_ptr<ClientNode> RoomClient;
	BOOL AllReady = TRUE;

	while (time < SyncTime)
	{
		for (char i = 0; i < 6; i++)
		{
			RoomClient = Room->Player[i];
			if (RoomClient)
			{
				if (RoomClient->IsReady == FALSE)
				{
					AllReady = FALSE;
				}
			}
		}
		if (AllReady)
		{
			break;//所有人准备好就马上跳出循环否则继续等待
		}
		AllReady = TRUE;
		time += 1000;
		Sleep(1000);//每休眠一秒进行一次验证 超时自动跳出
	}


	for (char i = 0; i < 6; i++)
	{
		if (PlayerNum == 0)
		{
			return;//防止房间删除后没人了还继续执行后续代码
		}
		RoomClient = Room->Player[i];
		if (RoomClient)
		{
			if (RoomClient->IsReady == TRUE)
			{
				// 551/28346 already follows 509 in RequestStartGame.
				// OnPrepareTest only gates 120-ready clients before 511.
			}
			else
			{
				// Not ready after SyncTime: only kick with 514/LeaveRoom.
				// Do not send 551 here; it must stay before 120 on the normal start path.

				SendToClient(RoomClient, 514, (const BYTE*)"\x00\x00\x00\x00", 4, RoomClient->GameID, FE_GAMELOGIC, 1, Notify);
				if (Room->ChatRoomType == 6)
				{
					//异常退出3d房间
					ResponseCreateRoom(RoomClient, Room, 0);
				}
				//LeaveOutdoor(RoomClient);
				LeaveRoom(RoomClient, 0);//异常退出房间
				PlayerNum--;
			}
		}
	}

	if (Room->isSkip == 1) {
		CreateRoomTimer(Room, SkipDancetime, OnBegin);//游戏开始跳舞的时间 0直接跳过
	}
	else {
		CreateRoomTimer(Room, DanceTime, OnBegin);//游戏开始跳舞的时间 0直接跳过
	}


}
std::shared_ptr<RoomNode> GetBorderRoom(long RoomID)
{
	std::shared_ptr<RoomNode> Room = NULL;
	std::lock_guard<std::recursive_mutex> lg(Mutex);
	auto iter = BorderRooms.find(RoomID);
	if (iter != BorderRooms.end())
	{
		Room = iter->second;
	}
	else
	{

	}
	return Room;


}

void RecycleRoomID(int id)
{
	std::lock_guard<std::recursive_mutex> lg(Mutex);
	recycledRoomIDs.push(id);
	Rooms.erase(id);
}

void RecycleBorderRoomID(int id)
{
	std::lock_guard<std::recursive_mutex> lg(Mutex);
	recycledRoomIDs.push(id);
	BorderRooms.erase(id);
}

std::shared_ptr<RoomNode> NewBorderRoom()
{
	std::shared_ptr<RoomNode> Room = std::make_shared<RoomNode>();
	std::lock_guard<std::recursive_mutex> lg(Mutex);

	if (!recycledRoomIDs.empty()) {
		Room->ID = recycledRoomIDs.front();
		recycledRoomIDs.pop();
	}
	else 
	{
		SeqRoomID++;
		Room->ID = SeqRoomID;
	}

	BorderRooms[Room->ID] = Room;
	return Room;
}

std::shared_ptr<RoomNode> NewRoom()
{
	std::shared_ptr<RoomNode> Room = std::make_shared<RoomNode>();
	std::lock_guard<std::recursive_mutex> lg(Mutex);


	if (!recycledRoomIDs.empty()) 
	{
		Room->ID = recycledRoomIDs.front();
		recycledRoomIDs.pop();
	}
	else 
	{
		SeqRoomID++;
		Room->ID = SeqRoomID;
	}

	Room->startStatus = 1;
	Room->BaseGameMode = 0;
	Room->SubGameMode = 0;
	Rooms[Room->ID] = Room;

	return Room;
}


std::shared_ptr<RoomNode> GetRoom(long RoomID)
{
	std::shared_ptr<RoomNode> Room = NULL;
	std::lock_guard<std::recursive_mutex> lg(Mutex);
	//DWORD dwStart = GetTickCount64(); //取windows启动到现在的流逝时间(毫秒)
	auto iter = Rooms.find(RoomID);
	if (iter != Rooms.end())
	{
		Room = iter->second;
	}
	else
	{
		//Room = iter->second;
		//printf("无效的房间ID:%d\n", RoomID);//产生房间错误如边境 3d休闲 竞速等会产生空指针
		//最好都提前执行LeaveRoom
	}
	//DWORD dwUsed = GetTickCount64() - dwStart; //计算该函数所消耗的时间
	//printf("Room Search lost:%d ms\n", dwUsed);
	return Room;


}

std::shared_ptr<RoomNode> GetActiveRaceRoom(const std::shared_ptr<ClientNode>& Client)
{
	if (!Client)
		return nullptr;

	std::shared_ptr<RoomNode> Room;
	if (Client->BorderRoomID != 0)
		Room = GetBorderRoom(Client->BorderRoomID);
	else if (Client->RoomID != 0)
		Room = GetRoom(Client->RoomID);

	if (!Room || Client->CurrentRoom != Room.get())
		return nullptr;
	return Room;
}

void PrintRoom()
{
	std::lock_guard<std::recursive_mutex> lg(Mutex);
	std::map<long, std::shared_ptr<RoomNode>>::iterator iter;
	iter = Rooms.begin();
	while (iter != Rooms.end())
	{
		std::shared_ptr<RoomNode> Room = iter->second;
		printf("RoomID:%d, ", Room->ID);
		printf("BaseGameMode:%d, ", Room->BaseGameMode);
		printf("SubGameMode:%d, ", Room->SubGameMode);
		printf("ServerID:%d, ", Room->ServerID);
		printf("RoomOwnerID:%d, ", Room->RoomOwnerID);
		printf("Name:%s\n", Room->Name);
		printf("Flag:%d, ", Room->Flag);
		printf("Password:[redacted]\n");
		int seatLimit = GetRoomSeatLimit(Room);
		for (char i = 0; i < seatLimit; i++)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			if (RoomClient)
			{
				printf("\tUIN:%d, SeatID:%d\n", RoomClient->Uin, i);
			}
		}
		iter++;
	}
}

VOID WINAPI OnTimer(PVOID p, BOOLEAN b)
{
	try {
		std::shared_ptr<RoomNode>* RoomPtr = reinterpret_cast<std::shared_ptr<RoomNode>*>(p);
		if (RoomPtr)
		{
			auto& SharedRoom = *RoomPtr;
			if (SharedRoom)
			{
				std::lock_guard<std::shared_mutex> lock(SharedRoom->mutex);
				if (SharedRoom->Timer)
				{
					DeleteTimerQueueTimer(TimerQueue, SharedRoom->Timer, NULL);
					SharedRoom->Timer = NULL;
					auto Callback = SharedRoom->Callback;
					if (Callback)
					{
						Callback(SharedRoom);
					}
					else
					{
						QQS_DebugPrintf10001("[ROOM-TIMER-CB-NULL] RoomID=%d Timer fired but Callback=NULL, skip\n", SharedRoom->ID);
					}
				}
			}
			delete RoomPtr;
		}
	}
	catch (const std::exception& e) {
		std::cerr << "Exception in OnTimer: " << e.what() << std::endl;
	}
	catch (...)
	{
		printf("OnTimer Exception!!!\n");
	}
}

void CreateRoomTimer(std::shared_ptr<RoomNode> Room, int Timeout, RoomOnTimer Callback) {
	if (Room == nullptr || TimerQueue == nullptr) {
		std::wcout << L"错误：Room 或 TimerQueue 为 null\n";
		return;
	}

	if (Callback == NULL) {
		QQS_DebugPrintf10001("[ROOM-TIMER-CB-NULL] refuse create timer RoomID=%d Timeout=%d\n", Room->ID, Timeout);
		return;
	}

	Room->Callback = Callback;
	auto RoomPtr = new std::shared_ptr<RoomNode>(Room);
	BOOL result = CreateTimerQueueTimer(
		&Room->Timer,           // Timer handle
		TimerQueue,             // Timer queue
		OnTimer,                // Callback function
		RoomPtr,                   // Parameter passed to callback
		Timeout,                // Initial wait time (ms)
		0,                      // No periodicity (one-time timer)
		WT_EXECUTEINTIMERTHREAD // Execute in timer thread
	);

	if (!result) {
		std::wcout << L"错误：创建定时器失败 (错误代码: " << GetLastError() << L")\n";
		Room->Timer = NULL; // 确保失败时将计时器设为 NULL
	}
}
//房间排序
bool CompareRoomNode(const std::shared_ptr<RoomNode> room1, const std::shared_ptr<RoomNode> room2)
{
	// 若room1的Callback为空，room2的Callback不为空，则room1排在room2前面
	if (room1->Callback == nullptr && room2->Callback != nullptr)
		return true;
	// 若room1的Callback不为空，room2的Callback为空，则room1排在room2后面
	if (room1->Callback != nullptr && room2->Callback == nullptr)
		return false;
	// 若room1的BaseGameMode为19，而room2的BaseGameMode不为19，则room1排在room2后面(紫钻练习场房间排最后)
	if (room1->BaseGameMode == 19 && room2->BaseGameMode != 19)
		return false;
	// 若room1的BaseGameMode不为19，而room2的BaseGameMode为19，则room1排在room2前面(紫钻练习场房间排最后)
	if (room1->BaseGameMode != 19 && room2->BaseGameMode == 19)
		return true;
	// 当两个RoomNode对象的Callback都为空时，按照ID从大到小排序
	return room1->ID > room2->ID;
}

void RequestGetRoomList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{

	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);

	USHORT StartRoomIdx = Read16(Body);//索引0 6 8
	USHORT GetRoomNum = Read16(Body);//0x18 一次性读取24个房间
	USHORT BaseGameMode = Read16(Body);
	USHORT SubGameMode = Read16(Body);
	BYTE FBLev = Read8(Body);
	BYTE FBSeason = Read8(Body);
	BYTE RoomFlag = Read8(Body);
	BYTE GetFlag = Read8(Body);
	WORD RoomID = Read16(Body);

	std::lock_guard<std::recursive_mutex> lg(Mutex);
	std::map<long, std::shared_ptr<RoomNode>>::iterator iter = Rooms.begin();;

	if (GetFlag == 1)
	{
		ResponseGetRoomList2(Client, 10000 + RoomID);
	}
	else
	{
		std::vector<std::shared_ptr<RoomNode>> AllRooms(60);
		int RoomNum = 0;
		int index = 0;
		while (iter != Rooms.end())
		{
			if (RoomNum == GetRoomNum && GetFlag == 0)
			{
				//hasMore = TRUE;
				break;
			}
			if (index < StartRoomIdx)
			{
				index++;
				iter++;
				continue;
			}
			std::shared_ptr<RoomNode> Room = iter->second;
			if (!IsNormalRoomVisible(Room))
			{
				index++;
				iter++;
				continue;
			}

			if (Room->BaseGameMode == 4)
			{
				index++;
				iter++;
				continue;
			}

			//滑板
			if (Client->ServerID == 900 && Room->ServerID != 900)
			{
				index++;
				iter++;
				continue;
			}
			if (Client->ServerID != 900 && Room->ServerID == 900)
			{
				index++;
				iter++;
				continue;
			}



			//轮滑
			if (Client->ServerID == 1000 && Room->ServerID != 1000)
			{
				index++;
				iter++;
				continue;
			}
			if (Client->ServerID != 1000 && Room->ServerID == 1000)
			{
				index++;
				iter++;
				continue;
			}
			// 只查询当前频道房间
	// 		if (Room->ServerID != Client->ServerID)
	// 		{ 
	// 			index++;
	// 			iter++;
	// 			continue;
	// 		}

			/*else if (Client->ServerID != 256 && Room->BaseGameMode == 18)
			{
				index++;
				iter++;
				continue;
			}
			else if (Client->ServerID == 256 && Room->BaseGameMode != 18)
			{
				index++;
				iter++;
				continue;
			}*/
			AllRooms[RoomNum] = Room;
			RoomNum++;
			index++;
			iter++;
		}
		// 对AllRooms进行排序
		std::sort(AllRooms.begin(), AllRooms.begin() + RoomNum, CompareRoomNode);
		ResponseGetRoomList(Client, BaseGameMode, SubGameMode, AllRooms, RoomNum, StartRoomIdx, RoomFlag);
	}
}
void ResponseGetRoomList(std::shared_ptr<ClientNode> Client, USHORT BaseGameMode, USHORT SubGameMode, std::vector<std::shared_ptr<RoomNode>>& AllRooms, int RoomNum, int StartRoomIdx, BYTE RoomFlag)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;

	
	Write16(p, 0); //ResultID

	Write16(p, 0); //CurTotalRoomNum
	Write16(p, StartRoomIdx); //StartIdx

	
	Write16(p, RoomNum); //RoomNum
	{
		for (int i = 0; i < RoomNum; i++)
		{
			std::shared_ptr<RoomNode> Room = AllRooms[i];
			const bool listCustomRoom = IsCustomRoomFlow(Room);
			const bool listBorderTeamRoom = IsBorderTeamMatchRoom(Room);
			if (listBorderTeamRoom)
			{
				NormalizeOfficial6BorderTeamRoom(Room);
			}
			NormalizeTwelvePlayerRoomWireState(Room, "102-list");
			const UINT listMapID = listCustomRoom ? GetCustomDisplayMapID(Room) : (listBorderTeamRoom ? GetBorderTeamRoomListMapID(Room) : Room->MapID);
			const BYTE listSpeFlag = (listBorderTeamRoom || IsTwelvePlayerRoom(Room)) ? 0 : GetEffectiveRoomSpeFlag(Room);
			const UINT listRoomNo = listCustomRoom ? GetRoomPublicNo(Client, Room) : Room->ID;
			const USHORT listSvrID = listCustomRoom ? GetRoomListSvrID(Client, Room) : 0;
			const USHORT listFlag = listBorderTeamRoom ? 0x0600 : Room->Flag;
			const USHORT listSceneID = listBorderTeamRoom ? 0 : Room->SceneID;
			const BYTE listWireSeatNum = GetRoomWireSeatNum(Room);
			BYTE* pRooms = p;
			Write16(pRooms, 0); //len

			Write16(pRooms, Room->ID); //RoomID
			Write16(pRooms, Room->BaseGameMode); //BaseGameMode
			Write16(pRooms, Room->SubGameMode); //SubGameMode
			Write8(pRooms, Room->CurrentPlayerNum); //CurrentPlayerNum
			Write8(pRooms, listWireSeatNum); //TotalSeatNum
			const bool listRoomCanEnter = (Room->Callback == NULL && listWireSeatNum > 1 && !IsBorderTeamRoomBusyForEnter(Room));
			if (listRoomCanEnter)
			{
				Write8(pRooms, 1); //Status 房间状态 1为可进?0为不可进? 可以通过callback判断
			}
			else
				Write8(pRooms, 0);
			Write32(pRooms, listMapID); //MapID

			Write16(pRooms, listFlag); //Flag


			memcpy(pRooms, Room->Name, MaxRoomName); //RoomName
			pRooms += MaxRoomName;

			Write8(pRooms, 0); //HasFBInfo



			Write8(pRooms, listSpeFlag); //SpeFlag
			Write32(pRooms, listRoomNo); //RoomNo
			Write16(pRooms, listSvrID); //SvrId
			//if(Room->ChatRoomType==0x6)
				//Write16(pRooms, 0x190); //SceneID

			Write16(pRooms, listSceneID); //SceneID
			Write8(pRooms, 0); //CurrentOBNum
			Write8(pRooms, 0); //TotalOBNum
			Write8(pRooms, 0); //SpecialActivityStatus
			Write8(pRooms, Room->AllowAutoStart); //AllowAutoStart
			Write32(pRooms, 0); //Gender
			Write32(pRooms, 0); //LocaleCode
			Write8(pRooms, 0); //TalkRoomBGID
			Write8(pRooms, 0); //SpecialActivityIDNum
			Write8(pRooms, 0); //SpecialActivityInfoNum

			if (listCustomRoom)
				WriteCustomRoomListTail102(pRooms, Room);
			else if (listBorderTeamRoom)
				WriteBorderTeamRoomListTail102(pRooms, Room);

			len = pRooms - p;
			Set16(p, (WORD)len);
			if (listBorderTeamRoom)
			{
				QQS_DebugPrintf10001("[FLOW][102-BORDER-TEAM-LIST] Uin=%u RoomID=%u Len=%zu Base=%u Sub=%u SeatNum=%u Status=%u Busy=%u Flag=0x%04X MapID=%u CreateRoomType=%u ServerMatchType=%u TeamMatchType=%u\n",
					Client ? Client->Uin : 0, Room->ID, len, Room->BaseGameMode, Room->SubGameMode,
					(unsigned)Room->SeatNum, (unsigned)(listRoomCanEnter ? 1 : 0), IsBorderTeamRoomBusyForEnter(Room) ? 1u : 0u,
					(unsigned)listFlag, listMapID, (unsigned)Room->CreateRoomType,
					(unsigned)Room->ServerMatchType, (unsigned)Room->TeamMatchType);
			}
			p += len;
		}
	}
	

	Write8(p, 0); //ReasonLen

	Write16(p, BaseGameMode); //BaseGameMode
	Write16(p, SubGameMode); //SubGameMode
	Write8(p, 0); //FBAvailableLev
	Write8(p, 0); //FBLev
	Write8(p, 0); //FBSeason

	Write16(p, 0); //ChannelStatus
	Write8(p, RoomFlag); //RoomFlag
	Write8(p, 0); //GetType
	len = p - buf.data();
	SendToClient(Client, 102, buf.data(), len, Client->ServerID, FE_GAMESVRD, 1, Response);
}


void ResponseGetRoomList2(std::shared_ptr<ClientNode> Client, WORD RoomID)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;


	Write16(p, 0); //ResultID

	Write16(p, 0); //CurTotalRoomNum
	Write16(p, 0); //StartIdx

	int RoomNum = 0;
	int BaseGameMode = 0;
	int SubGameMode = 0;
	int Flag = 0;

	std::shared_ptr<RoomNode> Room = GetRoom(RoomID);
	if (Room && IsNormalRoomVisible(Room))
	{
		BaseGameMode = Room->BaseGameMode;
		SubGameMode = Room->SubGameMode;
		Flag = Room->Flag;
		RoomNum = 1;
		Write16(p, RoomNum); //RoomNum
	}
	else
	{
		Write16(p, 0); //RoomNum
	}
	
	for (int i = 0; i < RoomNum; i++)
	{
		const bool listCustomRoom = IsCustomRoomFlow(Room);
		const bool listBorderTeamRoom = IsBorderTeamMatchRoom(Room);
		if (listBorderTeamRoom)
		{
			NormalizeOfficial6BorderTeamRoom(Room);
		}
		NormalizeTwelvePlayerRoomWireState(Room, "102-list2");
		const UINT listMapID = listCustomRoom ? GetCustomDisplayMapID(Room) : (listBorderTeamRoom ? GetBorderTeamRoomListMapID(Room) : Room->MapID);
		const BYTE listSpeFlag = (listBorderTeamRoom || IsTwelvePlayerRoom(Room)) ? 0 : GetEffectiveRoomSpeFlag(Room);
		const UINT listRoomNo = listCustomRoom ? GetRoomPublicNo(Client, Room) : Room->ID;
		const USHORT listSvrID = listCustomRoom ? GetRoomListSvrID(Client, Room) : 0;
		const USHORT listFlag = listBorderTeamRoom ? 0x0600 : Room->Flag;
		const USHORT listSceneID = listBorderTeamRoom ? 0 : Room->SceneID;
		const BYTE listWireSeatNum = GetRoomWireSeatNum(Room);
		BYTE* pRooms = p;
		Write16(pRooms, 0); //len


		Write16(pRooms, Room->ID); //RoomID
		Write16(pRooms, Room->BaseGameMode); //BaseGameMode
		Write16(pRooms, Room->SubGameMode); //SubGameMode
		Write8(pRooms, Room->CurrentPlayerNum); //CurrentPlayerNum
		Write8(pRooms, listWireSeatNum); //TotalSeatNum
		const bool listRoomCanEnter = (Room->Callback == NULL && listWireSeatNum > 1 && !IsBorderTeamRoomBusyForEnter(Room));
		if (listRoomCanEnter)
		{
			Write8(pRooms, 1); //Status 房间状态 1为可进?0为不可进? 可以通过callback判断
		}
		else
			Write8(pRooms, 0);
		Write32(pRooms, listMapID); //MapID

		Write16(pRooms, listFlag); //Flag


		memcpy(pRooms, Room->Name, MaxRoomName); //RoomName
		pRooms += MaxRoomName;

		Write8(pRooms, 0); //HasFBInfo



		Write8(pRooms, listSpeFlag); //SpeFlag
		Write32(pRooms, listRoomNo); //RoomNo
		Write16(pRooms, listSvrID); //SvrId
		//if(Room->ChatRoomType==0x6)
			//Write16(pRooms, 0x190); //SceneID

		Write16(pRooms, listSceneID); //SceneID
		Write8(pRooms, 0); //CurrentOBNum
		Write8(pRooms, 0); //TotalOBNum
		Write8(pRooms, 0); //SpecialActivityStatus
		Write8(pRooms, Room->AllowAutoStart); //AllowAutoStart
		Write32(pRooms, 0); //Gender
		Write32(pRooms, 0); //LocaleCode
		Write8(pRooms, 0); //TalkRoomBGID
		Write8(pRooms, 0); //SpecialActivityIDNum
		Write8(pRooms, 0); //SpecialActivityInfoNum

		if (listCustomRoom)
			WriteCustomRoomListTail102(pRooms, Room);
		else if (listBorderTeamRoom)
			WriteBorderTeamRoomListTail102(pRooms, Room);


		len = pRooms - p;
		Set16(p, (WORD)len);
		if (listBorderTeamRoom)
		{
			QQS_DebugPrintf10001("[FLOW][102-BORDER-TEAM-LIST2] Uin=%u RoomID=%u Len=%zu Base=%u Sub=%u SeatNum=%u Flag=0x%04X MapID=%u CreateRoomType=%u ServerMatchType=%u TeamMatchType=%u\n",
				Client ? Client->Uin : 0, Room->ID, len, Room->BaseGameMode, Room->SubGameMode,
				(unsigned)Room->SeatNum, (unsigned)listFlag, listMapID, (unsigned)Room->CreateRoomType,
				(unsigned)Room->ServerMatchType, (unsigned)Room->TeamMatchType);
		}
		p += len;

	}
	
	Write8(p, 0); //ReasonLen

	Write16(p, BaseGameMode); //BaseGameMode
	Write16(p, SubGameMode); //SubGameMode
	Write8(p, 0); //FBAvailableLev
	Write8(p, 0); //FBLev
	Write8(p, 0); //FBSeason

	Write16(p, 0); //ChannelStatus
	Write8(p, Flag); //RoomFlag
	Write8(p, 1); //GetType
	len = p - buf.data();
	SendToClient(Client, 102, buf.data(), len, Client->ServerID, FE_GAMESVRD, 1, Response);
}

static std::string BuildCreateRoomGVoiceRoomName(const std::shared_ptr<ClientNode>& Client, DWORD ServerTime)
{
	return "1-" + std::to_string(Client ? Client->Uin : 0) + "-" + std::to_string(ServerTime);
}

static void ApplyAuroraWorkToRoom(
	const std::shared_ptr<RoomNode>& Room,
	const QQSpeedAuroraOfficial::Work& Work)
{
	Room->AuroraUGC = true;
	Room->AuroraWorkID = Work.workId;
	Room->AuroraVersion = Work.version;
	Room->AuroraRenderDataCosURL = Work.renderUrl;
	Room->AuroraMD5 = Work.md5;
	Room->MapID = Work.mapId;
}

void RequestCreateRoom(
	std::shared_ptr<ClientNode> Client,
	BYTE* Body,
	size_t BodyLen,
	std::uint64_t AuroraWorkID,
	std::uint64_t RoomUGCSkinWorkID)
{
	const QQSpeedAuroraOfficial::Work* auroraWork = AuroraWorkID != 0
		? QQSpeedAuroraOfficial::FindWork(AuroraWorkID)
		: nullptr;
	if (AuroraWorkID != 0 && !auroraWork)
	{
		QQS_DebugPrintf10001(
			"[AURORA][103-CREATE-ABORT] Uin=%u WorkID=%llu Error=unknown-work\n",
			Client ? Client->Uin : 0,
			static_cast<unsigned long long>(AuroraWorkID));
		return;
	}

	RequestCancelMatch(Client);
	LeaveRoom(Client, 0);
	LeaveBorderRoom(Client, 0);
	LeaveOutdoor(Client);
	SpeedLadder_ClearRaceContext(Client, "create-room-reset");
	SpeedLadder_ClearReturnContext(Client, "create-room-reset");
	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);

	USHORT BaseGameMode = Read16(Body);
	//if (BaseGameMode == 1 || BaseGameMode == 2 || BaseGameMode == 19)
	//{
	//}
	//else
	//{
	//	ResponseStartGame(Client, "暂未开放");
	//	return;
	//}

	USHORT SubGameMode = Read16(Body);
	if (BaseGameMode == 4)
	{
		SubGameMode = 1;//舞蹈固定1
	}
#ifdef ZingSpeed
	char* RoomName = (char*)Body;
	Body += MaxRoomName;

	BYTE SeatNum = Read8(Body);
	BYTE Flag = Read8(Body);
#else
	USHORT Flag = Read16(Body);

	char* RoomName = (char*)Body;
	/*for (int i = 0; i < 22; i++)
	{
		if (Body[i] != 0)
		{
			break;
		}
		else if (Body[21] == 0&&i==21)
		{
			//房间创建失败 不能空名字
			return;
		}
	}*/
	Body += MaxRoomName;

	BYTE SeatNum = Read8(Body);
#endif

	char Password[16] = { 0 };
	if (Flag)
	{
		memcpy(Password, Body, 16);
	}
	Body += 16;

	BYTE IsNewerOpen = Read8(Body);
	BYTE FBSeason = Read8(Body);
	BYTE Contex = Read8(Body);
	BYTE FBLev = Read8(Body);

#ifndef ZingSpeed
	BYTE SpeFlag = Read8(Body);

	BYTE MoreInfoNum = Read8(Body);
	for (BYTE i = 0; i < MoreInfoNum; i++)
	{
		//m_astMoreInfos[].m_iTaskID
		Body += Get16(Body);
	}

	USHORT CreateRoomType = Read16(Body);
	USHORT ServerMatchType = Read16(Body);
	USHORT TeamMatchType = Read16(Body);
	BYTE ChatRoomType = Read8(Body);
	USHORT SceneID = Read16(Body);
	BYTE PresetOBNum = Read8(Body);
	BYTE AllowAutoStart = Read8(Body);
	BYTE TalkRoomBGID = Read8(Body);

	BYTE ValueNum = Read8(Body);
	for (BYTE i = 0; i < ValueNum; i++)
	{
		//m_aiEnterRoomValue[]
		Read32(Body);
	}

	// 官服 Lua 走的是普通竞速房承载赛道天梯；部分客户端/私服样本房名不一定稳定等于
	// "SpeedLadder Room"。这里不再把房名作为硬门槛，只作为日志证据。
	const bool speedLadderRoomName = QQS_FixedRoomNameEquals(RoomName, "SpeedLadder Room");
	const bool speedLadderRoomShape = (BaseGameMode == 1 && SubGameMode == 1 && SeatNum >= 1 && SeatNum <= ROOM_SEAT_NORMAL_MAX && AllowAutoStart == 0);
	const bool speedLadderLobbyArmed = SpeedLadder_IsLobbyArmed(Client);
	const bool speedLadderRoomCandidate = speedLadderRoomShape && speedLadderLobbyArmed;

	char DebugRoomName[MaxRoomName + 1] = { 0 };
	char DebugRoomNameHex[MaxRoomName * 3 + 1] = { 0 };
	memcpy(DebugRoomName, RoomName, MaxRoomName);
	DebugRoomName[MaxRoomName] = '\0';
	for (int i = 0; i < MaxRoomName; i++)
	{
		sprintf_s(DebugRoomNameHex + i * 3, sizeof(DebugRoomNameHex) - i * 3, "%02X ", (unsigned char)RoomName[i]);
	}
#if 0
	// Disabled: noisy normal create-room trace.
	printf("[FLOW][103-CREATE-REQ] ClientUin=%u ReqUin=%u ServerID=%u BodyLen=%zu Base=%u Sub=%u Flag=0x%04X SeatNum=%u RoomName='%s' NameHex=%s IsNewerOpen=%u FBSeason=%u Contex=%u FBLev=%u SpeFlag=%u MoreInfoNum=%u CreateRoomType=%u ServerMatchType=%u TeamMatchType=%u ChatRoomType=%u SceneID=%u PresetOBNum=%u AllowAutoStart=%u TalkRoomBGID=%u ValueNum=%u\n",
		Client->Uin,
		Uin,
		Client->ServerID,
		BodyLen,
		BaseGameMode,
		SubGameMode,
		(unsigned)Flag,
		(unsigned)SeatNum,
		DebugRoomName,
		DebugRoomNameHex,
		(unsigned)IsNewerOpen,
		(unsigned)FBSeason,
		(unsigned)Contex,
		(unsigned)FBLev,
		(unsigned)SpeFlag,
		(unsigned)MoreInfoNum,
		(unsigned)CreateRoomType,
		(unsigned)ServerMatchType,
		(unsigned)TeamMatchType,
		(unsigned)ChatRoomType,
		(unsigned)SceneID,
		(unsigned)PresetOBNum,
		(unsigned)AllowAutoStart,
		(unsigned)TalkRoomBGID,
		(unsigned)ValueNum);
#endif
#endif

	//auto ServerLogger = spdlog::get("SpeedServer_logger");
	////ServerLogger->info("[玩家创建房间]");
	//ServerLogger->info("[玩家创建房间][Uin:{:d}]]", Client->Uin);
	//ServerLogger->flush();

	ScrubClientFromRoomsForNormalFlow(Client, 0, "create-room");

	std::shared_ptr<RoomNode> Room = NewRoom();
	if (!Room)
	{
		return;
	}
	Room->BaseGameMode = BaseGameMode;
	Room->SubGameMode = SubGameMode;
	if (RoomName && Room->Name) {
		// 使用 strnncpy_s 进行安全的字符串复制
		strncpy_s(Room->Name, MaxRoomName, RoomName, _TRUNCATE);
		Room->Name[MaxRoomName - 1] = '\0';  // 手动添加字符串结尾的 null 字符
	}


	Room->ServerID = Client->ServerID;
	Client->RoomID = Room->ID;
	Client->CurrentRoom = Room.get();
	Room->TwelvePlayerCustomRoomShell = (SeatNum == ROOM_SEAT_12_MAX);
	const bool officialBorderTeamCreateRequest = IsOfficialBorderTeamCreateRequest(
		BaseGameMode,
		SubGameMode,
		SeatNum);
	QQS_DebugPrintf10001("[FLOW][103-CREATE-SHELL] Uin=%u RoomID=%u Base=%u Sub=%u SpeFlag=%u SeatNumReq=%u PresetOBReq=%u CreateRoomType=%u ServerMatchType=%u TeamMatchType=%u ChatRoomType=%u SceneID=%u Is12Shell=%d BorderRaw=%d\n",
		Client ? Client->Uin : 0, Room->ID,
		(unsigned)BaseGameMode, (unsigned)SubGameMode, (unsigned)SpeFlag,
		(unsigned)SeatNum, (unsigned)PresetOBNum, (unsigned)CreateRoomType,
		(unsigned)ServerMatchType, (unsigned)TeamMatchType, (unsigned)ChatRoomType,
		(unsigned)SceneID, Room->TwelvePlayerCustomRoomShell ? 1 : 0,
		officialBorderTeamCreateRequest ? 1 : 0);
	if (SeatNum == ROOM_SEAT_12_MAX)
		Room->SeatNum = ROOM_SEAT_12_MAX;
	else if (SeatNum >= 1 && SeatNum <= ROOM_SEAT_NORMAL_MAX)
		Room->SeatNum = SeatNum;
	else
		Room->SeatNum = ROOM_SEAT_NORMAL_MAX;
	Room->CurrentPlayerNum = 1;
	const UINT roomConsumeVipLevel = Client
		? QQS_GetRoomConsumeVipLevel(Client->Uin, Client)
		: 0;
	Room->Flag = QQS_BuildOfficialRoomFlag(
		Flag,
		Client ? Client->NobleLevel : 0,
		Client ? Client->GuildVipLevel : 0,
		Client ? Client->LoverVipLevel : 0,
		roomConsumeVipLevel);
	memcpy(Room->Password, Password, 16);
	Room->ChatRoomType = ChatRoomType;
	Room->SpeFlag = SpeFlag;
	Room->PresetOBNum = PresetOBNum;
	Room->SceneID = SceneID;
	Room->CreateRoomType = CreateRoomType;
	Room->ServerMatchType = ServerMatchType;
	Room->TeamMatchType = TeamMatchType;
	Room->AllowAutoStart = AllowAutoStart;
	Room->RoomUGCSkinWorkID = RoomUGCSkinWorkID;
	Room->RoomUGCSkinEquipperUin = RoomUGCSkinWorkID != 0 ? Client->Uin : 0;
	if (auroraWork)
	{
		ApplyAuroraWorkToRoom(Room, *auroraWork);
		Client->MapID = auroraWork->mapId;
	}
	if (speedLadderRoomCandidate)
	{
		Client->SpeedLadderRacePending = true;
		Client->SpeedLadderRaceActive = false;
		Client->SpeedLadderRaceMapID = 0;
		Client->SpeedLadderRaceSeasonID = SpeedLadder_GetCurrentSeasonID();
		Client->SpeedLadderRaceArmTick = GetTickCount();
		QQS_DebugPrintf10001("[SpeedLadder-RACE] pending source=normal-room-lobby Uin=%u RoomID=%u Season=%u LobbyStage=%u LobbyDelta=%u Base=%u Sub=%u SeatNum=%u AllowAutoStart=%u RoomNameMatch=%u RoomName='%s'\n",
			Client ? Client->Uin : 0, Room->ID, Client ? Client->SpeedLadderRaceSeasonID : 0,
			Client ? Client->SpeedLadderLobbyStage : 0,
			(Client && Client->SpeedLadderLobbyTick) ? (GetTickCount() - Client->SpeedLadderLobbyTick) : 0xFFFFFFFF,
			(unsigned)BaseGameMode, (unsigned)SubGameMode, (unsigned)SeatNum, (unsigned)AllowAutoStart,
			speedLadderRoomName ? 1 : 0, DebugRoomName);
	}
	else if (speedLadderRoomName || speedLadderLobbyArmed)
	{
		QQS_DebugPrintf10001("[SpeedLadder-RACE] create-room not-armed Uin=%u RoomID=%u LobbyStage=%u LobbyTick=%u Shape=%u NameMatch=%u Base=%u Sub=%u SeatNum=%u AllowAutoStart=%u\n",
			Client ? Client->Uin : 0, Room->ID, Client ? Client->SpeedLadderLobbyStage : 0, Client ? Client->SpeedLadderLobbyTick : 0,
			speedLadderRoomShape ? 1 : 0, speedLadderRoomName ? 1 : 0,
			(unsigned)BaseGameMode, (unsigned)SubGameMode, (unsigned)SeatNum, (unsigned)AllowAutoStart);
	}
	if (officialBorderTeamCreateRequest)
	{
		ApplyOfficial6BorderTeamRoomState(Room);
	}
	else if (IsBorderTeamMatchRoom(Room))
	{
		NormalizeOfficial6BorderTeamRoom(Room);
	}
	QQS_DebugPrintf10001("[VIP][103-ROOM-FLAG] Uin=%u RoomID=%u Req=0x%04X NobleLv=%u GuildLv=%u LoverLv=%u ConsumeLv=%u Final=0x%04X\n",
		Client ? Client->Uin : 0,
		Room ? Room->ID : 0,
		static_cast<unsigned>(Flag),
		Client ? Client->NobleLevel : 0,
		Client ? Client->GuildVipLevel : 0,
		Client ? Client->LoverVipLevel : 0,
		roomConsumeVipLevel,
		Room ? static_cast<unsigned>(Room->Flag) : 0);
	int roomSeatLimit = GetRoomSeatLimit(Room);
	int roomSeatCapacity = GetRoomSeatCapacity(Room);
	for (int i = 0; i < ROOM_SEAT_MAX; i++)
	{
		Room->Player[i] = nullptr;
		Room->SeatState[i] = (i < roomSeatCapacity) ? 1 : 0;
	}

	Room->Player[0] = Client;
	Client->SeatID = 0;
	Room->RoomOwnerUin = Client->Uin;
	Room->RoomOwnerID = GetRoomConnPlayerID(Room, Client);
	if (IsTwelvePlayerRoom(Room))
		EnsureTwelvePlayerDefaultCustomScheme(Room, "create-room-seat12");
	Client->ReadyState = QQS_ROOM_READY_STATE_READY;
	Client->ChatRoomID = -1;

	if (BaseGameMode == 0x2e)//龙晶大闯关
	{
		Client->MapID = Room->MapID = 464;
		ResponseCreateHyperSpaceJumboRoom(Client, Room);
		ResponseStartGame(Client, "当前模式为跃动大闯关\n发送/C01我要玩龙晶/C00即可玩龙晶大闯关\n发送/C01我要玩幻域/C00即可玩幻域大闯关\n发送/C01我要玩冲刺/C00即可玩冲刺大闯关\n发送/C01我要玩跃动/C00即可玩跃动大闯关");
		return;
	}
	else
	{
		ResponseCreateRoom(Client, Room, ChatRoomType);
		//设置观战
		SendToClient(Client, 134, (BYTE*)"\x00\x00\x00\x00", 4, Client->ConnID, FE_PLAYER, 1, Response);
		if (ChatRoomType == 0x6)
		{
			RequestEnter3DRoom(Client);
		}
	}
}
void ResponseCreateRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, BYTE ChatRoomType)
{

	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;
	NormalizeTwelvePlayerRoomWireState(Room, "103-create-wire");
	const bool createCustomRoom = IsCustomRoomFlow(Room);
	const bool createTwelveRoom = IsTwelvePlayerRoom(Room);
	const bool createThreeDRoom = ChatRoomType == 6;
	const UINT createMapID = Room->AuroraUGC
		? 0
		: (createCustomRoom ? GetCustomDisplayMapID(Room) : Room->MapID);
	const BYTE createSpeFlag = GetEffectiveRoomSpeFlag(Room);
	const UINT createRoomNo = createThreeDRoom
		? static_cast<UINT>(Room->ServerID) * 10000u + Room->ID
		: (createCustomRoom ? GetRoomPublicNo(Client, Room) : Room->ID);
	const USHORT createGuildScoreTotalAward = createTwelveRoom ? 10 : 0;
	const BYTE createWireSeatNum = GetRoomWireSeatNum(Room);
	const DWORD createServerTime = (DWORD)time(nullptr);
	if (Room)
		Room->GVoiceRoomName = BuildCreateRoomGVoiceRoomName(Client, createServerTime);
#ifndef ZingSpeed
	BYTE createWireSpeFlag = 0xa;
	if (IsBorderTeamMatchRoom(Room))
		createWireSpeFlag = Room->SpeFlag;
	else if (createTwelveRoom)
		createWireSpeFlag = 0; // 官服 12P 创建 103：这里是 0，不是自定义 SpeFlag=9。
	else if (createCustomRoom)
		createWireSpeFlag = createSpeFlag;
#endif
	
	/*WriteHex(p, "000000E600010001000031312E3220CBCD41B3B5CFF7B7E7000000000000000006000000000000000000671F63CE00000000000000000000000000000000000A00008F489600000000000000000000000004010000000000000000000000000017312D313732383531303534362D3137333031313034313400000000000000000001000A000000000000000A00");
	len = p - buf.data();
	SendToClient(Client, 103, buf.data(), len, Client->ServerID, FE_GAMESVRD, 1, Response);
	return;*/

	Write16(p, 0); //ResultID

	Write16(p, Room->ID); //RoomID
	Write16(p, Room->BaseGameMode); //BaseGameMode
	Write16(p, Room->SubGameMode); //SubGameMode
#ifdef ZingSpeed
	memcpy(p, Room->Name, MaxRoomName); //RoomName
	p += MaxRoomName;

	Write8(p, createWireSeatNum); //SeatNum
	Write8(p, (BYTE)Room->Flag); //Flag
#else
	Write16(p, Room->Flag); //Flag

	memcpy(p, Room->Name, MaxRoomName); //RoomName
	p += MaxRoomName;

	Write8(p, createWireSeatNum); //SeatNum
#endif // ZingSpeed


	BYTE createSeatID = 0;
	if (Client && Client->RoomID == Room->ID && Client->SeatID >= 0 && Client->SeatID < GetRoomSeatLimit(Room))
	{
		createSeatID = (BYTE)Client->SeatID;
	}
	Write8(p, createSeatID); //SeatID
	Write8(p, 0); //TeamID

	Write8(p, 0); //ReasonLen


	Write8(p, 0); //LoverRaceOrPro

	Write32(p, createMapID); //MapID
	Write8(p, 0); //DebutOrX5
	Write32(p, createServerTime); //ServerTime
	Write32(p, 0); //PreKnowMapID
	Write32(p, 0); //PreKnownMusicID

	Write8(p, 0); //HasFBInfo
	/*
m_astFBInfo[%d].m_bAvailableLev
m_astFBInfo[%d].m_bLev
m_astFBInfo[%d].m_bSubLev
m_astFBInfo[%d].m_iBossID
m_astFBInfo[%d].m_bFBSeason
	*/

	Write32(p, 0); //HasFBInfoGuildScoreInNextGame
	Write8(p, 0); //HasFBInfoContex
	Write16(p, 0); //GuildScoreAlreadyAward
	Write16(p, createGuildScoreTotalAward); //GuildScoreTotalAward
#ifndef ZingSpeed
	Write8(p, createWireSpeFlag); //HasFBInfoSpeFlag
	Write32(p, createRoomNo); //RoomNo

	Write8(p, 0); //MoreInfoNum
	/*
m_astMoreInfos[%d].m_iTaskID
	*/


	Write16(p, 0); //RaceCardNums
	/*
m_astRaceCardAddValueInfo[%d].m_uiRaceCardID
m_astRaceCardAddValueInfo[%d].m_uiAddValue
	*/

	Write16(p, Room->CreateRoomType); //CreateRoomType
	Write16(p, Room->ServerMatchType); //ServerMatchType
	Write16(p, Room->TeamMatchType); //TeamMatchType
	Write8(p, ChatRoomType); //ChatRoomType 3d房间是6
	if (ChatRoomType == 6)
	{
		Write16(p, 0x190);
	}
	else
	{
		Write16(p, 0); //SceneID 0x190
	}

	Write8(p, Room->PresetOBNum); //PresetOBNum
	Write8(p, Room->AllowAutoStart ? 1 : 0); //AllowAutoStart
	Write8(p, 0); //TalkRoomBGID
	Write16(p, 0); //Role

	Write8(p, 0); //ParaNum
	//ParaList[]

	Write8(p, 0); //ValueNum
	//EnterRoomValue[]

	Write32(p, 0); //PlayerEquipKartID
	Write32(p, 0); //RankedMatchTestDriveKartID
	if (GVoice|| version > 1.3) {
		WriteString_MC(p, Room->GVoiceRoomName.c_str()); //GVoiceRoomNameLen
	}
	else {
		Write8(p, 0); 
	}
	
	Write8(p, 0); //GVoiceReasonLen
	Write8(p, 0); //TDCExcluItemNum
	//TDCExclusiveItemID[]

	Write8(p, 0); //TestKartGameType
	Write32(p, 0); //GameTestKartID

	Write8(p, 0); //HasStarsFightingInfo
	/*
m_stStarsFightingInfo[%d].m_iStar
m_stStarsFightingInfo[%d].m_iStarVal
m_stStarsFightingInfo[%d].m_iStarsFightingStarLevel
	*/

	Write8(p, 0); //HasYuLeJiaNianHuaInfo
	/*
m_stYuLeJiaNianHuaInfo[%d].m_uiMode
m_stYuLeJiaNianHuaInfo[%d].m_uiScore
m_stYuLeJiaNianHuaInfo[%d].m_uchRankRangNum
m_stYuLeJiaNianHuaInfo[%d].m_auiRankRange[%d]
m_stYuLeJiaNianHuaInfo[%d].m_uchRankScoreNum
m_stYuLeJiaNianHuaInfo[%d].m_auiRankScore[%d]
m_stYuLeJiaNianHuaInfo[%d].m_uchInRecommend
m_stYuLeJiaNianHuaInfo[%d].m_uchInDoubleScore
	*/
#endif
	//WriteHex(p, "01000A000000000000000A00");
	Write8(p, 0); //m_uchHasNoVipStartGameInfo
	Write8(p, 0); //m_ucRoomLabel
	Write8(p, 0); //m_ucWidenP
	Write8(p, 0); //m_ucHasSpringPropDaga

	Write32(p, 0); //m_uiMyWinStreak
	Write32(p, createTwelveRoom ? 4 : 0); //m_uiRoomOtherTag
	Write32(p, static_cast<DWORD>(Room->AuroraWorkID >> 32)); //m_u64AuroraWorkID high
	Write32(p, static_cast<DWORD>(Room->AuroraWorkID)); //m_u64AuroraWorkID low

	len = p - buf.data();
	const short createWireDstID = GetClientWirePlayerHeaderID(Client);
	int inboundWireMsgSeq = -1;
	int inboundProtocolSeq = -1;
	const bool hasInboundWireMsgSeq = TryGetCurrentInboundWireMsgSeq(Client, inboundWireMsgSeq);
	const bool hasInboundProtocolSeq = TryGetCurrentInboundProtocolSeq(Client, inboundProtocolSeq);
	const int createWireMsgSeq = hasInboundWireMsgSeq ? inboundWireMsgSeq : -1;
	QQS_DebugPrintf10001("[FLOW][103-CREATE-OFFICIAL] Uin=%u RoomID=%u HeadSeq=%lu ServerTime=%lu GVoice='%s' WireDstID=%d WireMsgSeq=%d ProtocolSeq=%d AuroraWorkID=%llu PayloadLen=%zu WireBodyLen=%zu\n",
		Client ? Client->Uin : 0,
		Room ? Room->ID : 0,
		Client ? (unsigned long)Client->HeadSeq : 0,
		(unsigned long)createServerTime,
		Room ? Room->GVoiceRoomName.c_str() : "",
		(int)createWireDstID,
		createWireMsgSeq,
		hasInboundProtocolSeq ? inboundProtocolSeq : -1,
		static_cast<unsigned long long>(Room->AuroraWorkID),
		len,
		len + sizeof(USHORT));
	if (hasInboundWireMsgSeq && inboundWireMsgSeq >= 0 &&
		hasInboundProtocolSeq && inboundProtocolSeq >= 0)
	{
		SendCorrelatedResponseToClient(
			Client, 103, buf.data(), static_cast<DWORD>(len), Client->ServerID,
			FE_GAMESVRD, createWireDstID, inboundWireMsgSeq, inboundProtocolSeq);
	}
	else
	{
		SendToClientWithWireDstIDAndMsgSeq(
			Client, 103, buf.data(), static_cast<DWORD>(len), Client->ServerID,
			FE_GAMESVRD, createWireDstID, createWireMsgSeq, Response);
	}
}

void ResponseBorderTeamReturnRoom103(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room)
{
	if (!Client || !Room)
	{
		return;
	}

	NormalizeOfficial6BorderTeamRoom(Room);
	Room->AllowAutoStart = 1;

	char seatID = Client->SeatID;
	if (seatID < 0 || seatID > 5 || Room->Player[seatID] != Client)
	{
		for (char i = 0; i < 6; ++i)
		{
			if (Room->Player[i] == Client)
			{
				seatID = i;
				break;
			}
		}
	}
	if (seatID < 0 || seatID > 5)
	{
		seatID = 0;
	}
	Client->SeatID = seatID;

	std::vector<BYTE> buf(819200);
	BYTE* p = buf.data();
	size_t len;
	const bool createCustomRoom = IsCustomRoomFlow(Room);
	const UINT createMapID = createCustomRoom ? GetCustomDisplayMapID(Room) : Room->MapID;
	const BYTE createSpeFlag = GetEffectiveRoomSpeFlag(Room);
	const UINT createRoomNo = createCustomRoom ? GetRoomPublicNo(Client, Room) : Room->ID;
	const BYTE chatRoomType = Room->ChatRoomType;

	Write16(p, 0); //ResultID

	Write16(p, Room->ID); //RoomID
	Write16(p, Room->BaseGameMode); //BaseGameMode
	Write16(p, Room->SubGameMode); //SubGameMode
#ifdef ZingSpeed
	memcpy(p, Room->Name, MaxRoomName); //RoomName
	p += MaxRoomName;

	Write8(p, Room->SeatNum); //SeatNum
	Write8(p, (BYTE)Room->Flag); //Flag
#else
	Write16(p, Room->Flag); //Flag

	memcpy(p, Room->Name, MaxRoomName); //RoomName
	p += MaxRoomName;

	Write8(p, Room->SeatNum); //SeatNum
#endif // ZingSpeed

	Write8(p, (BYTE)Client->SeatID); //SeatID
	Write8(p, 0); //TeamID

	Write8(p, 0); //ReasonLen

	Write8(p, 0); //LoverRaceOrPro

	Write32(p, createMapID); //MapID
	Write8(p, 0); //DebutOrX5
	Write32(p, (DWORD)time(nullptr)); //ServerTime
	Write32(p, 0); //PreKnowMapID
	Write32(p, 0); //PreKnownMusicID

	Write8(p, 0); //HasFBInfo
	Write32(p, 0); //HasFBInfoGuildScoreInNextGame
	Write8(p, 0); //HasFBInfoContex
	Write16(p, 0); //GuildScoreAlreadyAward
	Write16(p, 0); //GuildScoreTotalAward
#ifndef ZingSpeed
	Write8(p, IsBorderTeamMatchRoom(Room) ? Room->SpeFlag : (createCustomRoom ? createSpeFlag : 0xa)); //HasFBInfoSpeFlag
	Write32(p, createRoomNo); //RoomNo

	Write8(p, 0); //MoreInfoNum
	Write16(p, 0); //RaceCardNums

	Write16(p, Room->CreateRoomType); //CreateRoomType
	Write16(p, Room->ServerMatchType); //ServerMatchType
	Write16(p, Room->TeamMatchType); //TeamMatchType
	Write8(p, chatRoomType); //ChatRoomType
	if (chatRoomType == 6)
	{
		Write16(p, 0x190);
	}
	else
	{
		Write16(p, 0); //SceneID
	}

	Write8(p, Room->PresetOBNum); //PresetOBNum
	Write8(p, Room->AllowAutoStart ? 1 : 0); //AllowAutoStart
	Write8(p, 0); //TalkRoomBGID
	Write16(p, 0); //Role

	Write8(p, 0); //ParaNum
	Write8(p, 0); //ValueNum

	Write32(p, 0); //PlayerEquipKartID
	Write32(p, 0); //RankedMatchTestDriveKartID
	if (GVoice || version > 1.3) {
		WriteString_MC(p, Room->GVoiceRoomName.c_str()); //GVoiceRoomNameLen
	}
	else {
		Write8(p, 0);
	}

	Write8(p, 0); //GVoiceReasonLen
	Write8(p, 0); //TDCExcluItemNum

	Write8(p, 0); //TestKartGameType
	Write32(p, 0); //GameTestKartID

	Write8(p, 0); //HasStarsFightingInfo
	Write8(p, 0); //HasYuLeJiaNianHuaInfo
#endif
	Write8(p, 0); //m_uchHasNoVipStartGameInfo
	Write8(p, 0); //m_ucRoomLabel
	Write8(p, 0); //m_ucWidenP
	Write8(p, 0); //m_ucHasSpringPropDaga
	Write32(p, 0); //m_uiMyWinStreak
	Write32(p, 0); //m_uiRoomOtherTag
	Write32(p, static_cast<DWORD>(Room->AuroraWorkID >> 32)); //m_u64AuroraWorkID high
	Write32(p, static_cast<DWORD>(Room->AuroraWorkID)); //m_u64AuroraWorkID low

	len = p - buf.data();
	SendToClient(Client, 103, buf.data(), len, Client->ServerID, FE_GAMESVRD, 1, Response);
}

void ResponseCreateHyperSpaceJumboRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room)
{

	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;

	Write16(p, 0); //ResultID

	Write16(p, Room->ID); //RoomID
	Write16(p, Room->BaseGameMode); //BaseGameMode
	Write16(p, Room->SubGameMode); //SubGameMode

	Write16(p, 0x600); //Flag

	memcpy(p, Room->Name, MaxRoomName); //RoomName
	p += MaxRoomName;

	Write8(p, Room->SeatNum); //SeatNum



	Write8(p, 0); //SeatID
	Write8(p, 0); //TeamID

	Write8(p, 0); //ReasonLen


	Write8(p, 0); //LoverRaceOrPro

	Write32(p, Room->MapID); //MapID
	Write8(p, 0); //DebutOrX5
	Write32(p, (DWORD)time(nullptr)); //ServerTime
	Write32(p, 0); //PreKnowMapID
	Write32(p, 0); //PreKnownMusicID

	Write8(p, 0); //HasFBInfo
	/*
m_astFBInfo[%d].m_bAvailableLev
m_astFBInfo[%d].m_bLev
m_astFBInfo[%d].m_bSubLev
m_astFBInfo[%d].m_iBossID
m_astFBInfo[%d].m_bFBSeason
	*/

	Write32(p, 0); //HasFBInfoGuildScoreInNextGame
	Write8(p, 0); //HasFBInfoContex
	Write16(p, 0); //GuildScoreAlreadyAward
	Write16(p, 10); //GuildScoreTotalAward

	Write8(p, 0); //HasFBInfoSpeFlag
	Write32(p, Room->ID); //RoomNo

	Write8(p, 0); //MoreInfoNum
	/*
m_astMoreInfos[%d].m_iTaskID
	*/


	Write16(p, 0); //RaceCardNums
	/*
m_astRaceCardAddValueInfo[%d].m_uiRaceCardID
m_astRaceCardAddValueInfo[%d].m_uiAddValue
	*/

	Write16(p, 1); //CreateRoomType
	Write16(p, 17); //ServerMatchType
	Write16(p, 0); //TeamMatchType
	Write8(p, 0); //ChatRoomType 3d房间是6
	
	Write16(p, 0); //SceneID 0x190

	Write8(p, 0); //PresetOBNum
	Write8(p, Room->AllowAutoStart); //AllowAutoStart
	Write8(p, 1); //TalkRoomBGID
	Write16(p, 8); //Role

	Write8(p, 0); //ParaNum
	//ParaList[]

	Write8(p, 0); //ValueNum
	//EnterRoomValue[]

	Write32(p, 0); //PlayerEquipKartID
	Write32(p, 0); //RankedMatchTestDriveKartID
	if (GVoice || version > 1.3) {
		WriteString_MC(p, Room->GVoiceRoomName.c_str()); //GVoiceRoomNameLen
	}
	else {
		Write8(p, 0);
	}

	Write8(p, 0); //GVoiceReasonLen
	Write8(p, 0); //TDCExcluItemNum
	//TDCExclusiveItemID[]

	Write8(p, 0); //TestKartGameType
	Write32(p, 0); //GameTestKartID

	Write8(p, 0); //HasStarsFightingInfo
	/*
m_stStarsFightingInfo[%d].m_iStar
m_stStarsFightingInfo[%d].m_iStarVal
m_stStarsFightingInfo[%d].m_iStarsFightingStarLevel
	*/

	Write8(p, 0); //HasYuLeJiaNianHuaInfo
	/*
m_stYuLeJiaNianHuaInfo[%d].m_uiMode
m_stYuLeJiaNianHuaInfo[%d].m_uiScore
m_stYuLeJiaNianHuaInfo[%d].m_uchRankRangNum
m_stYuLeJiaNianHuaInfo[%d].m_auiRankRange[%d]
m_stYuLeJiaNianHuaInfo[%d].m_uchRankScoreNum
m_stYuLeJiaNianHuaInfo[%d].m_auiRankScore[%d]
m_stYuLeJiaNianHuaInfo[%d].m_uchInRecommend
m_stYuLeJiaNianHuaInfo[%d].m_uchInDoubleScore
	*/

	Write8(p, 0); //m_uchHasNoVipStartGameInfo
	Write8(p, 0); //m_ucRoomLabel
	Write8(p, 0); //m_ucWidenP
	Write8(p, 0); //m_ucHasSpringPropDaga
	Write32(p, 0); //m_uiMyWinStreak
	Write32(p, 0); //m_uiRoomOtherTag

	//WriteHex(p, "01000A000000000000000A00");
	len = p - buf.data();
	SendToClient(Client, 103, buf.data(), len, Client->ServerID, FE_GAMESVRD, 1, Response);
}

void RequestEnterInvitedRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room)
{
	if (!IsNormalRoomVisible(Room))
	{
		return;
	}
	ScrubClientFromRoomsForNormalFlow(Client, 0, "enter-invited-room");
	if (IsBorderTeamMatchRoom(Room))
	{
		NormalizeOfficial6BorderTeamRoom(Room);
	}
	int seatLimit = GetRoomSeatLimit(Room);
	int seatCapacity = GetRoomSeatCapacity(Room);
	if (Room->Callback != NULL || Room->CurrentPlayerNum >= seatCapacity)
	{
		return;
	}
	for (int i = 0; i < seatLimit; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient == 0 && Room->SeatState[i] == 1)
		{
			Client->SeatID = i;
			Client->RoomID = Room->ID;
			Client->CurrentRoom = Room.get();
			Client->ReadyState = QQS_ROOM_READY_STATE_NOT_READY;
			Room->Player[i] = Client;
			Room->CurrentPlayerNum++;
			Client->ChatRoomID = -1;

			if (Room->ChatRoomType == 6)
			{
				//进入3d房间
				ResponseCreateRoom(Client, Room, 6);
				RequestEnter3DRoom(Client);
				NotifyChangeRoomOwner(Client, Room);
			}
			else
			{
				ResponseEnterRoom(Client, Room, i, 0, 0, EnterRoom104RosterMode::OtherVisiblePlayers);
			}

			//通知其它玩家
			for (char i2 = 0; i2 < seatLimit; i2++)
			{
				std::shared_ptr<ClientNode> RoomClient = Room->Player[i2];
				if (RoomClient && RoomClient != Client)
				{
					NotifyEnterRoom(RoomClient, Room, Client, i);
				}
			}

			if (obs == 1) {
				if (Room->ChatRoomType != 6) {
					//观战
					for (char i5 = 0; i5 < seatLimit; i5++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[i5];
						if (RoomClient && RoomClient != Client)
						{
							if (Client->OBState == 1) {
								NotifyChangeOBState(RoomClient, Client->OBState, Client->Uin);
							}
							if (RoomClient->OBState == 1) {
								NotifyChangeOBState(Client, RoomClient->OBState, RoomClient->Uin);
							}
						}
					}
				}
			}
			//通知其它玩家

			break;
		}
	}
}

void RequestEnterRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (Client && Body && BodyLen >= 10)
	{
		USHORT TargetRoomID = Get16(Body + 8);
		std::shared_ptr<RoomNode> TargetRoom = GetRoom(TargetRoomID);
		if (IsBorderTeamRoomBusyForEnter(TargetRoom))
		{
			ResponseStartGame(Client, "border team room is matching or racing");
			return;
		}
	}
	// 取消当前匹配和离开任何已加入的房间
	RequestCancelMatch(Client);
	LeaveRoom(Client, 0);
	LeaveBorderRoom(Client, 0);
	LeaveOutdoor(Client);
	SpeedLadder_ClearReturnContext(Client, "enter-room-reset");
	//printf("RequestEnterRoom\n");
	//请求进入房间 
	//自行添加验证密码和游戏过程能否进入

	// 解析请求数据
	UINT Uin = Read32(Body);            // 用户ID
	UINT Time = Read32(Body);           // 时间戳
	USHORT RoomID = Read16(Body);       // 目标房间ID

	char Password[16] = { 0 };
	memcpy(Password, Body, 16);         // 获取输入的房间密码
	Body += 16;


	BYTE InviteType = Read8(Body);      // 邀请类型
	UINT InviterUin = Read32(Body);     // 邀请者ID
	BOOL HasMoreInfo = FALSE;

	// 获取指定房间的节点信息
	ScrubClientFromRoomsForNormalFlow(Client, 0, "enter-room");

	std::shared_ptr<RoomNode> Room = GetRoom(RoomID);
	if (Room && Room->LuckyMatchInternalRoom)
	{
		ResponseStartGame(Client, "internal lucky match room");
		return;
	}
	if (!Room)
	{
		// 如果房间不存在
		ResponseStartGame(Client, "对不起,您要加入的房间已经不存在,请选择加入其他房间!");
		return;
	}
	if (IsBorderTeamRoomBusyForEnter(Room))
	{
		ResponseStartGame(Client, "border team room is matching or racing");
		return;
	}
	if (IsBorderTeamMatchRoom(Room))
	{
		NormalizeOfficial6BorderTeamRoom(Room);
	}
	int seatLimit = GetRoomSeatLimit(Room);
	int seatCapacity = GetRoomSeatCapacity(Room);
	// 检查房间人数是否已满
	if (Room->CurrentPlayerNum >= seatCapacity) {
		ResponseStartGame(Client, "你要进入的房间已经满人了");
		return;
	}
	// 检查房间是否正在游戏中
	if (Room->Callback != NULL)
	{
		ResponseStartGame(Client, "对不起,您要加入的房间正在游戏中!");
		return;
	}
	// 如果用户不是管理员，则验证房间密码
	if (Client->admin != 1) {
		for (int i = 0; i < 15; i++)
		{
			if (Password[i + 1] != Room->Password[i + 1])
			{    //默认传0x30
				//从第1位开始比较 0位不管
				ResponseStartGame(Client, "房间密码错误!");
				return;
			}
		}
	}

	// 查找房间内的空座位
	for (int i = 0; i < seatLimit; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient == 0 && Room->SeatState[i] == 1)
		{
			Client->SeatID = i;
			Client->RoomID = RoomID;
			Client->CurrentRoom = Room.get();
			Client->ReadyState = QQS_ROOM_READY_STATE_NOT_READY;
			Room->Player[i] = Client;
			Room->CurrentPlayerNum++;
			Client->ChatRoomID = -1;
			// 根据房间类型处理用户加入房间的响应
			if (Room->ChatRoomType == 6)
			{
				//进入3d房间
				ResponseCreateRoom(Client, Room, 6);
				RequestEnter3DRoom(Client);
				NotifyChangeRoomOwner(Client, Room);
			}
			else
			{
				// 官服 104 返回进入者以外的现有玩家；进入者自己不在 roster 中。
				ResponseEnterRoom(Client, Room, i, 0, 0, EnterRoom104RosterMode::OtherVisiblePlayers);

				// 根据房间的不同模式发送相应的提示信息
				if (Room->Israce == 1) {
					Client->isRace = 1;
					ResponseStartGame(Client, "该房间已开启比赛模式！");
				}
				if (Room->IsAnonymous == 1) {
					Client->IsAnonymous = 1;
					ResponseStartGame(Client, "该房间已开启匿名模式！");
				}
				if (Room->IsStake == 1) {
					Client->IsStake = 1;
					ResponseStartGame(Client, "该房间已开启下注单挑模式！");
				}
				// 根据 IsBottomGame 的模式生成不同的提示信息
				if (Room->IsBottomGame == 1) {
					Client->IsBottomGame = 1;
					std::string msg = GetItemName(IsBottomGame[1]);
					msg = "该房间已开启垫底赛模式！消耗道具是/C01【" + GetItemName(IsBottomGame[1]) + "】/C00，扣除最后/C01【" + std::to_string(IsBottomGame[3]) + "】/C00名的道具，" + "扣除数量是：/C01【" + std::to_string(IsBottomGame[2]) + "】";
					ResponseStartGame(Client, msg.c_str());
				}
				if (Room->IsBottomGame == 2) {
					Client->IsBottomGame = 1;
					std::string msg = GetItemName(IsBottomGame[1]);
					msg = "该房间已开启垫底赛模式！消耗道具是/C01【" + GetItemName(IsBottomGame[1]) + "】/C00，扣除最后/C01【" + std::to_string(IsBottomGame[3]) + "】/C00名的道具，" + "扣除数量是：/C01【" + std::to_string(Room->BottomGameNum) + "】";
					ResponseStartGame(Client, msg.c_str());
				}
				if (Room->IsBottomGame == 4) {
					Client->IsBottomGame = 1;
					std::string msg = BuildBottomSuperMoneyNotice(4);
					ResponseStartGame(Client, msg.c_str());
				}
				if (Room->IsBottomGame == 6) {
					Client->IsBottomGame = 1;
					std::string msg = BuildBottomSuperMoneyNotice(6);
					ResponseStartGame(Client, msg.c_str());
				}
				if (Room->IsBottomGame == 7) {
					Client->IsBottomGame = 1;
					int Frirst = 5;
					int Second = 4;
					int Third = 3;
					int Four = 2;
					int Five = 1;
					std::string msg = "房主已开启竞技模式！你会获得道具/C01【" + GetItemName(106614) + "】/C00\n第一名/C01赢【" + std::to_string(Frirst) + "】/C00，第二名/C01赢【"
						+ std::to_string(Second) + "】/C00，第三名/C01赢【" + std::to_string(Third) + "】/C00第四名/C01赢【" + std::to_string(Four) + "】/C00第五名/C01赢【" + std::to_string(Five) + "】/C00";

					ResponseStartGame(Client, msg.c_str());
				}

				if (Room->IsBottomGame == 9) {
					std::string msg = GetItemName(Mantissa[3]);
					msg = "房主已开启尾数！消耗道具是/C01【" + GetItemName(Mantissa[3]) + "】/C00，最低下注/C01【" + std::to_string(Mantissa[1]) + "】/C00最高下注/C01【" + std::to_string(Mantissa[2]) + "】输入obs观战后，尾数有尾大，尾小，尾单 ，尾双，四种选择。例如输入：/C01尾数单100";
					ResponseStartGame(Client, msg.c_str());
				}

			}
			//通知其它玩家   通知房间内的其他玩家用户进入房间的信息
			for (char i2 = 0; i2 < seatLimit; i2++)
			{
				std::shared_ptr<ClientNode> RoomClient = Room->Player[i2];
				if (RoomClient && RoomClient != Client)
				{
					NotifyEnterRoom(RoomClient, Room, Client, i);
				}
			}
			// 如果是观战模式，发送观战状态通知
			if (obs == 1) {
				if (Room->ChatRoomType != 6) {
					//观战
					for (char i5 = 0; i5 < seatLimit; i5++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[i5];
						if (RoomClient && RoomClient != Client)
						{
							if (Client->OBState == 1) {
								NotifyChangeOBState(RoomClient, Client->OBState, Client->Uin);
							}
							if (RoomClient->OBState == 1) {
								NotifyChangeOBState(Client, RoomClient->OBState, RoomClient->Uin);
							}
						}
					}
				}
			}


			break;
		}
	}

}

static void WriteOfficialRoomPlayerVisibleInfo(BYTE*& p, const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& RoomClient, char SeatID)
{
	if (!RoomClient)
		return;
	const USHORT playerID = GetRoomConnPlayerID(Room, RoomClient);
	const bool isRoomOwner = (playerID != 0 && playerID == GetRoomConnOwnerID(Room));
	// PlayerRoomVisibleInfo 字段很大，底层字段 writer 已包含 21043 反编译确认的尾部字段；
	// 这里不复用旧 104/500 包体，只复用单个 visible 结构字段写入。
	WriteVisbleInfo(RoomClient, p, RoomClient->Uin, SeatID, (short)playerID, isRoomOwner);
}

static void WriteOfficialRoomPlayerRelationInfo(BYTE*& p, const std::shared_ptr<ClientNode>& RoomClient)
{
	BYTE* pPlayerRelationInfo = p;
	size_t len;
	Write16(pPlayerRelationInfo, 0); //len
	Write32(pPlayerRelationInfo, RoomClient ? RoomClient->Uin : 0); //SrcUin
	Write32(pPlayerRelationInfo, 0); //RelationFlag
	Write32(pPlayerRelationInfo, 0); //RelationUin
	memset(pPlayerRelationInfo, 0, MaxNickName);
	pPlayerRelationInfo += MaxNickName;
	Write32(pPlayerRelationInfo, 0); //EngageTime
	Write32(pPlayerRelationInfo, 0); //NextCanBookingTimeLimit
	Write32(pPlayerRelationInfo, 0); //BeginHoneyMoonTime
	Write32(pPlayerRelationInfo, 0); //EndHoneyMoonTime
	Write8(pPlayerRelationInfo, 0x4); //EngageFlag
	len = pPlayerRelationInfo - p;
	Set16(p, (WORD)len);
	p += len;
}

static void WriteOfficialRoomPlayerKartRefitInfo(BYTE*& p, const std::shared_ptr<ClientNode>& RoomClient)
{
	BYTE* pKartRefitInfo = p;
	size_t len;
	Write16(pKartRefitInfo, 0); //len
	Write32(pKartRefitInfo, RoomClient ? RoomClient->Uin : 0); //Uin
	Write32(pKartRefitInfo, RoomClient ? RoomClient->KartID : 0); //KartId
	Write32(pKartRefitInfo, 0); //RefitCout
	Write16(pKartRefitInfo, 0); //MaxFlags
	Write16(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.WWeight : 0); //WWeight
	Write16(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.SpeedWeight : 0); //SpeedWeight
	Write16(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.JetWeight : 0); //JetWeight
	Write16(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.SJetWeight : 0); //SJetWeight
	Write16(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.AccuWeight : 0); //AccuWeight
	Write32(pKartRefitInfo, 0); //ShapeRefitCount
	Write32(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.KartHeadRefitItemID : 0); //KartHeadRefitItemID
	Write32(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.KartTailRefitItemID : 0); //KartTailRefitItemID
	Write32(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.KartFlankRefitItemID : 0); //KartFlankRefitItemID
	Write32(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.KartTireRefitItemID : 0); //KartTireRefitItemID
	Write32(pKartRefitInfo, 0); //SecondRefitCount
	Write16(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.Speed2Weight : 0); //Speed2Weight
	Write16(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.DriftVecWeight : 0); //DriftVecWeight
	Write16(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.AdditionalZSpeedWeight : 0); //AdditionalZSpeedWeight
	Write16(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.AntiCollisionWeight : 0); //AntiCollisionWeight
	Write16(pKartRefitInfo, 0); //LuckyValue
	Write16(pKartRefitInfo, 0); //RefitLuckyValueMaxWeight
	Write32(pKartRefitInfo, RoomClient ? RoomClient->KartSkinID : 0); //ShapeSuitID
	Write8(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.LegendSuitLevel : 0); //LegendSuitLevel
	Write32(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.LegendSuitLevelChoice : 0); //LegendSuitLevelChoice
	Write32(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.ShapeLegendSuitID : 0); //ShapeLegendSuitID
	Write8(pKartRefitInfo, RoomClient ? RoomClient->ClientKartRefitItemInfo.ShapeOriginLevel : 0); //ShapeOriginLevel
	Write32(pKartRefitInfo, 0); //Roles
	Write32(pKartRefitInfo, 0); //Roles
	len = pKartRefitInfo - p;
	Set16(p, (WORD)len);
	p += len;
}

static void WriteOfficialRoomInfoChange(BYTE*& p)
{
	BYTE* pRoomInfoChange = p;
	size_t len;
	Write16(pRoomInfoChange, 0); //len
	Write32(pRoomInfoChange, 0); //TeamJumpLevel
	Write32(pRoomInfoChange, 0); //RoomBGID
	len = pRoomInfoChange - p;
	Set16(p, (WORD)len);
	p += len;
}

static void SendOfficialEnterRoomVisible500(
	std::shared_ptr<ClientNode> Client,
	std::shared_ptr<RoomNode> Room,
	std::shared_ptr<ClientNode> RoomClient,
	char SeatID,
	const char* FlowTag)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	size_t len;

	Write16(p, Room->ID); //RoomID
	WriteOfficialRoomPlayerVisibleInfo(p, Room, RoomClient, SeatID); //PlayerRoomVisibleInfo
	WriteOfficialRoomPlayerRelationInfo(p, RoomClient); //PlayerRelationInfo
	Write16(p, 1); //KartNum
	WriteOfficialRoomPlayerKartRefitInfo(p, RoomClient); //KartRefitInfo[1]
	WriteOfficialRoomInfoChange(p); //RoomInfoChange
	Write8(p, 0); //EnterRoomReason
	len = p - buf.data();
	QQS_DebugPrintf10001("%s Uin=%u RoomID=%u VisibleUin=%u SeatID=%d OwnerID=%u PlayerID=%u WireDstID=%d PayloadLen=%zu WireBodyLen=%zu EnterRoomReason=%u BorderTeam=%d\n",
		FlowTag ? FlowTag : "[FLOW][500-ENTER-OFFICIAL]",
		Client ? Client->Uin : 0, Room ? Room->ID : 0, RoomClient ? RoomClient->Uin : 0, SeatID,
		Room ? GetRoomConnOwnerID(Room) : 0, Room ? GetRoomConnPlayerID(Room, RoomClient) : 0,
		Room ? GetRoomWireDstIDForClient(Room, Client) : 0, len, len + 2,
		0u, IsBorderTeamMatchRoom(Room) ? 1 : 0);
	SendToClientWithWireDstID(Client, 500, buf.data(), len, Room->ID, FE_ROOM, GetRoomWireDstIDForClient(Room, Client), Notify);
}

static bool IsOfficialReturnRosterClient(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& RoomClient)
{
	if (!Room || !RoomClient)
		return false;
	if (RoomClient->deleteflag || !RoomClient->IsLogin || RoomClient->Uin == 0 || RoomClient->OBState)
		return false;
	if (RoomClient->RoomID != Room->ID || RoomClient->CurrentRoom != Room.get())
		return false;
	const int seatLimit = GetRoomSeatLimit(Room);
	if (RoomClient->SeatID < 0 || RoomClient->SeatID >= seatLimit || RoomClient->SeatID >= (char)Room->Player.size())
		return false;
	if (Room->Player[RoomClient->SeatID] != RoomClient)
		return false;
	return true;
}

static std::vector<std::shared_ptr<ClientNode>> BuildOfficialEnterRoomRoster(
	const std::shared_ptr<RoomNode>& Room,
	const std::shared_ptr<ClientNode>& Receiver,
	EnterRoom104RosterMode rosterMode)
{
	std::vector<std::shared_ptr<ClientNode>> roster;
	if (!Room || rosterMode != EnterRoom104RosterMode::OtherVisiblePlayers)
		return roster;

	std::vector<std::shared_ptr<ClientNode>> source;
	{
		const int seatLimit = GetRoomSeatLimit(Room);
		for (char si = 0; si < seatLimit && si < (char)Room->Player.size(); ++si)
		{
			if (Room->Player[si])
				source.push_back(Room->Player[si]);
		}
	}

	for (const auto& roomClient : source)
	{
		if (roomClient == Receiver)
			continue;
		if (!IsOfficialReturnRosterClient(Room, roomClient))
			continue;
		roster.push_back(roomClient);
	}
	return roster;
}

static size_t CountOfficialReturnRoomPlayers(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return 0;
	size_t count = 0;
	const int seatLimit = GetRoomSeatLimit(Room);
	for (char si = 0; si < seatLimit && si < (char)Room->Player.size(); ++si)
	{
		if (IsOfficialReturnRosterClient(Room, Room->Player[si]))
			count++;
	}
	return count;
}

void ResponseEnterRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, char SeatID, BOOL HasMoreInfo, char index, EnterRoom104RosterMode RosterMode)
{
	//printf("ResponseEnterRoom\n");
	//回复进入房间
	// 官服普通自定义回房主链是 514(waitEnterRoom=1) -> 104 -> 504/609 -> 500 -> 24640。
	// 12P 另走 12P 分支，不能把普通自定义套成 12P 房壳。
	// 104 RoomOwnerID uses the stable server-issued ConnID owner model.
	EnsureRoomOwnerAlive(Room, nullptr, "response-104");
	NormalizeOfficial6BorderTeamRoom(Room);
	NormalizeNormalCustomRoomWireState(Room, "104-enter-wire");
	NormalizeTwelvePlayerRoomWireState(Room, "104-enter-wire");
	if (IsCustomRoomFlow(Room))
		NormalizeCustomSchemeForOfficial(Room, "104-enter-wire");
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;
	const bool isCustomRoom = IsCustomRoomFlow(Room);
	const bool isTwelveRoom = IsTwelvePlayerRoom(Room);
	const bool isThreeDRoom = Room->ChatRoomType == 6;
	const UINT enterMapID = isCustomRoom ? GetCustomDisplayMapID(Room) : Room->MapID;
	// 12 人自定义房在线路上不能按普通自定义 SpeFlag=9 发；
	// 创建 103 已按官服口径发 0。赛后 104 如果发 9，客户端会按 6 人自定义壳重建。
	const BYTE enterSpeFlag = GetEffectiveRoomSpeFlag(Room);
	const UINT enterRoomNo = isThreeDRoom
		? static_cast<UINT>(Room->ServerID) * 10000u + Room->ID
		: (isCustomRoom ? GetRoomPublicNo(Client, Room) : Room->ID);
	const USHORT enterSvrID = isThreeDRoom
		? Room->ServerID
		: (isCustomRoom ? GetRoomListSvrID(Client, Room) : 0);
	const BYTE enterWireSeatNum = GetRoomWireSeatNum(Room);
	const BYTE enterMapNum = (isCustomRoom && !IsTwelvePlayerRoom(Room)) ? GetCustomSchemeMapNum104(Room) : 0;

	Write16(p, 0); //ResultID

	Write16(p, Room->ID); //RoomID
	Write16(p, Room->BaseGameMode); //BaseGameMode
	Write16(p, Room->SubGameMode); //SubGameMode


	memcpy(p, Room->Name, MaxRoomName); //RoomName
	p += MaxRoomName;

	Write8(p, enterWireSeatNum); //SeatNum
#ifdef ZingSpeed
	Write8(p, (BYTE)Room->Flag); //Flag
#else
	Write16(p, Room->Flag); //Flag
#endif

	Write8(p, SeatID); //SeatID
	Write8(p, 0); //TeamID
	Write32(p, enterMapID); //MapID
	Write16(p, GetRoomConnOwnerID(Room)); //RoomOwnerID

	// 21043 Network.dll 反编译确认 SeatStatus 固定 12 字节。
	// 6P 普通自定义/普通房后 6 位必须写 0，不能只写 6 字节导致后续 PlayerCount 错位。
	int seatStatusCount = ROOM_SEAT_MAX;
	//SeatStatus[12]
	for (size_t i = 0; i < (size_t)seatStatusCount; i++)
	{
		BYTE seatStatus = 1; // 1=open empty
		if (i < Room->Player.size() && Room->Player[i])
		{
			seatStatus = 2; // occupied
		}
		else if (Room->SeatState[i] == 0 ||
			(!isTwelveRoom && enterWireSeatNum > 0 && i >= enterWireSeatNum))
		{
			seatStatus = 0; // locked / unavailable
		}
		Write8(p, seatStatus);
	}

	std::vector<std::shared_ptr<ClientNode>> roster104 = BuildOfficialEnterRoomRoster(Room, Client, RosterMode);
	if (roster104.size() > 255)
		roster104.resize(255);
	BYTE playerCount104 = (BYTE)roster104.size();
	Write8(p, playerCount104); //PlayerCount
	for (const auto& roomClient : roster104)
	{
		WriteOfficialRoomPlayerVisibleInfo(p, Room, roomClient, roomClient ? roomClient->SeatID : 0);
	}

	Write8(p, 0); //ReasonLen
	BYTE playerCount2104 = playerCount104;
	Write8(p, playerCount2104); //PlayerCount2
	for (const auto& roomClient : roster104)
	{
		WriteOfficialRoomPlayerRelationInfo(p, roomClient);
	}

	USHORT kartNum104 = (USHORT)roster104.size();
	Write16(p, kartNum104); //KartNum
	for (const auto& roomClient : roster104)
	{
		WriteOfficialRoomPlayerKartRefitInfo(p, roomClient);
	}

	Write8(p, 0); //LoverRaceOrPro

	{ //STDynamicInfo
		BYTE* pSTDynamicInfo = p;
		Write16(pSTDynamicInfo, 0); //len

		Write8(pSTDynamicInfo, isThreeDRoom ? 0xFF : Room->STDynamicInfo.Season);
		Write8(pSTDynamicInfo, isThreeDRoom ? 0xFF : Room->STDynamicInfo.Timeslot);
		Write8(pSTDynamicInfo, isThreeDRoom ? 0xFF : Room->STDynamicInfo.Weather);

		len = pSTDynamicInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	Write32(p, 0); //PreKnowMapID
	Write8(p, 0); //HasMoreInfo
	Write32(p, 0); //MapSubID

	Write8(p, 0); //HasFBInfo

	Write32(p, 0); //GuildScoreInNextGame
	Write16(p, 0); //GuildScoreAlreadyAward
	Write16(p, 0); //GuildScoreTotalAward

	Write8(p, enterSpeFlag); //SpeFlag
	Write8(p, enterMapNum); //MapNum

	Write32(p, enterRoomNo); //RoomNo
	Write16(p, enterSvrID); //SvrId
	Write16(p, 0); //RaceCardNums

	Write16(p, Room->CreateRoomType); //CreateRoomType
	Write16(p, Room->ServerMatchType); //ServerMatchType
	Write16(p, Room->TeamMatchType); //TeamMatchType
	Write16(p, 0); //MatchedRoomType
	Write8(p, 0); //InviteType
	Write8(p, 0); //HasWeRelayRoomInfo

	Write8(p, isThreeDRoom ? 1 : 0); //RoomFlag
	Write8(p, Room->PresetOBNum); //MaxOBNum
	Write8(p, Room->AllowAutoStart ? 1 : 0); //AllowAutoStart
	Write8(p, 0); //HasLMInfo

	Write8(p, 0); //TalkRoomBGID
	Write16(p, 0); //Role
	Write8(p, 0); //ParaNum
	//Write32(p, 0); //ParaList[]
	{ //RoomInfoChange
		BYTE* pRoomInfoChange = p;
		Write16(pRoomInfoChange, 0); //len

		Write32(pRoomInfoChange, 0); //TeamJumpLevel
		Write32(pRoomInfoChange, 0); //RoomBGID

		len = pRoomInfoChange - p;
		Set16(p, (WORD)len);
		p += len;
	}
	Write32(p, 0); //PlayerEquipKartID
	Write32(p, 0); //RankedMatchTestDriveKartID
	Write8(p, 0); //FirstlyTipMap
	if (isThreeDRoom) {
		Write8(p, 0); // GVoiceRoomNameLen: official 3D return 104 is empty.
	}
	else if (GVoice || version > 1.3) {
		WriteString_MC(p, Room->GVoiceRoomName.c_str()); //GVoiceRoomNameLen
	}
	else {
		Write8(p, 0);
	}
	Write8(p, 0); //GVoiceReasonLen
	Write8(p, 0); //TDCExcluItemNum
	//Write32(p, 0); //TDCExclusiveItemID[]
	Write8(p, 0); //TestKartGameType
	Write32(p, 0); //GameTestKartID
	Write8(p, 0); //HasStarsFightingInfo

	Write8(p, 0); //HasYuLeJiaNianHuaInfo

	size_t customSchemeLen104 = 0;
	size_t customTailLen104 = 0;
	UINT enterRoomOtherTag104 = 0;
	BYTE enterHasCustomScheme104 = 0;
	const char* enterTailMode104 = "none";
	if (isCustomRoom)
	{
		const bool isTwelveEnterRoom104 = IsTwelvePlayerRoom(Room);
		enterTailMode104 = isTwelveEnterRoom104 ? "12P" : "6P";
		enterRoomOtherTag104 = isTwelveEnterRoom104 ? 4 : 0;
		customTailLen104 = WriteEnterRoom104CustomTail(
			p, Room, enterRoomOtherTag104, enterHasCustomScheme104, customSchemeLen104);
	}
	else if (isThreeDRoom)
	{
		enterTailMode104 = "3D";
		customTailLen104 = WriteEnterRoom104ThreeDRoomTail(p);
	}

	len = p - buf.data();
	if (IsBorderTeamMatchRoom(Room))
	{
	}
	if (isCustomRoom)
	{
	}
	const short wireDstID104 = GetRoomWireDstIDForClient(Room, Client);
	const size_t activeReturnPlayers104 = CountOfficialReturnRoomPlayers(Room);
	QQS_DebugPrintf10001("[FLOW][104-ENTER] Uin=%u RoomID=%u SeatID=%d SeatNum=%u SeatLimit=%d PresetOB=%u SpeFlag=%u EnterMapNum=%u Custom=%d Is12=%d TailMode=%s RoomOtherTag=%u HasCustomScheme=%u RosterMode=%u ActiveReturnPlayers=%zu RosterCount=%u PlayerCount2=%u KartNum=%u OwnerID=%u SelfPlayerID=%u WireDstID=%d SchemeLen104=%zu CustomTailLen=%zu PayloadLen=%zu WireBodyLen=%zu\n",
		Client ? Client->Uin : 0, Room ? Room->ID : 0, SeatID,
		Room ? (unsigned)Room->SeatNum : 0, GetRoomSeatLimit(Room),
		Room ? (unsigned)Room->PresetOBNum : 0, (unsigned)enterSpeFlag,
		(unsigned)enterMapNum, isCustomRoom ? 1 : 0, IsTwelvePlayerRoom(Room) ? 1 : 0,
		enterTailMode104, enterRoomOtherTag104, (unsigned)enterHasCustomScheme104,
		(unsigned)RosterMode, activeReturnPlayers104, (unsigned)playerCount104, (unsigned)playerCount2104, (unsigned)kartNum104, Room ? GetRoomConnOwnerID(Room) : 0,
		Room ? GetRoomConnPlayerID(Room, Client) : 0, wireDstID104,
		customSchemeLen104, customTailLen104, len, len + 2);
	SendToClientWithWireDstID(Client, 104, buf.data(), len, Room->ID, FE_ROOM, wireDstID104, Response);
}

void NotifyEnterRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, std::shared_ptr<ClientNode> RoomClient, char SeatID)
{
	if (!Client || !Room || !RoomClient)
		return;
	if (Client == RoomClient)
		return;
	if (!IsOfficialReturnRosterClient(Room, RoomClient))
		return;

	SendOfficialEnterRoomVisible500(Client, Room, RoomClient, SeatID, "[FLOW][500-ENTER-OFFICIAL]");
}

static void ClearOfficialEscFlowState(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;
	Client->OfficialEscFlowKind = QQS_OFFICIAL_ESC_FLOW_NONE;
	Client->OfficialEscPhase = QQS_OFFICIAL_ESC_PHASE_NONE;
	Client->OfficialEscStartedTick = 0;
	Client->OfficialEscLast105Tick = 0;
	Client->OfficialEscRoomID = 0;
	Client->OfficialEscBaseGameMode = 0;
	Client->OfficialEscSubGameMode = 0;
}

static void BuildOfficialClearedLeaveRoom105Response(std::shared_ptr<ClientNode> Client, short LeaveType)
{
	if (!Client)
		return;

	std::vector<BYTE> buf(32);
	BYTE* p = buf.data();
	Write16(p, 0); // ResultID
	Write8(p, 0);  // ReasonLen
#ifndef ZingSpeed
	Write16(p, LeaveType);
	Write16(p, 0); // BaseGameMode: duplicate request is already outside the room
	Write16(p, 0); // SubGameMode
	Write8(p, 0);  // IsLeaveGVoice
#endif
	const size_t len = (size_t)(p - buf.data());
	QQS_DebugPrintf10001("[FLOW][ESC-OFFICIAL-105-DUP-ACK] Uin=%u OldRoomID=%u LeaveType=%d Base=0 Sub=0 GVoice=0 PayloadLen=%zu WireBodyLen=%zu\n",
		Client->Uin, (unsigned)Client->OfficialEscRoomID, (int)LeaveType, len, len + 2);
	int inboundWireMsgSeq = -1;
	int inboundProtocolSeq = -1;
	if (!TryGetCurrentInboundWireMsgSeq(Client, inboundWireMsgSeq) || inboundWireMsgSeq < 0 ||
		!TryGetCurrentInboundProtocolSeq(Client, inboundProtocolSeq) || inboundProtocolSeq < 0)
	{
		QQS_DebugPrintf10001("[FLOW][ESC-OFFICIAL-105-DUP-ACK-ABORT] Uin=%u OldRoomID=%u Reason=missing-inbound-response-correlation WireMsgSeq=%d ProtocolSeq=%d\n",
			Client->Uin, (unsigned)Client->OfficialEscRoomID, inboundWireMsgSeq, inboundProtocolSeq);
		return;
	}
	SendCorrelatedResponseToClient(
		Client, 105, buf.data(), static_cast<DWORD>(len),
		0, FE_ROOM, GetClientWirePlayerHeaderID(Client),
		inboundWireMsgSeq, inboundProtocolSeq);
}

static bool HandleOfficialEscLeaveRoom105(std::shared_ptr<ClientNode> Client, short LeaveType)
{
	if (!Client || Client->OfficialEscFlowKind == QQS_OFFICIAL_ESC_FLOW_NONE ||
		Client->OfficialEscPhase == QQS_OFFICIAL_ESC_PHASE_NONE)
	{
		return false;
	}

	const DWORD nowTick = GetTickCount();

	if (Client->OfficialEscPhase == QQS_OFFICIAL_ESC_PHASE_WAITING_CLIENT_105)
	{
		const DWORD waitMs = nowTick - Client->OfficialEscStartedTick;
		const bool borderReturnHall =
			Client->OfficialEscFlowKind == QQS_OFFICIAL_ESC_FLOW_BORDER_RETURN_HALL;
		const USHORT clientRoomID = borderReturnHall
			? Client->BorderRoomID
			: Client->RoomID;
		std::shared_ptr<RoomNode> Room = borderReturnHall
			? GetBorderRoom(clientRoomID)
			: GetRoom(clientRoomID);
		const bool validFlowKind =
			Client->OfficialEscFlowKind == QQS_OFFICIAL_ESC_FLOW_SINGLE_RETURN_ROOM ||
			Client->OfficialEscFlowKind == QQS_OFFICIAL_ESC_FLOW_MULTI_RETURN_HALL ||
			borderReturnHall;
		if (!validFlowKind || !Room || Room->ID != Client->OfficialEscRoomID)
		{
			QQS_DebugPrintf10001("[FLOW][ESC-OFFICIAL-105-CONTEXT-MISMATCH] Uin=%u Flow=%u Phase=%u ClientRoomID=%u ExpectedRoomID=%u WaitMs=%u HasRoom=%d Border=%d -> normal 105\n",
				Client->Uin, (unsigned)Client->OfficialEscFlowKind,
				(unsigned)Client->OfficialEscPhase, (unsigned)clientRoomID,
				(unsigned)Client->OfficialEscRoomID, (unsigned)waitMs, Room ? 1 : 0,
				borderReturnHall ? 1 : 0);
			ClearOfficialEscFlowState(Client);
			return false;
		}

		const BYTE flowKind = Client->OfficialEscFlowKind;
		Client->OfficialEscPhase = QQS_OFFICIAL_ESC_PHASE_LEFT_AWAITING_NEXT;
		Client->OfficialEscLast105Tick = nowTick;
		QQS_DebugPrintf10001("[FLOW][ESC-OFFICIAL-105-COMMIT] Uin=%u Flow=%u RoomID=%u LeaveType=%d Base=%u Sub=%u WaitMs=%u Border=%d -> leave now\n",
			Client->Uin, (unsigned)flowKind, (unsigned)Room->ID, (int)LeaveType,
			(unsigned)Client->OfficialEscBaseGameMode,
			(unsigned)Client->OfficialEscSubGameMode, (unsigned)waitMs,
			borderReturnHall ? 1 : 0);
		if (borderReturnHall)
		{
			// ResponseLeaveRoom 需要仍然存在的 BorderRoomID 来构造关联 S105；
			// 随后的两个 Leave 只提交边境房及可能存在的组队房状态，
			// 不能再主动发送第二个 S105。
			ResponseLeaveRoom(Client, LeaveType);
			LeaveBorderRoom(Client, LeaveType, false);
			LeaveRoom(Client, LeaveType, false);
		}
		else
		{
			LeaveRoom(Client, LeaveType);
			LeaveOutdoor(Client);
		}
		return true;
	}

	if (Client->OfficialEscPhase == QQS_OFFICIAL_ESC_PHASE_LEFT_AWAITING_NEXT &&
		Client->OfficialEscFlowKind == QQS_OFFICIAL_ESC_FLOW_SINGLE_RETURN_ROOM)
	{
		// 官服单人链会收到第二个 105，并返回已清空房间上下文的 ACK；
		// 不能再执行一次 LeaveRoom，也不能静默丢包。
		BuildOfficialClearedLeaveRoom105Response(Client, LeaveType);
		return true;
	}
	return false;
}

void RequestLeaveRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	size_t minBodyLen = 8;
#ifndef ZingSpeed
	minBodyLen = 10;
#endif

	if (!Client || !Body || BodyLen < minBodyLen)
	{
		QQS_DebugPrintf10001("[FLOW][105-GUARD] invalid leave-room request Client=%p Body=%p BodyLen=%zu Need=%zu\n",
			Client.get(), Body, BodyLen, minBodyLen);
		return;
	}
	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);
	short LeaveType = 0;
#ifndef ZingSpeed
	LeaveType = Read16(Body);//正常是0 客户端某些时候会自动请求该消息导致退房错误 
	//BYTE LeaveRoomReason = Read8(Body);
#endif

	if (TryHandleLuckyMatchLeaveRoom(Client, LeaveType))
	{
		return;
	}

	if (HandleOfficialEscLeaveRoom105(Client, LeaveType))
		return;

	//普通房间强制观战后防止自动退出
	if (obs == 1) {
		if (Client->OBState == 1 && Client->ChatRoomID == -1)
		{
			std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
			//防止观战状态强制退出 会请求2次
			if (Room)
			{
				if (Client->AutoLeave != 1)//保证每局只触发自动退出一次释放资源
				{
					ResponseLeaveRoom(Client, 0);
					Client->AutoLeave = 1;
					return;
				}
				ResponseEnterRoom(Client, Room, Client->SeatID, 0, 0);
				int obSeatLimit = GetRoomSeatLimit(Room);
				for (char j = 0; j < obSeatLimit; j++)
				{
					std::shared_ptr<ClientNode> RoomClient2 = Room->Player[j];
					if (RoomClient2 && RoomClient2 != Client)
					{
						NotifyEnterRoom(Client, Room, RoomClient2, RoomClient2->SeatID);
					}
				}
				SendToClient(Client, 134, (BYTE*)"\x00\x00\x01\x00", 4, Client->ConnID, FE_PLAYER, 1, Response);
				ResponseStartGame(Client, "请关闭观战模式再退出房间");

				if (Room->ChatRoomType != 6) {
					for (char i5 = 0; i5 < obSeatLimit; i5++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[i5];
						if (RoomClient && RoomClient != Client)
						{
							NotifyChangeOBState(RoomClient, Client->OBState, Client->Uin);
						}
					}
				}
				if (Room->IsBottomGame == 9 && Client->news.length() > 0) {
					NoticeClient(Client, Client->news);
					Client->news = "";
				}
				//Client->AutoLeave = 0;
				return;
			}
		}
	}


	if (LeaveType == 0 && Client && Client->BorderRoomID == 0)//防止匹配自动退出
	{
		LeaveRoom(Client, 0);
		LeaveOutdoor(Client);
	}

}
void LeaveRoom(std::shared_ptr<ClientNode> Client, short LeaveType, bool sendLeaveAck)
{
	if (!Client)
	{
		return;
	}
	ClearOfficialRoomReturnFlow(Client, "leave-room");
	SpeedLadder_ClearRaceContext(Client, "leave-room");
	if (Client->deleteflag || !Client->IsLogin)
	{
		RequestCancelMatchExactSession(Client);
	}
	else
	{
		RequestCancelMatch(Client);
	}
	std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
	if (!Room)
	{
		return;
	}
	int seatLimit = GetRoomSeatLimit(Room);
	for (char i = 0; i < seatLimit; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			if (RoomClient == Client)
			{
				if (sendLeaveAck)
					ResponseLeaveRoom(Client, LeaveType);
				if (Room->ChatRoomType == 6 && Client->ChatRoomID >= 0)
				{
					// 动态 3D 上下文由游戏 RoomID 索引；必须在 RoomID 清零或
					// Rooms.erase 之前退出，否则后续无法再定位同一官服 outdoor 房间。
					LeaveOutdoor(Client);
				}
				const BOOL WasRoomOwner = (IsRoomConnOwner(Room, Client) || (Room->RoomOwnerUin != 0 && Room->RoomOwnerUin == Client->Uin)) ? TRUE : FALSE;
				RoomClient->ReadyState = QQS_ROOM_READY_STATE_NOT_READY;
				//RoomClient->OBState = 0;
				RoomClient->TaskID = 0;
				RoomClient->RoomID = 0;
				RoomClient->CurrentRoom = NULL;
				ResetNormalRaceWireState(RoomClient);
				RoomClient->GameID = 0;
				RoomClient->isRace = 0;
				RoomClient->IsAnonymous = 0;
				RoomClient->IsStake = 0;
				RoomClient->IsBottomGame = 0;
				RoomClient->StakeNum = 0;
				RoomClient->TeamID = 0;
				Room->Player[i] = NULL;
				Room->CurrentPlayerNum--;

				BOOL IsRoomOwner = WasRoomOwner;
				USHORT NewRoomOwnerID = WasRoomOwner ? 0 : GetRoomConnOwnerID(Room);
				if (WasRoomOwner)
				{
					Room->RoomOwnerUin = 0;
					for (char i2 = 0; i2 < seatLimit; i2++)
					{
						RoomClient = Room->Player[i2];
						if (RoomClient)
						{
							Room->RoomOwnerUin = RoomClient->Uin;
							NewRoomOwnerID = GetRoomConnPlayerID(Room, RoomClient); //转让房主
							RoomClient->ReadyState = QQS_ROOM_READY_STATE_READY;
							// 在转让房主时，关闭所有特定模式
							if (IsSuperMoneyBottomGameMode(Room->IsBottomGame))
								SetBottomSuperMoneyAnonymousState(Room, false);
							Room->IsBottomGame = 0;             // 关闭垫底赛模式
							Room->BottomGameNum = 0;
							Room->BottomGameSettlementDone = false;
							break;
						}
					}
				}
				Room->RoomOwnerID = NewRoomOwnerID;
				if (Room->CurrentPlayerNum == 0)
					Room->RoomOwnerUin = 0;
				if (Room->CurrentPlayerNum > 0)
				{
					USHORT beforeEnsureOwner = Room->RoomOwnerID;
					EnsureRoomOwnerAlive(Room, nullptr, "leave-room");
					if (Room->RoomOwnerID != beforeEnsureOwner)
					{
						NewRoomOwnerID = Room->RoomOwnerID;
						IsRoomOwner = TRUE;
					}
				}

				//通知其它玩家
				int n = 0;
				for (char i2 = 0; i2 < seatLimit; i2++)
				{
					RoomClient = Room->Player[i2];
					if (RoomClient)
					{
						NotifyLeaveRoom(RoomClient, Client->Uin, i, IsRoomOwner, NewRoomOwnerID, LeaveType);
						n++;
					}
				}

				if (n == 0)
				{
					if (Room->Timer)
					{
						if (DeleteTimerQueueTimer(TimerQueue, Room->Timer, INVALID_HANDLE_VALUE))
						{
							Room->Timer = NULL;
						}
					}
					RecycleRoomID(Room->ID);
// 					std::lock_guard<std::recursive_mutex> lg(Mutex);
// 					Rooms.erase(Room->ID);
					//Room = NULL;
				}
				else
				{
					HandlePlayerLeaveRace(Client, Room, "leave-room");
				}
				break;

			}
		}
	}
}
void LeaveBorderRoom(std::shared_ptr<ClientNode> Client, short LeaveType, bool sendLeaveAck)
{
	std::shared_ptr<RoomNode> Room = GetBorderRoom(Client->BorderRoomID);
	if (!Room)
	{
		return;
	}
	for (char i = 0; i < 6; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			if (RoomClient == Client)
			{
				// Preserve finish/DNF state while the client still owns the race fields.
				// The mutable ClientNode and room slot are cleared immediately below.
				HandlePlayerLeaveRace(Client, Room, "leave-border-room");
				if (sendLeaveAck)
				{
					ResponseLeaveRoom(RoomClient, LeaveType);
				}
				else
				{
					QQS_DebugPrintf10001("[FLOW][105-BORDER-LEAVE-ACK-SKIP] Uin=%u BorderRoomID=%u TeamRoomID=%u LeaveType=%d Reason=sendLeaveAck-disabled\n",
						RoomClient ? RoomClient->Uin : 0,
						Room ? Room->ID : 0,
						RoomClient ? RoomClient->RoomID : 0,
						(int)LeaveType);
				}
				RoomClient->ReadyState = QQS_ROOM_READY_STATE_NOT_READY;
				RoomClient->TaskID = 0;
				ResetNormalRaceWireState(RoomClient);
				RoomClient->GameID = 0;
				RoomClient->IsFinish = 0;
				RoomClient->FinishTime = -1;
				RoomClient->Round = 0;
				RoomClient->PassedCheckPoint = 0;
				RoomClient->ranknumber = 0;
				RoomClient->HighestSpeed = 0;
				RoomClient->CollisionCount = 0;
				RoomClient->N2OGatherCount = 0;
				RoomClient->DriftCount = 0;
				RoomClient->RaceDetail = RaceDetailRecord{};
				RoomClient->LapTime = 0;
				RoomClient->MAXLapTime = 0;
				RoomClient->LastDistanceToEnd = 0;
				RoomClient->LastRaceLapTime = 0;
				RoomClient->BorderRoomID = 0;
				RoomClient->CurrentRoom = NULL;
				RoomClient->isRace = 0;
				RoomClient->IsStake = 0;
				RoomClient->IsBottomGame = 0;
				RoomClient->StakeNum = 0;
				RoomClient->IsAnonymous = 0;
				RoomClient->TeamID = 0;
				Room->Player[i] = NULL;
				Room->CurrentPlayerNum--;

				//通知其它玩家
				int n = 0;
				for (char i2 = 0; i2 < 6; i2++)
				{
					RoomClient = Room->Player[i2];
					if (RoomClient)
					{
						NotifyLeaveRoom(RoomClient, Client->Uin, i, 0, 0, LeaveType);
						n++;
					}
				}

				if (n == 0)
				{
					if (Room->Timer)
					{
						if (DeleteTimerQueueTimer(TimerQueue, Room->Timer, INVALID_HANDLE_VALUE))
						{
							Room->Timer = NULL;
						}
					}
					RecycleBorderRoomID(Room->ID);
// 					std::lock_guard<std::recursive_mutex> lg(Mutex);
// 					BorderRooms.erase(Room->ID);
				}
				break;
			}
		}
	}
}

void ClearFinishedBorderRoom(std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Room || Room->matchQueueType != 0)
		return;

	ClearRoomGameLogicSession(Room, reason ? reason : "clear-finished-border-room");
	const UINT borderRoomID = Room->ID;
	int clearedSlots = 0;
	int detachedClients = 0;
	int staleForeignLinks = 0;
	const int seatLimit = ((int)Room->Player.size() < ROOM_SEAT_NORMAL_MAX) ? (int)Room->Player.size() : ROOM_SEAT_NORMAL_MAX;
	for (int i = 0; i < seatLimit; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (!RoomClient)
			continue;

		// 只改仍指向本临时房的对象；槽位若引用了已进入其他房间的新状态，
		// 只清槽位，不能覆盖玩家的新房间状态。
		if (RoomClient->BorderRoomID == borderRoomID || RoomClient->CurrentRoom == Room.get())
		{
			RoomClient->ReadyState = QQS_ROOM_READY_STATE_NOT_READY;
			RoomClient->IsReady = FALSE;
			RoomClient->TaskID = 0;
			ResetNormalRaceWireState(RoomClient);
			RoomClient->GameID = 0;
			RoomClient->IsFinish = FALSE;
			RoomClient->FinishTime = -1;
			RoomClient->Round = 0;
			RoomClient->PassedCheckPoint = 0;
			RoomClient->ranknumber = 0;
			RoomClient->HighestSpeed = 0;
			RoomClient->CollisionCount = 0;
			RoomClient->N2OGatherCount = 0;
			RoomClient->DriftCount = 0;
			RoomClient->RaceDetail = RaceDetailRecord{};
			RoomClient->LapTime = 0;
			RoomClient->MAXLapTime = 0;
			RoomClient->LastDistanceToEnd = 0;
			RoomClient->LastRaceLapTime = 0;
			RoomClient->BorderRoomID = 0;
			if (RoomClient->CurrentRoom == Room.get())
				RoomClient->CurrentRoom = NULL;
			RoomClient->isRace = 0;
			RoomClient->IsStake = 0;
			RoomClient->IsBottomGame = 0;
			RoomClient->StakeNum = 0;
			RoomClient->IsAnonymous = 0;
			RoomClient->TeamID = 0;
			detachedClients++;
		}
		else
		{
			staleForeignLinks++;
		}
		Room->Player[i].reset();
		clearedSlots++;
	}

	Room->CurrentPlayerNum = 0;
	Room->RaceStartPlayerNum = 0;
	Room->RaceActiveMask = 0;
	Room->BorderRaceSnapshotValid = false;
	Room->BorderRaceStartMask = 0;
	Room->BorderRaceConnectedMask = 0;
	Room->BorderRaceFinishedMask = 0;
	Room->BorderRaceQuitMask = 0;
	Room->BorderRacePhase = BorderRaceSettlementPhase::Complete;
	Room->BorderRaceStartRoster.clear();
	Room->CountDownFlag = 0;
	Room->ranknumber = 0;
	Room->startStatus = 1;
	Room->Callback = NULL;

	bool recycled = false;
	if (GetBorderRoom(borderRoomID) == Room)
	{
		if (Room->Timer)
		{
			DeleteTimerQueueTimer(TimerQueue, Room->Timer, INVALID_HANDLE_VALUE);
			Room->Timer = NULL;
		}
		RecycleBorderRoomID(borderRoomID);
		recycled = true;
	}

	QQS_DebugPrintf10001("[FLOW][BORDER-ROOM-CLEAR] Reason=%s BorderRoomID=%u ClearedSlots=%d DetachedClients=%d ForeignLinks=%d Recycled=%d\n",
		reason ? reason : "", borderRoomID, clearedSlots, detachedClients, staleForeignLinks, recycled ? 1 : 0);
}

void ResponseLeaveRoom(std::shared_ptr<ClientNode> Client, short LeaveType)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;
	const bool officialBorderEscFirst105 =
		Client &&
		Client->OfficialEscFlowKind == QQS_OFFICIAL_ESC_FLOW_BORDER_RETURN_HALL &&
		Client->OfficialEscPhase == QQS_OFFICIAL_ESC_PHASE_LEFT_AWAITING_NEXT &&
		Client->OfficialEscRoomID != 0 &&
		Client->OfficialEscRoomID == Client->BorderRoomID;
	const USHORT leaveRoomID = officialBorderEscFirst105
		? Client->BorderRoomID
		: (Client ? Client->RoomID : 0);
	std::shared_ptr<RoomNode> Room = officialBorderEscFirst105
		? GetBorderRoom(leaveRoomID)
		: GetRoom(leaveRoomID);
	const bool officialEscFirst105 =
		Client &&
		Client->OfficialEscPhase == QQS_OFFICIAL_ESC_PHASE_LEFT_AWAITING_NEXT &&
		Client->OfficialEscRoomID != 0 &&
		Client->OfficialEscRoomID == leaveRoomID;
	USHORT leaveBaseMode = officialEscFirst105
		? Client->OfficialEscBaseGameMode
		: (Room ? Room->BaseGameMode : 0);
	USHORT leaveSubMode = officialEscFirst105
		? Client->OfficialEscSubGameMode
		: (Room ? Room->SubGameMode : 0);

	Write16(p, 0); //ResultID

	Write8(p, 0); //ReasonLen

#ifndef ZingSpeed
	Write16(p, LeaveType); //LeaveType
	Write16(p, leaveBaseMode); //BaseGameMode
	Write16(p, leaveSubMode); //SubGameMode
	if (officialEscFirst105 || GVoice || version > 1.3) {
		Write8(p, 1); //IsLeaveGVoice
	}
	else {
		Write8(p, 0);
	}

#endif

	len = p - buf.data();
	QQS_DebugPrintf10001("[FLOW][105-LEAVE-ROOM-ACK] Uin=%u RoomID=%u LeaveType=%d Base=%u Sub=%u OfficialEscFirst=%d PayloadLen=%zu WireBodyLen=%zu\n",
		Client ? Client->Uin : 0, leaveRoomID, LeaveType,
		leaveBaseMode, leaveSubMode, officialEscFirst105 ? 1 : 0, len, len + 2);
	if (officialEscFirst105)
	{
		int inboundWireMsgSeq = -1;
		int inboundProtocolSeq = -1;
		if (!TryGetCurrentInboundWireMsgSeq(Client, inboundWireMsgSeq) || inboundWireMsgSeq < 0 ||
			!TryGetCurrentInboundProtocolSeq(Client, inboundProtocolSeq) || inboundProtocolSeq < 0)
		{
			QQS_DebugPrintf10001("[FLOW][ESC-OFFICIAL-105-ACK-ABORT] Uin=%u RoomID=%u Reason=missing-inbound-response-correlation WireMsgSeq=%d ProtocolSeq=%d\n",
				Client->Uin, leaveRoomID, inboundWireMsgSeq, inboundProtocolSeq);
			return;
		}
		SendCorrelatedResponseToClient(
			Client, 105, buf.data(), static_cast<DWORD>(len),
			leaveRoomID, FE_ROOM, GetClientWirePlayerHeaderID(Client),
			inboundWireMsgSeq, inboundProtocolSeq);
		return;
	}
	SendToClient(Client, 105, buf.data(), len, Client->RoomID, FE_ROOM, 1, Response);
}
void NotifyLeaveRoom(std::shared_ptr<ClientNode> Client, UINT Uin, char SeatID, bool IsChangeRoomOwner, USHORT NewRoomOwnerID, short LeaveType)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, Client->RoomID); //RoomID
	Write32(p, Uin); //Uin
	Write8(p, SeatID); //SeatID
	Write8(p, IsChangeRoomOwner); //IsChangeRoomOwner
	Write16(p, NewRoomOwnerID); //NewRoomOwnerID

#ifdef ZingSpeed
	Write8(p, 0); //Flag
#else
	Write16(p, 0); //Flag
#endif

	Write8(p, 0); //HasFBInfo
	/*
m_astFBInfo[].m_bAvailableLev
m_astFBInfo[].m_bLev
m_astFBInfo[].m_bSubLev
m_astFBInfo[].m_iBossID
m_astFBInfo[].m_bFBSeason
	*/

	len = p - buf.data();
	SendToClient(Client, 501, buf.data(), len, Client->RoomID, FE_ROOM, 1, Notify);
}


static bool IsRankedStartPlayerForRoom(
	const std::shared_ptr<RoomNode>& Room,
	const std::shared_ptr<ClientNode>& Client,
	int seat);

bool NotifyBorderPlayerInfo(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> BorderRoom)
{
	if (!Client || !BorderRoom || BorderRoom->RaceGameID == 0)
		return false;
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	size_t len;
	Write32(p, BorderRoom->BaseGameMode); //BaseMode
	Write32(p, BorderRoom->SubGameMode); //SubMode
	const bool rankedRoom = BorderRoom->matchQueueType == 0x0A;
	std::vector<std::shared_ptr<ClientNode>> visiblePlayers;
	for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX; ++seat)
	{
		const std::shared_ptr<ClientNode>& roomClient = BorderRoom->Player[seat];
		if (!roomClient)
			continue;
		if (rankedRoom)
		{
			const RoomSeatMask bit = GetRoomSeatBit(seat);
			if ((BorderRoom->RaceActiveMask & bit) == 0 ||
				!IsRankedStartPlayerForRoom(BorderRoom, roomClient, seat))
				continue;
		}
		visiblePlayers.emplace_back(roomClient);
	}
	if (visiblePlayers.empty() || visiblePlayers.size() > 0xFF ||
		(rankedRoom && visiblePlayers.size() != BorderRoom->RaceStartPlayerNum))
	{
		QQS_DebugPrintf10001(
			"[FLOW][548-ABORT] Uin=%u RoomID=%u Ranked=%d Visible=%zu Expected=%u Reason=roster-mismatch\n",
			Client->Uin, BorderRoom->ID, rankedRoom ? 1 : 0,
			visiblePlayers.size(), static_cast<unsigned>(BorderRoom->RaceStartPlayerNum));
		return false;
	}
	Write8(p, static_cast<BYTE>(visiblePlayers.size())); //PlayerCount
	for (const std::shared_ptr<ClientNode>& roomClient : visiblePlayers)
		WriteVisbleInfo1(roomClient, p, roomClient->Uin, 0, 0);
	Write16(p, 0);

	if (Client->TaskID == 0xFFFD || Client->TaskID == 0)
	{
		memcpy(p, "\x00\x00\x00\x00\x00", 5);
		p += 5;
	}
	else if (Client->TaskID == 0xFFFC)
	{
		memcpy(p, "\x00\x00\x01\x00\x1e\x00\x01\x98\x99\x00\x01\x98\xa3\x00\x01\x98\x99\x00\x01\x98\xa3\x00\x00\x00\x00\x00\x00\x27\x3d\x00\x00\x27\x30\x00\x00", 35);
		p += 35;
	}

	len = p - buf.data();

	// 官服 548 的 SrcID 与 20081 AddrInfo.SvrID 相同，都是本局选中的
	// GAMESVRD；FE_PLAYER/GAMELOGIC 后续流才使用玩家服务路由 ServerID。
	const BOOL sent = SendToClient(
		Client, 548, buf.data(), static_cast<DWORD>(len),
		static_cast<short>(BorderRoom->RaceGameID), FE_GAMESVRD,
		GetClientWirePlayerHeaderID(Client), Notify);
	return sent != FALSE;
	//delete[] buf;
}
static void WriteRankedBanMapInfo(BYTE*& p, const CMatchBanMapInfo& mapInfo)
{
	BYTE* node = p;
	Write16(node, 0);
	Write32(node, mapInfo.MapID);
	Write32(node, mapInfo.Status);
	Write8(node, mapInfo.ThemeMapOrNot);
	Write8(node, mapInfo.SeasonNewMapOrNot);
	Write8(node, mapInfo.PinnacleBattleMapOrNot);
	Write16(node, mapInfo.TotalBeBannedNums);
	Write16(node, mapInfo.FinalRandBanMapType);
	const size_t len = node - p;
	Set16(p, static_cast<WORD>(len));
	p += len;
}

static constexpr UINT kRankedBanGradeLevelLimit = 23;
static constexpr UINT kRankedBanChooseTimeSeconds = 10;
static constexpr UINT kRankedBanShowTimeSeconds = 3;
static constexpr UINT kRankedBanTotalMapNum = 8;
static constexpr UINT kRankedBanChooseMapNum = 7;
static constexpr UINT kRankedBanBonusScore = 1;
static constexpr UINT kRankedPinnacleTotalMapNum = 6;
static constexpr UINT kRankedPinnacleChooseMapNum = 5;
static constexpr UINT kRankedPinnacleInternalGrade = 5; // wire GradeLevel 25，星耀
static constexpr int kRankedRaceBeginDelayMs = 16000;
static bool UseRankedPinnacleMapLayout(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room)
		return false;
	bool hasPlayer = false;
	for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX && seat < static_cast<int>(Room->Player.size()); ++seat)
	{
		const std::shared_ptr<ClientNode>& player = Room->Player[seat];
		if (!player)
			continue;
		hasPlayer = true;
		if (player->GradeLevel < kRankedPinnacleInternalGrade)
			return false;
	}
	return hasPlayer;
}

static const RankGradeMapPool* GetRankedRoomMapPool(const std::shared_ptr<RoomNode>& Room)
{
	const RankConfig& rankCfg = GetRankConfig();
	if (!Room || !rankCfg.LoadedFromYaml || !rankCfg.Enabled)
		return nullptr;

	bool hasPlayer = false;
	UINT highestInternalGrade = 0;
	for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX && seat < static_cast<int>(Room->Player.size()); ++seat)
	{
		const std::shared_ptr<ClientNode>& player = Room->Player[seat];
		if (!player)
			continue;
		if (player->GradeLevel > 7)
			return nullptr;
		hasPlayer = true;
		highestInternalGrade = (std::max)(highestInternalGrade, player->GradeLevel);
	}
	if (!hasPlayer)
		return nullptr;

	// Client::GradeLevel为内部0..7，对应线上的20..27；当前赛季地图配置到26，
	// 因此技术车神(27)与车神段共用最高档地图池。多人房间只有一张公共地图，取最高当前段位。
	const UINT mapInternalGrade = highestInternalGrade == 7 ? 6 : highestInternalGrade;
	return GetRankGradeMapPool(static_cast<USHORT>(20 + mapInternalGrade));
}

static size_t GetRankedBanMapCount(const std::shared_ptr<RoomNode>& Room)
{
	return UseRankedPinnacleMapLayout(Room)
		? static_cast<size_t>(kRankedPinnacleTotalMapNum)
		: static_cast<size_t>(kRankedBanTotalMapNum);
}

static int FindRankedRoomSeat(
	const std::shared_ptr<RoomNode>& Room,
	const std::shared_ptr<ClientNode>& Client)
{
	if (!Room || !Client)
		return -1;
	for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX && seat < static_cast<int>(Room->Player.size()); ++seat)
	{
		if (Room->Player[seat] == Client)
			return seat;
	}
	return -1;
}

static bool IsRankedStartPlayerForRoom(
	const std::shared_ptr<RoomNode>& Room,
	const std::shared_ptr<ClientNode>& Client,
	int seat)
{
	if (!Room || !Client || seat < 0 || seat >= ROOM_SEAT_NORMAL_MAX ||
		seat >= static_cast<int>(Room->Player.size()) || Room->Player[seat] != Client)
		return false;
	if (Client->Uin == 0 || Client->TransportConnID == 0 || !Client->IsLogin ||
		Client->deleteflag || Client->OBState || GetClient(Client->Uin) != Client)
		return false;
	return Client->BorderRoomID == Room->ID && Client->CurrentRoom == Room.get() &&
		Client->TaskID == 0xFFFC && Client->GameID == Room->RaceGameID &&
		Room->RaceGameID != 0;
}

static bool IsRankedBanMapSelectableStatus(UINT status)
{
	// 客户端枚举：0=OPEN, 4=THEMEMAP, 5=SEASONEWMAP,
	// 6=PINNACLEBATTLE。抓包中这些特殊图保持各自状态，禁用后变 2，
	// 最终地图变 3；它们不是可按位 OR 的 flag。
	return status == 0 || status == 4 || status == 5 || status == 6;
}

static bool InitializeRankedBanMaps(
	const std::shared_ptr<RoomNode>& Room,
	const RankGradeMapPool* MapPool,
	size_t mapCount)
{
	if (!Room || !MapPool ||
		(mapCount != kRankedBanTotalMapNum && mapCount != kRankedPinnacleTotalMapNum))
		return false;

	std::vector<UINT> mapIDs = MapPool->NormalMaps;
	const auto invalidMap = std::find_if(mapIDs.begin(), mapIDs.end(), [](UINT mapID) {
		return mapID == 0;
	});
	if (invalidMap != mapIDs.end())
	{
		QQS_DebugPrintf10001(
			"[RANKED-BAN-MAP][INIT-FAIL] RoomID=%u MapID=%d Reason=invalid-pool-map-id\n",
			Room->ID, static_cast<int>(*invalidMap));
		return false;
	}
	std::sort(mapIDs.begin(), mapIDs.end());
	mapIDs.erase(std::unique(mapIDs.begin(), mapIDs.end()), mapIDs.end());
	if (mapIDs.size() < mapCount)
	{
		QQS_DebugPrintf10001(
			"[RANKED-BAN-MAP][INIT-FAIL] RoomID=%u PoolSize=%zu Required=%zu\n",
			Room->ID, mapIDs.size(), mapCount);
		return false;
	}

	static thread_local std::mt19937 generator(std::random_device{}());
	std::shuffle(mapIDs.begin(), mapIDs.end(), generator);
	Room->MatchBanMaps.clear();
	Room->MatchBanMaps.reserve(mapCount);
	for (size_t i = 0; i < mapCount; ++i)
	{
		const UINT mapID = mapIDs[i];
		if (!GetMap(mapID))
		{
			QQS_DebugPrintf10001(
				"[RANKED-BAN-MAP][INIT-FAIL] RoomID=%u MapID=%u Reason=map-config-missing\n",
				Room->ID, mapID);
			Room->MatchBanMaps.clear();
			return false;
		}
		CMatchBanMapInfo mapInfo;
		mapInfo.MapID = mapID;
		mapInfo.Status = i + 1 == mapCount ? 1u : 0u;
		Room->MatchBanMaps.emplace_back(mapInfo);
	}
	Room->MatchBanMapType = 0;
	QQS_DebugPrintf10001(
		"[RANKED-BAN-MAP][INIT] RoomID=%u MapCount=%zu RankYmlGrade=%u PinnacleLayout=%d\n",
		Room->ID, mapCount, static_cast<unsigned>(MapPool->GradeLevel),
		UseRankedPinnacleMapLayout(Room) ? 1 : 0);
	return true;
}

static bool InitializeRankedRoomMapState(const std::shared_ptr<RoomNode>& Room)
{
	if (!Room || Room->matchQueueType != 0x0A || Room->CurrentPlayerNum == 0)
		return false;

	const RankGradeMapPool* mapPool = GetRankedRoomMapPool(Room);
	if (!InitializeRankedBanMaps(Room, mapPool, GetRankedBanMapCount(Room)))
		return false;

	static thread_local std::mt19937 generator(std::random_device{}());
	std::uniform_int_distribution<size_t> distribution(0, mapPool->NormalMaps.size() - 1);
	const UINT initialMapID = mapPool->NormalMaps[distribution(generator)];
	const bool belongsToPool = std::find(
		mapPool->NormalMaps.begin(), mapPool->NormalMaps.end(), initialMapID) !=
		mapPool->NormalMaps.end();
	Map* map = initialMapID != 0 ? GetMap(initialMapID) : nullptr;
	if (!belongsToPool || !map)
	{
		QQS_DebugPrintf10001(
			"[RANKED-BAN-MAP][INIT-FAIL] RoomID=%u RankYmlGrade=%u InitialMapID=%u InPool=%d Reason=invalid-initial-map\n",
			Room->ID, static_cast<unsigned>(mapPool->GradeLevel), initialMapID, belongsToPool ? 1 : 0);
		Room->MatchBanMaps.clear();
		return false;
	}

	Room->MapID = initialMapID;
	Room->RankedStartPhase = RankedMatchStartPhase::Idle;
	Room->RankedEnteredMask = 0;
	Room->RankedRaceBeginSent = false;
	for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX; ++seat)
	{
		const std::shared_ptr<ClientNode>& player = Room->Player[seat];
		if (!player)
			continue;
		player->MapCompatibilityMode = false;
		player->MapID = initialMapID;
		player->EndCheckPoint = map->End;
		player->TotalRound = map->Round;
		player->Loop = map->Loop;
	}
	QQS_DebugPrintf10001(
		"[RANKED-BAN-MAP][ROOM-INIT] RoomID=%u RankYmlGrade=%u InitialMapID=%u MapCount=%zu PinnacleLayout=%d\n",
		Room->ID, static_cast<unsigned>(mapPool->GradeLevel), Room->MapID, Room->MatchBanMaps.size(),
		UseRankedPinnacleMapLayout(Room) ? 1 : 0);
	return true;
}

static bool NotifyRankedMatchBanMapInfo(std::shared_ptr<ClientNode> Client)
{
	const std::shared_ptr<RoomNode> Room = GetActiveRaceRoom(Client);
	if (!Client || !Room || Room->MatchBanMaps.empty())
		return false;

	std::vector<BYTE> buf(4096);
	BYTE* p = buf.data();
	{
		BYTE* cfg = p;
		Write16(cfg, 0);
		Write32(cfg, kRankedBanGradeLevelLimit);
		Write32(cfg, kRankedBanChooseTimeSeconds);
		Write32(cfg, kRankedBanShowTimeSeconds);
		Write32(cfg, kRankedBanTotalMapNum);
		Write32(cfg, kRankedBanChooseMapNum);
		Write32(cfg, kRankedBanBonusScore);
		Write32(cfg, kRankedPinnacleTotalMapNum);
		Write32(cfg, kRankedPinnacleChooseMapNum);
		const size_t len = cfg - p;
		Set16(p, static_cast<WORD>(len));
		p += len;
	}

	Write32(p, static_cast<UINT>(Room->MatchBanMaps.size()));
	for (const CMatchBanMapInfo& mapInfo : Room->MatchBanMaps)
		WriteRankedBanMapInfo(p, mapInfo);
	Write8(p, Room->MatchBanMapType);
	Write8(p, 0); // PlayerNum
	Write8(p, 0); // HasSJKart
	Write8(p, 0); // HasSJFruit
	Write8(p, 0); // ParaNum

	const DWORD len = static_cast<DWORD>(p - buf.data());
	const BOOL sent = SendToClient(
		Client, 25009, buf.data(), len,
		Client->ConnID, FE_PLAYER, GetClientWirePlayerHeaderID(Client), Notify);
	QQS_DebugPrintf10001(
		"[RANKED-BAN-MAP][25009] Uin=%u RoomID=%u MapCount=%zu PayloadLen=%u WireBodyLen=%u Sent=%d\n",
		Client->Uin, Room->ID, Room->MatchBanMaps.size(),
		static_cast<unsigned>(len), static_cast<unsigned>(len + 2), sent ? 1 : 0);
	return sent != FALSE;
}

NOINLINE static bool NotifyRankedMatchBanMapResult(
	std::shared_ptr<ClientNode> Client,
	const std::vector<CMatchBanMapInfo>& maps,
	BYTE type)
{
	if (!Client || maps.empty())
		return false;
	std::vector<BYTE> buf(4 + maps.size() * 17 + 1);
	BYTE* p = buf.data();
	Write32(p, static_cast<UINT>(maps.size()));
	for (const CMatchBanMapInfo& mapInfo : maps)
		WriteRankedBanMapInfo(p, mapInfo);
	Write8(p, type);
	const DWORD len = static_cast<DWORD>(p - buf.data());
	const BOOL sent = SendToClient(
		Client, 25011, buf.data(), len,
		Client->ConnID, FE_PLAYER, GetClientWirePlayerHeaderID(Client), Notify);
	QQS_DebugPrintf10001(
		"[RANKED-BAN-MAP][25011] Uin=%u MapCount=%zu Type=%u PayloadLen=%u WireBodyLen=%u Sent=%d\n",
		Client->Uin, maps.size(), static_cast<unsigned>(type),
		static_cast<unsigned>(len), static_cast<unsigned>(len + 2), sent ? 1 : 0);
	return sent != FALSE;
}


void WINAPI OnBorderRace(std::shared_ptr<RoomNode> Room)
{
	if (!Room)
		return;
	if (Room->matchQueueType == 0x0A)
	{
		const RoomSeatMask activeMask = Room->RaceActiveMask;
		if (Room->RankedStartPhase != RankedMatchStartPhase::RaceBeginPending ||
			Room->RankedRaceBeginSent || activeMask == 0)
		{
			QQS_DebugPrintf10001(
				"[RANKED-START][511-BLOCK] RoomID=%u Phase=%u ActiveMask=0x%04X Sent=%d\n",
				Room->ID, static_cast<unsigned>(Room->RankedStartPhase),
				static_cast<unsigned>(activeMask),
				Room->RankedRaceBeginSent ? 1 : 0);
			return;
		}

		Room->dwStart = GetTickCount();
		int sentNum = 0;
		for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX; ++seat)
		{
			const RoomSeatMask bit = GetRoomSeatBit(seat);
			if ((activeMask & bit) == 0)
				continue;
			const std::shared_ptr<ClientNode>& RoomClient = Room->Player[seat];
			if (!IsRankedStartPlayerForRoom(Room, RoomClient, seat) ||
				!RoomClient->NormalRaceWireActive ||
				!RoomClient->NormalRaceKartPhysReady ||
				!NotifyRaceBegin(RoomClient))
			{
				Room->RankedStartPhase = RankedMatchStartPhase::Failed;
				Room->dwStart = 0;
				QQS_DebugPrintf10001(
					"[RANKED-START][511-FAIL] RoomID=%u Seat=%d Uin=%u SentNum=%d\n",
					Room->ID, seat, RoomClient ? RoomClient->Uin : 0, sentNum);
				return;
			}
			++sentNum;
		}
		Room->RankedRaceBeginSent = true;
		Room->RankedStartPhase = RankedMatchStartPhase::Racing;
		QQS_DebugPrintf10001(
			"[RANKED-START][511] RoomID=%u PlayerNum=%d ActiveMask=0x%04X\n",
			Room->ID, sentNum, static_cast<unsigned>(activeMask));
		return;
	}

	Room->dwStart = GetTickCount();
	for (char i = 0; i < 6; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			NotifyRaceBegin(RoomClient);
			//SendToClient(RoomClient, 28241, (BYTE*)"\x00\x00\x27\x11\x00\x00\x14\x00\x00\x00\x63\x00\x00\x00\x00\x00\x00\x00", 18, RoomClient->GameID, FE_GAMELOGIC, 1, Notify);

		}
	}
}

void WINAPI OnBorderGame(std::shared_ptr<RoomNode> Room)
{
	if (!Room)
		return;
	if (Room->matchQueueType == 0x0A)
	{
		if (Room->RankedStartPhase != RankedMatchStartPhase::BanMapResult ||
			Room->RaceActiveMask == 0 || !Room->RaceAuthorityValid)
		{
			QQS_DebugPrintf10001(
				"[RANKED-START][LOAD-BLOCK] RoomID=%u Phase=%u ActiveMask=0x%04X Authority=%d\n",
				Room->ID, static_cast<unsigned>(Room->RankedStartPhase),
				static_cast<unsigned>(Room->RaceActiveMask), Room->RaceAuthorityValid ? 1 : 0);
			return;
		}

		const RoomSeatMask activeMask = Room->RaceActiveMask;
		for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX; ++seat)
		{
			const RoomSeatMask bit = GetRoomSeatBit(seat);
			if ((activeMask & bit) == 0)
				continue;
			const std::shared_ptr<ClientNode>& player = Room->Player[seat];
			if (!IsRankedStartPlayerForRoom(Room, player, seat) ||
				player->ServerID == 0 || GetClientWirePlayerID16(player) == 0 ||
				player->MapID != Room->MapID || !PreflightKartPhysParamForPlayer(player))
			{
				Room->RankedStartPhase = RankedMatchStartPhase::Failed;
				QQS_DebugPrintf10001(
					"[RANKED-START][LOAD-PREFLIGHT-FAIL] RoomID=%u Seat=%d Uin=%u ServerID=%u MapID=%u RoomMapID=%u\n",
					Room->ID, seat, player ? player->Uin : 0,
					player ? static_cast<unsigned>(player->ServerID) : 0,
					player ? player->MapID : 0, Room->MapID);
				return;
			}
		}

		Room->RankedRaceBeginSent = false;
		Room->RankedStartPhase = RankedMatchStartPhase::Loading;
		int dispatchedNum = 0;
		bool dispatchFailed = false;
		for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX; ++seat)
		{
			const RoomSeatMask bit = GetRoomSeatBit(seat);
			if ((activeMask & bit) == 0)
				continue;
			const std::shared_ptr<ClientNode>& RoomClient = Room->Player[seat];

			// 排位加载帧保持连续序号：548 -> 509(seq1) ->
			// 551(seq2) -> 784(seq3)；511 由本分支的服务端加载计时推进。
			if (!NotifyBorderPlayerInfo(RoomClient, Room))
			{
				dispatchFailed = true;
				break;
			}
			ResetNormalRaceWireState(RoomClient);
			{
				std::lock_guard<std::mutex> sequenceLock(RoomClient->NormalRaceWireSequenceMutex);
				RoomClient->NormalRaceWireActive = true;
				RoomClient->NormalRaceKartPhysReady = false;
				RoomClient->NormalRaceWireRouteID = RoomClient->ServerID;
				RoomClient->NormalRaceWireLastMsgSeq = 0;
			}
			RoomClient->MsgSequence = -1;
			RoomClient->IsReady = FALSE;

			const bool gameBeginSent = NotifyGameBegin(RoomClient, Room->Player);
			const bool kartPhysSent = gameBeginSent && NotifyKartPhysParam(RoomClient, Room->Player);
			const bool npcInfoSent = kartPhysSent && NotifyGameNPCInfo(RoomClient);
			if (!gameBeginSent || !kartPhysSent || !npcInfoSent)
			{
				QQS_DebugPrintf10001(
					"[RANKED-START][LOAD-DISPATCH-FAIL] RoomID=%u Uin=%u RouteID=%u Sent548=1 Sent509=%d Sent551=%d Sent784=%d\n",
					Room->ID, RoomClient->Uin,
					static_cast<unsigned>(RoomClient->NormalRaceWireRouteID),
					gameBeginSent ? 1 : 0, kartPhysSent ? 1 : 0, npcInfoSent ? 1 : 0);
				dispatchFailed = true;
				break;
			}
			RoomClient->NormalRaceKartPhysReady = true;
			++dispatchedNum;
		}

		if (dispatchFailed)
		{
			Room->RankedStartPhase = RankedMatchStartPhase::Failed;
			for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX; ++seat)
			{
				if (Room->Player[seat])
					ResetNormalRaceWireState(Room->Player[seat]);
			}
			return;
		}

		Room->RankedStartPhase = RankedMatchStartPhase::RaceBeginPending;
		QQS_DebugPrintf10001(
			"[RANKED-START][LOADING] RoomID=%u MapID=%u PlayerNum=%d ActiveMask=0x%04X DelayMs=%d -> 511\n",
			Room->ID, Room->MapID, dispatchedNum,
			static_cast<unsigned>(activeMask), kRankedRaceBeginDelayMs);
		CreateRoomTimer(Room, kRankedRaceBeginDelayMs, OnBorderRace);
		return;
	}

	for (char i = 0; i < 6; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			RoomClient->MsgSequence = -1;
			NotifyUpdateKartSkillStoneInfo(RoomClient, RoomClient->KartID);
			if (RoomClient->KartSkinID != 0 && RoomClient->KartSkinID != RoomClient->KartID)
				NotifyUpdateKartSkillStoneInfo(RoomClient, RoomClient->KartSkinID);
			if (RoomClient->GlobalStone == 1)
			{
				NotifyGlobalStoneForKartAndSkin(RoomClient);//全局宝石/糖果，按当前可见皮肤一起刷新
				//NotifyUpdateKartSkillStoneInfo2(RoomClient, RoomClient->KartID);//全局宝石
			}


			const bool gameBeginSent = NotifyGameBegin(RoomClient, Room->Player);
			QQS_DebugPrintf10001(
				"[FLOW][BORDER-509-RESULT] Uin=%u RoomID=%u BeiginMode=%d Sent=%d Next=NPC/551\n",
				RoomClient->Uin, Room->ID, beiginmode, gameBeginSent ? 1 : 0);
			NotifyGameNPCInfo(RoomClient);
			//NotifyKartPhysParam(RoomClient, NULL);
			if (mode28346 == 2) {
				NotifyKartPhysParam(RoomClient, Room->Player);
			}
			else {
				NotifyEncryptedMsg(RoomClient);
			}
		}
	}

	//跳舞并同步加载
	CreateRoomTimer(Room, 16000, OnBorderRace);

}

void WINAPI OnRankedMatchBanMapInfo(std::shared_ptr<RoomNode> Room)
{
	if (!Room || Room->MatchBanMaps.empty() ||
		Room->RankedStartPhase != RankedMatchStartPhase::BanMapChoosing)
		return;

	for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX; ++seat)
	{
		const RoomSeatMask bit = GetRoomSeatBit(seat);
		if ((Room->RaceActiveMask & bit) == 0)
			continue;
		const std::shared_ptr<ClientNode>& RoomPlayer = Room->Player[seat];
		if (!IsRankedStartPlayerForRoom(Room, RoomPlayer, seat))
		{
			QQS_DebugPrintf10001(
				"[RANKED-BAN-MAP][FINALIZE-FAIL] RoomID=%u Seat=%d Uin=%u Reason=active-player-invalid\n",
				Room->ID, seat, RoomPlayer ? RoomPlayer->Uin : 0);
			Room->RankedStartPhase = RankedMatchStartPhase::Failed;
			return;
		}
		for (UINT selectedMapID : RoomPlayer->MatchBanMapIDs)
		{
			auto selected = std::find_if(
				Room->MatchBanMaps.begin(), Room->MatchBanMaps.end(),
				[selectedMapID](const CMatchBanMapInfo& info) {
					return info.MapID == selectedMapID;
				});
			if (selected == Room->MatchBanMaps.end() ||
				(!IsRankedBanMapSelectableStatus(selected->Status) && selected->Status != 2))
				continue;
			if (selected->Status != 2)
				selected->Status = 2;
			selected->TotalBeBannedNums++;
		}
	}

	std::vector<size_t> candidates;
	for (size_t i = 0; i < Room->MatchBanMaps.size(); ++i)
	{
		if (IsRankedBanMapSelectableStatus(Room->MatchBanMaps[i].Status))
			candidates.emplace_back(i);
	}
	if (candidates.empty())
	{
		QQS_DebugPrintf10001(
			"[RANKED-BAN-MAP][FINALIZE-FAIL] RoomID=%u Reason=no-selectable-map\n",
			Room->ID);
		Room->RankedStartPhase = RankedMatchStartPhase::Failed;
		return;
	}

	static thread_local std::mt19937 generator(std::random_device{}());
	std::uniform_int_distribution<size_t> distribution(0, candidates.size() - 1);
	CMatchBanMapInfo& finalMap = Room->MatchBanMaps[candidates[distribution(generator)]];
	finalMap.Status = 3;
	Room->MapID = finalMap.MapID;

	Map* pMap = GetMap(Room->MapID);
	if (!pMap)
	{
		QQS_DebugPrintf10001(
			"[RANKED-BAN-MAP][FINALIZE-FAIL] RoomID=%u MapID=%u Reason=map-config-missing\n",
			Room->ID, Room->MapID);
		Room->RankedStartPhase = RankedMatchStartPhase::Failed;
		return;
	}

	bool resultSent = true;
	int resultSentNum = 0;
	for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX; ++seat)
	{
		const RoomSeatMask bit = GetRoomSeatBit(seat);
		if ((Room->RaceActiveMask & bit) == 0)
			continue;
		const std::shared_ptr<ClientNode>& RoomPlayer = Room->Player[seat];
		if (!IsRankedStartPlayerForRoom(Room, RoomPlayer, seat))
		{
			resultSent = false;
			break;
		}
		RoomPlayer->MapCompatibilityMode = false;
		RoomPlayer->EndCheckPoint = pMap->End;
		RoomPlayer->TotalRound = pMap->Round;
		RoomPlayer->Loop = pMap->Loop;
		RoomPlayer->MapID = Room->MapID;
		if (!NotifyRankedMatchBanMapResult(
			RoomPlayer, Room->MatchBanMaps, Room->MatchBanMapType))
		{
			resultSent = false;
			break;
		}
		++resultSentNum;
	}
	if (!resultSent || resultSentNum != Room->RaceStartPlayerNum)
	{
		QQS_DebugPrintf10001(
			"[RANKED-BAN-MAP][25011-FAIL] RoomID=%u Sent=%d Expected=%u\n",
			Room->ID, resultSentNum, static_cast<unsigned>(Room->RaceStartPlayerNum));
		Room->RankedStartPhase = RankedMatchStartPhase::Failed;
		return;
	}
	Room->RankedStartPhase = RankedMatchStartPhase::BanMapResult;
	CreateRoomTimer(Room, static_cast<int>(kRankedBanShowTimeSeconds * 1000), OnBorderGame);
}

void WINAPI OnNotifyPlayerInfo(std::shared_ptr<RoomNode> Room)
{
	if (!Room)
		return;
	const bool isAuroraRoom =
		Room->AuroraUGC && Room->matchQueueType == 8;
	if (isAuroraRoom &&
		Room->AuroraStartPhase != AuroraMatchStartPhase::EnterRoomComplete)
	{
		QQS_DebugPrintf10001(
			"[AURORA][1718-BLOCK] RoomID=%u Phase=%u Reason=20082-not-complete\n",
			Room->ID, static_cast<unsigned>(Room->AuroraStartPhase));
		return;
	}
	if (Room->matchQueueType == 0x0A)
	{
		if (Room->RankedStartPhase != RankedMatchStartPhase::EnterRoomComplete)
			return;
		int notifyNum = 0;
		for (int seat = 0; seat < ROOM_SEAT_NORMAL_MAX; ++seat)
		{
			const RoomSeatMask bit = GetRoomSeatBit(seat);
			if ((Room->RaceActiveMask & bit) == 0)
				continue;
			const std::shared_ptr<ClientNode>& RoomClient = Room->Player[seat];
			if (!IsRankedStartPlayerForRoom(Room, RoomClient, seat) ||
				!NotifyRankedMatchBanMapInfo(RoomClient))
			{
				Room->RankedStartPhase = RankedMatchStartPhase::Failed;
				QQS_DebugPrintf10001(
					"[RANKED-START][25009-FAIL] RoomID=%u Seat=%d Uin=%u\n",
					Room->ID, seat, RoomClient ? RoomClient->Uin : 0);
				return;
			}
			++notifyNum;
		}
		Room->RankedStartPhase = RankedMatchStartPhase::BanMapChoosing;
		QQS_DebugPrintf10001(
			"[RANKED-START][25009] RoomID=%u PlayerNum=%d MapCount=%zu ChooseTime=%u\n",
			Room->ID, notifyNum, Room->MatchBanMaps.size(), kRankedBanChooseTimeSeconds);
		CreateRoomTimer(
			Room, static_cast<int>(kRankedBanChooseTimeSeconds * 1000),
			OnRankedMatchBanMapInfo);
		return;
	}

	int time = 0;
	bool isBorderRaceRoom = IsTrackedBorderRaceRoomLocal(Room);
	int skippedSlotNum = 0;
	int notifyNum = 0;
	for (char i = 0; i < 6; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			if (isBorderRaceRoom && !IsBorderRacePlayerForRoomLocal(Room, RoomClient))
			{
				skippedSlotNum++;
				continue;
			}
			notifyNum++;
			const bool playerInfoSent = NotifyBorderPlayerInfo(RoomClient, Room);
			if (isAuroraRoom && !playerInfoSent)
			{
				Room->AuroraStartPhase = AuroraMatchStartPhase::Failed;
				QQS_DebugPrintf10001(
					"[AURORA][1718-FAIL] RoomID=%u Uin=%u Reason=response-send\n",
					Room->ID, RoomClient->Uin);
				return;
			}
			if (RoomClient->TaskID == 0xFFFC)
			{
				time = 4000;
				NotifyRankedMatchBanMapInfo(RoomClient);//排位禁图

			}
			else
			{
				time = 1000;
			}
		}
	}
	if (isBorderRaceRoom && (skippedSlotNum > 0 || notifyNum != Room->CurrentPlayerNum))
	{
		QQS_DebugPrintf10001("[FLOW][BORDER-BEGIN-ROSTER] Stage=OnNotifyPlayerInfo BorderRoomID=%u CurrentPlayerNum=%u NotifyNum=%d Skipped=%d\n",
			Room->ID, (unsigned)Room->CurrentPlayerNum, notifyNum, skippedSlotNum);
	}
	if (time == 4000)
	{
		CreateRoomTimer(Room, time, OnRankedMatchBanMapInfo);
		return;
	}
	if (isAuroraRoom)
	{
		// packet_dumps_6752: the 1718 dispatch occurs in the same official
		// callback as the successful 20082 response, and 509 follows without
		// the legacy one-second non-ranked delay.
		Room->AuroraStartPhase = AuroraMatchStartPhase::Loading;
		QQS_DebugPrintf10001(
			"[AURORA][1718] RoomID=%u PlayerNum=%d Phase=loading -> 509 Source=dump6752\n",
			Room->ID, notifyNum);
		OnBorderGame(Room);
		return;
	}
	//开始游戏
	CreateRoomTimer(Room, time, OnBorderGame);
}

void WINAPI OnMatchResult(std::shared_ptr<RoomNode> Room)
{
	if (!Room)
		return;
	const bool isRankedRoom = Room->matchQueueType == 0x0A;
	const bool isOfficialBorderRoom =
		!Room->AuroraUGC && Room->matchQueueType == 0 &&
		IsTrackedBorderRaceRoomLocal(Room);
	const bool isAuroraRoom =
		Room->AuroraUGC && Room->matchQueueType == 8;
	if (isAuroraRoom)
	{
		// CreateRoomTimer invokes this callback while holding Room->mutex.
		// Locking it again here deadlocks before the official 20081 handoff.
		if (Room->AuroraStartPhase != AuroraMatchStartPhase::Idle ||
			Room->CurrentPlayerNum != 1 || Room->RaceActiveMask == 0 ||
			!Room->RaceAuthorityValid)
		{
			Room->AuroraStartPhase = AuroraMatchStartPhase::Failed;
			QQS_DebugPrintf10001(
				"[AURORA][20081-BLOCK] RoomID=%u PlayerNum=%u ActiveMask=0x%04X Authority=%d\n",
				Room->ID, static_cast<unsigned>(Room->CurrentPlayerNum),
				static_cast<unsigned>(Room->RaceActiveMask),
				Room->RaceAuthorityValid ? 1 : 0);
			return;
		}
		Room->AuroraStartPhase = AuroraMatchStartPhase::AwaitEnterRoom;
	}
	if (isRankedRoom)
	{
		if (Room->RankedStartPhase != RankedMatchStartPhase::Idle ||
			Room->RaceActiveMask == 0 || !Room->RaceAuthorityValid)
		{
			Room->RankedStartPhase = RankedMatchStartPhase::Failed;
			QQS_DebugPrintf10001(
				"[RANKED-START][20081-BLOCK] RoomID=%u ActiveMask=0x%04X Authority=%d\n",
				Room->ID, static_cast<unsigned>(Room->RaceActiveMask),
				Room->RaceAuthorityValid ? 1 : 0);
			return;
		}
		Room->RankedEnteredMask = 0;
		Room->RankedRaceBeginSent = false;
	}
	if (isOfficialBorderRoom)
	{
		if (Room->RaceActiveMask == 0 || !Room->RaceAuthorityValid)
		{
			QQS_DebugPrintf10001(
				"[FLOW][20081-BORDER-BLOCK] RoomID=%u ActiveMask=0x%04X Authority=%d\n",
				Room->ID, static_cast<unsigned>(Room->RaceActiveMask),
				Room->RaceAuthorityValid ? 1 : 0);
			return;
		}
		Room->BorderStartDispatched = false;
	}

	bool isBorderRaceRoom = IsTrackedBorderRaceRoomLocal(Room);
	int skippedSlotNum = 0;
	int notifyNum = 0;
	for (char i = 0; i < 6; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			if (isRankedRoom)
			{
				const RoomSeatMask bit = GetRoomSeatBit(i);
				if ((Room->RaceActiveMask & bit) == 0 ||
					!IsRankedStartPlayerForRoom(Room, RoomClient, i))
				{
					skippedSlotNum++;
					continue;
				}
			}
			if (isBorderRaceRoom && !IsBorderRacePlayerForRoomLocal(Room, RoomClient))
			{
				skippedSlotNum++;
				continue;
			}
			notifyNum++;
			if (!NotifyMatchResult(RoomClient, Room->matchQueueType))
			{
				if (isRankedRoom)
					Room->RankedStartPhase = RankedMatchStartPhase::Failed;
				if (isAuroraRoom)
					Room->AuroraStartPhase = AuroraMatchStartPhase::Failed;
				return;
			}
		}
	}
	if (isBorderRaceRoom && (skippedSlotNum > 0 || notifyNum != Room->CurrentPlayerNum))
	{
		QQS_DebugPrintf10001("[FLOW][BORDER-BEGIN-ROSTER] Stage=OnMatchResult BorderRoomID=%u CurrentPlayerNum=%u NotifyNum=%d Skipped=%d\n",
			Room->ID, (unsigned)Room->CurrentPlayerNum, notifyNum, skippedSlotNum);
	}
	if (isRankedRoom)
	{
		if (notifyNum != Room->RaceStartPlayerNum)
		{
			Room->RankedStartPhase = RankedMatchStartPhase::Failed;
			QQS_DebugPrintf10001(
				"[RANKED-START][20081-FAIL] RoomID=%u Sent=%d Expected=%u Skipped=%d\n",
				Room->ID, notifyNum, static_cast<unsigned>(Room->RaceStartPlayerNum), skippedSlotNum);
			return;
		}
		// The original working 20081 does not advertise EnterInfo/PVP handoff, so
		// no 697/20082 control connection is expected. Continue to 25009 on the
		// established player sessions, matching the old ranked startup order.
		Room->RankedStartPhase = RankedMatchStartPhase::EnterRoomComplete;
		QQS_DebugPrintf10001(
			"[RANKED-START][20081-MAIN] RoomID=%u PlayerNum=%d DelayMs=1000 -> 25009\n",
			Room->ID, notifyNum);
		CreateRoomTimer(Room, 1000, OnNotifyPlayerInfo);
		return;
	}
	if (isAuroraRoom)
	{
		if (notifyNum != 1)
		{
			Room->AuroraStartPhase = AuroraMatchStartPhase::Failed;
			QQS_DebugPrintf10001(
				"[AURORA][20081-FAIL] RoomID=%u Sent=%d Expected=1 Skipped=%d\n",
				Room->ID, notifyNum, skippedSlotNum);
			return;
		}
		QQS_DebugPrintf10001(
			"[AURORA][20081-WAIT] RoomID=%u PlayerNum=1 Next=client-20082 Source=dump6752\n",
			Room->ID);
		return;
	}
	if (isOfficialBorderRoom)
	{
		if (notifyNum != Room->RaceStartPlayerNum)
		{
			QQS_DebugPrintf10001(
				"[FLOW][20081-BORDER-FAIL] RoomID=%u Sent=%d Expected=%u Skipped=%d\n",
				Room->ID, notifyNum,
				static_cast<unsigned>(Room->RaceStartPlayerNum), skippedSlotNum);
			return;
		}
		QQS_DebugPrintf10001(
			"[FLOW][20081-BORDER-MAIN] RoomID=%u PlayerNum=%d ActiveMask=0x%04X HasEnterInfo=0 DelayMs=1000 Next=25009\n",
			Room->ID, notifyNum, static_cast<unsigned>(Room->RaceActiveMask));
		CreateRoomTimer(Room, 1000, OnNotifyPlayerInfo);
		return;
	}

	// 非排位旧链仍按原有 1 秒延时继续。
	CreateRoomTimer(Room, 1000, OnNotifyPlayerInfo);

}


void RankedMatchBeginTest()
{
	std::lock_guard<std::recursive_mutex> lg(RankedQueueMutex);
	const RankConfig& rankCfg = GetRankConfig();
	if (!rankCfg.LoadedFromYaml || !rankCfg.Enabled || rankCfg.MinimumMatchPlayers == 0)
	{
		return;
	}
	const int minimumPlayers = static_cast<int>(rankCfg.MinimumMatchPlayers);
	PruneInvalidMatchQueue_NoLock(RankedMatchQueue, 0xFFFC, "ranked-pre");
	int dequesize = RankedMatchQueue.size();


	auto now = std::chrono::steady_clock::now();
	if (dequesize < minimumPlayers && rankCfg.ManMachineEnabled) {
		for (auto it = RankedMatchQueue.begin(); it != RankedMatchQueue.end(); /* no increment here */)
		{
			if (!IsClientValidForMatchQueue(it->first, 0xFFFC))
			{
				it = RankedMatchQueue.erase(it);
				continue;
			}
			auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - it->second);
			if (duration.count() > static_cast<long long>(rankCfg.ManMachineWaitSeconds) && it->first->isManMachine == 1)
			{
				ResponseStartQuickMatchGame2_Rank(it->first, 0xa);
				// 根据需要执行其他操作，如移除过期的客户端
				it = RankedMatchQueue.erase(it); // 删除过期的元素
			}
			else
			{
				++it; // 只有在没有删除元素时才移动迭代器
			}
		}
	}
	PruneInvalidMatchQueue_NoLock(RankedMatchQueue, 0xFFFC, "ranked-post");
	dequesize = RankedMatchQueue.size();


	//printf("RankedMatchQueueSize:%d\n", dequesize);
	if (dequesize >= minimumPlayers)
	{
		std::shared_ptr<RoomNode> RankedRoom = NewBorderRoom();
		if (!RankedRoom)
		{
			return;
		}
		RankedRoom->startStatus = 2;
		RankedRoom->BaseGameMode = 0x28;
		RankedRoom->SubGameMode = 1;
		RankedRoom->SeatNum = ROOM_SEAT_NORMAL_MAX;
		RankedRoom->matchQueueType = 0xa;
		if (!BeginRoomGameLogicSession(RankedRoom, "ranked-match"))
			return;

		for (int i = 0; i < dequesize; i++)
		{
			if (i == 6 || RankedMatchQueue.empty())
			{
				break;
			}
			std::shared_ptr<ClientNode> Client = RankedMatchQueue.front().first;
			RankedMatchQueue.pop_front();//队列删除用户
			if (!IsClientValidForMatchQueue(Client, 0xFFFC))
			{
				i--;
				continue;
			}
			RankedRoom->Player[i] = Client;//用户加入排位房间
			ResetNormalRaceWireState(Client);
			Client->GameID = RankedRoom->RaceGameID;
			Client->SeatID = static_cast<char>(i);
			Client->MatchBanMapIDs.clear();
			Client->MapCompatibilityMode = false;
			Client->FinishTime = 0;
			Client->Round = 0;
			Client->PassedCheckPoint = 0;
			Client->IsFinish = FALSE;
			Client->ranknumber = 0;
			Client->HighestSpeed = 0;
			Client->CollisionCount = 0;
			Client->N2OGatherCount = 0;
			Client->DriftCount = 0;
			Client->RaceDetail = RaceDetailRecord{};
			Client->LapTime = 0;
			Client->MAXLapTime = 0;
			Client->LastDistanceToEnd = 0;
			Client->LastRaceLapTime = 0;
			Client->IsReady = TRUE;
			Client->real = 1;
			Client->BorderRoomID = RankedRoom->ID;
			Client->CurrentRoom = RankedRoom.get();
			Client->IsBanMap = false;
			RankedRoom->CurrentPlayerNum++;
		}
		if (RankedRoom->CurrentPlayerNum > 0)
		{
			if (!InitializeRankedRoomMapState(RankedRoom))
			{
				QQS_DebugPrintf10001(
					"[RANKED-START][ABORT] RoomID=%u Reason=ranked-map-state-invalid\n",
					RankedRoom->ID);
				return;
			}
			if (!CaptureRoomRaceStartSnapshot(RankedRoom, "ranked-match-start"))
			{
				QQS_DebugPrintf10001(
					"[RANKED-START][ABORT] RoomID=%u Reason=race-snapshot-invalid\n",
					RankedRoom->ID);
				return;
			}
			CreateRoomTimer(RankedRoom, 1000, OnMatchResult);
		}
	}
}


void BorderMatchBeginTest()
{
	std::lock_guard<std::recursive_mutex> lg(BorderQueueMutex);
	const size_t queueSizeBeforePrune = BorderMatchQueue.size();
	PruneInvalidMatchQueue_NoLock(BorderMatchQueue, 0xFFFD, "border-pre");

	int dequesize = (int)BorderMatchQueue.size();
	if (queueSizeBeforePrune > 0 || dequesize > 0)
	{
		int validQueueClients = 0;
		for (const auto& entry : BorderMatchQueue)
		{
			if (IsClientValidForMatchQueue(entry.first, 0xFFFD))
				++validQueueClients;
		}
		QQS_DebugPrintf10001(
			"[FLOW][BORDER-MATCH-TICK] QueueBefore=%zu QueueAfter=%d Valid=%d Required=%d\n",
			queueSizeBeforePrune, dequesize, validQueueClients,
			GetBorderMatchRequiredPlayerNum());
	}

	auto now = std::chrono::steady_clock::now();
	if (dequesize < GetBorderMatchRequiredPlayerNum() && Borderset[1] == 1) {
		for (auto it = BorderMatchQueue.begin(); it != BorderMatchQueue.end(); /* no increment here */)
		{
			if (!IsClientValidForMatchQueue(it->first, 0xFFFD))
			{
				it = BorderMatchQueue.erase(it);
				continue;
			}
			auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - it->second);
			if (!IsQueuedBorderTeamMember_NoLock(it->first) &&
				duration.count() > BorderManTime && it->first->isManMachine == 1)
			{
				ResponseStartQuickMatchGame2(it->first, 0x0, 0);
				// 根据需要执行其他操作，如移除过期的客户端
				it = BorderMatchQueue.erase(it); // 删除过期的元素
			}
			else
			{
				++it; // 只有在没有删除元素时才移动迭代器
			}
		}
	}
	PruneInvalidMatchQueue_NoLock(BorderMatchQueue, 0xFFFD, "border-post");
	dequesize = (int)BorderMatchQueue.size();
	//printf("BorderMatchQueueSize:%d\n", dequesize);
	std::vector<std::shared_ptr<ClientNode>> matchRoster;
	if (TryBuildBorderMatchRoster_NoLock(matchRoster))
	{
		std::shared_ptr<RoomNode> BorderRoom = NewBorderRoom();
		if (!BorderRoom)
		{
			return;
		}
		BorderRoom->SubGameMode = BorderRoom->BaseGameMode = 0x1;

		//随机地图
		BorderRoom->matchQueueType = 0x0;
		BorderRoom->startStatus = 2;
		BorderRoom->MapID = GetRandMap(5);//边境地图池
		if (!BeginRoomGameLogicSession(BorderRoom, "border-match"))
			return;
		for (int i = 0; i < (int)matchRoster.size(); i++)
		{
			if (i == ROOM_SEAT_NORMAL_MAX)
			{
				break;
			}
			std::shared_ptr<ClientNode> Client = matchRoster[i];
			if (!IsClientValidForMatchQueue(Client, 0xFFFD))
			{
				continue;
			}

			BorderRoom->Player[i] = Client;//用户加入排位房间
			ResetNormalRaceWireState(Client);
			Client->GameID = BorderRoom->RaceGameID;
			Map* pMap = GetMap(BorderRoom->MapID);
			if (pMap)
			{
				Client->MapCompatibilityMode = false;
				Client->EndCheckPoint = pMap->End;
				Client->TotalRound = pMap->Round;
				Client->Loop = pMap->Loop;
			}
			else
			{
				Client->MapCompatibilityMode = true;
				Client->EndCheckPoint = 0;
				Client->TotalRound = 1;
				Client->Loop = false;
			}
			Client->MapID = BorderRoom->MapID;
			Client->FinishTime = 0;
			Client->Round = 0;
			Client->PassedCheckPoint = 0;
			Client->IsFinish = FALSE;
			Client->ranknumber = 0;
			Client->HighestSpeed = 0;
			Client->CollisionCount = 0;
			Client->N2OGatherCount = 0;
			Client->DriftCount = 0;
			Client->RaceDetail = RaceDetailRecord{};
			Client->LapTime = 0;
			Client->MAXLapTime = 0;
			Client->LastDistanceToEnd = 0;
			Client->LastRaceLapTime = 0;
			//Client->IsReady = FALSE;
			Client->IsReady = TRUE;
			Client->BorderRoomID = BorderRoom->ID;
			Client->CurrentRoom = BorderRoom.get();
			Client->IsBanMap = false;
			Client->real = 1;
			BorderRoom->CurrentPlayerNum++;
		}
		if (BorderRoom->CurrentPlayerNum > 0)
		{
			QQS_DebugPrintf10001("[FLOW][BORDER-MATCH-BEGIN] BorderRoomID=%u QueueLeft=%zu Roster=%zu CurrentPlayerNum=%u MapID=%u -> 20213/20081/548/509/551/511\n",
				BorderRoom->ID, BorderMatchQueue.size(), matchRoster.size(), (unsigned)BorderRoom->CurrentPlayerNum, BorderRoom->MapID);
			if (!CaptureRoomRaceStartSnapshot(BorderRoom, "match-result-start"))
			{
				QQS_DebugPrintf10001("[FLOW][MATCH-START-ABORT] BorderRoomID=%u Reason=race-snapshot-invalid\n", BorderRoom->ID);
				return;
			}
			CreateRoomTimer(BorderRoom, 1000, OnMatchResult);
		}
	}
}


void NotifyMuchPlayerInfo(std::shared_ptr<ClientNode> Client)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;
	std::shared_ptr<RoomNode> Room = GetActiveRaceRoom(Client);
	RoomNode* rawRoom = Room ? Room.get() : nullptr;
	bool isCustomRoom = (rawRoom && (rawRoom->HasCustomScheme || rawRoom->SpeFlag == 9));
	bool isBorderRaceRoom = (Room && Room->matchQueueType == 0 && Client->BorderRoomID == Room->ID);
	UINT customBaseMode = rawRoom ? rawRoom->CustomBaseGameMode : 0;
	UINT customSubMode = rawRoom ? rawRoom->CustomSubGameMode : 0;
	UINT baseMode = rawRoom ? rawRoom->BaseGameMode : Client->BaseGameMode;
	UINT subMode = rawRoom ? rawRoom->SubGameMode : Client->SubGameMode;
	if (isCustomRoom) {
		baseMode = 42;
		subMode = 27;
	}

	std::vector<std::pair<std::shared_ptr<ClientNode>, char>> visiblePlayers;
	int skippedSlotNum = 0;
	if (Room && isCustomRoom) {
		std::vector<std::shared_ptr<ClientNode>> roster = BuildCustomRaceRoster(Room);
		for (auto& RoomClient : roster) {
			if (!RoomClient)
				continue;
			char seatID = RoomClient->SeatID;
			int seatLimit = GetRoomSeatLimit(Room);
			if (seatID < 0 || seatID >= seatLimit) seatID = (char)visiblePlayers.size();
			visiblePlayers.push_back({ RoomClient, seatID });
		}
		skippedSlotNum = (int)Room->RaceStartPlayerNum - (int)visiblePlayers.size();
		if (skippedSlotNum < 0) skippedSlotNum = 0;
	}
	else if (rawRoom) {
		int seatLimit = Room ? GetRoomSeatLimit(Room) : ROOM_SEAT_NORMAL_MAX;
		for (size_t i = 0; i < rawRoom->Player.size() && i < (size_t)seatLimit; i++) {
			std::shared_ptr<ClientNode> RoomClient = rawRoom->Player[i];
			if (!RoomClient)
				continue;
			if (isCustomRoom && rawRoom->RaceActiveMask != 0)
			{
				RoomSeatMask bit = GetRoomSeatBit((int)i);
				if ((rawRoom->RaceActiveMask & bit) == 0)
				{
					skippedSlotNum++;
					continue;
				}
			}
			if (isBorderRaceRoom && !IsBorderRacePlayerForRoomLocal(Room, RoomClient)) {
				skippedSlotNum++;
				continue;
			}
			char seatID = RoomClient->SeatID;
			if (seatID < 0 || seatID >= seatLimit) seatID = (char)i;
			visiblePlayers.push_back({ RoomClient, seatID });
		}
	}
	else {
		visiblePlayers.push_back({ Client, 0 });
	}

	BYTE playerCount = (BYTE)visiblePlayers.size();
	Write32(p, baseMode);
	Write32(p, subMode);
	Write8(p, playerCount);

	for (auto& entry : visiblePlayers) {
		std::shared_ptr<ClientNode> RoomClient = entry.first;
		WriteVisbleInfo1(RoomClient, p, RoomClient->Uin, entry.second, (short)GetRoomConnPlayerID(Room, RoomClient));
	}
	Write16(p, 0); // KartNum
	Write8(p, 0);  // HasMoreInfo
	len = p - buf.data();
	if (isBorderRaceRoom && (skippedSlotNum > 0 || playerCount != Room->CurrentPlayerNum)) {
		QQS_DebugPrintf10001("[FLOW][1718-ROSTER] Uin=%u BorderRoomID=%u CurrentPlayerNum=%u PlayerCount=%u Skipped=%d\n",
			Client->Uin, Room->ID, (unsigned)Room->CurrentPlayerNum, playerCount, skippedSlotNum);
	}
	if (isCustomRoom) {
		QQS_DebugPrintf10001("[FLOW][1718-CUSTOM] send player info Uin=%u RoomID=%u SpeFlag=%u VisibleBase=%u VisibleSub=%u CustomBase=%u CustomSub=%u PlayerCount=%u StartNum=%u ActiveMask=0x%04X Skipped=%d PayloadLen=%zu WireBodyLen=%zu\n",
			Client->Uin, Client->RoomID, rawRoom ? rawRoom->SpeFlag : 0, baseMode, subMode,
			customBaseMode, customSubMode, playerCount,
			rawRoom ? rawRoom->RaceStartPlayerNum : 0,
			rawRoom ? (unsigned)rawRoom->RaceActiveMask : 0,
			skippedSlotNum, len, len + 2);
	}
	else {
		QQS_DebugPrintf10001("[FLOW][1718] send player info Uin=%u RoomID=%u BorderRoomID=%u Base=%u Sub=%u PlayerCount=%u PayloadLen=%zu WireBodyLen=%zu\n",
			Client->Uin, Client->RoomID, Client->BorderRoomID, baseMode, subMode, playerCount, len, len + 2);
	}
	SendToClient(Client, 1718, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}

bool ResponseEnterMatchRoom(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return false;
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;
	Write16(p, 0); // resultId
	Write8(p, 0);  // reasonLen
	Write8(p, 0);  // HasMemoryRaceInfo
	len = p - buf.data();
	QQS_DebugPrintf10001("[FLOW][20082-v2-wire6] response ok Uin=%u PayloadLen=%zu WireBodyLen=%zu\n", Client->Uin, len, len + 2);
	return SendToClient(
		Client, 20082, buf.data(), static_cast<DWORD>(len),
		Client->ConnID, FE_PLAYER, Client->ConnID, Response) != FALSE;
}
void RequestEnterMatchRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (!Client || !Body || BodyLen < sizeof(USHORT))
		return;
	// OnRecvFromClient passes Body at the first payload byte while BodyLen still
	// includes the two-byte wire length. Official Msg20082 is exactly 12 bytes.
	const size_t payloadLen = BodyLen - sizeof(USHORT);
	BYTE* p = Body;
	UINT Uin = 0;
	UINT Time = 0;
	UINT RoomID = 0;
	if (payloadLen >= 4) Uin = Read32(p);
	if (payloadLen >= 8) Time = Read32(p);
	if (payloadLen >= 12) RoomID = Read32(p);

	// In official matchmaking Msg697/20082 use a fresh PVP transport. Resolve
	// its authenticated request back to the canonical lobby session for room
	// state, while keeping Client for the response on this transport.
	std::shared_ptr<ClientNode> RaceClient = Client;
	if (!Client->IsLogin && Uin != 0 && Client->TransportConnIDUin == Uin)
	{
		const std::shared_ptr<ClientNode> sessionClient = GetClient(Uin);
		if (sessionClient && sessionClient != Client)
			RaceClient = sessionClient;
	}
	const std::shared_ptr<RoomNode> ActiveRoom = GetActiveRaceRoom(RaceClient);
	const bool isAuroraRequest = ActiveRoom && ActiveRoom->AuroraUGC &&
		ActiveRoom->matchQueueType == 8;
	if (isAuroraRequest)
	{
		if (payloadLen != 12u)
		{
			QQS_DebugPrintf10001(
				"[AURORA][20082-REJECT] Uin=%u RoomID=%u BodyLen=%zu PayloadLen=%zu Reason=wire-length\n",
				Client->Uin, ActiveRoom->ID, BodyLen, payloadLen);
			return;
		}

		const std::shared_ptr<RoomNode> RequestedRoom =
			RoomID ? GetBorderRoom(RoomID) : nullptr;
		std::unique_lock<std::shared_mutex> roomLock(ActiveRoom->mutex);
		const int seat = FindRankedRoomSeat(ActiveRoom, RaceClient);
		const bool validPlayer = seat >= 0 &&
			RaceClient->BorderRoomID == ActiveRoom->ID &&
			RaceClient->CurrentRoom == ActiveRoom.get() &&
			RaceClient->GameID == ActiveRoom->RaceGameID &&
			ActiveRoom->RaceGameID != 0;
		if (RequestedRoom != ActiveRoom || Uin != RaceClient->Uin ||
			!validPlayer ||
			ActiveRoom->AuroraStartPhase != AuroraMatchStartPhase::AwaitEnterRoom)
		{
			QQS_DebugPrintf10001(
				"[AURORA][20082-REJECT] Uin=%u ReqUin=%u Time=%u ReqRoomID=%u ActiveRoomID=%u Seat=%d Phase=%u Reason=state-or-identity\n",
				RaceClient->Uin, Uin, Time, RoomID, ActiveRoom->ID, seat,
				static_cast<unsigned>(ActiveRoom->AuroraStartPhase));
			return;
		}
		if (!ResponseEnterMatchRoom(Client))
		{
			ActiveRoom->AuroraStartPhase = AuroraMatchStartPhase::Failed;
			QQS_DebugPrintf10001(
				"[AURORA][20082-FAIL] Uin=%u RoomID=%u Reason=response-send\n",
				RaceClient->Uin, ActiveRoom->ID);
			return;
		}

		ActiveRoom->AuroraStartPhase = AuroraMatchStartPhase::EnterRoomComplete;
		QQS_DebugPrintf10001(
			"[AURORA][20082] Uin=%u RoomID=%u Phase=complete -> 1718 Source=dump6752\n",
			RaceClient->Uin, ActiveRoom->ID);
		roomLock.unlock();
		OnNotifyPlayerInfo(ActiveRoom);
		return;
	}
	const bool isOfficialBorderRequest = ActiveRoom && !ActiveRoom->AuroraUGC &&
		ActiveRoom->matchQueueType == 0;
	if (isOfficialBorderRequest)
	{
		if (payloadLen != 12u)
		{
			QQS_DebugPrintf10001(
				"[FLOW][20082-BORDER-REJECT] Uin=%u RoomID=%u BodyLen=%zu PayloadLen=%zu Reason=wire-length\n",
				RaceClient->Uin, ActiveRoom->ID, BodyLen, payloadLen);
			return;
		}

		const std::shared_ptr<RoomNode> RequestedRoom =
			RoomID ? GetBorderRoom(RoomID) : nullptr;
		std::unique_lock<std::shared_mutex> roomLock(ActiveRoom->mutex);
		const int seat = FindRankedRoomSeat(ActiveRoom, RaceClient);
		const RoomSeatMask bit = GetRoomSeatBit(seat);
		const bool validPlayer = bit != 0 &&
			(ActiveRoom->RaceActiveMask & bit) != 0 &&
			RaceClient->TaskID == 0xFFFD &&
			RaceClient->BorderRoomID == ActiveRoom->ID &&
			RaceClient->CurrentRoom == ActiveRoom.get() &&
			IsBorderRacePlayerForRoomLocal(ActiveRoom, RaceClient);
		if (RequestedRoom != ActiveRoom || Uin != RaceClient->Uin || !validPlayer)
		{
			QQS_DebugPrintf10001(
				"[FLOW][20082-BORDER-REJECT] Uin=%u ReqUin=%u Time=%u ReqRoomID=%u ActiveRoomID=%u Seat=%d ActiveMask=0x%04X Started=%d Reason=state-or-identity\n",
				RaceClient->Uin, Uin, Time, RoomID, ActiveRoom->ID, seat,
				static_cast<unsigned>(ActiveRoom->RaceActiveMask),
				ActiveRoom->BorderStartDispatched ? 1 : 0);
			return;
		}
		if (!ResponseEnterMatchRoom(Client))
		{
			QQS_DebugPrintf10001(
				"[FLOW][20082-BORDER-FAIL] Uin=%u RoomID=%u Seat=%d Reason=response-send\n",
				RaceClient->Uin, ActiveRoom->ID, seat);
			return;
		}

		const bool duplicate = ActiveRoom->BorderStartDispatched;
		ActiveRoom->BorderStartDispatched = true;
		QQS_DebugPrintf10001(
			"[FLOW][20082-BORDER] Uin=%u RoomID=%u Seat=%d ActiveMask=0x%04X Duplicate=%d StartNow=%d\n",
			RaceClient->Uin, ActiveRoom->ID, seat,
			static_cast<unsigned>(ActiveRoom->RaceActiveMask),
			duplicate ? 1 : 0, duplicate ? 0 : 1);
		roomLock.unlock();
		if (!duplicate)
			OnNotifyPlayerInfo(ActiveRoom);
		return;
	}
	const bool isRankedRequest = ActiveRoom && ActiveRoom->matchQueueType == 0x0A &&
		Client->TaskID == 0xFFFC;
	if (isRankedRequest)
	{
		if (payloadLen != 12)
		{
			QQS_DebugPrintf10001(
				"[RANKED-START][20082-REJECT] Uin=%u RoomID=%u BodyLen=%zu PayloadLen=%zu Reason=wire-length\n",
				Client->Uin, ActiveRoom->ID, BodyLen, payloadLen);
			return;
		}

		const std::shared_ptr<RoomNode> RequestedRoom = RoomID ? GetBorderRoom(RoomID) : nullptr;
		std::unique_lock<std::shared_mutex> roomLock(ActiveRoom->mutex);
		const int seat = FindRankedRoomSeat(ActiveRoom, RaceClient);
		const RoomSeatMask bit = GetRoomSeatBit(seat);
		const bool alreadyEntered = bit != 0 && (ActiveRoom->RankedEnteredMask & bit) != 0;
		const bool validPhase =
			ActiveRoom->RankedStartPhase == RankedMatchStartPhase::AwaitEnterRoom ||
			(ActiveRoom->RankedStartPhase == RankedMatchStartPhase::EnterRoomComplete && alreadyEntered);
		if (!validPhase || RequestedRoom != ActiveRoom || Uin != RaceClient->Uin ||
			bit == 0 || (ActiveRoom->RaceActiveMask & bit) == 0 ||
			!IsRankedStartPlayerForRoom(ActiveRoom, RaceClient, seat))
		{
			QQS_DebugPrintf10001(
				"[RANKED-START][20082-REJECT] Uin=%u ReqUin=%u Time=%u ReqRoomID=%u ActiveRoomID=%u Seat=%d Phase=%u ActiveMask=0x%04X EnteredMask=0x%04X Reason=state-or-identity\n",
				RaceClient->Uin, Uin, Time, RoomID, ActiveRoom->ID, seat,
				static_cast<unsigned>(ActiveRoom->RankedStartPhase),
				static_cast<unsigned>(ActiveRoom->RaceActiveMask),
				static_cast<unsigned>(ActiveRoom->RankedEnteredMask));
			return;
		}
		if (!ResponseEnterMatchRoom(Client))
		{
			ActiveRoom->RankedStartPhase = RankedMatchStartPhase::Failed;
			QQS_DebugPrintf10001(
				"[RANKED-START][20082-FAIL] Uin=%u RoomID=%u Seat=%d Reason=response-send\n",
				RaceClient->Uin, ActiveRoom->ID, seat);
			return;
		}

		if (!alreadyEntered)
			ActiveRoom->RankedEnteredMask = static_cast<RoomSeatMask>(ActiveRoom->RankedEnteredMask | bit);
		const RoomSeatMask activeMask = ActiveRoom->RaceActiveMask;
		const bool allEntered = activeMask != 0 &&
			(ActiveRoom->RankedEnteredMask & activeMask) == activeMask;
		QQS_DebugPrintf10001(
			"[RANKED-START][20082] Uin=%u RoomID=%u Seat=%d ActiveMask=0x%04X EnteredMask=0x%04X Duplicate=%d AllEntered=%d\n",
			RaceClient->Uin, ActiveRoom->ID, seat,
			static_cast<unsigned>(activeMask),
			static_cast<unsigned>(ActiveRoom->RankedEnteredMask),
			alreadyEntered ? 1 : 0, allEntered ? 1 : 0);
		if (allEntered && ActiveRoom->RankedStartPhase == RankedMatchStartPhase::AwaitEnterRoom)
		{
			ActiveRoom->RankedStartPhase = RankedMatchStartPhase::EnterRoomComplete;
			CreateRoomTimer(ActiveRoom, 1000, OnNotifyPlayerInfo);
		}
		return;
	}

	std::shared_ptr<RoomNode> ReqRoom = RoomID ? GetRoom(RoomID) : nullptr;
	std::shared_ptr<RoomNode> ClientRoom = Client->RoomID ? GetRoom(Client->RoomID) : nullptr;
	bool isCustomRequest = IsCustomRoomFlow(ReqRoom) || IsCustomRoomFlow(ClientRoom);
	if (!isCustomRequest && RoomID != 0 && Client->BorderRoomID == 0)
		Client->BorderRoomID = RoomID;

	QQS_DebugPrintf10001("[FLOW][20082] recv enter room ClientUin=%u ReqUin=%u Time=%u ReqRoomID=%u ClientRoomID=%u BorderRoomID=%u IsCustomReq=%d BodyLen=%zu PayloadLen=%zu\n",
		Client->Uin, Uin, Time, RoomID, Client->RoomID, Client->BorderRoomID, isCustomRequest ? 1 : 0, BodyLen, payloadLen);

	ResponseEnterMatchRoom(Client);
	if (isCustomRequest) {
		std::shared_ptr<RoomNode> Room = ReqRoom ? ReqRoom : ClientRoom;
		if (!Room)
		{
			QQS_DebugPrintf10001("[FLOW][20082-CUSTOM-ACK-IGNORED] no room Uin=%u ReqRoomID=%u ClientRoomID=%u\n", Client->Uin, RoomID, Client->RoomID);
			return;
		}
		QQS_DebugPrintf10001("[FLOW][20082-CUSTOM-ACK-IGNORED] Uin=%u RoomID=%u ReqRoomID=%u ActiveMask=0x%04X StartNum=%u LastS2C=%u -> ack only\n",
			Client->Uin, Room->ID, RoomID, (unsigned)Room->RaceActiveMask, Room->RaceStartPlayerNum, Room->CustomLastSentMsgID);
		return;
	}
	// Official match rooms publish their roster through Msg548. Its PlayerID=0
	// entries are intentionally unresolved and are used by the client's initial
	// Msg153 destinations. Do not overwrite that state with the normal/custom
	// Msg1718 connection-ID roster here.
}

void RequestAddToTeamMatchQueue(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (!Client)
	{
		return;
	}

	BYTE* p = Body;
	size_t payloadLen = BodyLen;
	if (Body && BodyLen >= 2)
	{
		USHORT wireLen = ((USHORT)Body[0] << 8) | Body[1];
		if (wireLen == BodyLen)
		{
			p = Body + 2;
			payloadLen = BodyLen - 2;
		}
	}

	UINT ReqUin = 0;
	UINT ReqTime = 0;
	if (p && payloadLen >= 4) ReqUin = Read32(p);
	if (p && payloadLen >= 8) ReqTime = Read32(p);

	std::shared_ptr<RoomNode> Room = Client->RoomID ? GetRoom(Client->RoomID) : nullptr;
	if (!Room && Client->CurrentRoom)
	{
		std::lock_guard<std::recursive_mutex> lg(Mutex);
		for (auto& kv : Rooms)
		{
			if (kv.second && kv.second.get() == Client->CurrentRoom)
			{
				Room = kv.second;
				break;
			}
		}
	}

	if (!IsBorderTeamMatchRoom(Room))
	{
		QQS_DebugPrintf10001("[FLOW][20210-BORDER-TEAM] ignored Uin=%u ReqUin=%u RoomID=%u Base=%u Sub=%u SeatNum=%u CreateRoomType=%u ServerMatchType=%u TeamMatchType=%u BodyLen=%zu\n",
			Client->Uin, ReqUin, Room ? Room->ID : 0, Room ? Room->BaseGameMode : 0, Room ? Room->SubGameMode : 0,
			Room ? Room->SeatNum : 0, Room ? Room->CreateRoomType : 0, Room ? Room->ServerMatchType : 0, Room ? Room->TeamMatchType : 0, BodyLen);
		return;
	}
	NormalizeOfficial6BorderTeamRoom(Room);

	std::vector<std::shared_ptr<ClientNode>> Members;
	UINT slotUin[6] = { 0 };
	UINT slotRoomID[6] = { 0 };
	UINT slotBorderRoomID[6] = { 0 };
	UINT slotTaskID[6] = { 0 };
	UINT slotConnID[6] = { 0 };
	int slotReadyState[6] = { -1, -1, -1, -1, -1, -1 };
	int slotLive[6] = { 0 };
	int slotCurrentRoomOK[6] = { 0 };
	int slotSeatID[6] = { -1, -1, -1, -1, -1, -1 };
	int skippedSlotNum = 0;
	int invalidSlotNum = 0;
	UINT liveSourceSeatNum = 0;
	bool hasNotReadyMember = false;
	UINT notReadyUin = 0;
	for (char i = 0; i < 6; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (!RoomClient)
			continue;

		slotUin[i] = RoomClient->Uin;
		slotRoomID[i] = RoomClient->RoomID;
		slotBorderRoomID[i] = RoomClient->BorderRoomID;
		slotTaskID[i] = RoomClient->TaskID;
		slotConnID[i] = RoomClient->ConnID;
		slotReadyState[i] = RoomClient->ReadyState;
		slotLive[i] = (RoomClient->Uin != 0 && GetClient(RoomClient->Uin) == RoomClient) ? 1 : 0;
		slotCurrentRoomOK[i] = (RoomClient->CurrentRoom == Room.get()) ? 1 : 0;
		slotSeatID[i] = RoomClient->SeatID;

		if (!IsValidBorderTeamMemberForStart(Room, RoomClient, i))
		{
			invalidSlotNum++;
			skippedSlotNum++;
			continue;
		}
		liveSourceSeatNum++;
		if (!IsRoomConnOwner(Room, RoomClient) &&
			RoomClient->ReadyState != QQS_ROOM_READY_STATE_READY)
		{
			hasNotReadyMember = true;
			notReadyUin = RoomClient->Uin;
		}
		Members.push_back(RoomClient);
	}
	if (hasNotReadyMember)
	{
		QQS_DebugPrintf10001("[FLOW][20210-BORDER-TEAM-ROOM] reject not ready ReqUin=%u TeamRoomID=%u NotReadyUin=%u Members=%zu LiveSeats=%u InvalidSlots=%d Slots=[0:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,1:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,2:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,3:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,4:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,5:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d]\n",
			ReqUin, Room->ID, notReadyUin, Members.size(), liveSourceSeatNum, invalidSlotNum,
			slotUin[0], slotConnID[0], slotRoomID[0], slotBorderRoomID[0], slotTaskID[0], slotSeatID[0], slotReadyState[0], slotLive[0], slotCurrentRoomOK[0],
			slotUin[1], slotConnID[1], slotRoomID[1], slotBorderRoomID[1], slotTaskID[1], slotSeatID[1], slotReadyState[1], slotLive[1], slotCurrentRoomOK[1],
			slotUin[2], slotConnID[2], slotRoomID[2], slotBorderRoomID[2], slotTaskID[2], slotSeatID[2], slotReadyState[2], slotLive[2], slotCurrentRoomOK[2],
			slotUin[3], slotConnID[3], slotRoomID[3], slotBorderRoomID[3], slotTaskID[3], slotSeatID[3], slotReadyState[3], slotLive[3], slotCurrentRoomOK[3],
			slotUin[4], slotConnID[4], slotRoomID[4], slotBorderRoomID[4], slotTaskID[4], slotSeatID[4], slotReadyState[4], slotLive[4], slotCurrentRoomOK[4],
			slotUin[5], slotConnID[5], slotRoomID[5], slotBorderRoomID[5], slotTaskID[5], slotSeatID[5], slotReadyState[5], slotLive[5], slotCurrentRoomOK[5]);
		ResponseStartGame(Client, "玩家未准备!");
		return;
	}

	if (Members.size() < 2)
	{
		QQS_DebugPrintf10001("[FLOW][20210-BORDER-TEAM-ROOM] reject need at least 2 valid members ReqUin=%u TeamRoomID=%u Members=%zu LiveSeats=%u Skipped=%d InvalidSlots=%d\n",
			ReqUin, Room->ID, Members.size(), liveSourceSeatNum, skippedSlotNum, invalidSlotNum);
		ResponseStartGame(Client, "组队边境至少需要2名有效队员!");
		return;
	}

	for (const auto& RoomClient : Members)
	{
		if (PreflightBorderKartPhysEntry(RoomClient, "team-queue"))
			continue;
		if (RoomClient != Client)
			ResponseStartGame(RoomClient, QQS_BORDER_KART_PHYS_MISSING_REASON);
		ResponseStartGame(Client, QQS_BORDER_KART_PHYS_MISSING_REASON);
		return;
	}

	// 组队边境不是单独开只包含本队的比赛房；官服体验是整队进入公共边境队列，
	// 与单人边境玩家一起匹配。这里保留原组队房 RoomID/CurrentRoom，等
	// BorderMatchBeginTest() 从公共 BorderMatchQueue 出队后再分配临时 BorderRoom。
	auto now = std::chrono::steady_clock::now();
	{
		std::lock_guard<std::recursive_mutex> lg(BorderQueueMutex);
		for (auto& RoomClient : Members)
		{
			RemoveFromBorderMatchQueue_NoLock(RoomClient);
			RoomClient->TaskID = 0xFFFD;
			RoomClient->BorderRoomID = 0;
			ResetNormalRaceWireState(RoomClient);
			RoomClient->GameID = 0;
			RoomClient->CurrentRoom = Room.get();
			RoomClient->RoomID = Room->ID;
			RoomClient->IsReady = FALSE;
			RoomClient->IsFinish = FALSE;
			RoomClient->FinishTime = 0;
			RoomClient->Round = 0;
			RoomClient->PassedCheckPoint = 0;
			BorderMatchQueue.push_front({ RoomClient, now });
		}
	}

	Room->startStatus = 2;
	Room->Callback = NULL;

	for (auto& RoomClient : Members)
	{
		NotifyBorderTeamMatchQueueAck(RoomClient, Room);
	}

	QQS_DebugPrintf10001("[FLOW][20210-BORDER-TEAM-QUEUE] ReqUin=%u ReqTime=%u TeamRoomID=%u Members=%zu LiveSeats=%u Skipped=%d InvalidSlots=%d Queue=BorderMatchQueue SharedWithSingle=1 Slots=[0:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,1:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,2:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,3:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,4:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d,5:%u/C%u/R%u/B%u/T%u/S%d/D%d/L%d/CR%d]\n",
		ReqUin, ReqTime, Room->ID, Members.size(), liveSourceSeatNum, skippedSlotNum, invalidSlotNum,
		slotUin[0], slotConnID[0], slotRoomID[0], slotBorderRoomID[0], slotTaskID[0], slotSeatID[0], slotReadyState[0], slotLive[0], slotCurrentRoomOK[0],
		slotUin[1], slotConnID[1], slotRoomID[1], slotBorderRoomID[1], slotTaskID[1], slotSeatID[1], slotReadyState[1], slotLive[1], slotCurrentRoomOK[1],
		slotUin[2], slotConnID[2], slotRoomID[2], slotBorderRoomID[2], slotTaskID[2], slotSeatID[2], slotReadyState[2], slotLive[2], slotCurrentRoomOK[2],
		slotUin[3], slotConnID[3], slotRoomID[3], slotBorderRoomID[3], slotTaskID[3], slotSeatID[3], slotReadyState[3], slotLive[3], slotCurrentRoomOK[3],
		slotUin[4], slotConnID[4], slotRoomID[4], slotBorderRoomID[4], slotTaskID[4], slotSeatID[4], slotReadyState[4], slotLive[4], slotCurrentRoomOK[4],
		slotUin[5], slotConnID[5], slotRoomID[5], slotBorderRoomID[5], slotTaskID[5], slotSeatID[5], slotReadyState[5], slotLive[5], slotCurrentRoomOK[5]);
}

int GetBorderSize() {
	std::lock_guard<std::recursive_mutex> lg(BorderQueueMutex);
	return BorderMatchQueue.size();
}

int GetRankeSize() {
	std::lock_guard<std::recursive_mutex> lg(RankedQueueMutex);
	return RankedMatchQueue.size();
}
void ResponseStartQuickMatchGame2(std::shared_ptr<ClientNode> Client, USHORT matchQueueType, UINT MapID)
{
	//单人

	std::shared_ptr<RoomNode> BorderRoom = NewBorderRoom();

	if (!BorderRoom)
	{
		return;
	}
	if (!BeginRoomGameLogicSession(BorderRoom, "quick-match-single"))
		return;
	Client->BorderRoomID = BorderRoom->ID;
	Client->CurrentRoom = BorderRoom.get();
	Client->IsBanMap = false;
	if (matchQueueType == 0xa)
	{
		BorderRoom->BaseGameMode = 0x28;
		BorderRoom->SubGameMode = 1;
	}
	else
	{
		BorderRoom->BaseGameMode = BorderRoom->SubGameMode = 1;
	}

	//随机地图
	BorderRoom->matchQueueType = matchQueueType;
	BorderRoom->MapID = GetRandMap(16);//全随地图池
	Client->BaseGameMode = 1;
	Client->SubGameMode = 1;
	Client->real = 0;
	if (matchQueueType == 12)
	{
		Client->BaseGameMode = 17;
	}

	//BorderRoom->MapID = 112;//全随地图池
	//加入房间
	memset(Client->NpcFinishInfo, 0, sizeof(NpcRacerFinishInfo) * NpcNum);
	for (char i = 0; i < 6; i++)
	{
		if (!BorderRoom->Player[i])//空位
		{

			BorderRoom->Player[i] = Client;
			Client->SeatID = i;
			BorderRoom->CurrentPlayerNum++;
			if (MapID != 0)
			{
				BorderRoom->MapID = MapID;
			}
			if (BorderRoom->matchQueueType == 0)
				Client->TaskID = 0xFFFD;
			else if (BorderRoom->matchQueueType == 0xa)
				Client->TaskID = 0xFFFC;
			else if (BorderRoom->matchQueueType == 0x1)//普通立即竞速
				Client->TaskID = 0xFFFB;
			ResetNormalRaceWireState(Client);
			Client->GameID = BorderRoom->RaceGameID;
			Map* pMap = GetMap(BorderRoom->MapID);
			if (pMap)
			{
				Client->MapCompatibilityMode = false;
				Client->EndCheckPoint = pMap->End;
				Client->TotalRound = pMap->Round;
				Client->Loop = pMap->Loop;
			}
			else
			{
				Client->MapCompatibilityMode = true;
				Client->EndCheckPoint = 0;
				Client->TotalRound = 1;
				Client->Loop = false;
			}
			Client->MapID = BorderRoom->MapID;
			Client->FinishTime = 0;
			Client->Round = 0;
			Client->PassedCheckPoint = 0;
			Client->IsFinish = FALSE;
			Client->IsReady = TRUE;
			Client->BorderRoomID = BorderRoom->ID;
			Client->CurrentRoom = BorderRoom.get();
			Client->IsBanMap = false;
			for (char i2 = 1; i2 < 6; i2++)
			{
				Client->NpcFinishInfo[i2].NpcID = i2;//初始化NPCID
			}
			break;
		}
	}
	if (!CaptureRoomRaceStartSnapshot(BorderRoom, "match-result-start"))
	{
		QQS_DebugPrintf10001("[FLOW][MATCH-START-ABORT] BorderRoomID=%u Reason=race-snapshot-invalid\n", BorderRoom->ID);
		return;
	}
	CreateRoomTimer(BorderRoom, 1000, OnMatchResult);

}

void ResponseStartQuickMatchGame2_Rank(std::shared_ptr<ClientNode> Client, USHORT matchQueueType)
{
	//RequestCancelMatch(Client);
	LeaveRoom(Client, 0);
	LeaveBorderRoom(Client, 0);
	LeaveOutdoor(Client);
	ResponseAddToMatchQueue(Client, matchQueueType);
	//单人

	std::shared_ptr<RoomNode> BorderRoom = NewBorderRoom();

	if (!BorderRoom)
	{
		return;
	}
	if (!BeginRoomGameLogicSession(BorderRoom, "ranked-quick-match-single"))
		return;
	Client->BorderRoomID = BorderRoom->ID;
	Client->CurrentRoom = BorderRoom.get();
	Client->IsBanMap = false;
	if (matchQueueType == 0xa)
	{
		BorderRoom->BaseGameMode = 0x28;
		BorderRoom->SubGameMode = 1;
	}
	else
	{
		BorderRoom->BaseGameMode = BorderRoom->SubGameMode = 1;
	}

	//随机地图
	BorderRoom->matchQueueType = matchQueueType;
	BorderRoom->SeatNum = ROOM_SEAT_NORMAL_MAX;
	Client->BaseGameMode = 1;
	Client->SubGameMode = 1;
	Client->MatchBanMapIDs.clear();
	Client->MapCompatibilityMode = false;
	Client->real = 0;
	if (matchQueueType == 12)
	{
		Client->BaseGameMode = 17;
	}

	//BorderRoom->MapID = 112;//全随地图池
	//加入房间
	memset(Client->NpcFinishInfo, 0, sizeof(NpcRacerFinishInfo) * NpcNum);
	for (char i = 0; i < 6; i++)
	{
		if (!BorderRoom->Player[i])//空位
		{

			BorderRoom->Player[i] = Client;
			Client->SeatID = i;
			BorderRoom->CurrentPlayerNum++;
			Client->SeatID = i;
			if (BorderRoom->matchQueueType == 0)
				Client->TaskID = 0xFFFD;
			else if (BorderRoom->matchQueueType == 0xa)
				Client->TaskID = 0xFFFC;
			else if (BorderRoom->matchQueueType == 0x1)//普通立即竞速
				Client->TaskID = 0xFFFB;
			ResetNormalRaceWireState(Client);
			Client->GameID = BorderRoom->RaceGameID;
			Client->MapCompatibilityMode = false;
			Client->MatchBanMapIDs.clear();
			Client->FinishTime = 0;
			Client->Round = 0;
			Client->PassedCheckPoint = 0;
			Client->IsFinish = FALSE;
			Client->ranknumber = 0;
			Client->HighestSpeed = 0;
			Client->CollisionCount = 0;
			Client->N2OGatherCount = 0;
			Client->DriftCount = 0;
			Client->RaceDetail = RaceDetailRecord{};
			Client->LapTime = 0;
			Client->MAXLapTime = 0;
			Client->LastDistanceToEnd = 0;
			Client->LastRaceLapTime = 0;
			Client->IsReady = TRUE;
			Client->BorderRoomID = BorderRoom->ID;
			Client->CurrentRoom = BorderRoom.get();
			Client->IsBanMap = false;
			for (char i2 = 1; i2 < 6; i2++)
			{
				Client->NpcFinishInfo[i2].NpcID = i2;//初始化NPCID
			}
			break;
		}
	}
	if (!InitializeRankedRoomMapState(BorderRoom))
	{
		QQS_DebugPrintf10001(
			"[RANKED-START][ABORT] RoomID=%u Reason=ranked-map-state-invalid\n",
			BorderRoom->ID);
		return;
	}
	if (!CaptureRoomRaceStartSnapshot(BorderRoom, "ranked-quick-match-start"))
	{
		QQS_DebugPrintf10001("[FLOW][MATCH-START-ABORT] BorderRoomID=%u Reason=race-snapshot-invalid\n", BorderRoom->ID);
		return;
	}
	CreateRoomTimer(BorderRoom, 1000, OnMatchResult);

}

void ResponseStartRankedGame(std::shared_ptr<ClientNode> Client, USHORT matchQueueType, UINT MapID)
{
	RequestCancelMatch(Client);
	LeaveRoom(Client, 0);
	LeaveBorderRoom(Client, 0);
	LeaveOutdoor(Client);
	ResponseAddToMatchQueue(Client, matchQueueType);

	Client->TaskID = 0xFFFC;
	std::lock_guard<std::recursive_mutex> lg(RankedQueueMutex);
	//RankedMatchQueue.push_front(Client);
	auto now = std::chrono::steady_clock::now();
	RankedMatchQueue.push_front({ Client, now });
}

void ResponseStartBorderGame(std::shared_ptr<ClientNode> Client, USHORT matchQueueType, UINT MapID)
{
	if (!PreflightBorderKartPhysEntry(Client, "single-queue"))
	{
		ResponseStartGame(Client, QQS_BORDER_KART_PHYS_MISSING_REASON);
		return;
	}

	RequestCancelMatch(Client);
	LeaveRoom(Client, 0);
	LeaveBorderRoom(Client, 0);
	LeaveOutdoor(Client);
	ResponseAddToMatchQueue(Client, matchQueueType);

	Client->TaskID = 0xFFFD;
	size_t queueSize = 0;
	{
		std::lock_guard<std::recursive_mutex> lg(BorderQueueMutex);
		//BorderMatchQueue.push_front(Client);

		auto now = std::chrono::steady_clock::now();
		BorderMatchQueue.push_front({ Client, now });
		queueSize = BorderMatchQueue.size();
	}

	QQS_DebugPrintf10001(
		"[FLOW][BORDER-QUEUE-ENQUEUE] Uin=%u Queue=%zu Required=%d TaskID=%u -> immediate-match-check\n",
		Client->Uin, queueSize, GetBorderMatchRequiredPlayerNum(), Client->TaskID);
	// Do not make a successful match depend on the five-second Run4 worker.
	// The periodic worker remains as a fallback for timeouts and stale entries.
	BorderMatchBeginTest();

}

void ResponseStartQuickMatchGame(std::shared_ptr<ClientNode> Client, USHORT matchQueueType, UINT MapID)
{
	const std::uint64_t pendingAuroraWorkID = Client ? Client->AuroraPendingWorkID : 0;
	const UINT pendingRankedTimeRaceMapID = matchQueueType == 41 ? MapID : 0;
	const auto* auroraWork = (matchQueueType == 8 && pendingAuroraWorkID != 0)
		? QQSpeedAuroraOfficial::FindWork(pendingAuroraWorkID)
		: nullptr;
	if (pendingAuroraWorkID != 0 && !auroraWork)
	{
		Client->AuroraPendingWorkID = 0;
		QQS_DebugPrintf10001("[AURORA][MATCH-ABORT] Uin=%u WorkID=%llu Error=unknown-work\n",
			Client ? Client->Uin : 0,
			static_cast<unsigned long long>(pendingAuroraWorkID));
		return;
	}
	if (matchQueueType == 0 && !PreflightBorderKartPhysEntry(Client, "single-immediate"))
	{
		ResponseStartGame(Client, QQS_BORDER_KART_PHYS_MISSING_REASON);
		return;
	}
	if (matchQueueType == 41 && pendingRankedTimeRaceMapID == 0)
		return;

	RequestCancelMatch(Client);
	LeaveRoom(Client, 0);
	LeaveBorderRoom(Client, 0);
	LeaveOutdoor(Client);
	// Leave helpers must not erase the one-shot handoff before 20079 is built.
	Client->AuroraPendingWorkID = pendingAuroraWorkID;
	Client->RankedTimeRacePendingMapID = pendingRankedTimeRaceMapID;
	ResponseAddToMatchQueue(Client, matchQueueType);
	//单人

	std::shared_ptr<RoomNode> BorderRoom = NewBorderRoom();

	if (!BorderRoom)
	{
		Client->AuroraPendingWorkID = 0;
		Client->RankedTimeRacePendingMapID = 0;
		return;
	}
	if (!BeginRoomGameLogicSession(BorderRoom, "quick-match"))
	{
		Client->AuroraPendingWorkID = 0;
		Client->RankedTimeRacePendingMapID = 0;
		return;
	}
	Client->BorderRoomID = BorderRoom->ID;
	Client->CurrentRoom = BorderRoom.get();
	Client->IsBanMap = false;
	if (auroraWork)
	{
		BorderRoom->BaseGameMode = 42;
		BorderRoom->SubGameMode = 29;
	}
	else if (matchQueueType == 0xa)
	{
		BorderRoom->BaseGameMode = 0x28;
		BorderRoom->SubGameMode = 1;
	}
	else
	{
		BorderRoom->BaseGameMode = BorderRoom->SubGameMode = 1;
	}

	//随机地图
	BorderRoom->matchQueueType = matchQueueType;
	BorderRoom->RankedTimeRace = matchQueueType == 41;
	BorderRoom->MapID = auroraWork ? auroraWork->mapId : GetRandMap(16);//全随地图池
	Client->BaseGameMode = auroraWork ? 42 : 1;
	Client->SubGameMode = auroraWork ? 29 : 1;
	if (matchQueueType == 12)
	{
		Client->BaseGameMode = 17;
	}

	//BorderRoom->MapID = 112;//全随地图池
	//加入房间
	memset(Client->NpcFinishInfo, 0, sizeof(NpcRacerFinishInfo) * NpcNum);
	for (char i = 0; i < 6; i++)
	{
		if (!BorderRoom->Player[i])//空位
		{

			BorderRoom->Player[i] = Client;
			Client->SeatID = i;
			BorderRoom->CurrentPlayerNum++;
			if (MapID != 0)
			{
				BorderRoom->MapID = MapID;
			}
			if (BorderRoom->matchQueueType == 0)
				Client->TaskID = 0xFFFD;
			else if (BorderRoom->matchQueueType == 0xa)
				Client->TaskID = 0xFFFC;
			else if (BorderRoom->matchQueueType == 0x1)//普通立即竞速
				Client->TaskID = 0xFFFB;
			else if (auroraWork && BorderRoom->matchQueueType == 8)
				Client->TaskID = 0xFFFB;
			else if (BorderRoom->RankedTimeRace)
				Client->TaskID = 0xFFFB;
			ResetNormalRaceWireState(Client);
			Client->GameID = BorderRoom->RaceGameID;
			Map* pMap = GetMap(BorderRoom->MapID);
			if (pMap)
			{
				Client->MapCompatibilityMode = false;
				Client->EndCheckPoint = pMap->End;
				Client->TotalRound = pMap->Round;
				Client->Loop = pMap->Loop;
			}
			else
			{
				Client->MapCompatibilityMode = true;
				Client->EndCheckPoint = 0;
				Client->TotalRound = 1;
				Client->Loop = false;
			}
			Client->MapID = BorderRoom->MapID;
			Client->FinishTime = 0;
			Client->Round = 0;
			Client->PassedCheckPoint = 0;
			Client->IsFinish = FALSE;
			Client->IsReady = TRUE;
			Client->BorderRoomID = BorderRoom->ID;
			Client->CurrentRoom = BorderRoom.get();
			Client->IsBanMap = false;
			for (char i2 = 1; i2 < 6; i2++)
			{
				Client->NpcFinishInfo[i2].NpcID = i2;//初始化NPCID
			}
			break;
		}
	}
	if (auroraWork)
	{
		ApplyAuroraWorkToRoom(BorderRoom, *auroraWork);
	}
	Client->AuroraPendingWorkID = 0;
	Client->RankedTimeRacePendingMapID = 0;
	if (!CaptureRoomRaceStartSnapshot(BorderRoom, "match-result-start"))
	{
		QQS_DebugPrintf10001("[FLOW][MATCH-START-ABORT] BorderRoomID=%u Reason=race-snapshot-invalid\n", BorderRoom->ID);
		return;
	}
	CreateRoomTimer(BorderRoom, 1000, OnMatchResult);

}

void ResponseStartCheShenGame(std::shared_ptr<ClientNode> Client, USHORT matchQueueType, UINT MapID)
{
	RequestCancelMatch(Client);
	LeaveRoom(Client, 0);
	LeaveBorderRoom(Client, 0);
	LeaveOutdoor(Client);
	ResponseAddToMatchQueue(Client, matchQueueType);
	//单人

	std::shared_ptr<RoomNode> BorderRoom = NewBorderRoom();

	if (!BorderRoom)
	{
		return;
	}
	Client->BorderRoomID = BorderRoom->ID;
	Client->CurrentRoom = BorderRoom.get();
	Client->IsBanMap = false;
	if (matchQueueType == 0xa)
	{
		BorderRoom->BaseGameMode = 0x28;
		BorderRoom->SubGameMode = 1;
	}
	else
	{
		BorderRoom->BaseGameMode = BorderRoom->SubGameMode = 1;
	}

	//随机地图
	BorderRoom->matchQueueType = matchQueueType;
	BorderRoom->MapID = MapID;
	Client->BaseGameMode = 1;
	Client->SubGameMode = 1;
	if (matchQueueType == 12)
	{
		Client->BaseGameMode = 17;
	}

	//BorderRoom->MapID = 112;//全随地图池
	//加入房间
	memset(Client->NpcFinishInfo, 0, sizeof(NpcRacerFinishInfo) * NpcNum);
	for (char i = 0; i < 6; i++)
	{
		if (!BorderRoom->Player[i])//空位
		{

			BorderRoom->Player[i] = Client;
			BorderRoom->CurrentPlayerNum++;
			if (MapID != 0)
			{
				BorderRoom->MapID = MapID;
			}
			if (BorderRoom->matchQueueType == 0)
				Client->TaskID = 0xFFFD;
			else if (BorderRoom->matchQueueType == 0xa)
				Client->TaskID = 0xFFFC;
			else if (BorderRoom->matchQueueType == 0x1)//普通立即竞速
				Client->TaskID = 0xFFFB;
			Client->GameID = 0x000001D8;
			Map* pMap = GetMap(BorderRoom->MapID);
			if (pMap)
			{
				Client->MapCompatibilityMode = false;
				Client->EndCheckPoint = pMap->End;
				Client->TotalRound = pMap->Round;
				Client->Loop = pMap->Loop;
			}
			else
			{
				Client->MapCompatibilityMode = true;
				Client->EndCheckPoint = 0;
				Client->TotalRound = 1;
				Client->Loop = false;
			}
			Client->MapID = BorderRoom->MapID;
			Client->FinishTime = 0;
			Client->Round = 0;
			Client->PassedCheckPoint = 0;
			Client->IsFinish = FALSE;
			Client->IsReady = TRUE;
			Client->BorderRoomID = BorderRoom->ID;
			Client->CurrentRoom = BorderRoom.get();
			Client->IsBanMap = false;
			for (char i2 = 1; i2 < 6; i2++)
			{
				Client->NpcFinishInfo[i2].NpcID = i2;//初始化NPCID
			}
			break;
		}
	}
	if (!CaptureRoomRaceStartSnapshot(BorderRoom, "match-result-start"))
	{
		QQS_DebugPrintf10001("[FLOW][MATCH-START-ABORT] BorderRoomID=%u Reason=race-snapshot-invalid\n", BorderRoom->ID);
		return;
	}
	CreateRoomTimer(BorderRoom, 1000, OnMatchResult);

}



namespace
{
	constexpr UINT kPurplePracticeTaskID = 0x06A5;
	constexpr USHORT kPurplePracticeLogicType = 33;
	constexpr DWORD kPurplePracticeKartInfoDelayMs = 500;
	constexpr DWORD kPurplePracticeCountdownDelayMs = 6000;
	constexpr DWORD kPurplePracticeCountdownTimeMs = 4000;

	struct PurplePracticeReadyContext
	{
		std::shared_ptr<ClientNode> Client;
		USHORT GameID = 0;
		short WirePlayerID = 0;
		DWORD ReadyTick = 0;
	};

	bool IsCurrentPurplePracticeSession(const PurplePracticeReadyContext& context)
	{
		const std::shared_ptr<ClientNode>& Client = context.Client;
		return Client &&
			Client->IsLogin &&
			!Client->deleteflag &&
			Client->TaskID == kPurplePracticeTaskID &&
			Client->GameID == context.GameID &&
			GetClientWirePlayerHeaderID(Client) == context.WirePlayerID &&
			Client->IsReady;
	}

	void WaitUntilPurplePracticeDeadline(DWORD readyTick, DWORD delayMs)
	{
		const DWORD elapsed = GetTickCount() - readyTick;
		if (elapsed < delayMs)
			Sleep(delayMs - elapsed);
	}

	DWORD WINAPI PurplePracticeReadyWorker(LPVOID parameter)
	{
		std::unique_ptr<PurplePracticeReadyContext> context(
			static_cast<PurplePracticeReadyContext*>(parameter));

		WaitUntilPurplePracticeDeadline(
			context->ReadyTick, kPurplePracticeKartInfoDelayMs);
		if (!IsCurrentPurplePracticeSession(*context))
			return 0;

		if (!NotifyPurplePracticeKartPhysParam(context->Client))
			return 0;
		if (!IsCurrentPurplePracticeSession(*context))
			return 0;
		if (!NotifyOtherKartStoneInfo(
			context->Client, { context->Client }, context->WirePlayerID))
		{
			return 0;
		}

		WaitUntilPurplePracticeDeadline(
			context->ReadyTick, kPurplePracticeCountdownDelayMs);
		if (!IsCurrentPurplePracticeSession(*context))
			return 0;

		BYTE countdown[sizeof(DWORD)] = {};
		BYTE* p = countdown;
		Write32(p, kPurplePracticeCountdownTimeMs);
		SendToClientWithWireDstIDAndMsgSeq(
			context->Client,
			531,
			countdown,
			static_cast<DWORD>(sizeof(countdown)),
			static_cast<short>(context->GameID),
			FE_GAMELOGIC,
			context->WirePlayerID,
			1,
			Notify);
		return 0;
	}
}

void StartPurplePractice(std::shared_ptr<ClientNode> Client, UINT MapID)
{
	if (!Client)
		return;

	std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
	if (!Room)
	{
		MapID = GetRandMap(0);
	}
	else
	{
		if (Room->MapID < 100)
			MapID = GetRandMap(0);
		else
			MapID = Room->MapID;
	}

	Map* pMap = GetMap(MapID);
	if (!pMap)
		return;

	const USHORT gameID = AllocateGameLogicSessionID();
	if (gameID == 0)
		return;

	ResetNormalRaceWireState(Client);
	Client->TaskID = kPurplePracticeTaskID;
	Client->GameID = gameID;
	Client->IsReady = FALSE;
	Client->IsFinish = FALSE;
	Client->MapCompatibilityMode = false;
	Client->EndCheckPoint = pMap->End;
	Client->TotalRound = pMap->Round;
	Client->Loop = pMap->Loop;
	Client->MapID = MapID;
	Client->FinishTime = 0;
	Client->Round = 0;
	Client->PassedCheckPoint = 0;

	std::array<BYTE, 110> payload{};
	BYTE* p = payload.data();
	Write32(p, 60000);                         // NormalLoadTime
	Write32(p, kPurplePracticeTaskID);          // TaskID
	Write32(p, MapID);                          // MapID
	memset(p, 0, 32);                           // MapHash
	p += 32;
	Write32(p, gameID);                         // GameID
	Write32(p, static_cast<DWORD>(time(nullptr))); // Seed
	Write16(p, static_cast<USHORT>(Client->TotalRound));
	Write32(p, 0);                              // TimeLimit
	Write32(p, 0);                              // MaxNPCDistance
	Write16(p, 0);                              // InitPropNum
	Write16(p, 0);                              // NpcNum
	Write32(p, 0);                              // PlayerItemNum
	Write8(p, 0);                               // SyncCarNum
	Write8(p, 0);                               // PlayerNum
	Write8(p, 1);                               // FeedBackEnabled
	Write16(p, 0);                              // PropID
	Write32(p, 0);                              // PropIndex
	Write16(p, 0);                              // PropNum
	Write16(p, kPurplePracticeLogicType);        // LogicType
	Write8(p, 0);                               // MapCheckpointFileIndex
	Write32(p, 0);                              // Duration
	Write32(p, 0);                              // BeginCDTime
	Write32(p, 0);                              // PropInterval
	Write8(p, 0);                               // NoNeedTask
	Write32(p, 0x01D10000);                     // GameBeginSwitchFlag
	memset(p, 0, 9);
	p += 9;
	if (p != payload.data() + payload.size())
		return;

	SendToClientWithWireDstIDAndMsgSeq(
		Client,
		530,
		payload.data(),
		static_cast<DWORD>(payload.size()),
		static_cast<short>(gameID),
		FE_GAMELOGIC,
		GetClientWirePlayerHeaderID(Client),
		0,
		Notify);
}

void RequestPurplePracticePrepareReady(
	std::shared_ptr<ClientNode> Client,
	BYTE* Body,
	size_t BodyLen)
{
	if (!Client ||
		Client->TaskID != kPurplePracticeTaskID ||
		Client->GameID == 0 ||
		Client->IsReady ||
		BodyLen < 48)
	{
		return;
	}

	const DWORD readyTick = GetTickCount();
	RequestPrepareReady(Client, Body, BodyLen);
	if (!Client->IsReady)
		return;

	auto context = std::make_unique<PurplePracticeReadyContext>();
	context->Client = Client;
	context->GameID = Client->GameID;
	context->WirePlayerID = GetClientWirePlayerHeaderID(Client);
	context->ReadyTick = readyTick;

	HANDLE worker = CreateThread(
		nullptr,
		0,
		PurplePracticeReadyWorker,
		context.get(),
		0,
		nullptr);
	if (!worker)
		return;

	context.release();
	CloseHandle(worker);
}

void StartSingleGame(std::shared_ptr<ClientNode> Client, BYTE* Body)
{
	int TaskID = Read32(Body);
	UINT MapID = Read32(Body);
	if (TaskID == kPurplePracticeTaskID)
		return;
	const bool rankedMapTraining = TaskID == 4311 || TaskID == 4312;
	if (rankedMapTraining)
	{
		const RankConfig& rankCfg = GetRankConfig();
		const auto map = std::find_if(
			rankCfg.SeasonMapChallenge.Maps.begin(), rankCfg.SeasonMapChallenge.Maps.end(),
			[MapID](const RankSeasonMapChallengeMapInfo& row) { return row.MapId == MapID; });
		if (!rankCfg.LoadedFromYaml || !rankCfg.Enabled || map == rankCfg.SeasonMapChallenge.Maps.end())
		{
			ResponseStartGame(Client, "排位训练地图无效");
			return;
		}
	}
	Client->TaskID = TaskID;
	RequestCancelMatch(Client);
	LeaveRoom(Client, 0);
	LeaveBorderRoom(Client, 0);
	LeaveOutdoor(Client);
	//printf("TaskID:%d\n", Client->TaskID);
	std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
	if (rankedMapTraining)
	{
		// 4311/4312的请求MapID来自Msg25000赛季地图配置，离房后仍须原样保留。
	}
	else if (!Room)
	{
		MapID = GetRandMap(0);
	}
	else
	{
		if (Room->MapID < 100)
		{
			MapID = GetRandMap(0);
		}
		else
		{
			MapID = Room->MapID;
		}
	}

	if (TaskID == 0x49) //暴风随机
	{
		//int i = rand() % 5 ;
		MapID = baofengmap[Client->OptIndex];
		if (Client->OptIndex <= 0)
			Client->OptIndex = 4;
		else
			Client->OptIndex -= 1;


	}
	Map* pMap = GetMap(MapID);
	ResetNormalRaceWireState(Client);
	Client->GameID = 0;
	Client->IsReady = TRUE;
	if (pMap)
	{
		Client->MapCompatibilityMode = false;
		Client->EndCheckPoint = pMap->End;
		Client->TotalRound = pMap->Round;
		Client->Loop = pMap->Loop;
	}
	else
	{
		Client->MapCompatibilityMode = true;
		Client->EndCheckPoint = 0;
		Client->TotalRound = 1;
		Client->Loop = false;
	}
	Client->MapID = MapID;
	Client->FinishTime = 0;
	Client->Round = 0;
	Client->PassedCheckPoint = 0;
	if (TaskID == 0x49)
	{
		BYTE* pp = baofengtiaozhan;
		pp += 8;
		//rand()%(b-a+1) +a 
		Write32(pp, MapID);
		pp += 40;
		Write16(pp, Client->TotalRound);
		SendToClient(Client, 530, baofengtiaozhan, 1928, Client->GameID, FE_GAMELOGIC, 1, Notify);
	}
	else if (TaskID == 0x6e)//树苗
	{
		BYTE* pp = shumiaoyunshu;
		pp += 8;
		Write32(pp, MapID);
		pp += 40;
		Write16(pp, Client->TotalRound);
		SendToClient(Client, 530, shumiaoyunshu, 98, Client->GameID, FE_GAMELOGIC, 1, Notify);
	}
	/*else if (TaskID == 0x48)//车王考验
	{
	BYTE* pp = chewangkaoyan;
	pp += 8;
	Write32(pp, MapID);
	pp += 40;
	Write16(pp, Client->TotalRound);
	Write32(pp, 0x2FFFD);//timelimit
	SendToClient(Client, 530, chewangkaoyan, 98, Client->GameID, FE_GAMELOGIC, 1, Notify);

	}*/


	//NotifyKartPhysParam(Client, NULL);
	if (mode28346 == 2) {
		NotifyKartPhysParam(Client, Room ? Room->Player : std::vector<std::shared_ptr<ClientNode>>{ Client });
	}
	else {
		NotifyEncryptedMsg(Client);
	}

	SendToClient(Client, 531, (BYTE*)"\x00\x00", 2, Client->GameID, FE_GAMELOGIC, 1, Notify);

}

void WINAPI OnStart(std::shared_ptr<RoomNode> Room)
{

}


void WINAPI OnCustomEnterLoading(std::shared_ptr<RoomNode> Room)
{
	if (!Room)
		return;
	if (!IsCustomRoomFlow(Room))
		return;
	if (Room->CustomWaitingRaceBegin || Room->CustomRaceBeginSent ||
		Room->CustomLastSentMsgID == 1718 || Room->CustomLastSentMsgID == 509 ||
		Room->CustomLastSentMsgID == 551 || Room->CustomLastSentMsgID == 28346)
	{
		QQS_DebugPrintf10001("[FLOW][CUSTOM-LOADING-SKIP] RoomID=%u already loading/racing LastS2C=%u ActiveMask=0x%04X Waiting=%d RaceBeginSent=%d\n",
			Room->ID, Room->CustomLastSentMsgID, (unsigned)Room->RaceActiveMask,
			Room->CustomWaitingRaceBegin ? 1 : 0, Room->CustomRaceBeginSent ? 1 : 0);
		return;
	}
	if (Room->startStatus != 2 || !HasCustomRaceStartEligibleClient(Room))
	{
		AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-invalid");
		return;
	}

	NormalizeCustomSchemeForOfficial(Room, "custom-enter-loading");
	if (!EnsureCustomRaceStartSnapshot(Room, "custom-enter-loading", false))
	{
		AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-no-snapshot");
		QQS_DebugPrintf10001("[FLOW][CUSTOM-OFFICIAL-LOADING-ABORT] RoomID=%u no frozen snapshot before 1718/551/509/511\n", Room->ID);
		return;
	}
	int seatLimit = GetRoomSeatLimit(Room);
	RoomSeatMask activeMask = Room->CustomRaceSnapshotMask ? Room->CustomRaceSnapshotMask : (Room->RaceActiveMask ? Room->RaceActiveMask : BuildCurrentRoomPlayerMask(Room));
	if (Room->RaceActiveMask == 0 && activeMask != 0)
		QQS_DebugPrintf10001("[FLOW][CUSTOM-ACTIVE-MASK-RECOVER] RoomID=%u Reason=custom-enter-loading ActiveMask=0x%04X\n",
			Room->ID, (unsigned)activeMask);
	Room->RaceActiveMask = activeMask;
	Room->RaceStartPlayerNum = Room->CustomRaceSnapshotPlayerNum ? Room->CustomRaceSnapshotPlayerNum : CountRoomSeatMaskBits(activeMask, seatLimit);
	Room->CustomLoadReadyMask = 0;

	std::vector<std::shared_ptr<ClientNode>> activePlayers;
	for (char pi = 0; pi < seatLimit; pi++)
	{
		RoomSeatMask bit = GetRoomSeatBit(pi);
		if ((activeMask & bit) == 0)
			continue;
		std::shared_ptr<ClientNode> RoomClient = (pi < (int)Room->Player.size()) ? Room->Player[pi] : nullptr;
		if (!IsCustomRaceStartEligibleClient(Room, RoomClient, pi))
		{
			if (RoomClient)
				QQS_DebugPrintf10001("[FLOW][CUSTOM-LOADING-SKIP-RETURNED] RoomID=%u Seat=%d Uin=%u GameID=%u isRace=%u LastReturn=%u\n",
					Room->ID, pi, RoomClient->Uin, RoomClient->GameID, RoomClient->isRace, RoomClient->LastRoomReturnTick);
			continue;
		}
		if (RoomClient)
			activePlayers.push_back(RoomClient);
	}
	if (activePlayers.empty())
	{
		AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-no-active");
		QQS_DebugPrintf10001("[FLOW][CUSTOM-OFFICIAL-LOADING-ABORT] RoomID=%u ActiveMask=0x%04X no active players\n", Room->ID, (unsigned)activeMask);
		return;
	}
	if (Room->CustomRaceSnapshotValid && activePlayers.size() != Room->CustomRaceStartSnapshot.size())
	{
		QQS_DebugPrintf10001("[FLOW][CUSTOM-OFFICIAL-LOADING-ABORT] RoomID=%u SnapshotNum=%u ActivePlayers=%zu ActiveMask=0x%04X Reason=snapshot-live-mismatch\n",
			Room->ID, Room->CustomRaceSnapshotPlayerNum, activePlayers.size(), (unsigned)activeMask);
		AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-snapshot-live-mismatch");
		return;
	}
	bool custom509TrackUsed[20] = { false };
	bool custom509TrackOK = true;
	RoomSeatMask custom509TrackBadMask = 0;
	for (auto& RoomClient : activePlayers)
	{
		if (!RoomClient)
			continue;
		CustomRaceStartSnapshotEntry snapshotEntry;
		bool snapshotFound = FindCustomRaceStartSnapshotEntry(Room, RoomClient->Uin, &snapshotEntry);
		bool invalidTrack = !snapshotFound || snapshotEntry.TrackSlot >= 20;
		bool duplicateTrack = !invalidTrack && custom509TrackUsed[snapshotEntry.TrackSlot];
		if (invalidTrack || duplicateTrack)
		{
			custom509TrackOK = false;
			if (RoomClient->SeatID >= 0 && RoomClient->SeatID < GetRoomSeatLimit(Room))
				custom509TrackBadMask = (RoomSeatMask)(custom509TrackBadMask | GetRoomSeatBit(RoomClient->SeatID));
			QQS_DebugPrintf10001("[FLOW][CUSTOM-509-TRACK-ABORT] RoomID=%u Uin=%u Seat=%d SnapshotFound=%d TrackSlot=%u Duplicate=%d ActiveMask=0x%04X\n",
				Room->ID, RoomClient->Uin, RoomClient->SeatID,
				snapshotFound ? 1 : 0, snapshotFound ? (unsigned)snapshotEntry.TrackSlot : 0xFF,
				duplicateTrack ? 1 : 0, (unsigned)activeMask);
			continue;
		}
		custom509TrackUsed[snapshotEntry.TrackSlot] = true;
	}
	if (!custom509TrackOK)
	{
		QQS_DebugPrintf10001("[FLOW][CUSTOM-OFFICIAL-LOADING-ABORT] RoomID=%u ActiveMask=0x%04X BadTrackMask=0x%04X Reason=509-track-snapshot-invalid\n",
			Room->ID, (unsigned)activeMask, (unsigned)custom509TrackBadMask);
		AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-509-track-invalid");
		return;
	}

	UINT currentStageMapID = GetCustomStageMapID(Room, Room->CustomCurrentStageIndex, Room->MapID);
	if (currentStageMapID != 0)
		Room->MapID = currentStageMapID;
	Map* pCurrentStageMap = GetMap(Room->MapID);
	USHORT currentPhysicalRound = (pCurrentStageMap && pCurrentStageMap->Round > 0) ? (USHORT)pCurrentStageMap->Round : 1;
	USHORT currentEffectiveRound = GetCustomOfficialTotalRound(Room, Room->MapID, currentPhysicalRound);

	QQS_DebugPrintf10001("[FLOW][CUSTOM-OFFICIAL-LOADING] RoomID=%u MapID=%u SpeFlag=%u SchemeLen=%zu PlayerNum=%u ActiveMask=0x%04X EffectiveRound=%u PhysicalRound=%u -> 1718/509/551 then client 120 -> 511\n",
		Room->ID, Room->MapID, Room->SpeFlag, Room->CustomSchemeWire.size(), (unsigned)activePlayers.size(), (unsigned)activeMask,
		currentEffectiveRound, currentPhysicalRound);

	if (Room->startStatus != 2 || !HasCustomRaceStartEligibleClient(Room))
	{
		AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-before-dispatch");
		return;
	}

	Room->CustomWaitingPrepareReadyBeforeLoad = false;
	Room->CustomPreloadReadySatisfied = false;
	Room->CustomWaitingRaceBegin = false;
	Room->CustomRaceBeginSent = false;
	Room->CustomLoadingDispatching = true;
	if (Room->CustomLoadingStartTick == 0) Room->CustomLoadingStartTick = GetTickCount();
	Room->CustomLastSentMsgID = 0;
	if (Room->RaceGameID == 0)
	{
		AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-missing-game-session");
		return;
	}

	for (auto& RoomClient : activePlayers) {
		if (!RoomClient) continue;
		if (!IsCustomRaceStartEligibleClient(Room, RoomClient, RoomClient->SeatID)) continue;
		RoomClient->CurrentRoom = Room.get();
		RoomClient->BorderRoomID = 0;
		RoomClient->RoomID = (USHORT)Room->ID;
		RoomClient->GameID = Room->RaceGameID;
		RoomClient->IsReady = FALSE;
		RoomClient->IsFinish = FALSE;
		RoomClient->FinishTime = 0;
		RoomClient->Round = 0;
		RoomClient->PassedCheckPoint = 0;
		RoomClient->CustomLastDistanceZeroTick = 0;
		RoomClient->MapID = Room->MapID;
		if (pCurrentStageMap)
		{
			RoomClient->MapCompatibilityMode = false;
			RoomClient->EndCheckPoint = pCurrentStageMap->End;
			RoomClient->TotalRound = currentEffectiveRound;
			RoomClient->Loop = pCurrentStageMap->Loop;
		}
		else
		{
			RoomClient->MapCompatibilityMode = true;
			RoomClient->EndCheckPoint = 0;
			RoomClient->TotalRound = currentEffectiveRound;
			RoomClient->Loop = false;
		}
	}
	EnsureCustomRaceStartSnapshot(Room, "custom-enter-loading-refresh-runtime", false);
	if (!ValidateCustomRaceKartPhysSnapshot(Room, "custom-enter-loading-preflight"))
	{
		QQS_DebugPrintf10001("[FLOW][CUSTOM-OFFICIAL-LOADING-ABORT] RoomID=%u SnapshotNum=%u PhysReady=0x%04X PhysMissing=0x%04X Reason=551-preflight-missing\n",
			Room->ID, Room->CustomRaceSnapshotPlayerNum,
			(unsigned)Room->CustomRaceKartPhysReadyMask,
			(unsigned)Room->CustomRaceKartPhysMissingMask);
		AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-551-preflight-missing");
		return;
	}
	for (auto& RoomClient : activePlayers) {
		if (!RoomClient) continue;
		if (!IsCustomRaceStartEligibleClient(Room, RoomClient, RoomClient->SeatID)) continue;
		NotifyKartStoneForKartAndSkin(RoomClient);
	}
	QQS_DebugPrintf10001("[FLOW][CUSTOM-BATCH-906] sent kart stone config RoomID=%u PlayerNum=%u ActiveMask=0x%04X\n",
		Room->ID, (unsigned)activePlayers.size(), (unsigned)activeMask);

	for (auto& RoomClient : activePlayers) {
		if (!RoomClient) continue;
		if (!IsCustomRaceStartEligibleClient(Room, RoomClient, RoomClient->SeatID)) continue;
		NotifyMuchPlayerInfo(RoomClient);
		Room->CustomLastSentMsgID = 1718;
	}
	QQS_DebugPrintf10001("[FLOW][CUSTOM-BATCH-1718] sent all RoomID=%u PlayerNum=%u ActiveMask=0x%04X\n", Room->ID, (unsigned)activePlayers.size(), (unsigned)activeMask);

	for (auto& RoomClient : activePlayers) {
		if (!RoomClient) continue;
		if (!IsCustomRaceStartEligibleClient(Room, RoomClient, RoomClient->SeatID)) continue;
		if (!NotifyGameBegin(RoomClient, Room->Player))
		{
			QQS_DebugPrintf10001("[FLOW][CUSTOM-OFFICIAL-LOADING-ABORT] RoomID=%u Uin=%u Reason=509-send-failed\n",
				Room->ID, RoomClient->Uin);
			AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-509-send-failed");
			return;
		}
		Room->CustomLastSentMsgID = 509;
	}
	QQS_DebugPrintf10001("[FLOW][CUSTOM-BATCH-509] sent all after 1718 RoomID=%u PlayerNum=%u ActiveMask=0x%04X\n", Room->ID, (unsigned)activePlayers.size(), (unsigned)activeMask);

	for (auto& RoomClient : activePlayers) {
		if (!RoomClient) continue;
		if (!IsCustomRaceStartEligibleClient(Room, RoomClient, RoomClient->SeatID)) continue;
		if (!NotifyKartPhysParam(RoomClient, Room->Player))
		{
			QQS_DebugPrintf10001("[FLOW][CUSTOM-OFFICIAL-LOADING-ABORT] RoomID=%u Uin=%u Reason=551-send-failed\n",
				Room->ID, RoomClient->Uin);
			AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-551-send-failed");
			return;
		}
	}
	QQS_DebugPrintf10001("[FLOW][CUSTOM-BATCH-KARTPHYS] dispatched all after 509 RoomID=%u PlayerNum=%u ActiveMask=0x%04X\n", Room->ID, (unsigned)activePlayers.size(), (unsigned)activeMask);

	if (Room->startStatus != 2 || !HasCustomRaceStartEligibleClient(Room))
	{
		AbortCustomRaceStartAfterReturn(Room, "custom-enter-loading-after-dispatch");
		return;
	}

	Room->CustomLoadingDispatching = false;
	Room->CustomWaitingRaceBegin = true;
	Room->CustomRaceBeginSent = false;
	QQS_DebugPrintf10001("[FLOW][CUSTOM-ON-120-THEN-511] RoomID=%u PlayerNum=%u ActiveMask=0x%04X LastS2C=%u -> client 120 triggers 511\n",
		Room->ID, (unsigned)activePlayers.size(), (unsigned)activeMask, Room->CustomLastSentMsgID);
	if (Room->Timer)
		CancelRoomTimer(Room, "custom enter loading complete");
}

void RequestStartGame(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (BodyLen < 8)
		return;

	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);
	UCHAR Unknown08 = 0;
	if (BodyLen >= 9)
		Unknown08 = Read8(Body);



	std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
	if (!Room)
	{
		return;
	}
	if (IsCustomRoomFlow(Room))
		NormalizeCustomSchemeForOfficial(Room, "110-start-game");
	int roomSeatLimit = GetRoomSeatLimit(Room);
	int activeRoomPlayers = CountRoomPlayers(Room);
	QQS_DebugPrintf10001("[FLOW][110] start Uin=%u ReqUin=%u Time=%u Unknown08=%u RoomID=%u Base=%u Sub=%u RoomMap=%u SpeFlag=%u HasCustom=%d SchemeID=%u CustomMapNum=%u CustomMap=[%u,%u,%u] CustomRound=%u SeatLimit=%d ActivePlayerNum=%d BodyLen=%zu\n",
		Client->Uin, Uin, Time, Unknown08, Client->RoomID, Room->BaseGameMode, Room->SubGameMode, Room->MapID,
		Room->SpeFlag, Room->HasCustomScheme ? 1 : 0, Room->CustomSchemeID, Room->CustomMapNum,
		Room->CustomMapIDList[0], Room->CustomMapIDList[1], Room->CustomMapIDList[2],
		Room->CustomRoundNum, roomSeatLimit, activeRoomPlayers, BodyLen);

	for (char i = 0; i < roomSeatLimit; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient && !IsRoomConnOwner(Room, RoomClient))
		{
			if (RoomClient->ReadyState != QQS_ROOM_READY_STATE_READY)
			{
				ResponseStartGame(Client, "玩家未准备!");
				return;
			}
		}
	}
	if (Room->Callback)
	{
		ResponseStartGame(Client, "游戏进行中,无法开始!");
		return;//防止重复点击开始bug
	}

	if (Room->IsStake) {
		if (Room->BaseGameMode == 41 || Room->BaseGameMode == 4) {
			ResponseStartGame(Client, "只允许普通开房进行下注比赛！");
			return;
		}
		if (Room->CurrentPlayerNum != 2) {
			ResponseStartGame(Client, "当前房间属于下注模式，只允许双人开始游戏");
			return;
		}
		int commonStakeNum = -1; // 默认值，用于检查
		for (char i = 0; i < 6; i++)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			if (RoomClient)
			{
				if (commonStakeNum == -1) {
					// 初始化 commonStakeNum
					commonStakeNum = RoomClient->StakeNum;
				}
				else if (commonStakeNum != RoomClient->StakeNum) {
					ResponseStartGame(Client, "当前房间属于下注模式，双方下注数量不一致，请重新下注！");
					return; // 或者其他处理
				}
			}
		}

		for (char i = 0; i < 6; i++)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			if (RoomClient)
			{
				int Itemnum = PlayerDB_GetItemNum(RoomClient->Uin, Stake[1]);
				if (Itemnum - commonStakeNum <= 0) {
					std::string nickNameStr(RoomClient->NickName, std::strlen(RoomClient->NickName));
					std::string msg = "玩家/C01【" + nickNameStr + "】/C00当前道具数量为：/C01" + std::to_string(Itemnum) + "/C00，下注数量为：/C01" + std::to_string(commonStakeNum) + "/C00，道具不足，无法开始游戏！";
					ResponseStartGame(Client, msg.c_str());
					return;
				}
			}
		}
	}

	if (Room->IsBottomGame) {

		if (Room->BaseGameMode == 41 || Room->BaseGameMode == 4) {
			ResponseStartGame(Client, "只允许普通开房进行垫底模式或者尾数模式比赛！");
			return;
		}

		//尾数
		if (Room->IsBottomGame == 9) {
			UINT Itemnum = 0;
			for (char i = 0; i < 6; i++)
			{
				std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
				if (RoomClient && RoomClient != Client)
				{
					if (RoomClient->MantissaNum == 0) {
						std::string nickNameStr(RoomClient->NickName, std::strlen(RoomClient->NickName));
						std::string msg = "玩家/C01【" + nickNameStr + "】/C00未下注，无法开始游戏！";
						ResponseStartGame(Client, msg.c_str());
						return;
					}
					if (RoomClient->OBState == 0) {
						std::string nickNameStr(RoomClient->NickName, std::strlen(RoomClient->NickName));
						std::string msg = "玩家/C01【" + nickNameStr + "】/C00未开启观战，无法开始游戏！";
						ResponseStartGame(Client, msg.c_str());
						return;
					}
					int RoomClientNum = PlayerDB_GetItemNum(RoomClient->Uin, Mantissa[3]);
					if (RoomClientNum < RoomClient->MantissaNum) {
						std::string nickNameStr(RoomClient->NickName, std::strlen(RoomClient->NickName));
						std::string msg = "玩家/C01【" + nickNameStr + "】/C00数量不足，无法开始尾数游戏！";
						ResponseStartGame(Client, msg.c_str());
						return;
					}
					Itemnum += RoomClient->MantissaNum;
				}
			}
			int ClientNum = PlayerDB_GetItemNum(Client->Uin, Mantissa[3]);
			if (ClientNum < Itemnum) {
				std::string msg = "总共下注数量：/C01" + std::to_string(Itemnum) + " /C00你的数量为：/C01" + std::to_string(ClientNum) + " /C00数量不足，无法开始游戏！";
				ResponseStartGame(Client, msg.c_str());
				return;
			}

			if (Room->CurrentPlayerNum < 2) {
				ResponseStartGame(Client, "当前房间属于尾数模式，人数不够！");
				return;
			}
		}
		else {
			if (IsSuperMoneyBottomGameMode(Room->IsBottomGame)) {
				if (!g_BottomSuperMoneyConfig.Enabled) {
					ResponseStartGame(Client, "点券垫底赛配置未开启或无效！");
					return;
				}
				const UINT expectedPlayers = Room->IsBottomGame == 4 ? 4u : 6u;
				const std::vector<std::shared_ptr<ClientNode>> racePlayers = GetBottomSuperMoneyRacePlayers(Room);
				if (racePlayers.size() != expectedPlayers) {
					std::string msg = "当前点券垫底赛需要准确/C01【" + std::to_string(expectedPlayers) + "】/C00名参赛玩家！";
					ResponseStartGame(Client, msg.c_str());
					return;
				}
				if (g_BottomSuperMoneyConfig.DenySameIp && BottomSuperMoneyHasInvalidOrDuplicateIp(racePlayers)) {
					ResponseStartGame(Client, "参赛玩家登录IP无效或存在相同IP，不能开始点券垫底赛！");
					return;
				}
				const BottomSuperMoneyModeConfig* mode = GetBottomSuperMoneyModeConfig(Room->IsBottomGame);
				const std::uint32_t maxCost = mode ? GetBottomGameMaxLoserCost(*mode) : 0;
				if (!mode || maxCost == 0) {
					ResponseStartGame(Client, "点券垫底赛配置无效！");
					return;
				}
				for (const auto& roomClient : racePlayers) {
					if (static_cast<std::int64_t>(roomClient->SuperMoney) < static_cast<std::int64_t>(maxCost)) {
						std::string nickNameStr(roomClient->NickName, strnlen_s(roomClient->NickName, sizeof(roomClient->NickName)));
						std::string msg = "玩家/C01【" + nickNameStr + "】/C00点券不足/C01【" + std::to_string(maxCost) + "】/C00，无法开始游戏！";
						ResponseStartGame(Client, msg.c_str());
						return;
					}
				}
			}
			else {
				if (Room->CurrentPlayerNum < 5 && Room->IsBottomGame == 7) {
					ResponseStartGame(Client, "当前房间属于竞技模式，人数不准确！");
					return;
				}
				if (Room->CurrentPlayerNum < 2) {
					ResponseStartGame(Client, "当前房间属于垫底模式，人数不够！");
					return;
				}

				if (Room->IsBottomGame != 7) {
					const UINT bottomItemNum = Room->IsBottomGame == 1 ? IsBottomGame[2] : Room->BottomGameNum;
					for (char i = 0; i < 6; i++)
					{
						std::shared_ptr<ClientNode> roomClient = Room->Player[i];
						if (roomClient)
						{
							const int itemNum = PlayerDB_GetItemNum(roomClient->Uin, IsBottomGame[1]);
							if (itemNum <= static_cast<int>(bottomItemNum)) {
								std::string nickNameStr(roomClient->NickName, std::strlen(roomClient->NickName));
								std::string msg = "玩家/C01【" + nickNameStr + "】/C00当前道具数量为：/C01" + std::to_string(itemNum) + "/C00，垫底数量为：/C01" + std::to_string(bottomItemNum) + "/C00，道具不足，无法开始游戏！";
								ResponseStartGame(Client, msg.c_str());
								return;
							}
						}
					}
				}
			}
		}

		
	}



	bool isCustomFlow = IsCustomRoomFlow(Room);
	if (isCustomFlow && !Room->HasCustomScheme) {
		ResponseStartGame(Client, "custom scheme not ready");
		QQS_DebugPrintf10001("[FLOW][StartGame-CUSTOM] reject RoomID=%u SpeFlag=%u because CustomSchemeWire is empty/not applied\n", Room->ID, Room->SpeFlag);
		return;
	}
	// Fail before 509. Once the client enters Top-Kart loading, skipping the
	// exact skin/kart 551 and continuing to 511 creates a deterministic null
	// physics object crash. Validate every active racer while room state is
	// still recoverable.
	if (!isCustomFlow && Room->BaseGameMode != 4 && Room->BaseGameMode != 41)
	{
		for (char i = 0; i < roomSeatLimit; ++i)
		{
			std::shared_ptr<ClientNode> roomClient = Room->Player[i];
			if (!roomClient || roomClient->OBState || roomClient->deleteflag || !roomClient->IsLogin || roomClient->Uin == 0)
				continue;
			UINT resolvedLoadID = 0;
			if (!PreflightKartPhysParamForPlayer(roomClient, &resolvedLoadID))
			{
				QQS_DebugPrintf10001("[FLOW][StartGame-551-PREFLIGHT-ABORT] RoomID=%u Uin=%u Seat=%d KartID=%u SkinID=%u LoadID=%u -> no 509/551/907/511\n",
					Room->ID, roomClient->Uin, i, roomClient->KartID, roomClient->KartSkinID, resolvedLoadID);
				ResponseStartGame(Client, "赛车物理数据缺失，请更换赛车后重试");
				return;
			}
		}
	}
	if (isCustomFlow) {
		Room->CustomWaitingPrepareReadyBeforeLoad = false;
		Room->CustomPreloadReadySatisfied = false;
		Room->CustomPreloadReadyMask = 0;
		Room->CustomWaitingRaceBegin = false;
		Room->CustomRaceBeginSent = false;
		Room->CustomLoadingDispatching = false;
		Room->CustomLoadReadyMask = 0;
		Room->CustomLoadingStartTick = 0;
		Room->CustomLastSentMsgID = 0;
		Room->CustomCurrentStageIndex = 0;
		Room->CustomLastFinishStageIndex = 0xFF;
		Room->CustomLastStageFinishTick = 0;
		Room->CustomStageArriveStageIndex = 0xFF;
		Room->CustomStageArriveMask = 0;
		Room->CustomStageArriveCount = 0;
		Room->CustomStageArriveNeed = 0;
		Room->CustomStageFirstArriveTick = 0;
		ClearCustomRaceStartSnapshot(Room, "start-game-custom-reset");
	}

	for (char i = 0; i < roomSeatLimit && i < (char)Room->Player.size(); ++i)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
			ClearOfficialRoomReturnFlow(RoomClient, "start-game-commit");
	}
	Room->BottomGameSettlementDone = false;
	Room->Callback = OnStart;//设置回调
	Room->ranknumber = 0;//排名
	Room->CountDownFlag = 0;
	Room->startStatus = 2;
	if (!CaptureRoomRaceStartSnapshot(Room, isCustomFlow ? "start-game-custom" : "start-game"))
	{
		Room->startStatus = 1;
		Room->Callback = NULL;
		ResponseStartGame(Client, "开局快照无效，请检查房间成员状态");
		return;
	}
	if (Room->BaseGameMode == 41)
	{
		for (char i = 0; i < 6; i++)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			if (RoomClient && RoomClient->GameBan)
			{
				std::string nickNameStr(RoomClient->NickName, std::strlen(RoomClient->NickName));
				std::string msg = "玩家/C01【" + nickNameStr + "】/C00已经被禁止跑边境排位！";
				ResponseStartGame(Client, msg.c_str());
				return;
			}
		}

		std::lock_guard<std::recursive_mutex> lg(BorderQueueMutex);
		for (char i = 0; i < 6; i++)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			//InitClientGameArrt(RoomClient);
			if (RoomClient)
			{
				auto now = std::chrono::steady_clock::now();
				ResponseAddToMatchQueue(RoomClient, 0x0);
				RemoveFromBorderMatchQueue_NoLock(RoomClient);
				RoomClient->TaskID = 0xFFFD;
				BorderMatchQueue.push_front({ RoomClient, now });
			}
		}
		return;
	}


	if (Room->BaseGameMode == 4) //舞蹈
	{
		if (Room->SceneID == 0)
		{
			Room->SceneID = 17;
		}

		if (Room->MusicID < 10)
		{
			Room->MusicID = 35;
		}

		for (char i = 0; i < 6; i++)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			if (RoomClient)
			{
				RoomClient->GameID = 1;
				RoomClient->IsReady = FALSE;
				RoomClient->IsFinish = FALSE;

				RoomClient->Scores = 0;
				RoomClient->DancePerfectCount = 0;
				NotifyDanceGameBegin(RoomClient, Room);
			}
		}
		CreateRoomTimer(Room, DanceMusicDuration(Room->MusicID), OnDanceTimer);
	}
	else
	{
		UINT MapID = Room->MapID;
		if (isCustomFlow && Room->CustomMapNum > 0)
		{
			BYTE stageCount = BuildCustomStageMapOrder(Room);
			MapID = GetCustomStageMapID(Room, 0, Room->MapID);
			Room->MapID = MapID;
			QQS_DebugPrintf10001("[FLOW][StartGame-CUSTOM-MAP] RoomID=%u StageIdx=0/%u MapID=%u Round=%u MapList=[%u,%u,%u] Order=[%u,%u,%u]\n",
				Room->ID, stageCount, MapID, Room->CustomRoundNum,
				Room->CustomMapIDList[0], Room->CustomMapIDList[1], Room->CustomMapIDList[2],
				Room->CustomStageMapOrder[0], Room->CustomStageMapOrder[1], Room->CustomStageMapOrder[2]);
		}
		else if (MapID < 100) // random map
		{
			UINT mapPoolID = MapID;
			MapID = GetRandMap(MapID);
			// 普通随机房：Room->MapID 是房间 UI/玩家选择的地图池 ID，不能被本局真实地图覆盖。
			// 本局实际地图只写到 RoomClient->MapID / 509，否则结算/ESC 回房后会显示某一张具体地图，
			// 玩家下一局还要重新选择随机池。
			Room->MapID = mapPoolID;
		}
		else
		{
			Room->MapID = MapID;
		}
		QQS_DebugPrintf10001("[FLOW][StartGame] selected MapID=%u RoomID=%u RoomMap=%u HasCustom=%d CustomMapNum=%u CustomMap=[%u,%u,%u] CustomRound=%u\n",
			MapID, Room->ID, Room->MapID, Room->HasCustomScheme ? 1 : 0, Room->CustomMapNum,
			Room->CustomMapIDList[0], Room->CustomMapIDList[1], Room->CustomMapIDList[2], Room->CustomRoundNum);
		if (MapID == 335 || MapID == 419 || MapID == 155 || MapID == 156)
		{
			ResponseStartGame(Client, "该地图无法开始游戏!");
			return;
		}
		Map* pMap = GetMap(MapID);
		if (!pMap)
		{
			printf("请配置 MapID:%d\n", MapID);
			fflush(stdout);
		}

		const bool speedLadderMapConfigured = SpeedLadder_IsConfiguredMap(MapID);
		const bool speedLadderNormalCarrier = (Room->BaseGameMode == 1 && Room->SubGameMode == 1 && !isCustomFlow);
		if (Client && !Client->SpeedLadderRacePending && !Client->SpeedLadderRaceActive &&
			speedLadderNormalCarrier && speedLadderMapConfigured && SpeedLadder_IsLobbyArmed(Client))
		{
			Client->SpeedLadderRacePending = true;
			Client->SpeedLadderRaceActive = false;
			Client->SpeedLadderRaceMapID = MapID;
			Client->SpeedLadderRaceSeasonID = SpeedLadder_GetCurrentSeasonID();
			Client->SpeedLadderRaceArmTick = GetTickCount();
			QQS_DebugPrintf10001("[SpeedLadder-RACE] pending source=start-game-lobby Uin=%u RoomID=%u MapID=%u Season=%u LobbyStage=%u LobbyDelta=%u Base=%u Sub=%u\n",
				Client->Uin, Room->ID, MapID, Client->SpeedLadderRaceSeasonID,
				Client->SpeedLadderLobbyStage,
				Client->SpeedLadderLobbyTick ? (GetTickCount() - Client->SpeedLadderLobbyTick) : 0xFFFFFFFF,
				(unsigned)Room->BaseGameMode, (unsigned)Room->SubGameMode);
		}

		const bool speedLadderStart =
			Client &&
			Client->SpeedLadderRacePending &&
			!isCustomFlow &&
			speedLadderMapConfigured &&
			(Client->SpeedLadderRaceMapID == 0 || Client->SpeedLadderRaceMapID == MapID);
		if (speedLadderStart)
		{
			Client->SpeedLadderRaceActive = true;
			Client->SpeedLadderRaceMapID = MapID;
			if (Client->SpeedLadderRaceSeasonID == 0)
				Client->SpeedLadderRaceSeasonID = SpeedLadder_GetCurrentSeasonID();
			QQS_DebugPrintf10001("[SpeedLadder-RACE] active Uin=%u RoomID=%u MapID=%u Season=%u ArmAgo=%u LobbyStage=%u\n",
				Client->Uin, Room->ID, MapID, Client->SpeedLadderRaceSeasonID,
				Client->SpeedLadderRaceArmTick ? (GetTickCount() - Client->SpeedLadderRaceArmTick) : 0xFFFFFFFF,
				Client->SpeedLadderLobbyStage);
		}
		else if (Client && Client->SpeedLadderRacePending)
		{
			QQS_DebugPrintf10001("[SpeedLadder-RACE] start-not-ladder clear Uin=%u RoomID=%u MapID=%u PendingMap=%u IsCustom=%u Configured=%u\n",
				Client->Uin, Room->ID, MapID, Client->SpeedLadderRaceMapID, isCustomFlow ? 1 : 0, speedLadderMapConfigured ? 1 : 0);
			SpeedLadder_ClearRaceContext(Client, "start-game-not-ladder");
		}

		int customStartPlayerNum = 0;
		bool normalRaceDispatchFailed = false;
		if (isCustomFlow)
		{
			customStartPlayerNum = Room->RaceStartPlayerNum;
			if (customStartPlayerNum <= 0)
			{
				ResponseStartGame(Client, "custom room has no players");
				QQS_DebugPrintf10001("[FLOW][StartGame-CUSTOM] reject RoomID=%u because no active players\n", Room->ID);
				return;
			}
		}
		if (!BeginRoomGameLogicSession(Room, "start-game"))
		{
			Room->startStatus = 1;
			Room->Callback = NULL;
			ResponseStartGame(Client, "游戏逻辑会话创建失败");
			return;
		}

		std::vector<std::shared_ptr<ClientNode>> raceStartClients;
		raceStartClients.reserve((size_t)roomSeatLimit);
		for (char i = 0; i < roomSeatLimit; i++)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			if (!RoomClient)
				continue;
			if (isCustomFlow)
			{
				RoomSeatMask bit = GetRoomSeatBit(i);
				if ((Room->RaceActiveMask & bit) == 0)
					continue;
			}

			// Prepare the complete authoritative roster before building the
			// first 509. The V5 client parser expects every listed racer to
			// already share the same game and map state.
			ResetNormalRaceWireState(RoomClient);
			RoomClient->GameID = Room->RaceGameID;
			RoomClient->IsReady = FALSE;
			RoomClient->IsFinish = FALSE;
			RoomClient->IsRefitItem = false;
			if (pMap)
			{
				RoomClient->MapCompatibilityMode = false;
				RoomClient->EndCheckPoint = pMap->End;
				USHORT physicalRound = pMap->Round > 0 ? (USHORT)pMap->Round : 1;
				RoomClient->TotalRound = isCustomFlow ? GetCustomOfficialTotalRound(Room, MapID, physicalRound) : physicalRound;
				RoomClient->Loop = pMap->Loop;
			}
			else
			{
				RoomClient->MapCompatibilityMode = true;
				RoomClient->EndCheckPoint = 0;
				RoomClient->TotalRound = 1;
				RoomClient->Loop = false;
			}
			RoomClient->MapID = MapID;
			RoomClient->FinishTime = 0;
			RoomClient->Round = 0;
			RoomClient->PassedCheckPoint = 0;
			RoomClient->MsgSequence = -1;
			RoomClient->CustomRaceStageIndex = 0;
			if (!isCustomFlow)
			{
				NotifyUpdateKartSkillStoneInfo(RoomClient, RoomClient->KartID);
				if (RoomClient->KartSkinID != 0 && RoomClient->KartSkinID != RoomClient->KartID)
					NotifyUpdateKartSkillStoneInfo(RoomClient, RoomClient->KartSkinID);
				if (RoomClient->GlobalStone == 1)
				{
					NotifyGlobalStoneForKartAndSkin(RoomClient);
					//NotifyUpdateKartSkillStoneInfo2(RoomClient, RoomClient->KartID);
				}
			}

			if (Room->ChatRoomType == 6)
				LeaveOutdoor(RoomClient);//离开3d房休闲区防止卡起步
			if (isCustomFlow)
			{
				RoomClient->CurrentRoom = Room.get();
				RoomClient->BorderRoomID = 0;
				RoomClient->RoomID = (USHORT)Room->ID;
				RoomClient->CustomLastDistanceZeroTick = 0;
			}
			raceStartClients.push_back(RoomClient);
		}

		for (const auto& RoomClient : raceStartClients)
		{
			if (isCustomFlow)
			{
				NotifyCustomMatchAck(RoomClient, Room);
				Room->CustomLastSentMsgID = 20213;
				NotifyCustomMatchResult(RoomClient, Room);
				Room->CustomLastSentMsgID = 20081;
				QQS_DebugPrintf10001("[FLOW][StartGame-CUSTOM-OFFICIAL-PLAYER] Uin=%u RoomID=%u MapID=%u EffectiveRound=%u PhysicalRound=%u CustomRound=%u MapNum=%u ActiveMask=0x%04X -> 20213/20081 trigger, no 20082 gate\n",
					RoomClient->Uin, Room->ID, RoomClient->MapID, RoomClient->TotalRound,
					pMap ? (unsigned)pMap->Round : 1,
					Room->CustomRoundNum, Room->CustomMapNum, (unsigned)Room->RaceActiveMask);
				continue;
			}

			const bool officialNormalWire =
				!Room->LuckyMatchInternalRoom && RoomClient->TaskID == 0 && RoomClient->BorderRoomID == 0;
			RoomClient->NormalRaceWireActive = officialNormalWire;
			RoomClient->NormalRaceKartPhysReady = false;
			RoomClient->NormalRaceWireRouteID = officialNormalWire
				? static_cast<USHORT>(RoomClient->GameID)
				: 0;

			const bool gameBeginSent = NotifyGameBegin(RoomClient, Room->Player);
			const bool kartPhysSent = gameBeginSent && NotifyKartPhysParam(RoomClient, Room->Player);
			const bool kartStoneSent = kartPhysSent && NotifyOtherKartStoneInfo(RoomClient, Room->Player);
			if (!gameBeginSent || !kartPhysSent || !kartStoneSent)
			{
				RoomClient->NormalRaceKartPhysReady = false;
				normalRaceDispatchFailed = true;
				QQS_DebugPrintf10001("[FLOW][NORMAL-RACE-DISPATCH-ABORT] RoomID=%u Uin=%u RouteID=%u Sent509=%d Sent551=%d Sent907=%d -> block timer/511\n",
					Room->ID, RoomClient->Uin, (unsigned)RoomClient->NormalRaceWireRouteID,
					gameBeginSent ? 1 : 0, kartPhysSent ? 1 : 0, kartStoneSent ? 1 : 0);
				break;
			}
			RoomClient->NormalRaceKartPhysReady = officialNormalWire;
			QQS_DebugPrintf10001("[FLOW][NORMAL-RACE-WIRE-READY] RoomID=%u Uin=%u RouteID=%u -> 509(seq1)/551(seq2)/907(seq3), server timer sends 511(seq4)\n",
				Room->ID, RoomClient->Uin, (unsigned)RoomClient->NormalRaceWireRouteID);
		}
		if (normalRaceDispatchFailed)
		{
			Room->startStatus = 1;
			Room->Callback = NULL;
			Room->CountDownFlag = 0;
			for (char i = 0; i < roomSeatLimit; ++i)
			{
				std::shared_ptr<ClientNode> roomClient = Room->Player[i];
				if (!roomClient)
					continue;
				ResetNormalRaceWireState(roomClient);
				roomClient->GameID = 0;
			}
			ClearRoomGameLogicSession(Room, "normal-race-dispatch-failed");
			ResponseStartGame(Client, "开局数据发送失败，请重新开始");
			return;
		}
		if (isCustomFlow)
		{
			Room->CustomWaitingPrepareReadyBeforeLoad = false;
			Room->CustomPreloadReadySatisfied = false;
			Room->CustomWaitingRaceBegin = false;
			Room->CustomRaceBeginSent = false;
			Room->CustomLoadingDispatching = false;
			Room->CustomLoadReadyMask = 0;
			Room->CustomPreloadReadyMask = 0;
			Room->CustomLoadingStartTick = GetTickCount();
			Room->CustomLastSentMsgID = 20081;
			QQS_DebugPrintf10001("[FLOW][StartGame-CUSTOM-OFFICIAL] 110 locked RoomID=%u MapID=%u PlayerNum=%d ActiveMask=0x%04X EffectiveRound=%u CustomRound=%u MapNum=%u; 20213/20081 sent, enter 1718/509/551 now, client 120 later triggers 511\n",
				Room->ID, Room->MapID, customStartPlayerNum, (unsigned)Room->RaceActiveMask,
				Room->Player[0] ? (unsigned)Room->Player[0]->TotalRound : 0, Room->CustomRoundNum, Room->CustomMapNum);
			OnCustomEnterLoading(Room);
			return;
		}
		// Normal rooms do not require a client 120 acknowledgement. Continue the
		// 509/551/907 stream with 511 after the configured loading delay.
		QQS_DebugPrintf10001(
			"[FLOW][NORMAL-RACE-511-SCHEDULE] RoomID=%u DelayMs=%u Trigger=server-timer-no-120\n",
			Room->ID, static_cast<unsigned>(SyncTime));
		CreateRoomTimer(Room, SyncTime, OnBegin);
	}


}
void ResponseStartGame(std::shared_ptr<ClientNode> Client, const char* Reason)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, -1); //ResultID

	len = strlen(Reason);
	Write8(p, (BYTE)len); //ReasonLen
	memcpy(p, Reason, len);
	p += len;

	len = p - buf.data();
	SendToClient(Client, 110, buf.data(), len, Client->RoomID, FE_ROOM, 1, Response);
}


void RequestChangeMap(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	UINT NewMapID = 0;
	char Season = -1;
	char Timeslot = -1;
	char Weather = -1;
#ifndef ZingSpeed
	QQSpeedAuroraOfficial::RoomChangeRequest request;
	QQSpeedAuroraOfficial::RoomAuroraInfo resolvedAuroraInfo;
	std::string auroraError;
	const std::size_t payloadLength =
		QQSpeedAuroraOfficial::PayloadLengthFromWireBodyLength(BodyLen);
	if (!Client ||
		!QQSpeedAuroraOfficial::ParseRoomChangeRequest(
			Body, payloadLength, request, &auroraError) ||
		request.uin != Client->Uin ||
		request.newMapId < 0)
	{
		QQS_DebugPrintf10001(
			"[AURORA][121-REJECT] Uin=%u WireBodyLen=%zu PayloadLen=%zu "
			"RequestUin=%u NewMapID=%d Stage=parse Error=%s\n",
			Client ? Client->Uin : 0, BodyLen, payloadLength,
			request.uin, request.newMapId, auroraError.c_str());
		return;
	}
	NewMapID = static_cast<UINT>(request.newMapId);
	Season = static_cast<char>(request.season);
	Timeslot = static_cast<char>(request.timeslot);
	Weather = static_cast<char>(request.weather);
	const bool wantsAurora = request.hasAuroraInfo;
	const QQSpeedAuroraOfficial::Work* auroraWork = nullptr;
	if (wantsAurora)
	{
		auroraWork =
			QQSpeedAuroraOfficial::FindWork(request.auroraInfo.workId);
		bool resolved = false;
		if (request.newMapId != 0)
			auroraError =
				"Aurora room change requires wire NewMapID zero";
		else if (!auroraWork)
			auroraError =
				"Aurora room work is not in the playable catalog";
		else
			resolved = QQSpeedAuroraOfficial::ResolveRoomAuroraInfo(
				request.auroraInfo.workId,
				resolvedAuroraInfo,
				&auroraError);
		if (!resolved)
		{
			QQS_DebugPrintf10001(
				"[AURORA][121-REJECT] Uin=%u RoomID=%u NewMapID=%d "
				"WorkID=%llu Stage=resolve Error=%s\n",
				Client->Uin, Client->RoomID, request.newMapId,
				static_cast<unsigned long long>(
					request.auroraInfo.workId),
				auroraError.c_str());
			return;
		}
	}
#else
	Read32(Body); // Uin
	Read32(Body); // Time
	NewMapID = Read32(Body);
	{
		BYTE* pSTDynamicInfo = Body;
		USHORT len = Read16(pSTDynamicInfo);

		Season = Read8(pSTDynamicInfo);
		Timeslot = Read8(pSTDynamicInfo);
		Weather = Read8(pSTDynamicInfo);

		Body += len;
	}
#endif

	std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
	if (!Room)
	{
		return;
	}
	const bool wasCustomRoom = IsCustomRoomFlow(Room);
	const bool wasAuroraRoom = Room->AuroraUGC;
	if (wasCustomRoom)
	{
		QQS_DebugPrintf10001("[FLOW][121-CUSTOM-TO-NORMAL] Uin=%u RoomID=%u OldMap=%u NewMap=%u SpeFlag=%u SchemeID=%u CustomMapNum=%u CustomMap=[%u,%u,%u]\n",
			Client ? Client->Uin : 0, Room->ID, Room->MapID, NewMapID,
			Room->SpeFlag, Room->CustomSchemeID, Room->CustomMapNum,
			Room->CustomMapIDList[0], Room->CustomMapIDList[1], Room->CustomMapIDList[2]);
		ClearCustomRoomState(Room, "121-change-map-normal");
	}
#ifndef ZingSpeed
	if (wantsAurora)
	{
		ApplyAuroraWorkToRoom(Room, *auroraWork);
		Client->MapID = auroraWork->mapId;
	}
	else
	{
		Room->AuroraUGC = false;
		Room->AuroraWorkID = 0;
		Room->AuroraVersion = 0;
		Room->AuroraRenderDataCosURL.clear();
		Room->AuroraMD5.clear();
		Room->MapID = NewMapID;
		Client->MapID = NewMapID;
	}
	const bool roomTypeChanged =
		wasCustomRoom || (wasAuroraRoom != wantsAurora);
#else
	Room->MapID = NewMapID;
	if (Client)
		Client->MapID = NewMapID;
	const bool roomTypeChanged = wasCustomRoom;
#endif
	const bool speedLadderMapConfigured = SpeedLadder_IsConfiguredMap(NewMapID);
	const bool speedLadderNormalCarrier =
		(Room->BaseGameMode == 1 && Room->SubGameMode == 1 &&
			!wasCustomRoom && !Room->AuroraUGC);
	if (Client && !Client->SpeedLadderRacePending && !Client->SpeedLadderRaceActive &&
		speedLadderNormalCarrier && speedLadderMapConfigured && SpeedLadder_IsLobbyArmed(Client))
	{
		Client->SpeedLadderRacePending = true;
		Client->SpeedLadderRaceActive = false;
		Client->SpeedLadderRaceMapID = NewMapID;
		Client->SpeedLadderRaceSeasonID = SpeedLadder_GetCurrentSeasonID();
		Client->SpeedLadderRaceArmTick = GetTickCount();
		QQS_DebugPrintf10001("[SpeedLadder-RACE] pending source=change-map-lobby Uin=%u RoomID=%u MapID=%u Season=%u LobbyStage=%u LobbyDelta=%u Base=%u Sub=%u\n",
			Client->Uin, Room->ID, NewMapID, Client->SpeedLadderRaceSeasonID,
			Client->SpeedLadderLobbyStage,
			Client->SpeedLadderLobbyTick ? (GetTickCount() - Client->SpeedLadderLobbyTick) : 0xFFFFFFFF,
			(unsigned)Room->BaseGameMode, (unsigned)Room->SubGameMode);
	}
	if (Client && Client->SpeedLadderRacePending)
	{
		if (speedLadderMapConfigured)
		{
			Client->SpeedLadderRaceMapID = NewMapID;
			if (Client->SpeedLadderRaceSeasonID == 0)
				Client->SpeedLadderRaceSeasonID = SpeedLadder_GetCurrentSeasonID();
			QQS_DebugPrintf10001("[SpeedLadder-RACE] map-armed Uin=%u RoomID=%u MapID=%u Season=%u Pending=%u Active=%u\n",
				Client->Uin, Room->ID, NewMapID, Client->SpeedLadderRaceSeasonID,
				Client->SpeedLadderRacePending ? 1 : 0, Client->SpeedLadderRaceActive ? 1 : 0);
		}
		else
		{
			QQS_DebugPrintf10001("[SpeedLadder-RACE] map-invalid clear Uin=%u RoomID=%u MapID=%u\n",
				Client->Uin, Room->ID, NewMapID);
			SpeedLadder_ClearRaceContext(Client, "change-map-not-ladder");
		}
	}
	Room->STDynamicInfo.Season = Season;
	Room->STDynamicInfo.Timeslot = Timeslot;
	Room->STDynamicInfo.Weather = Weather;

	ResponseChangeMap(Client, Room, roomTypeChanged);

	for (char i = 0; i < 6; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			if (RoomClient != Client)
			{
				NotifyChangeMap(
					RoomClient, Client->Uin, Room, roomTypeChanged);
			}
		}
	}


}
#ifndef ZingSpeed
static bool QQS_WriteRoomAuroraInfo(
	BYTE*& p,
	const std::shared_ptr<RoomNode>& Room,
	const char* source)
{
	if (!Room || !Room->AuroraUGC)
	{
		Write8(p, 0); // HasAuroraInfo
		return true;
	}

	QQSpeedAuroraOfficial::RoomAuroraInfo info;
	std::vector<std::uint8_t> nested;
	std::string error;
	if (!QQSpeedAuroraOfficial::ResolveRoomAuroraInfo(
			Room->AuroraWorkID, info, &error) ||
		!QQSpeedAuroraOfficial::BuildRoomAuroraInfo(
			info, nested, &error))
	{
		QQS_DebugPrintf10001(
			"[AURORA][CHANGE-MAP-ABORT] Source=%s RoomID=%u "
			"WorkID=%llu Error=%s\n",
			source ? source : "", Room ? Room->ID : 0,
			Room
				? static_cast<unsigned long long>(Room->AuroraWorkID)
				: 0ULL,
			error.c_str());
		return false;
	}

	Write8(p, 1); // HasAuroraInfo
	memcpy(p, nested.data(), nested.size());
	p += nested.size();
	return true;
}
#endif

void ResponseChangeMap(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, bool RoomTypeChanged)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, 0); //ResultID
#ifndef ZingSpeed
	const UINT wireMapID = Room->AuroraUGC ? 0 : Room->MapID;
#else
	const UINT wireMapID = Room->MapID;
#endif
	Write32(p, wireMapID); //NewMapID
	Write8(p, 0); //ReasonLen
	Write8(p, 0); //UnLockMapCondDescNum
	//m_astUnLockMapCondDesc[].m_bDescLen

	{ //STDynamicInfo
		BYTE* pSTDynamicInfo = p;
		Write16(pSTDynamicInfo, 0); //len

		Write8(pSTDynamicInfo, Room->STDynamicInfo.Season);
		Write8(pSTDynamicInfo, Room->STDynamicInfo.Timeslot);
		Write8(pSTDynamicInfo, Room->STDynamicInfo.Weather);

		len = pSTDynamicInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}
	Write32(p, 0); //PreKnowMapID
	Write32(p, 0); //MapSubID
#ifndef ZingSpeed
	Write8(p, 0); //FirstlyTipMap
	Write8(p, RoomTypeChanged ? 1 : 0); //RoomTypeChanged
	if (!QQS_WriteRoomAuroraInfo(p, Room, "ResponseChangeMap"))
		return;
#endif

	len = p - buf.data();
	SendToClient(Client, 121, buf.data(), len, Client->GameID, FE_GAMELOGIC, 1, Response);
}
void NotifyChangeMap(std::shared_ptr<ClientNode> Client, UINT Uin, std::shared_ptr<RoomNode> Room, bool RoomTypeChanged)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write32(p, Uin); //Uin
#ifndef ZingSpeed
	const UINT wireMapID = Room->AuroraUGC ? 0 : Room->MapID;
#else
	const UINT wireMapID = Room->MapID;
#endif
	Write32(p, wireMapID); //NewMapID

	{ //STDynamicInfo
		BYTE* pSTDynamicInfo = p;
		Write16(pSTDynamicInfo, 0); //len

		Write8(pSTDynamicInfo, Room->STDynamicInfo.Season);
		Write8(pSTDynamicInfo, Room->STDynamicInfo.Timeslot);
		Write8(pSTDynamicInfo, Room->STDynamicInfo.Weather);

		len = pSTDynamicInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	Write32(p, 0); //PreKnowMapID
	Write32(p, 0); //MapSubID
#ifndef ZingSpeed
	Write8(p, 0); //FirstlyTipMap
	Write8(p, RoomTypeChanged ? 1 : 0); //RoomTypeChanged
	if (!QQS_WriteRoomAuroraInfo(p, Room, "NotifyChangeMap"))
		return;
#endif

	len = p - buf.data();
	SendToClient(Client, 520, buf.data(), len, Client->RoomID, FE_ROOM, 1, Notify);
}

void NotifyChangeSeatState(std::shared_ptr<ClientNode> Client, char SeatID, char SeatState)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;

	Write8(p, SeatID); //SeatID
	Write8(p, SeatState); //SeatState

	len = p - buf.data();
	SendToClient(Client, 502, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void RequestChangeSeatState(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);

	short RoomID = Read16(Body);
	BYTE SeatID = Read8(Body);
	BYTE SeatState = Read8(Body);//0关闭 1开放

	std::shared_ptr<RoomNode> Room = GetRoom(RoomID);
	if (!Room)
	{
		return;
	}
	if (!IsRoomConnOwner(Room, Client))
	{
		return;
	}
	int seatLimit = GetRoomSeatLimit(Room);
	if (SeatID >= seatLimit)
	{
		QQS_DebugPrintf10001("[FLOW][107-SEATSTATE-IGNORED] Uin=%u RoomID=%u SeatID=%u SeatLimit=%d SeatNum=%u\n",
			Client ? Client->Uin : 0, Room->ID, (unsigned)SeatID, seatLimit, (unsigned)Room->SeatNum);
		return;
	}

	BYTE normalizedSeatState = SeatState ? 1 : 0;
	BYTE oldSeatState = Room->SeatState[SeatID] ? 1 : 0;
	Room->SeatState[SeatID] = normalizedSeatState;
	if (IsTwelvePlayerRoom(Room))
	{
		// 12P 房壳容量始终为 12；SeatNum 仅由本局真实的 107 开/锁座操作动态计算，
		// 不能从任何抓包样本预设固定锁位。
		Room->SeatNum = GetConfiguredRoomSeatNum(Room);
	}
	else if (normalizedSeatState != oldSeatState)
	{
		if (normalizedSeatState == 1 && Room->SeatNum < ROOM_SEAT_NORMAL_MAX)
		{
			Room->SeatNum++;
		}
		else if (normalizedSeatState == 0 && Room->SeatNum > 0)
		{
			Room->SeatNum--;
		}
	}
	seatLimit = GetRoomSeatLimit(Room);
	QQS_DebugPrintf10001("[FLOW][107-SEATSTATE] Uin=%u RoomID=%u SeatID=%u State=%u Old=%u SeatNum=%u SeatLimit=%d Is12=%d\n",
		Client ? Client->Uin : 0, Room->ID, (unsigned)SeatID, (unsigned)normalizedSeatState, (unsigned)oldSeatState,
		(unsigned)Room->SeatNum, seatLimit, IsTwelvePlayerRoom(Room) ? 1 : 0);

	ResponseChangeSeatState(Client, SeatID, normalizedSeatState);

	for (char i = 0; i < seatLimit; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			if (RoomClient != Client)
			{
				NotifyChangeSeatState(RoomClient, SeatID, normalizedSeatState);
			}
		}
	}

}
void ResponseChangeSeatState(std::shared_ptr<ClientNode> Client, BYTE SeatID, BYTE SeatState)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, 0); //ResultID

	Write8(p, SeatID);
	Write8(p, SeatState);

	Write8(p, 0); //ReasonLen

	len = p - buf.data();
	SendToClient(Client, 107, buf.data(), len, Client->RoomID, FE_ROOM, 1, Response);
}

void NotifyKickFromRoom(std::shared_ptr<ClientNode> Client, UINT Uin, USHORT RoomID)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;

	Write16(p, RoomID); //RoomID
	Write32(p, Uin); //DstUin
	Write16(p, 0); //ReasonID
	Write8(p, 0); //ReasonLen
	Write16(p, 0); //BaseGameMode

	len = p - buf.data();
	SendToClient(Client, 503, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void RequestKickFromRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);

	UINT DstUin = Read32(Body);
	BYTE KickReason = Read8(Body);

	std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
	if (!Room)
	{
		return;
	}
	if (!IsRoomConnOwner(Room, Client))
	{
		return;
	}
	for (char i = 0; i < 6; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			if (RoomClient->Uin == DstUin)
			{
				if (RoomClient->admin == 1)
				{
					ResponseStartGame(Client, "他是尊敬的管理员，你没有权限踢他！");
					return;
				}

				//if (RoomClient->OBState == 1)
				//{
				//	ResponseStartGame(Client, "无法踢出观战者!");
				//	return;
				//}
				ResponseKickFromRoom(Client, DstUin);//踢出房间
				NotifyKickFromRoom(RoomClient, DstUin, Room->ID);
				//LeaveRoom(RoomClient, 0);
				//LeaveOutdoor(RoomClient);
				if (Room->ChatRoomType == 0)
				{
					ResponseStartGame(RoomClient, "你已被踢出房间!");
				}

			}
		}
	}
}
void ResponseKickFromRoom(std::shared_ptr<ClientNode> Client, UINT DstUin)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, 0); //ResultID

	Write32(p, DstUin);

	Write8(p, 0); //ReasonLen

	len = p - buf.data();
	SendToClient(Client, 108, buf.data(), len, Client->RoomID, FE_ROOM, 1, Response);
}


void NotifyChangeReadyState(std::shared_ptr<ClientNode> Client, UINT Uin, BYTE ReadyState)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write32(p, Uin);
	Write8(p, ReadyState);

	len = p - buf.data();
	std::shared_ptr<RoomNode> Room = GetRoom(Client ? Client->RoomID : 0);
	SendToClientWithWireDstID(Client, 504, buf.data(), len,
		Room ? Room->ID : (Client ? Client->RoomID : 0),
		FE_ROOM,
		GetRoomWireDstIDForClient(Room, Client),
		Notify);
}

void NotifyOfficialReturnReadyObRoster(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Client || !Room)
		return;

	const int seatLimit = GetRoomSeatLimit(Room);
	int sent = 0;
	for (char j = 0; j < seatLimit && j < (int)Room->Player.size(); ++j)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
		if (!RoomClient || RoomClient == Client)
			continue;
		if (RoomClient->deleteflag || !RoomClient->IsLogin || RoomClient->Uin == 0 || RoomClient->OBState)
			continue;
		if (RoomClient->RoomID != Room->ID || RoomClient->CurrentRoom != Room.get() || RoomClient->SeatID != j)
			continue;

		// 504 只刷新已经存在的玩家对象；值必须来自房间权威槽位。
		NotifyChangeReadyState(Client, RoomClient->Uin, RoomClient->ReadyState);
		NotifyChangeOBState(Client, FALSE, RoomClient->Uin);
		sent++;
	}

	QQS_DebugPrintf10001("[FLOW][RETURN-504609-ROSTER] Reason=%s Uin=%u RoomID=%u Sent=%d SeatLimit=%d OtherOnly=1 OwnerRaw=%u OwnerID=%u SelfWireID=%u\n",
		reason ? reason : "", Client->Uin, Room->ID, sent, seatLimit,
		(unsigned)Room->RoomOwnerID, (unsigned)GetRoomConnOwnerID(Room),
		(unsigned)GetRoomConnPlayerID(Room, Client));
}

void NotifyOfficialReturnSelfReadyObState(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, const char* reason)
{
	if (!Client || !Room)
		return;

	const int seatLimit = GetRoomSeatLimit(Room);
	const int seat = (int)Client->SeatID;
	if (seat < 0 || seat >= seatLimit || seat >= (int)Room->Player.size() ||
		Room->Player[seat] != Client || Client->RoomID != Room->ID || Client->CurrentRoom != Room.get())
	{
		QQS_DebugPrintf10001("[FLOW][RETURN-SELF-504609-REJECT] Reason=%s Uin=%u RoomID=%u ClientRoomID=%u Seat=%d SeatLimit=%d\n",
			reason ? reason : "", Client->Uin, Room->ID, Client->RoomID, seat, seatLimit);
		return;
	}

	// 官服赛后回房的 504/609 都指向接收者自己，不是 S104 中的其他玩家 roster。
	NotifyChangeReadyState(Client, Client->Uin, Client->ReadyState);
	NotifyChangeOBState(Client, FALSE, Client->Uin);
	QQS_DebugPrintf10001("[FLOW][RETURN-SELF-504609] Reason=%s Uin=%u RoomID=%u Seat=%d ReadyState=%u OwnerID=%u SelfWireID=%u\n",
		reason ? reason : "", Client->Uin, Room->ID, seat, (unsigned)Client->ReadyState,
		(unsigned)GetRoomConnOwnerID(Room), (unsigned)GetRoomConnPlayerID(Room, Client));
}

static bool IsOfficialRoomReturnKind(BYTE Kind)
{
	return Kind == QQS_OFFICIAL_ROOM_RETURN_KIND_NORMAL ||
		Kind == QQS_OFFICIAL_ROOM_RETURN_KIND_CUSTOM ||
		Kind == QQS_OFFICIAL_ROOM_RETURN_KIND_TWELVE_CUSTOM ||
		Kind == QQS_OFFICIAL_ROOM_RETURN_KIND_BORDER_TEAM;
}

static bool IsOfficialRoomReturnPhase(BYTE Phase)
{
	return Phase == QQS_OFFICIAL_ROOM_RETURN_PHASE_SERVER_CHAIN_PENDING ||
		Phase == QQS_OFFICIAL_ROOM_RETURN_PHASE_WAIT_AT62_SEND_ROSTER ||
		Phase == QQS_OFFICIAL_ROOM_RETURN_PHASE_WAIT_AT62_ACK_ONLY ||
		Phase == QQS_OFFICIAL_ROOM_RETURN_PHASE_AT62_COMPLETE;
}

void ClearOfficialRoomReturnFlow(std::shared_ptr<ClientNode> Client, const char* reason)
{
	if (!Client)
		return;

	const OfficialRoomReturnFlowState old = Client->OfficialRoomReturn;
	Client->OfficialRoomReturn = OfficialRoomReturnFlowState{};
	if (old.Kind != QQS_OFFICIAL_ROOM_RETURN_KIND_NONE || old.Phase != QQS_OFFICIAL_ROOM_RETURN_PHASE_NONE)
	{
		QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-CLEAR] Reason=%s Uin=%u Kind=%u Phase=%u ReadyRequestCommitted=%d RoomID=%u AgeMs=%u Action62AgeMs=%u ReadyRequestAgeMs=%u\n",
			reason ? reason : "", Client->Uin, (unsigned)old.Kind, (unsigned)old.Phase,
			old.ReadyRequestCommitted ? 1 : 0,
			(unsigned)old.RoomID,
			old.StartedTick ? (unsigned)(GetTickCount() - old.StartedTick) : 0,
			old.Action62Tick ? (unsigned)(GetTickCount() - old.Action62Tick) : 0,
			old.ReadyRequestTick ? (unsigned)(GetTickCount() - old.ReadyRequestTick) : 0);
	}
}

void BeginOfficialRoomReturnFlow(
	std::shared_ptr<ClientNode> Client,
	std::shared_ptr<RoomNode> Room,
	BYTE Kind,
	BYTE Phase,
	const char* reason)
{
	if (!Client || !Room || !IsOfficialRoomReturnKind(Kind) || !IsOfficialRoomReturnPhase(Phase))
	{
		if (Client)
			ClearOfficialRoomReturnFlow(Client, "begin-invalid");
		return;
	}

	const DWORD nowTick = GetTickCount();
	Client->OfficialRoomReturn.Kind = Kind;
	Client->OfficialRoomReturn.Phase = Phase;
	Client->OfficialRoomReturn.ReadyRequestCommitted = false;
	Client->OfficialRoomReturn.StartedTick = nowTick;
	Client->OfficialRoomReturn.Action62Tick = 0;
	Client->OfficialRoomReturn.ReadyRequestTick = 0;
	Client->OfficialRoomReturn.RoomID = Room->ID;
	Client->LastRoomReturnTick = nowTick;
	QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-BEGIN] Reason=%s Uin=%u Kind=%u Phase=%u RoomID=%u Seat=%d ReadyState=%d startStatus=%u\n",
		reason ? reason : "", Client->Uin, (unsigned)Kind, (unsigned)Phase,
		(unsigned)Room->ID, (int)Client->SeatID, (int)Client->ReadyState,
		(unsigned)Room->startStatus);
}

static bool ResolveOfficialRoomReturnContext(
	const std::shared_ptr<ClientNode>& Client,
	std::shared_ptr<RoomNode>& Room,
	const char** rejectReason)
{
	if (rejectReason)
		*rejectReason = "ok";
	if (!Client)
	{
		if (rejectReason)
			*rejectReason = "missing-client";
		return false;
	}

	const OfficialRoomReturnFlowState& flow = Client->OfficialRoomReturn;
	if (!IsOfficialRoomReturnKind(flow.Kind) || !IsOfficialRoomReturnPhase(flow.Phase) ||
		flow.RoomID == 0 || flow.StartedTick == 0)
	{
		if (rejectReason)
			*rejectReason = "invalid-flow-state";
		return false;
	}
	if ((GetTickCount() - flow.StartedTick) >= QQS_OFFICIAL_ROOM_RETURN_TIMEOUT_MS)
	{
		if (rejectReason)
			*rejectReason = "expired";
		return false;
	}
	if (Client->RoomID != flow.RoomID)
	{
		if (rejectReason)
			*rejectReason = "client-room-mismatch";
		return false;
	}

	Room = GetRoom(flow.RoomID);
	if (!Room || Room->ID != flow.RoomID || Client->CurrentRoom != Room.get())
	{
		if (rejectReason)
			*rejectReason = "room-context-mismatch";
		return false;
	}

	const int seatLimit = GetRoomSeatLimit(Room);
	const int seat = (int)Client->SeatID;
	if (seat < 0 || seat >= seatLimit || seat >= (int)Room->Player.size() || Room->Player[seat] != Client)
	{
		if (rejectReason)
			*rejectReason = "authoritative-seat-mismatch";
		return false;
	}
	return true;
}

bool AdvanceOfficialRoomReturnFlow(
	std::shared_ptr<ClientNode> Client,
	std::shared_ptr<RoomNode> Room,
	BYTE ExpectedPhase,
	BYTE NextPhase,
	const char* reason)
{
	if (!Client || !Room || !IsOfficialRoomReturnPhase(ExpectedPhase) || !IsOfficialRoomReturnPhase(NextPhase))
		return false;

	std::shared_ptr<RoomNode> contextRoom;
	const char* rejectReason = "unknown";
	if (!ResolveOfficialRoomReturnContext(Client, contextRoom, &rejectReason) || contextRoom != Room)
	{
		QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-ADVANCE-REJECT] Reason=%s Reject=%s Uin=%u Kind=%u Phase=%u Expected=%u Next=%u ClientRoomID=%u FlowRoomID=%u\n",
			reason ? reason : "", rejectReason, Client->Uin,
			(unsigned)Client->OfficialRoomReturn.Kind,
			(unsigned)Client->OfficialRoomReturn.Phase,
			(unsigned)ExpectedPhase, (unsigned)NextPhase,
			(unsigned)Client->RoomID, (unsigned)Client->OfficialRoomReturn.RoomID);
		ClearOfficialRoomReturnFlow(Client, rejectReason);
		return false;
	}
	if (Client->OfficialRoomReturn.Phase != ExpectedPhase)
	{
		QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-ADVANCE-REJECT] Reason=%s Reject=phase-mismatch Uin=%u Kind=%u Phase=%u Expected=%u Next=%u RoomID=%u\n",
			reason ? reason : "", Client->Uin,
			(unsigned)Client->OfficialRoomReturn.Kind,
			(unsigned)Client->OfficialRoomReturn.Phase,
			(unsigned)ExpectedPhase, (unsigned)NextPhase, (unsigned)Room->ID);
		return false;
	}

	Client->OfficialRoomReturn.Phase = NextPhase;
	QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-ADVANCE] Reason=%s Uin=%u Kind=%u RoomID=%u Phase=%u->%u\n",
		reason ? reason : "", Client->Uin,
		(unsigned)Client->OfficialRoomReturn.Kind, (unsigned)Room->ID,
		(unsigned)ExpectedPhase, (unsigned)NextPhase);
	return true;
}

bool TryHandleOfficialRoomReturnAction62(std::shared_ptr<ClientNode> Client)
{
	if (!Client || Client->OfficialRoomReturn.Kind == QQS_OFFICIAL_ROOM_RETURN_KIND_NONE)
		return false;

	std::shared_ptr<RoomNode> Room;
	const char* rejectReason = "unknown";
	if (!ResolveOfficialRoomReturnContext(Client, Room, &rejectReason))
	{
		QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-AT62-INVALID] Reject=%s Uin=%u Kind=%u Phase=%u ClientRoomID=%u FlowRoomID=%u\n",
			rejectReason, Client->Uin,
			(unsigned)Client->OfficialRoomReturn.Kind,
			(unsigned)Client->OfficialRoomReturn.Phase,
			(unsigned)Client->RoomID,
			(unsigned)Client->OfficialRoomReturn.RoomID);
		ClearOfficialRoomReturnFlow(Client, rejectReason);
		return false;
	}

	if (Room->startStatus != 1)
	{
		QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-AT62-DEFER] Uin=%u Kind=%u RoomID=%u Phase=%u startStatus=%u AgeMs=%u -> wait room commit\n",
			Client->Uin, (unsigned)Client->OfficialRoomReturn.Kind,
			(unsigned)Room->ID, (unsigned)Client->OfficialRoomReturn.Phase,
			(unsigned)Room->startStatus,
			(unsigned)(GetTickCount() - Client->OfficialRoomReturn.StartedTick));
		return true;
	}

	const BYTE phase = Client->OfficialRoomReturn.Phase;
	if (phase == QQS_OFFICIAL_ROOM_RETURN_PHASE_SERVER_CHAIN_PENDING)
	{
		// 自定义/边境必须先完成 104/500/504/609，官服客户端不会在该阶段抢跑 AT=62。
		// 此处消费越序包但不伪造成功，避免回落到 full scheme 响应破坏回房对象树。
		QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-AT62-DEFER] Uin=%u Kind=%u RoomID=%u Phase=%u AgeMs=%u -> wait server room chain\n",
			Client->Uin, (unsigned)Client->OfficialRoomReturn.Kind,
			(unsigned)Room->ID, (unsigned)phase,
			(unsigned)(GetTickCount() - Client->OfficialRoomReturn.StartedTick));
		return true;
	}

	if (phase == QQS_OFFICIAL_ROOM_RETURN_PHASE_WAIT_AT62_SEND_ROSTER)
	{
		// 官服赛后回房顺序：C24640(AT=62) -> S504/S609(self) -> S24640 ACK。
		NotifyOfficialReturnSelfReadyObState(Client, Room, "room-return-at62");
	}
	else if (phase != QQS_OFFICIAL_ROOM_RETURN_PHASE_WAIT_AT62_ACK_ONLY &&
		phase != QQS_OFFICIAL_ROOM_RETURN_PHASE_AT62_COMPLETE)
	{
		ClearOfficialRoomReturnFlow(Client, "at62-invalid-phase");
		return false;
	}

	Client->OfficialRoomReturn.Action62Tick = GetTickCount();
	Client->OfficialRoomReturn.Phase = QQS_OFFICIAL_ROOM_RETURN_PHASE_AT62_COMPLETE;
	ResponsePlayerActionParaAck(Client, 62);
	QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-AT62-ACK] Uin=%u Kind=%u RoomID=%u OldPhase=%u NewPhase=%u ReadyRequestCommitted=%d ReadyState=%d AgeMs=%u\n",
		Client->Uin, (unsigned)Client->OfficialRoomReturn.Kind, (unsigned)Room->ID,
		(unsigned)phase, (unsigned)Client->OfficialRoomReturn.Phase,
		Client->OfficialRoomReturn.ReadyRequestCommitted ? 1 : 0,
		(int)Client->ReadyState,
		(unsigned)(GetTickCount() - Client->OfficialRoomReturn.StartedTick));
	return true;
}

bool CanAcceptOfficialRoomReturnReadyChange(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room)
{
	if (!Client || Client->OfficialRoomReturn.Kind == QQS_OFFICIAL_ROOM_RETURN_KIND_NONE)
		return true;

	std::shared_ptr<RoomNode> contextRoom;
	const char* rejectReason = "unknown";
	if (!ResolveOfficialRoomReturnContext(Client, contextRoom, &rejectReason) || contextRoom != Room)
	{
		QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-109-CONTEXT-RESET] Reject=%s Uin=%u Kind=%u Phase=%u ClientRoomID=%u FlowRoomID=%u\n",
			rejectReason, Client->Uin,
			(unsigned)Client->OfficialRoomReturn.Kind,
			(unsigned)Client->OfficialRoomReturn.Phase,
			(unsigned)Client->RoomID,
			(unsigned)Client->OfficialRoomReturn.RoomID);
		ClearOfficialRoomReturnFlow(Client, rejectReason);
		return true;
	}

	if (Client->OfficialRoomReturn.Phase != QQS_OFFICIAL_ROOM_RETURN_PHASE_AT62_COMPLETE)
	{
		// C109 是独立的房间状态请求。返房事务只约束 104/500/504/609/AT=62
		// 的包序，不能因为 AT=62 尚未到达就丢弃客户端已经发出的真实准备请求。
		QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-109-ACCEPT] Uin=%u Kind=%u Phase=%u RoomID=%u AgeMs=%u PendingAT62=1\n",
			Client->Uin, (unsigned)Client->OfficialRoomReturn.Kind,
			(unsigned)Client->OfficialRoomReturn.Phase, (unsigned)Room->ID,
			(unsigned)(GetTickCount() - Client->OfficialRoomReturn.StartedTick));
	}
	return true;
}

void CompleteOfficialRoomReturnReadyChange(std::shared_ptr<ClientNode> Client, const char* reason)
{
	if (!Client || Client->OfficialRoomReturn.Kind == QQS_OFFICIAL_ROOM_RETURN_KIND_NONE)
		return;
	const BYTE oldPhase = Client->OfficialRoomReturn.Phase;
	Client->OfficialRoomReturn.ReadyRequestCommitted = true;
	Client->OfficialRoomReturn.ReadyRequestTick = GetTickCount();
	QQS_DebugPrintf10001("[FLOW][ROOM-RETURN-109-COMMIT] Reason=%s Uin=%u Kind=%u RoomID=%u Phase=%u ReadyRequestCommitted=1 ReadyState=%d\n",
		reason ? reason : "", Client->Uin,
		(unsigned)Client->OfficialRoomReturn.Kind,
		(unsigned)Client->OfficialRoomReturn.RoomID,
		(unsigned)oldPhase,
		(int)Client->ReadyState);
}

void RequestChangeReadyState(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	// OnRecvFromClient 传入的是 MSGHEAD::BodyLen（包含其自身 2B），Body 已指向 payload。
	constexpr size_t kRequestPayloadLen = sizeof(UINT) + sizeof(UINT) + sizeof(BYTE);
	constexpr size_t kRequestWireBodyLen = sizeof(USHORT) + kRequestPayloadLen;
	if (!Client || !Body || BodyLen < kRequestWireBodyLen)
	{
		QQS_DebugPrintf10001("[FLOW][109-REJECT] Reason=invalid-structure Uin=%u Body=%p BodyLen=%zu NeedWireBodyLen=%zu\n",
			Client ? Client->Uin : 0, Body, BodyLen, kRequestWireBodyLen);
		return;
	}

	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);
	BYTE ReadyStateValue = Read8(Body);
	if (Uin != Client->Uin || ReadyStateValue > QQS_ROOM_READY_STATE_NOT_READY)
	{
		QQS_DebugPrintf10001("[FLOW][109-REJECT] Reason=%s Uin=%u ReqUin=%u ReadyState=%u Time=%u BodyLen=%zu\n",
			Uin != Client->Uin ? "uin-mismatch" : "invalid-ready-state",
			Client->Uin, Uin, (unsigned)ReadyStateValue, Time, BodyLen);
		return;
	}
	std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
	if (!Room)
	{
		QQS_DebugPrintf10001("[FLOW][109-REJECT] Reason=missing-room Uin=%u ReqUin=%u ClientRoomID=%u ReadyState=%u Time=%u BodyLen=%zu\n",
			Client->Uin, Uin, Client->RoomID, (unsigned)ReadyStateValue, Time, BodyLen);
		return;
	}
	if (Room->startStatus != 1)
	{
		// 加载/比赛阶段不能回一个“成功”但不落权威状态的 109；那会直接制造 UI/服务端分叉。
		QQS_DebugPrintf10001("[FLOW][109-REJECT] Reason=room-not-idle Uin=%u ReqUin=%u RoomID=%u Seat=%d ReadyState=%u startStatus=%u Waiting=%d Dispatching=%d RaceBeginSent=%d LastS2C=%u Time=%u BodyLen=%zu\n",
			Client->Uin, Uin, Room->ID, (int)Client->SeatID,
			(unsigned)ReadyStateValue, (unsigned)Room->startStatus,
			Room->CustomWaitingRaceBegin ? 1 : 0,
			Room->CustomLoadingDispatching ? 1 : 0,
			Room->CustomRaceBeginSent ? 1 : 0,
			(unsigned)Room->CustomLastSentMsgID,
			Time, BodyLen);
		return;
	}
	if (!CanAcceptOfficialRoomReturnReadyChange(Client, Room))
		return;

	const int seatLimit = GetRoomSeatLimit(Room);
	int authoritativeSeat = -1;
	const int clientSeat = (int)Client->SeatID;
	if (clientSeat >= 0 && clientSeat < seatLimit &&
		clientSeat < (int)Room->Player.size() &&
		Room->Player[clientSeat] == Client)
	{
		authoritativeSeat = clientSeat;
	}
	else
	{
		for (int i = 0; i < seatLimit && i < (int)Room->Player.size(); ++i)
		{
			if (Room->Player[i] == Client)
			{
				authoritativeSeat = i;
				break;
			}
		}
	}
	if (authoritativeSeat < 0 || Client->RoomID != Room->ID || Client->CurrentRoom != Room.get())
	{
		QQS_DebugPrintf10001("[FLOW][109-REJECT] Reason=authoritative-seat-mismatch Uin=%u ReqUin=%u RoomID=%u ClientRoomID=%u Seat=%d ReadyState=%u SeatLimit=%d CurrentRoomOK=%d\n",
			Client->Uin, Uin, Room->ID, Client->RoomID, (int)Client->SeatID,
			(unsigned)ReadyStateValue, seatLimit, Client->CurrentRoom == Room.get() ? 1 : 0);
		return;
	}
	Client->SeatID = (char)authoritativeSeat;

	// Room->Player[seat] 是开局检查读取的权威对象。先提交它，再发 S109 和给其它成员的 S504。
	std::shared_ptr<ClientNode> AuthoritativeClient = Room->Player[authoritativeSeat];
	AuthoritativeClient->ReadyState = ReadyStateValue;
	ResponseChangeReadyState(Client, ReadyStateValue);

	int notifyCount = 0;
	for (char i = 0; i < seatLimit && i < (char)Room->Player.size(); ++i)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (!RoomClient || RoomClient == AuthoritativeClient)
			continue;
		if (RoomClient->deleteflag || !RoomClient->IsLogin || RoomClient->Uin == 0)
			continue;
		if (RoomClient->RoomID != Room->ID || RoomClient->CurrentRoom != Room.get() || RoomClient->SeatID != i)
			continue;
		NotifyChangeReadyState(RoomClient, AuthoritativeClient->Uin, ReadyStateValue);
		notifyCount++;
	}
	CompleteOfficialRoomReturnReadyChange(Client, "109-ready-state-committed");
	QQS_DebugPrintf10001("[FLOW][109-READY-STATE-COMMIT] Uin=%u ReqUin=%u RoomID=%u Seat=%d ReadyState=%u AuthoritativeReadyState=%d Notify504=%d SeatLimit=%d Time=%u BodyLen=%zu\n",
		Client->Uin, Uin, Room->ID, authoritativeSeat,
		(unsigned)ReadyStateValue, (int)AuthoritativeClient->ReadyState,
		notifyCount, seatLimit, Time, BodyLen);
}
void ResponseChangeReadyState(std::shared_ptr<ClientNode> Client, BYTE ReadyState)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, 0); //ResultID

	Write8(p, ReadyState);

	Write8(p, 0); //ReasonLen


	len = p - buf.data();
	SendToClient(Client, 109, buf.data(), len, Client->RoomID, FE_ROOM, 1, Response);
}


// 自定义函数，将字符串分割成两部分，返回后面部分的UINT值
uint32_t splitAndConvert(const std::string& content) {
	// 寻找分割点
	size_t pos = content.find("kick");
	if (pos == std::string::npos) {
		// 如果找不到 "kick"，则返回默认值或者抛出异常，视情况而定
		return 0; // 这里返回0作为示例，你可以根据实际情况选择其他处理方式
	}

	// 提取后面的部分
	std::string secondPart = content.substr(pos + 4); // "+4" 是因为 "kick" 长度为4

	// 将后面的部分转换为UINT
	uint32_t value;
	std::istringstream iss(secondPart);
	iss >> value;
	return value;
}


bool isNumeric(const std::string& str) {
	// 定义匹配的正则表达式
	std::regex pattern("^[0-9]{5,6}$");

	// 进行匹配
	return std::regex_match(str, pattern);
}


bool IsStrictItemIdContent(const char* data, USHORT len, UINT* outItemId)
{
	if (!data) return false;
	if (len != 5 && len != 6) return false;

	UINT value = 0;
	for (USHORT i = 0; i < len; i++)
	{
		unsigned char c = static_cast<unsigned char>(data[i]);
		if (c < '0' || c > '9') return false;
		value = value * 10 + (c - '0');
	}

	if (outItemId)
	{
		*outItemId = value;
	}
	return true;
}

std::string getDayOfWeekString(int dayOfWeek) {
	switch (dayOfWeek) {
	case 0: return "星期日";
	case 1: return "星期一";
	case 2: return "星期二";
	case 3: return "星期三";
	case 4: return "星期四";
	case 5: return "星期五";
	case 6: return "星期六";
	default: return "Unknown";
	}
}


bool isInteger(const char* str) {
	if (str == nullptr || *str == '\0') {
		return false;
	}

	// 处理可能的负号
	if (*str == '-') {
		str++;
		if (*str == '\0') return false; // 只包含负号的情况
	}

	// 检查剩余的字符是否全是数字
	while (*str) {
		if (!std::isdigit(static_cast<unsigned char>(*str))) {
			return false;
		}
		str++;
	}

	return true;
}


void TalkStake(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, char* Content) {
	try
	{
		char* Num = Content;
		Num += 4;
		if (isInteger(Num)) {
			int Mynum = PlayerDB_GetItemNum(Client->Uin, Stake[1]);
			if (Mynum < atoi(Num)) {
				std::string msg = GetItemName(Stake[1]);
				msg = "下注道具目前设置为/C01【" + msg + "】/C00您的数量只有/C01【" + std::to_string(Mynum) + "】/C00道具不足！";
				ResponseStartGame(Client, msg.c_str());
				return;
			}
			Client->IsStake = true;
			Client->StakeNum = atoi(Num);
			Room->IsStake = 1;
			ResponseStartGame(Client, "下注成功，请通知对方一起下注相同数量，否则无法开始游戏！");
		}
		else {
			ResponseStartGame(Client, "下注格式异常，只允许输入数字！");
			return;
		}

	}
	catch (...)
	{
		printf("赌注异常\n");
	}
}

void TalkMantissa(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, char* Content) {
	try
	{
		std::string str(Content);
		std::string thirdPart;
		std::string msg;

		
		// 检查 Content 的长度是否满足要求
		if (str.length() >= 6) { // 假设至少需要 6 个字符
			// 提取第三个部分（假设第三个字符就是我们需要的部分）
			thirdPart = str.substr(4, 2); // 从索引 3 开始提取 1 个字符
		}

		if (!thirdPart.empty()) {
			if (thirdPart == "单") {
				Client->Mantissa = 1;
			}
			else if (thirdPart == "双")
			{
				Client->Mantissa = 2;
			}
			else if (thirdPart == "大") {
				Client->Mantissa = 3;
			}
			else if (thirdPart == "小") {
				Client->Mantissa = 4;
			}
			else {
				msg = "尾数格式异常，可以输入/C01尾数单20，尾数双20，尾数大20，尾数小20/C00";
				ResponseStartGame(Client, msg.c_str());
				return;
			}
		}
		else {
			msg = "尾数格式异常，可以输入/C01尾数单20，尾数双20，尾数大20，尾数小20/C00";
			ResponseStartGame(Client, msg.c_str());
			return;
		}

		char* Num = Content;
		Num += 6;
		if (isInteger(Num)) {
			int itemnum = atoi(Num);
			if (itemnum < Mantissa[1] || itemnum > Mantissa[2]) {
				msg = "下注数量不在范围之内，最低/C01" + std::to_string(Mantissa[1]) + "/C00最高/C01" + std::to_string(Mantissa[2]);
				ResponseStartGame(Client, msg.c_str());
				return;
			}

			int Mynum = PlayerDB_GetItemNum(Client->Uin, Mantissa[3]);
			if (Mynum < itemnum) {
				msg = GetItemName(Mantissa[3]);
				msg = "尾数下注道具目前设置为/C01【" + msg + "】/C00您的数量只有/C01【" + std::to_string(Mynum) + "】/C00道具不足！";
				ResponseStartGame(Client, msg.c_str());
				return;
			}
			Client->MantissaNum = itemnum;
			ResponseStartGame(Client, "尾数下注成功！");
		}
		else {
			msg = "尾数格式异常，可以输入/C01尾数单20，尾数双20，尾数大20，尾数小20/C00";
			ResponseStartGame(Client, msg.c_str());
			return;
		}

	}
	catch (...)
	{
		printf("尾数异常\n");
	}
}









void TalkBottomGame(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, char* Content) {
	try
	{
		//d::string Content = "开启2人扣数量10";
		int playerCount = 0;
		int damageAmount = 0;

		// 将 char* 转换为 std::string
		std::string strContent(Content);

		// 使用正则表达式匹配特定格式
		std::regex regexPattern(R"(开启(\d+)人扣数量(\d+))");
		std::smatch matches;

		// 尝试匹配内容
		if (std::regex_match(strContent, matches, regexPattern)) {
			// 提取玩家数量和扣血数量
			playerCount = std::stoi(matches[1]);
			damageAmount = std::stoi(matches[2]);

			if (playerCount > 0 && damageAmount > 0) {
				//// 输出结果
				//std::cout << "玩家数量: " << playerCount << std::endl;
				//std::cout << "扣血数量: " << damageAmount << std::endl;
				Room->IsBottomGame = 3;
				Room->BottomGameNum = damageAmount;
				Room->BottomPlayerNum = playerCount;
			}
		}
		else {
			ResponseStartGame(Client, "垫底格式异常！");
		}


	}
	catch (...)
	{
		printf("垫底说话异常\n");
	}
}

std::string extractQQ(const char* content) {
	// 找到 "绑定qq" 的位置
	const char* prefix = "绑定qq";
	const char* start = std::strstr(content, prefix);

	// 如果没有找到返回空字符串
	if (!start) return "";

	// 移动指针到 QQ 号的起始位置
	start += std::strlen(prefix);

	// 读取数字直到遇到非数字字符或达到十位
	std::string qq;
	while (std::isdigit(*start) && qq.size() < 10) {
		qq += *start;
		start++;
	}

	return qq;
}


// 函数：将时间字符串转换为 time_t 类型
std::time_t stringToTimeT(const std::string& timeStr) {
	std::tm tm = {};
	std::istringstream ss(timeStr);
	ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
	return std::mktime(&tm);
}

// 函数：将 time_t 类型的时间转换为字符串
std::string timeTToString(std::time_t time) {
	struct tm* tm_now;
	localtime_s(tm_now, &time);
	std::ostringstream oss;
	oss << std::put_time(tm_now, "%Y-%m-%d %H:%M:%S");
	return oss.str();
}

bool contains(const std::string& str, const std::string& substr) {
	return str.find(substr) != std::string::npos;
}

static bool PermissionMessContains(const std::string& allowedMess, const std::string& itemMess)
{
	// itemMess 为空时，std::string::find("") 会恒为 true，之前会导致分类 GM 校验被误放行。
	return !allowedMess.empty() && !itemMess.empty() && contains(allowedMess, itemMess);
}

static bool PermissionItemAllowedByMess(const std::string& allowedMess, const std::string& itemMess)
{
	return PermissionMessContains(allowedMess, itemMess);
}

static bool BlockGmItemByVersionLimit(
	std::shared_ptr<ClientNode> Client,
	UINT itemid,
	const GmUinLimit* limit,
	UINT fallbackMaxItemID,
	const char* roleName)
{
	const UINT maxId = limit ? limit->MaxItemID : fallbackMaxItemID;
	if (limit && limit->DenyItems.count(itemid))
	{
		std::string tips = std::string(roleName) + "不能刷物品" + std::to_string(itemid) + "！";
		ResponseStartGame(Client, tips.c_str());
		return true;
	}
	if (maxId > 0 && itemid >= maxId)
	{
		std::string tips = std::string(roleName) + "只能刷" + std::to_string(maxId) + "以下物品！";
		if (limit && !limit->VersionName.empty() && limit->VersionName != "default")
			tips = std::string(roleName) + "(" + limit->VersionName + ")只能刷" + std::to_string(maxId) + "以下物品！";
		ResponseStartGame(Client, tips.c_str());
		return true;
	}
	return false;
}

std::string getTimeDifferenceString(std::time_t futureTime, std::time_t currentTime) {
	double diffSeconds = std::difftime(futureTime, currentTime);

	int days = static_cast<int>(diffSeconds) / 86400;
	diffSeconds -= days * 86400;
	int hours = static_cast<int>(diffSeconds) / 3600;
	diffSeconds -= hours * 3600;
	int minutes = static_cast<int>(diffSeconds) / 60;

	std::ostringstream result;
	result << days << " 天, " << hours << " 小时, " << minutes << " 分钟";
	return result.str();
}

void RequestTalk(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	//请求说话
	UINT Uin = Read32(Body);               // 用户ID
	UINT Time = Read32(Body);              // 时间戳
	BYTE Type = Read8(Body);               // 消息类型
	BYTE ClientUseFlag = Read8(Body);      // 客户端使用标识
	UINT DestPlayerUin = Read32(Body);     // 目标玩家的用户ID

	USHORT ContentLength = Read16(Body);   // 消息内容长度
	char* Content = (char*)Body;           // 消息内容
	Body += ContentLength;
	//printf("Content:%s\n", Content);
	BYTE BugletType = Read8(Body);         // 喇叭类型
	BYTE ReserveFlag = Read8(Body);        // 保留标识

	// Debug only: ASCII-safe output to avoid console encoding issues.
	// Remove this block after confirming Type/Flag/Uin/permission state.
	if (Client->Uin == 10001)
	{
		std::string dbgContent(Content, ContentLength);
		std::string dbgPreview = dbgContent;
		for (auto& ch : dbgPreview)
		{
			unsigned char c = static_cast<unsigned char>(ch);
			if (c < 0x20 || c > 0x7E)
			{
				ch = '.';
			}
		}

		bool dbgNumeric = isNumeric(dbgContent);
		UINT dbgItemId = 0;
		UINT dbgItemType = 0;
		std::string dbgMess;
		if (dbgNumeric)
		{
			dbgItemId = static_cast<UINT>(std::strtoul(dbgContent.c_str(), nullptr, 10));
			dbgItemType = GetItemType(dbgItemId);
			dbgMess = GetItemMess(dbgItemId);
		}

		if (QQS_ShouldPrintDebugUin10001(Uin) || QQS_ShouldPrintDebugUin10001(Client->Uin))
		{
			printf("[TalkDBG] PacketUin=%u ClientUin=%u Type=%u Flag=%u Len=%u Content='%s' gm=%u admin=%u Clothes=%llu Car=%llu NewClothes=%llu NewCar=%llu NewClothesCar=%llu CfgAdmin=%llu Numeric=%d ItemId=%u ItemType=%u MessLen=%llu\n",
				Uin,
				Client->Uin,
				(unsigned int)Type,
				(unsigned int)ClientUseFlag,
				(unsigned int)ContentLength,
				dbgPreview.c_str(),
				Client->gm,
				Client->admin,
				(unsigned long long)ClothesGMUins.count(Client->Uin),
				(unsigned long long)CarGMUins.count(Client->Uin),
				(unsigned long long)NewClothesGMUins.count(Client->Uin),
				(unsigned long long)NewCarGMUins.count(Client->Uin),
				(unsigned long long)NewClothesCarGMUins.count(Client->Uin),
				(unsigned long long)AdminUins.count(Client->Uin),
				dbgNumeric ? 1 : 0,
				dbgItemId,
				dbgItemType,
				(unsigned long long)dbgMess.size());

			printf("[TalkDBG_CONTENT_HEX]");
			for (unsigned char c : dbgContent)
			{
				printf(" %02X", c);
			}
			printf("\n");

			if (!dbgMess.empty())
			{
				printf("[TalkDBG_MESS_HEX]");
				for (unsigned char c : dbgMess)
				{
					printf(" %02X", c);
				}
				printf("\n");
			}
		}
	}

	// 检查用户是否被禁言
	if (Client->Speaking) {
		ResponseStartGame(Client, "你已经被禁言！");
		return;
	}

	// 管理员发送物品指令
	if (strncmp(Content, "give", 4) == 0 && Client->admin == 1) {
		if (SendAnyItem(Client, Content) > 0) {
			ResponseStartGame(Client, "赠送成功！");
		}
		return;
	}
	// 管理员踢出指定玩家
	if (Client->admin == 1) {
		UINT DestUin = splitAndConvert(Content);
		if (DestUin > 0 && DestUin != Client->Uin)
		{
			std::shared_ptr<ClientNode> DestClient = GetClient(DestUin);
			if (DestClient) {
				if (DestUin == 10001 || DestUin == 10002 || DestUin == 10003) {
					ResponseStartGame(Client, "禁止踢管理下线!");
					return;
				}
				else
				{
					NotifyKickPlayer(DestClient, "你已经被管理员踢下线", 3);
					//GetClient(DestUin)->deleteflag = 1;
					DestClient->IsLogin = FALSE;
					DestClient->Server->Disconnect(DestClient->TransportConnID);
					DestClient->deleteflag = 1;
					return;
				}

			}
		}
	}

	// 处理退出游戏请求
	if (Type == 1 && ClientUseFlag == 0 && _stricmp(Content, "g") == 0)
	{
		if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
			return;
		}
		//手动发送GameOver消息
		/*if (Client->TaskID == 0xFFFE)
		{ //极限挑战强制退出
			Client->IsFinish = TRUE;
			HANDLE over = CreateThread(NULL, 0, OnSingleOver, Client, 0, NULL);
			if (over)
			{
				CloseHandle(over);
			}
		}*/
		if ((Client->TaskID == 0 || Client->TaskID == 0xFFFD || Client->TaskID == 0xFFFC || Client->TaskID == 0xFFFB) && Client->GameID != 0)
		{//排位 边境 竞速快速退出
			QuickbugLeaveGame(Client);
		}
		return;
	}
	//ResponseTalk(Client, Type, ClientUseFlag, DestPlayerUin);

	// 喇叭消息处理
	else if (Type == 3)
	{
		const UINT ItemID = QQS_ResolveBugletItemId(BugletType);
		if (ItemID == 0)
		{
			ResponseStartGame(Client, "不支持的喇叭类型!");
			return;
		}

		if (isDBtHasItem(Client, ItemID, 1))
		{
			const auto contentId = QQS_MakeTalkContentId();
			std::shared_ptr<RoomNode> Room = NULL;
			if (Client->TaskID == 0xFFFD || Client->TaskID == 0xFFFC)
				Room = GetBorderRoom(Client->BorderRoomID);
			else
				Room = GetRoom(Client->RoomID);

			const BYTE notifyBugletType = QQS_NormalizeNotifyBugletType(Type, BugletType);
			if (Room)
			{
				for (char i = 0; i < 6; ++i)
				{
					std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
					if (RoomClient)
					{
						NotifyTalk(RoomClient, Client, Type, ClientUseFlag, DestPlayerUin,
							notifyBugletType, ContentLength, Content, contentId.data());
					}
				}
			}
			NotifyUseItemResult2(Client, ItemID, 1);
			ResponseTalk(Client, Type, ClientUseFlag, DestPlayerUin, notifyBugletType);
			NotifyBugletTalkInternal(Client, Content, ContentLength, BugletType, contentId, true);
		}
		else
		{
			ResponseStartGame(Client, "喇叭数量不足!");
		}
		return;
	}
	//printf("Type:%d, ClientUseFlag:%d. DestPlayerUin:%d BugletType:%d\n", Type, ClientUseFlag, DestPlayerUin, BugletType);
	//房间内说话Type:4, ClientUseFlag : 2
	//对局内说话Type:1, ClientUseFlag:0
	// 房间内聊天或对局聊天
	else if (Type == 1 || Type == 2 || Type == 4)
	{
		std::shared_ptr<RoomNode> Room = NULL;
		if (Client->TaskID == 0xFFFD || Client->TaskID == 0xFFFC) {
			Room = GetBorderRoom(Client->BorderRoomID);
		}
		else {
			Room = GetRoom(Client->RoomID);
		}
		if (!Room)
		{
			return;
		}

		// 处理房间内特定指令
		if (ClientUseFlag == 2) {
			// 切换观战模式
			if (strcmp(Content, "obs") == 0) {
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				if (Client->OBState == 1) {
					ResponseStartGame(Client, "观战模式关闭");
					Client->OBState = 0;
				}
				else {
					ResponseStartGame(Client, "观战模式开启");
					Client->OBState = 1;
				}
				ResponseChangeOBState(Client, Client->OBState); //134

				std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
				if (Room)
				{

					for (char i = 0; i < 6; i++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
						if (RoomClient && RoomClient != Client)
						{
							NotifyChangeOBState(RoomClient, Client->OBState, Client->Uin); //609
						}
					}
					return;
				}
				return;
			}

			// 切换房间地图
			if (Type == 1 && strstr(Content, "map"))
			{
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				//自定义切换地图
				char* mapid = Content;
				mapid += 4;
				Room->MapID = atoi(mapid);
				for (char i = 0; i < 6; i++)
				{
					std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
					if (RoomClient)
					{
						NotifyChangeMap(RoomClient, RoomClient->Uin, Room);
					}
				}
				return;
			}

			// 各种模式开关控制
			if (Type == 1 && strstr(Content, "关闭跳舞颁奖") && Client->skipFlag == 1)
			{
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				Room->isSkip = 1;
				for (char j = 0; j < 6; j++)
				{
					std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
					if (RoomClient)
					{
						ResponseStartGame(RoomClient, "房主已关闭跳舞颁奖流程!");
					}
				}
				return;

			}

			if (Type == 1 && strstr(Content, "开启比赛模式"))
			{
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				if (IsRoomConnOwner(Room, Client)) {
					Room->Israce = 1;
					for (char j = 0; j < 6; j++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
						if (RoomClient)
						{
							RoomClient->isRace = 1;
							ResponseStartGame(RoomClient, "房主已开启比赛模式!");
						}
					}
					return;
				}

			}

			if (Type == 1 && strstr(Content, "关闭比赛模式"))
			{
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				if (IsRoomConnOwner(Room, Client)) {
					Room->Israce = 0;
					for (char j = 0; j < 6; j++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
						if (RoomClient)
						{
							RoomClient->isRace = 0;
							ResponseStartGame(RoomClient, "房主已关闭比赛模式!");
						}
					}
					return;
				}

			}

			if (Type == 1 && strstr(Content, "开启匿名模式"))
			{
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				if (IsRoomConnOwner(Room, Client)) {
					Room->IsAnonymous = 1;
					for (char j = 0; j < 6; j++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
						if (RoomClient)
						{
							RoomClient->IsAnonymous = 1;
							ResponseStartGame(RoomClient, "房主已开启匿名模式!");
						}
					}
					return;
				}

			}

			if (Type == 1 && strstr(Content, "关闭匿名模式"))
			{
				if (IsSuperMoneyBottomGameMode(Room->IsBottomGame)) {
					ResponseStartGame(Client, "点券垫底赛开启期间不能单独关闭匿名模式，请先关闭垫底赛！");
					return;
				}
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				if (IsRoomConnOwner(Room, Client)) {
					Room->IsAnonymous = 0;
					for (char j = 0; j < 6; j++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
						if (RoomClient)
						{
							RoomClient->IsAnonymous = 0;
							ResponseStartGame(RoomClient, "房主已关闭匿名模式!");
						}
					}
					return;
				}

			}
			try
			{    // 检查GM权限和输入内容的合法性
				bool isNewClothesCategoryGM = NewClothesGMUins.count(Client->Uin) > 0;
				bool isNewCarCategoryGM = NewCarGMUins.count(Client->Uin) > 0;
				bool isNewClothesCarCategoryGM = NewClothesCarGMUins.count(Client->Uin) > 0;
				// 列表优先：只要 UIN 写进分类 GM，就算 BaseInfo.gm > 0 也必须按该列表限制。
				bool isClothesCategoryGM = ClothesGMUins.count(Client->Uin) > 0;
				bool isCarCategoryGM = CarGMUins.count(Client->Uin) > 0;
				bool isYamlCategoryGM = isNewClothesCarCategoryGM || isNewClothesCategoryGM || isNewCarCategoryGM || isClothesCategoryGM || isCarCategoryGM;
				// AdminUins 作为配置文件全GM：不需要数据库 BaseInfo.gm，也不走分类限制。
				bool isConfigFullGM = AdminUins.count(Client->Uin) > 0;
				if (Type == 1 && (Client->gm > 0 || Client->admin == 1 || isConfigFullGM || isYamlCategoryGM)) {
					// Parse a strict 5-6 digit item id using ContentLength only.
					UINT itemid = 0;
					if (IsStrictItemIdContent(Content, ContentLength, &itemid)) {
						if (LimitTalk && !(Client->gm > 0 || Client->admin > 0 || isConfigFullGM || isYamlCategoryGM)) {
							return;
						}
						BOOLEAN isweek = false;
						// 非管理员用户的额外检查
						if (Client->admin != 1) {
							
							TREASUREBOX* TreasureBox = GetTreasureBox(itemid);
							if (TreasureBox)
							{
								return;
							}
							// 检查物品是否是宝箱，是否被锁住，以及GM时间限制
							if (itemid == 62787) {
								return;
							}
							if (isLock(itemid)) {
								ResponseStartGame(Client, "物品被锁住！");
								return;
							}
							// GM时间限制条件
							if (!isYamlCategoryGM && !isConfigFullGM && GMTime[0] == 1) {
								// 获取当前系统时间
								auto now = std::chrono::system_clock::now();
								std::time_t now_time = std::chrono::system_clock::to_time_t(now);
								struct tm tm_now;
								localtime_s(&tm_now, &now_time);

								// 获取小时
								int hour = tm_now.tm_hour;
								// 获取当前是星期几，tm_wday 的范围是 0（星期日）到 6（星期六）
								std::string day_of_week = std::to_string(tm_now.tm_wday);
								std::string Reason = "GM开启时间在";
								// 将逗号分隔的星期几字符串分割成单独的值
								std::istringstream ss(week);
								std::string token;
								std::vector<std::string> tokens;

								while (std::getline(ss, token, ',')) {
									tokens.push_back(token);
								}

								for (const auto& day : tokens) {
									Reason.append(getDayOfWeekString(std::stoi(day)));
									if (day == day_of_week) {
										isweek = true;
									}
								}

								if (!(hour >= GMTime[1] && hour < GMTime[2] && isweek == true)) {
									Reason.append("的");
									Reason.append(std::to_string(GMTime[1]));
									Reason.append("点到");
									Reason.append(std::to_string(GMTime[2]));
									Reason.append("点，你小子等会吧!");
									ResponseStartGame(Client, Reason.c_str());
									return;
								}
							}

							// GM使用期限检查
							if (!isYamlCategoryGM && !isConfigFullGM && GMTime[3] > 0 && Client->gmtips == 0 && Client->gm != 1) {
								Client->gmtips = 1;
								std::time_t initialTime = stringToTimeT(Client->gmtime);
								// 将天数转换为秒数
								const int secondsInDay = 86400; // 1 天 = 86400 秒
								std::time_t futureTime = initialTime + GMTime[3] * secondsInDay;
								// 获取当前时间
								std::time_t currentTime = std::time(nullptr);
								std::string resultMessage;
								if (futureTime > currentTime) {
									std::string timeDifference = getTimeDifferenceString(futureTime, currentTime);
									resultMessage = "您的Gm使用期限还有：/C01" + timeDifference + "/C00，请及时续费！";
									ResponseStartGame(Client, resultMessage.c_str());
									return;
								}
								else {
									UserDB_ban(Client->Uin);
									NotifyKickPlayer(Client, "Gm时间到期，你已经被封号！", 3);
									memcpy(Client->Key, "", 16);
									//GetClient(DestUin)->deleteflag = 1;
									Client->IsLogin = FALSE;
									Client->Server->Disconnect(Client->TransportConnID);
									Client->deleteflag = 1;
									return;
								}

							}
							// 分类 GM 必须同时满足：ItemAllNew.Mess 命中配置；如果 MaxItemID>0，还必须低于 ID 上限。
							// 不能用 ItemType 绕过 Mess，否则配置了 MaxItemID 后会出现“只过 ID、不看 Mess”的放行。
							std::string itemMess = GetItemMess(itemid);
							if (isNewClothesCarCategoryGM) {
								if (!PermissionMessContains(NewClothesCarGMMess, itemMess)) {
									ResponseStartGame(Client, "新版衣服+赛车GM只能刷配置 Mess 允许的道具类型！");
									return;
								}
								if (BlockGmItemByVersionLimit(Client, itemid, FindGmUinLimit(NewClothesCarGMLimits, Client->Uin), NewClothesCarGMMaxItemID, "新版衣服+赛车GM")) {
									return;
								}
							}
							else if (isNewClothesCategoryGM) {
								if (!PermissionItemAllowedByMess(NewClothesGMMess, itemMess)) {
									ResponseStartGame(Client, "新版衣服GM只能刷配置 Mess 允许的衣服类道具！");
									return;
								}
								if (BlockGmItemByVersionLimit(Client, itemid, FindGmUinLimit(NewClothesGMLimits, Client->Uin), NewClothesGMMaxItemID, "新版衣服GM")) {
									return;
								}
							}
							else if (isNewCarCategoryGM) {
								if (!PermissionItemAllowedByMess(NewCarGMMess, itemMess)) {
									ResponseStartGame(Client, "新版赛车GM只能刷配置 Mess 允许的赛车类道具！");
									return;
								}
								if (BlockGmItemByVersionLimit(Client, itemid, FindGmUinLimit(NewCarGMLimits, Client->Uin), NewCarGMMaxItemID, "新版赛车GM")) {
									return;
								}
							}
							else if (isClothesCategoryGM) {
								if (!PermissionItemAllowedByMess(ClothesGMMess, itemMess)) {
									ResponseStartGame(Client, "您只能刷配置 Mess 允许的衣服类道具！");
									return;
								}
								if (BlockGmItemByVersionLimit(Client, itemid, FindGmUinLimit(ClothesGMLimits, Client->Uin), ClothesGMMaxItemID, "衣服GM")) {
									return;
								}
							}
							else if (isCarCategoryGM) {
								if (!PermissionItemAllowedByMess(CarGMMess, itemMess)) {
									ResponseStartGame(Client, "您只能刷配置 Mess 允许的赛车类道具！");
									return;
								}
								if (BlockGmItemByVersionLimit(Client, itemid, FindGmUinLimit(CarGMLimits, Client->Uin), CarGMMaxItemID, "赛车GM")) {
									return;
								}
							}

							if (!isConfigFullGM && !isNewClothesCarCategoryGM && !isNewClothesCategoryGM && !isNewCarCategoryGM && !isClothesCategoryGM && !isCarCategoryGM && GMMess.length() > 0 && !PermissionMessContains(GMMess, itemMess)) {
								std::string tips = "您只能获取以下类型道具：/C01" + GMMess;
								ResponseStartGame(Client, tips.c_str());
								return;
							}
						}
						// 创建物品信息并发送
						ItemInfo Items;

						if (GetItemType(itemid) == EAIT_PROP) {
							Items.ItemNum = 20000;
							Items.AvailPeriod = 0;
						}
						else {
							Items.ItemNum = 1;
							Items.AvailPeriod = -1;
						}
						Items.ItemID = itemid;
						Items.ItemType = 0;
						Items.ObtainTime = 0;
						PlayerDB_AddItem2(Client, Client->Uin, Items.ItemID, Items.ItemNum, Items.AvailPeriod, 0);
						virtualsend(Client, { Items }, 1, 23402);
						return;
						
					}
				}
			}
			catch (...)
			{
				printf("内置GM异常\n");
			}
			if (Type == 1 && strstr(Content, "下注") && Stake[0]) {
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				TalkStake(Client, Room, Content);
				return;
			}

			if (Type == 1 && strstr(Content, "开启尾数") != nullptr && Mantissa[0]) {
				if (!AdminUins.count(Client->Uin)) {
					ResponseStartGame(Client, "只有管理员才能进行此操作！");
					return;
				}
				if (IsRoomConnOwner(Room, Client)) {
					Room->IsBottomGame = 9;
					std::string msg = GetItemName(Mantissa[3]);
					msg = "房主已开启尾数！消耗道具是/C01【" + GetItemName(Mantissa[3]) + "】/C00，最低下注/C01【" + std::to_string(Mantissa[1]) + "】/C00最高下注/C01【" + std::to_string(Mantissa[2]) + "】输入obs观战后，尾数有尾大，尾小，尾单 ，尾双，四种选择。例如输入：/C01尾数单100";
					for (char j = 0; j < 6; j++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
						if (RoomClient)
						{
							RoomClient->IsBottomGame = 1;
							ResponseStartGame(RoomClient, msg.c_str());
						}
					}
					return;
				}
			}
			if (Type == 1 && strstr(Content, "尾数") != nullptr && Mantissa[0] && Room->IsBottomGame == 9) {
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				TalkMantissa(Client, Room, Content);
				return;
			}




			if (Type == 1 && strstr(Content, "开启垫底赛") != nullptr && IsBottomGame[0]) {
	/*			if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}*/
				if (!AdminUins.count(Client->Uin)) {
					ResponseStartGame(Client, "只有管理员才能进行此操作！");
					return;
				}
				if (IsRoomConnOwner(Room, Client)) {
					Room->IsBottomGame = 1;
					std::string msg = GetItemName(IsBottomGame[1]);
					msg = "房主已开启垫底赛模式！消耗道具是/C01【" + GetItemName(IsBottomGame[1]) + "】/C00，扣除最后/C01【" + std::to_string(IsBottomGame[3]) + "】/C00名的道具，" + "扣除数量是：/C01【" + std::to_string(IsBottomGame[2]) + "】";
					for (char j = 0; j < 6; j++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
						if (RoomClient)
						{
							RoomClient->IsBottomGame = 1;
							ResponseStartGame(RoomClient, msg.c_str());
						}
					}
					return;
				}
			}

			if (Type == 1 && strstr(Content, "关闭垫底赛") && (IsBottomGame[0] || g_BottomSuperMoneyConfig.Enabled)) {
				//if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
				//	return;
				//}
				const bool superMoneyMode = IsSuperMoneyBottomGameMode(Room->IsBottomGame);
				if (!superMoneyMode && !AdminUins.count(Client->Uin)) {
					ResponseStartGame(Client, "只有管理员才能进行此操作！");
					return;
				}
				if (!IsRoomConnOwner(Room, Client)) {
					ResponseStartGame(Client, "只有房主才能进行此操作！");
					return;
				}
				Room->IsBottomGame = 0;
				Room->BottomGameNum = 0;
				Room->BottomGameSettlementDone = false;
				if (superMoneyMode)
					SetBottomSuperMoneyAnonymousState(Room, false);
				for (char j = 0; j < 6; j++)
				{
					std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
					if (RoomClient)
					{
						RoomClient->IsBottomGame = 0;
						ResponseStartGame(RoomClient, "房主已关闭垫底赛模式！");
					}
				}
				return;
			}

			if (Type == 1 && (strstr(Content, "开启四人垫底") || strstr(Content, "开启4人垫底"))) {
				if (!g_BottomSuperMoneyConfig.Enabled) {
					ResponseStartGame(Client, "点券垫底赛配置未开启或无效！");
					return;
				}
				if (!IsRoomConnOwner(Room, Client)) {
					ResponseStartGame(Client, "只有房主才能开启点券垫底赛！");
					return;
				}
				const auto racePlayers = GetBottomSuperMoneyRacePlayers(Room);
				if (g_BottomSuperMoneyConfig.DenySameIp && BottomSuperMoneyHasInvalidOrDuplicateIp(racePlayers)) {
					ResponseStartGame(Client, "当前参赛玩家登录IP无效或存在相同IP，不能开启点券垫底赛！");
					return;
				}
				Room->BottomGameNum = g_BottomSuperMoneyConfig.Four.TotalCost;
				Room->IsBottomGame = 4;
				Room->BottomGameSettlementDone = false;
				SetBottomSuperMoneyAnonymousState(Room, true);
				const std::string msg = BuildBottomSuperMoneyNotice(4);
				for (const auto& roomClient : Room->Player)
					if (roomClient)
						ResponseStartGame(roomClient, msg.c_str());
				return;
			}

			if (Type == 1 && (strstr(Content, "开启六人垫底") || strstr(Content, "开启6人垫底"))) {
				if (!g_BottomSuperMoneyConfig.Enabled) {
					ResponseStartGame(Client, "点券垫底赛配置未开启或无效！");
					return;
				}
				if (!IsRoomConnOwner(Room, Client)) {
					ResponseStartGame(Client, "只有房主才能开启点券垫底赛！");
					return;
				}
				const auto racePlayers = GetBottomSuperMoneyRacePlayers(Room);
				if (g_BottomSuperMoneyConfig.DenySameIp && BottomSuperMoneyHasInvalidOrDuplicateIp(racePlayers)) {
					ResponseStartGame(Client, "当前参赛玩家登录IP无效或存在相同IP，不能开启点券垫底赛！");
					return;
				}
				Room->BottomGameNum = g_BottomSuperMoneyConfig.Six.TotalCost;
				Room->IsBottomGame = 6;
				Room->BottomGameSettlementDone = false;
				SetBottomSuperMoneyAnonymousState(Room, true);
				const std::string msg = BuildBottomSuperMoneyNotice(6);
				for (const auto& roomClient : Room->Player)
					if (roomClient)
						ResponseStartGame(roomClient, msg.c_str());
				return;
			}

			if (Type == 1 && strstr(Content, "开启竞技模式") != nullptr && IsBottomGame[0]) {
				//if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
				//	return;
				//}
				if (!AdminUins.count(Client->Uin)) {
					ResponseStartGame(Client, "只有管理员才能进行此操作！");
					return;
				}
				if (IsRoomConnOwner(Room, Client)) {
					Room->IsBottomGame = 7;
					int Frirst = 5;
					int Second = 4;
					int Third = 3;
					int Four = 2;
					int Five = 1;
					std::string msg = "房主已开启竞技模式！你会获得道具/C01【" + GetItemName(106614) + "】/C00\n第一名/C01赢【" + std::to_string(Frirst) + "】/C00，第二名/C01赢【"
						+ std::to_string(Second) + "】/C00，第三名/C01赢【" + std::to_string(Third) + "】/C00第四名/C01赢【" + std::to_string(Four) + "】/C00第五名/C01赢【" + std::to_string(Five) + "】/C00";
					for (char j = 0; j < 6; j++)
					{
						std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
						if (RoomClient)
						{
							RoomClient->IsBottomGame = 1;
							ResponseStartGame(RoomClient, msg.c_str());
						}
					}
					return;
				}
			}

			/*if (Type == 1 && strstr(Content, "垫底") && IsBottomGame[0]) {

				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				if (IsRoomConnOwner(Room, Client)) {

					char* Num = Content;
					Num += 4;
					if (isInteger(Num)) {
						Room->BottomGameNum = atoi(Num);
						Room->IsBottomGame = 2;
						std::string msg = GetItemName(IsBottomGame[1]);
						msg = "房主已开启垫底赛模式！消耗道具是/C01【" + GetItemName(IsBottomGame[1]) + "】/C00，扣除最后/C01【" + std::to_string(IsBottomGame[3]) + "】/C00名的道具，" + "扣除数量是：/C01【" + std::to_string(Room->BottomGameNum) + "】";
						for (char j = 0; j < 6; j++)
						{
							std::shared_ptr<ClientNode> RoomClient = Room->Player[j];
							if (RoomClient)
							{
								RoomClient->IsBottomGame = 1;
								ResponseStartGame(RoomClient, msg.c_str());
							}
						}
						return;
					}
					else {
						ResponseStartGame(Client, "垫底数量格式异常，只允许输入数字！");
						return;
					}
				}
			}*/



			if (Type == 1 && strstr(Content, "fps")) {
				//if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
				//	return;
				//}
				if (!AdminUins.count(Client->Uin)) {
					ResponseStartGame(Client, "只有管理员才能进行此操作！");
					return;
				}
				const char* message = (PlayerDB_UpdateBaseInfoFps(Client) > 0) ?
					"游戏内60帧数已开启，刷新频道生效！" :
					"游戏内60帧数已关闭，刷新频道生效!";
				ResponseStartGame(Client, message);
				return;
			}

			if (Type == 1 && strstr(Content, "绑定qq")) {
				if (LimitTalk && !(Client->gm > 0 || Client->admin > 0)) {
					return;
				}
				std::string qqNumber = extractQQ(Content);

				PlayerDB_UpdateBaseInfo(Client->Uin, qqNumber);
				qqNumber = "您绑定的QQ号码是/C01 " + qqNumber + " /C00刷新频道即可看到自己头像！";
				ResponseStartGame(Client, qqNumber.c_str());
				return;
			}

		
			if (Type == 1 && strcmp(Content, "我要玩龙晶") == 0)
			{
				std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
				if (Room && Room->BaseGameMode == 0x2e)
				{
					Client->MapID = Room->MapID = 431;
					ResponseStartGame(Client, "切换模式为龙晶大闯关");
					return;
				}
			}
			if (Type == 1 && strcmp(Content, "我要玩幻域") == 0)
			{
				std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
				if (Room && Room->BaseGameMode == 0x2e)
				{
					Client->MapID = Room->MapID = 432;
					ResponseStartGame(Client, "切换模式为幻域大闯关");
					return;
				}
			}
			if (Type == 1 && strcmp(Content, "我要玩冲刺") == 0)
			{
				std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
				if (Room && Room->BaseGameMode == 0x2e)
				{
					Client->MapID = Room->MapID = 452;
					ResponseStartGame(Client, "切换模式为冲刺大闯关");
					return;
				}
			}
			if (Type == 1 && strcmp(Content, "我要玩跃动") == 0)
			{
				std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
				if (Room && Room->BaseGameMode == 0x2e)
				{
					Client->MapID = Room->MapID = 464;
					ResponseStartGame(Client, "切换模式为跃动大闯关");
					return;
				}
			}
		}


		const auto contentId = QQS_MakeTalkContentId();
		for (char i = 0; i < 6; i++)
		{
			std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
			if (RoomClient)
			{
#ifdef ZingSpeed
				if (RoomClient == Client)
				{
					continue;
				}
#endif
				NotifyTalk(RoomClient, Client, Type, ClientUseFlag, DestPlayerUin,
					QQS_NormalizeNotifyBugletType(Type, BugletType), ContentLength, Content,
					contentId.data());
				/*BYTE* Body;
				Test(Client, Body, 1);*/
			}
		}
		ResponseTalk(Client, Type, ClientUseFlag, DestPlayerUin,
			QQS_NormalizeNotifyBugletType(Type, BugletType));
	}
}
void ResponseTalk(std::shared_ptr<ClientNode> Client, BYTE Type, BYTE ClientUseFlag, UINT DestPlayerUin, BYTE BugletType)
{
#ifdef ZingSpeed
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	size_t len;

	Write16(p, 0); //ResultID

	Write8(p, Type);
	Write8(p, ClientUseFlag);
	Write32(p, DestPlayerUin);

	Write8(p, 0); //ReasonLen
	Write8(p, BugletType); //BugletType
	Write8(p, 0); //ReserveFlag
	Write8(p, 0); //ReserveData

	len = p - buf.data();
	SendToClient(Client, 111, buf.data(), len, Client->RoomID, FE_ROOM, 1, Response);
#else
	std::vector<BYTE> buf(64); BYTE* p = buf.data();
	BYTE* pBodyLen = p;
	Write16(p, 0); //BodyLen
	Write16(p, 0); //ResultID
	Write8(p, Type);
	Write8(p, ClientUseFlag);
	Write32(p, DestPlayerUin);
	Write8(p, 0); //ReasonLen
	Write8(p, BugletType);
	Write8(p, 0); //ReserveFlag
	Write8(p, 0); //ReserveData
	Write8(p, 0); //TalkSpecialType
	Write32(p, 0); //Seq
	const size_t len = p - buf.data();
	Set16(pBodyLen, static_cast<WORD>(len));
	SendToClientOfficial14(Client, 111, buf.data(), static_cast<DWORD>(len),
		Client->ConnID, FE_PLAYER, Client->ConnID, Response);
#endif
}


void NotifyTalk(std::shared_ptr<ClientNode> Client, std::shared_ptr<ClientNode> RoomClient,
	BYTE Type, BYTE ClientUseFlag, UINT DestPlayerUin, BYTE BugletType,
	short ContentLength, const char* Content, const BYTE* ContentId)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	BYTE* pBodyLen = p;
	Write16(p, 0); //BodyLen
	Write32(p, RoomClient->Uin);

	memset(p, 0, 17);

	memcpy(p, RoomClient->NickName, 17);

	p += 17;

	Write8(p, static_cast<BYTE>(RoomClient->Gender | 0x40)); //Gender

	Write8(p, Type);
	Write8(p, ClientUseFlag);
	Write32(p, DestPlayerUin);

	Write16(p, ContentLength);
	memcpy(p, Content, ContentLength);
	p += ContentLength;


	Write8(p, QQS_NormalizeNotifyBugletType(Type, BugletType)); //BugletType
	Write32(p, 0); //Identify
	Write16(p, static_cast<USHORT>(RoomClient ? RoomClient->VipFlag : 0)); //VipFlag
	Write8(p, 0); //CanReport
	{ //TalkerInfo
		BYTE* pTalkerInfo = p;
		Write16(pTalkerInfo, 0); //len

		Write32(pTalkerInfo, RoomClient->Experience); //Exp

		{ //NobleInfo
			BYTE* pNobleInfo = pTalkerInfo;
			Write16(pNobleInfo, 0); //len

			Write32(pNobleInfo, RoomClient->NobleID); //NobleID
			Write8(pNobleInfo, RoomClient->NobleLevel); //NobleLevel
			Write32(pNobleInfo, RoomClient->NoblePoint); //NoblePoint
			Write32(pNobleInfo, -1); //NobleLeftDays

			len = pNobleInfo - pTalkerInfo;
			Set16(pTalkerInfo, (WORD)len);
			pTalkerInfo += len;
		}
#ifndef ZingSpeed
		{ //GuildVipBaseInfo
			BYTE* pGuildVipBaseInfo = pTalkerInfo;
			Write16(pGuildVipBaseInfo, 0); //len

			Write8(pGuildVipBaseInfo, RoomClient->GuildVipLevel); //GuildVipLevel
			Write32(pGuildVipBaseInfo, RoomClient->GuildVipPoint); //GuildVipPoint

			len = pGuildVipBaseInfo - pTalkerInfo;
			Set16(pTalkerInfo, (WORD)len);
			pTalkerInfo += len;
		}
		Write32(pTalkerInfo, static_cast<UINT>(RoomClient->GuildID)); //GuildId

		const BYTE hasLoverVip = (RoomClient && RoomClient->LoverVipLevel > 0) ? 1 : 0;
		Write8(pTalkerInfo, hasLoverVip); //HasLoverVip

		for (size_t i = 0; i < hasLoverVip; i++)
		{ //LoverVipInfo
			BYTE* pLoverVipInfo = pTalkerInfo;
			Write16(pLoverVipInfo, 0); //len

			Write8(pLoverVipInfo, RoomClient->LoverVipLevel); //LoverVipLevel
			Write32(pLoverVipInfo, RoomClient->LoverVipPoint); //LoverVipPoint
			Write8(pLoverVipInfo, 10); //GrowRate

			len = pLoverVipInfo - pTalkerInfo;
			Set16(pTalkerInfo, (WORD)len);
			pTalkerInfo += len;
		}

		CharmVipPlayerStateRow cvState = QQS_LoadCharmVipState(RoomClient ? RoomClient->Uin : 0);
		const UINT consumeVipLevel = QQS_GetRoomConsumeVipLevel(RoomClient ? RoomClient->Uin : 0, RoomClient);
		const UINT charmCareerLevel = QQS_GetCharmVipCareerLevel(cvState);
		Write8(pTalkerInfo, RoomClient->GardenLevel); //GardenLevel
		Write8(pTalkerInfo, static_cast<BYTE>(std::min<UINT>(consumeVipLevel, 9))); //ConsumeVipLevel
		{ //EmperorInfo
			BYTE* pEmperorInfo = pTalkerInfo;
			Write16(pEmperorInfo, 0); //len

			Write8(pEmperorInfo, RoomClient->EmperorLevel); //EmperorLevel
			Write32(pEmperorInfo, RoomClient->EmperorPoint); //EmperorPoint
			Write32(pEmperorInfo, -1); //EmperorLeftDays
			Write8(pEmperorInfo, 10); //EmperorGrowRate

			len = pEmperorInfo - pTalkerInfo;
			Set16(pTalkerInfo, (WORD)len);
			pTalkerInfo += len;
		}
		Write32(pTalkerInfo, charmCareerLevel); //CharmCareerLevel
		Write32(pTalkerInfo, 0); //HelperLev
		Write8(pTalkerInfo, QQS_GetCharmVipIconPreference(consumeVipLevel, charmCareerLevel)); //ShowConsumeVip
#endif

		len = pTalkerInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}


	Write8(p, 0); //ReserveFlag
	Write32(p, 0); //ReserveData
#ifndef ZingSpeed
	memcpy(p, ContentId, 18); //ContentId[18]
	p += 18;
	Write8(p, 0); //transBufType
	Write16(p, 0); //transBufLen
	Write8(p, 0); //MsgSource
	Write16(p, 0); //DstNickNameLength
	Write32(p, 0); //Seq
	Write32(p, 0); //PresentItemID
	Write8(p, 0); //AutoMsg
	{
		BYTE* pTalkExtraData = p;
		Write16(pTalkExtraData, 0); //TalkExtraData len
		Write32(pTalkExtraData, 0); //AtUin
		const size_t talkExtraDataLen = pTalkExtraData - p;
		Set16(p, static_cast<WORD>(talkExtraDataLen));
		p += talkExtraDataLen;
	}
#else
	Write8(p, 0); //transBufType
	Write16(p, 0); //transBufLen
#endif

	len = p - buf.data();
	Set16(pBodyLen, (WORD)len);
#ifdef ZingSpeed
	SendToClient(Client, 505, buf.data(), len, Client->RoomID, FE_ROOM, 1, Notify);
#else
	SendToClientOfficial14(Client, 505, buf.data(), static_cast<DWORD>(len),
		Client->ConnID, FE_PLAYER, Client->ConnID, Notify);
#endif
}


static void NotifyChangeRoomOwnerInternal(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, USHORT OriRoomOwnerID)
{
	if (!Client || !Room)
		return;
	EnsureRoomOwnerAlive(Room, Client, "notify-owner-change");
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;

	Write16(p, Room->ID); //RoomID
	Write16(p, GetRoomConnOwnerID(Room)); //NewRoomOwnerID
	Write8(p, 0); //HasFBInfo
	// 21043 Network.dll CNotifyChangeRoomOwnerPara 反编译字段偏移：
	// RoomID@0, NewOwnerID@2, HasFBInfo@4, FBInfo fixed slot@5..12,
	// OriRoomOwnerID@13, ChangeType@15, RoomType@16。
	// 即使 HasFBInfo=0，客户端仍按固定 8 字节 FBInfo 槽位取后续字段；
	// 旧包直接把 OriRoomOwnerID 写在 offset 5，导致本人 owner 状态不清/不生效。
	Write8(p, 0);  // FBInfo.AvailableLev
	Write8(p, 0);  // FBInfo.Lev
	Write8(p, 0);  // FBInfo.SubLev
	Write32(p, 0); // FBInfo.BossID
	Write8(p, 0);  // FBInfo.FBSeason
	Write16(p, OriRoomOwnerID); //OriRoomOwnerID
	Write8(p, 0); //ChangeType
	Write16(p, 0); //RoomType

	len = p - buf.data();
	// owner-change 走 FE_PLAYER，包体 NewOwnerID/OriOwnerID 用 ConnID。
	QQS_DebugPrintf10001("[FLOW][OWNER-CHANGE] Uin=%u RoomID=%u OwnerID=%u OriOwnerID=%u SelfWireID=%u Route=FE_PLAYER FBInfoSlot=8 PayloadLen=%zu WireBodyLen=%zu\n",
		Client->Uin, Room->ID, GetRoomConnOwnerID(Room), OriRoomOwnerID, GetRoomConnPlayerID(Room, Client), len, len + 2);
	SendToClient(Client, 0x020C, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}

void NotifyChangeRoomOwner(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room)
{
	NotifyChangeRoomOwnerInternal(Client, Room, 0);
}
void ResponseChangeRoomOwner(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, UINT NewOwnerID, USHORT ResultID)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;

	QQS_DebugPrintf10001("[FLOW][713-OWNER-RSP] Uin=%u RoomID=%u RoomOwnerID=%u NewOwnerID=%u ResultID=%u\n",
		Client ? Client->Uin : 0,
		Room ? Room->ID : 0,
		Room ? GetRoomConnOwnerID(Room) : 0,
		NewOwnerID,
		ResultID);

	Write32(p, Client->Uin); //Uin
	Write32(p, NewOwnerID); //NewOwnerID(ConnID16)
	Write16(p, ResultID); //ResultID
	Write8(p, 0); //ReasonLen

	len = p - buf.data();
	SendToClient(Client, 713, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Response);
}
void RequestChangeRoomOwner(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (!Client || !Body || BodyLen < 12)
	{
		return;
	}
	BYTE* p = Body;
	UINT Uin = Read32(p);
	UINT Time = Read32(p);
	UINT NewOwner = Read32(p);
	UINT NewOwnerID = 0;
	// 获取房间信息
	std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
	if (!Room) // 如果房间不存在，直接返回
	{
		return;
	}
	if (!IsRoomConnOwner(Room, Client))
	{
		QQS_DebugPrintf10001("[FLOW][713-OWNER-DENY] Uin=%u ReqUin=%u RoomID=%u CurrentOwner=%u ClientPlayerID=%u NewOwnerUin=%u\n",
			Client->Uin, Uin, Room->ID, GetRoomConnOwnerID(Room), GetRoomConnPlayerID(Room, Client), NewOwner);
		ResponseChangeRoomOwner(Client, Room, GetRoomConnOwnerID(Room), 2);
		return;
	}
	// 遍历房间内的玩家
	std::shared_ptr<ClientNode> TargetOwner = nullptr;
	int seatLimit = GetRoomSeatLimit(Room);
	for (char i = 0; i < seatLimit; i++)
	{
		std::shared_ptr<ClientNode> NewRoomOwner = Room->Player[i];
		if (NewRoomOwner)
		{// 如果找到匹配的新房主
			if (NewRoomOwner->Uin == NewOwner)
			{
				NewOwnerID = GetRoomConnPlayerID(Room, NewRoomOwner);// 设置新房主协议ID
				TargetOwner = NewRoomOwner;
				NewRoomOwner->ReadyState = QQS_ROOM_READY_STATE_READY;
				break;
			}
		}
	}
	if (!TargetOwner || NewOwnerID == 0)
	{
		QQS_DebugPrintf10001("[FLOW][713-OWNER-NOTFOUND] Uin=%u ReqUin=%u RoomID=%u OldOwner=%u NewOwnerUin=%u TargetFound=%u TargetConn=%u TargetPlayerID=%u -> keep owner\n",
			Client->Uin, Uin, Room->ID, GetRoomConnOwnerID(Room), NewOwner,
			TargetOwner ? 1 : 0,
			TargetOwner ? TargetOwner->TransportConnID : 0,
			TargetOwner ? GetRoomConnPlayerID(Room, TargetOwner) : 0);
		ResponseChangeRoomOwner(Client, Room, GetRoomConnOwnerID(Room), 1);
		return;
	}
	USHORT OldOwnerID = GetRoomConnOwnerID(Room);
	std::shared_ptr<ClientNode> OldOwnerClient = Client;
	Room->RoomOwnerUin = TargetOwner->Uin;
	Room->RoomOwnerID = (USHORT)NewOwnerID; // 更新房主ID
	if (IsSuperMoneyBottomGameMode(Room->IsBottomGame))
		SetBottomSuperMoneyAnonymousState(Room, false);
	Room->IsBottomGame = 0;    // 关闭垫底赛模式
	Room->BottomGameNum = 0;
	Room->BottomGameSettlementDone = false;
	QQS_DebugPrintf10001("[FLOW][713-OWNER-OK] Uin=%u ReqUin=%u RoomID=%u OldOwner=%u NewOwnerID=%u NewOwnerUin=%u TargetConn=%u\n",
		Client->Uin, Uin, Room->ID, OldOwnerID, Room->RoomOwnerID, TargetOwner->Uin, TargetOwner->TransportConnID);
	ResponseChangeRoomOwner(Client, Room, GetRoomConnOwnerID(Room), 0);   // 响应房主变更
	// 通知房间内的每个玩家房主已变更。
	// 进房链路的 player-id sync -> owner-change 才能让客户端 self.PlayerID 表和房主绑定一起就绪；
	// 转让如果只发 owner-change，B 的 Uin->WireID 表可能因为之前某条流被覆盖/漂移，
	// 客户端 self.ConnID != NewOwnerID，self-owner 分支不触发，
	// 表现为旧房主/其他人对，唯独新房主自己没房主权限。
	// 这里给每个房间成员先发 player-id sync 刷一遍 Uin->WireID 表，再发 owner-change。
	const USHORT chainNewOwnerID = GetRoomConnOwnerID(Room);
	for (char i = 0; i < seatLimit; i++)
	{
		std::shared_ptr<ClientNode> RoomClient = Room->Player[i];
		if (RoomClient)
		{
			const USHORT recipientSelfWireID = GetRoomConnPlayerID(Room, RoomClient);
			QQS_DebugPrintf10001("[FLOW][713-OWNER-CHAIN] RoomID=%u recipient=%u Step=playerid-then-owner SelfWireID=%u OwnerID=%u OldOwnerID=%u\n",
				Room->ID, RoomClient->Uin, recipientSelfWireID, chainNewOwnerID, OldOwnerID);
			NotifyClientUpdatePlayerID(RoomClient, Room); // 先刷 Uin->WireID 表
			NotifyChangeRoomOwnerInternal(RoomClient, Room, OldOwnerID);// 再通知房主变更
		}
	}
	// 转让房主只靠 713 + owner-change 更新 owner 状态，不补 self 500。
	// self 500 是玩家对象刷新，旧房主本地会残留旧 owner 标记，表现为两个房主。
	QQS_DebugPrintf10001("[FLOW][713-OWNER-NO-SELF500] RoomID=%u OldOwnerUin=%u NewOwnerUin=%u OldOwnerID=%u NewOwnerID=%u\n",
		Room->ID,
		OldOwnerClient ? OldOwnerClient->Uin : 0,
		TargetOwner ? TargetOwner->Uin : 0,
		(unsigned)OldOwnerID,
		(unsigned)GetRoomConnOwnerID(Room));

}

void NotifyLmtEnterRoom(std::shared_ptr<ClientNode> Client)
{
	BYTE buf[8192]; BYTE* p = buf; size_t len;

	{ //PlayerInfo
		BYTE* pPlayerInfo = p;
		Write16(pPlayerInfo, 0); //len

		Write32(pPlayerInfo, Client->Uin); //Uin
		WriteString(pPlayerInfo, Client->NickName); //NickName[]
		Write32(pPlayerInfo, 0); //Identify
		Write8(pPlayerInfo, Client->Gender); //Gender
		Write32(pPlayerInfo, 0); //EquipedIconID
		WriteUn(pPlayerInfo, 0); //Experience
		{ //PlayerRecord
			BYTE* pPlayerRecord = pPlayerInfo;
			Write16(pPlayerRecord, 0); //len

			Write32(pPlayerRecord, 0); //MatchNum
			Write8(pPlayerRecord, 0); //BestRank
			Write32(pPlayerRecord, 0); //ChamNum
			Write32(pPlayerRecord, 0); //WinNum

			len = pPlayerRecord - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		Write16(pPlayerInfo, static_cast<USHORT>(Client ? Client->VipFlag : 0)); //VipFlag
		{ //NobleInfo
			BYTE* pNobleInfo = pPlayerInfo;
			Write16(pNobleInfo, 0); //len

			Write32(pNobleInfo, Client->NobleID); //NobleID
			Write8(pNobleInfo, Client->NobleLevel); //NobleLevel
			Write32(pNobleInfo, Client->NoblePoint); //NoblePoint
			Write32(pNobleInfo, -1); //NobleLeftDays

			len = pNobleInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		{ //GuildVipBaseInfo
			BYTE* pGuildVipBaseInfo = pPlayerInfo;
			Write16(pGuildVipBaseInfo, 0); //len

			Write8(pGuildVipBaseInfo, Client->GuildVipLevel); //GuildVipLevel
			Write32(pGuildVipBaseInfo, Client->GuildVipPoint); //GuildVipPoint

			len = pGuildVipBaseInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		const BYTE hasLoverVip = (Client && Client->LoverVipLevel > 0) ? 1 : 0;
		Write8(pPlayerInfo, hasLoverVip); //HasLoverVip
		for (size_t i = 0; i < hasLoverVip; i++)
		{ //LoverVipInfo
			BYTE* pLoverVipInfo = pPlayerInfo;
			Write16(pLoverVipInfo, 0); //len

			Write8(pLoverVipInfo, Client ? Client->LoverVipLevel : 0); //LoverVipLevel
			Write32(pLoverVipInfo, Client ? Client->LoverVipPoint : 0); //LoverVipPoint
			Write8(pLoverVipInfo, 10); //GrowRate

			len = pLoverVipInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		Write8(pPlayerInfo, Client ? Client->GardenLevel : 0); //GardenLevel
		{ //EmperorInfo
			BYTE* pEmperorInfo = pPlayerInfo;
			Write16(pEmperorInfo, 0); //len

			Write8(pEmperorInfo, Client ? Client->EmperorLevel : 0); //EmperorLevel
			Write32(pEmperorInfo, Client ? Client->EmperorPoint : 0); //EmperorPoint
			Write32(pEmperorInfo, -1); //EmperorLeftDays
			Write8(pEmperorInfo, 10); //EmperorGrowRate

			len = pEmperorInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		{ //ConsumeVipInfo
			BYTE* pConsumeVipInfo = pPlayerInfo;
			Write16(pConsumeVipInfo, 0); //len

			CharmVipPlayerStateRow cvState = QQS_LoadCharmVipState(Client ? Client->Uin : 0);
			const UINT consumeVipLevel = QQS_GetRoomConsumeVipLevel(Client ? Client->Uin : 0, Client);
			const UINT charmValue = QQS_LoadRoomCharmValue(Client ? Client->Uin : 0, Client);
			const UINT charmCareerLevel = QQS_GetCharmVipCareerLevel(cvState);
			WriteUn(pConsumeVipInfo, consumeVipLevel); //VipLevel
			WriteUn(pConsumeVipInfo, charmValue); //CharmValueOfMonth
			WriteUn(pConsumeVipInfo, 0); //SearchTreasureNums
			WriteUn(pConsumeVipInfo, 0); //GetTreasureNums
			Write32(pConsumeVipInfo, charmCareerLevel); //CharmCareerLevel
			Write8(pConsumeVipInfo, QQS_GetCharmVipIconPreference(consumeVipLevel, charmCareerLevel)); //ShowConsumeVip

			len = pConsumeVipInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		Write32(pPlayerInfo, 0); //BeLikedNums
		Write8(pPlayerInfo, 0); //AreaID

		len = pPlayerInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	len = p - buf;
	SendToClient(Client, 801, buf, len, Client->ConnID, FE_PLAYER, 1, Notify);
}

static void ResponseLmtEnterChatRoom_LegacyDisabled(std::shared_ptr<ClientNode> Client);
static void RequestLmtEnterChatRoom_LegacyDisabled(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	BYTE* p = Body; WORD len;

	UINT Uin = Read32(p);
	UINT Time = Read32(p);
	UINT RoomID = Read32(p);
	ResponseLmtEnterChatRoom_LegacyDisabled(Client);
}

static void ResponseLmtEnterChatRoom_LegacyDisabled(std::shared_ptr<ClientNode> Client)
{
	//测试
	//NotifyLuckyMatchReady(Client);
	{
		std::string Byte = "000000000000000000000000000000";
		const unsigned char* Msg403 = EHexToByte(Byte.data());
		SendToClient(Client, 403, (const BYTE*)Msg403, Byte.length() / 2, Client->ConnID, FE_PLAYER, 1, Response);
		delete[] Msg403;
	}

	{
		std::string Byte = "0001000A0005000000000000";
		const unsigned char* Msg413 = EHexToByte(Byte.data());
		SendToClient(Client, 413, (const BYTE*)Msg413, Byte.length() / 2, Client->ConnID, FE_PLAYER, 1, Response);
		delete[] Msg413;
	}

	{
		std::string Byte = "000000000400080005000000000008000666FA6BF90008000766FA6BF90008000866FA6BF9";
		const unsigned char* Msg439 = EHexToByte(Byte.data());
		SendToClient(Client, 439, (const BYTE*)Msg439, Byte.length() / 2, Client->ConnID, FE_PLAYER, 1, Response);
		delete[] Msg439;
	}

	{
		std::string Byte = "0028000003E800000001020007030000000000070300000000000F00000001000000000000000000";
		const unsigned char* Msg810 = EHexToByte(Byte.data());
		SendToClient(Client, 810, (const BYTE*)Msg810, Byte.length() / 2, Client->ConnID, FE_PLAYER, 1, Response);
		delete[] Msg810;
	}
	NotifyLmtGetScoreInfo(Client);

	BYTE buf[8192]; BYTE* p = buf; size_t len;

	Write16(p, 0); //ResultID
	Write8(p, 0); //ReasonLen
	Write16(p, 1); //PlayerNum
	for (size_t i = 0; i < 1; i++)
	{ //PlayerInfo
		BYTE* pPlayerInfo = p;
		Write16(pPlayerInfo, 0); //len

		Write32(pPlayerInfo, 0); //Uin
		WriteString(pPlayerInfo, "测试"); //NickName[]
		Write32(pPlayerInfo, 0); //Identify
		Write8(pPlayerInfo, 0); //Gender
		Write32(pPlayerInfo, 0); //EquipedIconID
		WriteUn(pPlayerInfo, 0); //Experience
		{ //PlayerRecord
			BYTE* pPlayerRecord = pPlayerInfo;
			Write16(pPlayerRecord, 0); //len

			Write32(pPlayerRecord, 0); //MatchNum
			Write8(pPlayerRecord, 0); //BestRank
			Write32(pPlayerRecord, 0); //ChamNum
			Write32(pPlayerRecord, 0); //WinNum

			len = pPlayerRecord - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		Write16(pPlayerInfo, 0); //VipFlag
		{ //NobleInfo
			BYTE* pNobleInfo = pPlayerInfo;
			Write16(pNobleInfo, 0); //len

			Write32(pNobleInfo, 0); //NobleID
			Write8(pNobleInfo, 0); //NobleLevel
			Write32(pNobleInfo, 0); //NoblePoint
			Write32(pNobleInfo, 0); //NobleLeftDays

			len = pNobleInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		{ //GuildVipBaseInfo
			BYTE* pGuildVipBaseInfo = pPlayerInfo;
			Write16(pGuildVipBaseInfo, 0); //len

			Write8(pGuildVipBaseInfo, 0); //GuildVipLevel
			Write32(pGuildVipBaseInfo, 0); //GuildVipPoint

			len = pGuildVipBaseInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		Write8(pPlayerInfo, 0); //HasLoverVip
		for (size_t i = 0; i < 0; i++)
		{ //LoverVipInfo
			BYTE* pLoverVipInfo = pPlayerInfo;
			Write16(pLoverVipInfo, 0); //len

			Write8(pLoverVipInfo, 0); //LoverVipLevel
			Write32(pLoverVipInfo, 0); //LoverVipPoint
			Write8(pLoverVipInfo, 0); //GrowRate

			len = pLoverVipInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		Write8(pPlayerInfo, 0); //GardenLevel
		{ //EmperorInfo
			BYTE* pEmperorInfo = pPlayerInfo;
			Write16(pEmperorInfo, 0); //len

			Write8(pEmperorInfo, 0); //EmperorLevel
			Write32(pEmperorInfo, 0); //EmperorPoint
			Write32(pEmperorInfo, 0); //EmperorLeftDays
			Write8(pEmperorInfo, 0); //EmperorGrowRate

			len = pEmperorInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		{ //ConsumeVipInfo
			BYTE* pConsumeVipInfo = pPlayerInfo;
			Write16(pConsumeVipInfo, 0); //len

			CharmVipPlayerStateRow cvState = QQS_LoadCharmVipState(Client ? Client->Uin : 0);
			const UINT consumeVipLevel = QQS_GetRoomConsumeVipLevel(Client ? Client->Uin : 0, Client);
			const UINT charmValue = QQS_LoadRoomCharmValue(Client ? Client->Uin : 0, Client);
			const UINT charmCareerLevel = QQS_GetCharmVipCareerLevel(cvState);
			WriteUn(pConsumeVipInfo, consumeVipLevel); //VipLevel
			WriteUn(pConsumeVipInfo, charmValue); //CharmValueOfMonth
			WriteUn(pConsumeVipInfo, 0); //SearchTreasureNums
			WriteUn(pConsumeVipInfo, 0); //GetTreasureNums
			Write32(pConsumeVipInfo, charmCareerLevel); //CharmCareerLevel
			Write8(pConsumeVipInfo, QQS_GetCharmVipIconPreference(consumeVipLevel, charmCareerLevel)); //ShowConsumeVip

			len = pConsumeVipInfo - pPlayerInfo;
			Set16(pPlayerInfo, (WORD)len);
			pPlayerInfo += len;
		}
		Write32(pPlayerInfo, 0); //BeLikedNums
		Write8(pPlayerInfo, 0); //AreaID

		len = pPlayerInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	len = p - buf;
	SendToClient(Client, 381, buf, len, Client->ConnID, FE_PLAYER, 1, Response);

	NotifyGangsterCfg(Client);

	{
		std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;
		WriteHex(p, "1B3E889D00C80077000000010000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F00000000000000010000C3500000000000001F40000A00000000000000000000000100000002000000010000000001000E0000000000004B64000001F40073000000020000000BFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100370000000100000001000000030000000000002710000A0000000000000000000000000000000001000E000036B0000088B8000000C8007B000000030000000BFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000201800804000000000001003F0000000000000001000138800000000000001B58000A00000000000000000000000100000002000000080000000001000E0000000000004330000003E80073000000040000000BFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A000000000000088B8000A00000000000000000000000100000002000000010000000001000E0000000000004B64000005DC0073000000050000000BFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000010000000000000100370000000000000001000138800000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000001900073000000060000000BFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000006000000000001003F0000000000000001000138800000000000001B58000A00000000000000000000000100000002000000560000000001000E0000000000004330000003840065000000070000000B000000010000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100290000000000000001000186A00000000100000000000A00000000000000000000000800000000000073000000080000000BFFFFFFFF000000000000000000000000002E000000000000000100000002001200000002000000020180080400000000000E000000010000000100007530000100290000000000000001000186A00000000100000000000A00000000000000000000000600000000000065000000090000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000090000000100000BB80001002D0000000000000001000186A00000000100000000000A0000000000000000000000070000000100001F400000650000000A0000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000500000001000000000001002D0000000100000001000000020000000000000FA0000A0000000000000000000000030000000100001D4C00007B0000000B0000000BFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F0000000000000001000088B80000000000000BB8000A00000000000000000000000100000002000000560000000001000E0000000000004330000003E800650000000C0000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000075300001002D0000000000000001000186A00000000100000000000A0000000000000000000000070000000100001F400000A40000000D0000000BFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000010000000000000200290000000100000001000000020000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000000BB8000A00000000000000000000000100000002000000560000000001000E00004B64000088B80000025800650000000E0000000BFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000100310000000000000001000186A000000000FFFFFFFF000A000000000000000000000001000000020000000C0000007800007B0000000F0000000BFFFFFFFF00000000000000000000000000200000000100000001000000010012000000020000000200000000000000100001003F00000000000000010000EA600000000000001B58000A00000000000000000000000100000002000000560000000001000E0000000000004330000003E800AA000000100000000BFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000200290000000000000001000186A00000000100000000000A0000000000000000000000080000000000004D0000000000000001000186A00000000000007530000A00000000000000000000000100000002000000010000000002000E0000000000004B6400000384000E00004B64000088B80000019000AE000000110000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000090000000100001770000200370000000000000001000138800000000000001770000A0000000000000000000000000000000001000E000036B0000088B8000001F4003F0000000200000001000000010000000000001770000A00000000000000000000000100000002000000010000000001000E0000000000004B64000001F4006F000000120000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000050000000100000000000100370000000000000001000186A00000000000001B58000A0000000000000000000000000000000001000E000036B0000088B8000002580065000000130000000BFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100290000000100000001000000050000000100000000000A00000000000000000000000800000000000061000000140000000BFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001002D0000000000000001000186A000000000FFFFFFFF000A0000000000000000000000030000000100001194000061000000150000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000900000001000003E8000100290000000000000001000186A00000000100000000000A00000000000000000000000600000000000073000000160000000BFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A000000000FFFFFFFF000A000000000000000000000001000000020000000C000003E801000E00004394000088B8000002BC0069000000170000000BFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000704000000200001002D0000000000000001000088B80000000100000000000A00000000000000000000000700000001000027100000B2000000180000000BFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000200290000000000000001000186A00000000100000000000A0000000000000000000000060000000000004D0000000200000001000000010000000000001388000A00000000000000000000000100000002000000020000000002000E0000000000004330000004B0000E00004394000088B8000002BC0065000000190000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000100000001000075300001002D0000000000000001000186A00000000000007530000A0000000000000000000000030000000100000FA00000610000001A0000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000900000001000061A8000100290000000000000001000186A00000000100000000000A000000000000000000000008000000000000690000001B0000000BFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001002D00000000000000010000D6D80000000100000000000A00000000000000000000000700000001000059D80000770000001C0000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000090000000100001F400001003F0000000000000001000111700000000000001F40000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000001F400730000001D0000000BFFFFFFFF0000"
			"000000000000000000000018000000000000000100000001000A00000005000000000001003F0000000100000001000000020000000000000FA0000A000000000000000000000001000000020000000C0000000001000E00004394000088B8000003E800690000001E0000000BFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000201800804000000000001002D0000000100000001000000030000000000001388000A00000000000000000000000300000001000017700000920000001F0000000BFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000002002D0000000000000001000186A00000000100000000000A000000000000000000000007000000010000C35000003100000002000000010000000100000000FFFFFFFF000A00000000000000000000000400000002000186A0000186A00000AA000000200000000BFFFFFFFF0000000000000000000000000026000000000000000000000002000E0000000B000000010000AFC8000A0000000C00000000000200290000000000000001000186A00000000100000000000A0000000000000000000000060000000000003F00000002000000010000000100000000000007D0000A00000000000000000000000100000002000000560000000001000E0000000000004330000003E800AE000000210000000BFFFFFFFF0000000000000000000000000026000000000000000000000002000E0000000B000000010000AFC8000A0000000C000000000002002D0000000000000001000186A00000000100000000000A000000000000000000000007000000010000EA6000003F0000000200000001000000010000000000000BB8000A00000000000000000000000100000002000000010000000001000E0000000000004B64000003200065000000220000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC80001002D0000000000000001000186A00000000000001388000A0000000000000000000000030000000100004E2000006D000000230000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC8000100350000000000000001000186A00000000000000BB8000A0000000000000000000000000000000300000000000037DC000003E800005D000000240000000BFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000100290000000000000001000186A000000000FFFFFFFF000A0000000000000000000000090000000000007B000000250000000BFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000704000000200001003F0000000000000001000088B80000000000001F40000A00000000000000000000000100000002000000010000000001000E00004B64000088B800000190006F000000260000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000400000001FFFFFFFF000100370000000000000001000111700000000000000BB8000A0000000000000000000000010000000001000E000036B0000088B8000002580077000000270000000BFFFFFFFF000000000000000000000000002E000000000000000100000002000E0000000100000001000075300012000000020000000200000700000000200001002D0000000000000001000186A00000000100000000000A0000000000000000000000070000000100002710000065000000280000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001002D0000000000000001000186A00000000100000000000A0000000000000000000000070000000100001B5800007B000000290000000BFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F0000000000000001000088B80000000000001770000A00000000000000000000000100000002000000560000000001000E00004B64000088B80000025800770000002A0000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F0000000000000001000080E80000000000001F40000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000001F400690000002B0000000BFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000704000000200001002D0000000000000001000186A00000000100000000000A0000000000000000000000070000000100000BB80000AA0000002C0000000BFFFFFFFF0000000000000000000000000026000000000000000000000002000E0000000B000000010000AFC8000A0000000C000000000002003F0000000000000001000186A00000000100000000000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000001F400290000000200000001000000010000000000001770000A0000000000000000000000000000000000006B0000002D0000000BFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000500000000000100370000000100000001000000020000000000000FA0000A0000000000000000000000000000000001000E000036B0000088B8000000C800690000002E0000000BFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001002D0000000000000001000186A00000000100000000000A00000000000000000000000700000001000032C800006F0000002F0000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000400000001FFFFFFFF000100370000000100000001000000030000000000001B58000A0000000000000000000000010000000001000E000036B0000088B8000002580073000000300000000CFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100370000000100000001000000030000000000001388000A0000000000000000000000000000000001000E000036B0000088B8000002580073000000310000000CFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100370000000100000001000000020000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000003840077000000320000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F00000000000000010000EA600000000000001F40000A00000000000000000000000100000002000000010000000001000E0000000000004B64000002580077000000330000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F0000000000000001000186A00000000000001F40000A00000000000000000000000100000002000000010000000001000E0000000000004B6400000384007B000000340000000CFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F0000000000000001000186A00000000000000BB8000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000000C8007B000000350000000CFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F0000000000000001000186A00000000000000BB8000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000001900077000000360000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000300000001FFFFFFFF0001003F0000000100000001000000030000000000001388000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000004B00077000000370000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000300000001FFFFFFFF0001003F0000000100000001000000020000000000001F40000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000007080077000000380000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F0000000000000001000138800000000000001388000A00000000000000000000000100000002000000010000000001000E0000000000004B64000003840077000000390000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F0000000000000001000186A00000000000001388000A00000000000000000000000100000002000000010000000001000E0000000000004B64000004E200770000003A0000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000900000001000017700001003F000000000000000100011170000000000000170C000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000019000770000003B0000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000900000001000017700001003F000000000000000100017318000000000000170C000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000002EE007B0000003C0000000CFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000704000000200001003F0000000100000001000000050000000000001388000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000004B0007B0000003D0000000CFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000704000000200001003F0000000100000001000000030000000000001388000A00000000000000000000000100000002000000560000000001000E00004B64000088B800000898007B0000003E0000000CFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F00000000000000010000EA600000000000001388000A00000000000000000000000100000002000000560000000001000E000000000000433000000708007B0000003F0000000CFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F000000000000000100015F900000000000001388000A00000000000000000000000100000002000000560000000001000E0000000000004330000009C400A4000000400000000C00000004000000000000000000000000001C000000000000000100000001000E0000001000000001000000040002003F0000000000000001000186A00000000000001770000A00000000000000000000000100000002000000560000000001000E0000000000004330000007D0002D0000000200000001000000010000000100000000000A00000000000000000000000700000001000075300000A4000000410000000C00000006000000000000000000000000001C000000000000000100000001000E0000001000000001000000040002003F0000000000000001000186A00000000000001F40000A00000000000000000000000100000002000000560000000001000E000000000000433000000BB8002D0000000200000001000000010000000100000000000A000000000000000000000007000000010000C3500000AA000000420000000CFFFFFFFF000007D000000000000000000026000000010000000000000002000E0000000B000000010000AFC8000A0000000D00000000000200290000000000000001000186A00000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000001388000A00000000000000000000000100000002000000560000000001000E00000000000043300000064000AA000000430000000CFFFFFFFF000007D000000000000000000026000000010000000000000002000E0000000B000000010000AFC8000A0000000D00000000000200290000000000000001000186A00000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000001F40000A00000000000000000000000100000002000000560000000001000E000000000000433000000C8000DC000000440000000CFFFFFFFF000000000000000000000000002C000000000000000100000001001E0000000700000005000007FC00000001000000010000000100000000000200410000000000000001000186A00000000200000001001E000000070000000500000002000000020000000100000001000000000000000300000001000017700000530000000200000001000000010000000200000001001E000000070000000500000002000000020000000100000001000000000000000100000002000000010000000001000E0000000000004B640000025800DC000000450000000CFFFFFFFF000000000000000000000000002C000000000000000100000001001E0000000700000005000007FC00000001000000010000000100000000000200410000000000000001000186A00000000200000001001E000000070000000500000002000000020000000100000001000000000000000300000001000027100000530000000200000001000000010000000200000001001E000000070000000500000002000000020000000100000001000000000000000100000002000000010000000001000E0000000000004B6400000384006B000000460000000CFFFFFFFF000007D000000000000000000018000000010000000100000001000A0000000E00000000000100370000000000000001000111700000000000000FA0000A0000000000000000000000000000000001000E000036B0000088B800000258006B000000470000000CFFFFFFFF000007D000000000000000000018000000010000000100000001000A0000000E00000000000100370000000000000001000186A00000000000001388000A0000000000000000000000000000000001000E000036B0000088B800000384006F000000480000000CFFFFFFFF000000000000000000000000001C000000010000000100000001000E0000000500000001000000000001003700000000000000010000EA600000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000001F4006F000000490000000CFFFFFFFF000000000000000000000000001C000000010000000100000001000E00000005000000010000000000010037000000000000000100015F900000000000001F40000A0000000000000000000000000000000001000E000036B0000088B80000032000A40000004A0000000CFFFFFFFF0000000000000000000000000020000000010000000100000001001200000002000000020000010000000000000200290000000100000001000000020000000100000000000A0000000000000000000000060000000000003F0000000100000001000000020000000000000BB8000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000004B000A40000004B0000000CFFFFFFFF0000000000000000000000000020000000010000000100000001001200000002000000020000010000000000000200290000000100000001000000020000000100000000000A0000000000000000000000060000000000003F0000000100000001000000020000000000001388000A00000000000000000000000100000002000000560000000001000E00004B64000088B80000096000A40000004C0000000CFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000200310000000000000001000186A000000000FFFFFFFF000A000000000000000000000001000000020000000C0000012C00003F00000002000000010000000100000000FFFFFFFF000A000000000000000000000001000000020000000C0000000001000E00004B64000088B8000004B000A40000004D0000000CFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000200310000000000000001000186A000000000FFFFFFFF000A000000000000000000000001000000020000000C000003E800003F00000002000000010000000100000000FFFFFFFF000A000000000000000000000001000000020000000C0000000001000E00004B64000088B80000096000730000004E0000000CFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A00000000000007530000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000004B000730000004F0000000CFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A0000000000000C350000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000007080065000000500000000C000000010000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100290000000000000001000186A00000000100000000000A00000000000000000000000800000000000065000000510000000C000000030000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100290000000000000001000186A00000000100000000000A00000000000000000000000800000000000098000000520000000CFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000200370000000000000001000186A00000000000007530000A0000000000000000000000000000000001000E000036B0000088B800000190002D0000000200000001000000010000000000007530000A0000000000000000000000030000000100001388000098000000530000000CFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000200370000000000000001000186A00000000000009C40000A0000000000000000000000000000000001000E000036B0000088B800000258002D0000000200000001000000010000000000009C40000A0000000000000000000000030000000100001B5800009C000000540000000C00000004000000000000000000000000001C000000000000000100000001000E000000100000000100000003000200370000000000000001000186A00000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000002EE002D0000000200000001000000010000000000001F40000A0000000000000000000000030000000100001F4000009C000000550000000C0000000600000000000000000000000"
			"0001C000000000000000100000001000E000000100000000100000003000200370000000000000001000186A00000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000003E8002D0000000200000001000000010000000000001F40000A0000000000000000000000030000000100002EE0000065000000560000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001002D0000000000000001000186A00000000100000000000A0000000000000000000000070000000100001F40000065000000570000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001002D0000000000000001000186A00000000100000000000A000000000000000000000007000000010000465000005D000000580000000CFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000100290000000000000001000186A000000000FFFFFFFF000A00000000000000000000000A0000000000005D000000590000000CFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000100290000000000000001000186A000000000FFFFFFFF000A0000000000000000000000090000000000009C0000005A0000000CFFFFFFFF000007D00000000000000000001C000000010000000100000001000E0000000B000000010000AFC80002002D0000000000000001000186A00000000100000000000A000000000000000000000007000000010000C3500000370000000000000001000186A00000000000002710000A0000000000000000000000000000000001000E000036B0000088B800000258009C0000005B0000000CFFFFFFFF000007D00000000000000000001C000000010000000100000001000E0000000B000000010000AFC80002002D0000000000000001000186A00000000100000000000A00000000000000000000000700000001000138800000370000000000000001000186A00000000000002710000A0000000000000000000000000000000001000E000036B0000088B8000003E800AA0000005C0000000CFFFFFFFF000007D000000000000000000026000000010000000000000002000E0000000B000000010000AFC8000A0000000C00000000000200290000000000000001000186A00000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000000FA0000A00000000000000000000000100000002000000560000000001000E0000000000004330000003E800AA0000005D0000000CFFFFFFFF000007D000000000000000000026000000010000000000000002000E0000000B000000010000AFC8000A0000000C00000000000200290000000000000001000186A00000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000001388000A00000000000000000000000100000002000000560000000001000E0000000000004330000005DC00650000005E0000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC80001002D00000000000000010000EA600000000100000000000A000000000000000000000007000000010000EA600000650000005F0000000CFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC80001002D0000000000000001000173180000000100000000000A0000000000000000000000070000000100017318000077000000600000000BFFFFFFFF000000000000000000000000001C000000010000000100000001000E0000000400000001FFFFFFFF0001003F00000000000000010000EA600000000000001F40000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000001900077000000610000000BFFFFFFFF000000000000000000000000001C000000010000000100000001000E0000000500000001000000000001003F0000000000000001000111700000000000000BB8000A00000000000000000000000100000002000000560000000001000E00004B64000088B80000044C0073000000620000000BFFFFFFFF0000000000000000000000000018000000010000000100000001000A00000006000000000001003F0000000000000001000186A00000000000000FA0000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000001F40065000000630000000BFFFFFFFF0000000000000000000000000020000000010000000100000001001200000002000000020000030000000000000100290000000100000001000000020000000100000000000A00000000000000000000000600000000000061000000640000000BFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000090000000100001F40000100290000000000000001000186A00000000000000000000A00000000000000000000000600000000000065000000650000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000001100000000000100310000000000000001000186A00000000100000000000A00000000000000000000000B0000000200000002000003E8000065000000660000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000001100000000000100310000000000000001000186A00000000100000000000A00000000000000000000000B0000000200000002000000C8000069000000670000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000120000000100000028000100310000000000000001000186A00000000100000000000A00000000000000000000000B0000000200000001000003E8000069000000680000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000001200000001000000280001003100000000000000010000C3500000000100000000000A00000000000000000000000B0000000200000001000001F400006D000000690000000DFFFFFFFF0000000000000000000000000020000000000000000100000001001200000013000000020000020000000000000100310000000000000001000186A00000000100000000000A00000000000000000000000B0000000200000002000003E800006D0000006A0000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000130000000200000200000000000001003100000000000000010000C3500000000100000000000A00000000000000000000000B0000000200000002000001F40000650000006B0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000001400000000000100310000000000000001000186A00000000100000000000A00000000000000000000000B0000000200000001000003E80000650000006C0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000014000000000001003100000000000000010000C3500000000100000000000A00000000000000000000000B0000000200000001000001F400006D0000006D0000000DFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020180080400000000000100310000000000000001000186A00000000100000000000A00000000000000000000000B0000000200000002000003E800006D0000006E0000000DFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020180080400000000000100310000000000000001000186A00000000100000000000A00000000000000000000000B0000000200000002000000C80000730000006F0000000DFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100370000000100000001000000030000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000002580073000000700000000DFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100370000000100000001000000030000000000001770000A0000000000000000000000000000000001000E000036B0000088B8000001900073000000710000000DFFFFFFFF0000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100370000000100000001000000030000000000001388000A0000000000000000000000000000000001000E000036B0000088B80000012C0077000000720000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F00000000000000010000EA600000000000001F40000A00000000000000000000000100000002000000010000000001000E0000000000004B64000002EE0077000000730000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F00000000000000010000C3500000000000001B58000A00000000000000000000000100000002000000010000000001000E0000000000004B64000001F40077000000740000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F000000000000000100009C400000000000001388000A00000000000000000000000100000002000000010000000001000E0000000000004B64000001C2007B000000750000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F0000000000000001000186A00000000000000BB8000A00000000000000000000000100000002000000010000000001000E00004B64000088B800000190007B000000760000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F0000000000000001000186A00000000000000BB8000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000000C80077000000770000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000900000001000017700001003F0000000000000001000111700000000000001770000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000001C20077000000780000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000900000001000017700001003F00000000000000010000C3500000000000001770000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000012C0077000000790000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000900000001000017700001003F000000000000000100009C400000000000001388000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000000C8007B0000007A0000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000704000000200001003F0000000100000001000000050000000000001388000A00000000000000000000000100000002000000560000000001000E00004B64000088B800000708007B0000007B0000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000704000000200001003F00000001000000010000000500"
			"00000000000FA0000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000004B0007B0000007C0000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000704000000200001003F0000000100000001000000080000000000000FA0000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000003C0007B0000007D0000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F00000000000000010000EA600000000000001388000A00000000000000000000000100000002000000560000000001000E000000000000433000000708007B0000007E0000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F0000000000000001000075300000000000001388000A00000000000000000000000100000002000000560000000001000E000000000000433000000708007B0000007F0000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001003F0000000000000001000061A80000000000001388000A00000000000000000000000100000002000000560000000001000E0000000000004330000003E800AA000000800000000DFFFFFFFF000007D000000000000000000026000000010000000000000002000E0000000B000000010000AFC8000A0000000D00000000000200290000000000000001000186A00000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000001388000A00000000000000000000000100000002000000560000000001000E0000000000004330000009C400AA000000810000000DFFFFFFFF000007D000000000000000000026000000010000000000000002000E0000000B000000010000AFC8000A0000000D00000000000200290000000000000001000186A00000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000000BB8000A00000000000000000000000100000002000000560000000001000E00000000000043300000070800AA000000820000000DFFFFFFFF000007D000000000000000000026000000010000000000000002000E0000000B000000010000AFC8000A0000000D00000000000200290000000000000001000186A00000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000000BB8000A00000000000000000000000100000002000000560000000001000E000000000000433000000320006F000000830000000DFFFFFFFF000000000000000000000000001C000000010000000100000001000E0000000500000001000000000001003700000000000000010000EA600000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000001F4006F000000840000000DFFFFFFFF000000000000000000000000001C000000010000000100000001000E000000050000000100000000000100370000000000000001000075300000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000001F4006F000000850000000DFFFFFFFF000000000000000000000000001C000000010000000100000001000E00000005000000010000000000010037000000000000000100003A980000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000001F400A4000000860000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000200310000000000000001000186A000000000FFFFFFFF000A000000000000000000000001000000020000000C0000019000003F00000002000000010000000100000000FFFFFFFF000A000000000000000000000001000000020000000C0000000001000E00004B64000088B8000004B000A4000000870000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000200310000000000000001000186A000000000FFFFFFFF000A000000000000000000000001000000020000000C0000009600003F00000002000000010000000100000000FFFFFFFF000A000000000000000000000001000000020000000C0000000001000E00004B64000088B80000034800A4000000880000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000200310000000000000001000186A000000000FFFFFFFF000A000000000000000000000001000000020000000C0000009600003F00000002000000010000000100000000FFFFFFFF000A000000000000000000000001000000020000000C0000000001000E00004B64000088B80000016800A4000000890000000DFFFFFFFF0000000000000000000000000020000000010000000100000001001200000002000000020000010000000000000200290000000100000001000000020000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000000BB8000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000003C000A40000008A0000000DFFFFFFFF0000000000000000000000000020000000010000000100000001001200000002000000020000010000000000000200290000000100000001000000030000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000000BB8000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000002D000A40000008B0000000DFFFFFFFF0000000000000000000000000020000000010000000100000001001200000002000000020000010000000000000200290000000100000001000000040000000100000000000A0000000000000000000000060000000000003F0000000200000001000000010000000000000BB8000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000001E000730000008C0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A00000000000007530000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000005A000730000008D0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A00000000000004E20000A00000000000000000000000100000002000000560000000001000E00004B64000088B80000043800730000008E0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A00000000000003A98000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000002D000650000008F0000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001002D0000000000000001000186A00000000100000000000A0000000000000000000000070000000100001F40000065000000900000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001002D0000000000000001000186A00000000100000000000A0000000000000000000000070000000100000FA0000065000000910000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001002D0000000000000001000186A00000000100000000000A00000000000000000000000700000001000007D0000065000000920000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC80001002D00000000000000010000EA600000000100000000000A0000000000000000000000070000000100011170000065000000930000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC80001002D00000000000000010000C3500000000100000000000A0000000000000000000000070000000100009C40000065000000940000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC80001002D0000000000000001000075300000000100000000000A00000000000000000000000700000001000080E8000077000000950000000DFFFFFFFF000000000000000000000000001C000000010000000100000001000E0000000400000001FFFFFFFF0001003F000000000000000100009C400000000000001770000A00000000000000000000000100000002000000010000000001000E00004B64000088B8000001900077000000960000000DFFFFFFFF000000000000000000000000001C000000010000000100000001000E0000000400000001FFFFFFFF0001003F0000000000000001000061A80000000000001770000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000014D0077000000970000000DFFFFFFFF000000000000000000000000001C000000010000000100000001000E0000000400000001FFFFFFFF0001003F000000000000000100004E200000000000000FA0000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000010B0065000000980000000D000000010000000000000000000000000020000000000000000100000001001200000002000000020000000000000010000100290000000000000001000186A00000000100000000000A00000000000000000000000800000000000065000000990000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000090000000100000BB80001002D0000000000000001000186A00000000100000000000A0000000000000000000000070000000100001F400000610000009A0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001002D0000000000000001000186A000000000FFFFFFFF000A00000000000000000000000300000001000005DC0000610000009B0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001002D0000000000000001000186A000000000FFFFFFFF000A00000000000000000000000300000001000003E80000AA0000009C0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000014000000000002004D0000000000000001000186A00000000000004E20000A00000000000000000000000100000002000000010000000002000E0000000000004B64000001F4000E00004B64000088B80000019000290000000200000001000000010000000100000000000A000000000000000000000008000000000000AA0000009D0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000014000000000002004D0000000000000001000186A00000000000002710000A00000000000000000000000100000002000000010000000002000E0000000000004B64000001F4000E00004B64000088B80000014D00290000000200000001000000010000000100000000000A000000000000000000000008000000000000AA0000009E0000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000014000000000002004D0000000000000001000186A00000000000001F40000A00000000000000000000000100000002000000010000000002000E0000000000004B6400000190000E00004B64000088B80000010B00290000000200000001000000010000000100000000000A000000000000000000000008000000000000650000009F0000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000010000000100004E200001002D0000000000000001000186A00000000000004E20000A0000000000000000000000030000000100000BB8000065000000A00000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000100000001000027100001002D0000000000000001000186A00000000000002710000A0000000000000000000000030000000100000BB8000065000000A10000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000010000000100001F400001002D0000000000000001000186A00000000000001F40000A00000000000000000000000300000001000007D0000069000000A20000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001002D0000000000000001000061A80000000100000000000A00000000000000000000000700000001000059D8000069000000A30000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001002D0000000000000001000032C80000000100000000000A00000000000000000000000700000001000059D8000069000000A40000000DFFFFFFFF00000000000000000000000000200000000000000001000000010012000000020000000200000000000000100001002D0000000000000001000027100000000100000000000A0000000000000000000000070000000100003A98000069000000A50000000EFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000150000000100003A98000100310000000000000001000186A00000000000003A98000A00000000000000000000000400000002000186A0000186A0000061000000A60000000EFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000150000000100003A98000100290000000000000001000186A00000000000003A98000A00000000000000000000000900000000000065000000A70000000EFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000150000000100003A980001002D0000000000000001000186A00000000000003A98000A0000000000000000000000020000000100000BB800006D000000A80000000FFFFFFFFF0000000000000000000000000024000000000000000100000001001600000016000000030000000600000001000000010001002D0000000000000001000186A00000000000003A98000A00000000000000000000000D000000010000000A00006B000000A900000010FFFFFFFF0000000000000000000000000018000000000000000100000001000A0000000000000000000100370000000000000001000186A000000000FFFFFFFF000A0000000000000000000000000000000001000E000036B0000088B80000012C0073000000AA00000010FFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A000000000FFFFFFFF000A00000000000000000000000100000002000000560000000001000E0000000000004330000003E80073000000AB00000010FFFFFFFF0000000000000000000000000018000000000000000100000001000A000000000000000000010"
			"03F0000000000000001000186A000000000FFFFFFFF000A00000000000000000000000100000002000000560000000001000E00004B64000088B8000002D00073000000AC00000010FFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A000000000FFFFFFFF000A00000000000000000000000100000002000000010000000001000E0000000000004B64000001F40073000000AD00000010FFFFFFFF0000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000186A000000000FFFFFFFF000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000012C0073000000AE0000000D000000000000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000000000000000100000000000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000012C0073000000AF0000000D000000000000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000000000000000100000000000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000012C0073000000B00000000D000000000000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000000000000000100000000000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000012C0073000000B10000000D000000000000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000000000000000100000000000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000012C0073000000B20000000D000000000000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000000000000000100000000000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000012C0073000000B30000000D000000000000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000000000000000100000000000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000012C0086000000B40000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A0000001100000000000200290000000000000001000186A00000000100000000000A000000000000000000000008000000000000290000000000000001000186A00000000100000000000A00000000000000000000000800000000000071000000B50000000DFFFFFFFF0000000000000000000000000024000000000000000100000001001600000017000000030000003C0000006400000000000100310000000000000001000186A00000000100000000000A00000000000000000000000B0000000200000002000005DC000071000000B60000000DFFFFFFFF0000000000000000000000000024000000000000000100000001001600000017000000030000003C00000064000000000001003100000000000000010000C3500000000100000000000A00000000000000000000000B0000000200000002000005DC000069000000B70000000DFFFFFFFF0000000000000000000000000024000000000000000100000001001600000017000000030000003C0000006400000000000100290000000000000001000186A00000000100000000000A0000000000000000000000080000000000006F000000B80000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000120000000100000028000100370000000000000001000186A00000000000000BB8000A0000000000000000000000000000000001000E000036B0000088B8000001F4006F000000B90000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E000000120000000100000028000100370000000000000001000186A00000000000000BB8000A0000000000000000000000000000000001000E000036B0000088B8000000FA0077000000BA0000000DFFFFFFFF0000000000000000000000000024000000000000000100000001001600000017000000030000003C0000006400000000000100370000000000000001000186A00000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000005DC0077000000BB0000000DFFFFFFFF0000000000000000000000000024000000000000000100000001001600000017000000030000003C0000006400000000000100370000000000000001000186A00000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000003E80077000000BC0000000DFFFFFFFF0000000000000000000000000024000000000000000100000001001600000017000000030000003C0000006400000000000100370000000000000001000186A00000000000001F40000A0000000000000000000000000000000001000E000036B0000088B8000001F4006D000000BD0000000DFFFFFFFF0000000000000000000000000024000000000000000100000001001600000017000000030000003C00000064000000000001002D0000000000000001000186A00000000000001F40000A0000000000000000000000030000000100004E2000006D000000BE0000000DFFFFFFFF0000000000000000000000000024000000000000000100000001001600000017000000030000003C00000064000000000001002D0000000000000001000186A00000000000001F40000A0000000000000000000000030000000100003A9800006D000000BF0000000DFFFFFFFF0000000000000000000000000024000000000000000100000001001600000017000000030000003C00000064000000000001002D0000000000000001000186A00000000000001F40000A0000000000000000000000030000000100002710000073000000C00000000D000000000000000000000000000000000018000000000000000100000001000A00000000000000000001003F0000000000000001000000000000000100000000000A00000000000000000000000100000002000000010000000001000E00004B64000088B80000012C0065000000C10000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC80001002D00000000000000010000EA600000000100000000000A0000000000000000000000070000000100015F90000065000000C20000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC80001002D00000000000000010000EA600000000100000000000A0000000000000000000000070000000100011170000065000000C30000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000B000000010000AFC80001002D00000000000000010000EA600000000100000000000A00000000000000000000000700000001000080E80000AA000000C40000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000014000000000002004D0000000000000001000186A00000000000004E20000A00000000000000000000000100000002000000010000000002000E0000000000004B64000001F4000E00004B64000088B80000019000290000000200000001000000010000000100000000000A000000000000000000000008000000000000AA000000C50000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000014000000000002004D0000000000000001000186A00000000000002710000A00000000000000000000000100000002000000010000000002000E0000000000004B64000001F4000E00004B64000088B80000014D00290000000200000001000000010000000100000000000A000000000000000000000008000000000000AA000000C60000000DFFFFFFFF0000000000000000000000000018000000000000000100000001000A00000014000000000002004D0000000000000001000186A00000000000001F40000A00000000000000000000000100000002000000010000000002000E0000000000004B6400000190000E00004B64000088B80000010B00290000000200000001000000010000000100000000000A00000000000000000000000800000000000077000000C70000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F00000000000000010000EA600000000000001F40000A00000000000000000000000100000002000000010000000001000E0000000000004B64000002EE0077000000C80000000DFFFFFFFF000000000000000000000000001C000000000000000100000001000E0000000800000001000000010001003F00000000000000010000C3500000000000001B58000A00000000000000000000000100000002000000010000000001000E0000000000004B64000001F40C000F0000000700000001000003E8000035000000000000000003000E0000000000001F4000002710000E00001F4000003A9800002710000E00003A980000520800002710003D000000010000000200000001000007D003000E0000000000001F4000002710000E00001F4000003A9800002710000E00003A980000520800002710003D000000010000000200000001000007D003000E0000000000001F4000002710000E00001F4000003A9800002710000E00003A980000520800002710003D000000010000000200000001000007D003000E0000000000001F4000002710000E00001F4000003A9800002710000E00003A980000520800002710003D000000010000000200000001000007D003000E0000000000001F4000002710000E00001F4000003A9800002710000E00003A980000520800002710003D000000010000000200000001000007D003000E0000000000001F4000002710000E00001F4000003A9800002710000E00003A980000520800002710003D000000010000000200000001000007D003000E0000000000001F4000002710000E00001F4000003A9800002710000E00003A980000520800002710003D000000010000000200000001000007D003000E0000000000001F4000002710000E00001F4000003A9800002710000E00003A980000520800002710000F00000003000000010000C35000000F00000002000000010000753000000F000000050000000100061A800001");
		len = p - buf.data();
		SendToClient(Client, 28404, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
	}
}
