#pragma once

#include <windows.h>

BOOL ConfigureSpeedHookTarget(const char* host, USHORT port);
void ClearSpeedHookTarget();

BOOL WriteSpeedHackAuthToken(const char* label);
void CloseSpeedHackAuthTokenMapping();

void CloseSpeedHookConfigMapping();
HANDLE CreateSpeedHookConfigMapping(DWORD gameProcessId);
HANDLE GetSpeedHookConfigMappingHandle();

BOOL CreateLoaderAuxiliaryMappings();
void CloseLoaderAuxiliaryMappings();
