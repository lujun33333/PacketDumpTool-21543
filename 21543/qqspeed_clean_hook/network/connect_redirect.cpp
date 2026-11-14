#include "pch.h"

#include "network/connect_redirect.h"

#include "logging/hook_log.h"
#include "vmp_protection.h"

#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <string>
#include <vector>
#include <TlHelp32.h>
#include <ws2tcpip.h>

namespace
{
using ConnectFn = int(WSAAPI*)(SOCKET socket, const sockaddr* name, int nameLength);
using WSAConnectFn = int(WSAAPI*)(
	SOCKET socket,
	const sockaddr* name,
	int nameLength,
	LPWSABUF callerData,
	LPWSABUF calleeData,
	LPQOS socketQos,
	LPQOS groupQos);
using SendToFn = int(WSAAPI*)(
	SOCKET socket,
	const char* buffer,
	int length,
	int flags,
	const sockaddr* to,
	int toLength);
using WSASendToFn = int(WSAAPI*)(
	SOCKET socket,
	LPWSABUF buffers,
	DWORD bufferCount,
	LPDWORD numberOfBytesSent,
	DWORD flags,
	const sockaddr* to,
	int toLength,
	LPWSAOVERLAPPED overlapped,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE completionRoutine);
using GetAddrInfoFn = int(WSAAPI*)(
	PCSTR nodeName,
	PCSTR serviceName,
	const ADDRINFOA* hints,
	PADDRINFOA* result);
using GetAddrInfoWFn = int(WSAAPI*)(
	PCWSTR nodeName,
	PCWSTR serviceName,
	const ADDRINFOW* hints,
	PADDRINFOW* result);
using HtonsFn = u_short(WSAAPI*)(u_short hostshort);
using InetAddrFn = unsigned long(WSAAPI*)(const char* cp);
using MfcAsyncSocketConnectFn = int(__fastcall*)(void* socket, const char* hostAddress, unsigned int hostPort);

#pragma pack(push, 1)
struct SpeedHookConfig
{
	char Host[64];
	USHORT Port;
};
#pragma pack(pop)

static_assert(sizeof(SpeedHookConfig) == 0x42, "SpeedHookCfg layout must match launcher");

enum class Ws2Api
{
	Connect,
	WSAConnect,
	SendTo,
	WSASendTo,
	GetAddrInfoAApi,
	GetAddrInfoWApi,
	Htons,
	InetAddr,
};

struct ApiPatchSpec
{
	Ws2Api api;
	const char* name;
	WORD ordinal;
	void* target;
	void* hook;
};

struct Ipv4Endpoint
{
	char ip[INET_ADDRSTRLEN] = {};
	unsigned short port = 0;
};

ConnectFn g_originalConnect = nullptr;
WSAConnectFn g_originalWSAConnect = nullptr;
SendToFn g_originalSendTo = nullptr;
WSASendToFn g_originalWSASendTo = nullptr;
GetAddrInfoFn g_originalGetAddrInfo = nullptr;
GetAddrInfoWFn g_originalGetAddrInfoW = nullptr;
HtonsFn g_originalHtons = nullptr;
InetAddrFn g_originalInetAddr = nullptr;
MfcAsyncSocketConnectFn g_originalMfcAsyncSocketConnect = nullptr;
void** g_mfcAsyncSocketConnectSlot = nullptr;
std::string g_targetHost;
unsigned short g_targetPort = 0;
HMODULE g_selfModule = nullptr;
volatile LONG g_sendToNonCandidateLogs = 0;
volatile LONG g_wsaSendToNonCandidateLogs = 0;

constexpr WORD kWs2ConnectOrdinal = 4;
constexpr WORD kWs2WSAConnectOrdinal = 46;
constexpr WORD kWs2SendToOrdinal = 20;
constexpr WORD kWs2WSASendToOrdinal = 99;
constexpr WORD kWs2GetAddrInfoOrdinal = 177;
constexpr WORD kWs2GetAddrInfoWOrdinal = 31;
constexpr WORD kWs2HtonsOrdinal = 9;
constexpr WORD kWs2InetAddrOrdinal = 11;
constexpr WORD kMfcCAsyncSocketConnectOrdinal = 2919;
constexpr LONG kMaxDatagramNonCandidateLogs = 64;
constexpr unsigned short kLockedPortMask = 0x6D3Au;
constexpr unsigned short kLockedPortEncoded = static_cast<unsigned short>(28880u ^ kLockedPortMask);

int WSAAPI HookedConnect(SOCKET socket, const sockaddr* name, int nameLength);
int WSAAPI HookedWSAConnect(
	SOCKET socket,
	const sockaddr* name,
	int nameLength,
	LPWSABUF callerData,
	LPWSABUF calleeData,
	LPQOS socketQos,
	LPQOS groupQos);
int WSAAPI HookedSendTo(
	SOCKET socket,
	const char* buffer,
	int length,
	int flags,
	const sockaddr* to,
	int toLength);
int WSAAPI HookedWSASendTo(
	SOCKET socket,
	LPWSABUF buffers,
	DWORD bufferCount,
	LPDWORD numberOfBytesSent,
	DWORD flags,
	const sockaddr* to,
	int toLength,
	LPWSAOVERLAPPED overlapped,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE completionRoutine);
int WSAAPI HookedGetAddrInfo(
	PCSTR nodeName,
	PCSTR serviceName,
	const ADDRINFOA* hints,
	PADDRINFOA* result);
int WSAAPI HookedGetAddrInfoW(
	PCWSTR nodeName,
	PCWSTR serviceName,
	const ADDRINFOW* hints,
	PADDRINFOW* result);
u_short WSAAPI HookedHtons(u_short hostshort);
unsigned long WSAAPI HookedInetAddr(const char* cp);
int __fastcall HookedMfcAsyncSocketConnect(void* socket, const char* hostAddress, unsigned int hostPort);

bool ShouldRedirectPort(unsigned short port)
{
	return port >= 10000 && port <= 10005;
}

bool ShouldRedirectAddressString(const char* address)
{
	return address &&
		(std::strcmp(address, "113.96.13.205") == 0 ||
		 std::strcmp(address, "121.14.75.60") == 0 ||
		 std::strcmp(address, "120.232.19.139") == 0 ||
		 std::strcmp(address, "157.255.13.55") == 0);
}

bool TryCopyString(char* destination, std::size_t destinationSize, const char* source)
{
	if (!destination || destinationSize == 0) {
		return false;
	}

	destination[0] = 0;
	if (!source) {
		return false;
	}

	__try
	{
		strncpy_s(destination, destinationSize, source, _TRUNCATE);
		return destination[0] != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		destination[0] = 0;
		return false;
	}
}

QQSPEED_VMP_NOINLINE bool DecodeLockedRedirectTarget(
	char* host,
	std::size_t hostSize,
	unsigned short& port)
{
	QQSPEED_VMP_BEGIN_ULTRA("Login.Redirect.Lock.Decode");
	bool decoded = false;
	if (host && hostSize >= INET_ADDRSTRLEN) {
		constexpr unsigned char kLockedHostOctets[] = { 127, 0, 0, 1 };
		const int written = sprintf_s(
			host,
			hostSize,
			"%u.%u.%u.%u",
			static_cast<unsigned int>(kLockedHostOctets[0]),
			static_cast<unsigned int>(kLockedHostOctets[1]),
			static_cast<unsigned int>(kLockedHostOctets[2]),
			static_cast<unsigned int>(kLockedHostOctets[3]));
		port = static_cast<unsigned short>(kLockedPortEncoded ^ kLockedPortMask);
		decoded = written > 0 && static_cast<std::size_t>(written) < hostSize && port != 0;
	}
	QQSPEED_VMP_END();
	return decoded;
}

QQSPEED_VMP_NOINLINE bool VerifyProtectedRedirectRuntime()
{
	QQSPEED_VMP_BEGIN_ULTRA("Login.Redirect.Lock.VerifyVmp");
	bool valid = false;
#if QQSPEED_VMP_SERVER_LOCK_ENABLED
	valid = VMProtectIsProtected() && VMProtectIsValidImageCRC();
#endif
	QQSPEED_VMP_END();
	return valid;
}

QQSPEED_VMP_NOINLINE bool ValidateProtectedRedirectTarget(
	const char* configuredHost,
	unsigned short configuredPort,
	std::string& acceptedHost,
	unsigned short& acceptedPort)
{
	QQSPEED_VMP_BEGIN_ULTRA("Login.Redirect.Lock.ValidateConfig");
	bool valid = false;
#if QQSPEED_VMP_SERVER_LOCK_ENABLED
	char lockedHost[INET_ADDRSTRLEN] = {};
	unsigned short lockedPort = 0;
	if (VerifyProtectedRedirectRuntime() &&
		DecodeLockedRedirectTarget(lockedHost, sizeof(lockedHost), lockedPort) &&
		configuredHost &&
		std::strcmp(configuredHost, lockedHost) == 0 &&
		configuredPort == lockedPort) {
		acceptedHost.assign(lockedHost);
		acceptedPort = lockedPort;
		valid = true;
	}
	SecureZeroMemory(lockedHost, sizeof(lockedHost));
#else
	(void)configuredHost;
	(void)configuredPort;
	(void)acceptedHost;
	(void)acceptedPort;
#endif
	QQSPEED_VMP_END();
	return valid;
}

QQSPEED_VMP_NOINLINE bool RejectProtectedRedirect(const char* apiName)
{
	QQSPEED_VMP_BEGIN_ULTRA("Login.Redirect.Lock.Decide");
	bool rejected = false;
#if QQSPEED_VMP_SERVER_LOCK_ENABLED
	WSASetLastError(WSAEACCES);
	rejected = true;
#else
	(void)apiName;
#endif
	QQSPEED_VMP_END();
	if (rejected) {
		qqspeed::logging::Logf("%s redirect denied stage=endpoint_lock error=%d", apiName, WSAEACCES);
	}
	return rejected;
}

bool ReadTargetFromSharedConfig(std::string& host, unsigned short& port)
{
	wchar_t mapName[64] = {};
	swprintf_s(mapName, L"SpeedHookCfg_%lu", GetCurrentProcessId());

	HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, mapName);
	if (!mapping) {
		qqspeed::logging::Logf("SpeedHookCfg open failed error=%lu", GetLastError());
		return false;
	}

	SpeedHookConfig* config = static_cast<SpeedHookConfig*>(
		MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(SpeedHookConfig)));
	if (!config) {
		qqspeed::logging::Logf("SpeedHookCfg view failed error=%lu", GetLastError());
		CloseHandle(mapping);
		return false;
	}

	SpeedHookConfig local = {};
	memcpy(&local, config, sizeof(local));
	UnmapViewOfFile(config);
	CloseHandle(mapping);

	local.Host[sizeof(local.Host) - 1] = 0;
	if (local.Host[0] == 0 || local.Port == 0) {
		qqspeed::logging::Logf("SpeedHookCfg invalid host='%s' port=%u", local.Host, local.Port);
		return false;
	}

	host = local.Host;
	port = local.Port;
	return true;
}

QQSPEED_VMP_NOINLINE void LoadRedirectTarget()
{
	QQSPEED_VMP_BEGIN_ULTRA("Login.Redirect.LoadTarget");
	std::string host;
	unsigned short port = 0;
	if (!ReadTargetFromSharedConfig(host, port)) {
		g_targetHost.clear();
		g_targetPort = 0;
		qqspeed::logging::Logf("socket redirect disabled: missing SpeedHookCfg");
	}
	else {
#if QQSPEED_VMP_SERVER_LOCK_ENABLED
		std::string acceptedHost;
		unsigned short acceptedPort = 0;
		if (!ValidateProtectedRedirectTarget(host.c_str(), port, acceptedHost, acceptedPort)) {
			g_targetHost.clear();
			g_targetPort = 0;
			qqspeed::logging::Logf("socket redirect disabled: protected endpoint validation failed");
		}
		else {
			g_targetHost = acceptedHost;
			g_targetPort = acceptedPort;
			qqspeed::logging::Logf("socket redirect protected target ready");
		}
#else
		g_targetHost = host;
		g_targetPort = port;
		qqspeed::logging::Logf("socket redirect target=%s:%u", g_targetHost.c_str(), g_targetPort);
#endif
	}
	QQSPEED_VMP_END();
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
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		return nullptr;
	}
	return nt;
}

DWORD GetImageSize(HMODULE module)
{
	IMAGE_NT_HEADERS* nt = GetNtHeaders(module);
	return nt ? nt->OptionalHeader.SizeOfImage : 0;
}

std::string GetModuleBaseName(HMODULE module)
{
	if (!module) {
		return "unknown";
	}

	wchar_t path[MAX_PATH] = {};
	const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
	if (length == 0) {
		return "path_failed";
	}

	const wchar_t* name = path;
	for (const wchar_t* cursor = path; *cursor; ++cursor) {
		if (*cursor == L'\\' || *cursor == L'/') {
			name = cursor + 1;
		}
	}

	char narrow[MAX_PATH] = {};
	WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, static_cast<int>(sizeof(narrow)), nullptr, nullptr);
	return narrow[0] ? narrow : "convert_failed";
}

bool QueryModuleForAddress(void* address, HMODULE& module, std::uintptr_t& rva, std::string& moduleName)
{
	module = nullptr;
	rva = 0;
	moduleName = "unknown";
	if (!address) {
		return false;
	}

	MEMORY_BASIC_INFORMATION mbi = {};
	if (VirtualQuery(address, &mbi, sizeof(mbi)) == 0 || !mbi.AllocationBase) {
		return false;
	}

	module = static_cast<HMODULE>(mbi.AllocationBase);
	const DWORD imageSize = GetImageSize(module);
	const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
	const std::uintptr_t value = reinterpret_cast<std::uintptr_t>(address);
	if (imageSize != 0 && value >= base && value < base + imageSize) {
		rva = value - base;
	}
	moduleName = GetModuleBaseName(module);
	return true;
}

void ResolveCaller(void* returnAddress, std::string& callerName, std::uintptr_t& callerRva)
{
	HMODULE callerModule = nullptr;
	callerRva = 0;
	callerName.clear();
	QueryModuleForAddress(returnAddress, callerModule, callerRva, callerName);
	if (callerName.empty()) {
		callerName = "unknown";
	}
}

std::vector<HMODULE> ListLoadedModules()
{
	std::vector<HMODULE> modules;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
	if (snapshot == INVALID_HANDLE_VALUE) {
		qqspeed::logging::Logf("socket iat module enum failed error=%lu", GetLastError());
		return modules;
	}

	MODULEENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (Module32FirstW(snapshot, &entry)) {
		do {
			modules.push_back(entry.hModule);
			entry.dwSize = sizeof(entry);
		} while (Module32NextW(snapshot, &entry));
	}
	else {
		qqspeed::logging::Logf("socket iat module enum first failed error=%lu", GetLastError());
	}

	CloseHandle(snapshot);
	return modules;
}

template <typename T>
T* RvaToPtr(HMODULE module, DWORD imageSize, DWORD rva)
{
	if (!module || rva == 0 || rva >= imageSize || sizeof(T) > imageSize - rva) {
		return nullptr;
	}
	return reinterpret_cast<T*>(reinterpret_cast<unsigned char*>(module) + rva);
}

bool IsWs2ModuleName(const char* moduleName)
{
	return moduleName &&
		(_stricmp(moduleName, "WS2_32.dll") == 0 || _stricmp(moduleName, "WS2_32") == 0);
}

void* GetOriginalAddress(Ws2Api api)
{
	switch (api) {
	case Ws2Api::Connect:
		return reinterpret_cast<void*>(g_originalConnect);
	case Ws2Api::WSAConnect:
		return reinterpret_cast<void*>(g_originalWSAConnect);
	case Ws2Api::SendTo:
		return reinterpret_cast<void*>(g_originalSendTo);
	case Ws2Api::WSASendTo:
		return reinterpret_cast<void*>(g_originalWSASendTo);
	case Ws2Api::GetAddrInfoAApi:
		return reinterpret_cast<void*>(g_originalGetAddrInfo);
	case Ws2Api::GetAddrInfoWApi:
		return reinterpret_cast<void*>(g_originalGetAddrInfoW);
	case Ws2Api::Htons:
		return reinterpret_cast<void*>(g_originalHtons);
	case Ws2Api::InetAddr:
		return reinterpret_cast<void*>(g_originalInetAddr);
	default:
		return nullptr;
	}
}

void SetOriginalAddress(Ws2Api api, void* address)
{
	switch (api) {
	case Ws2Api::Connect:
		if (!g_originalConnect) {
			g_originalConnect = reinterpret_cast<ConnectFn>(address);
		}
		break;
	case Ws2Api::WSAConnect:
		if (!g_originalWSAConnect) {
			g_originalWSAConnect = reinterpret_cast<WSAConnectFn>(address);
		}
		break;
	case Ws2Api::SendTo:
		if (!g_originalSendTo) {
			g_originalSendTo = reinterpret_cast<SendToFn>(address);
		}
		break;
	case Ws2Api::WSASendTo:
		if (!g_originalWSASendTo) {
			g_originalWSASendTo = reinterpret_cast<WSASendToFn>(address);
		}
		break;
	case Ws2Api::GetAddrInfoAApi:
		if (!g_originalGetAddrInfo) {
			g_originalGetAddrInfo = reinterpret_cast<GetAddrInfoFn>(address);
		}
		break;
	case Ws2Api::GetAddrInfoWApi:
		if (!g_originalGetAddrInfoW) {
			g_originalGetAddrInfoW = reinterpret_cast<GetAddrInfoWFn>(address);
		}
		break;
	case Ws2Api::Htons:
		if (!g_originalHtons) {
			g_originalHtons = reinterpret_cast<HtonsFn>(address);
		}
		break;
	case Ws2Api::InetAddr:
		if (!g_originalInetAddr) {
			g_originalInetAddr = reinterpret_cast<InetAddrFn>(address);
		}
		break;
	default:
		break;
	}
}

bool IsApiImport(
	HMODULE module,
	DWORD imageSize,
	const IMAGE_THUNK_DATA& lookupThunk,
	void* currentValue,
	const ApiPatchSpec& spec)
{
	if (!spec.target) {
		return false;
	}

	const auto ordinalValue = static_cast<ULONGLONG>(lookupThunk.u1.Ordinal);
	if ((ordinalValue & IMAGE_ORDINAL_FLAG64) != 0) {
		const WORD ordinal = static_cast<WORD>(ordinalValue & 0xFFFFu);
		return ordinal == spec.ordinal || currentValue == spec.target;
	}

	const DWORD nameRva = static_cast<DWORD>(lookupThunk.u1.AddressOfData);
	IMAGE_IMPORT_BY_NAME* importName = RvaToPtr<IMAGE_IMPORT_BY_NAME>(module, imageSize, nameRva);
	if (importName && std::strcmp(reinterpret_cast<const char*>(importName->Name), spec.name) == 0) {
		return true;
	}

	return currentValue != nullptr && currentValue == spec.target;
}

bool PatchWs2IatSlot(HMODULE ownerModule, void** slot, const ApiPatchSpec& spec)
{
	if (!slot || !spec.target || !spec.hook) {
		return false;
	}

	void* const previous = *slot;
	if (previous == spec.hook) {
		SetOriginalAddress(spec.api, spec.target);
		qqspeed::logging::Logf(
			"socket iat patch skipped: already patched api=%s owner=%s slot=%p original=%p",
			spec.name,
			GetModuleBaseName(ownerModule).c_str(),
			slot,
			GetOriginalAddress(spec.api));
		return true;
	}

	if (previous != spec.target) {
		qqspeed::logging::Logf(
			"socket iat patch skipped: unexpected slot api=%s owner=%s slot=%p current=%p target=%p",
			spec.name,
			GetModuleBaseName(ownerModule).c_str(),
			slot,
			previous,
			spec.target);
		return false;
	}

	SetOriginalAddress(spec.api, previous ? previous : spec.target);

	DWORD oldProtect = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		qqspeed::logging::Logf("socket iat patch VirtualProtect failed api=%s slot=%p error=%lu", spec.name, slot, GetLastError());
		return false;
	}

	*slot = spec.hook;
	FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));

	DWORD ignoredProtect = 0;
	VirtualProtect(slot, sizeof(*slot), oldProtect, &ignoredProtect);

	qqspeed::logging::Logf(
		"socket iat patch api=%s owner=%s slot=%p previous=%p hook=%p original=%p oldProtect=0x%lX",
		spec.name,
		GetModuleBaseName(ownerModule).c_str(),
		slot,
		previous,
		spec.hook,
		GetOriginalAddress(spec.api),
		oldProtect);
	return true;
}

std::size_t PatchModuleWs2Imports(HMODULE module, const ApiPatchSpec* specs, std::size_t specCount)
{
	IMAGE_NT_HEADERS* nt = GetNtHeaders(module);
	if (!nt) {
		return 0;
	}

	const DWORD imageSize = nt->OptionalHeader.SizeOfImage;

	const IMAGE_DATA_DIRECTORY& importDirectory =
		nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (importDirectory.VirtualAddress == 0 || importDirectory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
		return 0;
	}

	IMAGE_IMPORT_DESCRIPTOR* descriptors =
		RvaToPtr<IMAGE_IMPORT_DESCRIPTOR>(module, imageSize, importDirectory.VirtualAddress);
	if (!descriptors) {
		qqspeed::logging::Logf(
			"socket iat patch skipped: invalid import directory owner=%s rva=0x%lX size=0x%lX",
			GetModuleBaseName(module).c_str(),
			importDirectory.VirtualAddress,
			importDirectory.Size);
		return 0;
	}

	std::size_t patched = 0;
	const DWORD descriptorLimit = importDirectory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
	for (DWORD descriptorIndex = 0; descriptorIndex < descriptorLimit; ++descriptorIndex) {
		const IMAGE_IMPORT_DESCRIPTOR& descriptor = descriptors[descriptorIndex];
		if (descriptor.Name == 0 && descriptor.FirstThunk == 0) {
			break;
		}

		const char* moduleName = RvaToPtr<char>(module, imageSize, descriptor.Name);
		if (!IsWs2ModuleName(moduleName)) {
			continue;
		}

		const DWORD lookupRva = descriptor.OriginalFirstThunk != 0 ?
			descriptor.OriginalFirstThunk :
			descriptor.FirstThunk;
		const DWORD iatRva = descriptor.FirstThunk;
		qqspeed::logging::Logf(
			"socket iat scan owner=%s import=%s lookupRva=0x%lX iatRva=0x%lX",
			GetModuleBaseName(module).c_str(),
			moduleName,
			lookupRva,
			iatRva);

		const DWORD maxThunkCount = 4096;
		for (DWORD thunkIndex = 0; thunkIndex < maxThunkCount; ++thunkIndex) {
			const DWORD lookupEntryRva = lookupRva + thunkIndex * static_cast<DWORD>(sizeof(IMAGE_THUNK_DATA));
			const DWORD iatEntryRva = iatRva + thunkIndex * static_cast<DWORD>(sizeof(IMAGE_THUNK_DATA));
			IMAGE_THUNK_DATA* lookupThunk = RvaToPtr<IMAGE_THUNK_DATA>(module, imageSize, lookupEntryRva);
			IMAGE_THUNK_DATA* iatThunk = RvaToPtr<IMAGE_THUNK_DATA>(module, imageSize, iatEntryRva);
			if (!lookupThunk || !iatThunk || lookupThunk->u1.AddressOfData == 0) {
				break;
			}

			void** slot = reinterpret_cast<void**>(&iatThunk->u1.Function);
			void* currentValue = *slot;
			for (std::size_t specIndex = 0; specIndex < specCount; ++specIndex) {
				const ApiPatchSpec& spec = specs[specIndex];
				if (!IsApiImport(module, imageSize, *lookupThunk, currentValue, spec)) {
					continue;
				}

				if (PatchWs2IatSlot(module, slot, spec)) {
					++patched;
				}
				break;
			}
		}
	}

	return patched;
}

void** FindImportSlotByOrdinal(
	HMODULE module,
	const char* importedModule,
	WORD ordinal,
	std::uintptr_t& slotRva)
{
	slotRva = 0;
	IMAGE_NT_HEADERS* nt = GetNtHeaders(module);
	if (!nt) {
		return nullptr;
	}

	const DWORD imageSize = nt->OptionalHeader.SizeOfImage;
	const IMAGE_DATA_DIRECTORY& importDirectory =
		nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (importDirectory.VirtualAddress == 0 || importDirectory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
		return nullptr;
	}

	IMAGE_IMPORT_DESCRIPTOR* descriptors =
		RvaToPtr<IMAGE_IMPORT_DESCRIPTOR>(module, imageSize, importDirectory.VirtualAddress);
	if (!descriptors) {
		return nullptr;
	}

	const DWORD descriptorLimit = importDirectory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
	for (DWORD descriptorIndex = 0; descriptorIndex < descriptorLimit; ++descriptorIndex) {
		const IMAGE_IMPORT_DESCRIPTOR& descriptor = descriptors[descriptorIndex];
		if (descriptor.Name == 0 && descriptor.FirstThunk == 0) {
			break;
		}

		const char* moduleName = RvaToPtr<char>(module, imageSize, descriptor.Name);
		if (!moduleName || _stricmp(moduleName, importedModule) != 0) {
			continue;
		}

		const DWORD lookupRva = descriptor.OriginalFirstThunk != 0 ?
			descriptor.OriginalFirstThunk :
			descriptor.FirstThunk;
		const DWORD iatRva = descriptor.FirstThunk;
		const DWORD maxThunkCount = 4096;
		for (DWORD thunkIndex = 0; thunkIndex < maxThunkCount; ++thunkIndex) {
			const DWORD lookupEntryRva = lookupRva + thunkIndex * static_cast<DWORD>(sizeof(IMAGE_THUNK_DATA));
			const DWORD iatEntryRva = iatRva + thunkIndex * static_cast<DWORD>(sizeof(IMAGE_THUNK_DATA));
			IMAGE_THUNK_DATA* lookupThunk = RvaToPtr<IMAGE_THUNK_DATA>(module, imageSize, lookupEntryRva);
			IMAGE_THUNK_DATA* iatThunk = RvaToPtr<IMAGE_THUNK_DATA>(module, imageSize, iatEntryRva);
			if (!lookupThunk || !iatThunk || lookupThunk->u1.AddressOfData == 0) {
				break;
			}

			const auto ordinalValue = static_cast<ULONGLONG>(lookupThunk->u1.Ordinal);
			if ((ordinalValue & IMAGE_ORDINAL_FLAG64) == 0 ||
				static_cast<WORD>(ordinalValue & 0xFFFFu) != ordinal) {
				continue;
			}

			slotRva = iatEntryRva;
			return reinterpret_cast<void**>(&iatThunk->u1.Function);
		}
	}

	return nullptr;
}

void** FindMfcCAsyncSocketConnectSlot(HMODULE networkModule, std::uintptr_t& slotRva)
{
	void** slot = FindImportSlotByOrdinal(
		networkModule,
		"mfc140.dll",
		kMfcCAsyncSocketConnectOrdinal,
		slotRva);
	if (slot) {
		return slot;
	}

	return FindImportSlotByOrdinal(
		networkModule,
		"mfc140u.dll",
		kMfcCAsyncSocketConnectOrdinal,
		slotRva);
}

std::size_t PatchLoadedModuleWs2Imports(const ApiPatchSpec* specs, std::size_t specCount)
{
	std::size_t totalPatched = 0;
	const std::vector<HMODULE> modules = ListLoadedModules();
	qqspeed::logging::Logf(
		"socket iat module scan start count=%llu",
		static_cast<unsigned long long>(modules.size()));

	for (HMODULE module : modules) {
		if (!module || module == g_selfModule) {
			continue;
		}
		totalPatched += PatchModuleWs2Imports(module, specs, specCount);
	}

	qqspeed::logging::Logf(
		"socket iat module scan complete patched=%llu",
		static_cast<unsigned long long>(totalPatched));
	return totalPatched;
}

bool PatchNetworkMfcAsyncSocketConnectImport(HMODULE networkModule)
{
	if (!networkModule) {
		qqspeed::logging::Logf("MFC CAsyncSocket::Connect patch skipped: Network.dll unavailable");
		return false;
	}

	std::uintptr_t slotRva = 0;
	void** slot = FindMfcCAsyncSocketConnectSlot(networkModule, slotRva);
	if (!slot) {
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect patch skipped: import ordinal not found ordinal=%u",
			static_cast<unsigned int>(kMfcCAsyncSocketConnectOrdinal));
		return false;
	}

	void* previous = nullptr;
	__try
	{
		previous = *slot;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect patch skipped: unreadable slot=%p rva=0x%llX",
			slot,
			static_cast<unsigned long long>(slotRva));
		return false;
	}

	if (previous == reinterpret_cast<void*>(&HookedMfcAsyncSocketConnect)) {
		g_mfcAsyncSocketConnectSlot = slot;
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect patch skipped: already patched slot=%p original=%p",
			slot,
			reinterpret_cast<void*>(g_originalMfcAsyncSocketConnect));
		return true;
	}

	if (!previous || previous == reinterpret_cast<void*>(~static_cast<std::uintptr_t>(0))) {
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect patch skipped: unresolved slot=%p current=%p",
			slot,
			previous);
		return false;
	}

	if (!g_originalMfcAsyncSocketConnect) {
		g_originalMfcAsyncSocketConnect = reinterpret_cast<MfcAsyncSocketConnectFn>(previous);
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect patch VirtualProtect failed slot=%p error=%lu",
			slot,
			GetLastError());
		return false;
	}

	*slot = reinterpret_cast<void*>(&HookedMfcAsyncSocketConnect);
	FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
	g_mfcAsyncSocketConnectSlot = slot;

	DWORD ignoredProtect = 0;
	const BOOL protectionRestored = VirtualProtect(slot, sizeof(*slot), oldProtect, &ignoredProtect);
	if (!protectionRestored) {
		const DWORD restoreError = GetLastError();
		InterlockedCompareExchangePointer(
			reinterpret_cast<PVOID volatile*>(slot),
			previous,
			reinterpret_cast<void*>(&HookedMfcAsyncSocketConnect));
		DWORD rollbackIgnoredProtect = 0;
		const BOOL rollbackProtectionRestored = VirtualProtect(
			slot,
			sizeof(*slot),
			oldProtect,
			&rollbackIgnoredProtect);
		if (*slot == previous && rollbackProtectionRestored) {
			g_mfcAsyncSocketConnectSlot = nullptr;
		}
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect patch protection restore failed slot=%p error=%lu rollback=%d",
			slot,
			restoreError,
			(*slot == previous && rollbackProtectionRestored) ? 1 : 0);
		return false;
	}

	qqspeed::logging::Logf(
		"MFC CAsyncSocket::Connect patch slot=%p rva=0x%llX previous=%p hook=%p original=%p oldProtect=0x%lX",
		slot,
		static_cast<unsigned long long>(slotRva),
		previous,
		reinterpret_cast<void*>(&HookedMfcAsyncSocketConnect),
		reinterpret_cast<void*>(g_originalMfcAsyncSocketConnect),
		oldProtect);
	return true;
}

bool UnpatchNetworkMfcAsyncSocketConnectImport()
{
	void** slot = g_mfcAsyncSocketConnectSlot;
	void* const original = reinterpret_cast<void*>(g_originalMfcAsyncSocketConnect);
	void* const hook = reinterpret_cast<void*>(&HookedMfcAsyncSocketConnect);
	if (!slot || !original) {
		return true;
	}

	void* current = nullptr;
	__try
	{
		current = *slot;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect rollback failed slot=%p reason=unreadable",
			slot);
		return false;
	}
	if (current == original) {
		g_mfcAsyncSocketConnectSlot = nullptr;
		return true;
	}
	if (current != hook) {
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect rollback failed slot=%p current=%p expectedHook=%p",
			slot,
			current,
			hook);
		return false;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &oldProtect)) {
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect rollback VirtualProtect failed slot=%p error=%lu",
			slot,
			GetLastError());
		return false;
	}

	void* const previous = InterlockedCompareExchangePointer(
		reinterpret_cast<PVOID volatile*>(slot),
		original,
		hook);
	FlushInstructionCache(GetCurrentProcess(), slot, sizeof(*slot));
	DWORD ignoredProtect = 0;
	const BOOL protectionRestored = VirtualProtect(slot, sizeof(*slot), oldProtect, &ignoredProtect);
	const bool restored =
		(previous == hook || previous == original) &&
		*slot == original &&
		protectionRestored;
	qqspeed::logging::Logf(
		"MFC CAsyncSocket::Connect rollback slot=%p previous=%p original=%p result=%d protectRestored=%d",
		slot,
		previous,
		original,
		restored ? 1 : 0,
		protectionRestored ? 1 : 0);
	if (restored) {
		g_mfcAsyncSocketConnectSlot = nullptr;
	}
	return restored;
}

bool TryReadIpv4Endpoint(const sockaddr* address, int addressLength, Ipv4Endpoint& endpoint)
{
	if (!address || addressLength < static_cast<int>(sizeof(sockaddr_in)) || address->sa_family != AF_INET) {
		return false;
	}

	const sockaddr_in* ipv4 = reinterpret_cast<const sockaddr_in*>(address);
	endpoint.port = ntohs(ipv4->sin_port);
	if (!inet_ntop(AF_INET, &ipv4->sin_addr, endpoint.ip, sizeof(endpoint.ip))) {
		strcpy_s(endpoint.ip, "inet_ntop_failed");
	}
	return true;
}

bool TryParseServicePort(PCSTR serviceName, unsigned short& port)
{
	port = 0;
	if (!serviceName || !serviceName[0]) {
		return false;
	}

	char* end = nullptr;
	const unsigned long value = strtoul(serviceName, &end, 10);
	if (!end || *end != 0 || value > 0xFFFFul) {
		return false;
	}

	port = static_cast<unsigned short>(value);
	return true;
}

bool TryParseServicePort(PCWSTR serviceName, unsigned short& port)
{
	port = 0;
	if (!serviceName || !serviceName[0]) {
		return false;
	}

	wchar_t* end = nullptr;
	const unsigned long value = wcstoul(serviceName, &end, 10);
	if (!end || *end != 0 || value > 0xFFFFul) {
		return false;
	}

	port = static_cast<unsigned short>(value);
	return true;
}

std::wstring Utf8ToWide(const std::string& text)
{
	if (text.empty()) {
		return {};
	}

	const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (length <= 1) {
		return {};
	}

	std::wstring wide(static_cast<std::size_t>(length - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
	return wide;
}

void LogGetAddrInfoCall(
	const char* apiName,
	void* returnAddress,
	const char* nodeName,
	const char* serviceName,
	unsigned short servicePort,
	bool redirectCandidate)
{
	std::string callerName;
	std::uintptr_t callerRva = 0;
	ResolveCaller(returnAddress, callerName, callerRva);
	qqspeed::logging::Logf(
		"%s observe caller=%s callerRva=0x%llX node=%s service=%s parsedPort=%u redirectCandidate=%d",
		apiName,
		callerName.c_str(),
		static_cast<unsigned long long>(callerRva),
		nodeName ? nodeName : "(null)",
		serviceName ? serviceName : "(null)",
		servicePort,
		redirectCandidate ? 1 : 0);
}

QQSPEED_VMP_NOINLINE bool EnsureRedirectTargetAvailable()
{
	QQSPEED_VMP_BEGIN_ULTRA("Login.Redirect.BuildSockaddr");
	if (g_targetHost.empty() || g_targetPort == 0) {
		LoadRedirectTarget();
	}
	const bool available = !g_targetHost.empty() && g_targetPort != 0;
	QQSPEED_VMP_END();
	return available;
}

bool BuildRedirectedSockaddr(
	const char* apiName,
	const Ipv4Endpoint& original,
	sockaddr_in& redirected)
{
	bool success = false;
	if (!EnsureRedirectTargetAvailable()) {
		qqspeed::logging::Logf("%s redirect skipped: target unavailable original=%s:%u", apiName, original.ip, original.port);
	}
	else {
		ZeroMemory(&redirected, sizeof(redirected));
		redirected.sin_family = AF_INET;
		redirected.sin_port = htons(g_targetPort);
		if (InetPtonA(AF_INET, g_targetHost.c_str(), &redirected.sin_addr) != 1) {
			qqspeed::logging::Logf("%s redirect skipped: non-IPv4 target host=%s", apiName, g_targetHost.c_str());
		}
		else {
			qqspeed::logging::Logf(
				"%s redirect %s:%u -> %s:%u",
				apiName,
				original.ip,
				original.port,
				g_targetHost.c_str(),
				g_targetPort);
			success = true;
		}
	}
	return success;
}

bool ShouldLogDatagramObservation(Ws2Api api, bool redirectCandidate)
{
	(void)api;
	return redirectCandidate;
}

int WSAAPI HookedConnect(SOCKET socket, const sockaddr* name, int nameLength)
{
	if (!g_originalConnect) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}

	std::string callerName;
	std::uintptr_t callerRva = 0;
	ResolveCaller(_ReturnAddress(), callerName, callerRva);

	Ipv4Endpoint endpoint = {};
	if (TryReadIpv4Endpoint(name, nameLength, endpoint)) {
		const bool redirectCandidate = ShouldRedirectPort(endpoint.port);
		if (redirectCandidate) {
			qqspeed::logging::Logf(
				"connect observe caller=%s callerRva=0x%llX socket=%llu target=%s:%u len=%d redirectCandidate=%d",
				callerName.c_str(),
				static_cast<unsigned long long>(callerRva),
				static_cast<unsigned long long>(socket),
				endpoint.ip,
				endpoint.port,
				nameLength,
				redirectCandidate ? 1 : 0);
		}

		if (redirectCandidate) {
			sockaddr_in redirected = {};
			if (BuildRedirectedSockaddr("connect", endpoint, redirected)) {
				const int result = g_originalConnect(socket, reinterpret_cast<sockaddr*>(&redirected), sizeof(redirected));
				const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
				qqspeed::logging::Logf(
					"connect redirect result caller=%s callerRva=0x%llX socket=%llu result=%d error=%d",
					callerName.c_str(),
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long long>(socket),
					result,
					error);
				if (result == SOCKET_ERROR) {
					WSASetLastError(error);
				}
				return result;
			}
			if (RejectProtectedRedirect("connect")) {
				return SOCKET_ERROR;
			}
		}
	}

	return g_originalConnect(socket, name, nameLength);
}

int WSAAPI HookedWSAConnect(
	SOCKET socket,
	const sockaddr* name,
	int nameLength,
	LPWSABUF callerData,
	LPWSABUF calleeData,
	LPQOS socketQos,
	LPQOS groupQos)
{
	if (!g_originalWSAConnect) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}

	std::string callerName;
	std::uintptr_t callerRva = 0;
	ResolveCaller(_ReturnAddress(), callerName, callerRva);

	Ipv4Endpoint endpoint = {};
	if (TryReadIpv4Endpoint(name, nameLength, endpoint)) {
		const bool redirectCandidate = ShouldRedirectPort(endpoint.port);
		if (redirectCandidate) {
			qqspeed::logging::Logf(
				"WSAConnect observe caller=%s callerRva=0x%llX socket=%llu target=%s:%u len=%d redirectCandidate=%d",
				callerName.c_str(),
				static_cast<unsigned long long>(callerRva),
				static_cast<unsigned long long>(socket),
				endpoint.ip,
				endpoint.port,
				nameLength,
				redirectCandidate ? 1 : 0);
		}

		if (redirectCandidate) {
			sockaddr_in redirected = {};
			if (BuildRedirectedSockaddr("WSAConnect", endpoint, redirected)) {
				const int result = g_originalWSAConnect(
					socket,
					reinterpret_cast<sockaddr*>(&redirected),
					sizeof(redirected),
					callerData,
					calleeData,
					socketQos,
					groupQos);
				const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
				qqspeed::logging::Logf(
					"WSAConnect redirect result caller=%s callerRva=0x%llX socket=%llu result=%d error=%d",
					callerName.c_str(),
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long long>(socket),
					result,
					error);
				if (result == SOCKET_ERROR) {
					WSASetLastError(error);
				}
				return result;
			}
			if (RejectProtectedRedirect("WSAConnect")) {
				return SOCKET_ERROR;
			}
		}
	}

	return g_originalWSAConnect(socket, name, nameLength, callerData, calleeData, socketQos, groupQos);
}

int WSAAPI HookedSendTo(
	SOCKET socket,
	const char* buffer,
	int length,
	int flags,
	const sockaddr* to,
	int toLength)
{
	if (!g_originalSendTo) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}

	std::string callerName;
	std::uintptr_t callerRva = 0;
	ResolveCaller(_ReturnAddress(), callerName, callerRva);

	Ipv4Endpoint endpoint = {};
	if (TryReadIpv4Endpoint(to, toLength, endpoint)) {
		const bool redirectCandidate = ShouldRedirectPort(endpoint.port);
		if (ShouldLogDatagramObservation(Ws2Api::SendTo, redirectCandidate)) {
			qqspeed::logging::Logf(
				"sendto observe caller=%s callerRva=0x%llX socket=%llu target=%s:%u bytes=%d flags=0x%X toLen=%d redirectCandidate=%d",
				callerName.c_str(),
				static_cast<unsigned long long>(callerRva),
				static_cast<unsigned long long>(socket),
				endpoint.ip,
				endpoint.port,
				length,
				flags,
				toLength,
				redirectCandidate ? 1 : 0);
		}

		if (redirectCandidate) {
			sockaddr_in redirected = {};
			if (BuildRedirectedSockaddr("sendto", endpoint, redirected)) {
				const int result = g_originalSendTo(
					socket,
					buffer,
					length,
					flags,
					reinterpret_cast<sockaddr*>(&redirected),
					sizeof(redirected));
				const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
				qqspeed::logging::Logf(
					"sendto redirect result caller=%s callerRva=0x%llX socket=%llu result=%d error=%d",
					callerName.c_str(),
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long long>(socket),
					result,
					error);
				if (result == SOCKET_ERROR) {
					WSASetLastError(error);
				}
				return result;
			}
			if (RejectProtectedRedirect("sendto")) {
				return SOCKET_ERROR;
			}
		}
	}

	return g_originalSendTo(socket, buffer, length, flags, to, toLength);
}

int WSAAPI HookedWSASendTo(
	SOCKET socket,
	LPWSABUF buffers,
	DWORD bufferCount,
	LPDWORD numberOfBytesSent,
	DWORD flags,
	const sockaddr* to,
	int toLength,
	LPWSAOVERLAPPED overlapped,
	LPWSAOVERLAPPED_COMPLETION_ROUTINE completionRoutine)
{
	if (!g_originalWSASendTo) {
		WSASetLastError(WSAEINVAL);
		return SOCKET_ERROR;
	}

	std::string callerName;
	std::uintptr_t callerRva = 0;
	ResolveCaller(_ReturnAddress(), callerName, callerRva);

	Ipv4Endpoint endpoint = {};
	if (TryReadIpv4Endpoint(to, toLength, endpoint)) {
		const bool redirectCandidate = ShouldRedirectPort(endpoint.port);
		if (ShouldLogDatagramObservation(Ws2Api::WSASendTo, redirectCandidate)) {
			qqspeed::logging::Logf(
				"WSASendTo observe caller=%s callerRva=0x%llX socket=%llu target=%s:%u bufferCount=%lu flags=0x%lX toLen=%d overlapped=%p redirectCandidate=%d",
				callerName.c_str(),
				static_cast<unsigned long long>(callerRva),
				static_cast<unsigned long long>(socket),
				endpoint.ip,
				endpoint.port,
				bufferCount,
				flags,
				toLength,
				overlapped,
				redirectCandidate ? 1 : 0);
		}

		if (redirectCandidate) {
			sockaddr_in redirected = {};
			if (BuildRedirectedSockaddr("WSASendTo", endpoint, redirected)) {
				const int result = g_originalWSASendTo(
					socket,
					buffers,
					bufferCount,
					numberOfBytesSent,
					flags,
					reinterpret_cast<sockaddr*>(&redirected),
					sizeof(redirected),
					overlapped,
					completionRoutine);
				const int error = result == SOCKET_ERROR ? WSAGetLastError() : 0;
				qqspeed::logging::Logf(
					"WSASendTo redirect result caller=%s callerRva=0x%llX socket=%llu result=%d error=%d",
					callerName.c_str(),
					static_cast<unsigned long long>(callerRva),
					static_cast<unsigned long long>(socket),
					result,
					error);
				if (result == SOCKET_ERROR) {
					WSASetLastError(error);
				}
				return result;
			}
			if (RejectProtectedRedirect("WSASendTo")) {
				return SOCKET_ERROR;
			}
		}
	}

	return g_originalWSASendTo(
		socket,
		buffers,
		bufferCount,
		numberOfBytesSent,
		flags,
		to,
		toLength,
		overlapped,
		completionRoutine);
}

int WSAAPI HookedGetAddrInfo(
	PCSTR nodeName,
	PCSTR serviceName,
	const ADDRINFOA* hints,
	PADDRINFOA* result)
{
	if (!g_originalGetAddrInfo) {
		WSASetLastError(WSAEINVAL);
		return EAI_FAIL;
	}

	unsigned short servicePort = 0;
	const bool parsedPort = TryParseServicePort(serviceName, servicePort);
	const bool redirectCandidate = parsedPort && ShouldRedirectPort(servicePort);
	if (redirectCandidate) {
		LogGetAddrInfoCall("getaddrinfo", _ReturnAddress(), nodeName, serviceName, servicePort, true);
		if (g_targetHost.empty() || g_targetPort == 0) {
			LoadRedirectTarget();
		}
		if (!g_targetHost.empty() && g_targetPort != 0) {
			char redirectedService[16] = {};
			sprintf_s(redirectedService, "%u", g_targetPort);
			qqspeed::logging::Logf(
				"getaddrinfo redirect node=%s service=%s -> %s:%s",
				nodeName ? nodeName : "(null)",
				serviceName ? serviceName : "(null)",
				g_targetHost.c_str(),
				redirectedService);
			const int redirectedResult = g_originalGetAddrInfo(g_targetHost.c_str(), redirectedService, hints, result);
			qqspeed::logging::Logf(
				"getaddrinfo redirect result result=%d wsaError=%d",
				redirectedResult,
				redirectedResult != 0 ? WSAGetLastError() : 0);
			return redirectedResult;
		}
		qqspeed::logging::Logf("getaddrinfo redirect skipped: target unavailable");
	}
	else if (parsedPort && servicePort >= 9000 && servicePort <= 11000) {
		LogGetAddrInfoCall("getaddrinfo", _ReturnAddress(), nodeName, serviceName, servicePort, false);
	}

	return g_originalGetAddrInfo(nodeName, serviceName, hints, result);
}

int WSAAPI HookedGetAddrInfoW(
	PCWSTR nodeName,
	PCWSTR serviceName,
	const ADDRINFOW* hints,
	PADDRINFOW* result)
{
	if (!g_originalGetAddrInfoW) {
		WSASetLastError(WSAEINVAL);
		return EAI_FAIL;
	}

	unsigned short servicePort = 0;
	const bool parsedPort = TryParseServicePort(serviceName, servicePort);
	const bool redirectCandidate = parsedPort && ShouldRedirectPort(servicePort);
	if (redirectCandidate) {
		char narrowNode[256] = {};
		char narrowService[64] = {};
		if (nodeName) {
			WideCharToMultiByte(CP_UTF8, 0, nodeName, -1, narrowNode, static_cast<int>(sizeof(narrowNode)), nullptr, nullptr);
		}
		if (serviceName) {
			WideCharToMultiByte(CP_UTF8, 0, serviceName, -1, narrowService, static_cast<int>(sizeof(narrowService)), nullptr, nullptr);
		}
		LogGetAddrInfoCall(
			"GetAddrInfoW",
			_ReturnAddress(),
			narrowNode[0] ? narrowNode : nullptr,
			narrowService[0] ? narrowService : nullptr,
			servicePort,
			true);

		if (g_targetHost.empty() || g_targetPort == 0) {
			LoadRedirectTarget();
		}
		const std::wstring wideHost = Utf8ToWide(g_targetHost);
		if (!wideHost.empty() && g_targetPort != 0) {
			wchar_t redirectedService[16] = {};
			swprintf_s(redirectedService, L"%u", g_targetPort);
			qqspeed::logging::Logf(
				"GetAddrInfoW redirect node=%s service=%s -> %s:%u",
				narrowNode[0] ? narrowNode : "(null)",
				narrowService[0] ? narrowService : "(null)",
				g_targetHost.c_str(),
				g_targetPort);
			const int redirectedResult = g_originalGetAddrInfoW(wideHost.c_str(), redirectedService, hints, result);
			qqspeed::logging::Logf(
				"GetAddrInfoW redirect result result=%d wsaError=%d",
				redirectedResult,
				redirectedResult != 0 ? WSAGetLastError() : 0);
			return redirectedResult;
		}
		qqspeed::logging::Logf("GetAddrInfoW redirect skipped: target unavailable");
	}
	else if (parsedPort && servicePort >= 9000 && servicePort <= 11000) {
		char narrowNode[256] = {};
		char narrowService[64] = {};
		if (nodeName) {
			WideCharToMultiByte(CP_UTF8, 0, nodeName, -1, narrowNode, static_cast<int>(sizeof(narrowNode)), nullptr, nullptr);
		}
		if (serviceName) {
			WideCharToMultiByte(CP_UTF8, 0, serviceName, -1, narrowService, static_cast<int>(sizeof(narrowService)), nullptr, nullptr);
		}
		LogGetAddrInfoCall(
			"GetAddrInfoW",
			_ReturnAddress(),
			narrowNode[0] ? narrowNode : nullptr,
			narrowService[0] ? narrowService : nullptr,
			servicePort,
			false);
	}

	return g_originalGetAddrInfoW(nodeName, serviceName, hints, result);
}

u_short WSAAPI HookedHtons(u_short hostshort)
{
	if (!g_originalHtons) {
		return 0;
	}

	if (ShouldRedirectPort(hostshort)) {
		std::string callerName;
		std::uintptr_t callerRva = 0;
		ResolveCaller(_ReturnAddress(), callerName, callerRva);
		if (g_targetHost.empty() || g_targetPort == 0) {
			LoadRedirectTarget();
		}
		if (g_targetPort != 0) {
			qqspeed::logging::Logf(
				"htons redirect caller=%s callerRva=0x%llX port=%u -> %u",
				callerName.c_str(),
				static_cast<unsigned long long>(callerRva),
				hostshort,
				g_targetPort);
			return g_originalHtons(g_targetPort);
		}
		qqspeed::logging::Logf(
			"htons redirect skipped caller=%s callerRva=0x%llX port=%u targetPort=0",
			callerName.c_str(),
			static_cast<unsigned long long>(callerRva),
			hostshort);
	}

	return g_originalHtons(hostshort);
}

unsigned long WSAAPI HookedInetAddr(const char* cp)
{
	if (!g_originalInetAddr) {
		return INADDR_NONE;
	}

	if (ShouldRedirectAddressString(cp)) {
		std::string callerName;
		std::uintptr_t callerRva = 0;
		ResolveCaller(_ReturnAddress(), callerName, callerRva);
		if (g_targetHost.empty() || g_targetPort == 0) {
			LoadRedirectTarget();
		}
		if (!g_targetHost.empty()) {
			qqspeed::logging::Logf(
				"inet_addr redirect caller=%s callerRva=0x%llX ip=%s -> %s",
				callerName.c_str(),
				static_cast<unsigned long long>(callerRva),
				cp,
				g_targetHost.c_str());
			return g_originalInetAddr(g_targetHost.c_str());
		}
		qqspeed::logging::Logf(
			"inet_addr redirect skipped caller=%s callerRva=0x%llX ip=%s targetHost=empty",
			callerName.c_str(),
			static_cast<unsigned long long>(callerRva),
			cp);
	}

	return g_originalInetAddr(cp);
}

int __fastcall HookedMfcAsyncSocketConnect(void* socket, const char* hostAddress, unsigned int hostPort)
{
	if (!g_originalMfcAsyncSocketConnect) {
		qqspeed::logging::Logf("MFC CAsyncSocket::Connect called before original resolved socket=%p", socket);
		return 0;
	}

	char host[128] = {};
	const bool copiedHost = TryCopyString(host, sizeof(host), hostAddress);
	const bool addressCandidate = copiedHost && ShouldRedirectAddressString(host);
	const bool portCandidate = hostPort <= 0xFFFFu && ShouldRedirectPort(static_cast<unsigned short>(hostPort));
	const bool redirectCandidate = portCandidate;
	const bool shouldLog = redirectCandidate || addressCandidate || (hostPort >= 9000 && hostPort <= 11000);
	if (!shouldLog) {
		return g_originalMfcAsyncSocketConnect(socket, hostAddress, hostPort);
	}

#if defined(QQSPEED_VMP_PROTECTION)
	// Keep protected diagnostics useful without exposing either endpoint or code addresses.
	qqspeed::logging::Logf(
		"MFC protected redirect decision addressAllowed=%d portAllowed=%d candidate=%d",
		addressCandidate ? 1 : 0,
		portCandidate ? 1 : 0,
		redirectCandidate ? 1 : 0);
#endif

	std::string callerName;
	std::uintptr_t callerRva = 0;
	ResolveCaller(_ReturnAddress(), callerName, callerRva);

	if (shouldLog) {
		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect observe caller=%s callerRva=0x%llX socket=%p host=%s port=%u redirectCandidate=%d",
			callerName.c_str(),
			static_cast<unsigned long long>(callerRva),
			socket,
			copiedHost ? host : "(unreadable)",
			hostPort,
			redirectCandidate ? 1 : 0);
	}

	if (redirectCandidate) {
		if (EnsureRedirectTargetAvailable()) {
			LARGE_INTEGER started = {};
			LARGE_INTEGER finished = {};
			LARGE_INTEGER frequency = {};
			QueryPerformanceCounter(&started);
			const int result = g_originalMfcAsyncSocketConnect(
				socket,
				g_targetHost.c_str(),
				static_cast<unsigned int>(g_targetPort));
			const int error = result == 0 ? WSAGetLastError() : 0;
			QueryPerformanceCounter(&finished);
			QueryPerformanceFrequency(&frequency);
			const unsigned long long elapsedUs = frequency.QuadPart > 0
				? static_cast<unsigned long long>(
					(finished.QuadPart - started.QuadPart) * 1000000LL / frequency.QuadPart)
				: 0;
			const int asyncPending = result == 0 && error == WSAEWOULDBLOCK ? 1 : 0;
#if defined(QQSPEED_VMP_PROTECTION)
			qqspeed::logging::Logf(
				"MFC protected redirect applied originalPort=%u result=%d error=%d elapsedUs=%llu asyncPending=%d",
				hostPort,
				result,
				error,
				elapsedUs,
				asyncPending);
#endif
			qqspeed::logging::Logf(
				"MFC CAsyncSocket::Connect redirect caller=%s callerRva=0x%llX socket=%p %s:%u -> %s:%u result=%d error=%d elapsedUs=%llu asyncPending=%d",
				callerName.c_str(),
				static_cast<unsigned long long>(callerRva),
				socket,
				host,
				hostPort,
				g_targetHost.c_str(),
				g_targetPort,
				result,
				error,
				elapsedUs,
				asyncPending);
			if (result == 0) {
				WSASetLastError(error);
			}
			return result;
		}

		if (RejectProtectedRedirect("MFC CAsyncSocket::Connect")) {
			return 0;
		}

		qqspeed::logging::Logf(
			"MFC CAsyncSocket::Connect redirect skipped caller=%s callerRva=0x%llX host=%s port=%u target=%s:%u",
			callerName.c_str(),
			static_cast<unsigned long long>(callerRva),
			host,
			hostPort,
			g_targetHost.empty() ? "(empty)" : g_targetHost.c_str(),
			g_targetPort);
	}

	return g_originalMfcAsyncSocketConnect(socket, hostAddress, hostPort);
}
}

namespace qqspeed::network
{
bool InstallConnectRedirectHook(HMODULE networkModule)
{
	g_selfModule = nullptr;
	GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(&InstallConnectRedirectHook),
		&g_selfModule);

	LoadRedirectTarget();

	const bool mfcConnectPatched = PatchNetworkMfcAsyncSocketConnectImport(networkModule);
	qqspeed::logging::Logf(
		"socket redirect scoped iat complete requestedNetwork=%p mfcConnect=%d original=%p",
		networkModule,
		mfcConnectPatched ? 1 : 0,
		reinterpret_cast<void*>(g_originalMfcAsyncSocketConnect));
	return mfcConnectPatched;
}

bool UninstallConnectRedirectHook()
{
	return UnpatchNetworkMfcAsyncSocketConnectImport();
}
}
