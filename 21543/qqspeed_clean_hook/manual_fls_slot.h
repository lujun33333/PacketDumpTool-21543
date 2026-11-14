#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#include <new>

namespace qqspeed::runtime
{
// Loader-independent per-thread storage. Unlike compiler-provided static TLS,
// this does not create a PE TLS directory. Each value is destroyed by Windows
// when its owning thread exits.
template <typename T>
class ManualFlsSlot final
{
public:
	ManualFlsSlot() noexcept = default;
	ManualFlsSlot(const ManualFlsSlot&) = delete;
	ManualFlsSlot& operator=(const ManualFlsSlot&) = delete;

	T* GetOrCreate() noexcept
	{
		if (!EnsureInitialized()) {
			return nullptr;
		}

		if (void* value = FlsGetValue(index_)) {
			return static_cast<T*>(value);
		}

		T* value = new (std::nothrow) T{};
		if (!value) {
			return nullptr;
		}
		if (!FlsSetValue(index_, value)) {
			delete value;
			return nullptr;
		}
		return value;
	}

	T* Get() noexcept
	{
		return EnsureInitialized() ? static_cast<T*>(FlsGetValue(index_)) : nullptr;
	}

	bool ResetCurrent() noexcept
	{
		if (!EnsureInitialized()) {
			return false;
		}
		T* value = static_cast<T*>(FlsGetValue(index_));
		if (!value) {
			return false;
		}
		if (!FlsSetValue(index_, nullptr)) {
			return false;
		}
		delete value;
		return true;
	}

	// Terminal operation: callers must first stop every thread that can access
	// this slot. The slot is intentionally not released from a global destructor
	// because CRT teardown can race target threads in a manually mapped DLL.
	void Release() noexcept
	{
		if (!EnsureInitialized()) {
			return;
		}
		ResetCurrent();
		FlsFree(index_);
		index_ = FLS_OUT_OF_INDEXES;
	}

private:
	static VOID CALLBACK DestroyValue(void* value) noexcept
	{
		delete static_cast<T*>(value);
	}

	static BOOL CALLBACK Initialize(PINIT_ONCE, void* parameter, void**) noexcept
	{
		auto* self = static_cast<ManualFlsSlot*>(parameter);
		self->index_ = FlsAlloc(&DestroyValue);
		return TRUE;
	}

	bool EnsureInitialized() noexcept
	{
		if (!InitOnceExecuteOnce(&once_, &Initialize, this, nullptr)) {
			return false;
		}
		return index_ != FLS_OUT_OF_INDEXES;
	}

	INIT_ONCE once_ = INIT_ONCE_STATIC_INIT;
	DWORD index_ = FLS_OUT_OF_INDEXES;
};
}
