#include "pch.h"

#include "topkart/topkart_worldbound_guard.h"

#include "logging/hook_log.h"
#include "network/code_patch.h"

#include <intrin.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace
{
constexpr DWORD kTopKartWaitMs = 120000;
constexpr DWORD kTopKartPollMs = 250;

constexpr std::uintptr_t kTopKartKnownImageSize = 0x3101000;
constexpr std::uintptr_t kTopKartTextStartRva = 0x1000;
constexpr std::uintptr_t kTopKartTextEndRva = 0x1EDD000;
constexpr std::uintptr_t kTopKartRdataStartRva = 0x1ED0000;
constexpr std::uintptr_t kTopKartRdataEndRva = 0x25C3000;
constexpr std::uintptr_t kTopKartTvmStartRva = 0x2CAD000;
constexpr std::uintptr_t kTopKartTvmEndRva = 0x3101000;

constexpr std::uintptr_t kNiNodeUpdateWorldBoundChildCallRvas[] = {
	0x163DA3F,
};

constexpr unsigned char kNiNodeUpdateWorldBoundChildCallBytes[] = {
	0xFF, 0x90, 0xF8, 0x00, 0x00, 0x00,
};

constexpr std::uintptr_t kWorldBoundRadiusOffset = 0x4C;
constexpr std::uintptr_t kUpdateWorldBoundSlotOffset = 0xF8;

using UpdateWorldBoundFn = void(__fastcall*)(void* self, float time, unsigned char updateControllers);

struct ProbeThreadContext
{
	qqspeed::config::HookConfig config;
};

struct ExceptionSnapshot
{
	DWORD code = 0;
	void* address = nullptr;
	ULONG_PTR info0 = 0;
	void* info1 = nullptr;
};

struct RegisterSnapshot
{
	std::uintptr_t rbx = 0;
	std::uintptr_t rsi = 0;
	std::uintptr_t rdi = 0;
	std::uintptr_t rbp = 0;
};

std::uintptr_t g_topKartBase = 0;
volatile LONG g_installState = 0;
volatile LONG g_invalidChildCount = 0;
volatile LONG g_childExceptionCount = 0;
std::size_t g_patchedCalls = 0;

std::uintptr_t TopKartCallRvaFromReturnAddress(void* returnAddress)
{
	const std::uintptr_t ret = reinterpret_cast<std::uintptr_t>(returnAddress);
	if (!g_topKartBase || ret < g_topKartBase) {
		return 0;
	}

	const std::uintptr_t retRva = ret - g_topKartBase;
	return retRva >= 5 ? retRva - 5 : 0;
}

bool IsTopKartRvaInRange(std::uintptr_t address, std::uintptr_t startRva, std::uintptr_t endRva)
{
	if (!g_topKartBase || address < g_topKartBase) {
		return false;
	}

	const std::uintptr_t rva = address - g_topKartBase;
	return rva >= startRva && rva < endRva;
}

bool IsLikelyTopKartVtable(std::uintptr_t address)
{
	return IsTopKartRvaInRange(address, kTopKartRdataStartRva, kTopKartRdataEndRva);
}

bool IsLikelyTopKartCode(std::uintptr_t address)
{
	return IsTopKartRvaInRange(address, kTopKartTextStartRva, kTopKartTextEndRva) ||
		IsTopKartRvaInRange(address, kTopKartTvmStartRva, kTopKartTvmEndRva);
}

bool SafeReadPointer(const void* address, std::uintptr_t* out)
{
	if (!out) {
		return false;
	}
	*out = 0;
	return qqspeed::network::patch::SafeCopyRaw(out, address, sizeof(*out));
}

bool SafeZeroWorldBoundRadius(void* child)
{
	if (!child) {
		return false;
	}

	__try
	{
		*reinterpret_cast<float*>(reinterpret_cast<unsigned char*>(child) + kWorldBoundRadiusOffset) = 0.0f;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

LONG CaptureExceptionInfo(PEXCEPTION_POINTERS pointers, ExceptionSnapshot* snapshot)
{
	if (snapshot && pointers && pointers->ExceptionRecord) {
		snapshot->code = pointers->ExceptionRecord->ExceptionCode;
		snapshot->address = pointers->ExceptionRecord->ExceptionAddress;
		snapshot->info0 = pointers->ExceptionRecord->NumberParameters > 0 ?
			pointers->ExceptionRecord->ExceptionInformation[0] : 0;
		snapshot->info1 = pointers->ExceptionRecord->NumberParameters > 1 ?
			reinterpret_cast<void*>(pointers->ExceptionRecord->ExceptionInformation[1]) : nullptr;
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

bool ShouldLogCount(LONG count)
{
	return count <= 32 || (count > 0 && (count & (count - 1)) == 0);
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

void LogInvalidChild(
	const char* reason,
	LONG count,
	void* child,
	std::uintptr_t vtable,
	std::uintptr_t target,
	bool zeroed,
	std::uintptr_t callRva,
	float time,
	unsigned char updateControllers,
	const RegisterSnapshot& registers)
{
	if (!ShouldLogCount(count)) {
		return;
	}

	char stack[1024] = {};
	FormatTopKartStack(stack, sizeof(stack));

	qqspeed::logging::Logf(
		"topkart_worldbound_guard action=skip reason=%s count=%ld callRva=0x%llX child=%p vtable=%p target=%p "
		"zeroRadius=%d time=%.3f update=%u entryRbx=%p entryRsi=%p entryRdi=0x%llX entryRbp=0x%llX stack=%s",
		reason ? reason : "unknown",
		count,
		static_cast<unsigned long long>(callRva),
		child,
		reinterpret_cast<void*>(vtable),
		reinterpret_cast<void*>(target),
		zeroed ? 1 : 0,
		static_cast<double>(time),
		static_cast<unsigned int>(updateControllers),
		reinterpret_cast<void*>(registers.rbx),
		reinterpret_cast<void*>(registers.rsi),
		static_cast<unsigned long long>(registers.rdi),
		static_cast<unsigned long long>(registers.rbp),
		stack);
}

void LogChildException(
	LONG count,
	void* child,
	std::uintptr_t vtable,
	std::uintptr_t target,
	bool zeroed,
	std::uintptr_t callRva,
	const ExceptionSnapshot& exception,
	const RegisterSnapshot& registers)
{
	if (!ShouldLogCount(count)) {
		return;
	}

	char stack[1024] = {};
	FormatTopKartStack(stack, sizeof(stack));

	qqspeed::logging::Logf(
		"topkart_worldbound_guard action=caught_exception count=%ld callRva=0x%llX child=%p vtable=%p target=%p "
		"zeroRadius=%d exceptionCode=0x%08lX exceptionAddress=%p info0=0x%llX info1=%p "
		"entryRbx=%p entryRsi=%p entryRdi=0x%llX entryRbp=0x%llX stack=%s",
		count,
		static_cast<unsigned long long>(callRva),
		child,
		reinterpret_cast<void*>(vtable),
		reinterpret_cast<void*>(target),
		zeroed ? 1 : 0,
		exception.code,
		exception.address,
		static_cast<unsigned long long>(exception.info0),
		exception.info1,
		reinterpret_cast<void*>(registers.rbx),
		reinterpret_cast<void*>(registers.rsi),
		static_cast<unsigned long long>(registers.rdi),
		static_cast<unsigned long long>(registers.rbp),
		stack);
}

__declspec(noinline) void __fastcall DetourNiNodeChildUpdateWorldBound(
	void* child,
	float time,
	unsigned char updateControllers)
{
	CONTEXT entryContext = {};
	RtlCaptureContext(&entryContext);
	const RegisterSnapshot registers = {
		entryContext.Rbx,
		entryContext.Rsi,
		entryContext.Rdi,
		entryContext.Rbp,
	};
	const std::uintptr_t callRva = TopKartCallRvaFromReturnAddress(_ReturnAddress());

	std::uintptr_t vtable = 0;
	if (!SafeReadPointer(child, &vtable)) {
		const LONG count = InterlockedIncrement(&g_invalidChildCount);
		const bool zeroed = SafeZeroWorldBoundRadius(child);
		LogInvalidChild("child_read_failed", count, child, 0, 0, zeroed, callRva, time, updateControllers, registers);
		return;
	}

	std::uintptr_t target = 0;
	if (!IsLikelyTopKartVtable(vtable) ||
		!SafeReadPointer(reinterpret_cast<const void*>(vtable + kUpdateWorldBoundSlotOffset), &target)) {
		const LONG count = InterlockedIncrement(&g_invalidChildCount);
		const bool zeroed = SafeZeroWorldBoundRadius(child);
		LogInvalidChild("bad_vtable", count, child, vtable, target, zeroed, callRva, time, updateControllers, registers);
		return;
	}

	if (!IsLikelyTopKartCode(target)) {
		const LONG count = InterlockedIncrement(&g_invalidChildCount);
		const bool zeroed = SafeZeroWorldBoundRadius(child);
		LogInvalidChild("bad_target", count, child, vtable, target, zeroed, callRva, time, updateControllers, registers);
		return;
	}

	ExceptionSnapshot exception = {};
	__try
	{
		reinterpret_cast<UpdateWorldBoundFn>(target)(child, time, updateControllers);
	}
	__except (CaptureExceptionInfo(GetExceptionInformation(), &exception))
	{
		const LONG count = InterlockedIncrement(&g_childExceptionCount);
		const bool zeroed = SafeZeroWorldBoundRadius(child);
		LogChildException(count, child, vtable, target, zeroed, callRva, exception, registers);
	}
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

	if (!config.features.enableTopKartWorldBoundGuard) {
		qqspeed::logging::Logf("topkart_worldbound_guard disabled");
		return 0;
	}

	if (InterlockedCompareExchange(&g_installState, 1, 0) != 0) {
		return 0;
	}

	HMODULE module = WaitForTopKartModule(config);
	if (!module) {
		qqspeed::logging::Logf("topkart_worldbound_guard skipped: Top-Kart.dll not loaded after %lu ms", kTopKartWaitMs);
		InterlockedExchange(&g_installState, 3);
		return 0;
	}

	g_topKartBase = reinterpret_cast<std::uintptr_t>(module);
	g_patchedCalls = qqspeed::network::patch::PatchCallSitesWithRelay(
		g_topKartBase,
		kNiNodeUpdateWorldBoundChildCallRvas,
		_countof(kNiNodeUpdateWorldBoundChildCallRvas),
		kNiNodeUpdateWorldBoundChildCallBytes,
		sizeof(kNiNodeUpdateWorldBoundChildCallBytes),
		reinterpret_cast<void*>(&DetourNiNodeChildUpdateWorldBound),
		"TopKartWorldBoundGuard NiNode::UpdateWorldBound child");

	InterlockedExchange(&g_installState, g_patchedCalls ? 2 : 3);
	qqspeed::logging::Logf(
		"topkart_worldbound_guard installed=%d patchedCalls=%llu base=%p",
		g_patchedCalls ? 1 : 0,
		static_cast<unsigned long long>(g_patchedCalls),
		reinterpret_cast<void*>(g_topKartBase));
	return 0;
}
}

namespace qqspeed::topkart
{
void StartWorldBoundGuardThread(const qqspeed::config::HookConfig& config)
{
	if (!config.features.enableTopKartWorldBoundGuard) {
		qqspeed::logging::Logf("topkart_worldbound_guard disabled");
		return;
	}

	auto context = std::make_unique<ProbeThreadContext>();
	context->config = config;

	HANDLE thread = CreateThread(nullptr, 0, &ProbeThread, context.get(), 0, nullptr);
	if (!thread) {
		qqspeed::logging::Logf("topkart_worldbound_guard thread create failed error=%lu", GetLastError());
		return;
	}

	context.release();
	CloseHandle(thread);
}
}
