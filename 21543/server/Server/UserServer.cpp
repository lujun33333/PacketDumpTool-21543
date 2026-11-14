#include "Message.h"
#include "Database.h"
#include <mutex>
#include <stdio.h>
#include "z_debug.h"
#include "ConnectionPool.h"
#include "DBConnection.h"
#include "LauncherRuntimeMaterial.h"
#include "LauncherRuntimeAdmission.h"
#include <atomic>
#include <shared_mutex>

std::mutex RegsiterMutex;
extern ConnectionPool usr;
extern UINT isinvite;
extern int isLog;
extern int PreventSDP[2];
extern std::vector<std::string> IpList;
void MyResponseGetUin(std::shared_ptr<ClientNode> Client, UINT Uin)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, 0);
	Write32(p, Uin);
	if (Uin > 10000)
	{
		CheckDupUser(Uin, NULL);
		std::string RandomKey = GetKey(Uin);
		if (RandomKey.empty())
		{
			std::lock_guard<std::mutex> lg(KeysMutex);
			char randomKey[17] = { 0 };
			gen_randomcdk(randomKey, 16);
			RandomKey.assign(randomKey, 16);
			Keys[Uin] = RandomKey;
			UserDB_UpdateLoginKey(Uin, RandomKey.c_str());
		}
		memcpy(p, RandomKey.c_str(), 16);
		p += 16;

	}

	len = p - buf.data();
	SendToClient(Client, 1, buf.data(), len, Client->GameID, FE_GAMELOGIC, 1, Response);

}

void MyRequestGetUin(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	char UserName[65] = { 0 };
	char Password[65] = { 0 };
	DWORD Uin = 0;
	if (!Client) return;
	if (!Body || BodyLen < 2) goto Reply;
	{
		const BYTE* cursor = Body;
		const BYTE* end = Body + (BodyLen - 2); // MSGHEAD.BodyLen includes its own uint16 field.
		auto ReadField = [&cursor, end](char* output, size_t outputSize) -> bool
		{
			if (cursor >= end) return false;
			const size_t length = *cursor++;
			if (length == 0 || length + 1 > outputSize || static_cast<size_t>(end - cursor) < length) return false;
			memcpy(output, cursor, length);
			output[length] = '\0';
			cursor += length;
			return true;
		};
		if (!ReadField(UserName, sizeof(UserName)) || !ReadField(Password, sizeof(Password)) || cursor != end) goto Reply;
	}

	Uin = UserDB_GetUin(Client->Ip, UserName, Password);

Reply:
	if (isLog == 1) {
		auto ServerLogger = spdlog::get("SpeedServer_logger");
		ServerLogger->info("[LauncherLogin][Uin:{:d}][Account:{:s}][Password:[redacted]]", Uin, UserName);
		ServerLogger->flush();
	}
	SecureZeroMemory(Password, sizeof(Password));
	if (Client) {
		std::unique_lock<std::shared_mutex> lock(Client->mtx);
		Client->LauncherAuthenticatedUin = Uin > 10000 ? Uin : 0;
		// The material state is connection-scoped and must never be reopened by
		// either a successful or a failed login. Fresh connections start at zero.
	}
	MyResponseGetUin(Client, Uin);
}

namespace {

bool EnsureRuntimeMaterialService(std::string& error)
{
	auto& service = launcher_runtime::GetRuntimeMaterialService();
	if (service.IsConfigured()) return true;
	static std::mutex loadMutex;
	std::lock_guard<std::mutex> lock(loadMutex);
	if (service.IsConfigured()) return true;
	return service.ConfigureFromEmbeddedRelease(error);
}

launcher_runtime::PreloginAdmission& GetPreloginAdmission()
{
	// The burst accommodates shared-NAT launcher starts. Sustained anonymous
	// traffic is limited to eight requests per second per public IP.
	static launcher_runtime::PreloginAdmission admission({ 32, 125, 8, 8192, 600000 });
	return admission;
}

void DisconnectRuntimeMaterialClient(const std::shared_ptr<ClientNode>& Client)
{
	if (Client && Client->Server) Client->Server->Disconnect(Client->TransportConnID);
}

void LogRuntimeMaterialRejection(
	const std::shared_ptr<ClientNode>& Client,
	const char* mode,
	UINT authenticatedUin,
	const std::string& reason)
{
	static launcher_runtime::IntervalGate logGate(5000);
	static std::atomic<std::uint64_t> rejectedCount{ 0 };
	rejectedCount.fetch_add(1, std::memory_order_relaxed);
	if (!logGate.ShouldEmit(launcher_runtime::MonotonicNowMs())) return;
	const auto aggregateCount = rejectedCount.exchange(0, std::memory_order_relaxed);
	const char* ip = Client ? Client->Ip : "";
	printf("[LauncherRuntimeMaterial][Rejected][Mode:%s][Uin:%u][IP:%s][Count:%llu][Reason:%s]\n",
		mode, authenticatedUin, ip, static_cast<unsigned long long>(aggregateCount), reason.c_str());
	try {
		if (auto ServerLogger = spdlog::get("SpeedServer_logger")) {
			ServerLogger->warn(
				"[LauncherRuntimeMaterial][Rejected][Mode:{:s}][Uin:{:d}][IP:{:s}][Count:{:d}][Reason:{:s}]",
				mode,
				authenticatedUin,
				ip,
				aggregateCount,
				reason);
			// Do not synchronously flush attacker-controlled rejection traffic.
		}
	}
	catch (...) {
		// Rejection logging must never prevent the caller from disconnecting.
	}
}

void SealRuntimeMaterialConnectionState(const std::shared_ptr<ClientNode>& Client)
{
	if (!Client) return;
	std::unique_lock<std::shared_mutex> lock(Client->mtx);
	Client->LauncherRuntimeMaterialState = 2;
}

} // namespace

void MyRequestRuntimeMaterial(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (!Client || !Body || BodyLen < 2) return;
	const size_t payloadLength = BodyLen - 2; // MSGHEAD.BodyLen includes its own uint16 field.
	const bool claimsPreloginV2 =
		payloadLength > 0 && Body[0] == launcher_runtime::kPreloginProtocolVersion;
	const bool validPreloginShape = payloadLength == launcher_runtime::kRequestBytes;
	UINT authenticatedUin = 0;
	launcher_runtime::RequestGateDecision gateDecision;
	{
		std::unique_lock<std::shared_mutex> lock(Client->mtx);
		authenticatedUin = Client->LauncherAuthenticatedUin;
		gateDecision = launcher_runtime::EvaluateRequestGate(
			Client->LauncherRuntimeMaterialState,
			authenticatedUin,
			claimsPreloginV2,
			validPreloginShape);
		if (gateDecision == launcher_runtime::RequestGateDecision::AllowAuthenticatedV1 ||
			gateDecision == launcher_runtime::RequestGateDecision::AllowPreloginV2) {
			Client->LauncherRuntimeMaterialState = 1;
		}
	}
	if (gateDecision == launcher_runtime::RequestGateDecision::IgnoreLegacyUnauthenticated) return;
	if (gateDecision == launcher_runtime::RequestGateDecision::RejectPreloginV2) {
		SealRuntimeMaterialConnectionState(Client);
		LogRuntimeMaterialRejection(Client, "prelogin-v2", authenticatedUin, "request state or shape is invalid");
		DisconnectRuntimeMaterialClient(Client);
		return;
	}

	const bool preloginRequest =
		gateDecision == launcher_runtime::RequestGateDecision::AllowPreloginV2;
	launcher_runtime::PreloginAdmission::Lease admissionLease;
	if (preloginRequest) {
		const auto admissionDecision = GetPreloginAdmission().TryAcquire(
			Client->Ip,
			launcher_runtime::MonotonicNowMs(),
			admissionLease);
		if (admissionDecision != launcher_runtime::AdmissionDecision::Allowed) {
			SealRuntimeMaterialConnectionState(Client);
			LogRuntimeMaterialRejection(
				Client,
				"prelogin-v2",
				0,
				admissionDecision == launcher_runtime::AdmissionDecision::Busy
					? "global prelogin concurrency limit reached"
					: "prelogin IP rate limit reached");
			DisconnectRuntimeMaterialClient(Client);
			return;
		}
	}

	std::string error;
	std::vector<BYTE> response;
	const bool ready = EnsureRuntimeMaterialService(error);
	const bool issued = ready && (preloginRequest
		? launcher_runtime::GetRuntimeMaterialService().IssuePrelogin(
			reinterpret_cast<const std::uint8_t*>(Body),
			payloadLength,
			0,
			response,
			error)
		: launcher_runtime::GetRuntimeMaterialService().Issue(
			authenticatedUin,
			reinterpret_cast<const std::uint8_t*>(Body),
			payloadLength,
			0,
			response,
		error));
	if (!issued) {
		SealRuntimeMaterialConnectionState(Client);
		LogRuntimeMaterialRejection(
			Client,
			preloginRequest ? "prelogin-v2" : "authenticated-v1",
			authenticatedUin,
			error);
		DisconnectRuntimeMaterialClient(Client);
		return;
	}
	const BOOL sent = SendToClient(
		Client,
		3,
		response.data(),
		static_cast<DWORD>(response.size()),
		Client->GameID,
		FE_GAMELOGIC,
		1,
		Response);
	SecureZeroMemory(response.data(), response.size());
	{
		std::unique_lock<std::shared_mutex> lock(Client->mtx);
		if (Client->LauncherRuntimeMaterialState == 1) {
			// A failed send is terminal for this connection as well; seal before
			// disconnecting so another worker cannot reopen the issuing window.
			Client->LauncherRuntimeMaterialState = 2;
		}
	}
	if (!sent) {
		LogRuntimeMaterialRejection(
			Client,
			preloginRequest ? "prelogin-v2" : "authenticated-v1",
			authenticatedUin,
			"runtime material response send failed");
		DisconnectRuntimeMaterialClient(Client);
	}
}

void MyResponseRegister(std::shared_ptr<ClientNode> Client, USHORT Result)
{
	std::vector<BYTE> buf(819200); BYTE* p = buf.data();
	//BYTE* p = buf;
	size_t len;

	Write16(p, Result);

	len = p - buf.data();
	SendToClient(Client, 2, buf.data(), len, Client->GameID, FE_GAMELOGIC, 1, Response);
}


void MyRequestRegister(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	
	
	int result;
	char cdkactive = 0;
	USHORT Result = 1;
	size_t len;

	// See MyRequestRegisterNew: reject forged/truncated packets before a length
	// prefix can drive memcpy past the received body (three fields here:
	// UserName, Password, cdk). Only BodyLen - 2 bytes are reachable from Body.
	{
		const size_t payloadLen = (BodyLen >= 2) ? (BodyLen - 2) : 0;
		const BYTE* pScan = Body;
		const BYTE* const pScanEnd = Body + payloadLen;
		bool badPacket = false;
		for (int field = 0; field < 3 && !badPacket; ++field)
		{
			if (pScan >= pScanEnd) { badPacket = true; break; }
			const size_t fieldLen = *pScan++;
			if (fieldLen > static_cast<size_t>(pScanEnd - pScan)) { badPacket = true; break; }
			pScan += fieldLen;
		}
		if (badPacket)
		{
			printf("MyRequestRegister: malformed packet dropped (BodyLen=%zu)\n", BodyLen);
			return;
		}
	}

	len = Read8(Body);
	char* UserName = new char[len + 1];
	memcpy(UserName, Body, len);
	Body += len;
	UserName[len] = 0;

	len = Read8(Body);
	char* Password = new char[len + 1];
	memcpy(Password, Body, len);
	Body += len;
	Password[len] = 0;

	len = Read8(Body);
	char* cdk = new char[len + 1];
	memcpy(cdk, Body, len);
	Body += len;
	cdk[len] = 0;

	auto ServerLogger = spdlog::get("SpeedServer_logger");
	//ServerLogger->info("[数据库文件异常][Uin:{:d}][KartID:{:d}][KartSkinID:{:d}]", Players[erri]->Uin, Players[erri]->KartID, Players[erri]->KartSkinID);
	ServerLogger->info("[用户注册][Uin:{:d}][账号:{:s}][密码:[redacted]]", Client->Uin, UserName);
	ServerLogger->flush();
	std::lock_guard<std::mutex> lg(RegsiterMutex);
	int KeyIndex = 0;
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		// 使用准备好的查询语句，占位符 ? 指定参数位置
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("select KeyIndex from CdKey where KeyValue=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, cdk);

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// 处理结果
		while (res->next()) {
			KeyIndex = res->getUInt("KeyIndex");
			break;
		}
		res.reset();
		if (KeyIndex > 0) {
			cdkactive = 1;
		}

		if (cdkactive)
		{
			Result = UserDB_Register(UserName, Password,cdk) ? 0 : 1;
			if (Result == 0) {

				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM CdKey WHERE KeyIndex=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, KeyIndex);
				prep_stmt->executeUpdate();

				prep_stmt.reset();

			}
		}
		else {
			Result = 2; //CDK不存在或已使用
		}


		delete[] UserName;
		SecureZeroMemory(Password, strlen(Password));
		delete[] Password;
		delete[] cdk;
		MyResponseRegister(Client, Result);
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("MyRequestRegister Error\n");
		//throw;关闭
	}
}

void MyRequestRegisterNew(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	size_t len;

	// The payload is four 1-byte-length-prefixed fields (UserName, Password,
	// cdk, yqm). A forged or truncated packet must never let a length prefix
	// drive memcpy past the bytes actually received. BodyLen still counts the
	// MSGHEAD length word, so only BodyLen - 2 bytes are reachable from Body.
	{
		const size_t payloadLen = (BodyLen >= 2) ? (BodyLen - 2) : 0;
		const BYTE* pScan = Body;
		const BYTE* const pScanEnd = Body + payloadLen;
		bool badPacket = false;
		for (int field = 0; field < 4 && !badPacket; ++field)
		{
			if (pScan >= pScanEnd) { badPacket = true; break; }
			const size_t fieldLen = *pScan++;
			if (fieldLen > static_cast<size_t>(pScanEnd - pScan)) { badPacket = true; break; }
			pScan += fieldLen;
		}
		if (badPacket)
		{
			printf("MyRequestRegisterNew: malformed packet dropped (BodyLen=%zu)\n", BodyLen);
			return;
		}
	}

	len = Read8(Body);
	char* UserName = new char[len + 1];
	memcpy(UserName, Body, len);
	Body += len;
	UserName[len] = 0;

	len = Read8(Body);
	char* Password = new char[len + 1];
	memcpy(Password, Body, len);
	Body += len;
	Password[len] = 0;

	len = Read8(Body);
	char* cdk = new char[len + 1];
	memcpy(cdk, Body, len);
	Body += len;
	cdk[len] = 0;

	len = Read8(Body);
	char* yqm = new char[len + 1];
	memcpy(yqm, Body, len);
	Body += len;
	yqm[len] = 0;


	auto ServerLogger = spdlog::get("SpeedServer_logger");
	//ServerLogger->info("[数据库文件异常][Uin:{:d}][KartID:{:d}][KartSkinID:{:d}]", Players[erri]->Uin, Players[erri]->KartID, Players[erri]->KartSkinID);
	ServerLogger->info("[用户注册][Uin:{:d}][账号:{:s}][密码:[redacted]][邀请码:[redacted]]", Client->Uin, UserName);
	ServerLogger->flush();

	int cdkactive = 0;
	USHORT Result = 1;
	int KeyIndex = 0;
	std::lock_guard<std::mutex> lg(RegsiterMutex);
	DBConnection myDBConnection(usr); auto conn = myDBConnection.getConnection(); if (!conn) return;
	try
	{
		// 使用准备好的查询语句，占位符 ? 指定参数位置
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("select KeyIndex from CdKey where KeyValue=?;"));
		prep_stmt->clearParameters();
		prep_stmt->setString(1, cdk);

		// 执行查询
		std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
		prep_stmt.reset();
		// 处理结果
		while (res->next()) {
			KeyIndex = res->getUInt("KeyIndex");
			break;
		}
		res.reset();

		if (KeyIndex > 0) {
			cdkactive = 1;
		}
		else
		{
			Result = 2; //CDK不存在或已使用
			goto Reply;
		}

		if (isinvite == 1) {

			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("select MyInviteCode from user where MyInviteCode=?;"));
			prep_stmt->clearParameters();
			prep_stmt->setString(1, yqm);

			// 执行查询
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			prep_stmt.reset();
			// 处理结果
			if (res->rowsCount() == 0) {
				res.reset();
				Result = 3; //邀请码不正确
				goto Reply;
			}

		}


		{
			std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("SELECT Uin FROM User WHERE Name = ?;"));
			prep_stmt->clearParameters();
			prep_stmt->setString(1, UserName);

			// 执行查询
			std::unique_ptr<sql::ResultSet> res(prep_stmt->executeQuery());
			prep_stmt.reset();
			// 处理结果
			if (res->rowsCount() > 0) {
				res.reset();
				Result = 4; //账号已存在
				goto Reply;
			}
		}

		if (cdkactive == 1)
		{
			if (isinvite == 1) {
				Result = UserDB_RegisterNew(UserName, Password, yqm) ? 0 : 1;
			}
			else
			{
				Result = UserDB_Register(UserName, Password, cdk) ? 0 : 1;
			}
			if (Result == 0) {
				std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("DELETE FROM CdKey WHERE KeyIndex=?;"));
				prep_stmt->clearParameters();
				prep_stmt->setUInt(1, KeyIndex);
				prep_stmt->executeUpdate();
				prep_stmt.reset();

			}
		}

	Reply:

		delete[] UserName;
		SecureZeroMemory(Password, strlen(Password));
		delete[] Password;
		delete[] cdk;
		delete[] yqm;
		MyResponseRegister(Client, Result);
	}
	catch (const std::exception& e)
	{
		printf(e.what());
		printf("MyRequestRegisterNew Error\n");
		//throw;关闭
	}


}
