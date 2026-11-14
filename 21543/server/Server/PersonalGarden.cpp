#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "PersonalGarden.h"

#include "Database.h"
#include "DBConnection.h"
#include "Item.h"
#include "Outdoor.h"
#include "Room.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

extern std::string IP;
extern std::string Name;
extern USHORT TcpPort;
extern USHORT UdpPort;
extern std::shared_ptr<ClientNode> GetClient(long Uin);

void NotifyNPCInfo(std::shared_ptr<ClientNode> Client);
void NotifyGuildRoomMoreInfo(std::shared_ptr<ClientNode> Client);
void NotifyChairInfo(std::shared_ptr<ClientNode> Client);
void NotifyEnterOutdoor(
	std::shared_ptr<ClientNode> Client,
	ChatRoomNode* ChatRoom,
	std::shared_ptr<ClientNode> RoomClient,
	char SeatID);

namespace
{
constexpr BYTE kGardenRoomType = 5;
constexpr USHORT kGardenSceneID = 193;
constexpr USHORT kGardenServerID = 1020;
constexpr INT kFirstGardenChatRoomID = 420;
constexpr short kGardenRoomCapacity = 15;
constexpr int kGardenSpawnX = -7951;
constexpr int kGardenSpawnY = 7397;
constexpr int kGardenSpawnZ = 799;

constexpr UINT kGardenKeyItemID = 63505;
constexpr UINT kGardenCoinItemID = 63560;
constexpr UINT kUnlockCost = 2;
constexpr UINT kBuildingUpgradeCost = 5;
constexpr UINT kParkingTimeout = 14400;

constexpr USHORT kResultSuccess = 0;
constexpr USHORT kResultFailed = 1;
constexpr BYTE kSlotUnavailable = 0;
constexpr BYTE kSlotLocked = 1;
constexpr BYTE kSlotEmpty = 2;
constexpr BYTE kSlotOccupied = 3;
constexpr BYTE kRecordParking = 1;
constexpr BYTE kRecordPet = 2;
constexpr BYTE kRecordActive = 1;
constexpr BYTE kRecordFinished = 2;

constexpr std::array<UINT, 4> kParkingBlacklist = {
	10020u, 19294u, 33275u, 28788u
};
constexpr std::array<UINT, 7> kGardenLevelThreshold = {
	0u, 500u, 1800u, 4500u, 8000u, 14000u, 0u
};
constexpr std::array<BYTE, 7> kDailyOwnParkingCount = {
	0u, 4u, 5u, 6u, 7u, 8u, 10u
};
constexpr std::array<BYTE, 7> kDailyOtherParkingCount = {
	1u, 2u, 2u, 3u, 3u, 4u, 6u
};
constexpr std::array<BYTE, 7> kOutParkingCount = {
	1u, 2u, 2u, 3u, 3u, 4u, 6u
};

struct GardenState
{
	UINT Uin = 0;
	BYTE Level = 0;
	UINT Point = 0;
	UINT WeekPopularity = 0;
	UINT TotalPopularity = 0;
	UINT LastUpdatePopularityTime = 0;
	BYTE PrivateType = 0;
	BYTE BuildingLevel = 1;
	BYTE RubberDuckState = 0;
	UINT CreateTime = 0;
	USHORT UnlockedParkingMask = 0;
	BYTE UnlockedPetMask = 0;
	UINT LastReceiveAwardTime = 0;
};

struct GardenRecord
{
	std::uint64_t RecordID = 0;
	UINT OwnerUin = 0;
	BYTE RecordType = 0;
	BYTE SlotIndex = 0;
	UINT EntityID = 0;
	UINT EntityOwnerUin = 0;
	std::string EntityNickname;
	UINT StartTime = 0;
	UINT EndTime = 0;
	BYTE Status = 0;
	INT RefitCount = 0;
	INT ShapeSuitID = 0;
	BYTE ZhizhenStage = 0;
	UINT KartSkinModel = 0;
	USHORT PetLevel = 0;
	BYTE PetStatus = 0;
	UINT RewardItemID = 0;
	UINT RewardItemNum = 0;
	UINT Popularity = 0;
	BYTE AwardClaimed = 0;
	BYTE FirstReceivedFlag = 1;
};

struct GardenRoom
{
	UINT OwnerUin = 0;
	std::string Name;
	ChatRoomNode Room{};
};

struct PendingGardenHandoff
{
	UINT OwnerUin = 0;
	UINT RoomKey = 0;
	INT ChatRoomID = -1;
};

class Transaction
{
public:
	explicit Transaction(sql::Connection* Connection)
		: connection_(Connection)
	{
		if (connection_)
		{
			connection_->setAutoCommit(false);
			active_ = true;
		}
	}

	~Transaction()
	{
		if (!active_ || !connection_)
			return;
		try { connection_->rollback(); }
		catch (const std::exception&) {}
		try { connection_->setAutoCommit(true); }
		catch (const std::exception&) {}
	}

	bool Commit()
	{
		if (!active_ || !connection_)
			return false;
		connection_->commit();
		connection_->setAutoCommit(true);
		active_ = false;
		return true;
	}

private:
	sql::Connection* connection_ = nullptr;
	bool active_ = false;
};

std::atomic<bool> gGardenSchemaReady{ false };
std::mutex gGardenDataMutex;
std::recursive_mutex gGardenRoomMutex;
std::map<UINT, std::shared_ptr<GardenRoom>> gGardenRoomsByOwner;
std::map<INT, std::shared_ptr<GardenRoom>> gGardenRoomsByID;
std::unordered_map<UINT, PendingGardenHandoff> gPendingHandoffs;
INT gNextGardenChatRoomID = kFirstGardenChatRoomID;

UINT ClampGardenLevel(UINT Level)
{
	return (std::min)(Level, 6u);
}

void Write64(BYTE*& p, std::uint64_t Value)
{
	for (int shift = 56; shift >= 0; shift -= 8)
		Write8(p, static_cast<BYTE>((Value >> shift) & 0xFFu));
}

void WriteFixedString(BYTE*& p, const std::string& Value, size_t Width)
{
	const size_t copyLength = (std::min)(Width, Value.size());
	if (copyLength > 0)
		std::memcpy(p, Value.data(), copyLength);
	if (copyLength < Width)
		std::memset(p + copyLength, 0, Width - copyLength);
	p += Width;
}

void FinishNested(BYTE* Start, BYTE* End)
{
	Set16(Start, static_cast<WORD>(End - Start));
}

bool OpenWirePayload(BYTE* Body, size_t BodyLen, size_t ExpectedWireLength, BYTE*& p)
{
	p = nullptr;
	if (!Body ||
		BodyLen != ExpectedWireLength ||
		BodyLen < sizeof(USHORT) ||
		BodyLen > static_cast<size_t>((std::numeric_limits<WORD>::max)()))
	{
		return false;
	}
	// Body points after the two-byte wire length, while BodyLen still includes it.
	p = Body;
	return true;
}

std::string ClientNickName(const std::shared_ptr<ClientNode>& Client)
{
	if (!Client)
		return {};
	const size_t length = strnlen_s(Client->NickName, sizeof(Client->NickName));
	return std::string(Client->NickName, length);
}

std::string ResolveNickName(UINT Uin, const std::shared_ptr<ClientNode>& Hint = nullptr)
{
	if (Hint && Hint->Uin == Uin)
		return ClientNickName(Hint);
	const std::shared_ptr<ClientNode> online = GetClient(Uin);
	if (online)
		return ClientNickName(online);
	std::string nickname;
	PlayerDB_SelectNickName(Uin, nickname);
	return nickname;
}

std::string GardenDisplayName(const std::string& NickName)
{
	static const char suffix[] = "\xB5\xC4\xBA\xF3\xBB\xA8\xD4\xB0";
	std::string result = NickName;
	result.append(suffix, sizeof(suffix) - 1);
	if (result.size() > 25)
		result.resize(25);
	return result;
}

void SendPersonalGardenMessage(
	const std::shared_ptr<ClientNode>& Client,
	USHORT MsgID,
	const BYTE* Data,
	size_t Length,
	MsgType Type)
{
	if (!Client)
		return;
	SendToClient(
		Client,
		static_cast<short>(MsgID),
		Data,
		static_cast<DWORD>(Length),
		Client->ConnID,
		FE_PLAYER,
		1,
		static_cast<short>(Type));
}

bool LoadGardenState(
	sql::Connection* Connection,
	UINT Uin,
	bool ForUpdate,
	GardenState& State)
{
	State = {};
	State.Uin = Uin;
	if (!Connection || Uin == 0)
		return false;

	std::string playerSql =
		"SELECT GardenLevel,GardenPoint FROM Player WHERE Uin=?";
	if (ForUpdate)
		playerSql += " FOR UPDATE";
	std::unique_ptr<sql::PreparedStatement> playerSelect(
		Connection->prepareStatement(playerSql));
	playerSelect->setUInt(1, Uin);
	std::unique_ptr<sql::ResultSet> playerResult(playerSelect->executeQuery());
	if (!playerResult->next())
		return false;
	State.Level = static_cast<BYTE>((std::min)(playerResult->getUInt("GardenLevel"), 255u));
	State.Point = playerResult->getUInt("GardenPoint");
	if (State.Level == 0)
		return true;

	const UINT now = static_cast<UINT>(std::time(nullptr));
	const BYTE initialBuilding = static_cast<BYTE>((std::max)(1u, ClampGardenLevel(State.Level)));
	std::unique_ptr<sql::PreparedStatement> insert(
		Connection->prepareStatement(
			"INSERT IGNORE INTO PersonalGardenState ("
			"Uin,BuildingLevel,CreateTime,UnlockedParkingMask,UnlockedPetMask,UpdatedAt) "
			"VALUES (?,?,?,1,0,?);"));
	insert->setUInt(1, Uin);
	insert->setUInt(2, initialBuilding);
	insert->setUInt(3, now);
	insert->setUInt(4, now);
	insert->executeUpdate();

	std::string stateSql =
		"SELECT WeekPopularity,TotalPopularity,LastUpdatePopularityTime,"
		"PrivateType,BuildingLevel,RubberDuckState,CreateTime,"
		"UnlockedParkingMask,UnlockedPetMask,LastReceiveAwardTime "
		"FROM PersonalGardenState WHERE Uin=?";
	if (ForUpdate)
		stateSql += " FOR UPDATE";
	std::unique_ptr<sql::PreparedStatement> stateSelect(
		Connection->prepareStatement(stateSql));
	stateSelect->setUInt(1, Uin);
	std::unique_ptr<sql::ResultSet> stateResult(stateSelect->executeQuery());
	if (!stateResult->next())
		return false;
	State.WeekPopularity = stateResult->getUInt("WeekPopularity");
	State.TotalPopularity = stateResult->getUInt("TotalPopularity");
	State.LastUpdatePopularityTime = stateResult->getUInt("LastUpdatePopularityTime");
	State.PrivateType = static_cast<BYTE>(stateResult->getUInt("PrivateType"));
	State.BuildingLevel = static_cast<BYTE>(stateResult->getUInt("BuildingLevel"));
	State.RubberDuckState = static_cast<BYTE>(stateResult->getUInt("RubberDuckState"));
	State.CreateTime = stateResult->getUInt("CreateTime");
	State.UnlockedParkingMask = static_cast<USHORT>(stateResult->getUInt("UnlockedParkingMask"));
	State.UnlockedPetMask = static_cast<BYTE>(stateResult->getUInt("UnlockedPetMask"));
	State.LastReceiveAwardTime = stateResult->getUInt("LastReceiveAwardTime");

	// The current official low-level snapshot has parking slot 0 open by default.
	if ((State.UnlockedParkingMask & 1u) == 0)
	{
		std::unique_ptr<sql::PreparedStatement> normalize(
			Connection->prepareStatement(
				"UPDATE PersonalGardenState SET UnlockedParkingMask="
				"(UnlockedParkingMask | 1),UpdatedAt=? WHERE Uin=?;"));
		normalize->setUInt(1, now);
		normalize->setUInt(2, Uin);
		normalize->executeUpdate();
		State.UnlockedParkingMask |= 1u;
	}
	return true;
}

bool LoadGardenState(UINT Uin, bool ForUpdate, GardenState& State)
{
	if (!gGardenSchemaReady.load(std::memory_order_acquire))
		return false;
	try
	{
		DBConnection database(player);
		sql::Connection* connection = database.getConnection();
		return connection && LoadGardenState(connection, Uin, ForUpdate, State);
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << " LoadGardenState Error" << std::endl;
		return false;
	}
}

bool SaveGardenState(sql::Connection* Connection, const GardenState& State)
{
	if (!Connection || State.Uin == 0)
		return false;
	std::unique_ptr<sql::PreparedStatement> update(
		Connection->prepareStatement(
			"UPDATE PersonalGardenState SET WeekPopularity=?,TotalPopularity=?,"
			"LastUpdatePopularityTime=?,PrivateType=?,BuildingLevel=?,RubberDuckState=?,"
			"CreateTime=?,UnlockedParkingMask=?,UnlockedPetMask=?,LastReceiveAwardTime=?,"
			"UpdatedAt=? WHERE Uin=?;"));
	update->setUInt(1, State.WeekPopularity);
	update->setUInt(2, State.TotalPopularity);
	update->setUInt(3, State.LastUpdatePopularityTime);
	update->setUInt(4, State.PrivateType);
	update->setUInt(5, State.BuildingLevel);
	update->setUInt(6, State.RubberDuckState);
	update->setUInt(7, State.CreateTime);
	update->setUInt(8, State.UnlockedParkingMask);
	update->setUInt(9, State.UnlockedPetMask);
	update->setUInt(10, State.LastReceiveAwardTime);
	update->setUInt(11, static_cast<UINT>(std::time(nullptr)));
	update->setUInt(12, State.Uin);
	return update->executeUpdate() == 1;
}

bool ConsumeItem(
	sql::Connection* Connection,
	UINT Uin,
	UINT ItemID,
	UINT Count,
	INT& Remaining)
{
	Remaining = 0;
	if (!Connection || Uin == 0 || ItemID == 0 || Count == 0)
		return false;
	std::unique_ptr<sql::PreparedStatement> select(
		Connection->prepareStatement(
			"SELECT ItemNum FROM Item WHERE Uin=? AND ItemID=? FOR UPDATE;"));
	select->setUInt(1, Uin);
	select->setUInt(2, ItemID);
	std::unique_ptr<sql::ResultSet> result(select->executeQuery());
	if (!result->next())
		return false;
	const INT current = result->getInt("ItemNum");
	if (current < static_cast<INT>(Count))
	{
		Remaining = current;
		return false;
	}
	Remaining = current - static_cast<INT>(Count);
	if (Remaining == 0)
	{
		std::unique_ptr<sql::PreparedStatement> remove(
			Connection->prepareStatement("DELETE FROM Item WHERE Uin=? AND ItemID=?;"));
		remove->setUInt(1, Uin);
		remove->setUInt(2, ItemID);
		return remove->executeUpdate() == 1;
	}
	std::unique_ptr<sql::PreparedStatement> update(
		Connection->prepareStatement(
			"UPDATE Item SET ItemNum=? WHERE Uin=? AND ItemID=?;"));
	update->setInt(1, Remaining);
	update->setUInt(2, Uin);
	update->setUInt(3, ItemID);
	return update->executeUpdate() == 1;
}

USHORT ParkingAvailableMask(const GardenState& State)
{
	// Level 2 (0,1,2,8) and level 6 (all ten) are anchored by current
	// official full-state captures; intermediate levels follow the same
	// current 20145 progression without creating extra server-side gates.
	const UINT level = ClampGardenLevel(State.BuildingLevel);
	switch (level)
	{
	case 0: return 0;
	case 1: return 0x001u;
	case 2: return 0x107u;
	case 3: return 0x30Fu;
	case 4: return 0x31Fu;
	case 5: return 0x33Fu;
	default: return 0x3FFu;
	}
}

BYTE PetAvailableMask(const GardenState& State)
{
	const UINT level = ClampGardenLevel(State.BuildingLevel);
	switch (level)
	{
	case 0: return 0;
	case 1:
	case 2: return 0x01u;
	case 3: return 0x03u;
	case 4:
	case 5: return 0x0Fu;
	default: return 0x3Fu;
	}
}

BYTE EmptyParkingType(const GardenState& State, BYTE Index)
{
	if (Index >= 10 || (ParkingAvailableMask(State) & (1u << Index)) == 0)
		return kSlotUnavailable;
	return (State.UnlockedParkingMask & (1u << Index)) != 0
		? kSlotEmpty
		: kSlotLocked;
}

BYTE EmptyPetType(const GardenState& State, BYTE Index)
{
	if (Index >= 6 || (PetAvailableMask(State) & (1u << Index)) == 0)
		return kSlotUnavailable;
	return (State.UnlockedPetMask & (1u << Index)) != 0
		? kSlotEmpty
		: kSlotLocked;
}

bool IsFriend(UINT FirstUin, UINT SecondUin)
{
	if (FirstUin == 0 || SecondUin == 0 || FirstUin == SecondUin)
		return FirstUin == SecondUin;
	return PlayerDB_IsHasFriend(FirstUin, SecondUin) ||
		PlayerDB_IsHasFriend(SecondUin, FirstUin);
}

bool CanEnterGarden(UINT VisitorUin, const GardenState& State)
{
	if (State.Level == 0)
		return false;
	if (VisitorUin == State.Uin)
		return true;
	if (State.PrivateType == 0)
		return true;
	if (State.PrivateType == 1)
		return IsFriend(VisitorUin, State.Uin);
	return false;
}

void FillRecord(sql::ResultSet* Result, GardenRecord& Record)
{
	Record.RecordID = Result->getUInt64("RecordID");
	Record.OwnerUin = Result->getUInt("OwnerUin");
	Record.RecordType = static_cast<BYTE>(Result->getUInt("RecordType"));
	Record.SlotIndex = static_cast<BYTE>(Result->getUInt("SlotIndex"));
	Record.EntityID = Result->getUInt("EntityID");
	Record.EntityOwnerUin = Result->getUInt("EntityOwnerUin");
	Record.EntityNickname = Result->getString("EntityNickname").c_str();
	Record.StartTime = Result->getUInt("StartTime");
	Record.EndTime = Result->getUInt("EndTime");
	Record.Status = static_cast<BYTE>(Result->getUInt("Status"));
	Record.RefitCount = Result->getInt("RefitCount");
	Record.ShapeSuitID = Result->getInt("ShapeSuitID");
	Record.ZhizhenStage = static_cast<BYTE>(Result->getUInt("ZhizhenStage"));
	Record.KartSkinModel = Result->getUInt("KartSkinModel");
	Record.PetLevel = static_cast<USHORT>(Result->getUInt("PetLevel"));
	Record.PetStatus = static_cast<BYTE>(Result->getUInt("PetStatus"));
	Record.RewardItemID = Result->getUInt("RewardItemID");
	Record.RewardItemNum = Result->getUInt("RewardItemNum");
	Record.Popularity = Result->getUInt("Popularity");
	Record.AwardClaimed = static_cast<BYTE>(Result->getUInt("AwardClaimed"));
	Record.FirstReceivedFlag = static_cast<BYTE>(Result->getUInt("FirstReceivedFlag"));
}

const char* RecordColumns()
{
	return "RecordID,OwnerUin,RecordType,SlotIndex,EntityID,EntityOwnerUin,"
		"EntityNickname,StartTime,EndTime,Status,RefitCount,ShapeSuitID,"
		"ZhizhenStage,KartSkinModel,PetLevel,PetStatus,RewardItemID,RewardItemNum,"
		"Popularity,AwardClaimed,FirstReceivedFlag";
}

bool SelectActiveRecord(
	sql::Connection* Connection,
	UINT OwnerUin,
	BYTE RecordType,
	BYTE SlotIndex,
	bool ForUpdate,
	GardenRecord& Record)
{
	std::string sql = "SELECT ";
	sql += RecordColumns();
	sql += " FROM PersonalGardenRecord WHERE OwnerUin=? AND RecordType=? "
		"AND SlotIndex=? AND Status=1 ORDER BY RecordID DESC LIMIT 1";
	if (ForUpdate)
		sql += " FOR UPDATE";
	std::unique_ptr<sql::PreparedStatement> select(Connection->prepareStatement(sql));
	select->setUInt(1, OwnerUin);
	select->setUInt(2, RecordType);
	select->setUInt(3, SlotIndex);
	std::unique_ptr<sql::ResultSet> result(select->executeQuery());
	if (!result->next())
		return false;
	FillRecord(result.get(), Record);
	return true;
}

std::map<BYTE, GardenRecord> LoadActiveRecords(UINT OwnerUin, BYTE RecordType)
{
	std::map<BYTE, GardenRecord> records;
	try
	{
		DBConnection database(player);
		sql::Connection* connection = database.getConnection();
		if (!connection)
			return records;
		std::string sql = "SELECT ";
		sql += RecordColumns();
		sql += " FROM PersonalGardenRecord WHERE OwnerUin=? AND RecordType=? "
			"AND Status=1 ORDER BY RecordID;";
		std::unique_ptr<sql::PreparedStatement> select(connection->prepareStatement(sql));
		select->setUInt(1, OwnerUin);
		select->setUInt(2, RecordType);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		while (result->next())
		{
			GardenRecord record;
			FillRecord(result.get(), record);
			records[record.SlotIndex] = std::move(record);
		}
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << " LoadActiveGardenRecords Error" << std::endl;
	}
	return records;
}

bool InsertRecord(sql::Connection* Connection, const GardenRecord& Record)
{
	std::unique_ptr<sql::PreparedStatement> insert(
		Connection->prepareStatement(
			"INSERT INTO PersonalGardenRecord (OwnerUin,RecordType,SlotIndex,EntityID,"
			"EntityOwnerUin,EntityNickname,StartTime,EndTime,Status,RefitCount,"
			"ShapeSuitID,ZhizhenStage,KartSkinModel,PetLevel,PetStatus,RewardItemID,"
			"RewardItemNum,Popularity,AwardClaimed,FirstReceivedFlag) "
			"VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);"));
	insert->setUInt(1, Record.OwnerUin);
	insert->setUInt(2, Record.RecordType);
	insert->setUInt(3, Record.SlotIndex);
	insert->setUInt(4, Record.EntityID);
	insert->setUInt(5, Record.EntityOwnerUin);
	insert->setString(6, Record.EntityNickname);
	insert->setUInt(7, Record.StartTime);
	insert->setUInt(8, Record.EndTime);
	insert->setUInt(9, Record.Status);
	insert->setInt(10, Record.RefitCount);
	insert->setInt(11, Record.ShapeSuitID);
	insert->setUInt(12, Record.ZhizhenStage);
	insert->setUInt(13, Record.KartSkinModel);
	insert->setUInt(14, Record.PetLevel);
	insert->setUInt(15, Record.PetStatus);
	insert->setUInt(16, Record.RewardItemID);
	insert->setUInt(17, Record.RewardItemNum);
	insert->setUInt(18, Record.Popularity);
	insert->setUInt(19, Record.AwardClaimed);
	insert->setUInt(20, Record.FirstReceivedFlag);
	return insert->executeUpdate() == 1;
}

bool FinishRecord(sql::Connection* Connection, const GardenRecord& Record, UINT EndTime)
{
	std::unique_ptr<sql::PreparedStatement> update(
		Connection->prepareStatement(
			"UPDATE PersonalGardenRecord SET EndTime=?,Status=? WHERE RecordID=? AND Status=1;"));
	update->setUInt(1, EndTime);
	update->setUInt(2, kRecordFinished);
	update->setUInt64(3, Record.RecordID);
	return update->executeUpdate() == 1;
}

UINT LocalMidnight()
{
	std::time_t now = std::time(nullptr);
	std::tm local{};
	localtime_s(&local, &now);
	local.tm_hour = 0;
	local.tm_min = 0;
	local.tm_sec = 0;
	return static_cast<UINT>(std::mktime(&local));
}

UINT CountActiveOutgoing(sql::Connection* Connection, UINT Uin)
{
	std::unique_ptr<sql::PreparedStatement> select(
		Connection->prepareStatement(
			"SELECT COUNT(*) AS Cnt FROM PersonalGardenRecord WHERE RecordType=1 "
			"AND EntityOwnerUin=? AND Status=1;"));
	select->setUInt(1, Uin);
	std::unique_ptr<sql::ResultSet> result(select->executeQuery());
	return result->next() ? result->getUInt("Cnt") : 0;
}

UINT CountDailyParking(
	sql::Connection* Connection,
	UINT Uin,
	bool OwnGarden)
{
	std::string sql =
		"SELECT COUNT(*) AS Cnt FROM PersonalGardenRecord WHERE RecordType=1 "
		"AND EntityOwnerUin=? AND StartTime>=? AND OwnerUin";
	sql += OwnGarden ? "=?" : "<>?";
	std::unique_ptr<sql::PreparedStatement> select(Connection->prepareStatement(sql));
	select->setUInt(1, Uin);
	select->setUInt(2, LocalMidnight());
	select->setUInt(3, Uin);
	std::unique_ptr<sql::ResultSet> result(select->executeQuery());
	return result->next() ? result->getUInt("Cnt") : 0;
}

bool HasActiveEntity(
	sql::Connection* Connection,
	BYTE RecordType,
	UINT EntityOwnerUin,
	UINT EntityID)
{
	std::unique_ptr<sql::PreparedStatement> select(
		Connection->prepareStatement(
			"SELECT RecordID FROM PersonalGardenRecord WHERE RecordType=? "
			"AND EntityOwnerUin=? AND EntityID=? AND Status=1 LIMIT 1 FOR UPDATE;"));
	select->setUInt(1, RecordType);
	select->setUInt(2, EntityOwnerUin);
	select->setUInt(3, EntityID);
	std::unique_ptr<sql::ResultSet> result(select->executeQuery());
	return result->next();
}

bool LoadOwnedKart(
	sql::Connection* Connection,
	const std::shared_ptr<ClientNode>& Client,
	UINT KartID,
	GardenRecord& Record)
{
	if (!Connection || !Client || KartID == 0 || GetItemType(KartID) != EAIT_CAR ||
		std::find(kParkingBlacklist.begin(), kParkingBlacklist.end(), KartID) !=
		kParkingBlacklist.end())
	{
		return false;
	}
	std::unique_ptr<sql::PreparedStatement> item(
		Connection->prepareStatement(
			"SELECT ItemNum FROM Item WHERE Uin=? AND ItemID=? FOR UPDATE;"));
	item->setUInt(1, Client->Uin);
	item->setUInt(2, KartID);
	std::unique_ptr<sql::ResultSet> itemResult(item->executeQuery());
	if (!itemResult->next() || itemResult->getInt("ItemNum") <= 0)
		return false;

	Record.EntityOwnerUin = Client->Uin;
	Record.EntityID = KartID;
	Record.EntityNickname = ClientNickName(Client);
	std::unique_ptr<sql::PreparedStatement> refit(
		Connection->prepareStatement(
			"SELECT RefitCout,ShapeSuitID,LegendSuitLevelChoice,SkinSuitID "
			"FROM KartRefit WHERE Uin=? AND KartID=? LIMIT 1;"));
	refit->setUInt(1, Client->Uin);
	refit->setUInt(2, KartID);
	std::unique_ptr<sql::ResultSet> refitResult(refit->executeQuery());
	if (refitResult->next())
	{
		Record.RefitCount = refitResult->getInt("RefitCout");
		Record.ShapeSuitID = refitResult->getInt("ShapeSuitID");
		Record.ZhizhenStage = static_cast<BYTE>((std::min)(
			refitResult->getUInt("LegendSuitLevelChoice"), 255u));
		Record.KartSkinModel = refitResult->getUInt("SkinSuitID");
	}
	return true;
}

void WriteGardenBaseInfo(BYTE*& p, const GardenState& State)
{
	BYTE* base = p;
	Write16(p, 0);
	Write8(p, State.Level);
	Write32(p, State.Point);
	Write8(p, static_cast<BYTE>((std::min)(19u + ClampGardenLevel(State.Level), 255u)));
	Write32(p, 0xFFFFFFFFu);
	BYTE* simple = p;
	Write16(p, 0);
	Write32(p, State.WeekPopularity);
	Write32(p, State.TotalPopularity);
	Write32(p, State.LastUpdatePopularityTime);
	Write8(p, State.PrivateType);
	FinishNested(simple, p);
	FinishNested(base, p);
}

void WriteParkingInfo(BYTE*& p, const GardenRecord& Record, BYTE Type)
{
	BYTE* nested = p;
	Write16(p, 0);
	Write8(p, Record.SlotIndex);
	Write8(p, Type);
	Write32(p, Record.EntityID);
	Write32(p, Record.EntityOwnerUin);
	WriteFixedString(p, Record.EntityNickname, 17);
	Write32(p, Record.StartTime);
	Write32(p, static_cast<UINT>(Record.RefitCount));
	Write32(p, static_cast<UINT>(Record.ShapeSuitID));
	Write8(p, 0);
	Write8(p, 0);
	Write8(p, Record.ZhizhenStage);
	Write32(p, Record.KartSkinModel);
	FinishNested(nested, p);
}

void WriteEmptyParkingInfo(BYTE*& p, const GardenState& State, BYTE Index)
{
	GardenRecord empty;
	empty.SlotIndex = Index;
	WriteParkingInfo(p, empty, EmptyParkingType(State, Index));
}

void WritePetInfo(BYTE*& p, const GardenRecord& Record, BYTE Type)
{
	BYTE* nested = p;
	Write16(p, 0);
	Write8(p, Record.SlotIndex);
	Write8(p, Type);
	Write32(p, Record.EntityID);
	Write32(p, Record.StartTime);
	Write16(p, Record.PetLevel);
	Write8(p, Record.PetStatus);
	FinishNested(nested, p);
}

void WriteEmptyPetInfo(BYTE*& p, const GardenState& State, BYTE Index)
{
	GardenRecord empty;
	empty.SlotIndex = Index;
	WritePetInfo(p, empty, EmptyPetType(State, Index));
}

void SendGardenBaseUpdate(
	const std::shared_ptr<ClientNode>& Client,
	const GardenState& State)
{
	BYTE buffer[64]{};
	BYTE* p = buffer;
	Write32(p, State.Uin);
	WriteGardenBaseInfo(p, State);
	SendPersonalGardenMessage(Client, 20301, buffer, p - buffer, Notify);
}

std::shared_ptr<GardenRoom> GetOrCreateGardenRoom(
	UINT OwnerUin,
	const std::shared_ptr<ClientNode>& Owner = nullptr)
{
	std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
	auto found = gGardenRoomsByOwner.find(OwnerUin);
	if (found != gGardenRoomsByOwner.end())
		return found->second;
	if (gNextGardenChatRoomID <= 0 || gNextGardenChatRoomID > 0xFFFF)
		return nullptr;

	auto holder = std::make_shared<GardenRoom>();
	holder->OwnerUin = OwnerUin;
	holder->Name = GardenDisplayName(ResolveNickName(OwnerUin, Owner));
	holder->Room.ID = static_cast<UINT>(gNextGardenChatRoomID++);
	holder->Room.Name = holder->Name.c_str();
	holder->Room.CurrPlayerNum = 0;
	holder->Room.MaxPlayerNum = kGardenRoomCapacity;
	holder->Room.RoomType = kGardenRoomType;
	holder->Room.SceneID = kGardenSceneID;
	holder->Room.x = kGardenSpawnX;
	holder->Room.y = kGardenSpawnY;
	holder->Room.z = kGardenSpawnZ;
	gGardenRoomsByOwner.emplace(OwnerUin, holder);
	gGardenRoomsByID.emplace(static_cast<INT>(holder->Room.ID), holder);
	return holder;
}

std::shared_ptr<GardenRoom> FindGardenRoomByID(INT ChatRoomID)
{
	std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
	auto found = gGardenRoomsByID.find(ChatRoomID);
	return found == gGardenRoomsByID.end() ? nullptr : found->second;
}

BYTE GardenRoomCurrentPlayers(UINT OwnerUin)
{
	std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
	auto found = gGardenRoomsByOwner.find(OwnerUin);
	if (found == gGardenRoomsByOwner.end())
		return 0;
	return static_cast<BYTE>((std::min)(static_cast<int>(found->second->Room.CurrPlayerNum), 255));
}

std::vector<std::shared_ptr<ClientNode>> SnapshotGardenRoomClients(UINT OwnerUin)
{
	std::vector<std::shared_ptr<ClientNode>> clients;
	std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
	auto found = gGardenRoomsByOwner.find(OwnerUin);
	if (found == gGardenRoomsByOwner.end())
		return clients;
	for (short index = 0; index < found->second->Room.MaxPlayerNum; ++index)
	{
		if (found->second->Room.Player[index])
			clients.push_back(found->second->Room.Player[index]);
	}
	return clients;
}

void SendParkingChanged(UINT OwnerUin, const GardenRecord& Record, BYTE Type)
{
	BYTE buffer[128]{};
	BYTE* p = buffer;
	Write32(p, OwnerUin);
	WriteParkingInfo(p, Record, Type);
	for (const std::shared_ptr<ClientNode>& client : SnapshotGardenRoomClients(OwnerUin))
		SendPersonalGardenMessage(client, 20148, buffer, p - buffer, Notify);
}

void SendPetChanged(UINT OwnerUin, const GardenRecord& Record, BYTE Type)
{
	BYTE buffer[64]{};
	BYTE* p = buffer;
	Write32(p, OwnerUin);
	WritePetInfo(p, Record, Type);
	for (const std::shared_ptr<ClientNode>& client : SnapshotGardenRoomClients(OwnerUin))
		SendPersonalGardenMessage(client, 20147, buffer, p - buffer, Notify);
}

void SendRubberDuckState(UINT OwnerUin, BYTE State)
{
	BYTE buffer[8]{};
	BYTE* p = buffer;
	Write32(p, OwnerUin);
	Write8(p, State);
	for (const std::shared_ptr<ClientNode>& client : SnapshotGardenRoomClients(OwnerUin))
		SendPersonalGardenMessage(client, 20155, buffer, p - buffer, Notify);
}

void WriteAddressInfo(BYTE*& p, bool Success)
{
	BYTE* address = p;
	Write16(p, 0);
	Write16(p, Success ? kGardenServerID : 0);
	Write32(p, Success ? inet_addr(IP.c_str()) : 0);
	Write8(p, Success ? 3 : 0);
	if (Success)
	{
		Write16(p, TcpPort);
		Write16(p, static_cast<USHORT>(TcpPort + 1));
		Write16(p, static_cast<USHORT>(TcpPort + 2));
	}
	Write8(p, Success ? 1 : 0);
	if (Success)
		Write16(p, UdpPort);
	Write8(p, 0);
	Write8(p, 0);
	Write64(p, 0);
	FinishNested(address, p);
}

void SendEnterGardenResponse(
	const std::shared_ptr<ClientNode>& Client,
	USHORT Result,
	UINT RoomKey,
	INT ChatRoomID)
{
	BYTE buffer[128]{};
	BYTE* p = buffer;
	const bool success = Result == kResultSuccess;
	Write16(p, Result);
	Write32(p, Client ? Client->Uin : 0);
	Write32(p, RoomKey);
	Write16(p, success ? static_cast<USHORT>(ChatRoomID) : 0);
	WriteAddressInfo(p, success);
	SendPersonalGardenMessage(Client, 20137, buffer, p - buffer, Response);
}

void WriteInitialHeroMove(BYTE*& p, int X, int Y, int Z)
{
	BYTE* hero = p;
	Write16(p, 0);
	Write32(p, 0);
	Write8(p, 0);
	Write32(p, static_cast<UINT>(X));
	Write32(p, static_cast<UINT>(Y));
	Write32(p, static_cast<UINT>(Z));
	Write32(p, 1000);
	Write32(p, 0);
	Write32(p, 0);
	Write32(p, 0);
	Write32(p, 1000);
	Write32(p, 0);
	Write32(p, 0);
	Write32(p, 0);
	Write32(p, 1000);
	Write32(p, 0);
	Write32(p, 0);
	Write8(p, 0);
	Write32(p, 0);
	Write32(p, 0);
	Write32(p, 0);
	Write16(p, 0);
	Write16(p, 0);
	FinishNested(hero, p);
}

BOOL SendFullGardenInfo(
	const std::shared_ptr<ClientNode>& Client,
	const std::shared_ptr<GardenRoom>& Room,
	const GardenState& State)
{
	if (!Client || !Room)
		return FALSE;
	const std::map<BYTE, GardenRecord> parking =
		LoadActiveRecords(Room->OwnerUin, kRecordParking);
	const std::map<BYTE, GardenRecord> pets =
		LoadActiveRecords(Room->OwnerUin, kRecordPet);
	const std::string ownerNickname = ResolveNickName(Room->OwnerUin, Client);
	const std::string gardenName = GardenDisplayName(ownerNickname);

	std::vector<BYTE> buffer(4096);
	BYTE* p = buffer.data();
	Write16(p, static_cast<USHORT>(Room->Room.ID));
	Write16(p, Room->Room.SceneID);
	WriteInitialHeroMove(p, Room->Room.x, Room->Room.y, Room->Room.z);
	Write16(p, kResultSuccess);
	const size_t reasonLength = (std::min)(Name.size(), static_cast<size_t>(255));
	Write8(p, static_cast<BYTE>(reasonLength));
	if (reasonLength > 0)
	{
		std::memcpy(p, Name.data(), reasonLength);
		p += reasonLength;
	}
	Write16(p, 0);
	Write16(p, 0);
	Write16(p, 0);
	Write16(p, 0);
	Write16(p, 0);
	Write16(p, kGardenRoomCapacity);

	BYTE* garden = p;
	Write16(p, 0);
	Write32(p, Room->OwnerUin);
	WriteFixedString(p, ownerNickname, 17);
	WriteFixedString(p, gardenName, 25);
	WriteGardenBaseInfo(p, State);
	Write8(p, State.BuildingLevel);
	Write8(p, State.PrivateType);
	Write32(p, State.CreateTime);

	BYTE* kartExhibition = p;
	Write16(p, 0);
	Write16(p, 0);
	Write16(p, 0);
	Write16(p, 0);
	Write8(p, 0);
	BYTE* parkingList = p;
	Write16(p, 0);
	Write8(p, 10);
	for (BYTE index = 0; index < 10; ++index)
	{
		auto found = parking.find(index);
		if (found == parking.end())
			WriteEmptyParkingInfo(p, State, index);
		else
			WriteParkingInfo(p, found->second, kSlotOccupied);
	}
	FinishNested(parkingList, p);
	Write32(p, State.LastReceiveAwardTime);
	FinishNested(kartExhibition, p);

	BYTE* petExhibition = p;
	Write16(p, 0);
	Write8(p, 6);
	for (BYTE index = 0; index < 6; ++index)
	{
		auto found = pets.find(index);
		if (found == pets.end())
			WriteEmptyPetInfo(p, State, index);
		else
			WritePetInfo(p, found->second, kSlotOccupied);
	}
	FinishNested(petExhibition, p);
	FinishNested(garden, p);

	Write32(p, static_cast<UINT>(std::time(nullptr)));
	Write8(p, 0);
	Write16(p, 0);
	Write8(p, 0);
	Write32(p, 0xFFFFFFFFu);
	Write32(p, 0);
	Write8(p, 0);
	Write8(p, 0);
	Write8(p, 0);
	Write32(p, 0);
	Write32(p, 0);
	Write32(p, 0);
	for (size_t index = 0; index < 3; ++index)
	{
		Write8(p, 0);
		Write8(p, 3);
		Write8(p, 0);
	}
	Write32(p, 0);
	Write8(p, 0);
	Write8(p, kGardenRoomType);
	Write8(p, 0);
	Write16(p, 0);
	Write16(p, 0);
	Write8(p, 0);
	Write16(p, 0);

	return SendToClient(
		Client,
		352,
		buffer.data(),
		static_cast<DWORD>(p - buffer.data()),
		static_cast<short>(Room->Room.ID),
		FE_OUTDOORSVRD,
		1,
		Response);
}

bool RemoveClientFromGardenRoom(
	const std::shared_ptr<ClientNode>& Client,
	const std::shared_ptr<GardenRoom>& Room,
	bool NotifyClient)
{
	if (!Client || !Room)
		return false;
	std::vector<std::shared_ptr<ClientNode>> others;
	bool removed = false;
	{
		std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
		for (short index = 0; index < Room->Room.MaxPlayerNum; ++index)
		{
			if (Room->Room.Player[index] != Client)
				continue;
			Room->Room.Player[index].reset();
			if (Room->Room.CurrPlayerNum > 0)
				--Room->Room.CurrPlayerNum;
			removed = true;
			break;
		}
		if (removed)
		{
			for (short index = 0; index < Room->Room.MaxPlayerNum; ++index)
			{
				if (Room->Room.Player[index])
					others.push_back(Room->Room.Player[index]);
			}
		}
	}
	if (!removed)
		return false;
	if (NotifyClient)
		ResponseLeaveOutdoor(Client);
	for (const std::shared_ptr<ClientNode>& other : others)
		NotifyLeaveOutdoor(other, Client->Uin);
	Client->ChatRoomID = -1;
	Client->GardenSessionOwnerUin = 0;
	Client->GardenSessionRoomKey = 0;
	Client->GardenSessionChatRoomID = -1;
	Client->GardenHandoffPending = false;
	{
		std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
		gPendingHandoffs.erase(Client->Uin);
	}
	return true;
}

void HandleEnterGarden(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 23, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE roomType = Read8(p);
	const UINT roomKey = Read32(p);
	Read32(p);
	Read32(p);
	GardenState state;
	if (uin != Client->Uin || roomType != kGardenRoomType ||
		!LoadGardenState(roomKey, false, state) || !CanEnterGarden(Client->Uin, state))
	{
		SendEnterGardenResponse(Client, kResultFailed, roomKey, -1);
		return;
	}

	const std::shared_ptr<GardenRoom> room = GetOrCreateGardenRoom(roomKey, GetClient(roomKey));
	if (!room || room->Room.CurrPlayerNum >= room->Room.MaxPlayerNum)
	{
		SendEnterGardenResponse(Client, kResultFailed, roomKey, -1);
		return;
	}

	const PendingGardenHandoff handoff{
		roomKey,
		roomKey,
		static_cast<INT>(room->Room.ID)
	};
	{
		std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
		gPendingHandoffs[Client->Uin] = handoff;
	}
	Client->GardenSessionOwnerUin = handoff.OwnerUin;
	Client->GardenSessionRoomKey = handoff.RoomKey;
	Client->GardenSessionChatRoomID = handoff.ChatRoomID;
	Client->GardenHandoffPending = true;
	SendEnterGardenResponse(Client, kResultSuccess, roomKey, handoff.ChatRoomID);
}

void HandleUnlockParking(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 11, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE index = Read8(p);
	USHORT resultCode = kResultFailed;
	INT remaining = 0;
	GardenState state;
	if (uin == Client->Uin && Client->GardenSessionOwnerUin == Client->Uin)
	{
		std::lock_guard<std::mutex> guard(gGardenDataMutex);
		try
		{
			DBConnection database(player);
			sql::Connection* connection = database.getConnection();
			if (connection)
			{
				Transaction transaction(connection);
				GardenRecord occupied;
				if (LoadGardenState(connection, Client->Uin, true, state) &&
					index < 10 && EmptyParkingType(state, index) == kSlotLocked &&
					!SelectActiveRecord(connection, Client->Uin, kRecordParking, index, true, occupied) &&
					ConsumeItem(connection, Client->Uin, kGardenCoinItemID, kUnlockCost, remaining))
				{
					state.UnlockedParkingMask |= static_cast<USHORT>(1u << index);
					if (SaveGardenState(connection, state) && transaction.Commit())
						resultCode = kResultSuccess;
				}
			}
		}
		catch (const std::exception& exception)
		{
			std::cerr << exception.what() << " HandleUnlockParking Error" << std::endl;
		}
	}
	BYTE response[4]{};
	p = response;
	Write16(p, resultCode);
	Write8(p, index);
	SendPersonalGardenMessage(Client, 20139, response, p - response, Response);
	if (resultCode == kResultSuccess)
	{
		NotifyUseItemResult(Client, kGardenCoinItemID, kUnlockCost, remaining);
		GardenRecord empty;
		empty.SlotIndex = index;
		SendParkingChanged(Client->Uin, empty, kSlotEmpty);
	}
}

void SendParkingResponse(
	const std::shared_ptr<ClientNode>& Client,
	USHORT ResultCode,
	UINT OwnerUin,
	BYTE OptType,
	BYTE Index,
	const GardenRecord& Record,
	BYTE Surplus)
{
	BYTE response[64]{};
	BYTE* p = response;
	Write16(p, ResultCode);
	Write32(p, OwnerUin);
	Write8(p, OptType);
	Write8(p, Index);
	Write32(p, Record.EntityID);
	Write32(p, Record.StartTime);
	Write8(p, Surplus);
	Write32(p, static_cast<UINT>(Record.ShapeSuitID));
	Write8(p, 0);
	Write8(p, 0);
	Write8(p, Record.ZhizhenStage);
	Write32(p, Record.KartSkinModel);
	SendPersonalGardenMessage(Client, 20140, response, p - response, Response);
}

void HandleParking(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 16, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE optType = Read8(p);
	const BYTE index = Read8(p);
	const UINT kartID = Read32(p);
	const UINT ownerUin = Client->GardenSessionOwnerUin;
	USHORT resultCode = kResultFailed;
	BYTE surplus = 0;
	GardenRecord wireRecord;
	wireRecord.SlotIndex = index;
	wireRecord.EntityID = kartID;
	GardenState ownerState;
	GardenState actorState;
	GardenRecord notifyRecord;
	BYTE notifyType = kSlotUnavailable;

	if (uin == Client->Uin && ownerUin != 0 && index < 10 && optType <= 2)
	{
		std::lock_guard<std::mutex> guard(gGardenDataMutex);
		try
		{
			DBConnection database(player);
			sql::Connection* connection = database.getConnection();
			if (connection)
			{
				Transaction transaction(connection);
				if (LoadGardenState(connection, ownerUin, true, ownerState) &&
					LoadGardenState(connection, Client->Uin, true, actorState) &&
					CanEnterGarden(Client->Uin, ownerState))
				{
					if (optType == 0)
					{
						GardenRecord occupied;
						const UINT actorLevel = ClampGardenLevel(actorState.Level);
						const bool ownGarden = ownerUin == Client->Uin;
						const UINT dailyLimit = ownGarden
							? kDailyOwnParkingCount[actorLevel]
							: kDailyOtherParkingCount[actorLevel];
						const UINT dailyUsed = CountDailyParking(connection, Client->Uin, ownGarden);
						if (EmptyParkingType(ownerState, index) == kSlotEmpty &&
							!SelectActiveRecord(connection, ownerUin, kRecordParking, index, true, occupied) &&
							!HasActiveEntity(connection, kRecordParking, Client->Uin, kartID) &&
							CountActiveOutgoing(connection, Client->Uin) < kOutParkingCount[actorLevel] &&
							dailyUsed < dailyLimit)
						{
							GardenRecord record;
							record.OwnerUin = ownerUin;
							record.RecordType = kRecordParking;
							record.SlotIndex = index;
							record.StartTime = static_cast<UINT>(std::time(nullptr));
							record.Status = kRecordActive;
							if (LoadOwnedKart(connection, Client, kartID, record) &&
								InsertRecord(connection, record) && transaction.Commit())
							{
								resultCode = kResultSuccess;
								wireRecord = record;
								notifyRecord = record;
								notifyType = kSlotOccupied;
								const UINT usedAfter = dailyUsed + 1;
								surplus = static_cast<BYTE>(dailyLimit > usedAfter
									? dailyLimit - usedAfter
									: 0);
							}
						}
					}
					else
					{
						GardenRecord active;
						if (SelectActiveRecord(connection, ownerUin, kRecordParking, index, true, active) &&
							((optType == 1 && active.EntityOwnerUin == Client->Uin) ||
							 (optType == 2 && ownerUin == Client->Uin)) &&
							FinishRecord(connection, active, static_cast<UINT>(std::time(nullptr))) &&
							transaction.Commit())
						{
							resultCode = kResultSuccess;
							wireRecord = active;
							notifyRecord = {};
							notifyRecord.SlotIndex = index;
							notifyType = EmptyParkingType(ownerState, index);
						}
					}
				}
			}
		}
		catch (const std::exception& exception)
		{
			std::cerr << exception.what() << " HandleGardenParking Error" << std::endl;
		}
	}
	SendParkingResponse(Client, resultCode, ownerUin, optType, index, wireRecord, surplus);
	if (resultCode == kResultSuccess)
		SendParkingChanged(ownerUin, notifyRecord, notifyType);
}

void HandleParkingAward(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 11, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE index = Read8(p);
	if (uin != Client->Uin)
		return;
	// The current official samples define the wire shape and 4-hour timeout,
	// but do not contain the reward table/formula. Do not manufacture a payout.
	BYTE response[12]{};
	p = response;
	Write16(p, kResultFailed);
	Write8(p, index);
	Write32(p, 0);
	Write8(p, 0);
	SendPersonalGardenMessage(Client, 20141, response, p - response, Response);
}

void HandleUnlockPet(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 11, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE index = Read8(p);
	USHORT resultCode = kResultFailed;
	INT remaining = 0;
	GardenState state;
	if (uin == Client->Uin && Client->GardenSessionOwnerUin == Client->Uin)
	{
		std::lock_guard<std::mutex> guard(gGardenDataMutex);
		try
		{
			DBConnection database(player);
			sql::Connection* connection = database.getConnection();
			if (connection)
			{
				Transaction transaction(connection);
				GardenRecord occupied;
				if (LoadGardenState(connection, Client->Uin, true, state) &&
					index < 6 && EmptyPetType(state, index) == kSlotLocked &&
					!SelectActiveRecord(connection, Client->Uin, kRecordPet, index, true, occupied) &&
					ConsumeItem(connection, Client->Uin, kGardenCoinItemID, kUnlockCost, remaining))
				{
					state.UnlockedPetMask |= static_cast<BYTE>(1u << index);
					if (SaveGardenState(connection, state) && transaction.Commit())
						resultCode = kResultSuccess;
				}
			}
		}
		catch (const std::exception& exception)
		{
			std::cerr << exception.what() << " HandleUnlockGardenPet Error" << std::endl;
		}
	}
	BYTE response[4]{};
	p = response;
	Write16(p, resultCode);
	Write8(p, index);
	SendPersonalGardenMessage(Client, 20143, response, p - response, Response);
	if (resultCode == kResultSuccess)
	{
		NotifyUseItemResult(Client, kGardenCoinItemID, kUnlockCost, remaining);
		GardenRecord empty;
		empty.SlotIndex = index;
		SendPetChanged(Client->Uin, empty, kSlotEmpty);
	}
}

bool FindOwnedPet(
	const std::shared_ptr<ClientNode>& Client,
	UINT PetID,
	GardenRecord& Record)
{
	std::vector<PetInfo> pets;
	PlayerDB_SelectPetInfo(Client->Uin, pets);
	for (const PetInfo& pet : pets)
	{
		if (pet.ID != PetID)
			continue;
		Record.EntityID = pet.ID;
		Record.EntityOwnerUin = Client->Uin;
		Record.EntityNickname = ClientNickName(Client);
		Record.PetLevel = static_cast<USHORT>((std::min)(pet.Level, 65535u));
		Record.PetStatus = static_cast<BYTE>((std::min)(pet.Status, 255u));
		return true;
	}
	return false;
}

void HandleStockPet(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 15, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE index = Read8(p);
	const UINT petID = Read32(p);
	const UINT ownerUin = Client->GardenSessionOwnerUin;
	USHORT resultCode = kResultFailed;
	GardenRecord responseRecord;
	responseRecord.SlotIndex = index;
	GardenRecord notifyRecord;
	BYTE responseType = kSlotUnavailable;
	BYTE notifyType = kSlotUnavailable;
	GardenState state;
	GardenRecord ownedPet;
	const bool petLoaded = petID == 0 || FindOwnedPet(Client, petID, ownedPet);

	if (uin == Client->Uin && ownerUin == Client->Uin && index < 6 && petLoaded)
	{
		std::lock_guard<std::mutex> guard(gGardenDataMutex);
		try
		{
			DBConnection database(player);
			sql::Connection* connection = database.getConnection();
			if (connection)
			{
				Transaction transaction(connection);
				if (LoadGardenState(connection, ownerUin, true, state))
				{
					GardenRecord active;
					const bool hasActive = SelectActiveRecord(
						connection, ownerUin, kRecordPet, index, true, active);
					if (petID == 0)
					{
						if (hasActive && active.EntityOwnerUin == Client->Uin &&
							FinishRecord(connection, active, static_cast<UINT>(std::time(nullptr))) &&
							transaction.Commit())
						{
							resultCode = kResultSuccess;
							responseType = EmptyPetType(state, index);
							notifyType = responseType;
							notifyRecord.SlotIndex = index;
						}
					}
					else if (!hasActive && EmptyPetType(state, index) == kSlotEmpty &&
						!HasActiveEntity(connection, kRecordPet, Client->Uin, petID))
					{
						ownedPet.OwnerUin = ownerUin;
						ownedPet.RecordType = kRecordPet;
						ownedPet.SlotIndex = index;
						ownedPet.StartTime = static_cast<UINT>(std::time(nullptr));
						ownedPet.Status = kRecordActive;
						std::unique_ptr<sql::PreparedStatement> verify(
							connection->prepareStatement(
								"SELECT PetId FROM Pet WHERE Uin=? AND PetId=? FOR UPDATE;"));
						verify->setUInt(1, Client->Uin);
						verify->setUInt(2, petID);
						std::unique_ptr<sql::ResultSet> verifyResult(verify->executeQuery());
						if (verifyResult->next() && InsertRecord(connection, ownedPet) && transaction.Commit())
						{
							resultCode = kResultSuccess;
							responseRecord = ownedPet;
							responseType = kSlotOccupied;
							notifyRecord = ownedPet;
							notifyType = kSlotOccupied;
						}
					}
				}
			}
		}
		catch (const std::exception& exception)
		{
			std::cerr << exception.what() << " HandleGardenStockPet Error" << std::endl;
		}
	}
	BYTE response[64]{};
	p = response;
	Write16(p, resultCode);
	Write32(p, ownerUin);
	WritePetInfo(p, responseRecord, responseType);
	SendPersonalGardenMessage(Client, 20144, response, p - response, Response);
	if (resultCode == kResultSuccess)
		SendPetChanged(ownerUin, notifyRecord, notifyType);
}

void HandleBatchGardenInfo(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 52, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const USHORT count = Read16(p);
	std::array<UINT, 10> requested{};
	for (UINT& value : requested)
		value = Read32(p);
	if (uin != Client->Uin || count > requested.size())
		return;

	struct Info
	{
		GardenState State;
		BYTE Current = 0;
		BYTE Friend = 0;
	};
	std::vector<Info> infos;
	for (USHORT index = 0; index < count; ++index)
	{
		GardenState state;
		if (requested[index] == 0 || !LoadGardenState(requested[index], false, state) ||
			state.Level == 0)
		{
			continue;
		}
		Info info;
		info.State = state;
		info.Current = GardenRoomCurrentPlayers(state.Uin);
		info.Friend = IsFriend(Client->Uin, state.Uin) ? 1 : 0;
		infos.push_back(info);
	}

	BYTE response[512]{};
	p = response;
	Write16(p, kResultSuccess);
	Write32(p, Client->Uin);
	Write16(p, static_cast<USHORT>(infos.size()));
	for (const Info& info : infos)
	{
		BYTE* nested = p;
		Write16(p, 0);
		Write32(p, info.State.Uin);
		Write32(p, info.Current);
		Write32(p, kGardenRoomCapacity);
		Write8(p, info.State.PrivateType);
		Write8(p, info.State.Level);
		Write8(p, info.Friend);
		FinishNested(nested, p);
	}
	SendPersonalGardenMessage(Client, 20146, response, p - response, Response);
}

void HandleParkingMap(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 10, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	if (uin != Client->Uin)
		return;
	std::vector<GardenRecord> records;
	try
	{
		DBConnection database(player);
		sql::Connection* connection = database.getConnection();
		if (!connection)
			return;
		std::string sql = "SELECT ";
		sql += RecordColumns();
		sql += " FROM PersonalGardenRecord WHERE RecordType=1 AND EntityOwnerUin=? "
			"AND Status=1 ORDER BY RecordID DESC LIMIT 255;";
		std::unique_ptr<sql::PreparedStatement> select(connection->prepareStatement(sql));
		select->setUInt(1, Client->Uin);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		while (result->next())
		{
			GardenRecord record;
			FillRecord(result.get(), record);
			records.push_back(std::move(record));
		}
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << " HandleGardenParkingMap Error" << std::endl;
		return;
	}

	std::vector<BYTE> response(8192);
	p = response.data();
	Write32(p, Client->Uin);
	BYTE* list = p;
	Write16(p, 0);
	Write8(p, static_cast<BYTE>(records.size()));
	for (const GardenRecord& record : records)
	{
		BYTE* nested = p;
		Write16(p, 0);
		Write32(p, record.EntityID);
		Write32(p, record.OwnerUin);
		Write8(p, record.SlotIndex);
		Write32(p, record.StartTime);
		FinishNested(nested, p);
	}
	FinishNested(list, p);
	SendPersonalGardenMessage(Client, 20149, response.data(), p - response.data(), Response);
}

void HandleGardenActivities(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 10, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	if (uin != Client->Uin)
		return;
	std::vector<GardenRecord> records;
	try
	{
		DBConnection database(player);
		sql::Connection* connection = database.getConnection();
		if (!connection)
			return;
		std::string sql = "SELECT ";
		sql += RecordColumns();
		sql += " FROM PersonalGardenRecord WHERE RecordType=1 AND "
			"(OwnerUin=? OR EntityOwnerUin=?) ORDER BY RecordID DESC LIMIT 5;";
		std::unique_ptr<sql::PreparedStatement> select(connection->prepareStatement(sql));
		select->setUInt(1, Client->Uin);
		select->setUInt(2, Client->Uin);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		while (result->next())
		{
			GardenRecord record;
			FillRecord(result.get(), record);
			records.push_back(std::move(record));
		}
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << " HandleGardenActivities Error" << std::endl;
		return;
	}

	std::vector<BYTE> response(4096);
	p = response.data();
	Write16(p, kResultSuccess);
	Write32(p, Client->Uin);
	BYTE* list = p;
	Write16(p, 0);
	Write16(p, static_cast<USHORT>(records.size()));
	for (const GardenRecord& record : records)
	{
		BYTE* nested = p;
		Write16(p, 0);
		Write32(p, record.EndTime != 0 ? record.EndTime : record.StartTime);
		Write32(p, record.EntityOwnerUin);
		WriteFixedString(p, record.EntityNickname, 17);
		Write32(p, record.EntityID);
		Write32(p, record.OwnerUin);
		WriteFixedString(p, ResolveNickName(record.OwnerUin), 17);
		const UINT endTime = record.EndTime != 0
			? record.EndTime
			: static_cast<UINT>(std::time(nullptr));
		Write32(p, endTime > record.StartTime ? (endTime - record.StartTime) / 3600u : 0);
		Write32(p, record.Popularity);
		Write8(p, record.RewardItemID != 0 ? 1 : 0);
		if (record.RewardItemID != 0)
			Write32(p, record.RewardItemID);
		Write8(p, record.FirstReceivedFlag);
		FinishNested(nested, p);
	}
	FinishNested(list, p);
	SendPersonalGardenMessage(Client, 20150, response.data(), p - response.data(), Response);
}

void HandleChangePrivacy(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 11, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE type = Read8(p);
	UINT resultCode = kResultFailed;
	GardenState state;
	if (uin == Client->Uin && Client->GardenSessionOwnerUin == Client->Uin && type <= 1)
	{
		std::lock_guard<std::mutex> guard(gGardenDataMutex);
		try
		{
			DBConnection database(player);
			sql::Connection* connection = database.getConnection();
			if (connection)
			{
				Transaction transaction(connection);
				if (LoadGardenState(connection, Client->Uin, true, state) &&
					state.Level >= 6)
				{
					state.PrivateType = type;
					if (SaveGardenState(connection, state) && transaction.Commit())
						resultCode = kResultSuccess;
				}
			}
		}
		catch (const std::exception& exception)
		{
			std::cerr << exception.what() << " HandleGardenPrivacy Error" << std::endl;
		}
	}
	BYTE response[4]{};
	p = response;
	Write32(p, resultCode);
	SendPersonalGardenMessage(Client, 20151, response, p - response, Response);
	if (resultCode == kResultSuccess)
		SendGardenBaseUpdate(Client, state);
}

struct RecommendGarden
{
	GardenState State;
	UINT RoomID = 0;
	std::string Name;
	UINT ParkingCount = 0;
};

void HandleRecommendGardens(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 17, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE roomType = Read8(p);
	const UINT context = Read32(p);
	const USHORT requested = Read16(p);
	if (uin != Client->Uin || roomType != kGardenRoomType || requested > 100)
		return;

	std::vector<UINT> candidates;
	try
	{
		DBConnection database(player);
		sql::Connection* connection = database.getConnection();
		if (!connection)
			return;
		std::unique_ptr<sql::PreparedStatement> select(
			connection->prepareStatement(
				"SELECT s.Uin FROM PersonalGardenState s JOIN Player p ON p.Uin=s.Uin "
				"WHERE p.GardenLevel>0 AND s.Uin<>? ORDER BY s.TotalPopularity DESC,s.Uin LIMIT 100;"));
		select->setUInt(1, Client->Uin);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		while (result->next())
			candidates.push_back(result->getUInt("Uin"));
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << " HandleGardenRecommendList Error" << std::endl;
		return;
	}

	std::vector<RecommendGarden> gardens;
	for (UINT ownerUin : candidates)
	{
		if (gardens.size() >= requested)
			break;
		GardenState state;
		if (!LoadGardenState(ownerUin, false, state) || !CanEnterGarden(Client->Uin, state))
			continue;
		const std::shared_ptr<GardenRoom> room = GetOrCreateGardenRoom(ownerUin);
		if (!room)
			continue;
		RecommendGarden garden;
		garden.State = state;
		garden.RoomID = room->Room.ID;
		garden.Name = room->Name;
		garden.ParkingCount = static_cast<UINT>(LoadActiveRecords(ownerUin, kRecordParking).size());
		gardens.push_back(std::move(garden));
	}

	std::vector<BYTE> response(16384);
	p = response.data();
	Write16(p, kResultSuccess);
	Write8(p, roomType);
	Write32(p, context);
	Write16(p, static_cast<USHORT>(gardens.size()));
	for (const RecommendGarden& garden : gardens)
	{
		BYTE* nested = p;
		Write16(p, 0);
		Write8(p, kGardenRoomType);
		Write32(p, garden.State.Uin);
		Write32(p, garden.RoomID);
		WriteFixedString(p, garden.Name, 25);
		const std::array<UINT, 8> attributes = {
			0u,
			static_cast<UINT>(kGardenRoomCapacity),
			static_cast<UINT>(GardenRoomCurrentPlayers(garden.State.Uin)),
			static_cast<UINT>(garden.State.PrivateType),
			garden.ParkingCount,
			static_cast<UINT>(garden.State.Level),
			garden.State.WeekPopularity,
			IsFriend(Client->Uin, garden.State.Uin) ? 1u : 0u
		};
		for (UINT attribute : attributes)
			Write32(p, attribute);
		FinishNested(nested, p);
	}
	SendPersonalGardenMessage(Client, 20152, response.data(), p - response.data(), Response);
}

void HandleUpgradeBuilding(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 11, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE newLevel = Read8(p);
	UINT resultCode = kResultFailed;
	INT remaining = 0;
	GardenState state;
	if (uin == Client->Uin && Client->GardenSessionOwnerUin == Client->Uin)
	{
		std::lock_guard<std::mutex> guard(gGardenDataMutex);
		try
		{
			DBConnection database(player);
			sql::Connection* connection = database.getConnection();
			if (connection)
			{
				Transaction transaction(connection);
				if (LoadGardenState(connection, Client->Uin, true, state) &&
					newLevel == static_cast<BYTE>(state.BuildingLevel + 1) &&
					newLevel <= ClampGardenLevel(state.Level) &&
					ConsumeItem(connection, Client->Uin, kGardenCoinItemID, kBuildingUpgradeCost, remaining))
				{
					state.BuildingLevel = newLevel;
					if (SaveGardenState(connection, state) && transaction.Commit())
						resultCode = kResultSuccess;
				}
			}
		}
		catch (const std::exception& exception)
		{
			std::cerr << exception.what() << " HandleGardenBuildingUpgrade Error" << std::endl;
		}
	}
	BYTE response[8]{};
	p = response;
	Write32(p, resultCode);
	Write8(p, newLevel);
	SendPersonalGardenMessage(Client, 20153, response, p - response, Response);
	if (resultCode == kResultSuccess)
		NotifyUseItemResult(Client, kGardenCoinItemID, kBuildingUpgradeCost, remaining);
}

void HandleRubberDuck(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 11, p))
		return;
	const UINT uin = Read32(p);
	Read32(p);
	const BYTE stateValue = Read8(p);
	USHORT resultCode = kResultFailed;
	GardenState state;
	if (uin == Client->Uin && Client->GardenSessionOwnerUin == Client->Uin && stateValue <= 1)
	{
		std::lock_guard<std::mutex> guard(gGardenDataMutex);
		try
		{
			DBConnection database(player);
			sql::Connection* connection = database.getConnection();
			if (connection)
			{
				Transaction transaction(connection);
				if (LoadGardenState(connection, Client->Uin, true, state) && state.Level >= 2)
				{
					state.RubberDuckState = stateValue;
					if (SaveGardenState(connection, state) && transaction.Commit())
						resultCode = kResultSuccess;
				}
			}
		}
		catch (const std::exception& exception)
		{
			std::cerr << exception.what() << " HandleGardenRubberDuck Error" << std::endl;
		}
	}
	BYTE response[12]{};
	p = response;
	Write32(p, Client->Uin);
	Write16(p, resultCode);
	Write8(p, stateValue);
	SendPersonalGardenMessage(Client, 20154, response, p - response, Response);
	if (resultCode == kResultSuccess)
		SendRubberDuckState(Client->Uin, stateValue);
}
} // namespace

void PersonalGardenEnsureSchema()
{
	gGardenSchemaReady.store(false, std::memory_order_release);
	try
	{
		DBConnection database(player);
		sql::Connection* connection = database.getConnection();
		if (!connection)
		{
			std::cerr << "[DATABASE][Garden] player database connection unavailable" << std::endl;
			return;
		}
		std::unique_ptr<sql::Statement> statement(connection->createStatement());
		statement->execute(
			"CREATE TABLE IF NOT EXISTS PersonalGardenState ("
			"Uin INT UNSIGNED NOT NULL,"
			"WeekPopularity INT UNSIGNED NOT NULL DEFAULT 0,"
			"TotalPopularity INT UNSIGNED NOT NULL DEFAULT 0,"
			"LastUpdatePopularityTime INT UNSIGNED NOT NULL DEFAULT 0,"
			"PrivateType TINYINT UNSIGNED NOT NULL DEFAULT 0,"
			"BuildingLevel TINYINT UNSIGNED NOT NULL DEFAULT 1,"
			"RubberDuckState TINYINT UNSIGNED NOT NULL DEFAULT 0,"
			"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
			"UnlockedParkingMask SMALLINT UNSIGNED NOT NULL DEFAULT 1,"
			"UnlockedPetMask TINYINT UNSIGNED NOT NULL DEFAULT 0,"
			"LastReceiveAwardTime INT UNSIGNED NOT NULL DEFAULT 0,"
			"UpdatedAt INT UNSIGNED NOT NULL DEFAULT 0,"
			"PRIMARY KEY (Uin)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
		statement->execute(
			"CREATE TABLE IF NOT EXISTS PersonalGardenRecord ("
			"RecordID BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
			"OwnerUin INT UNSIGNED NOT NULL,"
			"RecordType TINYINT UNSIGNED NOT NULL,"
			"SlotIndex TINYINT UNSIGNED NOT NULL,"
			"EntityID INT UNSIGNED NOT NULL DEFAULT 0,"
			"EntityOwnerUin INT UNSIGNED NOT NULL DEFAULT 0,"
			"EntityNickname VARBINARY(17) NOT NULL,"
			"StartTime INT UNSIGNED NOT NULL DEFAULT 0,"
			"EndTime INT UNSIGNED NOT NULL DEFAULT 0,"
			"Status TINYINT UNSIGNED NOT NULL DEFAULT 1,"
			"RefitCount INT NOT NULL DEFAULT 0,"
			"ShapeSuitID INT NOT NULL DEFAULT 0,"
			"ZhizhenStage TINYINT UNSIGNED NOT NULL DEFAULT 0,"
			"KartSkinModel INT UNSIGNED NOT NULL DEFAULT 0,"
			"PetLevel SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
			"PetStatus TINYINT UNSIGNED NOT NULL DEFAULT 0,"
			"RewardItemID INT UNSIGNED NOT NULL DEFAULT 0,"
			"RewardItemNum INT UNSIGNED NOT NULL DEFAULT 0,"
			"Popularity INT UNSIGNED NOT NULL DEFAULT 0,"
			"AwardClaimed TINYINT UNSIGNED NOT NULL DEFAULT 0,"
			"FirstReceivedFlag TINYINT UNSIGNED NOT NULL DEFAULT 1,"
			"PRIMARY KEY (RecordID),"
			"KEY idx_GardenRecord_OwnerSlot (OwnerUin,RecordType,SlotIndex,Status),"
			"KEY idx_GardenRecord_Entity (EntityOwnerUin,RecordType,Status),"
			"KEY idx_GardenRecord_StartTime (StartTime)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
		gGardenSchemaReady.store(true, std::memory_order_release);
		std::cout << "[DATABASE][Garden] two-table schema ready" << std::endl;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what() << " PersonalGardenEnsureSchema Error" << std::endl;
	}
}

bool PersonalGardenTryUseKey(
	const std::shared_ptr<ClientNode>& Client,
	UINT RequestUin)
{
	if (!Client || RequestUin != Client->Uin)
		return true;
	INT remaining = 0;
	GardenState state;
	bool success = false;
	bool opened = false;
	{
		std::lock_guard<std::mutex> guard(gGardenDataMutex);
		try
		{
			DBConnection database(player);
			sql::Connection* connection = database.getConnection();
			if (connection && gGardenSchemaReady.load(std::memory_order_acquire))
			{
				Transaction transaction(connection);
				std::unique_ptr<sql::PreparedStatement> playerSelect(
					connection->prepareStatement(
						"SELECT GardenLevel,GardenPoint FROM Player WHERE Uin=? FOR UPDATE;"));
				playerSelect->setUInt(1, Client->Uin);
				std::unique_ptr<sql::ResultSet> playerResult(playerSelect->executeQuery());
				if (playerResult->next() &&
					ConsumeItem(connection, Client->Uin, kGardenKeyItemID, 1, remaining))
				{
					UINT level = playerResult->getUInt("GardenLevel");
					UINT point = playerResult->getUInt("GardenPoint");
					opened = level == 0;
					if (opened)
					{
						level = 1;
						point = 10;
					}
					else
					{
						point += 100;
						if (level < 6 && point >= kGardenLevelThreshold[level])
							++level;
					}
					std::unique_ptr<sql::PreparedStatement> update(
						connection->prepareStatement(
							"UPDATE Player SET GardenLevel=?,GardenPoint=? WHERE Uin=?;"));
					update->setUInt(1, level);
					update->setUInt(2, point);
					update->setUInt(3, Client->Uin);
					if (update->executeUpdate() == 1 &&
						LoadGardenState(connection, Client->Uin, true, state) &&
						transaction.Commit())
					{
						success = true;
						state.Level = static_cast<BYTE>(level);
						state.Point = point;
					}
				}
			}
		}
		catch (const std::exception& exception)
		{
			std::cerr << exception.what() << " PersonalGardenTryUseKey Error" << std::endl;
		}
	}
	if (!success)
	{
		ResponseStartGame(Client, "道具不足!");
		return true;
	}
	Client->GardenLevel = state.Level;
	Client->GardenPoint = state.Point;
	NotifyUseItemResult(Client, kGardenKeyItemID, 1, remaining);
	SendGardenBaseUpdate(Client, state);
	ResponseStartGame(Client, opened
		? "/C03开通后花园成功!"
		: "/C03后花园成长值增加100点!");
	return true;
}

void PersonalGardenAttachPendingLogin(const std::shared_ptr<ClientNode>& Client)
{
	if (!Client || Client->Uin == 0)
		return;
	std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
	auto found = gPendingHandoffs.find(Client->Uin);
	if (found == gPendingHandoffs.end())
		return;
	Client->GardenSessionOwnerUin = found->second.OwnerUin;
	Client->GardenSessionRoomKey = found->second.RoomKey;
	Client->GardenSessionChatRoomID = found->second.ChatRoomID;
	Client->GardenHandoffPending = true;
	gPendingHandoffs.erase(found);
}

void PersonalGardenWriteBaseInfo(
	BYTE*& p,
	UINT Uin,
	const std::shared_ptr<ClientNode>& Client)
{
	GardenState state;
	if (!LoadGardenState(Uin, false, state))
	{
		state.Uin = Uin;
		if (Client && Client->Uin == Uin)
		{
			state.Level = static_cast<BYTE>((std::min)(Client->GardenLevel, 255u));
			state.Point = Client->GardenPoint;
		}
	}
	WriteGardenBaseInfo(p, state);
}

void PersonalGardenWriteOptionalBase(
	BYTE*& p,
	UINT Uin,
	const std::shared_ptr<ClientNode>& Client)
{
	GardenState state;
	const bool loaded = LoadGardenState(Uin, false, state);
	const bool opened = loaded && state.Level > 0;
	Write8(p, opened ? 1 : 0);
	if (opened)
		WriteGardenBaseInfo(p, state);
}

void PersonalGardenHandleRequest(
	const std::shared_ptr<ClientNode>& Client,
	USHORT MsgID,
	BYTE* Body,
	size_t BodyLen)
{
	switch (MsgID)
	{
	case 20137: HandleEnterGarden(Client, Body, BodyLen); break;
	case 20139: HandleUnlockParking(Client, Body, BodyLen); break;
	case 20140: HandleParking(Client, Body, BodyLen); break;
	case 20141: HandleParkingAward(Client, Body, BodyLen); break;
	case 20143: HandleUnlockPet(Client, Body, BodyLen); break;
	case 20144: HandleStockPet(Client, Body, BodyLen); break;
	case 20146: HandleBatchGardenInfo(Client, Body, BodyLen); break;
	case 20149: HandleParkingMap(Client, Body, BodyLen); break;
	case 20150: HandleGardenActivities(Client, Body, BodyLen); break;
	case 20151: HandleChangePrivacy(Client, Body, BodyLen); break;
	case 20152: HandleRecommendGardens(Client, Body, BodyLen); break;
	case 20153: HandleUpgradeBuilding(Client, Body, BodyLen); break;
	case 20154: HandleRubberDuck(Client, Body, BodyLen); break;
	default: break;
	}
}

ChatRoomNode* PersonalGardenFindChatRoom(INT ChatRoomID)
{
	const std::shared_ptr<GardenRoom> room = FindGardenRoomByID(ChatRoomID);
	return room ? &room->Room : nullptr;
}

bool PersonalGardenTryEnterOutdoor(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	BYTE* p = nullptr;
	if (!Client || !OpenWirePayload(Body, BodyLen, 49, p))
		return false;
	const UINT uin = Read32(p);
	Read32(p);
	Read16(p);
	const USHORT requestedChatRoomID = Read16(p);
	Read16(p);
	if (uin != Client->Uin ||
		Client->GardenSessionOwnerUin == 0 ||
		Client->GardenSessionRoomKey != Client->GardenSessionOwnerUin ||
		Client->GardenSessionChatRoomID < 0 ||
		requestedChatRoomID != static_cast<USHORT>(Client->GardenSessionChatRoomID))
	{
		return false;
	}

	const std::shared_ptr<GardenRoom> room =
		FindGardenRoomByID(Client->GardenSessionChatRoomID);
	GardenState state;
	if (!room || room->OwnerUin != Client->GardenSessionOwnerUin ||
		!LoadGardenState(room->OwnerUin, false, state) ||
		!CanEnterGarden(Client->Uin, state))
	{
		return true;
	}

	char seat = -1;
	{
		std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
		if (room->Room.CurrPlayerNum >= room->Room.MaxPlayerNum)
			return true;
		for (char index = 0; index < room->Room.MaxPlayerNum; ++index)
		{
			if (room->Room.Player[index])
				continue;
			seat = index;
			room->Room.Player[index] = Client;
			++room->Room.CurrPlayerNum;
			break;
		}
	}
	if (seat < 0)
		return true;

	Client->ChatRoomID = static_cast<INT>(room->Room.ID);
	if (!SendFullGardenInfo(Client, room, state))
	{
		RemoveClientFromGardenRoom(Client, room, false);
		return true;
	}

	NotifyNPCInfo(Client);
	NotifyGuildRoomMoreInfo(Client);
	NotifyChairInfo(Client);
	for (short index = 0; index < room->Room.MaxPlayerNum; ++index)
	{
		const std::shared_ptr<ClientNode> other = room->Room.Player[index];
		if (!other || other == Client)
			continue;
		NotifyEnterOutdoor(other, &room->Room, Client, seat);
		NotifyEnterOutdoor(Client, &room->Room, other, static_cast<char>(index));
	}
	SendRubberDuckState(room->OwnerUin, state.RubberDuckState);

	Client->GardenHandoffPending = false;
	{
		std::lock_guard<std::recursive_mutex> lock(gGardenRoomMutex);
		gPendingHandoffs.erase(Client->Uin);
	}
	return true;
}

bool PersonalGardenTryLeaveOutdoor(const std::shared_ptr<ClientNode>& Client)
{
	if (!Client || Client->ChatRoomID < 0)
		return false;
	const std::shared_ptr<GardenRoom> room = FindGardenRoomByID(Client->ChatRoomID);
	if (!room)
		return false;
	return RemoveClientFromGardenRoom(Client, room, true);
}
