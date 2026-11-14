#include "pch.h"

#include "MinHook.h"
#include "hook_log.h"
#include "op4_collector.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{
constexpr uintptr_t kNetworkImageSize = 0x784000;
constexpr uintptr_t kRequestWrapperRva = 0x3EC290;
constexpr uintptr_t kNetworkHolderGlobalRva = 0x5E5A00;
constexpr UINT kCollectorMessage = WM_APP + 0x476;
constexpr DWORD kResponseTimeoutMs = 15000;

using RequestGameServerFn = int(__fastcall*)(
	void* network,
	unsigned int msgId,
	unsigned int route,
	unsigned int arg4,
	unsigned char arg5,
	unsigned short arg6,
	unsigned int arg7,
	const void* body,
	int bodyLen);

struct RequestContext
{
	void* network = nullptr;
	unsigned int route = 0;
	unsigned int arg4 = 0;
	unsigned char arg5 = 0;
	unsigned short arg6 = 0;
	unsigned int arg7 = 0;
};

struct CollectorCommand
{
	unsigned char operation = 0;
	unsigned long long workId = 0;
	unsigned short pageNo = 0;
	unsigned char pageSize = 0;
};

HMODULE g_ownerModule = nullptr;
HANDLE g_stopEvent = nullptr;
HANDLE g_workerThread = nullptr;
HHOOK g_messageHook = nullptr;
DWORD g_uiThreadId = 0;
HWND g_mainWindow = nullptr;
SRWLOCK g_stateLock = SRWLOCK_INIT;

RequestGameServerFn g_originalRequestGameServer = nullptr;
void* g_requestTarget = nullptr;
RequestContext g_requestContext;
std::array<unsigned char, 46> g_requestTemplate = {};
bool g_requestReady = false;
bool g_blocked = false;

std::deque<CollectorCommand> g_pending;
std::unordered_set<std::string> g_seenCommands;
std::array<unsigned int, 256> g_inflightByOperation = {};
unsigned int g_maxInflight = 2;
unsigned int g_totalInflight = 0;
unsigned long long g_sentList = 0;
unsigned long long g_completedList = 0;
unsigned long long g_sentDetail = 0;
unsigned long long g_completedDetail = 0;
DWORD g_lastSendTick = 0;
DWORD g_lastResponseTick = 0;

std::wstring g_moduleDirectory;
std::wstring g_listQueuePath;
std::wstring g_detailQueuePath;
std::wstring g_inflightPath;
std::wstring g_statusPath;

void WriteBigEndian16(unsigned char* out, unsigned short value)
{
	out[0] = static_cast<unsigned char>(value >> 8);
	out[1] = static_cast<unsigned char>(value);
}

void WriteBigEndian32(unsigned char* out, unsigned int value)
{
	out[0] = static_cast<unsigned char>(value >> 24);
	out[1] = static_cast<unsigned char>(value >> 16);
	out[2] = static_cast<unsigned char>(value >> 8);
	out[3] = static_cast<unsigned char>(value);
}

void WriteBigEndian64(unsigned char* out, unsigned long long value)
{
	for (int index = 7; index >= 0; --index) {
		out[7 - index] = static_cast<unsigned char>(value >> (index * 8));
	}
}

unsigned int ReadBigEndian32(const unsigned char* data)
{
	return (static_cast<unsigned int>(data[0]) << 24) |
		(static_cast<unsigned int>(data[1]) << 16) |
		(static_cast<unsigned int>(data[2]) << 8) |
		static_cast<unsigned int>(data[3]);
}

std::wstring GetModuleDirectory(HMODULE module)
{
	wchar_t path[MAX_PATH] = {};
	DWORD length = GetModuleFileNameW(module, path, _countof(path));
	if (length == 0 || length >= _countof(path)) {
		return std::wstring();
	}
	std::wstring result(path, length);
	size_t slash = result.find_last_of(L"\\/");
	if (slash == std::wstring::npos) {
		return std::wstring();
	}
	result.resize(slash + 1);
	return result;
}

bool IsExecutableAddress(const void* address)
{
	MEMORY_BASIC_INFORMATION info = {};
	if (!address || VirtualQuery(address, &info, sizeof(info)) != sizeof(info)) {
		return false;
	}
	if (info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
		return false;
	}
	const DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
	return (info.Protect & executable) != 0;
}

bool IsSupportedNetworkBuild(HMODULE networkModule)
{
	if (!networkModule) {
		return false;
	}
	__try {
		const auto base = reinterpret_cast<const unsigned char*>(networkModule);
		const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
			return false;
		}
		const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage != kNetworkImageSize) {
			return false;
		}
		const unsigned char expected[] = { 0x40, 0x53, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xD9 };
		return memcmp(base + kRequestWrapperRva, expected, sizeof(expected)) == 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

bool TryResolveRequestTarget(HMODULE networkModule, void*& target)
{
	target = nullptr;
	if (!IsSupportedNetworkBuild(networkModule)) {
		return false;
	}
	__try {
		auto base = reinterpret_cast<unsigned char*>(networkModule);
		void* holder = *reinterpret_cast<void**>(base + kNetworkHolderGlobalRva);
		if (!holder) {
			return false;
		}
		void* network = *reinterpret_cast<void**>(static_cast<unsigned char*>(holder) + sizeof(void*));
		if (!network) {
			return false;
		}
		auto vtable = *reinterpret_cast<void***>(network);
		if (!vtable) {
			return false;
		}
		target = vtable[0x58 / sizeof(void*)];
		return IsExecutableAddress(target);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		target = nullptr;
		return false;
	}
}

BOOL CALLBACK FindMainWindowCallback(HWND window, LPARAM parameter)
{
	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	if (processId != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr) {
		return TRUE;
	}
	auto result = reinterpret_cast<HWND*>(parameter);
	*result = window;
	return FALSE;
}

void PostCollectorMessage()
{
	DWORD threadId = g_uiThreadId;
	if (threadId != 0) {
		PostThreadMessageW(threadId, kCollectorMessage, 0, 0);
	}
}

std::string CommandKey(const CollectorCommand& command)
{
	char key[96] = {};
	if (command.operation == 10) {
		snprintf(key, sizeof(key), "10:%u:%u", command.pageNo, command.pageSize);
	}
	else {
		snprintf(key, sizeof(key), "4:%llu", command.workId);
	}
	return key;
}

void AddCommand(const CollectorCommand& command)
{
	const std::string key = CommandKey(command);
	AcquireSRWLockExclusive(&g_stateLock);
	if (g_seenCommands.insert(key).second) {
		g_pending.push_back(command);
	}
	ReleaseSRWLockExclusive(&g_stateLock);
}

void LoadListCommands()
{
	FILE* file = nullptr;
	if (_wfopen_s(&file, g_listQueuePath.c_str(), L"rt") != 0 || !file) {
		return;
	}
	wchar_t line[128] = {};
	while (fgetws(line, _countof(line), file)) {
		wchar_t* end = nullptr;
		unsigned long value = wcstoul(line, &end, 10);
		if (end == line || value > 249) {
			continue;
		}
		CollectorCommand command;
		command.operation = 10;
		command.pageNo = static_cast<unsigned short>(value);
		command.pageSize = value == 0 ? 6 : 9;
		AddCommand(command);
	}
	fclose(file);
}

void LoadDetailCommands()
{
	FILE* file = nullptr;
	if (_wfopen_s(&file, g_detailQueuePath.c_str(), L"rt") != 0 || !file) {
		return;
	}
	wchar_t line[128] = {};
	while (fgetws(line, _countof(line), file)) {
		wchar_t* end = nullptr;
		unsigned long long value = _wcstoui64(line, &end, 10);
		if (end == line || value == 0) {
			continue;
		}
		CollectorCommand command;
		command.operation = 4;
		command.workId = value;
		AddCommand(command);
	}
	fclose(file);
}

void LoadMaxInflight()
{
	FILE* file = nullptr;
	if (_wfopen_s(&file, g_inflightPath.c_str(), L"rt") != 0 || !file) {
		return;
	}
	unsigned int value = 0;
	if (fwscanf_s(file, L"%u", &value) == 1 && value >= 1 && value <= 16) {
		AcquireSRWLockExclusive(&g_stateLock);
		g_maxInflight = value;
		ReleaseSRWLockExclusive(&g_stateLock);
	}
	fclose(file);
}

void WriteStatus()
{
	unsigned int pending = 0;
	unsigned int inflight = 0;
	unsigned int maxInflight = 0;
	unsigned long long sentList = 0;
	unsigned long long completedList = 0;
	unsigned long long sentDetail = 0;
	unsigned long long completedDetail = 0;
	bool ready = false;
	bool blocked = false;
	AcquireSRWLockShared(&g_stateLock);
	pending = static_cast<unsigned int>(g_pending.size());
	inflight = g_totalInflight;
	maxInflight = g_maxInflight;
	sentList = g_sentList;
	completedList = g_completedList;
	sentDetail = g_sentDetail;
	completedDetail = g_completedDetail;
	ready = g_requestReady;
	blocked = g_blocked;
	ReleaseSRWLockShared(&g_stateLock);

	FILE* file = nullptr;
	if (_wfopen_s(&file, g_statusPath.c_str(), L"wt") != 0 || !file) {
		return;
	}
	fwprintf(file,
		L"pid=%lu\nready=%u\nblocked=%u\npending=%u\ninflight=%u\nmax_inflight=%u\n"
		L"list_sent=%llu\nlist_completed=%llu\nop4_sent=%llu\nop4_completed=%llu\n",
		GetCurrentProcessId(),
		ready ? 1U : 0U,
		blocked ? 1U : 0U,
		pending,
		inflight,
		maxInflight,
		sentList,
		completedList,
		sentDetail,
		completedDetail);
	fclose(file);
}

bool BuildBody(const CollectorCommand& command, std::array<unsigned char, 46>& body)
{
	AcquireSRWLockShared(&g_stateLock);
	if (!g_requestReady) {
		ReleaseSRWLockShared(&g_stateLock);
		return false;
	}
	body = g_requestTemplate;
	ReleaseSRWLockShared(&g_stateLock);

	WriteBigEndian32(body.data() + 6, GetTickCount());
	body[10] = command.operation;
	if (command.operation == 10) {
		memset(body.data() + 11, 0, body.size() - 11);
		WriteBigEndian16(body.data() + 19, command.pageNo);
		body[21] = command.pageSize;
		return true;
	}
	if (command.operation == 4) {
		WriteBigEndian64(body.data() + 11, command.workId);
		memset(body.data() + 19, 0, body.size() - 19);
		return true;
	}
	return false;
}

void SendAvailableCommands()
{
	for (;;) {
		CollectorCommand command;
		RequestContext context;
		RequestGameServerFn request = nullptr;
		AcquireSRWLockExclusive(&g_stateLock);
		if (g_blocked || !g_requestReady || !g_originalRequestGameServer ||
			g_pending.empty() || g_totalInflight >= g_maxInflight) {
			ReleaseSRWLockExclusive(&g_stateLock);
			break;
		}
		command = g_pending.front();
		g_pending.pop_front();
		context = g_requestContext;
		request = g_originalRequestGameServer;
		++g_totalInflight;
		++g_inflightByOperation[command.operation];
		if (command.operation == 10) {
			++g_sentList;
		}
		else {
			++g_sentDetail;
		}
		g_lastSendTick = GetTickCount();
		ReleaseSRWLockExclusive(&g_stateLock);

		std::array<unsigned char, 46> body = {};
		if (!BuildBody(command, body)) {
			AcquireSRWLockExclusive(&g_stateLock);
			g_blocked = true;
			ReleaseSRWLockExclusive(&g_stateLock);
			HookLog("Aurora collector stopped: request body template unavailable");
			break;
		}

		const int result = request(
			context.network,
			28476,
			context.route,
			context.arg4,
			context.arg5,
			context.arg6,
			context.arg7,
			body.data(),
			static_cast<int>(body.size()));
		if (result != 0) {
			AcquireSRWLockExclusive(&g_stateLock);
			if (g_totalInflight > 0) {
				--g_totalInflight;
			}
			if (g_inflightByOperation[command.operation] > 0) {
				--g_inflightByOperation[command.operation];
			}
			g_blocked = true;
			ReleaseSRWLockExclusive(&g_stateLock);
			HookLog("Aurora collector send failed operation=%u workId=%llu page=%u result=%d",
				static_cast<unsigned int>(command.operation),
				command.workId,
				static_cast<unsigned int>(command.pageNo),
				result);
			break;
		}
	}
}

LRESULT CALLBACK CollectorGetMessageHook(int code, WPARAM wParam, LPARAM lParam)
{
	if (code >= 0 && lParam) {
		auto message = reinterpret_cast<MSG*>(lParam);
		if (message->message == kCollectorMessage) {
			message->message = WM_NULL;
			SendAvailableCommands();
		}
	}
	return CallNextHookEx(g_messageHook, code, wParam, lParam);
}

int __fastcall HookedRequestGameServer(
	void* network,
	unsigned int msgId,
	unsigned int route,
	unsigned int arg4,
	unsigned char arg5,
	unsigned short arg6,
	unsigned int arg7,
	const void* body,
	int bodyLen)
{
	if (msgId == 28476 && body && bodyLen == static_cast<int>(g_requestTemplate.size())) {
		const auto bytes = static_cast<const unsigned char*>(body);
		if (bytes[10] == 10 || bytes[10] == 4) {
			AcquireSRWLockExclusive(&g_stateLock);
			memcpy(g_requestTemplate.data(), bytes, g_requestTemplate.size());
			g_requestContext.network = network;
			g_requestContext.route = route;
			g_requestContext.arg4 = arg4;
			g_requestContext.arg5 = arg5;
			g_requestContext.arg6 = arg6;
			g_requestContext.arg7 = arg7;
			const bool becameReady = !g_requestReady;
			g_requestReady = true;
			ReleaseSRWLockExclusive(&g_stateLock);
			if (becameReady) {
				HookLog("Aurora collector captured official request context operation=%u route=%u arg4=%u arg5=%u arg6=%u arg7=%u",
					static_cast<unsigned int>(bytes[10]), route, arg4,
					static_cast<unsigned int>(arg5), static_cast<unsigned int>(arg6), arg7);
			}
			PostCollectorMessage();
		}
	}
	return g_originalRequestGameServer(
		network, msgId, route, arg4, arg5, arg6, arg7, body, bodyLen);
}

bool InstallRequestHook(HMODULE networkModule)
{
	void* target = nullptr;
	if (!TryResolveRequestTarget(networkModule, target)) {
		return false;
	}
	MH_STATUS status = MH_CreateHook(
		target,
		reinterpret_cast<void*>(&HookedRequestGameServer),
		reinterpret_cast<void**>(&g_originalRequestGameServer));
	if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
		HookLog("Aurora collector request hook create failed target=%p status=%s", target, MH_StatusToString(status));
		return false;
	}
	status = MH_EnableHook(target);
	if (status != MH_OK && status != MH_ERROR_ENABLED) {
		HookLog("Aurora collector request hook enable failed target=%p status=%s", target, MH_StatusToString(status));
		return false;
	}
	g_requestTarget = target;
	HookLog("Aurora collector request hook installed target=%p original=%p", target, g_originalRequestGameServer);
	return true;
}

bool InstallUiMessageHook()
{
	HWND window = nullptr;
	EnumWindows(FindMainWindowCallback, reinterpret_cast<LPARAM>(&window));
	if (!window) {
		return false;
	}
	DWORD threadId = GetWindowThreadProcessId(window, nullptr);
	if (!threadId) {
		return false;
	}
	HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, CollectorGetMessageHook, g_ownerModule, threadId);
	if (!hook) {
		HookLog("Aurora collector UI message hook failed thread=%lu error=%lu", threadId, GetLastError());
		return false;
	}
	g_mainWindow = window;
	g_uiThreadId = threadId;
	g_messageHook = hook;
	HookLog("Aurora collector UI message hook installed hwnd=%p thread=%lu hook=%p", window, threadId, hook);
	return true;
}

DWORD WINAPI CollectorWorker(void*)
{
	HMODULE networkModule = nullptr;
	while (WaitForSingleObject(g_stopEvent, 100) == WAIT_TIMEOUT) {
		networkModule = GetModuleHandleW(L"Network.dll");
		if (networkModule && IsSupportedNetworkBuild(networkModule)) {
			break;
		}
	}
	if (!networkModule || WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) {
		return 0;
	}

	while (WaitForSingleObject(g_stopEvent, 100) == WAIT_TIMEOUT) {
		if (InstallRequestHook(networkModule)) {
			break;
		}
	}
	if (!g_requestTarget || WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) {
		return 0;
	}

	while (WaitForSingleObject(g_stopEvent, 100) == WAIT_TIMEOUT) {
		if (InstallUiMessageHook()) {
			break;
		}
	}
	if (!g_messageHook || WaitForSingleObject(g_stopEvent, 0) == WAIT_OBJECT_0) {
		return 0;
	}

	HookLog("Aurora collector ready listQueue=%ls detailQueue=%ls inflightConfig=%ls",
		g_listQueuePath.c_str(), g_detailQueuePath.c_str(), g_inflightPath.c_str());
	while (WaitForSingleObject(g_stopEvent, 200) == WAIT_TIMEOUT) {
		LoadMaxInflight();
		LoadListCommands();
		LoadDetailCommands();
		PostCollectorMessage();

		bool timedOut = false;
		AcquireSRWLockExclusive(&g_stateLock);
		if (!g_blocked && g_totalInflight > 0 &&
			static_cast<DWORD>(GetTickCount() - g_lastResponseTick) > kResponseTimeoutMs &&
			static_cast<DWORD>(GetTickCount() - g_lastSendTick) > kResponseTimeoutMs) {
			g_blocked = true;
			timedOut = true;
		}
		ReleaseSRWLockExclusive(&g_stateLock);
		if (timedOut) {
			HookLog("Aurora collector stopped: response timeout inflight remains nonzero");
		}
		WriteStatus();
	}
	return 0;
}
}

void StartOp4Collector(HMODULE ownerModule)
{
	if (g_workerThread) {
		return;
	}
	g_ownerModule = ownerModule;
	g_moduleDirectory = GetModuleDirectory(ownerModule);
	if (g_moduleDirectory.empty()) {
		HookLog("Aurora collector disabled: module directory unavailable");
		return;
	}
	g_listQueuePath = g_moduleDirectory + L"aurora_list_pages.txt";
	g_detailQueuePath = g_moduleDirectory + L"aurora_op4_work_ids.txt";
	g_inflightPath = g_moduleDirectory + L"aurora_collector_max_inflight.txt";
	g_statusPath = g_moduleDirectory + L"aurora_collector_status_" + std::to_wstring(GetCurrentProcessId()) + L".txt";
	HookLog("Aurora collector worker starting moduleDirectory=%ls", g_moduleDirectory.c_str());
	g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!g_stopEvent) {
		HookLog("Aurora collector disabled: stop event creation failed error=%lu", GetLastError());
		return;
	}
	g_workerThread = CreateThread(nullptr, 0, CollectorWorker, nullptr, 0, nullptr);
	if (!g_workerThread) {
		HookLog("Aurora collector disabled: worker creation failed error=%lu", GetLastError());
		CloseHandle(g_stopEvent);
		g_stopEvent = nullptr;
	}
}

void StopOp4Collector()
{
	if (g_stopEvent) {
		SetEvent(g_stopEvent);
	}
	if (g_workerThread) {
		WaitForSingleObject(g_workerThread, 3000);
		CloseHandle(g_workerThread);
		g_workerThread = nullptr;
	}
	if (g_messageHook) {
		UnhookWindowsHookEx(g_messageHook);
		g_messageHook = nullptr;
	}
	if (g_requestTarget) {
		MH_DisableHook(g_requestTarget);
		g_requestTarget = nullptr;
	}
	if (g_stopEvent) {
		CloseHandle(g_stopEvent);
		g_stopEvent = nullptr;
	}
}

void NotifyOp4CollectorResponse(unsigned int msgId, const void* body, int bodyLen)
{
	if (msgId != 28476 || !body || bodyLen < 10) {
		return;
	}
	const auto bytes = static_cast<const unsigned char*>(body);
	const unsigned int operation = ReadBigEndian32(bytes + 6);
	if (operation != 4 && operation != 10) {
		return;
	}
	bool consumed = false;
	unsigned long long completed = 0;
	AcquireSRWLockExclusive(&g_stateLock);
	if (g_inflightByOperation[operation] > 0) {
		--g_inflightByOperation[operation];
		if (g_totalInflight > 0) {
			--g_totalInflight;
		}
		if (operation == 10) {
			completed = ++g_completedList;
		}
		else {
			completed = ++g_completedDetail;
		}
		g_lastResponseTick = GetTickCount();
		consumed = true;
	}
	ReleaseSRWLockExclusive(&g_stateLock);
	if (consumed) {
		if (completed <= 10 || completed % 50 == 0) {
			HookLog("Aurora collector response operation=%u completed=%llu", operation, completed);
		}
		PostCollectorMessage();
	}
}
