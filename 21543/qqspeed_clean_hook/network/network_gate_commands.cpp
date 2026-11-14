#include "pch.h"

#include "network/network_gate_commands.h"

namespace qqspeed::network
{
NetworkGateClass ClassifyNetworkGate(std::uint16_t msgId)
{
	if (msgId == 28346) {
		return NetworkGateClass::Special28346;
	}

	if (msgId == 551 || msgId == 21210) {
		return NetworkGateClass::StaticReturn0;
	}

	if (msgId >= 82 && msgId <= 223) {
		return NetworkGateClass::LookupGate;
	}

	return NetworkGateClass::None;
}

ExcludedRepairClass ClassifyExcludedRepair(std::uint16_t msgId)
{
	if (msgId == 100 || msgId == 223) {
		return ExcludedRepairClass::PlainExpand;
	}

	if (msgId == 551) {
		return ExcludedRepairClass::TopKartCompactFeed;
	}

	if (msgId == 21210 || (msgId >= 82 && msgId <= 99)) {
		return ExcludedRepairClass::CandidatePlainExpand;
	}

	return ExcludedRepairClass::None;
}

bool IsKnownNetworkExcluded(std::uint16_t msgId)
{
	return ClassifyExcludedRepair(msgId) != ExcludedRepairClass::None;
}

bool IsSafe28346PackCandidate(std::uint16_t msgId)
{
	return IsKnownNetworkExcluded(msgId);
}

const char* KnownNetworkExcludedIdsText()
{
	return "82-100,223,551,21210";
}

const char* ToString(NetworkGateClass gateClass)
{
	switch (gateClass) {
	case NetworkGateClass::None:
		return "None";
	case NetworkGateClass::LookupGate:
		return "LookupGate";
	case NetworkGateClass::StaticReturn0:
		return "StaticReturn0";
	case NetworkGateClass::Special28346:
		return "Special28346";
	default:
		return "Unknown";
	}
}

const char* ToString(ExcludedRepairClass repairClass)
{
	switch (repairClass) {
	case ExcludedRepairClass::None:
		return "None";
	case ExcludedRepairClass::PlainExpand:
		return "PlainExpand";
	case ExcludedRepairClass::TopKartCompactFeed:
		return "TopKartCompactFeed";
	case ExcludedRepairClass::CandidatePlainExpand:
		return "CandidatePlainExpand";
	default:
		return "Unknown";
	}
}
}

