#pragma once

#include <cstdint>

namespace qqspeed::network
{
enum class NetworkGateClass
{
	None,
	LookupGate,
	StaticReturn0,
	Special28346,
};

enum class ExcludedRepairClass
{
	None,
	PlainExpand,
	TopKartCompactFeed,
	CandidatePlainExpand,
};

NetworkGateClass ClassifyNetworkGate(std::uint16_t msgId);
ExcludedRepairClass ClassifyExcludedRepair(std::uint16_t msgId);
bool IsKnownNetworkExcluded(std::uint16_t msgId);
bool IsSafe28346PackCandidate(std::uint16_t msgId);
const char* KnownNetworkExcludedIdsText();
const char* ToString(NetworkGateClass gateClass);
const char* ToString(ExcludedRepairClass repairClass);
}

