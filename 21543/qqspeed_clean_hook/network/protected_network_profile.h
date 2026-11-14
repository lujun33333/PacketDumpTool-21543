#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(QQSPEED_VMP_PROTECTION)
namespace qqspeed::network
{
using ResolvedNetworkUnpackFn = int(__fastcall*)(
	const unsigned char*, unsigned int, const void*, const void*, void*, void*, unsigned char*, int*);
using ResolvedNetworkPackFn = int(__fastcall*)(
	unsigned char*, int*, unsigned int, unsigned int, unsigned char*, int, const void*, void*,
	unsigned char, unsigned short, short, char, char, short, short, const void*, int, int, char);
using ResolvedNetworkSerializeBodyFn = int(__fastcall*)(
	unsigned int, const void*, unsigned char*, unsigned short*);
using ResolvedNetworkSpecialDecodeFn = int(__fastcall*)(void*, const void*, unsigned char*, int*, void*);
using ResolvedFilterProtocolInfoFn = unsigned char(__fastcall*)(
	void*, void*, unsigned short*, unsigned char*, int);
using ResolvedNetworkInnerDispatchFn = int(__fastcall*)(
	short, const unsigned char*, int, void*, void*, void*, int);

inline constexpr std::size_t kNetworkUnpackCallSiteCount = 1;
inline constexpr std::size_t kNetworkPackCallSiteCount = 8;
inline constexpr std::size_t kNetworkFilterCallSiteCount = 1;
inline constexpr std::size_t kNetworkInnerDispatchCallSiteCount = 1;
inline constexpr std::size_t kNetworkSerializeCallSiteCount = 1;
inline constexpr std::size_t kNetworkSafeBuilderCallSiteCount = 1;
inline constexpr std::size_t kNetworkSpecialDecodeCallSiteCount = 1;

template <std::size_t Capacity>
struct ResolvedAddressList
{
	std::array<std::uintptr_t, Capacity> values{};
	std::size_t count = 0;
};

// This object contains resolved addresses only. It is created on the installer
// stack, consumed immediately, and securely cleared before the installer exits.
struct ResolvedNetworkProfile
{
	ResolvedNetworkUnpackFn unpackFunction = nullptr;
	ResolvedNetworkPackFn packFunction = nullptr;
	ResolvedNetworkSerializeBodyFn serializeBodyFunction = nullptr;
	void* safePacketBuilderFunction = nullptr;
	ResolvedNetworkSpecialDecodeFn specialDecodeFunction = nullptr;
	ResolvedNetworkInnerDispatchFn innerDispatchFunction = nullptr;
	ResolvedFilterProtocolInfoFn filterProtocolInfoFunction = nullptr;

	std::uintptr_t gameSocketOnMyReceiveUnpackCallAddress = 0;
	std::uintptr_t directInnerDispatchCallAddress = 0;
	std::uintptr_t socketCachedDstIdStateOffset = 0;
	std::uintptr_t unpackMsgGateTargetTableAddress = 0;
	std::uintptr_t unpackMsgGateIndexTableAddress = 0;
	std::uintptr_t unpackMsgGateReturn0Address = 0;

	ResolvedAddressList<kNetworkUnpackCallSiteCount> unpackCallSites;
	ResolvedAddressList<kNetworkPackCallSiteCount> packCallSites;
	ResolvedAddressList<kNetworkFilterCallSiteCount> filterProtocolInfoCallSites;
	ResolvedAddressList<kNetworkInnerDispatchCallSiteCount> innerDispatchCallSites;
	ResolvedAddressList<kNetworkSerializeCallSiteCount> serializeBodyCallSites;
	ResolvedAddressList<kNetworkSafeBuilderCallSiteCount> safePacketBuilderCallSites;
	ResolvedAddressList<kNetworkSpecialDecodeCallSiteCount> specialDecodeCallSites;
};

bool ResolveProtectedNetworkProfile(
	std::uintptr_t moduleBase,
	std::size_t imageSize,
	ResolvedNetworkProfile& profile);
void ClearResolvedNetworkProfile(ResolvedNetworkProfile& profile) noexcept;
}
#endif
