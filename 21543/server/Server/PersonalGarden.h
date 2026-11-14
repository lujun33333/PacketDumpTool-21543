#pragma once

#include "Message.h"

class ChatRoomNode;

void PersonalGardenEnsureSchema();
bool PersonalGardenTryUseKey(
	const std::shared_ptr<ClientNode>& Client,
	UINT RequestUin);

void PersonalGardenAttachPendingLogin(const std::shared_ptr<ClientNode>& Client);

void PersonalGardenWriteBaseInfo(
	BYTE*& p,
	UINT Uin,
	const std::shared_ptr<ClientNode>& Client);
void PersonalGardenWriteOptionalBase(
	BYTE*& p,
	UINT Uin,
	const std::shared_ptr<ClientNode>& Client);

void PersonalGardenHandleRequest(
	const std::shared_ptr<ClientNode>& Client,
	USHORT MsgID,
	BYTE* Body,
	size_t BodyLen);

ChatRoomNode* PersonalGardenFindChatRoom(INT ChatRoomID);
bool PersonalGardenTryEnterOutdoor(
	const std::shared_ptr<ClientNode>& Client,
	BYTE* Body,
	size_t BodyLen);
bool PersonalGardenTryLeaveOutdoor(const std::shared_ptr<ClientNode>& Client);
