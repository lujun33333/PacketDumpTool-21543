#pragma once

#include <windows.h>

BOOL WriteSpeedHackAuthToken(const char* label);
void CloseSpeedHackAuthTokenMapping();

void CloseSpeedHookConfigMapping();
HANDLE CreateSpeedHookConfigMapping(DWORD gameProcessId);
HANDLE GetSpeedHookConfigMappingHandle();

BOOL CreateLoaderAuxiliaryMappings();
void CloseLoaderAuxiliaryMappings();
