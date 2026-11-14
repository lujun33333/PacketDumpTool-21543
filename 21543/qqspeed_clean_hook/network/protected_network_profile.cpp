#include "pch.h"

#include "network/protected_network_profile.h"
#include "vmp_protection.h"

#include <array>
#include <cstring>
#include <limits>

#if defined(QQSPEED_VMP_PROTECTION)
namespace qqspeed::network
{
namespace
{
constexpr std::size_t kProfileValueCount = 27;
constexpr std::size_t kProtectedWordCount = kProfileValueCount + 1;
constexpr std::size_t kProtectedByteCount = kProtectedWordCount * sizeof(std::uint32_t);
constexpr std::uint32_t kFNVOffset = 2166136261u;
constexpr std::uint32_t kFNVPrime = 16777619u;
constexpr std::uint32_t kDeltaSeed = 0x9E3779B9u;
constexpr std::uint32_t kMaskSeed = 0xA5C3F17Du;
constexpr std::uint32_t kMaskStep = 0x7F4A7C15u;
}

QQSPEED_VMP_NOINLINE bool ResolveProtectedNetworkProfile(
	std::uintptr_t moduleBase,
	std::size_t imageSize,
	ResolvedNetworkProfile& profile)
{
	QQSPEED_VMP_BEGIN_ULTRA("Login.NetworkProfile.Resolve");
	ClearResolvedNetworkProfile(profile);
	bool valid =
		moduleBase != 0 &&
		imageSize != 0 &&
		VMProtectIsProtected() &&
		VMProtectIsValidImageCRC();
	std::array<unsigned char, kProtectedByteCount> protectedBytes{};
	const char* protectedText = QQSPEED_VMP_DECRYPT_STRING_A(
		"Tu7oaFhOoS4ZPwbK6S9HsutGdYxTi4xMGnCNrm0osbVvdxNyGOMU/CvUrj9K7eZ1Z8RT/SXuyrsBnnkrVY5SeGyP+6ZTYrSrUpzia37iXVe8VmIgQRl5HRqXt40VHCZMBr0cAcgS63xR1tT2FmLzbg==");

	std::size_t protectedTextLength = protectedText ? strlen(protectedText) : 0;
	std::size_t outputPos = 0;
	if (!protectedText || protectedTextLength == 0 || (protectedTextLength & 3u) != 0) {
		valid = false;
	}

	for (std::size_t inputPos = 0; valid && inputPos < protectedTextLength; inputPos += 4) {
		int sextets[4] = {};
		int padding = 0;
		bool sawPadding = false;
		for (std::size_t j = 0; j < 4; ++j) {
			const unsigned char ch = static_cast<unsigned char>(protectedText[inputPos + j]);
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
			else if (ch == '=' && inputPos + 4 == protectedTextLength && j >= 2) {
				sextets[j] = 0;
				sawPadding = true;
				++padding;
			}
			else {
				valid = false;
				break;
			}
		}

		if (!valid || padding > 2 || (padding != 0 && inputPos + 4 != protectedTextLength)) {
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
		if (produced > 0) protectedBytes[outputPos++] = static_cast<unsigned char>((packed >> 16) & 0xFFu);
		if (produced > 1) protectedBytes[outputPos++] = static_cast<unsigned char>((packed >> 8) & 0xFFu);
		if (produced > 2) protectedBytes[outputPos++] = static_cast<unsigned char>(packed & 0xFFu);
	}
	valid = valid && outputPos == protectedBytes.size();

	std::uint32_t checksum = kFNVOffset;
	std::uint32_t mixed = 0;
	std::uint32_t plain = 0;
	for (std::size_t index = 0; valid && index < kProtectedWordCount; ++index) {
		const std::size_t offset = index * sizeof(std::uint32_t);
		mixed =
			static_cast<std::uint32_t>(protectedBytes[offset]) |
			(static_cast<std::uint32_t>(protectedBytes[offset + 1]) << 8) |
			(static_cast<std::uint32_t>(protectedBytes[offset + 2]) << 16) |
			(static_cast<std::uint32_t>(protectedBytes[offset + 3]) << 24);
		const std::uint32_t mask = kMaskSeed + static_cast<std::uint32_t>(index) * kMaskStep;
		const unsigned int rotation = static_cast<unsigned int>((index * 7u + 5u) & 31u);
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
			checksum = (checksum ^ static_cast<unsigned char>((plain >> shift) & 0xFFu)) * kFNVPrime;
		}

		const bool isObjectOffset = index == 9;
		if (plain == 0 || plain >= imageSize ||
			(!isObjectOffset && moduleBase > (std::numeric_limits<std::uintptr_t>::max)() - plain)) {
			valid = false;
			break;
		}

		const std::uintptr_t resolved = moduleBase + static_cast<std::uintptr_t>(plain);
		switch (index) {
		case 0: profile.unpackFunction = reinterpret_cast<ResolvedNetworkUnpackFn>(resolved); break;
		case 1: profile.packFunction = reinterpret_cast<ResolvedNetworkPackFn>(resolved); break;
		case 2: profile.serializeBodyFunction = reinterpret_cast<ResolvedNetworkSerializeBodyFn>(resolved); break;
		case 3: profile.safePacketBuilderFunction = reinterpret_cast<void*>(resolved); break;
		case 4: profile.specialDecodeFunction = reinterpret_cast<ResolvedNetworkSpecialDecodeFn>(resolved); break;
		case 5: profile.innerDispatchFunction = reinterpret_cast<ResolvedNetworkInnerDispatchFn>(resolved); break;
		case 6: profile.filterProtocolInfoFunction = reinterpret_cast<ResolvedFilterProtocolInfoFn>(resolved); break;
		case 7: profile.gameSocketOnMyReceiveUnpackCallAddress = resolved; break;
		case 8: profile.directInnerDispatchCallAddress = resolved; break;
		case 9: profile.socketCachedDstIdStateOffset = plain; break;
		case 10: profile.unpackMsgGateTargetTableAddress = resolved; break;
		case 11: profile.unpackMsgGateIndexTableAddress = resolved; break;
		case 12: profile.unpackMsgGateReturn0Address = resolved; break;
		case 13: profile.unpackCallSites.values[0] = resolved; break;
		case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21:
			profile.packCallSites.values[index - 14] = resolved;
			break;
		case 22: profile.filterProtocolInfoCallSites.values[0] = resolved; break;
		case 23: profile.innerDispatchCallSites.values[0] = resolved; break;
		case 24: profile.serializeBodyCallSites.values[0] = resolved; break;
		case 25: profile.safePacketBuilderCallSites.values[0] = resolved; break;
		case 26: profile.specialDecodeCallSites.values[0] = resolved; break;
		default: valid = false; break;
		}
	}

	if (valid) {
		profile.unpackCallSites.count = kNetworkUnpackCallSiteCount;
		profile.packCallSites.count = kNetworkPackCallSiteCount;
		profile.filterProtocolInfoCallSites.count = kNetworkFilterCallSiteCount;
		profile.innerDispatchCallSites.count = kNetworkInnerDispatchCallSiteCount;
		profile.serializeBodyCallSites.count = kNetworkSerializeCallSiteCount;
		profile.safePacketBuilderCallSites.count = kNetworkSafeBuilderCallSiteCount;
		profile.specialDecodeCallSites.count = kNetworkSpecialDecodeCallSiteCount;
	}
	else {
		ClearResolvedNetworkProfile(profile);
	}

	SecureZeroMemory(protectedBytes.data(), protectedBytes.size());
	SecureZeroMemory(&mixed, sizeof(mixed));
	SecureZeroMemory(&plain, sizeof(plain));
	SecureZeroMemory(&checksum, sizeof(checksum));
	QQSPEED_VMP_FREE_STRING(protectedText);
	QQSPEED_VMP_END();
	return valid;
}

void ClearResolvedNetworkProfile(ResolvedNetworkProfile& profile) noexcept
{
	SecureZeroMemory(&profile, sizeof(profile));
}
}
#endif
