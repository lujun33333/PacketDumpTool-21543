#include "RelayBuffTrace.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace relay_buff_trace
{
namespace
{

bool IsTruthyEnvironmentValue(const char* name)
{
	char value[32] = {};
	const DWORD length = GetEnvironmentVariableA(name, value, static_cast<DWORD>(sizeof(value)));
	if (length == 0 || length >= sizeof(value))
	{
		return false;
	}

	std::string normalized(value, value + length);
	std::transform(
		normalized.begin(),
		normalized.end(),
		normalized.begin(),
		[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return normalized == "1" || normalized == "true" || normalized == "on" || normalized == "yes";
}

std::string ResolveOutputPath()
{
	char configured[MAX_PATH * 4] = {};
	const DWORD length = GetEnvironmentVariableA(
		"QQS_TRACE_153_BUFF_FILE",
		configured,
		static_cast<DWORD>(sizeof(configured)));
	if (length > 0 && length < sizeof(configured))
	{
		return std::string(configured, configured + length);
	}

	CreateDirectoryA("Logs", nullptr);
	char fallback[MAX_PATH] = {};
	std::snprintf(
		fallback,
		sizeof(fallback),
		"Logs\\transfer_buff_trace_%lu.jsonl",
		static_cast<unsigned long>(GetCurrentProcessId()));
	return fallback;
}

class TraceState final
{
public:
	TraceState()
	{
		if (!IsTruthyEnvironmentValue("QQS_TRACE_153_BUFF"))
		{
			return;
		}

		path = ResolveOutputPath();
		file = CreateFileA(
			path.c_str(),
			FILE_APPEND_DATA,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			nullptr,
			OPEN_ALWAYS,
			FILE_ATTRIBUTE_NORMAL,
			nullptr);
		if (file == INVALID_HANDLE_VALUE)
		{
			return;
		}

		if (BCryptOpenAlgorithmProvider(
				&algorithm,
				BCRYPT_SHA256_ALGORITHM,
				nullptr,
				0) < 0)
		{
			algorithm = nullptr;
		}
		else
		{
			ULONG resultSize = 0;
			if (BCryptGetProperty(
					algorithm,
					BCRYPT_OBJECT_LENGTH,
					reinterpret_cast<PUCHAR>(&hashObjectSize),
					sizeof(hashObjectSize),
					&resultSize,
					0) < 0 ||
				hashObjectSize == 0)
			{
				BCryptCloseAlgorithmProvider(algorithm, 0);
				algorithm = nullptr;
				hashObjectSize = 0;
			}
		}

		enabled = true;
		std::ostringstream start;
		start << "{\"event\":\"trace_start\",\"pid\":" << GetCurrentProcessId()
			  << ",\"tick_ms\":" << GetTickCount64()
			  << ",\"sha256_ready\":" << (algorithm ? "true" : "false")
			  << "}\r\n";
		Write(start.str());
	}

	~TraceState()
	{
		if (algorithm)
		{
			BCryptCloseAlgorithmProvider(algorithm, 0);
		}
		if (file != INVALID_HANDLE_VALUE)
		{
			CloseHandle(file);
		}
	}

	TraceState(const TraceState&) = delete;
	TraceState& operator=(const TraceState&) = delete;

	bool Fingerprint(
		const std::uint8_t* data,
		std::size_t length,
		std::array<std::uint8_t, 32>& digest) noexcept
	{
		digest.fill(0);
		if (!algorithm || hashObjectSize == 0 ||
			(!data && length != 0) ||
			length > std::numeric_limits<ULONG>::max())
		{
			return false;
		}

		try
		{
			std::vector<std::uint8_t> hashObject(hashObjectSize);
			BCRYPT_HASH_HANDLE hash = nullptr;
			if (BCryptCreateHash(
					algorithm,
					&hash,
					hashObject.data(),
					hashObjectSize,
					nullptr,
					0,
					0) < 0)
			{
				return false;
			}

			NTSTATUS status = 0;
			if (length != 0)
			{
				status = BCryptHashData(
					hash,
					const_cast<PUCHAR>(data),
					static_cast<ULONG>(length),
					0);
			}
			if (status >= 0)
			{
				status = BCryptFinishHash(
					hash,
					digest.data(),
					static_cast<ULONG>(digest.size()),
					0);
			}
			BCryptDestroyHash(hash);
			SecureZeroMemory(hashObject.data(), hashObject.size());
			return status >= 0;
		}
		catch (...)
		{
			digest.fill(0);
			return false;
		}
	}

	void Write(const std::string& line) noexcept
	{
		if (file == INVALID_HANDLE_VALUE || line.empty())
		{
			return;
		}

		try
		{
			std::lock_guard<std::mutex> lock(writeMutex);
			DWORD written = 0;
			WriteFile(
				file,
				line.data(),
				static_cast<DWORD>(std::min<std::size_t>(
					line.size(),
					std::numeric_limits<DWORD>::max())),
				&written,
				nullptr);
		}
		catch (...)
		{
			// Diagnostics must never alter packet handling.
		}
	}

	bool enabled = false;
	std::string path;
	HANDLE file = INVALID_HANDLE_VALUE;
	BCRYPT_ALG_HANDLE algorithm = nullptr;
	ULONG hashObjectSize = 0;
	std::mutex writeMutex;
};

TraceState& State()
{
	static TraceState state;
	return state;
}

std::string HexDigest(const std::uint8_t* buff, std::size_t buffLen)
{
	std::array<std::uint8_t, 32> digest = {};
	if (!State().Fingerprint(buff, buffLen, digest))
	{
		return "ERROR";
	}

	std::ostringstream hex;
	hex << std::uppercase << std::hex << std::setfill('0');
	for (const std::uint8_t value : digest)
	{
		hex << std::setw(2) << static_cast<unsigned int>(value);
	}
	return hex.str();
}

} // namespace

bool IsEnabled() noexcept
{
	return State().enabled;
}

void RecordReject(
	const char* reason,
	std::uint32_t connectionUin,
	std::size_t bodyLen) noexcept
{
	if (!IsEnabled())
	{
		return;
	}
	try
	{
		std::ostringstream line;
		line << "{\"event\":\"msg153_reject\",\"tick_ms\":" << GetTickCount64()
			 << ",\"reason\":\"" << (reason ? reason : "unknown")
			 << "\",\"connection_uin\":" << connectionUin
			 << ",\"body_len\":" << bodyLen
			 << "}\r\n";
		State().Write(line.str());
	}
	catch (...)
	{
	}
}

void RecordParsed153(
	std::uint32_t srcUin,
	std::uint8_t ver,
	std::uint32_t seq,
	std::uint8_t dstCount,
	const std::uint8_t* buff,
	std::size_t buffLen,
	std::size_t requestExtensionLen) noexcept
{
	if (!IsEnabled())
	{
		return;
	}
	try
	{
		const std::string digest = HexDigest(buff, buffLen);
		std::ostringstream line;
		line << "{\"event\":\"msg153_parsed\",\"tick_ms\":" << GetTickCount64()
			 << ",\"src_uin\":" << srcUin
			 << ",\"ver\":" << static_cast<unsigned int>(ver)
			 << ",\"seq\":" << seq
			 << ",\"dst_count\":" << static_cast<unsigned int>(dstCount)
			 << ",\"buff_len\":" << buffLen
			 << ",\"buff_sha256\":\"" << digest
			 << "\",\"request_extension_len\":" << requestExtensionLen
			 << "}\r\n";
		State().Write(line.str());
	}
	catch (...)
	{
	}
}

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
	bool sendOk) noexcept
{
	if (!IsEnabled())
	{
		return;
	}
	try
	{
		const std::string digest = HexDigest(buff, buffLen);
		std::ostringstream line;
		line << "{\"event\":\"msg560_forward\",\"tick_ms\":" << GetTickCount64()
			 << ",\"outcome\":\"" << (outcome ? outcome : "unknown")
			 << "\",\"src_uin\":" << srcUin
			 << ",\"src_player_id\":" << srcPlayerId
			 << ",\"dst_uin\":" << dstUin
			 << ",\"requested_dst_player_id\":" << requestedDstPlayerId
			 << ",\"ver\":" << static_cast<unsigned int>(ver)
			 << ",\"seq\":" << seq
			 << ",\"buff_len\":" << buffLen
			 << ",\"buff_sha256\":\"" << digest
			 << "\",\"send_ok\":" << (sendOk ? "true" : "false")
			 << "}\r\n";
		State().Write(line.str());
	}
	catch (...)
	{
	}
}

} // namespace relay_buff_trace
