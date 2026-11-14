#include "pch.h"

#include "network/kart_refit_shadow.h"

#include "logging/hook_log.h"
#include "manual_fls_slot.h"
#include "network/code_patch.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
constexpr std::size_t kRecordSize = 0x5A;
constexpr std::size_t kNativeCapacity = 1500;
constexpr std::size_t kShadowHardLimit = 4096;

// QQSpeed 21932 / 2.0.203.20 (Network.dll MD5 97d25c1c4cf7730f26c226cdc6e8bb3c).
// The merge helper and all three Msg318 page-handler call sites were migrated
// together and each direct call was revalidated against the current image.
constexpr std::uintptr_t kNetworkMergeRva = 0x4E28D0;
constexpr std::array<std::uintptr_t, 3> kNetworkMergeCallRvas = {
	0x4E4930,
	0x4E540A,
	0x4E5E75
};

// QQSpeed 21932 / 2.0.203.20 (Top-Kart.dll MD5 26b621a8dc3eaf027486081f9b67cec2).
// These are migrated as one verified chain: handler table slot -> handler ->
// record inserter call -> 0x5A-byte vector inserter.
constexpr std::uintptr_t kTopKartHandlerRva = 0x12F1800;
constexpr std::uintptr_t kTopKartHandlerSlotRva = 0x2C56F08;
constexpr std::uintptr_t kTopKartInsertRva = 0x12E4BC0;
constexpr std::uintptr_t kTopKartInsertCallRva = 0x12F18AE;
constexpr std::size_t kTopKartVectorBeginOffset = 0xB0;
constexpr std::size_t kTopKartVectorEndOffset = 0xB8;

using Record = std::array<unsigned char, kRecordSize>;
using NetworkMergeFn = unsigned short(__fastcall*)(
	void* destination,
	unsigned short currentCount,
	const void* source,
	unsigned short sourceCount);
using TopKartInsertFn = std::uintptr_t(__fastcall*)(void* manager, const void* record);
using TopKartHandlerFn = void(__fastcall*)(
	std::uintptr_t,
	void*,
	std::uintptr_t,
	std::uintptr_t,
	std::uintptr_t,
	std::uintptr_t,
	std::uintptr_t);

NetworkMergeFn g_originalNetworkMerge = nullptr;
TopKartInsertFn g_originalTopKartInsert = nullptr;
TopKartHandlerFn g_originalTopKartHandler = nullptr;
PVOID volatile* g_topKartHandlerSlot = nullptr;

std::mutex g_shadowMutex;
std::vector<Record> g_shadowRecords;

struct HandlerThreadContext
{
	const unsigned char* lastRecord = nullptr;
	bool active = false;
};

qqspeed::runtime::ManualFlsSlot<HandlerThreadContext> g_handlerContext;

unsigned int ReadKartId(const void* record)
{
	unsigned int kartId = 0;
	if (record) {
		std::memcpy(
			&kartId,
			static_cast<const unsigned char*>(record) + sizeof(unsigned int),
			sizeof(kartId));
	}
	return kartId;
}

void UpsertRecord(std::vector<Record>& records, const void* source)
{
	if (!source || records.size() >= kShadowHardLimit) {
		return;
	}
	const unsigned int kartId = ReadKartId(source);
	const auto found = std::find_if(
		records.begin(),
		records.end(),
		[kartId](const Record& record) {
			return ReadKartId(record.data()) == kartId;
		});
	if (found != records.end()) {
		std::memcpy(found->data(), source, found->size());
		return;
	}
	Record record = {};
	std::memcpy(record.data(), source, record.size());
	records.push_back(record);
}

unsigned short __fastcall HookedNetworkMerge(
	void* destination,
	unsigned short currentCount,
	const void* source,
	unsigned short sourceCount)
{
	NetworkMergeFn original = g_originalNetworkMerge;
	if (!original) {
		return currentCount;
	}

	std::size_t shadowSize = 0;
	bool reset = false;
	{
		std::lock_guard<std::mutex> lock(g_shadowMutex);
		if (destination && source && sourceCount != 0) {
			if (currentCount <= 300) {
				g_shadowRecords.clear();
				reset = true;
				const std::size_t baseCount =
					(std::min)(static_cast<std::size_t>(currentCount), kNativeCapacity);
				const auto* base = static_cast<const unsigned char*>(destination);
				for (std::size_t index = 0; index < baseCount; ++index) {
					UpsertRecord(g_shadowRecords, base + index * kRecordSize);
				}
			}

			const std::size_t pageCount =
				(std::min)(static_cast<std::size_t>(sourceCount), kNativeCapacity);
			const auto* page = static_cast<const unsigned char*>(source);
			for (std::size_t index = 0; index < pageCount; ++index) {
				UpsertRecord(g_shadowRecords, page + index * kRecordSize);
			}
		}
		shadowSize = g_shadowRecords.size();
	}

	const unsigned short result =
		original(destination, currentCount, source, sourceCount);
	if (reset || shadowSize > kNativeCapacity) {
		qqspeed::logging::Logf(
			"kart_refit_shadow merge reset=%d nativeBefore=%u page=%u nativeAfter=%u shadow=%llu",
			reset ? 1 : 0,
			static_cast<unsigned int>(currentCount),
			static_cast<unsigned int>(sourceCount),
			static_cast<unsigned int>(result),
			static_cast<unsigned long long>(shadowSize));
	}
	return result;
}

void AppendMissingShadowRecords(void* manager)
{
	if (!manager || !g_originalTopKartInsert) {
		return;
	}

	std::vector<Record> snapshot;
	{
		std::lock_guard<std::mutex> lock(g_shadowMutex);
		if (g_shadowRecords.size() <= kNativeCapacity) {
			return;
		}
		snapshot = g_shadowRecords;
	}

	auto* const managerBytes = static_cast<unsigned char*>(manager);
	unsigned char* begin = nullptr;
	unsigned char* end = nullptr;
	std::memcpy(&begin, managerBytes + kTopKartVectorBeginOffset, sizeof(begin));
	std::memcpy(&end, managerBytes + kTopKartVectorEndOffset, sizeof(end));
	if (!begin || !end || end < begin) {
		return;
	}
	const std::size_t byteCount = static_cast<std::size_t>(end - begin);
	if (byteCount % kRecordSize != 0 ||
		byteCount / kRecordSize > kShadowHardLimit) {
		return;
	}

	std::vector<unsigned int> present;
	present.reserve(byteCount / kRecordSize + snapshot.size());
	for (unsigned char* record = begin; record < end; record += kRecordSize) {
		present.push_back(ReadKartId(record));
	}

	std::size_t appended = 0;
	for (const Record& record : snapshot) {
		const unsigned int kartId = ReadKartId(record.data());
		if (std::find(present.begin(), present.end(), kartId) != present.end()) {
			continue;
		}
		g_originalTopKartInsert(manager, record.data());
		present.push_back(kartId);
		++appended;
	}

	if (appended != 0) {
		qqspeed::logging::Logf(
			"kart_refit_shadow publish native=%llu shadow=%llu appended=%llu final=%llu",
			static_cast<unsigned long long>(byteCount / kRecordSize),
			static_cast<unsigned long long>(snapshot.size()),
			static_cast<unsigned long long>(appended),
			static_cast<unsigned long long>(present.size()));
	}
}

std::uintptr_t __fastcall HookedTopKartInsert(void* manager, const void* record)
{
	TopKartInsertFn original = g_originalTopKartInsert;
	if (!original) {
		return 0;
	}
	const std::uintptr_t result = original(manager, record);
	HandlerThreadContext* context = g_handlerContext.Get();
	if (context && context->active &&
		record == context->lastRecord) {
		AppendMissingShadowRecords(manager);
	}
	return result;
}

void __fastcall HookedTopKartHandler(
	std::uintptr_t arg1,
	void* response,
	std::uintptr_t arg3,
	std::uintptr_t arg4,
	std::uintptr_t arg5,
	std::uintptr_t arg6,
	std::uintptr_t arg7)
{
	TopKartHandlerFn original = g_originalTopKartHandler;
	if (!original) {
		return;
	}

	HandlerThreadContext* context = g_handlerContext.GetOrCreate();
	if (!context) {
		original(arg1, response, arg3, arg4, arg5, arg6, arg7);
		return;
	}

	const HandlerThreadContext previous = *context;
	unsigned short count = 0;
	if (response) {
		std::memcpy(
			&count,
			static_cast<const unsigned char*>(response) + 6,
			sizeof(count));
	}
	context->active = response != nullptr && count != 0;
	context->lastRecord = context->active
		? static_cast<const unsigned char*>(response) +
			8 + (static_cast<std::size_t>(count) - 1) * kRecordSize
		: nullptr;

	original(arg1, response, arg3, arg4, arg5, arg6, arg7);
	*context = previous;
}

bool ExchangeHandlerSlot(
	PVOID volatile* slot,
	PVOID expected,
	PVOID replacement)
{
	if (!slot || !expected || !replacement) {
		return false;
	}
	DWORD oldProtection = 0;
	if (!VirtualProtect(
			const_cast<PVOID*>(slot),
			sizeof(PVOID),
			PAGE_READWRITE,
			&oldProtection)) {
		return false;
	}
	const PVOID previous =
		InterlockedCompareExchangePointer(slot, replacement, expected);
	DWORD ignoredProtection = 0;
	const BOOL restored = VirtualProtect(
		const_cast<PVOID*>(slot),
		sizeof(PVOID),
		oldProtection,
		&ignoredProtection);
	return restored != FALSE &&
		(previous == expected || previous == replacement);
}
}

namespace qqspeed::kart_refit
{
bool InstallNetworkMergeHook(HMODULE networkModule)
{
	if (!networkModule) {
		return false;
	}
	const std::uintptr_t base =
		reinterpret_cast<std::uintptr_t>(networkModule);
	g_originalNetworkMerge =
		reinterpret_cast<NetworkMergeFn>(base + kNetworkMergeRva);
	const std::size_t patched = qqspeed::network::patch::PatchDirectCalls(
		base,
		kNetworkMergeCallRvas.data(),
		kNetworkMergeCallRvas.size(),
		kNetworkMergeRva,
		reinterpret_cast<void*>(&HookedNetworkMerge),
		"KartRefit318 shadow merge");
	const bool installed = patched == kNetworkMergeCallRvas.size();
	qqspeed::logging::Logf(
		"kart_refit_shadow network_install result=%d patched=%llu/%llu targetRva=0x%llX",
		installed ? 1 : 0,
		static_cast<unsigned long long>(patched),
		static_cast<unsigned long long>(kNetworkMergeCallRvas.size()),
		static_cast<unsigned long long>(kNetworkMergeRva));
	return installed;
}

bool InstallTopKartConsumerHooks(HMODULE topKartModule)
{
	if (!topKartModule) {
		return false;
	}
	const std::uintptr_t base =
		reinterpret_cast<std::uintptr_t>(topKartModule);
	g_originalTopKartInsert =
		reinterpret_cast<TopKartInsertFn>(base + kTopKartInsertRva);
	g_originalTopKartHandler =
		reinterpret_cast<TopKartHandlerFn>(base + kTopKartHandlerRva);

	const std::size_t patchedInsert =
		qqspeed::network::patch::PatchDirectCalls(
			base,
			&kTopKartInsertCallRva,
			1,
			kTopKartInsertRva,
			reinterpret_cast<void*>(&HookedTopKartInsert),
			"KartRefit TopKart inserter");

	g_topKartHandlerSlot =
		reinterpret_cast<PVOID volatile*>(base + kTopKartHandlerSlotRva);
	const bool handlerPatched = ExchangeHandlerSlot(
		g_topKartHandlerSlot,
		reinterpret_cast<PVOID>(g_originalTopKartHandler),
		reinterpret_cast<PVOID>(&HookedTopKartHandler));
	const bool installed = patchedInsert == 1 && handlerPatched;
	qqspeed::logging::Logf(
		"kart_refit_shadow topkart_install result=%d insert=%llu/1 handlerSlot=%d handlerRva=0x%llX slotRva=0x%llX",
		installed ? 1 : 0,
		static_cast<unsigned long long>(patchedInsert),
		handlerPatched ? 1 : 0,
		static_cast<unsigned long long>(kTopKartHandlerRva),
		static_cast<unsigned long long>(kTopKartHandlerSlotRva));
	return installed;
}
}
