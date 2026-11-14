#include "pch.h"

#include "network/connect_redirect.h"
#include "vmp_protection.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <ws2tcpip.h>

namespace
{
using MfcAsyncSocketConnectFn = int(__fastcall*)(
	void* socket,
	const char* hostAddress,
	unsigned int hostPort);

#pragma pack(push, 1)
struct SpeedHookConfig
{
	char host[64];
	USHORT port;
};
#pragma pack(pop)

static_assert(sizeof(SpeedHookConfig) == 0x42, "SpeedHookCfg layout must match launcher");

constexpr WORD kMfcCAsyncSocketConnectOrdinal = 2919;
constexpr unsigned short kLockedPortMask = 0x6D3Au;
constexpr unsigned short kLockedPortEncoded =
	static_cast<unsigned short>(28880u ^ kLockedPortMask);

MfcAsyncSocketConnectFn g_originalConnect = nullptr;
void** g_connectSlot = nullptr;
std::string g_targetHost;
unsigned short g_targetPort = 0;

int __fastcall HookedMfcAsyncSocketConnect(
	void* socket,
	const char* hostAddress,
	unsigned int hostPort);

bool ShouldRedirectPort(unsigned int port) noexcept
{
	return port >= 10000u && port <= 10005u;
}

__forceinline bool AppendDecimal(
	char*& cursor,
	const char* end,
	unsigned int value) noexcept
{
	char reversed[10] = {};
	std::size_t count = 0;
	do {
		reversed[count++] = static_cast<char>('0' + value % 10u);
		value /= 10u;
	} while (value != 0 && count < std::size(reversed));
	if (cursor + count >= end) {
		return false;
	}
	while (count > 0) {
		*cursor++ = reversed[--count];
	}
	return true;
}

__forceinline bool BuildLockedHost(char* host, std::size_t hostSize) noexcept
{
	if (!host || hostSize < INET_ADDRSTRLEN) {
		return false;
	}
	constexpr unsigned char kLockedHostOctets[] = { 127, 0, 0, 1 };
	char* cursor = host;
	const char* const end = host + hostSize;
	bool built = true;
	for (std::size_t index = 0; index < std::size(kLockedHostOctets); ++index) {
		if (index != 0) {
			if (cursor + 1 >= end) {
				built = false;
				break;
			}
			*cursor++ = '.';
		}
		if (!AppendDecimal(cursor, end, kLockedHostOctets[index])) {
			built = false;
			break;
		}
	}
	if (built) {
		*cursor = 0;
	}
	else {
		host[0] = 0;
	}
	return built;
}

bool BuildMappingName(wchar_t* name, std::size_t nameCount) noexcept
{
	constexpr wchar_t kPrefix[] = L"SpeedHookCfg_";
	if (!name || nameCount <= std::size(kPrefix)) {
		return false;
	}
	std::memcpy(name, kPrefix, sizeof(kPrefix) - sizeof(wchar_t));
	std::size_t cursor = std::size(kPrefix) - 1;
	wchar_t reversed[10] = {};
	std::size_t digitCount = 0;
	DWORD value = GetCurrentProcessId();
	do {
		reversed[digitCount++] = static_cast<wchar_t>(L'0' + value % 10u);
		value /= 10u;
	} while (value != 0 && digitCount < std::size(reversed));
	if (cursor + digitCount >= nameCount) {
		name[0] = 0;
		return false;
	}
	while (digitCount > 0) {
		name[cursor++] = reversed[--digitCount];
	}
	name[cursor] = 0;
	return true;
}

QQSPEED_VMP_NOINLINE bool AcceptRedirectTarget(
	const SpeedHookConfig& configured,
	std::string& acceptedHost,
	unsigned short& acceptedPort)
{
	QQSPEED_VMP_BEGIN_ULTRA("Prod.Connect.RedirectDecision");
	bool accepted = false;
#if QQSPEED_VMP_SERVER_LOCK_ENABLED
	char lockedHost[INET_ADDRSTRLEN] = {};
	const bool hostBuilt = BuildLockedHost(lockedHost, sizeof(lockedHost));
	const unsigned short lockedPort =
		static_cast<unsigned short>(kLockedPortEncoded ^ kLockedPortMask);
	accepted =
		VMProtectIsProtected() &&
		VMProtectIsValidImageCRC() &&
		hostBuilt &&
		configured.host[0] != 0 &&
		std::strcmp(configured.host, lockedHost) == 0 &&
		configured.port == lockedPort;
	if (accepted) {
		acceptedHost.assign(lockedHost);
		acceptedPort = lockedPort;
	}
	SecureZeroMemory(lockedHost, sizeof(lockedHost));
#else
	if (configured.host[0] != 0 && configured.port != 0) {
		acceptedHost.assign(configured.host);
		acceptedPort = configured.port;
		accepted = true;
	}
#endif
	QQSPEED_VMP_END();
	return accepted;
}

bool ReadRedirectTarget(std::string& host, unsigned short& port)
{
	wchar_t mappingName[64] = {};
	if (!BuildMappingName(mappingName, std::size(mappingName))) {
		return false;
	}

	HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName);
	if (!mapping) {
		return false;
	}

	auto* mapped = static_cast<const SpeedHookConfig*>(
		MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SpeedHookConfig)));
	if (!mapped) {
		CloseHandle(mapping);
		return false;
	}

	SpeedHookConfig local = {};
	std::memcpy(&local, mapped, sizeof(local));
	UnmapViewOfFile(mapped);
	CloseHandle(mapping);
	local.host[sizeof(local.host) - 1] = 0;

	const bool accepted = AcceptRedirectTarget(local, host, port);
	SecureZeroMemory(&local, sizeof(local));
	return accepted;
}

void LoadRedirectTarget()
{
	std::string host;
	unsigned short port = 0;
	if (ReadRedirectTarget(host, port)) {
		g_targetHost.swap(host);
		g_targetPort = port;
		return;
	}
	g_targetHost.clear();
	g_targetPort = 0;
}

bool EnsureRedirectTargetAvailable()
{
	if (g_targetHost.empty() || g_targetPort == 0) {
		LoadRedirectTarget();
	}
	return !g_targetHost.empty() && g_targetPort != 0;
}

IMAGE_NT_HEADERS* GetNtHeaders(HMODULE module)
{
	if (!module) {
		return nullptr;
	}
	auto* base = reinterpret_cast<unsigned char*>(module);
	auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
		return nullptr;
	}
	auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

template <typename T>
T* RvaToPtr(HMODULE module, DWORD imageSize, DWORD rva)
{
	if (!module || rva == 0 || rva >= imageSize || sizeof(T) > imageSize - rva) {
		return nullptr;
	}
	return reinterpret_cast<T*>(reinterpret_cast<unsigned char*>(module) + rva);
}

void** FindImportSlotByOrdinal(
	HMODULE module,
	const char* importedModule,
	WORD ordinal)
{
	IMAGE_NT_HEADERS* nt = GetNtHeaders(module);
	if (!nt) {
		return nullptr;
	}

	const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
	const IMAGE_DATA_DIRECTORY& importDirectory =
		nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (importDirectory.VirtualAddress == 0 ||
		importDirectory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
		return nullptr;
	}

	auto* descriptors = RvaToPtr<IMAGE_IMPORT_DESCRIPTOR>(
		module,
		imageSize,
		importDirectory.VirtualAddress);
	if (!descriptors) {
		return nullptr;
	}

	const DWORD descriptorLimit =
		importDirectory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
	for (DWORD descriptorIndex = 0;
		descriptorIndex < descriptorLimit;
		++descriptorIndex) {
		const IMAGE_IMPORT_DESCRIPTOR& descriptor = descriptors[descriptorIndex];
		if (descriptor.Name == 0 && descriptor.FirstThunk == 0) {
			break;
		}

		const char* moduleName =
			RvaToPtr<char>(module, imageSize, descriptor.Name);
		if (!moduleName || _stricmp(moduleName, importedModule) != 0) {
			continue;
		}

		const DWORD lookupRva = descriptor.OriginalFirstThunk != 0
			? descriptor.OriginalFirstThunk
			: descriptor.FirstThunk;
		const DWORD iatRva = descriptor.FirstThunk;
		constexpr DWORD kMaxThunkCount = 4096;
		for (DWORD thunkIndex = 0; thunkIndex < kMaxThunkCount; ++thunkIndex) {
			const DWORD lookupEntryRva =
				lookupRva + thunkIndex * static_cast<DWORD>(sizeof(IMAGE_THUNK_DATA));
			const DWORD iatEntryRva =
				iatRva + thunkIndex * static_cast<DWORD>(sizeof(IMAGE_THUNK_DATA));
			auto* lookupThunk = RvaToPtr<IMAGE_THUNK_DATA>(
				module,
				imageSize,
				lookupEntryRva);
			auto* iatThunk = RvaToPtr<IMAGE_THUNK_DATA>(
				module,
				imageSize,
				iatEntryRva);
			if (!lookupThunk || !iatThunk || lookupThunk->u1.AddressOfData == 0) {
				break;
			}

			const auto ordinalValue = static_cast<ULONGLONG>(lookupThunk->u1.Ordinal);
			if ((ordinalValue & IMAGE_ORDINAL_FLAG64) != 0 &&
				static_cast<WORD>(ordinalValue & 0xFFFFu) == ordinal) {
				return reinterpret_cast<void**>(&iatThunk->u1.Function);
			}
		}
	}
	return nullptr;
}

void** FindMfcConnectSlot(HMODULE networkModule)
{
	void** slot = FindImportSlotByOrdinal(
		networkModule,
		"mfc140.dll",
		kMfcCAsyncSocketConnectOrdinal);
	return slot ? slot : FindImportSlotByOrdinal(
		networkModule,
		"mfc140u.dll",
		kMfcCAsyncSocketConnectOrdinal);
}

bool PatchMfcConnectSlot(HMODULE networkModule)
{
	void** slot = FindMfcConnectSlot(networkModule);
	if (!slot) {
		return false;
	}

	void* previous = nullptr;
	__try {
		previous = *slot;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	void* const hook = reinterpret_cast<void*>(&HookedMfcAsyncSocketConnect);
	if (previous == hook) {
		g_connectSlot = slot;
		return g_originalConnect != nullptr;
	}
	if (!previous || previous == reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0))) {
		return false;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		return false;
	}
	g_originalConnect = reinterpret_cast<MfcAsyncSocketConnectFn>(previous);
	*slot = hook;
	FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
	DWORD ignoredProtect = 0;
	const BOOL protectionRestored =
		VirtualProtect(slot, sizeof(*slot), oldProtect, &ignoredProtect);
	if (!protectionRestored) {
		InterlockedCompareExchangePointer(
			reinterpret_cast<PVOID volatile*>(slot),
			previous,
			hook);
		DWORD rollbackProtect = 0;
		VirtualProtect(slot, sizeof(*slot), oldProtect, &rollbackProtect);
		g_originalConnect = nullptr;
		return false;
	}

	g_connectSlot = slot;
	return true;
}

bool RestoreMfcConnectSlot()
{
	void** slot = g_connectSlot;
	void* const original = reinterpret_cast<void*>(g_originalConnect);
	void* const hook = reinterpret_cast<void*>(&HookedMfcAsyncSocketConnect);
	if (!slot || !original) {
		return true;
	}

	void* current = nullptr;
	__try {
		current = *slot;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
	if (current == original) {
		g_connectSlot = nullptr;
		return true;
	}
	if (current != hook) {
		return false;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		return false;
	}
	void* const previous = InterlockedCompareExchangePointer(
		reinterpret_cast<PVOID volatile*>(slot),
		original,
		hook);
	FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
	DWORD ignoredProtect = 0;
	const BOOL protectionRestored =
		VirtualProtect(slot, sizeof(*slot), oldProtect, &ignoredProtect);
	const bool restored =
		(previous == hook || previous == original) &&
		*slot == original &&
		protectionRestored != FALSE;
	if (restored) {
		g_connectSlot = nullptr;
	}
	return restored;
}

int __fastcall HookedMfcAsyncSocketConnect(
	void* socket,
	const char* hostAddress,
	unsigned int hostPort)
{
	MfcAsyncSocketConnectFn original = g_originalConnect;
	if (!original) {
		return 0;
	}
	if (!ShouldRedirectPort(hostPort)) {
		return original(socket, hostAddress, hostPort);
	}
	if (EnsureRedirectTargetAvailable()) {
		const int result = original(
			socket,
			g_targetHost.c_str(),
			static_cast<unsigned int>(g_targetPort));
		if (result == 0) {
			const int error = WSAGetLastError();
			WSASetLastError(error);
		}
		return result;
	}
#if QQSPEED_VMP_SERVER_LOCK_ENABLED
	WSASetLastError(WSAEACCES);
	return 0;
#else
	return original(socket, hostAddress, hostPort);
#endif
}
}

namespace qqspeed::network
{
bool InstallConnectRedirectHook(HMODULE networkModule)
{
	LoadRedirectTarget();
	return PatchMfcConnectSlot(networkModule);
}

bool UninstallConnectRedirectHook()
{
	return RestoreMfcConnectSlot();
}
}
