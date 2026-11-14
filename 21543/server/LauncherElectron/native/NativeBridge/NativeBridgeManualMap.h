#pragma once

#include <windows.h>

// Maps an authenticated x64 Hook PE into the supplied local-sandbox GameApp
// process. No Hook image path is created or passed to the target process.
BOOL ManualMapHookImage(HANDLE process, const BYTE* fileBytes, SIZE_T fileSize);

