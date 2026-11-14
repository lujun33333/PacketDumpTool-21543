#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "Advapi32.lib")

namespace
{
struct Options
{
	std::wstring launcherPath;
	std::wstring launcherArgs;
	std::wstring workDir;
	std::wstring processName = L"GameApp.exe";
	std::wstring moduleName = L"Network.dll";
	std::wstring dllPath;
	DWORD timeoutMs = 120000;
	DWORD pollMs = 50;
	bool waitModuleBeforeInject = false;
};

struct ProcessInfo
{
	DWORD pid = 0;
	DWORD parentPid = 0;
	std::wstring exeName;
};

std::wstring ToLower(std::wstring value)
{
	std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
		return static_cast<wchar_t>(towlower(ch));
	});
	return value;
}

bool EqualsIgnoreCase(const std::wstring& left, const std::wstring& right)
{
	return ToLower(left) == ToLower(right);
}

std::wstring QuoteArg(const std::wstring& arg)
{
	std::wstring result = L"\"";
	for (wchar_t ch : arg) {
		if (ch == L'"') {
			result += L'\\';
		}
		result += ch;
	}
	result += L"\"";
	return result;
}

std::wstring GetExeDirectory()
{
	wchar_t path[MAX_PATH] = {};
	DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (len == 0 || len == MAX_PATH) {
		return L".";
	}

	std::wstring full(path);
	size_t slash = full.find_last_of(L"\\/");
	if (slash == std::wstring::npos) {
		return L".";
	}
	return full.substr(0, slash);
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name)
{
	if (dir.empty()) {
		return name;
	}
	wchar_t last = dir.back();
	if (last == L'\\' || last == L'/') {
		return dir + name;
	}
	return dir + L"\\" + name;
}

bool FileExists(const std::wstring& path)
{
	DWORD attrs = GetFileAttributesW(path.c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

void PrintUsage()
{
	std::printf(
		"PacketDumpLauncher.exe --launcher <path> [options]\n"
		"\n"
		"Options:\n"
		"  --args <text>       Arguments passed to launcher.\n"
		"  --workdir <path>    Working directory. Defaults to launcher directory.\n"
		"  --process <name>    Target process. Default: GameApp.exe\n"
		"  --module <name>     Module to report/wait for. Default: Network.dll\n"
		"  --dll <path>        DLL to inject. Default: PacketDumpHook.dll beside this exe.\n"
		"  --timeout <ms>      Wait timeout. Default: 120000\n"
		"  --poll <ms>         Poll interval. Default: 50\n"
		"  --wait-module       Legacy mode: wait for module before injecting.\n"
		"\n"
		"Example:\n"
		"  PacketDumpLauncher.exe --launcher \"D:\\\\OtherLauncher\\\\Launcher.exe\" --process GameApp.exe\n");
}

bool ParseDword(const wchar_t* text, DWORD& value)
{
	if (!text || !*text) {
		return false;
	}

	wchar_t* end = nullptr;
	unsigned long parsed = wcstoul(text, &end, 10);
	if (!end || *end != 0) {
		return false;
	}
	value = static_cast<DWORD>(parsed);
	return true;
}

bool ParseOptions(int argc, wchar_t** argv, Options& options)
{
	for (int i = 1; i < argc; ++i) {
		std::wstring arg = argv[i];
		auto needValue = [&](const wchar_t* name) -> const wchar_t* {
			if (i + 1 >= argc) {
				std::printf("Missing value for %ls\n", name);
				return nullptr;
			}
			return argv[++i];
		};

		if (arg == L"--help" || arg == L"-h" || arg == L"/?") {
			PrintUsage();
			return false;
		}
		else if (arg == L"--launcher") {
			const wchar_t* value = needValue(L"--launcher");
			if (!value) return false;
			options.launcherPath = value;
		}
		else if (arg == L"--args") {
			const wchar_t* value = needValue(L"--args");
			if (!value) return false;
			options.launcherArgs = value;
		}
		else if (arg == L"--workdir") {
			const wchar_t* value = needValue(L"--workdir");
			if (!value) return false;
			options.workDir = value;
		}
		else if (arg == L"--process") {
			const wchar_t* value = needValue(L"--process");
			if (!value) return false;
			options.processName = value;
		}
		else if (arg == L"--module") {
			const wchar_t* value = needValue(L"--module");
			if (!value) return false;
			options.moduleName = value;
		}
		else if (arg == L"--dll") {
			const wchar_t* value = needValue(L"--dll");
			if (!value) return false;
			options.dllPath = value;
		}
		else if (arg == L"--timeout") {
			const wchar_t* value = needValue(L"--timeout");
			if (!value || !ParseDword(value, options.timeoutMs)) {
				std::printf("Invalid --timeout value\n");
				return false;
			}
		}
		else if (arg == L"--poll") {
			const wchar_t* value = needValue(L"--poll");
			if (!value || !ParseDword(value, options.pollMs) || options.pollMs == 0) {
				std::printf("Invalid --poll value\n");
				return false;
			}
		}
		else if (arg == L"--wait-module") {
			options.waitModuleBeforeInject = true;
		}
		else {
			std::printf("Unknown argument: %ls\n", arg.c_str());
			PrintUsage();
			return false;
		}
	}

	if (options.launcherPath.empty()) {
		PrintUsage();
		return false;
	}

	if (options.dllPath.empty()) {
		options.dllPath = JoinPath(GetExeDirectory(), L"PacketDumpHook.dll");
	}

	if (options.workDir.empty()) {
		size_t slash = options.launcherPath.find_last_of(L"\\/");
		if (slash != std::wstring::npos) {
			options.workDir = options.launcherPath.substr(0, slash);
		}
	}

	return true;
}

bool EnableDebugPrivilege()
{
	HANDLE token = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
		std::printf("OpenProcessToken failed error=%lu\n", GetLastError());
		return false;
	}

	TOKEN_PRIVILEGES privileges = {};
	privileges.PrivilegeCount = 1;
	if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &privileges.Privileges[0].Luid)) {
		std::printf("LookupPrivilegeValue SeDebugPrivilege failed error=%lu\n", GetLastError());
		CloseHandle(token);
		return false;
	}

	privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
	BOOL adjusted = AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr);
	DWORD error = GetLastError();
	CloseHandle(token);
	if (!adjusted || error == ERROR_NOT_ALL_ASSIGNED) {
		std::printf("Enable SeDebugPrivilege failed error=%lu\n", error);
		return false;
	}

	std::printf("Enable SeDebugPrivilege succeeded\n");
	return true;
}

const wchar_t* MachineToString(USHORT machine)
{
	switch (machine) {
	case IMAGE_FILE_MACHINE_UNKNOWN:
		return L"native";
	case IMAGE_FILE_MACHINE_I386:
		return L"x86";
	case IMAGE_FILE_MACHINE_AMD64:
		return L"x64";
	case IMAGE_FILE_MACHINE_ARM64:
		return L"arm64";
	default:
		return L"unknown";
	}
}

std::wstring DescribeProcessMachine(DWORD pid)
{
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) {
		return L"query_failed";
	}

	USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
	USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
	using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
	auto fn = reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
	if (fn && fn(process, &processMachine, &nativeMachine)) {
		CloseHandle(process);
		if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN) {
			return std::wstring(L"native/") + MachineToString(nativeMachine);
		}
		return std::wstring(MachineToString(processMachine)) + L" on " + MachineToString(nativeMachine);
	}

	BOOL wow64 = FALSE;
	if (IsWow64Process(process, &wow64)) {
		CloseHandle(process);
		return wow64 ? L"x86 on x64" : L"native";
	}

	CloseHandle(process);
	return L"unknown";
}

std::vector<ProcessInfo> EnumerateProcesses()
{
	std::vector<ProcessInfo> result;
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return result;
	}

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (Process32FirstW(snapshot, &entry)) {
		do {
			ProcessInfo info;
			info.pid = entry.th32ProcessID;
			info.parentPid = entry.th32ParentProcessID;
			info.exeName = entry.szExeFile;
			result.push_back(info);
		} while (Process32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return result;
}

std::set<DWORD> SnapshotProcessIdsByName(const std::wstring& processName)
{
	std::set<DWORD> ids;
	for (const auto& process : EnumerateProcesses()) {
		if (EqualsIgnoreCase(process.exeName, processName)) {
			ids.insert(process.pid);
		}
	}
	return ids;
}

bool IsProcessAlive(DWORD pid)
{
	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (!process) {
		return false;
	}

	DWORD exitCode = 0;
	bool alive = GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
	CloseHandle(process);
	return alive;
}

bool HasModuleLoaded(
	DWORD pid,
	const std::wstring& moduleName,
	uintptr_t* baseOut = nullptr,
	DWORD* sizeOut = nullptr,
	std::wstring* pathOut = nullptr)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return false;
	}

	bool found = false;
	MODULEENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (Module32FirstW(snapshot, &entry)) {
		do {
			if (EqualsIgnoreCase(entry.szModule, moduleName)) {
				found = true;
				if (baseOut) {
					*baseOut = reinterpret_cast<uintptr_t>(entry.modBaseAddr);
				}
				if (sizeOut) {
					*sizeOut = entry.modBaseSize;
				}
				if (pathOut) {
					*pathOut = entry.szExePath;
				}
				break;
			}
		} while (Module32NextW(snapshot, &entry));
	}

	CloseHandle(snapshot);
	return found;
}

bool StartLauncher(const Options& options, PROCESS_INFORMATION& processInfo)
{
	std::wstring commandLine = QuoteArg(options.launcherPath);
	if (!options.launcherArgs.empty()) {
		commandLine += L" ";
		commandLine += options.launcherArgs;
	}

	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
	mutableCommand.push_back(0);

	std::printf("Starting launcher: %ls\n", options.launcherPath.c_str());
	if (!CreateProcessW(
		options.launcherPath.c_str(),
		mutableCommand.data(),
		nullptr,
		nullptr,
		FALSE,
		0,
		nullptr,
		options.workDir.empty() ? nullptr : options.workDir.c_str(),
		&startup,
		&processInfo))
	{
		std::printf("CreateProcess failed error=%lu\n", GetLastError());
		return false;
	}

	std::printf("Launcher pid=%lu\n", processInfo.dwProcessId);
	return true;
}

bool InjectDll(DWORD pid, const std::wstring& dllPath)
{
	const size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
	HANDLE process = OpenProcess(
		PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
		FALSE,
		pid);
	if (!process) {
		std::printf("OpenProcess pid=%lu failed error=%lu\n", pid, GetLastError());
		return false;
	}

	void* remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remotePath) {
		std::printf("VirtualAllocEx pid=%lu failed error=%lu\n", pid, GetLastError());
		CloseHandle(process);
		return false;
	}

	BOOL wrote = WriteProcessMemory(process, remotePath, dllPath.c_str(), bytes, nullptr);
	if (!wrote) {
		std::printf("WriteProcessMemory pid=%lu failed error=%lu\n", pid, GetLastError());
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}

	HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
	auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(kernel32, "LoadLibraryW"));
	if (!loadLibraryW) {
		std::printf("GetProcAddress LoadLibraryW failed error=%lu\n", GetLastError());
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}

	HANDLE thread = CreateRemoteThread(process, nullptr, 0, loadLibraryW, remotePath, 0, nullptr);
	if (!thread) {
		std::printf("CreateRemoteThread pid=%lu failed error=%lu\n", pid, GetLastError());
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		CloseHandle(process);
		return false;
	}

	DWORD wait = WaitForSingleObject(thread, 15000);
	DWORD remoteResult = 0;
	GetExitCodeThread(thread, &remoteResult);
	CloseHandle(thread);
	VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
	CloseHandle(process);

	if (wait != WAIT_OBJECT_0 || remoteResult == 0) {
		std::printf("LoadLibraryW pid=%lu failed wait=%lu result=0x%08lX\n", pid, wait, remoteResult);
		return false;
	}

	std::printf("Injected pid=%lu dll=%ls module=0x%08lX\n", pid, dllPath.c_str(), remoteResult);
	return true;
}

bool PollAndInject(const Options& options, const std::set<DWORD>& initialPids, DWORD launcherPid)
{
	const ULONGLONG start = GetTickCount64();
	std::set<DWORD> attempted;
	std::set<DWORD> reported;
	std::printf("Poll start process=%ls module=%ls timeoutMs=%lu pollMs=%lu mode=%s\n",
		options.processName.c_str(),
		options.moduleName.c_str(),
		options.timeoutMs,
		options.pollMs,
		options.waitModuleBeforeInject ? "wait-module-before-inject" : "inject-on-process-found");

	while (GetTickCount64() - start <= options.timeoutMs) {
		if (launcherPid != 0 && !IsProcessAlive(launcherPid)) {
			std::printf("Launcher process exited; continuing target search until timeout.\n");
			launcherPid = 0;
		}

		for (const auto& process : EnumerateProcesses()) {
			if (!EqualsIgnoreCase(process.exeName, options.processName)) {
				continue;
			}
			if (initialPids.find(process.pid) != initialPids.end()) {
				continue;
			}
			if (attempted.find(process.pid) != attempted.end()) {
				continue;
			}

			if (reported.insert(process.pid).second) {
				std::wstring machine = DescribeProcessMachine(process.pid);
				std::printf("Target process found pid=%lu parent=%lu name=%ls machine=%ls elapsedMs=%llu\n",
					process.pid,
					process.parentPid,
					process.exeName.c_str(),
					machine.c_str(),
					static_cast<unsigned long long>(GetTickCount64() - start));
			}

			if (!options.waitModuleBeforeInject) {
				std::printf("Injecting before %ls wait pid=%lu elapsedMs=%llu\n",
					options.moduleName.c_str(),
					process.pid,
					static_cast<unsigned long long>(GetTickCount64() - start));
				attempted.insert(process.pid);
				if (InjectDll(process.pid, options.dllPath)) {
					return true;
				}
				continue;
			}

			uintptr_t moduleBase = 0;
			DWORD moduleSize = 0;
			std::wstring modulePath;
			if (!HasModuleLoaded(process.pid, options.moduleName, &moduleBase, &moduleSize, &modulePath)) {
				continue;
			}

			std::printf("Module found pid=%lu module=%ls base=0x%p size=%lu path=%ls elapsedMs=%llu\n",
				process.pid,
				options.moduleName.c_str(),
				reinterpret_cast<void*>(moduleBase),
				moduleSize,
				modulePath.c_str(),
				static_cast<unsigned long long>(GetTickCount64() - start));
			attempted.insert(process.pid);
			if (InjectDll(process.pid, options.dllPath)) {
				return true;
			}
		}

		Sleep(options.pollMs);
	}

	std::printf("Timed out waiting for %ls with %ls after %lu ms\n",
		options.processName.c_str(),
		options.moduleName.c_str(),
		options.timeoutMs);
	return false;
}
}

int wmain(int argc, wchar_t** argv)
{
	Options options;
	if (!ParseOptions(argc, argv, options)) {
		return 2;
	}

	if (!FileExists(options.launcherPath)) {
		std::printf("Launcher not found: %ls\n", options.launcherPath.c_str());
		return 2;
	}
	if (!FileExists(options.dllPath)) {
		std::printf("Hook DLL not found: %ls\n", options.dllPath.c_str());
		return 2;
	}

	std::set<DWORD> initialPids = SnapshotProcessIdsByName(options.processName);
	std::printf("Initial %ls process count=%llu\n",
		options.processName.c_str(),
		static_cast<unsigned long long>(initialPids.size()));
	std::printf("Hook DLL: %ls\n", options.dllPath.c_str());
	std::printf("Default injection mode: %s\n", options.waitModuleBeforeInject ? "wait-module-before-inject" : "inject-on-process-found");
	EnableDebugPrivilege();

	PROCESS_INFORMATION launcher = {};
	if (!StartLauncher(options, launcher)) {
		return 1;
	}

	bool ok = PollAndInject(options, initialPids, launcher.dwProcessId);
	if (launcher.hThread) {
		CloseHandle(launcher.hThread);
	}
	if (launcher.hProcess) {
		CloseHandle(launcher.hProcess);
	}
	return ok ? 0 : 1;
}
