#pragma once

namespace qqspeed::network
{
bool InstallConnectRedirectHook(HMODULE networkModule);
bool UninstallConnectRedirectHook();
}
