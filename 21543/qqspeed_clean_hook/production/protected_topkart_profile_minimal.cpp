#include "pch.h"

#include "topkart/protected_topkart_profile.h"
#include "vmp_protection.h"

#include <array>
#include <cstring>

#if defined(QQSPEED_VMP_PROTECTION)
namespace qqspeed::topkart
{
namespace
{
constexpr std::size_t kProfileValueCount = 3;
constexpr std::size_t kProtectedWordCount = kProfileValueCount + 1;
constexpr std::size_t kProtectedByteCount =
	kProtectedWordCount * sizeof(std::uint32_t);
constexpr std::uint32_t kFNVOffset = 2166136261u;
constexpr std::uint32_t kFNVPrime = 16777619u;
constexpr std::uint32_t kDeltaSeed = 0xD1B54A35u;
constexpr std::uint32_t kMaskSeed = 0xC6EF3720u;
constexpr std::uint32_t kMaskStep = 0x9E3779B1u;
}

bool ResolveProtectedTopKartCoreProfile(
	std::size_t imageSize,
	ResolvedTopKartCoreProfile& profile)
{
	ClearResolvedTopKartCoreProfile(profile);
	bool valid =
		imageSize != 0 &&
		VMProtectIsProtected() &&
		VMProtectIsValidImageCRC();
	std::array<unsigned char, kProtectedByteCount> protectedBytes{};
	const char* protectedText =
		QQSPEED_VMP_DECRYPT_STRING_A("g7wAFC7RDCVRLxi/i69qpQ==");

	const std::size_t protectedTextLength = protectedText ? strlen(protectedText) : 0;
	std::size_t outputPos = 0;
	if (!protectedText || protectedTextLength == 0 ||
		(protectedTextLength & 3u) != 0) {
		valid = false;
	}

	for (std::size_t inputPos = 0;
		valid && inputPos < protectedTextLength;
		inputPos += 4) {
		int sextets[4] = {};
		int padding = 0;
		bool sawPadding = false;
		for (std::size_t j = 0; j < 4; ++j) {
			const unsigned char ch =
				static_cast<unsigned char>(protectedText[inputPos + j]);
			if (!sawPadding && ch >= 'A' && ch <= 'Z') {
				sextets[j] = ch - 'A';
			}
			else if (!sawPadding && ch >= 'a' && ch <= 'z') {
				sextets[j] = ch - 'a' + 26;
			}
			else if (!sawPadding && ch >= '0' && ch <= '9') {
				sextets[j] = ch - '0' + 52;
			}
			else if (!sawPadding && ch == '+') {
				sextets[j] = 62;
			}
			else if (!sawPadding && ch == '/') {
				sextets[j] = 63;
			}
			else if (ch == '=' &&
				inputPos + 4 == protectedTextLength &&
				j >= 2) {
				sextets[j] = 0;
				sawPadding = true;
				++padding;
			}
			else {
				valid = false;
				break;
			}
		}

		if (!valid || padding > 2 ||
			(padding != 0 && inputPos + 4 != protectedTextLength)) {
			valid = false;
			break;
		}

		const std::uint32_t packed =
			(static_cast<std::uint32_t>(sextets[0]) << 18) |
			(static_cast<std::uint32_t>(sextets[1]) << 12) |
			(static_cast<std::uint32_t>(sextets[2]) << 6) |
			static_cast<std::uint32_t>(sextets[3]);
		const std::size_t produced = 3u - static_cast<std::size_t>(padding);
		if (outputPos + produced > protectedBytes.size()) {
			valid = false;
			break;
		}
		if (produced > 0) {
			protectedBytes[outputPos++] =
				static_cast<unsigned char>((packed >> 16) & 0xFFu);
		}
		if (produced > 1) {
			protectedBytes[outputPos++] =
				static_cast<unsigned char>((packed >> 8) & 0xFFu);
		}
		if (produced > 2) {
			protectedBytes[outputPos++] =
				static_cast<unsigned char>(packed & 0xFFu);
		}
	}
	valid = valid && outputPos == protectedBytes.size();

	std::uint32_t checksum = kFNVOffset;
	std::uint32_t mixed = 0;
	std::uint32_t plain = 0;
	for (std::size_t index = 0;
		valid && index < kProtectedWordCount;
		++index) {
		const std::size_t offset = index * sizeof(std::uint32_t);
		mixed =
			static_cast<std::uint32_t>(protectedBytes[offset]) |
			(static_cast<std::uint32_t>(protectedBytes[offset + 1]) << 8) |
			(static_cast<std::uint32_t>(protectedBytes[offset + 2]) << 16) |
			(static_cast<std::uint32_t>(protectedBytes[offset + 3]) << 24);
		const std::uint32_t mask =
			kMaskSeed + static_cast<std::uint32_t>(index) * kMaskStep;
		const unsigned int rotation =
			static_cast<unsigned int>((index * 11u + 9u) & 31u);
		const std::uint32_t masked = mixed ^ mask;
		const std::uint32_t added = rotation == 0
			? masked
			: ((masked >> rotation) | (masked << (32u - rotation)));
		plain = added - kDeltaSeed * static_cast<std::uint32_t>(index + 1u);

		if (index == kProfileValueCount) {
			valid = plain == checksum;
			break;
		}
		for (unsigned int shift = 0; shift < 32; shift += 8) {
			checksum =
				(checksum ^ static_cast<unsigned char>((plain >> shift) & 0xFFu)) *
				kFNVPrime;
		}
		if (plain == 0 || plain >= imageSize) {
			valid = false;
			break;
		}
		switch (index) {
		case 0: profile.antiCheckerExecuteRva = plain; break;
		case 1: profile.antiCheckerExecuteSlotRva = plain; break;
		case 2: profile.aceInterfaceGlobalRva = plain; break;
		default: valid = false; break;
		}
	}

	if (!valid) {
		ClearResolvedTopKartCoreProfile(profile);
	}
	SecureZeroMemory(protectedBytes.data(), protectedBytes.size());
	SecureZeroMemory(&mixed, sizeof(mixed));
	SecureZeroMemory(&plain, sizeof(plain));
	SecureZeroMemory(&checksum, sizeof(checksum));
	QQSPEED_VMP_FREE_STRING(protectedText);
	return valid;
}

void ClearResolvedTopKartCoreProfile(
	ResolvedTopKartCoreProfile& profile) noexcept
{
	SecureZeroMemory(&profile, sizeof(profile));
}
}
#endif
