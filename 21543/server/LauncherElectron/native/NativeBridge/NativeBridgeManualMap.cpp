#define _CRT_SECURE_NO_WARNINGS
#include "NativeBridgeManualMap.h"

#include "NativeBridgeLog.h"
#include "NativeBridgeState.h"

#include <tlhelp32.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{
constexpr DWORD kRemoteCallTimeoutMs = 30000;
constexpr DWORD kRemoteLoaderReadyTimeoutMs = 5000;
constexpr DWORD kRemoteLoaderPollIntervalMs = 15;
constexpr SIZE_T kMaximumMappedImageSize = 128 * 1024 * 1024;
constexpr SIZE_T kMaximumTlsDataSize = 1024 * 1024;
constexpr DWORD kMaximumTlsCallbacks = 64;
constexpr DWORD kRemoteInitializerSuccess = 0x4d4d0001;

extern "C" BYTE RemoteHookInitializerStart;
extern "C" BYTE RemoteHookInitializerEnd;

struct RemoteHookInitContext
{
	ULONGLONG ModuleBase;
	ULONGLONG TlsVector;
	ULONGLONG TlsCallbacks;
	ULONGLONG FunctionTable;
	DWORD FunctionCount;
	DWORD TlsIndex;
	ULONGLONG TlsData;
	ULONGLONG RtlAddFunctionTable;
	ULONGLONG EntryPoint;
	DWORD Stage;
	DWORD EntryResult;
};

static_assert(FIELD_OFFSET(RemoteHookInitContext, ModuleBase) == 0, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, TlsVector) == 8, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, TlsCallbacks) == 16, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, FunctionTable) == 24, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, FunctionCount) == 32, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, TlsIndex) == 36, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, TlsData) == 40, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, RtlAddFunctionTable) == 48, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, EntryPoint) == 56, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, Stage) == 64, "remote stub ABI");
static_assert(FIELD_OFFSET(RemoteHookInitContext, EntryResult) == 68, "remote stub ABI");
static_assert(sizeof(RemoteHookInitContext) == 72, "remote stub ABI");
static_assert(DLL_PROCESS_ATTACH == 1, "remote stub process-attach ABI");

struct RemoteCallResult
{
	bool Started;
	bool Completed;
	DWORD ExitCode;
};

bool RangeInside(SIZE_T total, SIZE_T offset, SIZE_T size)
{
	return offset <= total && size <= total - offset;
}

void SetManualMapError(const wchar_t* message, DWORD error)
{
	SetLastError(error);
	SetQQSpeedNativeBridgeLastError(message);
	LauncherLog("ManualMapHookImage failed error=%lu", error);
}

std::wstring BaseNameOf(const std::wstring& path)
{
	const std::wstring::size_type slash = path.find_last_of(L"\\/");
	return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

LPVOID FindRemoteModuleBase(DWORD processId, const wchar_t* baseName)
{
	if (!processId || !baseName || !baseName[0])
	{
		return nullptr;
	}
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, processId);
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		return nullptr;
	}

	LPVOID result = nullptr;
	MODULEENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (Module32FirstW(snapshot, &entry))
	{
		do
		{
			if (_wcsicmp(entry.szModule, baseName) == 0)
			{
				result = entry.modBaseAddr;
				break;
			}
		} while (Module32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return result;
}

RemoteCallResult RunRemoteThread(HANDLE process, LPVOID start, LPVOID parameter)
{
	RemoteCallResult result = { false, false, 0 };
	if (!process || !start)
	{
		return result;
	}
	HANDLE thread = CreateRemoteThread(
		process,
		nullptr,
		0,
		reinterpret_cast<LPTHREAD_START_ROUTINE>(start),
		parameter,
		0,
		nullptr);
	if (!thread)
	{
		return result;
	}
	result.Started = true;

	HANDLE waits[2] = { thread, process };
	const DWORD wait = WaitForMultipleObjects(_countof(waits), waits, FALSE, kRemoteCallTimeoutMs);
	if (wait == WAIT_OBJECT_0)
	{
		result.Completed = GetExitCodeThread(thread, &result.ExitCode) != FALSE;
	}
	else if (wait == WAIT_TIMEOUT)
	{
		SetLastError(WAIT_TIMEOUT);
	}
	else if (wait == WAIT_OBJECT_0 + 1)
	{
		SetLastError(ERROR_PROCESS_ABORTED);
	}
	CloseHandle(thread);
	return result;
}

bool ReadRemoteExact(HANDLE process, LPCVOID address, void* output, SIZE_T size)
{
	SIZE_T read = 0;
	return process && address && output && size &&
		ReadProcessMemory(process, address, output, size, &read) != FALSE && read == size;
}

bool FindNextRemoteStaticTlsIndex(HANDLE process, DWORD processId, DWORD* nextIndex)
{
	if (!process || !processId || !nextIndex)
	{
		return false;
	}

	HANDLE snapshot = CreateToolhelp32Snapshot(
		TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
		processId);
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	bool success = true;
	DWORD next = 0;
	MODULEENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (!Module32FirstW(snapshot, &entry))
	{
		success = false;
	}
	else
	{
		do
		{
			IMAGE_DOS_HEADER dos = {};
			if (entry.modBaseSize < sizeof(IMAGE_DOS_HEADER) + sizeof(IMAGE_NT_HEADERS64) ||
				!ReadRemoteExact(process, entry.modBaseAddr, &dos, sizeof(dos)) ||
				dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
				static_cast<SIZE_T>(dos.e_lfanew) >
					static_cast<SIZE_T>(entry.modBaseSize) - sizeof(IMAGE_NT_HEADERS64))
			{
				success = false;
				break;
			}

			IMAGE_NT_HEADERS64 nt = {};
			if (!ReadRemoteExact(
				process,
				entry.modBaseAddr + dos.e_lfanew,
				&nt,
				sizeof(nt)) ||
				nt.Signature != IMAGE_NT_SIGNATURE ||
				nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
				nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
			{
				success = false;
				break;
			}

			const IMAGE_DATA_DIRECTORY tlsDirectory =
				nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
			if (!tlsDirectory.VirtualAddress && !tlsDirectory.Size)
			{
				continue;
			}
			if (entry.modBaseSize < sizeof(IMAGE_TLS_DIRECTORY64) ||
				!tlsDirectory.VirtualAddress ||
				tlsDirectory.Size < sizeof(IMAGE_TLS_DIRECTORY64) ||
				tlsDirectory.VirtualAddress > entry.modBaseSize -
					sizeof(IMAGE_TLS_DIRECTORY64))
			{
				success = false;
				break;
			}

			IMAGE_TLS_DIRECTORY64 tls = {};
			if (!ReadRemoteExact(
				process,
				entry.modBaseAddr + tlsDirectory.VirtualAddress,
				&tls,
				sizeof(tls)) || !tls.AddressOfIndex)
			{
				success = false;
				break;
			}
			DWORD moduleIndex = TLS_OUT_OF_INDEXES;
			if (!ReadRemoteExact(
				process,
				reinterpret_cast<LPCVOID>(tls.AddressOfIndex),
				&moduleIndex,
				sizeof(moduleIndex)) ||
				moduleIndex == TLS_OUT_OF_INDEXES || moduleIndex >= 4096)
			{
				success = false;
				break;
			}
			if (moduleIndex >= next)
			{
				next = moduleIndex + 1;
			}
		} while (Module32NextW(snapshot, &entry));
		if (success && GetLastError() != ERROR_NO_MORE_FILES)
		{
			success = false;
		}
	}
	CloseHandle(snapshot);
	if (success)
	{
		*nextIndex = next;
		SetLastError(ERROR_SUCCESS);
	}
	return success;
}

bool GetLocalProcedureOwner(FARPROC procedure, HMODULE* owner, std::wstring* path)
{
	if (!procedure || !owner || !path)
	{
		return false;
	}
	HMODULE module = nullptr;
	if (!GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(procedure),
		&module))
	{
		return false;
	}
	wchar_t modulePath[32768] = {};
	const DWORD length = GetModuleFileNameW(module, modulePath, _countof(modulePath));
	if (!length || length >= _countof(modulePath))
	{
		return false;
	}
	*owner = module;
	path->assign(modulePath, length);
	return true;
}

bool RemoteImageMatchesLocalModule(HANDLE process, HMODULE localModule, FARPROC localProcedure)
{
	if (!process || !localModule || !localProcedure)
	{
		return false;
	}

	MEMORY_BASIC_INFORMATION memory = {};
	if (VirtualQueryEx(
		process,
		reinterpret_cast<LPCVOID>(localProcedure),
		&memory,
		sizeof(memory)) != sizeof(memory) ||
		memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE ||
		memory.AllocationBase != localModule ||
		(memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) != 0)
	{
		return false;
	}

	const auto* localDos = reinterpret_cast<const IMAGE_DOS_HEADER*>(localModule);
	if (localDos->e_magic != IMAGE_DOS_SIGNATURE || localDos->e_lfanew <= 0 ||
		localDos->e_lfanew > 1024 * 1024)
	{
		return false;
	}
	const auto* localNt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
		reinterpret_cast<const BYTE*>(localModule) + localDos->e_lfanew);
	if (localNt->Signature != IMAGE_NT_SIGNATURE ||
		localNt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
		localNt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
	{
		return false;
	}

	IMAGE_DOS_HEADER remoteDos = {};
	IMAGE_NT_HEADERS64 remoteNt = {};
	if (!ReadRemoteExact(process, localModule, &remoteDos, sizeof(remoteDos)) ||
		remoteDos.e_magic != IMAGE_DOS_SIGNATURE ||
		remoteDos.e_lfanew != localDos->e_lfanew ||
		!ReadRemoteExact(
			process,
			reinterpret_cast<const BYTE*>(localModule) + remoteDos.e_lfanew,
			&remoteNt,
			sizeof(remoteNt)))
	{
		return false;
	}

	return remoteNt.Signature == localNt->Signature &&
		remoteNt.FileHeader.Machine == localNt->FileHeader.Machine &&
		remoteNt.FileHeader.TimeDateStamp == localNt->FileHeader.TimeDateStamp &&
		remoteNt.OptionalHeader.Magic == localNt->OptionalHeader.Magic &&
		remoteNt.OptionalHeader.SizeOfImage == localNt->OptionalHeader.SizeOfImage &&
		remoteNt.OptionalHeader.CheckSum == localNt->OptionalHeader.CheckSum;
}

struct RemoteLoaderSnapshot
{
	bool Ntdll;
	bool Kernel32;
	bool KernelBase;
	bool LoadLibraryOwner;
	bool RtlOwner;
};

bool QueryRemoteLoaderSnapshot(
	DWORD processId,
	const wchar_t* loadLibraryOwnerName,
	const wchar_t* rtlOwnerName,
	RemoteLoaderSnapshot* state)
{
	if (!processId || !loadLibraryOwnerName || !loadLibraryOwnerName[0] ||
		!rtlOwnerName || !rtlOwnerName[0] || !state)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return false;
	}

	*state = {};
	HANDLE snapshot = CreateToolhelp32Snapshot(
		TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32,
		processId);
	if (snapshot == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	MODULEENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (!Module32FirstW(snapshot, &entry))
	{
		const DWORD error = GetLastError();
		CloseHandle(snapshot);
		SetLastError(error);
		return false;
	}

	for (;;)
	{
		state->Ntdll = state->Ntdll || _wcsicmp(entry.szModule, L"ntdll.dll") == 0;
		state->Kernel32 = state->Kernel32 || _wcsicmp(entry.szModule, L"kernel32.dll") == 0;
		state->KernelBase = state->KernelBase || _wcsicmp(entry.szModule, L"kernelbase.dll") == 0;
		state->LoadLibraryOwner = state->LoadLibraryOwner ||
			_wcsicmp(entry.szModule, loadLibraryOwnerName) == 0;
		state->RtlOwner = state->RtlOwner || _wcsicmp(entry.szModule, rtlOwnerName) == 0;

		if (!Module32NextW(snapshot, &entry))
		{
			const DWORD error = GetLastError();
			CloseHandle(snapshot);
			if (error != ERROR_NO_MORE_FILES)
			{
				SetLastError(error ? error : ERROR_GEN_FAILURE);
				return false;
			}
			SetLastError(ERROR_SUCCESS);
			return true;
		}
	}
}

bool IsRemoteLoaderSnapshotReady(const RemoteLoaderSnapshot& state)
{
	return state.Ntdll &&
		(state.Kernel32 || state.KernelBase) &&
		state.LoadLibraryOwner &&
		state.RtlOwner;
}

bool IsTransientRemoteLoaderError(DWORD error)
{
	return error == ERROR_NOT_READY ||
		error == ERROR_NO_MORE_FILES ||
		error == ERROR_PARTIAL_COPY ||
		error == ERROR_BAD_LENGTH;
}

bool WaitForRemoteLoaderReady(
	HANDLE process,
	DWORD processId,
	FARPROC localLoadLibraryW,
	FARPROC localRtlAddFunctionTable)
{
	if (!process || !processId || !localLoadLibraryW || !localRtlAddFunctionTable)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		return false;
	}

	HMODULE loadLibraryOwner = nullptr;
	HMODULE rtlOwner = nullptr;
	std::wstring loadLibraryOwnerPath;
	std::wstring rtlOwnerPath;
	if (!GetLocalProcedureOwner(localLoadLibraryW, &loadLibraryOwner, &loadLibraryOwnerPath) ||
		!GetLocalProcedureOwner(localRtlAddFunctionTable, &rtlOwner, &rtlOwnerPath))
	{
		SetLastError(ERROR_PROC_NOT_FOUND);
		return false;
	}
	const std::wstring loadLibraryOwnerName = BaseNameOf(loadLibraryOwnerPath);
	const std::wstring rtlOwnerName = BaseNameOf(rtlOwnerPath);
	const bool expectedLoadLibraryOwner =
		_wcsicmp(loadLibraryOwnerName.c_str(), L"kernel32.dll") == 0 ||
		_wcsicmp(loadLibraryOwnerName.c_str(), L"kernelbase.dll") == 0;
	const bool expectedRtlOwner = _wcsicmp(rtlOwnerName.c_str(), L"ntdll.dll") == 0;
	if (!expectedLoadLibraryOwner || !expectedRtlOwner)
	{
		LauncherLog(
			"WaitForRemoteLoaderReady rejected unexpected local owners load=%s rtl=%s",
			WideToUtf8Log(loadLibraryOwnerName.c_str()).c_str(),
			WideToUtf8Log(rtlOwnerName.c_str()).c_str());
		SetLastError(ERROR_INVALID_DATA);
		return false;
	}

	const ULONGLONG started = GetTickCount64();
	DWORD attempts = 0;
	DWORD lastObservationError = ERROR_NOT_READY;
	DWORD lastLoggedError = ERROR_SUCCESS;
	LauncherLog(
		"WaitForRemoteLoaderReady start pid=%lu timeoutMs=%lu pollMs=%lu loadOwner=%s rtlOwner=%s",
		processId,
		kRemoteLoaderReadyTimeoutMs,
		kRemoteLoaderPollIntervalMs,
		WideToUtf8Log(loadLibraryOwnerName.c_str()).c_str(),
		WideToUtf8Log(rtlOwnerName.c_str()).c_str());

	for (;;)
	{
		++attempts;
		const DWORD processState = WaitForSingleObject(process, 0);
		if (processState == WAIT_OBJECT_0)
		{
			DWORD exitCode = MAXDWORD;
			GetExitCodeProcess(process, &exitCode);
			LauncherLog(
				"WaitForRemoteLoaderReady process exited pid=%lu exitCode=%lu attempts=%lu elapsedMs=%llu",
				processId,
				exitCode,
				attempts,
				static_cast<unsigned long long>(GetTickCount64() - started));
			SetLastError(ERROR_PROCESS_ABORTED);
			return false;
		}
		if (processState == WAIT_FAILED)
		{
			const DWORD error = GetLastError();
			LauncherLog(
				"WaitForRemoteLoaderReady process wait failed pid=%lu error=%lu",
				processId,
				error);
			SetLastError(error);
			return false;
		}

		RemoteLoaderSnapshot state = {};
		const bool snapshotComplete = QueryRemoteLoaderSnapshot(
			processId,
			loadLibraryOwnerName.c_str(),
			rtlOwnerName.c_str(),
			&state);
		const DWORD snapshotError = snapshotComplete ? ERROR_SUCCESS : GetLastError();
		if (snapshotComplete && IsRemoteLoaderSnapshotReady(state))
		{
			LauncherLog(
				"WaitForRemoteLoaderReady success pid=%lu attempts=%lu elapsedMs=%llu modules=ntdll,kernel32/kernelbase",
				processId,
				attempts,
				static_cast<unsigned long long>(GetTickCount64() - started));
			SetLastError(ERROR_SUCCESS);
			return true;
		}

		const bool verifiedSharedBases =
			RemoteImageMatchesLocalModule(process, loadLibraryOwner, localLoadLibraryW) &&
			RemoteImageMatchesLocalModule(process, rtlOwner, localRtlAddFunctionTable);
		if (verifiedSharedBases)
		{
			LauncherLog(
				"WaitForRemoteLoaderReady success pid=%lu attempts=%lu elapsedMs=%llu mode=verified-shared-base",
				processId,
				attempts,
				static_cast<unsigned long long>(GetTickCount64() - started));
			SetLastError(ERROR_SUCCESS);
			return true;
		}

		lastObservationError = snapshotComplete ? ERROR_NOT_READY : snapshotError;
		if (!snapshotComplete && !IsTransientRemoteLoaderError(lastObservationError))
		{
			LauncherLog(
				"WaitForRemoteLoaderReady snapshot failed pid=%lu error=%lu attempts=%lu",
				processId,
				lastObservationError,
				attempts);
			SetLastError(lastObservationError);
			return false;
		}
		if (lastObservationError != lastLoggedError)
		{
			LauncherLog(
				"WaitForRemoteLoaderReady pending pid=%lu error=%lu ntdll=%d kernel32=%d kernelbase=%d attempts=%lu",
				processId,
				lastObservationError,
				state.Ntdll ? 1 : 0,
				state.Kernel32 ? 1 : 0,
				state.KernelBase ? 1 : 0,
				attempts);
			lastLoggedError = lastObservationError;
		}

		const ULONGLONG elapsed = GetTickCount64() - started;
		if (elapsed >= kRemoteLoaderReadyTimeoutMs)
		{
			LauncherLog(
				"WaitForRemoteLoaderReady timeout pid=%lu attempts=%lu elapsedMs=%llu lastError=%lu",
				processId,
				attempts,
				static_cast<unsigned long long>(elapsed),
				lastObservationError);
			SetLastError(ERROR_TIMEOUT);
			return false;
		}

		const DWORD remaining = static_cast<DWORD>(kRemoteLoaderReadyTimeoutMs - elapsed);
		const DWORD delay = (std::min)(kRemoteLoaderPollIntervalMs, remaining);
		const DWORD delayResult = WaitForSingleObject(process, delay);
		if (delayResult == WAIT_FAILED)
		{
			const DWORD error = GetLastError();
			LauncherLog(
				"WaitForRemoteLoaderReady poll wait failed pid=%lu error=%lu",
				processId,
				error);
			SetLastError(error);
			return false;
		}
	}
}

LPVOID ResolveRemoteModuleBase(
	HANDLE process,
	DWORD processId,
	HMODULE localModule,
	const std::wstring& baseName,
	FARPROC localProcedure)
{
	LPVOID remoteModule = FindRemoteModuleBase(processId, baseName.c_str());
	if (remoteModule)
	{
		return remoteModule;
	}
	if (RemoteImageMatchesLocalModule(process, localModule, localProcedure))
	{
		LauncherLog(
			"Remote module enumeration unavailable; verified identical image at shared ASLR base module=%s base=%p",
			WideToUtf8Log(baseName.c_str()).c_str(),
			localModule);
		return localModule;
	}
	return nullptr;
}

LPVOID ResolveExistingRemoteProcedure(
	HANDLE process,
	DWORD processId,
	FARPROC localProcedure)
{
	HMODULE localOwner = nullptr;
	std::wstring ownerPath;
	if (!GetLocalProcedureOwner(localProcedure, &localOwner, &ownerPath))
	{
		return nullptr;
	}
	const std::wstring ownerName = BaseNameOf(ownerPath);
	LPVOID remoteOwner = ResolveRemoteModuleBase(
		process,
		processId,
		localOwner,
		ownerName,
		localProcedure);
	if (!remoteOwner)
	{
		LauncherLog(
			"Remote procedure owner could not be verified module=%s local=%p error=%lu",
			WideToUtf8Log(ownerName.c_str()).c_str(),
			localOwner,
			GetLastError());
		return nullptr;
	}
	const ULONG_PTR rva = reinterpret_cast<ULONG_PTR>(localProcedure) -
		reinterpret_cast<ULONG_PTR>(localOwner);
	return reinterpret_cast<BYTE*>(remoteOwner) + rva;
}

bool RemoteLoadModule(
	HANDLE process,
	DWORD processId,
	LPVOID remoteLoadLibraryW,
	const std::wstring& fullPath,
	const std::wstring& baseName,
	HMODULE localModule,
	LPVOID* remoteBase)
{
	if (!remoteBase || !localModule || fullPath.empty() || baseName.empty())
	{
		return false;
	}
	*remoteBase = ResolveRemoteModuleBase(
		process,
		processId,
		localModule,
		baseName,
		reinterpret_cast<FARPROC>(localModule));
	if (*remoteBase)
	{
		return true;
	}

	const SIZE_T bytes = (fullPath.size() + 1) * sizeof(wchar_t);
	LPVOID remotePath = VirtualAllocEx(process, nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	if (!remotePath)
	{
		return false;
	}
	SIZE_T written = 0;
	if (!WriteProcessMemory(process, remotePath, fullPath.c_str(), bytes, &written) || written != bytes)
	{
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
		return false;
	}

	const RemoteCallResult loaded = RunRemoteThread(process, remoteLoadLibraryW, remotePath);
	if (loaded.Completed || !loaded.Started)
	{
		VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
	}
	else
	{
		// The remote loader may still read this buffer after a timeout. The caller
		// terminates a failed GameApp and lets the process reclaim it.
		return false;
	}
	for (int attempt = 0; attempt < 20; ++attempt)
	{
		*remoteBase = ResolveRemoteModuleBase(
			process,
			processId,
			localModule,
			baseName,
			reinterpret_cast<FARPROC>(localModule));
		if (*remoteBase)
		{
			return true;
		}
		Sleep(10);
	}
	return false;
}

bool ResolveImportedProcedure(
	HANDLE process,
	DWORD processId,
	LPVOID remoteLoadLibraryW,
	FARPROC localProcedure,
	ULONGLONG* remoteProcedure)
{
	if (!remoteProcedure)
	{
		return false;
	}
	HMODULE localOwner = nullptr;
	std::wstring ownerPath;
	if (!GetLocalProcedureOwner(localProcedure, &localOwner, &ownerPath))
	{
		return false;
	}
	const std::wstring ownerName = BaseNameOf(ownerPath);
	LPVOID remoteOwner = nullptr;
	if (!RemoteLoadModule(
		process,
		processId,
		remoteLoadLibraryW,
		ownerPath,
		ownerName,
		localOwner,
		&remoteOwner))
	{
		return false;
	}
	const ULONG_PTR rva = reinterpret_cast<ULONG_PTR>(localProcedure) -
		reinterpret_cast<ULONG_PTR>(localOwner);
	*remoteProcedure = reinterpret_cast<ULONGLONG>(remoteOwner) + rva;
	return true;
}

bool IsBareDllName(const char* name, SIZE_T available)
{
	if (!name || !available)
	{
		return false;
	}
	const SIZE_T length = strnlen_s(name, available);
	if (!length || length >= available || length > 128)
	{
		return false;
	}
	for (SIZE_T index = 0; index < length; ++index)
	{
		const unsigned char value = static_cast<unsigned char>(name[index]);
		if (value < 0x20 || value > 0x7e || value == '\\' || value == '/' || value == ':')
		{
			return false;
		}
	}
	return length >= 4 && _stricmp(name + length - 4, ".dll") == 0;
}

bool LoadLocalSystemModule(const char* name, HMODULE* module, std::wstring* path)
{
	if (!name || !module || !path)
	{
		return false;
	}
	wchar_t wideName[160] = {};
	if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, name, -1, wideName, _countof(wideName)))
	{
		return false;
	}
	wchar_t systemDirectory[MAX_PATH] = {};
	const UINT directoryLength = GetSystemDirectoryW(systemDirectory, _countof(systemDirectory));
	if (!directoryLength || directoryLength >= _countof(systemDirectory))
	{
		return false;
	}
	std::wstring fullPath(systemDirectory, directoryLength);
	fullPath.append(L"\\");
	fullPath.append(wideName);
	HMODULE loaded = LoadLibraryExW(fullPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!loaded)
	{
		return false;
	}
	wchar_t canonical[32768] = {};
	const DWORD canonicalLength = GetModuleFileNameW(loaded, canonical, _countof(canonical));
	if (!canonicalLength || canonicalLength >= _countof(canonical))
	{
		FreeLibrary(loaded);
		return false;
	}
	*module = loaded;
	path->assign(canonical, canonicalLength);
	return true;
}

bool ApplyRelocations(BYTE* image, SIZE_T imageSize, ULONGLONG remoteBase, IMAGE_NT_HEADERS64* nt)
{
	const LONGLONG delta = static_cast<LONGLONG>(remoteBase - nt->OptionalHeader.ImageBase);
	if (!delta)
	{
		return true;
	}
	const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
	if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_BASE_RELOCATION) ||
		!RangeInside(imageSize, directory.VirtualAddress, directory.Size))
	{
		return false;
	}

	SIZE_T consumed = 0;
	while (consumed < directory.Size)
	{
		if (directory.Size - consumed < sizeof(IMAGE_BASE_RELOCATION))
		{
			return false;
		}
		auto* block = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
			image + directory.VirtualAddress + consumed);
		if (block->SizeOfBlock < sizeof(IMAGE_BASE_RELOCATION) ||
			block->SizeOfBlock > directory.Size - consumed ||
			(block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) % sizeof(WORD) != 0)
		{
			return false;
		}
		const DWORD count =
			(block->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
		const WORD* entries = reinterpret_cast<const WORD*>(block + 1);
		for (DWORD index = 0; index < count; ++index)
		{
			const WORD type = entries[index] >> 12;
			const WORD offset = entries[index] & 0x0fff;
			if (type == IMAGE_REL_BASED_ABSOLUTE)
			{
				continue;
			}
			if (type != IMAGE_REL_BASED_DIR64)
			{
				return false;
			}
			const SIZE_T patchRva = static_cast<SIZE_T>(block->VirtualAddress) + offset;
			if (!RangeInside(imageSize, patchRva, sizeof(ULONGLONG)))
			{
				return false;
			}
			auto* patch = reinterpret_cast<ULONGLONG*>(image + patchRva);
			*patch = static_cast<ULONGLONG>(static_cast<LONGLONG>(*patch) + delta);
		}
		consumed += block->SizeOfBlock;
	}
	return consumed == directory.Size;
}

bool ResolveImports(
	HANDLE process,
	DWORD processId,
	BYTE* image,
	SIZE_T imageSize,
	IMAGE_NT_HEADERS64* nt,
	LPVOID remoteLoadLibraryW)
{
	const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!directory.VirtualAddress && !directory.Size)
	{
		return true;
	}
	if (!directory.VirtualAddress || directory.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) ||
		!RangeInside(imageSize, directory.VirtualAddress, directory.Size))
	{
		return false;
	}

	const SIZE_T descriptorLimit = directory.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);
	auto* descriptors = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + directory.VirtualAddress);
	bool terminated = false;
	for (SIZE_T descriptorIndex = 0; descriptorIndex < descriptorLimit; ++descriptorIndex)
	{
		auto& descriptor = descriptors[descriptorIndex];
		if (!descriptor.Name && !descriptor.FirstThunk && !descriptor.OriginalFirstThunk)
		{
			terminated = true;
			break;
		}
		if (!descriptor.Name || !descriptor.FirstThunk || descriptor.Name >= imageSize)
		{
			return false;
		}
		const char* moduleName = reinterpret_cast<const char*>(image + descriptor.Name);
		if (!IsBareDllName(moduleName, imageSize - descriptor.Name))
		{
			return false;
		}

		HMODULE localModule = nullptr;
		std::wstring localModulePath;
		if (!LoadLocalSystemModule(moduleName, &localModule, &localModulePath))
		{
			return false;
		}
		LPVOID remoteModule = nullptr;
		if (!RemoteLoadModule(
			process,
			processId,
			remoteLoadLibraryW,
			localModulePath,
			BaseNameOf(localModulePath),
			localModule,
			&remoteModule))
		{
			FreeLibrary(localModule);
			return false;
		}

		const DWORD lookupRva = descriptor.OriginalFirstThunk
			? descriptor.OriginalFirstThunk
			: descriptor.FirstThunk;
		if (!RangeInside(imageSize, lookupRva, sizeof(IMAGE_THUNK_DATA64)) ||
			!RangeInside(imageSize, descriptor.FirstThunk, sizeof(IMAGE_THUNK_DATA64)))
		{
			FreeLibrary(localModule);
			return false;
		}
		auto* lookup = reinterpret_cast<IMAGE_THUNK_DATA64*>(image + lookupRva);
		auto* iat = reinterpret_cast<IMAGE_THUNK_DATA64*>(image + descriptor.FirstThunk);
		const SIZE_T lookupCount = (imageSize - lookupRva) / sizeof(IMAGE_THUNK_DATA64);
		const SIZE_T iatCount = (imageSize - descriptor.FirstThunk) / sizeof(IMAGE_THUNK_DATA64);
		const SIZE_T thunkLimit = lookupCount < iatCount ? lookupCount : iatCount;
		bool thunkTerminated = false;
		for (SIZE_T thunkIndex = 0; thunkIndex < thunkLimit; ++thunkIndex)
		{
			if (!lookup[thunkIndex].u1.AddressOfData)
			{
				thunkTerminated = true;
				break;
			}
			FARPROC localProcedure = nullptr;
			if (IMAGE_SNAP_BY_ORDINAL64(lookup[thunkIndex].u1.Ordinal))
			{
				localProcedure = GetProcAddress(
					localModule,
					reinterpret_cast<LPCSTR>(IMAGE_ORDINAL64(lookup[thunkIndex].u1.Ordinal)));
			}
			else
			{
				const ULONGLONG nameRva64 = lookup[thunkIndex].u1.AddressOfData;
				if (nameRva64 > MAXDWORD ||
					!RangeInside(imageSize, static_cast<SIZE_T>(nameRva64), sizeof(IMAGE_IMPORT_BY_NAME)))
				{
					FreeLibrary(localModule);
					return false;
				}
				auto* importName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image + nameRva64);
				const SIZE_T available = imageSize - static_cast<SIZE_T>(nameRva64) -
					FIELD_OFFSET(IMAGE_IMPORT_BY_NAME, Name);
				if (!strnlen_s(reinterpret_cast<const char*>(importName->Name), available) ||
					strnlen_s(reinterpret_cast<const char*>(importName->Name), available) >= available)
				{
					FreeLibrary(localModule);
					return false;
				}
				localProcedure = GetProcAddress(localModule, reinterpret_cast<LPCSTR>(importName->Name));
			}
			ULONGLONG remoteProcedure = 0;
			if (!localProcedure || !ResolveImportedProcedure(
				process,
				processId,
				remoteLoadLibraryW,
				localProcedure,
				&remoteProcedure))
			{
				FreeLibrary(localModule);
				return false;
			}
			iat[thunkIndex].u1.Function = remoteProcedure;
		}
		FreeLibrary(localModule);
		if (!thunkTerminated)
		{
			return false;
		}
	}
	return terminated;
}

DWORD ProtectionForSection(DWORD characteristics)
{
	const bool execute = (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
	const bool read = (characteristics & IMAGE_SCN_MEM_READ) != 0;
	const bool write = (characteristics & IMAGE_SCN_MEM_WRITE) != 0;
	DWORD protection = PAGE_NOACCESS;
	if (execute)
	{
		protection = write ? PAGE_EXECUTE_READWRITE : (read ? PAGE_EXECUTE_READ : PAGE_EXECUTE);
	}
	else if (write)
	{
		protection = PAGE_READWRITE;
	}
	else if (read)
	{
		protection = PAGE_READONLY;
	}
	if (characteristics & IMAGE_SCN_MEM_NOT_CACHED)
	{
		protection |= PAGE_NOCACHE;
	}
	return protection;
}

bool ApplyImageProtections(
	HANDLE process,
	LPVOID remoteImage,
	const IMAGE_NT_HEADERS64* nt,
	const IMAGE_SECTION_HEADER* sections)
{
	DWORD oldProtection = 0;
	if (!VirtualProtectEx(
		process,
		remoteImage,
		nt->OptionalHeader.SizeOfImage,
		PAGE_NOACCESS,
		&oldProtection))
	{
		return false;
	}
	if (!VirtualProtectEx(
		process,
		remoteImage,
		nt->OptionalHeader.SizeOfHeaders,
		PAGE_READONLY,
		&oldProtection))
	{
		return false;
	}
	for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index)
	{
		const auto& section = sections[index];
		const SIZE_T size = section.Misc.VirtualSize > section.SizeOfRawData
			? section.Misc.VirtualSize
			: section.SizeOfRawData;
		if (!size)
		{
			continue;
		}
		if (!VirtualProtectEx(
			process,
			reinterpret_cast<BYTE*>(remoteImage) + section.VirtualAddress,
			size,
			ProtectionForSection(section.Characteristics),
			&oldProtection))
		{
			return false;
		}
	}
	return FlushInstructionCache(process, remoteImage, nt->OptionalHeader.SizeOfImage) != FALSE;
}
}

BOOL ManualMapHookImage(HANDLE process, const BYTE* fileBytes, SIZE_T fileSize)
{
	if (!process || !fileBytes || fileSize < sizeof(IMAGE_DOS_HEADER))
	{
		SetManualMapError(L"The in-memory Hook payload is unavailable.", ERROR_INVALID_PARAMETER);
		return FALSE;
	}

	BYTE* localImage = nullptr;
	LPVOID remoteImage = nullptr;
	LPVOID remoteTlsData = nullptr;
	LPVOID remoteTlsVector = nullptr;
	LPVOID remoteContext = nullptr;
	LPVOID remoteStub = nullptr;
	DWORD tlsIndex = TLS_OUT_OF_INDEXES;
	LPVOID remoteRtlDeleteFunctionTable = nullptr;
	LPVOID remoteFunctionTable = nullptr;
	bool remoteInitializerCompleted = false;
	bool remoteInitializerStarted = false;
	bool unwindMayBeRegistered = false;
	DWORD failureError = ERROR_BAD_EXE_FORMAT;
	const wchar_t* failureMessage = L"The in-memory Hook PE image is malformed.";
	IMAGE_NT_HEADERS64* nt = nullptr;
	IMAGE_SECTION_HEADER* sections = nullptr;
	DWORD processId = 0;
	HMODULE kernel32 = nullptr;
	HMODULE ntdll = nullptr;
	LPVOID remoteLoadLibraryW = nullptr;
	LPVOID remoteRtlAddFunctionTable = nullptr;
	RemoteHookInitContext init = {};
	IMAGE_DATA_DIRECTORY exceptionDirectory = {};
	DWORD functionCount = 0;
	const RUNTIME_FUNCTION* runtimeFunctions = nullptr;
	IMAGE_DATA_DIRECTORY tlsDirectory = {};
	const BYTE* stubStart = nullptr;
	const BYTE* stubEnd = nullptr;
	SIZE_T stubSize = 0;
	DWORD mappedImageSize = 0;

	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(fileBytes);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
		!RangeInside(fileSize, static_cast<SIZE_T>(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS64)))
	{
		SetManualMapError(failureMessage, failureError);
		return FALSE;
	}
	const auto* fileNt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(fileBytes + dos->e_lfanew);
	if (fileNt->Signature != IMAGE_NT_SIGNATURE ||
		fileNt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
		(fileNt->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0 ||
		fileNt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
		fileNt->OptionalHeader.NumberOfRvaAndSizes < IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
		fileNt->OptionalHeader.SectionAlignment < 0x1000 ||
		fileNt->OptionalHeader.SizeOfImage == 0 ||
		fileNt->OptionalHeader.SizeOfImage > kMaximumMappedImageSize ||
		fileNt->OptionalHeader.SizeOfHeaders > fileSize ||
		fileNt->FileHeader.NumberOfSections == 0 || fileNt->FileHeader.NumberOfSections > 96 ||
		fileNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress != 0 ||
		fileNt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].Size != 0)
	{
		SetManualMapError(failureMessage, failureError);
		return FALSE;
	}

	const SIZE_T sectionOffset = static_cast<SIZE_T>(dos->e_lfanew) +
		FIELD_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader) + fileNt->FileHeader.SizeOfOptionalHeader;
	const SIZE_T sectionBytes =
		static_cast<SIZE_T>(fileNt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
	if (!RangeInside(fileSize, sectionOffset, sectionBytes))
	{
		SetManualMapError(failureMessage, failureError);
		return FALSE;
	}
	const auto* fileSections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(fileBytes + sectionOffset);
	processId = GetProcessId(process);
	kernel32 = GetModuleHandleW(L"kernel32.dll");
	ntdll = GetModuleHandleW(L"ntdll.dll");
	const FARPROC localLoadLibraryW = kernel32 ? GetProcAddress(kernel32, "LoadLibraryW") : nullptr;
	const FARPROC localRtlAddFunctionTable = ntdll ? GetProcAddress(ntdll, "RtlAddFunctionTable") : nullptr;
	const FARPROC localRtlDeleteFunctionTable = ntdll ? GetProcAddress(ntdll, "RtlDeleteFunctionTable") : nullptr;
	if (!processId || !kernel32 || !ntdll || !localLoadLibraryW ||
		!localRtlAddFunctionTable || !localRtlDeleteFunctionTable)
	{
		const DWORD error = GetLastError() ? GetLastError() : ERROR_PROC_NOT_FOUND;
		SetManualMapError(L"Failed to resolve the local loader runtime.", error);
		return FALSE;
	}
	if (!WaitForRemoteLoaderReady(
		process,
		processId,
		localLoadLibraryW,
		localRtlAddFunctionTable))
	{
		const DWORD error = GetLastError() ? GetLastError() : ERROR_TIMEOUT;
		SetManualMapError(L"GameApp loader did not become ready for in-memory Hook mapping.", error);
		return FALSE;
	}
	remoteLoadLibraryW = ResolveExistingRemoteProcedure(
		process,
		processId,
		localLoadLibraryW);
	remoteRtlAddFunctionTable = ResolveExistingRemoteProcedure(
		process,
		processId,
		localRtlAddFunctionTable);
	remoteRtlDeleteFunctionTable = ResolveExistingRemoteProcedure(
		process,
		processId,
		localRtlDeleteFunctionTable);
	if (!remoteLoadLibraryW || !remoteRtlAddFunctionTable || !remoteRtlDeleteFunctionTable)
	{
		const DWORD error = GetLastError() ? GetLastError() : ERROR_PROC_NOT_FOUND;
		SetManualMapError(L"Failed to resolve required loader functions in GameApp.", error);
		return FALSE;
	}

	localImage = static_cast<BYTE*>(HeapAlloc(
		GetProcessHeap(),
		HEAP_ZERO_MEMORY,
		fileNt->OptionalHeader.SizeOfImage));
	if (!localImage)
	{
		SetManualMapError(L"Memory allocation failed while preparing the Hook image.", ERROR_NOT_ENOUGH_MEMORY);
		return FALSE;
	}
	memcpy(localImage, fileBytes, fileNt->OptionalHeader.SizeOfHeaders);
	for (WORD index = 0; index < fileNt->FileHeader.NumberOfSections; ++index)
	{
		const auto& section = fileSections[index];
		const DWORD mappedSize = section.Misc.VirtualSize > section.SizeOfRawData
			? section.Misc.VirtualSize
			: section.SizeOfRawData;
		if (mappedSize > fileNt->OptionalHeader.SizeOfImage ||
			section.VirtualAddress > fileNt->OptionalHeader.SizeOfImage - mappedSize ||
			(section.SizeOfRawData && !RangeInside(fileSize, section.PointerToRawData, section.SizeOfRawData)))
		{
			goto cleanup;
		}
		if (section.SizeOfRawData)
		{
			memcpy(
				localImage + section.VirtualAddress,
				fileBytes + section.PointerToRawData,
				section.SizeOfRawData);
		}
	}

	nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(localImage + dos->e_lfanew);
	sections = reinterpret_cast<IMAGE_SECTION_HEADER*>(localImage + sectionOffset);
	mappedImageSize = nt->OptionalHeader.SizeOfImage;
	remoteImage = VirtualAllocEx(
		process,
		reinterpret_cast<LPVOID>(nt->OptionalHeader.ImageBase),
		nt->OptionalHeader.SizeOfImage,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE);
	if (!remoteImage)
	{
		remoteImage = VirtualAllocEx(
			process,
			nullptr,
			nt->OptionalHeader.SizeOfImage,
			MEM_RESERVE | MEM_COMMIT,
			PAGE_READWRITE);
	}
	if (!remoteImage)
	{
		failureError = GetLastError();
		failureMessage = L"Failed to reserve memory for the in-memory Hook image.";
		goto cleanup;
	}

	if (!ApplyRelocations(
		localImage,
		nt->OptionalHeader.SizeOfImage,
		reinterpret_cast<ULONGLONG>(remoteImage),
		nt))
	{
		failureMessage = L"The Hook relocation table is invalid or unsupported.";
		goto cleanup;
	}

	if (!ResolveImports(
		process,
		processId,
		localImage,
		nt->OptionalHeader.SizeOfImage,
		nt,
		remoteLoadLibraryW))
	{
		failureError = GetLastError() ? GetLastError() : ERROR_PROC_NOT_FOUND;
		failureMessage = L"The Hook import table could not be resolved safely.";
		goto cleanup;
	}

	init = {};
	init.ModuleBase = reinterpret_cast<ULONGLONG>(remoteImage);
	init.TlsIndex = TLS_OUT_OF_INDEXES;
	init.RtlAddFunctionTable = reinterpret_cast<ULONGLONG>(remoteRtlAddFunctionTable);
	init.EntryPoint = reinterpret_cast<ULONGLONG>(remoteImage) + nt->OptionalHeader.AddressOfEntryPoint;

	exceptionDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
	if (!exceptionDirectory.VirtualAddress ||
		exceptionDirectory.Size < sizeof(RUNTIME_FUNCTION) ||
		exceptionDirectory.Size % sizeof(RUNTIME_FUNCTION) != 0 ||
		!RangeInside(nt->OptionalHeader.SizeOfImage, exceptionDirectory.VirtualAddress, exceptionDirectory.Size))
	{
		failureMessage = L"The Hook x64 unwind table is missing or malformed.";
		goto cleanup;
	}
	functionCount = exceptionDirectory.Size / sizeof(RUNTIME_FUNCTION);
	runtimeFunctions = reinterpret_cast<const RUNTIME_FUNCTION*>(
		localImage + exceptionDirectory.VirtualAddress);
	for (DWORD index = 0; index < functionCount; ++index)
	{
		if (runtimeFunctions[index].BeginAddress >= runtimeFunctions[index].EndAddress ||
			runtimeFunctions[index].EndAddress > nt->OptionalHeader.SizeOfImage ||
			runtimeFunctions[index].UnwindData >= nt->OptionalHeader.SizeOfImage)
		{
			failureMessage = L"The Hook x64 unwind entries are malformed.";
			goto cleanup;
		}
	}
	remoteFunctionTable = reinterpret_cast<BYTE*>(remoteImage) + exceptionDirectory.VirtualAddress;
	init.FunctionTable = reinterpret_cast<ULONGLONG>(remoteFunctionTable);
	init.FunctionCount = functionCount;

	tlsDirectory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
	if (tlsDirectory.VirtualAddress || tlsDirectory.Size)
	{
		if (!tlsDirectory.VirtualAddress || tlsDirectory.Size < sizeof(IMAGE_TLS_DIRECTORY64) ||
			!RangeInside(nt->OptionalHeader.SizeOfImage, tlsDirectory.VirtualAddress, sizeof(IMAGE_TLS_DIRECTORY64)))
		{
			failureMessage = L"The Hook TLS directory is malformed.";
			goto cleanup;
		}
		auto* tls = reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(localImage + tlsDirectory.VirtualAddress);
		const ULONGLONG imageBase = reinterpret_cast<ULONGLONG>(remoteImage);
		const ULONGLONG imageEnd = imageBase + nt->OptionalHeader.SizeOfImage;
		if (tls->AddressOfIndex < imageBase || tls->AddressOfIndex + sizeof(DWORD) > imageEnd ||
			tls->EndAddressOfRawData < tls->StartAddressOfRawData ||
			tls->StartAddressOfRawData < imageBase || tls->EndAddressOfRawData > imageEnd)
		{
			failureMessage = L"The Hook TLS addresses are outside the mapped image.";
			goto cleanup;
		}
		const SIZE_T templateSize = static_cast<SIZE_T>(
			tls->EndAddressOfRawData - tls->StartAddressOfRawData);
		const SIZE_T tlsDataSize = templateSize + tls->SizeOfZeroFill;
		if (tlsDataSize > kMaximumTlsDataSize ||
			tlsDataSize < templateSize ||
			!RangeInside(
				nt->OptionalHeader.SizeOfImage,
				static_cast<SIZE_T>(tls->StartAddressOfRawData - imageBase),
				templateSize))
		{
			failureMessage = L"The Hook TLS template is too large or malformed.";
			goto cleanup;
		}

		if (!FindNextRemoteStaticTlsIndex(process, processId, &tlsIndex))
		{
			failureError = GetLastError() ? GetLastError() : ERROR_INVALID_DATA;
			failureMessage = L"The target loader TLS vector could not be inspected safely.";
			goto cleanup;
		}
		LauncherLog("ManualMapHookImage initializer static TLS index=%lu", tlsIndex);
		init.TlsIndex = tlsIndex;
		*reinterpret_cast<DWORD*>(
			localImage + static_cast<SIZE_T>(tls->AddressOfIndex - imageBase)) = tlsIndex;

		remoteTlsData = VirtualAllocEx(
			process,
			nullptr,
			tlsDataSize ? tlsDataSize : 1,
			MEM_RESERVE | MEM_COMMIT,
			PAGE_READWRITE);
		if (!remoteTlsData)
		{
			failureError = GetLastError();
			failureMessage = L"GameApp could not allocate the Hook TLS template.";
			goto cleanup;
		}
		if (templateSize)
		{
			SIZE_T written = 0;
			if (!WriteProcessMemory(
				process,
				remoteTlsData,
				localImage + static_cast<SIZE_T>(tls->StartAddressOfRawData - imageBase),
				templateSize,
				&written) || written != templateSize)
			{
				failureError = GetLastError();
				failureMessage = L"The Hook TLS template could not be copied into GameApp.";
				goto cleanup;
			}
		}

		const SIZE_T tlsVectorBytes =
			(static_cast<SIZE_T>(tlsIndex) + 1) * sizeof(ULONGLONG);
		remoteTlsVector = VirtualAllocEx(
			process,
			nullptr,
			tlsVectorBytes,
			MEM_RESERVE | MEM_COMMIT,
			PAGE_READWRITE);
		if (!remoteTlsVector)
		{
			failureError = GetLastError();
			failureMessage = L"The initializer TLS vector could not be allocated.";
			goto cleanup;
		}
		init.TlsVector = reinterpret_cast<ULONGLONG>(remoteTlsVector);
		init.TlsData = reinterpret_cast<ULONGLONG>(remoteTlsData);

		if (tls->AddressOfCallBacks)
		{
			if (tls->AddressOfCallBacks < imageBase || tls->AddressOfCallBacks >= imageEnd)
			{
				failureMessage = L"The Hook TLS callback table is outside the mapped image.";
				goto cleanup;
			}
			const SIZE_T callbacksRva = static_cast<SIZE_T>(tls->AddressOfCallBacks - imageBase);
			bool callbackTerminated = false;
			for (DWORD index = 0; index < kMaximumTlsCallbacks; ++index)
			{
				const SIZE_T callbackOffset = callbacksRva + static_cast<SIZE_T>(index) * sizeof(ULONGLONG);
				if (!RangeInside(nt->OptionalHeader.SizeOfImage, callbackOffset, sizeof(ULONGLONG)))
				{
					break;
				}
				const ULONGLONG callback = *reinterpret_cast<const ULONGLONG*>(localImage + callbackOffset);
				if (!callback)
				{
					callbackTerminated = true;
					break;
				}
				if (callback < imageBase || callback >= imageEnd)
				{
					failureMessage = L"The Hook TLS callback address is outside the mapped image.";
					goto cleanup;
				}
			}
			if (!callbackTerminated)
			{
				failureMessage = L"The Hook TLS callback table is not terminated.";
				goto cleanup;
			}
			init.TlsCallbacks = tls->AddressOfCallBacks;
		}
	}

	{
		SIZE_T written = 0;
		if (!WriteProcessMemory(
			process,
			remoteImage,
			localImage,
			nt->OptionalHeader.SizeOfImage,
			&written) || written != nt->OptionalHeader.SizeOfImage)
		{
			failureError = GetLastError();
			failureMessage = L"The prepared Hook image could not be copied into GameApp.";
			goto cleanup;
		}
	}
	if (!ApplyImageProtections(process, remoteImage, nt, sections))
	{
		failureError = GetLastError();
		failureMessage = L"The Hook section protections could not be applied.";
		goto cleanup;
	}

	remoteContext = VirtualAllocEx(
		process,
		nullptr,
		sizeof(init),
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE);
	stubStart = &RemoteHookInitializerStart;
	stubEnd = &RemoteHookInitializerEnd;
	stubSize = stubEnd > stubStart ? static_cast<SIZE_T>(stubEnd - stubStart) : 0;
	if (!remoteContext || !stubSize || stubSize > 4096)
	{
		failureError = GetLastError() ? GetLastError() : ERROR_INVALID_DATA;
		failureMessage = L"The remote Hook initializer could not be allocated.";
		goto cleanup;
	}
	remoteStub = VirtualAllocEx(
		process,
		nullptr,
		stubSize,
		MEM_RESERVE | MEM_COMMIT,
		PAGE_READWRITE);
	if (!remoteStub)
	{
		failureError = GetLastError();
		failureMessage = L"The remote Hook initializer could not be allocated.";
		goto cleanup;
	}
	{
		SIZE_T writtenContext = 0;
		SIZE_T writtenStub = 0;
		if (!WriteProcessMemory(process, remoteContext, &init, sizeof(init), &writtenContext) ||
			writtenContext != sizeof(init) ||
			!WriteProcessMemory(process, remoteStub, stubStart, stubSize, &writtenStub) ||
			writtenStub != stubSize)
		{
			failureError = GetLastError();
			failureMessage = L"The remote Hook initializer could not be copied.";
			goto cleanup;
		}
		DWORD oldProtection = 0;
		if (!VirtualProtectEx(process, remoteStub, stubSize, PAGE_EXECUTE_READ, &oldProtection) ||
			!FlushInstructionCache(process, remoteStub, stubSize))
		{
			failureError = GetLastError();
			failureMessage = L"The remote Hook initializer could not be protected.";
			goto cleanup;
		}
	}

	{
		const RemoteCallResult initialized = RunRemoteThread(
			process,
			remoteStub,
			remoteContext);
		remoteInitializerStarted = initialized.Started;
		remoteInitializerCompleted = initialized.Completed;
		RemoteHookInitContext observed = {};
		if (remoteContext && ReadRemoteExact(
			process,
			remoteContext,
			&observed,
			sizeof(observed)))
		{
			unwindMayBeRegistered = observed.Stage >= 2;
			LauncherLog(
				"ManualMapHookImage initializer stage=%lu entry=%lu exit=%lu",
				observed.Stage,
				observed.EntryResult,
				initialized.ExitCode);
		}
		else
		{
			unwindMayBeRegistered = initialized.Completed;
		}
		if (!initialized.Completed || initialized.ExitCode != kRemoteInitializerSuccess)
		{
			if (initialized.Completed)
			{
				failureError = ERROR_DLL_INIT_FAILED;
				failureMessage = L"The in-memory Hook rejected DLL_PROCESS_ATTACH.";
			}
			else
			{
				failureError = GetLastError() ? GetLastError() : ERROR_TIMEOUT;
				failureMessage = L"The in-memory Hook initializer did not complete.";
			}
			goto cleanup;
		}
	}

	if (remoteTlsVector)
	{
		VirtualFreeEx(process, remoteTlsVector, 0, MEM_RELEASE);
		remoteTlsVector = nullptr;
	}
	if (remoteTlsData)
	{
		VirtualFreeEx(process, remoteTlsData, 0, MEM_RELEASE);
		remoteTlsData = nullptr;
	}
	VirtualFreeEx(process, remoteStub, 0, MEM_RELEASE);
	remoteStub = nullptr;
	VirtualFreeEx(process, remoteContext, 0, MEM_RELEASE);
	remoteContext = nullptr;
	SecureZeroMemory(localImage, mappedImageSize);
	HeapFree(GetProcessHeap(), 0, localImage);
	localImage = nullptr;
	LauncherLog(
		"ManualMapHookImage success process=%lu remoteBase=%p imageSize=%lu tls=%d unwind=%lu",
		processId,
		remoteImage,
		mappedImageSize,
		tlsIndex == TLS_OUT_OF_INDEXES ? 0 : 1,
		functionCount);
	return TRUE;

cleanup:
	if (remoteInitializerCompleted && unwindMayBeRegistered && remoteRtlDeleteFunctionTable && remoteFunctionTable)
	{
		RunRemoteThread(process, remoteRtlDeleteFunctionTable, remoteFunctionTable);
	}
	if (!remoteInitializerStarted || remoteInitializerCompleted)
	{
		if (remoteStub)
		{
			VirtualFreeEx(process, remoteStub, 0, MEM_RELEASE);
		}
		if (remoteContext)
		{
			VirtualFreeEx(process, remoteContext, 0, MEM_RELEASE);
		}
		if (remoteTlsData)
		{
			VirtualFreeEx(process, remoteTlsData, 0, MEM_RELEASE);
		}
		if (remoteTlsVector)
		{
			VirtualFreeEx(process, remoteTlsVector, 0, MEM_RELEASE);
		}
		if (remoteImage)
		{
			VirtualFreeEx(process, remoteImage, 0, MEM_RELEASE);
		}
	}
	if (localImage)
	{
		SecureZeroMemory(localImage, fileNt->OptionalHeader.SizeOfImage);
		HeapFree(GetProcessHeap(), 0, localImage);
	}
	SetManualMapError(failureMessage, failureError);
	return FALSE;
}
