#pragma once

#include <Windows.h>

namespace qqspeed::kart_refit
{
bool InstallNetworkMergeHook(HMODULE networkModule);
bool InstallTopKartConsumerHooks(HMODULE topKartModule);
}
