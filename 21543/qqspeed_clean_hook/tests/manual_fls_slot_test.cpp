#include "manual_fls_slot.h"

#include <Windows.h>

#include <atomic>
#include <cstdio>

namespace
{
std::atomic<int> g_destroyed{ 0 };

struct ThreadState
{
	int value = 0;

	~ThreadState()
	{
		g_destroyed.fetch_add(1, std::memory_order_relaxed);
	}
};

qqspeed::runtime::ManualFlsSlot<ThreadState> g_slot;

struct ThreadArgs
{
	int expected = 0;
	HANDLE release = nullptr;
	std::atomic<int>* ready = nullptr;
	std::atomic<int>* failures = nullptr;
	ThreadState* observed = nullptr;
};

DWORD WINAPI RunThread(void* raw)
{
	auto* args = static_cast<ThreadArgs*>(raw);
	ThreadState* state = g_slot.GetOrCreate();
	if (!state) {
		args->failures->fetch_add(1, std::memory_order_relaxed);
		args->ready->fetch_add(1, std::memory_order_release);
		return 1;
	}

	args->observed = state;
	state->value = args->expected;
	args->ready->fetch_add(1, std::memory_order_release);
	WaitForSingleObject(args->release, INFINITE);

	if (g_slot.Get() != state || state->value != args->expected) {
		args->failures->fetch_add(1, std::memory_order_relaxed);
		return 2;
	}
	return 0;
}
}

int main()
{
	constexpr int kThreadCount = 8;
	std::atomic<int> ready{ 0 };
	std::atomic<int> failures{ 0 };
	HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	if (!release) {
		return 10;
	}

	ThreadState* mainState = g_slot.GetOrCreate();
	if (!mainState || g_slot.Get() != mainState) {
		CloseHandle(release);
		return 11;
	}
	mainState->value = 0x314159;

	ThreadArgs args[kThreadCount] = {};
	HANDLE threads[kThreadCount] = {};
	for (int i = 0; i < kThreadCount; ++i) {
		args[i].expected = 1000 + i;
		args[i].release = release;
		args[i].ready = &ready;
		args[i].failures = &failures;
		threads[i] = CreateThread(nullptr, 0, &RunThread, &args[i], 0, nullptr);
		if (!threads[i]) {
			failures.fetch_add(1, std::memory_order_relaxed);
			ready.fetch_add(1, std::memory_order_release);
		}
	}

	while (ready.load(std::memory_order_acquire) != kThreadCount) {
		Sleep(1);
	}
	if (mainState->value != 0x314159) {
		failures.fetch_add(1, std::memory_order_relaxed);
	}
	for (int i = 0; i < kThreadCount; ++i) {
		if (!args[i].observed || args[i].observed == mainState) {
			failures.fetch_add(1, std::memory_order_relaxed);
		}
		for (int j = 0; j < i; ++j) {
			if (args[i].observed == args[j].observed) {
				failures.fetch_add(1, std::memory_order_relaxed);
			}
		}
	}

	SetEvent(release);
	for (HANDLE thread : threads) {
		if (thread) {
			WaitForSingleObject(thread, INFINITE);
			CloseHandle(thread);
		}
	}
	CloseHandle(release);

	if (g_destroyed.load(std::memory_order_acquire) != kThreadCount) {
		failures.fetch_add(1, std::memory_order_relaxed);
	}
	if (!g_slot.ResetCurrent() || g_destroyed.load(std::memory_order_acquire) != kThreadCount + 1) {
		failures.fetch_add(1, std::memory_order_relaxed);
	}
	g_slot.Release();

	const int failed = failures.load(std::memory_order_relaxed);
	if (failed != 0) {
		std::fprintf(stderr, "manual FLS slot behavior failures=%d destroyed=%d\n", failed, g_destroyed.load());
		return 20;
	}
	std::puts("manual FLS slot behavior passed");
	return 0;
}
