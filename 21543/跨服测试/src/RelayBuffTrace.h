#pragma once

#include <cstddef>
#include <cstdint>

namespace relay_buff_trace
{

// Disabled unless QQS_TRACE_153_BUFF is set to 1/true/on/yes before startup.
// The optional QQS_TRACE_153_BUFF_FILE variable overrides the JSONL output path.
bool IsEnabled() noexcept;

void RecordReject(
	const char* reason,
	std::uint32_t connectionUin,
	std::size_t bodyLen) noexcept;

void RecordParsed153(
	std::uint32_t srcUin,
	std::uint8_t ver,
	std::uint32_t seq,
	std::uint8_t dstCount,
	const std::uint8_t* buff,
	std::size_t buffLen,
	std::size_t requestExtensionLen) noexcept;

void RecordForward560(
	const char* outcome,
	std::uint32_t srcUin,
	std::uint16_t srcPlayerId,
	std::uint32_t dstUin,
	std::uint16_t requestedDstPlayerId,
	std::uint8_t ver,
	std::uint32_t seq,
	const std::uint8_t* buff,
	std::size_t buffLen,
	bool sendOk) noexcept;

} // namespace relay_buff_trace
