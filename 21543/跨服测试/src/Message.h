#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef _DEBUG
#define DEBUG
#endif

#include "HPSocket/HPSocket.h"


#include "spdlog/spdlog.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/async.h"
#include "ConnectionPool.h"
#pragma comment (lib,"spdlog.lib")
#include "Utils.h"
#include <io.h>
#include <array>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>
#ifdef ZingSpeed
constexpr int MaxNickName = 51;
constexpr int MaxQQNickName = 60;
constexpr int MaxSignature = 128;

constexpr int MAX_MUCH_SEATNUMINROOM = 6;
constexpr int MAXNPCNUMINROOM = 6;
#else
constexpr int MaxNickName = 17;//飞车名字长度
constexpr int MaxQQNickName = 20;//qq名字长度
constexpr int MaxSignature = 64;//签名长度
constexpr int MaxSecPwd = 11;

constexpr int MAX_MUCH_SEATNUMINROOM = 20;
constexpr int MAXNPCNUMINROOM = 20;
constexpr int MaxQQHeadUrl = 105;
#endif

#define NOINLINE __declspec(noinline)

#define WORDSWAP(w)		(((w) >> 8) | \
						(((w) << 8) & 0xFF00))

#define DWORDSWAP(dw)	(((dw) >> 24) | \
						(((dw) >> 8) & 0x0000FF00) | \
						(((dw) << 8) & 0x00FF0000) | \
						(((dw) << 24) & 0xFF000000))


#pragma pack(push, 1)


class RoomNode;
struct RankedSettlementState;
struct CarTreasureInfo;
typedef struct OpenBoxDef
{
	DWORD Uin;
	DWORD Time;
	DWORD BoxID;
	WORD BoxType;//BoxType
	DWORD ItemNum;
	WORD KeyType;//10
	DWORD KeyID;//KeyID
	DWORD KeyNum;//Num
}*LPOpenBoxDef;

typedef struct OpenBox2Def
{
	DWORD Uin;
	DWORD Time;
	DWORD BoxID;
	WORD BoxType;//BoxType
	DWORD KeyNum;
	WORD KeyType;//10
	BYTE tm1;
	DWORD ItemNum;//Num
}*LPOpenBox2Def;

#pragma pack(pop)

struct ShoppingCar
{
	int CommodityID;
	int PriceIdx;
	int DiscountCardID;
};

struct RaceDetailRecord
{
	UINT Uin = 0;
	std::string NickName;
	int KartId = 0;
	UINT FinTime = 0;
	UINT HighestSpeed = 0;
	UINT AverageSpeed = 0;
	UINT CollisionCount = 0;
	UINT N2OGatherCount = 0;
	UINT JetTime = 0;
	UINT ChaoJiQiBu = 0;
	UINT ZhuangQiangDianPen = 0;
	UINT DriftCount = 0;
	UINT SmallJetCount = 0;
	UINT DoubleJetCount = 0;
	UINT FlyJetCount = 0;
	UINT FallJetCount = 0;
	UINT DuanWeiQiBu = 0;
	UINT DuanWeiDoubleJetCount = 0;
	UINT LinkJetCount = 0;
	UINT WCJetCount = 0;
	UINT CeShenDriftCount = 0;
	UINT ShuaiWeiDriftCount = 0;
	UINT CWWJetCount = 0;
	UINT XiaoGuaCount = 0;
	UINT TeamID = 0;
	UINT FastCornersNum = 0;
	USHORT DiffCornerNum = 0;
	UINT FastCornerDetailInfo = 0;
	USHORT WSingleJetCount = 0;
	USHORT WCWJetCount = 0;
	USHORT CWWJetCount1 = 0;
	USHORT CeShenJetCount = 0;
	USHORT ShuaiWeiJetCount = 0;
	short RankedMatchGrade = 0;
	USHORT DuanWeiPiaoYi = 0;
	USHORT DuanWeiJet = 0;
	USHORT OutOfTurnQuickly = 0;
	USHORT Launch = 0;
	USHORT HitWaterDrift = 0;
	USHORT OutWaterJet = 0;
	USHORT N2DriftContinue = 0;
	USHORT WaveFlyJet = 0;
	USHORT WorldID = 0;
	UCHAR Npc = 0;
	UINT GameSeq = 0;
	UINT Time = 0;
	UINT RMTopRaceScore = 0;
};

struct PlayerRaceInfo
{
	UINT Uin;
	//UINT Round;
	UINT FinishTime;
	UINT Teamid;
	UINT kartid;
	UINT DriftCount = 0;
	UINT HighestSpeed = 0;
	UINT CollisionCount = 0;
	UINT N2OGatherCount = 0;
	RaceDetailRecord Detail;
	UINT RankGradeLevel = 0;
	UINT DistanceToEnd = 0;
	UINT GradeLevel = 0;
	UINT ChildLevel = 0;
	UINT GradeScore = 0;
	USHORT VipFlag = 0;
	UINT NobleLevel = 0;
	UINT GuildVipLevel = 0;
	UINT LoverVipLevel = 0;
	UINT GuildID = 0;
	UINT PetID = 0;
};

struct AppellationInfo
{
	UINT Type;
	UINT Level;
	UINT Difficulty;
	UINT MapId;
	UINT Value;
};

typedef struct DstInfo
{
	USHORT PlayerID;
	UINT Uin;
} DstInfo;

struct AuctionItemInfo
{
	UINT ItemID = 0;
	UINT BidPrice = 0;
	int ItemNum = 0;
	int AvailPeriod = 0;
	int StartingPrice = 0;
	int IncreasePrice = 0;
	UCHAR CommodityId = 0;
	std::string Time = "";
};
struct AuctionCfg
{
	char Open = 0;
	int AuctionItemNum = 0;
	std::vector<AuctionItemInfo> AuctionItems;
	std::string Content = "";
};
struct PetInfo
{
	UINT ID = 0;
	char Name[23] = {};
	UINT Exp = 0;
	UINT MaxExp = 0;
	UINT Level = 0;
	UINT Status = 0;
	UINT PL = 0;
	UINT StrengLevel = 0;
	UCHAR IsProper = 0;
	USHORT LevelLimit = 0;
	USHORT TotalPL = 0;
	UCHAR TopStatus = 0;
	UINT HuanHuaLevel = 0;
	UCHAR CanHuanHua = 0;
	UCHAR CanDrive = 0;
	UCHAR Drive = 0;
	USHORT PKStar = 0;
	UINT PetSkinID = 0;
	unsigned long long RoleID = 0;
};

struct MechTreasureInfo {
	UINT ItemId = 0;
	UINT ItemNum = 0;
	INT AvailPeriod = 0;
	UINT exchangePieceNum = 0;
	UINT exchangeNeedNum = 0;
	UINT maxGetNum = 0;
	UINT award_idx = 0;
	UINT item_type = 0;
	UINT status = 0;
	UINT getnum = 0;
	UINT errid = 0;
	UINT coupons = 0;
	UINT supermoney = 0;
	UINT merge = 0;
	int alreadyGot = 0;
	int DecomposeNum = 0;
	int seed = 0;
};

struct MapInfo
{
	UINT MapID;
	UINT Record;
};

struct ChallengeMap
{
	UINT Uin;
	UINT MapID;
	UINT RankID;
	UINT GuildID;
	int UseTime;
	UINT KartID;
	std::string Name;
	std::string GuildName;
	std::string FileMd5;
	std::string UUid;
	std::string LastUUid;
	UINT KeepCnt;
	UINT ChallengeCnt;
	char Status;
};

struct ItemStatus
{
	UINT ItemID;
	BYTE NewStatus;
};



struct ItemInfo
{
	UINT ItemID = 0;
	int ItemNum = 0;
	int AvailPeriod = 0;
	bool Status = 0;
	int ObtainTime = 0;
	int OtherAttribute = 0;
	int ItemType = 0;
	UINT ComsumeItemId = 0;
	UINT ComsumeItemNum = 0;
};
struct QQSIllusionSuitVisibleInfo
{
	UINT ItemID = 0;
	BYTE ShapeIdx = 0;
};
struct KartRefitItemInfo
{
	UINT KartId;
	UINT RefitCout;
	UINT MaxFlags;
	UINT WWeight;
	UINT SpeedWeight;
	UINT JetWeight;
	UINT SJetWeight;
	UINT AccuWeight;
	UINT Speed2Weight;
	UINT DriftVecWeight;
	UINT AdditionalZSpeedWeight;
	UINT AntiCollisionWeight;
	UINT LuckyValue = 0;
	UINT RefitLuckyValueMaxWeight = 0;
	UINT ShapeRefitCount;
	UINT KartHeadRefitItemID;
	UINT KartTailRefitItemID;
	UINT KartFlankRefitItemID;
	UINT KartTireRefitItemID;
	UINT ShapeSuitID;
	UINT SecondRefitCount;

	UINT LegendSuitLevel;
	UINT LegendSuitLevelChoice;
	UINT ShapeLegendSuitID;
	UINT SkinSuitID;
	UCHAR ShapeOriginLevel;
};
struct KartStoneItemInfo
{
	UINT KartId;
	UINT StoneID;
	UINT StoneUseOccaType;
	UINT SkillStoneID;
};
struct NpcRacerFinishInfo
{
	UINT NpcID;
	int FinishTime;
	UINT Experience;
	UINT SuperMoney;
	UINT TotalHonor;
	UINT Money;
	char ranknumber;
	BOOL IsFinish = 0;
};


struct MapChallengeAssessConfig
{
	std::string type = "cw_jet";
	UINT count = 10;
	std::string msg = "完成比赛并使用CW喷{tc}次";
	std::string msgGame = "使用CW喷{tc}次：";
	UINT assessType = 22;
};

struct MapChallengeOpenMainMap
{
	UINT Mapid;
	UINT Num;
	UINT firsttime;
	UINT secondtime;
	UINT thirdtime;
	std::string msg;
	MapChallengeAssessConfig assess;
};

struct MapChallengeOpenMain
{
	std::string msg;
	std::vector<MapChallengeOpenMainMap> Maps;
};

struct MoveInfo
{

	UINT Uin;
	char WalkStatue;
	int Posion[3];
	int Ori[9];
	UINT AttachUin;
	int WalkExtState;
	UCHAR Nums;
	UINT PermitSitHorsePetUins[5];
	int EquipedPetID;
	int EquipedPetStatus;
	int PetPKResultStatus;
	USHORT HeroMajorState;
	USHORT HorsePetStatus;
	int NextPoint;
	short PathLen;
	int Path[50];
	int CurCellID;
	USHORT Speed;
	int CellID;
	USHORT Flag;
};

struct EngageInfo
{
	UINT UIN = 0;
	char NickName[MaxNickName];
	char BabyName1[MaxNickName];
	char BabyName2[MaxNickName];
	UINT BabyOneID = 0;
	UINT BabyOneItemID = 0;
	UINT BabyOneEquipStat = 0;
	UINT BabyOneGender = 0;
	UINT BabyTwoID = 0;
	UINT BabyTwoItemID = 0;
	UINT BabyTwoEquipStat = 0;
	UINT BabyTwoGender = 0;
	UINT BackGroundID = 0;
	UINT EngageTime = 0;

};

struct PlayerInfo
{
	std::string LastLoginTime;
	UINT Uin;
	UINT Index;
	UINT VipFlag;
	UINT NobleID;
	UINT NobleLevel;
	UINT NoblePoint;
	UINT GuildVipLevel;

	UINT GuildVipPoint;
	UINT LoverVipLevel;
	UINT LoverVipPoint;
	UINT EmperorLevel;
	UINT EmperorPoint;
	UINT GardenLevel;
	UINT GardenPoint;
	UINT UserHeads;
	UINT UserHeadsBox;
	UINT UserFloor;
	std::string qqNumber;
};


struct ClientInfo
{
	std::string NickName;
	UINT Uin;
	UINT Gender;
	UINT Country;
	UINT License;
	UINT Experience;
	UINT SuperMoney;
	UINT Money;
	UINT WinNum;
	UINT SecondNum;
	UINT ThirdNum;
	UINT TotalNum;
	UINT CurHonor;
	UINT TotalHonor;
	UINT TodayHonor;
	UINT RelaxTime;
	UINT MonthDurationBefor;
	UINT MonthDurationCur;
	UINT Charm;
	UINT DurationGame;
	UINT DanceExp;
	UINT Coupons;
	UINT Admiration;
	UINT LuckMoney;
	UINT TeamWorkExp;
	UINT AchievePoint;
	UINT RegTime;
	std::string Signature;
	UINT SpeedCoin;
	UINT BugletTalk;
	UINT nitrogen;
	UINT admin;
	UINT fps;
	UINT UserHeads;
	UINT UserHeadsBox;
	UINT UserFloor;
	UINT SecPwdStatus;
	UINT Verify;
	UINT gm;
	USHORT SkipFlag;
	UINT SpeclEffect;
	std::string SecPwd;
};

struct StrengthenBagPlayerState
{
	static constexpr std::size_t AwardSlotCount = 5;
	std::array<INT, AwardSlotCount> AwardIndexes{};
	INT DayActivityBagNum = 0;
	INT DayOfflineBagNum = 0;
};

#ifndef QQS_CLIENT_SAVED_FLAG_BYTES
#define QQS_CLIENT_SAVED_FLAG_BYTES 25
#endif

#ifndef QQS_CLIENT_SAVED_FLAG_MAX_BIT
#define QQS_CLIENT_SAVED_FLAG_MAX_BIT 192
#endif

#ifndef QQS_EX_RIGHT_FLAG_BYTES
#define QQS_EX_RIGHT_FLAG_BYTES 24
#endif

#ifndef QQS_SPEED_LADDER_LOBBY_GATE_MS
#define QQS_SPEED_LADDER_LOBBY_GATE_MS 60000
#endif

#ifndef QQS_SPEED_LADDER_RACE_GATE_MS
#define QQS_SPEED_LADDER_RACE_GATE_MS 600000
#endif

#ifndef QQS_SPEED_LADDER_LOBBY_STAGE_NONE
#define QQS_SPEED_LADDER_LOBBY_STAGE_NONE 0
#define QQS_SPEED_LADDER_LOBBY_STAGE_28458 1
#define QQS_SPEED_LADDER_LOBBY_STAGE_28457 2
#define QQS_SPEED_LADDER_LOBBY_STAGE_28459 3
#define QQS_SPEED_LADDER_LOBBY_STAGE_TYPE449 4
#endif

constexpr BYTE QQS_OFFICIAL_ESC_FLOW_NONE = 0;
constexpr BYTE QQS_OFFICIAL_ESC_FLOW_SINGLE_RETURN_ROOM = 1;
constexpr BYTE QQS_OFFICIAL_ESC_FLOW_MULTI_RETURN_HALL = 2;
constexpr BYTE QQS_OFFICIAL_ESC_FLOW_BORDER_RETURN_HALL = 3;

constexpr BYTE QQS_OFFICIAL_ESC_PHASE_NONE = 0;
constexpr BYTE QQS_OFFICIAL_ESC_PHASE_WAITING_CLIENT_105 = 1;
constexpr BYTE QQS_OFFICIAL_ESC_PHASE_LEFT_AWAITING_NEXT = 2;

// C109/S109/S504 使用 ReadyState 枚举：0=准备、1=取消准备/未准备。
constexpr BYTE QQS_ROOM_READY_STATE_READY = 0;
constexpr BYTE QQS_ROOM_READY_STATE_NOT_READY = 1;

// PlayerRoomVisibleInfo::Status（S104/S500）使用相反的布尔准备标记：
// 0=未准备、1=已准备。官服同一玩家会先以 S500 Status=0 进房，
// 随后通过 S504 ReadyState=0 切换为准备，两个字段不能直接复用原值。
constexpr BYTE QQS_PLAYER_VISIBLE_STATUS_NOT_READY = 0;
constexpr BYTE QQS_PLAYER_VISIBLE_STATUS_READY = 1;
inline BYTE QQS_ToPlayerVisibleStatus(BYTE ReadyState)
{
	return ReadyState == QQS_ROOM_READY_STATE_READY
		? QQS_PLAYER_VISIBLE_STATUS_READY
		: QQS_PLAYER_VISIBLE_STATUS_NOT_READY;
}

constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_KIND_NONE = 0;
constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_KIND_NORMAL = 1;
constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_KIND_CUSTOM = 2;
constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_KIND_TWELVE_CUSTOM = 3;
constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_KIND_BORDER_TEAM = 4;

constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_PHASE_NONE = 0;
constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_PHASE_SERVER_CHAIN_PENDING = 1;
constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_PHASE_WAIT_AT62_SEND_ROSTER = 2;
constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_PHASE_WAIT_AT62_ACK_ONLY = 3;
constexpr BYTE QQS_OFFICIAL_ROOM_RETURN_PHASE_AT62_COMPLETE = 4;
constexpr DWORD QQS_OFFICIAL_ROOM_RETURN_TIMEOUT_MS = 60000;

struct OfficialRoomReturnFlowState
{
	BYTE Kind = QQS_OFFICIAL_ROOM_RETURN_KIND_NONE;
	BYTE Phase = QQS_OFFICIAL_ROOM_RETURN_PHASE_NONE;
	bool ReadyRequestCommitted = false;
	DWORD StartedTick = 0;
	DWORD Action62Tick = 0;
	DWORD ReadyRequestTick = 0;
	USHORT RoomID = 0;
};

constexpr size_t QQS_RANKED_GAME_FRAME_NODE_WIRE_SIZE = 112;
constexpr UINT QQS_RANKED_GAME_FRAME_NODE_LIMIT = 200;

// Msg2102 is the authoritative client report. Msg513 forwards one immutable
// snapshot to another racer, and Msg2103 is checked against that exact snapshot.
struct RankedGameFrameReport
{
	bool Valid = false;
	UINT RoomID = 0;
	USHORT GameID = 0;
	UINT Uin = 0;
	UINT Time = 0;
	UINT GameSeqHigh = 0;
	UINT GameSeqLow = 0;
	UINT MapID = 0;
	USHORT CheckPointID = 0;
	USHORT Trailer = 0;
	std::vector<std::array<BYTE, QQS_RANKED_GAME_FRAME_NODE_WIRE_SIZE>> FrameNodes;
};

struct RankedGameFrameForward
{
	bool Valid = false;
	UINT RoomID = 0;
	USHORT GameID = 0;
	UINT TargetUin = 0;
	UINT MapID = 0;
	USHORT CheckPointID = 0;
	std::vector<std::array<BYTE, QQS_RANKED_GAME_FRAME_NODE_WIRE_SIZE>> FrameNodes;
};

struct RankedGameFrameCheck
{
	bool Received = false;
	bool FrameMatchesForward = false;
	UINT RoomID = 0;
	USHORT GameID = 0;
	UINT ReporterUin = 0;
	UINT TargetUin = 0;
	UINT Time = 0;
	USHORT Result = 0;
};

struct ClientNode
{
	std::shared_mutex mtx; // 互斥锁
	u_long HeadSeq = 0;
	USHORT BaseGameMode = 0;;
	USHORT SubGameMode = 0;;
	INT getPropNum = 0;
	bool StartGameTime = false;
	ITcpServer* Server = NULL;
	LONG_PTR BufferPtr = NULL;
	LONG_PTR BufferSize;
	LONG_PTR BufferOffset;
	UINT TransportConnID = 0;     // HPSocket real connection id; can exceed 65535 after long uptime.
	USHORT ConnID = 0;           // 16-bit non-zero client/player id sent to the 21043 protocol.
	USHORT ProtocolVersion = 0;  // CSHEAD::Ver learned from this connection's validated inbound packets.
	BOOL IsLogin = FALSE;
	UINT TransportConnIDUin = 0;
	// Ranked matchmaking opens a second PVP transport after Msg20081.  Keep it
	// associated with the authoritative player session without replacing Clients[Uin].
	std::mutex RankedPVPTransportMutex;
	std::weak_ptr<ClientNode> RankedPVPTransport;
	UINT RankedPVPTransportRoomID = 0;
	UINT RankedPVPTransportKey = 0;
	DWORD Uin = 0;
	UINT PlayerIdentity = 0;
	UINT LauncherAuthenticatedUin = 0;
	UCHAR LauncherRuntimeMaterialState = 0; // 0=not issued, 1=issuing, 2=consumed
	BYTE Key[16];
	USHORT ServerID = 0;
	UINT KartID = 0;
	UINT TeamID = 0;
	//Room
	USHORT RoomID = 0;
	USHORT GameID = 0;
	BOOL IsReady;
	BOOL IsFinish = 0;
	//
	UINT MapID = 0;
	// Non-zero only while 28437 hands an Aurora solo request to the existing
	// synchronous quick-match room constructor.
	std::uint64_t AuroraPendingWorkID = 0;
	int FinishTime;
	int TotalRound;
	int Round;
	int PassedCheckPoint;
	int EndCheckPoint;
	bool Loop;
	bool MapCompatibilityMode;

	//Dance
	int Scores;
	UINT DancePerfectCount = 0;

	//Outdoor
	INT ChatRoomID = -1;

	UCHAR SuperN2O = 0;
	//SUPERJET
	UCHAR SuperJet;
	UINT SuperJetKartFuelInc = 0;
	USHORT CurrentAccelerateFuel = 0;
	USHORT PropID = 0;
	UINT PropIndex = 0;
	// 官服服务端判定型宝石（当前为重生宝珠）的单局次数与冷却状态。
	USHORT ServerSkillStoneGameID = 0;
	UINT ServerSkillStoneID = 0;
	UINT ServerSkillStoneUseCount = 0;
	ULONGLONG ServerSkillStoneLastActivationTick = 0;
	bool ServerSkillStoneHasActivated = false;
	UINT KartSkinID = 0;
	UINT PetID = 0;
	char PetStatus;
	//Other
	UINT TaskID = 0;
	UINT OptNum = 0;
	UINT OptIndex = 4;
	//UINT JumpThread = 0;
	HANDLE Timer = NULL;
	WAITORTIMERCALLBACK Callback = NULL;
	UINT BorderRoomID = 0;
	//char UDPState = 1;
	//UINT AllItemindex =0;
	char SeatID;
	char ReadyState = QQS_ROOM_READY_STATE_NOT_READY;
	char ranknumber = 0;
	char GlobalStone = 0;
	UINT GlobalItemStoneid = 0;
	char GlobalAddcard = 0;
	//char OBState = 0;
	//char AutoLeave = 0;
	UINT GradeScore = 0;
	UINT GradeLevel = 0;
	UINT ChildLevel;
	UINT TotalScore = 0;
	//晋级赛
	UINT PromotionMatch = 0;
	UINT CurrentPromotionRoundNums = 0;
	UINT CurrentPromotionWinRoundNums = 0;
	UINT CurrentPromotionLoseRoundNums = 0;
	UINT PromotionRecordTag = 0;
	char NickName[17] = { 0 };
	int Charm = 0;
	UINT Experience;
	int SuperMoney;
	int TotalHonor;
	int Money;//酷币
	UINT SpeedCoin = 0;
	int BugletTalk;
	UINT DanceExp;
	UINT EngageUin = 0;
	UINT OnlineTime = 0;
	UINT TotalOnlineTime = 0;
	KartRefitItemInfo ClientKartRefitItemInfo;
	char CheDengFlag = 0;
	char XiaoPendFlag = 0;
	int LuckyValue;
	NpcRacerFinishInfo NpcFinishInfo[6];
	char LoverBaby1EquipStat;
	char LoverBaby2EquipStat;
	int MsgSequence;
	// Normal-race game-logic stream. 509/551/907/511 and later game packets
	// keep one route and continuous sequence; 514 takes the next sequence and closes it.
	bool NormalRaceWireActive = false;
	bool NormalRaceKartPhysReady = false;
	USHORT NormalRaceWireRouteID = 0;
	int NormalRaceWireLastMsgSeq = 0;
	std::mutex NormalRaceWireSequenceMutex;
	std::mutex RankedGameFrameMutex;
	RankedGameFrameReport RankedFrameReport;
	RankedGameFrameForward RankedFrameForward;
	RankedGameFrameCheck RankedFrameCheck;
	char Ip[24];
	char deleteflag;
	char GuildName[17];
	int GuildID;
	char GuildDuty;
	MoveInfo HeroMoveInfo;//休闲区动作信息
	bool IsRefitItem;

	UINT isRace;
	DWORD LastRoomReturnTick = 0; // return timestamp for lifecycle checks/diagnostics; never infer AT=62 protocol state from it
	// 官服 ESC 由客户端继续驱动：424 -> 514[/424 ACK] -> 105 -> 103(single)/102(multi)。
	// 独立保存该短事务，避免把 GameID/isRace/LastRoomReturnTick 当成协议状态猜测。
	BYTE OfficialEscFlowKind = QQS_OFFICIAL_ESC_FLOW_NONE;
	BYTE OfficialEscPhase = QQS_OFFICIAL_ESC_PHASE_NONE;
	DWORD OfficialEscStartedTick = 0;
	DWORD OfficialEscLast105Tick = 0;
	USHORT OfficialEscRoomID = 0;
	USHORT OfficialEscBaseGameMode = 0;
	USHORT OfficialEscSubGameMode = 0;
	// 返房包序与 C109 状态提交是两个正交维度：C109 可能在 AT=62 前后到达，
	// 不能用同一个 Phase 表示两者，也不能再靠 GameID/isRace/时间窗口猜测。
	OfficialRoomReturnFlowState OfficialRoomReturn;
	DWORD LastMapChallengeLobbyTick = 0; // gate MapChallenge-only Co.MapChallenge_* lobby context
	DWORD SpeedLadderLobbyTick = 0; // gate SpeedLadder lobby preload chain: 28458 -> 28457/28459 -> Type=449
	BYTE SpeedLadderLobbyStage = QQS_SPEED_LADDER_LOBBY_STAGE_NONE;
	bool SpeedLadderRacePending = false; // official ladder room flow: SpeedLadder Room created from Lua entry
	bool SpeedLadderRaceActive = false;  // set at StartGame only after pending map matches SpeedLadderMaps.yml
	UINT SpeedLadderRaceMapID = 0;
	UINT SpeedLadderRaceSeasonID = 0;
	DWORD SpeedLadderRaceArmTick = 0;
	bool SpeedLadderReturnPending = false; // finished SpeedLadder race is returning to ladder UI, not MapChallenge room UI
	DWORD SpeedLadderReturnTick = 0;
	UINT SpeedLadderReturnMapID = 0;
	UINT SpeedLadderReturnSeasonID = 0;
	BYTE CustomRaceStageIndex = 0; // custom multi-map stage is per-player, not room-global
	DWORD CustomLastDistanceZeroTick = 0; // debounce custom multi-lap DistanceToEnd=0 before final finish
	UINT IsAnonymous;
	bool PetStrengthCfgSent = false; // 21043 official pet lobby: first 21140 request triggers strength Notify chain once
	bool DynamicCodeCfgSent = false; // Official login flow: 24028 EventID 749 triggers Msg21032 once.
	// 强化福袋只保存本服真实产生的待领状态；默认为空，不从官服账号抓包复制进度。
	StrengthenBagPlayerState StrengthenBagState;
	char isManMachine = 0;
	char Gender;
	UINT nitrogen = 0;
	UINT admin = 0;
	UINT gm = 0;
	std::shared_ptr<PetInfo> Pet = std::make_shared<PetInfo>(PetInfo{ 0, "", 0, 0, 0, 0, 0, 0 });
	EngageInfo* Engage = NULL;
	UINT OBState = 0;
	UINT QQHead = 0;
	UINT fps = 0;
	sql::Connection* conn;
	char SecPwd[MaxSecPwd];
	UINT SecPwdStats = 0;
	UINT Verify = 0;
	UINT FrozenLeftSeconds = 0;
	UINT FrozenSeconds = 0;
	UINT OrgKartId = 0;;
	int ServeryVerifyKey;
	UINT LoginClientPID = 0; // msg100 ClientFingerprint.m_uiPID; Network.dll 21273 safe 223 check uses GetCurrentProcessId().
	UINT KGMode = 0;
	INT SkillStoneList[5];
	UINT UserHeads = 0;
	UINT UserHeadsBox = 0;
	UINT UserFloor = 0;
	char QQHeadUrl[MaxQQHeadUrl] = {};
	//std::string QQHeadUrl;
	UINT skipFlag = 0;
	UINT AutoLeave = 0;
	UINT SpeclEffect = 0;
	UINT NobleID;
	USHORT VipFlag = 0;
	UINT NobleLevel;
	UINT NoblePoint;
	UINT GuildVipLevel;
	UINT GuildVipPoint;
	UINT LoverVipLevel;
	UINT LoverVipPoint;
	UINT EmperorLevel;
	UINT EmperorPoint;
	UINT GardenLevel;
	UINT GardenPoint;
	char Password[MaxSignature];
	UINT DriftCount = 0;
	UINT HighestSpeed = 0;
	UINT CollisionCount = 0;
	UINT N2OGatherCount = 0;
	RaceDetailRecord RaceDetail;
	bool IsStake = false;
	bool IsBottomGame = false;
	UINT StakeNum = 0;
	bool NewKartTip = false;
	UINT JumpLevel = 0;
	UINT LapTime = 0;
	UINT MAXLapTime = 0;
	UINT LastDistanceToEnd = 0;
	UINT LastRaceLapTime = 0;
	UINT EventTime = 0;
	UINT JumpThread = 0;
	UINT AppellationType = 0;
	bool IsBanMap = false;
	//std::shared_ptr<RoomNode> PlayerRoom = NULL;
	std::vector<UINT> MatchBanMapIDs;
	UINT real;
	std::string gmtime;
	UINT gmtips;
	UINT Speaking;
	std::vector<ChallengeMap> ChallengeMaps;
	LONG_PTR BufferChallengePtr = NULL;
	LONG_PTR BufferChallengeOffset = NULL;
	int MantissaNum;
	int Mantissa;
	std::string news;
	std::vector<UINT> MyKartLibrary;
	int SwitchFlag1;
	BYTE ClientSavedFlags[QQS_CLIENT_SAVED_FLAG_BYTES] = {};
	BYTE ExRightFlags[QQS_EX_RIGHT_FLAG_BYTES] = {
		0x22, 0x04, 0x40, 0xB0, 0x01, 0x20, 0xC4, 0x43,
		0xE0, 0x31, 0x01, 0x5C, 0x10, 0x1C, 0x20, 0x00,
		0x40, 0x20, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00
	};
	int GameBan;
	int LoginMode = 0;
	RoomNode* CurrentRoom = NULL;
};

void Write21032(std::shared_ptr<ClientNode> Client);

inline bool IsValidWireConnID16(USHORT id)
{
	return id != 0 && id != 0xFFFF;
}

inline USHORT GetClientWirePlayerID16(const std::shared_ptr<ClientNode>& Client)
{
	if (!Client)
		return 0;
	return IsValidWireConnID16(Client->ConnID) ? Client->ConnID : 0;
}

inline short GetClientWirePlayerHeaderID(const std::shared_ptr<ClientNode>& Client)
{
	const USHORT id = GetClientWirePlayerID16(Client);
	return static_cast<short>(id != 0 ? id : 1);
}

inline short NormalizeClientPlayerSrcID(const std::shared_ptr<ClientNode>& Client, char SrcFE, short SrcID)
{
	(void)Client;
	(void)SrcFE;
	// 官服做法：包头 SrcID 由调用点按语义显式填写（玩家/房间/游戏逻辑/服务端），
	// 不能再用 HPSocket TransportConnID 的 low16 去“猜测修正”，否则当其它玩家
	// 的协议 PlayerID 恰好等于当前连接 low16 时，会被错误改成接收方自己的 ConnID。
	return SrcID;
}

inline short NormalizeClientWireDstID(const std::shared_ptr<ClientNode>& Client, short WireDstID)
{
	(void)Client;
	// 官服做法：DstID 是协议路由 ID，不从 HPSocket TransportConnID 推导；
	// 只在调用方传入非法 0/0xFFFF 时回到稳定默认路由 1。
	const USHORT raw = static_cast<USHORT>(WireDstID);
	return IsValidWireConnID16(raw) ? WireDstID : 1;
}
struct ActiveHeadItemList
{
	int ActivePeriod;
	int ActiveHeadId;
	int ActiveItemId;
	std::string Method;
	std::string Desc;
	std::string Info;
};
enum ServerType
{
	Game,
	Dance,
	Relax
};
typedef struct MapPool
{
	std::vector<USHORT> MapID;
	UINT Size;
}MAPPOOL;

struct CMatchBanMapInfo // MapInfo
{
	UINT MapID = 0;
	UINT Status = 0;
	UCHAR ThemeMapOrNot = 0;
	UCHAR SeasonNewMapOrNot = 0;
	UCHAR PinnacleBattleMapOrNot = 0;
	USHORT TotalBeBannedNums = 0;
	USHORT FinalRandBanMapType = 0;
};

typedef struct TreasureAward
{
	UINT Seed;
	UINT ItemType;
	std::vector<ItemInfo> AwardItems;
}TREASUREAWARD;
typedef struct TreasureBox {
	int LuckyValue;
	UINT ExtraItemID = 0;
	UINT ExtraItemNum = 0;
	int ExtraItemPeriod = 0;
	USHORT ExtraItemType = 2;
	std::vector<TREASUREAWARD> TreasureAwards;
}TREASUREBOX;
TREASUREBOX* GetTreasureBox(long ID);

struct LimitMsgInfo
{
	UINT MSGID;
	UINT isInfo;
};

struct NewMapsInfo
{
	UINT MapID;
	std::string WordTxt;
};

struct ChallengeMapsInfo
{
	UINT MapID;
	char IsOpen;
	char IsNew;
};


void LoadRandomMapPool();
void LoadMapPool();
int LoadTreasureBox();
void LoadCarTreasure();


USHORT AllocateGameLogicSessionID();
void ResetNormalRaceWireState(const std::shared_ptr<ClientNode>& Client);
BOOL SendToClient(std::shared_ptr<ClientNode> Client, short MsgID, const BYTE* Data, DWORD Length, short SrcID, char SrcFE, short DstID, short MsgType, char Encrypt = true);
BOOL SendToClientWithWireDstID(std::shared_ptr<ClientNode> Client, short MsgID, const BYTE* Data, DWORD Length, short SrcID, char SrcFE, short WireDstID, short MsgType, char Encrypt = true);
BOOL SendToClientOfficial14(std::shared_ptr<ClientNode> Client, short MsgID, const BYTE* Data, DWORD Length, short SrcID, char SrcFE, short WireDstID, short MsgType);
BOOL SendToClient440Official(std::shared_ptr<ClientNode> Client, const BYTE* Data, DWORD Length, short SrcID, char SrcFE, short WireDstID, short MsgType);
BOOL SendToClientWithWireDstIDAndMsgSeq(std::shared_ptr<ClientNode> Client, short MsgID, const BYTE* Data, DWORD Length, short SrcID, char SrcFE, short WireDstID, int WireMsgSeq, short MsgType, char Encrypt = true);
BOOL SendCorrelatedResponseToClient(std::shared_ptr<ClientNode> Client, short MsgID, const BYTE* Data, DWORD Length, short SrcID, char SrcFE, short WireDstID, int WireMsgSeq, int ProtocolSeq, char Encrypt = true);
BOOL SendToClientWithRawWireRouteIDsAndDialog(std::shared_ptr<ClientNode> Client, short MsgID, const BYTE* Data, DWORD Length, short WireSrcID, char SrcFE, short WireDstID, short ProtocolDialogID, int WireMsgSeq, short MsgType, char Encrypt = true);
BOOL SendToClientViaTransport(std::shared_ptr<ClientNode> StateClient, std::shared_ptr<ClientNode> TransportClient, short MsgID, const BYTE* Data, DWORD Length, short SrcID, char SrcFE, short WireDstID, int WireMsgSeq, short MsgType, char Encrypt = true);
BOOL SendToClientViaTransportWithRawWireRouteIDsAndDialog(std::shared_ptr<ClientNode> StateClient, std::shared_ptr<ClientNode> TransportClient, short MsgID, const BYTE* Data, DWORD Length, short WireSrcID, char SrcFE, short WireDstID, short ProtocolDialogID, int WireMsgSeq, short MsgType, char Encrypt = true);
bool TryGetCurrentInboundWireMsgSeq(const std::shared_ptr<ClientNode>& Client, int& WireMsgSeq);
bool TryGetCurrentInboundProtocolSeq(const std::shared_ptr<ClientNode>& Client, int& ProtocolSeq);
void OnRecvFromClient(std::shared_ptr<ClientNode> Client, USHORT MsgID, BYTE* Body, size_t BodyLen);
void OnFullReceive(std::shared_ptr<ClientNode> Client, BYTE* Data, size_t Length);


void InitDirServer(const char* Name, UINT IP, USHORT TcpPort, USHORT UdpPort);
int GetServerType(int ServerID);


BOOL WritePlayerDBBaseInfo(BYTE*& p, UINT Uin, std::shared_ptr<ClientNode> Client);
BOOL WritePlayerDBBaseInfo1(BYTE*& p, UINT Uin, std::shared_ptr<ClientNode> Client);
void WritePlayerGuildInfo(BYTE*& p, UINT Uin, std::shared_ptr<ClientNode> Client);
UINT WriteEquipedItem(BYTE*& p, std::shared_ptr<ClientNode> Client);
UINT WriteEquipedItem(BYTE*& p, std::shared_ptr<ClientNode> Client, UINT* cPetId, BOOLEAN isBorder);
void WriteVisbleInfo(std::shared_ptr<ClientNode> Client, BYTE*& p, UINT Uin, char SeatID, short PlayerID, bool isRoomOwner = false);
void WriteVisbleInfo1(std::shared_ptr<ClientNode> Client, BYTE*& p, UINT Uin, char SeatID, short PlayerID);
std::vector<QQSIllusionSuitVisibleInfo> QQS_GetEquippedIllusionSuitVisibleInfo(UINT Uin);
void QQS_WriteEquippedIllusionSuitVisibleInfo(BYTE*& p, UINT Uin);
void QQS_NotifyIllusionSuitShapeChangedToRoom(std::shared_ptr<ClientNode> Client, UINT ItemID);


//封包
void RequestGetWorldInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetWorldInfo(std::shared_ptr<ClientNode> Client, UINT ClientVersion, UINT MasterVersion, UINT SlaveVersion);
void RequestGetChatRoomList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

extern std::mutex LogFileMutex;
extern FILE* LogFile;
void PrintfFile(std::shared_ptr<ClientNode> Client, USHORT MsgID, BYTE* Body, size_t BodyLen);


void RequestRegister2(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);


void RequestLogin(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestLogout(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseLogout(std::shared_ptr<ClientNode> Client);


#include "Room.h"


void RequestHello(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseHello(std::shared_ptr<ClientNode> Client, UINT Uin, UINT Time);

void RequestPrepareReady(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
bool TryHandleLuckyMatchPrepareReady(std::shared_ptr<ClientNode> Client);
bool TryHandleLuckyMatchSwitchDisconnect(std::shared_ptr<ClientNode> Client);
bool TryAttachLuckyMatchReconnect(std::shared_ptr<ClientNode> Client);
bool TryIsLuckyMatchLoginPending(std::shared_ptr<ClientNode> Client);
bool TryHandleLuckyMatchLoginFastPath(std::shared_ptr<ClientNode> Client);
bool TryHandleLuckyMatchEnterAlias(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
bool TryHandleLuckyMatchLeaveRoom(std::shared_ptr<ClientNode> Client, short LeaveType);
bool TryHandleLuckyMatchRaceOver(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, bool First);
bool TryHandleLuckyMatchGameOver(std::shared_ptr<ClientNode> Client, USHORT LeaveGameType);
bool TryHandleLuckyMatchLeaveGame(std::shared_ptr<ClientNode> Client, USHORT LeaveGameType);

bool NotifyGameBegin(std::shared_ptr<ClientNode> Client, std::vector<std::shared_ptr<ClientNode>> Player);
bool NotifyGameBegin1(std::shared_ptr<ClientNode> Client, std::vector<std::shared_ptr<ClientNode>> Player);
bool NotifyGameBegin2(std::shared_ptr<ClientNode> Client, std::vector<std::shared_ptr<ClientNode>> Player);
bool NotifyGameBegin3(std::shared_ptr<ClientNode> Client, std::vector<std::shared_ptr<ClientNode>> Player);
bool NotifyGameBegin4(std::shared_ptr<ClientNode> Client, std::vector<std::shared_ptr<ClientNode>> Player);
bool NotifyGameBegin5(std::shared_ptr<ClientNode> Client, std::vector<std::shared_ptr<ClientNode>> Player);
void NotifyGameBeginCS(std::shared_ptr<ClientNode> Client);
void NotifyMuchPlayerInfo(std::shared_ptr<ClientNode> Client);


bool PreflightKartPhysParamForPlayer(std::shared_ptr<ClientNode> Player, UINT* OutLoadID = nullptr);
bool NotifyKartPhysParam(std::shared_ptr<ClientNode> Client, std::vector<std::shared_ptr<ClientNode>> Player);
bool NotifyPurplePracticeKartPhysParam(std::shared_ptr<ClientNode> Client);
bool NotifyOtherKartStoneInfo(std::shared_ptr<ClientNode> Client, const std::vector<std::shared_ptr<ClientNode>>& Players, short PlayerRouteSourceID = 0);
bool IsRankedStonePresetEnabled();
bool IsConfiguredRankedStonePresetID(UINT StoneID);
bool NotifyRankedStonePresetForKartAndSkin(std::shared_ptr<ClientNode> Client);
bool ValidateCustomRaceKartPhysSnapshot(std::shared_ptr<RoomNode> Room, const char* reason);
bool NotifyRaceBegin(std::shared_ptr<ClientNode> Client);

void RequestReportCurrentInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
UINT SpeedLadder_GetCurrentSeasonID();
bool SpeedLadder_CalcTrophyForMap(UINT MapID, DWORD BestTime, BYTE& TrophyLevel, BYTE& TrophyCount);
bool SpeedLadder_IsConfiguredMap(UINT MapID);
bool SpeedLadder_IsLobbyArmed(std::shared_ptr<ClientNode> Client);
bool SpeedLadder_IsReturnContextArmed(std::shared_ptr<ClientNode> Client);
void SpeedLadder_MarkReturnContext(std::shared_ptr<ClientNode> Client, UINT MapID, UINT SeasonID, const char* Reason);
void SpeedLadder_ClearRaceContext(std::shared_ptr<ClientNode> Client, const char* Reason);
void SpeedLadder_ClearReturnContext(std::shared_ptr<ClientNode> Client, const char* Reason);
void MyResponseQuickRPC(std::shared_ptr<ClientNode> Client, BYTE* Msg, size_t MsgLen);
void NotifySpeedLadderBreakMapRecordQuickRPC(std::shared_ptr<ClientNode> Client, UINT MapID, DWORD OldTime, BYTE OldTrophyLevel, DWORD NewTime, BYTE NewTrophyLevel, BYTE CrossedMilestoneCount, bool IsCrossedMilestone, bool IsFirstComplete);
void ResponseSpeedLadderRewardDisplayRefresh449(std::shared_ptr<ClientNode> Client);

struct SpeedLadderRewardItem
{
	UINT ItemID = 0;
	UINT ItemNum = 1;
	INT AvailPeriod = 0;
	UINT ItemType = 0;
};

struct SpeedLadderRewardAward
{
	UINT Exp = 0;
	UINT Money = 0;
	UINT SuperMoney = 0;
	UINT Coupons = 0;
	UINT SpeedCoin = 0;
	std::vector<SpeedLadderRewardItem> Items;
};

struct SpeedLadderRewardConfigRow
{
	UINT PosKey = 0;
	UINT AwardIdx = 0;
	UINT NeedTrophy = 0;
	WORD ResultID = 11498;
	BYTE LabelStatus = 0;
	SpeedLadderRewardAward Award;
};

struct SpeedLadderTrophySnapshot
{
	UINT TotalTrophy = 0;
	UINT MapCount = 0;
	UINT DbRows = 0;
	UINT SkippedInvalidTime = 0;
	UINT SkippedNoMapCfg = 0;
	UINT SkippedNoTrophy = 0;
	bool DbOk = false;
};

bool SpeedLadder_LoadRewardRows(std::vector<SpeedLadderRewardConfigRow>& rows, std::string* loadedPath = nullptr, bool* usedFallback = nullptr);
bool SpeedLadder_GetRewardRow(UINT PosKey, SpeedLadderRewardConfigRow& row);
SpeedLadderTrophySnapshot SpeedLadder_GetTrophySnapshotForReward(std::shared_ptr<ClientNode> Client, UINT SeasonID);

void NotifyCountDown(std::shared_ptr<ClientNode> Client, UINT WinnerUin, bool WinnerNewRecord, UINT FinTime);
void NotifyRaceShow(std::shared_ptr<ClientNode> Client, UINT RaceShowTime = 0);



void RequestChangeItemStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseChangeItemStatus(std::shared_ptr<ClientNode> Client, USHORT ItemNum, std::vector<ItemStatus> ItemStatus);
void RequestGetLicenseInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetLicenseInfo(std::shared_ptr<ClientNode> Client);

void RequestGetSystemTaskList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetSystemTaskList(std::shared_ptr<ClientNode> Client, UINT TaskVersion);

void RequestChangeGender(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseChangeGender(std::shared_ptr<ClientNode> Client, BYTE NewGender, BYTE UseType);

void RequestSkipFreshTask(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseSkipFreshTask(std::shared_ptr<ClientNode> Client);

void RequestStartShadowChallenge(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseStartShadowChallenge(std::shared_ptr<ClientNode> Client);
void RequestEndShadowChallenge(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseEndShadowChallenge(std::shared_ptr<ClientNode> Client, int ResultID);

void RequestFishLogin(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseFishLogin(std::shared_ptr<ClientNode> Client);

void RequestGetPlayerEventCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetPlayerEventCfg(std::shared_ptr<ClientNode> Client);
void RequestGetPlayerEvent(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetPlayerEvent(std::shared_ptr<ClientNode> Client, BYTE TargetType, BYTE TimeType);

void NotifyMsgBox(std::shared_ptr<ClientNode> Client);


void RequestTransferByTCP(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestGetTaskList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetTaskList(std::shared_ptr<ClientNode> Client);

void RequestUseProp2(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestChangeModel(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseChangeModel(std::shared_ptr<ClientNode> Client, BYTE NewModel);
void NotifyChangeModel(std::shared_ptr<ClientNode> Client, UINT Uin, BYTE NewModel);

void NotifyClientVipFlag(std::shared_ptr<ClientNode> Client);

void NotifySvrConfig(std::shared_ptr<ClientNode> Client);
void NotifySvrConfig2(std::shared_ptr<ClientNode> Client);
void NotifySvrConfig3(std::shared_ptr<ClientNode> Client);
void NotifySvrConfig4(std::shared_ptr<ClientNode> Client);
void NotifySelectedSvrConfig(std::shared_ptr<ClientNode> Client);
void NotifySvrConfig3MC(std::shared_ptr<ClientNode> Client);






void RequestChangeUdpState(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestTransferUDPOK(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseTransferUDPOK(std::shared_ptr<ClientNode> Client);

void RequestInlaySkillStone(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseInlaySkillStone(std::shared_ptr<ClientNode> Client, int KartID, int StoneGrooveID, int SkillStoneID);

void RequestGetProp(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetProp(std::shared_ptr<ClientNode> Client);

void NotifySpeed2Cfg(std::shared_ptr<ClientNode> Client);

void RequestKartRefitInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseKartRefitInfo(std::shared_ptr<ClientNode> Client, UINT ItemNum, KartRefitItemInfo* Items, bool HasMoreInfo, UINT ResponseContext);

void RequestNewGetFriendList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseNewGetFriendList(std::shared_ptr<ClientNode> Client);

void RequestFizzInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseFizzInfo(std::shared_ptr<ClientNode> Client);

void RequestGetShoppingCarItemList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetShoppingCarItemList(std::shared_ptr<ClientNode> Client);

void RequestReplayOperate(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseReplayOperate(std::shared_ptr<ClientNode> Client, UCHAR Cmd);

void RequestGetKeyTranslateInfoNew(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetKeyTranslateInfoNew(std::shared_ptr<ClientNode> Client);


void RequestGetMaster(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetMaster(std::shared_ptr<ClientNode> Client);

void RequestSSCOpenStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseSSCOpenStatus(std::shared_ptr<ClientNode> Client);

void RequestSwitchInterface(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseSwitchInterface(std::shared_ptr<ClientNode> Client);

void RequestSecondGetKartRefitLimitCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseSecondGetKartRefitLimitCfg(std::shared_ptr<ClientNode> Client);



void RequestGetActivityCenterInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetActivityCenterInfo(std::shared_ptr<ClientNode> Client);

void NotifyTopUIItemInfo(std::shared_ptr<ClientNode> Client);
void NotifyTopUIItemInfoNew(std::shared_ptr<ClientNode> Client);
void NotifyTopUIItemInfoNew1(std::shared_ptr<ClientNode> Client);
void NotifyRedPointInfo(std::shared_ptr<ClientNode> Client);

void RequestGetRandomTask(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetRandomTask(std::shared_ptr<ClientNode> Client);

void RequestGetItemTimeLimtCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponsetGetItemTimeLimtCfg(std::shared_ptr<ClientNode> Client, UINT UpdateTime);

void RequestGetShapeRefitCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetShapeRefitCfg(std::shared_ptr<ClientNode> Client);
void ResponseGetShapeRefitCfgTest(std::shared_ptr<ClientNode> Client);

void RequestWeRelayCommonCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseWeRelayCommonCfg(std::shared_ptr<ClientNode> Client);

void RequestC2GSign3Operate(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2GSign3Operate(std::shared_ptr<ClientNode> Client, UCHAR OperateType);

void NotifyClientAddItem(std::shared_ptr<ClientNode> Client, UINT ItemNum, std::vector<ItemInfo> Items);
void NotifyClientAddItemNew(std::shared_ptr<ClientNode> Client, UINT ItemNum, std::vector<ItemInfo> Items);
void NotifyClientAddItemBox(std::shared_ptr<ClientNode> Client, UINT ItemNum, std::vector<ItemInfo> Items);
void ResponseRegister(std::shared_ptr<ClientNode> Client, const char* Nickname, BYTE  Gender, BYTE Country, UINT ItemNum, std::vector<ItemInfo> Items);
void NotifyUpdateKartSkillStoneInfo(std::shared_ptr<ClientNode> Client, int KartID);
void NotifyUpdateKartSkillStoneInfoNew(std::shared_ptr<ClientNode> Client, int KartID);

void RequestDeleteOneItem(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseDeleteOneItem(std::shared_ptr<ClientNode> Client, int ItemID);

void RequestRefitKart(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseRefitKart(std::shared_ptr<ClientNode> Client, int KartItemId, const char* Reason, short inc_SJetWeight, short inc_AccuWeight, short inc_JetWeight, short inc_WWeight, short inc_SpeedWeight, short inc_AntiCollisionWeight);
void RequestHyperSpaceJumboGetCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseHyperSpaceJumboGetCfg(std::shared_ptr<ClientNode> Client, UCHAR CfgType);
void ResponseHyperSpaceJumboGetCfg2(std::shared_ptr<ClientNode> Client, UCHAR CfgType); // 跃动大闯关

void RequestTimerChallenge2ndMoreHallInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseTimerChallenge2ndMoreHallInfo(std::shared_ptr<ClientNode> Client);

void RequestPreGetCommonBoxAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestBatchGetCommonBoxAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void LoadSupremeTreasureExchangeShopCommonBox();
void LoadSecretCompassCommonBox();
void LoadSummonDragonExchangeShopCommonBox();
void RequestGetSecretCompassInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestSecretCompassDrawLottery(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponsePreGetCommonBoxAward(std::shared_ptr<ClientNode> Client, BOOL HasMore, USHORT Type, std::vector<ItemInfo>& MiracleItems, char MiracleItemNum);
void ResponsePreGetCommonBoxAwardElse(std::shared_ptr<ClientNode> Client, USHORT Type, int AwardID, UCHAR IsShowAllAward, int EchoClient);
void ResponseRefreshCarTreasureCommonBoxAward(std::shared_ptr<ClientNode> Client, USHORT Type);
bool SummerCarTreasure_IsCommonBoxType(USHORT Type);
bool SummerCarTreasure_HandleCommonBoxPreview(std::shared_ptr<ClientNode> Client, USHORT Type, int AwardID, UCHAR IsShowAllAward, int EchoClient);
bool SummerCarTreasure_TryResolveCommonBoxRow(std::shared_ptr<ClientNode> Client, USHORT Type, int AwardIdx, int PosKey, CarTreasureInfo& Row, UINT& CostItemID);
void RequestAddToMatchQueue(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseAddToMatchQueue(std::shared_ptr<ClientNode> Client, USHORT matchQueueType);

void NotifyCustomMatchAck(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
bool NotifyMatchResult(std::shared_ptr<ClientNode> Client, USHORT matchQueueType);
void NotifyCustomStageEvent1727(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room, BYTE StageIndex);
void NotifyCustomMatchResult(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
void NotifyMatchResultLogin(std::shared_ptr<ClientNode> Client);
void RequestPVPLogin(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
bool ResponsePVPLogin(std::shared_ptr<ClientNode> Client);
void RequestPVPHello(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
std::shared_ptr<ClientNode> GetRankedPVPTransport(
	const std::shared_ptr<ClientNode>& SessionClient,
	const std::shared_ptr<RoomNode>& Room);
bool ResolveActiveRaceStateClient(
	const std::shared_ptr<ClientNode>& PacketClient,
	UINT PacketUin,
	std::shared_ptr<ClientNode>& StateClient,
	std::shared_ptr<RoomNode>& Room);
void ResetRankedPVPTransport(const std::shared_ptr<ClientNode>& SessionClient);



void NotifyPlayerAllActiveKartInfo(std::shared_ptr<ClientNode> Client); //24338
void NotifyKartActiveCfgInfo(std::shared_ptr<ClientNode> Client);
void RequestLeaveGame(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseLeaveGame(std::shared_ptr<ClientNode> Client);

void RequestModifyPlayerSignature(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseModifyPlayerSignature(std::shared_ptr<ClientNode> Client, char* Signature);

void RequestFindPlayerByQQ(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseFindPlayerByQQ(std::shared_ptr<ClientNode> Client, UINT DstUin, UCHAR AreaIdDst, UCHAR SimpleInfo, char* NickName);
void ResponseFindPlayerByQQ2(std::shared_ptr<ClientNode> Client, UINT DstUin, UCHAR AreaIdDst, UCHAR SimpleInfo);
void RequestGetAchieveList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetAchieveList(std::shared_ptr<ClientNode> Client, UINT DstUin);

void NotifyRandRoomNameList(std::shared_ptr<ClientNode> Client);

void NotifyTopListDesc(std::shared_ptr<ClientNode> Client);
void RequestTopList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseTopList(std::shared_ptr<ClientNode> Client, USHORT ID, UCHAR Type);

void NotifyUseItemResult(std::shared_ptr<ClientNode> Client, int ItemID, int UseItemNum, int CurrentItemNum);

void RequestHeroDoAction(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestOutDoorEggInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestOutDoorSmashEgg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestChangeHammerStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestUseHammer(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestHeroPos(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
#include "Room.h"
#include "Dance.h"
#include "Outdoor.h"

void RequestBuyCommodity(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseBuyCommodity(std::shared_ptr<ClientNode> Client, UINT Uin, UINT Time, const char* Reason, int BuyCount, ShoppingCar* aShoppingCar);

std::string UTF2GB(const char* str);
std::wstring Utf8ToWide(const std::string& utf8);
std::string SqlStingUtf8(std::string str);
//GB2312到UTF-8的转换
char* G2U(const char* gb2312);
void NotifyChangeItemStatus(std::shared_ptr<ClientNode> Client, std::shared_ptr<ClientNode> MyClient, USHORT ItemNum, std::vector<ItemStatus> ItemStatus);
void PrintfBody(std::shared_ptr<ClientNode> Client, USHORT MsgID, BYTE* Body, size_t BodyLen);
void PrintfBody(std::shared_ptr<ClientNode> Client, USHORT MsgID, BYTE* Body, size_t BodyLen, const char* filename);
void ResponseSkinChange(std::shared_ptr<ClientNode> Client, int KartItemId, int SkinItemId, int karthead, int kartail, int karflank, int kartire);
void ResponseMagicHouseCallOn(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetExchangeShopInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseDoShopExchange(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetMagicHouseInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);			 //?
void getGameMode(std::shared_ptr<ClientNode> Client);
void RequestPropEffectResult(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //126
void NotifyPropEffectResult(std::shared_ptr<ClientNode> Client, UINT Uin, std::vector<UCHAR> RemoveOwnProp, UINT SrcUin, UINT PropID, UINT PropIndex, UINT StoneSkillType, UINT LapTime);
void NotifyPropPoint(std::shared_ptr<ClientNode> Client, UINT Uin, UINT PropID);
void RequestChangeAimState(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //127
void NotifyChangeAimState(std::shared_ptr<ClientNode> Client, UINT Uin, UINT DstUin, UINT AimState, UINT DstType);


#include<deque>
#include <mutex>
#include<map>

#include<unordered_map>
#include <stdio.h>

void RequestCancelMatch(std::shared_ptr<ClientNode> Client);
void RequestCancelMatchExactSession(std::shared_ptr<ClientNode> Client);

typedef struct CREFITITEMCFG //进阶改装配件信息
{
	UINT ItemID;
	UCHAR ItemType;
	UCHAR ItemWeightLevel;
	WORD Value;
	UINT FragmentItemID;
	UINT FragmentNumToOneItem;
	WORD IsItemOpen;
} *LPCREFITITEMCFG;
const static CREFITITEMCFG RefitItemCfg[] =
{
	/*
		ItemID 进阶改装配件ID
		ItemType	改装道具类型 5=推进 6=离合 7=平衡
		ItemWeightLevel	道具重量值	1=普通 2=精良	3=极致
		Value			改装增加值
		FragmentItemID	碎片道具ID
		FragmentNumToOneItem	碎片数量
		IsItemOpen 是否启用
	*/
	{65025, 5, 1, 1, 65037, 100, 1},
	{65026, 5, 2, 1, 65038, 100, 1},
	{65027, 5, 3, 1, 65039, 100, 1},
	{65028, 6, 1, 1, 65040, 100, 1},
	{65029, 6, 2, 1, 65041, 100, 1},
	{65030, 6, 3, 1, 65042, 100, 1},
	{65031, 7, 1, 1, 65043, 100, 1},
	{65032, 7, 2, 1, 65044, 100, 1},
	{65033, 7, 3, 1, 65045, 100, 1},
	{65034, 5, 1, -1, 0, 0, 1},
	{65035, 6, 1, -1, 0, 0, 1},
	{65036, 7, 1, -1, 0, 0, 1},
};


#include<random>

struct SUPERPROP {
	int ItemID;
	int GetNum;
};
extern void NotifyUseItemResult2(std::shared_ptr<ClientNode> Client, int ItemID, int UseItemNum);
int64_t GetSysTimeMicros();
void formatTimeStamp(long long microTimestamp, char* temp);
void NotifyClientUpdateMoneyExp(std::shared_ptr<ClientNode> Client);
void AddCharmAndSync(std::shared_ptr<ClientNode> Client, int CharmDelta, bool NotifyClient = true);

extern void RequestEnter3DRoom(std::shared_ptr<ClientNode> Client);
extern void NotifyChangeRoomOwner(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);


typedef struct AdvancedProp {
	UINT PropID;
	UINT Add;
}ADVCANCEDPROP;
typedef struct AdvancedCfg
{
	std::vector<ADVCANCEDPROP> AdvancedProp;
}ADVANCEDCFG;

typedef struct MiracleCfg {
	std::vector<ItemInfo> MiracleItems;
}MIRACLECFG;

typedef struct RandomMapPool {
	std::vector<int> MapID;
}RANDOMMAPPOOL;

typedef struct Guidinfo {
	UINT RankLevel;
	UINT Score;
	UINT Attribute;
	UINT MaxMember;
	UINT CurrentMember;
	UINT TeamAddition;
	UINT Prosperity;
	UINT Level;
	std::string NickName;
	std::string Name;
	UINT Duty;
	UINT Uin;
	UINT ID;
	UINT MemProsperity;
	UINT EnlistType;
}GUIDINFO;



extern void NotifyKickPlayer(std::shared_ptr<ClientNode> Client, const char* Reason, UINT KickType);
void NotifyBugletTalk(std::shared_ptr<ClientNode> Client, const char* Content, short clength, char BugletType);
extern void gen_randomcdk(char* s, const int len);
extern char* generateRandomKey();
extern std::mutex KeysMutex;
extern std::map<long, std::string> Keys;
extern std::string GetKey(long Uin);
extern ConnectionPool player;
extern ConnectionPool usr;
extern ConnectionPool activity;
void CheckDupUser(UINT Uin, std::shared_ptr<ClientNode> Client);

extern void DisConnectUser(char* dstip, UINT ConnIDUin);

void RequestGetGuildList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGuildDetail(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestCreateGuild(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestApplyJoinGuild(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestReplyJoinGuild(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestRemoveMember(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestFindGuild(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestAppointedPosition(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestSaveShapeRefitNew(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseSaveShapeRefitNew(std::shared_ptr<ClientNode> Client, CONST UINT KartID, CONST UINT ShapeSuitID);
void NotifyChangeSuitItemStatus(std::shared_ptr<ClientNode> Client, CONST UINT ShapeSuitID, CONST UINT KartID);
void ResponseShapeLegendOperate_OpType_0(std::shared_ptr<ClientNode> Client);
void ResponseShapeLegendOperate_OpType_2(std::shared_ptr<ClientNode> Client, CONST UINT ShapeID, CONST UINT NewShowChoice);
void ResponseShapeLegendOperate_OpType_Level(std::shared_ptr<ClientNode> Client, CONST UINT ShapeID, CONST UINT NewShowChoice, CONST  UINT KartID, CONST  UINT ShapeSuitID);
void ResponseShapeLegendOperate_OpType_3(std::shared_ptr<ClientNode> Client, CONST UINT ShapeID);
void ResponseGetSuperN2O1(std::shared_ptr<ClientNode> Client, UINT PropID);
void ResponseC2SGetTimesExperienceInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2SReceiveTimesExperience(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2SFrostThawTimesExperience(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void ResponseC2GGetWizardBaseInfo(std::shared_ptr<ClientNode> Client);							  // 11526
void RequestChangeCarryWizard(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //?
void ResponseChangeCarryWizard(std::shared_ptr<ClientNode> Client, USHORT WizardID, bool Carry); //11506
void ResponseChangeLoverBabyEquipStatus(std::shared_ptr<ClientNode> Client, short EquipStatus); //24386
void RequestGetPetSkillList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); // 440
void ResponseGetPetSkillList(std::shared_ptr<ClientNode> Client);							 // 440
void RequestPayRebateCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); // 20077
void ResponsePayRebateCfg(std::shared_ptr<ClientNode> Client); // 20077
void RequestGetPKPetInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //?
void ResponseGetPKPetInfo(std::shared_ptr<ClientNode> Client, UINT DstUin, char Flag, int PetId); //21133
void RequestGetPKPetCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //21140
void ResponseGetPKPetCfg(std::shared_ptr<ClientNode> Client); //21140
void NotifyGetPKPetMoreCfg(std::shared_ptr<ClientNode> Client); //21149
void RequestChangePetEquipStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //15398
void ResponseChangePetEquipStatus(std::shared_ptr<ClientNode> Client, int NewStatus, USHORT ResultID); //15398
void RequestPetStrengthen(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //20101
void ResponsePetStrengthen(
	std::shared_ptr<ClientNode> Client,
	UINT Result,
	UINT UseItemNum = 0,
	UINT Type = 0,
	const PetInfo* TargetPet = nullptr); //20101
void RequestPetTrialInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //?
void ResponsePetTrialInfo(std::shared_ptr<ClientNode> Client, USHORT TeamID); //21150
void NotifyPetStrengCfgInfo(std::shared_ptr<ClientNode> Client); //20100
void NotifyPetStrengCfgMoreInfo(std::shared_ptr<ClientNode> Client); //28228
void NotifyPetStrengPetID(std::shared_ptr<ClientNode> Client); //24074
void NotifyPetHuanHuaCfgInfo(std::shared_ptr<ClientNode> Client); //24073
void NotifyAvatarSynthesisCfg(std::shared_ptr<ClientNode> Client); //20103
void RequestC2GGetWizardBaseInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //?

void RequestGetAuctionCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetCurAuctionInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetAuctionList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetParticipateAuction(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestBidAuctionCommodity(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestPayAuctionCommodity(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetSingleAuctionInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);


void RequestRankedMatchTopRaceOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseRankedMatchTopRaceOpt(std::shared_ptr<ClientNode> Client, short OptType, UINT Para0);


void RequestChangeTeam(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);											// 128
void ResponseChangeTeam(std::shared_ptr<ClientNode> Client, bool NewTeamID);																	// 128
void NotifyPlayerChangeTeam(std::shared_ptr<ClientNode> Client, UINT Uin, bool NewTeamID);

void RequestCheckGuildName(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseCheckGuildName(std::shared_ptr<ClientNode> Client, UINT Type);
void RequestUpdateGuildName(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseUpdateGuildName(std::shared_ptr<ClientNode> Client, char* newGuildname);

void RequestGetSecondPassInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);									  //?
void NotifySecondPassInfo(std::shared_ptr<ClientNode> Client);
void NotifySecondPassReClose(std::shared_ptr<ClientNode> Client); //21016
void RequestOpSecondPass(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //?
void ResponseOpSecondPass(std::shared_ptr<ClientNode> Client, char Op, UINT Status); //21017
void NotifySecondPassOpNotAllowed(std::shared_ptr<ClientNode> Client); //21018														  // 21018

void ResponseCarLottoryGetInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void NotifySkillStoneKartInfo(std::shared_ptr<ClientNode> Client);


void NotifyEncryptedMsg(std::shared_ptr<ClientNode> Client);

void ResponseGetGameBaseRecordList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetGameDetailRecordList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetGameDetailRecordList(std::shared_ptr<ClientNode> Client, UINT Recordid);

void RequestGetCarTreasureNoSelfSelectCfg(std::shared_ptr<ClientNode> Client, BYTE* Body);
void RequestGetCarTreasureNoSelfSelectAward(std::shared_ptr<ClientNode> Client, BYTE* Body);
void RequestGetCarTreasureCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetCarTreasureAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestBeta80SelectGroupIndex(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetSummerCarTreasureCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetSummerCarTreasureAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestBeta96SelectGroupIndex(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestFourBeautiesMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestFourBeautiesDoAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_AnnualRacingGetCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_AnnualRacingOperate(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99AnimeSkinMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
bool RockParty_HandleCommonEvent(std::shared_ptr<ClientNode> Client, UINT Uin, UINT Time, UCHAR EventType, UINT EventID, const UINT* Parameters, size_t ParameterCount);
bool RockParty_RecordBoxOpen(std::shared_ptr<ClientNode> Client, UINT BoxItemID, UINT DrawCount);
bool RockParty_TryPurchaseBox(std::shared_ptr<ClientNode> Client, UINT CommodityID, int PriceIndex, int PayType, int RequestBuyNum, std::string& Reason);
bool AnnualRacing_IsExchangeType(USHORT Type);
bool AnnualRacing_LoadExchangeShops(std::string* Error = nullptr);
bool AnnualRacing_GetPeriodKey(UINT& SeasonID, UINT& OpenDate, std::string* Error = nullptr);
void Request_Beta99BusinessHotBoxMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99HallGetEntryCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_GetActivityEntryCdnCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99ActivityManagerNewMapEntry(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99ActivityManagerBenefitsMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99ActiveActivityMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99ActiveActivityProcessReward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99SignActivityMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99SignActivityReward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta100WarmUpActivityMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta102RedDemonMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99AnnualSkinMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99AnnualSkinAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta102SpeedCoinTimeLimitBubble(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta97StrengthenBagMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta97StrengthenBagRedPoint(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta97StrengthenBagGetAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99BusinessHotBoxGetActivityInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99BusinessHotBoxEntryList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99BusinessHotBoxGetBenefitsCollectionInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta99BusinessHotBoxGetOuterEntryInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta97BattlePassMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta97BattlePassEntry(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta97BattlePassSettlement(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta97BattlePassAwardInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta97BattlePassTaskInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void Request_Beta97BattlePassAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void SP_AddRaceResult(
	std::shared_ptr<ClientNode> Client,
	bool ranked,
	bool border,
	bool trackWorkshop,
	int finishRank,
	bool champion);
void SP_AddDanceResult(std::shared_ptr<ClientNode> Client, UINT perfectCount);
void SP_AddLoginDay(std::shared_ptr<ClientNode> Client);
void SP_AddKartRefit(std::shared_ptr<ClientNode> Client, int count);
void SP_AddGemSynthesis(std::shared_ptr<ClientNode> Client);
void Request_Beta99BusinessHotBoxGetGiftBoxInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta84GetIllusionSuitCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta84GetIllusionSuitInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta84SetIllusionSuitInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_CarLibraryGetCollect(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta99SpringMechaEntrys(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta99SpringMechaInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta99SpringMechaAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta100WuHuShangJiangEntrys(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta100WuHuShangJiangInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta100WuHuShangJiangAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta101WastelandExploryEntrys(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta101WastelandExploryInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta101WastelandExploryAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta101WastelandTaskBaseMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta101WastelandTaskBaseAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta102NorthdoorInvisibleInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta102NorthdoorInvisibleAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta102WheelsTreasureInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta102WheelsTreasureAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta92RareTreasureMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta92RareTreasureAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta87SummonDragonMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta87SummonDragonAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta82ExplorationMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta82ExplorationDoAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta82ExplorationBoxAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_HmDreamInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_HmDreamAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_ZhiYaoDrawTemplateInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_ZhiYaoDrawTemplateAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_ZhiYaoFashionMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta94TarotCardEntrys(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta94TarotCardAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta94TarotCardMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta95VIPMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta95VIPAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta95VIPRedPoint(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestUseItem(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestReportCommonEvent(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); // 24028

void RequestGetGameEndBoxAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);									  // 24756
void ResponseGetGameEndBoxAward(std::shared_ptr<ClientNode> Client);															  // 24756

void RequestTest(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseStartTimerChallenge2(std::shared_ptr<ClientNode> Client);
void ResponseStartTimerChallenge(std::shared_ptr<ClientNode> Client);
void RequestReportTimerChallenge2ndEvent(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void RequestC2GLoadFile(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2GLoadFile(std::shared_ptr<ClientNode> Client, UCHAR Cmd, UCHAR UuidLen, std::string FileName);
void RequestReportAntiCollisionDataBuff(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); // 26114

void RequestGetBattleModeCarSkillInfoList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); //?
void ResponseGetBattleModeCarSkillInfoList(std::shared_ptr<ClientNode> Client); //10929

UINT GetAppellationLevel(UINT ID);
void NotifyAppellationList(std::shared_ptr<ClientNode> Client);
void RequestWearAppellation(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);	// 1618
void ResponseWearAppellation(std::shared_ptr<ClientNode> Client, UCHAR Type);					// 1618
void RequestUnwearAppellation(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);	// 1619
void ResponseUnwearAppellation(std::shared_ptr<ClientNode> Client, UCHAR Type);					// 1619

void RequestRankedMatchDoBanMap(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
bool ResponseRankedMatchDoBanMap(std::shared_ptr<ClientNode> Client, CONST UCHAR Type);
void RequestReportGameFrameInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); // 2102
void RequestReportGameFrameCheckInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen); // 2103


void Request_Beta87_ReqSelfHeadAndHeadShokInfo(std::shared_ptr<ClientNode> Client, BYTE* Body);
void Request_Beta87_ReqBatchHeadAndHeadShokInfo(std::shared_ptr<ClientNode> Client, BYTE* Body);
void Beta87_ReqHeadAndHeadShokActiveItemCfg(std::shared_ptr<ClientNode> Client, BYTE* Body);
void Beta87_ReqHeadAndHeadShokAvatarCfg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta87_ReqOpHeadAndHeadShok(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta87_ReqSetOrUnSetUseQQHead(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta87_ReqGetQQUrlHead(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta78_MechSkinAwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta81_ReqMechSkinEntrys(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta78_ReqMechSkinInfo(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void RequestGetBeta84ColorfulGourdCfg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void RequestMapChallenge_OpenMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void RequestMapChallenge_GetUserMapInfo(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_beta91_Mountain_Music_MainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_DaZaoWorkshop_OpenMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta72ReqGodOfWarBoxCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta72ReqGodOfWarBoxAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta72RequestGetKittyGiftCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta72RequestGetKittyGiftAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta91_Silk_Road_MainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta87_Dragon_City_MainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta87_Dragon_City_AwardOpt(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_RequestBeta90_SkinTreasureCfg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta77_ReqNiChangPavilionCfg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta91_Xinjiang_MainStation_MainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_CarLibrary_OpenMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_CarLibraryOp(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_CarLibrary_QueryOtherData(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_LimitRecommend_OpenMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta87_GetLoginCfg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Power_GetStatus(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_Beta72RankedMatch_GetSuitPreviewInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
NOINLINE void Request_Beta91_Cinamon_Cafe_MainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);
NOINLINE void Request_ConsumeScore_OpenMainDlg(std::shared_ptr<ClientNode> Client, BYTE* Body);


void NotifyBroadcast65(std::shared_ptr<ClientNode> Client);
BYTE* NotifyBroadcast(BYTE*& ptr, UINT BCFlag, UINT ConTime, UINT DisplayFlag, std::string news);
void NotifyBroadcast17();
void NotifyBroadcast17(std::string news);
void NotifyGame(std::shared_ptr<ClientNode> Client);
void RequestOpenBox(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);											// 168
void RequestGetBoxItems(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);										// 190
void RequestSelOptBoxAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);								// 616
void ResponseOpenBox(std::shared_ptr<ClientNode> Client, int TreasureBoxID, ItemInfo* aItemStatus, int itemNum, int ItemIDNum); // 168
void RequestRestartSingleTask(std::shared_ptr<ClientNode> Client, BYTE* Body);
void ResponseRestartSingleTask(std::shared_ptr<ClientNode> Client);
//车神
void ResponseC2GGetChallengeInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2GGetGuildChallengeInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2GGetMapsChallengeInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2GGetMapChallengeRank(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestC2GGetMapChallengeRank();

void ResponseC2GGetRecentChallengeMsg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestC2GChallenge(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2GChallenge(std::shared_ptr<ClientNode> Client, USHORT MapID, USHORT Rank, char FileMd5);
void NotifyG2CLoadChallengeFile(std::shared_ptr<ClientNode> Client, std::vector<ChallengeMap> ChallengeMaps);
void NotifyC2GStartChallengeResult(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void NotifyC2GLoadChallengeFileResult(std::shared_ptr<ClientNode> Client, std::vector<ChallengeMap> ChallengeMaps);
void NotifyC2GLoadChallengeFileResult(std::shared_ptr<ClientNode> Client, UINT MapID);
void NotifyG2CStartChallenge(std::shared_ptr<ClientNode> Client);
void ResponseC2GChallengeCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void NotifyG2CChallengeResult(std::shared_ptr<ClientNode> Client, UINT Result, int FinTime);
void RequestC2FUpLoad(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2FUpLoad(std::shared_ptr<ClientNode> Client, UCHAR Cmd, UINT DataLen, UINT TotalLen);
void ResponseC2GUpdateChallengeRank(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void NotifyG2CUpLoadChallengeFile(std::shared_ptr<ClientNode> Client);
void NotifyG2CGuildChallengeMsg(std::shared_ptr<ClientNode> Client);
void NotifyG2CUpdateChallengeRankResult(std::shared_ptr<ClientNode> Client, int Result);
void RequestC2GGetMapOwner(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseC2GGetMapOwner(std::shared_ptr<ClientNode> Client, UINT MapId);

void Request24617(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseLuckMatchBetAwardInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseLuckMatchShowInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseLuckyMatchBillboard(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseParticipateLuckyMatch(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseCancelParticipateLuckyMatch(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);

void NotifyCompetitionSystemConfig(std::shared_ptr<ClientNode> Client);
void NotifyConsumeGiftBagInfo(std::shared_ptr<ClientNode> Client);
void RequestGetConsumeGiftBagAward(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void NotifyRelaxRoomAdvertisement(std::shared_ptr<ClientNode> Client);
void NotifyActivityRecommendCfg(std::shared_ptr<ClientNode> Client);
void NotifyAdvertisement(std::shared_ptr<ClientNode> Client);
void NotifyFileCheckList(std::shared_ptr<ClientNode> Client);
void NotifyTVLiveInfo(std::shared_ptr<ClientNode> Client);
void NotifyClientUpdateItemDescInfo(std::shared_ptr<ClientNode> Client);


void ResponseGetPrivilegeIntroInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetExclusiveSpeedWayInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponsePRGetInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetConsumeScoreConfUrl(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetConsumeScoreConfUrl(std::shared_ptr<ClientNode> Client, UCHAR Md5StrLen);

void RequestWorldOnlineInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseWorldOnlineInfo(std::shared_ptr<ClientNode> Client);
std::string bytes_to_hex(const unsigned char* data, size_t length);
bool compare_char_and_byte(const char* str, const unsigned char byte_arr[16]);

void NotifyLuckyMatchReady(std::shared_ptr<ClientNode> Client);
void NotifyParticipateLuckyMatchTimeOut(std::shared_ptr<ClientNode> Client);
void ResponseLmtUpdateStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestChangeRecommandStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void NotifyLmtMatchOver(std::shared_ptr<ClientNode> Client);
void NotifyLmtEnterRoom(std::shared_ptr<ClientNode> Client);
void ResponseLmtEnterChatRoom(std::shared_ptr<ClientNode> Client);
void RequestLmtEnterChatRoom(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestLmtBuglet(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponsetLmtBuglet(std::shared_ptr<ClientNode> Client, UCHAR OpType);

void NotifyG2CFizzStarCfg(std::shared_ptr<ClientNode> Client);
void NotifyItemType2ndCfg(std::shared_ptr<ClientNode> Client);
void NotifyMultiUseItemListCfg(std::shared_ptr<ClientNode> Client);
void NotifyMusicBlackList(std::shared_ptr<ClientNode> Client);
void NotifyClientPersonalGardenCfg(std::shared_ptr<ClientNode> Client);
void NotifyClientDomainResolve(std::shared_ptr<ClientNode> Client);

void RequestQsLabCommittalTaskInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseQsLabCommittalTaskInfo(std::shared_ptr<ClientNode> Client);
void NotifyClientSkillTrainingTipsInfo(std::shared_ptr<ClientNode> Client);
void NotifyClientNoviceGuideCfg(std::shared_ptr<ClientNode> Client);
void ResponseGetWizardHomeBaseInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void RequestGetTaskAwardInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseGetTaskAwardInfo(std::shared_ptr<ClientNode> Client, UINT Type, std::vector<UINT> TaskList, std::string TaskStr);
void NotifyFirstRechargeCfg(std::shared_ptr<ClientNode> Client);
void NotifyGroupBuyingrCfg(std::shared_ptr<ClientNode> Client);
void NotifyFBPlayerLevInfo(std::shared_ptr<ClientNode> Client);
void ResponseJiaNianHuaCfg(std::shared_ptr<ClientNode> Client);
void NotifyAllAreaNameCfg(std::shared_ptr<ClientNode> Client);
void NotifyAllCityCfg(std::shared_ptr<ClientNode> Client);
void NotifyScoreBoxCfg(std::shared_ptr<ClientNode> Client);
void NotifyRedEnvelopeInfo(std::shared_ptr<ClientNode> Client);
void NotifyPlayerRaceCardAddInfo(std::shared_ptr<ClientNode> Client);
void NotifyClientGolumBallAwardInfo(std::shared_ptr<ClientNode> Client);
void NotifyLmtGetScoreInfo(std::shared_ptr<ClientNode> Client);
void ResponseLmtGetScoreInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponseLmtGetElimInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void NotifyGangsterCfg(std::shared_ptr<ClientNode> Client);
void RequestPlayerActionPara(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen);
void ResponsePlayerActionPara(std::shared_ptr<ClientNode> Client, USHORT ActionType, UINT Para0, UINT Para1, UINT Para2, UINT Para3, UINT Para4);
void ResponsePlayerActionParaClientFlags(std::shared_ptr<ClientNode> Client, USHORT ActionType, UINT Para0, UINT Para1, UINT Para2, UINT Para3, UINT Para4, const BYTE* ClientFlags, USHORT ClientFlagLen);
void ResponsePlayerActionParaCustomScheme(std::shared_ptr<ClientNode> Client, USHORT ActionType, UINT schemeID, const std::vector<BYTE>& schemeWire);
// 24640 简化响应：HasCustomScheme=0，不带 scheme 数据。21043 官方 6P 自定义赛后回房就是用这个版本（AT=62，body 91B）。
void ResponsePlayerActionParaAck(std::shared_ptr<ClientNode> Client, USHORT ActionType);
void ResponseCustomSchemeMapList28461(std::shared_ptr<ClientNode> Client);
void ResponseCustomSchemeList28460(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
