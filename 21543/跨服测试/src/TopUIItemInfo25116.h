#pragma once

#include "Message.h"
#include <vector>

// MsgID 25116 / CNotifyTopUIItemInfo.
// Reads .\Config\TopUIItemInfo25116.yml and encodes the listed rows in order.
std::vector<BYTE> BuildTopUIItemInfo25116Payload(DWORD uin);
void NotifyTopUIItemInfoNew(std::shared_ptr<ClientNode> Client);
