#include "pch.h"

#include "hook_runtime.h"
#include "network/connect_redirect.h"
#include "topkart/protected_topkart_profile.h"
#include "vmp_protection.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <wincrypt.h>

namespace
{
constexpr wchar_t kNetworkModuleName[] = L"Network.dll";
constexpr wchar_t kTopKartModuleName[] = L"Top-Kart.dll";
constexpr std::size_t kNetworkImageSize = 0x968000;
constexpr std::array<BYTE, 16> kNetworkMd5 = {
	0x97, 0xD2, 0x5C, 0x1C, 0x4C, 0xF7, 0x73, 0x0F,
	0x26, 0xC2, 0x26, 0xCD, 0xC6, 0xE8, 0xBB, 0x3C
};
constexpr std::size_t kTopKartImageSize = 0x3D63000;
constexpr std::array<BYTE, 16> kTopKartMd5 = {
	0x26, 0xB6, 0x21, 0xA8, 0xDC, 0x3E, 0xAF, 0x02,
	0x74, 0x86, 0x08, 0x1F, 0x9B, 0x67, 0xCE, 0xC2
};
#if !defined(QQSPEED_VMP_PROTECTION)
constexpr std::uintptr_t kAntiCheckerExecuteRva = 0x342D90;
constexpr std::uintptr_t kAntiCheckerExecuteSlotRva = 0x2B55F98;
constexpr std::uintptr_t kAceInterfaceGlobalRva = 0x36C2D08;
#endif
constexpr std::array<BYTE, 5> kAntiCheckerOriginal = { 0xE9, 0x24, 0xAE, 0x62, 0x03 };
constexpr std::size_t kAceFeedSlotOffset = 0x28;
constexpr bool kEnableAntiCheckerConditional = true;

using AntiCheckerExecuteFn = void(__fastcall*)(void*);
using AceFeedFn = std::uint64_t(__fastcall*)(void*, const void*, int);

HMODULE g_topKartModuleReference = nullptr;
AntiCheckerExecuteFn g_originalAntiCheckerExecute = nullptr;
AceFeedFn g_originalAceFeed = nullptr;
PVOID volatile* g_antiCheckerExecuteSlot = nullptr;
PVOID volatile* g_aceFeedSlot = nullptr;
volatile LONG g_validAceFeedCount = 0;

struct HookUnicodeString
{
	USHORT length;
	USHORT maximumLength;
	PWSTR buffer;
};

struct HookLdrDllLoadedNotificationData
{
	ULONG flags;
	const HookUnicodeString* fullDllName;
	const HookUnicodeString* baseDllName;
	PVOID dllBase;
	ULONG sizeOfImage;
};

union HookLdrDllNotificationData
{
	HookLdrDllLoadedNotificationData loaded;
	HookLdrDllLoadedNotificationData unloaded;
};

using LdrDllNotificationFn = void(CALLBACK*)(
	ULONG,
	const HookLdrDllNotificationData*,
	void*);
using LdrRegisterDllNotificationFn = LONG(NTAPI*)(
	ULONG,
	LdrDllNotificationFn,
	void*,
	void**);
using LdrUnregisterDllNotificationFn = LONG(NTAPI*)(void*);

void CALLBACK OnLoaderDllNotification(
	ULONG reason,
	const HookLdrDllNotificationData* data,
	void* context)
{
	constexpr ULONG kLoaded = 1;
	if (reason != kLoaded || !data || !context ||
		!data->loaded.baseDllName || !data->loaded.baseDllName->buffer) {
		return;
	}
	const HookUnicodeString& name = *data->loaded.baseDllName;
	const std::size_t chars = name.length / sizeof(wchar_t);
	if (chars == _countof(kNetworkModuleName) - 1 &&
		_wcsnicmp(name.buffer, kNetworkModuleName, chars) == 0) {
		SetEvent(static_cast<HANDLE>(context));
	}
}

std::wstring GetModulePath(HMODULE module)
{
	std::vector<wchar_t> path(MAX_PATH);
	for (;;) {
		const DWORD length = GetModuleFileNameW(
			module,
			path.data(),
			static_cast<DWORD>(path.size()));
		if (length == 0) {
			return {};
		}
		if (length < path.size() - 1) {
			return std::wstring(path.data(), length);
		}
		path.resize(path.size() * 2);
	}
}

std::size_t GetModuleImageSize(HMODULE module)
{
	if (!module) {
		return 0;
	}
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) {
		return 0;
	}
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
	return nt->Signature == IMAGE_NT_SIGNATURE
		? nt->OptionalHeader.SizeOfImage
		: 0;
}

bool FileMd5Equals(
	const std::wstring& path,
	const std::array<BYTE, 16>& expected) noexcept
{
	HANDLE file = CreateFileW(
		path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return false;
	}

	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	bool matches = false;
	if (CryptAcquireContextW(
			&provider,
			nullptr,
			nullptr,
			PROV_RSA_FULL,
			CRYPT_VERIFYCONTEXT) &&
		CryptCreateHash(provider, CALG_MD5, 0, 0, &hash)) {
		std::array<BYTE, 64 * 1024> buffer{};
		DWORD bytesRead = 0;
		BOOL readOk = FALSE;
		do {
			readOk = ReadFile(
				file,
				buffer.data(),
				static_cast<DWORD>(buffer.size()),
				&bytesRead,
				nullptr);
			if (readOk && bytesRead > 0 &&
				!CryptHashData(hash, buffer.data(), bytesRead, 0)) {
				readOk = FALSE;
			}
		} while (readOk && bytesRead > 0);

		if (readOk) {
			std::array<BYTE, 16> digest{};
			DWORD digestSize = static_cast<DWORD>(digest.size());
			matches = CryptGetHashParam(
				hash,
				HP_HASHVAL,
				digest.data(),
				&digestSize,
				0) &&
				digestSize == digest.size() &&
				std::memcmp(digest.data(), expected.data(), digest.size()) == 0;
			SecureZeroMemory(digest.data(), digest.size());
		}
		SecureZeroMemory(buffer.data(), buffer.size());
	}
	if (hash) {
		CryptDestroyHash(hash);
	}
	if (provider) {
		CryptReleaseContext(provider, 0);
	}
	CloseHandle(file);
	return matches;
}

bool ValidateModule(
	HMODULE module,
	std::size_t expectedImageSize,
	const std::array<BYTE, 16>& expectedMd5)
{
	if (GetModuleImageSize(module) != expectedImageSize) {
		return false;
	}
	const std::wstring path = GetModulePath(module);
	return !path.empty() && FileMd5Equals(path, expectedMd5);
}

bool SafeReadSelf(void* destination, const void* source, std::size_t size) noexcept
{
	if (!destination || !source || size == 0) {
		return false;
	}
	__try {
		std::memcpy(destination, source, size);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		std::memset(destination, 0, size);
		return false;
	}
}

bool IsExecutableAddress(const void* address) noexcept
{
	if (!address) {
		return false;
	}
	MEMORY_BASIC_INFORMATION information = {};
	if (VirtualQuery(address, &information, sizeof(information)) != sizeof(information) ||
		information.State != MEM_COMMIT ||
		(information.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
		return false;
	}
	const DWORD protection = information.Protect & 0xFF;
	return protection == PAGE_EXECUTE ||
		protection == PAGE_EXECUTE_READ ||
		protection == PAGE_EXECUTE_READWRITE ||
		protection == PAGE_EXECUTE_WRITECOPY;
}

QQSPEED_VMP_NOINLINE bool ExchangePointerSlot(
	PVOID volatile* slot,
	PVOID expected,
	PVOID replacement) noexcept
{
	QQSPEED_VMP_BEGIN_ULTRA("Prod.AntiChecker.ExchangeSlot");
	bool success = false;
	if (slot && expected && replacement) {
		DWORD oldProtect = 0;
		if (VirtualProtect(
				const_cast<PVOID*>(slot),
				sizeof(PVOID),
				PAGE_READWRITE,
				&oldProtect)) {
			PVOID previous =
				InterlockedCompareExchangePointer(slot, replacement, expected);
			DWORD ignoredProtect = 0;
			const BOOL protectionRestored = VirtualProtect(
				const_cast<PVOID*>(slot),
				sizeof(PVOID),
				oldProtect,
				&ignoredProtect);
			success =
				(previous == expected || previous == replacement) &&
				protectionRestored != FALSE;
		}
	}
	QQSPEED_VMP_END();
	return success;
}

QQSPEED_VMP_NOINLINE bool RestorePointerSlot(
	PVOID volatile* slot,
	PVOID expectedReplacement,
	PVOID original) noexcept
{
	QQSPEED_VMP_BEGIN_ULTRA("Prod.AntiChecker.RestoreSlot");
	bool success = false;
	if (slot && expectedReplacement && original) {
		DWORD oldProtect = 0;
		if (VirtualProtect(
				const_cast<PVOID*>(slot),
				sizeof(PVOID),
				PAGE_READWRITE,
				&oldProtect)) {
			PVOID previous =
				InterlockedCompareExchangePointer(
					slot,
					original,
					expectedReplacement);
			DWORD ignoredProtect = 0;
			const BOOL protectionRestored = VirtualProtect(
				const_cast<PVOID*>(slot),
				sizeof(PVOID),
				oldProtect,
				&ignoredProtect);
			success =
				(previous == expectedReplacement || previous == original) &&
				protectionRestored != FALSE;
		}
	}
	QQSPEED_VMP_END();
	return success;
}

QQSPEED_VMP_NOINLINE bool IsStructurallyValidAceFeed(
	const void* data,
	int length) noexcept
{
	QQSPEED_VMP_BEGIN_MUTATION("Prod.AceFeed.Validate");
	bool valid = data != nullptr && length >= 8 && length <= 0xFFFF;
	std::array<BYTE, 8> header = {};
	if (valid) {
		valid = SafeReadSelf(header.data(), data, header.size());
	}
	if (valid) {
		const std::uint32_t version =
			(static_cast<std::uint32_t>(header[0]) << 24) |
			(static_cast<std::uint32_t>(header[1]) << 16) |
			(static_cast<std::uint32_t>(header[2]) << 8) |
			static_cast<std::uint32_t>(header[3]);
		const std::uint16_t encodedLength = static_cast<std::uint16_t>(
			(static_cast<std::uint16_t>(header[4]) << 8) |
			static_cast<std::uint16_t>(header[5]));
		const std::uint16_t channel = static_cast<std::uint16_t>(
			(static_cast<std::uint16_t>(header[6]) << 8) |
			static_cast<std::uint16_t>(header[7]));
		valid = version == 1 &&
			encodedLength == static_cast<std::uint16_t>(length) &&
			channel == 0x1000;
	}
	SecureZeroMemory(header.data(), header.size());
	QQSPEED_VMP_END();
	return valid;
}

QQSPEED_VMP_NOINLINE std::uint64_t __fastcall AceFeedObserved(
	void* context,
	const void* data,
	int length) noexcept
{
	QQSPEED_VMP_BEGIN_MUTATION("Prod.AceFeed.Observe");
	AceFeedFn original = g_originalAceFeed;
	std::uint64_t result = 0;
	if (original) {
		const bool valid = IsStructurallyValidAceFeed(data, length);
		result = original(context, data, length);
		if (valid) {
			InterlockedIncrement(&g_validAceFeedCount);
		}
	}
	QQSPEED_VMP_END();
	return result;
}

QQSPEED_VMP_NOINLINE bool ShouldExecuteAntiChecker(LONG validFeedCount) noexcept
{
	QQSPEED_VMP_BEGIN_MUTATION("Prod.AntiChecker.ExecuteGate");
	const bool allowed = validFeedCount > 0;
	QQSPEED_VMP_END();
	return allowed;
}

void __fastcall AntiCheckerConditionalExecute(void* self) noexcept
{
	const LONG feedCount = InterlockedCompareExchange(
		&g_validAceFeedCount,
		0,
		0);
	AntiCheckerExecuteFn original = g_originalAntiCheckerExecute;
	if (ShouldExecuteAntiChecker(feedCount) && original) {
		original(self);
	}
}

QQSPEED_VMP_NOINLINE bool VerifyAntiCheckerSlots(
	PVOID volatile* aceFeedSlot,
	PVOID volatile* executeSlot) noexcept
{
	QQSPEED_VMP_BEGIN_ULTRA("Prod.AntiChecker.VerifySlots");
	PVOID installedFeed = nullptr;
	PVOID installedExecute = nullptr;
	const bool installed =
		SafeReadSelf(
			&installedFeed,
			const_cast<PVOID*>(aceFeedSlot),
			sizeof(installedFeed)) &&
		SafeReadSelf(
			&installedExecute,
			const_cast<PVOID*>(executeSlot),
			sizeof(installedExecute)) &&
		installedFeed == reinterpret_cast<PVOID>(&AceFeedObserved) &&
		installedExecute ==
			reinterpret_cast<PVOID>(&AntiCheckerConditionalExecute);
	QQSPEED_VMP_END();
	return installed;
}

bool PatchTopKartConditionalGate(HMODULE module)
{
	const std::size_t imageSize = GetModuleImageSize(module);
	if (!ValidateModule(module, kTopKartImageSize, kTopKartMd5)) {
		return false;
	}

	struct ScopedRvas
	{
		std::uintptr_t execute = 0;
		std::uintptr_t executeSlot = 0;
		std::uintptr_t aceInterfaceGlobal = 0;
		~ScopedRvas()
		{
#if defined(QQSPEED_VMP_PROTECTION)
			SecureZeroMemory(this, sizeof(*this));
#endif
		}
	} rvas;

#if defined(QQSPEED_VMP_PROTECTION)
	qqspeed::topkart::ResolvedTopKartCoreProfile profile = {};
	const bool profileResolved =
		qqspeed::topkart::ResolveProtectedTopKartCoreProfile(imageSize, profile);
	rvas.execute = profile.antiCheckerExecuteRva;
	rvas.executeSlot = profile.antiCheckerExecuteSlotRva;
	rvas.aceInterfaceGlobal = profile.aceInterfaceGlobalRva;
	qqspeed::topkart::ClearResolvedTopKartCoreProfile(profile);
	if (!profileResolved) {
		return false;
	}
#else
	rvas.execute = kAntiCheckerExecuteRva;
	rvas.executeSlot = kAntiCheckerExecuteSlotRva;
	rvas.aceInterfaceGlobal = kAceInterfaceGlobalRva;
#endif

	const auto rvaInImage = [imageSize](std::uintptr_t rva, std::size_t size) {
		return rva != 0 && rva < imageSize && size <= imageSize - rva;
	};
	if (!rvaInImage(rvas.execute, kAntiCheckerOriginal.size()) ||
		!rvaInImage(rvas.executeSlot, sizeof(PVOID)) ||
		!rvaInImage(rvas.aceInterfaceGlobal, sizeof(PVOID))) {
		return false;
	}

	auto* execute = reinterpret_cast<BYTE*>(module) + rvas.execute;
	std::array<BYTE, kAntiCheckerOriginal.size()> executeBytes = {};
	if (!SafeReadSelf(executeBytes.data(), execute, executeBytes.size()) ||
		executeBytes != kAntiCheckerOriginal ||
		!IsExecutableAddress(execute)) {
		return false;
	}

	auto* executeSlot = reinterpret_cast<PVOID volatile*>(
		reinterpret_cast<BYTE*>(module) + rvas.executeSlot);
	PVOID executeSlotValue = nullptr;
	if (!SafeReadSelf(
			&executeSlotValue,
			const_cast<PVOID*>(executeSlot),
			sizeof(executeSlotValue)) ||
		executeSlotValue != execute) {
		return false;
	}

	constexpr DWORD kAceInterfaceWaitMs = 60000;
	const ULONGLONG waitStarted = GetTickCount64();
	std::uintptr_t interfacePointer = 0;
	PVOID interfaceContext = nullptr;
	PVOID aceFeed = nullptr;
	while (GetTickCount64() - waitStarted < kAceInterfaceWaitMs) {
		interfacePointer = 0;
		interfaceContext = nullptr;
		aceFeed = nullptr;
		const bool interfaceReady =
			SafeReadSelf(
				&interfacePointer,
				reinterpret_cast<const BYTE*>(module) + rvas.aceInterfaceGlobal,
				sizeof(interfacePointer)) &&
			interfacePointer != 0 &&
			SafeReadSelf(
				&interfaceContext,
				reinterpret_cast<const void*>(interfacePointer),
				sizeof(interfaceContext)) &&
			interfaceContext != nullptr &&
			SafeReadSelf(
				&aceFeed,
				reinterpret_cast<const void*>(
					interfacePointer + kAceFeedSlotOffset),
				sizeof(aceFeed)) &&
			IsExecutableAddress(aceFeed);
		if (interfaceReady) {
			break;
		}
		Sleep(GetTickCount64() - waitStarted < 15000 ? 1 : 5);
	}
	if (!interfacePointer || !interfaceContext || !IsExecutableAddress(aceFeed)) {
		return false;
	}

	auto* aceFeedSlot = reinterpret_cast<PVOID volatile*>(
		interfacePointer + kAceFeedSlotOffset);
	g_originalAceFeed = reinterpret_cast<AceFeedFn>(aceFeed);
	g_originalAntiCheckerExecute = reinterpret_cast<AntiCheckerExecuteFn>(execute);
	MemoryBarrier();
	if (!ExchangePointerSlot(
			aceFeedSlot,
			aceFeed,
			reinterpret_cast<PVOID>(&AceFeedObserved))) {
		g_originalAceFeed = nullptr;
		g_originalAntiCheckerExecute = nullptr;
		return false;
	}
	if (!ExchangePointerSlot(
			executeSlot,
			execute,
			reinterpret_cast<PVOID>(&AntiCheckerConditionalExecute))) {
		RestorePointerSlot(
			aceFeedSlot,
			reinterpret_cast<PVOID>(&AceFeedObserved),
			aceFeed);
		g_originalAceFeed = nullptr;
		g_originalAntiCheckerExecute = nullptr;
		return false;
	}

	const bool installed =
		VerifyAntiCheckerSlots(aceFeedSlot, executeSlot);
	if (!installed) {
		RestorePointerSlot(
			executeSlot,
			reinterpret_cast<PVOID>(&AntiCheckerConditionalExecute),
			execute);
		RestorePointerSlot(
			aceFeedSlot,
			reinterpret_cast<PVOID>(&AceFeedObserved),
			aceFeed);
		g_originalAceFeed = nullptr;
		g_originalAntiCheckerExecute = nullptr;
		return false;
	}

	g_aceFeedSlot = aceFeedSlot;
	g_antiCheckerExecuteSlot = executeSlot;
	return true;
}

bool WaitForTopKartAndPatch()
{
	constexpr DWORD kWaitMs = 180000;
	constexpr DWORD kPollMs = 100;
	DWORD waited = 0;
	HMODULE module = nullptr;
	GetModuleHandleExW(0, kTopKartModuleName, &module);
	while (!module && waited < kWaitMs) {
		Sleep(kPollMs);
		waited += kPollMs;
		GetModuleHandleExW(0, kTopKartModuleName, &module);
	}
	if (!module) {
		return false;
	}
	if (!PatchTopKartConditionalGate(module)) {
		FreeLibrary(module);
		return false;
	}
	g_topKartModuleReference = module;
	return true;
}

HMODULE WaitForNetworkModule()
{
	constexpr DWORD kWaitMs = 30000;
	constexpr DWORD kFallbackPollMs = 10;
	HANDLE loadedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	auto registerNotification = ntdll
		? reinterpret_cast<LdrRegisterDllNotificationFn>(
			GetProcAddress(ntdll, "LdrRegisterDllNotification"))
		: nullptr;
	auto unregisterNotification = ntdll
		? reinterpret_cast<LdrUnregisterDllNotificationFn>(
			GetProcAddress(ntdll, "LdrUnregisterDllNotification"))
		: nullptr;
	void* notificationCookie = nullptr;
	const bool notificationRegistered =
		loadedEvent && registerNotification && unregisterNotification &&
		registerNotification(
			0,
			&OnLoaderDllNotification,
			loadedEvent,
			&notificationCookie) >= 0;

	HMODULE networkModule = GetModuleHandleW(kNetworkModuleName);
	if (!networkModule && notificationRegistered) {
		WaitForSingleObject(loadedEvent, kWaitMs);
		networkModule = GetModuleHandleW(kNetworkModuleName);
	}
	if (!networkModule && !notificationRegistered) {
		DWORD waited = 0;
		while (!networkModule && waited <= kWaitMs) {
			Sleep(kFallbackPollMs);
			waited += kFallbackPollMs;
			networkModule = GetModuleHandleW(kNetworkModuleName);
		}
	}
	if (notificationRegistered) {
		unregisterNotification(notificationCookie);
	}
	if (loadedEvent) {
		CloseHandle(loadedEvent);
	}
	return networkModule;
}

bool InstallProductionHooks()
{
	HMODULE networkModule = WaitForNetworkModule();
	if (!networkModule ||
		!ValidateModule(networkModule, kNetworkImageSize, kNetworkMd5)) {
		return false;
	}
	if (!qqspeed::network::InstallConnectRedirectHook(networkModule)) {
		qqspeed::network::UninstallConnectRedirectHook();
		return false;
	}
	if constexpr (kEnableAntiCheckerConditional) {
		if (!WaitForTopKartAndPatch()) {
			qqspeed::network::UninstallConnectRedirectHook();
			return false;
		}
	}
	return true;
}

DWORD WINAPI RuntimeThread(void*)
{
	return InstallProductionHooks() ? 0u : 1u;
}
}

namespace qqspeed::runtime
{
void Start(HMODULE)
{
	HANDLE thread = CreateThread(nullptr, 0, &RuntimeThread, nullptr, 0, nullptr);
	if (thread) {
		CloseHandle(thread);
	}
}
}
