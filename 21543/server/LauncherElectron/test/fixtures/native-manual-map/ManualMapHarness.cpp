#include "FixtureShared.h"

#include "NativeBridgeManualMap.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace
{
struct ChildGuard
{
	HANDLE Job = nullptr;
	HANDLE Process = nullptr;
	HANDLE Thread = nullptr;
	HANDLE Mapping = nullptr;
	NativeManualMapFixtureState* State = nullptr;
	bool Exited = false;

	~ChildGuard()
	{
		if (State && Process && !Exited)
		{
			InterlockedExchange(&State->ExitRequested, 1);
			if (WaitForSingleObject(Process, 1000) != WAIT_OBJECT_0)
			{
				TerminateProcess(Process, 99);
				WaitForSingleObject(Process, 1000);
			}
		}
		if (State)
		{
			UnmapViewOfFile(State);
		}
		if (Thread)
		{
			CloseHandle(Thread);
		}
		if (Process)
		{
			CloseHandle(Process);
		}
		if (Mapping)
		{
			CloseHandle(Mapping);
		}
		if (Job)
		{
			CloseHandle(Job);
		}
	}
};

int Fail(const wchar_t* message, DWORD error = ERROR_SUCCESS)
{
	if (error)
	{
		fwprintf(stderr, L"FAIL %ls (error=%lu)\n", message, error);
	}
	else
	{
		fwprintf(stderr, L"FAIL %ls\n", message);
	}
	return 1;
}

bool ReadPayload(const wchar_t* path, std::vector<BYTE>* bytes)
{
	if (!path || !bytes)
	{
		return false;
	}
	std::ifstream stream(path, std::ios::binary | std::ios::ate);
	if (!stream)
	{
		return false;
	}
	const std::streamoff size = stream.tellg();
	if (size <= 0 || size > 16 * 1024 * 1024)
	{
		return false;
	}
	bytes->resize(static_cast<SIZE_T>(size));
	stream.seekg(0, std::ios::beg);
	return static_cast<bool>(stream.read(
		reinterpret_cast<char*>(bytes->data()),
		static_cast<std::streamsize>(bytes->size())));
}

bool ValidateFixturePe(const std::vector<BYTE>& bytes)
{
	if (bytes.size() < sizeof(IMAGE_DOS_HEADER))
	{
		return false;
	}
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
		static_cast<SIZE_T>(dos->e_lfanew) > bytes.size() - sizeof(IMAGE_NT_HEADERS64))
	{
		return false;
	}
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE ||
		nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
		(nt->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0 ||
		nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
		nt->OptionalHeader.ImageBase != kNativeManualMapPreferredBase)
	{
		return false;
	}
	for (const DWORD directory : {
		IMAGE_DIRECTORY_ENTRY_IMPORT,
		IMAGE_DIRECTORY_ENTRY_BASERELOC,
		IMAGE_DIRECTORY_ENTRY_TLS,
		IMAGE_DIRECTORY_ENTRY_EXCEPTION })
	{
		if (!nt->OptionalHeader.DataDirectory[directory].VirtualAddress ||
			!nt->OptionalHeader.DataDirectory[directory].Size)
		{
			return false;
		}
	}
	return true;
}

bool WaitForReady(ChildGuard& child, DWORD timeoutMs)
{
	const ULONGLONG deadline = GetTickCount64() + timeoutMs;
	while (GetTickCount64() < deadline)
	{
		const LONG ready = InterlockedCompareExchange(&child.State->Ready, 0, 0);
		if (ready != 0)
		{
			return ready == 1;
		}
		if (WaitForSingleObject(child.Process, 0) == WAIT_OBJECT_0)
		{
			return false;
		}
		Sleep(10);
	}
	return false;
}

bool IsFileAbsent(const wchar_t* path)
{
	SetLastError(ERROR_SUCCESS);
	const DWORD attributes = GetFileAttributesW(path);
	const DWORD error = GetLastError();
	return attributes == INVALID_FILE_ATTRIBUTES &&
		(error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
}
}

int wmain(int argc, wchar_t** argv)
{
	if (argc != 3)
	{
		return Fail(L"usage: NativeManualMapHarness.exe <child.exe> <fixture.dll>");
	}

	std::vector<BYTE> payload;
	if (!ReadPayload(argv[2], &payload) || !ValidateFixturePe(payload))
	{
		return Fail(L"fixture PE is missing or does not cover the required directories");
	}
	if (!DeleteFileW(argv[2]) || !IsFileAbsent(argv[2]))
	{
		return Fail(L"fixture PE could not be removed before the memory-only load", GetLastError());
	}

	ChildGuard child;
	child.Job = CreateJobObjectW(nullptr, nullptr);
	if (!child.Job)
	{
		return Fail(L"CreateJobObjectW", GetLastError());
	}
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo = {};
	jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (!SetInformationJobObject(
		child.Job,
		JobObjectExtendedLimitInformation,
		&jobInfo,
		sizeof(jobInfo)))
	{
		return Fail(L"SetInformationJobObject", GetLastError());
	}

	STARTUPINFOW startup = {};
	startup.cb = sizeof(startup);
	PROCESS_INFORMATION processInfo = {};
	std::wstring commandLine = L"\"";
	commandLine.append(argv[1]);
	commandLine.append(L"\"");
	if (!CreateProcessW(
		argv[1],
		commandLine.data(),
		nullptr,
		nullptr,
		FALSE,
		CREATE_SUSPENDED | CREATE_NO_WINDOW,
		nullptr,
		nullptr,
		&startup,
		&processInfo))
	{
		return Fail(L"CreateProcessW", GetLastError());
	}
	child.Process = processInfo.hProcess;
	child.Thread = processInfo.hThread;
	if (!AssignProcessToJobObject(child.Job, child.Process))
	{
		return Fail(L"AssignProcessToJobObject", GetLastError());
	}

	wchar_t mappingName[64] = {};
	if (!BuildNativeManualMapFixtureName(
		processInfo.dwProcessId,
		mappingName,
		NativeManualMapCountOf(mappingName)))
	{
		return Fail(L"shared mapping name");
	}
	child.Mapping = CreateFileMappingW(
		INVALID_HANDLE_VALUE,
		nullptr,
		PAGE_READWRITE,
		0,
		sizeof(NativeManualMapFixtureState),
		mappingName);
	if (!child.Mapping)
	{
		return Fail(L"CreateFileMappingW", GetLastError());
	}
	child.State = static_cast<NativeManualMapFixtureState*>(MapViewOfFile(
		child.Mapping,
		FILE_MAP_ALL_ACCESS,
		0,
		0,
		sizeof(NativeManualMapFixtureState)));
	if (!child.State)
	{
		return Fail(L"MapViewOfFile", GetLastError());
	}
	ZeroMemory(child.State, sizeof(*child.State));

	if (ResumeThread(child.Thread) == static_cast<DWORD>(-1))
	{
		return Fail(L"ResumeThread", GetLastError());
	}
	CloseHandle(child.Thread);
	child.Thread = nullptr;
	if (!WaitForReady(child, 10000))
	{
		return Fail(L"blank child did not reserve the fixture preferred base");
	}
	if (child.State->TargetProcessId != processInfo.dwProcessId ||
		child.State->ReservedPreferredBase != kNativeManualMapPreferredBase)
	{
		return Fail(L"preferred-base collision was not established");
	}

	if (!ManualMapHookImage(child.Process, payload.data(), payload.size()))
	{
		DWORD processExit = STILL_ACTIVE;
		GetExitCodeProcess(child.Process, &processExit);
		fwprintf(
			stderr,
			L"EVIDENCE mapperError=%lu processExit=0x%08lx flags=0x%lx "
			L"accepted=0x%llx callback=0x%llx tlsValue=0x%lx tlsIndex=%lu\n",
			GetLastError(),
			processExit,
			static_cast<unsigned long>(InterlockedCompareExchange(
				&child.State->LifecycleFlags,
				0,
				0)),
			static_cast<unsigned long long>(child.State->AcceptedModuleBase),
			static_cast<unsigned long long>(child.State->TlsCallbackModuleBase),
			static_cast<unsigned long>(child.State->TlsValueSeenByDllMain),
			static_cast<unsigned long>(child.State->TlsIndexSeenByDllMain));
		return Fail(L"valid fixture PE was rejected", GetLastError());
	}
	const LONG lifecycle = InterlockedCompareExchange(&child.State->LifecycleFlags, 0, 0);
	if ((lifecycle & kNativeManualMapRequiredLifecycleFlags) !=
		kNativeManualMapRequiredLifecycleFlags ||
		child.State->AcceptedModuleBase == 0 ||
		child.State->AcceptedModuleBase == kNativeManualMapPreferredBase ||
		child.State->TlsCallbackModuleBase != child.State->AcceptedModuleBase ||
		child.State->TlsValueSeenByDllMain != kNativeManualMapTlsCallbackValue ||
		child.State->TlsIndexSeenByDllMain == TLS_OUT_OF_INDEXES)
	{
		fwprintf(
			stderr,
			L"EVIDENCE flags=0x%lx required=0x%lx accepted=0x%llx callback=0x%llx "
			L"tlsValue=0x%lx tlsIndex=%lu preferred=0x%llx\n",
			static_cast<unsigned long>(lifecycle),
			static_cast<unsigned long>(kNativeManualMapRequiredLifecycleFlags),
			static_cast<unsigned long long>(child.State->AcceptedModuleBase),
			static_cast<unsigned long long>(child.State->TlsCallbackModuleBase),
			static_cast<unsigned long>(child.State->TlsValueSeenByDllMain),
			static_cast<unsigned long>(child.State->TlsIndexSeenByDllMain),
			static_cast<unsigned long long>(child.State->ReservedPreferredBase));
		return Fail(L"fixture lifecycle evidence is incomplete");
	}

	InterlockedExchange(&child.State->RejectAttach, 1);
	child.State->RejectedModuleBase = 0;
	SetLastError(ERROR_SUCCESS);
	if (ManualMapHookImage(child.Process, payload.data(), payload.size()))
	{
		return Fail(L"fixture attach rejection unexpectedly succeeded");
	}
	const DWORD rejectError = GetLastError();
	const LONG rejectedFlags = InterlockedCompareExchange(
		&child.State->LifecycleFlags,
		0,
		0);
	if (rejectError != ERROR_DLL_INIT_FAILED ||
		child.State->RejectedModuleBase == 0 ||
		(rejectedFlags & NativeManualMapAttachRejected) == 0)
	{
		return Fail(L"fixture rejection evidence is incomplete", rejectError);
	}

	MEMORY_BASIC_INFORMATION rejectedMemory = {};
	if (!VirtualQueryEx(
		child.Process,
		reinterpret_cast<LPCVOID>(child.State->RejectedModuleBase),
		&rejectedMemory,
		sizeof(rejectedMemory)) ||
		rejectedMemory.State != MEM_FREE)
	{
		return Fail(L"rejected image allocation was not rolled back", GetLastError());
	}

	std::vector<BYTE> malformed = payload;
	reinterpret_cast<IMAGE_DOS_HEADER*>(malformed.data())->e_magic = 0;
	SetLastError(ERROR_SUCCESS);
	if (ManualMapHookImage(child.Process, malformed.data(), malformed.size()) ||
		GetLastError() != ERROR_BAD_EXE_FORMAT)
	{
		return Fail(L"malformed PE did not fail closed", GetLastError());
	}
	if (!IsFileAbsent(argv[2]))
	{
		return Fail(L"fixture PE reappeared on disk");
	}

	InterlockedExchange(&child.State->ExitRequested, 1);
	if (WaitForSingleObject(child.Process, 10000) != WAIT_OBJECT_0)
	{
		return Fail(L"blank child did not exit");
	}
	DWORD childExitCode = MAXDWORD;
	if (!GetExitCodeProcess(child.Process, &childExitCode) || childExitCode != 0)
	{
		return Fail(L"blank child exit code", childExitCode);
	}
	child.Exited = true;

	printf(
		"PASS lifecycle=0x%lx rollback=MEM_FREE child_exit=0 payload_file=absent\n",
		static_cast<unsigned long>(InterlockedCompareExchange(
			&child.State->LifecycleFlags,
			0,
			0)));
	SecureZeroMemory(payload.data(), payload.size());
	SecureZeroMemory(malformed.data(), malformed.size());
	return 0;
}
