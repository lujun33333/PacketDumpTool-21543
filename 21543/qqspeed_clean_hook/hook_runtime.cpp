#include "pch.h"

#include "hook_config.h"
#include "hook_runtime.h"
#include "diagnostics/process_exception_trace.h"
#include "logging/hook_log.h"
#include "network/code_patch.h"
#include "network/connect_redirect.h"
#include "network/kart_refit_shadow.h"
#include "network/network_gate_commands.h"
#include "network/network_hooks.h"
#if defined(QQSPEED_VMP_PROTECTION)
#include "network/protected_network_profile.h"
#endif
#include "topkart/topkart_change_item_probe.h"
#include "topkart/topkart_exit_trace_probe.h"
#include "topkart/topkart_lua_table_guard.h"
#include "topkart/topkart_mapid_probe.h"
#if defined(QQSPEED_VMP_PROTECTION)
#include "topkart/protected_topkart_profile.h"
#endif
#include "topkart/topkart_worldbound_guard.h"
#include "vmp_protection.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <string>
#include <vector>
#include <wincrypt.h>

namespace
{
std::string Md5File(const std::wstring& path);

constexpr std::size_t kTopKartCurrentImageSize = 0x3D63000;
constexpr char kTopKartCurrentMd5[] = "26b621a8dc3eaf027486081f9b67cec2";
#if !defined(QQSPEED_VMP_PROTECTION)
constexpr std::uintptr_t kTopKartAntiCheckerExecuteRva = 0x342D90;
constexpr std::uintptr_t kTopKartAntiCheckerExecuteSlotRva = 0x2B55F98;
constexpr std::uintptr_t kTopKartAceInterfaceGlobalRva = 0x36C2D08;
#endif
constexpr std::array<BYTE, 5> kTopKartAntiCheckerOriginal = { 0xE9, 0x24, 0xAE, 0x62, 0x03 };
constexpr std::uintptr_t kTopKartDataBssBeginRva = 0x33D9C00;
constexpr std::uintptr_t kTopKartDataEndRva = 0x3740E0C;
constexpr std::size_t kTopKartAceFeedSlotOffset = 0x28;
constexpr DWORD kTopKartAceInterfaceWaitMs = 60000;
using TopKartAntiCheckerExecuteFn = void(__fastcall*)(void*);
using TopKartAceFeedFn = std::uint64_t(__fastcall*)(void*, const void*, int);
HMODULE gTopKartAntiCheckerModuleReference = nullptr;
TopKartAntiCheckerExecuteFn gTopKartOriginalAntiCheckerExecute = nullptr;
TopKartAceFeedFn gTopKartOriginalAceFeed = nullptr;
PVOID volatile* gTopKartAntiCheckerExecuteSlot = nullptr;
PVOID volatile* gTopKartAceFeedSlot = nullptr;
volatile LONG gTopKartValidAceFeedCount = 0;

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

using LdrDllNotificationFn = void(CALLBACK*)(ULONG, const HookLdrDllNotificationData*, void*);
using LdrRegisterDllNotificationFn = LONG(NTAPI*)(ULONG, LdrDllNotificationFn, void*, void**);
using LdrUnregisterDllNotificationFn = LONG(NTAPI*)(void*);

void CALLBACK OnLoaderDllNotification(ULONG reason, const HookLdrDllNotificationData* data, void* context)
{
	constexpr ULONG kLoaded = 1;
	if (reason != kLoaded || !data || !context || !data->loaded.baseDllName ||
		!data->loaded.baseDllName->buffer) {
		return;
	}
	constexpr wchar_t kNetworkName[] = L"Network.dll";
	const HookUnicodeString& name = *data->loaded.baseDllName;
	const std::size_t chars = name.length / sizeof(wchar_t);
	if (chars == _countof(kNetworkName) - 1 &&
		_wcsnicmp(name.buffer, kNetworkName, chars) == 0) {
		SetEvent(static_cast<HANDLE>(context));
	}
}

std::wstring GetModulePath(HMODULE module)
{
	std::vector<wchar_t> path(MAX_PATH);
	for (;;) {
		const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
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
	if (nt->Signature != IMAGE_NT_SIGNATURE) {
		return 0;
	}
	return nt->OptionalHeader.SizeOfImage;
}

bool ValidateNetworkModule(HMODULE module, const qqspeed::config::HookConfig& config)
{
	const std::size_t imageSize = GetModuleImageSize(module);
	const std::wstring path = GetModulePath(module);
	const ULONGLONG hashStarted = GetTickCount64();
	const std::string md5 = path.empty() ? "path_failed" : Md5File(path);
	const ULONGLONG hashMs = GetTickCount64() - hashStarted;
	for (const auto& fingerprint : config.network.acceptedFingerprints) {
		const bool sizeOk = imageSize == fingerprint.imageSize;
		const bool md5Ok = fingerprint.md5 && _stricmp(md5.c_str(), fingerprint.md5) == 0;
		if (!sizeOk || !md5Ok) {
			continue;
		}

		qqspeed::logging::Logf(
			"network_preflight accepted=1 selected=%s size=0x%llX md5=%s hashMs=%llu profile=current",
			fingerprint.name ? fingerprint.name : "<unnamed>",
			static_cast<unsigned long long>(imageSize),
			md5.c_str(),
			static_cast<unsigned long long>(hashMs));
		return true;
	}

	qqspeed::logging::Logf(
		"network_preflight accepted=0 selected=<none> size=0x%llX md5=%s hashMs=%llu candidates=official-auto-97d25c1c:0x968000/97d25c1c4cf7730f26c226cdc6e8bb3c",
		static_cast<unsigned long long>(imageSize),
		md5.c_str(),
		static_cast<unsigned long long>(hashMs));
	return false;
}

bool ValidateTopKartModule(HMODULE module)
{
	const std::size_t imageSize = GetModuleImageSize(module);
	const std::wstring path = GetModulePath(module);
	const ULONGLONG hashStarted = GetTickCount64();
	const std::string md5 = path.empty() ? "path_failed" : Md5File(path);
	const ULONGLONG hashMs = GetTickCount64() - hashStarted;
	const bool accepted =
		imageSize == kTopKartCurrentImageSize &&
		_stricmp(md5.c_str(), kTopKartCurrentMd5) == 0;
	qqspeed::logging::Logf(
		"topkart_preflight accepted=%d imageSize=0x%llX md5=%s hashMs=%llu",
		accepted ? 1 : 0,
		static_cast<unsigned long long>(imageSize),
		md5.c_str(),
		static_cast<unsigned long long>(hashMs));
	return accepted;
}

std::string BytesToLowerHex(const BYTE* bytes, DWORD length)
{
	constexpr char kHex[] = "0123456789abcdef";
	std::string result;
	result.resize(static_cast<size_t>(length) * 2);
	for (DWORD i = 0; i < length; ++i) {
		result[static_cast<size_t>(i) * 2] = kHex[(bytes[i] >> 4) & 0x0F];
		result[static_cast<size_t>(i) * 2 + 1] = kHex[bytes[i] & 0x0F];
	}
	return result;
}

std::string Md5File(const std::wstring& path)
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
		return "open_failed";
	}

	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	std::string result = "hash_failed";

	if (CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) &&
		CryptCreateHash(provider, CALG_MD5, 0, 0, &hash))
	{
		std::array<BYTE, 64 * 1024> buffer{};
		DWORD bytesRead = 0;
		BOOL readOk = FALSE;

		do {
			readOk = ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
			if (readOk && bytesRead > 0) {
				if (!CryptHashData(hash, buffer.data(), bytesRead, 0)) {
					readOk = FALSE;
					break;
				}
			}
		} while (readOk && bytesRead > 0);

		if (readOk) {
			std::array<BYTE, 16> digest{};
			DWORD digestSize = static_cast<DWORD>(digest.size());
			if (CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digestSize, 0)) {
				result = BytesToLowerHex(digest.data(), digestSize);
			}
		}
	}

	if (hash != 0) {
		CryptDestroyHash(hash);
	}
	if (provider != 0) {
		CryptReleaseContext(provider, 0);
	}
	CloseHandle(file);
	return result;
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
	QQSPEED_VMP_BEGIN_ULTRA("TopKart.AntiChecker.ExchangeSlot");
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
			const BOOL restored = VirtualProtect(
				const_cast<PVOID*>(slot),
				sizeof(PVOID),
				oldProtect,
				&ignoredProtect);
			success =
				(previous == expected || previous == replacement) &&
				restored != FALSE;
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
	QQSPEED_VMP_BEGIN_ULTRA("TopKart.AntiChecker.RestoreSlot");
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
			const BOOL restored = VirtualProtect(
				const_cast<PVOID*>(slot),
				sizeof(PVOID),
				oldProtect,
				&ignoredProtect);
			success =
				(previous == expectedReplacement || previous == original) &&
				restored != FALSE;
		}
	}
	QQSPEED_VMP_END();
	return success;
}

QQSPEED_VMP_NOINLINE bool IsStructurallyValidAceFeed(
	const void* data,
	int length) noexcept
{
	QQSPEED_VMP_BEGIN_MUTATION("TopKart.AceFeed.Validate538");
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
		const std::uint16_t encodedLength =
			static_cast<std::uint16_t>(
				(static_cast<std::uint16_t>(header[4]) << 8) |
				static_cast<std::uint16_t>(header[5]));
		const std::uint16_t channel =
			static_cast<std::uint16_t>(
				(static_cast<std::uint16_t>(header[6]) << 8) |
				static_cast<std::uint16_t>(header[7]));
		valid =
			version == 1 &&
			encodedLength == static_cast<std::uint16_t>(length) &&
			channel == 0x1000;
	}
	SecureZeroMemory(header.data(), header.size());
	QQSPEED_VMP_END();
	return valid;
}

QQSPEED_VMP_NOINLINE std::uint64_t __fastcall TopKartAceFeedObserved(
	void* context,
	const void* data,
	int length) noexcept
{
	QQSPEED_VMP_BEGIN_MUTATION("TopKart.AceFeed.Observe538");
	TopKartAceFeedFn original = gTopKartOriginalAceFeed;
	std::uint64_t result = 0;
	if (!original) {
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional aceFeed result=0 reason=missing_original");
	}
	else {
		const bool structurallyValid =
			IsStructurallyValidAceFeed(data, length);
		result = original(context, data, length);
		LONG feedCount = InterlockedCompareExchange(
			&gTopKartValidAceFeedCount,
			0,
			0);
		if (structurallyValid) {
			feedCount = InterlockedIncrement(&gTopKartValidAceFeedCount);
		}
		if (!structurallyValid || feedCount == 1) {
			qqspeed::logging::Logf(
				"topkart_anticheck_conditional aceFeed length=%d structurallyValid=%d feedCount=%ld nativeResult=0x%llX",
				length,
				structurallyValid ? 1 : 0,
				feedCount,
				static_cast<unsigned long long>(result));
		}
	}
	QQSPEED_VMP_END();
	return result;
}

QQSPEED_VMP_NOINLINE void __fastcall TopKartAntiCheckerConditionalExecute(
	void* self) noexcept
{
	QQSPEED_VMP_BEGIN_ULTRA("TopKart.AntiChecker.ExecuteGate");
	const LONG feedCount = InterlockedCompareExchange(
		&gTopKartValidAceFeedCount,
		0,
		0);
	TopKartAntiCheckerExecuteFn original = gTopKartOriginalAntiCheckerExecute;
	if (!original) {
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional execute nativeCalled=0 reason=missing_original feedCount=%ld",
			feedCount);
	}
	else if (feedCount <= 0) {
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional execute nativeCalled=0 reason=no_valid_s2c_538 feedCount=%ld",
			feedCount);
	}
	else {
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional execute nativeCalled=1 reason=valid_s2c_538 feedCount=%ld",
			feedCount);
		original(self);
	}
	QQSPEED_VMP_END();
}

QQSPEED_VMP_NOINLINE bool VerifyTopKartAntiCheckerSlots(
	PVOID volatile* aceFeedSlot,
	PVOID volatile* executeSlot) noexcept
{
	QQSPEED_VMP_BEGIN_ULTRA("TopKart.AntiChecker.VerifySlots");
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
		installedFeed == reinterpret_cast<PVOID>(&TopKartAceFeedObserved) &&
		installedExecute ==
			reinterpret_cast<PVOID>(&TopKartAntiCheckerConditionalExecute);
	QQSPEED_VMP_END();
	return installed;
}

#if !defined(QQSPEED_VMP_PROTECTION)
void ObserveTopKartAceInterfaceCandidates(HMODULE module)
{
	struct Candidate
	{
		std::uintptr_t rva = 0;
		std::size_t hits = 0;
		std::uintptr_t interfacePointer = 0;
		PVOID interfaceContext = nullptr;
		PVOID aceFeed = nullptr;
		const wchar_t* ownerName = nullptr;
	};

	struct ModuleRange
	{
		const wchar_t* name;
		std::uintptr_t begin;
		std::uintptr_t end;
	};

	constexpr std::array<const wchar_t*, 6> kAceModuleNames = {
		L"ACE-Base64.dll",
		L"ACE-PBC-Game64.dll",
		L"ACE-DFS64.dll",
		L"ACE-ATS64.dll",
		L"ACE-CSI64.dll",
		L"SGuardAgent64.dll"
	};
	constexpr ULONGLONG kObserveMs = 60000;
	constexpr DWORD kPollMs = 1000;
	constexpr std::size_t kStableHits = 3;
	const auto base = reinterpret_cast<const BYTE*>(module);
	const std::uintptr_t moduleBegin =
		reinterpret_cast<std::uintptr_t>(module);
	const std::uintptr_t moduleEnd =
		moduleBegin + kTopKartCurrentImageSize;
	const ULONGLONG started = GetTickCount64();
	std::vector<Candidate> candidates;

	while (GetTickCount64() - started < kObserveMs) {
		std::vector<ModuleRange> aceModules;
		for (const wchar_t* name : kAceModuleNames) {
			HMODULE aceModule = GetModuleHandleW(name);
			const std::size_t aceSize = GetModuleImageSize(aceModule);
			if (!aceModule || aceSize == 0) {
				continue;
			}
			const std::uintptr_t begin =
				reinterpret_cast<std::uintptr_t>(aceModule);
			aceModules.push_back({ name, begin, begin + aceSize });
		}

		for (std::uintptr_t rva = kTopKartDataBssBeginRva;
			rva + sizeof(std::uintptr_t) <= kTopKartDataEndRva;
			rva += sizeof(std::uintptr_t)) {
			std::uintptr_t interfacePointer = 0;
			if (!SafeReadSelf(
					&interfacePointer,
					base + rva,
					sizeof(interfacePointer)) ||
				interfacePointer < 0x10000 ||
				interfacePointer >= 0x0000800000000000ULL ||
				(interfacePointer >= moduleBegin &&
				 interfacePointer < moduleEnd)) {
				continue;
			}

			MEMORY_BASIC_INFORMATION interfaceMemory = {};
			if (VirtualQuery(
					reinterpret_cast<const void*>(interfacePointer),
					&interfaceMemory,
					sizeof(interfaceMemory)) != sizeof(interfaceMemory) ||
				interfaceMemory.State != MEM_COMMIT ||
				interfaceMemory.Type != MEM_PRIVATE ||
				(interfaceMemory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				continue;
			}

			PVOID interfaceContext = nullptr;
			PVOID aceFeed = nullptr;
			if (!SafeReadSelf(
					&interfaceContext,
					reinterpret_cast<const void*>(interfacePointer),
					sizeof(interfaceContext)) ||
				!interfaceContext ||
				!SafeReadSelf(
					&aceFeed,
					reinterpret_cast<const void*>(
						interfacePointer + kTopKartAceFeedSlotOffset),
					sizeof(aceFeed))) {
				continue;
			}

			const std::uintptr_t feedAddress =
				reinterpret_cast<std::uintptr_t>(aceFeed);
			const auto owner = std::find_if(
				aceModules.begin(),
				aceModules.end(),
				[feedAddress](const ModuleRange& range) {
					return feedAddress >= range.begin &&
						feedAddress < range.end;
				});
			if (owner == aceModules.end()) {
				continue;
			}

			auto found = std::find_if(
				candidates.begin(),
				candidates.end(),
				[rva](const Candidate& candidate) {
					return candidate.rva == rva;
				});
			if (found == candidates.end()) {
				candidates.push_back({
					rva, 1, interfacePointer, interfaceContext, aceFeed,
					owner->name
				});
				found = candidates.end() - 1;
			}
			else {
				++found->hits;
				found->interfacePointer = interfacePointer;
				found->interfaceContext = interfaceContext;
				found->aceFeed = aceFeed;
				found->ownerName = owner->name;
			}

			if (found->hits == kStableHits) {
				qqspeed::logging::Logf(
					"topkart_ace_candidate_stable globalRva=0x%llX hits=%llu interface=0x%llX context=%p feed=%p owner=%ls patchApplied=0",
					static_cast<unsigned long long>(found->rva),
					static_cast<unsigned long long>(found->hits),
					static_cast<unsigned long long>(found->interfacePointer),
					found->interfaceContext,
					found->aceFeed,
					found->ownerName);
			}
		}
		Sleep(kPollMs);
	}

	std::sort(
		candidates.begin(),
		candidates.end(),
		[](const Candidate& lhs, const Candidate& rhs) {
			return lhs.hits > rhs.hits;
		});
	for (const Candidate& candidate : candidates) {
		qqspeed::logging::Logf(
			"topkart_ace_candidate_summary globalRva=0x%llX hits=%llu interface=0x%llX context=%p feed=%p owner=%ls",
			static_cast<unsigned long long>(candidate.rva),
			static_cast<unsigned long long>(candidate.hits),
			static_cast<unsigned long long>(candidate.interfacePointer),
			candidate.interfaceContext,
			candidate.aceFeed,
			candidate.ownerName);
	}
	qqspeed::logging::Logf(
		"topkart_ace_candidate_scan complete candidates=%llu observedMs=%llu patchApplied=0",
		static_cast<unsigned long long>(candidates.size()),
		static_cast<unsigned long long>(GetTickCount64() - started));
}
#endif

bool PatchTopKartAntiCheckerConditionalGate(HMODULE module)
{
	const std::size_t imageSize = GetModuleImageSize(module);
	const std::wstring path = GetModulePath(module);
	const ULONGLONG hashStarted = GetTickCount64();
	const std::string md5 = path.empty() ? "path_failed" : Md5File(path);
	const ULONGLONG hashMs = GetTickCount64() - hashStarted;
	const bool fingerprintAccepted =
		imageSize == kTopKartCurrentImageSize &&
		_stricmp(md5.c_str(), kTopKartCurrentMd5) == 0;
	qqspeed::logging::Logf(
		"topkart_anticheck_conditional_preflight accepted=%d imageSize=0x%llX md5=%s hashMs=%llu",
		fingerprintAccepted ? 1 : 0,
		static_cast<unsigned long long>(imageSize),
		md5.c_str(),
		static_cast<unsigned long long>(hashMs));
	if (!fingerprintAccepted) {
		return false;
	}

	struct ScopedConditionalRvas
	{
		std::uintptr_t execute = 0;
		std::uintptr_t executeSlot = 0;
		std::uintptr_t aceInterfaceGlobal = 0;

		~ScopedConditionalRvas()
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
	if (!profileResolved ||
		!rvas.execute ||
		!rvas.executeSlot ||
		!rvas.aceInterfaceGlobal) {
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional result=0 reason=protected_profile_resolution_failed");
		return false;
	}
#else
	rvas.execute = kTopKartAntiCheckerExecuteRva;
	rvas.executeSlot = kTopKartAntiCheckerExecuteSlotRva;
	rvas.aceInterfaceGlobal = kTopKartAceInterfaceGlobalRva;
#endif

	const auto rvaInImage = [imageSize](std::uintptr_t rva, std::size_t size) {
		return rva != 0 &&
			rva < imageSize &&
			size <= imageSize - rva;
	};
	if (!rvaInImage(rvas.execute, kTopKartAntiCheckerOriginal.size()) ||
		!rvaInImage(rvas.executeSlot, sizeof(PVOID)) ||
		!rvaInImage(rvas.aceInterfaceGlobal, sizeof(PVOID))) {
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional result=0 reason=rva_out_of_range");
		return false;
	}

	auto* execute = reinterpret_cast<BYTE*>(module) + rvas.execute;
	std::array<BYTE, kTopKartAntiCheckerOriginal.size()> executeBytes = {};
	if (!SafeReadSelf(
			executeBytes.data(),
			execute,
			executeBytes.size()) ||
		executeBytes != kTopKartAntiCheckerOriginal ||
		!IsExecutableAddress(execute)) {
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional result=0 reason=main_entry_mismatch");
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
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional result=0 reason=main_slot_not_original");
		return false;
	}

	const ULONGLONG interfaceWaitStarted = GetTickCount64();
	std::uintptr_t interfacePointer = 0;
	PVOID interfaceContext = nullptr;
	PVOID aceFeed = nullptr;
	while (GetTickCount64() - interfaceWaitStarted <
		kTopKartAceInterfaceWaitMs) {
		interfacePointer = 0;
		interfaceContext = nullptr;
		aceFeed = nullptr;
		const bool interfaceReady =
			SafeReadSelf(
				&interfacePointer,
				reinterpret_cast<const BYTE*>(module) +
					rvas.aceInterfaceGlobal,
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
					interfacePointer + kTopKartAceFeedSlotOffset),
				sizeof(aceFeed)) &&
			IsExecutableAddress(aceFeed);
		if (interfaceReady) {
			break;
		}
		Sleep(GetTickCount64() - interfaceWaitStarted < 15000 ? 1 : 5);
	}
	if (!interfacePointer || !interfaceContext || !IsExecutableAddress(aceFeed)) {
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional result=0 reason=ace_interface_timeout waitedMs=%llu",
			static_cast<unsigned long long>(
				GetTickCount64() - interfaceWaitStarted));
		return false;
	}

	auto* aceFeedSlot = reinterpret_cast<PVOID volatile*>(
		interfacePointer + kTopKartAceFeedSlotOffset);
	gTopKartOriginalAceFeed = reinterpret_cast<TopKartAceFeedFn>(aceFeed);
	gTopKartOriginalAntiCheckerExecute =
		reinterpret_cast<TopKartAntiCheckerExecuteFn>(execute);
	MemoryBarrier();

	if (!ExchangePointerSlot(
			aceFeedSlot,
			aceFeed,
			reinterpret_cast<PVOID>(&TopKartAceFeedObserved))) {
		gTopKartOriginalAceFeed = nullptr;
		gTopKartOriginalAntiCheckerExecute = nullptr;
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional result=0 reason=ace_feed_slot_patch");
		return false;
	}

	if (!ExchangePointerSlot(
			executeSlot,
			execute,
			reinterpret_cast<PVOID>(
				&TopKartAntiCheckerConditionalExecute))) {
		const bool feedRolledBack = RestorePointerSlot(
			aceFeedSlot,
			reinterpret_cast<PVOID>(&TopKartAceFeedObserved),
			aceFeed);
		gTopKartOriginalAceFeed = nullptr;
		gTopKartOriginalAntiCheckerExecute = nullptr;
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional result=0 reason=main_slot_patch feedRolledBack=%d",
			feedRolledBack ? 1 : 0);
		return false;
	}

	const bool installed =
		VerifyTopKartAntiCheckerSlots(aceFeedSlot, executeSlot);
	if (!installed) {
		const bool executeRolledBack = RestorePointerSlot(
			executeSlot,
			reinterpret_cast<PVOID>(
				&TopKartAntiCheckerConditionalExecute),
			execute);
		const bool feedRolledBack = RestorePointerSlot(
			aceFeedSlot,
			reinterpret_cast<PVOID>(&TopKartAceFeedObserved),
			aceFeed);
		gTopKartOriginalAceFeed = nullptr;
		gTopKartOriginalAntiCheckerExecute = nullptr;
		qqspeed::logging::Logf(
			"topkart_anticheck_conditional result=0 reason=post_patch_verify executeRolledBack=%d feedRolledBack=%d",
			executeRolledBack ? 1 : 0,
			feedRolledBack ? 1 : 0);
		return false;
	}

	gTopKartAceFeedSlot = aceFeedSlot;
	gTopKartAntiCheckerExecuteSlot = executeSlot;
	qqspeed::logging::Logf(
		"topkart_anticheck_conditional result=1 patchKind=two_pointer_slots validFeedRequired=1 childPenaltyCodeOriginal=1 hotPath=ace_feed_and_one_shot_execute waitedMs=%llu",
		static_cast<unsigned long long>(
			GetTickCount64() - interfaceWaitStarted));
	return true;
}

bool WaitForTopKartAndInstallConsumers(
	const qqspeed::config::HookConfig& config)
{
	constexpr DWORD kWaitMs = 180000;
	constexpr DWORD kPollMs = 100;
	DWORD waited = 0;
	HMODULE module = nullptr;
	GetModuleHandleExW(0, config.topKartModuleName, &module);
	while (!module && waited < kWaitMs) {
		Sleep(kPollMs);
		waited += kPollMs;
		GetModuleHandleExW(0, config.topKartModuleName, &module);
	}
	if (!module) {
		qqspeed::logging::Logf(
			"topkart_hook_wait result=0 waitedMs=%lu reason=module_not_loaded",
			waited);
		return false;
	}

	qqspeed::logging::Logf(
		"topkart_hook_wait result=1 waitedMs=%lu base=0x%llX refHeld=1",
		waited,
		static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(module)));
	if (!ValidateTopKartModule(module)) {
		FreeLibrary(module);
		qqspeed::logging::Logf(
			"topkart_hook_gate result=0 waitedMs=%lu refHeld=0 reason=module_preflight",
			waited);
		return false;
	}
	const bool antiCheckerPatched =
		!config.features.enableTopKartAntiCheckerConditional ||
		PatchTopKartAntiCheckerConditionalGate(module);
	const bool kartRefitPatched =
		!config.features.enableKartRefitShadowCache ||
		qqspeed::kart_refit::InstallTopKartConsumerHooks(module);
	const bool patched = antiCheckerPatched && kartRefitPatched;
	if (patched) {
		gTopKartAntiCheckerModuleReference = module;
	}
	else {
		FreeLibrary(module);
	}
	qqspeed::logging::Logf(
		"topkart_hook_gate result=%d antiChecker=%d kartRefit=%d waitedMs=%lu refHeld=%d",
		patched ? 1 : 0,
		antiCheckerPatched ? 1 : 0,
		kartRefitPatched ? 1 : 0,
		waited,
		patched ? 1 : 0);
	return patched;
}

void LogModuleFingerprint(const wchar_t* moduleName, const char* logName)
{
	HMODULE module = GetModuleHandleW(moduleName);
	if (!module) {
		qqspeed::logging::Logf("%s base=0x0 md5=unloaded", logName);
		return;
	}

	const std::wstring path = GetModulePath(module);
	const std::string md5 = path.empty() ? "path_failed" : Md5File(path);
	qqspeed::logging::Logf(
		"%s base=0x%llX md5=%s",
		logName,
		static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(module)),
		md5.c_str());
}

HMODULE WaitForNetworkModule(const qqspeed::config::HookConfig& config)
{
	constexpr DWORD kWaitMs = 30000;
	constexpr DWORD kFallbackPollMs = 10;
	HANDLE loadedEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	auto registerNotification = ntdll ? reinterpret_cast<LdrRegisterDllNotificationFn>(
		GetProcAddress(ntdll, "LdrRegisterDllNotification")) : nullptr;
	auto unregisterNotification = ntdll ? reinterpret_cast<LdrUnregisterDllNotificationFn>(
		GetProcAddress(ntdll, "LdrUnregisterDllNotification")) : nullptr;
	void* notificationCookie = nullptr;
	const bool notificationRegistered = loadedEvent && registerNotification && unregisterNotification &&
		registerNotification(0, &OnLoaderDllNotification, loadedEvent, &notificationCookie) >= 0;
	qqspeed::logging::Logf(
		"network loader notification registered=%d event=%p cookie=%p",
		notificationRegistered ? 1 : 0,
		loadedEvent,
		notificationCookie);

	HMODULE networkModule = GetModuleHandleW(config.networkModuleName);
	if (!networkModule && notificationRegistered) {
		WaitForSingleObject(loadedEvent, kWaitMs);
		networkModule = GetModuleHandleW(config.networkModuleName);
	}
	if (!networkModule && !notificationRegistered) {
		DWORD waited = 0;
		while (!networkModule && waited <= kWaitMs) {
			Sleep(kFallbackPollMs);
			waited += kFallbackPollMs;
			networkModule = GetModuleHandleW(config.networkModuleName);
		}
	}
	if (notificationRegistered) {
		unregisterNotification(notificationCookie);
	}
	if (loadedEvent) {
		CloseHandle(loadedEvent);
	}
	if (networkModule) {
		return networkModule;
	}

	qqspeed::logging::Logf("Network.dll was not loaded after %lu ms; network hooks skipped", kWaitMs);
	return nullptr;
}

bool InstallNetworkHooksIfEnabled(HMODULE networkModule, const qqspeed::config::HookConfig& config)
{
	if (!config.features.enableNetworkHooks) {
		qqspeed::logging::Logf("network hooks disabled");
		return false;
	}
	if (!networkModule) {
		return false;
	}

#if defined(QQSPEED_VMP_PROTECTION)
	qqspeed::network::ResolvedNetworkProfile profile = {};
	const bool resolved = qqspeed::network::ResolveProtectedNetworkProfile(
		reinterpret_cast<std::uintptr_t>(networkModule),
		GetModuleImageSize(networkModule),
		profile);
	if (!resolved) {
		qqspeed::network::ClearResolvedNetworkProfile(profile);
		qqspeed::logging::Logf("network protected profile resolution failed");
		return false;
	}

	const bool installed = qqspeed::network::InstallNetworkHooks(networkModule, config, profile);
	qqspeed::network::ClearResolvedNetworkProfile(profile);
	return installed;
#else
	return qqspeed::network::InstallNetworkHooks(networkModule, config);
#endif
}

bool InstallConnectRedirectIfEnabled(HMODULE networkModule, const qqspeed::config::HookConfig& config)
{
	if (!config.features.enableConnectRedirect) {
		return false;
	}
	if (!networkModule) {
		qqspeed::logging::Logf("connect redirect install skipped: Network.dll unavailable");
		return false;
	}

	return qqspeed::network::InstallConnectRedirectHook(networkModule);
}

std::size_t ExpectedNetworkCallSitePatchCount(const qqspeed::config::HookConfig& config)
{
	std::size_t expected = 0;
	if (config.features.enableNetworkUnpackMsg100ConnIdPatch) {
#if defined(QQSPEED_VMP_PROTECTION)
		expected += qqspeed::network::kNetworkUnpackCallSiteCount;
#else
		expected += config.network.unpackCallSites.count;
#endif
	}
	if (config.features.enableNetworkPackSerializerBodyRepair || config.features.enablePacketFlowLog) {
#if defined(QQSPEED_VMP_PROTECTION)
		expected += qqspeed::network::kNetworkPackCallSiteCount;
#else
		expected += config.network.packCallSites.count;
#endif
	}
	if (config.features.enableNetworkSerializeBodyRepair) {
#if defined(QQSPEED_VMP_PROTECTION)
		expected += qqspeed::network::kNetworkSerializeCallSiteCount;
#else
		expected += config.network.serializeBodyCallSites.count;
#endif
	}
	if (config.features.enableNetworkSpecial28346DecodeHook) {
#if defined(QQSPEED_VMP_PROTECTION)
		expected += qqspeed::network::kNetworkSpecialDecodeCallSiteCount;
#else
		expected += config.network.special28346DecodeCallSites.count;
#endif
	}
	if (config.features.enableNetworkFilterProtocolConnIdAlign) {
#if defined(QQSPEED_VMP_PROTECTION)
		expected += qqspeed::network::kNetworkFilterCallSiteCount;
#else
		expected += config.network.filterProtocolInfoCallSites.count;
#endif
	}
	if (config.features.enableNetworkInnerDispatchPlainExpand) {
#if defined(QQSPEED_VMP_PROTECTION)
		expected += qqspeed::network::kNetworkInnerDispatchCallSiteCount;
#else
		expected += config.network.innerDispatchCallSites.count;
#endif
	}
	if (config.features.enableKartRefitShadowCache) {
		expected += 3;
	}
	return expected;
}

bool WaitForNetworkAndInstallHooks(const qqspeed::config::HookConfig& config)
{
	HMODULE networkModule = WaitForNetworkModule(config);
	if (!networkModule) {
		return false;
	}
	if (!ValidateNetworkModule(networkModule, config)) {
		qqspeed::logging::Logf("network hook install blocked by module preflight");
		return false;
	}

	const bool networkHooksInstalled = InstallNetworkHooksIfEnabled(networkModule, config);
	const bool hooksReady = !config.features.enableNetworkHooks || networkHooksInstalled;
	// Make the scoped redirect ready as soon as the required Network.dll
	// compatibility hooks are exact.  Top-Kart may load noticeably later; it
	// must not hold the first directory connection behind its wait/hash path.
	const bool redirectInstalled = hooksReady && InstallConnectRedirectIfEnabled(networkModule, config);
	const bool redirectReady = !config.features.enableConnectRedirect || redirectInstalled;
	if (!hooksReady && config.features.enableConnectRedirect) {
		qqspeed::logging::Logf("connect redirect skipped: required Network hooks are incomplete");
	}
	if (!redirectReady) {
		const bool redirectRolledBack = qqspeed::network::UninstallConnectRedirectHook();
		const std::size_t expectedRollback = networkHooksInstalled
			? ExpectedNetworkCallSitePatchCount(config)
			: 0;
		const std::size_t rolledBack = networkHooksInstalled
			? qqspeed::network::patch::RollbackAllCallSites()
			: 0;
		const bool networkRollbackComplete = rolledBack == expectedRollback;
		const bool rollbackComplete = redirectRolledBack && networkRollbackComplete;
		qqspeed::logging::Logf(
			"connect redirect failed; redirectRolledBack=%d NetworkRolledBack=%llu NetworkExpected=%llu rollbackComplete=%d partial=%d fatal=%d",
			redirectRolledBack ? 1 : 0,
			static_cast<unsigned long long>(rolledBack),
			static_cast<unsigned long long>(expectedRollback),
			rollbackComplete ? 1 : 0,
			rollbackComplete ? 0 : 1,
			rollbackComplete ? 0 : 1);
		qqspeed::logging::Logf(
			"network_ready_result redirect=0 hooks=0 antiChecker=0 exactReady=0 rollbackComplete=%d partial=%d",
			rollbackComplete ? 1 : 0,
			rollbackComplete ? 0 : 1);
		return false;
	}

	const bool kartRefitNetworkReady =
		!config.features.enableKartRefitShadowCache ||
		qqspeed::kart_refit::InstallNetworkMergeHook(networkModule);
	if (!kartRefitNetworkReady) {
		qqspeed::network::UninstallConnectRedirectHook();
		const std::size_t rolledBack =
			qqspeed::network::patch::RollbackAllCallSites();
		qqspeed::logging::Logf(
			"kart_refit_shadow blocked initialization; rollback=%llu",
			static_cast<unsigned long long>(rolledBack));
		return false;
	}

	bool topKartReady = true;
	if (config.features.enableTopKartAntiCheckerConditional ||
		config.features.enableKartRefitShadowCache) {
		topKartReady = WaitForTopKartAndInstallConsumers(config);
	}
	if (!topKartReady) {
		const bool redirectRolledBack = !redirectInstalled ||
			qqspeed::network::UninstallConnectRedirectHook();
		const std::size_t expectedRollback = ExpectedNetworkCallSitePatchCount(config);
		const std::size_t rolledBack = qqspeed::network::patch::RollbackAllCallSites();
		const bool networkRollbackComplete = rolledBack == expectedRollback;
		const bool rollbackComplete = redirectRolledBack && networkRollbackComplete;
		qqspeed::logging::Logf(
			"topkart_hook_gate blocked initialization; redirectRolledBack=%d Network hooks rolledBack=%llu expected=%llu rollbackComplete=%d partial=%d fatal=%d",
			redirectRolledBack ? 1 : 0,
			static_cast<unsigned long long>(rolledBack),
			static_cast<unsigned long long>(expectedRollback),
			rollbackComplete ? 1 : 0,
			rollbackComplete ? 0 : 1,
			rollbackComplete ? 0 : 1);
		return false;
	}
	qqspeed::logging::Logf(
		"network_ready_result redirect=%d hooks=%d topKart=%d exactReady=%d",
		redirectReady ? 1 : 0,
		hooksReady ? 1 : 0,
		topKartReady ? 1 : 0,
		(redirectReady && hooksReady && topKartReady) ? 1 : 0);
	return redirectReady && hooksReady && topKartReady;
}

DWORD WINAPI RuntimeThread(void* parameter)
{
	const auto selfModule = static_cast<HMODULE>(parameter);
	const ULONGLONG initializationStarted = GetTickCount64();

	qqspeed::logging::Initialize(selfModule);
	qqspeed::logging::Logf("clean hook loaded");

	const qqspeed::config::HookConfig& config = qqspeed::config::ActiveConfig();
#if defined(QQSPEED_VMP_PROTECTION)
	qqspeed::logging::Logf("hook_config protected_profile=1");
#else
	qqspeed::logging::Logf(
		"hook_config exceptionTrace=%d networkHooks=%d connectRedirect=%d packetFlowLog=%d antiCheckerConditional=%d exitTrace=%d handlerUiTrace=%d worldBoundGuard=%d luaTableGuard=%d networkMaxSize=0x%llX unpack=0x%llX pack=0x%llX serialize=0x%llX special=0x%llX filter=0x%llX inner=0x%llX socketCachedDst=0x%llX",
		config.features.enableProcessExceptionTrace ? 1 : 0,
		config.features.enableNetworkHooks ? 1 : 0,
		config.features.enableConnectRedirect ? 1 : 0,
		config.features.enablePacketFlowLog ? 1 : 0,
		config.features.enableTopKartAntiCheckerConditional ? 1 : 0,
		config.features.enableTopKartExitTraceProbe ? 1 : 0,
		config.features.enableTopKartHandlerUiTrace ? 1 : 0,
		config.features.enableTopKartWorldBoundGuard ? 1 : 0,
		config.features.enableTopKartLuaTableGuard ? 1 : 0,
		static_cast<unsigned long long>(config.network.maxKnownImageSize),
		static_cast<unsigned long long>(config.network.unpack),
		static_cast<unsigned long long>(config.network.pack),
		static_cast<unsigned long long>(config.network.serializeBody),
		static_cast<unsigned long long>(config.network.special28346Decode),
		static_cast<unsigned long long>(config.network.filterProtocolInfo),
		static_cast<unsigned long long>(config.network.innerDispatch),
		static_cast<unsigned long long>(config.network.socketCachedDstIdStateOffset));
#endif
	qqspeed::diagnostics::StartProcessExceptionTrace(selfModule, config);

	const bool networkReady = WaitForNetworkAndInstallHooks(config);
	if (!networkReady) {
		qqspeed::logging::Logf(
			"hook initialization incomplete elapsedMs=%llu; optional probes remain disabled",
			static_cast<unsigned long long>(GetTickCount64() - initializationStarted));
		return 1;
	}
	qqspeed::logging::Logf(
		"hook initialization complete elapsedMs=%llu",
		static_cast<unsigned long long>(GetTickCount64() - initializationStarted));

	// Optional Top-Kart instrumentation is never part of login readiness.
	// The official/stability profile keeps all fixed-RVA mutations disabled.
	qqspeed::topkart::StartChangeItemProbeThread(config);
	qqspeed::topkart::StartExitTraceProbeThread(config);
	qqspeed::topkart::StartMapIdProbeThread(config);
	qqspeed::topkart::StartWorldBoundGuardThread(config);
	qqspeed::topkart::StartLuaTableGuardThread(config);

	qqspeed::logging::Logf(
		"knownExcludedIds=%s",
		qqspeed::network::KnownNetworkExcludedIdsText());

	return 0;
}
}

namespace qqspeed::runtime
{
void Start(HMODULE module)
{
	HANDLE thread = CreateThread(nullptr, 0, &RuntimeThread, module, 0, nullptr);
	if (thread != nullptr) {
		CloseHandle(thread);
	}
}
}
