#include "Outdoor.h"

#include "Room.h"

#include <stdio.h>
#include <algorithm>
#include <limits>
#include <mutex>
#include <string>
#include <time.h>
#include<map>
#include "yaml-cpp/yaml.h"
#include "Database.h"
#include "PersonalGarden.h"


constexpr int MAX_CHAIR_NUM = 200;
constexpr int MAX_NUM_IN_EACHCHAIR = 2;
extern std::map<long, std::shared_ptr<ClientNode>> RelaxClients;
extern std::recursive_mutex RelaxClientMutex;
extern std::string Name;
//struct MoveInfo
//{
//	UINT Uin;
//	char WalkStatue;
//	int Posion[3];
//	int Ori[9];
//	UINT AttachUin;
//	int WalkExtState;
//};


ChatRoomNode ChatRooms[ChatRoomNum];

namespace
{
	constexpr USHORT kOfficial3DRacingSceneID = 400;
	constexpr int kOfficial3DRacingX = -3726;
	constexpr int kOfficial3DRacingY = -81;
	constexpr int kOfficial3DRacingZ = 9;

	static std::shared_ptr<ChatRoomNode> GetOrCreateOfficial3DRoomWithRoomLockHeld(
		const std::shared_ptr<RoomNode>& Room)
	{
		if (!Room || Room->ChatRoomType != 6 || Room->ID > 0xFFFFu)
		{
			return nullptr;
		}

		if (Room->OutdoorRoom)
		{
			if (Room->OutdoorRoom->ID != Room->ID ||
				Room->OutdoorRoom->SceneID != Room->SceneID)
			{
				QQS_DebugPrintf10001(
					"[3D-ROOM][REJECT] RoomID=%u ContextRoomID=%u SceneID=%d ContextSceneID=%d Reason=context-mismatch\n",
					Room->ID,
					Room->OutdoorRoom->ID,
					Room->SceneID,
					Room->OutdoorRoom->SceneID);
				return nullptr;
			}
			return Room->OutdoorRoom;
		}

		// 官服 packet_dumps_34328 的 Msg103 请求为 SceneID=400；
		// 随后的原始 Msg352 明确给出 RoomID、SceneID 和坐标
		// (-3726, -81, 9)。没有对应抓包的场景不在这里猜测坐标。
		if (Room->SceneID != kOfficial3DRacingSceneID)
		{
			QQS_DebugPrintf10001(
				"[3D-ROOM][REJECT] RoomID=%u SceneID=%d Reason=uncaptured-scene\n",
				Room->ID,
				Room->SceneID);
			return nullptr;
		}

		std::shared_ptr<ChatRoomNode> ChatRoom = std::make_shared<ChatRoomNode>();
		ChatRoom->ID = Room->ID;
		ChatRoom->Name = "3DRoom";
		ChatRoom->CurrPlayerNum = 0;
		ChatRoom->MaxPlayerNum = 99;
		ChatRoom->RoomType = 0;
		ChatRoom->SceneID = kOfficial3DRacingSceneID;
		ChatRoom->x = kOfficial3DRacingX;
		ChatRoom->y = kOfficial3DRacingY;
		ChatRoom->z = kOfficial3DRacingZ;
		Room->OutdoorRoom = ChatRoom;
		return ChatRoom;
	}

	static std::shared_ptr<ChatRoomNode> GetOrCreateOfficial3DRoom(
		const std::shared_ptr<RoomNode>& Room)
	{
		if (!Room)
			return nullptr;

		std::unique_lock<std::shared_mutex> lock(Room->mutex);
		return GetOrCreateOfficial3DRoomWithRoomLockHeld(Room);
	}
}

void InitChatRoom()
{
	ChatRooms[0].ID = 0;
	ChatRooms[0].Name = "琳琅湾";
	ChatRooms[0].CurrPlayerNum = 0;
	ChatRooms[0].MaxPlayerNum = 99;
	ChatRooms[0].RoomType = 1;//RoomType必须一致才能进入房间
	ChatRooms[0].SceneID = 109;
	ChatRooms[0].x = 0;
	ChatRooms[0].y = -50000;
	ChatRooms[0].z = 0;
	for (size_t i = 0; i < 99; i++)
	{
		ChatRooms[0].Player[i] = NULL;
	}

	ChatRooms[1].ID = 1;
	ChatRooms[1].Name = "琳琅镇";
	ChatRooms[1].CurrPlayerNum = 0;
	ChatRooms[1].MaxPlayerNum = 99;
	ChatRooms[1].RoomType = 2;
	ChatRooms[1].SceneID = 25;
	ChatRooms[1].x = 0;
	ChatRooms[1].y = 0;
	ChatRooms[1].z = 0;
	for (size_t i = 0; i < 99; i++)
	{
		ChatRooms[1].Player[i] = NULL;
	}

	ChatRooms[2].ID = 2;
	ChatRooms[2].Name = "单身派对";
	ChatRooms[2].CurrPlayerNum = 0;
	ChatRooms[2].MaxPlayerNum = 99;
	ChatRooms[2].RoomType = 3;
	ChatRooms[2].SceneID = 109;
	ChatRooms[2].x = 0;
	ChatRooms[2].y = -40000;
	ChatRooms[2].z = 0;
	for (size_t i = 0; i < 99; i++)
	{
		ChatRooms[2].Player[i] = NULL;
	}

	ChatRooms[3].ID = 3;
	ChatRooms[3].Name = "皇族休闲区";
	ChatRooms[3].CurrPlayerNum = 0;
	ChatRooms[3].MaxPlayerNum = 99;
	ChatRooms[3].RoomType = 4;
	ChatRooms[3].SceneID = 136;
	ChatRooms[3].x = 10000;
	ChatRooms[3].y = 0;
	ChatRooms[3].z = 0;
	for (size_t i = 0; i < 99; i++)
	{
		ChatRooms[3].Player[i] = NULL;
	}

	ChatRooms[4].ID = 4;
	ChatRooms[4].Name = "中国城";
	ChatRooms[4].CurrPlayerNum = 0;
	ChatRooms[4].MaxPlayerNum = 99;
	ChatRooms[4].RoomType = 3;
	ChatRooms[4].SceneID = 4;
	ChatRooms[4].x = 33071;
	ChatRooms[4].y = -45773;
	ChatRooms[4].z = 4368;
	for (size_t i = 0; i < 99; i++)
	{
		ChatRooms[4].Player[i] = NULL;
	}

	ChatRooms[5].ID = 5;
	ChatRooms[5].Name = "3DRoom";
	ChatRooms[5].CurrPlayerNum = 0;
	ChatRooms[5].MaxPlayerNum = 99;
	ChatRooms[5].RoomType = 0;
	ChatRooms[5].SceneID = 0x190;
	ChatRooms[5].x = 0xfffff172;
	ChatRooms[5].y = 0xffffffaf;
	ChatRooms[5].z = 0x9;
	for (size_t i = 0; i < 99; i++)
	{
		ChatRooms[5].Player[i] = NULL;
	}
	ChatRooms[6].ID = 6;
	ChatRooms[6].Name = "3DDanceRoom";
	ChatRooms[6].CurrPlayerNum = 0;
	ChatRooms[6].MaxPlayerNum = 99;
	ChatRooms[6].RoomType = 0;
	ChatRooms[6].SceneID = 401;
	ChatRooms[6].x = 0;
	ChatRooms[6].y = 0;
	ChatRooms[6].z = 0;
	for (size_t i = 0; i < 99; i++)
	{
		ChatRooms[6].Player[i] = NULL;
	}
}



void NotifyNPCInfo(std::shared_ptr<ClientNode> Client);
void NotifyChairInfo(std::shared_ptr<ClientNode> Client);
void ResponseEnterOutdoor(std::shared_ptr<ClientNode> Client, short SceneID, int x, int y, int z);
void NotifyGuildRoomMoreInfo(std::shared_ptr<ClientNode> Client);
void ResponseChangeChairState(std::shared_ptr<ClientNode> Client, BYTE ChangeState, int ChairIndx, BYTE SeatIndx);
void NotifyEnterOutdoor(std::shared_ptr<ClientNode> Client, ChatRoomNode* ChatRoom, std::shared_ptr<ClientNode> RoomClient, char SeatID);

extern bool GetRandItem(std::shared_ptr<ClientNode> Client, UINT Itemid, short count, std::vector<ItemInfo>& items);

typedef struct EggInfo {
	UINT TypeID;
	int Pos;
	int KeyID;
	int UseNum;
}EGGINFO;
typedef struct ChatRoomEggInfo {
	std::vector<EGGINFO> EggInfos;
}CHATROOMEGGINFO;
std::map<long, std::shared_ptr<CHATROOMEGGINFO>> ChatRoomEggInfos;

void LoadOutDoorEggs()
{
	try
	{
		YAML::Node Config = YAML::LoadFile(".\\config\\OutDoorEgg.yml");
		size_t Num = Config.size();
		YAML::Node EggConfig;
		int ChatRoomID = 0;
		for (size_t i = 0; i < Num; i++)
		{
			std::shared_ptr<CHATROOMEGGINFO> ChatRoomEggInfo = std::make_shared<CHATROOMEGGINFO>();

			ChatRoomID = Config[i]["ChatRoomID"].as<int>();
			EggConfig = Config[i]["Eggs"];
			size_t EggNum = EggConfig.size();
			ChatRoomEggInfo->EggInfos.resize(EggNum);
			//printf("ChatRoomID:%d\n", ChatRoomID);
			//printf("EggNum:%d\n", EggNum);
			for (size_t j = 0; j < EggNum; j++)
			{
				ChatRoomEggInfo->EggInfos[j].TypeID = EggConfig[j]["Type"].as<int>();
				ChatRoomEggInfo->EggInfos[j].Pos = EggConfig[j]["Pos"].as<int>();
				ChatRoomEggInfo->EggInfos[j].KeyID = EggConfig[j]["KeyID"].as<int>();
				ChatRoomEggInfo->EggInfos[j].UseNum = EggConfig[j]["UseNum"].as<int>();
				//printf("TypeID:%d\n", EggInfos[j].TypeID);
				//printf("Pos:%d\n", EggInfos[j].Pos);
				//printf("KeyID:%d\n", EggInfos[j].KeyID);
				//printf("UseNum:%d\n", EggInfos[j].UseNum);
			}
			ChatRoomEggInfos[ChatRoomID] = ChatRoomEggInfo;
		}
	}
	catch (const std::exception& e)
	{
		printf("Load OutDoor Error!\n");
		return;
	}
	printf("Load OutDoorEgg Success!\n");
}

CHATROOMEGGINFO* GetChatRoomEggInfo(long ChatRoomID)
{
	CHATROOMEGGINFO* ChatRoomEggInfo = NULL;
	auto iter = ChatRoomEggInfos.find(ChatRoomID);
	if (iter != ChatRoomEggInfos.end())
	{
		ChatRoomEggInfo = iter->second.get();
	}
	else
	{
	}
	return ChatRoomEggInfo;
}
//休闲区增加经验
void NotifyClientUpdateExp(std::shared_ptr<ClientNode> Client)
{
	
	
	int result;
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;
	int RandExp = rand() % 100 + 1;
	//Client->Experience += RandExp;

	SelectClientExp(Client);
	Client->Experience = (Client->Experience > UINT_MAX - RandExp) ? UINT_MAX : Client->Experience + RandExp;
	//Client->Experience += RandExp;
	UpdateClientBase(Client);


	Write32(p, Client->Uin); //Uin
	Write32(p, RandExp); //ExpInc
	Write32(p, Client->Experience); //ExpTotal
	Write16(p, 0); //PrefixStringLen

	len = p - buf.data();
	SendToClient(Client, 546, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}

ChatRoomNode* GetChatRoom(int ChatRoomID)
{
	ChatRoomNode* ChatRoom = NULL;
	for (size_t i = 0; i < ChatRoomNum; i++)
	{
		if (ChatRooms[i].ID == ChatRoomID)
		{
			ChatRoom = &ChatRooms[i];
			break;
		}
	}
	if (!ChatRoom)
		ChatRoom = PersonalGardenFindChatRoom(ChatRoomID);
	if (!ChatRoom && ChatRoomID >= 0)
	{
		std::shared_ptr<RoomNode> Room = GetRoom(ChatRoomID);
		if (Room && Room->OutdoorRoom &&
			Room->OutdoorRoom->ID == static_cast<UINT>(ChatRoomID))
		{
			ChatRoom = Room->OutdoorRoom.get();
		}
	}
	return ChatRoom;
}


static void RequestEnter3DRoomInternal(
	std::shared_ptr<ClientNode> Client,
	bool roomLockAlreadyHeld)
{
	if (!Client)
	{
		return;
	}

	std::shared_ptr<RoomNode> Room = GetRoom(Client->RoomID);
	if (!Room || Room->ChatRoomType != 6)
	{
		QQS_DebugPrintf10001(
			"[3D-ROOM][ENTER-REJECT] Uin=%u RoomID=%u Reason=invalid-game-room\n",
			Client->Uin,
			Client->RoomID);
		return;
	}

	std::shared_ptr<ChatRoomNode> ChatRoomOwner = roomLockAlreadyHeld
		? GetOrCreateOfficial3DRoomWithRoomLockHeld(Room)
		: GetOrCreateOfficial3DRoom(Room);
	ChatRoomNode* ChatRoom = ChatRoomOwner.get();
	if (!ChatRoom)
	{
		return;
	}


	for (char i = 0; i < 99; i++)
	{
		if (!ChatRoom->Player[i])
		{
			Client->ChatRoomID = static_cast<INT>(Room->ID);
			ChatRoom->Player[i] = Client;
			ChatRoom->CurrPlayerNum++;


			int x = ChatRoom->x;
			int y = ChatRoom->y;
			int z = ChatRoom->z;
			//printf("SceneID:%d\n", SceneID);
			//fflush(stdout);
			NotifyNPCInfo(Client);
			ResponseEnterOutdoor(Client, ChatRoom->SceneID, x, y, z);
			NotifyGuildRoomMoreInfo(Client);
			NotifyChairInfo(Client);

			//通知其它玩家
			for (char i2 = 0; i2 < 99; i2++)
			{
				std::shared_ptr<ClientNode> RoomClient = ChatRoom->Player[i2];
				if (RoomClient)
				{
					if (RoomClient != Client)
					{
						NotifyEnterOutdoor(RoomClient, ChatRoom, Client, i);//通知其他人看到自己
						NotifyEnterOutdoor(Client, ChatRoom, RoomClient, i2);//通知自己看到其他人
					}
				}
			}

			break;
		}
	}
}

void RequestEnter3DRoom(std::shared_ptr<ClientNode> Client)
{
	RequestEnter3DRoomInternal(Client, false);
}

void RequestEnter3DRoomWithRoomLockHeld(std::shared_ptr<ClientNode> Client)
{
	RequestEnter3DRoomInternal(Client, true);
}
extern void RequestEnterInvitedRoom(std::shared_ptr<ClientNode> Client, std::shared_ptr<RoomNode> Room);
void RequestEnterOutdoor(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (!Client || !Body || BodyLen < 16 ||
		BodyLen > static_cast<size_t>((std::numeric_limits<WORD>::max)()))
	{
		return;
	}
	BYTE* requestBody = Body;
	// Message dispatch has already consumed the wire BodyLen field. Body points
	// at Uin while BodyLen still reports the original wire-body length.
	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);

	USHORT SceneID = Read16(Body);
	USHORT ChatRoomID = Read16(Body);
	USHORT NPCCfgVer = Read16(Body);
	if (Uin != Client->Uin)
		return;

	RequestCancelMatch(Client);
	LeaveRoom(Client, 0);
	LeaveBorderRoom(Client, 0);
	if (PersonalGardenTryEnterOutdoor(Client, requestBody, BodyLen))
		return;
	LeaveOutdoor(Client);
	if (ChatRoomID > 100)
	{
		//邀请进入游戏房间
		std::shared_ptr<RoomNode> Room = GetRoom(ChatRoomID);
		if (!Room)
		{
			return;
		}
		RequestEnterInvitedRoom(Client, Room);
		return;
	}


	//printf("SceneID:%d, ChatRoomID:%d\n",SceneID, ChatRoomID);

	//fflush(stdout);
	ChatRoomNode* ChatRoom = GetChatRoom(ChatRoomID);
	if (!ChatRoom)
	{
		return;
	}
	

	for (char i = 0; i < 99; i++)
	{
		if (!ChatRoom->Player[i])
		{
			Client->ChatRoomID = ChatRoomID;
			ChatRoom->Player[i] = Client;
			ChatRoom->CurrPlayerNum++;
			
			SceneID = ChatRoom->SceneID;
			int x = ChatRoom->x;
			int y = ChatRoom->y;
			int z = ChatRoom->z;
			//printf("SceneID:%d\n", SceneID);
			//fflush(stdout);
			NotifyNPCInfo(Client);
			ResponseEnterOutdoor(Client, SceneID, x, y, z);
			NotifyGuildRoomMoreInfo(Client);
			NotifyChairInfo(Client);

			//通知其它玩家
			for (char i2 = 0; i2 < 99; i2++)
			{
				std::shared_ptr<ClientNode> RoomClient = ChatRoom->Player[i2];
				if (RoomClient)
				{
					if (RoomClient != Client)
					{
						NotifyEnterOutdoor(RoomClient, ChatRoom, Client, i);//通知其他人看到自己
						NotifyEnterOutdoor(Client, ChatRoom, RoomClient, i2);//通知自己看到其他人
					}
				}
			}

			break;
		}
	}
	
}
void ResponseEnterOutdoor(std::shared_ptr<ClientNode> Client, short SceneID, int x, int y, int z)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;


	Write16(p, Client->ChatRoomID); //ChatRoomID
	Write16(p, SceneID); //SceneID

	{ //HeroMoveInfo
		BYTE* pHeroMoveInfo = p;
		Write16(pHeroMoveInfo, 0); //len

		Write32(pHeroMoveInfo, 0); //Uin
		Write8(pHeroMoveInfo, 0); //WalkStatue

		Write32(pHeroMoveInfo, x); //Posion[3]
		Write32(pHeroMoveInfo, y);
		Write32(pHeroMoveInfo, z);

		Write32(pHeroMoveInfo, 1000); //Ori[9]
		Write32(pHeroMoveInfo, 0);
		Write32(pHeroMoveInfo, 0);
		Write32(pHeroMoveInfo, 0);
		Write32(pHeroMoveInfo, 1000);
		Write32(pHeroMoveInfo, 0);
		Write32(pHeroMoveInfo, 0);
		Write32(pHeroMoveInfo, 0);
		Write32(pHeroMoveInfo, 1000);

		Write32(pHeroMoveInfo, 0); //AttachUin
		Write32(pHeroMoveInfo, 0); //WalkExtState
		Write8(pHeroMoveInfo, 0); //PermitSitHorsePetUinNum
		Write32(pHeroMoveInfo, 0); //EquipedPetID
		Write32(pHeroMoveInfo, 0); //EquipedPetStatus
		Write32(pHeroMoveInfo, 0); //PetPKResultStatus
		Write16(pHeroMoveInfo, 0); //HeroMajorState
		Write16(pHeroMoveInfo, 0); //HorsePetStatus

		len = pHeroMoveInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	Write16(p, 0); //ResultID

	const std::string& RoomName = Name;
	len = (std::min)(RoomName.size(), static_cast<size_t>(255));
	Write8(p, (BYTE)len); //ReasonLen
	memcpy(p, RoomName.data(), len);
	p += len;


	Write16(p, 0); //HeroCount

	
	Write16(p, 0); //RelationHeroCount
	

	Write16(p, 0); //KartNum
	

	Write16(p, 0); //HammerCount
	/*
m_astEquipedHammer[].m_iEquipedHammerID
	*/

	Write16(p, 0); //StealPigCount
	/*
m_astStealPig[].m_cStatus
m_astStealPig[].m_cPigPos
m_astStealPig[].m_iItemID
	*/
	ChatRoomNode* responseRoom = GetChatRoom(Client->ChatRoomID);
	Write16(p, responseRoom ? responseRoom->MaxPlayerNum : 0); //MaxHeroNumInRoom


	len = p - buf.data();
	SendToClient(Client, 352, buf.data(), len, Client->ChatRoomID, FE_OUTDOORSVRD, 1, Response);
}

void NotifyEnterOutdoor(std::shared_ptr<ClientNode> Client, ChatRoomNode* ChatRoom, std::shared_ptr<ClientNode> RoomClient, char SeatID)
{
	
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, ChatRoom->ID); //ChatRoomID
	Write16(p, ChatRoom->SceneID); //SceneID
	Write32(p, (DWORD)time(nullptr)); //Time


	{ //HeroInfo
		
		BYTE* pHeroInfo = p;
		Write16(pHeroInfo, 0); //len

		{ //HeroMoveInfo
			BYTE* pHeroMoveInfo = pHeroInfo;
			Write16(pHeroMoveInfo, 0); //len

			Write32(pHeroMoveInfo, RoomClient->Uin); //Uin
			Write8(pHeroMoveInfo, 0); //WalkStatue

			Write32(pHeroMoveInfo, ChatRoom->x); //Posion[3]
			Write32(pHeroMoveInfo, ChatRoom->y);
			Write32(pHeroMoveInfo, ChatRoom->z);

			Write32(pHeroMoveInfo, 1000); //Ori[9]
			Write32(pHeroMoveInfo, 0);
			Write32(pHeroMoveInfo, 0);
			Write32(pHeroMoveInfo, 0);
			Write32(pHeroMoveInfo, 1000);
			Write32(pHeroMoveInfo, 0);
			Write32(pHeroMoveInfo, 0);
			Write32(pHeroMoveInfo, 0);
			Write32(pHeroMoveInfo, 1000);

			Write32(pHeroMoveInfo, 0); //AttachUin
			Write32(pHeroMoveInfo, 0); //WalkExtState
			Write8(pHeroMoveInfo, 0); //PermitSitHorsePetUinNum
			Write32(pHeroMoveInfo, 0); //EquipedPetID
			Write32(pHeroMoveInfo, 0); //EquipedPetStatus
			Write32(pHeroMoveInfo, 0); //PetPKResultStatus
			Write16(pHeroMoveInfo, 0); //HeroMajorState
			Write16(pHeroMoveInfo, 0); //HorsePetStatus

			len = pHeroMoveInfo - pHeroInfo;
			Set16(pHeroInfo, (WORD)len);
			pHeroInfo += len;
		}

		Write32(pHeroInfo, 0); //NextPoint
		Write16(pHeroInfo, 0); //PathLen
		//m_astHeroInfo[].m_aiPath[]

		WriteVisbleInfo(RoomClient, pHeroInfo, RoomClient->Uin, SeatID, (short)GetClientWirePlayerID16(RoomClient)); //HeroVisbleInfo PlayerID


		Write16(pHeroInfo, 0); //HeroEnterDelayTime


		len = pHeroInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}
	


	{ //PlayerRelationInfo
		BYTE* pPlayerRelationInfo = p;
		Write16(pPlayerRelationInfo, 0); //len

		Write32(pPlayerRelationInfo, RoomClient->Uin); //SrcUin
		Write32(pPlayerRelationInfo, 0); //RelationFlag
		Write32(pPlayerRelationInfo, 0); //RelationUin

		memset(pPlayerRelationInfo, 0, MaxNickName); //RelationNickname[]
		strcpy_s((char*)pPlayerRelationInfo, MaxNickName, "test");
		pPlayerRelationInfo += MaxNickName;

		Write32(pPlayerRelationInfo, 0); //EngageTime
		Write32(pPlayerRelationInfo, 0); //NextCanBookingTimeLimit
		Write32(pPlayerRelationInfo, 0); //BeginHoneyMoonTime
		Write32(pPlayerRelationInfo, 0); //EndHoneyMoonTime
		
		Write8(pPlayerRelationInfo, 0); //EngageFlag


		len = pPlayerRelationInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	Write16(p, 0); //KartNum
	/*
m_astKartRefitInfo[].m_uiUin
m_astKartRefitInfo[].m_iKartId
m_astKartRefitInfo[].m_iRefitCout
m_astKartRefitInfo[].m_shMaxFlags
m_astKartRefitInfo[].m_shWWeight
m_astKartRefitInfo[].m_shSpeedWeight
m_astKartRefitInfo[].m_shJetWeight
m_astKartRefitInfo[].m_shSJetWeight
m_astKartRefitInfo[].m_shAccuWeight
m_astKartRefitInfo[].m_iShapeRefitCount
m_astKartRefitInfo[].m_iKartHeadRefitItemID
m_astKartRefitInfo[].m_iKartTailRefitItemID
m_astKartRefitInfo[].m_iKartFlankRefitItemID
m_astKartRefitInfo[].m_iKartTireRefitItemID
	*/

	{ //StealPig
		BYTE* pStealPig = p;
		Write16(pStealPig, 0); //len


		Write8(pStealPig, 0); //Status
		Write8(pStealPig, 0); //PigPos
		Write32(pStealPig, 0); //ItemID


		len = pStealPig - p;
		Set16(p, (WORD)len);
		p += len;
	}


	len = p - buf.data();
	SendToClient(Client, 852, buf.data(), len, ChatRoom->ID, FE_OUTDOORSVRD, 1, Notify);
}

void NotifyNPCInfo(std::shared_ptr<ClientNode> Client)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, 4); //NPCCfgVer
	Write8(p, 0); //NPCNum


	len = p - buf.data();
	SendToClient(Client, 863, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}

void NotifyGuildRoomMoreInfo(std::shared_ptr<ClientNode> Client)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write32(p, 0); //GuildID
	Write8(p, 0); //GuildRoomID

	BYTE* pHeroCount = p;
	Write16(p, 0); //HeroCount
	/*ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
	if (!ChatRoom)
	{
		return;
	}
	short HeroCount = 0;
	for (char i = 0; i < 99; i++)
	{
		if (ChatRoom->Player[i] && ChatRoom->Player[i] != Client)
		{ //HeroInfo
			HeroCount++;

			BYTE* pHeroInfo = p;
			Write16(pHeroInfo, 0); //len

			{ //HeroMoveInfo
				BYTE* pHeroMoveInfo = pHeroInfo;
				Write16(pHeroMoveInfo, 0); //len

				Write32(pHeroMoveInfo, ChatRoom->Player[i]->Uin); //Uin
				Write8(pHeroMoveInfo, 0); //WalkStatue

				Write32(pHeroMoveInfo, ChatRoom->x); //Posion[3]
				Write32(pHeroMoveInfo, ChatRoom->y);
				Write32(pHeroMoveInfo, ChatRoom->z);

				Write32(pHeroMoveInfo, 1000); //Ori[9]
				Write32(pHeroMoveInfo, 0);
				Write32(pHeroMoveInfo, 0);
				Write32(pHeroMoveInfo, 0);
				Write32(pHeroMoveInfo, 1000);
				Write32(pHeroMoveInfo, 0);
				Write32(pHeroMoveInfo, 0);
				Write32(pHeroMoveInfo, 0);
				Write32(pHeroMoveInfo, 1000);

				Write32(pHeroMoveInfo, 0); //AttachUin
				Write32(pHeroMoveInfo, 0); //WalkExtState

				len = pHeroMoveInfo - pHeroInfo;
				Set16(pHeroInfo, (WORD)len);
				pHeroInfo += len;
			}

			Write32(pHeroInfo, 0); //NextPoint
			Write16(pHeroInfo, 0); //PathLen
			//m_astHeroInfo[].m_aiPath[]

			WriteVisbleInfo(ChatRoom->Player[i], pHeroInfo, ChatRoom->Player[i]->Uin, i, (short)GetClientWirePlayerID16(ChatRoom->Player[i])); //HeroVisbleInfo PlayerID
			
			Write16(pHeroInfo, 0); //HeroEnterDelayTime


			len = pHeroInfo - p;
			Set16(p, (WORD)len);
			p += len;
		}
	}
	Set16(pHeroCount, HeroCount);
	*/

	Write16(p, 0); //RelationHeroCount
	/*
m_astPlayerRelationInfo[].m_uiSrcUin
m_astPlayerRelationInfo[].m_iRelationFlag
m_astPlayerRelationInfo[].m_uiRelationUin
m_astPlayerRelationInfo[].m_szRelationNickname[]:
m_astPlayerRelationInfo[].m_u32EngageTime
m_astPlayerRelationInfo[].m_uiNextCanBookingTimeLimit
m_astPlayerRelationInfo[].m_uiBeginHoneyMoonTime
m_astPlayerRelationInfo[].m_uiEndHoneyMoonTime
m_astPlayerRelationInfo[].m_bEngageFlag
	*/

	Write16(p, 0); //KartNum
	/*
m_astKartRefitInfo[].m_uiUin
m_astKartRefitInfo[].m_iKartId
m_astKartRefitInfo[].m_iRefitCout
m_astKartRefitInfo[].m_shMaxFlags
m_astKartRefitInfo[].m_shWWeight
m_astKartRefitInfo[].m_shSpeedWeight
m_astKartRefitInfo[].m_shJetWeight
m_astKartRefitInfo[].m_shSJetWeight
m_astKartRefitInfo[].m_shAccuWeight
m_astKartRefitInfo[].m_iShapeRefitCount
m_astKartRefitInfo[].m_iKartHeadRefitItemID
m_astKartRefitInfo[].m_iKartTailRefitItemID
m_astKartRefitInfo[].m_iKartFlankRefitItemID
m_astKartRefitInfo[].m_iKartTireRefitItemID
	*/

	Write16(p, 0); //HammerCount
	/*
m_astEquipedHammer[].m_iEquipedHammerID
	*/

	Write16(p, 0); //StealPigCount
	/*
m_astStealPig[].m_cStatus
m_astStealPig[].m_cPigPos
m_astStealPig[].m_iItemID
	*/

	Write8(p, 0); //HaveNext

	Write8(p, 0); //HaveAppellation
	/*
m_astCurAppellation[].m_u8Type
m_astCurAppellation[].m_u8Level
m_astCurAppellation[].m_u8Status
m_astCurAppellation[].m_u8Difficulty
m_astCurAppellation[].m_i32MapId
m_astCurAppellation[].m_u32Value
	*/


	len = p - buf.data();
	SendToClient(Client, 11019, buf.data(), len, Client->ChatRoomID, FE_OUTDOORSVRD, 1, Notify);
}

void NotifyChairInfo(std::shared_ptr<ClientNode> Client)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	for (size_t i = 0; i < MAX_CHAIR_NUM; i++)
	{ //ChairInfo
		BYTE* pChairInfo = p;
		Write16(pChairInfo, 0); //len

		for (size_t i = 0; i < MAX_NUM_IN_EACHCHAIR; i++)
		{ //UinInChair
			Write32(pChairInfo, 0);
		}

		len = pChairInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	len = p - buf.data();
	SendToClient(Client, 864, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}

void NotifyChangeChairState(std::shared_ptr<ClientNode> Client,UINT Uin,BYTE ChangeState, int ChairIndx, BYTE SeatIndx)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;

	Write32(p, ChairIndx); //ChairIndx
	Write8(p, SeatIndx); //SeatIndx
	Write32(p, Uin); //Uin
	Write8(p, ChangeState); //State

	len = p - buf.data();
	SendToClient(Client, 865, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void RequestChangeChairState(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);


	BYTE ChangeState = Read8(Body);
	int ChairIndx = Read32(Body);
	BYTE SeatIndx = Read8(Body);

	ResponseChangeChairState(Client, ChangeState, ChairIndx, SeatIndx);
	ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
	if (!ChatRoom)
	{
		return;
	}
	for (char i2 = 0; i2 < 99; i2++)
	{
		std::shared_ptr<ClientNode> RoomClient = ChatRoom->Player[i2];
		if (RoomClient)
		{
			if (RoomClient != Client)
			{
				NotifyChangeChairState(RoomClient, Client->Uin, ChangeState, ChairIndx, SeatIndx);
			}
		}
	}
}
void ResponseChangeChairState(std::shared_ptr<ClientNode> Client, BYTE ChangeState, int ChairIndx, BYTE SeatIndx)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;


	Write32(p, Client->Uin); //Uin
	Write16(p, 0); //Result

	Write8(p, ChangeState);
	Write32(p, ChairIndx);
	Write8(p, SeatIndx);


	len = p - buf.data();
	SendToClient(Client, 360, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Response);
}

void LeaveOutdoor(std::shared_ptr<ClientNode> Client)
{
	if (PersonalGardenTryLeaveOutdoor(Client))
		return;
	ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
	if (!ChatRoom)
	{
		return;
	}
	for (char i = 0; i < 99; i++)
	{
		if (ChatRoom->Player[i] == Client)
		{
			ResponseLeaveOutdoor(Client);
			ChatRoom->Player[i] = NULL;
			Client->ChatRoomID = -1;
			ChatRoom->CurrPlayerNum--;

			//通知其它玩家
			for (char i2 = 0; i2 < 99; i2++)
			{
				std::shared_ptr<ClientNode> RoomClient = ChatRoom->Player[i2];
				if (RoomClient)
				{
					if (RoomClient != Client)
					{
						NotifyLeaveOutdoor(RoomClient, Client->Uin);
					}
				}
			}
			break;
		}
	}
}
void NotifyChangeHammerStatus(std::shared_ptr<ClientNode> Client)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;

	Write32(p, 0); //SrcUin
	Write32(p, 0); //Time
	{ //EquipedHammer
		BYTE* pEquipedHammer = p;
		Write16(pEquipedHammer, 0); //len

		Write32(pEquipedHammer, 0); //EquipedHammerID

		len = pEquipedHammer - p;
		Set16(p, (WORD)len);
		p += len;
	}

	len = p - buf.data();
	SendToClient(Client, 861, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void ResponseChangeHammerStatus(std::shared_ptr<ClientNode> Client)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;

	Write16(p, 0); //ResultID
	{ //EquipedHammer
		BYTE* pEquipedHammer = p;
		Write16(pEquipedHammer, 0); //len

		Write32(pEquipedHammer, 0); //EquipedHammerID

		len = pEquipedHammer - p;
		Set16(p, (WORD)len);
		p += len;
	}
	Write8(p, 0); //ReasonLen

	len = p - buf.data();
	SendToClient(Client, 359, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Response);
}

//void RequestChangeHammerStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
//{
//	//Msg 359 休闲区切换锤子pk
//	BYTE* p = Body; WORD len;
//
//	UINT Uin = Read32(p);
//	UINT Time = Read32(p);
//	{ //EquipedHammer
//		BYTE* pEquipedHammer = p;
//		len = Read16(pEquipedHammer);
//
//		int EquipedHammerID = Read32(pEquipedHammer);
//
//		p += len;
//	}
//
//}
void RequestLeaveOutdoor(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	UINT Uin = Read32(Body);
	UINT Time = Read32(Body);
	USHORT Temp1 = Read16(Body);//LeaveType
	USHORT Time2 = Read16(Body);//Room ID
	if (Temp1 == 0)//进入休闲区会自动请求这个消息 需要判断防止异常退出
	{
		LeaveRoom(Client, 0);
		LeaveBorderRoom(Client, 0);
		LeaveOutdoor(Client);
		std::lock_guard<std::recursive_mutex> lg(RelaxClientMutex);
		RelaxClients.erase(Client->Uin);//清理休闲用户
	}
		
}
void ResponseLeaveOutdoor(std::shared_ptr<ClientNode> Client)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;

	Write16(p, 0); //ResultID
	Write8(p, 0); //ReasonLen

	len = p - buf.data();
	SendToClient(Client, 353, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Response);
}
void NotifyLeaveOutdoor(std::shared_ptr<ClientNode> Client, UINT Uin)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;

	Write32(p, Uin); //Uin
	Write32(p, 0); //Time

	len = p - buf.data();
	SendToClient(Client, 853, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}

//void NotifyHeroMove(std::shared_ptr<ClientNode> Client, BYTE* buf,int len1,int len2);
//
//void RequestHeroMove(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
//{
//	BYTE* p = Body; WORD len1;
//	WORD len2;
//
//	UINT Uin = Read32(p);
//	UINT Time = Read32(p);
//	len1=Read16(p);
//	len2 = BodyLen - 10;
//	/*MoveInfo HeroMoveInfo;
//	 { //HeroMoveInfo
//		BYTE* pHeroMoveInfo = p;
//		len = Read16(pHeroMoveInfo);
//
//		HeroMoveInfo.Uin = Read32(pHeroMoveInfo);
//		HeroMoveInfo.WalkStatue = Read8(pHeroMoveInfo);
//		for (size_t i = 0; i < 3; i++)
//		{
//			HeroMoveInfo.Posion[i] = Read32(pHeroMoveInfo);
//		}
//		for (size_t i = 0; i < 9; i++)
//		{
//			HeroMoveInfo.Ori[i] = Read32(pHeroMoveInfo);
//		}
//		HeroMoveInfo.AttachUin = Read32(pHeroMoveInfo);
//		HeroMoveInfo.WalkExtState = Read32(pHeroMoveInfo);
//
//		p += len;
//	}
//	int NextPoint = Read32(p);
//	short PathLen = Read16(p);
//	//int[] Path[] = Read32(p);
//	int CurCellID = Read32(p);
//	*/
//	
//
//	ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
//	if (!ChatRoom)
//	{
//		return;
//	}
//	for (char i = 0; i < 99; i++)
//	{
//		if (ChatRoom->Player[i] && ChatRoom->Player[i] != Client)
//		{
//			NotifyHeroMove(ChatRoom->Player[i], p,len1,len2);
//		}
//	}
//}
//void NotifyHeroMove(std::shared_ptr<ClientNode> Client, BYTE* buf, int len1,int len2)
//{
//	BYTE buf1[8192]; BYTE* p = buf1; size_t len;
//	Write16(p, len1); //len
//	memcpy(p, buf, len2);
//	len = len2 + 2;
//	/* { //HeroMoveInfo
//		BYTE* pHeroMoveInfo = p;
//		Write16(pHeroMoveInfo, 0); //len
//
//		Write32(pHeroMoveInfo, HeroMoveInfo->Uin);
//		Write8(pHeroMoveInfo, HeroMoveInfo->WalkStatue);
//		for (size_t i = 0; i < 3; i++)
//		{
//			Write32(pHeroMoveInfo, HeroMoveInfo->Posion[i]);
//		}
//		for (size_t i = 0; i < 9; i++)
//		{
//			Write32(pHeroMoveInfo, HeroMoveInfo->Ori[i]);
//		}
//		Write32(pHeroMoveInfo, HeroMoveInfo->AttachUin);
//		Write32(pHeroMoveInfo, HeroMoveInfo->WalkExtState);
//
//		len = pHeroMoveInfo - p;
//		Set16(p, (WORD)len);
//		p += len;
//	}
//	Write32(p, 0); //NextPoint
//	Write16(p, 0); //PathLen
//	//Write32(p, 0); //Path[]
//	Write32(p, 0); //Time
//
//	len = p - buf.data();
//	*/
//	SendToClient(Client, 851, buf1, len, Client->ConnID, FE_PLAYER, 1, Notify);
//}


void NotifyHeroMove(std::shared_ptr<ClientNode> Client, MoveInfo* HeroMoveInfo)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;
	{ // HeroMoveInfo
		BYTE* pHeroMoveInfo = p;
		Write16(pHeroMoveInfo, 0); // len

		Write32(pHeroMoveInfo, HeroMoveInfo->Uin);
		Write8(pHeroMoveInfo, HeroMoveInfo->WalkStatue);
		for (size_t i = 0; i < 3; i++)
		{
			Write32(pHeroMoveInfo, HeroMoveInfo->Posion[i]);
		}
		for (size_t i = 0; i < 9; i++)
		{
			Write32(pHeroMoveInfo, HeroMoveInfo->Ori[i]);
		}
		Write32(pHeroMoveInfo, HeroMoveInfo->AttachUin);
		Write32(pHeroMoveInfo, HeroMoveInfo->WalkExtState);
		const UCHAR permitNum = (std::min)(HeroMoveInfo->Nums, static_cast<UCHAR>(6));
		Write8(pHeroMoveInfo, permitNum); // Nums
		for (size_t i = 0; i < permitNum; i++)
		{
			Write32(pHeroMoveInfo, HeroMoveInfo->PermitSitHorsePetUins[i]);
		}
		Write32(pHeroMoveInfo, HeroMoveInfo->EquipedPetID); // EquipedPetID
		Write32(pHeroMoveInfo, HeroMoveInfo->EquipedPetStatus);				 // EquipedPetStatus
		Write32(pHeroMoveInfo, HeroMoveInfo->PetPKResultStatus); // PetPKResultStatus
		Write16(pHeroMoveInfo, HeroMoveInfo->HeroMajorState);	 // HeroMajorState
		Write16(pHeroMoveInfo, HeroMoveInfo->HorsePetStatus);	 // HorsePetStatus
		len = pHeroMoveInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}
	Write32(p, HeroMoveInfo->NextPoint); // NextPoint
	Write16(p, HeroMoveInfo->PathLen);	 // PathLen
	for (size_t i = 0; i < HeroMoveInfo->PathLen; i++)
	{
		Write32(p, HeroMoveInfo->Path[i]);
	}
	Write32(p, 0); // Time

	len = p - buf.data();
	SendToClient(Client, 851, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void RequestHeroMove(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	BYTE* p = Body;
	WORD len;

	UINT Uin = Read32(p);
	UINT Time = Read32(p);
	MoveInfo* HeroMoveInfo = &(Client->HeroMoveInfo);
	{ // HeroMoveInfo
		BYTE* pHeroMoveInfo = p;
		len = Read16(pHeroMoveInfo);

		HeroMoveInfo->Uin = Read32(pHeroMoveInfo);
		HeroMoveInfo->WalkStatue = Read8(pHeroMoveInfo);
		for (size_t i = 0; i < 3; i++)
		{
			HeroMoveInfo->Posion[i] = Read32(pHeroMoveInfo);
		}
		for (size_t i = 0; i < 9; i++)
		{
			HeroMoveInfo->Ori[i] = Read32(pHeroMoveInfo);
		}
		HeroMoveInfo->AttachUin = Read32(pHeroMoveInfo);
		HeroMoveInfo->WalkExtState = Read32(pHeroMoveInfo);
		HeroMoveInfo->Nums = Read8(pHeroMoveInfo);
		if (HeroMoveInfo->Nums > 6)
			return;
		for (size_t i = 0; i < HeroMoveInfo->Nums; i++)
		{
			HeroMoveInfo->PermitSitHorsePetUins[i] = Read32(pHeroMoveInfo);
		}
		HeroMoveInfo->EquipedPetID = Read32(pHeroMoveInfo);
		HeroMoveInfo->EquipedPetStatus = Read32(pHeroMoveInfo);
		HeroMoveInfo->PetPKResultStatus = Read32(pHeroMoveInfo);
		HeroMoveInfo->HeroMajorState = Read16(pHeroMoveInfo);
		HeroMoveInfo->HorsePetStatus = Read16(pHeroMoveInfo);
		p += len;
	}
	HeroMoveInfo->NextPoint = Read32(p);
	HeroMoveInfo->PathLen = Read16(p);
	for (size_t i = 0; i < HeroMoveInfo->PathLen; i++)
	{
		HeroMoveInfo->Path[i] = Read32(p);
	}
	HeroMoveInfo->CurCellID = Read32(p);
	HeroMoveInfo->Speed = Read16(p);

	ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
	if (!ChatRoom)
	{
		return;
	}
	for (char i = 0; i < 99; i++)
	{
		if (ChatRoom->Player[i] && ChatRoom->Player[i] != Client)
		{
			NotifyHeroMove(ChatRoom->Player[i], HeroMoveInfo);
		}
	}
}


void RequestHeroPos(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	BYTE* p = Body;
	WORD len;
	UINT Uin = Read32(p);
	UINT Time = Read32(p);
	MoveInfo* HeroMoveInfo = &(Client->HeroMoveInfo);
	{ // HeroMoveInfo
		BYTE* pHeroMoveInfo = p;
		len = Read16(pHeroMoveInfo);

		HeroMoveInfo->Uin = Read32(pHeroMoveInfo);
		HeroMoveInfo->WalkStatue = Read8(pHeroMoveInfo);
		for (size_t i = 0; i < 3; i++)
		{
			HeroMoveInfo->Posion[i] = Read32(pHeroMoveInfo);
		}
		for (size_t i = 0; i < 9; i++)
		{
			HeroMoveInfo->Ori[i] = Read32(pHeroMoveInfo);
		}
		HeroMoveInfo->AttachUin = Read32(pHeroMoveInfo);
		HeroMoveInfo->WalkExtState = Read32(pHeroMoveInfo);
		HeroMoveInfo->Nums = Read8(pHeroMoveInfo);
		if (HeroMoveInfo->Nums > 6)
			return;
		for (size_t i = 0; i < HeroMoveInfo->Nums; i++)
		{
			HeroMoveInfo->PermitSitHorsePetUins[i] = Read32(pHeroMoveInfo);
		}
		HeroMoveInfo->EquipedPetID = Read32(pHeroMoveInfo);
		HeroMoveInfo->EquipedPetStatus = Read32(pHeroMoveInfo);
		HeroMoveInfo->PetPKResultStatus = Read32(pHeroMoveInfo);
		HeroMoveInfo->HeroMajorState = Read16(pHeroMoveInfo);
		HeroMoveInfo->HorsePetStatus = Read16(pHeroMoveInfo);
		p += len;
	}
	HeroMoveInfo->CellID = Read32(p);
	HeroMoveInfo->Flag = Read8(p);

	ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
	if (!ChatRoom)
	{
		return;
	}
	for (char i = 0; i < 99; i++)
	{
		if (ChatRoom->Player[i] && ChatRoom->Player[i] != Client)
		{
			NotifyHeroMove(ChatRoom->Player[i], HeroMoveInfo);
			NotifyHeroMove(Client, &(ChatRoom->Player[i]->HeroMoveInfo));//双向同步
		}
	}
}


void ResponseOutDoorEggInfo(std::shared_ptr<ClientNode> Client, UINT Uin, CHATROOMEGGINFO* ChatRoomEggInfo)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;

	Write16(p, 0); //ResultID
	Write32(p, Uin); //Uin
	Write8(p, 0); //ReasonLen
	Write32(p, ChatRoomEggInfo->EggInfos.size()); // EggNumber
	for (size_t i = 0; i < ChatRoomEggInfo->EggInfos.size(); i++)
	{
		BYTE* pEggInfo = p;
		Write16(pEggInfo, 0); //len
		Write32(pEggInfo, ChatRoomEggInfo->EggInfos[i].Pos); //EggPosition
		Write32(pEggInfo, ChatRoomEggInfo->EggInfos[i].TypeID); //EggTypeID
		len = pEggInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}
	len = p - buf.data();
	SendToClient(Client, 169, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Response);
}
void NotifyOutDoorEggInfo(std::shared_ptr<ClientNode> Client, CHATROOMEGGINFO* ChatRoomEggInfo)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;

	Write32(p, ChatRoomEggInfo->EggInfos.size()); //EggNumber
	for (size_t i = 0; i < ChatRoomEggInfo->EggInfos.size(); i++)
	{ //EggInfo
		BYTE* pEggInfo = p;
		Write16(pEggInfo, 0); //len

		Write32(pEggInfo, ChatRoomEggInfo->EggInfos[i].Pos); //EggPosition
		Write32(pEggInfo, ChatRoomEggInfo->EggInfos[i].TypeID); //EggTypeID

		len = pEggInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}
	Write8(p, 0); //Type

	len = p - buf.data();
	SendToClient(Client, 580, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void RequestOutDoorEggInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	BYTE* p = Body; WORD len;

	UINT Uin = Read32(p);
	UINT Time = Read32(p);
	CHATROOMEGGINFO* ChatRoomEggInfo = GetChatRoomEggInfo(Client->ChatRoomID);
	if (!ChatRoomEggInfo)
	{
		return;//没有
	}
	ResponseOutDoorEggInfo(Client, Uin, ChatRoomEggInfo);
	NotifyOutDoorEggInfo(Client, ChatRoomEggInfo);


}


void NotifyOutDoorSmashEgg(std::shared_ptr<ClientNode> Client, UINT Uin, UINT EggPosition, int EggTypeID)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;

	Write32(p, Uin); //Uin
	{ //EggInfo
		BYTE* pEggInfo = p;
		Write16(pEggInfo, 0); //len

		Write32(pEggInfo, EggPosition); //EggPosition
		Write32(pEggInfo, EggTypeID); //EggTypeID

		len = pEggInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	len = p - buf.data();
	SendToClient(Client, 579, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void ResponseOutDoorSmashEgg(std::shared_ptr<ClientNode> Client, UINT Uin, UINT EggPosition, int EggTypeID, int UseItemID, int UseItemNum)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data(); size_t len;

	Write16(p, 0); //ResultID
	Write32(p, Client->Uin); //Uin
	{ //EggInfo
		BYTE* pEggInfo = p;
		Write16(pEggInfo, 0); //len

		Write32(pEggInfo, EggPosition); //EggPosition
		Write32(pEggInfo, EggTypeID); //EggTypeID

		len = pEggInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	std::vector<ItemInfo> Item;
	bool result = GetRandItem(Client, EggTypeID, 1, Item);
	Write32(p, UseItemID); //UseItemID
	Write32(p, UseItemNum); //UseItemNum
	Write32(p, 0); //AddExperience
	Write32(p, 0); //AddMoney
	Write32(p, 0); //AddSuperMoney
	if (!result)
	{
		Write16(p, 0); //ItemNum
	}
	else
	{
		Write16(p, 1); //ItemNum
		for (size_t i = 0; i < 1; i++)
		{ //AddItemInfo
			BYTE* pAddItemInfo = p;
			Write16(pAddItemInfo, 0); //len

			Write32(pAddItemInfo, Item[0].ItemID); //ItemID
			Write32(pAddItemInfo, Item[0].ItemNum); //ItemNum
			Write32(pAddItemInfo, Item[0].AvailPeriod); //AvailPeriod
			Write8(pAddItemInfo, 0); //Status
			Write32(pAddItemInfo, time(nullptr)); //ObtainTime
			Write32(pAddItemInfo, 0); //OtherAttribute

			len = pAddItemInfo - p;
			Set16(p, (WORD)len);
			p += len;
		}
	}
	Write8(p, 0); //ReasonLen
	Write32(p, 0); //AddCoupons
	len = p - buf.data();
	SendToClient(Client, 170, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Response);
	NotifyUseItemResult2(Client, UseItemID, UseItemNum);
	if (!result)
	{
		ResponseStartGame(Client, "宝箱里面没有物品请配置宝箱!");
		return;
	}
	NotifyClientAddItem(Client, 1, Item);//通知客户端添加道具
}
void RequestOutDoorSmashEgg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	BYTE* p = Body; WORD len;

	UINT Uin = Read32(p);
	UINT Time = Read32(p);
	{ //EggInfo
		BYTE* pEggInfo = p;
		len = Read16(pEggInfo);

		UINT EggPosition = Read32(pEggInfo);
		int EggTypeID = Read32(pEggInfo);

		p += len;
		CHATROOMEGGINFO* ChatRoomEggInfo = GetChatRoomEggInfo(Client->ChatRoomID);
		if (!ChatRoomEggInfo)
		{
			return;//没有
		}
		int KeyItemID = 0;
		int UseItemNum = 0;
		for (int i = 0; i < ChatRoomEggInfo->EggInfos.size(); i++)
		{
			if (ChatRoomEggInfo->EggInfos[i].TypeID == EggTypeID)
			{
				KeyItemID = ChatRoomEggInfo->EggInfos[i].KeyID;
				UseItemNum = ChatRoomEggInfo->EggInfos[i].UseNum;
				break;
			}
		}
		ResponseOutDoorSmashEgg(Client, Uin, EggPosition, EggTypeID, KeyItemID, UseItemNum);
		ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
		if (!ChatRoom)
		{
			return;
		}

		for (char i = 0; i < 99; i++)
		{
			if (ChatRoom->Player[i])
			{
				NotifyOutDoorSmashEgg(ChatRoom->Player[i], Uin, EggPosition, EggTypeID);

				NotifyOutDoorEggInfo(ChatRoom->Player[i], ChatRoomEggInfo);//无限敲蛋
			}
		}

	}

}


void NotifyUseItem(std::shared_ptr<ClientNode> Client, int ItemID, UINT DstUin, bool DstType, UINT Uin)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write32(p, Uin);	// SrcUin
	Write32(p, DstUin); // DstUin
	Write32(p, ItemID); // ItemID
	Write8(p, 0);		// AttachDataLen
	Write8(p, DstType); // DstType
	Write8(p, 1);		// HasPKAbilityInfo
	for (size_t i = 0; i < 1; i++)
	{ // PKAbilityInfo
		BYTE* pPKAbilityInfo = p;
		Write16(pPKAbilityInfo, 0); // len

		Write16(pPKAbilityInfo, 1); // SrcPKAbility
		Write8(pPKAbilityInfo, 1);	// IsSrcPKStrong
		Write16(pPKAbilityInfo, 0); // DstPKAbility
		Write8(pPKAbilityInfo, 0);	// IsDstPKStrong

		len = pPKAbilityInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}
	Write32(p, 0); // Para
	Write8(p, 0);  // OpType

	len = p - buf.data();
	SendToClient(Client, 567, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void ResponseUseHammer(std::shared_ptr<ClientNode> Client, int ItemID, UINT DstUin, bool DstType)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, 0);		// ResultID
	Write32(p, ItemID); // ItemID
	Write32(p, DstUin); // DstUin
	Write8(p, 0);		// ReasonLen
	Write8(p, DstType); // DstType
	Write8(p, 1);		// HasPKAbilityInfo
	for (size_t i = 0; i < 1; i++)
	{ // PKAbilityInfo
		BYTE* pPKAbilityInfo = p;
		Write16(pPKAbilityInfo, 0); // len

		Write16(pPKAbilityInfo, 1); // SrcPKAbility
		Write8(pPKAbilityInfo, 1);	// IsSrcPKStrong
		Write16(pPKAbilityInfo, 0); // DstPKAbility
		Write8(pPKAbilityInfo, 0);	// IsDstPKStrong

		len = pPKAbilityInfo - p;
		Set16(p, (WORD)len);
		p += len;
	}

	len = p - buf.data();
	SendToClient(Client, 358, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Response);
}
void RequestUseHammer(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	BYTE* p = Body;
	WORD len;

	UINT Uin = Read32(p);
	UINT Time = Read32(p);
	int ItemID = Read32(p);
	UINT DstUin = Read32(p);
	int SrcWeight = Read32(p);
	int DstWeight = Read32(p);
	bool DstType = Read8(p);
	UINT DistToTgt = Read32(p);
	UCHAR MsgSeq = Read8(p);
	int Position1 = Read32(p);
	int Position2 = Read32(p);
	int Position3 = Read32(p);
	int CellID = Read32(p);
	ResponseUseHammer(Client, ItemID, DstUin, DstType);
	ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
	if (!ChatRoom)
	{
		return;
	}
	for (char i = 0; i < 99; i++)
	{
		if (ChatRoom->Player[i])
		{
			NotifyUseItem(ChatRoom->Player[i], ItemID, DstUin, DstType, Uin);
		}
	}
}

void ResponseChangeHammerStatus(std::shared_ptr<ClientNode> Client, int EquipedHammerID)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, 0); // ResultID
	{			   // EquipedHammer
		BYTE* pEquipedHammer = p;
		Write16(pEquipedHammer, 0); // len

		Write32(pEquipedHammer, EquipedHammerID); // EquipedHammerID

		len = pEquipedHammer - p;
		Set16(p, (WORD)len);
		p += len;
	}
	Write8(p, 0); // ReasonLen

	len = p - buf.data();
	SendToClient(Client, 359, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Response);
}
void NotifyChangeHammerStatus(std::shared_ptr<ClientNode> Client, int EquipedHammerID, UINT SrcUin)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write32(p, SrcUin); // SrcUin
	Write32(p, 0);		// Time
	{					// EquipedHammer
		BYTE* pEquipedHammer = p;
		Write16(pEquipedHammer, 0); // len

		Write32(pEquipedHammer, EquipedHammerID); // EquipedHammerID

		len = pEquipedHammer - p;
		Set16(p, (WORD)len);
		p += len;
	}

	len = p - buf.data();
	SendToClient(Client, 861, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void RequestChangeHammerStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	BYTE* p = Body;
	WORD len;

	UINT Uin = Read32(p);
	UINT Time = Read32(p);
	{ // EquipedHammer
		BYTE* pEquipedHammer = p;
		len = Read16(pEquipedHammer);

		int EquipedHammerID = Read32(pEquipedHammer);

		p += len;
		ResponseChangeHammerStatus(Client, EquipedHammerID);
		ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
		if (!ChatRoom)
		{
			return;
		}
		for (char i = 0; i < 99; i++)
		{
			if (ChatRoom->Player[i] && ChatRoom->Player[i] != Client)
			{
				NotifyChangeHammerStatus(ChatRoom->Player[i], EquipedHammerID, Client->Uin);
			}
		}
	}
}





void ResponseHeroDoAction(std::shared_ptr<ClientNode> Client, short ActionID, int Para1)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, 0);	   // ResultID
	Write8(p, 0);	   // ReasonLen
	Write32(p, Para1); // Para1

	len = p - buf.data();
	SendToClient(Client, 355, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Response);
}

void NotifyHeroDoAction(std::shared_ptr<ClientNode> Client, short ActionID, int Para1, UINT Uin)
{
	std::vector<BYTE> buf(819200);BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write32(p, Uin);	  // Uin
	Write32(p, 0);		  // Time
	Write16(p, ActionID); // ActionID
	Write32(p, Para1);	  // Para1

	len = p - buf.data();
	SendToClient(Client, 855, buf.data(), len, Client->ConnID, FE_PLAYER, 1, Notify);
}
void RequestHeroDoAction(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	BYTE* p = Body;
	WORD len;

	UINT Uin = Read32(p);
	UINT Time = Read32(p);
	short ActionID = Read16(p);
	int Para1 = Read32(p);
	ResponseHeroDoAction(Client, ActionID, Para1);
	ChatRoomNode* ChatRoom = GetChatRoom(Client->ChatRoomID);
	if (!ChatRoom)
	{
		return;
	}
	for (char i = 0; i < 99; i++)
	{
		if (ChatRoom->Player[i])
		{
			NotifyHeroDoAction(ChatRoom->Player[i], ActionID, Para1, Uin);
		}
	}

}
