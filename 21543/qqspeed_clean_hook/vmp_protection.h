#pragma once

#if defined(QQSPEED_VMP_PROTECTION) && defined(_MSC_VER)
#define QQSPEED_VMP_NOINLINE __declspec(noinline)
#else
#define QQSPEED_VMP_NOINLINE
#endif

#if defined(QQSPEED_VMP_PROTECTION)
#include "third_party/vmprotect/VMProtectSDK.h"
#define QQSPEED_VMP_SERVER_LOCK_ENABLED 1
#define QQSPEED_VMP_BEGIN_ULTRA(name) VMProtectBeginUltra(name)
#define QQSPEED_VMP_BEGIN_MUTATION(name) VMProtectBeginMutation(name)
#define QQSPEED_VMP_END() VMProtectEnd()
#define QQSPEED_VMP_DECRYPT_STRING_A(value) VMProtectDecryptStringA(value)
#define QQSPEED_VMP_FREE_STRING(value) VMProtectFreeString(value)
#else
#define QQSPEED_VMP_SERVER_LOCK_ENABLED 0
#define QQSPEED_VMP_BEGIN_ULTRA(name) ((void)0)
#define QQSPEED_VMP_BEGIN_MUTATION(name) ((void)0)
#define QQSPEED_VMP_END() ((void)0)
#define QQSPEED_VMP_DECRYPT_STRING_A(value) (value)
#define QQSPEED_VMP_FREE_STRING(value) ((void)(value))
#endif
