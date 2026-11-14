#include "Database.h"
#include "Item.h"
#include "Message.h"
#include "pet.h"
#include "PersonalGarden.h"

#include "iostream"
#include "ConnectionPool.h"
#include "ini.h"
#include "DBConnection.h"
#include "MD5.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <utility>


extern BYTE shumiaoyunshu[100];
//extern BYTE chewangkaoyan[100];
extern BYTE baofengtiaozhan[2048];
extern BYTE taskaward[128];
extern BYTE StartTimerChallenge[1024];
extern BYTE StartTimerChallenge2[1024];
extern BYTE BorderOver[187];
extern BYTE RankedOver[402];

extern BYTE jump24238[91];
extern BYTE jump24255[91];
extern BYTE msg900[52];
extern UINT IsCS;
extern int ismd5;
extern std::string GetItemName(UINT Itemid);
extern int NewKart;
extern int PreventSDP[2];
extern std::shared_ptr<ClientNode> GetClient(long Uin);
extern std::mutex ChallengeMapsMutex;
extern std::vector<ChallengeMap> ChallengeAllMaps;
extern void initializeLibrary(std::vector<UINT>& library, const std::string& input);
extern double version;


ConnectionPool player;
ConnectionPool usr;
ConnectionPool activity;

namespace
{
std::atomic<bool> g_AuroraActivityDbReady{ false };
std::atomic<bool> g_AnnualRacingActivityDbReady{ false };
std::string g_AuroraActivityConfigurationError =
	"database configuration not initialized";
std::string g_ActivityDatabaseSchema;

std::string DatabaseQuoteIdentifier(const std::string& identifier)
{
	std::string quoted = "`";
	for (char ch : identifier)
	{
		if (ch == '`')
			quoted += "``";
		else
			quoted += ch;
	}
	quoted += '`';
	return quoted;
}

std::string ActivityDB_QualifiedTable(const char* tableName)
{
	return DatabaseQuoteIdentifier(g_ActivityDatabaseSchema) + "." +
		DatabaseQuoteIdentifier(tableName ? tableName : "");
}

std::uint64_t AuroraNowMilliseconds()
{
	return static_cast<std::uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string DatabaseConfigPath()
{
	char executablePath[32768] = {};
	const DWORD pathLength = GetModuleFileNameA(
		nullptr, executablePath,
		static_cast<DWORD>(sizeof(executablePath)));
	if (pathLength == 0u ||
		pathLength >= static_cast<DWORD>(sizeof(executablePath)))
		return "mysql.ini";

	std::string path(executablePath, pathLength);
	const auto separator = path.find_last_of("\\/");
	if (separator == std::string::npos)
		return "mysql.ini";
	path.resize(separator + 1u);
	path += "mysql.ini";
	return path;
}

bool ActivityDB_ExecDDL(const char* sqlText, std::string& error)
{
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
		{
			error = "activity database connection unavailable";
			return false;
		}
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(sqlText));
		statement->execute();
		return true;
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return false;
	}
}

bool ActivityDB_EnsureAnnualRacingTables(std::string& error)
{
	const char* statements[] = {
		"CREATE TABLE IF NOT EXISTS AnnualRacingState ("
		"Uin INT UNSIGNED NOT NULL,"
		"SeasonID INT UNSIGNED NOT NULL,"
		"OpenDate INT UNSIGNED NOT NULL,"
		"StarLevel TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"OpenStatus TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"DrawCount INT UNSIGNED NOT NULL DEFAULT 0,"
		"BaoDiLeftNum INT NOT NULL DEFAULT 2,"
		"BaoDiType TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"SageEggGetNum INT UNSIGNED NOT NULL DEFAULT 0,"
		"StarRewardMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"RowVersion INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, SeasonID, OpenDate),"
		"KEY idx_AnnualRacingState_Update (UpdateTime)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;",

		"CREATE TABLE IF NOT EXISTS AnnualRacingTempItem ("
		"Id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
		"Uin INT UNSIGNED NOT NULL,"
		"SeasonID INT UNSIGNED NOT NULL,"
		"OpenDate INT UNSIGNED NOT NULL,"
		"AwardIndex INT NOT NULL DEFAULT 0,"
		"ItemID INT UNSIGNED NOT NULL,"
		"ItemID1 INT UNSIGNED NOT NULL DEFAULT 0,"
		"ItemNum INT NOT NULL DEFAULT 1,"
		"ItemType INT NOT NULL DEFAULT 0,"
		"AvailPeriod INT NOT NULL DEFAULT 0,"
		"ExchangePieceNum INT NOT NULL DEFAULT 0,"
		"Status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Id),"
		"KEY idx_AnnualRacingTemp_Active "
		"(Uin, SeasonID, OpenDate, Status, Id),"
		"KEY idx_AnnualRacingTemp_Item "
		"(Uin, SeasonID, OpenDate, ItemID, Status)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;",

		"CREATE TABLE IF NOT EXISTS AnnualRacingCounter ("
		"Uin INT UNSIGNED NOT NULL,"
		"SeasonID INT UNSIGNED NOT NULL,"
		"OpenDate INT UNSIGNED NOT NULL,"
		"CounterType TINYINT UNSIGNED NOT NULL,"
		"CounterScope SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
		"CounterKey INT UNSIGNED NOT NULL,"
		"CounterValue INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, SeasonID, OpenDate, CounterType, CounterScope, CounterKey),"
		"KEY idx_AnnualRacingCounter_Update (UpdateTime)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"
	};

	for (const char* statement : statements)
	{
		if (!ActivityDB_ExecDDL(statement, error))
			return false;
	}
	error = "ok";
	return true;
}

bool ActivityDB_AnnualRacingTableExists(
	const char* tableName,
	bool& exists,
	std::string& error)
{
	exists = false;
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
		{
			error = "activity database connection unavailable";
			return false;
		}
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"SELECT COUNT(*) AS TableCount "
				"FROM INFORMATION_SCHEMA.TABLES "
				"WHERE TABLE_SCHEMA=DATABASE() "
				"AND LOWER(TABLE_NAME)=LOWER(?);"));
		statement->setString(1, tableName);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		exists = result->next() && result->getUInt("TableCount") > 0;
		return true;
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return false;
	}
}

bool ActivityDB_MigrateAnnualRacingLegacyCounters(std::string& error)
{
	bool packageTableExists = false;
	if (!ActivityDB_AnnualRacingTableExists(
			"annualracingpackagestate",
			packageTableExists,
			error))
	{
		return false;
	}
	if (packageTableExists)
	{
		if (!ActivityDB_ExecDDL(
				"INSERT INTO AnnualRacingCounter("
				"Uin,SeasonID,OpenDate,CounterType,CounterScope,"
				"CounterKey,CounterValue,UpdateTime) "
				"SELECT Uin,SeasonID,OpenDate,1,0,SpecialFlag,GetNum,UpdateTime "
				"FROM AnnualRacingPackageState "
				"ON DUPLICATE KEY UPDATE "
				"CounterValue=GREATEST("
				"AnnualRacingCounter.CounterValue,VALUES(CounterValue)),"
				"UpdateTime=GREATEST("
				"AnnualRacingCounter.UpdateTime,VALUES(UpdateTime));",
				error))
		{
			return false;
		}
		if (!ActivityDB_ExecDDL(
				"DROP TABLE AnnualRacingPackageState;",
				error))
		{
			return false;
		}
	}

	bool exchangeTableExists = false;
	if (!ActivityDB_AnnualRacingTableExists(
			"annualracingexchangestate",
			exchangeTableExists,
			error))
	{
		return false;
	}
	if (exchangeTableExists)
	{
		if (!ActivityDB_ExecDDL(
				"INSERT INTO AnnualRacingCounter("
				"Uin,SeasonID,OpenDate,CounterType,CounterScope,"
				"CounterKey,CounterValue,UpdateTime) "
				"SELECT Uin,SeasonID,OpenDate,2,ShopType,AwardIndex,"
				"GetNum,UpdateTime FROM AnnualRacingExchangeState "
				"ON DUPLICATE KEY UPDATE "
				"CounterValue=GREATEST("
				"AnnualRacingCounter.CounterValue,VALUES(CounterValue)),"
				"UpdateTime=GREATEST("
				"AnnualRacingCounter.UpdateTime,VALUES(UpdateTime));",
				error))
		{
			return false;
		}
		if (!ActivityDB_ExecDDL(
				"DROP TABLE AnnualRacingExchangeState;",
				error))
		{
			return false;
		}
	}
	error = "ok";
	return true;
}

bool ActivityDB_LoadRuntimeWorks(std::string& error)
{
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
		{
			error = "activity database connection unavailable";
			return false;
		}
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"SELECT WorkID,OwnerUin,NameRaw,CurrentVersion,"
				"RenderMd5,RenderUrl FROM AuroraWork "
				"WHERE LifecycleState=1 AND RenderUrl<>'' "
				"AND CHAR_LENGTH(RenderMd5)=32;"));
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		while (result->next())
		{
			QQSpeedAuroraOfficial::Work work;
			work.workId = result->getUInt64("WorkID");
			work.ownerUin = result->getUInt("OwnerUin");
			work.mapId = 499u;
			work.version = result->getUInt("CurrentVersion");
			work.displayName = result->getString("NameRaw");
			work.renderUrl = result->getString("RenderUrl");
			work.md5 = result->getString("RenderMd5");
			QQSpeedAuroraOfficial::RegisterRuntimeWork(work);
		}
		error = "ok";
		return true;
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return false;
	}
}

bool ActivityDB_ReadWork(
	const sql::ResultSet& result,
	AuroraActivityWorkRow& work)
{
	try
	{
		work = {};
		work.WorkID = result.getUInt64("WorkID");
		work.OwnerUin = result.getUInt("OwnerUin");
		work.NameRaw = result.getString("NameRaw");
		work.LifecycleState = static_cast<std::uint8_t>(
			result.getUInt("LifecycleState"));
		work.QualificationStatus = static_cast<std::uint8_t>(
			result.getUInt("QualificationStatus"));
		work.CurrentVersion = result.getUInt("CurrentVersion");
		work.ComponentCount = result.getInt("ComponentCount");
		work.RenderMd5 = result.getString("RenderMd5");
		work.RenderObjectKey = result.getString("RenderObjectKey");
		work.RenderUrl = result.getString("RenderUrl");
		work.SettingsMd5 = result.getString("SettingsMd5");
		work.SettingsObjectKey = result.getString("SettingsObjectKey");
		work.SettingsUrl = result.getString("SettingsUrl");
		work.CreatedAtMs = result.getUInt64("CreatedAtMs");
		work.UpdatedAtMs = result.getUInt64("UpdatedAtMs");
		return work.WorkID != 0u && work.OwnerUin != 0u;
	}
	catch (const std::exception&)
	{
		work = {};
		return false;
	}
}

bool ActivityDB_ReadUpload(
	const sql::ResultSet& result,
	AuroraActivityUploadRow& upload)
{
	try
	{
		upload = {};
		upload.UploadID = result.getUInt64("UploadID");
		upload.WorkID = result.getUInt64("WorkID");
		upload.OwnerUin = result.getUInt("OwnerUin");
		upload.Operation = static_cast<std::uint8_t>(
			result.getUInt("Operation"));
		upload.Command = static_cast<std::uint8_t>(
			result.getUInt("CosCommand"));
		upload.RequestTimestamp = result.getUInt("RequestTimestamp");
		upload.WorkVersion = result.getUInt("WorkVersion");
		upload.ClientRequestID = result.getString("ClientRequestID");
		upload.ExpectedMd5 = result.getString("ExpectedMd5");
		upload.ObjectID = result.getUInt64("ObjectID");
		upload.ObjectKey = result.getString("ObjectKey");
		upload.PublicUrl = result.getString("PublicUrl");
		upload.AuthHash = result.getString("AuthHash");
		upload.Status = static_cast<std::uint8_t>(result.getUInt("Status"));
		upload.CreatedAtMs = result.getUInt64("CreatedAtMs");
		upload.ReportedAtMs = result.getUInt64("ReportedAtMs");
		return upload.UploadID != 0u && upload.WorkID != 0u;
	}
	catch (const std::exception&)
	{
		upload = {};
		return false;
	}
}

bool ActivityDB_SelectWorkForUpdate(
	sql::Connection* connection,
	std::uint64_t workId,
	AuroraActivityWorkRow& work,
	std::string& error)
{
	std::unique_ptr<sql::PreparedStatement> statement(connection->prepareStatement(
		"SELECT WorkID,OwnerUin,NameRaw,LifecycleState,QualificationStatus,"
		"CurrentVersion,ComponentCount,RenderMd5,RenderObjectKey,RenderUrl,"
		"SettingsMd5,SettingsObjectKey,SettingsUrl,CreatedAtMs,UpdatedAtMs "
		"FROM AuroraWork WHERE WorkID=? FOR UPDATE;"));
	statement->setUInt64(1, workId);
	std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
	if (!result->next())
	{
		error = "Aurora work not found";
		return false;
	}
	if (!ActivityDB_ReadWork(*result, work))
	{
		error = "Aurora work row invalid";
		return false;
	}
	return true;
}

bool ActivityDB_AllocateSequence(
	sql::Connection* connection,
	const char* sequenceName,
	std::uint64_t& value,
	std::string& error)
{
	std::unique_ptr<sql::PreparedStatement> select(connection->prepareStatement(
		"SELECT NextValue FROM AuroraSequence "
		"WHERE SequenceName=? FOR UPDATE;"));
	select->setString(1, sequenceName);
	std::unique_ptr<sql::ResultSet> result(select->executeQuery());
	if (!result->next())
	{
		error = "Aurora sequence missing";
		return false;
	}
	value = result->getUInt64("NextValue");
	if (value == 0u ||
		value == (std::numeric_limits<std::uint64_t>::max)())
	{
		error = "Aurora sequence exhausted";
		return false;
	}
	std::unique_ptr<sql::PreparedStatement> update(connection->prepareStatement(
		"UPDATE AuroraSequence SET NextValue=? WHERE SequenceName=?;"));
	update->setUInt64(1, value + 1u);
	update->setString(2, sequenceName);
	if (update->executeUpdate() != 1)
	{
		error = "Aurora sequence update affected unexpected rows";
		return false;
	}
	return true;
}

std::string ActivityDB_PublicUrl(
	const std::string& publicBaseUrl,
	const std::string& objectKey)
{
	return publicBaseUrl + "/" + objectKey;
}
}

std::string ActivityDB_QualifiedTableName(const char* tableName)
{
	return ActivityDB_QualifiedTable(tableName);
}

void InitTaskInfo()
{
	//???? ??????
	//????NPC????????104-138
	//\x01\x6e\x00\x00\x00\x01\xcc\xd8\xc0\xef\xcb\xb9\xcc\xb9\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb1\xa9\xb7\xe7\xcc\xec\xca\xb9\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x46\x00\x00\x27\x26\x00\x00\x2c\x77\x00\x00\x28\x3f\x00\x00\x2c\x79\x00\x00\x2c\x75\x00\x00\x2c\x78\x00\x00\x2c\x76\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x2c\x7a\x00\x00\x00\x00\x00\x00\x2c\x70\x00\x00\x27\x2d\x00\x00\x27\x75\x00\x00\x27\x73\x00\x00\x28\x08\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x27\x10\x00\x22\x00\x00\x00\x41\x00\x00\x00\x0f\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x22\x00\x00\x00\x37\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x01\x86\x9f\x00\x00\x00\xc8\x00\x22\x00\x00\x00\x3c\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x22\x00\x00\x00\x46\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\xff\xff\xff\x9c\x00\x22\x00\x00\x00\x50\x00\x00\x00\x19\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x01\xff\xff\xff\x9c\xff\xfe\x79\x61\x04\x00\x12\x00\x00\x00\x08\x00\x00\x00\x00\x00\x0f\x42\x36\x00\x00\x27\x0f\x00\x12\x00\x00\x00\x0a\x00\x00\x1f\x40\x00\x00\x03\xe7\x00\x00\x00\x14\x00\x12\x00\x00\x00\x01\x00\x00\x13\x88\x00\x00\x00\x14\xff\xff\xff\xec\x00\x12\x00\x00\x00\x02\x00\x00\x13\x88\xff\xff\xff\xec\xff\xff\xff\x9c\x00\x00\x00\x00
	memcpy(baofengtiaozhan, "\x00\x00\xea\x60\x00\x00\x00\x49\x00\x00\x00\x79\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x9b\x28\x9c\x8c\x6b\x00\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x01\x6e\x00\x00\x00\x01\xcc\xd8\xc0\xef\xcb\xb9\xcc\xb9\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb1\xa9\xb7\xe7\xcc\xec\xca\xb9\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x46\x00\x00\x27\x26\x00\x00\x2c\x77\x00\x00\x28\x3f\x00\x00\x2c\x79\x00\x00\x2c\x75\x00\x00\x2c\x78\x00\x00\x2c\x76\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x2c\x7a\x00\x00\x00\x00\x00\x00\x2c\x70\x00\x00\x27\x2d\x00\x00\x27\x75\x00\x00\x27\x73\x00\x00\x28\x08\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x27\x10\x00\x22\x00\x00\x00\x41\x00\x00\x00\x0f\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x22\x00\x00\x00\x37\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x01\x86\x9f\x00\x00\x00\xc8\x00\x22\x00\x00\x00\x3c\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x22\x00\x00\x00\x46\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\xff\xff\xff\x9c\x00\x22\x00\x00\x00\x50\x00\x00\x00\x19\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x01\xff\xff\xff\x9c\xff\xfe\x79\x61\x04\x00\x12\x00\x00\x00\x08\x00\x00\x00\x00\x00\x0f\x42\x36\x00\x00\x27\x0f\x00\x12\x00\x00\x00\x0a\x00\x00\x1f\x40\x00\x00\x03\xe7\x00\x00\x00\x14\x00\x12\x00\x00\x00\x01\x00\x00\x13\x88\x00\x00\x00\x14\xff\xff\xff\xec\x00\x12\x00\x00\x00\x02\x00\x00\x13\x88\xff\xff\xff\xec\xff\xff\xff\x9c\x00\x00\x00\x00\x01\x6e\x00\x00\x00\x02\xbf\xad\xc0\xf2\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb1\xa9\xb7\xe7\xcc\xec\xca\xb9\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x46\x00\x00\x27\x22\x00\x00\x2c\x07\x00\x00\x28\x42\x00\x00\x2c\x09\x00\x00\x2c\x05\x00\x00\x2c\x08\x00\x00\x2c\x06\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x2c\x0a\x00\x00\x00\x00\x00\x00\x2c\x6b\x00\x00\x28\x7c\x00\x00\x27\x75\x00\x00\x27\x73\x00\x00\x2b\xed\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x27\x10\x00\x22\x00\x00\x00\x1e\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x22\x00\x00\x00\x28\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x01\x86\x9f\x00\x00\x00\xc8\x00\x22\x00\x00\x00\x37\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x22\x00\x00\x00\x3c\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\xff\xff\xff\x9c\x00\x22\x00\x00\x00\x50\x00\x00\x00\x19\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x01\xff\xff\xff\x9c\xff\xfe\x79\x61\x04\x00\x12\x00\x00\x00\x08\x00\x00\x1f\x40\x00\x00\x00\xc8\x00\x00\x00\x01\x00\x12\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x12\x00\x00\x00\x01\x00\x00\x03\xe8\x00\x00\x00\x00\xff\xff\xfe\x0c\x00\x12\x00\x00\x00\x02\x00\x00\x00\x00\xff\xff\xfc\x19\xff\xff\xd8\xf1\x00\x00\x00\x00\x01\x6e\x00\x00\x00\x03\xbc\xd3\xce\xc4\xcb\xb9\xc0\xfb\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb1\xa9\xb7\xe7\xcc\xec\xca\xb9\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x46\x00\x00\x27\x26\x00\x00\x2c\x0d\x00\x00\x27\x8c\x00\x00\x2c\x0f\x00\x00\x2c\x0b\x00\x00\x2c\x0e\x00\x00\x2c\x0c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x2c\x10\x00\x00\x00\x00\x00\x00\x2b\x8b\x00\x00\x28\x7e\x00\x00\x27\x75\x00\x00\x27\x73\x00\x00\x28\x03\x00\x00\x29\x89\x00\x00\x00\x05\x00\x00\x27\x10\x00\x22\x00\x00\x00\x28\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x22\x00\x00\x00\x19\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x01\x86\x9f\x00\x00\x01\x2c\x00\x22\x00\x00\x00\x28\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x01\x2c\x00\x00\x00\x00\x00\x22\x00\x00\x00\x50\x00\x00\x00\x0a\x00\x00\x00\x0a\x00\x00\x00\x02\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\xff\xff\xff\x38\x00\x22\x00\x00\x00\x50\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x05\x00\x00\x00\x01\xff\xff\xff\x38\xff\xfe\x79\x61\x04\x00\x12\x00\x00\x00\x08\x00\x00\x1f\x40\x00\x00\x03\x20\x00\x00\x00\x32\x00\x12\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x00\x00\x12\x00\x00\x00\x01\x00\x00\x13\x88\x00\x00\x00\xc8\xff\xff\xff\x38\x00\x12\x00\x00\x00\x02\x00\x00\x00\x00\xff\xff\xff\xce\xff\xfe\x79\x61\x00\x00\x00\x00\x01\x6e\x00\x00\x00\x04\xd2\xc1\xc1\xab\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb1\xa9\xb7\xe7\xcc\xec\xca\xb9\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x46\x00\x00\x27\x22\x00\x00\x2c\x13\x00\x00\x27\x77\x00\x00\x27\x36\x00\x00\x2c\x11\x00\x00\x2c\x14\x00\x00\x2c\x12\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x2c\x15\x00\x00\x00\x00\x00\x00\x2b\xe6\x00\x00\x27\x2d\x00\x00\x27\x75\x00\x00\x27\x73\x00\x00\x29\xb8\x00\x00\x28\xbc\x00\x00\x00\x05\x00\x00\x27\x10\x00\x22\x00\x00\x00\x28\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x22\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x01\x86\x9f\x00\x00\x00\xc8\x00\x22\x00\x00\x00\x0f\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x22\x00\x00\x00\x19\x00\x00\x00\x0a\x00\x00\x00\x0a\x00\x00\x00\x02\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\xff\xff\xff\x38\x00\x22\x00\x00\x00\x32\x00\x00\x00\x14\x00\x00\x00\x0a\x00\x00\x00\x03\x00\x00\x00\x05\x00\x00\x00\x01\xff\xff\xff\x38\xff\xfe\x79\x61\x04\x00\x12\x00\x00\x00\x08\x00\x00\x00\x00\x00\x0f\x42\x36\x00\x00\x27\x0f\x00\x12\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x27\x0f\x00\x00\x03\xe7\x00\x12\x00\x00\x00\x01\x00\x00\x0f\xa0\x00\x00\x00\x64\xff\xff\xfe\x0c\x00\x12\x00\x00\x00\x02\x00\x00\x00\x00\xff\xff\xfc\x19\xff\xfe\x79\x61\x00\x00\x00\x00\x01\x6e\x00\x00\x00\x05\xb6\xf7\xd7\xf4\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xb1\xa9\xb7\xe7\xcc\xec\xca\xb9\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x46\x00\x00\x27\x26\x00\x00\x2c\x9d\x00\x00\x28\x53\x00\x00\x00\x00\x00\x00\x2c\x9b\x00\x00\x2c\x9e\x00\x00\x2c\x9c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x2c\x9f\x00\x00\x00\x00\x00\x00\x2b\xe5\x00\x00\x27\x2d\x00\x00\x27\x75\x00\x00\x27\x73\x00\x00\x28\x75\x00\x00\x27\x97\x00\x00\x00\x0a\x00\x00\x27\x10\x00\x22\x00\x00\x00\x28\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x22\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x01\x86\x9f\x00\x00\x00\x32\x00\x22\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x32\x00\x00\x00\x00\x00\x22\x00\x00\x00\x0c\x00\x00\x00\x02\x00\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\xff\xff\xff\x38\x00\x22\x00\x00\x00\x12\x00\x00\x00\x05\x00\x00\x00\x0a\x00\x00\x00\x05\x00\x00\x00\x05\x00\x00\x00\x01\xff\xff\xff\x38\xff\xfe\x79\x61\x04\x00\x12\x00\x00\x00\x08\x00\x00\x00\x00\x00\x0f\x42\x36\x00\x00\x27\x0f\x00\x12\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x27\x0f\x00\x00\x03\xe7\x00\x12\x00\x00\x00\x01\x00\x00\x03\xe8\x00\x00\x00\x64\xff\xff\xff\x9c\x00\x12\x00\x00\x00\x02\x00\x00\x01\xf4\xff\xff\xff\xb0\xff\xff\xd8\xf1\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 1928);
	memcpy(shumiaoyunshu, "\x00\x00\xea\x60\x00\x00\x00\x6e\x00\x00\x00\xc7\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\xdf\x20\xd2\xed\xf9\x00\x02\x00\x02\xbf\x20\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 98);
	//memcpy(chewangkaoyan, "\x00\x00\xea\x60\x00\x00\x00\x48\x00\x00\x00\x71\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x76\x7a\x7b\x38\xf9\x00\x03\x00\x00\xd6\xd8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x07\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",98);
	memcpy(taskaward, "\x00\x00\x27\x11\x00\x13\x00\x00\x00\x46\x01\x00\x00\x00\x00\x00\x00\x00\x0d\x00\x00\x00\x00\x00\x00\x73\x33\x00\x5b\x00\x00\x01\x90\x00\x00\xcd\x69\x00\x00\x00\x00\x00\x0F\xFF\xFF\x00\x00\x64\x00\xFF\xFF\xFF\xFF\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03\xfc\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 127);
	//	memcpy(StartTimerChallenge, "\x00\x00\x1f\x40\x00\x00\x01\x58\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x06\xe0\x68\x45\xe0\x16\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x27\x11\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\xff\xff\xff\xff\x00\x01\x00\x0b\x00\x00\x27\x11\x00\x00\x68\x2e\x00\x00\x00\x00\x00\x32\x00\x00\x00\x01\x64\x69\xa9\xa7\x04\x00\x89\x03\x25\x01\x01\x64\x69\xa9\xa7\x22\x22\x5a\xbe\x01\x01\x91\x8f\x12\xa1\x16\x72\x36\xcb\xae\x15\x41\x6b\x67\x98\x20\x42\xe4\x91\xe9\x21\xe8\xe8\x62\x58\x85\x00\x00\x00\x00\x04\xff\xff\x00\x00\x00\xc8\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7c\xbf\xf1\x01\x00\x1d\x00\x00\x27\x11\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x05\xbe\xaa\xaa\xe3\xcb\x3a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a\x00\x0b\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00\x37\x00\x00\x02\x00\x19\x00\x00\x05\x00\x00\x02\x58\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x0b\xb8\x00\x19\x00\x00\x05\x00\x00\x01\x2c\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x03\xe8\x00\x00\x03\xe8\x00\x3b\x00\x01\x02\x00\x19\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x32\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x1d\x00\x01\x06\x00\x00\x00\x00\x00\x00\x9c\x40\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x27\x10\x00\x00\x00\x00\x00\x0a\x00\x00\x01\x00\x05\x00\x02\x00\x00\x00\x00\x80\x00\x00\x00\x00\x00\x00", 589);
	memcpy(StartTimerChallenge, "\x00\x00\x1f\x40\x00\x00\x01\x58\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x06\xe0\x68\x45\xe0\x16\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x27\x11\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\xff\xff\xff\xff\x00\x01\x00\x0b\x00\x00\x27\x11\x00\x00\x68\x2e\x00\x00\x00\x00\x00\x32\x00\x00\x00\x01\x64\x69\xa9\xa7\x04\x00\x89\x03\x25\x01\x01\x64\x69\xa9\xa7\x22\x22\x5a\xbe\x01\x01\x91\x8f\x12\xa1\x16\x72\x36\xcb\xae\x15\x41\x6b\x67\x98\x20\x42\xe4\x91\xe9\x21\xe8\xe8\x62\x58\x85\x00\x00\x00\x00\x04\xff\xff\x00\x00\x00\xc8\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7c\xbf\xf1\x01\x00\x1d\x00\x00\x27\x11\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x05\xbe\xaa\xaa\xe3\xcb\x3a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a\x00\x0b\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00\x37\x00\x00\x02\x00\x19\x00\x00\x05\x00\x00\x02\x58\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x0b\xb8\x00\x19\x00\x00\x05\x00\x00\x01\x2c\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x03\xe8\x00\x00\x03\xe8\x00\x3b\x00\x01\x02\x00\x19\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x32\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x1d\x00\x01\x06\x00\x00\x00\x00\x00\x00\x9c\x40\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x27\x10\x00\x00\x00\x00\x00\x0a\x00\x00\x01\x00\x05\x00\x02\x00\x00\x00\x00\x80\x00\x00\x00\x00\x00\x00", 589);

	//memcpy(jump24238, "\x00\x00\x27\x11\x00\x00\x00\x03\x00\x00\x00\x31\x00\x00\x00\x03\x00\x00\x27\x10\x00\x00\x79\x00\x00\x00\x00\x0a\x00\x00\x00\x0a\x00\x00\x00\x01\x00\x31\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x10\x02\x4a\x00\x00\x00\x01\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x61\x6c", 91);
	memcpy(StartTimerChallenge2, "\x00\x00\x1f\x40\x00\x00\x01\xb0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x39\x52\xf3\x7d\xac\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x27\x11\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\xa1\x08\x00\x01\x00\x0b\x00\x00\x27\x11\x00\x01\xf2\x80\x00\x00\x00\x01\x0a\x32\x00\x00\x00\x00\x64\x72\x23\xfd\x65\x02\x30\x02\x2e\x01\x01\x64\x72\x23\xfd\x12\x8c\x17\x3e\x01\x01\x70\x4e\x24\xae\x45\xd2\xb2\xfa\x36\x7b\x4b\xe7\xe1\x03\x1e\xa8\x8e\xa1\xa6\x39\xfe\x08\xb3\x92\xe3\x00\x00\x00\x00\x04\xff\xff\x00\x00\x00\xc8\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x14\xaf\x5d\x01\x00\x35\x00\x00\x27\x11\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x01\x03\x00\x08\x00\xcc\x00\xcc\x00\x01\x00\x08\x00\xcf\x00\xcf\x00\x01\x00\x08\x00\xd2\x00\xd2\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x0a\xaa\x1d\xb2\x76\x6c\x86\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x2e\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\xcb\x00\x20\x00\x00\x00\x01\x00\x16\x00\x00\x27\x11\x00\x00\x00\x03\x00\x00\x00\x0d\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x27\x11\x00\x01\x00\x00\x00\x01\x03\x00\x08\x00\xcc\x00\x00\x00\x00\x00\x08\x00\xcf\x00\x00\x00\x00\x00\x08\x00\xd2\x00\x00\x00\x00\x00\x02\x00\x84\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x08\xc1\xd2\xd1\xe6\xb7\xc9\xd0\xd0\x00\x42\xc1\xf7\xbb\xf0\xc1\xfa\xbe\xa7\xb8\xb3\xd3\xe8\xc8\xfc\xb3\xb5\xb7\xc9\xd0\xd0\xb5\xc4\xc4\xdc\xc1\xa6\xa3\xac\xb3\xd6\xd0\xf8\xca\xb1\xbc\xe4\x35\xc3\xeb\xa3\xac\xc0\xe4\xc8\xb4\xca\xb1\xbc\xe4\x31\x35\xc3\xeb\xa3\xac\xca\xb9\xd3\xc3\xb4\xce\xca\xfd\x31\xb4\xce\x01\x00\x00\x00\x00\x00\x00\x00\x09\x00\x00\x00\x00\x00\x00\x00\x00\x13\x88\x00\x00\x00\x01\x63\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00\x37\x00\x00\x02\x00\x19\x00\x00\x05\x00\x00\x02\x58\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x0b\xb8\x00\x19\x00\x00\x05\x00\x00\x01\x2c\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x03\xe8\x00\x00\x03\xe8\x00\x3b\x00\x01\x02\x00\x19\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x32\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x1d\x00\x01\x06\x00\x00\x00\x00\x00\x00\x9c\x40\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x27\x10\x00\x00\x00\x00\x00\x0a\x00\x00\x01\x00\x05\x00\x02\x00\x00\x00\x07\xe0\x00\x00\x00\x00\x00\x00", 816);

	//memcpy(StartTimerChallenge2, "\x00\x00\x1f\x40\x00\x00\x01\xaf\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x39\x52\xf3\x7d\xac\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x27\x11\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\xa1\x08\x00\x01\x00\x0b\x00\x00\x27\x11\x00\x00\x68\x2e\x00\x00\x00\x01\x0a\x32\x00\x00\x00\x00\x64\x72\x23\xfd\x65\x02\x30\x02\x2e\x01\x01\x64\x72\x23\xfd\x12\x8c\x17\x3e\x01\x01\x70\x4e\x24\xae\x45\xd2\xb2\xfa\x36\x7b\x4b\xe7\xe1\x03\x1e\xa8\x8e\xa1\xa6\x39\xfe\x08\xb3\x92\xe3\x00\x00\x00\x00\x04\xff\xff\x00\x00\x00\xc8\x00\x00\x00\xc8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x14\xaf\x5d\x01\x00\x35\x00\x00\x27\x11\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x01\x03\x00\x08\x00\xcc\x00\xcc\x00\x01\x00\x08\x00\xcf\x00\xcf\x00\x01\x00\x08\x00\xd2\x00\xd2\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x0a\xaa\x1d\xb2\x76\x6c\x86\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x2e\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\xcb\x00\x20\x00\x00\x00\x01\x00\x16\x00\x00\x27\x11\x00\x00\x00\x03\x00\x00\x00\x0d\x00\x00\x00\x00\x00\x00\x00\x10\x00\x00\x00\x03\x00\x00\x27\x11\x00\x01\x00\x00\x00\x01\x03\x00\x08\x00\xcc\x00\x00\x00\x00\x00\x08\x00\xcf\x00\x00\x00\x00\x00\x08\x00\xd2\x00\x00\x00\x00\x00\x02\x00\x84\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x08\xc1\xd2\xd1\xe6\xb7\xc9\xd0\xd0\x00\x42\xc1\xf7\xbb\xf0\xc1\xfa\xbe\xa7\xb8\xb3\xd3\xe8\xc8\xfc\xb3\xb5\xb7\xc9\xd0\xd0\xb5\xc4\xc4\xdc\xc1\xa6\xa3\xac\xb3\xd6\xd0\xf8\xca\xb1\xbc\xe4\x35\xc3\xeb\xa3\xac\xc0\xe4\xc8\xb4\xca\xb1\xbc\xe4\x31\x35\xc3\xeb\xa3\xac\xca\xb9\xd3\xc3\xb4\xce\xca\xfd\x31\xb4\xce\x01\x00\x00\x00\x00\x00\x00\x00\x09\x00\x00\x00\x00\x00\x00\x00\x00\x13\x88\x00\x00\x00\x01\x63\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03\x00\x37\x00\x00\x02\x00\x19\x00\x00\x05\x00\x00\x02\x58\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x0b\xb8\x00\x19\x00\x00\x05\x00\x00\x01\x2c\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x03\xe8\x00\x00\x03\xe8\x00\x3b\x00\x01\x02\x00\x19\x00\x00\x05\x00\x00\x00\x00\x00\x00\x00\x32\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x00\x00\x00\x1d\x00\x01\x06\x00\x00\x00\x00\x00\x00\x9c\x40\xff\xff\xff\xff\xff\xff\xff\xff\x00\x00\x27\x10\x00\x00\x00\x00\x00\x0a\x00\x00\x01\x00\x05\x00\x02\x00\x00\x00\x07\xe0\x00\x00\x00\x00\x00\x00", 816);
	//4????? 
	//????00 00 1b 58 ??00 00 13 88 ?????00 00 27 10 ????03

	memcpy(jump24255, "\x00\x00\x00\x03\x00\x00\x00\x77\x00\xFF\xFF\xFF\x00\x01\x0f\xe3\x00\x01\x0f\xe3\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x08\x00\x00\x00\x00\x00\x00\x61\x6c", 91);

	memcpy(BorderOver, "\x00\xb1\x00\x00\x27\x11\x00\x02\xa2\x93\x00\x00\x00\x01\x00\x00\x00\x00\x00\x03\x02\x64\x00\x00\x01\xf4\x00\x00\x26\x56\x00\x00\x00\x64\x00\x00\x3d\x36\x00\x00\x00\x64\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x06\x00\x00\x00\x0f\x00\x00\x00\x06\x00\x00\x00\x00\x00\x00\x00\x0f\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x20\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x30\x17\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x01\x01\x0f\xa0\x00\x00\x06\x00\x00\x01\x01\x00\x00\x00\x00\x00\x00\x00\x42\x68\x00\x04\x00\x00\x00\x00", 187);
	memcpy(RankedOver, "\x01\x01\x79\x00\x00\x27\x11\x00\x01\xac\x54\x00\x00\x00\x00\x00\x00\x01\x90\x00\x01\x01\x70\x00\x00\x00\x9c\x00\x03\x14\x17\x02\x00\x00\x00\x05\x00\x00\x00\x07\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x90\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x26\x00\x00\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\xbb\x00\x16\x00\x00\x00\x16\x00\x00\x00\x04\x00\x00\x00\x13\x00\x00\x00\x28\x00\x00\x00\xa9\x00\x16\x00\x00\x00\x16\x00\x00\x00\x04\x00\x00\x00\x1d\x00\x00\x00\x28\x00\x00\x00\xb3\x00\x24\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x68\x00\x00\x00\x04\x00\x00\x00\x01\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x0a\x00\x00\x00\x17\x00\x00\x00\x04\x00\x00\x00\x06\x00\x00\x00\x26\x00\x00\x01\x2c\x00\x00\x00\x05\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x00\x20\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x30\x17\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04\x00\x01\x01\x00\x00\x00\x06\x00\x00\x01\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x59\xd8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 402);
	memcpy(msg900, (BYTE*)"\x00\x00\x00\x00\x27\x11\x29\xc4\xe3\xb5\xc4\xd3\xc3\xbb\xa7\xd2\xd1\xbe\xad\xd4\xda\xc1\xed\xd2\xbb\xb8\xf6\xc9\xe8\xb1\xb8\xb5\xc7\xc2\xbc\x2c\xc4\xfa\xb1\xbb\xc6\xc8\xcf\xc2\xcf\xdf\xa3\xa1\x00\x00\x00\x00", 52);

}


static void PlayerDB_ExecDDL(const char* sql)
{
	try
	{
		DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->execute();
		prep_stmt.reset();
	}
	catch (const std::exception&)
	{
	}
}

void ActivityDB_ExecDDLBestEffort(const char* sqlText)
{
	std::string ignoredError;
	ActivityDB_ExecDDL(sqlText, ignoredError);
}

static std::atomic<bool> g_PlayerDBSpeedCoinReady{ false };
static std::atomic<bool> g_PlayerDBRoomUGCReady{ false };

static bool PlayerDB_EnsureRoomUGCTables()
{
	g_PlayerDBRoomUGCReady.store(false, std::memory_order_release);
	try
	{
		DBConnection database(player);
		auto* connection = database.getConnection();
		if (!connection)
		{
			printf("[DATABASE][RoomUGC] player database connection unavailable\n");
			return false;
		}
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"CREATE TABLE IF NOT EXISTS RoomUGCOwnedWork ("
				"Uin INT UNSIGNED NOT NULL,"
				"WorkID BIGINT UNSIGNED NOT NULL,"
				"CurrencyItemID INT UNSIGNED NOT NULL,"
				"PaidPrice INT UNSIGNED NOT NULL,"
				"PurchaseTime INT UNSIGNED NOT NULL,"
				"PRIMARY KEY (Uin,WorkID)"
				") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"));
		statement->execute();
		statement.reset(connection->prepareStatement(
			"CREATE TABLE IF NOT EXISTS RoomUGCPlayerState ("
			"Uin INT UNSIGNED NOT NULL,"
			"UsingWorkID BIGINT UNSIGNED NOT NULL,"
			"PRIMARY KEY (Uin)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"));
		statement->execute();
		g_PlayerDBRoomUGCReady.store(true, std::memory_order_release);
		printf("[DATABASE][RoomUGC] ownership/equip schema ready\n");
		return true;
	}
	catch (const std::exception& exception)
	{
		printf("[DATABASE][RoomUGC] ownership/equip schema failed: %s\n",
			exception.what());
		return false;
	}
}

static bool PlayerDB_EnsurePetPKFormationTable()
{
	try
	{
		DBConnection database(player);
		auto* connection = database.getConnection();
		if (!connection)
		{
			printf("[DATABASE][PetPK] player database connection unavailable\n");
			return false;
		}
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"CREATE TABLE IF NOT EXISTS PetPKFormation ("
				"Uin INT UNSIGNED NOT NULL,"
				"IDsHex CHAR(80) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,"
				"CaptainPetID INT UNSIGNED NOT NULL,"
				"UsingArrayID TINYINT UNSIGNED NOT NULL,"
				"PRIMARY KEY (Uin)"
				") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"));
		statement->execute();
		printf("[DATABASE][PetPK] formation schema ready\n");
		return true;
	}
	catch (const std::exception& exception)
	{
		printf("[DATABASE][PetPK] formation schema failed: %s\n", exception.what());
		return false;
	}
}

static bool PlayerDB_EnsureBaseInfoSpeedCoinField()
{
	g_PlayerDBSpeedCoinReady.store(false, std::memory_order_release);
	try
	{
		DBConnection database(player);
		auto* connection = database.getConnection();
		if (!connection)
		{
			printf("[DATABASE][SpeedCoin] player database connection unavailable\n");
			return false;
		}

		auto inspectSchema = [&connection]() {
			std::pair<bool, bool> schema{ false, false };
			std::unique_ptr<sql::PreparedStatement> query(
				connection->prepareStatement(
					"SELECT DATA_TYPE,COLUMN_TYPE,IS_NULLABLE,COLUMN_DEFAULT "
					"FROM INFORMATION_SCHEMA.COLUMNS "
					"WHERE TABLE_SCHEMA=DATABASE() AND TABLE_NAME='BaseInfo' "
					"AND COLUMN_NAME='SpeedCoin' LIMIT 1;"));
			std::unique_ptr<sql::ResultSet> result(query->executeQuery());
			if (!result->next())
				return schema;
			schema.first = true;
			std::string columnType = result->getString("COLUMN_TYPE");
			std::transform(
				columnType.begin(), columnType.end(), columnType.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				});
			schema.second =
				result->getString("DATA_TYPE") == "int" &&
				columnType.find("unsigned") != std::string::npos &&
				result->getString("IS_NULLABLE") == "NO" &&
				result->getString("COLUMN_DEFAULT") == "0";
			return schema;
		};

		std::pair<bool, bool> schema = inspectSchema();
		if (!schema.first)
		{
			std::unique_ptr<sql::PreparedStatement> addColumn(
				connection->prepareStatement(
					"ALTER TABLE BaseInfo ADD COLUMN "
					"SpeedCoin INT UNSIGNED NOT NULL DEFAULT 0;"));
			addColumn->execute();
		}
		else if (!schema.second)
		{
			std::unique_ptr<sql::PreparedStatement> negativeQuery(
				connection->prepareStatement(
					"SELECT COUNT(*) AS NegativeCount "
					"FROM BaseInfo WHERE SpeedCoin<0;"));
			std::unique_ptr<sql::ResultSet> negativeResult(
				negativeQuery->executeQuery());
			if (!negativeResult->next())
				return false;
			const UINT negativeCount =
				negativeResult->getUInt("NegativeCount");
			if (negativeCount != 0)
			{
				printf(
					"[DATABASE][SpeedCoin] migration rejected: "
					"%u negative balance row(s)\n",
					negativeCount);
				return false;
			}

			std::unique_ptr<sql::PreparedStatement> normalizeNull(
				connection->prepareStatement(
					"UPDATE BaseInfo SET SpeedCoin=0 WHERE SpeedCoin IS NULL;"));
			normalizeNull->executeUpdate();

			std::unique_ptr<sql::PreparedStatement> normalizeSchema(
				connection->prepareStatement(
					"ALTER TABLE BaseInfo MODIFY COLUMN "
					"SpeedCoin INT UNSIGNED NOT NULL DEFAULT 0;"));
			normalizeSchema->execute();
		}

		schema = inspectSchema();
		const bool schemaValid = schema.first && schema.second;
		g_PlayerDBSpeedCoinReady.store(schemaValid, std::memory_order_release);
		printf(
			"[DATABASE][SpeedCoin] schema %s (BaseInfo.SpeedCoin INT UNSIGNED NOT NULL DEFAULT 0)\n",
			schemaValid ? "ready" : "invalid");
		return schemaValid;
	}
	catch (const std::exception& exception)
	{
		printf("[DATABASE][SpeedCoin] schema migration failed: %s\n", exception.what());
		return false;
	}
}

bool PlayerDB_IsSpeedCoinReady()
{
	return g_PlayerDBSpeedCoinReady.load(std::memory_order_acquire);
}

static void PlayerDB_EnsureBaseInfoClientSavedFlagField()
{
	PlayerDB_ExecDDL("ALTER TABLE BaseInfo ADD COLUMN ClientSavedFlagHex VARCHAR(64) NOT NULL DEFAULT '';");
}

static void PlayerDB_EnsureBaseInfoExRightFlagField()
{
	PlayerDB_ExecDDL("ALTER TABLE BaseInfo ADD COLUMN ExRightFlagHex VARCHAR(48) NOT NULL DEFAULT '';");
}

static void PlayerDB_EnsureBaseInfoSpeedLadderRewardFields()
{
	PlayerDB_ExecDDL("ALTER TABLE BaseInfo ADD COLUMN SpeedLadderRewardSeasonID INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BaseInfo ADD COLUMN SpeedLadderRewardClaimHex VARCHAR(128) NOT NULL DEFAULT '';");
}

static int QQS_HexCharValue(char ch)
{
	if (ch >= '0' && ch <= '9') return ch - '0';
	if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
	return -1;
}

static std::string QQS_ClientSavedFlagsToHex(const BYTE* Flags, size_t FlagLen)
{
	static const char* kHex = "0123456789ABCDEF";
	std::string out;
	if (!Flags || FlagLen == 0)
		return out;
	out.reserve(FlagLen * 2);
	for (size_t i = 0; i < FlagLen; ++i)
	{
		out.push_back(kHex[(Flags[i] >> 4) & 0x0F]);
		out.push_back(kHex[Flags[i] & 0x0F]);
	}
	return out;
}

static void QQS_ClientSavedFlagsFromHex(const std::string& Hex, BYTE* Flags, size_t FlagLen)
{
	if (!Flags || FlagLen == 0)
		return;
	memset(Flags, 0, FlagLen);
	const size_t hexPairs = Hex.size() / 2;
	const size_t pairs = (FlagLen < hexPairs) ? FlagLen : hexPairs;
	for (size_t i = 0; i < pairs; ++i)
	{
		const int hi = QQS_HexCharValue(Hex[i * 2]);
		const int lo = QQS_HexCharValue(Hex[i * 2 + 1]);
		if (hi < 0 || lo < 0)
			break;
		Flags[i] = (BYTE)((hi << 4) | lo);
	}
}

static void PlayerDB_EnsureCarTreasureTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS RetComItemTable ("
		"Uin INT UNSIGNED NOT NULL,"
		"ItemId INT UNSIGNED NOT NULL,"
		"Aviable INT NOT NULL DEFAULT 0,"
		"OpNum INT UNSIGNED NOT NULL DEFAULT 1,"
		"ExchangePieceNum INT UNSIGNED NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ItemId, Aviable)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS CarTreasureSelectedGroup ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL DEFAULT 202605,"
		"Slot TINYINT UNSIGNED NOT NULL,"
		"GroupIndex INT NOT NULL,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId, Slot),"
		"KEY idx_uin_activity (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS SummerCarTreasureState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"LuckValue INT UNSIGNED NOT NULL DEFAULT 0,"
		"CrystalProgress INT UNSIGNED NOT NULL DEFAULT 0,"
		"CrystalGotNum INT UNSIGNED NOT NULL DEFAULT 0,"
		"DiscountUsedNum INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE RetComItemTable ADD COLUMN OpNum INT UNSIGNED NOT NULL DEFAULT 1;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE RetComItemTable ADD COLUMN ExchangePieceNum INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE RetComItemTable MODIFY OpNum INT UNSIGNED NOT NULL DEFAULT 1;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE RetComItemTable ADD COLUMN CreateTime INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE RetComItemTable ADD COLUMN UpdateTime INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE RetComItemTable MODIFY Uin INT UNSIGNED NOT NULL, MODIFY ItemId INT UNSIGNED NOT NULL, MODIFY Aviable INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE RetComItemTable ADD PRIMARY KEY (Uin, ItemId, Aviable);");
}

void PlayerDB_EnsureFourBeautiesTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS FourBeautiesState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"CurDraw INT UNSIGNED NOT NULL DEFAULT 0,"
		"DrawItemCnt INT UNSIGNED NOT NULL DEFAULT 0,"
		"NextBuff INT UNSIGNED NOT NULL DEFAULT 5,"
		"BestAwardFlag INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS FourBeautiesTempItem ("
		"Id INT UNSIGNED NOT NULL AUTO_INCREMENT,"
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"ItemID INT UNSIGNED NOT NULL,"
		"ItemNum INT UNSIGNED NOT NULL DEFAULT 1,"
		"ItemType INT UNSIGNED NOT NULL DEFAULT 1,"
		"AvailPeriod INT NOT NULL DEFAULT -1,"
		"FenjieNum INT UNSIGNED NOT NULL DEFAULT 0,"
		"Status INT UNSIGNED NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Id),"
		"KEY idx_uin_activity_status (Uin, ActivityId, Status),"
		"KEY idx_uin_item (Uin, ActivityId, ItemID)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS FourBeautiesGotItem ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"ItemID INT UNSIGNED NOT NULL,"
		"GotTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId, ItemID)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS FourBeautiesBuff ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"BuffIdx INT UNSIGNED NOT NULL,"
		"`Count` INT UNSIGNED NOT NULL DEFAULT 0,"
		"UsedCount INT UNSIGNED NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId, BuffIdx),"
		"KEY idx_uin_activity (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
}


void PlayerDB_EnsureGodWarBoxTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS GodWarBoxState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"CurrentMulAwardIndex INT NOT NULL DEFAULT 0,"
		"GodWarBoxReceiveStatus INT NOT NULL DEFAULT 0,"
		"TreasureAwardHasGotStatus INT NOT NULL DEFAULT 0,"
		"GodWarBoxTriggerNum INT NOT NULL DEFAULT 0,"
		"TreasureAwardNum INT NOT NULL DEFAULT 0,"
		"DrawCount INT NOT NULL DEFAULT 0,"
		"CurrRoundNum INT NOT NULL DEFAULT 1,"
		"CurrCostItemNum INT NOT NULL DEFAULT 1,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS GodWarBoxTempItem ("
		"Id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"ItemIndex INT NOT NULL DEFAULT 0,"
		"ItemID INT UNSIGNED NOT NULL,"
		"ItemNum INT NOT NULL DEFAULT 1,"
		"ItemType INT NOT NULL DEFAULT 0,"
		"AvailPeriod INT NOT NULL DEFAULT 0,"
		"FenjieNum INT NOT NULL DEFAULT 0,"
		"Status TINYINT NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Id),"
		"KEY idx_GodWarBoxTempItem_UinActivity (Uin, ActivityId, Status),"
		"KEY idx_GodWarBoxTempItem_Item (Uin, ActivityId, ItemID)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS GodWarBoxDrawLog ("
		"Id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"RoundNum INT NOT NULL DEFAULT 0,"
		"AwardIndex INT NOT NULL DEFAULT 0,"
		"ItemID INT UNSIGNED NOT NULL DEFAULT 0,"
		"ItemNum INT NOT NULL DEFAULT 0,"
		"CostItemId INT UNSIGNED NOT NULL DEFAULT 0,"
		"CostItemNum INT NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Id),"
		"KEY idx_GodWarBoxDrawLog_UinActivity (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");

	ActivityDB_ExecDDLBestEffort("ALTER TABLE GodWarBoxState ADD COLUMN DrawCount INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE GodWarBoxTempItem ADD COLUMN ItemIndex INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE GodWarBoxTempItem ADD COLUMN FenjieNum INT NOT NULL DEFAULT 0;");
}


void PlayerDB_EnsureSupremeTreasureTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS SupremeTreasureState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"DrawNum INT NOT NULL DEFAULT 0,"
		"LuckScore INT NOT NULL DEFAULT 0,"
		"ScoreDrawDayCount INT NOT NULL DEFAULT 0,"
		"ScoreDrawWeekCount INT NOT NULL DEFAULT 0,"
		"AccuClaimMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
}

void PlayerDB_EnsureSecretCompassTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS SecretCompassState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"PieceScore INT NOT NULL DEFAULT 0,"
		"DrawCount INT NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
}

void PlayerDB_EnsureSpeedPassTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS SpeedPassState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"CurLevel INT NOT NULL DEFAULT 1,"
		"CurExp INT NOT NULL DEFAULT 0,"
		"PremiumUnlocked TINYINT NOT NULL DEFAULT 0,"
		"CollectionUnlocked TINYINT NOT NULL DEFAULT 0,"
		"WeekTaskCurScore INT NOT NULL DEFAULT 0,"
		"LastWeeklyReset INT NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS SpeedPassClaim ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"Tier VARCHAR(24) NOT NULL,"
		"Level INT NOT NULL,"
		"ClaimTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId, Tier, Level)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS SpeedPassTaskState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"TaskId INT NOT NULL,"
		"PeriodKey VARCHAR(32) NOT NULL,"
		"Progress INT NOT NULL DEFAULT 0,"
		"AwardCount INT NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId, TaskId, PeriodKey)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS SpeedPassExchangeState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"ItemId INT UNSIGNED NOT NULL,"
		"PeriodKey VARCHAR(32) NOT NULL DEFAULT 'season',"
		"ExchangeCount INT NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId, ItemId, PeriodKey)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE SpeedPassState ADD COLUMN PremiumUnlocked TINYINT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE SpeedPassState ADD COLUMN CollectionUnlocked TINYINT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE SpeedPassState ADD COLUMN WeekTaskCurScore INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE SpeedPassState ADD COLUMN LastWeeklyReset INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE SpeedPassTaskState ADD COLUMN AwardCount INT NOT NULL DEFAULT 0;");
}

static void ActivityDB_EnsureSpeedLadderTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS SpeedLadderPlayerMap ("
		"Uin INT UNSIGNED NOT NULL,"
		"SeasonID INT UNSIGNED NOT NULL DEFAULT 1,"
		"MapID INT UNSIGNED NOT NULL,"
		"BestTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"TrophyLevel TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"TrophyCount TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"Star1Done TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"Star2Done TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"Star3Done TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"AssessDone TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"RecordSource TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"ReplayMd5 VARCHAR(64) NOT NULL DEFAULT '',"
		"ReplayPath VARCHAR(255) NOT NULL DEFAULT '',"
		"UpdatedAt INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, SeasonID, MapID, RecordSource),"
		"KEY idx_season_map (SeasonID, MapID)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE SpeedLadderPlayerMap ADD COLUMN RecordSource TINYINT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE SpeedLadderPlayerMap DROP PRIMARY KEY, ADD PRIMARY KEY (Uin, SeasonID, MapID, RecordSource);");
	ActivityDB_ExecDDLBestEffort("CREATE INDEX idx_speed_ladder_player_source ON SpeedLadderPlayerMap (Uin, SeasonID, RecordSource);");

}

static void PlayerDB_EnsureRaceChallengeTables()
{
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN AverageSpeed INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN JetTime INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN ChaoJiQiBu INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN ZhuangQiangDianPen INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN SmallJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN DoubleJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN FlyJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN FallJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN DuanWeiQiBu INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN DuanWeiDoubleJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN LinkJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN WCJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN CeShenDriftCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN ShuaiWeiDriftCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN CWWJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN XiaoGuaCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN TeamID INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN FastCornersNum INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN DiffCornerNum INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN FastCornerDetailInfo INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN WSingleJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN WCWJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN CWWJetCount1 INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN CeShenJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN ShuaiWeiJetCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN RankedMatchGrade INT NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN DuanWeiPiaoYi INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN DuanWeiJet INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN OutOfTurnQuickly INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN LaunchCount INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN HitWaterDrift INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN OutWaterJet INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN N2DriftContinue INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN WaveFlyJet INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN WorldID INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN Npc INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN GameSeq INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN DetailTime INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE BattleDetails ADD COLUMN RMTopRaceScore INT UNSIGNED NOT NULL DEFAULT 0;");

}

void PlayerDB_EnsureTarotTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS TarotState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"DrawCount INT NOT NULL DEFAULT 0,"
		"CurDraw INT NOT NULL DEFAULT 0,"
		"CurScore INT NOT NULL DEFAULT 0,"
		"MaxScore INT NOT NULL DEFAULT 0,"
		"DrawItemCnt INT NOT NULL DEFAULT 0,"
		"AccuClaimMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"Page1CardMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"Page2CardMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"Page3CardMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"Page4CardMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS TarotTempItem ("
		"Id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"AwardIdx INT NOT NULL DEFAULT 0,"
		"ItemID INT UNSIGNED NOT NULL,"
		"ItemNum INT NOT NULL DEFAULT 1,"
		"ItemType INT NOT NULL DEFAULT 0,"
		"AvailPeriod INT NOT NULL DEFAULT 0,"
		"FenjieNum INT NOT NULL DEFAULT 0,"
		"GetNum INT NOT NULL DEFAULT 0,"
		"Status TINYINT NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Id),"
		"KEY idx_TarotTempItem_UinActivity (Uin, ActivityId, Status),"
		"KEY idx_TarotTempItem_Item (Uin, ActivityId, ItemID)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS TarotDrawLog ("
		"Id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"RoundNum INT NOT NULL DEFAULT 0,"
		"Page INT NOT NULL DEFAULT 1,"
		"CardIdx INT NOT NULL DEFAULT 0,"
		"ItemID INT UNSIGNED NOT NULL DEFAULT 0,"
		"ItemNum INT NOT NULL DEFAULT 0,"
		"ItemType INT NOT NULL DEFAULT 0,"
		"AvailPeriod INT NOT NULL DEFAULT 0,"
		"CostItemID INT UNSIGNED NOT NULL DEFAULT 0,"
		"CostItemNum INT NOT NULL DEFAULT 0,"
		"ZancunFlag INT NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Id),"
		"KEY idx_TarotDrawLog_UinActivity (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotState ADD COLUMN CurDraw INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotState ADD COLUMN CurScore INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotState ADD COLUMN MaxScore INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotState ADD COLUMN DrawItemCnt INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotState ADD COLUMN AccuClaimMask INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotState ADD COLUMN Page1CardMask INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotState ADD COLUMN Page2CardMask INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotState ADD COLUMN Page3CardMask INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotState ADD COLUMN Page4CardMask INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotTempItem ADD COLUMN AwardIdx INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotTempItem ADD COLUMN FenjieNum INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE TarotTempItem ADD COLUMN GetNum INT NOT NULL DEFAULT 0;");
}


void PlayerDB_EnsureFinalMagicHouseTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS FinalMagicHouseSelectedGroup ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"Slot TINYINT UNSIGNED NOT NULL,"
		"GroupIndex INT NOT NULL,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId, Slot),"
		"KEY idx_FMHSelected_UinActivity (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS FinalMagicHouseState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"DrawCount INT NOT NULL DEFAULT 0,"
		"CurrentRound INT NOT NULL DEFAULT 1,"
		"GotItemData TEXT NULL,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE FinalMagicHouseState ADD COLUMN CurrentRound INT NOT NULL DEFAULT 1;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE FinalMagicHouseState ADD COLUMN GotItemData TEXT NULL;");
}


void PlayerDB_EnsureHuimengDreamTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS HuimengDreamState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"DrawCount INT NOT NULL DEFAULT 0,"
		"GuaranteeCount INT NOT NULL DEFAULT 0,"
		"ExchangeData TEXT NULL,"
		"GiftData TEXT NULL,"
		"ExtraData TEXT NULL,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS HuimengDreamTempItem ("
		"Id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"AwardIdx INT NOT NULL DEFAULT 0,"
		"ItemID INT UNSIGNED NOT NULL,"
		"ItemNum INT NOT NULL DEFAULT 1,"
		"ItemType INT NOT NULL DEFAULT 0,"
		"AvailPeriod INT NOT NULL DEFAULT 0,"
		"FenjieNum INT NOT NULL DEFAULT 0,"
		"Status TINYINT NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Id),"
		"KEY idx_HuimengDreamTempItem_UinActivity (Uin, ActivityId, Status),"
		"KEY idx_HuimengDreamTempItem_Item (Uin, ActivityId, ItemID)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE HuimengDreamState ADD COLUMN GuaranteeCount INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE HuimengDreamState ADD COLUMN ExchangeData TEXT NULL;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE HuimengDreamState ADD COLUMN GiftData TEXT NULL;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE HuimengDreamState ADD COLUMN ExtraData TEXT NULL;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE HuimengDreamTempItem ADD COLUMN AwardIdx INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE HuimengDreamTempItem ADD COLUMN FenjieNum INT NOT NULL DEFAULT 0;");
}



void PlayerDB_EnsureZhiYaoGodsTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS ZhiYaoGodsState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"DrawCount INT NOT NULL DEFAULT 0,"
		"AccuScore INT NOT NULL DEFAULT 0,"
		"ClaimMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
}

void PlayerDB_EnsureGodHorseTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS GodHorseState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityType INT NOT NULL,"
		"DrawCount INT NOT NULL DEFAULT 0,"
		"ClaimMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityType)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS GodHorseExchange ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityType INT NOT NULL,"
		"AwardIdx INT NOT NULL,"
		"ExchangeCount INT NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityType, AwardIdx),"
		"KEY idx_GodHorseExchange_UinActivity (Uin, ActivityType)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE GodHorseState ADD COLUMN ClaimMask INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE GodHorseState ADD COLUMN UpdateTime INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE GodHorseExchange ADD COLUMN ExchangeCount INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE GodHorseExchange ADD COLUMN UpdateTime INT UNSIGNED NOT NULL DEFAULT 0;");
}

void PlayerDB_EnsureWastelandTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS WastelandState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityType INT NOT NULL,"
		"DrawCount INT NOT NULL DEFAULT 0,"
		"ClaimMask INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityType)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS WastelandTempItem ("
		"Id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,"
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityType INT NOT NULL,"
		"AwardIdx INT NOT NULL DEFAULT 0,"
		"ItemID INT UNSIGNED NOT NULL,"
		"ItemNum INT NOT NULL DEFAULT 1,"
		"ItemType INT NOT NULL DEFAULT 0,"
		"AvailPeriod INT NOT NULL DEFAULT 0,"
		"FenjieNum INT NOT NULL DEFAULT 0,"
		"Status TINYINT UNSIGNED NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Id),"
		"KEY idx_WastelandTempItem_UinActivity (Uin, ActivityType, Status),"
		"KEY idx_WastelandTempItem_Item (Uin, ActivityType, ItemID)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS WastelandExchange ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityType INT NOT NULL,"
		"AwardIdx INT NOT NULL,"
		"ExchangeCount INT NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityType, AwardIdx),"
		"KEY idx_WastelandExchange_UinActivity (Uin, ActivityType)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE WastelandState ADD COLUMN ClaimMask INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE WastelandState ADD COLUMN UpdateTime INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE WastelandTempItem ADD COLUMN FenjieNum INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE WastelandTempItem ADD COLUMN UpdateTime INT UNSIGNED NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE WastelandExchange ADD COLUMN ExchangeCount INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE WastelandExchange ADD COLUMN UpdateTime INT UNSIGNED NOT NULL DEFAULT 0;");
}


void PlayerDB_EnsureCharmVipTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS CharmVipClaimState ("
		"Uin INT UNSIGNED NOT NULL,"
		"ActivityId INT UNSIGNED NOT NULL,"
		"ClaimKey VARCHAR(64) NOT NULL,"
		"Opt INT NOT NULL DEFAULT 0,"
		"VipLevel INT NOT NULL DEFAULT 0,"
		"CreateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, ActivityId, ClaimKey),"
		"KEY idx_CharmVipClaim_UinActivity (Uin, ActivityId)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS CharmVipPlayerState ("
		"Uin INT UNSIGNED NOT NULL,"
		"CurVipLevel INT NOT NULL DEFAULT 1,"
		"BaseVipLevel INT NOT NULL DEFAULT 1,"
		"AccuVipLevel INT NOT NULL DEFAULT 1,"
		"CharmValueOfMonth INT NOT NULL DEFAULT 0,"
		"CharmMonthSum INT NOT NULL DEFAULT 0,"
		"CharmYearSum INT NOT NULL DEFAULT 0,"
		"CharmAccuValue INT NOT NULL DEFAULT 0,"
		"GrowthAccuValue INT NOT NULL DEFAULT 0,"
		"TotalBrightScore INT NOT NULL DEFAULT 0,"
		"CharmGrowthRemainder INT NOT NULL DEFAULT 0,"
		"UpdateTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN CurVipLevel INT NOT NULL DEFAULT 1;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN BaseVipLevel INT NOT NULL DEFAULT 1;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN AccuVipLevel INT NOT NULL DEFAULT 1;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN CharmValueOfMonth INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN CharmMonthSum INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN CharmYearSum INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN CharmAccuValue INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN GrowthAccuValue INT NOT NULL DEFAULT 0;");
	// 兼容旧库：旧实现把 GrowthAccuValue 直接作为璀璨积分下发。用 NULL 只标记
	// 第一次加列时已存在的 V10 记录，避免后续启动把新玩家的 0 分反复回填。
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN TotalBrightScore INT NULL DEFAULT NULL;");
	ActivityDB_ExecDDLBestEffort("UPDATE CharmVipPlayerState SET TotalBrightScore=CASE WHEN GrowthAccuValue>=100000 THEN GrowthAccuValue ELSE 0 END WHERE TotalBrightScore IS NULL;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState MODIFY COLUMN TotalBrightScore INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD INDEX idx_CharmVip_BrightRank (TotalBrightScore, Uin);");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN CharmGrowthRemainder INT NOT NULL DEFAULT 0;");
	ActivityDB_ExecDDLBestEffort("ALTER TABLE CharmVipPlayerState ADD COLUMN UpdateTime INT UNSIGNED NOT NULL DEFAULT 0;");
}

void PlayerDB_EnsureWelfareTables()
{
	ActivityDB_ExecDDLBestEffort("CREATE TABLE IF NOT EXISTS WelfareClaimState ("
		"Uin INT UNSIGNED NOT NULL,"
		"EntryID INT UNSIGNED NOT NULL,"
		"SeasonID INT NOT NULL,"
		"ClaimKind SMALLINT NOT NULL,"
		"ClaimIndex INT NOT NULL,"
		"ClaimTime INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin, EntryID, SeasonID, ClaimKind, ClaimIndex),"
		"KEY idx_WelfareClaim_UinEntry (Uin, EntryID, SeasonID, ClaimKind)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
}

void PlayerDB_EnsureRankSeasonTables()
{
	try
	{
		DBConnection database(player);
		auto* connection = database.getConnection();
		if (!connection)
		{
			printf("[DATABASE][Rank] player database connection unavailable\n");
			return;
		}
		std::unique_ptr<sql::PreparedStatement> statement(connection->prepareStatement(
			"CREATE TABLE IF NOT EXISTS RankSeasonState ("
			"Uin INT UNSIGNED NOT NULL,"
			"SeasonID INT UNSIGNED NOT NULL,"
			"SeasonRaceNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"WinNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"FinishNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"TotalRankIndex INT UNSIGNED NOT NULL DEFAULT 0,"
			"ContinFinishNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"MaxContinFinishNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"MaxContinWinNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"KillAllNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"LastRankIndex INT UNSIGNED NOT NULL DEFAULT 0,"
			"RMTopRaceScore INT UNSIGNED NOT NULL DEFAULT 0,"
			"DailyDate INT UNSIGNED NOT NULL DEFAULT 0,"
			"DailyRaceNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"DailyWinNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"ContinWinNums INT UNSIGNED NOT NULL DEFAULT 0,"
			"DailyTaskClaimMask INT UNSIGNED NOT NULL DEFAULT 0,"
			"BraveryPoints INT UNSIGNED NOT NULL DEFAULT 0,"
			"DailyHonorDefendUsed INT UNSIGNED NOT NULL DEFAULT 0,"
			"MilestoneAwardedCount INT UNSIGNED NOT NULL DEFAULT 0,"
			"DailyFirstWinClaimed INT UNSIGNED NOT NULL DEFAULT 0,"
			"DailyCommonBoxClaimMask INT UNSIGNED NOT NULL DEFAULT 0,"
			"UpdatedAt INT UNSIGNED NOT NULL DEFAULT 0,"
			"PRIMARY KEY (Uin,SeasonID)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"));
		statement->execute();
		printf("[DATABASE][Rank] season state schema ready\n");
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_EnsureRankSeasonTables Error" << std::endl;
	}

	// CREATE TABLE IF NOT EXISTS does not upgrade an existing RankSeasonState table.
	// These best-effort ALTERs are intentionally idempotent for old player databases.
	PlayerDB_ExecDDL("ALTER TABLE RankSeasonState ADD COLUMN WinNums INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE RankSeasonState ADD COLUMN FinishNums INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE RankSeasonState ADD COLUMN TotalRankIndex INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE RankSeasonState ADD COLUMN ContinFinishNums INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE RankSeasonState ADD COLUMN MaxContinFinishNums INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE RankSeasonState ADD COLUMN MaxContinWinNums INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE RankSeasonState ADD COLUMN KillAllNums INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE RankSeasonState ADD COLUMN LastRankIndex INT UNSIGNED NOT NULL DEFAULT 0;");
	PlayerDB_ExecDDL("ALTER TABLE RankSeasonState ADD COLUMN RMTopRaceScore INT UNSIGNED NOT NULL DEFAULT 0;");
	// Existing rows already tracked the current win streak; it is a proven lower
	// bound for the historical maximum when upgrading the schema.
	PlayerDB_ExecDDL("UPDATE RankSeasonState SET MaxContinWinNums=ContinWinNums WHERE MaxContinWinNums<ContinWinNums;");
	PlayerDB_ExecDDL(
		"CREATE TABLE IF NOT EXISTS RankSeasonMapUsage ("
		"Uin INT UNSIGNED NOT NULL,"
		"SeasonID INT UNSIGNED NOT NULL,"
		"MapID INT UNSIGNED NOT NULL,"
		"RaceNums INT UNSIGNED NOT NULL DEFAULT 0,"
		"WinNums INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdatedAt INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin,SeasonID,MapID)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
	PlayerDB_ExecDDL(
		"CREATE TABLE IF NOT EXISTS RankSeasonKartUsage ("
		"Uin INT UNSIGNED NOT NULL,"
		"SeasonID INT UNSIGNED NOT NULL,"
		"KartID INT UNSIGNED NOT NULL,"
		"RaceNums INT UNSIGNED NOT NULL DEFAULT 0,"
		"UpdatedAt INT UNSIGNED NOT NULL DEFAULT 0,"
		"PRIMARY KEY (Uin,SeasonID,KartID)"
		") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
}



void InitDB()
{
	try
	{
		ini::iniReader config;
		int poolsize;
		std::string host;
		std::string user;
		std::string password;
		std::string database;
		const std::string configPath = DatabaseConfigPath();
		bool ret = config.ReadConfig(configPath);
		if (ret == false)
		{
			g_AuroraActivityConfigurationError =
				"mysql.ini could not be read: " + configPath;
			printf("[DATABASE][CONFIG] read failed path=%s\n",
				configPath.c_str());
			return;
		}
		printf("[DATABASE][CONFIG] loaded path=%s\n", configPath.c_str());

		poolsize = config.ReadInt("database_player", "poolsize", 1);
		host = config.ReadString("database_player", "host", "");
		user = config.ReadString("database_player", "user", "");
		password = config.ReadString("database_player", "password", "");
		database = config.ReadString("database_player", "database", "");
		player.initialize(poolsize, host, user, password, database);
		PlayerDB_EnsureBaseInfoSpeedCoinField();
		PlayerDB_EnsureBaseInfoClientSavedFlagField();
		PlayerDB_EnsureBaseInfoExRightFlagField();
		PlayerDB_EnsureBaseInfoSpeedLadderRewardFields();
		PlayerDB_EnsureRaceChallengeTables();
		PlayerDB_EnsureRoomUGCTables();
		PlayerDB_EnsurePetPKFormationTable();
		PlayerDB_EnsureRankSeasonTables();
		PlayerDB_ExecDDL(
			"CREATE TABLE IF NOT EXISTS TreasureServerLimit ("
			"BoxID INT UNSIGNED NOT NULL,"
			"ItemID INT UNSIGNED NOT NULL,"
			"AvailPeriod INT NOT NULL,"
			"GotCount INT UNSIGNED NOT NULL DEFAULT 0,"
			"PRIMARY KEY (BoxID, ItemID, AvailPeriod)"
			") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;");
		PersonalGardenEnsureSchema();

		poolsize = config.ReadInt("database_activity", "poolsize", 0);
		host = config.ReadString("database_activity", "host", "");
		user = config.ReadString("database_activity", "user", "");
		password = config.ReadString("database_activity", "password", "");
		database = config.ReadString("database_activity", "database", "");
		g_ActivityDatabaseSchema = database;
		if (poolsize <= 0 || host.empty() || user.empty() || database.empty())
		{
			std::ostringstream configurationError;
			configurationError
				<< "database_activity missing or invalid in "
				<< configPath
				<< " (poolsize=" << poolsize
				<< ", host=" << (host.empty() ? "missing" : "set")
				<< ", user=" << (user.empty() ? "missing" : "set")
				<< ", database="
				<< (database.empty() ? "missing" : database)
				<< ")";
			g_AuroraActivityConfigurationError =
				configurationError.str();
			g_AuroraActivityDbReady.store(false, std::memory_order_release);
			g_AnnualRacingActivityDbReady.store(false, std::memory_order_release);
			printf("[ACTIVITY][DB] %s\n",
				g_AuroraActivityConfigurationError.c_str());
		}
		else
		{
			activity.initialize(
				static_cast<std::size_t>(poolsize),
				host, user, password, database);

			std::string auroraError;
			bool auroraReady = false;
			if (activity.GetAvailableConnections() > 0)
			{
				PlayerDB_EnsureCarTreasureTables();
				PlayerDB_EnsureGodWarBoxTables();
				PlayerDB_EnsureSupremeTreasureTables();
				PlayerDB_EnsureSecretCompassTables();
				ActivityDB_EnsureSpeedLadderTables();
				PlayerDB_EnsureFinalMagicHouseTables();
				PlayerDB_EnsureHuimengDreamTables();
				PlayerDB_EnsureZhiYaoGodsTables();
				PlayerDB_EnsureGodHorseTables();
				PlayerDB_EnsureWelfareTables();
				PlayerDB_EnsureTarotTables();
				PlayerDB_EnsureFourBeautiesTables();
				PlayerDB_EnsureSpeedPassTables();
				PlayerDB_EnsureWastelandTables();
				PlayerDB_EnsureCharmVipTables();
				auroraReady =
					ActivityDB_LoadRuntimeWorks(auroraError);
				std::string annualRacingError;
				const bool annualRacingReady =
					ActivityDB_InitializeAnnualRacing(annualRacingError);
				printf("[ANNUAL-RACING][DB] activity database ready=%u detail=%s\n",
					annualRacingReady ? 1u : 0u,
					annualRacingError.c_str());
			}
			else
			{
				auroraError = "activity database connection unavailable";
				g_AnnualRacingActivityDbReady.store(
					false, std::memory_order_release);
			}
			g_AuroraActivityDbReady.store(
				auroraReady, std::memory_order_release);
			g_AuroraActivityConfigurationError =
				auroraReady ? "ok" : auroraError;
			printf("[AURORA][DB] activity database ready=%u detail=%s\n",
				auroraReady ? 1u : 0u, auroraError.c_str());
		}

		poolsize = config.ReadInt("database_user", "poolsize", 1);
		host = config.ReadString("database_user", "host", "");
		user = config.ReadString("database_user", "user", "");
		password = config.ReadString("database_user", "password", "");
		database = config.ReadString("database_user", "database", "");
		usr.initialize(poolsize, host, user, password, database);

	}
	catch (const std::exception& e)
	{
		printf(e.what());
		return;
	}
}

void CloseDB()
{
	player.closePool();
	usr.closePool();
	activity.closePool();
}

bool ActivityDB_IsReady(std::string* error)
{
	const bool ready =
		g_AuroraActivityDbReady.load(std::memory_order_acquire);
	if (error)
		*error = ready ? "ok" : g_AuroraActivityConfigurationError;
	return ready;
}

bool ActivityDB_InitializeAnnualRacing(std::string& Error)
{
	bool ready = ActivityDB_EnsureAnnualRacingTables(Error);
	if (ready)
	{
		ready = ActivityDB_MigrateAnnualRacingLegacyCounters(Error);
	}
	g_AnnualRacingActivityDbReady.store(ready, std::memory_order_release);
	return ready;
}

bool ActivityDB_AnnualRacingIsReady()
{
	return g_AnnualRacingActivityDbReady.load(std::memory_order_acquire);
}

bool ActivityDB_AnnualRacingLoadState(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	AnnualRacingStateRow& State)
{
	State = {};
	State.Uin = Uin;
	State.SeasonID = SeasonID;
	State.OpenDate = OpenDate;
	State.BaoDiLeftNum = 2;
	if (!ActivityDB_AnnualRacingIsReady() || Uin == 0)
		return false;
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
			return false;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"SELECT StarLevel,OpenStatus,DrawCount,BaoDiLeftNum,"
				"BaoDiType,SageEggGetNum,StarRewardMask,RowVersion,UpdateTime "
				"FROM AnnualRacingState "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? LIMIT 1;"));
		statement->setUInt(1, Uin);
		statement->setUInt(2, SeasonID);
		statement->setUInt(3, OpenDate);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		if (!result->next())
			return true;
		State.StarLevel = result->getInt("StarLevel");
		State.OpenStatus = result->getInt("OpenStatus");
		State.DrawCount = result->getInt("DrawCount");
		State.BaoDiLeftNum = result->getInt("BaoDiLeftNum");
		State.BaoDiType = result->getInt("BaoDiType");
		State.SageEggGetNum = result->getInt("SageEggGetNum");
		State.StarRewardMask = result->getUInt("StarRewardMask");
		State.RowVersion = result->getUInt("RowVersion");
		State.UpdateTime = result->getUInt("UpdateTime");
		return true;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingLoadState Error" << std::endl;
		return false;
	}
}

bool ActivityDB_AnnualRacingSaveState(const AnnualRacingStateRow& State)
{
	if (!ActivityDB_AnnualRacingIsReady() || State.Uin == 0)
		return false;
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
			return false;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"INSERT INTO AnnualRacingState("
				"Uin,SeasonID,OpenDate,StarLevel,OpenStatus,DrawCount,"
				"BaoDiLeftNum,BaoDiType,SageEggGetNum,StarRewardMask,"
				"RowVersion,UpdateTime) "
				"VALUES(?,?,?,?,?,?,?,?,?,?,1,?) "
				"ON DUPLICATE KEY UPDATE "
				"StarLevel=VALUES(StarLevel),OpenStatus=VALUES(OpenStatus),"
				"DrawCount=VALUES(DrawCount),BaoDiLeftNum=VALUES(BaoDiLeftNum),"
				"BaoDiType=VALUES(BaoDiType),SageEggGetNum=VALUES(SageEggGetNum),"
				"StarRewardMask=VALUES(StarRewardMask),"
				"RowVersion=RowVersion+1,UpdateTime=VALUES(UpdateTime);"));
		statement->setUInt(1, State.Uin);
		statement->setUInt(2, State.SeasonID);
		statement->setUInt(3, State.OpenDate);
		statement->setInt(4, State.StarLevel);
		statement->setInt(5, State.OpenStatus);
		statement->setInt(6, State.DrawCount);
		statement->setInt(7, State.BaoDiLeftNum);
		statement->setInt(8, State.BaoDiType);
		statement->setInt(9, State.SageEggGetNum);
		statement->setUInt(10, State.StarRewardMask);
		statement->setUInt(11, static_cast<UINT>(time(nullptr)));
		return statement->executeUpdate() > 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingSaveState Error" << std::endl;
		return false;
	}
}

void ActivityDB_AnnualRacingLoadTempItems(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	std::vector<AnnualRacingTempItemRow>& Rows,
	INT Status)
{
	Rows.clear();
	if (!ActivityDB_AnnualRacingIsReady() || Uin == 0)
		return;
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
			return;
		std::unique_ptr<sql::PreparedStatement> statement;
		if (Status >= 0)
		{
			statement.reset(connection->prepareStatement(
				"SELECT Id,Uin,SeasonID,OpenDate,AwardIndex,ItemID,ItemID1,"
				"ItemNum,ItemType,AvailPeriod,ExchangePieceNum,Status,"
				"CreateTime,UpdateTime FROM AnnualRacingTempItem "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? AND Status=? "
				"ORDER BY Id ASC;"));
			statement->setUInt(1, Uin);
			statement->setUInt(2, SeasonID);
			statement->setUInt(3, OpenDate);
			statement->setInt(4, Status);
		}
		else
		{
			statement.reset(connection->prepareStatement(
				"SELECT Id,Uin,SeasonID,OpenDate,AwardIndex,ItemID,ItemID1,"
				"ItemNum,ItemType,AvailPeriod,ExchangePieceNum,Status,"
				"CreateTime,UpdateTime FROM AnnualRacingTempItem "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? ORDER BY Id ASC;"));
			statement->setUInt(1, Uin);
			statement->setUInt(2, SeasonID);
			statement->setUInt(3, OpenDate);
		}
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		while (result->next())
		{
			AnnualRacingTempItemRow row;
			row.Id = result->getUInt64("Id");
			row.Uin = result->getUInt("Uin");
			row.SeasonID = result->getUInt("SeasonID");
			row.OpenDate = result->getUInt("OpenDate");
			row.AwardIndex = result->getInt("AwardIndex");
			row.ItemID = result->getUInt("ItemID");
			row.ItemID1 = result->getUInt("ItemID1");
			row.ItemNum = result->getInt("ItemNum");
			row.ItemType = result->getInt("ItemType");
			row.AvailPeriod = result->getInt("AvailPeriod");
			row.ExchangePieceNum = result->getInt("ExchangePieceNum");
			row.Status = result->getUInt("Status");
			row.CreateTime = result->getUInt("CreateTime");
			row.UpdateTime = result->getUInt("UpdateTime");
			Rows.push_back(row);
		}
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingLoadTempItems Error" << std::endl;
	}
}

bool ActivityDB_AnnualRacingAddTempItem(const AnnualRacingTempItemRow& Row)
{
	if (!ActivityDB_AnnualRacingIsReady() || Row.Uin == 0 ||
		Row.ItemID == 0 || Row.ItemNum <= 0)
		return false;
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
			return false;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"INSERT INTO AnnualRacingTempItem("
				"Uin,SeasonID,OpenDate,AwardIndex,ItemID,ItemID1,ItemNum,"
				"ItemType,AvailPeriod,ExchangePieceNum,Status,CreateTime,UpdateTime) "
				"VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?);"));
		const UINT now = static_cast<UINT>(time(nullptr));
		statement->setUInt(1, Row.Uin);
		statement->setUInt(2, Row.SeasonID);
		statement->setUInt(3, Row.OpenDate);
		statement->setInt(4, Row.AwardIndex);
		statement->setUInt(5, Row.ItemID);
		statement->setUInt(6, Row.ItemID1);
		statement->setInt(7, Row.ItemNum);
		statement->setInt(8, Row.ItemType);
		statement->setInt(9, Row.AvailPeriod);
		statement->setInt(10, Row.ExchangePieceNum);
		statement->setUInt(11, Row.Status);
		statement->setUInt(12, now);
		statement->setUInt(13, now);
		return statement->executeUpdate() == 1;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingAddTempItem Error" << std::endl;
		return false;
	}
}

bool ActivityDB_AnnualRacingTakeTempItem(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	const AnnualRacingTempItemRow& Request,
	UINT NewStatus,
	AnnualRacingTempItemRow& Taken)
{
	Taken = {};
	if (!ActivityDB_AnnualRacingIsReady() || Uin == 0 ||
		Request.ItemID == 0 || Request.ItemNum <= 0)
		return false;
	DBConnection database(activity);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		connection->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> select(
			connection->prepareStatement(
				"SELECT Id,AwardIndex,ItemID,ItemID1,ItemNum,ItemType,"
				"AvailPeriod,ExchangePieceNum,Status,CreateTime,UpdateTime "
				"FROM AnnualRacingTempItem "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? AND ItemID=? "
				"AND ItemType=? AND AvailPeriod=? AND Status=0 "
				"ORDER BY Id ASC LIMIT 1 FOR UPDATE;"));
		select->setUInt(1, Uin);
		select->setUInt(2, SeasonID);
		select->setUInt(3, OpenDate);
		select->setUInt(4, Request.ItemID);
		select->setInt(5, Request.ItemType);
		select->setInt(6, Request.AvailPeriod);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		if (!result->next())
		{
			connection->rollback();
			connection->setAutoCommit(true);
			return false;
		}

		Taken.Uin = Uin;
		Taken.SeasonID = SeasonID;
		Taken.OpenDate = OpenDate;
		Taken.Id = result->getUInt64("Id");
		Taken.AwardIndex = result->getInt("AwardIndex");
		Taken.ItemID = result->getUInt("ItemID");
		Taken.ItemID1 = result->getUInt("ItemID1");
		const INT available = result->getInt("ItemNum");
		Taken.ItemType = result->getInt("ItemType");
		Taken.AvailPeriod = result->getInt("AvailPeriod");
		Taken.ExchangePieceNum = result->getInt("ExchangePieceNum");
		Taken.Status = NewStatus;
		Taken.CreateTime = result->getUInt("CreateTime");
		Taken.UpdateTime = static_cast<UINT>(time(nullptr));
		if (Request.ItemNum > available)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			Taken = {};
			return false;
		}
		Taken.ItemNum = Request.ItemNum;

		std::unique_ptr<sql::PreparedStatement> update;
		if (Request.ItemNum == available)
		{
			update.reset(connection->prepareStatement(
				"UPDATE AnnualRacingTempItem SET Status=?,UpdateTime=? "
				"WHERE Id=? AND Uin=? AND Status=0;"));
			update->setUInt(1, NewStatus);
			update->setUInt(2, Taken.UpdateTime);
			update->setUInt64(3, Taken.Id);
			update->setUInt(4, Uin);
		}
		else
		{
			update.reset(connection->prepareStatement(
				"UPDATE AnnualRacingTempItem SET ItemNum=ItemNum-?,UpdateTime=? "
				"WHERE Id=? AND Uin=? AND Status=0 AND ItemNum>=?;"));
			update->setInt(1, Request.ItemNum);
			update->setUInt(2, Taken.UpdateTime);
			update->setUInt64(3, Taken.Id);
			update->setUInt(4, Uin);
			update->setInt(5, Request.ItemNum);
		}
		if (update->executeUpdate() != 1)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			Taken = {};
			return false;
		}
		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingTakeTempItem Error" << std::endl;
		Taken = {};
		return false;
	}
}

INT ActivityDB_AnnualRacingGetPackageCount(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	INT SpecialFlag)
{
	if (!ActivityDB_AnnualRacingIsReady() || Uin == 0 || SpecialFlag <= 0)
		return 0;
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
			return 0;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"SELECT CounterValue FROM AnnualRacingCounter "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? "
				"AND CounterType=1 AND CounterScope=0 AND CounterKey=?;"));
		statement->setUInt(1, Uin);
		statement->setUInt(2, SeasonID);
		statement->setUInt(3, OpenDate);
		statement->setInt(4, SpecialFlag);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		return result->next() ? result->getInt("CounterValue") : 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingGetPackageCount Error" << std::endl;
		return 0;
	}
}

bool ActivityDB_AnnualRacingTryReservePackage(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	INT SpecialFlag,
	INT MaxGetNum,
	INT& NewGetNum)
{
	NewGetNum = 0;
	if (!ActivityDB_AnnualRacingIsReady() || Uin == 0 ||
		SpecialFlag <= 0 || MaxGetNum <= 0)
		return false;
	DBConnection database(activity);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		connection->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> select(
			connection->prepareStatement(
				"SELECT CounterValue FROM AnnualRacingCounter "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? "
				"AND CounterType=1 AND CounterScope=0 AND CounterKey=? "
				"FOR UPDATE;"));
		select->setUInt(1, Uin);
		select->setUInt(2, SeasonID);
		select->setUInt(3, OpenDate);
		select->setInt(4, SpecialFlag);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		const bool exists = result->next();
		const INT current =
			exists ? result->getInt("CounterValue") : 0;
		if (current >= MaxGetNum)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			NewGetNum = current;
			return false;
		}
		NewGetNum = current + 1;
		std::unique_ptr<sql::PreparedStatement> write;
		if (exists)
		{
			write.reset(connection->prepareStatement(
				"UPDATE AnnualRacingCounter SET CounterValue=?,UpdateTime=? "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? "
				"AND CounterType=1 AND CounterScope=0 AND CounterKey=?;"));
			write->setInt(1, NewGetNum);
			write->setUInt(2, static_cast<UINT>(time(nullptr)));
			write->setUInt(3, Uin);
			write->setUInt(4, SeasonID);
			write->setUInt(5, OpenDate);
			write->setInt(6, SpecialFlag);
		}
		else
		{
			write.reset(connection->prepareStatement(
				"INSERT INTO AnnualRacingCounter("
				"Uin,SeasonID,OpenDate,CounterType,CounterScope,"
				"CounterKey,CounterValue,UpdateTime) "
				"VALUES(?,?,?,1,0,?,1,?);"));
			write->setUInt(1, Uin);
			write->setUInt(2, SeasonID);
			write->setUInt(3, OpenDate);
			write->setInt(4, SpecialFlag);
			write->setUInt(5, static_cast<UINT>(time(nullptr)));
		}
		if (write->executeUpdate() != 1)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			NewGetNum = current;
			return false;
		}
		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingTryReservePackage Error" << std::endl;
		NewGetNum = 0;
		return false;
	}
}

bool ActivityDB_AnnualRacingReleasePackage(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	INT SpecialFlag)
{
	if (!ActivityDB_AnnualRacingIsReady() || Uin == 0 || SpecialFlag <= 0)
		return false;
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
			return false;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"UPDATE AnnualRacingCounter "
				"SET CounterValue=IF(CounterValue>0,CounterValue-1,0),"
				"UpdateTime=? WHERE Uin=? AND SeasonID=? AND OpenDate=? "
				"AND CounterType=1 AND CounterScope=0 AND CounterKey=?;"));
		statement->setUInt(1, static_cast<UINT>(time(nullptr)));
		statement->setUInt(2, Uin);
		statement->setUInt(3, SeasonID);
		statement->setUInt(4, OpenDate);
		statement->setInt(5, SpecialFlag);
		return statement->executeUpdate() == 1;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingReleasePackage Error" << std::endl;
		return false;
	}
}

INT ActivityDB_AnnualRacingGetExchangeCount(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	USHORT ShopType,
	INT AwardIndex)
{
	if (!ActivityDB_AnnualRacingIsReady() || Uin == 0 ||
		ShopType == 0 || AwardIndex <= 0)
		return 0;
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
			return 0;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"SELECT CounterValue FROM AnnualRacingCounter "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? "
				"AND CounterType=2 AND CounterScope=? AND CounterKey=?;"));
		statement->setUInt(1, Uin);
		statement->setUInt(2, SeasonID);
		statement->setUInt(3, OpenDate);
		statement->setUInt(4, ShopType);
		statement->setInt(5, AwardIndex);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		return result->next() ? result->getInt("CounterValue") : 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingGetExchangeCount Error" << std::endl;
		return 0;
	}
}

bool ActivityDB_AnnualRacingTryReserveExchange(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	USHORT ShopType,
	INT AwardIndex,
	INT AddCount,
	INT MaxGetNum,
	INT& NewGetNum)
{
	NewGetNum = 0;
	if (!ActivityDB_AnnualRacingIsReady() || Uin == 0 ||
		ShopType == 0 || AwardIndex <= 0 || AddCount <= 0 ||
		MaxGetNum <= 0)
		return false;
	DBConnection database(activity);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		connection->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> select(
			connection->prepareStatement(
				"SELECT CounterValue FROM AnnualRacingCounter "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? "
				"AND CounterType=2 AND CounterScope=? AND CounterKey=? "
				"FOR UPDATE;"));
		select->setUInt(1, Uin);
		select->setUInt(2, SeasonID);
		select->setUInt(3, OpenDate);
		select->setUInt(4, ShopType);
		select->setInt(5, AwardIndex);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		const bool exists = result->next();
		const INT current =
			exists ? result->getInt("CounterValue") : 0;
		if (current > MaxGetNum - AddCount)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			NewGetNum = current;
			return false;
		}
		NewGetNum = current + AddCount;
		std::unique_ptr<sql::PreparedStatement> write;
		if (exists)
		{
			write.reset(connection->prepareStatement(
				"UPDATE AnnualRacingCounter SET CounterValue=?,UpdateTime=? "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? "
				"AND CounterType=2 AND CounterScope=? AND CounterKey=?;"));
			write->setInt(1, NewGetNum);
			write->setUInt(2, static_cast<UINT>(time(nullptr)));
			write->setUInt(3, Uin);
			write->setUInt(4, SeasonID);
			write->setUInt(5, OpenDate);
			write->setUInt(6, ShopType);
			write->setInt(7, AwardIndex);
		}
		else
		{
			write.reset(connection->prepareStatement(
				"INSERT INTO AnnualRacingCounter("
				"Uin,SeasonID,OpenDate,CounterType,CounterScope,"
				"CounterKey,CounterValue,UpdateTime) "
				"VALUES(?,?,?,2,?,?,?,?);"));
			write->setUInt(1, Uin);
			write->setUInt(2, SeasonID);
			write->setUInt(3, OpenDate);
			write->setUInt(4, ShopType);
			write->setInt(5, AwardIndex);
			write->setInt(6, NewGetNum);
			write->setUInt(7, static_cast<UINT>(time(nullptr)));
		}
		if (write->executeUpdate() != 1)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			NewGetNum = current;
			return false;
		}
		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingTryReserveExchange Error" << std::endl;
		NewGetNum = 0;
		return false;
	}
}

bool ActivityDB_AnnualRacingReleaseExchange(
	UINT Uin,
	UINT SeasonID,
	UINT OpenDate,
	USHORT ShopType,
	INT AwardIndex,
	INT Count)
{
	if (!ActivityDB_AnnualRacingIsReady() || Uin == 0 ||
		ShopType == 0 || AwardIndex <= 0 || Count <= 0)
		return false;
	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
			return false;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"UPDATE AnnualRacingCounter "
				"SET CounterValue=IF(CounterValue>=?,CounterValue-?,0),"
				"UpdateTime=? "
				"WHERE Uin=? AND SeasonID=? AND OpenDate=? "
				"AND CounterType=2 AND CounterScope=? AND CounterKey=?;"));
		statement->setInt(1, Count);
		statement->setInt(2, Count);
		statement->setUInt(3, static_cast<UINT>(time(nullptr)));
		statement->setUInt(4, Uin);
		statement->setUInt(5, SeasonID);
		statement->setUInt(6, OpenDate);
		statement->setUInt(7, ShopType);
		statement->setInt(8, AwardIndex);
		return statement->executeUpdate() == 1;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " ActivityDB_AnnualRacingReleaseExchange Error" << std::endl;
		return false;
	}
}

AuroraActivityDbResult ActivityDB_CreateAuroraDraft(
	const QQSpeedAuroraOfficial::CreatorRequest& request,
	AuroraActivityWorkRow& work,
	std::string& error)
{
	work = {};
	if (!ActivityDB_IsReady(&error))
	{
		if (error.empty())
			error = "activity database not ready";
		return AuroraActivityDbResult::NotReady;
	}
	if (request.uin == 0u || request.nameRaw.empty() ||
		request.nameRaw.size() > 64u)
	{
		error = "create-draft database arguments invalid";
		return AuroraActivityDbResult::InvalidState;
	}

	DBConnection database(activity);
	auto* connection = database.getConnection();
	if (!connection)
	{
		error = "activity database connection unavailable";
		return AuroraActivityDbResult::NotReady;
	}

	try
	{
		connection->setAutoCommit(false);
		std::uint64_t workId = 0;
		if (!ActivityDB_AllocateSequence(
				connection, "work_id", workId, error))
		{
			connection->rollback();
			return AuroraActivityDbResult::DatabaseError;
		}
		const std::uint64_t nowMs = AuroraNowMilliseconds();
		std::unique_ptr<sql::PreparedStatement> insert(connection->prepareStatement(
			"INSERT INTO AuroraWork ("
			"WorkID,OwnerUin,NameRaw,DescriptionRaw,Difficulty,MaxPlayers,"
			"LifecycleState,QualificationStatus,CurrentVersion,ComponentCount,"
			"CreatedAtMs,UpdatedAtMs) "
			"VALUES (?,?,?,?,?,?,?,?,?,?,?,?);"));
		insert->setUInt64(1, workId);
		insert->setUInt(2, request.uin);
		insert->setString(3, request.nameRaw);
		insert->setString(4, request.descriptionRaw);
		insert->setUInt(5, request.difficulty);
		insert->setUInt(6, request.maxPlayers);
		insert->setUInt(7, 0u);
		insert->setUInt(8, 0u);
		// The first captured SaveDraft produces render version 2.
		insert->setUInt(9, 1u);
		insert->setInt(10, request.componentCount);
		insert->setUInt64(11, nowMs);
		insert->setUInt64(12, nowMs);
		if (insert->executeUpdate() != 1)
		{
			connection->rollback();
			error = "create-draft insert affected unexpected rows";
			return AuroraActivityDbResult::DatabaseError;
		}

		connection->commit();
		work.WorkID = workId;
		work.OwnerUin = request.uin;
		work.NameRaw = request.nameRaw;
		work.LifecycleState = 0u;
		work.QualificationStatus = 0u;
		work.CurrentVersion = 1u;
		work.ComponentCount = request.componentCount;
		work.CreatedAtMs = nowMs;
		work.UpdatedAtMs = nowMs;
		error = "ok";
		return AuroraActivityDbResult::Ok;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); }
		catch (...) {}
		error = exception.what();
		return AuroraActivityDbResult::DatabaseError;
	}
}

AuroraActivityDbResult ActivityDB_BeginAuroraUpload(
	const QQSpeedAuroraOfficial::CreatorRequest& request,
	std::uint8_t command,
	const std::string& publicBaseUrl,
	const std::string& authInfo,
	AuroraActivityWorkRow& work,
	AuroraActivityUploadRow& upload,
	std::string& error)
{
	work = {};
	upload = {};
	if (!ActivityDB_IsReady(&error))
	{
		if (error.empty())
			error = "activity database not ready";
		return AuroraActivityDbResult::NotReady;
	}
	const bool renderProfile =
		request.operateType == QQSpeedAuroraOfficial::kCreatorSaveDraft &&
		command == QQSpeedAuroraOfficial::kCosCommandRender;
	const bool settingsProfile =
		request.operateType ==
			QQSpeedAuroraOfficial::kCreatorUploadSettingsJson &&
		command == QQSpeedAuroraOfficial::kCosCommandSettingsJson;
	if ((!renderProfile && !settingsProfile) ||
		request.workId == 0u || request.uin == 0u ||
		request.clientRequestId.empty() || request.renderMd5.size() != 32u ||
		publicBaseUrl.empty() || authInfo.empty() ||
		authInfo.size() > (std::numeric_limits<std::uint16_t>::max)() ||
		publicBaseUrl.back() == '/' ||
		(publicBaseUrl.rfind("https://", 0) != 0u &&
			publicBaseUrl.rfind("http://", 0) != 0u))
	{
		error = "begin-upload database arguments invalid";
		return AuroraActivityDbResult::InvalidState;
	}

	DBConnection database(activity);
	auto* connection = database.getConnection();
	if (!connection)
	{
		error = "activity database connection unavailable";
		return AuroraActivityDbResult::NotReady;
	}

	try
	{
		connection->setAutoCommit(false);
		if (!ActivityDB_SelectWorkForUpdate(
				connection, request.workId, work, error))
		{
			connection->rollback();
			return AuroraActivityDbResult::NotFound;
		}
		if (work.OwnerUin != request.uin)
		{
			connection->rollback();
			error = "Aurora work owner mismatch";
			return AuroraActivityDbResult::Forbidden;
		}

		std::uint32_t workVersion = work.CurrentVersion;
		if (renderProfile)
		{
			if (workVersion ==
				(std::numeric_limits<std::uint32_t>::max)())
			{
				connection->rollback();
				error = "Aurora work version exhausted";
				return AuroraActivityDbResult::Conflict;
			}
			++workVersion;
		}

		std::uint64_t objectId = 0;
		if (!ActivityDB_AllocateSequence(
				connection, "object_id", objectId, error))
		{
			connection->rollback();
			return AuroraActivityDbResult::DatabaseError;
		}

		std::ostringstream objectKeyBuilder;
		if (renderProfile)
		{
			objectKeyBuilder << "aurora/" << request.workId << "/"
				<< workVersion << "/render/" << objectId;
		}
		else
		{
			objectKeyBuilder << "aurora/" << request.workId
				<< "/settings_json/" << objectId;
		}
		const std::string objectKey = objectKeyBuilder.str();
		if (objectKey.size() >
			(std::numeric_limits<std::uint8_t>::max)())
		{
			connection->rollback();
			error = "Aurora object key exceeds captured uint8 length";
			return AuroraActivityDbResult::Conflict;
		}
		const std::string publicUrl =
			ActivityDB_PublicUrl(publicBaseUrl, objectKey);
		if (publicUrl.size() >
			(std::numeric_limits<std::uint16_t>::max)())
		{
			connection->rollback();
			error = "Aurora public URL exceeds captured uint16 length";
			return AuroraActivityDbResult::Conflict;
		}

		const std::uint64_t nowMs = AuroraNowMilliseconds();
		std::string authHash;
		if (!QQSpeedAuroraOfficial::ComputeSha256Hex(
				authInfo, authHash, &error))
		{
			connection->rollback();
			return AuroraActivityDbResult::DatabaseError;
		}
		std::unique_ptr<sql::PreparedStatement> updateWork(
			connection->prepareStatement(renderProfile
				? "UPDATE AuroraWork SET CurrentVersion=?,RenderMd5=?,"
				  "ComponentCount=?,RowVersion=RowVersion+1,UpdatedAtMs=? "
				  "WHERE WorkID=? AND OwnerUin=?;"
				: "UPDATE AuroraWork SET SettingsMd5=?,"
				  "RowVersion=RowVersion+1,UpdatedAtMs=? "
				  "WHERE WorkID=? AND OwnerUin=?;"));
		if (renderProfile)
		{
			updateWork->setUInt(1, workVersion);
			updateWork->setString(2, request.renderMd5);
			updateWork->setInt(3, request.componentCount);
			updateWork->setUInt64(4, nowMs);
			updateWork->setUInt64(5, request.workId);
			updateWork->setUInt(6, request.uin);
		}
		else
		{
			updateWork->setString(1, request.renderMd5);
			updateWork->setUInt64(2, nowMs);
			updateWork->setUInt64(3, request.workId);
			updateWork->setUInt(4, request.uin);
		}
		if (updateWork->executeUpdate() != 1)
		{
			connection->rollback();
			error = "begin-upload work update affected unexpected rows";
			return AuroraActivityDbResult::Conflict;
		}

		std::unique_ptr<sql::PreparedStatement> insertUpload(
			connection->prepareStatement(
				"INSERT INTO AuroraUploadTicket ("
				"WorkID,OwnerUin,Operation,CosCommand,RequestTimestamp,WorkVersion,"
				"ClientRequestID,ExpectedMd5,ObjectID,ObjectKey,PublicUrl,"
				"AuthHash,Status,CreatedAtMs) "
				"VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?);"));
		insertUpload->setUInt64(1, request.workId);
		insertUpload->setUInt(2, request.uin);
		insertUpload->setUInt(3, request.operateType);
		insertUpload->setUInt(4, command);
		insertUpload->setUInt(5, request.timestamp);
		insertUpload->setUInt(6, workVersion);
		insertUpload->setString(7, request.clientRequestId);
		insertUpload->setString(8, request.renderMd5);
		insertUpload->setUInt64(9, objectId);
		insertUpload->setString(10, objectKey);
		insertUpload->setString(11, publicUrl);
		insertUpload->setString(12, authHash);
		insertUpload->setUInt(13, 0u);
		insertUpload->setUInt64(14, nowMs);
		if (insertUpload->executeUpdate() != 1)
		{
			connection->rollback();
			error = "begin-upload ticket insert affected unexpected rows";
			return AuroraActivityDbResult::DatabaseError;
		}

		std::uint64_t uploadId = 0;
		std::unique_ptr<sql::Statement> lastIdStatement(
			connection->createStatement());
		std::unique_ptr<sql::ResultSet> lastIdResult(
			lastIdStatement->executeQuery(
				"SELECT LAST_INSERT_ID() AS UploadID;"));
		if (!lastIdResult->next() ||
			(uploadId = lastIdResult->getUInt64("UploadID")) == 0u)
		{
			connection->rollback();
			error = "begin-upload generated ID unavailable";
			return AuroraActivityDbResult::DatabaseError;
		}

		connection->commit();
		work.CurrentVersion = workVersion;
		work.ComponentCount = renderProfile
			? request.componentCount
			: work.ComponentCount;
		if (renderProfile) work.RenderMd5 = request.renderMd5;
		else work.SettingsMd5 = request.renderMd5;
		work.UpdatedAtMs = nowMs;

		upload.UploadID = uploadId;
		upload.WorkID = request.workId;
		upload.OwnerUin = request.uin;
		upload.Operation = request.operateType;
		upload.Command = command;
		upload.RequestTimestamp = request.timestamp;
		upload.WorkVersion = workVersion;
		upload.ClientRequestID = request.clientRequestId;
		upload.ExpectedMd5 = request.renderMd5;
		upload.ObjectID = objectId;
		upload.ObjectKey = objectKey;
		upload.PublicUrl = publicUrl;
		upload.AuthHash = authHash;
		upload.Status = 0u;
		upload.CreatedAtMs = nowMs;
		error = "ok";
		return AuroraActivityDbResult::Ok;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); }
		catch (...) {}
		error = exception.what();
		return AuroraActivityDbResult::DatabaseError;
	}
}

AuroraActivityDbResult ActivityDB_CompleteAuroraUpload(
	const QQSpeedAuroraOfficial::UploadReportRequest& request,
	const std::string& authHash,
	AuroraActivityWorkRow& work,
	AuroraActivityUploadRow& upload,
	std::string& error)
{
	work = {};
	upload = {};
	if (!ActivityDB_IsReady(&error))
	{
		if (error.empty())
			error = "activity database not ready";
		return AuroraActivityDbResult::NotReady;
	}
	if (request.uin == 0u || request.workId == 0u ||
		request.resultId != 0u || request.objectKeys.size() != 1u ||
		authHash.size() != 64u)
	{
		error = "complete-upload database arguments invalid";
		return AuroraActivityDbResult::InvalidState;
	}

	DBConnection database(activity);
	auto* connection = database.getConnection();
	if (!connection)
	{
		error = "activity database connection unavailable";
		return AuroraActivityDbResult::NotReady;
	}

	try
	{
		connection->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> selectUpload(
			connection->prepareStatement(
				"SELECT UploadID,WorkID,OwnerUin,Operation,CosCommand,"
				"RequestTimestamp,WorkVersion,ClientRequestID,ExpectedMd5,ObjectID,ObjectKey,"
				"PublicUrl,AuthHash,Status,CreatedAtMs,ReportedAtMs "
				"FROM AuroraUploadTicket "
				"WHERE WorkID=? AND OwnerUin=? AND CosCommand=? "
				"AND ObjectKey=? ORDER BY UploadID DESC LIMIT 1 FOR UPDATE;"));
		selectUpload->setUInt64(1, request.workId);
		selectUpload->setUInt(2, request.uin);
		selectUpload->setUInt(3, request.command);
		selectUpload->setString(4, request.objectKeys.front());
		std::unique_ptr<sql::ResultSet> uploadResult(
			selectUpload->executeQuery());
		if (!uploadResult->next())
		{
			connection->rollback();
			error = "Aurora upload ticket not found";
			return AuroraActivityDbResult::NotFound;
		}
		if (!ActivityDB_ReadUpload(*uploadResult, upload))
		{
			connection->rollback();
			error = "Aurora upload ticket row invalid";
			return AuroraActivityDbResult::DatabaseError;
		}
		if (upload.Status != 0u)
		{
			connection->rollback();
			error = "Aurora upload ticket is not pending";
			return AuroraActivityDbResult::InvalidState;
		}
		if (upload.AuthHash != authHash)
		{
			connection->rollback();
			error = "Aurora upload authorization mismatch";
			return AuroraActivityDbResult::Forbidden;
		}
		if (upload.Command != request.command ||
			upload.WorkID != request.workId ||
			upload.OwnerUin != request.uin)
		{
			connection->rollback();
			error = "Aurora upload ticket identity mismatch";
			return AuroraActivityDbResult::Forbidden;
		}

		if (!ActivityDB_SelectWorkForUpdate(
				connection, request.workId, work, error))
		{
			connection->rollback();
			return AuroraActivityDbResult::NotFound;
		}
		if (work.OwnerUin != request.uin ||
			work.CurrentVersion != upload.WorkVersion)
		{
			connection->rollback();
			error = "Aurora upload callback is stale or cross-owner";
			return AuroraActivityDbResult::InvalidState;
		}

		const std::uint64_t nowMs = AuroraNowMilliseconds();
		std::unique_ptr<sql::PreparedStatement> updateUpload(
			connection->prepareStatement(
				"UPDATE AuroraUploadTicket SET Status=1,ReportResultID=?,"
				"ReportedAtMs=? WHERE UploadID=? AND Status=0;"));
		updateUpload->setUInt(1, request.resultId);
		updateUpload->setUInt64(2, nowMs);
		updateUpload->setUInt64(3, upload.UploadID);
		if (updateUpload->executeUpdate() != 1)
		{
			connection->rollback();
			error = "upload completion affected unexpected ticket rows";
			return AuroraActivityDbResult::Conflict;
		}

		const bool render =
			request.command == QQSpeedAuroraOfficial::kCosCommandRender;
		std::unique_ptr<sql::PreparedStatement> updateWork(
			connection->prepareStatement(render
				? "UPDATE AuroraWork SET LifecycleState=1,RenderObjectKey=?,"
				  "RenderUrl=?,RowVersion=RowVersion+1,UpdatedAtMs=? "
				  "WHERE WorkID=? AND OwnerUin=? AND CurrentVersion=?;"
				: "UPDATE AuroraWork SET SettingsObjectKey=?,SettingsUrl=?,"
				  "RowVersion=RowVersion+1,UpdatedAtMs=? "
				  "WHERE WorkID=? AND OwnerUin=? AND CurrentVersion=?;"));
		updateWork->setString(1, upload.ObjectKey);
		updateWork->setString(2, upload.PublicUrl);
		updateWork->setUInt64(3, nowMs);
		updateWork->setUInt64(4, request.workId);
		updateWork->setUInt(5, request.uin);
		updateWork->setUInt(6, upload.WorkVersion);
		if (updateWork->executeUpdate() != 1)
		{
			connection->rollback();
			error = "upload completion affected unexpected work rows";
			return AuroraActivityDbResult::Conflict;
		}

		connection->commit();
		upload.Status = 1u;
		upload.ReportedAtMs = nowMs;
		if (render)
		{
			work.LifecycleState = 1u;
			work.RenderObjectKey = upload.ObjectKey;
			work.RenderUrl = upload.PublicUrl;
		}
		else
		{
			work.SettingsObjectKey = upload.ObjectKey;
			work.SettingsUrl = upload.PublicUrl;
		}
		work.UpdatedAtMs = nowMs;
		error = "ok";
		return AuroraActivityDbResult::Ok;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); }
		catch (...) {}
		error = exception.what();
		return AuroraActivityDbResult::DatabaseError;
	}
}

AuroraActivityDbResult ActivityDB_GetAuroraWork(
	std::uint64_t workId,
	AuroraActivityWorkRow& work,
	std::string& error)
{
	work = {};
	if (!ActivityDB_IsReady(&error))
	{
		if (error.empty())
			error = "activity database not ready";
		return AuroraActivityDbResult::NotReady;
	}
	if (workId == 0u)
	{
		error = "Aurora WorkID is zero";
		return AuroraActivityDbResult::InvalidState;
	}

	try
	{
		DBConnection database(activity);
		auto* connection = database.getConnection();
		if (!connection)
		{
			error = "activity database connection unavailable";
			return AuroraActivityDbResult::NotReady;
		}
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"SELECT WorkID,OwnerUin,NameRaw,LifecycleState,"
				"QualificationStatus,CurrentVersion,ComponentCount,"
				"RenderMd5,RenderObjectKey,RenderUrl,SettingsMd5,"
				"SettingsObjectKey,SettingsUrl,CreatedAtMs,UpdatedAtMs "
				"FROM AuroraWork WHERE WorkID=?;"));
		statement->setUInt64(1, workId);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		if (!result->next())
		{
			error = "Aurora work not found";
			return AuroraActivityDbResult::NotFound;
		}
		if (!ActivityDB_ReadWork(*result, work))
		{
			error = "Aurora work row invalid";
			return AuroraActivityDbResult::DatabaseError;
		}
		error = "ok";
		return AuroraActivityDbResult::Ok;
	}
	catch (const std::exception& exception)
	{
		error = exception.what();
		return AuroraActivityDbResult::DatabaseError;
	}
}



BOOL UserDB_RegisterNew(const char* Username, const char* Password, const char* yqm)
{

	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO User (Name, Password, InviteCode) VALUES(?, ?, ?);"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, Username);
		prep_stmt->setString(2, Password);
		prep_stmt->setString(3, yqm);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UserDB_RegisterNew Error" << std::endl;
		//throw;??
	}
}

BOOL UserDB_Register(const char* Username, const char* Password, const char* cdk)
{
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn)  return false;
	try
	{
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Name FROM User WHERE Name = ?;"));
			prep_stmt->clearParameters();
			prep_stmt->setString(1, Username);

			// ????
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			prep_stmt.reset();
			// ????
			if (res->rowsCount() > 0) {
				res.reset();
				return false;
			}
			res.reset();
		}

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO User (Name, Password,regcdk) VALUES(?, ?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, Username);
		prep_stmt->setString(2, Password);
		prep_stmt->setString(3, cdk);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UserDB_Register Error" << std::endl;
		//throw;??
	}
}


void UserDB_ClearTransportConnID(DWORD Uin)
{//Lupin
	if (Uin < 10000) return;
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	try
	{

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE User SET ConnID=null WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin - 10000);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_ClearTransportConnID Error\n");

	}
}

void UserDB_ClearTransportConnIDIfMatch(DWORD Uin, DWORD TransportConnID)
{
	if (Uin < 10000 || TransportConnID == 0) return;
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE User SET ConnID=null WHERE Uin=? AND ConnID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin - 10000);
		prep_stmt->setUInt(2, TransportConnID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_ClearTransportConnIDIfMatch Error\n");
	}
}


void UserDB_ClearTransportConnID()
{//Lupin
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	try
	{

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE User SET ConnID=null;"));
		prep_stmt->clearParameters();
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_ClearTransportConnIDAll Error\n");

	}
}


UINT UserDB_SelectTransportConnID(DWORD Uin)
{//Lupin
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	UINT TransportConnID = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ConnID FROM User WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin - 10000);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			TransportConnID = res->getUInt("ConnID");
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_SelectTransportConnID Error\n");
	}
	return TransportConnID;
}

UINT UserDB_SelectUsrByTransportConnID(DWORD TransportConnID)
{//Lupin
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	UINT ret = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM User WHERE ConnID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, TransportConnID);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			ret = 1;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_SelectUsrByTransportConnID Error\n");
	}
	return ret;
}



void UserDB_SetTransportConnID(DWORD Uin, DWORD TransportConnID)
{//Lupin
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	try
	{

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE User SET ConnID=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, TransportConnID);
		prep_stmt->setUInt(2, Uin - 10000);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_SetTransportConnID Error\n");

	}
}

void UserDB_SelectTransportConnIDs(std::vector<UINT>& TransportConnIDs)
{//Lupin
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	try
	{

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ConnID FROM User WHERE ConnID > 0;"));
		prep_stmt->clearParameters();

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			TransportConnIDs.push_back(res->getUInt("ConnID"));
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_SelectTransportConnIDs Error\n");

	}
}

std::string UserDB_GetLoginKey(UINT Uin)
{
	std::string key = "";
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::string sql = "SELECT LoginKey FROM User WHERE Uin = ?;";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin - 10000);

		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		if (res->rowsCount() > 0) {
			res->next();
			key = UTF2GB(res->getString("LoginKey").c_str());
		}
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_GetLoginKey Error\n");
	}
	return key;
}

void UserDB_UpdateLoginKey(UINT Uin, const char* key)
{
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE User SET LoginKey=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, key);
		prep_stmt->setUInt(2, Uin - 10000);
		prep_stmt->executeUpdate();
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_UpdateLoginKey Error\n");

	}
}

BOOL UserDB_CmpIP(const char* IP)
{
	BOOL ret = FALSE;
	UINT connid = 0;
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	try
	{
		// ??????????,????? ? ??????
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT connid  FROM User  WHERE ip=? and isban is null;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, IP);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			res->next();
			connid = res->getUInt("connid");
			if (connid > 0) {
				ret = TRUE;
			}
		}
		res.reset();
		return ret;

	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_CmpIP Error\n");
	}
}

std::string UserDB_GetIP(UINT Uin)
{
	std::string Ip = "";
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	try
	{
		// ??????????,????? ? ??????
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ip  FROM User  WHERE Uin =?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin - 10000);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			res->next();
			Ip = UTF2GB(res->getString("ip").c_str());
		}
		res.reset();
		return Ip;

	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_GetIP Error\n");
	}

}

BOOL UserDB_SelectIP(const char* IP)
{
	BOOL ret = FALSE;
	UINT connid = 0;
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	try
	{
		// ??????????,????? ? ??????
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1  FROM User  WHERE ip=? and isban is null;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, IP);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			ret = TRUE;
		}
		res.reset();
		return ret;

	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_SelectIP Error\n");
	}

}

BOOL UserDB_SelectIPNoTime(const char* IP)
{
	BOOL ret = FALSE;
	UINT connid = 0;
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection();
	try
	{
		// ??????????,????? ? ??????
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1  FROM User  WHERE ip=? and isban is null and STR_TO_DATE(lastlogintime, '%Y-%m-%d %H:%i:%s') >= NOW() - INTERVAL 8 HOUR;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, IP);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			ret = TRUE;
		}
		res.reset();
		return ret;

	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("UserDB_SelectIPNoTime Error\n");
	}

}


void UserDB_GetAllUin(std::vector<UINT>& Uins)
{
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		Uins.clear();
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Uin FROM User"));
		prep_stmt->clearParameters();
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			Uins.push_back(res->getUInt("Uin"));
		}
		res.reset();

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UserDB_GetAllUin Error" << std::endl;
		//throw;??
	}
}


void PlayerDB_GetAllBaseInfoUin(std::vector<UINT>& Uins)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		Uins.clear();
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Uin FROM BaseInfo ORDER BY Uin;"));
		prep_stmt->clearParameters();
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next()) {
			UINT uin = res->getUInt("Uin");
			if (uin >= 10000) {
				Uins.push_back(uin);
			}
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetAllBaseInfoUin Error" << std::endl;
	}
}
void UserDB_UpdateMD5(const char* Username)
{

	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE User SET NameEncryp=? WHERE Name=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, md5(Username));
		prep_stmt->setString(2, Username);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UserDB_UpdateMD51 Error" << std::endl;
		//throw;??
	}
}

void UserDB_UpdateMD5()
{

	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Name  FROM User WHere NameEncryp Is null;"));
		prep_stmt->clearParameters();

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			UserDB_UpdateMD5(res->getString("Name").c_str());
		}

		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UserDB_UpdateMD52 Error" << std::endl;
		//throw;??
	}
}


DWORD UserDB_GetUin(const char* ip, const char* Username, const char* Pwd)
{

	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		UINT uin = 0;
		int IsBan = 0;
		std::string pwd;
		// ??????????,????? ? ??????
		std::string sql;
		/*if (PreventSDP[0] > 0) {
			sql = "SELECT Uin,Password,IsBan FROM User WHERE BINARY  NameEncryp = ?;";
		}
		else {
			sql = "SELECT Uin,Password,IsBan FROM User WHERE BINARY  Name = ?;";
		}*/

		sql = "SELECT Uin,Password,IsBan FROM User WHERE BINARY  Name = ?;";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, Username);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			uin = res->getUInt("Uin");
			pwd = res->getString("Password");
			IsBan = res->getInt("IsBan");
		}
		res.reset();

		if (pwd.size() != 0) {
			uin += 10000; //????10000,??????
			if (ismd5) {
				pwd = md5(pwd);
			}

			if (std::string(Pwd) != pwd)
			{
				uin = 0;
			}
			if (IsBan == 1)
			{
				uin = 1;
			}
		}
		else {
			uin = 0;
		}

		if (uin > 10000)
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE User SET IP = ?,LastLoginTime = ?  WHERE Uin = ?;"));
			prep_stmt->clearParameters();
			prep_stmt->setString(1, ip);

			// ??????,????????????
			auto now = std::chrono::system_clock::now();
			auto now_time_t = std::chrono::system_clock::to_time_t(now);
			struct tm timeinfo;
			char buffer[20]; // ?????????
			localtime_s(&timeinfo,&now_time_t);
			strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
			prep_stmt->setString(2, buffer);


			prep_stmt->setUInt(3, uin - 10000);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}

		return uin;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UserDB_GetUin1 Error" << std::endl;
		//throw;??
	}
}

BOOL UserDB_GetUin(UINT Uin)
{
	bool ret = false;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{

		std::string sql = "SELECT Uin FROM Baseinfo WHERE Uin = ?;";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			ret = true;
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UserDB_GetUin2 Error" << std::endl;
		//throw;??
	}
}


BOOL UserDB_CmpPassword(DWORD Uin, const char* Password)
{
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn)  return false;
	try
	{
		std::string pwd;
		BOOL ret = FALSE;
		UINT isban = 0;
		// ??????????,????? ? ??????

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Password,isban  FROM User  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin - 10000);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			while (res->next()) {
				pwd = res->getString("Password");
				isban = res->getInt("isban");
				break;
			}
			if (ismd5) {
				pwd = md5(pwd);
			}
			if (pwd == std::string(Password) && isban != 1)
			{
				ret = TRUE;
			}
		}
		// ????
		res.reset();
		return ret;

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UserDB_CmpPassword Error" << std::endl;
		//throw;??
	}
}

bool isFiveOrSixDigit(DWORD ItemID) {
	// Convert ItemID to string to count digits
	std::string strID = std::to_string(ItemID);

	// Check if the length of the string is either 5 or 6
	return (strID.length() == 5 || strID.length() == 6);
}

int PlayerDB_AddItem1(std::shared_ptr<ClientNode> Client, DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status, sql::Connection* conn)
{
	if (!isFiveOrSixDigit(ItemID) || Uin < 10000) return ItemNum;

	int count = 0;
	int OldAvailPeriod = 0;
	BOOL isExist = false;
	int ItemType = 0;
	// ??????????,????? ? ??????
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemNum,AvailPeriod FROM Item  WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ItemID);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			count = res->getInt("ItemNum");
			OldAvailPeriod = res->getInt("AvailPeriod");
			isExist = true;
		}
		res.reset();



		if (isExist) {
			if (GetItemType(ItemID) == EAIT_PROP || GetItemType(ItemID) == EAIT_STONE) {
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET ItemNum = ? WHERE Uin = ? AND ItemID = ? ;"));
				prep_stmt->clearParameters();
				prep_stmt->setInt(1, ItemNum + count);
				prep_stmt->setUInt(2, Uin);
				prep_stmt->setInt(3, ItemID);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}
			else {
				UINT ItemCfgType = GetItemType(ItemID);
				if (ItemCfgType == EAIT_CAR || ItemCfgType == EAIT_SKIN || ItemCfgType == EAIT_CLOTHES) {
					if (AvailPeriod <= 0) {
						std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = ?,ItemNum = 1 WHERE Uin = ? AND ItemID = ? ;"));
						prep_stmt->clearParameters();
						prep_stmt->setInt(1, AvailPeriod);
						prep_stmt->setUInt(2, Uin);
						prep_stmt->setInt(3, ItemID);
						prep_stmt->executeUpdate();
						prep_stmt.reset();
					}
					else if (OldAvailPeriod > 0) {
						std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET ItemNum = 1,AvailPeriod = CASE WHEN UNIX_TIMESTAMP() - ObtainTime < AvailPeriod * 3600 THEN AvailPeriod + ? ELSE ? END,ObtainTime = UNIX_TIMESTAMP() WHERE Uin = ? AND ItemID = ? ;"));
						prep_stmt->clearParameters();
						prep_stmt->setInt(1, AvailPeriod);
						prep_stmt->setInt(2, AvailPeriod);
						prep_stmt->setUInt(3, Uin);
						prep_stmt->setInt(4, ItemID);
						prep_stmt->executeUpdate();
						prep_stmt.reset();
					}
					// OldAvailPeriod <= 0 means the player already owns a permanent car/skin/clothes item; keep it permanent.
				}
				else {
					if (AvailPeriod == -1) {
						std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = -1,ItemNum = 1 WHERE Uin = ? AND ItemID = ? ;"));
						prep_stmt->clearParameters();
						prep_stmt->setUInt(1, Uin);
						prep_stmt->setInt(2, ItemID);
						prep_stmt->executeUpdate();
						prep_stmt.reset();
					}
					else if (AvailPeriod > 0) {
						std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = ?,ObtainTime = UNIX_TIMESTAMP(),ItemNum = 1 WHERE Uin = ? AND ItemID = ? AND AvailPeriod <> -1;"));
						prep_stmt->clearParameters();
						prep_stmt->setInt(1, AvailPeriod);
						prep_stmt->setUInt(2, Uin);
						prep_stmt->setInt(3, ItemID);
						prep_stmt->executeUpdate();
						prep_stmt.reset();
					}
				}
				count = 0;
			}

		}
		else {
			if (GetItemType(ItemID) == EAIT_CAR)
			{
				if (NewKart != 0) {
					goto insert;
				}
				ItemType = 1;
				//????????????????????bug
				int ID = 0;

				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM kartstonenew  WHERE Uin=? AND KartID=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				// ????
				std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
				// ????
				while (res->next()) {
					ID = 1;
				}
				res.reset();
				prep_stmt.reset();


				if (ID == 0)//????????ID? ?????????
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO kartstonenew (Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();

					NotifyUpdateKartSkillStoneInfoNew(Client, ItemID);
				}

				{
					ID = 0;
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM KartRefit WHERE Uin=? AND KartID=?;"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					// ????
					std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
					// ????
					while (res->next()) {
						ID = 1;
					}
					prep_stmt.reset();
					res.reset();
				}
				//????
				//printf("ID:%d\n", ID);
				if (ID == 0)
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO KartRefit(Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();

				}
			}
			else if (GetItemType(ItemID) == EAIT_PET)
			{ // ????
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Pet(Uin,PetId,Name,Experience,PL) VALUES (?,?,?,?,?);"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				prep_stmt->setString(3, G2U(GetItemName(ItemID).c_str()));
				prep_stmt->setInt(4, 238966);
				prep_stmt->setInt(5, 0);
				prep_stmt->executeUpdate();
				prep_stmt.reset();

			}
		insert:
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Item (Uin,ItemID,ItemNum,AvailPeriod,Status,ItemType,ObtainTime) VALUES (?,?,?,?,?,?,UNIX_TIMESTAMP());"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ItemID);
			prep_stmt->setInt(3, ItemNum);
			prep_stmt->setInt(4, AvailPeriod);
			prep_stmt->setInt(5, Status);
			prep_stmt->setInt(6, ItemType);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_AddItem1 Error" << std::endl;
		//throw;??
	}

	return ItemNum + count;
}

int PlayerDB_AddItem2(std::shared_ptr<ClientNode> Client, DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status, bool BypassTreasureLimit, bool* Success)
{
	return PlayerDB_AddItem2Txn(Client, Uin, ItemID, ItemNum, AvailPeriod, Status, BypassTreasureLimit, Success, nullptr, nullptr);
}

int PlayerDB_AddItem2Txn(std::shared_ptr<ClientNode> Client, DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status, bool BypassTreasureLimit, bool* Success, sql::Connection* transaction, std::vector<UINT>* deferredKartNotifications)
{
	if (Success) *Success = false;
	if (!isFiveOrSixDigit(ItemID) || Uin < 10000) return ItemNum;
	(void)BypassTreasureLimit;

	int count = 0;
	int OldAvailPeriod = 0;
	BOOL isExist = false;
	int ItemType = 0;
	// 优先使用调用方事务；普通调用自行获取连接并提交。
	std::unique_ptr<DBConnection> ownedConnection;
	auto conn = transaction;
	if (!conn) {
		ownedConnection = std::make_unique<DBConnection>(player);
		conn = ownedConnection->getConnection();
		if (!conn) return ItemNum;
		conn->setAutoCommit(false);
	}
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(transaction
			? "SELECT ItemNum,AvailPeriod FROM Item WHERE Uin=? AND ItemID=? FOR UPDATE;"
			: "SELECT ItemNum,AvailPeriod FROM Item  WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ItemID);

		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next()) {
			count = res->getInt("ItemNum");
			OldAvailPeriod = res->getInt("AvailPeriod");
			isExist = true;
		}
		res.reset();



		if (isExist) {
			if (GetItemType(ItemID) == EAIT_PROP || GetItemType(ItemID) == EAIT_STONE) {
				if (transaction && (count < 0 || static_cast<unsigned long long>(ItemNum) + count > (std::numeric_limits<int>::max)()))
					throw std::runtime_error("item count overflow");
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET ItemNum = ? WHERE Uin = ? AND ItemID = ? ;"));
				prep_stmt->clearParameters();
				prep_stmt->setInt(1, ItemNum + count);
				prep_stmt->setUInt(2, Uin);
				prep_stmt->setInt(3, ItemID);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}
			else {
				if (AvailPeriod == -1) {
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = -1,ItemNum = 1 WHERE Uin = ? AND ItemID = ? ;"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}
				else if (AvailPeriod > 0 && OldAvailPeriod != -1) {
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET ItemNum = 1,AvailPeriod = CASE WHEN UNIX_TIMESTAMP() - ObtainTime < AvailPeriod * 3600 THEN AvailPeriod + ? ELSE ? END,ObtainTime = UNIX_TIMESTAMP() WHERE Uin = ? AND ItemID = ? AND AvailPeriod <> -1;"));
					prep_stmt->clearParameters();
					prep_stmt->setInt(1, AvailPeriod);
					prep_stmt->setInt(2, AvailPeriod);
					prep_stmt->setUInt(3, Uin);
					prep_stmt->setInt(4, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}
				count = 0;
			}

		}
		else {
			if (GetItemType(ItemID) == EAIT_CAR)
			{
				ItemType = 1;
				// 首次获得赛车时补齐宝石槽和改装记录。
				int ID = 0;

				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM kartstonenew  WHERE Uin=? AND KartID=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
				while (res->next()) {
					ID = 1;
				}
				res.reset();
				prep_stmt.reset();


				if (ID == 0) // 缺少宝石槽记录时创建。
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO kartstonenew (Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();

					if (transaction) {
						if (deferredKartNotifications) deferredKartNotifications->push_back(ItemID);
					}
					else NotifyUpdateKartSkillStoneInfoNew(Client, ItemID);
				}

				{
					ID = 0;
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM KartRefit WHERE Uin=? AND KartID=?;"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
					while (res->next()) {
						ID = 1;
					}
					prep_stmt.reset();
					res.reset();
				}

				//printf("ID:%d\n", ID);
				if (ID == 0)
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO KartRefit(Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();

				}
			}
			else if (GetItemType(ItemID) == EAIT_PET)
			{ // 首次获得宠物时创建宠物资料。
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Pet(Uin,PetId,Name,Experience,PL) VALUES (?,?,?,?,?);"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				prep_stmt->setString(3, G2U(GetItemName(ItemID).c_str()));
				prep_stmt->setInt(4, 238966);
				prep_stmt->setInt(5, 0);
				prep_stmt->executeUpdate();
				prep_stmt.reset();

			}
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Item (Uin,ItemID,ItemNum,AvailPeriod,Status,ItemType,ObtainTime) VALUES (?,?,?,?,?,?,UNIX_TIMESTAMP());"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ItemID);
			prep_stmt->setInt(3, ItemNum);
			prep_stmt->setInt(4, AvailPeriod);
			prep_stmt->setInt(5, Status);
			prep_stmt->setInt(6, ItemType);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}
		if (!transaction) {
			conn->commit();
			conn->setAutoCommit(true);
		}
		if (Success) *Success = true;
	}
	catch (const std::exception& e)
	{
		if (transaction) throw; // 外部调用方负责整批事务回滚。
		try { conn->rollback(); conn->setAutoCommit(true); }
		catch (...) {}
		std::cerr << e.what() << "PlayerDB_AddItem2 Error" << std::endl;
		// 保留旧接口返回行为，调用方可通过 Success 检查是否成功。
	}

	return ItemNum + count;
}

int PlayerDB_AddItem3(std::shared_ptr<ClientNode> Client, DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status, bool Skip)
{
	if (!isFiveOrSixDigit(ItemID) || Uin < 10000) return ItemNum;

	int count = 0;
	BOOL isExist = false;
	int ItemType = 0;
	// ??????????,????? ? ??????
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return ItemNum;
	conn->setAutoCommit(false);
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemNum FROM Item  WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ItemID);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			count = res->getInt("ItemNum");
			isExist = true;
		}
		res.reset();



		if (isExist) {
			if (GetItemType(ItemID) == EAIT_PROP || GetItemType(ItemID) == EAIT_STONE) {
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET ItemNum = ? WHERE Uin = ? AND ItemID = ? ;"));
				prep_stmt->clearParameters();
				prep_stmt->setInt(1, ItemNum + count);
				prep_stmt->setUInt(2, Uin);
				prep_stmt->setInt(3, ItemID);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}
			else {
				if (AvailPeriod == -1) {
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = -1,ItemNum = 1 WHERE Uin = ? AND ItemID = ? ;"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}
				else if (AvailPeriod > 0) {
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = ?,ObtainTime = UNIX_TIMESTAMP(),ItemNum = 1 WHERE Uin = ? AND ItemID = ?;"));
					prep_stmt->clearParameters();
					prep_stmt->setInt(1, AvailPeriod);
					prep_stmt->setUInt(2, Uin);
					prep_stmt->setInt(3, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}
				count = 0;
			}

		}
		else {
			if (GetItemType(ItemID) == EAIT_CAR && !Skip)
			{
				ItemType = 1;
				//????????????????????bug
				int ID = 0;

				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM kartstonenew  WHERE Uin=? AND KartID=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				// ????
				std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
				// ????
				while (res->next()) {
					ID = 1;
				}
				res.reset();
				prep_stmt.reset();


				if (ID == 0)//????????ID? ?????????
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO kartstonenew (Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
					NotifyUpdateKartSkillStoneInfoNew(Client, ItemID);
				}

				{
					ID = 0;
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM KartRefit WHERE Uin=? AND KartID=?;"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					// ????
					std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
					// ????
					while (res->next()) {
						ID = 1;
					}
					prep_stmt.reset();
					res.reset();
				}
				//????
				//printf("ID:%d\n", ID);
				if (ID == 0)
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO KartRefit(Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();

				}
			}
			else if (GetItemType(ItemID) == EAIT_PET)
			{ // ????
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Pet(Uin,PetId,Name,Experience,PL) VALUES (?,?,?,?,?);"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				prep_stmt->setString(3, G2U(GetItemName(ItemID).c_str()));
				prep_stmt->setInt(4, 238966);
				prep_stmt->setInt(5, 0);
				prep_stmt->executeUpdate();
				prep_stmt.reset();

			}
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Item (Uin,ItemID,ItemNum,AvailPeriod,Status,ItemType,ObtainTime) VALUES (?,?,?,?,?,?,UNIX_TIMESTAMP());"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ItemID);
			prep_stmt->setInt(3, ItemNum);
			prep_stmt->setInt(4, AvailPeriod);
			prep_stmt->setInt(5, Status);
			prep_stmt->setInt(6, ItemType);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}
		conn->commit();
		conn->setAutoCommit(true);
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_AddItem3 Error" << std::endl;
		//throw;??
	}

	return ItemNum + count;
}


int PlayerDB_AddItem4(DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status, bool BypassTreasureLimit, bool* Success)
{
	if (Success) *Success = false;
	if (!isFiveOrSixDigit(ItemID) || Uin < 10000) return ItemNum;
	(void)BypassTreasureLimit;

	int count = 0;
	BOOL isExist = false;
	int ItemType = 0;
	// ??????????,????? ? ??????
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return ItemNum;
	conn->setAutoCommit(false);
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemNum FROM Item  WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ItemID);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			count = res->getInt("ItemNum");
			isExist = true;
		}
		res.reset();


		if (isExist) {
			if (GetItemType(ItemID) == EAIT_PROP || GetItemType(ItemID) == EAIT_STONE) {
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET ItemNum = ? WHERE Uin = ? AND ItemID = ? ;"));
				prep_stmt->clearParameters();
				prep_stmt->setInt(1, ItemNum + count);
				prep_stmt->setUInt(2, Uin);
				prep_stmt->setInt(3, ItemID);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}
			else {
				if (AvailPeriod == -1) {
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = -1,ItemNum = 1 WHERE Uin = ? AND ItemID = ? ;"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}
				else if (AvailPeriod > 0) {
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = ?,ObtainTime = UNIX_TIMESTAMP(),ItemNum = 1 WHERE Uin = ? AND ItemID = ?;"));
					prep_stmt->clearParameters();
					prep_stmt->setInt(1, AvailPeriod);
					prep_stmt->setUInt(2, Uin);
					prep_stmt->setInt(3, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}
				count = 0;
			}

		}
		else {
			if (GetItemType(ItemID) == EAIT_CAR)
			{
				ItemType = 1;
				//????????????????????bug
				int ID = 0;

				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM kartstonenew  WHERE Uin=? AND KartID=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				// ????
				std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
				// ????
				while (res->next()) {
					ID = 1;
				}
				res.reset();
				prep_stmt.reset();


				if (ID == 0)//????????ID? ?????????
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO kartstonenew (Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}

				{
					ID = 0;
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM KartRefit WHERE Uin=? AND KartID=?;"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					// ????
					std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
					// ????
					while (res->next()) {
						ID = 1;
					}
					prep_stmt.reset();
					res.reset();
				}
				//????
				//printf("ID:%d\n", ID);
				if (ID == 0)
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO KartRefit(Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();

				}
			}
			else if (GetItemType(ItemID) == EAIT_PET)
			{ // ????
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Pet(Uin,PetId,Name,Experience,PL) VALUES (?,?,?,?,?);"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				prep_stmt->setString(3, G2U(GetItemName(ItemID).c_str()));
				prep_stmt->setInt(4, 238966);
				prep_stmt->setInt(5, 0);
				prep_stmt->executeUpdate();
				prep_stmt.reset();

			}
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Item (Uin,ItemID,ItemNum,AvailPeriod,Status,ItemType,ObtainTime) VALUES (?,?,?,?,?,?,UNIX_TIMESTAMP());"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ItemID);
			prep_stmt->setInt(3, ItemNum);
			prep_stmt->setInt(4, AvailPeriod);
			prep_stmt->setInt(5, Status);
			prep_stmt->setInt(6, ItemType);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}
		conn->commit();
		conn->setAutoCommit(true);
		if (Success) *Success = true;
	}
	catch (const std::exception& e)
	{
		try { conn->rollback(); conn->setAutoCommit(true); }
		catch (...) {}
		std::cerr << e.what() << "PlayerDB_AddItem4 Error" << std::endl;
		//throw;??
	}

	return ItemNum + count;

}

int PlayerDB_AddItem5(DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status, sql::Connection* conn, bool* Success)
{
	if (Success) *Success = false;
	if (!isFiveOrSixDigit(ItemID) || Uin < 10000 || !conn) return ItemNum;

	int count = 0;
	int OldAvailPeriod = 0;
	BOOL isExist = false;
	int ItemType = 0;
	// ??????????,????? ? ??????
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemNum,AvailPeriod FROM Item  WHERE Uin=? AND ItemID=? FOR UPDATE;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ItemID);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			count = res->getInt("ItemNum");
			OldAvailPeriod = res->getInt("AvailPeriod");
			isExist = true;
		}
		res.reset();



		if (isExist) {
			if (GetItemType(ItemID) == EAIT_PROP || GetItemType(ItemID) == EAIT_STONE) {
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET ItemNum = ? WHERE Uin = ? AND ItemID = ? ;"));
				prep_stmt->clearParameters();
				prep_stmt->setInt(1, ItemNum + count);
				prep_stmt->setUInt(2, Uin);
				prep_stmt->setInt(3, ItemID);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}
			else {
				if (AvailPeriod == -1) {
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = -1,ItemNum = 1 WHERE Uin = ? AND ItemID = ? ;"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}
				else if (AvailPeriod > 0 && OldAvailPeriod > 0) {
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET AvailPeriod = ?,ObtainTime = UNIX_TIMESTAMP(),ItemNum = 1 WHERE Uin = ? AND ItemID = ?;"));
					prep_stmt->clearParameters();
					prep_stmt->setInt(1, AvailPeriod);
					prep_stmt->setUInt(2, Uin);
					prep_stmt->setInt(3, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}
				count = 0;
			}

		}
		else {
			if (GetItemType(ItemID) == EAIT_CAR)
			{
				ItemType = 1;
				//????????????????????bug
				int ID = 0;

				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM kartstonenew  WHERE Uin=? AND KartID=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				// ????
				std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
				// ????
				while (res->next()) {
					ID = 1;
				}
				res.reset();
				prep_stmt.reset();


				if (ID == 0)//????????ID? ?????????
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO kartstonenew (Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
					//NotifyUpdateKartSkillStoneInfoNew(Client, ItemID);
				}

				{
					ID = 0;
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM KartRefit WHERE Uin=? AND KartID=?;"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					// ????
					std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
					// ????
					while (res->next()) {
						ID = 1;
					}
					prep_stmt.reset();
					res.reset();
				}
				//????
				//printf("ID:%d\n", ID);
				if (ID == 0)
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO KartRefit(Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();

				}
			}
			else if (GetItemType(ItemID) == EAIT_PET)
			{ // ????
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Pet(Uin,PetId,Name,Experience,PL) VALUES (?,?,?,?,?);"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				prep_stmt->setString(3, G2U(GetItemName(ItemID).c_str()));
				prep_stmt->setInt(4, 238966);
				prep_stmt->setInt(5, 0);
				prep_stmt->executeUpdate();
				prep_stmt.reset();

			}
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Item (Uin,ItemID,ItemNum,AvailPeriod,Status,ItemType,ObtainTime) VALUES (?,?,?,?,?,?,UNIX_TIMESTAMP());"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ItemID);
			prep_stmt->setInt(3, ItemNum);
			prep_stmt->setInt(4, AvailPeriod);
			prep_stmt->setInt(5, Status);
			prep_stmt->setInt(6, ItemType);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}

		if (Success) *Success = true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_AddItem5 Error" << std::endl;
		//throw;??
	}

	return ItemNum + count;

}

int PlayerDB_AddItem5Batch(const std::vector<UINT>& Uins, DWORD ItemID, DWORD ItemNum, int AvailPeriod, bool Status)
{
	if (Uins.empty()) return 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	int success = 0;
	for (UINT uin : Uins)
	{
		if (uin < 10000) continue;
		PlayerDB_AddItem5(uin, ItemID, ItemNum, AvailPeriod, Status, conn);
		++success;
	}
	return success;
}

// ????
std::unordered_map<UINT, std::mutex> mutex_map;

// ????????
std::mutex& get_or_create_mutex(UINT uin) {
	// ?? std::lock_guard ???????
	static std::mutex map_mutex; // ?? map ???
	std::lock_guard<std::mutex> guard(map_mutex);

	// ?? uin ????????,????
	auto it = mutex_map.find(uin);
	if (it != mutex_map.end()) {
		return it->second;
	}

	// ??,????????? map ?
	auto& new_mutex = mutex_map[uin]; // std::unordered_map ? operator[] ?????
	return new_mutex;
}

int PlayerDB_GetItemAvailPeriod(UINT Uin, UINT Itemid)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	int AvailPeriod = 0;
	// ??????????,????? ? ??????
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("select AvailPeriod from item  where uin = ? And Itemid =? "));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, Itemid);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			AvailPeriod = res->getInt("AvailPeriod");
		}
		return AvailPeriod;
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetStoneCar Error" << std::endl;
		//throw;??
	}

	return 1;

}



int PlayerDB_AddStoneCar(std::shared_ptr<ClientNode> Client)
{
	if (Client->Uin < 10000) return 0;

	// ???
	std::mutex& mtx = get_or_create_mutex(Client->Uin);
	std::lock_guard<std::mutex> lock(mtx);

	//std::cout << Client->Uin << std::endl;
	std::vector<UINT> items;

	PlayerDB_GetStone(Client->Uin, items);
	for (size_t i = 0; i < items.size(); i++)
	{
		if (GetItemType(items[i]) == EAIT_CAR) {
			NotifyUpdateKartSkillStoneInfoNew(Client, items[i]);
		}
	}
	if (items.size() > 0) {
		PlayerDB_AddKartStoneGroove(Client->Uin, items);
	}


	items.clear();
	PlayerDB_GetKartRefit(Client->Uin, items);
	if (items.size() > 0) {
		PlayerDB_AddKartRefit(Client->Uin, items);
	}
	return 1;
}



int PlayerDB_GetStone(UINT Uin, std::vector<UINT>& vecItem)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	// ??????????,????? ? ??????
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("select itemid from item a,itemallnew b where a.Itemid = b.id and b.Type = 'EAIT_CAR' and a.uin = ? and a.Itemid not in(select kartid from kartstonenew where uin = ?) "));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			UINT ItemID = res->getInt("Itemid");
			vecItem.push_back(ItemID);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetStoneCar Error" << std::endl;
		//throw;??
	}

	return 1;

}


int PlayerDB_GetKartRefit(UINT Uin, std::vector<UINT>& vecItem)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	// ??????????,????? ? ??????
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("select itemid from item a,itemallnew b where a.Itemid = b.id and b.Type = 'EAIT_CAR' and a.uin = ? and a.Itemid not in(select kartid from kartrefit where uin = ?)"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			UINT ItemID = res->getInt("Itemid");
			vecItem.push_back(ItemID);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetStoneCar Error" << std::endl;
		//throw;??
	}

	return 1;

}

int PlayerDB_AddKartStoneGroove(UINT Uin, std::vector<UINT> vecItem)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	conn->setAutoCommit(false);
	// ??????????,????? ? ??????
	try
	{
		// ???????
		//auto start = std::chrono::high_resolution_clock::now();

		for (size_t i = 0; i < vecItem.size(); i++)
		{
			UINT ItemID = vecItem[i];
			if (GetItemType(ItemID) == EAIT_CAR)
			{
				int ItemType = 1;
				//????????????????????bug
				int ID = 0;

				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM kartstonenew  WHERE Uin=? AND KartID=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				// ????
				std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
				// ????
				if (res->rowsCount() > 0) {
					ID = 1;
				}
				res.reset();
				prep_stmt.reset();


				if (ID == 0)//????????ID? ?????????
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO kartstonenew (Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();
				}
			}
		}
		conn->commit();
		conn->setAutoCommit(true);

		// ???????
		//auto end = std::chrono::high_resolution_clock::now();

		// ?????
		//auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

		// ??????
		//std::cout << "??????: " << duration.count() << " ??" << std::endl;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_AddKartStoneGroove Error" << std::endl;
		//throw;??
	}

	return 1;

}

int PlayerDB_AddKartRefit(UINT Uin, std::vector<UINT> vecItem)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	conn->setAutoCommit(false);
	// ??????????,????? ? ??????
	try
	{
		// ???????
		//auto start = std::chrono::high_resolution_clock::now();
		for (size_t i = 0; i < vecItem.size(); i++)
		{
			UINT ItemID = vecItem[i];
			if (GetItemType(ItemID) == EAIT_CAR)
			{
				int ItemType = 1;
				//????????????????????bug
				int ID = 0;

				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM KartRefit WHERE Uin=? AND KartID=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				// ????
				std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
				// ????
				if (res->rowsCount() > 0) {
					ID = 1;
				}
				prep_stmt.reset();
				res.reset();

				//????
				//printf("ID:%d\n", ID);
				if (ID == 0)
				{
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO KartRefit(Uin,KartID) VALUES (?,?);"));
					prep_stmt->clearParameters();
					prep_stmt->setUInt(1, Uin);
					prep_stmt->setInt(2, ItemID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();

				}
			}
		}
		conn->commit();
		conn->setAutoCommit(true);
		// ???????
		//auto end = std::chrono::high_resolution_clock::now();

		// ?????
		//auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

		// ??????
		//std::cout << "??????: " << duration.count() << " ??" << std::endl;

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_AddKartRefit Error" << std::endl;
		//throw;??
	}
	return 1;
}

DWORD PlayerDB_GetMapRecord(DWORD Uin, int MapID, std::shared_ptr<ClientNode> Client)
{
	int ret = -1;
	// ??????????,????? ? ??????
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Record  FROM MapRecord  WHERE Uin=? AND MapID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, MapID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			ret = res->getInt("Record");
		}
		res.reset();

		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetMapRecord Error" << std::endl;
		//throw;??
	}
}

bool PlayerDB_SetMapRecord(DWORD Uin, int MapID, DWORD Record, std::shared_ptr<ClientNode> Client)
{
	bool NewRecord = true;
	// ??????????,????? ? ??????
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Record  FROM MapRecord  WHERE Uin=? AND MapID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, MapID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());

		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0)
		{
			while (res->next()) {
				//printf("%d\n", res->getInt("Record"));
				if (res->getInt("Record") < Record) {
					NewRecord = false;
				}
				else {
					std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE MapRecord SET Record=?  WHERE Uin=? AND MapID=?;"));
					prep_stmt->clearParameters();
					prep_stmt->setInt(1, Record);
					prep_stmt->setUInt(2, Uin);
					prep_stmt->setInt(3, MapID);
					prep_stmt->executeUpdate();
					prep_stmt.reset();

				}
				break;
			}
		}
		else
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO MapRecord (Uin, MapID, Record) VALUES(?, ?, ?);"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, MapID);
			prep_stmt->setInt(3, Record);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}
		res.reset();

		return NewRecord;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SetMapRecord Error" << std::endl;
		//throw;??
	}
}

bool PlayerDB_GetSpeedLadderPlayerMap(DWORD Uin, UINT SeasonID, UINT MapID, SpeedLadderPlayerMapRow& Row)
{
	Row = SpeedLadderPlayerMapRow();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT Uin,SeasonID,MapID,BestTime,TrophyLevel,TrophyCount,Star1Done,Star2Done,Star3Done,AssessDone,RecordSource,ReplayMd5,ReplayPath,UpdatedAt "
			"FROM SpeedLadderPlayerMap WHERE Uin=? AND SeasonID=? AND MapID=? ORDER BY RecordSource DESC LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, SeasonID);
		prep_stmt->setUInt(3, MapID);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		if (!res->next()) return false;
		Row.Uin = res->getUInt("Uin");
		Row.SeasonID = res->getUInt("SeasonID");
		Row.MapID = res->getUInt("MapID");
		Row.BestTime = res->getUInt("BestTime");
		Row.TrophyLevel = static_cast<BYTE>(res->getUInt("TrophyLevel"));
		Row.TrophyCount = static_cast<BYTE>(res->getUInt("TrophyCount"));
		Row.Star1Done = static_cast<BYTE>(res->getUInt("Star1Done"));
		Row.Star2Done = static_cast<BYTE>(res->getUInt("Star2Done"));
		Row.Star3Done = static_cast<BYTE>(res->getUInt("Star3Done"));
		Row.AssessDone = static_cast<BYTE>(res->getUInt("AssessDone"));
		Row.RecordSource = static_cast<BYTE>(res->getUInt("RecordSource"));
		Row.ReplayMd5 = res->getString("ReplayMd5");
		Row.ReplayPath = res->getString("ReplayPath");
		Row.UpdatedAt = res->getUInt("UpdatedAt");
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetSpeedLadderPlayerMap Error" << std::endl;
	}
	return false;
}

bool PlayerDB_GetSpeedLadderPlayerMapBySource(DWORD Uin, UINT SeasonID, UINT MapID, BYTE RecordSource, SpeedLadderPlayerMapRow& Row)
{
	Row = SpeedLadderPlayerMapRow();
	if (RecordSource > SPEED_LADDER_RECORD_SOURCE_RANKED_TIME_RACE) RecordSource = SPEED_LADDER_RECORD_SOURCE_LEGACY;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT Uin,SeasonID,MapID,BestTime,TrophyLevel,TrophyCount,Star1Done,Star2Done,Star3Done,AssessDone,RecordSource,ReplayMd5,ReplayPath,UpdatedAt "
			"FROM SpeedLadderPlayerMap WHERE Uin=? AND SeasonID=? AND MapID=? AND RecordSource=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, SeasonID);
		prep_stmt->setUInt(3, MapID);
		prep_stmt->setUInt(4, RecordSource);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		if (!res->next()) return false;
		Row.Uin = res->getUInt("Uin");
		Row.SeasonID = res->getUInt("SeasonID");
		Row.MapID = res->getUInt("MapID");
		Row.BestTime = res->getUInt("BestTime");
		Row.TrophyLevel = static_cast<BYTE>(res->getUInt("TrophyLevel"));
		Row.TrophyCount = static_cast<BYTE>(res->getUInt("TrophyCount"));
		Row.Star1Done = static_cast<BYTE>(res->getUInt("Star1Done"));
		Row.Star2Done = static_cast<BYTE>(res->getUInt("Star2Done"));
		Row.Star3Done = static_cast<BYTE>(res->getUInt("Star3Done"));
		Row.AssessDone = static_cast<BYTE>(res->getUInt("AssessDone"));
		Row.RecordSource = static_cast<BYTE>(res->getUInt("RecordSource"));
		Row.ReplayMd5 = res->getString("ReplayMd5");
		Row.ReplayPath = res->getString("ReplayPath");
		Row.UpdatedAt = res->getUInt("UpdatedAt");
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetSpeedLadderPlayerMapBySource Error" << std::endl;
	}
	return false;
}

bool PlayerDB_GetSpeedLadderPlayerMapsBySource(DWORD Uin, UINT SeasonID, BYTE RecordSource, std::vector<SpeedLadderPlayerMapRow>& Rows)
{
	Rows.clear();
	if (Uin == 0 || SeasonID == 0) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT Uin,SeasonID,MapID,BestTime,TrophyLevel,TrophyCount,Star1Done,Star2Done,Star3Done,AssessDone,RecordSource,ReplayMd5,ReplayPath,UpdatedAt "
			"FROM SpeedLadderPlayerMap WHERE Uin=? AND SeasonID=? AND RecordSource=? ORDER BY UpdatedAt DESC, MapID ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, SeasonID);
		prep_stmt->setUInt(3, RecordSource);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		while (res->next())
		{
			SpeedLadderPlayerMapRow row;
			row.Uin = res->getUInt("Uin");
			row.SeasonID = res->getUInt("SeasonID");
			row.MapID = res->getUInt("MapID");
			row.BestTime = res->getUInt("BestTime");
			row.TrophyLevel = static_cast<BYTE>(res->getUInt("TrophyLevel"));
			row.TrophyCount = static_cast<BYTE>(res->getUInt("TrophyCount"));
			row.Star1Done = static_cast<BYTE>(res->getUInt("Star1Done"));
			row.Star2Done = static_cast<BYTE>(res->getUInt("Star2Done"));
			row.Star3Done = static_cast<BYTE>(res->getUInt("Star3Done"));
			row.AssessDone = static_cast<BYTE>(res->getUInt("AssessDone"));
			row.RecordSource = static_cast<BYTE>(res->getUInt("RecordSource"));
			row.ReplayMd5 = res->getString("ReplayMd5");
			row.ReplayPath = res->getString("ReplayPath");
			row.UpdatedAt = res->getUInt("UpdatedAt");
			Rows.push_back(row);
		}
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetSpeedLadderPlayerMapsBySource Error" << std::endl;
	}
	return false;
}

bool PlayerDB_UpsertSpeedLadderPlayerMap(DWORD Uin, UINT SeasonID, UINT MapID, UINT BestTime, BYTE TrophyLevel, BYTE TrophyCount, BYTE Star1Done, BYTE Star2Done, BYTE Star3Done, BYTE AssessDone, BYTE RecordSource, const std::string& ReplayMd5, const std::string& ReplayPath)
{
	if (Uin == 0 || MapID == 0 || BestTime == 0 || BestTime == 0xFFFFFFFF) return false;
	if (RecordSource > SPEED_LADDER_RECORD_SOURCE_RANKED_TIME_RACE) RecordSource = SPEED_LADDER_RECORD_SOURCE_LEGACY;
	SpeedLadderPlayerMapRow oldRow;
	const bool exists = PlayerDB_GetSpeedLadderPlayerMapBySource(Uin, SeasonID, MapID, RecordSource, oldRow);
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		const UINT nowTime = static_cast<UINT>(time(nullptr));
		if (!exists)
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
				"INSERT INTO SpeedLadderPlayerMap "
				"(Uin,SeasonID,MapID,BestTime,TrophyLevel,TrophyCount,Star1Done,Star2Done,Star3Done,AssessDone,RecordSource,ReplayMd5,ReplayPath,UpdatedAt) "
				"VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?);"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setUInt(2, SeasonID);
			prep_stmt->setUInt(3, MapID);
			prep_stmt->setUInt(4, BestTime);
			prep_stmt->setUInt(5, TrophyLevel);
			prep_stmt->setUInt(6, TrophyCount);
			prep_stmt->setUInt(7, Star1Done ? 1 : 0);
			prep_stmt->setUInt(8, Star2Done ? 1 : 0);
			prep_stmt->setUInt(9, Star3Done ? 1 : 0);
			prep_stmt->setUInt(10, AssessDone ? 1 : 0);
			prep_stmt->setUInt(11, RecordSource);
			prep_stmt->setString(12, ReplayMd5);
			prep_stmt->setString(13, ReplayPath);
			prep_stmt->setUInt(14, nowTime);
			prep_stmt->executeUpdate();
			return true;
		}

		const bool betterTime = (oldRow.BestTime == 0 || BestTime < oldRow.BestTime);
		const UINT newBestTime = betterTime ? BestTime : oldRow.BestTime;
		const BYTE newTrophyLevel = betterTime ? TrophyLevel : std::max<BYTE>(oldRow.TrophyLevel, TrophyLevel);
		const BYTE newTrophyCount = betterTime ? TrophyCount : std::max<BYTE>(oldRow.TrophyCount, TrophyCount);
		const BYTE newStar1Done = (oldRow.Star1Done || Star1Done) ? 1 : 0;
		const BYTE newStar2Done = (oldRow.Star2Done || Star2Done) ? 1 : 0;
		const BYTE newStar3Done = (oldRow.Star3Done || Star3Done) ? 1 : 0;
		const BYTE newAssessDone = (oldRow.AssessDone || AssessDone) ? 1 : 0;
		const std::string newReplayMd5 = (betterTime && !ReplayMd5.empty()) ? ReplayMd5 : oldRow.ReplayMd5;
		const std::string newReplayPath = (betterTime && !ReplayPath.empty()) ? ReplayPath : oldRow.ReplayPath;

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"UPDATE SpeedLadderPlayerMap SET BestTime=?,TrophyLevel=?,TrophyCount=?,Star1Done=?,Star2Done=?,Star3Done=?,AssessDone=?,ReplayMd5=?,ReplayPath=?,UpdatedAt=? "
			"WHERE Uin=? AND SeasonID=? AND MapID=? AND RecordSource=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, newBestTime);
		prep_stmt->setUInt(2, newTrophyLevel);
		prep_stmt->setUInt(3, newTrophyCount);
		prep_stmt->setUInt(4, newStar1Done);
		prep_stmt->setUInt(5, newStar2Done);
		prep_stmt->setUInt(6, newStar3Done);
		prep_stmt->setUInt(7, newAssessDone);
		prep_stmt->setString(8, newReplayMd5);
		prep_stmt->setString(9, newReplayPath);
		prep_stmt->setUInt(10, nowTime);
		prep_stmt->setUInt(11, Uin);
		prep_stmt->setUInt(12, SeasonID);
		prep_stmt->setUInt(13, MapID);
		prep_stmt->setUInt(14, RecordSource);
		prep_stmt->executeUpdate();
		return betterTime;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpsertSpeedLadderPlayerMap Error" << std::endl;
	}
	return false;
}

UINT PlayerDB_GetSpeedLadderTotalTrophyCount(UINT Uin, UINT SeasonID)
{
	if (Uin == 0 || SeasonID == 0)
		return 0;

	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT COALESCE(SUM(TrophyCount),0) AS TotalTrophy FROM SpeedLadderPlayerMap WHERE Uin=? AND SeasonID=? AND RecordSource=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, SeasonID);
		prep_stmt->setUInt(3, SPEED_LADDER_RECORD_SOURCE_SPEED_LADDER);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			return res->getUInt("TotalTrophy");
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetSpeedLadderTotalTrophyCount Error" << std::endl;
	}
	return 0;
}

static constexpr size_t kSpeedLadderRewardClaimBytes = 32; // 150 official rewards, keep room for 256 bits.

static bool QQS_TestFlagBit(const BYTE* Flags, size_t FlagLen, UINT BitIndex)
{
	if (!Flags || FlagLen == 0) return false;
	const size_t byteIndex = BitIndex >> 3;
	if (byteIndex >= FlagLen) return false;
	return (Flags[byteIndex] & (1u << (BitIndex & 7))) != 0;
}

static bool QQS_SetFlagBit(BYTE* Flags, size_t FlagLen, UINT BitIndex)
{
	if (!Flags || FlagLen == 0) return false;
	const size_t byteIndex = BitIndex >> 3;
	if (byteIndex >= FlagLen) return false;
	Flags[byteIndex] = static_cast<BYTE>(Flags[byteIndex] | (1u << (BitIndex & 7)));
	return true;
}

static bool PlayerDB_LoadSpeedLadderRewardClaims(UINT Uin, UINT SeasonID, BYTE* Flags, size_t FlagLen)
{
	if (Flags && FlagLen > 0)
		memset(Flags, 0, FlagLen);
	if (!Flags || FlagLen == 0 || Uin == 0 || SeasonID == 0)
		return false;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT SpeedLadderRewardSeasonID,SpeedLadderRewardClaimHex FROM BaseInfo WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (!res->next())
			return false;

		const UINT savedSeasonID = res->getUInt("SpeedLadderRewardSeasonID");
		if (savedSeasonID != SeasonID)
			return true; // new season: empty claim mask

		std::string claimHex = res->getString("SpeedLadderRewardClaimHex");
		QQS_ClientSavedFlagsFromHex(claimHex, Flags, FlagLen);
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_LoadSpeedLadderRewardClaims Error" << std::endl;
	}
	return false;
}

bool PlayerDB_GetSpeedLadderRewardClaimed(UINT Uin, UINT SeasonID, UINT PosKey)
{
	if (PosKey == 0)
		return false;
	BYTE flags[kSpeedLadderRewardClaimBytes] = {};
	if (!PlayerDB_LoadSpeedLadderRewardClaims(Uin, SeasonID, flags, sizeof(flags)))
		return false;
	return QQS_TestFlagBit(flags, sizeof(flags), PosKey - 1);
}

bool PlayerDB_SetSpeedLadderRewardClaimed(UINT Uin, UINT SeasonID, UINT PosKey)
{
	if (Uin == 0 || SeasonID == 0 || PosKey == 0)
		return false;

	BYTE flags[kSpeedLadderRewardClaimBytes] = {};
	PlayerDB_LoadSpeedLadderRewardClaims(Uin, SeasonID, flags, sizeof(flags));
	if (!QQS_SetFlagBit(flags, sizeof(flags), PosKey - 1))
		return false;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::string claimHex = QQS_ClientSavedFlagsToHex(flags, sizeof(flags));
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"UPDATE BaseInfo SET SpeedLadderRewardSeasonID=?,SpeedLadderRewardClaimHex=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, SeasonID);
		prep_stmt->setString(2, claimHex);
		prep_stmt->setUInt(3, Uin);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SetSpeedLadderRewardClaimed Error" << std::endl;
	}
	return false;
}

bool PlayerDB_ResetSpeedLadderRewardClaims(UINT Uin, UINT SeasonID)
{
	if (Uin == 0 || SeasonID == 0)
		return false;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"UPDATE BaseInfo SET SpeedLadderRewardSeasonID=?,SpeedLadderRewardClaimHex='' WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, SeasonID);
		prep_stmt->setUInt(2, Uin);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_ResetSpeedLadderRewardClaims Error" << std::endl;
	}
	return false;
}

static void RankReadSeasonState(sql::ResultSet* result, RankSeasonStateRow& state)
{
	state.Uin = result->getUInt("Uin");
	state.SeasonID = result->getUInt("SeasonID");
	state.SeasonRaceNums = result->getUInt("SeasonRaceNums");
	state.WinNums = result->getUInt("WinNums");
	state.FinishNums = result->getUInt("FinishNums");
	state.TotalRankIndex = result->getUInt("TotalRankIndex");
	state.ContinFinishNums = result->getUInt("ContinFinishNums");
	state.MaxContinFinishNums = result->getUInt("MaxContinFinishNums");
	state.MaxContinWinNums = result->getUInt("MaxContinWinNums");
	state.KillAllNums = result->getUInt("KillAllNums");
	state.LastRankIndex = result->getUInt("LastRankIndex");
	state.RMTopRaceScore = result->getUInt("RMTopRaceScore");
	state.DailyDate = result->getUInt("DailyDate");
	state.DailyRaceNums = result->getUInt("DailyRaceNums");
	state.DailyWinNums = result->getUInt("DailyWinNums");
	state.ContinWinNums = result->getUInt("ContinWinNums");
	state.DailyTaskClaimMask = result->getUInt("DailyTaskClaimMask");
	state.BraveryPoints = result->getUInt("BraveryPoints");
	state.DailyHonorDefendUsed = result->getUInt("DailyHonorDefendUsed");
	state.MilestoneAwardedCount = result->getUInt("MilestoneAwardedCount");
	state.DailyFirstWinClaimed = result->getUInt("DailyFirstWinClaimed");
	state.DailyCommonBoxClaimMask = result->getUInt("DailyCommonBoxClaimMask");
	state.UpdatedAt = result->getUInt("UpdatedAt");
}

static bool RankLockSeasonState(
	sql::Connection* connection,
	UINT uin,
	UINT seasonID,
	UINT dailyDate,
	RankSeasonStateRow& state)
{
	const UINT now = static_cast<UINT>(time(nullptr));
	std::unique_ptr<sql::PreparedStatement> insert(connection->prepareStatement(
		"INSERT INTO RankSeasonState (Uin,SeasonID,DailyDate,UpdatedAt) VALUES (?,?,?,?) "
		"ON DUPLICATE KEY UPDATE Uin=VALUES(Uin);"));
	insert->setUInt(1, uin);
	insert->setUInt(2, seasonID);
	insert->setUInt(3, dailyDate);
	insert->setUInt(4, now);
	insert->executeUpdate();

	std::unique_ptr<sql::PreparedStatement> select(connection->prepareStatement(
		"SELECT Uin,SeasonID,SeasonRaceNums,WinNums,FinishNums,TotalRankIndex,"
		"ContinFinishNums,MaxContinFinishNums,MaxContinWinNums,KillAllNums,LastRankIndex,RMTopRaceScore,"
		"DailyDate,DailyRaceNums,DailyWinNums,ContinWinNums,"
		"DailyTaskClaimMask,BraveryPoints,DailyHonorDefendUsed,MilestoneAwardedCount,"
		"DailyFirstWinClaimed,DailyCommonBoxClaimMask,UpdatedAt "
		"FROM RankSeasonState WHERE Uin=? AND SeasonID=? FOR UPDATE;"));
	select->setUInt(1, uin);
	select->setUInt(2, seasonID);
	std::unique_ptr<sql::ResultSet> result(select->executeQuery());
	if (!result->next())
		return false;
	RankReadSeasonState(result.get(), state);

	if (state.DailyDate != dailyDate)
	{
		std::unique_ptr<sql::PreparedStatement> reset(connection->prepareStatement(
			"UPDATE RankSeasonState SET DailyDate=?,DailyRaceNums=0,DailyWinNums=0,"
			"DailyTaskClaimMask=0,DailyHonorDefendUsed=0,DailyFirstWinClaimed=0,"
			"DailyCommonBoxClaimMask=0,UpdatedAt=? WHERE Uin=? AND SeasonID=?;"));
		reset->setUInt(1, dailyDate);
		reset->setUInt(2, now);
		reset->setUInt(3, uin);
		reset->setUInt(4, seasonID);
		if (reset->executeUpdate() != 1)
			return false;
		state.DailyDate = dailyDate;
		state.DailyRaceNums = 0;
		state.DailyWinNums = 0;
		state.DailyTaskClaimMask = 0;
		state.DailyHonorDefendUsed = 0;
		state.DailyFirstWinClaimed = 0;
		state.DailyCommonBoxClaimMask = 0;
		state.UpdatedAt = now;
	}
	return true;
}

static UINT RankCompletedChallengeTaskCount(UINT braveryPoints, const std::vector<UINT>& incrementalNeeds)
{
	UINT completed = 0;
	UINT cumulativeNeed = 0;
	for (UINT need : incrementalNeeds)
	{
		if (need == 0 || cumulativeNeed > (std::numeric_limits<UINT>::max)() - need)
			break;
		cumulativeNeed += need;
		if (braveryPoints < cumulativeNeed)
			break;
		++completed;
	}
	return completed;
}

bool PlayerDB_LoadRankSeasonState(UINT Uin, UINT SeasonID, UINT DailyDate, RankSeasonStateRow& State)
{
	State = {};
	if (Uin == 0 || SeasonID == 0 || DailyDate == 0)
		return false;
	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		connection->setAutoCommit(false);
		if (!RankLockSeasonState(connection, Uin, SeasonID, DailyDate, State))
		{
			connection->rollback();
			connection->setAutoCommit(true);
			State = {};
			return false;
		}
		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& e)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << e.what() << " PlayerDB_LoadRankSeasonState Error" << std::endl;
		State = {};
		return false;
	}
}

bool PlayerDB_GetRankSeasonPanel(UINT Uin, UINT SeasonID, RankSeasonPanelRow& Row)
{
	Row = {};
	if (Uin == 0 || SeasonID == 0)
		return false;
	Row.State.Uin = Uin;
	Row.State.SeasonID = SeasonID;
	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> exists(connection->prepareStatement(
			"SELECT 1 FROM BaseInfo WHERE Uin=? LIMIT 1;"));
		exists->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> existsResult(exists->executeQuery());
		if (!existsResult->next())
			return false;

		std::unique_ptr<sql::PreparedStatement> stateStatement(connection->prepareStatement(
			"SELECT Uin,SeasonID,SeasonRaceNums,WinNums,FinishNums,TotalRankIndex,"
			"ContinFinishNums,MaxContinFinishNums,MaxContinWinNums,KillAllNums,LastRankIndex,RMTopRaceScore,"
			"DailyDate,DailyRaceNums,DailyWinNums,ContinWinNums,DailyTaskClaimMask,BraveryPoints,"
			"DailyHonorDefendUsed,MilestoneAwardedCount,DailyFirstWinClaimed,DailyCommonBoxClaimMask,UpdatedAt "
			"FROM RankSeasonState WHERE Uin=? AND SeasonID=?;"));
		stateStatement->setUInt(1, Uin);
		stateStatement->setUInt(2, SeasonID);
		std::unique_ptr<sql::ResultSet> stateResult(stateStatement->executeQuery());
		if (stateResult->next())
			RankReadSeasonState(stateResult.get(), Row.State);

		std::unique_ptr<sql::PreparedStatement> mapStatement(connection->prepareStatement(
			"SELECT MapID FROM RankSeasonMapUsage WHERE Uin=? AND SeasonID=? "
			"ORDER BY RaceNums DESC,MapID ASC LIMIT 1;"));
		mapStatement->setUInt(1, Uin);
		mapStatement->setUInt(2, SeasonID);
		std::unique_ptr<sql::ResultSet> mapResult(mapStatement->executeQuery());
		if (mapResult->next())
			Row.MostUsedMapID = mapResult->getUInt("MapID");

		std::unique_ptr<sql::PreparedStatement> kartStatement(connection->prepareStatement(
			"SELECT KartID FROM RankSeasonKartUsage WHERE Uin=? AND SeasonID=? "
			"ORDER BY RaceNums DESC,KartID ASC LIMIT 1;"));
		kartStatement->setUInt(1, Uin);
		kartStatement->setUInt(2, SeasonID);
		std::unique_ptr<sql::ResultSet> kartResult(kartStatement->executeQuery());
		if (kartResult->next())
			Row.MostUsedKartID = kartResult->getUInt("KartID");
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GetRankSeasonPanel Error" << std::endl;
		Row = {};
		return false;
	}
}

bool PlayerDB_HasPermanentItem(UINT Uin, UINT ItemID)
{
	if (Uin == 0 || ItemID == 0)
		return false;
	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(connection->prepareStatement(
			"SELECT 1 FROM Item WHERE Uin=? AND ItemID=? AND ItemNum>0 AND AvailPeriod=-1 LIMIT 1;"));
		statement->setUInt(1, Uin);
		statement->setUInt(2, ItemID);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		return result->next();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_HasPermanentItem Error" << std::endl;
		return false;
	}
}

bool PlayerDB_ApplyRankedRace(
	UINT Uin,
	UINT SeasonID,
	UINT DailyDate,
	bool Win,
	bool Finished,
	UINT RankIndex,
	UINT MapID,
	UINT KartID,
	UINT RMTopRaceScore,
	UINT DailyFirstWinSuperMoney,
	UINT DailyFirstWinCoupons,
	const std::vector<UINT>& DailyWinNeeds,
	const std::vector<UINT>& DailyWinScores,
	const std::vector<UINT>& DailyExtraTaskIDs,
	const std::vector<UINT>& ChallengeIncrementalNeeds,
	UINT AwardHonorDefendNums,
	UINT HonorDefendAddScore,
	UINT MilestoneInterval,
	const std::vector<USHORT>& MilestoneAwardCounts,
	RankRaceApplyResult& Result)
{
	Result = {};
	if (Uin == 0 || SeasonID == 0 || DailyDate == 0 || RankIndex == 0 ||
		DailyWinNeeds.size() != DailyWinScores.size() || DailyWinNeeds.size() != DailyExtraTaskIDs.size())
		return false;
	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		connection->setAutoCommit(false);
		RankSeasonStateRow state;
		if (!RankLockSeasonState(connection, Uin, SeasonID, DailyDate, state))
			throw std::runtime_error("rank season state row unavailable");

		const UINT oldCompletedTasks = RankCompletedChallengeTaskCount(state.BraveryPoints, ChallengeIncrementalNeeds);
		++state.SeasonRaceNums;
		state.TotalRankIndex += RankIndex;
		state.LastRankIndex = RankIndex;
		state.RMTopRaceScore = (std::max)(state.RMTopRaceScore, RMTopRaceScore);
		if (Finished)
		{
			++state.FinishNums;
			++state.ContinFinishNums;
			state.MaxContinFinishNums = (std::max)(state.MaxContinFinishNums, state.ContinFinishNums);
		}
		else
		{
			state.ContinFinishNums = 0;
		}
		++state.DailyRaceNums;
		Result.BraveryBaseScore = 3;
		state.BraveryPoints += Result.BraveryBaseScore;
		if (Win)
		{
			++state.WinNums;
			++state.DailyWinNums;
			++state.ContinWinNums;
			state.MaxContinWinNums = (std::max)(state.MaxContinWinNums, state.ContinWinNums);
			if (state.DailyFirstWinClaimed == 0)
			{
				state.DailyFirstWinClaimed = 1;
				Result.DailyFirstWinSuperMoney = DailyFirstWinSuperMoney;
				Result.DailyFirstWinCoupons = DailyFirstWinCoupons;
			}
			Result.BraveryContinWinExtraScore = (std::min)(state.ContinWinNums, 3u);
			state.BraveryPoints += Result.BraveryContinWinExtraScore;
			for (size_t i = 0; i < DailyWinNeeds.size(); ++i)
			{
				if (DailyWinNeeds[i] != 0 && state.DailyWinNums == DailyWinNeeds[i])
				{
					Result.DailyExtraAddScore += DailyWinScores[i];
					Result.DailyExtraTaskID = DailyExtraTaskIDs[i];
				}
			}
		}
		else
		{
			state.ContinWinNums = 0;
		}

		const UINT completedTasks = RankCompletedChallengeTaskCount(state.BraveryPoints, ChallengeIncrementalNeeds);
		if (completedTasks > oldCompletedTasks)
			Result.NewlyFinishedChallengeTaskIndex = oldCompletedTasks + 1;
		const UINT dailyHonorLimit = completedTasks * AwardHonorDefendNums;
		if (!Win && HonorDefendAddScore != 0 && state.DailyHonorDefendUsed < dailyHonorLimit)
		{
			++state.DailyHonorDefendUsed;
			Result.HonorProtectAddScore = HonorDefendAddScore;
		}

		if (MilestoneInterval != 0 && !MilestoneAwardCounts.empty())
		{
			const UINT reached = (std::min)(
				state.SeasonRaceNums / MilestoneInterval,
				static_cast<UINT>(MilestoneAwardCounts.size()));
			for (UINT i = state.MilestoneAwardedCount; i < reached; ++i)
				Result.MilestoneAwardCoupons += MilestoneAwardCounts[i];
			state.MilestoneAwardedCount = (std::max)(state.MilestoneAwardedCount, reached);
		}

		const UINT now = static_cast<UINT>(time(nullptr));
		std::unique_ptr<sql::PreparedStatement> update(connection->prepareStatement(
			"UPDATE RankSeasonState SET SeasonRaceNums=?,WinNums=?,FinishNums=?,TotalRankIndex=?,"
			"ContinFinishNums=?,MaxContinFinishNums=?,MaxContinWinNums=?,KillAllNums=?,LastRankIndex=?,RMTopRaceScore=?,"
			"DailyDate=?,DailyRaceNums=?,DailyWinNums=?,ContinWinNums=?,DailyTaskClaimMask=?,BraveryPoints=?,DailyHonorDefendUsed=?,"
			"MilestoneAwardedCount=?,DailyFirstWinClaimed=?,DailyCommonBoxClaimMask=?,UpdatedAt=? "
			"WHERE Uin=? AND SeasonID=?;"));
		update->setUInt(1, state.SeasonRaceNums);
		update->setUInt(2, state.WinNums);
		update->setUInt(3, state.FinishNums);
		update->setUInt(4, state.TotalRankIndex);
		update->setUInt(5, state.ContinFinishNums);
		update->setUInt(6, state.MaxContinFinishNums);
		update->setUInt(7, state.MaxContinWinNums);
		update->setUInt(8, state.KillAllNums);
		update->setUInt(9, state.LastRankIndex);
		update->setUInt(10, state.RMTopRaceScore);
		update->setUInt(11, state.DailyDate);
		update->setUInt(12, state.DailyRaceNums);
		update->setUInt(13, state.DailyWinNums);
		update->setUInt(14, state.ContinWinNums);
		update->setUInt(15, state.DailyTaskClaimMask);
		update->setUInt(16, state.BraveryPoints);
		update->setUInt(17, state.DailyHonorDefendUsed);
		update->setUInt(18, state.MilestoneAwardedCount);
		update->setUInt(19, state.DailyFirstWinClaimed);
		update->setUInt(20, state.DailyCommonBoxClaimMask);
		update->setUInt(21, now);
		update->setUInt(22, Uin);
		update->setUInt(23, SeasonID);
		if (update->executeUpdate() != 1)
			throw std::runtime_error("rank season state update affected unexpected rows");

		if (MapID != 0)
		{
			std::unique_ptr<sql::PreparedStatement> mapUsage(connection->prepareStatement(
				"INSERT INTO RankSeasonMapUsage (Uin,SeasonID,MapID,RaceNums,WinNums,UpdatedAt) "
				"VALUES (?,?,?,1,?,?) ON DUPLICATE KEY UPDATE "
				"RaceNums=RaceNums+1,WinNums=WinNums+VALUES(WinNums),UpdatedAt=VALUES(UpdatedAt);"));
			mapUsage->setUInt(1, Uin);
			mapUsage->setUInt(2, SeasonID);
			mapUsage->setUInt(3, MapID);
			mapUsage->setUInt(4, Win ? 1u : 0u);
			mapUsage->setUInt(5, now);
			mapUsage->executeUpdate();
		}
		if (KartID != 0)
		{
			std::unique_ptr<sql::PreparedStatement> kartUsage(connection->prepareStatement(
				"INSERT INTO RankSeasonKartUsage (Uin,SeasonID,KartID,RaceNums,UpdatedAt) "
				"VALUES (?,?,?,1,?) ON DUPLICATE KEY UPDATE "
				"RaceNums=RaceNums+1,UpdatedAt=VALUES(UpdatedAt);"));
			kartUsage->setUInt(1, Uin);
			kartUsage->setUInt(2, SeasonID);
			kartUsage->setUInt(3, KartID);
			kartUsage->setUInt(4, now);
			kartUsage->executeUpdate();
		}

		if (Result.MilestoneAwardCoupons != 0)
		{
			std::unique_ptr<sql::PreparedStatement> award(connection->prepareStatement(
				"UPDATE BaseInfo SET Coupons=Coupons+? WHERE Uin=?;"));
			award->setUInt(1, Result.MilestoneAwardCoupons);
			award->setUInt(2, Uin);
			if (award->executeUpdate() != 1)
				throw std::runtime_error("rank milestone coupon update affected unexpected rows");
		}
		if (Result.DailyFirstWinSuperMoney != 0 || Result.DailyFirstWinCoupons != 0)
		{
			std::unique_ptr<sql::PreparedStatement> award(connection->prepareStatement(
				"UPDATE BaseInfo SET SuperMoney=SuperMoney+?,Coupons=Coupons+? WHERE Uin=?;"));
			award->setUInt(1, Result.DailyFirstWinSuperMoney);
			award->setUInt(2, Result.DailyFirstWinCoupons);
			award->setUInt(3, Uin);
			if (award->executeUpdate() != 1)
				throw std::runtime_error("rank daily first-win currency update affected unexpected rows");
		}

		connection->commit();
		connection->setAutoCommit(true);
		state.UpdatedAt = now;
		Result.State = state;
		return true;
	}
	catch (const std::exception& e)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << e.what() << " PlayerDB_ApplyRankedRace Error" << std::endl;
		Result = {};
		return false;
	}
}

bool PlayerDB_ClaimRankDailyTaskAndGrant(
	UINT Uin,
	UINT SeasonID,
	UINT DailyDate,
	UINT TaskIndex,
	UINT FinishNeed,
	UINT SuperMoney,
	UINT Coupons,
	const std::vector<RankDailyTaskAwardItemRow>& Items,
	RankSeasonStateRow& State,
	std::vector<UINT>& FinalItemNums)
{
	State = {};
	FinalItemNums.clear();
	if (Uin == 0 || SeasonID == 0 || DailyDate == 0 || TaskIndex >= 32 || FinishNeed == 0)
		return false;
	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		connection->setAutoCommit(false);
		if (!RankLockSeasonState(connection, Uin, SeasonID, DailyDate, State))
			throw std::runtime_error("rank daily task state row unavailable");
		const UINT bit = 1u << TaskIndex;
		if (State.DailyWinNums < FinishNeed || (State.DailyTaskClaimMask & bit) != 0)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			return false;
		}

		if (SuperMoney != 0 || Coupons != 0)
		{
			std::unique_ptr<sql::PreparedStatement> currency(connection->prepareStatement(
				"UPDATE BaseInfo SET SuperMoney=SuperMoney+?,Coupons=Coupons+? WHERE Uin=?;"));
			currency->setUInt(1, SuperMoney);
			currency->setUInt(2, Coupons);
			currency->setUInt(3, Uin);
			if (currency->executeUpdate() != 1)
				throw std::runtime_error("rank daily task currency update affected unexpected rows");
		}

		FinalItemNums.reserve(Items.size());
		for (const RankDailyTaskAwardItemRow& item : Items)
		{
			if (item.ItemID == 0 || item.ItemNum == 0)
				throw std::runtime_error("rank daily task award contains invalid item");
			bool success = false;
			const int finalNum = PlayerDB_AddItem5(
				Uin, item.ItemID, item.ItemNum, item.AvailPeriod, false, connection, &success);
			if (!success)
				throw std::runtime_error("rank daily task item grant failed");
			FinalItemNums.push_back(finalNum > 0 ? static_cast<UINT>(finalNum) : item.ItemNum);
		}

		State.DailyTaskClaimMask |= bit;
		State.UpdatedAt = static_cast<UINT>(time(nullptr));
		std::unique_ptr<sql::PreparedStatement> update(connection->prepareStatement(
			"UPDATE RankSeasonState SET DailyTaskClaimMask=?,UpdatedAt=? WHERE Uin=? AND SeasonID=? AND DailyDate=?;"));
		update->setUInt(1, State.DailyTaskClaimMask);
		update->setUInt(2, State.UpdatedAt);
		update->setUInt(3, Uin);
		update->setUInt(4, SeasonID);
		update->setUInt(5, DailyDate);
		if (update->executeUpdate() != 1)
			throw std::runtime_error("rank daily task claim update affected unexpected rows");
		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& e)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << e.what() << " PlayerDB_ClaimRankDailyTaskAndGrant Error" << std::endl;
		State = {};
		FinalItemNums.clear();
		return false;
	}
}

bool PlayerDB_ClaimRankDailyCommonBoxAndGrant(
	UINT Uin,
	UINT SeasonID,
	UINT DailyDate,
	UINT AwardIndex,
	UINT FinishNeed,
	UINT SuperMoney,
	UINT Coupons,
	const std::vector<RankDailyTaskAwardItemRow>& Items,
	RankSeasonStateRow& State,
	std::vector<UINT>& FinalItemNums)
{
	State = {};
	FinalItemNums.clear();
	if (Uin == 0 || SeasonID == 0 || DailyDate == 0 || AwardIndex >= 32 || FinishNeed == 0)
		return false;
	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		connection->setAutoCommit(false);
		if (!RankLockSeasonState(connection, Uin, SeasonID, DailyDate, State))
			throw std::runtime_error("rank daily common-box state row unavailable");
		if (State.DailyWinNums < FinishNeed || State.DailyCommonBoxClaimMask != 0)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			return false;
		}

		if (SuperMoney != 0 || Coupons != 0)
		{
			std::unique_ptr<sql::PreparedStatement> currency(connection->prepareStatement(
				"UPDATE BaseInfo SET SuperMoney=SuperMoney+?,Coupons=Coupons+? WHERE Uin=?;"));
			currency->setUInt(1, SuperMoney);
			currency->setUInt(2, Coupons);
			currency->setUInt(3, Uin);
			if (currency->executeUpdate() != 1)
				throw std::runtime_error("rank daily common-box currency update affected unexpected rows");
		}

		FinalItemNums.reserve(Items.size());
		for (const RankDailyTaskAwardItemRow& item : Items)
		{
			if (item.ItemID == 0 || item.ItemNum == 0)
				throw std::runtime_error("rank daily common-box award contains invalid item");
			bool success = false;
			const int finalNum = PlayerDB_AddItem5(
				Uin, item.ItemID, item.ItemNum, item.AvailPeriod, false, connection, &success);
			if (!success)
				throw std::runtime_error("rank daily common-box item grant failed");
			FinalItemNums.push_back(finalNum > 0 ? static_cast<UINT>(finalNum) : item.ItemNum);
		}

		State.DailyCommonBoxClaimMask = 1u << AwardIndex;
		State.UpdatedAt = static_cast<UINT>(time(nullptr));
		std::unique_ptr<sql::PreparedStatement> update(connection->prepareStatement(
			"UPDATE RankSeasonState SET DailyCommonBoxClaimMask=?,UpdatedAt=? "
			"WHERE Uin=? AND SeasonID=? AND DailyDate=?;"));
		update->setUInt(1, State.DailyCommonBoxClaimMask);
		update->setUInt(2, State.UpdatedAt);
		update->setUInt(3, Uin);
		update->setUInt(4, SeasonID);
		update->setUInt(5, DailyDate);
		if (update->executeUpdate() != 1)
			throw std::runtime_error("rank daily common-box claim update affected unexpected rows");

		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& e)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << e.what() << " PlayerDB_ClaimRankDailyCommonBoxAndGrant Error" << std::endl;
		State = {};
		FinalItemNums.clear();
		return false;
	}
}

bool PlayerDB_AddBaseInfoRewardCurrency(UINT Uin, UINT Exp, UINT Money, UINT SuperMoney, UINT Coupons, UINT SpeedCoin)
{
	if (Uin == 0)
		return false;
	if (Exp == 0 && Money == 0 && SuperMoney == 0 && Coupons == 0 && SpeedCoin == 0)
		return true;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"UPDATE BaseInfo SET Experience=Experience+?,Money=Money+?,SuperMoney=SuperMoney+?,Coupons=Coupons+?,SpeedCoin=SpeedCoin+? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Exp);
		prep_stmt->setUInt(2, Money);
		prep_stmt->setUInt(3, SuperMoney);
		prep_stmt->setUInt(4, Coupons);
		prep_stmt->setUInt(5, SpeedCoin);
		prep_stmt->setUInt(6, Uin);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_AddBaseInfoRewardCurrency Error" << std::endl;
	}
	return false;
}

bool PlayerDB_ApplyBottomGameSuperMoney(std::vector<BottomGameMoneyChange>& Changes)
{
	if (Changes.empty())
		return false;

	std::sort(Changes.begin(), Changes.end(), [](const BottomGameMoneyChange& lhs, const BottomGameMoneyChange& rhs) {
		return lhs.Uin < rhs.Uin;
		});
	for (size_t i = 0; i < Changes.size(); ++i)
	{
		Changes[i].NewSuperMoney = 0;
		if (Changes[i].Uin == 0 || (i != 0 && Changes[i - 1].Uin == Changes[i].Uin))
			return false;
	}

	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return false;

	try
	{
		connection->setAutoCommit(false);
		for (BottomGameMoneyChange& change : Changes)
		{
			std::unique_ptr<sql::PreparedStatement> select(connection->prepareStatement(
				"SELECT SuperMoney FROM BaseInfo WHERE Uin=? FOR UPDATE;"));
			select->setUInt(1, change.Uin);
			std::unique_ptr<sql::ResultSet> result(select->executeQuery());
			if (!result || !result->next())
				throw std::runtime_error("bottom game player balance not found");

			const std::int64_t current = result->getInt64("SuperMoney");
			const std::int64_t next = current + change.Delta;
			if (current < 0 || next < 0 || next > (std::numeric_limits<int>::max)())
				throw std::runtime_error("bottom game player balance out of range");
			change.NewSuperMoney = static_cast<UINT>(next);
		}

		for (const BottomGameMoneyChange& change : Changes)
		{
			std::unique_ptr<sql::PreparedStatement> update(connection->prepareStatement(
				"UPDATE BaseInfo SET SuperMoney=? WHERE Uin=?;"));
			update->setInt64(1, static_cast<std::int64_t>(change.NewSuperMoney));
			update->setUInt(2, change.Uin);
			if (update->executeUpdate() != 1)
				throw std::runtime_error("bottom game balance update affected unexpected rows");
		}

		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& e)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		for (BottomGameMoneyChange& change : Changes)
			change.NewSuperMoney = 0;
		std::cerr << e.what() << " PlayerDB_ApplyBottomGameSuperMoney Error" << std::endl;
		return false;
	}
}

void UserDB_ban(UINT Uin)
{
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE User SET isban=1 WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, Uin - 10000);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UserDB_ban Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_AddMagicCupTempBox(std::shared_ptr<ClientNode> Client, DWORD Uin, DWORD ItemID, DWORD ItemNum, int AvailPeriod, int ItemType)
{
	int result;
	int count = 0;
	BOOL isExist = FALSE;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemNum FROM MagicCupTempBox  WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ItemID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			res->next();
			isExist = TRUE;
			count = res->getInt("ItemNum");
		}
		res.reset();

		if (isExist) {
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE MagicCupTempBox SET ItemNum=?  WHERE Uin=? AND ItemID=?;"));
			prep_stmt->clearParameters();
			prep_stmt->setInt(1, ItemNum + count);
			prep_stmt->setUInt(2, Uin);
			prep_stmt->setInt(3, ItemID);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}
		else
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO MagicCupTempBox (Uin,ItemID,ItemNum,AvailPeriod,ItemType) VALUES (?,?,?,?,?);"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ItemID);
			prep_stmt->setInt(3, ItemNum);
			prep_stmt->setInt(4, AvailPeriod);
			prep_stmt->setInt(5, ItemType);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_AddMagicCupTempBox Error" << std::endl;
		//throw;??
	}
}

BOOL PlayerDB_SelectClientInfo(UINT Uin, ClientInfo*& clientInfo)
{
	int ret = false;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT *  FROM BaseInfo  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			res->next();
			ret = true;
			clientInfo[0].NickName = UTF2GB(res->getString("NickName").c_str());
			clientInfo[0].Gender = res->getUInt("Gender");
			clientInfo[0].Country = res->getUInt("Country");
			clientInfo[0].License = res->getUInt("License");
			clientInfo[0].Experience = res->getUInt("Experience");
			clientInfo[0].SuperMoney = res->getUInt("SuperMoney");
			clientInfo[0].Money = res->getUInt("Money");
			clientInfo[0].WinNum = res->getUInt("WinNum");
			clientInfo[0].SecondNum = res->getUInt("SecondNum");
			clientInfo[0].ThirdNum = res->getUInt("ThirdNum");
			clientInfo[0].TotalNum = res->getUInt("TotalNum");
			clientInfo[0].CurHonor = res->getUInt("CurHonor");
			clientInfo[0].TotalHonor = res->getUInt("TotalHonor");
			clientInfo[0].TodayHonor = res->getUInt("TodayHonor");
			clientInfo[0].RelaxTime = res->getUInt("RelaxTime");
			clientInfo[0].MonthDurationBefor = res->getUInt("MonthDurationBefor");
			clientInfo[0].MonthDurationCur = res->getUInt("MonthDurationCur");
			clientInfo[0].Charm = res->getUInt("Charm");
			clientInfo[0].DurationGame = res->getUInt("DurationGame");
			clientInfo[0].DanceExp = res->getUInt("DanceExp");
			clientInfo[0].Coupons = res->getUInt("Coupons");
			clientInfo[0].Admiration = res->getUInt("Admiration");
			clientInfo[0].LuckMoney = res->getUInt("LuckMoney");
			clientInfo[0].TeamWorkExp = res->getUInt("TeamWorkExp");
			clientInfo[0].AchievePoint = res->getUInt("AchievePoint");
			clientInfo[0].RegTime = res->getUInt("RegTime");
			clientInfo[0].SpeedCoin =
				PlayerDB_IsSpeedCoinReady() ? res->getUInt("SpeedCoin") : 0;
			clientInfo[0].Signature = UTF2GB(res->getString("Signature").c_str());
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectClientInfo Error" << std::endl;
		//throw;??
	}
}

BOOL PlayerDB_SelectClientInfo(std::shared_ptr<ClientNode> Client)
{
	int ret = false;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT *  FROM BaseInfo  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			res->next();
			ret = true;
			Client->fps = res->getUInt("fps");
			Client->SwitchFlag1 = res->getUInt("SwitchFlag1");
			Client->SpeedCoin =
				PlayerDB_IsSpeedCoinReady() ? res->getUInt("SpeedCoin") : 0;
			PlayerDB_LoadClientSavedFlags(Client->Uin, Client->ClientSavedFlags, QQS_CLIENT_SAVED_FLAG_BYTES);
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectClientInfo Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_LoadClientSavedFlags(UINT Uin, BYTE* Flags, size_t FlagLen)
{
	if (Flags && FlagLen > 0)
		memset(Flags, 0, FlagLen);
	if (!Flags || FlagLen == 0 || Uin == 0)
		return;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ClientSavedFlagHex FROM BaseInfo WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		if (res->rowsCount() > 0)
		{
			res->next();
			std::string flagHex = res->getString("ClientSavedFlagHex");
			QQS_ClientSavedFlagsFromHex(flagHex, Flags, FlagLen);
		}
		res.reset();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_LoadClientSavedFlags Error" << std::endl;
	}
}

bool PlayerDB_UpdateClientSavedFlagHex(UINT Uin, const BYTE* Flags, size_t FlagLen)
{
	if (!Flags || FlagLen == 0 || Uin == 0)
		return false;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::string flagHex = QQS_ClientSavedFlagsToHex(Flags, FlagLen);
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE BaseInfo SET ClientSavedFlagHex=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, flagHex);
		prep_stmt->setUInt(2, Uin);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateClientSavedFlagHex Error" << std::endl;
	}
	return false;
}

void PlayerDB_LoadExRightFlags(UINT Uin, BYTE* Flags, size_t FlagLen)
{
	if (!Flags || FlagLen == 0 || Uin == 0)
		return;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ExRightFlagHex FROM BaseInfo WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		if (res->rowsCount() > 0)
		{
			res->next();
			const std::string flagHex = res->getString("ExRightFlagHex");
			bool valid = flagHex.size() == FlagLen * 2;
			for (size_t i = 0; valid && i < flagHex.size(); ++i)
				valid = QQS_HexCharValue(flagHex[i]) >= 0;
			if (valid)
			{
				std::vector<BYTE> loaded(FlagLen);
				QQS_ClientSavedFlagsFromHex(flagHex, loaded.data(), loaded.size());
				memcpy(Flags, loaded.data(), FlagLen);
			}
		}
		res.reset();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_LoadExRightFlags Error" << std::endl;
	}
}

bool PlayerDB_UpdateExRightFlagHex(UINT Uin, const BYTE* Flags, size_t FlagLen)
{
	if (!Flags || FlagLen == 0 || Uin == 0)
		return false;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		const std::string flagHex = QQS_ClientSavedFlagsToHex(Flags, FlagLen);
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE BaseInfo SET ExRightFlagHex=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, flagHex);
		prep_stmt->setUInt(2, Uin);
		const int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateExRightFlagHex Error" << std::endl;
	}
	return false;
}



int PlayerDB_GetItemNum(DWORD Uin, DWORD ItemID)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	int ItemNum = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemNum FROM Item WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ItemID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			res->next();
			ItemNum = res->getInt("ItemNum");
			/*if (ItemNum == 0)
				PlayerDB_DeleteItem(Uin, ItemID);*/
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetItemNum Error" << std::endl;
		//throw;??
	}
	return ItemNum;
}

bool PlayerDB_TryConsumeItem(
	DWORD Uin,
	DWORD ItemID,
	INT ItemNum,
	INT& Remaining)
{
	Remaining = PlayerDB_GetItemNum(Uin, ItemID);
	if (Uin == 0 || ItemID == 0 || ItemNum <= 0)
		return false;
	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		connection->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> update(
			connection->prepareStatement(
				"UPDATE Item SET ItemNum=ItemNum-? "
				"WHERE Uin=? AND ItemID=? AND ItemNum>=?;"));
		update->setInt(1, ItemNum);
		update->setUInt(2, Uin);
		update->setUInt(3, ItemID);
		update->setInt(4, ItemNum);
		if (update->executeUpdate() != 1)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			Remaining = PlayerDB_GetItemNum(Uin, ItemID);
			return false;
		}
		std::unique_ptr<sql::PreparedStatement> select(
			connection->prepareStatement(
				"SELECT ItemNum FROM Item WHERE Uin=? AND ItemID=? FOR UPDATE;"));
		select->setUInt(1, Uin);
		select->setUInt(2, ItemID);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		Remaining = result->next() ? result->getInt("ItemNum") : 0;
		if (Remaining <= 0)
		{
			std::unique_ptr<sql::PreparedStatement> remove(
				connection->prepareStatement(
					"DELETE FROM Item WHERE Uin=? AND ItemID=? AND ItemNum<=0;"));
			remove->setUInt(1, Uin);
			remove->setUInt(2, ItemID);
			remove->executeUpdate();
			Remaining = 0;
		}
		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << exception.what()
			<< " PlayerDB_TryConsumeItem Error" << std::endl;
		Remaining = PlayerDB_GetItemNum(Uin, ItemID);
		return false;
	}
}

RoomUGCPurchaseDbResult PlayerDB_RoomUGCTryPurchase(
	DWORD Uin,
	std::uint64_t WorkID,
	DWORD CurrencyItemID,
	UINT Price,
	UINT& Remaining)
{
	Remaining = 0u;
	if (Uin == 0u || WorkID == 0u || CurrencyItemID == 0u || Price == 0u ||
		Price > static_cast<UINT>((std::numeric_limits<INT>::max)()) ||
		!g_PlayerDBRoomUGCReady.load(std::memory_order_acquire))
	{
		return RoomUGCPurchaseDbResult::DatabaseError;
	}

	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return RoomUGCPurchaseDbResult::DatabaseError;
	try
	{
		connection->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> insert(
			connection->prepareStatement(
				"INSERT IGNORE INTO RoomUGCOwnedWork("
				"Uin,WorkID,CurrencyItemID,PaidPrice,PurchaseTime) "
				"VALUES(?,?,?,?,UNIX_TIMESTAMP());"));
		insert->setUInt(1, Uin);
		insert->setUInt64(2, WorkID);
		insert->setUInt(3, CurrencyItemID);
		insert->setUInt(4, Price);
		if (insert->executeUpdate() != 1)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			return RoomUGCPurchaseDbResult::AlreadyOwned;
		}

		std::unique_ptr<sql::PreparedStatement> update(
			connection->prepareStatement(
				"UPDATE Item SET ItemNum=ItemNum-? "
				"WHERE Uin=? AND ItemID=? AND ItemNum>=?;"));
		update->setUInt(1, Price);
		update->setUInt(2, Uin);
		update->setUInt(3, CurrencyItemID);
		update->setUInt(4, Price);
		if (update->executeUpdate() != 1)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			return RoomUGCPurchaseDbResult::InsufficientFunds;
		}

		std::unique_ptr<sql::PreparedStatement> select(
			connection->prepareStatement(
				"SELECT ItemNum FROM Item "
				"WHERE Uin=? AND ItemID=? FOR UPDATE;"));
		select->setUInt(1, Uin);
		select->setUInt(2, CurrencyItemID);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		const INT itemBalance = result->next() ? result->getInt("ItemNum") : 0;
		Remaining = static_cast<UINT>((std::max)(0, itemBalance));
		if (itemBalance <= 0)
		{
			std::unique_ptr<sql::PreparedStatement> remove(
				(connection->prepareStatement(
					"DELETE FROM Item "
					"WHERE Uin=? AND ItemID=? AND ItemNum<=0;")));
			remove->setUInt(1, Uin);
			remove->setUInt(2, CurrencyItemID);
			remove->executeUpdate();
			Remaining = 0u;
		}
		connection->commit();
		connection->setAutoCommit(true);
		return RoomUGCPurchaseDbResult::Success;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		printf("[DATABASE][RoomUGC] purchase failed Uin=%u WorkID=%llu: %s\n",
			Uin, static_cast<unsigned long long>(WorkID), exception.what());
		return RoomUGCPurchaseDbResult::DatabaseError;
	}
}

bool PlayerDB_RoomUGCGetOwnership(
	DWORD Uin,
	std::uint64_t WorkID,
	bool& Owned)
{
	Owned = false;
	if (Uin == 0u || WorkID == 0u ||
		!g_PlayerDBRoomUGCReady.load(std::memory_order_acquire))
	{
		return false;
	}
	try
	{
		DBConnection database(player);
		auto* connection = database.getConnection();
		if (!connection)
			return false;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"SELECT 1 FROM RoomUGCOwnedWork "
				"WHERE Uin=? AND WorkID=? LIMIT 1;"));
		statement->setUInt(1, Uin);
		statement->setUInt64(2, WorkID);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		Owned = result->next();
		return true;
	}
	catch (const std::exception& exception)
	{
		printf("[DATABASE][RoomUGC] ownership lookup failed "
			"Uin=%u WorkID=%llu: %s\n",
			Uin, static_cast<unsigned long long>(WorkID), exception.what());
		return false;
	}
}

bool PlayerDB_RoomUGCGetOwnedState(
	DWORD Uin,
	std::vector<RoomUGCOwnedWorkRow>& OwnedWorks,
	std::uint64_t& UsingWorkID)
{
	OwnedWorks.clear();
	UsingWorkID = 0;
	if (Uin == 0u ||
		!g_PlayerDBRoomUGCReady.load(std::memory_order_acquire))
	{
		return false;
	}
	try
	{
		DBConnection database(player);
		auto* connection = database.getConnection();
		if (!connection)
			return false;

		std::unique_ptr<sql::PreparedStatement> ownershipQuery(
			connection->prepareStatement(
				"SELECT WorkID,PurchaseTime FROM RoomUGCOwnedWork "
				"WHERE Uin=? ORDER BY PurchaseTime DESC,WorkID DESC;"));
		ownershipQuery->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> ownershipResult(
			ownershipQuery->executeQuery());
		while (ownershipResult->next())
		{
			RoomUGCOwnedWorkRow row;
			row.WorkID = ownershipResult->getUInt64("WorkID");
			row.PurchaseTime = ownershipResult->getUInt("PurchaseTime");
			OwnedWorks.push_back(row);
		}

		std::unique_ptr<sql::PreparedStatement> stateQuery(
			connection->prepareStatement(
				"SELECT UsingWorkID FROM RoomUGCPlayerState "
				"WHERE Uin=? LIMIT 1;"));
		stateQuery->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> stateResult(stateQuery->executeQuery());
		if (stateResult->next())
			UsingWorkID = stateResult->getUInt64("UsingWorkID");
		return true;
	}
	catch (const std::exception& exception)
	{
		printf("[DATABASE][RoomUGC] owned-state lookup failed Uin=%u: %s\n",
			Uin, exception.what());
		OwnedWorks.clear();
		UsingWorkID = 0;
		return false;
	}
}

bool PlayerDB_RoomUGCGetUsingWork(DWORD Uin, std::uint64_t& UsingWorkID)
{
	UsingWorkID = 0;
	if (Uin == 0u ||
		!g_PlayerDBRoomUGCReady.load(std::memory_order_acquire))
	{
		return false;
	}
	try
	{
		DBConnection database(player);
		auto* connection = database.getConnection();
		if (!connection)
			return false;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"SELECT UsingWorkID FROM RoomUGCPlayerState "
				"WHERE Uin=? LIMIT 1;"));
		statement->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		if (result->next())
			UsingWorkID = result->getUInt64("UsingWorkID");
		return true;
	}
	catch (const std::exception& exception)
	{
		printf("[DATABASE][RoomUGC] equipped-work lookup failed Uin=%u: %s\n",
			Uin, exception.what());
		return false;
	}
}

RoomUGCUseStateDbResult PlayerDB_RoomUGCSetUsingWork(
	DWORD Uin,
	std::uint64_t WorkID)
{
	if (Uin == 0u ||
		!g_PlayerDBRoomUGCReady.load(std::memory_order_acquire))
	{
		return RoomUGCUseStateDbResult::DatabaseError;
	}

	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return RoomUGCUseStateDbResult::DatabaseError;
	try
	{
		connection->setAutoCommit(false);
		if (WorkID != 0u)
		{
			std::unique_ptr<sql::PreparedStatement> ownershipQuery(
				connection->prepareStatement(
					"SELECT 1 FROM RoomUGCOwnedWork "
					"WHERE Uin=? AND WorkID=? FOR UPDATE;"));
			ownershipQuery->setUInt(1, Uin);
			ownershipQuery->setUInt64(2, WorkID);
			std::unique_ptr<sql::ResultSet> ownershipResult(
				ownershipQuery->executeQuery());
			if (!ownershipResult->next())
			{
				connection->rollback();
				connection->setAutoCommit(true);
				return RoomUGCUseStateDbResult::NotOwned;
			}
		}

		std::unique_ptr<sql::PreparedStatement> update(
			connection->prepareStatement(
				"INSERT INTO RoomUGCPlayerState(Uin,UsingWorkID) VALUES(?,?) "
				"ON DUPLICATE KEY UPDATE UsingWorkID=VALUES(UsingWorkID);"));
		update->setUInt(1, Uin);
		update->setUInt64(2, WorkID);
		update->executeUpdate();
		connection->commit();
		connection->setAutoCommit(true);
		return RoomUGCUseStateDbResult::Success;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		printf("[DATABASE][RoomUGC] use-state update failed "
			"Uin=%u WorkID=%llu: %s\n",
			Uin, static_cast<unsigned long long>(WorkID), exception.what());
		return RoomUGCUseStateDbResult::DatabaseError;
	}
}

RoomUGCDeleteDbResult PlayerDB_RoomUGCDeleteOwnedWork(
	DWORD Uin,
	std::uint64_t WorkID)
{
	if (Uin == 0u || WorkID == 0u ||
		!g_PlayerDBRoomUGCReady.load(std::memory_order_acquire))
	{
		return RoomUGCDeleteDbResult::DatabaseError;
	}

	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return RoomUGCDeleteDbResult::DatabaseError;
	try
	{
		connection->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> remove(
			connection->prepareStatement(
				"DELETE FROM RoomUGCOwnedWork WHERE Uin=? AND WorkID=?;"));
		remove->setUInt(1, Uin);
		remove->setUInt64(2, WorkID);
		if (remove->executeUpdate() != 1)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			return RoomUGCDeleteDbResult::NotOwned;
		}

		std::unique_ptr<sql::PreparedStatement> clearUsing(
			connection->prepareStatement(
				"UPDATE RoomUGCPlayerState SET UsingWorkID=0 "
				"WHERE Uin=? AND UsingWorkID=?;"));
		clearUsing->setUInt(1, Uin);
		clearUsing->setUInt64(2, WorkID);
		clearUsing->executeUpdate();

		connection->commit();
		connection->setAutoCommit(true);
		return RoomUGCDeleteDbResult::Success;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		printf("[DATABASE][RoomUGC] delete-owned-work failed "
			"Uin=%u WorkID=%llu: %s\n",
			Uin, static_cast<unsigned long long>(WorkID), exception.what());
		return RoomUGCDeleteDbResult::DatabaseError;
	}
}

UINT PlayerDB_GetSpeedCoin(DWORD Uin)
{
	if (Uin == 0 || !PlayerDB_IsSpeedCoinReady())
		return 0;
	try
	{
		DBConnection database(player);
		auto* connection = database.getConnection();
		if (!connection)
			return 0;
		std::unique_ptr<sql::PreparedStatement> statement(
			connection->prepareStatement(
				"SELECT SpeedCoin FROM BaseInfo WHERE Uin=? LIMIT 1;"));
		statement->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		return result->next() ? result->getUInt("SpeedCoin") : 0;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " PlayerDB_GetSpeedCoin Error" << std::endl;
		return 0;
	}
}

bool PlayerDB_TrySpendSpeedCoin(DWORD Uin, UINT Cost, UINT& Remaining)
{
	Remaining = 0;
	if (Uin == 0 || !PlayerDB_IsSpeedCoinReady())
		return false;
	Remaining = PlayerDB_GetSpeedCoin(Uin);
	if (Cost == 0)
		return true;
	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return false;
	try
	{
		connection->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> update(
			connection->prepareStatement(
				"UPDATE BaseInfo SET SpeedCoin=SpeedCoin-? "
				"WHERE Uin=? AND SpeedCoin>=?;"));
		update->setUInt(1, Cost);
		update->setUInt(2, Uin);
		update->setUInt(3, Cost);
		if (update->executeUpdate() != 1)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			Remaining = PlayerDB_GetSpeedCoin(Uin);
			return false;
		}
		std::unique_ptr<sql::PreparedStatement> select(
			connection->prepareStatement(
				"SELECT SpeedCoin FROM BaseInfo WHERE Uin=? FOR UPDATE;"));
		select->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		Remaining = result->next() ? result->getUInt("SpeedCoin") : 0;
		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << exception.what()
			<< " PlayerDB_TrySpendSpeedCoin Error" << std::endl;
		Remaining = PlayerDB_GetSpeedCoin(Uin);
		return false;
	}
}

SuperMoneySpendDbResult PlayerDB_TrySpendSuperMoney(DWORD Uin, UINT Cost, UINT& Remaining)
{
	Remaining = 0;
	if (Uin == 0 || Cost == 0)
		return SuperMoneySpendDbResult::DatabaseError;

	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return SuperMoneySpendDbResult::DatabaseError;

	try
	{
		connection->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> select(
			connection->prepareStatement(
				"SELECT SuperMoney FROM BaseInfo WHERE Uin=? FOR UPDATE;"));
		select->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> result(select->executeQuery());
		if (!result->next())
		{
			connection->rollback();
			connection->setAutoCommit(true);
			return SuperMoneySpendDbResult::DatabaseError;
		}

		const UINT current = result->getUInt("SuperMoney");
		Remaining = current;
		if (current < Cost)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			return SuperMoneySpendDbResult::InsufficientFunds;
		}

		Remaining = current - Cost;
		std::unique_ptr<sql::PreparedStatement> update(
			connection->prepareStatement(
				"UPDATE BaseInfo SET SuperMoney=? WHERE Uin=? AND SuperMoney=?;"));
		update->setUInt(1, Remaining);
		update->setUInt(2, Uin);
		update->setUInt(3, current);
		if (update->executeUpdate() != 1)
		{
			connection->rollback();
			connection->setAutoCommit(true);
			Remaining = current;
			return SuperMoneySpendDbResult::DatabaseError;
		}

		connection->commit();
		connection->setAutoCommit(true);
		return SuperMoneySpendDbResult::Success;
	}
	catch (const std::exception& exception)
	{
		try { connection->rollback(); connection->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << exception.what()
			<< " PlayerDB_TrySpendSuperMoney Error" << std::endl;
		Remaining = 0;
		return SuperMoneySpendDbResult::DatabaseError;
	}
}

bool PlayerDB_RefundSpeedCoin(DWORD Uin, UINT Amount, UINT& Current)
{
	Current = 0;
	if (Uin == 0 || !PlayerDB_IsSpeedCoinReady())
		return false;
	Current = PlayerDB_GetSpeedCoin(Uin);
	if (Amount == 0)
		return true;
	try
	{
		DBConnection database(player);
		auto* connection = database.getConnection();
		if (!connection)
			return false;
		std::unique_ptr<sql::PreparedStatement> update(
			connection->prepareStatement(
				"UPDATE BaseInfo SET SpeedCoin=SpeedCoin+? WHERE Uin=?;"));
		update->setUInt(1, Amount);
		update->setUInt(2, Uin);
		if (update->executeUpdate() != 1)
			return false;
		Current = PlayerDB_GetSpeedCoin(Uin);
		return true;
	}
	catch (const std::exception& exception)
	{
		std::cerr << exception.what()
			<< " PlayerDB_RefundSpeedCoin Error" << std::endl;
		Current = PlayerDB_GetSpeedCoin(Uin);
		return false;
	}
}

BOOL PlayerDB_HasItem(DWORD Uin, DWORD ItemID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return FALSE;
	try
	{
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM Item WHERE Uin=? AND ItemID=? LIMIT 1;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ItemID);
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			if (res->next()) return TRUE;
		}
		if (GetItemType(ItemID) == EAIT_CAR)
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM KartRefit WHERE Uin=? AND KartID=? LIMIT 1;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ItemID);
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			if (res->next()) return TRUE;

			std::unique_ptr<sql::PreparedStatement> prep_stmt2(conn->prepareStatement("SELECT 1 FROM kartstonenew WHERE Uin=? AND KartID=? LIMIT 1;"));
			prep_stmt2->clearParameters();
			prep_stmt2->setUInt(1, Uin);
			prep_stmt2->setInt(2, ItemID);
			std::unique_ptr<sql::ResultSet> res2(prep_stmt2->executeQuery());
			if (res2->next()) return TRUE;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_HasItem Error" << std::endl;
	}
	return FALSE;
}



BOOL PlayerDB_IsItemExpired(DWORD Uin, DWORD ItemID)
{
	if (Uin < 10000 || ItemID == 0) return FALSE;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return FALSE;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM Item WHERE Uin=? AND ItemID=? AND AvailPeriod > 0 AND (AvailPeriod * 60 * 60 + ObtainTime) <= UNIX_TIMESTAMP() LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ItemID);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next()) return TRUE;
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_IsItemExpired Error" << std::endl;
	}
	return FALSE;
}

void PlayerDB_ClearExpiredSkinBindings(DWORD Uin)
{
	if (Uin < 10000) return;

	std::vector<UINT> expiredSkinIds;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemID FROM Item WHERE Uin=? AND AvailPeriod > 0 AND (AvailPeriod * 60 * 60 + ObtainTime) <= UNIX_TIMESTAMP();"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			prep_stmt.reset();
			while (res->next())
			{
				UINT itemId = res->getUInt("ItemID");
				if (itemId > 0 && GetItemType(itemId) == EAIT_SKIN)
				{
					expiredSkinIds.push_back(itemId);
				}
			}
			res.reset();
		}

		for (UINT skinId : expiredSkinIds)
		{
			{
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET Status=0 WHERE Uin=? AND ItemID=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setUInt(2, skinId);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}

			{
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE KartRefit SET ShapeSuitID=0,SkinSuitID=0,ShapeLegendSuitID=0,LegendSuitLevel=0,LegendSuitLevelChoice=0 WHERE Uin=? AND (ShapeSuitID=? OR SkinSuitID=? OR ShapeLegendSuitID=?);"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setUInt(2, skinId);
				prep_stmt->setUInt(3, skinId);
				prep_stmt->setUInt(4, skinId);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_ClearExpiredSkinBindings Error" << std::endl;
	}
}

BOOL PlayerDB_DeleteItem(DWORD Uin, int ItemID)
{
	if (ItemID == 10020)
	{
		return FALSE;
	}

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return FALSE;
	conn->setAutoCommit(false);
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM Item  WHERE  Uin=? AND ItemID = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();

		if (GetItemType(ItemID) == EAIT_CAR) {
			//??????
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM KartRefit  WHERE  Uin=? AND KartID = ?;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ItemID);
			prep_stmt->executeUpdate();
			prep_stmt.reset();

			{
				//??????
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM kartstonenew  WHERE  Uin=? AND KartID = ?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->setInt(2, ItemID);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}

		}
		conn->commit();
		conn->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_DeleteItem Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_Reduce(UINT Uin, UINT ItemID, int Num)
{
	UINT ItemNum = PlayerDB_GetItemNum(Uin, ItemID);
	if (ItemNum == Num) {
		PlayerDB_DeleteItem(Uin, ItemID);
		return;
	}

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET ItemNum=ItemNum-? WHERE Uin = ? AND ItemID = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, Num);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->setInt(3, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_Reduce Error" << std::endl;
		//throw;??
	}
}

bool PlayerDB_TryReduceItem(UINT Uin, UINT ItemID, int Num, int& Remaining)
{
	Remaining = 0;
	if (Uin < 10000 || ItemID == 0 || Num <= 0) return false;

	DBConnection myDBConnection(player);
	auto conn = myDBConnection.getConnection();
	if (!conn) return false;
	bool committed = false;

	try
	{
		conn->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> update(conn->prepareStatement(
			"UPDATE Item SET ItemNum=ItemNum-? WHERE Uin=? AND ItemID=? AND ItemNum>=?;"));
		update->clearParameters();
		update->setInt(1, Num);
		update->setUInt(2, Uin);
		update->setUInt(3, ItemID);
		update->setInt(4, Num);
		const int affected = update->executeUpdate();
		update.reset();
		if (affected != 1)
		{
			conn->rollback();
			conn->setAutoCommit(true);
			return false;
		}

		std::unique_ptr<sql::PreparedStatement> query(conn->prepareStatement(
			"SELECT ItemNum FROM Item WHERE Uin=? AND ItemID=? LIMIT 1 FOR UPDATE;"));
		query->clearParameters();
		query->setUInt(1, Uin);
		query->setUInt(2, ItemID);
		std::unique_ptr<sql::ResultSet> result(query->executeQuery());
		query.reset();
		if (!result->next())
		{
			result.reset();
			conn->rollback();
			conn->setAutoCommit(true);
			return false;
		}
		Remaining = result->getInt("ItemNum");
		result.reset();
		if (Remaining < 0)
		{
			conn->rollback();
			conn->setAutoCommit(true);
			Remaining = 0;
			return false;
		}
		if (Remaining == 0)
		{
			std::unique_ptr<sql::PreparedStatement> remove(conn->prepareStatement(
				"DELETE FROM Item WHERE Uin=? AND ItemID=? AND ItemNum=0;"));
			remove->clearParameters();
			remove->setUInt(1, Uin);
			remove->setUInt(2, ItemID);
			remove->executeUpdate();
			remove.reset();
		}
		conn->commit();
		committed = true;
		try
		{
			conn->setAutoCommit(true);
		}
		catch (const std::exception& resetError)
		{
			std::cerr << resetError.what() << " PlayerDB_TryReduceItem AutoCommitReset Error" << std::endl;
		}
		return true;
	}
	catch (const std::exception& e)
	{
		try
		{
			if (!committed) conn->rollback();
			conn->setAutoCommit(true);
		}
		catch (const std::exception&) {}
		std::cerr << e.what() << " PlayerDB_TryReduceItem Error" << std::endl;
		Remaining = 0;
		return false;
	}
}

void PlayerDB_UpdateItemNum(UINT Uin, UINT ItemID, int Num)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET ItemNum=? WHERE Uin = ? AND ItemID = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, Num);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->setInt(3, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateItemNum Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateItemWareHouse(UINT Uin, UINT ItemID, int OpType)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET WareHouse=? WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, OpType == 1 ? 0 : 1);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->setUInt(3, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateItemWareHouse Error" << std::endl;
		//throw;??
	}
}


void PlayerDB_UpdateItemOtherAttribute(UINT Uin, UINT ItemID, int type)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET OtherAttribute=?  WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, type);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->setUInt(3, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateItemOtherAttribute Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_SelectWareHouseItem(UINT Uin, std::vector<ItemInfo>& Items)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	int ItemNum = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement>prep_stmt(conn->prepareStatement("SELECT ItemID,ItemNum,AvailPeriod,Status,ObtainTime  FROM Item WHERE Uin = ? and WareHouse = 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			Items[ItemNum].ItemID = res->getUInt("ItemID");
			Items[ItemNum].ItemNum = res->getUInt("ItemNum");
			Items[ItemNum].AvailPeriod = res->getInt("AvailPeriod");
			Items[ItemNum].Status = res->getUInt("Status");
			Items[ItemNum].ObtainTime = res->getUInt("ObtainTime");
			Items[ItemNum].OtherAttribute = 0;
			Items[ItemNum].ItemType = 0;
			ItemNum++;
			if (ItemNum >= 600)
			{
				break;
			}
		}
		res.reset();
		return ItemNum;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectWareHouseItem Error" << std::endl;
		//throw;??
	}
}


int PlayerDB_SelectItems(UINT Uin, std::vector<ItemInfo>& vecItem)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	int ItemNum = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement>prep_stmt(conn->prepareStatement("SELECT ItemID,ItemNum,AvailPeriod,Status,ObtainTime,OtherAttribute  FROM Item WHERE Uin = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		ItemNum = res->rowsCount();

		for (size_t i = 0; i < ItemNum; i++)
		{
			ItemInfo TI;
			if (res->next()) {
				TI.ItemID = res->getUInt("ItemID");
				TI.ItemNum = res->getUInt("ItemNum");
				TI.AvailPeriod = res->getInt("AvailPeriod");
				TI.Status = res->getUInt("Status");
				TI.ObtainTime = res->getUInt("ObtainTime");
				TI.OtherAttribute = res->getUInt("OtherAttribute");
				TI.ItemType = GetItemType(TI.ItemID);
			}

			vecItem.push_back(TI);
		}
		res.reset();
		return ItemNum;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectItems Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_SelectItemsStatus(UINT Uin, std::vector<ItemInfo>& vecItem)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	int ItemNum = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement>prep_stmt(conn->prepareStatement("SELECT ItemID,ItemNum,AvailPeriod,Status,ObtainTime,OtherAttribute  FROM Item WHERE Uin = ? And Status = 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		ItemNum = res->rowsCount();

		for (size_t i = 0; i < ItemNum; i++)
		{
			ItemInfo TI;
			if (res->next()) {
				TI.ItemID = res->getUInt("ItemID");
				TI.ItemNum = res->getUInt("ItemNum");
				TI.AvailPeriod = res->getInt("AvailPeriod");
				TI.Status = res->getUInt("Status");
				TI.ObtainTime = res->getUInt("ObtainTime");
				TI.OtherAttribute = res->getUInt("OtherAttribute");
				TI.ItemType = GetItemType(TI.ItemID);
			}

			vecItem.push_back(TI);
		}
		res.reset();
		return ItemNum;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectItems Error" << std::endl;
		//throw;??
	}
}


int PlayerDB_LoginSelectItems(UINT Uin, std::vector<ItemInfo>& vecItem)
{
	vecItem.clear();
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	int ItemNum = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement>prep_stmt(conn->prepareStatement("SELECT ItemID,ItemNum,AvailPeriod,Status,ObtainTime,OtherAttribute FROM Item WHERE Uin = ? AND WareHouse = 0 AND (AvailPeriod <= 0 OR (AvailPeriod * 60 * 60 + ObtainTime) > UNIX_TIMESTAMP()) ORDER BY ItemID ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		ItemNum = res->rowsCount();

		for (size_t i = 0; i < ItemNum; i++)
		{
			if (res->next()) {
				ItemInfo TI;
				TI.ItemID = res->getUInt("ItemID");
				TI.ItemNum = res->getUInt("ItemNum");
				TI.AvailPeriod = res->getInt("AvailPeriod");
				TI.Status = res->getUInt("Status");
				TI.ObtainTime = res->getUInt("ObtainTime");
				TI.OtherAttribute = res->getUInt("OtherAttribute");
				TI.ItemType = GetItemType(TI.ItemID);
				vecItem.push_back(TI);
			}
		}
		res.reset();
		return ItemNum;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectItems Error" << std::endl;
	}
	return 0;
}

int PlayerDB_LoginSelectOverItems(UINT Uin, std::vector<ItemInfo>& vecItem)
{
	vecItem.clear();
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	int ItemNum = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement>prep_stmt(conn->prepareStatement("SELECT ItemID,ItemNum,AvailPeriod,Status,ObtainTime,OtherAttribute FROM Item WHERE Uin = ? AND WareHouse = 0 AND (AvailPeriod > 0 AND (AvailPeriod * 60 * 60 + ObtainTime) < UNIX_TIMESTAMP()) ORDER BY ItemID ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		ItemNum = res->rowsCount();

		for (size_t i = 0; i < ItemNum; i++)
		{
			if (res->next()) {
				ItemInfo TI;
				TI.ItemID = res->getUInt("ItemID");
				TI.ItemNum = res->getUInt("ItemNum");
				TI.AvailPeriod = res->getInt("AvailPeriod");
				TI.Status = res->getUInt("Status");
				TI.ObtainTime = res->getUInt("ObtainTime");
				TI.OtherAttribute = res->getUInt("OtherAttribute");
				TI.ItemType = GetItemType(TI.ItemID);
				vecItem.push_back(TI);
			}
		}
		res.reset();

		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Item SET Status = 0 WHERE Uin = ? AND WareHouse = 0 AND (AvailPeriod > 0 AND (AvailPeriod * 60 * 60 + ObtainTime) < UNIX_TIMESTAMP());"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}

		PlayerDB_ClearExpiredSkinBindings(Uin);

		{
			int hasCar = 0;
			std::unique_ptr<sql::PreparedStatement>prep_stmt(conn->prepareStatement("SELECT Count(1)  FROM Item a,basegametypestatus b WHERE a.uin = b.uin and a.Itemid = b.KartID and a.Uin =? AND (AvailPeriod > 0 and (AvailPeriod * 60 * 60 + ObtainTime) < UNIX_TIMESTAMP());"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			// ????
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			prep_stmt.reset();
			// ????
			while (res->next()) {
				hasCar = res->getUInt(1);
			}
			res.reset();

			if (hasCar) {
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Update basegametypestatus Set Kartid = 10020 WHERE Uin = ?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, Uin);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}
		}

		return ItemNum;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectItems Error" << std::endl;
	}
	return 0;
}


BOOL PlayerDB_IsHasFriend(UINT Uin, UINT FriendUin)
{
	BOOL haveFriend = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return FALSE;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM FriendInfo Where Uin=? and FriendUin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, FriendUin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			haveFriend = 1;
			break;
		}
		res.reset();
		return haveFriend;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_IsHasFriend Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_InsertFriend(UINT Uin, UINT FriendUin)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO FriendInfo VALUES(?, ?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, FriendUin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertFriend Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateEngageInfo(UINT Uin, UINT EngageUin)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE EngageInfo SET EngageUin=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, EngageUin);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateEngageInfo Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateEngageInfoStat(std::shared_ptr<ClientNode> Client)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE EngageInfo SET Baby1EquipStat=?,Baby2EquipStat=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->LoverBaby1EquipStat);
		prep_stmt->setUInt(2, Client->LoverBaby2EquipStat);
		prep_stmt->setUInt(3, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateEngageInfoStat Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_InsertEngageInfo(UINT Uin, UINT EngageUin)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO EngageInfo VALUES(?, ?,0,0);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, EngageUin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertEngageInfo Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_DeleteEngageInfo(UINT Uin, UINT EngageUin)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM EngageInfo WHERE Uin = ? or Uin = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, EngageUin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_DeleteEngageInfo Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateHead(std::shared_ptr<ClientNode> Client)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE BaseInfo SET UserHeads=?,UserHeadsBox=?,UserFloor=? WHERE Uin = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, Client->UserHeads);
		prep_stmt->setInt(2, Client->UserHeadsBox);
		prep_stmt->setInt(3, Client->UserFloor);
		prep_stmt->setUInt(4, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateHead Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateNobel(UINT Uin, UINT NobleID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Player SET NobleID = ?  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, NobleID);
		prep_stmt->setUInt(2, Uin);

		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateNobel Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_InsertStoneNew2(UINT id, UINT Uin, UINT KartId, sql::Connection* conn)
{
	try
	{
		std::string sql = "Select *  FROM kartstone Where Id = ?";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, id);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		UINT SkillStoneID1 = 0;
		UINT SkillStoneID2 = 0;
		UINT SkillStoneID3 = 0;
		UINT SkillStoneID4 = 0;
		UINT SkillStoneID5 = 0;

		if (res->next()) {
			SkillStoneID1 = res->getUInt("SkillStoneID");
		}
		if (res->next()) {
			SkillStoneID2 = res->getUInt("SkillStoneID");
		}
		if (res->next()) {
			SkillStoneID3 = res->getUInt("SkillStoneID");
		}
		if (res->next()) {
			SkillStoneID4 = res->getUInt("SkillStoneID");
		}
		if (res->next()) {
			SkillStoneID5 = res->getUInt("SkillStoneID");
		}
		res.reset();

		int has = 0;
		{

			std::string sql = "Select *  FROM kartstonenew Where Uin= ? and Kartid = ?";
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setUInt(2, KartId);
			// ????
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			prep_stmt.reset();
			if (res->rowsCount() > 0) {

				has = 1;
			}
			res.reset();
		}
		if (has == 0)
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Insert Into kartstonenew(Uin,KartID,SkillStoneID1,SkillStoneID2,SkillStoneID3,SkillStoneID4,SkillStoneID5) Values(?,?,?,?,?,?,?)"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setUInt(2, KartId);
			prep_stmt->setUInt(3, SkillStoneID1);
			prep_stmt->setUInt(4, SkillStoneID2);
			prep_stmt->setUInt(5, SkillStoneID3);
			prep_stmt->setUInt(6, SkillStoneID4);
			prep_stmt->setUInt(7, SkillStoneID5);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertStoneNew2 Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_InsertStoneNew()
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	conn->setAutoCommit(false);
	try
	{
		int count = 0; // ???
		const int progressThreshold = 100000; // ??? 100 ?????????
		std::string sql = "Select *  FROM kartstonegroove";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		std::cout << "?? " << res->rowsCount() << " ???" << std::endl;
		while (res->next()) {
			UINT Uin = res->getUInt("Uin");
			UINT KartID = res->getUInt("KartID");
			int id = res->getInt("ID");
			PlayerDB_InsertStoneNew2(id, Uin, KartID, conn);
			// ?????
			count++;
			//? progressThreshold ?????????
			if (count % progressThreshold == 0) {
				std::cout << "??: ??? " << count << " ???" << std::endl;
			}
		}
		res.reset();
		conn->commit();
		conn->setAutoCommit(true);
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertStoneNew Error" << std::endl;
		//throw;??
	}
}


int PlayerDB_UpdateKartStone(UINT Uin, UINT KartID, UINT StoneGrooveID, UINT SkillID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		int id = -1;
		std::string sql = "SELECT SkillStoneID" + std::to_string(StoneGrooveID) + "  FROM kartstonenew  WHERE Uin=? AND KartID=?;";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, KartID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next()) {
			id = res->getInt(1);
		}
		res.reset();

		{
			sql = "UPDATE kartstonenew SET SkillStoneID" + std::to_string(StoneGrooveID) + " = ?,StoneUseOccaType" + std::to_string(StoneGrooveID) + " =? WHERE Uin = ? AND KartID = ? ; ";
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, SkillID);
			prep_stmt->setUInt(2, 0);
			prep_stmt->setUInt(3, Uin);
			prep_stmt->setUInt(4, KartID);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}
		return id;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateKartStone Error" << std::endl;
		//throw;??
	}
}


void PlayerDB_SelectKartStone(UINT Uin, UINT KartID, std::vector<KartStoneItemInfo>& kartStoneItemInfo)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT *  FROM kartstonenew WHERE Uin =? and KartID = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, KartID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next()) {
			for (size_t i = 1; i <= 5; i++)
			{
				KartStoneItemInfo KI;
				KI.StoneUseOccaType = res->getUInt("StoneUseOccaType" + std::to_string(i));
				KI.SkillStoneID = res->getUInt("SkillStoneID" + std::to_string(i));
				kartStoneItemInfo.push_back(KI);
			}
			break;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectKartStone Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdatMagicLampInfo(UINT Uin, UINT ItemID, int LightOn, int LuckyValue)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE MagicLampInfo SET LightOn=?,LuckyValue=?  WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, LightOn);
		prep_stmt->setUInt(2, LuckyValue);
		prep_stmt->setUInt(3, Uin);
		prep_stmt->setUInt(4, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdatMagicLampInfo Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_DeleteMagicCupTempBox(UINT Uin, UINT ItemID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM MagicCupTempBox WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_DeleteMagicCupTempBox Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_GetMagicLampNum(DWORD Uin, DWORD ItemID)
{
	int CurrentRound = -1;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("select LightOn from MagicLampInfo where Uin=? and ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ItemID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			res->next();
			CurrentRound = res->getInt("LightOn");
		}
		res.reset();
	}
	catch (const std::exception  e)
	{
		std::cerr << e.what() << "PlayerDB_GetMagicLampNum Error" << std::endl;
		//throw;??
	}
	return CurrentRound;
}

void PlayerDB_InsertMagicLamp(DWORD Uin, DWORD ItemID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO MagicLampInfo(Uin,ItemID) VALUES(?, ?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertMagicLamp Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateBugletTalk(std::shared_ptr<ClientNode> Client)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE BaseInfo SET BugletTalk=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->BugletTalk);
		prep_stmt->setUInt(2, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateBugletTalk Error" << std::endl;
		//throw;??
	}
}

UINT PlayerDB_SelectGuildID(UINT GuildID, std::string& NickName)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	UINT Uin = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT GI.Uin,B.NickName FROM GuildInfo GI join BaseInfo B on GI.Uin=B.Uin where GI.Duty=0 and GI.GuildID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, GuildID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next()) {
			Uin = res->getUInt("Uin");
			NickName = UTF2GB(res->getString("NickName").c_str());
		}
		res.reset();
		return Uin;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectGuildID Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_SelectGuildInfo(UINT GuildID, Guidinfo*& guidinfo)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	UINT Uin = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ID,Name,EnlistType,RankLevel,Score,Attribute,MaxMember,CurrentMember,TeamAddition,Prosperity,Level FROM GuildList where ID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, GuildID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			guidinfo[0].ID = res->getUInt("ID");
			guidinfo[0].RankLevel = res->getUInt("RankLevel");
			guidinfo[0].Score = res->getUInt("Score");
			guidinfo[0].Attribute = res->getUInt("Attribute");
			guidinfo[0].MaxMember = res->getUInt("MaxMember");
			guidinfo[0].CurrentMember = res->getUInt("CurrentMember");
			guidinfo[0].TeamAddition = res->getUInt("TeamAddition");
			guidinfo[0].Prosperity = res->getUInt("Prosperity");
			guidinfo[0].Level = res->getUInt("Level");
			guidinfo[0].Name = UTF2GB(res->getString("Name").c_str());
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectGuildInfo Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_SelectGuildInfoByUin(UINT Uin, Guidinfo*& guidinfo)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT GI.GuildID,GI.Duty,GL.Name FROM GuildInfo GI join GuildList GL on GI.GuildID=GL.ID where GI.Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			guidinfo[0].ID = res->getUInt("GuildID");
			guidinfo[0].Duty = res->getUInt("Duty");
			guidinfo[0].Name = UTF2GB(res->getString("Name").c_str());
			break;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectGuildInfoByUin Error" << std::endl;
		//throw;??
	}
}


UINT PlayerDB_SelectGuildInfoAll(Guidinfo*& guidinfo)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	UINT count = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT * FROM GuildList Order By Score desc;"));
		prep_stmt->clearParameters();
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		count = res->rowsCount();
		guidinfo = new Guidinfo[count];
		memset(guidinfo, 0, sizeof(Guidinfo) * count);
		// ????
		for (size_t i = 0; i < count; i++)
		{
			if (res->next()) {
				guidinfo[i].ID = res->getUInt("ID");
				guidinfo[i].RankLevel = res->getUInt("RankLevel");
				guidinfo[i].Score = res->getUInt("Score");
				guidinfo[i].Attribute = res->getUInt("Attribute");
				guidinfo[i].MaxMember = res->getUInt("MaxMember");
				guidinfo[i].CurrentMember = res->getUInt("CurrentMember");
				guidinfo[i].TeamAddition = res->getUInt("TeamAddition");
				guidinfo[i].Prosperity = res->getUInt("Prosperity");
				guidinfo[i].Level = res->getUInt("Level");

				guidinfo[i].Name = UTF2GB(res->getString("Name").c_str());
				guidinfo[i].EnlistType = res->getUInt("EnlistType");
			}
		}
		res.reset();
		return count;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectGuildInfoAll Error" << std::endl;
		//throw;??
	}
}


UINT PlayerDB_SelectGuildMember(UINT GuildID, Guidinfo*& guidinfo)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	UINT count = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT GI.Uin,GI.Duty,GI.MemProsperity,B.NickName FROM GuildInfo GI join BaseInfo B on GI.Uin=B.Uin where GuildID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, GuildID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();

		count = res->rowsCount();
		guidinfo = new Guidinfo[count];
		memset(guidinfo, 0, sizeof(Guidinfo) * count);
		// ????
		for (size_t i = 0; i < count; i++)
		{
			if (res->next()) {
				guidinfo[i].Uin = res->getUInt("Uin");
				guidinfo[i].Duty = res->getUInt("Duty");
				guidinfo[i].MemProsperity = res->getUInt("MemProsperity");
				guidinfo[i].NickName = UTF2GB(res->getString("NickName").c_str());
			}
		}
		res.reset();
		return count;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectGuildMember Error" << std::endl;
		//throw;??
	}
}



void PlayerDB_InsertGuildList(UINT Uin, char* newGuildname)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("insert into GuildList(name,Score,Attribute,MaxMember,CurrentMember,Prosperity,Level,TeamAddition,RankLevel,LeaderUin) values(?,1,33,100,1,1,1,1,1,?);"));
		prep_stmt->clearParameters();
		if (newGuildname)
		{
			prep_stmt->setString(1, newGuildname);
		}
		prep_stmt->setUInt(2, Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertGuildList Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateGuildList(UINT ID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE GuildList SET CurrentMember=CurrentMember+1 WHERE ID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, ID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateGuildList Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateGuildListEnlistType(UINT ID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE GuildList SET EnlistType=3 WHERE ID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, ID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateGuildListEnlistType Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateGuildListCurrentMember(UINT ID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE GuildList SET CurrentMember=CurrentMember-1,EnlistType=2 WHERE ID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, ID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateGuildListCurrentMember Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_InsertGuildInfo(DWORD Uin, UINT GuildID, UINT Duty)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("insert into GuildInfo(Uin,GuildID,Duty) values(?,?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, GuildID);
		prep_stmt->setUInt(3, Duty);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertGuildInfo Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_DeleteGuildInfo(DWORD Uin)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM GuildInfo  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_DeleteGuildInfo Error" << std::endl;
		//throw;??
	}
}

void  PlayerDB_UpdateGuildInfoDuty(UINT Uin, UINT Duty)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Update  GuildInfo Set Duty = ?  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Duty);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateGuildInfoDuty Error" << std::endl;
		//throw;??
	}
}
void PlayerDB_UpdateGuildInfoDuty1(UINT Uin, UINT Duty, bool isTransferLeader)
{
	DBConnection myDBConnection(player);
	auto conn = myDBConnection.getConnection();
	if (!conn) return;

	try {
		// ????(????)
		conn->setAutoCommit(false);

		if (isTransferLeader && Duty == 0) {  // ?? 0 ?????? Duty ?
			// 1. ??????????????(Duty = 7)
			std::unique_ptr<sql::PreparedStatement> clear_stmt(conn->prepareStatement("UPDATE GuildInfo SET Duty = 7 WHERE Duty = 0;"));
			int affectedRows = clear_stmt->executeUpdate();
			clear_stmt.reset();

			std::cout << "Cleared current leader position, affected rows: " << affectedRows << std::endl;
		}

		// 2. ?????????
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE GuildInfo SET Duty = ? WHERE Uin = ?;"));
		prep_stmt->setUInt(1, Duty);
		prep_stmt->setUInt(2, Uin);
		int affectedRows = prep_stmt->executeUpdate();
		prep_stmt.reset();

		std::cout << "Updated user position in GuildInfo, affected rows: " << affectedRows << std::endl;

		// ????
		conn->commit();
		conn->setAutoCommit(true);
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << " PlayerDB_UpdateGuildInfoDuty Error" << std::endl;
		conn->rollback(); // ?????????
	}
}




void PlayerDB_UpdateGuildListScore(UINT GuildID, UINT Score, UINT Prosperity)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE  GuildList Set Score = Score +?,Prosperity = Prosperity+?  WHERE ID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Score);
		prep_stmt->setUInt(2, Prosperity);
		prep_stmt->setUInt(3, GuildID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateGuildListScore Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateGuildInfoScore(UINT Uin, UINT GuildID, UINT MeProsperity)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE  GuildInfo Set MemProsperity = MemProsperity + ?  WHERE Uin = ? and GuildID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, MeProsperity);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->setUInt(3, GuildID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateGuildInfoScore Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_InsertKartRefit(DWORD Uin, UINT KartItemID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO KartRefit (Uin,KartID) VALUES (?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, KartItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertKartRefit Error" << std::endl;
		//throw;??
	}
}

void PlayerDB_UpdateKartRefit(DWORD Uin, UINT* RefitItemID, UINT ShapeSuitID, UINT ShapeLegendSuitID, UINT SkinSuitID, UINT KartItemID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		const char* sql = nullptr;
		sql = "UPDATE KartRefit SET KartHeadRefitItemID=?, KartTailRefitItemID=?, KartFlankRefitItemID=?, "
			"KartTireRefitItemID=?, ShapeSuitID=?,ShapeLegendSuitID=?, SkinSuitID=? WHERE Uin=? AND KartID=?;";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, RefitItemID[0]);//??
		prep_stmt->setUInt(2, RefitItemID[1]);//??
		prep_stmt->setUInt(3, RefitItemID[2]);//??
		prep_stmt->setUInt(4, RefitItemID[3]);//??

		prep_stmt->setUInt(5, ShapeSuitID);//????
		//prep_stmt->setUInt(6, LegendSuitLevel);//????????
		//prep_stmt->setUInt(7, LegendSuitLevelChoice);//??????????
		prep_stmt->setUInt(6, ShapeLegendSuitID);//??????ID
		prep_stmt->setUInt(7, SkinSuitID);//????
		prep_stmt->setUInt(8, Uin);
		prep_stmt->setUInt(9, KartItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateKartRefit Error" << std::endl;
		//throw;??
	}
}


void PlayerDB_UpNickName(DWORD Uin, std::string& NickName)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE BaseInfo SET NickName=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, NickName);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpNickName Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_SelectNickName(UINT Uin, std::string& NickName)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT NickName FROM BaseInfo  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			NickName = UTF2GB(res->getString("NickName").c_str());
			break;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectNickName Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_SelectMapInfo(UINT Uin, std::vector<MapInfo>& mapInfo)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT MapID,Record  FROM MapRecord WHERE Uin = ?;"));

		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			MapInfo MI;
			MI.MapID = res->getUInt("MapID");
			MI.Record = res->getUInt("Record");
			mapInfo.push_back(MI);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectMapInfo Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_SelectPetInfo(UINT Uin, std::vector<PetInfo>& petInfo)
{
	petInfo.clear();
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT PetId,Name,Experience,PL,Status,StrengLevel FROM Pet WHERE Uin=? ORDER BY PetId ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			PetInfo PI{};
			PI.ID = res->getUInt("PetId");
			if (!QQSPetConfig::HasPet(PI.ID))
				continue;
			const std::string name = UTF2GB(res->getString("Name").c_str());
			if (name.empty() || name.size() > sizeof(PI.Name))
				continue;
			memcpy(PI.Name, name.data(), name.size());
			PI.Exp = res->getUInt("Experience");
			PI.PL = res->getUInt("PL");
			PI.Status = res->getUInt("Status");
			PI.StrengLevel = res->getUInt("StrengLevel");
			if (QQS_CompletePetInfo(PI))
				petInfo.push_back(PI);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectPetInfo Error" << std::endl;
		//throw;??
	}
	return static_cast<int>(petInfo.size());
}

int PlayerDB_SelectRankInfo(std::shared_ptr<ClientNode> Client)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT GradeScore,GradeLevel,ChildLevel,TotalScore,PromotionMatch,CurrentPromotionRoundNums,CurrentPromotionWinRoundNums,CurrentPromotionLoseRoundNums,PromotionRecordTag FROM RankInfo Where Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			res->next();
			Client->GradeScore = res->getUInt(1);
			Client->GradeLevel = res->getUInt(2);
			Client->ChildLevel = res->getUInt(3);
			Client->TotalScore = res->getUInt("TotalScore");
			Client->PromotionMatch = res->getUInt("PromotionMatch");
			Client->CurrentPromotionRoundNums = res->getUInt("CurrentPromotionRoundNums");
			Client->CurrentPromotionWinRoundNums = res->getUInt("CurrentPromotionWinRoundNums");
			Client->CurrentPromotionLoseRoundNums = res->getUInt("CurrentPromotionLoseRoundNums");
			Client->PromotionRecordTag = res->getUInt("PromotionRecordTag");

			if (Client->GradeLevel > 7)
			{
				Client->GradeLevel = 7;
			}
		}
		else
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO RankInfo(Uin, GradeScore, GradeLevel, ChildLevel) VALUES(? , 0, 0, 3);"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Client->Uin);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectRankInfo Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_GetMagicCupTempBoxNum(DWORD Uin, DWORD ItemID)
{
	int Itemnum = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemNum FROM MagicCupTempBox  WHERE Uin=? and ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ItemID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			res->next();
			Itemnum = res->getInt("ItemNum");
		}
		res.reset();
	}
	catch (const std::exception  e)
	{
		std::cerr << e.what() << "PlayerDB_GetMagicCupTempBoxNum Error" << std::endl;
		//throw;??
	}
	return Itemnum;
}

int  PlayerDB_GetMagicCupTempBox(DWORD Uin, std::vector<ItemInfo>& Items)
{
	int num = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemID,ItemNum,ItemType FROM MagicCupTempBox  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			Items[num].ItemID = res->getUInt("ItemID");
			Items[num].ItemNum = res->getUInt("ItemNum");
			Items[num].ItemType = res->getUInt("ItemType");
			num++;
			if (num == 1000)//???1000
			{
				break;
			}
		}
		res.reset();
		return num;
	}
	catch (const std::exception  e)
	{
		std::cerr << e.what() << "PlayerDB_GetMagicCupTempBox Error" << std::endl;
		//throw;??
	}
}

void  PlayerDB_GetMagicFormationNum(DWORD Uin, std::unordered_map<int, int>& Items)
{
	int num = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemID FROM MagicFormation  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			UINT ID = res->getUInt("ItemID");
			Items[ID] = 1;
		}
		res.reset();
	}
	catch (const std::exception  e)
	{
		std::cerr << e.what() << "PlayerDB_GetMagicFormationNum Error" << std::endl;
		//throw;??
	}
}

void  PlayerDB_InsertMagicFormation(DWORD Uin, UINT ItemID)
{
	int num = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO MagicFormation(Uin,ItemID) VALUES(?, ?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception  e)
	{
		std::cerr << e.what() << "PlayerDB_InsertMagicFormation Error" << std::endl;
		//throw;??
	}
}

void  PlayerDB_GetMagicShopInfo(DWORD Uin, std::unordered_map<int, int>& Items)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemID FROM MagicShopInfo  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			UINT ID = res->getUInt("ItemID");
			Items[ID] = 1;
		}
		res.reset();
	}
	catch (const std::exception  e)
	{
		std::cerr << e.what() << "PlayerDB_GetMagicShopInfo Error" << std::endl;
		//throw;??
	}
}


void  PlayerDB_InsertMagicShopInfo(DWORD Uin, UINT ItemID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO MagicShopInfo(Uin,ItemID) VALUES(?, ?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception  e)
	{
		std::cerr << e.what() << "PlayerDB_InsertMagicShopInfo Error" << std::endl;
		//throw;??
	}
}


int PlayerDB_GetPetLuck(DWORD Uin, UINT Petid)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	int Luckvalue = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT luckvalue FROM pet  WHERE Uin=? and petid = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, Petid);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		if (res->rowsCount() > 0) {
			res->next();
			Luckvalue = res->getUInt("luckvalue");
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetPetLuck Error" << std::endl;
		//throw;??
	}
	return Luckvalue;
}

void PlayerDB_UpdatePetLuck(DWORD Uin, UINT Petid, UINT Luck)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE pet set LuckValue = LuckValue + ?  WHERE Uin=? AND PetId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Luck);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->setUInt(3, Petid);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdatePetLuck Error" << std::endl;
		//throw;??
	}

}


void PlayerDB_UpdatePetStrengLevel(DWORD Uin, UINT Petid, UINT StrengLevel)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE pet SET StrengLevel = ?,Luckvalue = 0  WHERE Uin=? AND PetId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, StrengLevel);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->setUInt(3, Petid);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdatePetStrengLevel Error" << std::endl;
		//throw;??
	}

}


bool PlayerDB_ApplyPetStrengthenAttempt(
	DWORD Uin,
	UINT Petid,
	UINT ExpectedStrengLevel,
	UINT ItemID,
	UINT UseItemNum,
	UINT CostSuperMoney,
	bool StrengthenSucceeded,
	UINT FailAwardExp,
	PetStrengthenApplyResult& Result)
{
	Result = PetStrengthenApplyResult{};
	if (Uin == 0 || Petid == 0 || !QQSPetConfig::HasPet(Petid) ||
		ExpectedStrengLevel == UINT_MAX || (UseItemNum > 0 && ItemID == 0))
	{
		Result.Code = PET_STRENGTHEN_APPLY_INVALID;
		return false;
	}

	DBConnection connection(player);
	auto conn = connection.getConnection();
	if (!conn)
	{
		Result.Code = PET_STRENGTHEN_APPLY_DATABASE;
		return false;
	}

	bool transactionOpen = false;
	auto rollbackAndReset = [&]()
	{
		if (!transactionOpen)
			return;
		try
		{
			conn->rollback();
		}
		catch (...)
		{
			try { conn->close(); } catch (...) {}
			transactionOpen = false;
			return;
		}
		try
		{
			conn->setAutoCommit(true);
		}
		catch (...)
		{
			try { conn->close(); } catch (...) {}
		}
		transactionOpen = false;
	};
	auto reject = [&](UINT code)
	{
		Result.Code = code;
		rollbackAndReset();
		return false;
	};

	try
	{
		conn->setAutoCommit(false);
		transactionOpen = true;

		// Lock order is stable for every attempt: BaseInfo -> Pet -> owned items.
		UINT dbSuperMoney = 0;
		UINT dbExperience = 0;
		{
			std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
				"SELECT SuperMoney,Experience FROM BaseInfo WHERE Uin=? FOR UPDATE;"));
			stmt->setUInt(1, Uin);
			std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());
			if (res->rowsCount() != 1 || !res->next())
				return reject(PET_STRENGTHEN_APPLY_BASE_ROW);
			dbSuperMoney = res->getUInt("SuperMoney");
			dbExperience = res->getUInt("Experience");
		}
		if (dbSuperMoney < CostSuperMoney)
			return reject(PET_STRENGTHEN_APPLY_MONEY);

		UINT dbStrengLevel = 0;
		UINT dbLuckValue = 0;
		{
			std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
				"SELECT StrengLevel,LuckValue FROM Pet WHERE Uin=? AND PetId=? FOR UPDATE;"));
			stmt->setUInt(1, Uin);
			stmt->setUInt(2, Petid);
			std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());
			// The supplied schema has no unique key for (Uin,PetId). Never update
			// an ambiguous duplicate set as one strengthen operation.
			if (res->rowsCount() != 1 || !res->next())
				return reject(PET_STRENGTHEN_APPLY_PET_ROW);
			dbStrengLevel = res->getUInt("StrengLevel");
			dbLuckValue = res->getUInt("LuckValue");
		}
		if (dbStrengLevel != ExpectedStrengLevel)
			return reject(PET_STRENGTHEN_APPLY_STALE_LEVEL);

		// A Pet history row is not ownership. The matching, non-expired Item is
		// required before any card or currency can be consumed.
		{
			std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
				"SELECT ItemID FROM Item WHERE Uin=? AND ItemID=? AND ItemNum>0 "
				"AND (AvailPeriod<=0 OR (AvailPeriod*60*60+ObtainTime)>UNIX_TIMESTAMP()) "
				"FOR UPDATE;"));
			stmt->setUInt(1, Uin);
			stmt->setUInt(2, Petid);
			std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());
			if (res->rowsCount() != 1 || !res->next())
				return reject(PET_STRENGTHEN_APPLY_ITEM_ROW);
		}

		INT dbCardNum = -1;
		if (UseItemNum > 0)
		{
			std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
				"SELECT ItemNum FROM Item WHERE Uin=? AND ItemID=? FOR UPDATE;"));
			stmt->setUInt(1, Uin);
			stmt->setUInt(2, ItemID);
			std::unique_ptr<sql::ResultSet> res(stmt->executeQuery());
			if (res->rowsCount() != 1 || !res->next())
				return reject(PET_STRENGTHEN_APPLY_ITEM_ROW);
			dbCardNum = res->getInt("ItemNum");
			if (dbCardNum < 0 || static_cast<UINT>(dbCardNum) < UseItemNum)
				return reject(PET_STRENGTHEN_APPLY_ITEM_NUM);
		}

		const UINT newSuperMoney = dbSuperMoney - CostSuperMoney;
		const UINT expDelta = StrengthenSucceeded ? 0 : FailAwardExp;
		const unsigned long long newExperience64 =
			static_cast<unsigned long long>(dbExperience) + expDelta;
		const UINT newExperience = newExperience64 > UINT_MAX
			? UINT_MAX : static_cast<UINT>(newExperience64);
		const UINT newStrengLevel = StrengthenSucceeded
			? ExpectedStrengLevel + 1 : ExpectedStrengLevel;
		if (!StrengthenSucceeded && dbLuckValue == UINT_MAX)
			return reject(PET_STRENGTHEN_APPLY_DATABASE);
		const UINT newLuckValue = StrengthenSucceeded ? 0 : dbLuckValue + 1;
		const INT cardNumAfter = UseItemNum > 0
			? dbCardNum - static_cast<INT>(UseItemNum) : -1;

		if (UseItemNum > 0)
		{
			const char* sql = cardNumAfter == 0
				? "DELETE FROM Item WHERE Uin=? AND ItemID=? AND ItemNum=?;"
				: "UPDATE Item SET ItemNum=? WHERE Uin=? AND ItemID=? AND ItemNum=?;";
			std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(sql));
			if (cardNumAfter == 0)
			{
				stmt->setUInt(1, Uin);
				stmt->setUInt(2, ItemID);
				stmt->setInt(3, dbCardNum);
			}
			else
			{
				stmt->setInt(1, cardNumAfter);
				stmt->setUInt(2, Uin);
				stmt->setUInt(3, ItemID);
				stmt->setInt(4, dbCardNum);
			}
			if (stmt->executeUpdate() != 1)
				return reject(PET_STRENGTHEN_APPLY_DATABASE);
		}

		if (newSuperMoney != dbSuperMoney || newExperience != dbExperience)
		{
			std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
				"UPDATE BaseInfo SET SuperMoney=?,Experience=? WHERE Uin=? AND SuperMoney=? AND Experience=?;"));
			stmt->setUInt(1, newSuperMoney);
			stmt->setUInt(2, newExperience);
			stmt->setUInt(3, Uin);
			stmt->setUInt(4, dbSuperMoney);
			stmt->setUInt(5, dbExperience);
			if (stmt->executeUpdate() != 1)
				return reject(PET_STRENGTHEN_APPLY_DATABASE);
		}

		{
			std::unique_ptr<sql::PreparedStatement> stmt(conn->prepareStatement(
				"UPDATE Pet SET StrengLevel=?,LuckValue=? WHERE Uin=? AND PetId=? AND StrengLevel=? AND LuckValue=?;"));
			stmt->setUInt(1, newStrengLevel);
			stmt->setUInt(2, newLuckValue);
			stmt->setUInt(3, Uin);
			stmt->setUInt(4, Petid);
			stmt->setUInt(5, dbStrengLevel);
			stmt->setUInt(6, dbLuckValue);
			if (stmt->executeUpdate() != 1)
				return reject(PET_STRENGTHEN_APPLY_DATABASE);
		}

		conn->commit();
		transactionOpen = false;
		try { conn->setAutoCommit(true); }
		catch (...) { try { conn->close(); } catch (...) {} }

		Result.Code = PET_STRENGTHEN_APPLY_OK;
		Result.OldLuckValue = dbLuckValue;
		Result.NewLuckValue = newLuckValue;
		Result.NewStrengLevel = newStrengLevel;
		Result.NewSuperMoney = newSuperMoney;
		Result.NewExperience = newExperience;
		Result.CardNumAfter = cardNumAfter;
		return true;
	}
	catch (const std::exception& e)
	{
		rollbackAndReset();
		Result.Code = PET_STRENGTHEN_APPLY_DATABASE;
		std::cerr << e.what() << " PlayerDB_ApplyPetStrengthenAttempt Error" << std::endl;
	}
	return false;
}


void PlayerDB_InsertAuctionUnPayInfo(DWORD Uin, UINT BidPrice, AuctionItemInfo AuctionItems)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO AuctionUnPayInfo(BidUin,CommodityId,BidPrice,ItemId,ItemAvailPeriod,ItemNumber) VALUES(?,?,?,?,?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, AuctionItems.CommodityId);
		prep_stmt->setUInt(3, BidPrice);
		prep_stmt->setUInt(4, AuctionItems.ItemID);
		prep_stmt->setInt(5, AuctionItems.AvailPeriod);
		prep_stmt->setUInt(6, AuctionItems.ItemNum);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertAuctionUnPayInfo Error" << std::endl;
		//throw;??
	}

}

void PlayerDB_DeleteAuctionInfo(UINT CommodityId)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM AuctionInfo WHERE CommodityId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, CommodityId);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_DeleteAuctionInfo Error" << std::endl;
		//throw;??
	}

}

void PlayerDB_DeleteAuctionUnPayInfo(UINT Uin, UINT CommodityId)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM AuctionUnPayInfo WHERE CommodityId=? and BidUin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, CommodityId);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_DeleteAuctionUnPayInfo Error" << std::endl;
		//throw;??
	}

}

UINT PlayerDB_SelectAuctionUnPayInfo(DWORD Uin, std::vector<AuctionItemInfo>& auction)
{
	UINT UnPayNumber = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("select * from AuctionUnPayInfo where BidUin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		UnPayNumber = res->rowsCount();
		while (res->next()) {
			AuctionItemInfo AI;
			AI.CommodityId = res->getInt("CommodityId");
			AI.BidPrice = res->getUInt("BidPrice");
			AI.ItemID = res->getUInt("ItemId");
			AI.AvailPeriod = res->getInt("ItemAvailPeriod");
			AI.ItemNum = res->getUInt("ItemNumber");
			auction.push_back(AI);
		}
		res.reset();
		return UnPayNumber;

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectAuctionUnPayInfo Error" << std::endl;
		//throw;??
	}

}


int PlayerDB_SelectPlayerInfo(std::shared_ptr<ClientNode> Client, std::vector<PlayerInfo>& playerInfo)
{
	int ret = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT VipFlag,LastLoginTime,NobleID,NobleLevel,NoblePoint,GuildVipLevel,GuildVipPoint,LoverVipLevel,LoverVipPoint,EmperorLevel,EmperorPoint,GardenLevel,GardenPoint FROM Player WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			ret = 1;
			res->next();
			PlayerInfo PI;
			PI.VipFlag = res->getUInt("VipFlag");
			PI.NobleID = res->getUInt("NobleID");
			PI.NobleLevel = res->getUInt("NobleLevel");
			PI.NoblePoint = res->getUInt("NoblePoint");
			PI.GuildVipLevel = res->getUInt("GuildVipLevel");
			PI.GuildVipPoint = res->getUInt("GuildVipPoint");
			PI.LoverVipLevel = res->getUInt("LoverVipLevel");
			PI.LoverVipPoint = res->getUInt("LoverVipPoint");

			PI.EmperorLevel = res->getUInt("EmperorLevel");
			PI.EmperorPoint = res->getUInt("EmperorPoint");
			PI.GardenLevel = res->getUInt("GardenLevel");
			PI.GardenPoint = res->getUInt("GardenPoint");
			PI.LastLoginTime = UTF2GB(res->getString("LastLoginTime").c_str());
			playerInfo.push_back(PI);
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectPlayerInfo Error" << std::endl;
		//throw;??
	}

}

int PlayerDB_SelectPlayerInfo(UINT Uin, std::vector<PlayerInfo>& playerInfo)
{
	int ret = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT VipFlag,LastLoginTime,NobleID,NobleLevel,NoblePoint,GuildVipLevel,GuildVipPoint,LoverVipLevel,LoverVipPoint,EmperorLevel,EmperorPoint,GardenLevel,GardenPoint FROM Player WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			ret = 1;
			res->next();
			PlayerInfo PI;
			PI.VipFlag = res->getUInt("VipFlag");
			PI.NobleID = res->getUInt("NobleID");
			PI.NobleLevel = res->getUInt("NobleLevel");
			PI.NoblePoint = res->getUInt("NoblePoint");
			PI.GuildVipLevel = res->getUInt("GuildVipLevel");
			PI.GuildVipPoint = res->getUInt("GuildVipPoint");
			PI.LoverVipLevel = res->getUInt("LoverVipLevel");
			PI.LoverVipPoint = res->getUInt("LoverVipPoint");

			PI.EmperorLevel = res->getUInt("EmperorLevel");
			PI.EmperorPoint = res->getUInt("EmperorPoint");
			PI.GardenLevel = res->getUInt("GardenLevel");
			PI.GardenPoint = res->getUInt("GardenPoint");
			PI.LastLoginTime = UTF2GB(res->getString("LastLoginTime").c_str());
			playerInfo.push_back(PI);
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectPlayerInfo Error" << std::endl;
		//throw;??
	}

}


int PlayerDB_UpdatePlayerInfo(std::shared_ptr<ClientNode> Client, char* CurLoginTm)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Player SET LastLoginTime=?,NobleLevel=?,NoblePoint=?,GuildVipPoint=?,LoverVipPoint=?,EmperorPoint=?,GardenLevel=?,GardenPoint=? WHERE Uin = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, CurLoginTm);
		prep_stmt->setUInt(2, Client->NobleLevel);
		prep_stmt->setUInt(3, Client->NoblePoint);
		prep_stmt->setUInt(4, Client->GuildVipPoint);
		prep_stmt->setUInt(5, Client->LoverVipPoint);
		prep_stmt->setUInt(6, Client->EmperorPoint);
		prep_stmt->setUInt(7, Client->GardenLevel);
		prep_stmt->setUInt(8, Client->GardenPoint);
		prep_stmt->setUInt(9, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdatePlayerInfo Error" << std::endl;
		//throw;??
	}

}


int PlayerDB_InsertPlayer(UINT Uin)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Player (Uin,VipFlag,IsInTopList,NobleID) VALUES (?,?,?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, 0x0107);
		prep_stmt->setUInt(3, 1);
		prep_stmt->setUInt(4, Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_InsertPlayer Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_InsertBaseInfo(UINT Uin, char* newname, UINT Gender, UINT Country)
{
	DBConnection myDBConnection(player);
	auto conn = myDBConnection.getConnection();
	if (!conn) return 0;

	try {
		// ?? SuperMoney ???? 1888888
		std::unique_ptr<sql::PreparedStatement> prep_stmt(
			conn->prepareStatement("INSERT INTO BaseInfo (Uin, NickName, Gender, Country, SuperMoney) VALUES (?, ?, ?, ?, 1888888);")
		);

		prep_stmt->setUInt(1, Uin);

		if (newname) {
			prep_stmt->setString(2, newname);
			delete[] newname;
			newname = NULL;
		}

		prep_stmt->setUInt(3, Gender);
		prep_stmt->setUInt(4, Country);

		prep_stmt->executeUpdate();

		// ????(????)
		conn->commit();

		prep_stmt.reset();

		// ?? BaseGameTypeStatus
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(
				conn->prepareStatement("INSERT INTO BaseGameTypeStatus (Uin) VALUES (?);")
			);

			prep_stmt->setUInt(1, Uin);
			prep_stmt->executeUpdate();
			conn->commit();
			prep_stmt.reset();
		}
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << " - PlayerDB_InsertBaseInfo Error" << std::endl;
	}

	return 1;
}


int PlayerDB_UpdateBaseInfo(UINT Uin, std::string qqNumber)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Update BaseInfo Set QQ =? WHere Uin =?"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, qqNumber);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateBaseInfo Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_UpdateBaseInfoFps(std::shared_ptr<ClientNode> Client)
{
	Client->fps = (Client->fps == 0) ? 2 : 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Update BaseInfo Set fps =? WHere Uin =?"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->fps);
		prep_stmt->setUInt(2, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
		return Client->fps;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateBaseInfoFps Error" << std::endl;
		//throw;??
	}
}


int PlayerDB_SelectBaseInfoKartLibaray(UINT Uin, std::vector<UINT>& myKartLibrary)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Select KartLibaray From BaseInfo  WHere Uin =?"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			std::string KartLibaray = res->getString("KartLibaray");
			initializeLibrary(myKartLibrary, KartLibaray); // ?????????
			break;
		}
		res.reset();
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectBaseInfoKartLibaray Error" << std::endl;
		//throw;??
	}
}


int PlayerDB_UpdateBaseInfoKartLibaray(std::shared_ptr<ClientNode> Client)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::ostringstream oss;
		for (size_t i = 0; i < Client->MyKartLibrary.size(); ++i) {
			oss << Client->MyKartLibrary[i];
			if (i < Client->MyKartLibrary.size() - 1) {
				oss << '^'; // ?????
			}
		}
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Update BaseInfo Set KartLibaray =? WHere Uin =?"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, oss.str());
		prep_stmt->setUInt(2, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateBaseInfoKartLibaray Error" << std::endl;
		//throw;??
	}
}


int PlayerDB_SeleteBaseInfoHead(UINT Uin, std::vector<PlayerInfo>& playerInfo)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Select UserHeads,UserHeadsBox,UserFloor,QQ From BaseInfo Where Uin =?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);

		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			PlayerInfo PI;
			PI.UserHeads = res->getUInt("UserHeads");
			PI.UserHeadsBox = res->getUInt("UserHeadsBox");
			PI.UserFloor = res->getUInt("UserFloor");
			PI.qqNumber = res->getString("QQ");
			PI.Uin = Uin;
			playerInfo.push_back(PI);
			break;
		}
		res.reset();
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SeleteBaseInfoHead Error" << std::endl;
		//throw;??
	}
}



int PlayerDB_SelectEngageInfo(std::shared_ptr<ClientNode> Client)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT EngageUin,Baby1EquipStat,Baby2EquipStat FROM EngageInfo WHERE Uin = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			Client->EngageUin = res->getUInt("EngageUin");
			Client->LoverBaby1EquipStat = res->getUInt("Baby1EquipStat");
			Client->LoverBaby2EquipStat = res->getUInt("Baby2EquipStat");
			break;
		}
		res.reset();

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectEngageInfo Error" << std::endl;
		//throw;??
	}
}



int PlayerDB_SelectComItemTableNum(std::shared_ptr<ClientNode> Client) {

	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection();  if (!conn) return 0;
	int ItemNum = 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT COUNT(*) FROM ("
			"SELECT 1 FROM RetComItemTable WHERE Uin=? AND OpNum>0 "
			"GROUP BY ItemId,Aviable"
			") AS ActiveTempItems;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			res->next();
			ItemNum = res->getUInt(1);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectComItemTableNum Error" << std::endl;
		//throw;??
	}
	return ItemNum;
}


void PlayerDB_GetComItemTableRows(std::shared_ptr<ClientNode> Client, std::vector<CarTreasureComItemRow>& Rows)
{
	Rows.clear();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT ItemId,SUM(OpNum) AS OpNum,Aviable "
			"FROM RetComItemTable WHERE Uin=? AND OpNum>0 "
			"GROUP BY ItemId,Aviable ORDER BY MIN(CreateTime),ItemId,Aviable;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			CarTreasureComItemRow row;
			row.ItemId = res->getUInt("ItemId");
			row.OpNum = res->getUInt("OpNum");
			row.AvailPeriod = res->getInt("Aviable");
			row.ItemType = (GetItemType(row.ItemId) == EAIT_CAR) ? 1 : 0;
			Rows.push_back(row);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetComItemTableRows Error" << std::endl;
	}
}

bool PlayerDB_LoadCarTreasureSelectedGroups(UINT Uin, std::vector<int>& Groups, UINT ActivityId)
{
	Groups.clear();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Slot,GroupIndex FROM CarTreasureSelectedGroup WHERE Uin=? AND ActivityId=? ORDER BY Slot ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			Groups.push_back(res->getInt("Slot"));
			Groups.push_back(res->getInt("GroupIndex"));
		}
		res.reset();
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_LoadCarTreasureSelectedGroups Error" << std::endl;
	}
	return false;
}

void PlayerDB_SaveCarTreasureSelectedGroups(UINT Uin, const std::vector<int>& Groups, UINT ActivityId)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		conn->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> del_stmt(conn->prepareStatement("DELETE FROM CarTreasureSelectedGroup WHERE Uin=? AND ActivityId=?;"));
		del_stmt->clearParameters();
		del_stmt->setUInt(1, Uin);
		del_stmt->setUInt(2, ActivityId);
		del_stmt->executeUpdate();
		del_stmt.reset();

		std::unique_ptr<sql::PreparedStatement> ins_stmt(conn->prepareStatement("INSERT INTO CarTreasureSelectedGroup(Uin,ActivityId,Slot,GroupIndex,UpdateTime) VALUES(?,?,?,?,?);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		if ((Groups.size() % 2) == 0)
		{
			for (size_t i = 0; i + 1 < Groups.size(); i += 2)
			{
				int slot = Groups[i] > 0 ? Groups[i] : static_cast<int>((i / 2) + 1);
				int group = Groups[i + 1];
				if (group <= 0) continue;
				ins_stmt->clearParameters();
				ins_stmt->setUInt(1, Uin);
				ins_stmt->setUInt(2, ActivityId);
				ins_stmt->setInt(3, slot);
				ins_stmt->setInt(4, group);
				ins_stmt->setUInt(5, nowTime);
				ins_stmt->executeUpdate();
			}
		}
		else
		{
			for (size_t i = 0; i < Groups.size(); ++i)
			{
				int group = Groups[i];
				if (group <= 0) continue;
				ins_stmt->clearParameters();
				ins_stmt->setUInt(1, Uin);
				ins_stmt->setUInt(2, ActivityId);
				ins_stmt->setInt(3, static_cast<int>(i + 1));
				ins_stmt->setInt(4, group);
				ins_stmt->setUInt(5, nowTime);
				ins_stmt->executeUpdate();
			}
		}
		ins_stmt.reset();
		conn->commit();
		conn->setAutoCommit(true);
	}
	catch (const std::exception& e)
	{
		try { conn->rollback(); conn->setAutoCommit(true); } catch (const std::exception&) {}
		std::cerr << e.what() << " PlayerDB_SaveCarTreasureSelectedGroups Error" << std::endl;
	}
}

bool PlayerDB_SaveCarTreasureSelectedGroupsOnce(UINT Uin, const std::vector<int>& Groups, UINT ActivityId)
{
	if (Uin < 10000 || ActivityId == 0 || Groups.empty() || (Groups.size() % 2) != 0)
		return false;

	DBConnection myDBConnection(activity);
	auto conn = myDBConnection.getConnection();
	if (!conn) return false;
	bool committed = false;
	try
	{
		conn->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> selectStatement(conn->prepareStatement(
			"SELECT Slot FROM CarTreasureSelectedGroup "
			"WHERE Uin=? AND ActivityId=? LIMIT 1 FOR UPDATE;"));
		selectStatement->setUInt(1, Uin);
		selectStatement->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> selected(selectStatement->executeQuery());
		const bool alreadySelected = selected->next();
		selected.reset();
		selectStatement.reset();
		if (alreadySelected)
		{
			conn->rollback();
			conn->setAutoCommit(true);
			return false;
		}

		std::unique_ptr<sql::PreparedStatement> insertStatement(conn->prepareStatement(
			"INSERT INTO CarTreasureSelectedGroup"
			"(Uin,ActivityId,Slot,GroupIndex,UpdateTime) "
			"VALUES(?,?,?,?,UNIX_TIMESTAMP());"));
		for (size_t index = 0; index + 1 < Groups.size(); index += 2)
		{
			insertStatement->clearParameters();
			insertStatement->setUInt(1, Uin);
			insertStatement->setUInt(2, ActivityId);
			insertStatement->setInt(3, Groups[index]);
			insertStatement->setInt(4, Groups[index + 1]);
			if (insertStatement->executeUpdate() != 1)
				throw std::runtime_error("failed to insert summer car treasure selected group");
		}
		insertStatement.reset();
		conn->commit();
		committed = true;
		conn->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& e)
	{
		try
		{
			if (!committed) conn->rollback();
			conn->setAutoCommit(true);
		}
		catch (const std::exception&) {}
		std::cerr << e.what()
			<< " PlayerDB_SaveCarTreasureSelectedGroupsOnce Error" << std::endl;
		return false;
	}
}

bool PlayerDB_LoadSummerCarTreasureState(
	UINT Uin,
	UINT ActivityId,
	SummerCarTreasureStateRow& State)
{
	State = {};
	State.Uin = Uin;
	State.ActivityId = ActivityId;
	if (Uin < 10000 || ActivityId == 0) return false;

	DBConnection myDBConnection(activity);
	auto conn = myDBConnection.getConnection();
	if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(conn->prepareStatement(
			"SELECT LuckValue,CrystalProgress,CrystalGotNum,DiscountUsedNum "
			"FROM SummerCarTreasureState WHERE Uin=? AND ActivityId=? LIMIT 1;"));
		statement->setUInt(1, Uin);
		statement->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		if (result->next())
		{
			State.LuckValue = result->getInt("LuckValue");
			State.CrystalProgress = result->getInt("CrystalProgress");
			State.CrystalGotNum = result->getInt("CrystalGotNum");
			State.DiscountUsedNum = result->getInt("DiscountUsedNum");
		}
		result.reset();
		statement.reset();
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what()
			<< " PlayerDB_LoadSummerCarTreasureState Error" << std::endl;
		return false;
	}
}

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
	INT& RemainingTicket)
{
	ProgressAwards.LuckGranted = false;
	ProgressAwards.CrystalGranted = false;
	State = {};
	State.Uin = Uin;
	State.ActivityId = ActivityId;
	RemainingTicket = 0;
	if (Uin < 10000 || ActivityId == 0 || TicketItemId == 0 ||
		TicketCost <= 0 || Awards.empty())
	{
		return SummerCarTreasureDrawDbResult::DatabaseError;
	}

	DBConnection myDBConnection(player);
	auto conn = myDBConnection.getConnection();
	if (!conn) return SummerCarTreasureDrawDbResult::DatabaseError;
	bool committed = false;
	try
	{
		conn->setAutoCommit(false);
		const std::string summerStateTable =
			ActivityDB_QualifiedTable("SummerCarTreasureState");
		const std::string retComItemTable =
			ActivityDB_QualifiedTable("RetComItemTable");

		std::unique_ptr<sql::PreparedStatement> reduceTicket(conn->prepareStatement(
			"UPDATE Item SET ItemNum=ItemNum-? "
			"WHERE Uin=? AND ItemID=? AND ItemNum>=?;"));
		reduceTicket->setInt(1, TicketCost);
		reduceTicket->setUInt(2, Uin);
		reduceTicket->setUInt(3, TicketItemId);
		reduceTicket->setInt(4, TicketCost);
		const int reducedRows = reduceTicket->executeUpdate();
		reduceTicket.reset();
		if (reducedRows != 1)
		{
			conn->rollback();
			conn->setAutoCommit(true);
			return SummerCarTreasureDrawDbResult::InsufficientTicket;
		}

		std::unique_ptr<sql::PreparedStatement> readTicket(conn->prepareStatement(
			"SELECT ItemNum FROM Item WHERE Uin=? AND ItemID=? LIMIT 1 FOR UPDATE;"));
		readTicket->setUInt(1, Uin);
		readTicket->setUInt(2, TicketItemId);
		std::unique_ptr<sql::ResultSet> ticketResult(readTicket->executeQuery());
		if (!ticketResult->next())
			throw std::runtime_error("summer car treasure ticket row disappeared");
		RemainingTicket = ticketResult->getInt("ItemNum");
		ticketResult.reset();
		readTicket.reset();
		if (RemainingTicket == 0)
		{
			std::unique_ptr<sql::PreparedStatement> deleteTicket(conn->prepareStatement(
				"DELETE FROM Item WHERE Uin=? AND ItemID=? AND ItemNum=0;"));
			deleteTicket->setUInt(1, Uin);
			deleteTicket->setUInt(2, TicketItemId);
			deleteTicket->executeUpdate();
		}

		std::unique_ptr<sql::PreparedStatement> createState(conn->prepareStatement(
			"INSERT IGNORE INTO " + summerStateTable +
			"(Uin,ActivityId,LuckValue,CrystalProgress,CrystalGotNum,"
			"DiscountUsedNum,UpdateTime) VALUES(?,?,0,0,0,0,UNIX_TIMESTAMP());"));
		createState->setUInt(1, Uin);
		createState->setUInt(2, ActivityId);
		createState->executeUpdate();
		createState.reset();

		std::unique_ptr<sql::PreparedStatement> readState(conn->prepareStatement(
			"SELECT LuckValue,CrystalProgress,CrystalGotNum,DiscountUsedNum "
			"FROM " + summerStateTable + " "
			"WHERE Uin=? AND ActivityId=? LIMIT 1 FOR UPDATE;"));
		readState->setUInt(1, Uin);
		readState->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> stateResult(readState->executeQuery());
		if (!stateResult->next())
			throw std::runtime_error("summer car treasure state row missing");
		const INT oldLuck = stateResult->getInt("LuckValue");
		const INT oldCrystal = stateResult->getInt("CrystalProgress");
		State.CrystalGotNum = stateResult->getInt("CrystalGotNum");
		const INT oldDiscount = stateResult->getInt("DiscountUsedNum");
		stateResult.reset();
		readState.reset();

		const long long luckValue = static_cast<long long>(oldLuck) +
			(static_cast<long long>((std::max)(0, LuckAdd)));
		const long long crystalValue = static_cast<long long>(oldCrystal) +
			(static_cast<long long>((std::max)(0, CrystalAdd)));
		const long long discountValue = static_cast<long long>(oldDiscount) +
			(static_cast<long long>((std::max)(0, DiscountUsedAdd)));
		const bool luckReached =
			LuckAdd > 0 && LuckLimit > 0 && luckValue >= LuckLimit &&
			ProgressAwards.LuckAward.ItemId != 0;
		const bool crystalReached =
			CrystalAdd > 0 && CrystalLimit > 0 &&
			State.CrystalGotNum < CrystalTimes &&
			crystalValue >= CrystalLimit &&
			ProgressAwards.CrystalAward.ItemId != 0;
		const bool crystalExhausted =
			CrystalTimes > 0 && State.CrystalGotNum >= CrystalTimes;
		State.LuckValue = luckReached
			? 0
			: static_cast<INT>((std::min)(
				static_cast<long long>((std::max)(0, LuckLimit)), luckValue));
		State.CrystalProgress = (crystalReached || crystalExhausted)
			? 0
			: static_cast<INT>((std::min)(
				static_cast<long long>((std::max)(0, CrystalLimit)), crystalValue));
		if (luckReached)
		{
			ProgressAwards.LuckGranted = true;
			Awards.push_back(ProgressAwards.LuckAward);
		}
		if (crystalReached)
		{
			ProgressAwards.CrystalGranted = true;
			++State.CrystalGotNum;
			Awards.push_back(ProgressAwards.CrystalAward);
		}
		State.DiscountUsedNum = static_cast<INT>((std::min)(
			static_cast<long long>((std::numeric_limits<INT>::max)()),
			discountValue));

		std::unique_ptr<sql::PreparedStatement> updateState(conn->prepareStatement(
			"UPDATE " + summerStateTable + " SET LuckValue=?,CrystalProgress=?,"
			"CrystalGotNum=?,DiscountUsedNum=?,UpdateTime=UNIX_TIMESTAMP() "
			"WHERE Uin=? AND ActivityId=?;"));
		updateState->setInt(1, State.LuckValue);
		updateState->setInt(2, State.CrystalProgress);
		updateState->setInt(3, State.CrystalGotNum);
		updateState->setInt(4, State.DiscountUsedNum);
		updateState->setUInt(5, Uin);
		updateState->setUInt(6, ActivityId);
		if (updateState->executeUpdate() != 1)
			throw std::runtime_error("failed to update summer car treasure state");
		updateState.reset();

		for (SummerCarTreasureDbAwardRow& award : Awards)
		{
			if (award.ItemId == 0 || award.ItemNum == 0)
				throw std::runtime_error("invalid summer car treasure award");
			if (award.DirectToBackpack)
			{
				bool grantSucceeded = false;
				award.FinalItemNum = PlayerDB_AddItem5(
					Uin,
					award.ItemId,
					award.ItemNum,
					award.AvailPeriod,
					false,
					conn,
					&grantSucceeded);
				if (!grantSucceeded)
					throw std::runtime_error("failed to grant summer car treasure award");
				continue;
			}

			std::unique_ptr<sql::PreparedStatement> addTemp(conn->prepareStatement(
				"INSERT INTO " + retComItemTable +
				"(Uin,ItemId,Aviable,OpNum,ExchangePieceNum,CreateTime,UpdateTime) "
				"VALUES(?,?,?,?,?,UNIX_TIMESTAMP(),UNIX_TIMESTAMP()) "
				"ON DUPLICATE KEY UPDATE "
				"OpNum=OpNum+VALUES(OpNum),"
				"ExchangePieceNum=VALUES(ExchangePieceNum),"
				"UpdateTime=UNIX_TIMESTAMP();"));
			addTemp->setUInt(1, Uin);
			addTemp->setUInt(2, award.ItemId);
			addTemp->setInt(3, award.AvailPeriod);
			addTemp->setUInt(4, award.ItemNum);
			addTemp->setUInt(5, award.ExchangePieceNum);
			addTemp->executeUpdate();
		}

		conn->commit();
		committed = true;
		conn->setAutoCommit(true);
		return SummerCarTreasureDrawDbResult::Success;
	}
	catch (const std::exception& e)
	{
		try
		{
			if (!committed) conn->rollback();
			conn->setAutoCommit(true);
		}
		catch (const std::exception&) {}
		std::cerr << e.what()
			<< " PlayerDB_ApplySummerCarTreasureDraw Error" << std::endl;
		return SummerCarTreasureDrawDbResult::DatabaseError;
	}
}

bool PlayerDB_ApplySummerCarTreasureTempOperation(
	UINT Uin,
	INT Operation,
	UINT PieceItemId,
	std::vector<SummerCarTreasureDbAwardRow>& Items,
	INT& AddedPieceNum,
	INT& FinalPieceNum)
{
	AddedPieceNum = 0;
	FinalPieceNum = 0;
	if (Uin < 10000 || (Operation != 1 && Operation != 2) || Items.empty() ||
		(Operation == 2 && PieceItemId == 0))
	{
		return false;
	}

	DBConnection myDBConnection(player);
	auto conn = myDBConnection.getConnection();
	if (!conn) return false;
	bool committed = false;
	try
	{
		conn->setAutoCommit(false);
		const std::string retComItemTable =
			ActivityDB_QualifiedTable("RetComItemTable");
		long long totalPiece = 0;
		for (SummerCarTreasureDbAwardRow& item : Items)
		{
			if (item.ItemId == 0 || item.ItemNum == 0)
				throw std::runtime_error("invalid summer car treasure temp item");

			std::unique_ptr<sql::PreparedStatement> readTemp(conn->prepareStatement(
				"SELECT OpNum,ExchangePieceNum FROM " + retComItemTable + " "
				"WHERE Uin=? AND ItemId=? AND Aviable=? LIMIT 1 FOR UPDATE;"));
			readTemp->setUInt(1, Uin);
			readTemp->setUInt(2, item.ItemId);
			readTemp->setInt(3, item.AvailPeriod);
			std::unique_ptr<sql::ResultSet> tempResult(readTemp->executeQuery());
			if (!tempResult->next())
				throw std::runtime_error("summer car treasure temp item not found");
			const UINT availableNum = tempResult->getUInt("OpNum");
			const UINT storedExchangePieceNum =
				tempResult->getUInt("ExchangePieceNum");
			if (item.ExchangePieceNum == 0)
				item.ExchangePieceNum = storedExchangePieceNum;
			tempResult.reset();
			readTemp.reset();
			if (availableNum < item.ItemNum)
				throw std::runtime_error("summer car treasure temp item count is insufficient");
			if (Operation == 2 && item.ExchangePieceNum == 0)
				throw std::runtime_error("summer car treasure temp item cannot be decomposed");

			std::unique_ptr<sql::PreparedStatement> consumeTemp(conn->prepareStatement(
				"UPDATE " + retComItemTable + " SET OpNum=OpNum-?,UpdateTime=UNIX_TIMESTAMP() "
				"WHERE Uin=? AND ItemId=? AND Aviable=? AND OpNum>=?;"));
			consumeTemp->setUInt(1, item.ItemNum);
			consumeTemp->setUInt(2, Uin);
			consumeTemp->setUInt(3, item.ItemId);
			consumeTemp->setInt(4, item.AvailPeriod);
			consumeTemp->setUInt(5, item.ItemNum);
			if (consumeTemp->executeUpdate() != 1)
				throw std::runtime_error("failed to consume summer car treasure temp item");
			consumeTemp.reset();

			std::unique_ptr<sql::PreparedStatement> removeEmpty(conn->prepareStatement(
				"DELETE FROM " + retComItemTable + " "
				"WHERE Uin=? AND ItemId=? AND Aviable=? AND OpNum=0;"));
			removeEmpty->setUInt(1, Uin);
			removeEmpty->setUInt(2, item.ItemId);
			removeEmpty->setInt(3, item.AvailPeriod);
			removeEmpty->executeUpdate();
			removeEmpty.reset();

			if (Operation == 1)
			{
				const UINT itemType = GetItemType(item.ItemId);
				const bool stackable =
					itemType == EAIT_PROP || itemType == EAIT_STONE ||
					itemType == EAIT_CLOTHES;
				const UINT grantNum = stackable ? item.ItemNum : 1;
				const INT grantPeriod =
					!stackable && item.AvailPeriod > 0
						? item.AvailPeriod * static_cast<INT>(item.ItemNum)
						: item.AvailPeriod;
				bool grantSucceeded = false;
				item.FinalItemNum = PlayerDB_AddItem5(
					Uin,
					item.ItemId,
					grantNum,
					grantPeriod,
					false,
					conn,
					&grantSucceeded);
				if (!grantSucceeded)
					throw std::runtime_error("failed to receive summer car treasure temp item");
				item.AvailPeriod = grantPeriod;
			}
			else
			{
				totalPiece += static_cast<long long>(item.ExchangePieceNum) *
					static_cast<long long>(item.ItemNum);
				if (totalPiece > (std::numeric_limits<INT>::max)())
					throw std::runtime_error("summer car treasure piece count overflow");
			}
		}

		if (Operation == 2 && totalPiece > 0)
		{
			bool grantSucceeded = false;
			AddedPieceNum = static_cast<INT>(totalPiece);
			FinalPieceNum = PlayerDB_AddItem5(
				Uin,
				PieceItemId,
				static_cast<UINT>(AddedPieceNum),
				0,
				false,
				conn,
				&grantSucceeded);
			if (!grantSucceeded)
				throw std::runtime_error("failed to grant summer car treasure pieces");
		}

		conn->commit();
		committed = true;
		conn->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& e)
	{
		try
		{
			if (!committed) conn->rollback();
			conn->setAutoCommit(true);
		}
		catch (const std::exception&) {}
		std::cerr << e.what()
			<< " PlayerDB_ApplySummerCarTreasureTempOperation Error" << std::endl;
		return false;
	}
}

bool PlayerDB_FMH_LoadSelectedGroups(UINT Uin, UINT ActivityId, std::vector<int>& Groups)
{
	Groups.clear();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Slot,GroupIndex FROM FinalMagicHouseSelectedGroup WHERE Uin=? AND ActivityId=? ORDER BY Slot ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			Groups.push_back(res->getInt("Slot"));
			Groups.push_back(res->getInt("GroupIndex"));
		}
		res.reset();
		return true;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FMH_LoadSelectedGroups Error" << std::endl;
	}
	return false;
}

void PlayerDB_FMH_SaveSelectedGroups(UINT Uin, UINT ActivityId, const std::vector<int>& Groups)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		conn->setAutoCommit(false);
		std::unique_ptr<sql::PreparedStatement> del_stmt(conn->prepareStatement("DELETE FROM FinalMagicHouseSelectedGroup WHERE Uin=? AND ActivityId=?;"));
		del_stmt->clearParameters();
		del_stmt->setUInt(1, Uin);
		del_stmt->setUInt(2, ActivityId);
		del_stmt->executeUpdate();
		del_stmt.reset();

		std::unique_ptr<sql::PreparedStatement> ins_stmt(conn->prepareStatement("INSERT INTO FinalMagicHouseSelectedGroup(Uin,ActivityId,Slot,GroupIndex,UpdateTime) VALUES(?,?,?,?,?);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		for (size_t i = 0; i + 1 < Groups.size(); i += 2)
		{
			int slot = Groups[i] > 0 ? Groups[i] : static_cast<int>((i / 2) + 1);
			int group = Groups[i + 1];
			if (group <= 0) continue;
			ins_stmt->clearParameters();
			ins_stmt->setUInt(1, Uin);
			ins_stmt->setUInt(2, ActivityId);
			ins_stmt->setInt(3, slot);
			ins_stmt->setInt(4, group);
			ins_stmt->setUInt(5, nowTime);
			ins_stmt->executeUpdate();
		}
		ins_stmt.reset();
		conn->commit();
		conn->setAutoCommit(true);
	}
	catch (const std::exception& e)
	{
		try { conn->rollback(); conn->setAutoCommit(true); } catch (const std::exception&) {}
		std::cerr << e.what() << " PlayerDB_FMH_SaveSelectedGroups Error" << std::endl;
	}
}

bool PlayerDB_FMH_LoadState(UINT Uin, UINT ActivityId, FinalMagicHouseStateRow& State)
{
	State.Uin = Uin;
	State.ActivityId = ActivityId;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT DrawCount,CurrentRound,GotItemData FROM FinalMagicHouseState WHERE Uin=? AND ActivityId=? LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.DrawCount = res->getInt("DrawCount");
			State.CurrentRound = res->getInt("CurrentRound");
			State.GotItemData = res->getString("GotItemData");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FMH_LoadState Error" << std::endl;
	}
	return false;
}

void PlayerDB_FMH_SaveState(UINT Uin, UINT ActivityId, const FinalMagicHouseStateRow& State)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO FinalMagicHouseState(Uin,ActivityId,DrawCount,CurrentRound,GotItemData,UpdateTime) "
			"VALUES(?,?,?,?,?,?) ON DUPLICATE KEY UPDATE DrawCount=VALUES(DrawCount),CurrentRound=VALUES(CurrentRound),"
			"GotItemData=VALUES(GotItemData),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, State.DrawCount);
		prep_stmt->setInt(4, State.CurrentRound > 0 ? State.CurrentRound : 1);
		prep_stmt->setString(5, State.GotItemData);
		prep_stmt->setUInt(6, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FMH_SaveState Error" << std::endl;
	}
}


bool PlayerDB_FBLoadState(UINT Uin, UINT ActivityId, FourBeautiesStateRow& State)
{
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT CurDraw,DrawItemCnt,NextBuff,BestAwardFlag FROM FourBeautiesState WHERE Uin=? AND ActivityId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.CurDraw = res->getUInt("CurDraw");
			State.DrawItemCnt = res->getUInt("DrawItemCnt");
			State.NextBuff = res->getUInt("NextBuff");
			State.BestAwardFlag = res->getUInt("BestAwardFlag");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBLoadState Error" << std::endl;
	}
	return false;
}

void PlayerDB_FBSaveState(UINT Uin, UINT ActivityId, const FourBeautiesStateRow& State)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO FourBeautiesState(Uin,ActivityId,CurDraw,DrawItemCnt,NextBuff,BestAwardFlag,UpdateTime) VALUES(?,?,?,?,?,?,?) "
			"ON DUPLICATE KEY UPDATE CurDraw=VALUES(CurDraw),DrawItemCnt=VALUES(DrawItemCnt),NextBuff=VALUES(NextBuff),BestAwardFlag=VALUES(BestAwardFlag),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setUInt(3, State.CurDraw);
		prep_stmt->setUInt(4, State.DrawItemCnt);
		prep_stmt->setUInt(5, State.NextBuff);
		prep_stmt->setUInt(6, State.BestAwardFlag);
		prep_stmt->setUInt(7, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBSaveState Error" << std::endl;
	}
}

void PlayerDB_FBLoadTempItems(UINT Uin, UINT ActivityId, std::vector<FourBeautiesTempItemRow>& Rows)
{
	Rows.clear();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Id,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status FROM FourBeautiesTempItem WHERE Uin=? AND ActivityId=? AND Status=0 ORDER BY Id ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			FourBeautiesTempItemRow row;
			row.Id = res->getUInt("Id");
			row.ItemId = res->getUInt("ItemID");
			row.ItemNum = res->getUInt("ItemNum");
			row.ItemType = res->getUInt("ItemType");
			row.AvailPeriod = res->getInt("AvailPeriod");
			row.FenjieNum = res->getUInt("FenjieNum");
			row.Status = res->getUInt("Status");
			Rows.push_back(row);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBLoadTempItems Error" << std::endl;
	}
}

void PlayerDB_FBAddTempItem(UINT Uin, UINT ActivityId, const FourBeautiesTempItemRow& Row)
{
	if (Uin < 10000 || Row.ItemId == 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO FourBeautiesTempItem(Uin,ActivityId,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status,CreateTime,UpdateTime) VALUES(?,?,?,?,?,?,?,?,?,?);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setUInt(3, Row.ItemId);
		prep_stmt->setUInt(4, Row.ItemNum > 0 ? Row.ItemNum : 1);
		prep_stmt->setUInt(5, Row.ItemType);
		prep_stmt->setInt(6, Row.AvailPeriod);
		prep_stmt->setUInt(7, Row.FenjieNum);
		prep_stmt->setUInt(8, Row.Status);
		prep_stmt->setUInt(9, nowTime);
		prep_stmt->setUInt(10, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBAddTempItem Error" << std::endl;
	}
}

bool PlayerDB_FBRemoveTempItem(UINT Uin, UINT ActivityId, const FourBeautiesTempItemRow& Row, UINT NewStatus)
{
	if (Uin < 10000 || Row.ItemId == 0) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"UPDATE FourBeautiesTempItem SET Status=?,UpdateTime=? WHERE Uin=? AND ActivityId=? AND ItemID=? AND ItemNum=? AND ItemType=? AND AvailPeriod=? AND FenjieNum=? AND Status=0 ORDER BY Id ASC LIMIT 1;"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, NewStatus);
		prep_stmt->setUInt(2, nowTime);
		prep_stmt->setUInt(3, Uin);
		prep_stmt->setUInt(4, ActivityId);
		prep_stmt->setUInt(5, Row.ItemId);
		prep_stmt->setUInt(6, Row.ItemNum > 0 ? Row.ItemNum : 1);
		prep_stmt->setUInt(7, Row.ItemType);
		prep_stmt->setInt(8, Row.AvailPeriod);
		prep_stmt->setUInt(9, Row.FenjieNum);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBRemoveTempItem Error" << std::endl;
	}
	return false;
}

void PlayerDB_FBLoadGotItems(UINT Uin, UINT ActivityId, std::vector<UINT>& ItemIds)
{
	ItemIds.clear();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemID FROM FourBeautiesGotItem WHERE Uin=? AND ActivityId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next()) ItemIds.push_back(res->getUInt("ItemID"));
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBLoadGotItems Error" << std::endl;
	}
}

void PlayerDB_FBMarkGotItem(UINT Uin, UINT ActivityId, UINT ItemId)
{
	if (Uin < 10000 || ItemId == 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT IGNORE INTO FourBeautiesGotItem(Uin,ActivityId,ItemID,GotTime) VALUES(?,?,?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setUInt(3, ItemId);
		prep_stmt->setUInt(4, static_cast<UINT>(time(nullptr)));
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBMarkGotItem Error" << std::endl;
	}
}

void PlayerDB_FBLoadBuffs(UINT Uin, UINT ActivityId, std::vector<FourBeautiesBuffRow>& Rows)
{
	Rows.clear();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT BuffIdx,`Count` AS CountValue,UsedCount FROM FourBeautiesBuff WHERE Uin=? AND ActivityId=? AND `Count`>UsedCount ORDER BY BuffIdx ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			FourBeautiesBuffRow row;
			row.BuffIdx = res->getUInt("BuffIdx");
			row.Count = res->getUInt("CountValue");
			row.UsedCount = res->getUInt("UsedCount");
			Rows.push_back(row);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBLoadBuffs Error" << std::endl;
	}
}

void PlayerDB_FBAddBuff(UINT Uin, UINT ActivityId, UINT BuffIdx, UINT Count)
{
	if (Uin < 10000 || BuffIdx == 0 || Count == 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO FourBeautiesBuff(Uin,ActivityId,BuffIdx,`Count`,UsedCount,CreateTime,UpdateTime) VALUES(?,?,?,?,0,?,?) "
			"ON DUPLICATE KEY UPDATE `Count`=`Count`+VALUES(`Count`),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setUInt(3, BuffIdx);
		prep_stmt->setUInt(4, Count);
		prep_stmt->setUInt(5, nowTime);
		prep_stmt->setUInt(6, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBAddBuff Error" << std::endl;
	}
}

bool PlayerDB_FBConsumeBuff(UINT Uin, UINT ActivityId, UINT BuffIdx, UINT Count)
{
	if (Uin < 10000 || BuffIdx == 0 || Count == 0) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"UPDATE FourBeautiesBuff SET UsedCount=UsedCount+?,UpdateTime=? WHERE Uin=? AND ActivityId=? AND BuffIdx=? AND `Count`>=UsedCount+?;"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Count);
		prep_stmt->setUInt(2, nowTime);
		prep_stmt->setUInt(3, Uin);
		prep_stmt->setUInt(4, ActivityId);
		prep_stmt->setUInt(5, BuffIdx);
		prep_stmt->setUInt(6, Count);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_FBConsumeBuff Error" << std::endl;
	}
	return false;
}


bool PlayerDB_GWBLoadActivity(UINT ActivityId, GodWarBoxActivityRow& Row)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT ActivityId,SeasonId,ThemeIdx,CallonItemId,NumB,IOpenDate,IOpenTime,ICloseDate,ICloseTime,"
			"RoundTriggerA,TriggerNumN,DefaultCostItemNum,CurrentMulAwardIndex,GodWarBoxReceiveStatus,"
			"TreasureAwardHasGotStatus,GodWarBoxTriggerNum,TotalTreasureAwardNum,CurrRoundNum "
			"FROM GodWarBoxActivity WHERE ActivityId=? LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			Row.ActivityId = res->getUInt("ActivityId");
			Row.SeasonId = res->getInt("SeasonId");
			Row.ThemeIdx = res->getInt("ThemeIdx");
			Row.CallonItemId = res->getUInt("CallonItemId");
			Row.NumB = res->getInt("NumB");
			Row.IOpenDate = res->getInt("IOpenDate");
			Row.IOpenTime = res->getInt("IOpenTime");
			Row.ICloseDate = res->getInt("ICloseDate");
			Row.ICloseTime = res->getInt("ICloseTime");
			Row.RoundTriggerA = res->getInt("RoundTriggerA");
			Row.TriggerNumN = res->getInt("TriggerNumN");
			Row.DefaultCostItemNum = res->getInt("DefaultCostItemNum");
			Row.CurrentMulAwardIndex = res->getInt("CurrentMulAwardIndex");
			Row.GodWarBoxReceiveStatus = res->getInt("GodWarBoxReceiveStatus");
			Row.TreasureAwardHasGotStatus = res->getInt("TreasureAwardHasGotStatus");
			Row.GodWarBoxTriggerNum = res->getInt("GodWarBoxTriggerNum");
			Row.TotalTreasureAwardNum = res->getInt("TotalTreasureAwardNum");
			Row.CurrRoundNum = res->getInt("CurrRoundNum");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GWBLoadActivity Error" << std::endl;
	}
	return false;
}

void PlayerDB_GWBLoadAwards(UINT ActivityId, std::vector<GodWarBoxAwardRow>& Rows)
{
	Rows.clear();
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT ItemIndex,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,ConpousNum,SuperMoneyNum,AlreadyGotDefault "
			"FROM GodWarBoxAwardCfg WHERE ActivityId=? ORDER BY ItemIndex ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			GodWarBoxAwardRow row;
			row.ItemIndex = res->getInt("ItemIndex");
			row.ItemID = res->getUInt("ItemID");
			row.ItemNum = res->getInt("ItemNum");
			row.ItemType = res->getInt("ItemType");
			row.AvailPeriod = res->getInt("AvailPeriod");
			row.FenjieNum = res->getInt("FenjieNum");
			row.ConpousNum = res->getInt("ConpousNum");
			row.SuperMoneyNum = res->getInt("SuperMoneyNum");
			row.AlreadyGotDefault = res->getInt("AlreadyGotDefault");
			Rows.push_back(row);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GWBLoadAwards Error" << std::endl;
	}
}

bool PlayerDB_GWBLoadState(UINT Uin, UINT ActivityId, GodWarBoxStateRow& State)
{
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT CurrentMulAwardIndex,GodWarBoxReceiveStatus,TreasureAwardHasGotStatus,GodWarBoxTriggerNum,TreasureAwardNum,DrawCount,CurrRoundNum,CurrCostItemNum "
			"FROM GodWarBoxState WHERE Uin=? AND ActivityId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.Uin = Uin;
			State.ActivityId = ActivityId;
			State.CurrentMulAwardIndex = res->getInt("CurrentMulAwardIndex");
			State.GodWarBoxReceiveStatus = res->getInt("GodWarBoxReceiveStatus");
			State.TreasureAwardHasGotStatus = res->getInt("TreasureAwardHasGotStatus");
			State.GodWarBoxTriggerNum = res->getInt("GodWarBoxTriggerNum");
			State.TreasureAwardNum = res->getInt("TreasureAwardNum");
			State.DrawCount = res->getInt("DrawCount");
			State.CurrRoundNum = res->getInt("CurrRoundNum");
			State.CurrCostItemNum = res->getInt("CurrCostItemNum");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GWBLoadState Error" << std::endl;
	}
	return false;
}

void PlayerDB_GWBSaveState(UINT Uin, UINT ActivityId, const GodWarBoxStateRow& State)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO GodWarBoxState(Uin,ActivityId,CurrentMulAwardIndex,GodWarBoxReceiveStatus,TreasureAwardHasGotStatus,GodWarBoxTriggerNum,TreasureAwardNum,DrawCount,CurrRoundNum,CurrCostItemNum,UpdateTime) "
			"VALUES(?,?,?,?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE CurrentMulAwardIndex=VALUES(CurrentMulAwardIndex),GodWarBoxReceiveStatus=VALUES(GodWarBoxReceiveStatus),"
			"TreasureAwardHasGotStatus=VALUES(TreasureAwardHasGotStatus),GodWarBoxTriggerNum=VALUES(GodWarBoxTriggerNum),TreasureAwardNum=VALUES(TreasureAwardNum),"
			"DrawCount=VALUES(DrawCount),CurrRoundNum=VALUES(CurrRoundNum),CurrCostItemNum=VALUES(CurrCostItemNum),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, State.CurrentMulAwardIndex);
		prep_stmt->setInt(4, State.GodWarBoxReceiveStatus);
		prep_stmt->setInt(5, State.TreasureAwardHasGotStatus);
		prep_stmt->setInt(6, State.GodWarBoxTriggerNum);
		prep_stmt->setInt(7, State.TreasureAwardNum);
		prep_stmt->setInt(8, State.DrawCount);
		prep_stmt->setInt(9, State.CurrRoundNum);
		prep_stmt->setInt(10, State.CurrCostItemNum);
		prep_stmt->setUInt(11, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GWBSaveState Error" << std::endl;
	}
}


bool PlayerDB_STLoadState(UINT Uin, UINT ActivityId, SupremeTreasureStateRow& State)
{
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT DrawNum,LuckScore,ScoreDrawDayCount,ScoreDrawWeekCount,AccuClaimMask FROM SupremeTreasureState WHERE Uin=? AND ActivityId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.Uin = Uin; State.ActivityId = ActivityId;
			State.DrawNum = res->getInt("DrawNum");
			State.LuckScore = res->getInt("LuckScore");
			State.ScoreDrawDayCount = res->getInt("ScoreDrawDayCount");
			State.ScoreDrawWeekCount = res->getInt("ScoreDrawWeekCount");
			State.AccuClaimMask = res->getUInt("AccuClaimMask");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_STLoadState Error" << std::endl;
	}
	return false;
}

void PlayerDB_STSaveState(UINT Uin, UINT ActivityId, const SupremeTreasureStateRow& State)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO SupremeTreasureState(Uin,ActivityId,DrawNum,LuckScore,ScoreDrawDayCount,ScoreDrawWeekCount,AccuClaimMask,UpdateTime) "
			"VALUES(?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE DrawNum=VALUES(DrawNum),LuckScore=VALUES(LuckScore),"
			"ScoreDrawDayCount=VALUES(ScoreDrawDayCount),ScoreDrawWeekCount=VALUES(ScoreDrawWeekCount),AccuClaimMask=VALUES(AccuClaimMask),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, State.DrawNum);
		prep_stmt->setInt(4, State.LuckScore);
		prep_stmt->setInt(5, State.ScoreDrawDayCount);
		prep_stmt->setInt(6, State.ScoreDrawWeekCount);
		prep_stmt->setUInt(7, State.AccuClaimMask);
		prep_stmt->setUInt(8, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_STSaveState Error" << std::endl;
	}
}

bool PlayerDB_SCLoadState(UINT Uin, UINT ActivityId, SecretCompassStateRow& State)
{
	State = SecretCompassStateRow();
	State.Uin = Uin;
	State.ActivityId = ActivityId;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT PieceScore,DrawCount FROM SecretCompassState WHERE Uin=? AND ActivityId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.PieceScore = res->getInt("PieceScore");
			State.DrawCount = res->getInt("DrawCount");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_SCLoadState Error" << std::endl;
	}
	return false;
}

int PlayerDB_SCAddPieces(UINT Uin, UINT ActivityId, INT AddPieces, INT AddDrawCount)
{
	if (Uin < 10000 || AddPieces <= 0) return 0;
	if (AddDrawCount < 0) AddDrawCount = 0;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO SecretCompassState(Uin,ActivityId,PieceScore,DrawCount,UpdateTime) "
			"VALUES(?,?,?,?,?) ON DUPLICATE KEY UPDATE PieceScore=PieceScore+VALUES(PieceScore),"
			"DrawCount=DrawCount+VALUES(DrawCount),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, AddPieces);
		prep_stmt->setInt(4, AddDrawCount);
		prep_stmt->setUInt(5, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
		std::unique_ptr<sql::PreparedStatement> sel_stmt(conn->prepareStatement(
			"SELECT PieceScore FROM SecretCompassState WHERE Uin=? AND ActivityId=?;"));
		sel_stmt->clearParameters();
		sel_stmt->setUInt(1, Uin);
		sel_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(sel_stmt->executeQuery());
		sel_stmt.reset();
		if (res->next())
		{
			const int pieceScore = res->getInt("PieceScore");
			res.reset();
			return pieceScore;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_SCAddPieces Error" << std::endl;
	}
	return 0;
}

bool PlayerDB_SCTryConsumePieces(UINT Uin, UINT ActivityId, INT NeedPieces, INT* RemainPieces)
{
	if (RemainPieces) *RemainPieces = 0;
	if (Uin < 10000 || NeedPieces <= 0) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"UPDATE SecretCompassState SET PieceScore=PieceScore-?,UpdateTime=? "
			"WHERE Uin=? AND ActivityId=? AND PieceScore>=?;"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, NeedPieces);
		prep_stmt->setUInt(2, nowTime);
		prep_stmt->setUInt(3, Uin);
		prep_stmt->setUInt(4, ActivityId);
		prep_stmt->setInt(5, NeedPieces);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		std::unique_ptr<sql::PreparedStatement> sel_stmt(conn->prepareStatement(
			"SELECT PieceScore FROM SecretCompassState WHERE Uin=? AND ActivityId=?;"));
		sel_stmt->clearParameters();
		sel_stmt->setUInt(1, Uin);
		sel_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(sel_stmt->executeQuery());
		sel_stmt.reset();
		if (res->next() && RemainPieces) *RemainPieces = res->getInt("PieceScore");
		res.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_SCTryConsumePieces Error" << std::endl;
	}
	return false;
}


bool PlayerDB_SPLoadState(UINT Uin, UINT ActivityId, SpeedPassStateRow& State)
{
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT CurLevel,CurExp,PremiumUnlocked,CollectionUnlocked,"
			"WeekTaskCurScore,LastWeeklyReset "
			"FROM SpeedPassState WHERE Uin=? AND ActivityId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.Uin = Uin; State.ActivityId = ActivityId;
			State.CurLevel = res->getInt("CurLevel");
			State.CurExp = res->getInt("CurExp");
			State.PremiumUnlocked = res->getInt("PremiumUnlocked");
			State.CollectionUnlocked = res->getInt("CollectionUnlocked");
			State.WeekTaskCurScore = res->getInt("WeekTaskCurScore");
			State.LastWeeklyReset = res->getInt("LastWeeklyReset");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e) { std::cerr << e.what() << " PlayerDB_SPLoadState Error" << std::endl; }
	return false;
}

bool PlayerDB_SPSaveState(UINT Uin, UINT ActivityId, const SpeedPassStateRow& State)
{
	if (Uin < 10000) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO SpeedPassState("
			"Uin,ActivityId,CurLevel,CurExp,PremiumUnlocked,CollectionUnlocked,"
			"WeekTaskCurScore,LastWeeklyReset,UpdateTime) "
			"VALUES(?,?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE "
			"CurLevel=VALUES(CurLevel),CurExp=VALUES(CurExp),"
			"PremiumUnlocked=VALUES(PremiumUnlocked),"
			"CollectionUnlocked=VALUES(CollectionUnlocked),"
			"WeekTaskCurScore=VALUES(WeekTaskCurScore),"
			"LastWeeklyReset=VALUES(LastWeeklyReset),"
			"UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, State.CurLevel);
		prep_stmt->setInt(4, State.CurExp);
		prep_stmt->setInt(5, State.PremiumUnlocked);
		prep_stmt->setInt(6, State.CollectionUnlocked);
		prep_stmt->setInt(7, State.WeekTaskCurScore);
		prep_stmt->setInt(8, State.LastWeeklyReset);
		prep_stmt->setUInt(9, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
		return true;
	}
	catch (const std::exception& e) { std::cerr << e.what() << " PlayerDB_SPSaveState Error" << std::endl; }
	return false;
}

bool PlayerDB_SPCommitStateAndTasks(
	UINT Uin,
	UINT ActivityId,
	const SpeedPassStateRow& State,
	const std::vector<SpeedPassTaskStateRow>& TaskStates)
{
	if (Uin < 10000 || ActivityId == 0 || TaskStates.empty())
		return false;
	for (const auto& taskState : TaskStates)
	{
		if (taskState.TaskId <= 0 || taskState.PeriodKey.empty())
			return false;
	}

	DBConnection database(activity);
	auto* connection = database.getConnection();
	if (!connection)
		return false;

	try
	{
		connection->setAutoCommit(false);
		const UINT nowTime = static_cast<UINT>(time(nullptr));

		const std::string saveStateSql =
			"INSERT INTO " + ActivityDB_QualifiedTable("SpeedPassState") + "("
			"Uin,ActivityId,CurLevel,CurExp,PremiumUnlocked,CollectionUnlocked,"
			"WeekTaskCurScore,LastWeeklyReset,UpdateTime) "
			"VALUES(?,?,?,?,?,?,?,?,?) "
			"ON DUPLICATE KEY UPDATE "
			"CurLevel=VALUES(CurLevel),CurExp=VALUES(CurExp),"
			"PremiumUnlocked=VALUES(PremiumUnlocked),"
			"CollectionUnlocked=VALUES(CollectionUnlocked),"
			"WeekTaskCurScore=VALUES(WeekTaskCurScore),"
			"LastWeeklyReset=VALUES(LastWeeklyReset),"
			"UpdateTime=VALUES(UpdateTime);";
		std::unique_ptr<sql::PreparedStatement> saveState(
			connection->prepareStatement(saveStateSql));
		saveState->setUInt(1, Uin);
		saveState->setUInt(2, ActivityId);
		saveState->setInt(3, State.CurLevel);
		saveState->setInt(4, State.CurExp);
		saveState->setInt(5, State.PremiumUnlocked);
		saveState->setInt(6, State.CollectionUnlocked);
		saveState->setInt(7, State.WeekTaskCurScore);
		saveState->setInt(8, State.LastWeeklyReset);
		saveState->setUInt(9, nowTime);
		saveState->executeUpdate();

		std::unique_ptr<sql::PreparedStatement> saveTask(
			connection->prepareStatement(
				"INSERT INTO SpeedPassTaskState("
				"Uin,ActivityId,TaskId,PeriodKey,Progress,AwardCount,UpdateTime) "
				"VALUES(?,?,?,?,?,?,?) "
				"ON DUPLICATE KEY UPDATE "
				"Progress=VALUES(Progress),AwardCount=VALUES(AwardCount),"
				"UpdateTime=VALUES(UpdateTime);"));
		for (const auto& taskState : TaskStates)
		{
			saveTask->clearParameters();
			saveTask->setUInt(1, Uin);
			saveTask->setUInt(2, ActivityId);
			saveTask->setInt(3, taskState.TaskId);
			saveTask->setString(4, taskState.PeriodKey);
			saveTask->setInt(5, taskState.Progress);
			saveTask->setInt(6, taskState.AwardCount);
			saveTask->setUInt(7, nowTime);
			saveTask->executeUpdate();
		}

		connection->commit();
		connection->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& exception)
	{
		try
		{
			connection->rollback();
			connection->setAutoCommit(true);
		}
		catch (const std::exception&)
		{
		}
		std::cerr << exception.what()
			<< " PlayerDB_SPCommitStateAndTasks Error" << std::endl;
		return false;
	}
}

SpeedPassPurchaseDbResult PlayerDB_SPTryPurchase(
	UINT Uin,
	UINT ActivityId,
	UINT Cost,
	const SpeedPassStateRow& State,
	UINT& Remaining)
{
	Remaining = 0;
	if (Uin < 10000 || ActivityId == 0 || Cost == 0)
		return SpeedPassPurchaseDbResult::DatabaseError;
	if (!PlayerDB_IsSpeedCoinReady())
		return SpeedPassPurchaseDbResult::CurrencyUnavailable;

	DBConnection database(player);
	auto* connection = database.getConnection();
	if (!connection)
		return SpeedPassPurchaseDbResult::DatabaseError;

	try
	{
		connection->setAutoCommit(false);

		std::unique_ptr<sql::PreparedStatement> spend(
			connection->prepareStatement(
				"UPDATE BaseInfo SET SpeedCoin=SpeedCoin-? "
				"WHERE Uin=? AND SpeedCoin>=?;"));
		spend->setUInt(1, Cost);
		spend->setUInt(2, Uin);
		spend->setUInt(3, Cost);
		if (spend->executeUpdate() != 1)
		{
			bool playerFound = false;
			std::unique_ptr<sql::PreparedStatement> balance(
				connection->prepareStatement(
					"SELECT SpeedCoin FROM BaseInfo WHERE Uin=? FOR UPDATE;"));
			balance->setUInt(1, Uin);
			std::unique_ptr<sql::ResultSet> result(balance->executeQuery());
			if (result->next())
			{
				playerFound = true;
				Remaining = result->getUInt("SpeedCoin");
			}
			connection->rollback();
			connection->setAutoCommit(true);
			return playerFound && Remaining < Cost
				? SpeedPassPurchaseDbResult::InsufficientFunds
				: SpeedPassPurchaseDbResult::DatabaseError;
		}

		{
			std::unique_ptr<sql::PreparedStatement> balance(
				connection->prepareStatement(
					"SELECT SpeedCoin FROM BaseInfo WHERE Uin=? FOR UPDATE;"));
			balance->setUInt(1, Uin);
			std::unique_ptr<sql::ResultSet> result(balance->executeQuery());
			if (!result->next())
				throw std::runtime_error(
					"BaseInfo row disappeared during SpeedPass purchase");
			Remaining = result->getUInt("SpeedCoin");
		}

		const std::string saveStateSql =
			"INSERT INTO " + ActivityDB_QualifiedTable("SpeedPassState") + "("
			"Uin,ActivityId,CurLevel,CurExp,PremiumUnlocked,CollectionUnlocked,"
			"WeekTaskCurScore,LastWeeklyReset,UpdateTime) "
			"VALUES(?,?,?,?,?,?,?,?,?) "
			"ON DUPLICATE KEY UPDATE "
			"CurLevel=VALUES(CurLevel),CurExp=VALUES(CurExp),"
			"PremiumUnlocked=VALUES(PremiumUnlocked),"
			"CollectionUnlocked=VALUES(CollectionUnlocked),"
			"WeekTaskCurScore=VALUES(WeekTaskCurScore),"
			"LastWeeklyReset=VALUES(LastWeeklyReset),"
			"UpdateTime=VALUES(UpdateTime);";
		std::unique_ptr<sql::PreparedStatement> saveState(
			connection->prepareStatement(saveStateSql));
		saveState->setUInt(1, Uin);
		saveState->setUInt(2, ActivityId);
		saveState->setInt(3, State.CurLevel);
		saveState->setInt(4, State.CurExp);
		saveState->setInt(5, State.PremiumUnlocked);
		saveState->setInt(6, State.CollectionUnlocked);
		saveState->setInt(7, State.WeekTaskCurScore);
		saveState->setInt(8, State.LastWeeklyReset);
		saveState->setUInt(9, static_cast<UINT>(time(nullptr)));
		saveState->executeUpdate();

		connection->commit();
		connection->setAutoCommit(true);
		return SpeedPassPurchaseDbResult::Success;
	}
	catch (const std::exception& exception)
	{
		try
		{
			connection->rollback();
			connection->setAutoCommit(true);
		}
		catch (const std::exception&)
		{
		}
		std::cerr << exception.what()
			<< " PlayerDB_SPTryPurchase Error" << std::endl;
		return SpeedPassPurchaseDbResult::DatabaseError;
	}
}

bool PlayerDB_SPIsClaimed(UINT Uin, UINT ActivityId, const std::string& Tier, INT Level)
{
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT 1 FROM SpeedPassClaim WHERE Uin=? AND ActivityId=? AND Tier=? AND Level=? LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setString(3, Tier);
		prep_stmt->setInt(4, Level);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		bool hit = res->next();
		res.reset();
		return hit;
	}
	catch (const std::exception& e) { std::cerr << e.what() << " PlayerDB_SPIsClaimed Error" << std::endl; }
	return false;
}

void PlayerDB_SPMarkClaimed(UINT Uin, UINT ActivityId, const std::string& Tier, INT Level)
{
	if (Uin < 10000 || Level <= 0 || Tier.empty()) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO SpeedPassClaim(Uin,ActivityId,Tier,Level,ClaimTime) VALUES(?,?,?,?,?) ON DUPLICATE KEY UPDATE ClaimTime=VALUES(ClaimTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setString(3, Tier);
		prep_stmt->setInt(4, Level);
		prep_stmt->setUInt(5, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e) { std::cerr << e.what() << " PlayerDB_SPMarkClaimed Error" << std::endl; }
}

bool PlayerDB_SPLoadTaskState(UINT Uin, UINT ActivityId, INT TaskId, const std::string& PeriodKey, SpeedPassTaskStateRow& State)
{
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT Progress,AwardCount FROM SpeedPassTaskState "
			"WHERE Uin=? AND ActivityId=? AND TaskId=? AND PeriodKey=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, TaskId);
		prep_stmt->setString(4, PeriodKey);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.Uin = Uin; State.ActivityId = ActivityId; State.TaskId = TaskId; State.PeriodKey = PeriodKey;
			State.Progress = res->getInt("Progress");
			State.AwardCount = res->getInt("AwardCount");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e) { std::cerr << e.what() << " PlayerDB_SPLoadTaskState Error" << std::endl; }
	return false;
}

int PlayerDB_SPGetExchangeCount(UINT Uin, UINT ActivityId, UINT ItemId, const std::string& PeriodKey)
{
	if (Uin < 10000 || ActivityId == 0 || ItemId == 0 || PeriodKey.empty()) return 0;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT ExchangeCount FROM SpeedPassExchangeState WHERE Uin=? AND ActivityId=? AND ItemId=? AND PeriodKey=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setUInt(3, ItemId);
		prep_stmt->setString(4, PeriodKey);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			int count = res->getInt("ExchangeCount");
			res.reset();
			return count < 0 ? 0 : count;
		}
		res.reset();
	}
	catch (const std::exception& e) { std::cerr << e.what() << " PlayerDB_SPGetExchangeCount Error" << std::endl; }
	return 0;
}

void PlayerDB_SPSetExchangeCount(UINT Uin, UINT ActivityId, UINT ItemId, const std::string& PeriodKey, INT ExchangeCount)
{
	if (Uin < 10000 || ActivityId == 0 || ItemId == 0 || PeriodKey.empty()) return;
	if (ExchangeCount < 0) ExchangeCount = 0;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO SpeedPassExchangeState(Uin,ActivityId,ItemId,PeriodKey,ExchangeCount,UpdateTime) VALUES(?,?,?,?,?,?) "
			"ON DUPLICATE KEY UPDATE ExchangeCount=VALUES(ExchangeCount),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setUInt(3, ItemId);
		prep_stmt->setString(4, PeriodKey);
		prep_stmt->setInt(5, ExchangeCount);
		prep_stmt->setUInt(6, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e) { std::cerr << e.what() << " PlayerDB_SPSetExchangeCount Error" << std::endl; }
}

void PlayerDB_GWBLoadTempItems(UINT Uin, UINT ActivityId, std::vector<GodWarBoxTempItemRow>& Rows)
{
	Rows.clear();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT Id,ItemIndex,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status FROM GodWarBoxTempItem WHERE Uin=? AND ActivityId=? AND Status=0 ORDER BY Id ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			GodWarBoxTempItemRow row;
			row.Id = res->getUInt("Id");
			row.ItemIndex = res->getInt("ItemIndex");
			row.ItemID = res->getUInt("ItemID");
			row.ItemNum = res->getInt("ItemNum");
			row.ItemType = res->getInt("ItemType");
			row.AvailPeriod = res->getInt("AvailPeriod");
			row.FenjieNum = res->getInt("FenjieNum");
			row.Status = res->getUInt("Status");
			Rows.push_back(row);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GWBLoadTempItems Error" << std::endl;
	}
}

void PlayerDB_GWBAddTempItem(UINT Uin, UINT ActivityId, const GodWarBoxTempItemRow& Row)
{
	if (Uin < 10000 || Row.ItemID == 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO GodWarBoxTempItem(Uin,ActivityId,ItemIndex,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status,CreateTime,UpdateTime) VALUES(?,?,?,?,?,?,?,?,?,?,?);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, Row.ItemIndex);
		prep_stmt->setUInt(4, Row.ItemID);
		prep_stmt->setInt(5, Row.ItemNum > 0 ? Row.ItemNum : 1);
		prep_stmt->setInt(6, Row.ItemType);
		prep_stmt->setInt(7, Row.AvailPeriod);
		prep_stmt->setInt(8, Row.FenjieNum);
		prep_stmt->setUInt(9, Row.Status);
		prep_stmt->setUInt(10, nowTime);
		prep_stmt->setUInt(11, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GWBAddTempItem Error" << std::endl;
	}
}

bool PlayerDB_GWBRemoveTempItem(UINT Uin, UINT ActivityId, const GodWarBoxTempItemRow& Row, UINT NewStatus)
{
	if (Uin < 10000 || Row.ItemID == 0) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"UPDATE GodWarBoxTempItem SET Status=?,UpdateTime=? WHERE Uin=? AND ActivityId=? AND ItemID=? AND ItemNum=? AND ItemType=? AND AvailPeriod=? AND Status=0 ORDER BY Id ASC LIMIT 1;"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, NewStatus);
		prep_stmt->setUInt(2, nowTime);
		prep_stmt->setUInt(3, Uin);
		prep_stmt->setUInt(4, ActivityId);
		prep_stmt->setUInt(5, Row.ItemID);
		prep_stmt->setInt(6, Row.ItemNum > 0 ? Row.ItemNum : 1);
		prep_stmt->setInt(7, Row.ItemType);
		prep_stmt->setInt(8, Row.AvailPeriod);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GWBRemoveTempItem Error" << std::endl;
	}
	return false;
}

void PlayerDB_GWBAddDrawLog(UINT Uin, UINT ActivityId, INT RoundNum, INT AwardIndex, UINT ItemID, INT ItemNum, UINT CostItemId, INT CostItemNum)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO GodWarBoxDrawLog(Uin,ActivityId,RoundNum,AwardIndex,ItemID,ItemNum,CostItemId,CostItemNum,CreateTime) VALUES(?,?,?,?,?,?,?,?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, RoundNum);
		prep_stmt->setInt(4, AwardIndex);
		prep_stmt->setUInt(5, ItemID);
		prep_stmt->setInt(6, ItemNum);
		prep_stmt->setUInt(7, CostItemId);
		prep_stmt->setInt(8, CostItemNum);
		prep_stmt->setUInt(9, static_cast<UINT>(time(nullptr)));
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GWBAddDrawLog Error" << std::endl;
	}
}


bool PlayerDB_TarotLoadState(UINT Uin, UINT ActivityId, TarotStateRow& State)
{
	State.Uin = Uin;
	State.ActivityId = ActivityId;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT DrawCount,CurDraw,CurScore,MaxScore,DrawItemCnt,AccuClaimMask,Page1CardMask,Page2CardMask,Page3CardMask,Page4CardMask "
			"FROM TarotState WHERE Uin=? AND ActivityId=? LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.DrawCount = res->getInt("DrawCount");
			State.CurDraw = res->getInt("CurDraw");
			State.CurScore = res->getInt("CurScore");
			State.MaxScore = res->getInt("MaxScore");
			State.DrawItemCnt = res->getInt("DrawItemCnt");
			State.AccuClaimMask = res->getUInt("AccuClaimMask");
			State.Page1CardMask = res->getUInt("Page1CardMask");
			State.Page2CardMask = res->getUInt("Page2CardMask");
			State.Page3CardMask = res->getUInt("Page3CardMask");
			State.Page4CardMask = res->getUInt("Page4CardMask");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_TarotLoadState Error" << std::endl;
	}
	return false;
}

void PlayerDB_TarotSaveState(UINT Uin, UINT ActivityId, const TarotStateRow& State)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO TarotState(Uin,ActivityId,DrawCount,CurDraw,CurScore,MaxScore,DrawItemCnt,AccuClaimMask,Page1CardMask,Page2CardMask,Page3CardMask,Page4CardMask,UpdateTime) "
			"VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE DrawCount=VALUES(DrawCount),CurDraw=VALUES(CurDraw),CurScore=VALUES(CurScore),"
			"MaxScore=VALUES(MaxScore),DrawItemCnt=VALUES(DrawItemCnt),AccuClaimMask=VALUES(AccuClaimMask),Page1CardMask=VALUES(Page1CardMask),"
			"Page2CardMask=VALUES(Page2CardMask),Page3CardMask=VALUES(Page3CardMask),Page4CardMask=VALUES(Page4CardMask),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, State.DrawCount);
		prep_stmt->setInt(4, State.CurDraw);
		prep_stmt->setInt(5, State.CurScore);
		prep_stmt->setInt(6, State.MaxScore);
		prep_stmt->setInt(7, State.DrawItemCnt);
		prep_stmt->setUInt(8, State.AccuClaimMask);
		prep_stmt->setUInt(9, State.Page1CardMask);
		prep_stmt->setUInt(10, State.Page2CardMask);
		prep_stmt->setUInt(11, State.Page3CardMask);
		prep_stmt->setUInt(12, State.Page4CardMask);
		prep_stmt->setUInt(13, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_TarotSaveState Error" << std::endl;
	}
}

void PlayerDB_TarotLoadTempItems(UINT Uin, UINT ActivityId, std::vector<TarotTempItemRow>& Rows, INT Status)
{
	Rows.clear();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt;
		if (Status >= 0)
		{
			prep_stmt.reset(conn->prepareStatement(
				"SELECT Id,AwardIdx,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,GetNum,Status FROM TarotTempItem WHERE Uin=? AND ActivityId=? AND Status=? ORDER BY Id ASC;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setUInt(2, ActivityId);
			prep_stmt->setInt(3, Status);
		}
		else
		{
			prep_stmt.reset(conn->prepareStatement(
				"SELECT Id,AwardIdx,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,GetNum,Status FROM TarotTempItem WHERE Uin=? AND ActivityId=? ORDER BY Id ASC;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setUInt(2, ActivityId);
		}
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			TarotTempItemRow row;
			row.Id = res->getUInt("Id");
			row.AwardIdx = res->getInt("AwardIdx");
			row.ItemID = res->getUInt("ItemID");
			row.ItemNum = res->getInt("ItemNum");
			row.ItemType = res->getInt("ItemType");
			row.AvailPeriod = res->getInt("AvailPeriod");
			row.FenjieNum = res->getInt("FenjieNum");
			row.GetNum = res->getInt("GetNum");
			row.Status = res->getUInt("Status");
			Rows.push_back(row);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_TarotLoadTempItems Error" << std::endl;
	}
}

void PlayerDB_TarotAddTempItem(UINT Uin, UINT ActivityId, const TarotTempItemRow& Row)
{
	if (Uin < 10000 || Row.ItemID == 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO TarotTempItem(Uin,ActivityId,AwardIdx,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,GetNum,Status,CreateTime,UpdateTime) VALUES(?,?,?,?,?,?,?,?,?,?,?,?);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, Row.AwardIdx);
		prep_stmt->setUInt(4, Row.ItemID);
		prep_stmt->setInt(5, Row.ItemNum > 0 ? Row.ItemNum : 1);
		prep_stmt->setInt(6, Row.ItemType);
		prep_stmt->setInt(7, Row.AvailPeriod);
		prep_stmt->setInt(8, Row.FenjieNum);
		prep_stmt->setInt(9, Row.GetNum);
		prep_stmt->setUInt(10, Row.Status);
		prep_stmt->setUInt(11, nowTime);
		prep_stmt->setUInt(12, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_TarotAddTempItem Error" << std::endl;
	}
}

bool PlayerDB_TarotRemoveTempItem(UINT Uin, UINT ActivityId, const TarotTempItemRow& Row, UINT NewStatus)
{
	if (Uin < 10000 || Row.ItemID == 0) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"UPDATE TarotTempItem SET Status=?,UpdateTime=? WHERE Uin=? AND ActivityId=? AND ItemID=? AND ItemNum=? AND ItemType=? AND AvailPeriod=? AND Status=0 ORDER BY Id ASC LIMIT 1;"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, NewStatus);
		prep_stmt->setUInt(2, nowTime);
		prep_stmt->setUInt(3, Uin);
		prep_stmt->setUInt(4, ActivityId);
		prep_stmt->setUInt(5, Row.ItemID);
		prep_stmt->setInt(6, Row.ItemNum > 0 ? Row.ItemNum : 1);
		prep_stmt->setInt(7, Row.ItemType);
		prep_stmt->setInt(8, Row.AvailPeriod);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_TarotRemoveTempItem Error" << std::endl;
	}
	return false;
}

void PlayerDB_TarotAddDrawLog(UINT Uin, UINT ActivityId, INT RoundNum, INT Page, INT CardIdx, UINT ItemID, INT ItemNum, INT ItemType, INT AvailPeriod, UINT CostItemID, INT CostItemNum, INT ZancunFlag)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO TarotDrawLog(Uin,ActivityId,RoundNum,Page,CardIdx,ItemID,ItemNum,ItemType,AvailPeriod,CostItemID,CostItemNum,ZancunFlag,CreateTime) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, RoundNum);
		prep_stmt->setInt(4, Page);
		prep_stmt->setInt(5, CardIdx);
		prep_stmt->setUInt(6, ItemID);
		prep_stmt->setInt(7, ItemNum);
		prep_stmt->setInt(8, ItemType);
		prep_stmt->setInt(9, AvailPeriod);
		prep_stmt->setUInt(10, CostItemID);
		prep_stmt->setInt(11, CostItemNum);
		prep_stmt->setInt(12, ZancunFlag);
		prep_stmt->setUInt(13, static_cast<UINT>(time(nullptr)));
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_TarotAddDrawLog Error" << std::endl;
	}
}



bool PlayerDB_HM_LoadState(UINT Uin, UINT ActivityId, HuimengDreamStateRow& State)
{
	State.Uin = Uin;
	State.ActivityId = ActivityId;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT DrawCount,GuaranteeCount,ExchangeData,GiftData,ExtraData FROM HuimengDreamState WHERE Uin=? AND ActivityId=? LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.DrawCount = res->getInt("DrawCount");
			State.GuaranteeCount = res->getInt("GuaranteeCount");
			State.ExchangeData = res->getString("ExchangeData");
			State.GiftData = res->getString("GiftData");
			State.ExtraData = res->getString("ExtraData");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_HM_LoadState Error" << std::endl;
	}
	return false;
}

void PlayerDB_HM_SaveState(UINT Uin, UINT ActivityId, const HuimengDreamStateRow& State)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO HuimengDreamState(Uin,ActivityId,DrawCount,GuaranteeCount,ExchangeData,GiftData,ExtraData,UpdateTime) "
			"VALUES(?,?,?,?,?,?,?,?) ON DUPLICATE KEY UPDATE DrawCount=VALUES(DrawCount),GuaranteeCount=VALUES(GuaranteeCount),"
			"ExchangeData=VALUES(ExchangeData),GiftData=VALUES(GiftData),ExtraData=VALUES(ExtraData),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, State.DrawCount);
		prep_stmt->setInt(4, State.GuaranteeCount);
		prep_stmt->setString(5, State.ExchangeData);
		prep_stmt->setString(6, State.GiftData);
		prep_stmt->setString(7, State.ExtraData);
		prep_stmt->setUInt(8, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_HM_SaveState Error" << std::endl;
	}
}



bool PlayerDB_ZY_LoadState(UINT Uin, UINT ActivityId, ZhiYaoGodsStateRow& State)
{
	State.Uin = Uin;
	State.ActivityId = ActivityId;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT DrawCount,AccuScore,ClaimMask FROM ZhiYaoGodsState WHERE Uin=? AND ActivityId=? LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.DrawCount = res->getInt("DrawCount");
			State.AccuScore = res->getInt("AccuScore");
			State.ClaimMask = res->getUInt("ClaimMask");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_ZY_LoadState Error" << std::endl;
	}
	return false;
}

void PlayerDB_ZY_SaveState(UINT Uin, UINT ActivityId, const ZhiYaoGodsStateRow& State)
{
	if (Uin < 10000) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO ZhiYaoGodsState(Uin,ActivityId,DrawCount,AccuScore,ClaimMask,UpdateTime) "
			"VALUES(?,?,?,?,?,?) ON DUPLICATE KEY UPDATE DrawCount=VALUES(DrawCount),AccuScore=VALUES(AccuScore),ClaimMask=VALUES(ClaimMask),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, State.DrawCount);
		prep_stmt->setInt(4, State.AccuScore);
		prep_stmt->setUInt(5, State.ClaimMask);
		prep_stmt->setUInt(6, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_ZY_SaveState Error" << std::endl;
	}
}

void PlayerDB_HM_LoadTempItems(UINT Uin, UINT ActivityId, std::vector<HuimengDreamTempItemRow>& Rows, INT Status)
{
	Rows.clear();
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt;
		if (Status >= 0)
		{
			prep_stmt.reset(conn->prepareStatement(
				"SELECT Id,AwardIdx,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status FROM HuimengDreamTempItem WHERE Uin=? AND ActivityId=? AND Status=? ORDER BY Id ASC;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setUInt(2, ActivityId);
			prep_stmt->setInt(3, Status);
		}
		else
		{
			prep_stmt.reset(conn->prepareStatement(
				"SELECT Id,AwardIdx,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status FROM HuimengDreamTempItem WHERE Uin=? AND ActivityId=? ORDER BY Id ASC;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setUInt(2, ActivityId);
		}
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			HuimengDreamTempItemRow row;
			row.Id = res->getUInt("Id");
			row.AwardIdx = res->getInt("AwardIdx");
			row.ItemID = res->getUInt("ItemID");
			row.ItemNum = res->getInt("ItemNum");
			row.ItemType = res->getInt("ItemType");
			row.AvailPeriod = res->getInt("AvailPeriod");
			row.FenjieNum = res->getInt("FenjieNum");
			row.Status = res->getUInt("Status");
			Rows.push_back(row);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_HM_LoadTempItems Error" << std::endl;
	}
}

void PlayerDB_HM_AddTempItem(UINT Uin, UINT ActivityId, const HuimengDreamTempItemRow& Row)
{
	if (Uin < 10000 || Row.ItemID == 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO HuimengDreamTempItem(Uin,ActivityId,AwardIdx,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status,CreateTime,UpdateTime) VALUES(?,?,?,?,?,?,?,?,?,?,?);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setInt(3, Row.AwardIdx);
		prep_stmt->setUInt(4, Row.ItemID);
		prep_stmt->setInt(5, Row.ItemNum > 0 ? Row.ItemNum : 1);
		prep_stmt->setInt(6, Row.ItemType);
		prep_stmt->setInt(7, Row.AvailPeriod);
		prep_stmt->setInt(8, Row.FenjieNum);
		prep_stmt->setUInt(9, Row.Status);
		prep_stmt->setUInt(10, nowTime);
		prep_stmt->setUInt(11, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_HM_AddTempItem Error" << std::endl;
	}
}

bool PlayerDB_HM_RemoveTempItem(UINT Uin, UINT ActivityId, const HuimengDreamTempItemRow& Row, UINT NewStatus)
{
	if (Uin < 10000 || (Row.Id == 0 && Row.AwardIdx <= 0 && Row.ItemID == 0)) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		UINT nowTime = static_cast<UINT>(time(nullptr));
		std::unique_ptr<sql::PreparedStatement> prep_stmt;
		if (Row.Id > 0)
		{
			prep_stmt.reset(conn->prepareStatement(
				"UPDATE HuimengDreamTempItem SET Status=?,UpdateTime=? WHERE Uin=? AND ActivityId=? AND Id=? AND Status=0 LIMIT 1;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, NewStatus);
			prep_stmt->setUInt(2, nowTime);
			prep_stmt->setUInt(3, Uin);
			prep_stmt->setUInt(4, ActivityId);
			prep_stmt->setUInt(5, Row.Id);
			int affected = prep_stmt->executeUpdate();
			prep_stmt.reset();
			if (affected > 0) return true;
		}
		if (Row.AwardIdx > 0)
		{
			prep_stmt.reset(conn->prepareStatement(
				"UPDATE HuimengDreamTempItem SET Status=?,UpdateTime=? WHERE Uin=? AND ActivityId=? AND AwardIdx=? AND Status=0 ORDER BY Id ASC LIMIT 1;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, NewStatus);
			prep_stmt->setUInt(2, nowTime);
			prep_stmt->setUInt(3, Uin);
			prep_stmt->setUInt(4, ActivityId);
			prep_stmt->setInt(5, Row.AwardIdx);
			int affected = prep_stmt->executeUpdate();
			prep_stmt.reset();
			if (affected > 0) return true;
		}
		if (Row.ItemID > 0)
		{
			prep_stmt.reset(conn->prepareStatement(
				"UPDATE HuimengDreamTempItem SET Status=?,UpdateTime=? WHERE Uin=? AND ActivityId=? AND ItemID=? AND Status=0 ORDER BY Id ASC LIMIT 1;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, NewStatus);
			prep_stmt->setUInt(2, nowTime);
			prep_stmt->setUInt(3, Uin);
			prep_stmt->setUInt(4, ActivityId);
			prep_stmt->setUInt(5, Row.ItemID);
			int affected = prep_stmt->executeUpdate();
			prep_stmt.reset();
			if (affected > 0) return true;
		}
		return false;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_HM_RemoveTempItem Error" << std::endl;
	}
	return false;
}

bool PlayerDB_GHLoadState(UINT Uin, INT ActivityType, GodHorseStateRow& State)
{
	State.Uin = Uin;
	State.ActivityType = ActivityType;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT DrawCount,ClaimMask FROM GodHorseState WHERE Uin=? AND ActivityType=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ActivityType);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.DrawCount = res->getInt("DrawCount");
			State.ClaimMask = res->getUInt("ClaimMask");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GHLoadState Error" << std::endl;
	}
	return false;
}

void PlayerDB_GHSaveState(UINT Uin, INT ActivityType, const GodHorseStateRow& State)
{
	if (Uin < 10000 || ActivityType <= 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO GodHorseState(Uin,ActivityType,DrawCount,ClaimMask,UpdateTime) VALUES(?,?,?,?,?) "
			"ON DUPLICATE KEY UPDATE DrawCount=VALUES(DrawCount),ClaimMask=VALUES(ClaimMask),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ActivityType);
		prep_stmt->setInt(3, State.DrawCount);
		prep_stmt->setUInt(4, State.ClaimMask);
		prep_stmt->setUInt(5, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GHSaveState Error" << std::endl;
	}
}

void PlayerDB_GHAddDrawCount(UINT Uin, INT ActivityType, INT AddCount)
{
	if (Uin < 10000 || ActivityType <= 0 || AddCount <= 0) return;
	GodHorseStateRow state;
	state.Uin = Uin;
	state.ActivityType = ActivityType;
	PlayerDB_GHLoadState(Uin, ActivityType, state);
	state.DrawCount += AddCount;
	PlayerDB_GHSaveState(Uin, ActivityType, state);
}

bool PlayerDB_GHMarkScoreClaimed(UINT Uin, INT ActivityType, INT SpecialFlag)
{
	if (Uin < 10000 || ActivityType <= 0 || SpecialFlag <= 0 || SpecialFlag > 31) return false;
	GodHorseStateRow state;
	state.Uin = Uin;
	state.ActivityType = ActivityType;
	PlayerDB_GHLoadState(Uin, ActivityType, state);
	UINT bit = 1u << static_cast<UINT>(SpecialFlag - 1);
	if ((state.ClaimMask & bit) != 0) return false;
	state.ClaimMask |= bit;
	PlayerDB_GHSaveState(Uin, ActivityType, state);
	return true;
}

int PlayerDB_GHGetExchangeCount(UINT Uin, INT ActivityType, INT AwardIdx)
{
	if (Uin < 10000 || ActivityType <= 0 || AwardIdx <= 0) return 0;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT ExchangeCount FROM GodHorseExchange WHERE Uin=? AND ActivityType=? AND AwardIdx=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ActivityType);
		prep_stmt->setInt(3, AwardIdx);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			int count = res->getInt("ExchangeCount");
			res.reset();
			return count;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GHGetExchangeCount Error" << std::endl;
	}
	return 0;
}

void PlayerDB_GHAddExchangeCount(UINT Uin, INT ActivityType, INT AwardIdx, INT AddCount)
{
	if (Uin < 10000 || ActivityType <= 0 || AwardIdx <= 0 || AddCount <= 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO GodHorseExchange(Uin,ActivityType,AwardIdx,ExchangeCount,UpdateTime) VALUES(?,?,?,?,?) "
			"ON DUPLICATE KEY UPDATE ExchangeCount=ExchangeCount+VALUES(ExchangeCount),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ActivityType);
		prep_stmt->setInt(3, AwardIdx);
		prep_stmt->setInt(4, AddCount);
		prep_stmt->setUInt(5, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_GHAddExchangeCount Error" << std::endl;
	}
}

bool PlayerDB_WSTLoadState(UINT Uin, INT ActivityType, WastelandStateRow& State)
{
	State.Uin = Uin;
	State.ActivityType = ActivityType;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT DrawCount,ClaimMask FROM WastelandState WHERE Uin=? AND ActivityType=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ActivityType);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			State.DrawCount = res->getInt("DrawCount");
			State.ClaimMask = res->getUInt("ClaimMask");
			res.reset();
			return true;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_WSTLoadState Error" << std::endl;
	}
	return false;
}

void PlayerDB_WSTSaveState(UINT Uin, INT ActivityType, const WastelandStateRow& State)
{
	if (Uin < 10000 || ActivityType <= 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO WastelandState(Uin,ActivityType,DrawCount,ClaimMask,UpdateTime) VALUES(?,?,?,?,?) "
			"ON DUPLICATE KEY UPDATE DrawCount=VALUES(DrawCount),ClaimMask=VALUES(ClaimMask),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ActivityType);
		prep_stmt->setInt(3, State.DrawCount);
		prep_stmt->setUInt(4, State.ClaimMask);
		prep_stmt->setUInt(5, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_WSTSaveState Error" << std::endl;
	}
}

bool PlayerDB_WSTMarkScoreClaimed(UINT Uin, INT ActivityType, INT SpecialFlag)
{
	if (Uin < 10000 || ActivityType <= 0 || SpecialFlag <= 0 || SpecialFlag > 31) return false;
	WastelandStateRow state;
	state.Uin = Uin;
	state.ActivityType = ActivityType;
	PlayerDB_WSTLoadState(Uin, ActivityType, state);
	UINT bit = 1u << static_cast<UINT>(SpecialFlag - 1);
	if ((state.ClaimMask & bit) != 0) return false;
	state.ClaimMask |= bit;
	PlayerDB_WSTSaveState(Uin, ActivityType, state);
	return true;
}

int PlayerDB_WSTGetExchangeCount(UINT Uin, INT ActivityType, INT AwardIdx)
{
	if (Uin < 10000 || ActivityType <= 0 || AwardIdx <= 0) return 0;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT ExchangeCount FROM WastelandExchange WHERE Uin=? AND ActivityType=? AND AwardIdx=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ActivityType);
		prep_stmt->setInt(3, AwardIdx);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			int count = res->getInt("ExchangeCount");
			res.reset();
			return count;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_WSTGetExchangeCount Error" << std::endl;
	}
	return 0;
}

void PlayerDB_WSTAddExchangeCount(UINT Uin, INT ActivityType, INT AwardIdx, INT AddCount)
{
	if (Uin < 10000 || ActivityType <= 0 || AwardIdx <= 0 || AddCount <= 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO WastelandExchange(Uin,ActivityType,AwardIdx,ExchangeCount,UpdateTime) VALUES(?,?,?,?,?) "
			"ON DUPLICATE KEY UPDATE ExchangeCount=ExchangeCount+VALUES(ExchangeCount),UpdateTime=VALUES(UpdateTime);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ActivityType);
		prep_stmt->setInt(3, AwardIdx);
		prep_stmt->setInt(4, AddCount);
		prep_stmt->setUInt(5, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_WSTAddExchangeCount Error" << std::endl;
	}
}

void PlayerDB_WSTLoadTempItems(UINT Uin, INT ActivityType, std::vector<WastelandTempItemRow>& Rows, INT Status)
{
	Rows.clear();
	if (Uin < 10000 || ActivityType <= 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt;
		if (Status >= 0)
		{
			prep_stmt.reset(conn->prepareStatement(
				"SELECT Id,ActivityType,AwardIdx,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status FROM WastelandTempItem WHERE Uin=? AND ActivityType=? AND Status=? ORDER BY Id ASC;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ActivityType);
			prep_stmt->setInt(3, Status);
		}
		else
		{
			prep_stmt.reset(conn->prepareStatement(
				"SELECT Id,ActivityType,AwardIdx,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status FROM WastelandTempItem WHERE Uin=? AND ActivityType=? ORDER BY Id ASC;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setInt(2, ActivityType);
		}
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			WastelandTempItemRow row;
			row.Id = res->getUInt("Id");
			row.ActivityType = res->getInt("ActivityType");
			row.AwardIdx = res->getInt("AwardIdx");
			row.ItemID = res->getUInt("ItemID");
			row.ItemNum = res->getInt("ItemNum");
			row.ItemType = res->getInt("ItemType");
			row.AvailPeriod = res->getInt("AvailPeriod");
			row.FenjieNum = res->getInt("FenjieNum");
			row.Status = res->getUInt("Status");
			Rows.push_back(row);
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_WSTLoadTempItems Error" << std::endl;
	}
}

void PlayerDB_WSTAddTempItem(UINT Uin, INT ActivityType, const WastelandTempItemRow& Row)
{
	if (Uin < 10000 || ActivityType <= 0 || Row.ItemID == 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO WastelandTempItem(Uin,ActivityType,AwardIdx,ItemID,ItemNum,ItemType,AvailPeriod,FenjieNum,Status,CreateTime,UpdateTime) VALUES(?,?,?,?,?,?,?,?,?,?,?);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setInt(2, ActivityType);
		prep_stmt->setInt(3, Row.AwardIdx);
		prep_stmt->setUInt(4, Row.ItemID);
		prep_stmt->setInt(5, Row.ItemNum > 0 ? Row.ItemNum : 1);
		prep_stmt->setInt(6, Row.ItemType);
		prep_stmt->setInt(7, Row.AvailPeriod);
		prep_stmt->setInt(8, Row.FenjieNum);
		prep_stmt->setUInt(9, Row.Status);
		prep_stmt->setUInt(10, nowTime);
		prep_stmt->setUInt(11, nowTime);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_WSTAddTempItem Error" << std::endl;
	}
}

bool PlayerDB_WSTRemoveTempItem(UINT Uin, INT ActivityType, const WastelandTempItemRow& Row, UINT NewStatus)
{
	if (Uin < 10000 || ActivityType <= 0 || (Row.Id == 0 && Row.ItemID == 0)) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		UINT nowTime = static_cast<UINT>(time(nullptr));
		std::unique_ptr<sql::PreparedStatement> prep_stmt;
		if (Row.Id > 0)
		{
			prep_stmt.reset(conn->prepareStatement(
				"UPDATE WastelandTempItem SET Status=?,UpdateTime=? WHERE Uin=? AND ActivityType=? AND Id=? AND Status=0 LIMIT 1;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, NewStatus);
			prep_stmt->setUInt(2, nowTime);
			prep_stmt->setUInt(3, Uin);
			prep_stmt->setInt(4, ActivityType);
			prep_stmt->setUInt(5, Row.Id);
			int affected = prep_stmt->executeUpdate();
			prep_stmt.reset();
			if (affected > 0) return true;
		}
		if (Row.ItemID > 0)
		{
			prep_stmt.reset(conn->prepareStatement(
				"UPDATE WastelandTempItem SET Status=?,UpdateTime=? WHERE Uin=? AND ActivityType=? AND ItemID=? AND ItemType=? AND AvailPeriod=? AND FenjieNum=? AND Status=0 ORDER BY Id ASC LIMIT 1;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, NewStatus);
			prep_stmt->setUInt(2, nowTime);
			prep_stmt->setUInt(3, Uin);
			prep_stmt->setInt(4, ActivityType);
			prep_stmt->setUInt(5, Row.ItemID);
			prep_stmt->setInt(6, Row.ItemType);
			prep_stmt->setInt(7, Row.AvailPeriod);
			prep_stmt->setInt(8, Row.FenjieNum);
			int affected = prep_stmt->executeUpdate();
			prep_stmt.reset();
			if (affected > 0) return true;
		}
		return false;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_WSTRemoveTempItem Error" << std::endl;
	}
	return false;
}


bool PlayerDB_WFTryMarkClaimed(UINT Uin, UINT EntryID, INT SeasonID, INT ClaimKind, INT ClaimIndex, UINT ClaimTime)
{
	if (Uin < 10000 || EntryID == 0 || SeasonID < 0 || ClaimKind <= 0 || ClaimIndex <= 0) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT IGNORE INTO WelfareClaimState(Uin,EntryID,SeasonID,ClaimKind,ClaimIndex,ClaimTime) VALUES(?,?,?,?,?,?);"));
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, EntryID);
		prep_stmt->setInt(3, SeasonID);
		prep_stmt->setInt(4, ClaimKind);
		prep_stmt->setInt(5, ClaimIndex);
		prep_stmt->setUInt(6, ClaimTime);
		return prep_stmt->executeUpdate() > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_WFTryMarkClaimed Error" << std::endl;
	}
	return false;
}

void PlayerDB_WFLoadClaims(UINT Uin, UINT EntryID, INT SeasonID, INT ClaimKind, std::vector<INT>& ClaimIndexes, UINT& LastClaimTime)
{
	ClaimIndexes.clear();
	LastClaimTime = 0;
	if (Uin < 10000 || EntryID == 0 || SeasonID < 0 || ClaimKind <= 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT ClaimIndex,ClaimTime FROM WelfareClaimState WHERE Uin=? AND EntryID=? AND SeasonID=? AND ClaimKind=? ORDER BY ClaimIndex;"));
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, EntryID);
		prep_stmt->setInt(3, SeasonID);
		prep_stmt->setInt(4, ClaimKind);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		while (res->next())
		{
			ClaimIndexes.push_back(res->getInt("ClaimIndex"));
			LastClaimTime = (std::max)(LastClaimTime, res->getUInt("ClaimTime"));
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_WFLoadClaims Error" << std::endl;
	}
}

bool PlayerDB_CVIsClaimed(UINT Uin, UINT ActivityId, const std::string& ClaimKey)
{
	if (Uin < 10000 || ActivityId == 0 || ClaimKey.empty()) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT 1 FROM CharmVipClaimState WHERE Uin=? AND ActivityId=? AND ClaimKey=? LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setString(3, ClaimKey);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		bool found = res->next();
		res.reset();
		return found;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_CVIsClaimed Error" << std::endl;
	}
	return false;
}

bool PlayerDB_CVTryMarkClaimed(UINT Uin, UINT ActivityId, const std::string& ClaimKey, INT Opt, INT VipLevel)
{
	if (Uin < 10000 || ActivityId == 0 || ClaimKey.empty()) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT IGNORE INTO CharmVipClaimState(Uin,ActivityId,ClaimKey,Opt,VipLevel,CreateTime,UpdateTime) VALUES(?,?,?,?,?,?,?);"));
		UINT nowTime = static_cast<UINT>(time(nullptr));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		prep_stmt->setString(3, ClaimKey);
		prep_stmt->setInt(4, Opt);
		prep_stmt->setInt(5, VipLevel);
		prep_stmt->setUInt(6, nowTime);
		prep_stmt->setUInt(7, nowTime);
		int affected = prep_stmt->executeUpdate();
		prep_stmt.reset();
		return affected > 0;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_CVTryMarkClaimed Error" << std::endl;
	}
	return false;
}


static INT PlayerDB_CVDefaultLevel(INT DefaultVipLevel)
{
	if (DefaultVipLevel < 1) return 1;
	if (DefaultVipLevel > 12) return 12;
	return DefaultVipLevel;
}

static INT PlayerDB_CVCalcLevel(INT GrowthScore, const std::vector<CharmVipLevelNeed>& LevelNeeds, INT DefaultVipLevel)
{
	INT level = PlayerDB_CVDefaultLevel(DefaultVipLevel);
	for (size_t i = 0; i < LevelNeeds.size(); ++i)
	{
		const CharmVipLevelNeed& need = LevelNeeds[i];
		if (need.VipLevel < 1 || need.VipLevel > 12) continue;
		if (need.NeedScore < 0) continue;
		if (GrowthScore >= need.NeedScore && need.VipLevel > level)
		{
			level = need.VipLevel;
		}
	}
	return level;
}

static void PlayerDB_CVFillState(sql::ResultSet* res, CharmVipPlayerStateRow& State)
{
	if (!res) return;
	State.Uin = res->getUInt("Uin");
	State.CurVipLevel = res->getInt("CurVipLevel");
	State.BaseVipLevel = res->getInt("BaseVipLevel");
	State.AccuVipLevel = res->getInt("AccuVipLevel");
	State.CharmValueOfMonth = res->getInt("CharmValueOfMonth");
	State.CharmMonthSum = res->getInt("CharmMonthSum");
	State.CharmYearSum = res->getInt("CharmYearSum");
	State.CharmAccuValue = res->getInt("CharmAccuValue");
	State.GrowthAccuValue = res->getInt("GrowthAccuValue");
	State.TotalBrightScore = res->getInt("TotalBrightScore");
	State.CharmGrowthRemainder = res->getInt("CharmGrowthRemainder");
	State.UpdateTime = res->getUInt("UpdateTime");
	if (State.TotalBrightScore < 0) State.TotalBrightScore = 0;
	if (State.CurVipLevel < 1) State.CurVipLevel = 1;
	if (State.BaseVipLevel < 1) State.BaseVipLevel = State.CurVipLevel;
	if (State.AccuVipLevel < 1) State.AccuVipLevel = State.CurVipLevel;
}

void PlayerDB_CVLoadClaimedKeys(UINT Uin, UINT ActivityId, std::vector<std::string>& ClaimKeys)
{
	ClaimKeys.clear();
	if (Uin < 10000 || ActivityId == 0) return;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT ClaimKey FROM CharmVipClaimState WHERE Uin=? AND ActivityId=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, ActivityId);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			ClaimKeys.push_back(res->getString("ClaimKey").c_str());
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_CVLoadClaimedKeys Error" << std::endl;
	}
}

bool PlayerDB_CVLoadPlayerState(UINT Uin, CharmVipPlayerStateRow& State, INT DefaultVipLevel)
{
	DefaultVipLevel = PlayerDB_CVDefaultLevel(DefaultVipLevel);
	State = CharmVipPlayerStateRow();
	State.Uin = Uin;
	State.CurVipLevel = DefaultVipLevel;
	State.BaseVipLevel = DefaultVipLevel;
	State.AccuVipLevel = DefaultVipLevel;
	if (Uin < 10000) return false;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		UINT nowTime = static_cast<UINT>(time(nullptr));
		std::unique_ptr<sql::PreparedStatement> insert_stmt(conn->prepareStatement(
			"INSERT IGNORE INTO CharmVipPlayerState(Uin,CurVipLevel,BaseVipLevel,AccuVipLevel,UpdateTime) VALUES(?,?,?,?,?);"));
		insert_stmt->clearParameters();
		insert_stmt->setUInt(1, Uin);
		insert_stmt->setInt(2, DefaultVipLevel);
		insert_stmt->setInt(3, DefaultVipLevel);
		insert_stmt->setInt(4, DefaultVipLevel);
		insert_stmt->setUInt(5, nowTime);
		insert_stmt->executeUpdate();
		insert_stmt.reset();

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT Uin,CurVipLevel,BaseVipLevel,AccuVipLevel,CharmValueOfMonth,CharmMonthSum,CharmYearSum,CharmAccuValue,GrowthAccuValue,TotalBrightScore,CharmGrowthRemainder,UpdateTime FROM CharmVipPlayerState WHERE Uin=? LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			PlayerDB_CVFillState(res.get(), State);
			return true;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_CVLoadPlayerState Error" << std::endl;
	}
	return false;
}

static INT PlayerDB_CVOfficialBaseLevel(INT GrowthAccuValue)
{
	static const INT kGrowthNeed[10] = { 1, 19, 69, 200, 500, 1500, 4500, 12000, 35000, 100000 };
	INT level = 0;
	for (INT i = 0; i < 10; ++i)
	{
		if (GrowthAccuValue >= kGrowthNeed[i]) level = i + 1;
	}
	return level;
}

CharmVipCareerDisplayState PlayerDB_CVResolveCareerDisplay(const CharmVipPlayerStateRow& State, INT BaseVipMaxLevel, INT BaseBrightScore, INT SupremeVip2BaseRank)
{
	CharmVipCareerDisplayState display;
	if (BaseVipMaxLevel < 1) BaseVipMaxLevel = 10;
	if (BaseVipMaxLevel > 10) BaseVipMaxLevel = 10;
	if (BaseBrightScore < 0) BaseBrightScore = 0;
	if (SupremeVip2BaseRank < 1) SupremeVip2BaseRank = 100;

	display.TotalBrightScore = State.TotalBrightScore > 0 ? State.TotalBrightScore : 0;
	const bool activated = State.GrowthAccuValue > 0 || State.CharmAccuValue > 0 ||
		State.CharmMonthSum > 0 || State.CharmYearSum > 0;
	if (!activated)
		return display;

	INT storedBaseLevel = State.CurVipLevel;
	if (State.BaseVipLevel > storedBaseLevel) storedBaseLevel = State.BaseVipLevel;
	if (State.AccuVipLevel > storedBaseLevel) storedBaseLevel = State.AccuVipLevel;
	if (storedBaseLevel < 1) storedBaseLevel = 1;
	if (storedBaseLevel > BaseVipMaxLevel) storedBaseLevel = BaseVipMaxLevel;
	const INT growthBaseLevel = PlayerDB_CVOfficialBaseLevel(State.GrowthAccuValue);
	display.BaseVipLevel = growthBaseLevel > storedBaseLevel ? growthBaseLevel : storedBaseLevel;
	if (display.BaseVipLevel > BaseVipMaxLevel) display.BaseVipLevel = BaseVipMaxLevel;
	display.AccuVipLevel = State.AccuVipLevel;
	if (display.AccuVipLevel < display.BaseVipLevel) display.AccuVipLevel = display.BaseVipLevel;
	if (display.AccuVipLevel > BaseVipMaxLevel) display.AccuVipLevel = BaseVipMaxLevel;
	display.CurVipLevel = display.BaseVipLevel;

	// V10 绝影：达到基础 V10 且璀璨积分达到 BaseBrightScore。
	if (display.BaseVipLevel < BaseVipMaxLevel || display.TotalBrightScore < BaseBrightScore)
		return display;
	display.SupremeVipLevel = 1;
	display.CurVipLevel = BaseVipMaxLevel + 1;

	// V10 璀璨：客户端把 CharmCareerRank!=0 直接视为达成，所以榜外必须下发 0。
	if (State.Uin < 10000)
		return display;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return display;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT COUNT(*)+1 AS CareerRank FROM CharmVipPlayerState "
			"WHERE (GREATEST(CurVipLevel,BaseVipLevel,AccuVipLevel)>=10 OR GrowthAccuValue>=100000) "
			"AND (TotalBrightScore>? OR (TotalBrightScore=? AND Uin<?));"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, display.TotalBrightScore);
		prep_stmt->setInt(2, display.TotalBrightScore);
		prep_stmt->setUInt(3, State.Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next())
		{
			const INT careerRank = res->getInt("CareerRank");
			if (careerRank >= 1 && careerRank <= SupremeVip2BaseRank)
			{
				display.CharmCareerRank = careerRank;
				display.SupremeVipLevel = 2;
				display.CurVipLevel = BaseVipMaxLevel + 2;
			}
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << " PlayerDB_CVResolveCareerDisplay Error" << std::endl;
	}
	return display;
}

bool PlayerDB_CVAddCharmAndGrowth(UINT Uin, INT CharmDelta, INT CharmToGrowthRatio, const std::vector<CharmVipLevelNeed>& LevelNeeds, INT DefaultVipLevel, CharmVipPlayerStateRow& State, UINT& NewCharm)
{
	NewCharm = 0;
	DefaultVipLevel = PlayerDB_CVDefaultLevel(DefaultVipLevel);
	if (CharmToGrowthRatio <= 0) CharmToGrowthRatio = 100;
	if (CharmDelta <= 0)
	{
		return PlayerDB_CVLoadPlayerState(Uin, State, DefaultVipLevel);
	}
	if (Uin < 10000) return false;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	try
	{
		UINT nowTime = static_cast<UINT>(time(nullptr));
		conn->setAutoCommit(false);
		const std::string charmVipPlayerStateTable =
			ActivityDB_QualifiedTable("CharmVipPlayerState");

		std::unique_ptr<sql::PreparedStatement> insert_stmt(conn->prepareStatement(
			"INSERT IGNORE INTO " + charmVipPlayerStateTable +
			"(Uin,CurVipLevel,BaseVipLevel,AccuVipLevel,UpdateTime) VALUES(?,?,?,?,?);"));
		insert_stmt->clearParameters();
		insert_stmt->setUInt(1, Uin);
		insert_stmt->setInt(2, DefaultVipLevel);
		insert_stmt->setInt(3, DefaultVipLevel);
		insert_stmt->setInt(4, DefaultVipLevel);
		insert_stmt->setUInt(5, nowTime);
		insert_stmt->executeUpdate();
		insert_stmt.reset();

		std::unique_ptr<sql::PreparedStatement> select_stmt(conn->prepareStatement(
			"SELECT Uin,CurVipLevel,BaseVipLevel,AccuVipLevel,CharmValueOfMonth,CharmMonthSum,CharmYearSum,CharmAccuValue,GrowthAccuValue,TotalBrightScore,CharmGrowthRemainder,UpdateTime FROM " +
			charmVipPlayerStateTable + " WHERE Uin=? LIMIT 1 FOR UPDATE;"));
		select_stmt->clearParameters();
		select_stmt->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> res(select_stmt->executeQuery());
		select_stmt.reset();
		if (!res->next())
		{
			conn->rollback();
			conn->setAutoCommit(true);
			return false;
		}
		PlayerDB_CVFillState(res.get(), State);
		res.reset();

		const INT oldGrowthAccuValue = State.GrowthAccuValue;
		long long totalCharmForGrowth = static_cast<long long>(State.CharmGrowthRemainder) + static_cast<long long>(CharmDelta);
		INT growthDelta = static_cast<INT>(totalCharmForGrowth / CharmToGrowthRatio);
		INT newRemainder = static_cast<INT>(totalCharmForGrowth % CharmToGrowthRatio);
		State.CharmGrowthRemainder = newRemainder;
		State.CharmValueOfMonth += CharmDelta;
		State.CharmMonthSum += CharmDelta;
		State.CharmYearSum += CharmDelta;
		State.CharmAccuValue += CharmDelta;
		State.GrowthAccuValue += growthDelta;
		INT baseVip10Need = 100000;
		for (const CharmVipLevelNeed& need : LevelNeeds)
		{
			if (need.VipLevel == 10 && need.NeedScore >= 0)
			{
				baseVip10Need = need.NeedScore;
				break;
			}
		}
		const INT oldBrightEligibleGrowth = oldGrowthAccuValue > baseVip10Need ? oldGrowthAccuValue - baseVip10Need : 0;
		const INT newBrightEligibleGrowth = State.GrowthAccuValue > baseVip10Need ? State.GrowthAccuValue - baseVip10Need : 0;
		if (newBrightEligibleGrowth > oldBrightEligibleGrowth)
			State.TotalBrightScore += newBrightEligibleGrowth - oldBrightEligibleGrowth;
		INT newLevel = PlayerDB_CVCalcLevel(State.GrowthAccuValue, LevelNeeds, DefaultVipLevel);
		if (newLevel > State.CurVipLevel) State.CurVipLevel = newLevel;
		if (newLevel > State.BaseVipLevel) State.BaseVipLevel = newLevel;
		if (newLevel > State.AccuVipLevel) State.AccuVipLevel = newLevel;
		State.UpdateTime = nowTime;

		std::unique_ptr<sql::PreparedStatement> update_stmt(conn->prepareStatement(
			"UPDATE " + charmVipPlayerStateTable +
			" SET CurVipLevel=?,BaseVipLevel=?,AccuVipLevel=?,CharmValueOfMonth=?,CharmMonthSum=?,CharmYearSum=?,CharmAccuValue=?,GrowthAccuValue=?,TotalBrightScore=?,CharmGrowthRemainder=?,UpdateTime=? WHERE Uin=?;"));
		update_stmt->clearParameters();
		update_stmt->setInt(1, State.CurVipLevel);
		update_stmt->setInt(2, State.BaseVipLevel);
		update_stmt->setInt(3, State.AccuVipLevel);
		update_stmt->setInt(4, State.CharmValueOfMonth);
		update_stmt->setInt(5, State.CharmMonthSum);
		update_stmt->setInt(6, State.CharmYearSum);
		update_stmt->setInt(7, State.CharmAccuValue);
		update_stmt->setInt(8, State.GrowthAccuValue);
		update_stmt->setInt(9, State.TotalBrightScore);
		update_stmt->setInt(10, State.CharmGrowthRemainder);
		update_stmt->setUInt(11, nowTime);
		update_stmt->setUInt(12, Uin);
		update_stmt->executeUpdate();
		update_stmt.reset();

		std::unique_ptr<sql::PreparedStatement> charm_stmt(conn->prepareStatement(
			"UPDATE BaseInfo SET Charm=Charm+? WHERE Uin=?;"));
		charm_stmt->clearParameters();
		charm_stmt->setInt(1, CharmDelta);
		charm_stmt->setUInt(2, Uin);
		charm_stmt->executeUpdate();
		charm_stmt.reset();

		std::unique_ptr<sql::PreparedStatement> charm_select(conn->prepareStatement(
			"SELECT Charm FROM BaseInfo WHERE Uin=? LIMIT 1;"));
		charm_select->clearParameters();
		charm_select->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> charm_res(charm_select->executeQuery());
		charm_select.reset();
		if (charm_res->next())
		{
			NewCharm = charm_res->getUInt("Charm");
		}
		charm_res.reset();

		conn->commit();
		conn->setAutoCommit(true);
		return true;
	}
	catch (const std::exception& e)
	{
		try { conn->rollback(); conn->setAutoCommit(true); } catch (...) {}
		std::cerr << e.what() << " PlayerDB_CVAddCharmAndGrowth Error" << std::endl;
	}
	return false;
}



void PlayerDB_SelectComItemTable(std::shared_ptr<ClientNode> Client, BYTE*& pData, double& intValue) {

	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	int count = 0;
	bool first = false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT ItemId,SUM(OpNum) AS OpNum,Aviable "
			"FROM RetComItemTable WHERE Uin=? AND OpNum>0 "
			"GROUP BY ItemId,Aviable ORDER BY MIN(CreateTime),ItemId,Aviable;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			count++;
			if (first == false) {
				first = true;
				Write8(pData, 246);//F6
				intValue = 1;
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);  // ???????????

				Write8(pData, 253); //FD
				Write32(pData, 0x00000051);
				Write8(pData, 250);//FA
				Write8(pData, 6);//len
				memcpy(pData, "itemID", 6);
				pData += 6;
				Write8(pData, 246);//F6
				intValue = res->getUInt("ItemId");
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);  // ???????????

				Write8(pData, 250);//FA
				Write8(pData, 7);//len
				memcpy(pData, "bOpType", 7);
				pData += 7;
				Write8(pData, 246);//F6
				intValue = 0;
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);  // ???????????

				Write8(pData, 250);//FA
				Write8(pData, 8);//len
				memcpy(pData, "iAviable", 8);
				pData += 8;
				Write8(pData, 246);//F6
				intValue = res->getInt("Aviable");
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);  // ???????????

				Write8(pData, 250);//FA
				Write8(pData, 5);//len
				memcpy(pData, "OpNum", 5);
				pData += 5;
				Write8(pData, 246);//F6
				intValue = res->getUInt("OpNum");
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);  // ???????????

				Write8(pData, 251);  // FB
				Write8(pData, 4);  // Param_fb=4
				Write8(pData, 246); // F6
				intValue = 1; // Param_f6=0.0
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);

			}
			else {
				Write8(pData, 246);  // F6
				intValue = count;  // Param_f6=2.0
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);

				Write8(pData, 253); // FD
				Write32(pData, 0x00000037);  // Param_fd=00000037

				Write8(pData, 251);  // FB
				Write8(pData, 17);  // Param_fb=17
				Write8(pData, 246); // F6
				intValue = res->getUInt("ItemId"); // Param_f6=115165.0
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);

				Write8(pData, 251);  // FB
				Write8(pData, 18);  // Param_fb=18
				Write8(pData, 246); // F6
				intValue = 0.0; // Param_f6=0.0
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);

				Write8(pData, 251);  // FB
				Write8(pData, 19);  // Param_fb=19
				Write8(pData, 246); // F6
				intValue = res->getInt("Aviable");// Param_f6=720.0
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);

				Write8(pData, 251);  // FB
				Write8(pData, 20);  // Param_fb=20
				Write8(pData, 246); // F6
				intValue = res->getUInt("OpNum"); // Param_f6=1.0
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);

				Write8(pData, 251);  // FB
				Write8(pData, 4);  // Param_fb=4
				Write8(pData, 246); // F6
				intValue = 1; // Param_f6=0.0
				std::memcpy(pData, &intValue, sizeof(double));
				pData += sizeof(double);

			}
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SelectComItemTable Error" << std::endl;
		//throw;??
	}

}


void PlayerDB_AddComItemTable(std::shared_ptr<ClientNode> Client, UINT ItemId, int Aviable, int num)
{
	if (num == 0) {
		PlayerDB_AddItem2(Client, Client->Uin, ItemId, 1, 0, false);
		return;
	}
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"INSERT INTO RetComItemTable"
			"(Uin,ItemId,Aviable,OpNum,ExchangePieceNum,CreateTime,UpdateTime) "
			"VALUES(?,?,?,1,?,UNIX_TIMESTAMP(),UNIX_TIMESTAMP()) "
			"ON DUPLICATE KEY UPDATE "
			"OpNum=OpNum+1,ExchangePieceNum=VALUES(ExchangePieceNum),"
			"UpdateTime=UNIX_TIMESTAMP();"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		prep_stmt->setUInt(2, ItemId);
		prep_stmt->setInt(3, Aviable);
		prep_stmt->setUInt(4, num);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_AddComItemTable Error" << std::endl;
		//throw;??
	}
}
void PlayerDB_UpdateComItemTable(std::shared_ptr<ClientNode> Client, UINT ItemId, int Aviable, int num)
{
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE RetComItemTable Set OpNum = OpNum-? WHERE UIN=? And ItemId = ? And Aviable = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, num);
		prep_stmt->setUInt(2, Client->Uin);
		prep_stmt->setUInt(3, ItemId);
		prep_stmt->setInt(4, Aviable);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_UpdateComItemTable Error" << std::endl;
		//throw;??
	}
}

int PlayerDB_GetExchangePieceNum(std::shared_ptr<ClientNode> Client, UINT ItemId, int Aviable, UINT ItemNum)
{
	int itemnum = 0;
	DBConnection myDBConnection(activity); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ExchangePieceNum FROM RetComItemTable WHERE UIN=? And ItemId = ? And Aviable = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		prep_stmt->setUInt(2, ItemId);
		prep_stmt->setInt(3, Aviable);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			res->next();
			itemnum = res->getUInt("ExchangePieceNum") * ItemNum;
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_GetExchangePieceNum Error" << std::endl;
		//throw;??
	}
	return itemnum;
}


bool PlayerDB_ReconcileRankedMasterLeaderboard(
	UINT MasterGradeLevel,
	UINT GloryChildLevel,
	UINT CarGodGradeLevel,
	UINT CarGodChildLevel,
	UINT TopRank,
	bool DemoteWhenOut,
	std::vector<RankedMasterGradeChange>& Changes)
{
	Changes.clear();
	if (TopRank == 0 || MasterGradeLevel == CarGodGradeLevel) return false;

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return false;
	bool transactionStarted = false;
	try
	{
		conn->setAutoCommit(false);
		transactionStarted = true;

		struct RankedMasterRow {
			UINT Uin = 0;
			UINT GradeLevel = 0;
			UINT ChildLevel = 0;
		};

		std::vector<RankedMasterRow> rows;
		std::unique_ptr<sql::PreparedStatement> selectStmt(conn->prepareStatement(
			"SELECT Uin,GradeLevel,ChildLevel,GradeScore FROM RankInfo "
			"WHERE GradeLevel=? OR GradeLevel=? "
			"ORDER BY GradeScore DESC,Uin ASC FOR UPDATE;"));
		selectStmt->setUInt(1, MasterGradeLevel);
		selectStmt->setUInt(2, CarGodGradeLevel);
		std::unique_ptr<sql::ResultSet> res(selectStmt->executeQuery());
		while (res->next())
		{
			RankedMasterRow row;
			row.Uin = res->getUInt("Uin");
			row.GradeLevel = res->getUInt("GradeLevel");
			row.ChildLevel = res->getUInt("ChildLevel");
			rows.push_back(row);
		}
		res.reset();
		selectStmt.reset();

		std::vector<RankedMasterGradeChange> pendingChanges;
		std::unique_ptr<sql::PreparedStatement> updateStmt(conn->prepareStatement(
			"UPDATE RankInfo SET GradeLevel=?,ChildLevel=?,PromotionMatch=0,"
			"CurrentPromotionRoundNums=0,CurrentPromotionWinRoundNums=0,"
			"CurrentPromotionLoseRoundNums=0,PromotionRecordTag=0 WHERE Uin=?;"));
		for (size_t i = 0; i < rows.size(); ++i)
		{
			const UINT position = static_cast<UINT>(i + 1);
			const bool inTopRank = position <= TopRank;
			const bool isGloryMaster = rows[i].GradeLevel == MasterGradeLevel && rows[i].ChildLevel == GloryChildLevel;
			const bool isCarGod = rows[i].GradeLevel == CarGodGradeLevel && rows[i].ChildLevel == CarGodChildLevel;

			UINT targetGradeLevel = rows[i].GradeLevel;
			UINT targetChildLevel = rows[i].ChildLevel;
			if (inTopRank && isGloryMaster)
			{
				targetGradeLevel = CarGodGradeLevel;
				targetChildLevel = CarGodChildLevel;
			}
			else if (!inTopRank && isCarGod && DemoteWhenOut)
			{
				targetGradeLevel = MasterGradeLevel;
				targetChildLevel = GloryChildLevel;
			}

			if (targetGradeLevel == rows[i].GradeLevel && targetChildLevel == rows[i].ChildLevel)
				continue;

			updateStmt->clearParameters();
			updateStmt->setUInt(1, targetGradeLevel);
			updateStmt->setUInt(2, targetChildLevel);
			updateStmt->setUInt(3, rows[i].Uin);
			updateStmt->executeUpdate();

			RankedMasterGradeChange change;
			change.Uin = rows[i].Uin;
			change.GradeLevel = targetGradeLevel;
			change.ChildLevel = targetChildLevel;
			change.Position = position;
			pendingChanges.push_back(change);
		}
		updateStmt.reset();

		conn->commit();
		transactionStarted = false;
		try { conn->setAutoCommit(true); } catch (const std::exception&) {}
		Changes = std::move(pendingChanges);
		return true;
	}
	catch (const std::exception& e)
	{
		if (transactionStarted)
		{
			try { conn->rollback(); } catch (const std::exception&) {}
		}
		try { conn->setAutoCommit(true); } catch (const std::exception&) {}
		std::cerr << e.what() << " PlayerDB_ReconcileRankedMasterLeaderboard Error" << std::endl;
		return false;
	}
}

void PlayerDB_SetRankedInfo(std::shared_ptr<ClientNode> Client)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE RankInfo SET GradeLevel=?,ChildLevel=?,GradeScore=?,TotalScore=?,PromotionMatch=?,CurrentPromotionRoundNums=?,CurrentPromotionWinRoundNums=?,CurrentPromotionLoseRoundNums=?,PromotionRecordTag=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, Client->GradeLevel);
		prep_stmt->setInt(2, Client->ChildLevel);
		prep_stmt->setInt(3, Client->GradeScore);
		prep_stmt->setInt(4, Client->TotalScore);
		prep_stmt->setInt(5, Client->PromotionMatch);
		prep_stmt->setInt(6, Client->CurrentPromotionRoundNums);
		prep_stmt->setInt(7, Client->CurrentPromotionWinRoundNums);
		prep_stmt->setInt(8, Client->CurrentPromotionLoseRoundNums);
		prep_stmt->setInt(9, Client->PromotionRecordTag);
		prep_stmt->setUInt(10, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "PlayerDB_SetRankedInfo Error" << std::endl;
		//throw;??
	}
}


BOOL isDBtHasItem(std::shared_ptr<ClientNode> Client, int ItemID, int count)
{
	int ItemNum = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return FALSE;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT ItemNum from Item WHERE Uin=? AND ItemID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		prep_stmt->setInt(2, ItemID);
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// ????
		while (res->next()) {
			ItemNum = res->getInt("ItemNum");
		}
		res.reset();

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "isDBtHasItem Error" << std::endl;
		//throw;??
	}
	if (ItemNum >= count)
	{
		return TRUE;
	}
	return FALSE;
}




void UpdateClientBase(std::shared_ptr<ClientNode> Client) {
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE BaseInfo SET Experience=?,SuperMoney=?,TotalHonor=?,DanceExp=?,Charm=?,Money=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Experience);
		prep_stmt->setUInt(2, Client->SuperMoney);
		prep_stmt->setUInt(3, Client->TotalHonor);
		prep_stmt->setUInt(4, Client->DanceExp);
		prep_stmt->setUInt(5, Client->Charm);
		prep_stmt->setUInt(6, Client->Money);
		prep_stmt->setUInt(7, Client->Uin);
		prep_stmt->executeUpdate();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UpdateClientExp Error" << std::endl;
		//throw;??

	}
}


void SelectClientExp(std::shared_ptr<ClientNode> Client) {
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Experience,SuperMoney,TotalHonor,DanceExp,Charm,Money FROM BaseInfo WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);;
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			res->next();
			Client->Experience = res->getUInt("Experience");
			Client->SuperMoney = res->getUInt("SuperMoney");
			Client->TotalHonor = res->getUInt("TotalHonor");
			Client->DanceExp = res->getUInt("DanceExp");
			Client->Charm = res->getUInt("Charm");
			Client->Money = res->getUInt("Money");
		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectClientExp Error" << std::endl;
		//throw;??
	}
}


UINT SelectClientExp(UINT Uin) {
	UINT exp;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Experience FROM BaseInfo WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);;
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			res->next();
			exp = res->getInt("Experience");

		}
		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectClientExp Error" << std::endl;
		//throw;??
	}
	return exp;
}


BOOLEAN SelectSuperItem(std::shared_ptr<ClientNode> Client, UINT Itemid) {
	BOOLEAN Ret = false;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return FALSE;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM Item Where ItemId=? and Status=1 and Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, Itemid);
		prep_stmt->setUInt(2, Client->Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->rowsCount() > 0) {
			Ret = true;
		}
		res.reset();
		return Ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectSuperItem Error" << std::endl;
		//throw;??
	}
}

void InsertSign3Info(std::shared_ptr<ClientNode> Client) {
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO Sign3Info(Uin) VALUES(?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "InsertSign3Info Error" << std::endl;
		//throw;??
	}
}

void UpdateSign3Info(std::shared_ptr<ClientNode> Client, char NextSignDay, char* CurTm, int SignWeek) {
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Sign3Info SET SignNum=?,SignTime=?,SignWeek=?  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, NextSignDay);
		prep_stmt->setString(2, CurTm);
		prep_stmt->setInt(3, SignWeek);
		prep_stmt->setUInt(4, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UpdateSign3Info Error" << std::endl;
		//throw;??

	}
}

void UpdateSign3InfoWeek(std::shared_ptr<ClientNode> Client) {
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Sign3Info SET SignWeek=0  WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UpdateSign3InfoWeek Error" << std::endl;
		//throw;??

	}
}

void UpdateBuglet(std::shared_ptr<ClientNode> Client) {
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE BaseInfo SET BugletTalk=? WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, Client->BugletTalk);
		prep_stmt->setUInt(2, Client->Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UpdateBuglet Error" << std::endl;
		//throw;??
	}
}

void UpdateClientKart(std::shared_ptr<ClientNode> Client, int Speed2Weight, int DriftVecWeight, int AdditionalZSpeedWeight, int KartItemID) {
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(
			conn->prepareStatement("UPDATE KartRefit SET RefitCout=RefitCout+1, Speed2Weight=?,DriftVecWeight=?,AdditionalZSpeedWeight=? WHERE Uin=? AND KartID=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Speed2Weight);
		prep_stmt->setUInt(2, DriftVecWeight);
		prep_stmt->setUInt(3, AdditionalZSpeedWeight);
		prep_stmt->setUInt(4, Client->Uin);
		prep_stmt->setUInt(5, KartItemID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UpdateClientKart Error" << std::endl;
		//throw;??
	}
}


std::vector<int> executeQueryRow(const std::string& sqlQuery, const std::string& queryName, const std::vector<int>& conditionValues) {
	DBConnection myDBConnection(player);
	std::vector<int> resultRow;
	auto conn = myDBConnection.getConnection(); if (!conn) return resultRow;
	try {

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sqlQuery));
		for (size_t i = 0; i < conditionValues.size(); ++i) {
			prep_stmt->setUInt(i + 1, conditionValues[i]);
		}
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());

		// ?????????
		int num_cols = res->getMetaData()->getColumnCount();

		// ???????,??????
		if (res->next()) {
			for (int i = 1; i <= num_cols; ++i) {
				resultRow.push_back(res->getInt(i));
			}
		}


		// ??????,????
		res.reset();
		prep_stmt.reset();

		// ???????
		return resultRow;
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << " Error executing query: " << queryName << std::endl;
		//throw;??
	}
}

std::vector<int> executeQueryCol(const std::string& sqlQuery, const std::string& queryName, const std::vector<int>& conditionValues) {
	DBConnection myDBConnection(player);
	std::vector<int> resultRow;
	auto conn = myDBConnection.getConnection(); if (!conn) return resultRow;
	try {

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sqlQuery));
		for (size_t i = 0; i < conditionValues.size(); ++i) {
			prep_stmt->setUInt(i + 1, conditionValues[i]);
		}
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());

		// ?????????
		int num_cols = res->getMetaData()->getColumnCount();

		// ???????
		while (res->next()) {
			resultRow.push_back(res->getInt(1));
		}

		// ??????,????
		res.reset();
		prep_stmt.reset();

		// ????
		return resultRow;
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << " Error executing query: " << queryName << std::endl;
		//throw;??
	}
}



////std::vector<int>		RowData;
//typedef std::vector<int>		RowData;
//typedef std::vector<RowData>	TableData;
//
//int CommonSelectTable(std::string tableName, std::string where, TableData& tableData) 
//{
//	res = select(...);
//	int row = res.row;
//	int colum = res.column;
//
//	for (int i = 0; i < row; i++) {
//		resRow = res[i];
//		RowData rowData;
//		for (int j = 0; j < colum; j++) {
//			rowData.push_back(resRow[j]);
//		}
//		tableData.push_back(rowData);
//	}
//}


std::vector<int> executeQueryRow(const std::string& sqlQuery, const std::string& queryName, const std::vector<std::string>& conditionValues) {
	DBConnection myDBConnection(player);
	std::vector<int> resultRow;
	auto conn = myDBConnection.getConnection(); if (!conn) return resultRow;
	try {

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sqlQuery));
		for (size_t i = 0; i < conditionValues.size(); ++i) {
			prep_stmt->setString(i + 1, conditionValues[i]);
		}
		// ????
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());

		// ?????????
		int num_cols = res->getMetaData()->getColumnCount();

		// ???????,??????
		if (res->next()) {
			for (int i = 1; i <= num_cols; ++i) {
				resultRow.push_back(res->getInt(i));
			}
		}


		// ??????,????
		res.reset();
		prep_stmt.reset();

		// ???????
		return resultRow;
	}
	catch (const std::exception& e) {
		std::cerr << e.what() << " Error executing query: " << queryName << std::endl;
		//throw;??
	}
}


void SelectTopList(const std::string& sqlQuery, std::vector<UINT>& List) {

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		int index = 0;
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sqlQuery));
		prep_stmt->clearParameters();
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		while (res->next())
		{
			List[index] = res->getInt("Uin");
			index++;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectTopList Error" << std::endl;
		////throw;??
	}
}

UINT SelectKartRefitItemInfo(UINT Uin, KartRefitItemInfo*& KartRefit)
{
	PlayerDB_ClearExpiredSkinBindings(Uin);
	UINT count = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT RefitCout,MaxFlags,WWeight,SpeedWeight,JetWeight,SJetWeight,AccuWeight,ShapeRefitCount,KartHeadRefitItemID,KartTailRefitItemID,KartFlankRefitItemID,KartTireRefitItemID,AntiCollisionWeight,SecondRefitCount,Speed2Weight,DriftVecWeight,AdditionalZSpeedWeight,ShapeSuitID,LegendSuitLevel,LegendSuitLevelChoice,ShapeLegendSuitID,KartID  FROM KartRefit WHERE Uin=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		count = res->rowsCount();
		KartRefit = new KartRefitItemInfo[count];
		memset(KartRefit, 0, sizeof(KartRefitItemInfo) * count);
		for (size_t i = 0; i < count; i++)
		{
			if (res->next()) {
				KartRefit[i].RefitCout = res->getUInt("RefitCout");
				KartRefit[i].MaxFlags = res->getUInt("MaxFlags");
				KartRefit[i].MaxFlags = res->getUInt("WWeight");
				KartRefit[i].SpeedWeight = res->getUInt("SpeedWeight");
				KartRefit[i].JetWeight = res->getUInt("JetWeight");
				KartRefit[i].SJetWeight = res->getUInt("SJetWeight");
				KartRefit[i].AccuWeight = res->getUInt("AccuWeight");

				KartRefit[i].ShapeRefitCount = res->getUInt("ShapeRefitCount");
				KartRefit[i].KartHeadRefitItemID = res->getUInt("KartHeadRefitItemID");
				KartRefit[i].KartTailRefitItemID = res->getUInt("KartTailRefitItemID");
				KartRefit[i].KartFlankRefitItemID = res->getUInt("KartFlankRefitItemID");
				KartRefit[i].KartTireRefitItemID = res->getUInt("KartTireRefitItemID");
				KartRefit[i].AntiCollisionWeight = res->getUInt("AntiCollisionWeight");

				KartRefit[i].SecondRefitCount = res->getUInt("SecondRefitCount");
				KartRefit[i].Speed2Weight = res->getUInt("Speed2Weight");
				KartRefit[i].DriftVecWeight = res->getUInt("DriftVecWeight");
				KartRefit[i].AdditionalZSpeedWeight = res->getUInt("AdditionalZSpeedWeight");

				KartRefit[i].ShapeSuitID = res->getUInt("ShapeSuitID");
				KartRefit[i].LegendSuitLevel = res->getUInt("LegendSuitLevel");
				KartRefit[i].LegendSuitLevelChoice = res->getUInt("LegendSuitLevelChoice");
				KartRefit[i].ShapeLegendSuitID = res->getUInt("ShapeLegendSuitID");
				KartRefit[i].KartId = res->getUInt("KartID");
				KartRefit[i].ShapeOriginLevel = 0;
			}
		}
		res.reset();
		return count;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectKartRefitItemInfo Error" << std::endl;
		////throw;??
	}
}

UINT SelectKartRefitItemInfo(UINT Uin, std::vector<KartRefitItemInfo>& KartRefit)
{
	PlayerDB_ClearExpiredSkinBindings(Uin);
	UINT count = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
			"SELECT KartID,RefitCout,MaxFlags,WWeight,SpeedWeight,JetWeight,SJetWeight,AccuWeight,AntiCollisionWeight,SecondRefitCount,ShapeRefitCount,KartHeadRefitItemID,KartTailRefitItemID,KartFlankRefitItemID,KartTireRefitItemID,ShapeSuitID,Speed2Weight,DriftVecWeight,AdditionalZSpeedWeight,LegendSuitLevel,LegendSuitLevelChoice,ShapeLegendSuitID,SkinSuitID FROM KartRefit WHERE Uin = ? ORDER BY KartID ASC;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next())
		{
			KartRefitItemInfo KaIf{};
			KaIf.RefitCout = res->getUInt("RefitCout");
			KaIf.MaxFlags = res->getUInt("MaxFlags");
			KaIf.WWeight = res->getUInt("WWeight");
			KaIf.SpeedWeight = res->getUInt("SpeedWeight");
			KaIf.JetWeight = res->getUInt("JetWeight");
			KaIf.SJetWeight = res->getUInt("SJetWeight");
			KaIf.AccuWeight = res->getUInt("AccuWeight");
			KaIf.ShapeRefitCount = res->getUInt("ShapeRefitCount");
			KaIf.KartHeadRefitItemID = res->getUInt("KartHeadRefitItemID");
			KaIf.KartTailRefitItemID = res->getUInt("KartTailRefitItemID");
			KaIf.KartFlankRefitItemID = res->getUInt("KartFlankRefitItemID");
			KaIf.KartTireRefitItemID = res->getUInt("KartTireRefitItemID");
			KaIf.AntiCollisionWeight = res->getUInt("AntiCollisionWeight");
			KaIf.SecondRefitCount = res->getUInt("SecondRefitCount");
			KaIf.Speed2Weight = res->getUInt("Speed2Weight");
			KaIf.DriftVecWeight = res->getUInt("DriftVecWeight");
			KaIf.AdditionalZSpeedWeight = res->getUInt("AdditionalZSpeedWeight");
			KaIf.LuckyValue = 0;
			KaIf.RefitLuckyValueMaxWeight = (std::max)({
				KaIf.WWeight,
				KaIf.SpeedWeight,
				KaIf.JetWeight,
				KaIf.SJetWeight,
				KaIf.AccuWeight,
				KaIf.AntiCollisionWeight });
			KaIf.ShapeSuitID = res->getUInt("ShapeSuitID");
			KaIf.LegendSuitLevel = res->getUInt("LegendSuitLevel");
			KaIf.LegendSuitLevelChoice = res->getUInt("LegendSuitLevelChoice");
			KaIf.ShapeLegendSuitID = res->getUInt("ShapeLegendSuitID");
			KaIf.KartId = res->getUInt("KartID");
			KaIf.SkinSuitID = res->getUInt("SkinSuitID");
			KaIf.ShapeOriginLevel = 0;
			KartRefit.push_back(KaIf);
		}
		res.reset();
		return count;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectKartRefitItemInfo Error" << std::endl;
		////throw;??
	}
}


void SelectOneKartRefitItemInfo(UINT Uin, UINT KartID, KartRefitItemInfo*& KartRefit)
{
	PlayerDB_ClearExpiredSkinBindings(Uin);
	UINT count = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{

		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("select * from KartRefit where Uin=? and KartID=? limit 0,1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, KartID);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		KartRefit = new KartRefitItemInfo[1];
		memset(KartRefit, 0, sizeof(KartRefitItemInfo) * 1);

		if (res->next()) {
			KartRefit[0].RefitCout = res->getUInt("RefitCout");
			KartRefit[0].MaxFlags = res->getUInt("MaxFlags");
			KartRefit[0].WWeight = res->getUInt("WWeight");
			KartRefit[0].SpeedWeight = res->getUInt("SpeedWeight");
			KartRefit[0].JetWeight = res->getUInt("JetWeight");
			KartRefit[0].SJetWeight = res->getUInt("SJetWeight");
			KartRefit[0].AccuWeight = res->getUInt("AccuWeight");

			KartRefit[0].ShapeRefitCount = res->getUInt("ShapeRefitCount");
			KartRefit[0].KartHeadRefitItemID = res->getUInt("KartHeadRefitItemID");
			KartRefit[0].KartTailRefitItemID = res->getUInt("KartTailRefitItemID");
			KartRefit[0].KartFlankRefitItemID = res->getUInt("KartFlankRefitItemID");
			KartRefit[0].KartTireRefitItemID = res->getUInt("KartTireRefitItemID");
			KartRefit[0].AntiCollisionWeight = res->getUInt("AntiCollisionWeight");

			KartRefit[0].SecondRefitCount = res->getUInt("SecondRefitCount");
			KartRefit[0].Speed2Weight = res->getUInt("Speed2Weight");
			KartRefit[0].DriftVecWeight = res->getUInt("DriftVecWeight");
			KartRefit[0].AdditionalZSpeedWeight = res->getUInt("AdditionalZSpeedWeight");

			KartRefit[0].ShapeSuitID = res->getUInt("ShapeSuitID");
			KartRefit[0].LegendSuitLevel = res->getUInt("LegendSuitLevel");
			KartRefit[0].LegendSuitLevelChoice = res->getUInt("LegendSuitLevelChoice");
			KartRefit[0].ShapeLegendSuitID = res->getUInt("ShapeLegendSuitID");
			KartRefit[0].KartId = res->getUInt("KartID");
			KartRefit[0].SkinSuitID = res->getUInt("SkinSuitID");
			KartRefit[0].ShapeOriginLevel = 0;
		}

		res.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectOneKartRefitItemInfo Error" << std::endl;
		////throw;??
	}
}

void UpdateOneKartRefitItemInfo(UINT Uin, UINT KartID, KartRefitItemInfo*& KartRefit)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		if (KartRefit) {
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE KartRefit SET RefitCout=RefitCout+1, SJetWeight=?,AccuWeight=?,JetWeight=?,WWeight=?,SpeedWeight=?,AntiCollisionWeight=?  WHERE Uin=? AND KartID=?;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, KartRefit->SJetWeight);
			prep_stmt->setUInt(2, KartRefit->AccuWeight);
			prep_stmt->setUInt(3, KartRefit->JetWeight);
			prep_stmt->setUInt(4, KartRefit->WWeight);
			prep_stmt->setUInt(5, KartRefit->SpeedWeight);
			prep_stmt->setUInt(6, KartRefit->AntiCollisionWeight);
			prep_stmt->setUInt(7, Uin);
			prep_stmt->setUInt(8, KartID);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
			delete[] KartRefit;
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UpdateOneKartRefitItemInfo Error" << std::endl;
		////throw;??
	}
}


void InsertBattleRecord(PlayerRaceInfo* ArrPlayerRaceInfo, UINT Mapid, UINT GameMode)
{
	if (version < 1.1) return;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		conn->setAutoCommit(false);
		UINT recordid = 0;
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO BattleRecord(mapid,datetime,gamemode) VALUES (?,?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, Mapid);
		prep_stmt->setInt(2, (DWORD)time(nullptr));
		prep_stmt->setInt(3, GameMode);
		prep_stmt->executeUpdate();
		prep_stmt.reset();

		{
			// 保持原服务端兼容逻辑：插入后读取当前最新的对局记录ID。
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT recordid FROM BattleRecord ORDER BY recordid DESC LIMIT 1;"));
			prep_stmt->clearParameters();
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			while (res->next())
				recordid = res->getUInt("recordid");
			prep_stmt.reset();
			res.reset();
		}

		for (int i = 0; i < 6; i++)
		{
			if (ArrPlayerRaceInfo[i].Uin > 10000 && ArrPlayerRaceInfo[i].Uin != -1) {
				const RaceDetailRecord& detail = ArrPlayerRaceInfo[i].Detail;
				const UINT highestSpeed = detail.HighestSpeed ? detail.HighestSpeed : ArrPlayerRaceInfo[i].HighestSpeed;
				const UINT collisionCount = detail.CollisionCount ? detail.CollisionCount : ArrPlayerRaceInfo[i].CollisionCount;
				const UINT n2oGatherCount = detail.N2OGatherCount ? detail.N2OGatherCount : ArrPlayerRaceInfo[i].N2OGatherCount;
				const UINT driftCount = detail.DriftCount ? detail.DriftCount : ArrPlayerRaceInfo[i].DriftCount;
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(
					"INSERT INTO BattleDetails("
					"recordid,uin,mapid,ranking,gamemode,kartid,time,speed,collision,nitrogen,drift,RankGradeLevel,"
					"AverageSpeed,JetTime,ChaoJiQiBu,ZhuangQiangDianPen,SmallJetCount,DoubleJetCount,FlyJetCount,FallJetCount,"
					"DuanWeiQiBu,DuanWeiDoubleJetCount,LinkJetCount,WCJetCount,CeShenDriftCount,ShuaiWeiDriftCount,CWWJetCount,XiaoGuaCount,"
					"TeamID,FastCornersNum,DiffCornerNum,FastCornerDetailInfo,WSingleJetCount,WCWJetCount,CWWJetCount1,CeShenJetCount,ShuaiWeiJetCount,"
					"RankedMatchGrade,DuanWeiPiaoYi,DuanWeiJet,OutOfTurnQuickly,LaunchCount,HitWaterDrift,OutWaterJet,N2DriftContinue,WaveFlyJet,"
					"WorldID,Npc,GameSeq,DetailTime,RMTopRaceScore"
					") VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);"));
				prep_stmt->clearParameters();
				prep_stmt->setInt(1, recordid);
				prep_stmt->setUInt(2, ArrPlayerRaceInfo[i].Uin);
				prep_stmt->setInt(3, Mapid);
				prep_stmt->setInt(4, ArrPlayerRaceInfo[i].FinishTime == -1 ? -1 : i + 1);
				prep_stmt->setInt(5, GameMode);
				prep_stmt->setInt(6, ArrPlayerRaceInfo[i].kartid);
				prep_stmt->setInt(7, ArrPlayerRaceInfo[i].FinishTime);
				prep_stmt->setInt(8, highestSpeed);
				prep_stmt->setInt(9, collisionCount);
				prep_stmt->setInt(10, n2oGatherCount);
				prep_stmt->setInt(11, driftCount);
				prep_stmt->setInt(12, ArrPlayerRaceInfo[i].RankGradeLevel);
				prep_stmt->setUInt(13, detail.AverageSpeed);
				prep_stmt->setUInt(14, detail.JetTime);
				prep_stmt->setUInt(15, detail.ChaoJiQiBu);
				prep_stmt->setUInt(16, detail.ZhuangQiangDianPen);
				prep_stmt->setUInt(17, detail.SmallJetCount);
				prep_stmt->setUInt(18, detail.DoubleJetCount);
				prep_stmt->setUInt(19, detail.FlyJetCount);
				prep_stmt->setUInt(20, detail.FallJetCount);
				prep_stmt->setUInt(21, detail.DuanWeiQiBu);
				prep_stmt->setUInt(22, detail.DuanWeiDoubleJetCount);
				prep_stmt->setUInt(23, detail.LinkJetCount);
				prep_stmt->setUInt(24, detail.WCJetCount);
				prep_stmt->setUInt(25, detail.CeShenDriftCount);
				prep_stmt->setUInt(26, detail.ShuaiWeiDriftCount);
				prep_stmt->setUInt(27, detail.CWWJetCount);
				prep_stmt->setUInt(28, detail.XiaoGuaCount);
				prep_stmt->setUInt(29, detail.TeamID);
				prep_stmt->setUInt(30, detail.FastCornersNum);
				prep_stmt->setUInt(31, detail.DiffCornerNum);
				prep_stmt->setUInt(32, detail.FastCornerDetailInfo);
				prep_stmt->setUInt(33, detail.WSingleJetCount);
				prep_stmt->setUInt(34, detail.WCWJetCount);
				prep_stmt->setUInt(35, detail.CWWJetCount1);
				prep_stmt->setUInt(36, detail.CeShenJetCount);
				prep_stmt->setUInt(37, detail.ShuaiWeiJetCount);
				prep_stmt->setInt(38, detail.RankedMatchGrade);
				prep_stmt->setUInt(39, detail.DuanWeiPiaoYi);
				prep_stmt->setUInt(40, detail.DuanWeiJet);
				prep_stmt->setUInt(41, detail.OutOfTurnQuickly);
				prep_stmt->setUInt(42, detail.Launch);
				prep_stmt->setUInt(43, detail.HitWaterDrift);
				prep_stmt->setUInt(44, detail.OutWaterJet);
				prep_stmt->setUInt(45, detail.N2DriftContinue);
				prep_stmt->setUInt(46, detail.WaveFlyJet);
				prep_stmt->setUInt(47, detail.WorldID);
				prep_stmt->setUInt(48, detail.Npc);
				prep_stmt->setUInt(49, detail.GameSeq);
				prep_stmt->setUInt(50, detail.Time);
				prep_stmt->setUInt(51, detail.RMTopRaceScore);
				prep_stmt->executeUpdate();
				prep_stmt.reset();
			}
		}
		conn->commit();
		conn->setAutoCommit(true);

	}
	catch (const std::exception& e)
	{
		try { conn->rollback(); conn->setAutoCommit(true); }
		catch (const std::exception&) {}
		std::cerr << e.what() << " InsertBattleRecord Error" << std::endl;
		////throw;??
	}
}

void UpdateAppellationType(UINT Uin, int type)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("update BaseInfo set AppellationType = ? where Uin = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, type);
		prep_stmt->setUInt(2, Uin);
		prep_stmt->executeUpdate();
		prep_stmt.reset();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UpdateAppellationType Error" << std::endl;
		////throw;??
	}
}


int SelectAppellationType(UINT Uin)
{
	int AppellationType = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Select AppellationType From  BaseInfo  where Uin = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setInt(1, Uin);

		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next()) {
			AppellationType = res->getInt("AppellationType");
		}
		res.reset();

		return AppellationType;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectAppellationType Error" << std::endl;
		////throw;??
	}


}


void SelectGmTime(int Time)
{
	int AppellationType = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Uin FROM baseinfo WHERE IFNULL(gm, 0) > 1 And IFNULL(admin, 0)= 0 And DATE_ADD(gmtime, INTERVAL ? DAY) < NOW();"));
		prep_stmt->clearParameters();

		prep_stmt->setInt(1, Time);

		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next()) {
			UINT Uin = res->getUInt("Uin");
			UserDB_ban(Uin);
			std::shared_ptr<ClientNode> ClientTemp = GetClient(Uin);
			if (ClientTemp) {
				NotifyKickPlayer(ClientTemp, "Gm????,??????!", 3);
				memcpy(ClientTemp->Key, "", 16);
				ClientTemp->IsLogin = FALSE;
				ClientTemp->Server->Disconnect(ClientTemp->TransportConnID);
				ClientTemp->deleteflag = 1;
				//ClientTemp = NULL;
			}
		}
		res.reset();

	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectGmTime Error" << std::endl;
		////throw;??
	}


}



int SelectChallengeMap(UINT Mapid, UINT RankID, std::vector<ChallengeMap>& ChallengeMaps)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	ChallengeMaps.clear();
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT * FROM mapchallengerank WHERE Mapid =? and rankid = ?;"));
		prep_stmt->clearParameters();

		prep_stmt->setUInt(1, Mapid);
		prep_stmt->setUInt(2, RankID);

		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next()) {
			ChallengeMap CM;
			CM.MapID = res->getInt("Mapid");
			CM.RankID = res->getInt("RankID");
			CM.Uin = res->getInt("Uin");
			CM.Name = UTF2GB(res->getString("NickName").c_str());

			//CM.Name = res->getString("NickName").c_str();
			CM.GuildID = res->getInt("GuildID");
			CM.GuildName = UTF2GB(res->getString("GuildName").c_str());
			
			//CM.GuildName = res->getString("GuildName");
			CM.UseTime = res->getInt("UseTime");
			CM.KartID = res->getInt("KartID");
			CM.FileMd5 = res->getString("FileMd5");
			CM.UUid = res->getString("UUid");
			CM.LastUUid = res->getString("UUid");
			CM.KeepCnt = res->getInt("KeepCnt");
			CM.ChallengeCnt = res->getInt("ChallengeCnt");
			ChallengeMaps.push_back(CM);
		}
		res.reset();
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectChallengeMap Error" << std::endl;
		////throw;??
	}


}

int SelectChallengeMap(std::vector<ChallengeMap>& ChallengeMaps)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	ChallengeMaps.clear();
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT * FROM mapchallengerank WHERE rankid = 1 Limit 10;"));
		prep_stmt->clearParameters();
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next()) {
			ChallengeMap CM;
			CM.MapID = res->getInt("Mapid");
			CM.RankID = res->getInt("RankID");
			CM.Uin = res->getUInt("Uin");
			CM.Name = UTF2GB(res->getString("NickName").c_str());
			
			//CM.Name = res->getString("NickName").c_str();
			CM.GuildID = res->getInt("GuildID");
			CM.GuildName = UTF2GB(res->getString("GuildName").c_str());
			
			//CM.GuildName = res->getString("GuildName");
			CM.UseTime = res->getInt("UseTime");
			CM.KartID = res->getInt("KartID");
			CM.FileMd5 = res->getString("FileMd5");
			CM.UUid = res->getString("UUid");
			CM.LastUUid = res->getString("UUid");
			ChallengeMaps.push_back(CM);
		}
		res.reset();
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectChallengeMap2 Error" << std::endl;
		////throw;??
	}


}

int SelectChallengeMap(std::shared_ptr<ClientNode> Client, std::vector<ChallengeMap>& ChallengeMaps)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	ChallengeMaps.clear();
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("(SELECT * FROM mapchallengerank WHERE  GuildID = ? AND uin = ? Order by Rankid asc) UNION ALL (SELECT * FROM mapchallengerank WHERE GuildID = ? AND uin <> ? Order by Rankid asc) LIMIT 1;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Client->GuildID);
		prep_stmt->setUInt(2, Client->Uin);
		prep_stmt->setUInt(3, Client->GuildID);
		prep_stmt->setUInt(4, Client->Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next()) {
			ChallengeMap CM;
			CM.MapID = res->getInt("Mapid");
			CM.RankID = res->getInt("RankID");
			CM.Uin = res->getUInt("Uin");
			CM.Name = UTF2GB(res->getString("NickName").c_str());
			

			//CM.Name = res->getString("NickName").c_str();
			CM.GuildID = res->getInt("GuildID");
			CM.GuildName = UTF2GB(res->getString("GuildName").c_str());
			
			//CM.GuildName = res->getString("GuildName");
			CM.UseTime = res->getInt("UseTime");
			CM.KartID = res->getInt("KartID");
			CM.FileMd5 = res->getString("FileMd5");
			CM.UUid = res->getString("UUid");
			CM.LastUUid = res->getString("UUid");
			ChallengeMaps.push_back(CM);
		}
		res.reset();
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectChallengeMap3 Error" << std::endl;
		////throw;??
	}


}



int UpdateChallengeMapCNT(UINT Mapid, UINT RankID)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Update mapchallengerank Set keepCnt = keepCnt + 1 WHERE Mapid =? and rankid = ?;"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, Mapid);
		prep_stmt->setUInt(2, RankID);
		prep_stmt->executeUpdate();
		prep_stmt.reset();

		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "UpdateChallengeMapCNT Error" << std::endl;
		////throw;??
	}


}



int SelectChallengeMapTime(std::shared_ptr<ClientNode> Client)
{
	if (Client->ChallengeMaps.size() != 1) return 0;
	int UserTime = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT * FROM mapchallengerank WHERE Mapid =? and rankid = ?;"));
		prep_stmt->clearParameters();

		prep_stmt->setUInt(1, Client->ChallengeMaps[0].MapID);
		prep_stmt->setUInt(2, Client->ChallengeMaps[0].RankID);

		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next()) {
			UserTime = res->getInt("UseTime");
		}
		res.reset();
		return UserTime;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectChallengeMapTime Error" << std::endl;
		////throw;??
	}


}

bool CompareChallengeMaps(const ChallengeMap& a, const ChallengeMap& b) {
	if (a.MapID == b.MapID) {
		return a.RankID < b.RankID; // ?? MapID ??,??? RankID
	}
	return a.MapID < b.MapID; // ???? MapID
}

int SelectChallengeAllMap(std::vector<ChallengeMap>& ChallengeAllMaps, std::vector<ChallengeMapsInfo> ChallengeMaps)
{

	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	std::lock_guard<std::mutex> lg(ChallengeMapsMutex);
	ChallengeAllMaps.clear();
	try
	{
		for (size_t i = 0; i < ChallengeMaps.size(); i++)
		{
			if (ChallengeMaps[i].IsOpen == 0) continue;
			for (size_t j = 1; j <= 5; j++)
			{
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT * FROM mapchallengerank Where Mapid=? and RankID =?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, ChallengeMaps[i].MapID);
				prep_stmt->setUInt(2, j);
				std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
				prep_stmt.reset();
				ChallengeMap CM;
				if (res->next()) {
					CM.MapID = res->getUInt("Mapid");
					CM.RankID = res->getUInt("RankID");
					CM.Uin = res->getUInt("Uin");
					CM.Name = res->getString("NickName");
					CM.GuildID = res->getInt("GuildID");
					CM.GuildName = res->getString("GuildName");
					CM.UseTime = res->getInt("UseTime");
					CM.KartID = res->getInt("KartID");
					CM.FileMd5 = res->getString("FileMd5");
					CM.UUid = res->getString("UUid");
					CM.LastUUid = res->getString("UUid");
					CM.KeepCnt = res->getUInt("KeepCnt");
					CM.ChallengeCnt = res->getUInt("ChallengeCnt");
					CM.Status = 0;

				}
				else {
					CM.MapID = ChallengeMaps[i].MapID;
					CM.RankID = j;
					CM.Uin = 0;
					CM.Name = "";
					CM.GuildID = 0;
					CM.GuildName = "";
					CM.UseTime = 99999999;
					CM.KartID = 0;
					CM.FileMd5 = "";
					CM.UUid = "";
					CM.LastUUid = "";
					CM.KeepCnt = 0;
					CM.ChallengeCnt = 0;
					CM.Status = 0;
				}
				ChallengeAllMaps.push_back(CM);
				res.reset();
			}

		}
		// ??
		std::sort(ChallengeAllMaps.begin(), ChallengeAllMaps.end(), CompareChallengeMaps);

		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectChallengeMap Error" << std::endl;
		////throw;??
	}


}


int InSertChallengeMap(std::shared_ptr<ClientNode> Client, ChallengeMap* CM, std::string FilePath)
{
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		conn->setAutoCommit(false);
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("delete from  mapchallengerank Where Mapid=? And Rankid =?;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, CM->MapID);
			prep_stmt->setUInt(2, CM->RankID);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}


		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO mapchallengerank (Mapid, rankid, uin, nickname, guildid, guildname, usetime,kartid,uuid,lastuuid,filemd5,file) VALUES(?, ?, ?,?,?,?, ?, ?,?,?,?,?);"));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, CM->MapID);
		prep_stmt->setUInt(2, CM->RankID);
		prep_stmt->setUInt(3, CM->Uin);
		char* NickName = G2U(CM->Name.c_str());
		char* GuildName = G2U(CM->GuildName.c_str());
		prep_stmt->setString(4, NickName);
		prep_stmt->setUInt(5, CM->GuildID);
		prep_stmt->setString(6, GuildName);
		prep_stmt->setUInt(7, CM->UseTime);
		prep_stmt->setUInt(8, CM->KartID);
		prep_stmt->setString(9, CM->UUid);
		prep_stmt->setString(10, CM->UUid);
		prep_stmt->setString(11, CM->FileMd5);

		std::ifstream file(FilePath, std::ios::binary);
		if (!file) {
			throw std::runtime_error("Could not open file: " + FilePath);
		}
		prep_stmt->setBlob(12, &file);
		prep_stmt->executeUpdate();
		prep_stmt.reset();

		conn->setAutoCommit(true);
		conn->commit();
		if (NickName) {
			delete[] NickName;
			NickName = NULL;
		}
		if (GuildName) {
			delete[] GuildName;
			GuildName = NULL;
		}
		return 1;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "InSertChallengeMap Error" << std::endl;
		////throw;??
	}


}



int SelectBaseInfoEffect(UINT Uin)
{
	int ret = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT SpeclEffect FROM BaseInfo WHERE Uin = ?;"));
		prep_stmt->clearParameters();

		prep_stmt->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next()) {
			ret = res->getUInt("SpeclEffect");
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectBaseInfoEffect Error" << std::endl;
		////throw;??
	}
}


int SelectMyKartLibarayNum(UINT Uin, char* Mess)
{
	int ret = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::string sql = "SELECT count(*) FROM Item a,ItemallNew b WHERE a.itemid = b.id and b.mess = ? and a.Uin = ?";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();

		prep_stmt->setString(1, Mess);
		prep_stmt->setUInt(2, Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next()) {
			ret = res->getUInt(1);
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectMyKartLibarayNum Error" << std::endl;
		////throw;??
	}
}

int SelectMyKartLibarayAllNum(char* Mess)
{
	int ret = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::string sql = "SELECT count(*) FROM ItemallNew b WHERE  b.mess = ?";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();

		prep_stmt->setString(1, Mess);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		if (res->next()) {
			ret = res->getUInt(1);
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectMyKartLibarayAllNum Error" << std::endl;
		////throw;??
	}
}


int Selectmechskinaward(UINT Uin, std::vector<MechTreasureInfo>& MechTreasureInfos)
{
	int ret = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT * FROM mechskinaward WHERE Uin = ?;"));
		prep_stmt->clearParameters();

		prep_stmt->setUInt(1, Uin);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next()) {
			MechTreasureInfo MI;
			MI.ItemId = res->getUInt("Itemid");
			MI.ItemNum = res->getUInt("itemNum");
			MI.getnum = res->getUInt("GetNum");
			MI.DecomposeNum = res->getUInt("DecomposeNum");
			MI.AvailPeriod = res->getInt("Available");
			MechTreasureInfos.push_back(MI);
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "Selectmechskinaward Error" << std::endl;
		////throw;??
	}


}

int SelectmechskinawardGetNum(UINT Uin, UINT Itemid, int Available)
{
	int ret = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT getNum FROM mechskinaward WHERE Uin = ? And Itemid =? And Available =?;"));
		prep_stmt->clearParameters();

		prep_stmt->setUInt(1, Uin);
		prep_stmt->setUInt(2, Itemid);
		prep_stmt->setInt(3, Available);
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		while (res->next()) {
			ret = res->getInt("getNum");
		}
		res.reset();
		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "SelectmechskinawardGetNum Error" << std::endl;
		////throw;??
	}


}


int Insertmechskinaward(UINT Uin, MechTreasureInfo mechTreasureInfo)
{
	int ret = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		bool has = false;
		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT 1 FROM mechskinaward WHERE Uin = ? ANd itemid =? and Available =? ;"));
			prep_stmt->clearParameters();

			prep_stmt->setUInt(1, Uin);
			prep_stmt->setUInt(2, mechTreasureInfo.ItemId);
			prep_stmt->setInt(3, mechTreasureInfo.AvailPeriod);
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			prep_stmt.reset();
			while (res->next()) {
				has = true;
			}
			res.reset();
		}

		if (has) {
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("Update mechskinaward Set itemNum = itemNum + ? Where  Uin = ? ANd itemid =? and Available =? ;"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, mechTreasureInfo.ItemNum);
			prep_stmt->setUInt(2, Uin);
			prep_stmt->setUInt(3, mechTreasureInfo.ItemId);
			prep_stmt->setInt(4, mechTreasureInfo.AvailPeriod);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}
		else {
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("INSERT INTO mechskinaward (Uin, Itemid, itemNum,GetNum,DecomposeNum,Available,exchangePieceNum) VALUES(?, ?, ?,?, ?, ?,?);"));
			prep_stmt->clearParameters();
			prep_stmt->setUInt(1, Uin);
			prep_stmt->setUInt(2, mechTreasureInfo.ItemId);
			prep_stmt->setUInt(3, mechTreasureInfo.ItemNum);
			prep_stmt->setUInt(4, mechTreasureInfo.getnum);
			prep_stmt->setUInt(5, mechTreasureInfo.DecomposeNum);
			prep_stmt->setInt(6, mechTreasureInfo.AvailPeriod);
			prep_stmt->setInt(7, mechTreasureInfo.exchangeNeedNum);
			prep_stmt->executeUpdate();
			prep_stmt.reset();
		}


		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "Insertmechskinaward Error" << std::endl;
		////throw;??
	}


}

int Updatemechskinaward(UINT Uin, std::string Col, UINT Itemid, int itemnum, int Available)
{
	int ret = 0;
	DBConnection myDBConnection(player); auto conn = myDBConnection.getConnection(); if (!conn) return 0;
	try
	{
		std::string sql = "Update mechskinaward Set " + Col + " = " + Col + " + ?,itemNum = itemNum - ? WHere Uin = ? and itemid = ? and Available = ?";
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement(sql));
		prep_stmt->clearParameters();
		prep_stmt->setUInt(1, itemnum);
		prep_stmt->setUInt(2, itemnum);
		prep_stmt->setUInt(3, Uin);
		prep_stmt->setUInt(4, Itemid);
		prep_stmt->setInt(5, Available);
		prep_stmt->executeUpdate();
		prep_stmt.reset();

		return ret;
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << "Updatemechskinaward Error" << std::endl;
		////throw;??
	}


}
