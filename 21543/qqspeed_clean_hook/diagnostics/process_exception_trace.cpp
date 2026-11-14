#include "pch.h"

#include "diagnostics/process_exception_trace.h"

#include "logging/hook_log.h"
#include "manual_fls_slot.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <memory>

namespace
{
constexpr DWORD kModulePollMs = 500;
constexpr DWORD kModuleWaitMs = 120000;
constexpr std::size_t kMaxLogPathChars = 1024;
constexpr std::size_t kBucketCount = 128;
constexpr std::size_t kStackScanQwords = 192;
constexpr std::size_t kMaxStackHits = 28;

constexpr DWORD kCppExceptionCode = 0xE06D7363;

struct ModuleRange
{
	std::atomic<std::uintptr_t> base{ 0 };
	std::atomic<std::uintptr_t> size{ 0 };
};

struct ExceptionBucket
{
	volatile LONG state = 0;
	DWORD code = 0;
	std::uintptr_t ip = 0;
	std::uintptr_t faultAddress = 0;
	volatile LONG count = 0;
};

struct TraceThreadContext
{
	HMODULE selfModule = nullptr;
	qqspeed::config::HookConfig config;
};

ModuleRange g_gameApp;
ModuleRange g_topKart;
ModuleRange g_network;
ModuleRange g_hook;
ExceptionBucket g_buckets[kBucketCount] = {};
WCHAR g_logPath[kMaxLogPathChars] = {};
PVOID g_vehHandle = nullptr;
volatile LONG g_installState = 0;
volatile LONG g_totalInteresting = 0;

struct VehThreadState
{
	bool inHandler = false;
};

qqspeed::runtime::ManualFlsSlot<VehThreadState> g_vehThreadState;

std::uintptr_t GetImageSize(HMODULE module)
{
	if (!module) {
		return 0;
	}

	const auto base = reinterpret_cast<const unsigned char*>(module);
	__try
	{
		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
			return 0;
		}

		const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE) {
			return 0;
		}

		return nt->OptionalHeader.SizeOfImage;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return 0;
	}
}

void UpdateModule(ModuleRange& range, HMODULE module)
{
	const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(module);
	const std::uintptr_t size = GetImageSize(module);
	if (base && size) {
		range.base.store(base, std::memory_order_relaxed);
		range.size.store(size, std::memory_order_relaxed);
	}
}

bool AddressInRange(const ModuleRange& range, std::uintptr_t address)
{
	const std::uintptr_t base = range.base.load(std::memory_order_relaxed);
	const std::uintptr_t size = range.size.load(std::memory_order_relaxed);
	return base && size && address >= base && address < base + size;
}

bool FormatModuleAddress(char* out, std::size_t outSize, std::uintptr_t address)
{
	if (!out || outSize == 0) {
		return false;
	}

	struct NamedRange
	{
		const char* prefix;
		const ModuleRange* range;
	};

	const NamedRange ranges[] = {
		{ "APP", &g_gameApp },
		{ "TK", &g_topKart },
		{ "NET", &g_network },
		{ "HOOK", &g_hook },
	};

	for (const NamedRange& item : ranges) {
		const std::uintptr_t base = item.range->base.load(std::memory_order_relaxed);
		const std::uintptr_t size = item.range->size.load(std::memory_order_relaxed);
		if (base && size && address >= base && address < base + size) {
			sprintf_s(
				out,
				outSize,
				"%s+0x%llX",
				item.prefix,
				static_cast<unsigned long long>(address - base));
			return true;
		}
	}

	sprintf_s(out, outSize, "0x%llX", static_cast<unsigned long long>(address));
	return false;
}

bool IsInterestingAddress(std::uintptr_t address)
{
	return AddressInRange(g_gameApp, address) ||
		AddressInRange(g_topKart, address) ||
		AddressInRange(g_network, address) ||
		AddressInRange(g_hook, address);
}

bool ShouldTraceCode(DWORD code, bool traceCppExceptions)
{
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION:
	case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_ILLEGAL_INSTRUCTION:
	case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_STACK_OVERFLOW:
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		return true;
	case kCppExceptionCode:
		return traceCppExceptions;
	default:
		return false;
	}
}

bool AppendText(char* out, std::size_t outSize, std::size_t* used, const char* text)
{
	if (!out || !used || !text || *used >= outSize) {
		return false;
	}

	const std::size_t len = strnlen_s(text, outSize);
	if (*used + len + 1 >= outSize) {
		return false;
	}

	memcpy(out + *used, text, len);
	*used += len;
	out[*used] = 0;
	return true;
}

bool AppendStackHit(char* out, std::size_t outSize, std::size_t* used, std::uintptr_t address, std::size_t hitIndex)
{
	char formatted[64] = {};
	if (!FormatModuleAddress(formatted, sizeof(formatted), address)) {
		return false;
	}

	if (hitIndex != 0 && !AppendText(out, outSize, used, ">")) {
		return false;
	}
	return AppendText(out, outSize, used, formatted);
}

bool ScanInterestingStack(std::uintptr_t rsp, char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return false;
	}

	out[0] = 0;
	std::size_t used = 0;
	std::size_t hits = 0;
	for (std::size_t i = 0; i < kStackScanQwords && hits < kMaxStackHits; ++i) {
		std::uintptr_t value = 0;
		const auto slot = reinterpret_cast<const std::uintptr_t*>(rsp + i * sizeof(std::uintptr_t));
		__try
		{
			value = *slot;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			break;
		}

		if (!IsInterestingAddress(value)) {
			continue;
		}

		if (!AppendStackHit(out, outSize, &used, value, hits)) {
			break;
		}
		++hits;
	}

	return hits != 0;
}

const char* AccessKind(const EXCEPTION_RECORD* record)
{
	if (!record || record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters == 0) {
		return "n/a";
	}

	switch (record->ExceptionInformation[0]) {
	case 0:
		return "read";
	case 1:
		return "write";
	case 8:
		return "execute";
	default:
		return "other";
	}
}

std::uintptr_t FaultAddress(const EXCEPTION_RECORD* record)
{
	if (!record || record->NumberParameters < 2) {
		return 0;
	}
	return static_cast<std::uintptr_t>(record->ExceptionInformation[1]);
}

std::uint32_t HashException(DWORD code, std::uintptr_t ip, std::uintptr_t faultAddress)
{
	std::uint64_t value = static_cast<std::uint64_t>(ip >> 4) ^
		(static_cast<std::uint64_t>(faultAddress) << 7) ^
		static_cast<std::uint64_t>(code);
	value ^= value >> 33;
	value *= 0xff51afd7ed558ccdULL;
	value ^= value >> 33;
	return static_cast<std::uint32_t>(value);
}

LONG CountBucket(DWORD code, std::uintptr_t ip, std::uintptr_t faultAddress)
{
	const std::uint32_t hash = HashException(code, ip, faultAddress);
	for (std::size_t probe = 0; probe < kBucketCount; ++probe) {
		ExceptionBucket& bucket = g_buckets[(hash + probe) & (kBucketCount - 1)];
		const LONG state = bucket.state;
		if (state == 2) {
			if (bucket.code == code && bucket.ip == ip && bucket.faultAddress == faultAddress) {
				return InterlockedIncrement(&bucket.count);
			}
			continue;
		}
		if (state == 1) {
			continue;
		}
		if (InterlockedCompareExchange(&bucket.state, 1, 0) == 0) {
			bucket.code = code;
			bucket.ip = ip;
			bucket.faultAddress = faultAddress;
			bucket.count = 1;
			MemoryBarrier();
			InterlockedExchange(&bucket.state, 2);
			return 1;
		}
	}

	return InterlockedIncrement(&g_totalInteresting);
}

bool ShouldLogCount(LONG count)
{
	return count <= 16 || (count > 0 && (count & (count - 1)) == 0);
}

void AppendLine(const char* line)
{
	if (!line || !line[0] || !g_logPath[0]) {
		return;
	}

	OutputDebugStringA(line);

	HANDLE file = CreateFileW(
		g_logPath,
		FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		return;
	}

	DWORD written = 0;
	WriteFile(file, line, static_cast<DWORD>(strnlen_s(line, 4096)), &written, nullptr);
	CloseHandle(file);
}

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS pointers)
{
	VehThreadState* threadState = g_vehThreadState.GetOrCreate();
	if (!threadState || threadState->inHandler) {
		return EXCEPTION_CONTINUE_SEARCH;
	}
	threadState->inHandler = true;

	__try
	{
		if (!pointers || !pointers->ExceptionRecord || !pointers->ContextRecord) {
			threadState->inHandler = false;
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const EXCEPTION_RECORD* record = pointers->ExceptionRecord;
		const CONTEXT* context = pointers->ContextRecord;
		const bool traceCppExceptions = false;
		if (!ShouldTraceCode(record->ExceptionCode, traceCppExceptions)) {
			threadState->inHandler = false;
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const std::uintptr_t ip = record->ExceptionAddress ?
			reinterpret_cast<std::uintptr_t>(record->ExceptionAddress) :
			static_cast<std::uintptr_t>(context->Rip);
		const std::uintptr_t faultAddress = FaultAddress(record);

		char stack[1536] = {};
		const bool stackInteresting = ScanInterestingStack(static_cast<std::uintptr_t>(context->Rsp), stack, sizeof(stack));
		if (!IsInterestingAddress(ip) && !stackInteresting) {
			threadState->inHandler = false;
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const LONG bucketCount = CountBucket(record->ExceptionCode, ip, faultAddress);
		if (!ShouldLogCount(bucketCount)) {
			threadState->inHandler = false;
			return EXCEPTION_CONTINUE_SEARCH;
		}

		const LONG total = InterlockedIncrement(&g_totalInteresting);
		char ipText[64] = {};
		FormatModuleAddress(ipText, sizeof(ipText), ip);

		char line[4096] = {};
		snprintf(
			line,
			sizeof(line),
			"[qqspeed_clean_hook:%lu] process_exception_trace code=0x%08lX count=%ld total=%ld tid=%lu ip=%s "
			"access=%s fault=0x%llX flags=0x%08lX rsp=0x%llX rbp=0x%llX "
			"rax=0x%llX rbx=0x%llX rcx=0x%llX rdx=0x%llX rsi=0x%llX rdi=0x%llX "
			"r8=0x%llX r9=0x%llX r10=0x%llX r11=0x%llX stack=%s\r\n",
			GetCurrentProcessId(),
			record->ExceptionCode,
			bucketCount,
			total,
			GetCurrentThreadId(),
			ipText,
			AccessKind(record),
			static_cast<unsigned long long>(faultAddress),
			record->ExceptionFlags,
			static_cast<unsigned long long>(context->Rsp),
			static_cast<unsigned long long>(context->Rbp),
			static_cast<unsigned long long>(context->Rax),
			static_cast<unsigned long long>(context->Rbx),
			static_cast<unsigned long long>(context->Rcx),
			static_cast<unsigned long long>(context->Rdx),
			static_cast<unsigned long long>(context->Rsi),
			static_cast<unsigned long long>(context->Rdi),
			static_cast<unsigned long long>(context->R8),
			static_cast<unsigned long long>(context->R9),
			static_cast<unsigned long long>(context->R10),
			static_cast<unsigned long long>(context->R11),
			stackInteresting ? stack : "");
		AppendLine(line);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
	}

	threadState->inHandler = false;
	return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI ModuleCacheThread(void* parameter)
{
	std::unique_ptr<TraceThreadContext> context(static_cast<TraceThreadContext*>(parameter));
	const qqspeed::config::HookConfig config = context->config;

	UpdateModule(g_hook, context->selfModule);
	UpdateModule(g_gameApp, GetModuleHandleW(nullptr));

	DWORD waited = 0;
	bool loggedReady = false;
	while (waited <= kModuleWaitMs) {
		UpdateModule(g_topKart, GetModuleHandleW(config.topKartModuleName));
		UpdateModule(g_network, GetModuleHandleW(config.networkModuleName));

		const bool topKartReady = g_topKart.base.load(std::memory_order_relaxed) != 0;
		const bool networkReady = g_network.base.load(std::memory_order_relaxed) != 0;
		if (!loggedReady && topKartReady && networkReady) {
			loggedReady = true;
			qqspeed::logging::Logf(
				"process_exception_trace modules app=%p/0x%llX topKart=%p/0x%llX network=%p/0x%llX hook=%p/0x%llX",
				reinterpret_cast<void*>(g_gameApp.base.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(g_gameApp.size.load(std::memory_order_relaxed)),
				reinterpret_cast<void*>(g_topKart.base.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(g_topKart.size.load(std::memory_order_relaxed)),
				reinterpret_cast<void*>(g_network.base.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(g_network.size.load(std::memory_order_relaxed)),
				reinterpret_cast<void*>(g_hook.base.load(std::memory_order_relaxed)),
				static_cast<unsigned long long>(g_hook.size.load(std::memory_order_relaxed)));
		}

		if (loggedReady) {
			Sleep(5000);
		} else {
			Sleep(kModulePollMs);
			waited += kModulePollMs;
		}
	}

	return 0;
}
}

namespace qqspeed::diagnostics
{
void StartProcessExceptionTrace(HMODULE selfModule, const qqspeed::config::HookConfig& config)
{
	if (!config.features.enableProcessExceptionTrace) {
		qqspeed::logging::Logf("process_exception_trace disabled");
		return;
	}

	if (InterlockedCompareExchange(&g_installState, 1, 0) != 0) {
		return;
	}

	const std::wstring path = qqspeed::logging::LogPath();
	if (!path.empty()) {
		wcsncpy_s(g_logPath, path.c_str(), _TRUNCATE);
	}

	UpdateModule(g_hook, selfModule);
	UpdateModule(g_gameApp, GetModuleHandleW(nullptr));

	g_vehHandle = AddVectoredExceptionHandler(1, &VectoredExceptionHandler);
	InterlockedExchange(&g_installState, g_vehHandle ? 2 : 3);
	qqspeed::logging::Logf(
		"process_exception_trace installed=%d hardExceptions=1 cppExceptions=0 app=%p/0x%llX hook=%p/0x%llX",
		g_vehHandle ? 1 : 0,
		reinterpret_cast<void*>(g_gameApp.base.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(g_gameApp.size.load(std::memory_order_relaxed)),
		reinterpret_cast<void*>(g_hook.base.load(std::memory_order_relaxed)),
		static_cast<unsigned long long>(g_hook.size.load(std::memory_order_relaxed)));

	auto context = std::make_unique<TraceThreadContext>();
	context->selfModule = selfModule;
	context->config = config;
	HANDLE thread = CreateThread(nullptr, 0, &ModuleCacheThread, context.get(), 0, nullptr);
	if (!thread) {
		qqspeed::logging::Logf("process_exception_trace module thread create failed error=%lu", GetLastError());
		return;
	}

	context.release();
	CloseHandle(thread);
}
}
