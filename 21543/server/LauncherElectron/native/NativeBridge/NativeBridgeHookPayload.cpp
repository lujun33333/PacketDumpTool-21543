#include "NativeBridgeHookPayload.h"

#include "NativeBridgeLog.h"
#include "NativeBridgeState.h"

#include <cstdint>
#include <cstring>

namespace
{
constexpr DWORD kMinimumHookPayloadSize = 0x400;
constexpr DWORD kMaximumHookPayloadSize = 128 * 1024 * 1024;
SRWLOCK gHookPayloadLock = SRWLOCK_INIT;
BYTE* gHookPayloadBytes = nullptr;
DWORD gHookPayloadSize = 0;

bool IsRangeInside(std::size_t total, std::size_t offset, std::size_t size)
{
	return offset <= total && size <= total - offset;
}

bool ValidateHookPe(const BYTE* bytes, DWORD size)
{
	if (!bytes || size < kMinimumHookPayloadSize || size > kMaximumHookPayloadSize)
	{
		return false;
	}

	__try
	{
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 ||
			!IsRangeInside(size, static_cast<std::size_t>(dos->e_lfanew), sizeof(IMAGE_NT_HEADERS64)))
		{
			return false;
		}

		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(bytes + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE ||
			nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
			(nt->FileHeader.Characteristics & IMAGE_FILE_DLL) == 0 ||
			nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
			nt->OptionalHeader.NumberOfRvaAndSizes < IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
			nt->OptionalHeader.SizeOfHeaders > size ||
			nt->OptionalHeader.SizeOfImage < nt->OptionalHeader.SizeOfHeaders ||
			nt->OptionalHeader.SizeOfImage > 128 * 1024 * 1024 ||
			nt->OptionalHeader.AddressOfEntryPoint >= nt->OptionalHeader.SizeOfImage ||
			nt->FileHeader.NumberOfSections == 0 || nt->FileHeader.NumberOfSections > 96)
		{
			return false;
		}

		const std::size_t sectionOffset = static_cast<std::size_t>(dos->e_lfanew) +
			FIELD_OFFSET(IMAGE_NT_HEADERS64, OptionalHeader) + nt->FileHeader.SizeOfOptionalHeader;
		const std::size_t sectionBytes =
			static_cast<std::size_t>(nt->FileHeader.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER);
		if (!IsRangeInside(size, sectionOffset, sectionBytes))
		{
			return false;
		}

		const auto* sections = reinterpret_cast<const IMAGE_SECTION_HEADER*>(bytes + sectionOffset);
		bool entryPointBacked = false;
		for (WORD index = 0; index < nt->FileHeader.NumberOfSections; ++index)
		{
			const auto& section = sections[index];
			const DWORD mappedSize = section.Misc.VirtualSize > section.SizeOfRawData
				? section.Misc.VirtualSize
				: section.SizeOfRawData;
			if (mappedSize > nt->OptionalHeader.SizeOfImage ||
				section.VirtualAddress > nt->OptionalHeader.SizeOfImage - mappedSize ||
				(section.SizeOfRawData != 0 && !IsRangeInside(size, section.PointerToRawData, section.SizeOfRawData)))
			{
				return false;
			}
			if (nt->OptionalHeader.AddressOfEntryPoint >= section.VirtualAddress &&
				nt->OptionalHeader.AddressOfEntryPoint < section.VirtualAddress + mappedSize &&
				section.SizeOfRawData != 0)
			{
				entryPointBacked = true;
			}
		}

		const auto& exceptionDirectory =
			nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
		const auto& relocationDirectory =
			nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
		const auto& tlsDirectory =
			nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
		const auto& delayDirectory =
			nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
		return entryPointBacked &&
			exceptionDirectory.VirtualAddress != 0 &&
			exceptionDirectory.Size >= sizeof(RUNTIME_FUNCTION) &&
			exceptionDirectory.Size % sizeof(RUNTIME_FUNCTION) == 0 &&
			relocationDirectory.VirtualAddress != 0 &&
			relocationDirectory.Size >= sizeof(IMAGE_BASE_RELOCATION) &&
			tlsDirectory.VirtualAddress == 0 && tlsDirectory.Size == 0 &&
			delayDirectory.VirtualAddress == 0 && delayDirectory.Size == 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

void ClearHookPayloadLocked()
{
	if (gHookPayloadBytes)
	{
		SecureZeroMemory(gHookPayloadBytes, gHookPayloadSize);
		HeapFree(GetProcessHeap(), 0, gHookPayloadBytes);
		gHookPayloadBytes = nullptr;
	}
	gHookPayloadSize = 0;
}
}

extern "C" __declspec(dllexport) int __stdcall QQSpeedNativeBridge_SetHookPayload(
	const void* bytes,
	std::uint32_t size)
{
	if (!ValidateHookPe(static_cast<const BYTE*>(bytes), size))
	{
		SetQQSpeedNativeBridgeLastError(L"The in-memory Hook payload is not a supported x64 PE image.");
		return 1;
	}

	BYTE* copy = static_cast<BYTE*>(HeapAlloc(GetProcessHeap(), 0, size));
	if (!copy)
	{
		SetQQSpeedNativeBridgeLastError(L"Memory allocation failed while copying the Hook payload.");
		return 2;
	}
	__try
	{
		memcpy(copy, bytes, size);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		SecureZeroMemory(copy, size);
		HeapFree(GetProcessHeap(), 0, copy);
		SetQQSpeedNativeBridgeLastError(L"The in-memory Hook payload could not be read safely.");
		return 1;
	}

	AcquireSRWLockExclusive(&gHookPayloadLock);
	ClearHookPayloadLocked();
	gHookPayloadBytes = copy;
	gHookPayloadSize = size;
	ReleaseSRWLockExclusive(&gHookPayloadLock);
	LauncherLog("In-memory Hook payload accepted size=%lu", size);
	return 0;
}

bool GetHookPayloadSnapshot(HookPayloadSnapshot* snapshot)
{
	if (!snapshot)
	{
		return false;
	}
	AcquireSRWLockShared(&gHookPayloadLock);
	if (!gHookPayloadBytes || !gHookPayloadSize)
	{
		ReleaseSRWLockShared(&gHookPayloadLock);
		snapshot->Bytes = nullptr;
		snapshot->Size = 0;
		return false;
	}
	snapshot->Bytes = gHookPayloadBytes;
	snapshot->Size = gHookPayloadSize;
	return true;
}

void ReleaseHookPayloadSnapshot()
{
	ReleaseSRWLockShared(&gHookPayloadLock);
}

bool HasHookPayload()
{
	AcquireSRWLockShared(&gHookPayloadLock);
	const bool available = gHookPayloadBytes != nullptr && gHookPayloadSize != 0;
	ReleaseSRWLockShared(&gHookPayloadLock);
	return available;
}

void ClearHookPayload()
{
	AcquireSRWLockExclusive(&gHookPayloadLock);
	ClearHookPayloadLocked();
	ReleaseSRWLockExclusive(&gHookPayloadLock);
}
