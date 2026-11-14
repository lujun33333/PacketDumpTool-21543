#pragma once

#include <windows.h>

void StartOp4Collector(HMODULE ownerModule);
void StopOp4Collector();
void NotifyOp4CollectorResponse(unsigned int msgId, const void* body, int bodyLen);
