#include "CrossServerTest.h"

#include "Map.h"
#include "Room.h"
#include "yaml-cpp/yaml.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include <winsock2.h>
#include <ws2tcpip.h>

extern std::map<long, std::shared_ptr<ClientNode>> Clients;
extern std::recursive_mutex ClientMutex;
extern std::shared_ptr<ClientNode> GetClient(long Uin);
extern bool ResponseEnterMatchRoom(std::shared_ptr<ClientNode> Client);
extern void WINAPI OnNotifyPlayerInfo(std::shared_ptr<RoomNode> Room);

namespace
{
	enum class CrossServerRole
	{
		Disabled,
		Lobby,
		Worker
	};

	struct CrossServerConfig
	{
		bool Enabled = false;
		CrossServerRole Role = CrossServerRole::Disabled;
		std::string InstanceID;
		std::string CoordinatorHost;
		USHORT CoordinatorPort = 0;
		std::string CoordinatorToken;
		std::string WorkerIP;
		USHORT WorkerTcpPort = 0;
		USHORT WorkerUdpPort = 0;
		std::map<std::string, std::string> InstanceDisplayNames;
		UINT PollIntervalMs = 250;
		UINT QueueTimeoutSeconds = 120;
		UINT SocketTimeoutMs = 2000;
		std::string Error;
	};

	struct WorkerRoomState
	{
		CrossServerMatchTicket Ticket;
		std::weak_ptr<RoomNode> Room;
		RoomSeatMask AuthenticatedMask = 0;
		RoomSeatMask EnteredMask = 0;
		bool SnapshotReady = false;
		bool StartScheduled = false;
	};

	std::once_flag g_configOnce;
	CrossServerConfig g_config;
	std::mutex g_lobbyQueueMutex;
	std::map<UINT, unsigned long long> g_lobbyQueueGeneration;
	std::map<UINT, CrossServerMatchTicket> g_lobbyActiveTickets;
	std::set<UINT> g_lobbyEnteredTickets;
	unsigned long long g_nextLobbyQueueGeneration = 0;
	std::mutex g_workerMutex;
	std::map<UINT, WorkerRoomState> g_workerRooms;

	bool IsSafeProtocolToken(const std::string& value)
	{
		if (value.empty() || value.size() > 128)
			return false;
		return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
			return ch > 0x20 && ch < 0x7F && ch != '=' && ch != ':' && ch != ',';
		});
	}

	bool IsValidGBKTrail(unsigned char ch)
	{
		return ch >= 0x40 && ch <= 0xFE && ch != 0x7F;
	}

	void AppendGBKWithoutSplitting(std::string& output, const std::string& input, size_t capacity)
	{
		for (size_t offset = 0; offset < input.size() && output.size() < capacity;)
		{
			const unsigned char lead = static_cast<unsigned char>(input[offset]);
			size_t characterLength = 1;
			if (lead >= 0x81 && lead <= 0xFE && offset + 1 < input.size() &&
				IsValidGBKTrail(static_cast<unsigned char>(input[offset + 1])))
			{
				characterLength = 2;
			}
			if (output.size() + characterLength > capacity)
				break;
			output.append(input, offset, characterLength);
			offset += characterLength;
		}
	}

	void LoadConfigOnce()
	{
		std::call_once(g_configOnce, []() {
			try
			{
				const YAML::Node root = YAML::LoadFile(".\\Config\\config.yml");
				const YAML::Node node = root["CrossServerTest"];
				if (!node || !node["Enabled"] || !node["Enabled"].as<bool>())
					return;

				g_config.Enabled = true;
				const std::string role = node["Role"].as<std::string>();
				if (role == "Lobby")
					g_config.Role = CrossServerRole::Lobby;
				else if (role == "Worker")
					g_config.Role = CrossServerRole::Worker;
				else
					throw std::runtime_error("Role must be Lobby or Worker");

				g_config.InstanceID = node["InstanceID"] ? node["InstanceID"].as<std::string>() : std::string();
				g_config.CoordinatorHost = node["CoordinatorHost"].as<std::string>();
				g_config.CoordinatorPort = node["CoordinatorPort"].as<USHORT>();
				g_config.CoordinatorToken = node["CoordinatorToken"].as<std::string>();
				if (node["PollIntervalMs"])
					g_config.PollIntervalMs = node["PollIntervalMs"].as<UINT>();
				if (node["QueueTimeoutSeconds"])
					g_config.QueueTimeoutSeconds = node["QueueTimeoutSeconds"].as<UINT>();
				if (node["SocketTimeoutMs"])
					g_config.SocketTimeoutMs = node["SocketTimeoutMs"].as<UINT>();

				if (g_config.Role == CrossServerRole::Lobby)
				{
					g_config.WorkerIP = node["WorkerIP"].as<std::string>();
					g_config.WorkerTcpPort = node["WorkerTcpPort"].as<USHORT>();
					g_config.WorkerUdpPort = node["WorkerUdpPort"].as<USHORT>();
				}
				else
				{
					const YAML::Node displayNames = node["InstanceDisplayNames"];
					if (!displayNames || !displayNames.IsMap())
						throw std::runtime_error("Worker requires InstanceDisplayNames");
					for (const auto& entry : displayNames)
					{
						const std::string instance = entry.first.as<std::string>();
						// config.yml in this server tree is the same legacy GBK wire encoding
						// used by ClientNode::NickName, so keep the configured bytes unchanged.
						const std::string displayNameGBK = entry.second.as<std::string>();
						if (!IsSafeProtocolToken(instance) || displayNameGBK.empty() ||
							displayNameGBK.size() + 1 >= MaxNickName)
						{
							throw std::runtime_error("invalid worker instance display name");
						}
						g_config.InstanceDisplayNames[instance] = displayNameGBK;
					}
				}

				if (!IsSafeProtocolToken(g_config.InstanceID) ||
					!IsSafeProtocolToken(g_config.CoordinatorToken) ||
					g_config.CoordinatorHost.empty() || g_config.CoordinatorPort == 0 ||
					g_config.PollIntervalMs < 50 || g_config.PollIntervalMs > 5000 ||
					g_config.QueueTimeoutSeconds == 0 || g_config.QueueTimeoutSeconds > 3600 ||
					g_config.SocketTimeoutMs < 100 || g_config.SocketTimeoutMs > 10000)
				{
					throw std::runtime_error("invalid coordinator or timeout configuration");
				}
				if (g_config.Role == CrossServerRole::Lobby &&
					(g_config.WorkerIP.empty() || g_config.WorkerTcpPort == 0 || g_config.WorkerUdpPort == 0))
				{
					throw std::runtime_error("invalid worker endpoint");
				}
			}
			catch (const std::exception& exception)
			{
				g_config.Error = exception.what();
				g_config.Enabled = false;
				g_config.Role = CrossServerRole::Disabled;
			}
		});
	}

	bool SendAll(SOCKET socketHandle, const char* data, size_t length)
	{
		size_t offset = 0;
		while (offset < length)
		{
			const int sent = send(
				socketHandle,
				data + offset,
				static_cast<int>((std::min)(length - offset, static_cast<size_t>(INT_MAX))),
				0);
			if (sent <= 0)
				return false;
			offset += static_cast<size_t>(sent);
		}
		return true;
	}

	bool CoordinatorRequest(const std::string& command, std::string& response, std::string& error)
	{
		LoadConfigOnce();
		response.clear();
		error.clear();
		if (!g_config.Enabled)
		{
			error = g_config.Error.empty() ? "cross-server mode disabled" : g_config.Error;
			return false;
		}

		addrinfo hints{};
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		addrinfo* addresses = nullptr;
		const std::string port = std::to_string(g_config.CoordinatorPort);
		if (getaddrinfo(g_config.CoordinatorHost.c_str(), port.c_str(), &hints, &addresses) != 0)
		{
			error = "getaddrinfo failed";
			return false;
		}

		SOCKET socketHandle = INVALID_SOCKET;
		for (addrinfo* address = addresses; address; address = address->ai_next)
		{
			socketHandle = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
			if (socketHandle == INVALID_SOCKET)
				continue;
			const DWORD timeout = g_config.SocketTimeoutMs;
			setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
			setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
			if (connect(socketHandle, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0)
				break;
			closesocket(socketHandle);
			socketHandle = INVALID_SOCKET;
		}
		freeaddrinfo(addresses);
		if (socketHandle == INVALID_SOCKET)
		{
			error = "connect failed";
			return false;
		}

		const std::string wire = command + " token=" + g_config.CoordinatorToken + "\n";
		if (!SendAll(socketHandle, wire.data(), wire.size()))
		{
			error = "send failed";
			closesocket(socketHandle);
			return false;
		}

		std::array<char, 1024> buffer{};
		while (response.size() <= 8192)
		{
			const int received = recv(socketHandle, buffer.data(), static_cast<int>(buffer.size()), 0);
			if (received <= 0)
				break;
			response.append(buffer.data(), static_cast<size_t>(received));
			if (response.find('\n') != std::string::npos)
				break;
		}
		closesocket(socketHandle);
		if (response.size() > 8192)
		{
			error = "response too large";
			return false;
		}
		const size_t newline = response.find_first_of("\r\n");
		if (newline != std::string::npos)
			response.resize(newline);
		if (response.empty())
		{
			error = "empty response";
			return false;
		}
		return true;
	}

	bool ParseUnsigned(const std::string& text, UINT& value)
	{
		if (text.empty())
			return false;
		try
		{
			size_t consumed = 0;
			const unsigned long parsed = std::stoul(text, &consumed, 10);
			if (consumed != text.size() || parsed > UINT_MAX)
				return false;
			value = static_cast<UINT>(parsed);
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	bool ParseMatchResponse(const std::string& response, CrossServerMatchTicket& ticket, std::string& error)
	{
		ticket = CrossServerMatchTicket{};
		error.clear();
		std::istringstream stream(response);
		std::string status;
		stream >> status;
		if (status != "MATCH")
		{
			error = status.empty() ? "missing status" : status;
			return false;
		}

		std::map<std::string, std::string> fields;
		std::string token;
		while (stream >> token)
		{
			const size_t separator = token.find('=');
			if (separator == std::string::npos || separator == 0 || separator + 1 >= token.size())
			{
				error = "malformed match field";
				return false;
			}
			fields[token.substr(0, separator)] = token.substr(separator + 1);
		}

		UINT route = 0;
		if (!ParseUnsigned(fields["room"], ticket.RoomID) ||
			!ParseUnsigned(fields["map"], ticket.MapID) ||
			!ParseUnsigned(fields["key"], ticket.PVPKey) ||
			!ParseUnsigned(fields["seqh"], ticket.GameSeqHigh) ||
			!ParseUnsigned(fields["seql"], ticket.GameSeqLow) ||
			!ParseUnsigned(fields["context"], ticket.RaceEnvelopeContext) ||
			!ParseUnsigned(fields["route"], route) || route == 0 || route > 0x7FFF ||
			ticket.RoomID == 0 || ticket.RoomID > 0xFFFF || ticket.MapID == 0 ||
			ticket.PVPKey == 0 || ticket.GameSeqHigh == 0 || ticket.GameSeqLow == 0 ||
			ticket.RaceEnvelopeContext == 0)
		{
			error = "invalid match authority";
			return false;
		}
		ticket.GameRouteID = static_cast<USHORT>(route);

		const std::string players = fields["players"];
		std::istringstream playerStream(players);
		std::string playerToken;
		std::set<UINT> seenUins;
		std::set<UINT> seenSeats;
		while (std::getline(playerStream, playerToken, ','))
		{
			const size_t first = playerToken.find(':');
			const size_t second = first == std::string::npos ? std::string::npos : playerToken.find(':', first + 1);
			const size_t third = second == std::string::npos ? std::string::npos : playerToken.find(':', second + 1);
			if (first == std::string::npos || second == std::string::npos || third == std::string::npos)
			{
				error = "malformed player ticket";
				return false;
			}
			UINT uin = 0;
			UINT seat = 0;
			UINT pid = 0;
			if (!ParseUnsigned(playerToken.substr(0, first), uin) ||
				!ParseUnsigned(playerToken.substr(first + 1, second - first - 1), seat) ||
				!ParseUnsigned(playerToken.substr(second + 1, third - second - 1), pid) ||
				uin <= 10000 || seat >= ROOM_SEAT_NORMAL_MAX || pid == 0 ||
				!seenUins.insert(uin).second || !seenSeats.insert(seat).second)
			{
				error = "invalid player ticket";
				return false;
			}
			CrossServerPlayerTicket player;
			player.Uin = uin;
			player.SeatID = static_cast<BYTE>(seat);
			player.LoginClientPID = pid;
			player.InstanceID = playerToken.substr(third + 1);
			if (!IsSafeProtocolToken(player.InstanceID))
			{
				error = "invalid player instance";
				return false;
			}
			ticket.Players.push_back(player);
		}

		if (ticket.Players.size() < 2 || ticket.Players.size() > ROOM_SEAT_NORMAL_MAX)
		{
			error = "invalid player count";
			return false;
		}
		return true;
	}

	const CrossServerPlayerTicket* FindTicketPlayer(const CrossServerMatchTicket& ticket, UINT uin)
	{
		const auto found = std::find_if(ticket.Players.begin(), ticket.Players.end(), [uin](const CrossServerPlayerTicket& player) {
			return player.Uin == uin;
		});
		return found == ticket.Players.end() ? nullptr : &*found;
	}

	bool TicketsEqual(const CrossServerMatchTicket& left, const CrossServerMatchTicket& right)
	{
		if (left.RoomID != right.RoomID || left.MapID != right.MapID ||
			left.PVPKey != right.PVPKey || left.GameSeqHigh != right.GameSeqHigh ||
			left.GameSeqLow != right.GameSeqLow ||
			left.RaceEnvelopeContext != right.RaceEnvelopeContext ||
			left.GameRouteID != right.GameRouteID || left.Players.size() != right.Players.size())
		{
			return false;
		}
		for (const CrossServerPlayerTicket& player : left.Players)
		{
			const CrossServerPlayerTicket* other = FindTicketPlayer(right, player.Uin);
			if (!other || other->SeatID != player.SeatID ||
				other->LoginClientPID != player.LoginClientPID ||
				other->InstanceID != player.InstanceID)
				return false;
		}
		return true;
	}

	bool NotifyLobbyMatchResult(
		const std::shared_ptr<ClientNode>& Client,
		const CrossServerMatchTicket& ticket)
	{
		if (!Client || !CrossServerTestIsLobby())
			return false;
		IN_ADDR endpointAddress{};
		if (InetPtonA(AF_INET, g_config.WorkerIP.c_str(), &endpointAddress) != 1 ||
			endpointAddress.S_un.S_addr == INADDR_NONE || endpointAddress.S_un.S_addr == INADDR_ANY)
		{
			QQS_DebugPrintf10001("[CROSS][LOBBY-20081-FAIL] Uin=%u Reason=invalid-worker-ip\n", Client->Uin);
			return false;
		}

		std::vector<BYTE> body(256);
		BYTE* p = body.data();
		Write16(p, 0); // ResultID
		Write16(p, 0); // ReasonLen
		Write8(p, 0);  // border MatchQueueType
		Write8(p, 1);  // HasEnterInfo
		{
			BYTE* enterInfo = p;
			Write16(enterInfo, 0);
			{
				BYTE* addrInfo = enterInfo;
				Write16(addrInfo, 0);
				Write16(addrInfo, ticket.GameRouteID);
				Write32(addrInfo, endpointAddress.S_un.S_addr);
				Write8(addrInfo, 1);
				Write16(addrInfo, g_config.WorkerTcpPort);
				Write8(addrInfo, 1);
				Write16(addrInfo, g_config.WorkerUdpPort);
				Write8(addrInfo, 0);
				const size_t addrLength = static_cast<size_t>(addrInfo - enterInfo);
				Set16(enterInfo, static_cast<WORD>(addrLength));
				enterInfo += addrLength;
			}
			Write32(enterInfo, ticket.RoomID);
			Write16(enterInfo, 0);
			const size_t enterLength = static_cast<size_t>(enterInfo - p);
			Set16(p, static_cast<WORD>(enterLength));
			p += enterLength;
		}
		Write32(p, ticket.MapID);
		Write16(p, 1);
		Write16(p, 0);
		Write8(p, 1);
		Write32(p, ticket.PVPKey);
		Write32(p, ticket.GameSeqHigh);
		Write32(p, ticket.GameSeqLow);
		Write16(p, 0);
		Write16(p, 1);
		Write8(p, 4);
		for (int index = 0; index < 4; ++index)
			Write32(p, 0);

		const size_t length = static_cast<size_t>(p - body.data());
		const BOOL sent = SendToClient(
			Client, 20081, body.data(), static_cast<DWORD>(length),
			Client->ConnID, FE_PLAYER, 1, Notify);
		QQS_DebugPrintf10001(
			"[CROSS][LOBBY-20081] Instance=%s Uin=%u RoomID=%u MapID=%u Route=%u Endpoint=%s:%u Players=%zu Sent=%d\n",
			g_config.InstanceID.c_str(), Client->Uin, ticket.RoomID, ticket.MapID,
			static_cast<unsigned>(ticket.GameRouteID), g_config.WorkerIP.c_str(),
			static_cast<unsigned>(g_config.WorkerTcpPort), ticket.Players.size(), sent ? 1 : 0);
		return sent != FALSE;
	}

	bool IsLobbyQueueGenerationCurrent(UINT uin, unsigned long long generation)
	{
		std::lock_guard<std::mutex> lock(g_lobbyQueueMutex);
		const auto found = g_lobbyQueueGeneration.find(uin);
		return found != g_lobbyQueueGeneration.end() && found->second == generation;
	}

	void RunLobbyQueuePoll(
		std::weak_ptr<ClientNode> weakClient,
		UINT uin,
		UINT loginClientPID,
		UINT transportConnID,
		unsigned long long generation)
	{
		const auto deadline = std::chrono::steady_clock::now() +
			std::chrono::seconds(g_config.QueueTimeoutSeconds);
		bool firstRequest = true;
		while (std::chrono::steady_clock::now() < deadline)
		{
			if (!IsLobbyQueueGenerationCurrent(uin, generation))
				return;
			const std::shared_ptr<ClientNode> Client = weakClient.lock();
			if (!Client || Client->Uin != uin || Client->TransportConnID != transportConnID ||
				!Client->IsLogin || Client->deleteflag || Client->TaskID != 0xFFFD ||
				GetClient(uin) != Client)
			{
				return;
			}

			std::ostringstream command;
			if (firstRequest)
			{
				command << "QUEUE instance=" << g_config.InstanceID
					<< " uin=" << uin << " pid=" << loginClientPID;
			}
			else
			{
				command << "POLL instance=" << g_config.InstanceID << " uin=" << uin;
			}
			firstRequest = false;

			std::string response;
			std::string requestError;
			if (!CoordinatorRequest(command.str(), response, requestError))
			{
				QQS_DebugPrintf10001("[CROSS][LOBBY-QUEUE-ERROR] Instance=%s Uin=%u Error=%s\n",
					g_config.InstanceID.c_str(), uin, requestError.c_str());
				std::this_thread::sleep_for(std::chrono::milliseconds(g_config.PollIntervalMs));
				continue;
			}
			if (response == "WAIT" || response == "OK")
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(g_config.PollIntervalMs));
				continue;
			}

			CrossServerMatchTicket ticket;
			std::string parseError;
			if (!ParseMatchResponse(response, ticket, parseError))
			{
				QQS_DebugPrintf10001("[CROSS][LOBBY-QUEUE-REJECT] Instance=%s Uin=%u Response=%s\n",
					g_config.InstanceID.c_str(), uin, parseError.c_str());
				return;
			}
			const CrossServerPlayerTicket* player = FindTicketPlayer(ticket, uin);
			if (!player || player->LoginClientPID != loginClientPID)
			{
				QQS_DebugPrintf10001("[CROSS][LOBBY-QUEUE-REJECT] Instance=%s Uin=%u Reason=player-ticket-mismatch\n",
					g_config.InstanceID.c_str(), uin);
				return;
			}
			{
				std::lock_guard<std::mutex> lock(g_lobbyQueueMutex);
				const auto current = g_lobbyQueueGeneration.find(uin);
				if (current == g_lobbyQueueGeneration.end() || current->second != generation)
					return;
				g_lobbyActiveTickets[uin] = ticket;
			}
			if (!NotifyLobbyMatchResult(Client, ticket))
			{
				std::lock_guard<std::mutex> lock(g_lobbyQueueMutex);
				g_lobbyActiveTickets.erase(uin);
			}
			return;
		}

		QQS_DebugPrintf10001("[CROSS][LOBBY-QUEUE-TIMEOUT] Instance=%s Uin=%u Timeout=%u\n",
			g_config.InstanceID.c_str(), uin, g_config.QueueTimeoutSeconds);
	}

	RoomSeatMask ExpectedTicketMask(const CrossServerMatchTicket& ticket)
	{
		RoomSeatMask mask = 0;
		for (const CrossServerPlayerTicket& player : ticket.Players)
			mask = static_cast<RoomSeatMask>(mask | GetRoomSeatBit(player.SeatID));
		return mask;
	}

	bool InitializeWorkerRoom(
		WorkerRoomState& state,
		const CrossServerMatchTicket& ticket,
		std::shared_ptr<RoomNode>& room)
	{
		room = GetBorderRoom(ticket.RoomID);
		if (!room)
			room = NewBorderRoomWithID(ticket.RoomID);
		if (!room)
			return false;

		room->BaseGameMode = 1;
		room->SubGameMode = 1;
		room->SeatNum = ROOM_SEAT_NORMAL_MAX;
		room->matchQueueType = 0;
		room->startStatus = 2;
		room->MapID = ticket.MapID;
		room->ServerID = ticket.GameRouteID;
		room->RaceGameID = ticket.GameRouteID;
		room->RaceEnvelopeContext = ticket.RaceEnvelopeContext;
		room->RaceSeed = ticket.PVPKey;
		room->RaceMatchSeqHigh = ticket.GameSeqHigh;
		room->RaceMatchSeqLow = ticket.GameSeqLow;
		room->RaceAuthorityValid = true;
		state.Ticket = ticket;
		state.Room = room;
		return true;
	}

	void PrepareWorkerClientForRoom(
		const std::shared_ptr<ClientNode>& Client,
		const CrossServerPlayerTicket& player,
		const CrossServerMatchTicket& ticket,
		const std::shared_ptr<RoomNode>& room)
	{
		ResetNormalRaceWireState(Client);
		Client->Uin = player.Uin;
		Client->LoginClientPID = player.LoginClientPID;
		Client->ServerID = ticket.GameRouteID;
		Client->IsLogin = TRUE;
		Client->TaskID = 0xFFFD;
		Client->BaseGameMode = 1;
		Client->SubGameMode = 1;
		Client->GameID = ticket.GameRouteID;
		Client->SeatID = static_cast<char>(player.SeatID);
		Client->MapID = ticket.MapID;
		Client->BorderRoomID = ticket.RoomID;
		Client->CurrentRoom = room.get();
		Client->IsReady = TRUE;
		Client->ReadyState = QQS_ROOM_READY_STATE_READY;
		Client->IsFinish = FALSE;
		Client->FinishTime = 0;
		Client->Round = 0;
		Client->PassedCheckPoint = 0;
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
		Client->MapCompatibilityMode = false;
		Client->IsBanMap = false;
		Client->real = 1;
		const auto displayName = g_config.InstanceDisplayNames.find(player.InstanceID);
		if (displayName != g_config.InstanceDisplayNames.end())
		{
			const size_t originalLength = strnlen_s(Client->NickName, sizeof(Client->NickName));
			const std::string originalName(Client->NickName, originalLength);
			std::string crossServerName;
			crossServerName.reserve(sizeof(Client->NickName) - 1);
			AppendGBKWithoutSplitting(
				crossServerName, displayName->second + "-", sizeof(Client->NickName) - 1);
			AppendGBKWithoutSplitting(
				crossServerName, originalName, sizeof(Client->NickName) - 1);
			memset(Client->NickName, 0, sizeof(Client->NickName));
			memcpy(Client->NickName, crossServerName.data(), crossServerName.size());
		}
		Map* map = GetMap(ticket.MapID);
		if (map)
		{
			Client->EndCheckPoint = map->End;
			Client->TotalRound = map->Round;
			Client->Loop = map->Loop;
		}
	}

	bool SendWorkerHelloResponse(const std::shared_ptr<ClientNode>& Client)
	{
		BYTE result[2];
		BYTE* p = result;
		Write16(p, 0);
		return SendToClientWithRawWireRouteIDsAndDialog(
			Client, 703, result, static_cast<DWORD>(p - result),
			-1, 101, static_cast<short>(Client->ConnID), -1, -1, Response) != FALSE;
	}

	bool SendWorkerEnterResponse(const std::shared_ptr<ClientNode>& Client)
	{
		BYTE result[4];
		BYTE* p = result;
		Write16(p, 0); // ResultID
		Write8(p, 0);  // ReasonLen
		Write8(p, 0);  // HasMemoryRaceInfo
		return Client && SendToClientViaTransport(
			Client, Client, 20082, result, static_cast<DWORD>(p - result),
			static_cast<short>(Client->ConnID), FE_PLAYER,
			static_cast<short>(Client->ConnID), 0, Response) != FALSE;
	}

	bool CompleteWorkerEnter(
		const std::shared_ptr<ClientNode>& Client,
		UINT requestUin,
		UINT requestTime,
		UINT requestRoomID)
	{
		if (!Client || !Client->IsLogin || Client->Uin != requestUin ||
			Client->TaskID != 0xFFFD || Client->BorderRoomID != requestRoomID ||
			GetClient(requestUin) != Client)
		{
			return false;
		}

		std::shared_ptr<RoomNode> room;
		BYTE seat = 0xFF;
		{
			std::lock_guard<std::mutex> workerLock(g_workerMutex);
			const auto found = g_workerRooms.find(requestRoomID);
			if (found == g_workerRooms.end())
				return false;
			room = found->second.Room.lock();
			const CrossServerPlayerTicket* player = FindTicketPlayer(found->second.Ticket, requestUin);
			if (!room || !player || room->Player[player->SeatID] != Client)
				return false;
			seat = player->SeatID;
		}

		if (!SendWorkerEnterResponse(Client))
		{
			QQS_DebugPrintf10001("[CROSS][WORKER-20082-FAIL] Uin=%u RoomID=%u Reason=response-send\n",
				Client->Uin, requestRoomID);
			return false;
		}

		bool scheduleStart = false;
		bool duplicate = false;
		RoomSeatMask enteredMask = 0;
		RoomSeatMask expectedMask = 0;
		{
			std::lock_guard<std::mutex> workerLock(g_workerMutex);
			const auto found = g_workerRooms.find(requestRoomID);
			if (found == g_workerRooms.end())
				return false;
			WorkerRoomState& state = found->second;
			const RoomSeatMask seatBit = GetRoomSeatBit(seat);
			duplicate = (state.EnteredMask & seatBit) != 0;
			state.EnteredMask = static_cast<RoomSeatMask>(state.EnteredMask | seatBit);
			enteredMask = state.EnteredMask;
			expectedMask = ExpectedTicketMask(state.Ticket);
			if (state.SnapshotReady && !state.StartScheduled && expectedMask != 0 &&
				(state.EnteredMask & expectedMask) == expectedMask)
			{
				state.StartScheduled = true;
				scheduleStart = true;
			}
		}
		QQS_DebugPrintf10001(
			"[CROSS][WORKER-20082] Uin=%u Time=%u RoomID=%u Seat=%u Entered=0x%04X Expected=0x%04X Duplicate=%d Start=%d\n",
			Client->Uin, requestTime, requestRoomID, static_cast<unsigned>(seat),
			static_cast<unsigned>(enteredMask), static_cast<unsigned>(expectedMask),
			duplicate ? 1 : 0, scheduleStart ? 1 : 0);
		if (scheduleStart)
			CreateRoomTimer(room, 1000, OnNotifyPlayerInfo);
		return true;
	}

	void RunWorkerEnterPoll(
		std::weak_ptr<ClientNode> weakClient,
		UINT uin,
		UINT roomID,
		UINT pvpKey,
		UINT transportConnID)
	{
		const auto deadline = std::chrono::steady_clock::now() +
			std::chrono::seconds(g_config.QueueTimeoutSeconds);
		while (std::chrono::steady_clock::now() < deadline)
		{
			const std::shared_ptr<ClientNode> Client = weakClient.lock();
			if (!Client || Client->TransportConnID != transportConnID || Client->deleteflag ||
				Client->Uin != uin || Client->BorderRoomID != roomID || GetClient(uin) != Client)
			{
				return;
			}
			std::ostringstream command;
			command << "ENTERPOLL uin=" << uin << " key=" << pvpKey;
			std::string response;
			std::string error;
			if (CoordinatorRequest(command.str(), response, error))
			{
				if (response == "ENTERED")
				{
					CompleteWorkerEnter(Client, uin, 0, roomID);
					return;
				}
				if (response == "DENY")
					return;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(g_config.PollIntervalMs));
		}
	}
}

bool CrossServerTestEnabled()
{
	LoadConfigOnce();
	return g_config.Enabled;
}

bool CrossServerTestIsLobby()
{
	LoadConfigOnce();
	return g_config.Enabled && g_config.Role == CrossServerRole::Lobby;
}

bool CrossServerTestIsWorker()
{
	LoadConfigOnce();
	return g_config.Enabled && g_config.Role == CrossServerRole::Worker;
}

bool CrossServerTestValidateConfig(std::string& Error)
{
	LoadConfigOnce();
	Error = g_config.Error;
	return !g_config.Enabled || g_config.Error.empty();
}

bool CrossServerTestQueueBorderPlayer(const std::shared_ptr<ClientNode>& Client)
{
	if (!CrossServerTestIsLobby())
		return false;
	if (!Client || Client->Uin <= 10000 || Client->LoginClientPID == 0 ||
		!Client->IsLogin || Client->deleteflag || Client->TaskID != 0xFFFD)
	{
		QQS_DebugPrintf10001("[CROSS][LOBBY-QUEUE-FAIL] Instance=%s Uin=%u PID=%u Reason=invalid-client-state\n",
			g_config.InstanceID.c_str(), Client ? Client->Uin : 0,
			Client ? Client->LoginClientPID : 0);
		return true;
	}

	unsigned long long generation = 0;
	{
		std::lock_guard<std::mutex> lock(g_lobbyQueueMutex);
		generation = ++g_nextLobbyQueueGeneration;
		g_lobbyQueueGeneration[Client->Uin] = generation;
		g_lobbyEnteredTickets.erase(Client->Uin);
	}
	QQS_DebugPrintf10001("[CROSS][LOBBY-QUEUE] Instance=%s Uin=%u PID=%u Generation=%llu\n",
		g_config.InstanceID.c_str(), Client->Uin, Client->LoginClientPID, generation);
	std::thread(
		RunLobbyQueuePoll,
		std::weak_ptr<ClientNode>(Client),
		Client->Uin,
		Client->LoginClientPID,
		Client->TransportConnID,
		generation).detach();
	return true;
}

void CrossServerTestCancelBorderPlayer(const std::shared_ptr<ClientNode>& Client)
{
	if (!CrossServerTestIsLobby() || !Client || Client->Uin == 0 || Client->TaskID != 0xFFFD)
		return;
	const UINT uin = Client->Uin;
	bool handedOff = false;
	{
		std::lock_guard<std::mutex> lock(g_lobbyQueueMutex);
		g_lobbyQueueGeneration[uin] = ++g_nextLobbyQueueGeneration;
		g_lobbyActiveTickets.erase(uin);
		handedOff = g_lobbyEnteredTickets.erase(uin) != 0;
	}
	if (handedOff)
		return;
	std::thread([uin]() {
		std::ostringstream command;
		command << "CANCEL instance=" << g_config.InstanceID << " uin=" << uin;
		std::string response;
		std::string error;
		if (!CoordinatorRequest(command.str(), response, error))
		{
			QQS_DebugPrintf10001("[CROSS][LOBBY-CANCEL-ERROR] Instance=%s Uin=%u Error=%s\n",
				g_config.InstanceID.c_str(), uin, error.c_str());
		}
	}).detach();
}

bool CrossServerTestHandlePVPLogin(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	if (!CrossServerTestIsWorker())
		return false;
	constexpr size_t kWireBodyLen = sizeof(USHORT) + sizeof(UINT) * 3;
	if (!Client || !Body || BodyLen != kWireBodyLen || Client->TransportConnID == 0 ||
		!IsValidWireConnID16(Client->ConnID) || Client->ProtocolVersion == 0 || Client->deleteflag)
	{
		return true;
	}

	BYTE* p = Body;
	const UINT requestUin = Read32(p);
	const UINT requestTime = Read32(p);
	const UINT requestPVPKey = Read32(p);
	if (requestUin <= 10000 || Client->TransportConnIDUin != requestUin ||
		(Client->Uin != 0 && Client->Uin != requestUin) || requestPVPKey == 0)
	{
		QQS_DebugPrintf10001("[CROSS][WORKER-697-REJECT] Uin=%u Time=%u Reason=packet-identity\n",
			requestUin, requestTime);
		return true;
	}

	std::ostringstream command;
	command << "AUTH uin=" << requestUin << " key=" << requestPVPKey;
	std::string response;
	std::string requestError;
	if (!CoordinatorRequest(command.str(), response, requestError))
	{
		QQS_DebugPrintf10001("[CROSS][WORKER-697-REJECT] Uin=%u Time=%u Reason=coordinator Error=%s\n",
			requestUin, requestTime, requestError.c_str());
		return true;
	}
	CrossServerMatchTicket ticket;
	std::string parseError;
	if (!ParseMatchResponse(response, ticket, parseError) || ticket.PVPKey != requestPVPKey)
	{
		QQS_DebugPrintf10001("[CROSS][WORKER-697-REJECT] Uin=%u Time=%u Reason=%s\n",
			requestUin, requestTime, parseError.c_str());
		return true;
	}
	const CrossServerPlayerTicket* ticketPlayer = FindTicketPlayer(ticket, requestUin);
	if (!ticketPlayer || !GetMap(ticket.MapID) ||
		g_config.InstanceDisplayNames.find(ticketPlayer->InstanceID) == g_config.InstanceDisplayNames.end())
	{
		QQS_DebugPrintf10001("[CROSS][WORKER-697-REJECT] Uin=%u RoomID=%u MapID=%u Reason=ticket-player-or-map\n",
			requestUin, ticket.RoomID, ticket.MapID);
		return true;
	}

	Client->Uin = requestUin;
	if (!HydrateCrossServerWorkerClient(Client, ticketPlayer->LoginClientPID))
	{
		Client->Uin = 0;
		Client->IsLogin = FALSE;
		QQS_DebugPrintf10001("[CROSS][WORKER-697-REJECT] Uin=%u RoomID=%u Reason=hydrate-failed\n",
			requestUin, ticket.RoomID);
		return true;
	}

	std::shared_ptr<RoomNode> room;
	bool snapshotReady = false;
	{
		std::lock_guard<std::mutex> workerLock(g_workerMutex);
		WorkerRoomState& state = g_workerRooms[ticket.RoomID];
		if (!state.Ticket.Players.empty() && !TicketsEqual(state.Ticket, ticket))
		{
			QQS_DebugPrintf10001("[CROSS][WORKER-697-REJECT] Uin=%u RoomID=%u Reason=ticket-conflict\n",
				requestUin, ticket.RoomID);
			Client->Uin = 0;
			Client->IsLogin = FALSE;
			return true;
		}
		if (state.Ticket.Players.empty() && !InitializeWorkerRoom(state, ticket, room))
		{
			QQS_DebugPrintf10001("[CROSS][WORKER-697-REJECT] Uin=%u RoomID=%u Reason=room-create\n",
				requestUin, ticket.RoomID);
			Client->Uin = 0;
			Client->IsLogin = FALSE;
			return true;
		}
		if (!room)
			room = state.Room.lock();
		if (!room || ticketPlayer->SeatID >= room->Player.size())
		{
			Client->Uin = 0;
			Client->IsLogin = FALSE;
			return true;
		}

		{
			std::lock_guard<std::recursive_mutex> clientsLock(ClientMutex);
			const auto existing = Clients.find(requestUin);
			if (existing != Clients.end() && existing->second && existing->second != Client)
			{
				QQS_DebugPrintf10001("[CROSS][WORKER-697-REJECT] Uin=%u RoomID=%u Reason=duplicate-worker-session\n",
					requestUin, ticket.RoomID);
				Client->Uin = 0;
				Client->IsLogin = FALSE;
				return true;
			}
			Clients[requestUin] = Client;
		}

		const BYTE seat = ticketPlayer->SeatID;
		if (room->Player[seat] && room->Player[seat] != Client)
		{
			std::lock_guard<std::recursive_mutex> clientsLock(ClientMutex);
			if (Clients[requestUin] == Client)
				Clients.erase(requestUin);
			Client->Uin = 0;
			Client->IsLogin = FALSE;
			return true;
		}
		PrepareWorkerClientForRoom(Client, *ticketPlayer, ticket, room);
		if (!room->Player[seat])
		{
			room->Player[seat] = Client;
			room->SeatState[seat] = 0;
			room->CurrentPlayerNum++;
		}
		state.AuthenticatedMask = static_cast<RoomSeatMask>(
			state.AuthenticatedMask | GetRoomSeatBit(seat));

		const RoomSeatMask expectedMask = ExpectedTicketMask(ticket);
		if (!state.SnapshotReady && expectedMask != 0 &&
			(state.AuthenticatedMask & expectedMask) == expectedMask)
		{
			if (!CaptureRoomRaceStartSnapshot(room, "cross-worker-authenticated"))
			{
				QQS_DebugPrintf10001("[CROSS][WORKER-SNAPSHOT-FAIL] RoomID=%u AuthMask=0x%04X Expected=0x%04X\n",
					ticket.RoomID, static_cast<unsigned>(state.AuthenticatedMask),
					static_cast<unsigned>(expectedMask));
				return true;
			}
			// The lobby already published these values in Msg20081. Snapshot creation
			// generates the remaining opaque authority bytes, then these shared ticket
			// fields restore the cross-process identity visible to both clients.
			room->RaceSeed = ticket.PVPKey;
			room->RaceMatchSeqHigh = ticket.GameSeqHigh;
			room->RaceMatchSeqLow = ticket.GameSeqLow;
			room->RaceAuthorityValid = true;
			state.SnapshotReady = true;
		}
		snapshotReady = state.SnapshotReady;
	}

	const bool sent = ResponsePVPLogin(Client);
	QQS_DebugPrintf10001(
		"[CROSS][WORKER-697] Uin=%u RoomID=%u Seat=%u Route=%u Players=%zu SnapshotReady=%d Sent=%d\n",
		requestUin, ticket.RoomID, static_cast<unsigned>(ticketPlayer->SeatID),
		static_cast<unsigned>(ticket.GameRouteID), ticket.Players.size(),
		snapshotReady ? 1 : 0, sent ? 1 : 0);
	if (sent)
	{
		std::thread(
			RunWorkerEnterPoll,
			std::weak_ptr<ClientNode>(Client),
			requestUin,
			ticket.RoomID,
			ticket.PVPKey,
			Client->TransportConnID).detach();
	}
	return true;
}

bool CrossServerTestHandleLobbyEnterMatchRoom(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	if (!CrossServerTestIsLobby())
		return false;
	if (!Client || !Body)
		return false;

	CrossServerMatchTicket ticket;
	{
		std::lock_guard<std::mutex> lock(g_lobbyQueueMutex);
		const auto found = g_lobbyActiveTickets.find(Client->Uin);
		if (found == g_lobbyActiveTickets.end())
			return false;
		ticket = found->second;
	}

	constexpr size_t kWireBodyLen = sizeof(USHORT) + sizeof(UINT) * 3;
	if (BodyLen != kWireBodyLen || !Client->IsLogin || Client->deleteflag ||
		Client->TaskID != 0xFFFD || Client->LoginClientPID == 0)
	{
		QQS_DebugPrintf10001("[CROSS][LOBBY-20082-REJECT] Instance=%s Uin=%u Reason=client-state\n",
			g_config.InstanceID.c_str(), Client->Uin);
		return true;
	}

	BYTE* p = Body;
	const UINT requestUin = Read32(p);
	const UINT requestTime = Read32(p);
	const UINT requestRoomID = Read32(p);
	const CrossServerPlayerTicket* player = FindTicketPlayer(ticket, requestUin);
	if (requestUin != Client->Uin || requestRoomID != ticket.RoomID || !player ||
		player->LoginClientPID != Client->LoginClientPID || player->InstanceID != g_config.InstanceID)
	{
		QQS_DebugPrintf10001(
			"[CROSS][LOBBY-20082-REJECT] Instance=%s Uin=%u ReqUin=%u ReqRoomID=%u ActiveRoomID=%u Reason=ticket\n",
			g_config.InstanceID.c_str(), Client->Uin, requestUin, requestRoomID, ticket.RoomID);
		return true;
	}

	std::ostringstream command;
	command << "ENTER instance=" << g_config.InstanceID
		<< " uin=" << requestUin
		<< " room=" << requestRoomID
		<< " key=" << ticket.PVPKey;
	std::string response;
	std::string error;
	if (!CoordinatorRequest(command.str(), response, error) || response != "OK")
	{
		QQS_DebugPrintf10001(
			"[CROSS][LOBBY-20082-FAIL] Instance=%s Uin=%u RoomID=%u Time=%u Error=%s Response=%s\n",
			g_config.InstanceID.c_str(), requestUin, requestRoomID, requestTime,
			error.c_str(), response.c_str());
		return true;
	}
	{
		std::lock_guard<std::mutex> lock(g_lobbyQueueMutex);
		g_lobbyEnteredTickets.insert(requestUin);
	}
	QQS_DebugPrintf10001(
		"[CROSS][LOBBY-20082] Instance=%s Uin=%u RoomID=%u Time=%u Forwarded=1\n",
		g_config.InstanceID.c_str(), requestUin, requestRoomID, requestTime);
	return true;
}

bool CrossServerTestHandleEnterMatchRoom(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	if (!CrossServerTestIsWorker())
		return false;
	constexpr size_t kWireBodyLen = sizeof(USHORT) + sizeof(UINT) * 3;
	if (!Client || !Body || BodyLen != kWireBodyLen || !Client->IsLogin ||
		Client->TaskID != 0xFFFD || Client->BorderRoomID == 0)
	{
		return true;
	}

	BYTE* p = Body;
	const UINT requestUin = Read32(p);
	const UINT requestTime = Read32(p);
	const UINT requestRoomID = Read32(p);
	if (requestUin != Client->Uin || requestRoomID != Client->BorderRoomID)
	{
		QQS_DebugPrintf10001("[CROSS][WORKER-20082-REJECT] Uin=%u ReqUin=%u ReqRoomID=%u ActiveRoomID=%u\n",
			Client->Uin, requestUin, requestRoomID, Client->BorderRoomID);
		return true;
	}
	CompleteWorkerEnter(Client, requestUin, requestTime, requestRoomID);
	return true;
}

bool CrossServerTestHandlePVPHello(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen)
{
	(void)Body;
	if (!CrossServerTestIsWorker())
		return false;
	if (!Client || BodyLen != sizeof(USHORT) || !Client->IsLogin ||
		Client->TaskID != 0xFFFD || Client->BorderRoomID == 0 ||
		GetClient(Client->Uin) != Client || !GetBorderRoom(Client->BorderRoomID))
	{
		return true;
	}
	const bool sent = SendWorkerHelloResponse(Client);
	QQS_DebugPrintf10001("[CROSS][WORKER-703] Uin=%u RoomID=%u Sent=%d\n",
		Client->Uin, Client->BorderRoomID, sent ? 1 : 0);
	return true;
}

bool CrossServerTestIsExpectedWorkerPVPLoginPacket(
	const std::shared_ptr<ClientNode>& Client,
	const CSHEAD* ProtocolHead,
	const MSGHEAD* MsgHead,
	const BYTE* Body,
	USHORT WireBodyLen)
{
	constexpr char kPVPFrontEnd = 101;
	constexpr USHORT kWireBodyLen = static_cast<USHORT>(sizeof(USHORT) + sizeof(UINT) * 3);
	if (!CrossServerTestIsWorker() || !Client || !ProtocolHead || !MsgHead || !Body ||
		WireBodyLen != kWireBodyLen || ProtocolHead->BodyFlag == 0 ||
		ntohs(MsgHead->MsgType) != Request || MsgHead->DstFE != kPVPFrontEnd)
	{
		return false;
	}
	BYTE* p = const_cast<BYTE*>(Body);
	const UINT requestUin = Read32(p);
	(void)Read32(p);
	const UINT requestPVPKey = Read32(p);
	const UINT protocolUin = ntohl(ProtocolHead->Uin);
	return requestUin > 10000 && requestPVPKey != 0 && requestUin == protocolUin &&
		Client->TransportConnIDUin == requestUin;
}

bool CrossServerTestIsExpectedWorkerEnterRoomPacket(
	const std::shared_ptr<ClientNode>& Client,
	const CSHEAD* ProtocolHead,
	const MSGHEAD* MsgHead,
	const BYTE* Body,
	USHORT WireBodyLen)
{
	constexpr USHORT kWireBodyLen = static_cast<USHORT>(sizeof(USHORT) + sizeof(UINT) * 3);
	if (!CrossServerTestIsWorker() || !Client || !ProtocolHead || !MsgHead || !Body ||
		WireBodyLen != kWireBodyLen || ProtocolHead->BodyFlag == 0 ||
		ntohs(MsgHead->MsgType) != Request || MsgHead->DstFE != FE_PLAYER ||
		!Client->IsLogin || Client->TaskID != 0xFFFD)
	{
		return false;
	}
	BYTE* p = const_cast<BYTE*>(Body);
	const UINT requestUin = Read32(p);
	(void)Read32(p);
	const UINT requestRoomID = Read32(p);
	return requestUin == Client->Uin && requestUin == ntohl(ProtocolHead->Uin) &&
		requestRoomID == Client->BorderRoomID && GetClient(requestUin) == Client &&
		GetBorderRoom(requestRoomID) != nullptr;
}

bool CrossServerTestIsExpectedLobbyEnterRoomPacket(
	const std::shared_ptr<ClientNode>& Client,
	const CSHEAD* ProtocolHead,
	const MSGHEAD* MsgHead,
	const BYTE* Body,
	USHORT WireBodyLen)
{
	constexpr USHORT kWireBodyLen = static_cast<USHORT>(sizeof(USHORT) + sizeof(UINT) * 3);
	if (!CrossServerTestIsLobby() || !Client || !ProtocolHead || !MsgHead || !Body ||
		WireBodyLen != kWireBodyLen || ProtocolHead->BodyFlag == 0 ||
		ntohs(MsgHead->MsgType) != Request || MsgHead->DstFE != FE_PLAYER ||
		!Client->IsLogin || Client->deleteflag)
	{
		return false;
	}
	BYTE* p = const_cast<BYTE*>(Body);
	const UINT requestUin = Read32(p);
	(void)Read32(p);
	const UINT requestRoomID = Read32(p);
	if (requestUin != Client->Uin || requestUin != ntohl(ProtocolHead->Uin))
		return false;

	std::lock_guard<std::mutex> lock(g_lobbyQueueMutex);
	const auto found = g_lobbyActiveTickets.find(requestUin);
	if (found == g_lobbyActiveTickets.end() || found->second.RoomID != requestRoomID)
		return false;
	const CrossServerPlayerTicket* player = FindTicketPlayer(found->second, requestUin);
	return player && player->LoginClientPID == Client->LoginClientPID &&
		player->InstanceID == g_config.InstanceID;
}

bool CrossServerTestIsExpectedWorkerPVPHelloPacket(
	const std::shared_ptr<ClientNode>& Client,
	const CSHEAD* ProtocolHead,
	const MSGHEAD* MsgHead,
	USHORT WireBodyLen)
{
	if (!CrossServerTestIsWorker() || !Client || !ProtocolHead || !MsgHead ||
		WireBodyLen != sizeof(USHORT) || ProtocolHead->BodyFlag == 0 ||
		ntohs(MsgHead->MsgType) != Request || MsgHead->DstFE != FE_PLAYER ||
		!Client->IsLogin || Client->TaskID != 0xFFFD || Client->BorderRoomID == 0)
	{
		return false;
	}
	const UINT protocolUin = ntohl(ProtocolHead->Uin);
	return protocolUin == Client->Uin && GetClient(protocolUin) == Client &&
		GetBorderRoom(Client->BorderRoomID) != nullptr;
}
