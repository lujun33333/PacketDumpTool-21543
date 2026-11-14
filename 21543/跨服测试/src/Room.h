#pragma once
#include "Message.h"


#ifdef ZingSpeed
constexpr int MaxRoomName = 66;
#else
constexpr int MaxRoomName = 22;
#endif

constexpr int ROOM_SEAT_NORMAL_MAX = 6;
constexpr int ROOM_SEAT_12_MAX = 12;
constexpr int ROOM_SEAT_MAX = 12;
using RoomSeatMask = USHORT;

class RoomNode;
class ChatRoomNode;
typedef void(WINAPI* RoomOnTimer)(std::shared_ptr<RoomNode>);

struct CustomRaceStartSnapshotEntry
{
	char SeatID = -1;
	BYTE TrackSlot = 0xFF;
	DWORD Uin = 0;
	USHORT ConnID = 0;
	UINT GameID = 0;
	UINT MapID = 0;
	UINT KartID = 0;
	UINT KartSkinID = 0;
	UINT VisualKartID = 0;
	UINT PhysLoadID = 0;
	bool KartPhysReady = false;
};

// Border settlement keeps race membership independent from mutable room slots.
// Membership/seat identity is frozen at start; only runtime result fields below
// are refreshed when a racer finishes or leaves.
struct BorderRaceStartRosterEntry
{
	char SeatID = -1;
	DWORD Uin = 0;
	USHORT ConnID = 0;
	USHORT OriginalRoomID = 0;
	bool Finished = false;
	bool Quit = false;
	UINT FinishTime = 0xFFFFFFFF;
	UINT TeamID = 0;
	UINT KartID = 0;
	UINT KartSkinID = 0;
	UINT HighestSpeed = 0;
	UINT CollisionCount = 0;
	UINT N2OGatherCount = 0;
	UINT DriftCount = 0;
	RaceDetailRecord RaceDetail;
	UINT GradeLevel = 0;
	UINT Experience = 0;
	int SuperMoney = 0;
	int TotalHonor = 0;
};

enum class BorderRaceSettlementPhase : BYTE
{
	Idle = 0,
	Racing,
	CountDown,
	RaceShow,
	Result,
	Award,
	Returning,
	Complete
};

enum class RankedMatchStartPhase : BYTE
{
	Idle = 0,
	AwaitEnterRoom,
	EnterRoomComplete,
	BanMapChoosing,
	BanMapResult,
	Loading,
	RaceBeginPending,
	Racing,
	Failed
};

class RoomNode
{
public:
	UINT ID = 0;
	char Name[MaxRoomName];
	USHORT RoomOwnerID = 0;
	DWORD RoomOwnerUin = 0;
	USHORT ServerID = 0;
	USHORT BaseGameMode = 0;
	USHORT SubGameMode = 0;
	BYTE SeatNum = 0;
	BYTE CurrentPlayerNum = 0;
	USHORT Flag = 0x18C;//标志位
	char Password[16] = { 0 };//房间密码
	std::vector<std::shared_ptr<ClientNode>> Player;
	char SeatState[ROOM_SEAT_MAX] = { 1 };//座位状态
	HANDLE Timer = NULL;
	RoomOnTimer Callback = NULL;//std::shared_ptr<RoomNode> 指针 
	BYTE ChatRoomType = 0;
	std::shared_ptr<ChatRoomNode> OutdoorRoom;
	BYTE SpeFlag = 0; // custom room uses 9
	BYTE PresetOBNum = 0;
	USHORT CreateRoomType = 0;
	USHORT ServerMatchType = 0;
	USHORT TeamMatchType = 0;
	bool TwelvePlayerCustomRoomShell = false; // SeatNum=12 创建出的 12 人自定义房壳标记
	bool LuckyMatchInternalRoom = false; // internal LuckyMatch room hidden from normal 102/104
	//
	BYTE AllowAutoStart = 0;
	UINT MapID = 0;
	USHORT RaceGameID = 0;
	UINT RaceEnvelopeContext = 0;
	BYTE RaceStartPlayerNum = 0;
	RoomSeatMask RaceActiveMask = 0;
	std::vector<UINT> RaceStartUins;
	std::array<UINT, 20> RaceTrackOrders{};
	bool RaceTrackOrdersValid = false;
	UINT RaceSeed = 0;
	UINT RaceGameSeq = 0;
	UINT RaceMatchSeqHigh = 0;
	UINT RaceMatchSeqLow = 0;
	std::array<BYTE, 74> RaceMultiInfo{};
	bool RaceAuthorityValid = false;
	bool BorderRaceSnapshotValid = false;
	RoomSeatMask BorderRaceStartMask = 0;
	RoomSeatMask BorderRaceConnectedMask = 0;
	RoomSeatMask BorderRaceFinishedMask = 0;
	RoomSeatMask BorderRaceQuitMask = 0;
	BorderRaceSettlementPhase BorderRacePhase = BorderRaceSettlementPhase::Idle;
	std::vector<BorderRaceStartRosterEntry> BorderRaceStartRoster;
	UINT Israce = 0;
	UINT IsAnonymous = 0;
	UINT isSkip = 0;
	UINT IsStake = 0;
	UINT IsBottomGame = 0;
	UINT BottomGameNum = 0;
	UINT BottomPlayerNum = 0;
	std::vector<CMatchBanMapInfo> MatchBanMaps;
	BYTE MatchBanMapType = 0;
	RankedMatchStartPhase RankedStartPhase = RankedMatchStartPhase::Idle;
	RoomSeatMask RankedEnteredMask = 0;
	bool RankedRaceBeginSent = false;
	std::shared_ptr<RankedSettlementState> RankedSettlement;
	std::string GVoiceRoomName = "";
	class
	{
	public:
		char Season;
		char Timeslot;
		char Weather;
	} STDynamicInfo;

	//Dance
	int SceneID = 0;
	int MusicID = 0;
	UINT startStatus = 1;
	//
	//char BorderFlag;
	USHORT matchQueueType = 0;
	// Official Aurora UGC room/race metadata. The client downloads the render
	// package directly from AuroraRenderDataCosURL and verifies AuroraMD5.
	bool AuroraUGC = false;
	std::uint64_t AuroraWorkID = 0;
	std::uint32_t AuroraVersion = 0;
	std::string AuroraRenderDataCosURL;
	std::string AuroraMD5;
	char CountDownFlag = 0;
	char ranknumber = 0;
	DWORD dwStart = 0;
	// ---- 24640 CustomScheme / custom multiplayer ----
	bool HasCustomScheme = false;
	BYTE  CustomAllocSvrType = 14;
	UINT  CustomSchemeID = 0;
	USHORT CustomBaseGameMode = 0;
	USHORT CustomSubGameMode = 0;
	BYTE  CustomEliminationIntervalTs = 10;
	BYTE  CustomEliminationRanking = 0;
	BYTE  CustomRoundNum = 0;
	BYTE  CustomN2OMode = 0;
	int   CustomN2OEffect = 100;
	BYTE  CustomSmallSpray = 1;
	BYTE  CustomCollision = 0;
	BYTE  CustomGem = 1;
	BYTE  CustomCostume = 0;
	BYTE  CustomModification = 1;
	BYTE  CustomMapNum = 0;
	UINT  CustomMapIDList[3] = { 0, 0, 0 };
	UINT  CustomStageMapOrder[3] = { 0, 0, 0 };
	BYTE  CustomStageMapOrderNum = 0;
	DWORD CustomStageMapSeed = 0;
	BYTE  CustomNoCollisionSeconds = 5;
	BYTE  CustomCurrentStageIndex = 0;
	BYTE  CustomLastFinishStageIndex = 0xFF;
	DWORD CustomLastStageFinishTick = 0;
	BYTE  CustomStageArriveStageIndex = 0xFF;
	RoomSeatMask CustomStageArriveMask = 0;
	BYTE  CustomStageArriveCount = 0;
	BYTE  CustomStageArriveNeed = 0;
	DWORD CustomStageFirstArriveTick = 0;
	std::vector<BYTE> CustomSchemeWire;
	bool  CustomWaitingPrepareReadyBeforeLoad = false;
	bool  CustomPreloadReadySatisfied = false;
	RoomSeatMask CustomPreloadReadyMask = 0;
	bool  CustomWaitingRaceBegin = false;
	bool  CustomRaceBeginSent = false;
	bool  CustomLoadingDispatching = false;
	RoomSeatMask CustomLoadReadyMask = 0;
	DWORD CustomLoadingStartTick = 0;
	UINT  CustomLastSentMsgID = 0;
	bool  CustomRaceSnapshotValid = false;
	UINT  CustomRaceSnapshotRoomID = 0;
	BYTE  CustomRaceSnapshotPlayerNum = 0;
	RoomSeatMask CustomRaceSnapshotMask = 0;
	DWORD CustomRaceSnapshotTick = 0;
	bool  CustomRaceKartPhysAllReady = false;
	RoomSeatMask CustomRaceKartPhysReadyMask = 0;
	RoomSeatMask CustomRaceKartPhysMissingMask = 0;
	std::vector<CustomRaceStartSnapshotEntry> CustomRaceStartSnapshot;
	DWORD dwEnd = 0;
	RoomNode()
	{
		Player.resize(ROOM_SEAT_MAX);
	}
	std::shared_mutex mutex; 
};

std::shared_ptr<RoomNode> NewRoom();
std::shared_ptr<RoomNode> NewBorderRoomWithID(UINT RoomID);
std::shared_ptr<RoomNode> GetRoom(long RoomID);
std::shared_ptr<RoomNode> GetBorderRoom(long RoomID);
// Resolves exactly one authoritative active race room. A non-zero BorderRoomID
// owns the session even when RoomID is retained for a post-race team return.
// The two room namespaces are never used as fallbacks for each other.
std::shared_ptr<RoomNode> GetActiveRaceRoom(const std::shared_ptr<ClientNode>& Client);
bool BeginRoomGameLogicSession(const std::shared_ptr<RoomNode>& Room, const char* reason);
void ClearRoomGameLogicSession(const std::shared_ptr<RoomNode>& Room, const char* reason);
bool IsBorderTeamMatchRoom(const std::shared_ptr<RoomNode>& Room);
void LeaveRoom(std::shared_ptr<ClientNode> Client, short LeaveType, bool sendLeaveAck = true);
void LeaveBorderRoom(std::shared_ptr<ClientNode> Client, short LeaveType, bool sendLeaveAck = true);
void ClearFinishedBorderRoom(std::shared_ptr<RoomNode> Room, const char* reason);
void ScrubClientFromRoomsForDisconnect(const std::shared_ptr<ClientNode>& Client, long exceptRoomId, const char* reason);
void PrintRoom();
void CreateRoomTimer(std::shared_ptr<RoomNode> Room, int Timeout, RoomOnTimer Callback);

bool IsCustomRoomFlow(const std::shared_ptr<RoomNode>& Room);
bool IsTwelvePlayerRoom(const std::shared_ptr<RoomNode>& Room);
int GetRoomSeatLimitBySeatNum(BYTE SeatNum);
int GetRoomSeatLimit(const std::shared_ptr<RoomNode>& Room);
int GetRoomSeatCapacity(const std::shared_ptr<RoomNode>& Room);
RoomSeatMask GetRoomSeatBit(int seat);
int CountRoomPlayers(const std::shared_ptr<RoomNode>& Room);
std::vector<std::shared_ptr<ClientNode>> BuildCustomRaceRoster(const std::shared_ptr<RoomNode>& Room);
void ClearCustomRaceStartSnapshot(std::shared_ptr<RoomNode> Room, const char* reason);
bool EnsureCustomRaceStartSnapshot(std::shared_ptr<RoomNode> Room, const char* reason, bool rebuild = false);
bool FindCustomRaceStartSnapshotEntry(const std::shared_ptr<RoomNode>& Room, UINT Uin, CustomRaceStartSnapshotEntry* OutEntry = nullptr);
USHORT GetRoomConnPlayerID(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& Client);
USHORT GetRoomConnOwnerID(const std::shared_ptr<RoomNode>& Room);
bool IsRoomConnOwner(const std::shared_ptr<RoomNode>& Room, const std::shared_ptr<ClientNode>& Client);
bool EnsureTwelvePlayerDefaultCustomScheme(std::shared_ptr<RoomNode> Room, const char* reason);
bool NormalizeCustomSchemeForOfficial(std::shared_ptr<RoomNode> Room, const char* reason);
void CancelRoomTimer(std::shared_ptr<RoomNode> Room, const char* reason);
void ClearCustomRoomState(std::shared_ptr<RoomNode> Room, const char* reason);
void ScheduleCustomRaceBeginWithDance(std::shared_ptr<RoomNode> Room, const char* reason);
void WINAPI OnCustomEnterLoading(std::shared_ptr<RoomNode> Room);
BYTE BuildCustomStageMapOrder(std::shared_ptr<RoomNode> Room);
BYTE GetCustomStageCount(const std::shared_ptr<RoomNode>& Room);
UINT GetCustomStageMapID(const std::shared_ptr<RoomNode>& Room, BYTE stageIndex, UINT fallbackMapID);
USHORT GetCustomOfficialTotalRound(const std::shared_ptr<RoomNode>& Room, UINT mapID, USHORT physicalRound);
UINT GetCustomDisplayMapID(const std::shared_ptr<RoomNode>& Room);
BYTE GetEffectiveRoomSpeFlag(const std::shared_ptr<RoomNode>& Room);
bool CaptureRoomRaceStartSnapshot(std::shared_ptr<RoomNode> Room, const char* reason);
bool UpdateBorderRaceSnapshotFromClient(std::shared_ptr<RoomNode> Room, const std::shared_ptr<ClientNode>& Client, bool markQuit, const char* reason);
void HandlePlayerLeaveRace(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, const char* reason);
void EnsureRoomOwnerAlive(std::shared_ptr<RoomNode> Room, std::shared_ptr<ClientNode> preferred, const char* reason);
void NotifyRoomStateAfterEnter104(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, const char* tag);
void NotifyOfficialReturnReadyObRoster(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, const char* reason);
void NotifyOfficialReturnSelfReadyObState(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, const char* reason);
void BeginOfficialRoomReturnFlow(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, BYTE Kind, BYTE Phase, const char* reason);
bool AdvanceOfficialRoomReturnFlow(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, BYTE ExpectedPhase, BYTE NextPhase, const char* reason);
bool TryHandleOfficialRoomReturnAction62(std::shared_ptr<ClientNode> Client);
bool CanAcceptOfficialRoomReturnReadyChange(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
void CompleteOfficialRoomReturnReadyChange(std::shared_ptr<ClientNode> Client, const char* reason);
void ClearOfficialRoomReturnFlow(std::shared_ptr<ClientNode> Client, const char* reason);


void RequestGetRoomList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetRoomList(std::shared_ptr<ClientNode> Client, USHORT BaseGameMode, USHORT SubGameMode, std::vector<std::shared_ptr<RoomNode>>& AllRooms, int RoomNum, int StartRoomIdx, BYTE RoomFlag);
void ResponseGetRoomList2(std::shared_ptr<ClientNode> Client, WORD RoomID);
void StartPurplePractice(std::shared_ptr<ClientNode> Client, UINT MapID);
void RequestPurplePracticePrepareReady(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void StartSingleGame(std::shared_ptr<ClientNode> Client, BYTE* Body);
void RequestCreateRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseCreateRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, BYTE ChatRoomType);
void ResponseBorderTeamReturnRoom103(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
void ResponseCreateHyperSpaceJumboRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
void RequestAddToTeamMatchQueue(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestEnterRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
enum class EnterRoom104RosterMode : BYTE
{
	Empty = 0,              // 官服自定义/进入房壳：104 不带玩家 roster
	OtherVisiblePlayers = 1 // 官服自定义多人回房：104 带除接收者外的 visible/relation/kart
};
void ResponseEnterRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, char SeatID, BOOL HasMoreInfo, char index, EnterRoom104RosterMode RosterMode = EnterRoom104RosterMode::Empty);
void NotifyEnterRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, std::shared_ptr<ClientNode> RoomClient, char SeatID);

void RequestLeaveRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseLeaveRoom(std::shared_ptr<ClientNode> Client, short LeaveType);
void NotifyLeaveRoom(std::shared_ptr<ClientNode> Client, UINT uin, char SeatID, bool IsChangeRoomOwner, USHORT NewRoomOwnerID, short LeaveType);

void RequestStartGame(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseStartGame(std::shared_ptr<ClientNode> Client, const char* Reason);

void RequestChangeMap(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseChangeMap(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, bool RoomTypeChanged = false);
void NotifyChangeMap(std::shared_ptr<ClientNode> Client, UINT Uin, std::shared_ptr<RoomNode> Room, bool RoomTypeChanged = false);

void RequestChangeSeatState(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseChangeSeatState(std::shared_ptr<ClientNode> Client, BYTE SeatID, BYTE SeatState);

void RequestKickFromRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseKickFromRoom(std::shared_ptr<ClientNode> Client, UINT DstUin);

void RequestChangeReadyState(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseChangeReadyState(std::shared_ptr<ClientNode> Client, BYTE ReadyState);

void RequestTalk(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseTalk(std::shared_ptr<ClientNode> Client, BYTE Type, BYTE ClientUseFlag, UINT DestPlayerUin, BYTE BugletType);
void NotifyTalk(std::shared_ptr<ClientNode> Client, std::shared_ptr<ClientNode> RoomClient, BYTE Type, BYTE ClientUseFlag, UINT DestPlayerUin, BYTE BugletType, short ContentLength, const char* Content, const BYTE* ContentId);
void RequestChangeRoomOwner(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void NotifyChangeRoomOwner(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
