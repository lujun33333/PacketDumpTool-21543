#pragma once

#include <vector>

namespace qqspeed::network
{
bool IsSimulatedSerializerBodyMsg(unsigned short msgId);

bool BuildSimulatedSerializerBody(
	unsigned short msgId,
	const void* srcObject,
	unsigned int seq,
	std::vector<unsigned char>& out,
	int& outLen);
}
