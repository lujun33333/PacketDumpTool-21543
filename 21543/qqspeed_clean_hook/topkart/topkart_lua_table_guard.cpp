#include "pch.h"

#include "topkart/topkart_lua_table_guard.h"

#include "logging/hook_log.h"
#include "network/code_patch.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
constexpr DWORD kTopKartWaitMs = 120000;
constexpr DWORD kTopKartPollMs = 250;

constexpr std::uintptr_t kTopKartKnownImageSize = 0x3101000;
constexpr std::uintptr_t kLuaTableHashLookupRva = 0x1C698C0;
constexpr std::uintptr_t kLuaNilValueRva = 0x2025D30;

constexpr std::uintptr_t kLuaHashLookupCallRvas[] = {
	0x1C6967E,
	0x1C6A1BD,
	0x1C6C1E4,
	0x1C6C629,
	0x1C6C77A,
	0x1C6C9FF,
	0x1C6FD7B,
	0x1C6FDD9,
	0x1C701E2,
	0x1C70450,
};

constexpr std::uintptr_t kLuaTableHashBitsOffset = 0x0B;
constexpr std::uintptr_t kLuaTableNodeBaseOffset = 0x18;
constexpr std::uintptr_t kLuaKeyHashOffset = 0x0C;
constexpr std::uintptr_t kLuaNodeValueTagOffset = 0x08;
constexpr std::uintptr_t kLuaNodeKeyTagOffset = 0x09;
constexpr std::uintptr_t kLuaNodeNextOffset = 0x0C;
constexpr std::uintptr_t kLuaNodeKeyObjectOffset = 0x10;
constexpr std::uintptr_t kLuaNodeSize = 0x18;
constexpr unsigned char kLuaTableKeyTag = 0x44;
constexpr unsigned int kMaxLuaHashBits = 26;
constexpr unsigned int kMaxLuaHashChainSteps = 512;

struct ProbeThreadContext
{
	qqspeed::config::HookConfig config;
};

struct LookupSnapshot
{
	void* table = nullptr;
	void* key = nullptr;
	void* nodeBase = nullptr;
	void* node = nullptr;
	unsigned int hashBits = 0;
	std::int32_t keyHash = 0;
	std::int32_t chainStep = 0;
	unsigned int chainDepth = 0;
	DWORD exceptionCode = 0;
	void* exceptionAddress = nullptr;
	ULONG_PTR exceptionInfo0 = 0;
	void* exceptionInfo1 = nullptr;
};

using LuaHashLookupFn = void* (__fastcall*)(void* table, void* key);

std::uintptr_t g_topKartBase = 0;
volatile LONG g_installState = 0;
volatile LONG g_guardEventCount = 0;
std::size_t g_patchedCalls = 0;

void* LuaNilValue()
{
	if (!g_topKartBase) {
		return nullptr;
	}
	return reinterpret_cast<void*>(g_topKartBase + kLuaNilValueRva);
}

std::uintptr_t TopKartCallRvaFromReturnAddress(void* returnAddress)
{
	const std::uintptr_t ret = reinterpret_cast<std::uintptr_t>(returnAddress);
	if (!g_topKartBase || ret < g_topKartBase) {
		return 0;
	}

	const std::uintptr_t retRva = ret - g_topKartBase;
	return retRva >= 5 ? retRva - 5 : 0;
}

bool ShouldLogCount(LONG count)
{
	return count <= 64 || (count > 0 && (count & (count - 1)) == 0);
}

void FormatTopKartStack(char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}

	out[0] = 0;
	void* frames[24] = {};
	const USHORT count = RtlCaptureStackBackTrace(1, static_cast<DWORD>(_countof(frames)), frames, nullptr);
	std::size_t used = 0;
	for (USHORT i = 0; i < count; ++i) {
		char part[48] = {};
		const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(frames[i]);
		if (g_topKartBase && address >= g_topKartBase && address < g_topKartBase + kTopKartKnownImageSize) {
			sprintf_s(part, "TK+0x%llX", static_cast<unsigned long long>(address - g_topKartBase));
		} else {
			sprintf_s(part, "%p", frames[i]);
		}

		const std::size_t len = strlen(part);
		const std::size_t sep = used ? 1 : 0;
		if (used + sep + len + 1 >= outSize) {
			break;
		}
		if (sep) {
			out[used++] = '>';
		}
		memcpy(out + used, part, len);
		used += len;
		out[used] = 0;
	}
}

LONG CaptureLookupException(PEXCEPTION_POINTERS pointers, LookupSnapshot* snapshot)
{
	if (snapshot && pointers && pointers->ExceptionRecord) {
		snapshot->exceptionCode = pointers->ExceptionRecord->ExceptionCode;
		snapshot->exceptionAddress = pointers->ExceptionRecord->ExceptionAddress;
		snapshot->exceptionInfo0 = pointers->ExceptionRecord->NumberParameters > 0 ?
			pointers->ExceptionRecord->ExceptionInformation[0] : 0;
		snapshot->exceptionInfo1 = pointers->ExceptionRecord->NumberParameters > 1 ?
			reinterpret_cast<void*>(pointers->ExceptionRecord->ExceptionInformation[1]) : nullptr;
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

void LogGuardEvent(const char* reason, LONG count, std::uintptr_t callRva, const LookupSnapshot& snapshot)
{
	if (!ShouldLogCount(count)) {
		return;
	}

	char stack[1024] = {};
	FormatTopKartStack(stack, sizeof(stack));

	qqspeed::logging::Logf(
		"topkart_lua_table_guard action=nil reason=%s count=%ld callRva=0x%llX table=%p key=%p "
		"nodeBase=%p node=%p hashBits=%u keyHash=0x%08X chainStep=%d chainDepth=%u "
		"exceptionCode=0x%08lX exceptionAddress=%p info0=0x%llX info1=%p stack=%s",
		reason ? reason : "unknown",
		count,
		static_cast<unsigned long long>(callRva),
		snapshot.table,
		snapshot.key,
		snapshot.nodeBase,
		snapshot.node,
		snapshot.hashBits,
		static_cast<unsigned int>(snapshot.keyHash),
		snapshot.chainStep,
		snapshot.chainDepth,
		snapshot.exceptionCode,
		snapshot.exceptionAddress,
		static_cast<unsigned long long>(snapshot.exceptionInfo0),
		snapshot.exceptionInfo1,
		stack);
}

bool AddSignedOffset(std::uintptr_t value, std::int64_t delta, std::uintptr_t* out)
{
	if (!out) {
		return false;
	}

	if (delta >= 0) {
		const std::uint64_t positive = static_cast<std::uint64_t>(delta);
		if (value > UINTPTR_MAX - positive) {
			return false;
		}
		*out = value + positive;
		return true;
	}

	const std::uint64_t negative = static_cast<std::uint64_t>(-delta);
	if (value < negative) {
		return false;
	}
	*out = value - negative;
	return true;
}

void* GuardedLookup(void* table, void* key, LookupSnapshot* snapshot, const char** reason)
{
	if (snapshot) {
		snapshot->table = table;
		snapshot->key = key;
	}
	if (!reason) {
		return LuaNilValue();
	}
	*reason = nullptr;

	const std::uintptr_t tableAddress = reinterpret_cast<std::uintptr_t>(table);
	const std::uintptr_t keyAddress = reinterpret_cast<std::uintptr_t>(key);
	if (tableAddress < 0x10000) {
		*reason = "bad_table_ptr";
		return LuaNilValue();
	}
	if (keyAddress < 0x10000) {
		*reason = "bad_key_ptr";
		return LuaNilValue();
	}

	const auto tableBytes = static_cast<const unsigned char*>(table);
	const auto keyBytes = static_cast<const unsigned char*>(key);
	const unsigned int hashBits = *reinterpret_cast<const volatile unsigned char*>(tableBytes + kLuaTableHashBitsOffset);
	const std::uintptr_t nodeBase =
		*reinterpret_cast<const volatile std::uintptr_t*>(tableBytes + kLuaTableNodeBaseOffset);
	const std::int32_t keyHash =
		*reinterpret_cast<const volatile std::int32_t*>(keyBytes + kLuaKeyHashOffset);

	if (snapshot) {
		snapshot->hashBits = hashBits;
		snapshot->nodeBase = reinterpret_cast<void*>(nodeBase);
		snapshot->keyHash = keyHash;
	}

	if (hashBits > kMaxLuaHashBits) {
		*reason = "hash_bits_out_of_range";
		return LuaNilValue();
	}
	if (nodeBase < 0x10000) {
		*reason = "bad_node_base";
		return LuaNilValue();
	}

	const std::uint64_t mask = (std::uint64_t{1} << hashBits) - 1;
	const std::uint64_t bucket = static_cast<std::uint64_t>(static_cast<std::int64_t>(keyHash)) & mask;
	std::uintptr_t nodeAddress = nodeBase + bucket * kLuaNodeSize;

	for (unsigned int depth = 0; depth < kMaxLuaHashChainSteps; ++depth) {
		const auto nodeBytes = reinterpret_cast<const unsigned char*>(nodeAddress);
		if (snapshot) {
			snapshot->node = reinterpret_cast<void*>(nodeAddress);
			snapshot->chainDepth = depth;
		}

		const unsigned char keyTag =
			*reinterpret_cast<const volatile unsigned char*>(nodeBytes + kLuaNodeKeyTagOffset);
		const std::uintptr_t nodeKeyAddress =
			*reinterpret_cast<const volatile std::uintptr_t*>(nodeBytes + kLuaNodeKeyObjectOffset);
		if (keyTag == kLuaTableKeyTag && nodeKeyAddress == keyAddress) {
			// Touch the value tag so callers do not immediately fault on the returned TValue.
			(void)*reinterpret_cast<const volatile unsigned char*>(nodeBytes + kLuaNodeValueTagOffset);
			return reinterpret_cast<void*>(nodeAddress);
		}

		const std::int32_t chainStep =
			*reinterpret_cast<const volatile std::int32_t*>(nodeBytes + kLuaNodeNextOffset);
		if (snapshot) {
			snapshot->chainStep = chainStep;
		}
		if (chainStep == 0) {
			return LuaNilValue();
		}

		std::uintptr_t nextNode = 0;
		const std::int64_t delta = static_cast<std::int64_t>(chainStep) *
			static_cast<std::int64_t>(kLuaNodeSize);
		if (!AddSignedOffset(nodeAddress, delta, &nextNode) || nextNode < 0x10000) {
			*reason = "bad_chain_step";
			return LuaNilValue();
		}
		nodeAddress = nextNode;
	}

	*reason = "chain_limit";
	return LuaNilValue();
}

__declspec(noinline) void* __fastcall DetourLuaTableHashLookup(void* table, void* key)
{
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());
	LookupSnapshot snapshot = {};
	const char* reason = nullptr;
	void* result = nullptr;

	__try
	{
		result = GuardedLookup(table, key, &snapshot, &reason);
	}
	__except (CaptureLookupException(GetExceptionInformation(), &snapshot))
	{
		reason = "exception";
		result = LuaNilValue();
	}

	if (reason) {
		const LONG count = InterlockedIncrement(&g_guardEventCount);
		LogGuardEvent(reason, count, callRva, snapshot);
	}

	return result ? result : LuaNilValue();
}

HMODULE WaitForTopKartModule(const qqspeed::config::HookConfig& config)
{
	DWORD waited = 0;
	while (waited <= kTopKartWaitMs) {
		HMODULE module = GetModuleHandleW(config.topKartModuleName);
		if (module) {
			return module;
		}
		Sleep(kTopKartPollMs);
		waited += kTopKartPollMs;
	}
	return nullptr;
}

DWORD WINAPI ProbeThread(void* parameter)
{
	std::unique_ptr<ProbeThreadContext> context(static_cast<ProbeThreadContext*>(parameter));
	const qqspeed::config::HookConfig config = context->config;

	if (!config.features.enableTopKartLuaTableGuard) {
		qqspeed::logging::Logf("topkart_lua_table_guard disabled");
		return 0;
	}

	if (InterlockedCompareExchange(&g_installState, 1, 0) != 0) {
		return 0;
	}

	HMODULE module = WaitForTopKartModule(config);
	if (!module) {
		qqspeed::logging::Logf("topkart_lua_table_guard skipped: Top-Kart.dll not loaded after %lu ms", kTopKartWaitMs);
		InterlockedExchange(&g_installState, 3);
		return 0;
	}

	g_topKartBase = reinterpret_cast<std::uintptr_t>(module);
	g_patchedCalls = qqspeed::network::patch::PatchDirectCalls(
		g_topKartBase,
		kLuaHashLookupCallRvas,
		_countof(kLuaHashLookupCallRvas),
		kLuaTableHashLookupRva,
		reinterpret_cast<void*>(&DetourLuaTableHashLookup),
		"TopKartLuaTableGuard sub_181C698C0");

	InterlockedExchange(&g_installState, g_patchedCalls ? 2 : 3);
	qqspeed::logging::Logf(
		"topkart_lua_table_guard installed=%d patchedCalls=%llu base=%p targetRva=0x%llX nilRva=0x%llX",
		g_patchedCalls ? 1 : 0,
		static_cast<unsigned long long>(g_patchedCalls),
		reinterpret_cast<void*>(g_topKartBase),
		static_cast<unsigned long long>(kLuaTableHashLookupRva),
		static_cast<unsigned long long>(kLuaNilValueRva));
	return 0;
}
}

namespace qqspeed::topkart
{
void StartLuaTableGuardThread(const qqspeed::config::HookConfig& config)
{
	if (!config.features.enableTopKartLuaTableGuard) {
		qqspeed::logging::Logf("topkart_lua_table_guard disabled");
		return;
	}

	auto context = std::make_unique<ProbeThreadContext>();
	context->config = config;

	HANDLE thread = CreateThread(nullptr, 0, &ProbeThread, context.get(), 0, nullptr);
	if (!thread) {
		qqspeed::logging::Logf("topkart_lua_table_guard thread create failed error=%lu", GetLastError());
		return;
	}

	context.release();
	CloseHandle(thread);
}
}
