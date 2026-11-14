#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Message.h"

struct CrossServerPlayerTicket
{
	UINT Uin = 0;
	UINT LoginClientPID = 0;
	BYTE SeatID = 0;
	std::string InstanceID;
};

struct CrossServerMatchTicket
{
	UINT RoomID = 0;
	UINT MapID = 0;
	UINT PVPKey = 0;
	UINT GameSeqHigh = 0;
	UINT GameSeqLow = 0;
	UINT RaceEnvelopeContext = 0;
	USHORT GameRouteID = 0;
	std::vector<CrossServerPlayerTicket> Players;
};

bool CrossServerTestEnabled();
bool CrossServerTestIsLobby();
bool CrossServerTestIsWorker();
bool CrossServerTestValidateConfig(std::string& Error);

// Returns true when the request belongs to the cross-server lobby path. The
// caller must not also enqueue the client in the process-local border queue.
bool CrossServerTestQueueBorderPlayer(const std::shared_ptr<ClientNode>& Client);
void CrossServerTestCancelBorderPlayer(const std::shared_ptr<ClientNode>& Client);

// Worker handlers return true whenever the packet belongs to the worker role,
// including rejected packets, so callers never fall through to same-process
// ranked-PVP state.
bool CrossServerTestHandlePVPLogin(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen);
// Msg20082 stays on the lobby stream even after Msg697 authenticates the
// separate PVP connection. The lobby forwards this control event to Worker.
bool CrossServerTestHandleLobbyEnterMatchRoom(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen);
bool CrossServerTestHandleEnterMatchRoom(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen);
bool CrossServerTestHandlePVPHello(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen);

bool CrossServerTestIsExpectedWorkerPVPLoginPacket(
	const std::shared_ptr<ClientNode>& Client,
	const CSHEAD* ProtocolHead,
	const MSGHEAD* MsgHead,
	const BYTE* Body,
	USHORT WireBodyLen);
bool CrossServerTestIsExpectedWorkerEnterRoomPacket(
	const std::shared_ptr<ClientNode>& Client,
	const CSHEAD* ProtocolHead,
	const MSGHEAD* MsgHead,
	const BYTE* Body,
	USHORT WireBodyLen);
bool CrossServerTestIsExpectedLobbyEnterRoomPacket(
	const std::shared_ptr<ClientNode>& Client,
	const CSHEAD* ProtocolHead,
	const MSGHEAD* MsgHead,
	const BYTE* Body,
	USHORT WireBodyLen);
bool CrossServerTestIsExpectedWorkerPVPHelloPacket(
	const std::shared_ptr<ClientNode>& Client,
	const CSHEAD* ProtocolHead,
	const MSGHEAD* MsgHead,
	USHORT WireBodyLen);

// Implemented next to the normal login hydration code so the worker uses the
// same authoritative DB/equipment/refit loaders as an ordinary lobby login.
bool HydrateCrossServerWorkerClient(
	const std::shared_ptr<ClientNode>& Client,
	UINT LoginClientPID);
