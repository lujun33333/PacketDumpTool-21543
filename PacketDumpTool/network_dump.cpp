#include "pch.h"

#if MYHOOKCONNECT_STUB_DLL

#include "network_dump.h"
#ifdef PACKETDUMP_OP4_COLLECTOR
#include "op4_collector.h"
#endif

void StartNetworkDumpWorker(HMODULE)
{
}

void StopNetworkDumpWorker()
{
}

#else

#include "network_dump.h"
#ifdef PACKETDUMP_OP4_COLLECTOR
#include "op4_collector.h"
#endif

#include "MinHook.h"
#include "hook_log.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <cstring>
#include <cwchar>
#include <intrin.h>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <tlhelp32.h>
#include <vector>
#include <wincrypt.h>

#pragma comment(lib, "Advapi32.lib")

#ifdef _WIN64

namespace
{
struct TopKart551TraceLayout
{
	const char* Name;
	const char* Md5;
	size_t ImageSize;
	uintptr_t CompactDecode551Rva;
	uintptr_t Build551RuntimeCacheRva;
	uintptr_t NotifyKartPhysRva;
};

struct TopKartCallWindow
{
	const char* Name;
	uintptr_t CenterRva;
	size_t BytesBefore;
	size_t BytesAfter;
};

struct TopKartPreCallTraceTarget
{
	const char* Name;
	uintptr_t CallRva;
	uintptr_t TargetRva;
};

struct TopKartPreCallContext
{
	uintptr_t Rax;
	uintptr_t Rcx;
	uintptr_t Rdx;
	uintptr_t R8;
	uintptr_t R9;
	uintptr_t R10;
	uintptr_t R11;
};

constexpr DWORD kNetworkWaitMs = 30000;
constexpr DWORD kNetworkPollMs = 100;
constexpr DWORD kTopKartWaitMs = 60000;
constexpr DWORD kTopKartPollMs = 100;
constexpr DWORD kParserMonitorMs = 120000;
constexpr DWORD kParserMonitorPollMs = 50;
constexpr size_t kClientMsgHeadSize = 14;
constexpr size_t kMaxHexDumpBytes = 0x400;
constexpr size_t kMaxBinaryDumpBytes = 0x100000;
constexpr size_t kMaxFramedPacketBytes = 0x400000;
constexpr size_t kSafe28346ModeOffset = 0x7DD4;
constexpr size_t kSafe28346PayloadSize = kSafe28346ModeOffset + 1;
constexpr size_t kSafe28346InnerBodyOffset = 0x0C;
constexpr bool kEnablePacketBodyDumps = true;
constexpr bool kWriteBinaryDumps = true;
constexpr bool kWriteBinaryDumpsForAllMessages = true;
constexpr bool kEnableOfficialOneStopDump = true;
constexpr bool kWriteOfficialOneStopBinaryDumps = false;
constexpr bool kDumpOfficialOneStopSafeBuilderObjects = true;
constexpr bool kForceAllowDirectMsg100ForLoginDebug = false;
constexpr bool kForceAllowInnerDispatchLoginDebug = false;
constexpr int kMaxForcedInnerDispatchCopy = 0x400000;
constexpr unsigned short kSerializeBodyDefaultLen = 0x7E90;
constexpr long kTopKart551TraceFirstDetailed = 128;
constexpr long kTopKart551TraceSparseEvery = 1024;
constexpr size_t kTopKart551MaxSnapshotBytes = 0x20000;
constexpr size_t kTopKart551DefaultPointerDumpBytes = 0x400;
constexpr size_t kTopKart551StackDumpBytes = 0x240;
constexpr size_t kTopKart551ObjectDumpBytes = 0x800;
constexpr int kTopKart551MaxStackPointerSnapshots = 16;
constexpr long kTopKart100TraceFirstDetailed = 8;
constexpr size_t kTopKart100BufferHeadDumpBytes = 0x4000;
constexpr size_t kTopKart100ParserObjectDumpBytes = 0x800;
constexpr size_t kTopKart100HandlerNodeDumpBytes = 0x400;
constexpr size_t kTopKart100KeyDumpBytes = 0x100;
constexpr size_t kTopKart100StackDumpBytes = 0x300;
constexpr size_t kTopKart100StackPointerDumpBytes = 0x1000;
constexpr int kTopKart100MaxStackPointerSnapshots = 32;
constexpr long kTopKart100LoginTraceFirstDetailed = 8;
constexpr long kTopKart100ModelPreCallFirstDetailed = 256;
constexpr size_t kTopKart100LoginPlainItemNumOffset = 0x156;
constexpr size_t kTopKart100LoginPlainItemArrayOffset = 0x158;
constexpr size_t kTopKart100LoginPlainItemStride = 23;
constexpr size_t kTopKart100LoginPlainItemStatusOffset = 12;
constexpr size_t kTopKart100LoginPlainExtraCountOffset = 313902;
constexpr size_t kTopKart100LoginPlainExtraArrayOffset = 313903;
constexpr size_t kTopKart100LoginPlainExtraStride = 12;
constexpr unsigned int kDebugCurrentKartId = 12720;
constexpr unsigned int kDebugCurrentSkinId = 163036;

constexpr char kTopKart21487Md5[] = "d443f8ad60242f980244d6301e17fe11";
constexpr size_t kTopKart21487ImageSize = 0x3113000;
constexpr uintptr_t kTopKart21487AntiCheckerExecuteRva = 0x1870E0;
constexpr uintptr_t kTopKart21487AntiCheckerOriginalTargetRva = 0x2D3AF5A;
constexpr unsigned char kTopKart21487AntiCheckerEntryBytes[] = {
	0xE9, 0x75, 0x3E, 0xBB, 0x02,
};

constexpr uintptr_t kTopKart21273ProcessLoginRva = 0x7EA210;
constexpr uintptr_t kTopKart21273ProcessItemInfoOnLoginRva = 0x7E6850;
constexpr uintptr_t kTopKart21273ModelExistsRva = 0x1CB760;
constexpr uintptr_t kTopKart21273CheckActiveKartRva = 0x938FA0;
constexpr uintptr_t kTopKart21273CheckKartSkinRva = 0x939280;
constexpr uintptr_t kTopKart21273SetActiveKartCoreRva = 0x93BDF0;
constexpr uintptr_t kTopKart21273LoadKartAmRva = 0x93C3A0;
constexpr uintptr_t kTopKart21273AsyncChangeKartModelRva = 0x8EB750;
constexpr uintptr_t kTopKart21273OriginMapRva = 0xD37080;

constexpr char kKnownMd5[] = "3805d35c45d14bb70fef5abb0722aa0c";
constexpr size_t kKnownImageSize = 0x758000;
constexpr char kKnown21446Md5[] = "fe179ea08c8c9d1d543e4775b0a3c1a5";
constexpr size_t kKnown21446ImageSize = 0x77F000;
constexpr char kKnown8fdMd5[] = "8fd89fce68cea837d7047f373f355857";
constexpr size_t kKnown8fdImageSize = 0x784000;
constexpr char kKnownCe97Md5[] = "ce97d03ca74b530bbbdafb069ca2b3b8";
constexpr size_t kKnownCe97ImageSize = 0x833000;
constexpr char kKnown1642Md5[] = "164213ab8c4b6def1bac8c441d2b1b50";
constexpr size_t kKnown1642ImageSize = 0x840000;
constexpr char kKnown0679Md5[] = "0679c6a324679bbe967e7e8771cf2dc3";
constexpr size_t kKnown0679ImageSize = 0x846000;

constexpr TopKart551TraceLayout kTopKart551TraceLayouts[] = {
	{
		"old_551",
		"16183e5259bf8319a5bb03fa236590b8",
		0,
		0x81CAD0,
		0x82C7E0,
		0x7D7020,
	},
	{
		"21273",
		"d292f77ab3f418a080338854a701fb8e",
		0x2AF7000,
		0x81E160,
		0x82DE70,
		0x112BCB0,
	},
};

struct TopKart551ObservationLayout
{
	const char* Name;
	const char* Md5;
	size_t ImageSize;
	uintptr_t CompactDecode551Rva;
	uintptr_t CompactDecode551ReferenceRva;
	uintptr_t Build551RuntimeCacheRva;
	uintptr_t Build551RuntimeCacheCallRva;
	uintptr_t PostDecodeConsumerRva;
	uintptr_t PostDecodeConsumerReferenceRva;
};

constexpr TopKart551ObservationLayout kTopKart21681ObservationLayout = {
	"21681_observe",
	"23263a5accab594ad4d8467f44ac38f1",
	0x3149000,
	0x89C1D0,
	0x2132E30,
	0x1201090,
	0x1206B4B,
	0x84A5F0,
	0x212B408,
};

// Exact 23263a5a image prefixes; each must remain unique in .text.
constexpr int kTopKart21681CompactDecodeSignature[] = {
	0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C,
	0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
	0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x45, 0x33,
	0xE4, 0x48, 0x8D, 0x59, 0x0C, 0x44, 0x89, 0x23,
};

constexpr int kTopKart21681RuntimeBuildSignature[] = {
	0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44,
	0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x55,
	0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
	0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48, 0x83,
	0xEC, 0x78, 0x49, 0x8B, 0xC1, 0x4D, 0x8B, 0xF8,
};

constexpr int kTopKart21681PostDecodeConsumerSignature[] = {
	0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48,
	0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
	0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
	0x48, 0x8D, 0xA8, 0xD8, 0xFD, 0xFF, 0xFF, 0x48,
	0x81, 0xEC, 0x00, 0x03, 0x00, 0x00, 0x0F, 0x29,
	0x70, 0xC8, 0x48, 0x8B, 0x05, 0x5F, 0xF7, 0xDB,
	0x01, 0x48, 0x33, 0xC4,
};

constexpr unsigned char kTopKart21681RuntimeBuildCallBytes[] = {
	0xE8, 0x40, 0xA5, 0xFF, 0xFF,
};

constexpr TopKartCallWindow kTopKart21681ObservedWindows[] = {
	{ "21681_compact_decode", 0x89C1D0, 0x80, 0xE00 },
	{ "21681_runtime_build", 0x1201090, 0x80, 0x900 },
	{ "21681_runtime_build_call", 0x1206B4B, 0x180, 0x280 },
	{ "21681_post_decode_consumer", 0x84A5F0, 0x80, 0x1400 },
};

constexpr TopKartCallWindow kTopKart21273ObservedWindows[] = {
	{ "stack_return_7d5020", 0x7D5020, 0x180, 0x580 },
	{ "stack_return_7dc78f", 0x7DC78F, 0x300, 0x700 },
	{ "stack_return_7dde9e", 0x7DDE9E, 0x300, 0x700 },
	{ "build_func_82de70", 0x82DE70, 0x80, 0x700 },
	{ "build_callsite_82e466", 0x82E466, 0x380, 0x500 },
	{ "notify_candidate_112d610", 0x112D610, 0x240, 0x900 },
	{ "before_build_16d1850", 0x16D1850, 0x200, 0x500 },
	{ "after_build_16d1890", 0x16D1890, 0x200, 0x500 },
};

constexpr TopKartPreCallTraceTarget kTopKart21273PreCallTargets[] = {
	{ "pre_16d1850_before_build", 0x82E461, 0x16D1850 },
	{ "pre_16d1890_after_build", 0x82E470, 0x16D1890 },
	{ "pre_112d610_notify_candidate", 0x82E484, 0x112D610 },
	{ "pre_82e520_build_inner", 0x82DEBE, 0x82E520 },
	{ "pre_82c590_build_inner_a", 0x82DEE6, 0x82C590 },
	{ "pre_82c590_build_inner_b", 0x82DF7B, 0x82C590 },
	{ "pre_82c590_build_inner_c", 0x82E056, 0x82C590 },
};

constexpr TopKartPreCallTraceTarget kTopKart21273ModelPreCallTargets[] = {
	{ "model_check_active_kart", 0x93C049, kTopKart21273CheckActiveKartRva },
	{ "model_check_kart_skin", 0x93C145, kTopKart21273CheckKartSkinRva },
	{ "model_set_active_core_from_async", 0x8EA613, kTopKart21273SetActiveKartCoreRva },
	{ "model_set_active_core_main", 0x93DBEA, kTopKart21273SetActiveKartCoreRva },
	{ "model_load_kart_am_from_async", 0x8EB19A, kTopKart21273LoadKartAmRva },
	{ "model_load_kart_am_main", 0x93DBF6, kTopKart21273LoadKartAmRva },
	{ "model_async_change_kart_model", 0x93F70C, kTopKart21273AsyncChangeKartModelRva },
	{ "model_exists_check_active", 0x939044, kTopKart21273ModelExistsRva },
	{ "model_exists_skin_a", 0x9393F1, kTopKart21273ModelExistsRva },
	{ "model_exists_skin_b", 0x939422, kTopKart21273ModelExistsRva },
	{ "model_exists_skin_c", 0x939490, kTopKart21273ModelExistsRva },
	{ "origin_map_skin_a", 0x9393B7, kTopKart21273OriginMapRva },
	{ "origin_map_skin_b", 0x9393D5, kTopKart21273OriginMapRva },
	{ "origin_map_set_active", 0x93BEFF, kTopKart21273OriginMapRva },
};

#if 0
// Previous Network.dll IDA build:
// MD5 bd24352812012d52249365d5fce5d64e
// SizeOfImage 0x751000
// LoginCacheDecode call RVAs: 0x3E7061, 0x3E7290
//
// MD5 7707ae852fb354782294d3c09b8d5d33
// SizeOfImage 0x752000
// LoginCacheDecode call RVAs: 0x3E6F02, 0x3E6F9A
#endif
constexpr uintptr_t kUnpackRva = 0x2F66F0;
constexpr uintptr_t kPackRva = 0x2F5750;
constexpr uintptr_t kSpecialDecodeRva = 0x346520;
constexpr uintptr_t kInnerDispatchRva = 0x1F88E0;
constexpr uintptr_t kSafePacketBuilderRva = 0x322CF0;
constexpr uintptr_t kSerializeBodyRva = 0x2EECA0;
constexpr uintptr_t kParserGlobalRva = 0x5BF978;
constexpr uintptr_t kParserRegisterMsgIdRva = 0x3BE550;
constexpr uintptr_t kParserLuaDispatchRva = 0x3BECA0;
constexpr uintptr_t kParserQueueDispatchRva = 0x3BF7D0;
constexpr uintptr_t kLuaNetworkMgrInitRva = 0x3BF120;
constexpr uintptr_t kRequestGameServerWithBufRva = 0x3C0450;
constexpr uintptr_t kParserQueuedFlushRva = 0x3C0840;
constexpr uintptr_t kParserFastPathEnableRva = 0x3D9EE0;
constexpr uintptr_t kParserFastPathDisableRva = 0x3DC1E0;
constexpr uintptr_t kLuaNetworkMgrInitWrapperRva = 0x3E1910;
constexpr uintptr_t kLoginCacheDecodeRva = 0x3AFE80;
constexpr uintptr_t kLoginCacheEncodeRva = 0x3B0180;
constexpr size_t kLoginCachePlainLen = 0x4D791;
constexpr uintptr_t kInnerDispatchSmallMsgJumpTableRva = 0x2EBCFC;
constexpr uintptr_t kInnerDispatchDefaultReturn0Rva = 0x2EBC21;
constexpr uintptr_t kInnerDispatchReturn1Rva = 0x2EBCC3;
constexpr uintptr_t kInnerDispatchSwitchEntryRva = 0x1F89DE;
constexpr uintptr_t kGameSocketOnMyReceiveUnpackCallRva = 0x3B785A;
constexpr uintptr_t kUnpackCallRvas[] = {
	0x3B785A,
	0x3B85C4,
	0x3C3D11,
	0x3F3525,
	0x3F5BB8,
};
constexpr uintptr_t kPackCallRvas[] = {
	0x1EECD2,
	0x3B420E,
	0x3B4484,
	0x3B8D69,
	0x3B8F69,
	0x3B948D,
	0x3F379E,
	0x3F5A05,
};
constexpr uintptr_t kSpecialDecodeCallRvas[] = {
	0x2F6DAE,
};
constexpr uintptr_t kInnerDispatchCallRvas[] = {
	0x2F6D3A, // direct CEncryptUtil::Unpack after MSGHEAD decode
	0x3466BB, // mode0 special 28346 decoded inner dispatch
};
constexpr uintptr_t kSafePacketBuilderCallRvas[] = {
	0x2F58F2, // CEncryptUtil::Pack internal safe 28346 builder call
};
constexpr uintptr_t kSerializeBodyCallRvas[] = {
	0x2F5930, // CEncryptUtil::Pack internal body serializer call
};
constexpr uintptr_t kParserQueueDispatchCallRvas[] = {
	0x1F8982, // inner_dispatch early parser queue/Lua dispatch call
};
constexpr uintptr_t kLoginCacheDecodeCallRvas[] = {
	0x3E72F1, // 3805/0x758000 CNetworkImpl login init branch, cache decode before first use
	0x3E7520, // 3805/0x758000 CNetworkImpl login init branch, cache decode after state update
};
constexpr uintptr_t kLoginCacheDecodeCallRvasBd243[] = {
	0x3E7061, // bd243/0x751000 CNetworkImpl login init branch
	0x3E7290, // bd243/0x751000 CNetworkImpl login init branch
};
constexpr uintptr_t kLoginCacheDecodeCallRvas7707[] = {
	0x3E6F02, // 7707/0x752000 CNetworkImpl login init branch
	0x3E6F9A, // 7707/0x752000 CNetworkImpl login init branch
};

constexpr uintptr_t kUnpackRva21446 = 0x31DF40;
constexpr uintptr_t kPackRva21446 = 0x31C780;
constexpr uintptr_t kSpecialDecodeRva21446 = 0x36F300;
constexpr uintptr_t kInnerDispatchRva21446 = 0x21DA80;
constexpr uintptr_t kParserQueueDispatchRva21446 = 0x3EA700;
constexpr uintptr_t kSerializeBodyRva21446 = 0x3160A0;
constexpr uintptr_t kSafePacketBuilderRva21446 = 0x34AE80;
constexpr uintptr_t kUnpackCallRvas21446[] = {
	0x3E288D,
	0x3E3474,
	0x3EF351,
	0x41DD55,
	0x4202D8,
};
constexpr uintptr_t kPackCallRvas21446[] = {
	0x213C72,
	0x3DF23E,
	0x3DF49B,
	0x3E3C19,
	0x3E3E19,
	0x3E433D,
	0x41DFCE,
	0x420125,
};
constexpr uintptr_t kSpecialDecodeCallRvas21446[] = {
	0x31E349,
};
constexpr uintptr_t kInnerDispatchCallRvas21446[] = {
	0x31E2D5, // CEncryptUtil::Unpack direct inner dispatch
	0x36F49B, // special 28346 mode0 decoded inner dispatch
};
constexpr uintptr_t kSafePacketBuilderCallRvas21446[] = {
	0x31C9C4,
};
constexpr uintptr_t kSerializeBodyCallRvas21446[] = {
	0x31CA08,
};
constexpr uintptr_t kParserQueueDispatchCallRvas21446[] = {
	0x21DB22,
};

// Network.dll MD5 8fd89fce68cea837d7047f373f355857, SizeOfImage 0x784000.
// Verified from the official binary by resolving each E8 target in .text.
constexpr uintptr_t kUnpackRva8fd = 0x31E220;
constexpr uintptr_t kPackRva8fd = 0x31CA60;
constexpr uintptr_t kSpecialDecodeRva8fd = 0x36FAE0;
constexpr uintptr_t kInnerDispatchRva8fd = 0x21DD60;
constexpr uintptr_t kParserQueueDispatchRva8fd = 0x3EB610;
constexpr uintptr_t kSerializeBodyRva8fd = 0x316380;
constexpr uintptr_t kSafePacketBuilderRva8fd = 0x34B3F0;
constexpr uintptr_t kUnpackCallRvas8fd[] = {
	0x3E379D,
	0x3E4384,
	0x3F0261,
	0x41EC65,
	0x4211E8,
};
constexpr uintptr_t kPackCallRvas8fd[] = {
	0x213F52,
	0x3E014E,
	0x3E03AB,
	0x3E4B29,
	0x3E4D29,
	0x3E524D,
	0x41EEDE,
	0x421035,
};
constexpr uintptr_t kSpecialDecodeCallRvas8fd[] = {
	0x31E629,
};
constexpr uintptr_t kInnerDispatchCallRvas8fd[] = {
	0x31E5B5,
	0x36FC7B,
};
constexpr uintptr_t kSafePacketBuilderCallRvas8fd[] = {
	0x31CCA4,
};
constexpr uintptr_t kSerializeBodyCallRvas8fd[] = {
	0x31CCE8,
};
constexpr uintptr_t kParserQueueDispatchCallRvas8fd[] = {
	0x21DE02,
};

// Network.dll MD5 ce97d03ca74b530bbbdafb069ca2b3b8, SizeOfImage 0x833000.
// Resolved by normalized instruction matching against the 8fd build, then
// verified by decoding every E8 call target in the current image.
constexpr uintptr_t kUnpackRvaCe97 = 0x33E430;
constexpr uintptr_t kPackRvaCe97 = 0x33CFE0;
constexpr uintptr_t kSpecialDecodeRvaCe97 = 0x391B70;
constexpr uintptr_t kInnerDispatchRvaCe97 = 0x232F50;
constexpr uintptr_t kParserQueueDispatchRvaCe97 = 0x413B00;
constexpr uintptr_t kSerializeBodyRvaCe97 = 0x334010;
constexpr uintptr_t kSafePacketBuilderRvaCe97 = 0x36C650;
constexpr uintptr_t kUnpackCallRvasCe97[] = {
	0x33E3B2,
	0x33E41E,
};
constexpr uintptr_t kPackCallRvasCe97[] = {
	0x21DDBA,
	0x33CFB9,
};
constexpr uintptr_t kSpecialDecodeCallRvasCe97[] = {
	0x33EACC,
};
constexpr uintptr_t kInnerDispatchCallRvasCe97[] = {
	0x232EE8,
	0x232F39,
	0x334006,
};
constexpr uintptr_t kParserQueueDispatchCallRvasCe97[] = {
	0x233023,
};
constexpr uintptr_t kSerializeBodyCallRvasCe97[] = {
	0x33D1E0,
	0x425930,
};
constexpr uintptr_t kSafePacketBuilderCallRvasCe97[] = {
	0x33D1AB,
};

// Network.dll MD5 164213ab8c4b6def1bac8c441d2b1b50, SizeOfImage 0x840000.
// Matched against ce97 with normalized instruction similarity and verified by
// decoding every direct E8 call target in the updated image.
constexpr uintptr_t kUnpackRva1642 = 0x33E4E0;
constexpr uintptr_t kPackRva1642 = 0x33D090;
constexpr uintptr_t kSpecialDecodeRva1642 = 0x391C20;
constexpr uintptr_t kInnerDispatchRva1642 = 0x233000;
constexpr uintptr_t kParserQueueDispatchRva1642 = 0x41C290;
constexpr uintptr_t kSerializeBodyRva1642 = 0x3340C0;
constexpr uintptr_t kSafePacketBuilderRva1642 = 0x36C700;
constexpr uintptr_t kUnpackCallRvas1642[] = {
	0x33E462,
	0x33E4CE,
};
constexpr uintptr_t kPackCallRvas1642[] = {
	0x21DE6A,
	0x33D069,
};
constexpr uintptr_t kSpecialDecodeCallRvas1642[] = {
	0x33EB7C,
};
constexpr uintptr_t kInnerDispatchCallRvas1642[] = {
	0x232F98,
	0x232FE9,
	0x3340B6,
};
constexpr uintptr_t kParserQueueDispatchCallRvas1642[] = {
	0x2330D3,
};
constexpr uintptr_t kSerializeBodyCallRvas1642[] = {
	0x33D290,
	0x42D460,
};
constexpr uintptr_t kSafePacketBuilderCallRvas1642[] = {
	0x33D25B,
};

// Network.dll MD5 0679c6a324679bbe967e7e8771cf2dc3, SizeOfImage 0x846000.
// Migrated from 1642 by normalized function comparison, then verified by
// resolving every direct E8 call target in the current 2.0.203.20 image.
constexpr uintptr_t kUnpackRva0679 = 0x33E4E0;
constexpr uintptr_t kPackRva0679 = 0x33D090;
constexpr uintptr_t kSpecialDecodeRva0679 = 0x391C20;
constexpr uintptr_t kInnerDispatchRva0679 = 0x233000;
constexpr uintptr_t kParserQueueDispatchRva0679 = 0x41C320;
constexpr uintptr_t kSerializeBodyRva0679 = 0x3340C0;
constexpr uintptr_t kSafePacketBuilderRva0679 = 0x36C700;
constexpr uintptr_t kUnpackCallRvas0679[] = {
	0x33E462,
	0x33E4CE,
};
constexpr uintptr_t kPackCallRvas0679[] = {
	0x21DE6A,
	0x33D069,
};
constexpr uintptr_t kSpecialDecodeCallRvas0679[] = {
	0x33EB7C,
};
constexpr uintptr_t kInnerDispatchCallRvas0679[] = {
	0x232F98,
	0x232FE9,
	0x3340B6,
};
constexpr uintptr_t kParserQueueDispatchCallRvas0679[] = {
	0x2330D3,
};
constexpr uintptr_t kSerializeBodyCallRvas0679[] = {
	0x33D290,
	0x42DEC0,
};
constexpr uintptr_t kSafePacketBuilderCallRvas0679[] = {
	0x33D25B,
};

struct NetworkHookLayout
{
	uintptr_t unpackRva = kUnpackRva;
	uintptr_t packRva = kPackRva;
	uintptr_t specialDecodeRva = kSpecialDecodeRva;
	uintptr_t innerDispatchRva = kInnerDispatchRva;
	uintptr_t parserQueueDispatchRva = kParserQueueDispatchRva;
	uintptr_t serializeBodyRva = kSerializeBodyRva;
	uintptr_t safePacketBuilderRva = kSafePacketBuilderRva;
	uintptr_t loginCacheDecodeRva = kLoginCacheDecodeRva;
	std::vector<uintptr_t> unpackCallRvas;
	std::vector<uintptr_t> packCallRvas;
	std::vector<uintptr_t> specialDecodeCallRvas;
	std::vector<uintptr_t> innerDispatchCallRvas;
	std::vector<uintptr_t> parserQueueDispatchCallRvas;
	std::vector<uintptr_t> serializeBodyCallRvas;
	std::vector<uintptr_t> safePacketBuilderCallRvas;
	std::vector<uintptr_t> loginCacheDecodeCallRvas;
	bool resolvedFromRuntime = false;
	bool coreReady = false;
	bool minimalSafeCapture = false;
};

constexpr int kUnpackFunctionSignature[] = {
	0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
	0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C,
	0x24, 0xB8, 0x48, 0x81, 0xEC, 0x48, 0x01, 0x00,
	0x00, 0x48, 0x8B, 0x05, -1, -1, -1, -1,
	0x48, 0x33, 0xC4, 0x48, 0x89, 0x45, 0x38, 0x4C,
	0x89, 0x4D, 0x90, 0x4C, 0x89, 0x45, 0x88, 0x8B,
	0xFA, 0x89, 0x54, 0x24, 0x50, 0x48, 0x8B, 0xD9,
	0x48, 0x89, 0x4D, 0x80,
};

constexpr int kPackFunctionSignature[] = {
	0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
	0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC,
	0x24, 0xA8, 0x81, 0xFF, 0xFF, 0xB8, 0x58, 0x7F,
	0x00, 0x00, 0xE8, -1, -1, -1, -1, 0x48,
	0x2B, 0xE0, 0x48, 0x8B, 0x05, -1, -1, -1,
	-1, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x85, 0x40,
	0x7E, 0x00, 0x00, 0x45, 0x8B, 0xE1, 0x44, 0x89,
	0x44, 0x24, 0x64, 0x48, 0x89, 0x55, 0x80,
};

constexpr int kSpecialDecodeFunctionSignature[] = {
	0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41,
	0x55, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x40,
	0x03, 0xFF, 0xFF, 0xB8, 0xC0, 0xFD, 0x00, 0x00,
	0xE8, -1, -1, -1, -1, 0x48, 0x2B, 0xE0,
	0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48,
	0x33, 0xC4, 0x48, 0x89, 0x85, 0xB0, 0xFC, 0x00,
	0x00, 0x4D, 0x8B, 0xE9, 0x49, 0x8B, 0xF0,
};

constexpr int kInnerDispatchFunctionSignature[] = {
	0x4C, 0x89, 0x4C, 0x24, 0x20, 0x44, 0x89, 0x44,
	0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x89,
	0x4C, 0x24, 0x08, 0xB8, 0x38, 0xFE, 0x01, 0x00,
	0xE8, -1, -1, -1, -1, 0x48, 0x2B, 0xE0,
	0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48,
	0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x20, 0xFE,
	0x01, 0x00, 0xBA, 0x4B, 0x00, 0x00, 0x00,
};

constexpr int kSafePacketBuilderFunctionSignature[] = {
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C,
	0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
	0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56,
	0x41, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x4D, 0x8B,
	0xF1, 0x4D, 0x8B, 0xF8, 0x44, 0x8B, 0xE2, 0x0F,
	0xB7, 0xF1, 0x48, 0x8B, 0x2D, -1, -1, -1,
	-1, 0x48, 0x8B, 0x45, 0x08, 0x48, 0x8B, 0xCD,
};

constexpr int kSerializeBodyFunctionSignature[] = {
	0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC,
	0x88, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, -1,
	-1, -1, -1, 0x48, 0x33, 0xC4, 0x48, 0x89,
	0x44, 0x24, 0x70, 0x49, 0x8B, 0xD9, 0x49, 0x8B,
	0xF0, 0x48, 0x8B, 0xFA, 0x8B, 0xE9,
};

constexpr int kParserQueueDispatchFunctionSignature[] = {
	0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x41,
	0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x39,
	0x00, 0x49, 0x8B, 0xE8, 0x49, 0x63, 0xD9, 0x44,
	0x8B, 0xF2, 0x48, 0x8B, 0xF1,
};

constexpr int kLoginCacheDecodeFunctionSignature[] = {
	0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6,
	0x01, 0x4C, 0x8D, 0x8A, 0x90, 0xD7, 0x04, 0x00,
	0x30, 0x02, 0x48, 0x8B, 0xD9, 0x44, 0x0F, 0xB6,
	0x02, 0x41, 0xBA, 0x90, 0xD7, 0x04, 0x00,
};

volatile LONG g_stopWorker = 0;
volatile LONG g_installStarted = 0;
volatile LONG g_msg100ObservedLogged = 0;
volatile LONG g_dumpSequence = 0;
volatile LONG g_sharedDataDumped = 0;
volatile LONG g_parserIdsDumped = 0;
volatile LONG g_parserMonitorStarted = 0;
volatile LONG g_loginCacheDecodeDumpCount = 0;
volatile LONG g_msg123SourceDumpCount = 0;
volatile LONG g_officialOneStopHeaderWritten = 0;
uintptr_t g_dumpedPatchTargetAddresses[32] = {};
size_t g_dumpedPatchTargetAddressCount = 0;
uintptr_t g_dumpedPatchAllocationBases[32] = {};
size_t g_dumpedPatchAllocationBaseCount = 0;
SRWLOCK g_dumpLock = SRWLOCK_INIT;
std::ofstream g_networkTraceFile;
std::ofstream g_officialOneStopFile;
std::ofstream g_networkDumpFile;
std::ofstream g_framedPacketFile;
uintptr_t g_networkBase = 0;
uintptr_t g_topKartBase = 0;
NetworkHookLayout g_networkLayout = {};
bool g_captureMsg123Only = false;

#pragma pack(push, 1)
struct LauncherSharedData
{
	HWND hWnd;
	DWORD Uin;
	char PwdMD5[16];
	char Unknown1[24];
	DWORD WorldID;
	char UserDir[MAX_PATH];
	DWORD CommodityListCRC;
	char CommodityListURL[128];
	char Key[16];
	int SignKeyLen;
	char SignKey[128];
	int ClientKeyLen;
	char ClientKey[128];
	char QQNickNameLength;
	char QQNickName[37];
	char Unknown2[2974];
};
#pragma pack(pop)

static_assert(sizeof(LauncherSharedData) == 0xE9C, "Launcher shared data layout mismatch");

using NetworkUnpackFn = int(__fastcall*)(
	const unsigned char* packet,
	unsigned int packetLen,
	const void* key1,
	const void* key2,
	void* csHead,
	void* msgHead,
	unsigned char* outData,
	int* outLen,
	bool allowSpecialDecode,
	bool extendedMode,
	void* extendedResult);

using NetworkPackFn = int(__fastcall*)(
	unsigned char* outPacket,
	int* outPacketLen,
	unsigned int seq,
	unsigned int cmd,
	unsigned char* srcObject,
	int arg6,
	const void* key,
	void* extraData,
	unsigned char extraLen,
	unsigned short msgId,
	short msgType,
	char flag1,
	char flag2,
	short arg14,
	short arg15,
	const void* plainBody,
	int plainBodyLen,
	int arg18,
	char arg19);

using NetworkSpecialDecodeFn = int(__fastcall*)(
	void* msgHead,
	const void* csHead,
	unsigned char* outData,
	int* outLen,
	void* aux);

using NetworkInnerDispatchFn = int(__fastcall*)(
	short msgId,
	const unsigned char* body,
	int bodyLen,
	void* outData,
	void* outLen,
	void* aux,
	int arg7,
	bool extendedMode,
	int extendedFlags,
	void* extendedContext);

using NetworkSafePacketBuilderFn = int(__fastcall*)(
	short msgId,
	int seq,
	const void* srcObject,
	unsigned char* outSafePayload);

using NetworkSerializeBodyFn = int(__fastcall*)(
	unsigned int msgId,
	const void* srcObject,
	unsigned char* outBody,
	unsigned short* outBodyLen,
	void* aux);

using NetworkParserQueueDispatchFn = int(__fastcall*)(
	void* parser,
	unsigned int msgId,
	const void* body,
	int bodyLen);

using LoginCacheTransformFn = __int64(__fastcall*)(
	unsigned char* key,
	unsigned char* buffer);

using TopKartCompactDecode551Fn = unsigned char(__fastcall*)(
	void* object,
	unsigned __int64* base,
	unsigned __int64 end,
	unsigned int* offset);

using TopKartBuild551RuntimeCacheFn = __int64(__fastcall*)(
	__int64 arg1,
	__int64 arg2,
	__int64 arg3,
	__int64 arg4);

using TopKartNotifyKartPhysFn = __int64(__fastcall*)();

using TopKartProcessLoginFn = void(__fastcall*)(
	void* cache,
	int type,
	const unsigned char* loginPlain);

using TopKartProcessItemInfoOnLoginFn = void(__fastcall*)(
	void* cache,
	const unsigned char* loginPlain);

NetworkUnpackFn g_originalUnpack = nullptr;
NetworkPackFn g_originalPack = nullptr;
NetworkSpecialDecodeFn g_originalSpecialDecode = nullptr;
NetworkInnerDispatchFn g_originalInnerDispatch = nullptr;
NetworkSafePacketBuilderFn g_originalSafePacketBuilder = nullptr;
NetworkSerializeBodyFn g_originalSerializeBody = nullptr;
NetworkParserQueueDispatchFn g_originalParserQueueDispatch = nullptr;
LoginCacheTransformFn g_originalLoginCacheDecode = nullptr;
TopKartCompactDecode551Fn g_originalTopKartCompactDecode551 = nullptr;
TopKartBuild551RuntimeCacheFn g_originalTopKartBuild551RuntimeCache = nullptr;
TopKartNotifyKartPhysFn g_originalTopKartNotifyKartPhys = nullptr;
TopKartProcessLoginFn g_originalTopKartProcessLogin = nullptr;
TopKartProcessItemInfoOnLoginFn g_originalTopKartProcessItemInfoOnLogin = nullptr;

const char* DescribeUnpackMsgGate(unsigned short rawMsgId);
void DumpPacket(const char* direction, const void* data, int dataLen, unsigned short msgId, const char* detail);

struct CallSitePatch
{
	void* Address = nullptr;
	unsigned char Original[5] = {};
	bool Installed = false;
};

CallSitePatch g_callPatches[128] = {};
size_t g_callPatchCount = 0;
unsigned char* g_relayPage = nullptr;
size_t g_relayOffset = 0;
unsigned char* g_topKartRelayPage = nullptr;
size_t g_topKartRelayOffset = 0;
volatile LONG g_topKart551TraceInstallStarted = 0;
volatile LONG g_topKart551DecodeTraceCount = 0;
volatile LONG g_topKart551BuildTraceCount = 0;
volatile LONG g_topKart551NotifyTraceCount = 0;
volatile LONG g_topKartPreCallTraceCounts[_countof(kTopKart21273PreCallTargets)] = {};
volatile LONG g_topKart100ParserTraceCount = 0;
volatile LONG g_topKart100TraceInstallStarted = 0;
volatile LONG g_topKart100ProcessLoginTraceCount = 0;
volatile LONG g_topKart100ProcessItemInfoTraceCount = 0;
volatile LONG g_topKart100ModelPreCallTraceCounts[_countof(kTopKart21273ModelPreCallTargets)] = {};
volatile LONG g_topKart21681ObservationActive = 0;

inline void TraceTopKart21681NetworkMilestone(const char*, unsigned short, const char*, int, int) {}

enum class DumpProfile
{
	LegacyFull,
	FullAntiCheck,
	AntiCheckLight,
};

#if defined(PACKETDUMP_BUILD_FULL_ANTICHECK)
DumpProfile g_dumpProfile = DumpProfile::FullAntiCheck;
#elif defined(PACKETDUMP_BUILD_ANTICHECK_LIGHT)
DumpProfile g_dumpProfile = DumpProfile::AntiCheckLight;
#else
DumpProfile g_dumpProfile = DumpProfile::LegacyFull;
#endif

bool IsAntiCheckerTraceEnabled()
{
	return g_dumpProfile == DumpProfile::FullAntiCheck ||
		g_dumpProfile == DumpProfile::AntiCheckLight;
}

bool IsAntiCheckLightProfile()
{
	return g_dumpProfile == DumpProfile::AntiCheckLight;
}

const char* DumpProfileName()
{
	switch (g_dumpProfile) {
	case DumpProfile::FullAntiCheck:
		return "full_anticheck";
	case DumpProfile::AntiCheckLight:
		return "anticheck_probe";
	default:
		return "legacy_full";
	}
}

void ConfigureDumpProfile(HMODULE ownerModule)
{
#if defined(PACKETDUMP_BUILD_FULL_ANTICHECK)
	UNREFERENCED_PARAMETER(ownerModule);
	g_dumpProfile = DumpProfile::FullAntiCheck;
#elif defined(PACKETDUMP_BUILD_ANTICHECK_LIGHT)
	UNREFERENCED_PARAMETER(ownerModule);
	g_dumpProfile = DumpProfile::AntiCheckLight;
#else
	wchar_t path[MAX_PATH] = {};
	const DWORD len = ownerModule ? GetModuleFileNameW(ownerModule, path, MAX_PATH) : 0;
	const wchar_t* fileName = path;
	if (len > 0 && len < MAX_PATH) {
		const wchar_t* slash = wcsrchr(path, L'\\');
		if (slash) {
			fileName = slash + 1;
		}
	}

	if (fileName && _wcsicmp(fileName, L"PacketDumpHook_full_anticheck.dll") == 0) {
		g_dumpProfile = DumpProfile::FullAntiCheck;
	}
	else if (fileName && _wcsicmp(fileName, L"PacketDumpHook_anticheck_probe.dll") == 0) {
		g_dumpProfile = DumpProfile::AntiCheckLight;
	}
	else {
		g_dumpProfile = DumpProfile::LegacyFull;
	}
#endif
}

constexpr size_t kAntiCheckerTraceSampleCapacity = 256;
constexpr size_t kLightPacketSampleCapacity = 512;
constexpr size_t kLightPacketMaxPayload = 4096;

struct AntiCheckerTraceSample
{
	volatile LONG Ready = 0;
	LONG Sequence = 0;
	ULONGLONG TickMs = 0;
	LONGLONG Qpc = 0;
	DWORD ThreadId = 0;
	uintptr_t ThisPtr = 0;
	uintptr_t ReturnAddress = 0;
	uintptr_t EntryRsp = 0;
};

enum class LightPacketDirection : unsigned short
{
	InnerDispatch = 0,
	SerializeBody = 1,
};

struct LightPacketSample
{
	volatile LONG Ready = 0;
	LONG Sequence = 0;
	ULONGLONG TickMs = 0;
	DWORD ThreadId = 0;
	LightPacketDirection Direction = LightPacketDirection::InnerDispatch;
	unsigned short MsgId = 0;
	int DataLen = 0;
	unsigned short CopiedLen = 0;
	unsigned char Data[kLightPacketMaxPayload] = {};
};

AntiCheckerTraceSample g_antiCheckerTraceSamples[kAntiCheckerTraceSampleCapacity] = {};
LightPacketSample g_lightPacketSamples[kLightPacketSampleCapacity] = {};
volatile LONG g_antiCheckerTraceWriteCount = 0;
volatile LONG g_antiCheckerTraceOverflow = 0;
volatile LONG g_lightPacketWriteCount = 0;
volatile LONG g_lightPacketOverflow = 0;
volatile LONG g_topKartAntiCheckerTraceInstallStarted = 0;

struct DirectJumpPatch
{
	void* Address = nullptr;
	unsigned char Original[5] = {};
	unsigned char Patched[5] = {};
	bool Installed = false;
};

DirectJumpPatch g_topKartAntiCheckerPatch = {};
uintptr_t g_topKartAntiCheckerOriginalTarget = 0;

__declspec(thread) int g_packDepth = 0;
__declspec(thread) unsigned short g_packMsgId = 0;

bool SafeCopyRaw(void* dst, const void* src, size_t size)
{
	__try
	{
		memcpy(dst, src, size);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

bool IsLightProbeMsg(unsigned short msgId)
{
	return msgId == 538 || msgId == 15399;
}

void RecordLightPacket(
	LightPacketDirection direction,
	unsigned short msgId,
	const void* data,
	int dataLen)
{
	if (!IsAntiCheckLightProfile() || !IsLightProbeMsg(msgId) || !data || dataLen <= 0) {
		return;
	}

	const LONG sequence = InterlockedIncrement(&g_lightPacketWriteCount);
	if (sequence <= 0 || static_cast<size_t>(sequence) > kLightPacketSampleCapacity) {
		InterlockedExchange(&g_lightPacketOverflow, 1);
		return;
	}

	LightPacketSample& sample = g_lightPacketSamples[sequence - 1];
	const size_t copySize = (std::min<size_t>)(
		static_cast<size_t>(dataLen),
		kLightPacketMaxPayload);
	if (!SafeCopyRaw(sample.Data, data, copySize)) {
		InterlockedExchange(&g_lightPacketOverflow, 1);
		return;
	}

	sample.Sequence = sequence;
	sample.TickMs = GetTickCount64();
	sample.ThreadId = GetCurrentThreadId();
	sample.Direction = direction;
	sample.MsgId = msgId;
	sample.DataLen = dataLen;
	sample.CopiedLen = static_cast<unsigned short>(copySize);
	MemoryBarrier();
	InterlockedExchange(&sample.Ready, 1);
}

unsigned short ReadU16Safe(const void* ptr)
{
	unsigned short value = 0;
	if (ptr) {
		SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned char ReadU8Safe(const void* ptr)
{
	unsigned char value = 0;
	if (ptr) {
		SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

int ReadIntSafe(const int* ptr, int fallback)
{
	int value = fallback;
	if (ptr) {
		SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned int ReadU32Safe(const void* ptr)
{
	unsigned int value = 0;
	if (ptr) {
		SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

uintptr_t ReadPtrSafe(const void* ptr)
{
	uintptr_t value = 0;
	if (ptr) {
		SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned __int64 ReadU64Safe(const void* ptr)
{
	unsigned __int64 value = 0;
	if (ptr) {
		SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned short Swap16(unsigned short value)
{
	return static_cast<unsigned short>((value >> 8) | (value << 8));
}

struct ParserHandlerSnapshot
{
	uintptr_t runtime = 0;
	unsigned char fastPathFlag = 0;
	uintptr_t sentinel = 0;
	uintptr_t buckets = 0;
	uintptr_t count = 0;
	uintptr_t mask = 0;
	uintptr_t node = 0;
	unsigned int nodeMsg = 0;
	bool handlerFound = false;
};

ParserHandlerSnapshot ReadParserHandlerSnapshot(void* parser, unsigned int msgId)
{
	ParserHandlerSnapshot snapshot = {};
	if (!parser) {
		return snapshot;
	}

	auto base = reinterpret_cast<const unsigned char*>(parser);
	snapshot.runtime = ReadPtrSafe(base);
	snapshot.fastPathFlag = ReadU8Safe(base + 0x10);
	snapshot.sentinel = ReadPtrSafe(base + 0x48);
	snapshot.count = ReadPtrSafe(base + 0x50);
	snapshot.buckets = ReadPtrSafe(base + 0x58);
	snapshot.mask = ReadPtrSafe(base + 0x70);
	if (!snapshot.buckets || !snapshot.sentinel) {
		return snapshot;
	}

	unsigned int seed = (msgId ^ 0xDEADBEEFU) & 0x7FFFFFFFU;
	int value = 16807 * static_cast<int>(seed % 0x1F31DU) - 2836 * static_cast<int>(seed / 0x1F31DU);
	if (value < 0) {
		value += 0x7FFFFFFF;
	}
	uintptr_t bucket = snapshot.mask & static_cast<unsigned int>(value);
	uintptr_t bucketPair = snapshot.buckets + 16 * bucket;
	uintptr_t begin = ReadPtrSafe(reinterpret_cast<const void*>(bucketPair));
	uintptr_t node = ReadPtrSafe(reinterpret_cast<const void*>(bucketPair + 8));
	snapshot.node = node;

	if (!node || node == snapshot.sentinel) {
		return snapshot;
	}

	unsigned int guard = 0;
	unsigned int nodeMsg = ReadU32Safe(reinterpret_cast<const void*>(node + 0x10));
	while (msgId < nodeMsg && node != begin && guard++ < 256) {
		node = ReadPtrSafe(reinterpret_cast<const void*>(node + 8));
		if (!node || node == snapshot.sentinel) {
			snapshot.node = node;
			snapshot.nodeMsg = 0;
			return snapshot;
		}
		nodeMsg = ReadU32Safe(reinterpret_cast<const void*>(node + 0x10));
	}

	snapshot.node = node;
	snapshot.nodeMsg = nodeMsg;
	snapshot.handlerFound = node != snapshot.sentinel && nodeMsg == msgId;
	return snapshot;
}

bool IsLoginChainMsg(unsigned short msgId)
{
	switch (msgId) {
	case 1:
	case 90:
	case 100:
	case 223:
	case 28346:
		return true;
	default:
		return false;
	}
}

bool IsTopModelFlowMsg(unsigned short msgId)
{
	switch (msgId) {
	case 100:
	case 130:
	case 223:
	case 317:
	case 509:
	case 713:
	case 714:
	case 862:
	case 20129:
	case 24251:
	case 24313:
		return true;
	default:
		return false;
	}
}

bool IsOfficialOneStopMsg(unsigned short msgId)
{
	if (IsLoginChainMsg(msgId) || IsTopModelFlowMsg(msgId)) {
		return true;
	}

	// Official login-to-hall init has many non-24251 config/state messages.
	// Keep the dump scope broad enough to catch 271/317/714/1400 and 2xxxx config packets.
	if ((msgId >= 10500 && msgId <= 11599) ||
		(msgId >= 12000 && msgId <= 12099) ||
		(msgId >= 13700 && msgId <= 13800) ||
		(msgId >= 15400 && msgId <= 15499) ||
		(msgId >= 16000 && msgId <= 16199) ||
		(msgId >= 20000 && msgId <= 25299) ||
		(msgId >= 28200 && msgId <= 28499))
	{
		return true;
	}

	switch (msgId) {
	case 101:
	case 112:
	case 135:
	case 145:
	case 148:
	case 151:
	case 152:
	case 163:
	case 177:
	case 187:
	case 228:
	case 231:
	case 233:
	case 242:
	case 271:
	case 314:
	case 317:
	case 319:
	case 320:
	case 347:
	case 362:
	case 363:
	case 373:
	case 413:
	case 439:
	case 440:
	case 452:
	case 536:
	case 538:
	case 541:
	case 542:
	case 555:
	case 557:
	case 596:
	case 631:
	case 653:
	case 680:
	case 693:
	case 701:
	case 703:
	case 714:
	case 760:
	case 800:
	case 822:
	case 823:
	case 862:
	case 880:
	case 920:
	case 921:
	case 944:
	case 948:
	case 952:
	case 1400:
	case 1401:
	case 1616:
	case 2100:
	case 2101:
	case 15399:
		return true;
	default:
		return false;
	}
}

unsigned short NormalizeMsgId(unsigned short rawMsgId)
{
	if (IsOfficialOneStopMsg(rawMsgId)) {
		return rawMsgId;
	}

	unsigned short swapped = Swap16(rawMsgId);
	if (IsOfficialOneStopMsg(swapped)) {
		return swapped;
	}

	return rawMsgId;
}

std::string FindDebugModelItemHitsText(const void* data, int dataLen)
{
	if (!data || dataLen <= 0) {
		return "-";
	}

	constexpr unsigned int kDebugItemIds[] = {
		kDebugCurrentKartId, // current equipped kart: 12720
		kDebugCurrentSkinId, // current skin: 163036
		129099, // previous observed kart id, keep for old dumps
		127616, // previous observed skin id, keep for old dumps
		109563, // old/other observed kart id
	};
	constexpr size_t kMaxScanBytes = 0x40000;
	constexpr int kMaxTotalHits = 64;
	const size_t copySize = (std::min<size_t>)(static_cast<size_t>(dataLen), kMaxScanBytes);

	std::unique_ptr<unsigned char[]> copy(new (std::nothrow) unsigned char[copySize]);
	if (!copy) {
		return "<alloc_failed>";
	}
	if (!SafeCopyRaw(copy.get(), data, copySize)) {
		return "<unreadable>";
	}

	std::string result;
	int totalHits = 0;
	bool truncated = false;

	auto appendHit = [&](unsigned int id, const char* kind, size_t offset) {
		if (totalHits >= kMaxTotalHits) {
			truncated = true;
			return;
		}
		char text[96] = {};
		snprintf(text, sizeof(text), "%u_%s@0x%zX", id, kind, offset);
		if (!result.empty()) {
			result.push_back(';');
		}
		result += text;
		++totalHits;
	};

	auto scanPattern = [&](unsigned int id, const char* kind, const unsigned char* pattern, size_t patternLen) {
		if (!pattern || patternLen == 0 || copySize < patternLen) {
			return;
		}
		for (size_t offset = 0; offset + patternLen <= copySize; ++offset) {
			if (memcmp(copy.get() + offset, pattern, patternLen) == 0) {
				appendHit(id, kind, offset);
				if (totalHits >= kMaxTotalHits) {
					return;
				}
			}
		}
	};

	for (unsigned int id : kDebugItemIds) {
		unsigned char be32[4] = {
			static_cast<unsigned char>((id >> 24) & 0xFF),
			static_cast<unsigned char>((id >> 16) & 0xFF),
			static_cast<unsigned char>((id >> 8) & 0xFF),
			static_cast<unsigned char>(id & 0xFF),
		};
		unsigned char le32[4] = {
			static_cast<unsigned char>(id & 0xFF),
			static_cast<unsigned char>((id >> 8) & 0xFF),
			static_cast<unsigned char>((id >> 16) & 0xFF),
			static_cast<unsigned char>((id >> 24) & 0xFF),
		};
		unsigned short low16 = static_cast<unsigned short>(id & 0xFFFF);
		unsigned char be16[2] = {
			static_cast<unsigned char>((low16 >> 8) & 0xFF),
			static_cast<unsigned char>(low16 & 0xFF),
		};
		unsigned char le16[2] = {
			static_cast<unsigned char>(low16 & 0xFF),
			static_cast<unsigned char>((low16 >> 8) & 0xFF),
		};

		scanPattern(id, "be32", be32, sizeof(be32));
		scanPattern(id, "le32", le32, sizeof(le32));
		scanPattern(id, "be16low", be16, sizeof(be16));
		scanPattern(id, "le16low", le16, sizeof(le16));
	}

	if (result.empty()) {
		return "-";
	}
	if (truncated || static_cast<size_t>(dataLen) > copySize) {
		result += ";scan_truncated";
	}
	return result;
}

void ArmPostLoginDump(unsigned short msgId, const char* reason)
{
	if (msgId != 100 || InterlockedCompareExchange(&g_msg100ObservedLogged, 1, 0) != 0) {
		return;
	}

	HookLog("Network msg 100 observed at %s; packet body dump enabled", reason);
}

bool ShouldDumpPacket(unsigned short msgId)
{
	if (IsAntiCheckLightProfile()) {
		return IsLightProbeMsg(msgId);
	}
	return kEnablePacketBodyDumps;
}

bool IsNativePackDiagnosticMsg(unsigned short msgId)
{
	switch (msgId) {
	case 103:
	case 110:
	case 120:
		return true;
	default:
		return false;
	}
}

bool ShouldDumpSafeBuilderSourceMsg(unsigned short msgId)
{
	if (!IsAntiCheckLightProfile()) {
		return true;
	}

	if (kEnableOfficialOneStopDump &&
		kDumpOfficialOneStopSafeBuilderObjects &&
		IsOfficialOneStopMsg(msgId))
	{
		return true;
	}

	switch (msgId) {
	case 103:
	case 110:
	case 112:
	case 120:
	case 123:
	case 271:
	case 24251:
		return true;
	default:
		return false;
	}
}

bool ShouldDumpRawPacket(unsigned short msgId, unsigned short packetMsgId)
{
	if (msgId == 703 || packetMsgId == 703) {
		return true;
	}
	if (IsAntiCheckLightProfile()) {
		return IsLightProbeMsg(msgId) || IsLightProbeMsg(packetMsgId);
	}
	return kEnablePacketBodyDumps;
}

bool ShouldWriteBinaryDump(unsigned short msgId, const char* direction)
{
	if (msgId == 703) {
		return true;
	}
	if (IsAntiCheckLightProfile()) {
		UNREFERENCED_PARAMETER(direction);
		return IsLightProbeMsg(msgId);
	}
	if (!kWriteBinaryDumps) {
		return false;
	}
	if (kWriteBinaryDumpsForAllMessages || IsLoginChainMsg(msgId) || IsTopModelFlowMsg(msgId)) {
		return true;
	}
	if (kEnableOfficialOneStopDump &&
		kWriteOfficialOneStopBinaryDumps &&
		IsOfficialOneStopMsg(msgId))
	{
		return true;
	}
	return direction && (
		strstr(direction, "28346") != nullptr ||
		strstr(direction, "safe") != nullptr);
}

bool IsFullPacketCaptureDirection(const char* direction)
{
	return direction &&
		(strcmp(direction, "parser_queue_dispatch_body") == 0 ||
			strcmp(direction, "serialize_body_plain") == 0);
}

std::string SanitizeFileNamePart(const char* value)
{
	std::string result;
	if (!value) {
		return "unknown";
	}

	for (const char* p = value; *p; ++p) {
		char c = *p;
		if ((c >= 'a' && c <= 'z') ||
			(c >= 'A' && c <= 'Z') ||
			(c >= '0' && c <= '9') ||
			c == '_' || c == '-' || c == '.')
		{
			result.push_back(c);
		}
		else {
			result.push_back('_');
		}
	}
	if (result.empty()) {
		result = "unknown";
	}
	return result;
}

bool IsQuickRpcNameChar(unsigned char c)
{
	return (c >= 'a' && c <= 'z') ||
		(c >= 'A' && c <= 'Z') ||
		(c >= '0' && c <= '9') ||
		c == '_' || c == '.';
}

bool MatchesAsciiPrefix(const unsigned char* data, size_t len, size_t pos, const char* prefix)
{
	if (!data || !prefix) {
		return false;
	}
	size_t prefixLen = strlen(prefix);
	return prefixLen > 0 && pos + prefixLen <= len && memcmp(data + pos, prefix, prefixLen) == 0;
}

std::string ExtractQuickRpcNameFromBytes(const unsigned char* data, size_t len)
{
	if (!data || len == 0) {
		return std::string();
	}

	const char* prefixes[] = {
		"Co.",
		"DispatchCoResponse",
		"OnNotify",
		"OnNofity",
		"OnReq",
		"OnRes",
	};
	for (size_t i = 0; i < len; ++i) {
		for (size_t p = 0; p < _countof(prefixes); ++p) {
			if (!MatchesAsciiPrefix(data, len, i, prefixes[p])) {
				continue;
			}
			if (i > 0 && IsQuickRpcNameChar(data[i - 1])) {
				continue;
			}
			size_t end = i;
			while (end < len && IsQuickRpcNameChar(data[end]) && end - i < 96) {
				++end;
			}
			if (end > i) {
				return std::string(reinterpret_cast<const char*>(data + i), end - i);
			}
		}
	}
	return std::string();
}

std::string ExtractQuickRpcNameFromDetail(const char* detail)
{
	if (!detail) {
		return std::string();
	}
	const char* key = strstr(detail, "quickrpc=");
	if (!key) {
		return std::string();
	}
	key += strlen("quickrpc=");
	const char* end = key;
	while (*end && IsQuickRpcNameChar(static_cast<unsigned char>(*end)) && end - key < 96) {
		++end;
	}
	if (end <= key) {
		return std::string();
	}
	return std::string(key, end - key);
}

std::string BuildQuickRpcFileTag(unsigned short msgId, const unsigned char* data, size_t len, const char* detail)
{
	if (NormalizeMsgId(msgId) != 24251) {
		return std::string();
	}
	std::string name = ExtractQuickRpcNameFromBytes(data, len);
	if (name.empty()) {
		name = ExtractQuickRpcNameFromDetail(detail);
	}
	if (name.empty()) {
		return std::string();
	}
	if (name.compare(0, 3, "Co.") == 0) {
		name = "co." + name.substr(3);
	}
	else {
		name = "co." + name;
	}
	return "_" + SanitizeFileNamePart(name.c_str());
}

const char* ClassifyOfficialOneStopPhase(unsigned short msgId, const char* detail)
{
	if (msgId == 100 || msgId == 223 || msgId == 575) {
		return "login_cache";
	}
	if (msgId == 24251 || (detail && strstr(detail, "quickrpc="))) {
		return "quickrpc_lua";
	}
	if (msgId == 271 || msgId == 317 || msgId == 319 || msgId == 714 ||
		msgId == 862 || msgId == 13706 || msgId == 20100 || msgId == 28228)
	{
		return "model_config";
	}
	if (msgId == 1400 || msgId == 1401) {
		return "world_online";
	}
	if (msgId == 538 || msgId == 541 || msgId == 555) {
		return "heartbeat_runtime";
	}
	if (msgId >= 20000 || msgId >= 10500) {
		return "hall_ui_config";
	}
	return "login_hall_init";
}

bool ShouldTraceOfficialOneStopEvent(const char* eventName, unsigned short msgId, const char* detail)
{
	if (!kEnableOfficialOneStopDump) {
		return false;
	}
	if (IsOfficialOneStopMsg(msgId)) {
		return true;
	}
	if (detail && strstr(detail, "quickrpc=")) {
		return true;
	}
	if (eventName && strstr(eventName, "login_cache") != nullptr) {
		return true;
	}
	return false;
}

void WriteTsvField(std::ofstream& file, const char* value)
{
	if (!value) {
		return;
	}
	for (const char* p = value; *p; ++p) {
		char c = *p;
		if (c == '\t' || c == '\r' || c == '\n') {
			file << ' ';
		}
		else {
			file << c;
		}
	}
}

void TraceOfficialOneStopEventLocked(
	ULONGLONG tick,
	DWORD tid,
	const char* eventName,
	unsigned short msgId,
	int dataLen,
	const char* detail)
{
	if (!ShouldTraceOfficialOneStopEvent(eventName, msgId, detail)) {
		return;
	}

	std::string path = GetPacketDumpOutputDirectory() + "network_official_onestop_" + std::to_string(GetCurrentProcessId()) + ".tsv";
	if (!g_officialOneStopFile.is_open()) {
		g_officialOneStopFile.clear();
		g_officialOneStopFile.open(path, std::ios::app);
	}
	if (!g_officialOneStopFile) {
		return;
	}
	std::ofstream& file = g_officialOneStopFile;

	if (InterlockedCompareExchange(&g_officialOneStopHeaderWritten, 1, 0) == 0) {
		file << "tick\ttid\tevent\tphase\tmsg\tlen\tquickrpc\tdetail\n";
	}

	std::string quickRpc = ExtractQuickRpcNameFromDetail(detail);
	file << tick << '\t'
		<< tid << '\t';
	WriteTsvField(file, eventName);
	file << '\t'
		<< ClassifyOfficialOneStopPhase(msgId, detail) << '\t'
		<< msgId << '\t'
		<< dataLen << '\t';
	WriteTsvField(file, quickRpc.c_str());
	file << '\t';
	WriteTsvField(file, detail);
	file << '\n';
}

struct MsgHeadSnapshot
{
	unsigned short msgId = 0;
	short msgType = 0;
	int msgSeq = 0;
	unsigned char srcFe = 0;
	unsigned char dstFe = 0;
	short srcId = 0;
	short dstId = 0;
	bool valid = false;
};

MsgHeadSnapshot ReadMsgHeadSnapshot(const void* msgHead)
{
	MsgHeadSnapshot snapshot = {};
	if (msgHead) {
		snapshot.valid = SafeCopyRaw(&snapshot, msgHead, kClientMsgHeadSize);
	}
	return snapshot;
}

bool ShouldForceAllowDirectMsg100(
	uintptr_t callSiteRva,
	int originalResult,
	const MsgHeadSnapshot& head,
	unsigned short msgId,
	unsigned short packetMsgId,
	int actualOutLen,
	bool allowSpecialDecode,
	const unsigned char* outData,
	const int* outLen)
{
	if (!kForceAllowDirectMsg100ForLoginDebug) {
		return false;
	}
	if (originalResult != 0 || callSiteRva != kGameSocketOnMyReceiveUnpackCallRva) {
		return false;
	}
	if (!allowSpecialDecode || !head.valid || !outData || !outLen) {
		return false;
	}
	if (msgId != 100 && msgId != 223) {
		return false;
	}
	if (NormalizeMsgId(head.msgId) != msgId) {
		return false;
	}
	UNREFERENCED_PARAMETER(packetMsgId);
	if (actualOutLen <= static_cast<int>(kClientMsgHeadSize)) {
		return false;
	}
	return strcmp(DescribeUnpackMsgGate(msgId), "msg100_lookup_return0") == 0 ||
		strcmp(DescribeUnpackMsgGate(msgId), "lookup_return0") == 0;
}

uintptr_t AddressToRva(const void* address)
{
	uintptr_t value = reinterpret_cast<uintptr_t>(address);
	if (!g_networkBase || value < g_networkBase) {
		return 0;
	}
	return value - g_networkBase;
}

uintptr_t GetCallSiteRvaFromReturnAddress(const void* returnAddress)
{
	uintptr_t returnRva = AddressToRva(returnAddress);
	if (returnRva < 5) {
		return 0;
	}
	return returnRva - 5;
}

const char* DescribeUnpackCallSite(uintptr_t callRva)
{
	switch (callRva) {
	case 0x3B785A:
		return "GameSocket::OnMyReceive";
	case 0x3B85C4:
		return "GameSocket::ReceiveAlt";
	case 0x3C3D11:
		return "GameSocket::CloseQueueScan";
	case 0x3F3525:
		return "dispatch_path_3";
	case 0x3F5BB8:
		return "dispatch_path_5";
	default:
		return "unknown";
	}
}

const char* DescribeUnpackCallerAction(uintptr_t callRva, int result)
{
	if (result != 0) {
		return "continues";
	}

	switch (callRva) {
	case 0x3B785A:
		return "skip_login_game_dispatch";
	case 0x3B85C4:
		return "skip_receive_dispatch";
	case 0x3C3D11:
		return "skip_queue_item";
	case 0x3F3525:
	case 0x3F5BB8:
		return "skip_vtable_dispatch";
	default:
		return "skip_after_unpack";
	}
}

const char* DescribeUnpackMsgGate(unsigned short rawMsgId)
{
	if (rawMsgId == 0x0227 || rawMsgId == 0x52DA) {
		return "explicit_static_return0";
	}
	if (rawMsgId == 0x6EBA) {
		return "special_decode_path";
	}
	if (rawMsgId >= 0x52 && rawMsgId <= 0xDF) {
		unsigned int idx = rawMsgId - 0x52;
		unsigned char tableIndex = 0;
		unsigned int targetRva = 0;
		if (g_networkBase &&
			SafeCopyRaw(&tableIndex, reinterpret_cast<const void*>(g_networkBase + 0x2F6C1C + idx), sizeof(tableIndex)) &&
			SafeCopyRaw(&targetRva, reinterpret_cast<const void*>(g_networkBase + 0x2F6C14 + tableIndex * sizeof(unsigned int)), sizeof(targetRva)))
		{
			if (targetRva == 0x2F670C) {
				return rawMsgId == 100 ? "msg100_lookup_return0" : "lookup_return0";
			}
			return "lookup_continue";
		}
		return "lookup_unknown";
	}
	return "not_static_filtered";
}

unsigned short ReadPacketMsgId(const unsigned char* packet, int packetLen)
{
	if (!packet || packetLen < 18) {
		return 0;
	}

	unsigned char optLen = 0;
	if (!SafeCopyRaw(&optLen, packet + 17, sizeof(optLen))) {
		return 0;
	}

	size_t msgHeadOffset = 18u + optLen;
	if (static_cast<size_t>(packetLen) < msgHeadOffset + sizeof(unsigned short)) {
		return 0;
	}

	unsigned short netMsgId = 0;
	if (!SafeCopyRaw(&netMsgId, packet + msgHeadOffset, sizeof(netMsgId))) {
		return 0;
	}
	return Swap16(netMsgId);
}

void TracePacketEvent(const char* eventName, unsigned short msgId, int dataLen, const char* detail)
{
	ULONGLONG tick = GetTickCount64();
	DWORD tid = GetCurrentThreadId();
	AcquireSRWLockExclusive(&g_dumpLock);
	std::string path = GetPacketDumpOutputDirectory() + "network_trace_" + std::to_string(GetCurrentProcessId()) + ".log";
	if (!g_networkTraceFile.is_open()) {
		g_networkTraceFile.clear();
		g_networkTraceFile.open(path, std::ios::app);
	}
	if (g_networkTraceFile) {
		g_networkTraceFile << "[" << tick << "]"
			<< " tid=" << tid
			<< " event=" << eventName
			<< " msg=" << msgId
			<< " len=" << dataLen;
		if (detail && detail[0]) {
			g_networkTraceFile << " " << detail;
		}
		g_networkTraceFile << "\n";
	}
	TraceOfficialOneStopEventLocked(tick, tid, eventName, msgId, dataLen, detail);
	ReleaseSRWLockExclusive(&g_dumpLock);
}

void DumpParserRegisteredIdsOnce(uintptr_t parser, unsigned short triggerMsg)
{
	if (!parser) {
		return;
	}

	uintptr_t sentinel = ReadPtrSafe(reinterpret_cast<const void*>(parser + 0x48));
	uintptr_t count = ReadPtrSafe(reinterpret_cast<const void*>(parser + 0x50));
	if (!sentinel || count == 0 || count > 0x4000) {
		return;
	}
	if (InterlockedCompareExchange(&g_parserIdsDumped, 1, 0) != 0) {
		return;
	}

	std::vector<std::pair<unsigned int, unsigned int>> ids;
	ids.reserve(static_cast<size_t>((std::min)(count, static_cast<uintptr_t>(0x1000))));

	uintptr_t node = ReadPtrSafe(reinterpret_cast<const void*>(sentinel + 8));
	for (uintptr_t guard = 0; node && node != sentinel && guard < count + 8 && guard < 0x4000; ++guard) {
		unsigned int id = ReadU32Safe(reinterpret_cast<const void*>(node + 0x10));
		unsigned int enabled = ReadU8Safe(reinterpret_cast<const void*>(node + 0x14));
		if (id) {
			ids.emplace_back(id, enabled);
		}
		node = ReadPtrSafe(reinterpret_cast<const void*>(node + 8));
	}

	std::sort(ids.begin(), ids.end(), [](const auto& left, const auto& right) {
		return left.first < right.first;
	});

	AcquireSRWLockExclusive(&g_dumpLock);
	std::string path = GetPacketDumpOutputDirectory() + "network_parser_registered_ids_" + std::to_string(GetCurrentProcessId()) + ".log";
	std::ofstream file(path, std::ios::app);
	if (file) {
		file << "[" << GetTickCount64() << "]"
			<< " tid=" << GetCurrentThreadId()
			<< " triggerMsg=" << triggerMsg
			<< " parser=" << reinterpret_cast<void*>(parser)
			<< " sentinel=" << reinterpret_cast<void*>(sentinel)
			<< " declaredCount=" << count
			<< " traversedCount=" << ids.size()
			<< "\n";
		for (const auto& item : ids) {
			file << item.first << ":" << item.second << " ";
		}
		file << "\n";
	}
	ReleaseSRWLockExclusive(&g_dumpLock);
}

struct ParserMonitorSnapshot
{
	uintptr_t parser = 0;
	ParserHandlerSnapshot msg100 = {};
	ParserHandlerSnapshot msg223 = {};
};

ParserMonitorSnapshot ReadParserMonitorSnapshot()
{
	ParserMonitorSnapshot snapshot = {};
	if (!g_networkBase) {
		return snapshot;
	}

	snapshot.parser = ReadPtrSafe(reinterpret_cast<const void*>(g_networkBase + kParserGlobalRva));
	if (!snapshot.parser) {
		return snapshot;
	}

	snapshot.msg100 = ReadParserHandlerSnapshot(reinterpret_cast<void*>(snapshot.parser), 100);
	snapshot.msg223 = ReadParserHandlerSnapshot(reinterpret_cast<void*>(snapshot.parser), 223);
	return snapshot;
}

bool ParserMonitorSnapshotChanged(const ParserMonitorSnapshot& left, const ParserMonitorSnapshot& right)
{
	return left.parser != right.parser ||
		left.msg100.runtime != right.msg100.runtime ||
		left.msg100.fastPathFlag != right.msg100.fastPathFlag ||
		left.msg100.count != right.msg100.count ||
		left.msg100.mask != right.msg100.mask ||
		left.msg100.buckets != right.msg100.buckets ||
		left.msg100.sentinel != right.msg100.sentinel ||
		left.msg100.handlerFound != right.msg100.handlerFound ||
		left.msg100.node != right.msg100.node ||
		left.msg100.nodeMsg != right.msg100.nodeMsg ||
		left.msg223.handlerFound != right.msg223.handlerFound ||
		left.msg223.node != right.msg223.node ||
		left.msg223.nodeMsg != right.msg223.nodeMsg;
}

void TraceParserMonitorState(const char* reason, const ParserMonitorSnapshot& snapshot)
{
	AcquireSRWLockExclusive(&g_dumpLock);
	std::string path = GetPacketDumpOutputDirectory() + "network_parser_state_" + std::to_string(GetCurrentProcessId()) + ".log";
	std::ofstream file(path, std::ios::app);
	if (file) {
		file << "[" << GetTickCount64() << "]"
			<< " tid=" << GetCurrentThreadId()
			<< " reason=" << reason
			<< " parser=" << reinterpret_cast<void*>(snapshot.parser)
			<< " runtime=" << reinterpret_cast<void*>(snapshot.msg100.runtime)
			<< " fastPathFlag=" << static_cast<unsigned int>(snapshot.msg100.fastPathFlag)
			<< " count=" << snapshot.msg100.count
			<< " mask=0x" << std::hex << snapshot.msg100.mask << std::dec
			<< " buckets=" << reinterpret_cast<void*>(snapshot.msg100.buckets)
			<< " sentinel=" << reinterpret_cast<void*>(snapshot.msg100.sentinel)
			<< " handler100=" << (snapshot.msg100.handlerFound ? 1 : 0)
			<< " node100=" << reinterpret_cast<void*>(snapshot.msg100.node)
			<< " nodeMsg100=" << snapshot.msg100.nodeMsg
			<< " handler223=" << (snapshot.msg223.handlerFound ? 1 : 0)
			<< " node223=" << reinterpret_cast<void*>(snapshot.msg223.node)
			<< " nodeMsg223=" << snapshot.msg223.nodeMsg
			<< "\n";
	}
	ReleaseSRWLockExclusive(&g_dumpLock);
}

DWORD WINAPI ParserStateMonitorWorker(LPVOID)
{
	DWORD waited = 0;
	ParserMonitorSnapshot previous = {};
	bool havePrevious = false;
	while (InterlockedCompareExchange(&g_stopWorker, 0, 0) == 0 && waited <= kParserMonitorMs) {
		ParserMonitorSnapshot current = ReadParserMonitorSnapshot();
		if (current.parser) {
			if (!havePrevious || ParserMonitorSnapshotChanged(previous, current)) {
				TraceParserMonitorState(havePrevious ? "changed" : "initial", current);
				previous = current;
				havePrevious = true;
			}
		}
		Sleep(kParserMonitorPollMs);
		waited += kParserMonitorPollMs;
	}
	return 0;
}

void StartParserStateMonitor()
{
	if (InterlockedCompareExchange(&g_parserMonitorStarted, 1, 0) != 0) {
		return;
	}

	HANDLE thread = CreateThread(nullptr, 0, ParserStateMonitorWorker, nullptr, 0, nullptr);
	if (!thread) {
		HookLog("Network parser state monitor thread create failed error=%lu", GetLastError());
		return;
	}
	CloseHandle(thread);
}

std::string BytesToHex(const BYTE* bytes, DWORD count)
{
	static const char kHex[] = "0123456789abcdef";
	std::string result;
	result.resize(count * 2);
	for (DWORD i = 0; i < count; ++i) {
		result[i * 2] = kHex[(bytes[i] >> 4) & 0x0F];
		result[i * 2 + 1] = kHex[bytes[i] & 0x0F];
	}
	return result;
}

std::string ReadMemoryHex(uintptr_t address, size_t size)
{
	BYTE bytes[128] = {};
	size_t copySize = (std::min)(size, sizeof(bytes));
	if (!SafeCopyRaw(bytes, reinterpret_cast<const void*>(address), copySize)) {
		return "<unreadable>";
	}
	return BytesToHex(bytes, static_cast<DWORD>(copySize));
}

std::string WideToUtf8(const wchar_t* value)
{
	if (!value || !value[0]) {
		return "";
	}

	int needed = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
	if (needed <= 1) {
		return "";
	}

	std::string result(static_cast<size_t>(needed - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), needed, nullptr, nullptr);
	return result;
}

const char* MemoryTypeName(DWORD type)
{
	switch (type) {
	case MEM_IMAGE:
		return "MEM_IMAGE";
	case MEM_MAPPED:
		return "MEM_MAPPED";
	case MEM_PRIVATE:
		return "MEM_PRIVATE";
	default:
		return "UNKNOWN";
	}
}

std::string ProtectToString(DWORD protect)
{
	char text[64] = { 0 };
	snprintf(text, sizeof(text), "0x%lX", protect);
	return text;
}

std::string HexAddress(uintptr_t value)
{
	char text[32] = { 0 };
	snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(value));
	return text;
}

std::string DescribeMemoryRegion(uintptr_t address)
{
	if (!address) {
		return "<null>";
	}

	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))) {
		return "<VirtualQuery failed>";
	}

	wchar_t modulePath[MAX_PATH] = {};
	std::string module;
	if (GetModuleFileNameW(reinterpret_cast<HMODULE>(mbi.AllocationBase), modulePath, MAX_PATH)) {
		module = WideToUtf8(modulePath);
	}

	char text[256] = { 0 };
	snprintf(text, sizeof(text),
		"base=%p allocBase=%p size=0x%llX state=0x%lX protect=%s type=%s",
		mbi.BaseAddress,
		mbi.AllocationBase,
		static_cast<unsigned long long>(mbi.RegionSize),
		mbi.State,
		ProtectToString(mbi.Protect).c_str(),
		MemoryTypeName(mbi.Type));

	if (!module.empty()) {
		return std::string(text) + " module=" + module;
	}
	return text;
}

std::string GetModuleBaseNameUtf8(HMODULE module)
{
	wchar_t modulePath[MAX_PATH] = {};
	if (!module || !GetModuleFileNameW(module, modulePath, MAX_PATH)) {
		return "";
	}

	const wchar_t* name = wcsrchr(modulePath, L'\\');
	name = name ? name + 1 : modulePath;
	return WideToUtf8(name);
}

std::string FormatModuleRva(uintptr_t address)
{
	if (!address) {
		return "0x0000000000000000";
	}

	HMODULE module = nullptr;
	if (GetModuleHandleExW(
		GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		reinterpret_cast<LPCWSTR>(address),
		&module) && module)
	{
		std::string moduleName = GetModuleBaseNameUtf8(module);
		uintptr_t rva = address - reinterpret_cast<uintptr_t>(module);
		if (!moduleName.empty()) {
			return moduleName + "+0x" + HexAddress(rva);
		}
	}

	return "0x" + HexAddress(address);
}

std::string CaptureStackText()
{
	void* frames[16] = {};
	USHORT count = RtlCaptureStackBackTrace(0, static_cast<ULONG>(_countof(frames)), frames, nullptr);
	std::string text;
	for (USHORT i = 0; i < count; ++i) {
		if (!text.empty()) {
			text += ";";
		}
		text += "#";
		text += std::to_string(i);
		text += "=";
		text += FormatModuleRva(reinterpret_cast<uintptr_t>(frames[i]));
	}
	return text;
}

bool ShouldTraceTopKart551(volatile LONG* counter, LONG& index)
{
	index = InterlockedIncrement(counter);
	return index <= kTopKart551TraceFirstDetailed ||
		(kTopKart551TraceSparseEvery > 0 && (index % kTopKart551TraceSparseEvery) == 0);
}

bool ShouldTraceTopKart100(volatile LONG* counter, LONG& index)
{
	index = InterlockedIncrement(counter);
	return index <= kTopKart100TraceFirstDetailed;
}

uintptr_t GetTopKartCallSiteRvaFromReturnAddress(uintptr_t returnAddress)
{
	if (!g_topKartBase || returnAddress < g_topKartBase + 5) {
		return 0;
	}
	return returnAddress - g_topKartBase - 5;
}

void __fastcall RecordTopKartAntiCheckerExecute(
	int,
	uintptr_t returnAddress,
	uintptr_t entryRsp,
	const TopKartPreCallContext* context)
{
	const LONG sequence = InterlockedIncrement(&g_antiCheckerTraceWriteCount);
	if (sequence <= 0 || static_cast<size_t>(sequence) > kAntiCheckerTraceSampleCapacity) {
		InterlockedExchange(&g_antiCheckerTraceOverflow, 1);
		return;
	}

	LARGE_INTEGER qpc = {};
	QueryPerformanceCounter(&qpc);

	AntiCheckerTraceSample& sample = g_antiCheckerTraceSamples[sequence - 1];
	sample.Sequence = sequence;
	sample.TickMs = GetTickCount64();
	sample.Qpc = qpc.QuadPart;
	sample.ThreadId = GetCurrentThreadId();
	sample.ThisPtr = context ? context->Rcx : 0;
	sample.ReturnAddress = returnAddress;
	sample.EntryRsp = entryRsp;
	MemoryBarrier();
	InterlockedExchange(&sample.Ready, 1);
}

void TraceTopKart551(const char* eventName, LONG index, uintptr_t returnAddress, const char* detail, bool includeStack)
{
	uintptr_t callSiteRva = GetTopKartCallSiteRvaFromReturnAddress(returnAddress);
	std::string stack = includeStack ? CaptureStackText() : std::string();

	AcquireSRWLockExclusive(&g_dumpLock);
	std::string path = GetPacketDumpOutputDirectory() + "topkart_551_trace_" + std::to_string(GetCurrentProcessId()) + ".log";
	std::ofstream file(path, std::ios::app);
	if (file) {
		file << "[" << GetTickCount64() << "]"
			<< " tid=" << GetCurrentThreadId()
			<< " event=" << eventName
			<< " index=" << index
			<< " callRva=0x" << HexAddress(callSiteRva)
			<< " return=" << FormatModuleRva(returnAddress);
		if (detail && detail[0]) {
			file << " " << detail;
		}
		if (!stack.empty()) {
			file << " stack=" << stack;
		}
		file << "\n";
	}
	ReleaseSRWLockExclusive(&g_dumpLock);
}

void TraceTopKart100(const char* eventName, LONG index, uintptr_t returnAddress, const char* detail, bool includeStack)
{
	std::string stack = includeStack ? CaptureStackText() : std::string();

	AcquireSRWLockExclusive(&g_dumpLock);
	std::string path = GetPacketDumpOutputDirectory() + "topkart_100_trace_" + std::to_string(GetCurrentProcessId()) + ".log";
	std::ofstream file(path, std::ios::app);
	if (file) {
		file << "[" << GetTickCount64() << "]"
			<< " tid=" << GetCurrentThreadId()
			<< " event=" << eventName
			<< " index=" << index
			<< " return=" << FormatModuleRva(returnAddress)
			<< " topKartBase=0x" << HexAddress(g_topKartBase)
			<< " networkBase=0x" << HexAddress(g_networkBase);
		if (detail && detail[0]) {
			file << " " << detail;
		}
		if (!stack.empty()) {
			file << " stack=" << stack;
		}
		file << "\n";
	}
	ReleaseSRWLockExclusive(&g_dumpLock);
}

std::string ReadStackSlotsText(const void* returnAddressSlot, size_t count)
{
	std::string text;
	const auto* slot = reinterpret_cast<const uintptr_t*>(returnAddressSlot);
	for (size_t i = 0; i < count; ++i) {
		uintptr_t value = ReadPtrSafe(slot + i);
		if (!text.empty()) {
			text += ",";
		}
		text += "s";
		text += std::to_string(i);
		text += "=0x";
		text += HexAddress(value);
	}
	return text;
}

bool IsReadableMemoryProtect(DWORD protect);

std::string ReadPointerPreviewText(const char* name, uintptr_t value)
{
	std::string text = name ? name : "ptr";
	text += "=0x";
	text += HexAddress(value);
	if (!value) {
		text += " mem=<null>";
		return text;
	}

	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(reinterpret_cast<const void*>(value), &mbi, sizeof(mbi)) || !IsReadableMemoryProtect(mbi.Protect)) {
		text += " mem=<unreadable>";
		return text;
	}

	text += " mem=";
	text += ReadMemoryHex(value, 32);
	return text;
}

size_t GetReadableMemoryDumpSize(uintptr_t address, size_t requested)
{
	if (!address || requested == 0 || address < 0x10000 || address >= 0x0000800000000000ULL) {
		return 0;
	}

	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))) {
		return 0;
	}
	if (mbi.State != MEM_COMMIT || !IsReadableMemoryProtect(mbi.Protect)) {
		return 0;
	}

	uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
	uintptr_t regionEnd = regionBase + mbi.RegionSize;
	if (regionEnd <= address) {
		return 0;
	}

	size_t available = static_cast<size_t>(regionEnd - address);
	size_t cappedRequested = (std::min)(requested, kTopKart551MaxSnapshotBytes);
	return (std::min)(available, cappedRequested);
}

std::string BuildTopKartSnapshotDetail(
	const char* targetName,
	const char* label,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t address,
	size_t requested,
	size_t dumped)
{
	char detail[1024] = {};
	snprintf(detail, sizeof(detail),
		"target=%s label=%s index=%ld return=%s address=0x%s requested=0x%llX dumped=0x%llX region=%s",
		targetName ? targetName : "",
		label ? label : "",
		static_cast<long>(index),
		FormatModuleRva(returnAddress).c_str(),
		HexAddress(address).c_str(),
		static_cast<unsigned long long>(requested),
		static_cast<unsigned long long>(dumped),
		DescribeMemoryRegion(address).c_str());
	return detail;
}

bool DumpTopKartMemorySnapshot(
	unsigned short msgId,
	const char* directionPrefix,
	const char* targetName,
	const char* label,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t address,
	size_t requested)
{
	size_t dumpSize = GetReadableMemoryDumpSize(address, requested);
	if (dumpSize == 0) {
		return false;
	}

	std::string direction = directionPrefix ? directionPrefix : "topkart_";
	direction += targetName ? targetName : "unknown";
	direction += "_";
	direction += label ? label : "mem";

	std::string detail = BuildTopKartSnapshotDetail(targetName, label, index, returnAddress, address, requested, dumpSize);
	DumpPacket(direction.c_str(), reinterpret_cast<const void*>(address), static_cast<int>(dumpSize), msgId, detail.c_str());
	return true;
}

bool DumpTopKartMemorySnapshot(
	const char* targetName,
	const char* label,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t address,
	size_t requested)
{
	return DumpTopKartMemorySnapshot(
		551,
		"topkart_551_",
		targetName,
		label,
		index,
		returnAddress,
		address,
		requested);
}

void DumpTopKartStackPointerSnapshots(
	unsigned short msgId,
	const char* directionPrefix,
	const char* targetName,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t entryRsp,
	size_t stackBytes,
	size_t pointerBytes,
	int maxPointerSnapshots)
{
	if (!entryRsp || pointerBytes == 0) {
		return;
	}

	DumpTopKartMemorySnapshot(msgId, directionPrefix, targetName, "entry_stack", index, returnAddress, entryRsp, stackBytes);

	const auto* slots = reinterpret_cast<const uintptr_t*>(entryRsp);
	int dumped = 0;
	for (int i = 0; i < 48 && dumped < maxPointerSnapshots; ++i) {
		uintptr_t value = ReadPtrSafe(slots + i);
		if (GetReadableMemoryDumpSize(value, 0x20) == 0) {
			continue;
		}

		char label[32] = {};
		snprintf(label, sizeof(label), "stack_s%02d_ptr", i);
		if (DumpTopKartMemorySnapshot(msgId, directionPrefix, targetName, label, index, returnAddress, value, pointerBytes)) {
			++dumped;
		}
	}
}

void DumpTopKartStackPointerSnapshots(
	const char* targetName,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t entryRsp,
	size_t pointerBytes)
{
	DumpTopKartStackPointerSnapshots(
		551,
		"topkart_551_",
		targetName,
		index,
		returnAddress,
		entryRsp,
		kTopKart551StackDumpBytes,
		pointerBytes,
		kTopKart551MaxStackPointerSnapshots);
}

void DumpTopKart100LoginCacheContextSnapshots(
	const char* phase,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t entryRsp,
	unsigned char* key,
	unsigned char* buffer,
	__int64 result)
{
	const char* phaseName = phase ? phase : "unknown";
	std::string targetName = std::string("login_cache_decode_") + phaseName;

	char detail[1024] = {};
	snprintf(detail, sizeof(detail),
		"phase=%s transformIndex=%ld return=%s stackSlot=0x%s key=%p buffer=%p result=%lld bufferRegion=%s",
		phaseName,
		static_cast<long>(index),
		FormatModuleRva(returnAddress).c_str(),
		HexAddress(entryRsp).c_str(),
		key,
		buffer,
		static_cast<long long>(result),
		DescribeMemoryRegion(reinterpret_cast<uintptr_t>(buffer)).c_str());
	TraceTopKart100("login_cache_decode_context", index, returnAddress, detail, true);

	if (key) {
		DumpTopKartMemorySnapshot(
			100,
			"topkart_100_",
			targetName.c_str(),
			"key",
			index,
			returnAddress,
			reinterpret_cast<uintptr_t>(key),
			kTopKart100KeyDumpBytes);
	}
	if (buffer) {
		DumpTopKartMemorySnapshot(
			100,
			"topkart_100_",
			targetName.c_str(),
			"buffer_head",
			index,
			returnAddress,
			reinterpret_cast<uintptr_t>(buffer),
			kTopKart100BufferHeadDumpBytes);
	}
	DumpTopKartStackPointerSnapshots(
		100,
		"topkart_100_",
		targetName.c_str(),
		index,
		returnAddress,
		entryRsp,
		kTopKart100StackDumpBytes,
		kTopKart100StackPointerDumpBytes,
		kTopKart100MaxStackPointerSnapshots);
}

void DumpTopKart100ParserDispatchSnapshots(
	const char* phase,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t entryRsp,
	void* parser,
	unsigned int rawMsgId,
	unsigned short normalizedMsgId,
	const void* body,
	int bodyLen,
	const ParserHandlerSnapshot& snapshot,
	uintptr_t queueBlocks,
	uintptr_t queueMask,
	uintptr_t queueHead,
	uintptr_t queueCount,
	int result)
{
	const char* phaseName = phase ? phase : "unknown";
	std::string targetName = std::string("parser_queue_dispatch_") + phaseName;
	std::string bodyIdHits = FindDebugModelItemHitsText(body, bodyLen);

	char detail[2048] = {};
	snprintf(detail, sizeof(detail),
		"phase=%s index=%ld return=%s stackSlot=0x%s rawMsg=%u msg=%u body=%p bodyLen=%d parser=%p "
		"result=%d runtime=%p fastPath=%u handler=%d node=%p nodeMsg=%u count=%llu "
		"queueBlocks=%p queueMask=0x%llX queueHead=%llu queueCount=%llu bodyRegion=%s parserRegion=%s bodyIdHits=%s",
		phaseName,
		static_cast<long>(index),
		FormatModuleRva(returnAddress).c_str(),
		HexAddress(entryRsp).c_str(),
		rawMsgId,
		static_cast<unsigned int>(normalizedMsgId),
		body,
		bodyLen,
		parser,
		result,
		reinterpret_cast<void*>(snapshot.runtime),
		static_cast<unsigned int>(snapshot.fastPathFlag),
		snapshot.handlerFound ? 1 : 0,
		reinterpret_cast<void*>(snapshot.node),
		snapshot.nodeMsg,
		static_cast<unsigned long long>(snapshot.count),
		reinterpret_cast<void*>(queueBlocks),
		static_cast<unsigned long long>(queueMask),
		static_cast<unsigned long long>(queueHead),
		static_cast<unsigned long long>(queueCount),
		DescribeMemoryRegion(reinterpret_cast<uintptr_t>(body)).c_str(),
		DescribeMemoryRegion(reinterpret_cast<uintptr_t>(parser)).c_str(),
		bodyIdHits.c_str());
	TraceTopKart100("parser_queue_dispatch_context", index, returnAddress, detail, true);

	if (body && bodyLen > 0) {
		std::string direction = std::string("topkart_100_") + targetName + "_body";
		DumpPacket(direction.c_str(), body, bodyLen, 100, detail);
	}
	if (parser) {
		DumpTopKartMemorySnapshot(
			100,
			"topkart_100_",
			targetName.c_str(),
			"parser",
			index,
			returnAddress,
			reinterpret_cast<uintptr_t>(parser),
			kTopKart100ParserObjectDumpBytes);
	}
	if (snapshot.runtime) {
		DumpTopKartMemorySnapshot(
			100,
			"topkart_100_",
			targetName.c_str(),
			"runtime",
			index,
			returnAddress,
			snapshot.runtime,
			kTopKart100ParserObjectDumpBytes);
	}
	if (snapshot.node) {
		DumpTopKartMemorySnapshot(
			100,
			"topkart_100_",
			targetName.c_str(),
			"handler_node",
			index,
			returnAddress,
			snapshot.node,
			kTopKart100HandlerNodeDumpBytes);
	}
	DumpTopKartStackPointerSnapshots(
		100,
		"topkart_100_",
		targetName.c_str(),
		index,
		returnAddress,
		entryRsp,
		kTopKart100StackDumpBytes,
		kTopKart100StackPointerDumpBytes,
		kTopKart100MaxStackPointerSnapshots);
}

struct TopKartLoginItemSnapshot
{
	bool Found = false;
	unsigned int Index = 0;
	unsigned int ItemId = 0;
	unsigned int ItemNum = 0;
	unsigned int AvailPeriod = 0;
	unsigned char Status = 0;
	unsigned int ObtainTime = 0;
	unsigned int OtherAttribute = 0;
	unsigned short ItemType = 0;
};

struct TopKartLoginExtraSnapshot
{
	bool Found = false;
	unsigned int Index = 0;
	unsigned int ItemId = 0;
	unsigned int Value0 = 0;
	unsigned int Value1 = 0;
};

void AppendCsvUIntLocal(char* text, size_t textSize, unsigned int value)
{
	if (!text || textSize == 0) {
		return;
	}
	const size_t used = strnlen_s(text, textSize);
	if (used >= textSize - 1) {
		return;
	}
	snprintf(text + used, textSize - used, "%s%u", used ? "," : "", value);
}

bool ReadTopKartLoginItemById(const unsigned char* plain, size_t plainLen, unsigned int itemId, TopKartLoginItemSnapshot& out)
{
	out = {};
	if (!plain || plainLen < kTopKart100LoginPlainItemArrayOffset || itemId == 0) {
		return false;
	}

	const unsigned short rawCount = ReadU16Safe(plain + kTopKart100LoginPlainItemNumOffset);
	const size_t count = (std::min<size_t>)(rawCount, 8050);
	for (size_t i = 0; i < count; ++i) {
		const size_t offset = kTopKart100LoginPlainItemArrayOffset + i * kTopKart100LoginPlainItemStride;
		if (offset + kTopKart100LoginPlainItemStride > plainLen) {
			break;
		}
		const unsigned char* record = plain + offset;
		const unsigned int currentId = ReadU32Safe(record);
		if (currentId != itemId) {
			continue;
		}

		out.Found = true;
		out.Index = static_cast<unsigned int>(i);
		out.ItemId = currentId;
		out.ItemNum = ReadU32Safe(record + 4);
		out.AvailPeriod = ReadU32Safe(record + 8);
		out.Status = ReadU8Safe(record + kTopKart100LoginPlainItemStatusOffset);
		out.ObtainTime = ReadU32Safe(record + 13);
		out.OtherAttribute = ReadU32Safe(record + 17);
		out.ItemType = ReadU16Safe(record + 21);
		return true;
	}
	return false;
}

bool ReadTopKartLoginExtraById(const unsigned char* plain, size_t plainLen, unsigned int itemId, TopKartLoginExtraSnapshot& out)
{
	out = {};
	if (!plain || plainLen < kTopKart100LoginPlainExtraArrayOffset || itemId == 0) {
		return false;
	}

	const unsigned char rawCount = ReadU8Safe(plain + kTopKart100LoginPlainExtraCountOffset);
	const size_t count = (std::min<size_t>)(rawCount, 100);
	for (size_t i = 0; i < count; ++i) {
		const size_t offset = kTopKart100LoginPlainExtraArrayOffset + i * kTopKart100LoginPlainExtraStride;
		if (offset + kTopKart100LoginPlainExtraStride > plainLen) {
			break;
		}
		const unsigned char* record = plain + offset;
		const unsigned int currentId = ReadU32Safe(record);
		if (currentId != itemId) {
			continue;
		}

		out.Found = true;
		out.Index = static_cast<unsigned int>(i);
		out.ItemId = currentId;
		out.Value0 = ReadU32Safe(record + 4);
		out.Value1 = ReadU32Safe(record + 8);
		return true;
	}
	return false;
}

unsigned int CountTopKartLoginEquippedItems(const unsigned char* plain, size_t plainLen, char* firstIds, size_t firstIdsSize)
{
	if (firstIds && firstIdsSize) {
		firstIds[0] = 0;
	}
	if (!plain || plainLen < kTopKart100LoginPlainItemArrayOffset) {
		return 0;
	}

	const unsigned short rawCount = ReadU16Safe(plain + kTopKart100LoginPlainItemNumOffset);
	const size_t count = (std::min<size_t>)(rawCount, 8050);
	unsigned int equippedCount = 0;
	for (size_t i = 0; i < count; ++i) {
		const size_t offset = kTopKart100LoginPlainItemArrayOffset + i * kTopKart100LoginPlainItemStride;
		if (offset + kTopKart100LoginPlainItemStride > plainLen) {
			break;
		}
		const unsigned char* record = plain + offset;
		const unsigned int itemId = ReadU32Safe(record);
		const unsigned char status = ReadU8Safe(record + kTopKart100LoginPlainItemStatusOffset);
		if (!itemId || !status) {
			continue;
		}
		++equippedCount;
		if (equippedCount <= 16) {
			AppendCsvUIntLocal(firstIds, firstIdsSize, itemId);
		}
	}
	return equippedCount;
}

std::string DescribeTopKartLoginItem(const char* name, const TopKartLoginItemSnapshot& item)
{
	char text[256] = {};
	snprintf(text, sizeof(text),
		"%sFound=%d %sIdx=%u %sId=%u %sNum=%u %sPeriod=0x%08X %sStatus=%u %sObtain=%u %sOther=0x%08X %sType=%u",
		name, item.Found ? 1 : 0,
		name, item.Index,
		name, item.ItemId,
		name, item.ItemNum,
		name, item.AvailPeriod,
		name, static_cast<unsigned int>(item.Status),
		name, item.ObtainTime,
		name, item.OtherAttribute,
		name, static_cast<unsigned int>(item.ItemType));
	return text;
}

std::string DescribeTopKartLoginExtra(const char* name, const TopKartLoginExtraSnapshot& item)
{
	char text[160] = {};
	snprintf(text, sizeof(text),
		"%sExtraFound=%d %sExtraIdx=%u %sExtraId=%u %sExtraV0=0x%08X %sExtraV1=0x%08X",
		name, item.Found ? 1 : 0,
		name, item.Index,
		name, item.ItemId,
		name, item.Value0,
		name, item.Value1);
	return text;
}

void TraceTopKartLoginPlainStage(
	const char* eventName,
	const char* phase,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t entryRsp,
	void* cache,
	int type,
	const unsigned char* loginPlain,
	bool dumpFullPlain)
{
	constexpr size_t kPlainLen = kLoginCachePlainLen;
	const bool readable = loginPlain && GetReadableMemoryDumpSize(reinterpret_cast<uintptr_t>(loginPlain), kPlainLen) >= kPlainLen;
	const unsigned short resultId = readable ? ReadU16Safe(loginPlain) : 0;
	const unsigned char keyLen = readable ? ReadU8Safe(loginPlain + 2) : 0;
	const unsigned int uin = readable ? ReadU32Safe(loginPlain + 35) : 0;
	const unsigned int identity = readable ? ReadU32Safe(loginPlain + 39) : 0;
	const unsigned short playerId = readable ? ReadU16Safe(loginPlain + 43) : 0;
	const unsigned int processType = readable ? ReadU32Safe(loginPlain + 45) : 0;
	const unsigned short itemNum = readable ? ReadU16Safe(loginPlain + kTopKart100LoginPlainItemNumOffset) : 0;
	const unsigned char extraCount = readable ? ReadU8Safe(loginPlain + kTopKart100LoginPlainExtraCountOffset) : 0;
	const unsigned int flags = readable ? ReadU32Safe(loginPlain + 315827) : 0;

	TopKartLoginItemSnapshot kart = {};
	TopKartLoginItemSnapshot skin = {};
	TopKartLoginExtraSnapshot kartExtra = {};
	TopKartLoginExtraSnapshot skinExtra = {};
	char equippedIds[256] = {};
	unsigned int equippedCount = 0;
	if (readable) {
		ReadTopKartLoginItemById(loginPlain, kPlainLen, kDebugCurrentKartId, kart);
		ReadTopKartLoginItemById(loginPlain, kPlainLen, kDebugCurrentSkinId, skin);
		ReadTopKartLoginExtraById(loginPlain, kPlainLen, kDebugCurrentKartId, kartExtra);
		ReadTopKartLoginExtraById(loginPlain, kPlainLen, kDebugCurrentSkinId, skinExtra);
		equippedCount = CountTopKartLoginEquippedItems(loginPlain, kPlainLen, equippedIds, sizeof(equippedIds));
	}

	const std::string kartText = DescribeTopKartLoginItem("kart", kart);
	const std::string skinText = DescribeTopKartLoginItem("skin", skin);
	const std::string kartExtraText = DescribeTopKartLoginExtra("kart", kartExtra);
	const std::string skinExtraText = DescribeTopKartLoginExtra("skin", skinExtra);
	const std::string idHits = readable ? FindDebugModelItemHitsText(loginPlain, static_cast<int>(kPlainLen)) : "-";

	char detail[3600] = {};
	snprintf(detail, sizeof(detail),
		"phase=%s index=%ld return=%s entryRsp=0x%s cache=%p typeArg=%d loginPlain=%p readable=%d "
		"result=%u keyLen=%u uin=%u identity=0x%08X playerId=%u processTypeAt45=%u itemNum=%u extraCount=%u flags=0x%08X "
		"equippedCount=%u equippedIds=[%s] watchKart=%u watchSkin=%u %s %s %s %s idHits=%s",
		phase ? phase : "",
		static_cast<long>(index),
		FormatModuleRva(returnAddress).c_str(),
		HexAddress(entryRsp).c_str(),
		cache,
		type,
		loginPlain,
		readable ? 1 : 0,
		static_cast<unsigned int>(resultId),
		static_cast<unsigned int>(keyLen),
		uin,
		identity,
		static_cast<unsigned int>(playerId),
		processType,
		static_cast<unsigned int>(itemNum),
		static_cast<unsigned int>(extraCount),
		flags,
		equippedCount,
		equippedIds,
		kDebugCurrentKartId,
		kDebugCurrentSkinId,
		kartText.c_str(),
		skinText.c_str(),
		kartExtraText.c_str(),
		skinExtraText.c_str(),
		idHits.c_str());
	TraceTopKart100(eventName, index, returnAddress, detail, true);
	TracePacketEvent(eventName, 100, readable ? static_cast<int>(kPlainLen) : 0, detail);

	if (dumpFullPlain && readable) {
		std::string direction = std::string("topkart_100_") + (eventName ? eventName : "login_plain") + "_" + (phase ? phase : "unknown");
		DumpPacket(direction.c_str(), loginPlain, static_cast<int>(kPlainLen), 100, detail);
		DumpTopKartMemorySnapshot(
			100,
			"topkart_100_",
			eventName ? eventName : "login_plain",
			"entry_stack",
			index,
			returnAddress,
			entryRsp,
			kTopKart100StackDumpBytes);
	}
}

struct TopKartPreCallDumpPolicy
{
	size_t RcxBytes = 0;
	size_t RdxBytes = 0;
	size_t R8Bytes = 0;
	size_t R9Bytes = 0;
	bool RdxSizeFromR9 = false;
	size_t StackPointerBytes = 0;
	LONG FirstDumpCount = 0;
};

TopKartPreCallDumpPolicy GetTopKartPreCallDumpPolicy(const TopKartPreCallTraceTarget& target)
{
	TopKartPreCallDumpPolicy policy = {};
	if (target.TargetRva == 0x112D610) {
		policy.RcxBytes = kTopKart551ObjectDumpBytes;
		policy.RdxSizeFromR9 = true;
		policy.R8Bytes = kTopKart551DefaultPointerDumpBytes;
		policy.StackPointerBytes = kTopKart551DefaultPointerDumpBytes;
		policy.FirstDumpCount = 8;
		return policy;
	}

	if (target.CallRva == 0x82E461 || target.CallRva == 0x82E470) {
		policy.RcxBytes = kTopKart551ObjectDumpBytes;
		policy.RdxBytes = kTopKart551DefaultPointerDumpBytes;
		policy.R8Bytes = kTopKart551DefaultPointerDumpBytes;
		policy.R9Bytes = kTopKart551DefaultPointerDumpBytes;
		policy.StackPointerBytes = kTopKart551DefaultPointerDumpBytes;
		policy.FirstDumpCount = 4;
		return policy;
	}

	if (target.CallRva == 0x82DEBE) {
		policy.RdxBytes = kTopKart551ObjectDumpBytes;
		policy.R8Bytes = kTopKart551ObjectDumpBytes;
		policy.R9Bytes = kTopKart551ObjectDumpBytes;
		policy.StackPointerBytes = kTopKart551DefaultPointerDumpBytes;
		policy.FirstDumpCount = 4;
		return policy;
	}

	if (target.TargetRva == 0x82C590) {
		policy.RcxBytes = 0x200;
		policy.RdxBytes = 0x200;
		policy.R8Bytes = 0x200;
		policy.R9Bytes = 0x200;
		policy.StackPointerBytes = 0x200;
		policy.FirstDumpCount = 2;
		return policy;
	}

	policy.StackPointerBytes = 0x200;
	policy.FirstDumpCount = 2;
	return policy;
}

void DumpTopKartPreCallSnapshots(
	const TopKartPreCallTraceTarget& target,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t entryRsp,
	const TopKartPreCallContext& context)
{
	TopKartPreCallDumpPolicy policy = GetTopKartPreCallDumpPolicy(target);
	if (policy.FirstDumpCount <= 0 || index > policy.FirstDumpCount) {
		return;
	}

	size_t dumped = 0;
	dumped += DumpTopKartMemorySnapshot(target.Name, "rcx", index, returnAddress, context.Rcx, policy.RcxBytes) ? 1 : 0;

	size_t rdxBytes = policy.RdxSizeFromR9 ?
		static_cast<size_t>((std::min<uintptr_t>)(context.R9, static_cast<uintptr_t>(kTopKart551MaxSnapshotBytes))) :
		policy.RdxBytes;
	dumped += DumpTopKartMemorySnapshot(target.Name, policy.RdxSizeFromR9 ? "rdx_len_r9" : "rdx", index, returnAddress, context.Rdx, rdxBytes) ? 1 : 0;
	dumped += DumpTopKartMemorySnapshot(target.Name, "r8", index, returnAddress, context.R8, policy.R8Bytes) ? 1 : 0;
	dumped += DumpTopKartMemorySnapshot(target.Name, "r9", index, returnAddress, context.R9, policy.R9Bytes) ? 1 : 0;

	DumpTopKartStackPointerSnapshots(target.Name, index, returnAddress, entryRsp, policy.StackPointerBytes);

	char detail[512] = {};
	snprintf(detail, sizeof(detail),
		"name=%s index=%ld snapshotRegisterBlocks=%llu rdxSizeFromR9=%d entryRsp=%p stackPointerBytes=0x%llX",
		target.Name,
		static_cast<long>(index),
		static_cast<unsigned long long>(dumped),
		policy.RdxSizeFromR9 ? 1 : 0,
		reinterpret_cast<void*>(entryRsp),
		static_cast<unsigned long long>(policy.StackPointerBytes));
	TraceTopKart551("pre_call_snapshot", index, returnAddress, detail, false);
}

void DumpTopKartBuildSnapshots(
	const char* phase,
	LONG index,
	uintptr_t returnAddress,
	void* returnAddressSlot,
	__int64 arg1,
	__int64 arg2,
	__int64 arg3,
	__int64 arg4,
	__int64 result)
{
	if (index > 4) {
		return;
	}

	char targetName[64] = {};
	snprintf(targetName, sizeof(targetName), "build_runtime_cache_%s", phase ? phase : "snapshot");

	DumpTopKartMemorySnapshot(targetName, "arg1_rcx", index, returnAddress, static_cast<uintptr_t>(arg1), kTopKart551DefaultPointerDumpBytes);
	DumpTopKartMemorySnapshot(targetName, "arg2_rdx", index, returnAddress, static_cast<uintptr_t>(arg2), kTopKart551DefaultPointerDumpBytes);
	DumpTopKartMemorySnapshot(targetName, "arg3_r8", index, returnAddress, static_cast<uintptr_t>(arg3), kTopKart551ObjectDumpBytes);
	DumpTopKartMemorySnapshot(targetName, "arg4_r9", index, returnAddress, static_cast<uintptr_t>(arg4), kTopKart551DefaultPointerDumpBytes);
	DumpTopKartStackPointerSnapshots(targetName, index, returnAddress, reinterpret_cast<uintptr_t>(returnAddressSlot), kTopKart551DefaultPointerDumpBytes);

	char detail[512] = {};
	snprintf(detail, sizeof(detail),
		"phase=%s index=%ld result=0x%llX rcx=0x%llX rdx=0x%llX r8=0x%llX r9=0x%llX returnSlot=%p",
		phase ? phase : "",
		static_cast<long>(index),
		static_cast<unsigned long long>(result),
		static_cast<unsigned long long>(arg1),
		static_cast<unsigned long long>(arg2),
		static_cast<unsigned long long>(arg3),
		static_cast<unsigned long long>(arg4),
		returnAddressSlot);
	TraceTopKart551("build_runtime_cache_snapshot", index, returnAddress, detail, false);
}

void __fastcall TraceTopKartPreCall(int targetIndex, uintptr_t returnAddress, uintptr_t entryRsp, const TopKartPreCallContext* context)
{
	if (targetIndex < 0 || targetIndex >= static_cast<int>(_countof(kTopKart21273PreCallTargets)) || !context) {
		return;
	}

	LONG index = 0;
	bool shouldTrace = ShouldTraceTopKart551(&g_topKartPreCallTraceCounts[targetIndex], index);
	if (!shouldTrace) {
		return;
	}

	const TopKartPreCallTraceTarget& target = kTopKart21273PreCallTargets[targetIndex];
	std::string stackSlots = ReadStackSlotsText(reinterpret_cast<const void*>(entryRsp), 12);
	std::string rcxPreview = ReadPointerPreviewText("rcx", context->Rcx);
	std::string rdxPreview = ReadPointerPreviewText("rdx", context->Rdx);
	std::string r8Preview = ReadPointerPreviewText("r8", context->R8);
	std::string r9Preview = ReadPointerPreviewText("r9", context->R9);
	char detail[3500] = {};
	snprintf(detail, sizeof(detail),
		"name=%s expectedCallRva=0x%llX expectedTargetRva=0x%llX "
		"rax=0x%llX rcx=0x%llX rdx=0x%llX r8=0x%llX r9=0x%llX r10=0x%llX r11=0x%llX "
		"entryRsp=%p stackSlots=%s %s %s %s %s",
		target.Name,
		static_cast<unsigned long long>(target.CallRva),
		static_cast<unsigned long long>(target.TargetRva),
		static_cast<unsigned long long>(context->Rax),
		static_cast<unsigned long long>(context->Rcx),
		static_cast<unsigned long long>(context->Rdx),
		static_cast<unsigned long long>(context->R8),
		static_cast<unsigned long long>(context->R9),
		static_cast<unsigned long long>(context->R10),
		static_cast<unsigned long long>(context->R11),
		reinterpret_cast<void*>(entryRsp),
		stackSlots.c_str(),
		rcxPreview.c_str(),
		rdxPreview.c_str(),
		r8Preview.c_str(),
		r9Preview.c_str());
	TraceTopKart551("pre_call_enter", index, returnAddress, detail, true);
	DumpTopKartPreCallSnapshots(target, index, returnAddress, entryRsp, *context);
}

std::string BuildTopKartModelPreCallHint(const TopKartPreCallTraceTarget& target, const TopKartPreCallContext& context)
{
	char hint[512] = {};
	if (target.TargetRva == kTopKart21273CheckActiveKartRva) {
		snprintf(hint, sizeof(hint), "hint=CheckActiveKart player=%p activePtr=%p requestedKart=%u",
			reinterpret_cast<void*>(context.Rcx),
			reinterpret_cast<void*>(context.Rdx),
			static_cast<unsigned int>(context.R8));
	}
	else if (target.TargetRva == kTopKart21273CheckKartSkinRva) {
		snprintf(hint, sizeof(hint), "hint=CheckKartSkin player=%p flag=%u record=%p baseKart=%u",
			reinterpret_cast<void*>(context.Rcx),
			static_cast<unsigned int>(context.Rdx & 0xFF),
			reinterpret_cast<void*>(context.R8),
			static_cast<unsigned int>(context.R9));
	}
	else if (target.TargetRva == kTopKart21273SetActiveKartCoreRva) {
		snprintf(hint, sizeof(hint), "hint=SetActiveKartCore player=%p itemId=%u outKart=%p outDisplay=%p",
			reinterpret_cast<void*>(context.Rcx),
			static_cast<unsigned int>(context.Rdx),
			reinterpret_cast<void*>(context.R8),
			reinterpret_cast<void*>(context.R9));
	}
	else if (target.TargetRva == kTopKart21273LoadKartAmRva) {
		snprintf(hint, sizeof(hint), "hint=LoadKartAm player=%p",
			reinterpret_cast<void*>(context.Rcx));
	}
	else if (target.TargetRva == kTopKart21273AsyncChangeKartModelRva) {
		snprintf(hint, sizeof(hint), "hint=AsyncChangeKartModel pipeline=%p weakPlayer=%p newKartId=%u",
			reinterpret_cast<void*>(context.Rcx),
			reinterpret_cast<void*>(context.Rdx),
			static_cast<unsigned int>(context.R8));
	}
	else if (target.TargetRva == kTopKart21273ModelExistsRva) {
		snprintf(hint, sizeof(hint), "hint=ModelExists mgr=%p itemId=%u",
			reinterpret_cast<void*>(context.Rcx),
			static_cast<unsigned int>(context.Rdx));
	}
	else if (target.TargetRva == kTopKart21273OriginMapRva) {
		snprintf(hint, sizeof(hint), "hint=OriginMap mgr=%p itemId=%u",
			reinterpret_cast<void*>(context.Rcx),
			static_cast<unsigned int>(context.Rdx));
	}
	else {
		snprintf(hint, sizeof(hint), "hint=unknown");
	}
	return hint;
}

void DumpTopKartModelPreCallSnapshots(
	const TopKartPreCallTraceTarget& target,
	LONG index,
	uintptr_t returnAddress,
	uintptr_t entryRsp,
	const TopKartPreCallContext& context)
{
	if (index > 8) {
		return;
	}

	const char* prefix = "topkart_100_";
	size_t rcxBytes = 0x400;
	size_t rdxBytes = 0;
	size_t r8Bytes = 0;
	size_t r9Bytes = 0;

	if (target.TargetRva == kTopKart21273CheckActiveKartRva) {
		rdxBytes = 0x40;
	}
	else if (target.TargetRva == kTopKart21273CheckKartSkinRva) {
		r8Bytes = 0x400;
	}
	else if (target.TargetRva == kTopKart21273SetActiveKartCoreRva) {
		r8Bytes = 0x40;
		r9Bytes = 0x40;
	}
	else if (target.TargetRva == kTopKart21273AsyncChangeKartModelRva) {
		rdxBytes = 0x80;
	}

	DumpTopKartMemorySnapshot(100, prefix, target.Name, "rcx", index, returnAddress, context.Rcx, rcxBytes);
	DumpTopKartMemorySnapshot(100, prefix, target.Name, "rdx", index, returnAddress, context.Rdx, rdxBytes);
	DumpTopKartMemorySnapshot(100, prefix, target.Name, "r8", index, returnAddress, context.R8, r8Bytes);
	DumpTopKartMemorySnapshot(100, prefix, target.Name, "r9", index, returnAddress, context.R9, r9Bytes);
	DumpTopKartStackPointerSnapshots(
		100,
		prefix,
		target.Name,
		index,
		returnAddress,
		entryRsp,
		kTopKart100StackDumpBytes,
		0x400,
		12);
}

void __fastcall TraceTopKartModelPreCall(int targetIndex, uintptr_t returnAddress, uintptr_t entryRsp, const TopKartPreCallContext* context)
{
	if (targetIndex < 0 || targetIndex >= static_cast<int>(_countof(kTopKart21273ModelPreCallTargets)) || !context) {
		return;
	}

	LONG index = InterlockedIncrement(&g_topKart100ModelPreCallTraceCounts[targetIndex]);
	if (index > kTopKart100ModelPreCallFirstDetailed) {
		return;
	}

	const TopKartPreCallTraceTarget& target = kTopKart21273ModelPreCallTargets[targetIndex];
	std::string stackSlots = ReadStackSlotsText(reinterpret_cast<const void*>(entryRsp), 12);
	std::string hint = BuildTopKartModelPreCallHint(target, *context);
	std::string rcxPreview = ReadPointerPreviewText("rcx", context->Rcx);
	std::string rdxPreview = ReadPointerPreviewText("rdx", context->Rdx);
	std::string r8Preview = ReadPointerPreviewText("r8", context->R8);
	std::string r9Preview = ReadPointerPreviewText("r9", context->R9);
	char detail[3600] = {};
	snprintf(detail, sizeof(detail),
		"name=%s expectedCallRva=0x%llX expectedTargetRva=0x%llX %s "
		"rax=0x%llX rcx=0x%llX rdx=0x%llX r8=0x%llX r9=0x%llX r10=0x%llX r11=0x%llX "
		"entryRsp=%p stackSlots=%s %s %s %s %s",
		target.Name,
		static_cast<unsigned long long>(target.CallRva),
		static_cast<unsigned long long>(target.TargetRva),
		hint.c_str(),
		static_cast<unsigned long long>(context->Rax),
		static_cast<unsigned long long>(context->Rcx),
		static_cast<unsigned long long>(context->Rdx),
		static_cast<unsigned long long>(context->R8),
		static_cast<unsigned long long>(context->R9),
		static_cast<unsigned long long>(context->R10),
		static_cast<unsigned long long>(context->R11),
		reinterpret_cast<void*>(entryRsp),
		stackSlots.c_str(),
		rcxPreview.c_str(),
		rdxPreview.c_str(),
		r8Preview.c_str(),
		r9Preview.c_str());
	TraceTopKart100("model_pre_call", index, returnAddress, detail, true);
	DumpTopKartModelPreCallSnapshots(target, index, returnAddress, entryRsp, *context);
}

bool IsReadableMemoryProtect(DWORD protect)
{
	if (protect & PAGE_GUARD) {
		return false;
	}
	DWORD baseProtect = protect & 0xFF;
	return baseProtect == PAGE_READONLY ||
		baseProtect == PAGE_READWRITE ||
		baseProtect == PAGE_WRITECOPY ||
		baseProtect == PAGE_EXECUTE_READ ||
		baseProtect == PAGE_EXECUTE_READWRITE ||
		baseProtect == PAGE_EXECUTE_WRITECOPY;
}

uintptr_t ResolveTrampolineTarget(uintptr_t address)
{
	BYTE bytes[96] = {};
	if (!address || !SafeCopyRaw(bytes, reinterpret_cast<const void*>(address), sizeof(bytes))) {
		return 0;
	}

	if (bytes[0] == 0x48 && bytes[1] == 0xB8 && bytes[10] == 0xFF && bytes[11] == 0xE0) {
		uintptr_t target = 0;
		SafeCopyRaw(&target, bytes + 2, sizeof(target));
		return target;
	}
	if (bytes[0] == 0x49 && bytes[1] == 0xBB && bytes[10] == 0x41 && bytes[11] == 0xFF && bytes[12] == 0xE3) {
		uintptr_t target = 0;
		SafeCopyRaw(&target, bytes + 2, sizeof(target));
		return target;
	}
	if (bytes[0] == 0xE9) {
		int rel = 0;
		SafeCopyRaw(&rel, bytes + 1, sizeof(rel));
		return address + 5 + rel;
	}
	if (bytes[0] == 0xFF && bytes[1] == 0x25) {
		int rel = 0;
		SafeCopyRaw(&rel, bytes + 2, sizeof(rel));
		uintptr_t pointerAddress = address + 6 + rel;
		return ReadPtrSafe(reinterpret_cast<const void*>(pointerAddress));
	}
	for (size_t i = 1; i + 5 <= sizeof(bytes); ++i) {
		if (bytes[i] != 0xE9) {
			continue;
		}
		int rel = 0;
		SafeCopyRaw(&rel, bytes + i + 1, sizeof(rel));
		uintptr_t target = address + i + 5 + rel;
		if (target >= 0x10000 && target < 0x0000800000000000ULL) {
			return target;
		}
	}
	return 0;
}

bool MarkPatchAllocationDumped(uintptr_t allocationBase)
{
	if (!allocationBase) {
		return false;
	}

	AcquireSRWLockExclusive(&g_dumpLock);
	for (size_t i = 0; i < g_dumpedPatchAllocationBaseCount; ++i) {
		if (g_dumpedPatchAllocationBases[i] == allocationBase) {
			ReleaseSRWLockExclusive(&g_dumpLock);
			return false;
		}
	}
	if (g_dumpedPatchAllocationBaseCount >= _countof(g_dumpedPatchAllocationBases)) {
		ReleaseSRWLockExclusive(&g_dumpLock);
		return false;
	}
	g_dumpedPatchAllocationBases[g_dumpedPatchAllocationBaseCount++] = allocationBase;
	ReleaseSRWLockExclusive(&g_dumpLock);
	return true;
}

struct PatchAllocationDumpRequest
{
	uintptr_t AllocationBase = 0;
	unsigned short MsgId = 0;
	char Label[64] = {};
};

bool DumpMemoryRangeToFile(const std::string& path, uintptr_t base, size_t size)
{
	std::ofstream file(path, std::ios::binary);
	if (!file) {
		return false;
	}

	constexpr size_t kChunkSize = 0x10000;
	std::vector<unsigned char> chunk(kChunkSize);
	std::vector<unsigned char> zero(kChunkSize);
	size_t offset = 0;
	while (offset < size) {
		size_t toCopy = (std::min)(kChunkSize, size - offset);
		const void* src = reinterpret_cast<const void*>(base + offset);
		if (SafeCopyRaw(chunk.data(), src, toCopy)) {
			file.write(reinterpret_cast<const char*>(chunk.data()), static_cast<std::streamsize>(toCopy));
		}
		else {
			file.write(reinterpret_cast<const char*>(zero.data()), static_cast<std::streamsize>(toCopy));
		}
		offset += toCopy;
	}
	return true;
}

DWORD WINAPI PatchAllocationDumpWorker(LPVOID param)
{
	std::unique_ptr<PatchAllocationDumpRequest> request(static_cast<PatchAllocationDumpRequest*>(param));
	if (!request || !request->AllocationBase) {
		return 0;
	}

	std::string dir = GetPacketPayloadOutputDirectory()
		+ "patch_allocation_"
		+ std::to_string(GetCurrentProcessId())
		+ "_"
		+ HexAddress(request->AllocationBase)
		+ "\\";
	CreateDirectoryA(dir.c_str(), nullptr);

	std::string mapPath = dir + "allocation_map.txt";
	std::ofstream map(mapPath, std::ios::app);
	if (map) {
		map << "pid=" << GetCurrentProcessId()
			<< " msg=" << request->MsgId
			<< " label=" << request->Label
			<< " allocationBase=0x" << HexAddress(request->AllocationBase)
			<< "\n";
	}

	uintptr_t cursor = request->AllocationBase;
	size_t regionIndex = 0;
	size_t dumpedRegions = 0;
	unsigned long long dumpedBytes = 0;
	for (size_t guard = 0; guard < 4096; ++guard) {
		MEMORY_BASIC_INFORMATION mbi = {};
		if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &mbi, sizeof(mbi))) {
			break;
		}

		uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
		uintptr_t allocationBase = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
		uintptr_t next = regionBase + mbi.RegionSize;
		if (allocationBase != request->AllocationBase) {
			if (regionBase > request->AllocationBase) {
				break;
			}
			cursor = next;
			continue;
		}

		bool dumpable = mbi.State == MEM_COMMIT && IsReadableMemoryProtect(mbi.Protect);
		std::string fileName;
		if (dumpable) {
			fileName = "region_"
				+ std::to_string(regionIndex)
				+ "_base_"
				+ HexAddress(regionBase)
				+ "_size_"
				+ HexAddress(static_cast<uintptr_t>(mbi.RegionSize))
				+ "_protect_"
				+ HexAddress(static_cast<uintptr_t>(mbi.Protect))
				+ ".bin";
			if (DumpMemoryRangeToFile(dir + fileName, regionBase, mbi.RegionSize)) {
				++dumpedRegions;
				dumpedBytes += mbi.RegionSize;
			}
		}

		if (map) {
			map << "region=" << regionIndex
				<< " base=0x" << HexAddress(regionBase)
				<< " size=0x" << HexAddress(static_cast<uintptr_t>(mbi.RegionSize))
				<< " state=0x" << HexAddress(static_cast<uintptr_t>(mbi.State))
				<< " protect=0x" << HexAddress(static_cast<uintptr_t>(mbi.Protect))
				<< " type=" << MemoryTypeName(mbi.Type)
				<< " dumped=" << (dumpable ? 1 : 0)
				<< " file=" << fileName
				<< "\n";
		}

		++regionIndex;
		if (next <= cursor) {
			break;
		}
		cursor = next;
	}

	if (map) {
		map << "summary dumpedRegions=" << dumpedRegions
			<< " dumpedBytes=0x" << HexAddress(static_cast<uintptr_t>(dumpedBytes))
			<< "\n\n";
	}
	HookLog("Network patch allocation dumped base=%p regions=%llu bytes=0x%llX dir=%s",
		reinterpret_cast<void*>(request->AllocationBase),
		static_cast<unsigned long long>(dumpedRegions),
		dumpedBytes,
		dir.c_str());
	return 0;
}

void StartPatchAllocationDump(uintptr_t address, unsigned short msgId, const char* label)
{
	if (!address) {
		return;
	}

	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))) {
		return;
	}
	if (mbi.Type != MEM_PRIVATE || mbi.State != MEM_COMMIT || !IsReadableMemoryProtect(mbi.Protect)) {
		return;
	}

	uintptr_t allocationBase = reinterpret_cast<uintptr_t>(mbi.AllocationBase);
	if (!MarkPatchAllocationDumped(allocationBase)) {
		return;
	}

	auto* request = new (std::nothrow) PatchAllocationDumpRequest();
	if (!request) {
		return;
	}
	request->AllocationBase = allocationBase;
	request->MsgId = msgId;
	snprintf(request->Label, sizeof(request->Label), "%s", label ? label : "");

	HookLog("Network patch allocation dump queued base=%p msg=%u label=%s region=%s",
		reinterpret_cast<void*>(allocationBase),
		static_cast<unsigned int>(msgId),
		request->Label,
		DescribeMemoryRegion(address).c_str());

	HANDLE thread = CreateThread(nullptr, 0, PatchAllocationDumpWorker, request, 0, nullptr);
	if (!thread) {
		HookLog("Network patch allocation dump thread create failed base=%p error=%lu",
			reinterpret_cast<void*>(allocationBase),
			GetLastError());
		delete request;
		return;
	}
	CloseHandle(thread);
}

bool MarkPatchTargetDumped(uintptr_t address)
{
	if (!address) {
		return false;
	}

	AcquireSRWLockExclusive(&g_dumpLock);
	for (size_t i = 0; i < g_dumpedPatchTargetAddressCount; ++i) {
		if (g_dumpedPatchTargetAddresses[i] == address) {
			ReleaseSRWLockExclusive(&g_dumpLock);
			return false;
		}
	}
	if (g_dumpedPatchTargetAddressCount >= _countof(g_dumpedPatchTargetAddresses)) {
		ReleaseSRWLockExclusive(&g_dumpLock);
		return false;
	}
	g_dumpedPatchTargetAddresses[g_dumpedPatchTargetAddressCount++] = address;
	ReleaseSRWLockExclusive(&g_dumpLock);
	return true;
}

void DumpPatchTargetMemory(uintptr_t address, unsigned short msgId, const char* label)
{
	if (!MarkPatchTargetDumped(address)) {
		return;
	}

	MEMORY_BASIC_INFORMATION mbi = {};
	if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof(mbi))) {
		return;
	}

	uintptr_t regionEnd = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
	size_t dumpSize = static_cast<size_t>((std::min<uintptr_t>)(0x4000, regionEnd > address ? regionEnd - address : 0));
	if (dumpSize == 0) {
		return;
	}

	std::vector<unsigned char> bytes(dumpSize);
	if (!SafeCopyRaw(bytes.data(), reinterpret_cast<const void*>(address), dumpSize)) {
		return;
	}

	char fileName[256] = { 0 };
	snprintf(fileName, sizeof(fileName),
		"patch_target_%lu_msg%u_%s_%p.bin",
		GetCurrentProcessId(),
		static_cast<unsigned int>(msgId),
		SanitizeFileNamePart(label).c_str(),
		reinterpret_cast<void*>(address));
	std::string path = GetPacketPayloadOutputDirectory() + fileName;
	std::ofstream file(path, std::ios::binary);
	if (file) {
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	}
	HookLog("Network patch target dumped label=%s msg=%u address=%p size=0x%llX path=%s region=%s",
		label ? label : "",
		static_cast<unsigned int>(msgId),
		reinterpret_cast<void*>(address),
		static_cast<unsigned long long>(bytes.size()),
		path.c_str(),
		DescribeMemoryRegion(address).c_str());
}

void TraceInnerDispatchSwitchState(const char* phase, unsigned short msgId)
{
	if (!g_networkBase || (msgId != 100 && msgId != 223)) {
		return;
	}

	uintptr_t entryAddress = g_networkBase + kInnerDispatchSmallMsgJumpTableRva + static_cast<uintptr_t>(msgId) * sizeof(unsigned int);
	unsigned int entryRva = ReadU32Safe(reinterpret_cast<const void*>(entryAddress));
	uintptr_t targetAddress = entryRva ? g_networkBase + entryRva : 0;
	uintptr_t hop1 = ResolveTrampolineTarget(targetAddress);
	uintptr_t hop2 = ResolveTrampolineTarget(hop1);
	uintptr_t hop3 = ResolveTrampolineTarget(hop2);
	uintptr_t hop4 = ResolveTrampolineTarget(hop3);
	bool entryMatchesDefaultReturn0 = entryRva == kInnerDispatchDefaultReturn0Rva;
	bool entryMatchesReturn1 = entryRva == kInnerDispatchReturn1Rva;

	DumpPatchTargetMemory(targetAddress, msgId, "switch_target");
	StartPatchAllocationDump(targetAddress, msgId, "switch_target");
	if (hop1) {
		DumpPatchTargetMemory(hop1, msgId, "trampoline_hop1");
		StartPatchAllocationDump(hop1, msgId, "trampoline_hop1");
	}
	if (hop2 && hop2 != hop1) {
		DumpPatchTargetMemory(hop2, msgId, "trampoline_hop2");
		StartPatchAllocationDump(hop2, msgId, "trampoline_hop2");
	}
	if (hop3 && hop3 != hop2 && hop3 != hop1) {
		DumpPatchTargetMemory(hop3, msgId, "trampoline_hop3");
		StartPatchAllocationDump(hop3, msgId, "trampoline_hop3");
	}
	if (hop4 && hop4 != hop3 && hop4 != hop2 && hop4 != hop1) {
		DumpPatchTargetMemory(hop4, msgId, "trampoline_hop4");
		StartPatchAllocationDump(hop4, msgId, "trampoline_hop4");
	}

	AcquireSRWLockExclusive(&g_dumpLock);
	std::string path = GetPacketDumpOutputDirectory() + "network_patch_watch_" + std::to_string(GetCurrentProcessId()) + ".log";
	std::ofstream file(path, std::ios::app);
	if (file) {
		file << "[" << GetTickCount64() << "]"
			<< " tid=" << GetCurrentThreadId()
			<< " phase=" << phase
			<< " msg=" << msgId
			<< " entryAddress=" << reinterpret_cast<void*>(entryAddress)
			<< " entryRva=0x" << std::hex << entryRva
			<< " expectedDefaultRva=0x" << kInnerDispatchDefaultReturn0Rva
			<< " return1Rva=0x" << kInnerDispatchReturn1Rva
			<< std::dec
			<< " entryMatchesDefaultReturn0=" << (entryMatchesDefaultReturn0 ? 1 : 0)
			<< " entryMatchesReturn1=" << (entryMatchesReturn1 ? 1 : 0)
			<< " target=" << reinterpret_cast<void*>(targetAddress)
			<< " targetRegion=\"" << DescribeMemoryRegion(targetAddress) << "\""
			<< " hop1=" << reinterpret_cast<void*>(hop1)
			<< " hop1Region=\"" << DescribeMemoryRegion(hop1) << "\""
			<< " hop2=" << reinterpret_cast<void*>(hop2)
			<< " hop2Region=\"" << DescribeMemoryRegion(hop2) << "\""
			<< " hop3=" << reinterpret_cast<void*>(hop3)
			<< " hop3Region=\"" << DescribeMemoryRegion(hop3) << "\""
			<< " hop4=" << reinterpret_cast<void*>(hop4)
			<< " hop4Region=\"" << DescribeMemoryRegion(hop4) << "\""
			<< " entryBytes=" << ReadMemoryHex(entryAddress, 16)
			<< " switchEntryBytes=" << ReadMemoryHex(g_networkBase + kInnerDispatchSwitchEntryRva, 64)
			<< " defaultReturn0Bytes=" << ReadMemoryHex(g_networkBase + kInnerDispatchDefaultReturn0Rva, 64)
			<< " return1Bytes=" << ReadMemoryHex(g_networkBase + kInnerDispatchReturn1Rva, 32)
			<< " targetBytes=" << (targetAddress ? ReadMemoryHex(targetAddress, 64) : "<null>")
			<< " hop1Bytes=" << (hop1 ? ReadMemoryHex(hop1, 128) : "<null>")
			<< " hop2Bytes=" << (hop2 ? ReadMemoryHex(hop2, 128) : "<null>")
			<< " hop3Bytes=" << (hop3 ? ReadMemoryHex(hop3, 128) : "<null>")
			<< " hop4Bytes=" << (hop4 ? ReadMemoryHex(hop4, 128) : "<null>")
			<< "\n";
	}
	ReleaseSRWLockExclusive(&g_dumpLock);
}

bool ComputeFileMd5(const wchar_t* path, std::string& md5)
{
	HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) {
		HookLog("Network.dll file md5 open failed error=%lu", GetLastError());
		return false;
	}

	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	bool ok = false;

	if (CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) &&
		CryptCreateHash(provider, CALG_MD5, 0, 0, &hash))
	{
		BYTE buffer[0x4000];
		DWORD readBytes = 0;
		ok = true;
		while (true) {
			if (!ReadFile(file, buffer, sizeof(buffer), &readBytes, nullptr)) {
				HookLog("Network.dll file md5 read failed error=%lu", GetLastError());
				ok = false;
				break;
			}
			if (readBytes == 0) {
				break;
			}
			if (!CryptHashData(hash, buffer, readBytes, 0)) {
				HookLog("Network.dll file md5 hash update failed error=%lu", GetLastError());
				ok = false;
				break;
			}
		}

		if (ok) {
			BYTE digest[16] = { 0 };
			DWORD digestSize = sizeof(digest);
			ok = !!CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0);
			if (ok) {
				md5 = BytesToHex(digest, digestSize);
			}
			else {
				HookLog("Network.dll file md5 finalize failed error=%lu", GetLastError());
			}
		}
	}
	else {
		HookLog("Network.dll file md5 crypto init failed error=%lu", GetLastError());
	}

	if (hash) {
		CryptDestroyHash(hash);
	}
	if (provider) {
		CryptReleaseContext(provider, 0);
	}
	CloseHandle(file);
	return ok;
}

size_t GetModuleImageSize(HMODULE module)
{
	auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
	if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return 0;
	}

	auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
		reinterpret_cast<const unsigned char*>(module) + dos->e_lfanew);
	if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) {
		return 0;
	}

	return nt->OptionalHeader.SizeOfImage;
}

std::string GetModuleMd5(HMODULE module)
{
	wchar_t path[MAX_PATH] = { 0 };
	DWORD len = GetModuleFileNameW(module, path, MAX_PATH);
	if (len == 0 || len == MAX_PATH) {
		HookLog("Network.dll GetModuleFileNameW failed len=%lu error=%lu module=%p", len, GetLastError(), module);
		return "";
	}

	std::string md5;
	if (!ComputeFileMd5(path, md5)) {
		HookLog("Network.dll file md5 failed path=%ls", path);
		return "";
	}
	HookLog("Network.dll file md5 path=%ls md5=%s", path, md5.c_str());
	return md5;
}

bool GetModuleTextRange(HMODULE module, uintptr_t& textRva, size_t& textSize)
{
	textRva = 0;
	textSize = 0;
	auto base = reinterpret_cast<const unsigned char*>(module);
	auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return false;
	}

	auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) {
		return false;
	}

	auto section = IMAGE_FIRST_SECTION(nt);
	for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
		char name[9] = {};
		memcpy(name, section[i].Name, 8);
		if (strcmp(name, ".text") == 0) {
			textRva = section[i].VirtualAddress;
			textSize = section[i].Misc.VirtualSize ? section[i].Misc.VirtualSize : section[i].SizeOfRawData;
			return textRva != 0 && textSize >= 5;
		}
	}
	return false;
}

void AddUniqueRva(std::vector<uintptr_t>& rvas, uintptr_t rva)
{
	if (rva && std::find(rvas.begin(), rvas.end(), rva) == rvas.end()) {
		rvas.push_back(rva);
	}
}

template <size_t Count>
void AssignFallbackCallSites(std::vector<uintptr_t>& dst, const uintptr_t(&src)[Count])
{
	if (dst.empty()) {
		dst.assign(src, src + Count);
	}
}

template <size_t Count>
bool MatchPattern(HMODULE module, uintptr_t rva, const int(&pattern)[Count])
{
	uintptr_t base = reinterpret_cast<uintptr_t>(module);
	for (size_t i = 0; i < Count; ++i) {
		if (pattern[i] < 0) {
			continue;
		}

		unsigned char value = 0;
		if (!SafeCopyRaw(&value, reinterpret_cast<const void*>(base + rva + i), sizeof(value)) ||
			value != static_cast<unsigned char>(pattern[i]))
		{
			return false;
		}
	}
	return true;
}

bool LooksLikeReadableCode(HMODULE module, uintptr_t rva)
{
	unsigned char bytes[8] = {};
	uintptr_t base = reinterpret_cast<uintptr_t>(module);
	if (!rva || !SafeCopyRaw(bytes, reinterpret_cast<const void*>(base + rva), sizeof(bytes))) {
		return false;
	}
	for (unsigned char b : bytes) {
		if (b != 0x00 && b != 0xCC) {
			return true;
		}
	}
	return false;
}

template <size_t Count>
uintptr_t ResolveTargetByPattern(
	HMODULE module,
	uintptr_t textRva,
	size_t textSize,
	const int(&pattern)[Count],
	uintptr_t fallbackRva,
	const char* name,
	bool& dynamic)
{
	dynamic = false;
	std::vector<uintptr_t> matches;
	for (size_t offset = 0; offset + Count <= textSize; ++offset) {
		uintptr_t rva = textRva + offset;
		if (MatchPattern(module, rva, pattern)) {
			AddUniqueRva(matches, rva);
		}
	}

	if (matches.size() == 1) {
		dynamic = true;
		HookLog("Network layout target %s source=AOB rva=0x%llX",
			name,
			static_cast<unsigned long long>(matches[0]));
		return matches[0];
	}

	if (LooksLikeReadableCode(module, fallbackRva)) {
		HookLog("Network layout target %s AOB matches=%llu; using fallback rva=0x%llX",
			name,
			static_cast<unsigned long long>(matches.size()),
			static_cast<unsigned long long>(fallbackRva));
		return fallbackRva;
	}

	HookLog("Network layout target %s unresolved AOB matches=%llu fallback=0x%llX unreadable",
		name,
		static_cast<unsigned long long>(matches.size()),
		static_cast<unsigned long long>(fallbackRva));
	return 0;
}

std::vector<uintptr_t> ScanRel32CallSites(HMODULE module, uintptr_t textRva, size_t textSize, uintptr_t targetRva, const char* name)
{
	std::vector<uintptr_t> callSites;
	if (!targetRva) {
		return callSites;
	}

	uintptr_t base = reinterpret_cast<uintptr_t>(module);
	const unsigned char* text = reinterpret_cast<const unsigned char*>(base + textRva);
	for (size_t offset = 0; offset + 5 <= textSize; ++offset) {
		unsigned char opcode = 0;
		if (!SafeCopyRaw(&opcode, text + offset, sizeof(opcode)) || opcode != 0xE8) {
			continue;
		}

		int32_t rel = 0;
		if (!SafeCopyRaw(&rel, text + offset + 1, sizeof(rel))) {
			continue;
		}

		uintptr_t callRva = textRva + offset;
		uintptr_t target = base + callRva + 5 + rel;
		if (target >= base && target - base == targetRva) {
			AddUniqueRva(callSites, callRva);
		}
	}

	std::sort(callSites.begin(), callSites.end());
	HookLog("Network layout callsites %s target=0x%llX count=%llu",
		name,
		static_cast<unsigned long long>(targetRva),
		static_cast<unsigned long long>(callSites.size()));
	return callSites;
}

void LogResolvedCallSites(const char* name, const std::vector<uintptr_t>& rvas)
{
	std::string line = "Network layout ";
	line += name;
	line += " callsites:";
	for (uintptr_t rva : rvas) {
		char item[32] = {};
		snprintf(item, sizeof(item), " 0x%llX", static_cast<unsigned long long>(rva));
		line += item;
	}
	HookLog("%s", line.c_str());
}

void KeepFirstCallSites(std::vector<uintptr_t>& rvas, size_t keepCount, const char* name)
{
	if (rvas.size() <= keepCount) {
		return;
	}
	HookLog("Network layout %s callsites trimmed from %llu to %llu",
		name,
		static_cast<unsigned long long>(rvas.size()),
		static_cast<unsigned long long>(keepCount));
	rvas.resize(keepCount);
}

template <size_t Count>
void AssignExactCallSites(std::vector<uintptr_t>& dst, const uintptr_t(&src)[Count])
{
	dst.assign(src, src + Count);
}

bool IsKnown21446Network(size_t imageSize, const std::string& md5)
{
	return imageSize == kKnown21446ImageSize &&
		!md5.empty() &&
		_stricmp(md5.c_str(), kKnown21446Md5) == 0;
}

bool IsKnown8fdNetwork(size_t imageSize, const std::string& md5)
{
	return imageSize == kKnown8fdImageSize &&
		!md5.empty() &&
		_stricmp(md5.c_str(), kKnown8fdMd5) == 0;
}

bool IsKnownCe97Network(size_t imageSize, const std::string& md5)
{
	return imageSize == kKnownCe97ImageSize &&
		!md5.empty() &&
		_stricmp(md5.c_str(), kKnownCe97Md5) == 0;
}

bool IsKnown1642Network(size_t imageSize, const std::string& md5)
{
	return imageSize == kKnown1642ImageSize &&
		!md5.empty() &&
		_stricmp(md5.c_str(), kKnown1642Md5) == 0;
}

bool IsKnown0679Network(size_t imageSize, const std::string& md5)
{
	return imageSize == kKnown0679ImageSize &&
		!md5.empty() &&
		_stricmp(md5.c_str(), kKnown0679Md5) == 0;
}

void ApplyKnown21446Layout(NetworkHookLayout& layout)
{
	layout.unpackRva = kUnpackRva21446;
	layout.packRva = kPackRva21446;
	layout.specialDecodeRva = kSpecialDecodeRva21446;
	layout.innerDispatchRva = kInnerDispatchRva21446;
	layout.parserQueueDispatchRva = kParserQueueDispatchRva21446;
	layout.serializeBodyRva = kSerializeBodyRva21446;
	layout.safePacketBuilderRva = kSafePacketBuilderRva21446;
	layout.loginCacheDecodeRva = 0;
	AssignExactCallSites(layout.unpackCallRvas, kUnpackCallRvas21446);
	AssignExactCallSites(layout.packCallRvas, kPackCallRvas21446);
	AssignExactCallSites(layout.specialDecodeCallRvas, kSpecialDecodeCallRvas21446);
	AssignExactCallSites(layout.innerDispatchCallRvas, kInnerDispatchCallRvas21446);
	AssignExactCallSites(layout.parserQueueDispatchCallRvas, kParserQueueDispatchCallRvas21446);
	AssignExactCallSites(layout.serializeBodyCallRvas, kSerializeBodyCallRvas21446);
	AssignExactCallSites(layout.safePacketBuilderCallRvas, kSafePacketBuilderCallRvas21446);
	layout.loginCacheDecodeCallRvas.clear();
}

void ApplyKnown8fdLayout(NetworkHookLayout& layout)
{
	layout.unpackRva = kUnpackRva8fd;
	layout.packRva = kPackRva8fd;
	layout.specialDecodeRva = kSpecialDecodeRva8fd;
	layout.innerDispatchRva = kInnerDispatchRva8fd;
	layout.parserQueueDispatchRva = kParserQueueDispatchRva8fd;
	layout.serializeBodyRva = kSerializeBodyRva8fd;
	layout.safePacketBuilderRva = kSafePacketBuilderRva8fd;
	layout.loginCacheDecodeRva = 0;
	AssignExactCallSites(layout.unpackCallRvas, kUnpackCallRvas8fd);
	AssignExactCallSites(layout.packCallRvas, kPackCallRvas8fd);
	AssignExactCallSites(layout.specialDecodeCallRvas, kSpecialDecodeCallRvas8fd);
	AssignExactCallSites(layout.innerDispatchCallRvas, kInnerDispatchCallRvas8fd);
	AssignExactCallSites(layout.parserQueueDispatchCallRvas, kParserQueueDispatchCallRvas8fd);
	AssignExactCallSites(layout.serializeBodyCallRvas, kSerializeBodyCallRvas8fd);
	AssignExactCallSites(layout.safePacketBuilderCallRvas, kSafePacketBuilderCallRvas8fd);
	layout.loginCacheDecodeCallRvas.clear();
}

void ApplyKnownCe97Layout(NetworkHookLayout& layout)
{
	layout.unpackRva = kUnpackRvaCe97;
	layout.packRva = kPackRvaCe97;
	layout.specialDecodeRva = kSpecialDecodeRvaCe97;
	layout.innerDispatchRva = kInnerDispatchRvaCe97;
	layout.parserQueueDispatchRva = kParserQueueDispatchRvaCe97;
	layout.serializeBodyRva = kSerializeBodyRvaCe97;
	layout.safePacketBuilderRva = kSafePacketBuilderRvaCe97;
	layout.loginCacheDecodeRva = 0;
	AssignExactCallSites(layout.unpackCallRvas, kUnpackCallRvasCe97);
	AssignExactCallSites(layout.packCallRvas, kPackCallRvasCe97);
	AssignExactCallSites(layout.specialDecodeCallRvas, kSpecialDecodeCallRvasCe97);
	AssignExactCallSites(layout.innerDispatchCallRvas, kInnerDispatchCallRvasCe97);
	AssignExactCallSites(layout.parserQueueDispatchCallRvas, kParserQueueDispatchCallRvasCe97);
	AssignExactCallSites(layout.serializeBodyCallRvas, kSerializeBodyCallRvasCe97);
	AssignExactCallSites(layout.safePacketBuilderCallRvas, kSafePacketBuilderCallRvasCe97);
	layout.loginCacheDecodeCallRvas.clear();
}

void ApplyKnown1642Layout(NetworkHookLayout& layout)
{
	layout.unpackRva = kUnpackRva1642;
	layout.packRva = kPackRva1642;
	layout.specialDecodeRva = kSpecialDecodeRva1642;
	layout.innerDispatchRva = kInnerDispatchRva1642;
	layout.parserQueueDispatchRva = kParserQueueDispatchRva1642;
	layout.serializeBodyRva = kSerializeBodyRva1642;
	layout.safePacketBuilderRva = kSafePacketBuilderRva1642;
	layout.loginCacheDecodeRva = 0;
	AssignExactCallSites(layout.unpackCallRvas, kUnpackCallRvas1642);
	AssignExactCallSites(layout.packCallRvas, kPackCallRvas1642);
	AssignExactCallSites(layout.specialDecodeCallRvas, kSpecialDecodeCallRvas1642);
	AssignExactCallSites(layout.innerDispatchCallRvas, kInnerDispatchCallRvas1642);
	AssignExactCallSites(layout.parserQueueDispatchCallRvas, kParserQueueDispatchCallRvas1642);
	AssignExactCallSites(layout.serializeBodyCallRvas, kSerializeBodyCallRvas1642);
	AssignExactCallSites(layout.safePacketBuilderCallRvas, kSafePacketBuilderCallRvas1642);
	layout.loginCacheDecodeCallRvas.clear();
	layout.minimalSafeCapture = true;
}

void ApplyKnown0679Layout(NetworkHookLayout& layout)
{
	layout.unpackRva = kUnpackRva0679;
	layout.packRva = kPackRva0679;
	layout.specialDecodeRva = kSpecialDecodeRva0679;
	layout.innerDispatchRva = kInnerDispatchRva0679;
	layout.parserQueueDispatchRva = kParserQueueDispatchRva0679;
	layout.serializeBodyRva = kSerializeBodyRva0679;
	layout.safePacketBuilderRva = kSafePacketBuilderRva0679;
	layout.loginCacheDecodeRva = 0;
	AssignExactCallSites(layout.unpackCallRvas, kUnpackCallRvas0679);
	AssignExactCallSites(layout.packCallRvas, kPackCallRvas0679);
	AssignExactCallSites(layout.specialDecodeCallRvas, kSpecialDecodeCallRvas0679);
	AssignExactCallSites(layout.innerDispatchCallRvas, kInnerDispatchCallRvas0679);
	AssignExactCallSites(layout.parserQueueDispatchCallRvas, kParserQueueDispatchCallRvas0679);
	AssignExactCallSites(layout.serializeBodyCallRvas, kSerializeBodyCallRvas0679);
	AssignExactCallSites(layout.safePacketBuilderCallRvas, kSafePacketBuilderCallRvas0679);
	layout.loginCacheDecodeCallRvas.clear();
	layout.minimalSafeCapture = true;
}

NetworkHookLayout ResolveNetworkHookLayout(HMODULE networkModule, const std::string& md5)
{
	NetworkHookLayout layout = {};
	size_t imageSize = GetModuleImageSize(networkModule);
	uintptr_t textRva = 0;
	size_t textSize = 0;

	HookLog("Network layout resolve start imageSize=0x%llX md5=%s known0679=%s known1642=%s knownCe97=%s known8fd=%s known21446=%s knownLegacy=%s knownBd243=%s known7707=%s",
		static_cast<unsigned long long>(imageSize),
		md5.empty() ? "<unavailable>" : md5.c_str(),
		kKnown0679Md5,
		kKnown1642Md5,
		kKnownCe97Md5,
		kKnown8fdMd5,
		kKnown21446Md5,
		kKnownMd5,
		"bd24352812012d52249365d5fce5d64e",
		"7707ae852fb354782294d3c09b8d5d33");

	if (!GetModuleTextRange(networkModule, textRva, textSize)) {
		HookLog("Network layout resolve failed: .text not found");
		return layout;
	}

	if (IsKnown0679Network(imageSize, md5)) {
		ApplyKnown0679Layout(layout);
		layout.resolvedFromRuntime = true;
		layout.coreReady = true;
		HookLog("Network layout source=known-0679");
		LogResolvedCallSites("Unpack", layout.unpackCallRvas);
		LogResolvedCallSites("Pack", layout.packCallRvas);
		LogResolvedCallSites("SpecialDecode", layout.specialDecodeCallRvas);
		LogResolvedCallSites("InnerDispatch", layout.innerDispatchCallRvas);
		LogResolvedCallSites("ParserQueueDispatch", layout.parserQueueDispatchCallRvas);
		LogResolvedCallSites("SerializeBody", layout.serializeBodyCallRvas);
		LogResolvedCallSites("SafePacketBuilder", layout.safePacketBuilderCallRvas);
		LogResolvedCallSites("LoginCacheDecode", layout.loginCacheDecodeCallRvas);
		HookLog("Network layout resolve done source=known-0679 coreReady=%d", layout.coreReady ? 1 : 0);
		return layout;
	}

	if (IsKnown1642Network(imageSize, md5)) {
		ApplyKnown1642Layout(layout);
		layout.resolvedFromRuntime = true;
		layout.coreReady = true;
		HookLog("Network layout source=known-1642");
		LogResolvedCallSites("Unpack", layout.unpackCallRvas);
		LogResolvedCallSites("Pack", layout.packCallRvas);
		LogResolvedCallSites("SpecialDecode", layout.specialDecodeCallRvas);
		LogResolvedCallSites("InnerDispatch", layout.innerDispatchCallRvas);
		LogResolvedCallSites("ParserQueueDispatch", layout.parserQueueDispatchCallRvas);
		LogResolvedCallSites("SerializeBody", layout.serializeBodyCallRvas);
		LogResolvedCallSites("SafePacketBuilder", layout.safePacketBuilderCallRvas);
		LogResolvedCallSites("LoginCacheDecode", layout.loginCacheDecodeCallRvas);
		HookLog("Network layout resolve done source=known-1642 coreReady=%d", layout.coreReady ? 1 : 0);
		return layout;
	}

	if (IsKnownCe97Network(imageSize, md5)) {
		ApplyKnownCe97Layout(layout);
		layout.resolvedFromRuntime = true;
		layout.coreReady = true;
		HookLog("Network layout source=known-ce97");
		LogResolvedCallSites("Unpack", layout.unpackCallRvas);
		LogResolvedCallSites("Pack", layout.packCallRvas);
		LogResolvedCallSites("SpecialDecode", layout.specialDecodeCallRvas);
		LogResolvedCallSites("InnerDispatch", layout.innerDispatchCallRvas);
		LogResolvedCallSites("ParserQueueDispatch", layout.parserQueueDispatchCallRvas);
		LogResolvedCallSites("SerializeBody", layout.serializeBodyCallRvas);
		LogResolvedCallSites("SafePacketBuilder", layout.safePacketBuilderCallRvas);
		LogResolvedCallSites("LoginCacheDecode", layout.loginCacheDecodeCallRvas);
		HookLog("Network layout resolve done source=known-ce97 coreReady=%d", layout.coreReady ? 1 : 0);
		return layout;
	}

	if (IsKnown8fdNetwork(imageSize, md5)) {
		ApplyKnown8fdLayout(layout);
		layout.resolvedFromRuntime = true;
		layout.coreReady = true;
		HookLog("Network layout source=known-8fd");
		LogResolvedCallSites("Unpack", layout.unpackCallRvas);
		LogResolvedCallSites("Pack", layout.packCallRvas);
		LogResolvedCallSites("SpecialDecode", layout.specialDecodeCallRvas);
		LogResolvedCallSites("InnerDispatch", layout.innerDispatchCallRvas);
		LogResolvedCallSites("ParserQueueDispatch", layout.parserQueueDispatchCallRvas);
		LogResolvedCallSites("SerializeBody", layout.serializeBodyCallRvas);
		LogResolvedCallSites("SafePacketBuilder", layout.safePacketBuilderCallRvas);
		LogResolvedCallSites("LoginCacheDecode", layout.loginCacheDecodeCallRvas);
		HookLog("Network layout resolve done source=known-8fd coreReady=%d", layout.coreReady ? 1 : 0);
		return layout;
	}

	if (IsKnown21446Network(imageSize, md5)) {
		ApplyKnown21446Layout(layout);
		layout.resolvedFromRuntime = true;
		layout.coreReady = true;
		HookLog("Network layout source=known-21446");
		LogResolvedCallSites("Unpack", layout.unpackCallRvas);
		LogResolvedCallSites("Pack", layout.packCallRvas);
		LogResolvedCallSites("SpecialDecode", layout.specialDecodeCallRvas);
		LogResolvedCallSites("InnerDispatch", layout.innerDispatchCallRvas);
		LogResolvedCallSites("ParserQueueDispatch", layout.parserQueueDispatchCallRvas);
		LogResolvedCallSites("SerializeBody", layout.serializeBodyCallRvas);
		LogResolvedCallSites("SafePacketBuilder", layout.safePacketBuilderCallRvas);
		LogResolvedCallSites("LoginCacheDecode", layout.loginCacheDecodeCallRvas);
		HookLog("Network layout resolve done source=known-21446 coreReady=%d", layout.coreReady ? 1 : 0);
		return layout;
	}

	bool dynamicUnpack = false;
	bool dynamicPack = false;
	bool dynamicSpecialDecode = false;
	bool dynamicInnerDispatch = false;
	bool dynamicParserQueueDispatch = false;
	bool dynamicSerializeBody = false;
	bool dynamicSafePacketBuilder = false;
	bool dynamicLoginCacheDecode = false;

	layout.unpackRva = ResolveTargetByPattern(networkModule, textRva, textSize, kUnpackFunctionSignature, kUnpackRva, "Unpack", dynamicUnpack);
	layout.packRva = ResolveTargetByPattern(networkModule, textRva, textSize, kPackFunctionSignature, kPackRva, "Pack", dynamicPack);
	layout.specialDecodeRva = ResolveTargetByPattern(networkModule, textRva, textSize, kSpecialDecodeFunctionSignature, kSpecialDecodeRva, "SpecialDecode", dynamicSpecialDecode);
	layout.innerDispatchRva = ResolveTargetByPattern(networkModule, textRva, textSize, kInnerDispatchFunctionSignature, kInnerDispatchRva, "InnerDispatch", dynamicInnerDispatch);
	layout.parserQueueDispatchRva = ResolveTargetByPattern(networkModule, textRva, textSize, kParserQueueDispatchFunctionSignature, kParserQueueDispatchRva, "ParserQueueDispatch", dynamicParserQueueDispatch);
	layout.serializeBodyRva = ResolveTargetByPattern(networkModule, textRva, textSize, kSerializeBodyFunctionSignature, kSerializeBodyRva, "SerializeBody", dynamicSerializeBody);
	layout.safePacketBuilderRva = ResolveTargetByPattern(networkModule, textRva, textSize, kSafePacketBuilderFunctionSignature, kSafePacketBuilderRva, "SafePacketBuilder", dynamicSafePacketBuilder);
	layout.loginCacheDecodeRva = ResolveTargetByPattern(networkModule, textRva, textSize, kLoginCacheDecodeFunctionSignature, kLoginCacheDecodeRva, "LoginCacheDecode", dynamicLoginCacheDecode);

	const bool allowLegacyFallback =
		(imageSize == kKnownImageSize && _stricmp(md5.c_str(), kKnownMd5) == 0) ||
		(imageSize == 0x751000 && _stricmp(md5.c_str(), "bd24352812012d52249365d5fce5d64e") == 0) ||
		(imageSize == 0x752000 && _stricmp(md5.c_str(), "7707ae852fb354782294d3c09b8d5d33") == 0);
	if (!allowLegacyFallback) {
		if (!dynamicUnpack) layout.unpackRva = 0;
		if (!dynamicPack) layout.packRva = 0;
		if (!dynamicSpecialDecode) layout.specialDecodeRva = 0;
		if (!dynamicInnerDispatch) layout.innerDispatchRva = 0;
		if (!dynamicParserQueueDispatch) layout.parserQueueDispatchRva = 0;
		if (!dynamicSerializeBody) layout.serializeBodyRva = 0;
		if (!dynamicSafePacketBuilder) layout.safePacketBuilderRva = 0;
		if (!dynamicLoginCacheDecode) layout.loginCacheDecodeRva = 0;
		HookLog("Network layout legacy RVA fallback disabled for unknown md5/image");
	}

	layout.unpackCallRvas = ScanRel32CallSites(networkModule, textRva, textSize, layout.unpackRva, "Unpack");
	layout.packCallRvas = ScanRel32CallSites(networkModule, textRva, textSize, layout.packRva, "Pack");
	layout.specialDecodeCallRvas = ScanRel32CallSites(networkModule, textRva, textSize, layout.specialDecodeRva, "SpecialDecode");
	layout.innerDispatchCallRvas = ScanRel32CallSites(networkModule, textRva, textSize, layout.innerDispatchRva, "InnerDispatch");
	layout.parserQueueDispatchCallRvas = ScanRel32CallSites(networkModule, textRva, textSize, layout.parserQueueDispatchRva, "ParserQueueDispatch");
	layout.serializeBodyCallRvas = ScanRel32CallSites(networkModule, textRva, textSize, layout.serializeBodyRva, "SerializeBody");
	layout.safePacketBuilderCallRvas = ScanRel32CallSites(networkModule, textRva, textSize, layout.safePacketBuilderRva, "SafePacketBuilder");
	layout.loginCacheDecodeCallRvas = ScanRel32CallSites(networkModule, textRva, textSize, layout.loginCacheDecodeRva, "LoginCacheDecode");

	KeepFirstCallSites(layout.innerDispatchCallRvas, 2, "InnerDispatch");
	KeepFirstCallSites(layout.serializeBodyCallRvas, 1, "SerializeBody");
	if (imageSize == kKnownImageSize) {
		AssignExactCallSites(layout.loginCacheDecodeCallRvas, kLoginCacheDecodeCallRvas);
		HookLog("Network layout LoginCacheDecode callsites source=known-3805");
	}
	else if (imageSize == 0x752000) {
		AssignExactCallSites(layout.loginCacheDecodeCallRvas, kLoginCacheDecodeCallRvas7707);
		HookLog("Network layout LoginCacheDecode callsites source=known-7707");
	}
	else if (imageSize == 0x751000) {
		AssignExactCallSites(layout.loginCacheDecodeCallRvas, kLoginCacheDecodeCallRvasBd243);
		HookLog("Network layout LoginCacheDecode callsites source=known-bd243");
	}
	else {
		layout.loginCacheDecodeCallRvas.clear();
		HookLog("Network layout LoginCacheDecode callsites skipped for unknown imageSize=0x%llX",
			static_cast<unsigned long long>(imageSize));
	}

	if (allowLegacyFallback) {
		AssignFallbackCallSites(layout.unpackCallRvas, kUnpackCallRvas);
		AssignFallbackCallSites(layout.packCallRvas, kPackCallRvas);
		AssignFallbackCallSites(layout.specialDecodeCallRvas, kSpecialDecodeCallRvas);
		AssignFallbackCallSites(layout.innerDispatchCallRvas, kInnerDispatchCallRvas);
		AssignFallbackCallSites(layout.parserQueueDispatchCallRvas, kParserQueueDispatchCallRvas);
		AssignFallbackCallSites(layout.serializeBodyCallRvas, kSerializeBodyCallRvas);
		AssignFallbackCallSites(layout.safePacketBuilderCallRvas, kSafePacketBuilderCallRvas);
	}

	layout.resolvedFromRuntime =
		dynamicUnpack || dynamicPack || dynamicSpecialDecode || dynamicInnerDispatch ||
		dynamicParserQueueDispatch || dynamicSerializeBody || dynamicSafePacketBuilder || dynamicLoginCacheDecode;
	layout.coreReady =
		layout.unpackRva && layout.packRva && layout.specialDecodeRva && layout.innerDispatchRva &&
		!layout.unpackCallRvas.empty() && !layout.packCallRvas.empty() &&
		!layout.specialDecodeCallRvas.empty() && !layout.innerDispatchCallRvas.empty();

	LogResolvedCallSites("Unpack", layout.unpackCallRvas);
	LogResolvedCallSites("Pack", layout.packCallRvas);
	LogResolvedCallSites("SpecialDecode", layout.specialDecodeCallRvas);
	LogResolvedCallSites("InnerDispatch", layout.innerDispatchCallRvas);
	LogResolvedCallSites("ParserQueueDispatch", layout.parserQueueDispatchCallRvas);
	LogResolvedCallSites("SerializeBody", layout.serializeBodyCallRvas);
	LogResolvedCallSites("SafePacketBuilder", layout.safePacketBuilderCallRvas);
	LogResolvedCallSites("LoginCacheDecode", layout.loginCacheDecodeCallRvas);
	HookLog("Network layout resolve done source=%s coreReady=%d",
		layout.resolvedFromRuntime ? "dynamic" : "fallback",
		layout.coreReady ? 1 : 0);
	return layout;
}

std::string BoundedAscii(const char* data, size_t capacity, int explicitLen = -1)
{
	if (!data || capacity == 0) {
		return "";
	}

	size_t count = 0;
	if (explicitLen >= 0) {
		count = std::min<size_t>(static_cast<size_t>(explicitLen), capacity);
	}
	else {
		count = strnlen_s(data, capacity);
	}

	std::string text;
	text.reserve(count);
	for (size_t i = 0; i < count; ++i) {
		unsigned char ch = static_cast<unsigned char>(data[i]);
		text.push_back(ch >= 0x20 && ch < 0x7F ? static_cast<char>(ch) : '.');
	}
	return text;
}

const wchar_t* SkipCommandLineArg(const wchar_t* p)
{
	while (*p == L' ' || *p == L'\t') {
		++p;
	}
	if (*p == L'"') {
		++p;
		while (*p && *p != L'"') {
			++p;
		}
		if (*p == L'"') {
			++p;
		}
		return p;
	}
	while (*p && *p != L' ' && *p != L'\t') {
		++p;
	}
	return p;
}

DWORD ParseLauncherPidFromCommandLine()
{
	const wchar_t* cmd = GetCommandLineW();
	if (!cmd || !cmd[0]) {
		return 0;
	}

	const wchar_t* p = SkipCommandLineArg(cmd);
	while (*p == L' ' || *p == L'\t') {
		++p;
	}
	if (*p == L'"') {
		++p;
	}

	wchar_t* end = nullptr;
	unsigned long value = wcstoul(p, &end, 10);
	if (end == p || value == 0 || value > 0xFFFFFFFFUL) {
		HookLog("Launcher shared data pid parse failed cmd=%ls", cmd);
		return 0;
	}
	return static_cast<DWORD>(value);
}

struct SharedMapCandidate
{
	std::string name;
	DWORD pid = 0;
	std::string reason;
};

void AddSharedMapCandidate(std::vector<SharedMapCandidate>& candidates, const std::string& name, DWORD pid, const char* reason)
{
	for (const auto& existing : candidates) {
		if (existing.name == name) {
			return;
		}
	}
	candidates.push_back({ name, pid, reason ? reason : "" });
}

void AddSharedMapPidCandidates(std::vector<SharedMapCandidate>& candidates, DWORD pid, const char* reason)
{
	if (!pid) {
		return;
	}

	char name[96] = { 0 };
	snprintf(name, sizeof(name), "KartMap_Shared_%lu", pid);
	AddSharedMapCandidate(candidates, name, pid, reason);
	snprintf(name, sizeof(name), "Local\\KartMap_Shared_%lu", pid);
	AddSharedMapCandidate(candidates, name, pid, reason);
	snprintf(name, sizeof(name), "Global\\KartMap_Shared_%lu", pid);
	AddSharedMapCandidate(candidates, name, pid, reason);
	snprintf(name, sizeof(name), "ZingSpeedMap_Shared_%lu", pid);
	AddSharedMapCandidate(candidates, name, pid, reason);
	snprintf(name, sizeof(name), "Local\\ZingSpeedMap_Shared_%lu", pid);
	AddSharedMapCandidate(candidates, name, pid, reason);
	snprintf(name, sizeof(name), "Global\\ZingSpeedMap_Shared_%lu", pid);
	AddSharedMapCandidate(candidates, name, pid, reason);
}

DWORD GetParentProcessId(DWORD pid)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		return 0;
	}

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	DWORD parentPid = 0;
	if (Process32FirstW(snapshot, &entry)) {
		do {
			if (entry.th32ProcessID == pid) {
				parentPid = entry.th32ParentProcessID;
				break;
			}
		} while (Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
	return parentPid;
}

void AddAllProcessSharedMapCandidates(std::vector<SharedMapCandidate>& candidates)
{
	HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snapshot == INVALID_HANDLE_VALUE) {
		HookLog("Launcher shared data process snapshot failed error=%lu", GetLastError());
		return;
	}

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	if (Process32FirstW(snapshot, &entry)) {
		do {
			AddSharedMapPidCandidates(candidates, entry.th32ProcessID, "process-scan");
		} while (Process32NextW(snapshot, &entry));
	}
	CloseHandle(snapshot);
}

bool LooksLikeLauncherSharedData(const LauncherSharedData& snapshot)
{
	if (snapshot.Uin != 0 && snapshot.WorldID < 1000 &&
		snapshot.SignKeyLen >= 0 && snapshot.SignKeyLen <= static_cast<int>(sizeof(snapshot.SignKey)) &&
		snapshot.ClientKeyLen >= 0 && snapshot.ClientKeyLen <= static_cast<int>(sizeof(snapshot.ClientKey)))
	{
		return true;
	}
	return false;
}

bool TryDumpLauncherSharedData(const SharedMapCandidate& candidate)
{
	HANDLE map = OpenFileMappingA(FILE_MAP_READ, FALSE, candidate.name.c_str());
	if (!map) {
		return false;
	}

	void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, sizeof(LauncherSharedData));
	if (!view) {
		HookLog("Launcher shared data map view failed name=%s reason=%s error=%lu",
			candidate.name.c_str(), candidate.reason.c_str(), GetLastError());
		CloseHandle(map);
		return false;
	}

	LauncherSharedData snapshot = {};
	memcpy(&snapshot, view, sizeof(snapshot));
	UnmapViewOfFile(view);
	CloseHandle(map);

	if (!LooksLikeLauncherSharedData(snapshot)) {
		HookLog("Launcher shared data candidate rejected name=%s reason=%s uin=%lu worldId=%lu signKeyLen=%d clientKeyLen=%d",
			candidate.name.c_str(),
			candidate.reason.c_str(),
			snapshot.Uin,
			snapshot.WorldID,
			snapshot.SignKeyLen,
			snapshot.ClientKeyLen);
		return false;
	}

	std::string launcherPart = candidate.pid ? std::to_string(candidate.pid) : "unknown";
	std::string payloadPath = GetPacketPayloadOutputDirectory()
		+ "shared_data_"
		+ std::to_string(GetCurrentProcessId())
		+ "_launcher_"
		+ launcherPart
		+ ".bin";
	{
		std::ofstream binary(payloadPath, std::ios::binary);
		if (binary) {
			binary.write(reinterpret_cast<const char*>(&snapshot), sizeof(snapshot));
		}
	}

	int signKeyLen = snapshot.SignKeyLen;
	int signKeyDumpLen = (std::max)(0, (std::min)(signKeyLen, static_cast<int>(sizeof(snapshot.SignKey))));
	int clientKeyLen = snapshot.ClientKeyLen;
	int clientKeyDumpLen = (std::max)(0, (std::min)(clientKeyLen, static_cast<int>(sizeof(snapshot.ClientKey))));
	std::string keyHex = BytesToHex(reinterpret_cast<const BYTE*>(snapshot.Key), sizeof(snapshot.Key));
	std::string pwdHex = BytesToHex(reinterpret_cast<const BYTE*>(snapshot.PwdMD5), sizeof(snapshot.PwdMD5));
	std::string signKey = BoundedAscii(snapshot.SignKey, sizeof(snapshot.SignKey), signKeyDumpLen);
	std::string clientKey = BoundedAscii(snapshot.ClientKey, sizeof(snapshot.ClientKey), clientKeyDumpLen);
	std::string userDir = BoundedAscii(snapshot.UserDir, sizeof(snapshot.UserDir));
	std::string commodityUrl = BoundedAscii(snapshot.CommodityListURL, sizeof(snapshot.CommodityListURL));
	std::string nick = BoundedAscii(snapshot.QQNickName, sizeof(snapshot.QQNickName),
		static_cast<unsigned char>(snapshot.QQNickNameLength));

	HookLog("Launcher shared data dumped launcherPid=%lu map=%s reason=%s uin=%lu worldId=%lu signKeyLen=%d signKey=%s key=%s clientKeyLen=%d clientKey=%s payload=%s",
		candidate.pid,
		candidate.name.c_str(),
		candidate.reason.c_str(),
		snapshot.Uin,
		snapshot.WorldID,
		snapshot.SignKeyLen,
		signKey.c_str(),
		keyHex.c_str(),
		snapshot.ClientKeyLen,
		clientKey.c_str(),
		payloadPath.c_str());

	std::string textPath = GetPacketDumpOutputDirectory()
		+ "shared_data_"
		+ std::to_string(GetCurrentProcessId())
		+ "_launcher_"
		+ launcherPart
		+ ".log";
	std::ofstream text(textPath, std::ios::app);
	if (text) {
		text << "launcherPid=" << candidate.pid << " gamePid=" << GetCurrentProcessId()
			<< " map=" << candidate.name << " reason=" << candidate.reason << "\n";
		text << "sizeof=0x" << std::hex << sizeof(snapshot) << std::dec << "\n";
		text << "hWnd=" << snapshot.hWnd << "\n";
		text << "Uin=" << snapshot.Uin << "\n";
		text << "PwdMD5Hex=" << pwdHex << "\n";
		text << "WorldID=" << snapshot.WorldID << "\n";
		text << "UserDir=" << userDir << "\n";
		text << "CommodityListCRC=" << snapshot.CommodityListCRC << "\n";
		text << "CommodityListURL=" << commodityUrl << "\n";
		text << "KeyHex=" << keyHex << "\n";
		text << "SignKeyLen=" << snapshot.SignKeyLen << "\n";
		text << "SignKey=" << signKey << "\n";
		text << "ClientKeyLen=" << snapshot.ClientKeyLen << "\n";
		text << "ClientKey=" << clientKey << "\n";
		text << "QQNickNameLength=" << static_cast<int>(static_cast<unsigned char>(snapshot.QQNickNameLength)) << "\n";
		text << "QQNickName=" << nick << "\n";
		text << "Binary=" << payloadPath << "\n";
	}
	return true;
}

void DumpLauncherSharedData()
{
	if (InterlockedCompareExchange(&g_sharedDataDumped, 1, 0) != 0) {
		return;
	}

	DWORD commandLinePid = ParseLauncherPidFromCommandLine();
	DWORD parentPid = GetParentProcessId(GetCurrentProcessId());
	HookLog("Launcher shared data scan start cmd=%ls commandLinePid=%lu parentPid=%lu",
		GetCommandLineW(), commandLinePid, parentPid);

	std::vector<SharedMapCandidate> candidates;
	AddSharedMapPidCandidates(candidates, commandLinePid, "command-line");
	AddSharedMapPidCandidates(candidates, parentPid, "parent-process");
	AddSharedMapPidCandidates(candidates, GetCurrentProcessId(), "game-process");
	AddSharedMapCandidate(candidates, "KartMap_Shared", 0, "base-name");
	AddSharedMapCandidate(candidates, "Local\\KartMap_Shared", 0, "base-name");
	AddSharedMapCandidate(candidates, "Global\\KartMap_Shared", 0, "base-name");
	AddSharedMapCandidate(candidates, "ZingSpeedMap_Shared", 0, "base-name");
	AddSharedMapCandidate(candidates, "Local\\ZingSpeedMap_Shared", 0, "base-name");
	AddSharedMapCandidate(candidates, "Global\\ZingSpeedMap_Shared", 0, "base-name");
	AddAllProcessSharedMapCandidates(candidates);

	for (const auto& candidate : candidates) {
		if (TryDumpLauncherSharedData(candidate)) {
			InterlockedExchange(&g_sharedDataDumped, 2);
			return;
		}
	}

	HookLog("Launcher shared data open failed after scan commandLinePid=%lu parentPid=%lu candidates=%llu",
		commandLinePid,
		parentPid,
		static_cast<unsigned long long>(candidates.size()));
	InterlockedExchange(&g_sharedDataDumped, 0);
}

bool VerifyCallTarget(uintptr_t networkBase, uintptr_t callRva, uintptr_t expectedTargetRva, const char* name)
{
	unsigned char* call = reinterpret_cast<unsigned char*>(networkBase + callRva);
	unsigned char original[5] = {};
	if (!SafeCopyRaw(original, call, sizeof(original)) || original[0] != 0xE8) {
		HookLog("Network verify %s failed rva=0x%llX first=%02X",
			name,
			static_cast<unsigned long long>(callRva),
			original[0]);
		return false;
	}

	int32_t rel = 0;
	memcpy(&rel, original + 1, sizeof(rel));
	uintptr_t actualTarget = reinterpret_cast<uintptr_t>(call + 5 + rel);
	uintptr_t expectedTarget = networkBase + expectedTargetRva;
	if (actualTarget != expectedTarget) {
		HookLog("Network verify %s target mismatch callRva=0x%llX actualRva=0x%llX expectedRva=0x%llX",
			name,
			static_cast<unsigned long long>(callRva),
			static_cast<unsigned long long>(actualTarget - networkBase),
			static_cast<unsigned long long>(expectedTargetRva));
		return false;
	}

	return true;
}

bool VerifyCurrentIdaImageByCallSites(HMODULE networkModule)
{
	uintptr_t base = reinterpret_cast<uintptr_t>(networkModule);
	size_t imageSize = GetModuleImageSize(networkModule);
	if (imageSize != kKnownImageSize) {
		HookLog("Network.dll image size mismatch size=0x%llX expected=0x%llX",
			static_cast<unsigned long long>(imageSize),
			static_cast<unsigned long long>(kKnownImageSize));
		return false;
	}

	return
		VerifyCallTarget(base, kGameSocketOnMyReceiveUnpackCallRva, kUnpackRva, "GameSocket::OnMyReceive unpack") &&
		VerifyCallTarget(base, kSpecialDecodeCallRvas[0], kSpecialDecodeRva, "special 28346 decode") &&
		VerifyCallTarget(base, kInnerDispatchCallRvas[0], kInnerDispatchRva, "inner dispatch direct") &&
		VerifyCallTarget(base, kInnerDispatchCallRvas[1], kInnerDispatchRva, "inner dispatch mode0") &&
		VerifyCallTarget(base, kSafePacketBuilderCallRvas[0], kSafePacketBuilderRva, "safe 28346 builder") &&
		VerifyCallTarget(base, kSerializeBodyCallRvas[0], kSerializeBodyRva, "body serializer");
}

std::string ComputeReadableMemoryMd5(const void* data, size_t size)
{
	HCRYPTPROV provider = 0;
	HCRYPTHASH hash = 0;
	if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) ||
		!CryptCreateHash(provider, CALG_MD5, 0, 0, &hash))
	{
		if (hash) {
			CryptDestroyHash(hash);
		}
		if (provider) {
			CryptReleaseContext(provider, 0);
		}
		return "";
	}

	BYTE buffer[0x4000] = {};
	const unsigned char* cursor = static_cast<const unsigned char*>(data);
	size_t remaining = size;
	bool ok = true;
	while (remaining > 0) {
		size_t chunk = (std::min)(remaining, sizeof(buffer));
		if (!SafeCopyRaw(buffer, cursor, chunk) || !CryptHashData(hash, buffer, static_cast<DWORD>(chunk), 0)) {
			ok = false;
			break;
		}
		cursor += chunk;
		remaining -= chunk;
	}

	std::string result;
	if (ok) {
		BYTE digest[16] = {};
		DWORD digestSize = sizeof(digest);
		if (CryptGetHashParam(hash, HP_HASHVAL, digest, &digestSize, 0)) {
			result = BytesToHex(digest, digestSize);
		}
	}
	CryptDestroyHash(hash);
	CryptReleaseContext(provider, 0);
	return result;
}

void AppendCodeBytesLine(std::ofstream& out, uintptr_t base, const char* name, uintptr_t rva, size_t size)
{
	if (!rva) {
		out << name << " rva=0x0 address=0x0 len=0 bytes=<unresolved>\n";
		HookLog("Network code snapshot %s rva=0x0 bytes=<unresolved>", name);
		return;
	}
	BYTE bytes[128] = {};
	size_t copySize = (std::min)(size, sizeof(bytes));
	const void* address = reinterpret_cast<const void*>(base + rva);
	bool ok = SafeCopyRaw(bytes, address, copySize);
	std::string hex = ok ? BytesToHex(bytes, static_cast<DWORD>(copySize)) : "<unreadable>";
	out << name << " rva=0x" << std::hex << rva << " address=0x" << (base + rva)
		<< std::dec << " len=" << copySize << " bytes=" << hex << "\n";
	HookLog("Network code snapshot %s rva=0x%llX bytes=%s",
		name,
		static_cast<unsigned long long>(rva),
		hex.c_str());
}

void DumpNetworkCodeSnapshot(HMODULE networkModule)
{
	uintptr_t base = reinterpret_cast<uintptr_t>(networkModule);
	auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
		return;
	}

	auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	if (!nt || nt->Signature != IMAGE_NT_SIGNATURE) {
		return;
	}

	DWORD textRva = 0;
	DWORD textSize = 0;
	auto section = IMAGE_FIRST_SECTION(nt);
	for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
		char name[9] = {};
		memcpy(name, section->Name, 8);
		if (strcmp(name, ".text") == 0) {
			textRva = section->VirtualAddress;
			textSize = section->Misc.VirtualSize;
			break;
		}
	}

	std::string textMd5;
	if (textRva && textSize) {
		textMd5 = ComputeReadableMemoryMd5(reinterpret_cast<const void*>(base + textRva), textSize);
	}

	std::string path = GetPacketDumpOutputDirectory()
		+ "network_code_snapshot_"
		+ std::to_string(GetCurrentProcessId())
		+ ".log";
	std::ofstream out(path, std::ios::app);
	if (!out) {
		HookLog("Network code snapshot open failed path=%s", path.c_str());
		return;
	}

	out << "base=0x" << std::hex << base
		<< " imageSize=0x" << GetModuleImageSize(networkModule)
		<< " textRva=0x" << textRva
		<< " textSize=0x" << textSize
		<< std::dec << " textMd5=" << textMd5 << "\n";
	HookLog("Network code snapshot text rva=0x%lX size=0x%lX md5=%s",
		textRva,
		textSize,
		textMd5.c_str());

	out << "resolved source=" << (g_networkLayout.resolvedFromRuntime ? "dynamic" : "fallback")
		<< " coreReady=" << (g_networkLayout.coreReady ? 1 : 0)
		<< " unpack=0x" << std::hex << g_networkLayout.unpackRva
		<< " pack=0x" << g_networkLayout.packRva
		<< " specialDecode=0x" << g_networkLayout.specialDecodeRva
		<< " innerDispatch=0x" << g_networkLayout.innerDispatchRva
		<< " parserQueueDispatch=0x" << g_networkLayout.parserQueueDispatchRva
		<< " serializeBody=0x" << g_networkLayout.serializeBodyRva
		<< " safePacketBuilder=0x" << g_networkLayout.safePacketBuilderRva
		<< " loginCacheDecode=0x" << g_networkLayout.loginCacheDecodeRva
		<< std::dec << "\n";
	if (g_networkLayout.coreReady) {
		AppendCodeBytesLine(out, base, "resolved_func_unpack", g_networkLayout.unpackRva, 64);
		AppendCodeBytesLine(out, base, "resolved_func_pack", g_networkLayout.packRva, 64);
		AppendCodeBytesLine(out, base, "resolved_func_special_decode", g_networkLayout.specialDecodeRva, 64);
		AppendCodeBytesLine(out, base, "resolved_func_inner_dispatch", g_networkLayout.innerDispatchRva, 64);
		AppendCodeBytesLine(out, base, "resolved_func_parser_queue_dispatch", g_networkLayout.parserQueueDispatchRva, 96);
		AppendCodeBytesLine(out, base, "resolved_func_serialize_body", g_networkLayout.serializeBodyRva, 64);
		AppendCodeBytesLine(out, base, "resolved_func_safe_packet_builder", g_networkLayout.safePacketBuilderRva, 64);
		AppendCodeBytesLine(out, base, "resolved_func_login_cache_decode", g_networkLayout.loginCacheDecodeRva, 64);
	}

	AppendCodeBytesLine(out, base, "func_unpack", kUnpackRva, 64);
	AppendCodeBytesLine(out, base, "func_pack", kPackRva, 64);
	AppendCodeBytesLine(out, base, "func_special_decode", kSpecialDecodeRva, 64);
	AppendCodeBytesLine(out, base, "func_inner_dispatch", kInnerDispatchRva, 64);
	AppendCodeBytesLine(out, base, "func_safe_packet_builder", kSafePacketBuilderRva, 64);
	AppendCodeBytesLine(out, base, "func_serialize_body", kSerializeBodyRva, 64);
	AppendCodeBytesLine(out, base, "func_parser_register_msg_id", kParserRegisterMsgIdRva, 64);
	AppendCodeBytesLine(out, base, "func_parser_lua_dispatch", kParserLuaDispatchRva, 64);
	AppendCodeBytesLine(out, base, "func_parser_queue_dispatch", kParserQueueDispatchRva, 96);
	AppendCodeBytesLine(out, base, "func_lua_network_mgr_init", kLuaNetworkMgrInitRva, 64);
	AppendCodeBytesLine(out, base, "func_request_game_server_with_buf", kRequestGameServerWithBufRva, 64);
	AppendCodeBytesLine(out, base, "func_parser_queued_flush", kParserQueuedFlushRva, 64);
	AppendCodeBytesLine(out, base, "func_parser_fast_path_enable", kParserFastPathEnableRva, 64);
	AppendCodeBytesLine(out, base, "func_parser_fast_path_disable", kParserFastPathDisableRva, 64);
	AppendCodeBytesLine(out, base, "func_lua_network_mgr_init_wrapper", kLuaNetworkMgrInitWrapperRva, 64);
	AppendCodeBytesLine(out, base, "func_login_cache_decode", kLoginCacheDecodeRva, 64);
	AppendCodeBytesLine(out, base, "func_login_cache_encode", kLoginCacheEncodeRva, 64);
	AppendCodeBytesLine(out, base, "inner_dispatch_switch_entry", kInnerDispatchSwitchEntryRva, 96);
	AppendCodeBytesLine(out, base, "inner_dispatch_default_return0", kInnerDispatchDefaultReturn0Rva, 128);
	AppendCodeBytesLine(out, base, "inner_dispatch_return1", kInnerDispatchReturn1Rva, 48);
	AppendCodeBytesLine(out, base, "inner_dispatch_jump_entry_msg100", kInnerDispatchSmallMsgJumpTableRva + 100 * sizeof(unsigned int), 16);
	AppendCodeBytesLine(out, base, "inner_dispatch_jump_entry_msg223", kInnerDispatchSmallMsgJumpTableRva + 223 * sizeof(unsigned int), 16);
	AppendCodeBytesLine(out, base, "launcher_patch_site_348E8D", 0x348E8D, 32);

	for (uintptr_t rva : kUnpackCallRvas) {
		AppendCodeBytesLine(out, base, "call_unpack", rva, 16);
	}
	for (uintptr_t rva : kPackCallRvas) {
		AppendCodeBytesLine(out, base, "call_pack", rva, 16);
	}
	for (uintptr_t rva : kSpecialDecodeCallRvas) {
		AppendCodeBytesLine(out, base, "call_special_decode", rva, 16);
	}
	for (uintptr_t rva : kInnerDispatchCallRvas) {
		AppendCodeBytesLine(out, base, "call_inner_dispatch", rva, 16);
	}
	for (uintptr_t rva : kSafePacketBuilderCallRvas) {
		AppendCodeBytesLine(out, base, "call_safe_packet_builder", rva, 16);
	}
	for (uintptr_t rva : kSerializeBodyCallRvas) {
		AppendCodeBytesLine(out, base, "call_serialize_body", rva, 16);
	}
	for (uintptr_t rva : kParserQueueDispatchCallRvas) {
		AppendCodeBytesLine(out, base, "call_parser_queue_dispatch", rva, 16);
	}
	for (uintptr_t rva : g_networkLayout.unpackCallRvas) {
		AppendCodeBytesLine(out, base, "resolved_call_unpack", rva, 16);
	}
	for (uintptr_t rva : g_networkLayout.packCallRvas) {
		AppendCodeBytesLine(out, base, "resolved_call_pack", rva, 16);
	}
	for (uintptr_t rva : g_networkLayout.specialDecodeCallRvas) {
		AppendCodeBytesLine(out, base, "resolved_call_special_decode", rva, 16);
	}
	for (uintptr_t rva : g_networkLayout.innerDispatchCallRvas) {
		AppendCodeBytesLine(out, base, "resolved_call_inner_dispatch", rva, 16);
	}
	for (uintptr_t rva : g_networkLayout.safePacketBuilderCallRvas) {
		AppendCodeBytesLine(out, base, "resolved_call_safe_packet_builder", rva, 16);
	}
	for (uintptr_t rva : g_networkLayout.serializeBodyCallRvas) {
		AppendCodeBytesLine(out, base, "resolved_call_serialize_body", rva, 16);
	}
	for (uintptr_t rva : g_networkLayout.parserQueueDispatchCallRvas) {
		AppendCodeBytesLine(out, base, "resolved_call_parser_queue_dispatch", rva, 16);
	}
	for (uintptr_t rva : g_networkLayout.loginCacheDecodeCallRvas) {
		AppendCodeBytesLine(out, base, "resolved_call_login_cache_decode", rva, 16);
	}
	out << "\n";
}

bool IsRel32Reachable(uintptr_t source, uintptr_t destination)
{
	int64_t delta = static_cast<int64_t>(destination) - static_cast<int64_t>(source + 5);
	return delta >= (std::numeric_limits<int32_t>::min)() && delta <= (std::numeric_limits<int32_t>::max)();
}

unsigned char* AllocateRelayPageNear(uintptr_t base)
{
	SYSTEM_INFO sysInfo = {};
	GetSystemInfo(&sysInfo);
	const uintptr_t granularity = sysInfo.dwAllocationGranularity ? sysInfo.dwAllocationGranularity : 0x10000;
	const uintptr_t minAddress = reinterpret_cast<uintptr_t>(sysInfo.lpMinimumApplicationAddress);
	const uintptr_t maxAddress = reinterpret_cast<uintptr_t>(sysInfo.lpMaximumApplicationAddress);

	for (uintptr_t distance = 0; distance < 0x70000000ULL; distance += granularity) {
		uintptr_t down = base > distance ? (base - distance) & ~(granularity - 1) : 0;
		if (down >= minAddress && IsRel32Reachable(base, down)) {
			void* mem = VirtualAlloc(reinterpret_cast<void*>(down), granularity, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (mem) {
				return static_cast<unsigned char*>(mem);
			}
		}

		uintptr_t up = (base + distance + granularity - 1) & ~(granularity - 1);
		if (up >= base && up <= maxAddress && IsRel32Reachable(base, up)) {
			void* mem = VirtualAlloc(reinterpret_cast<void*>(up), granularity, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
			if (mem) {
				return static_cast<unsigned char*>(mem);
			}
		}
	}

	return nullptr;
}

void* CreateRelayStub(uintptr_t networkBase, void* detour, const char* name)
{
	constexpr size_t kRelayPageSize = 0x10000;
	constexpr size_t kRelayStubSize = 14;
	if (!g_relayPage) {
		g_relayPage = AllocateRelayPageNear(networkBase);
		if (!g_relayPage) {
			HookLog("Network call-site relay allocation failed base=%p", reinterpret_cast<void*>(networkBase));
			return nullptr;
		}
		HookLog("Network call-site relay page=%p base=%p", g_relayPage, reinterpret_cast<void*>(networkBase));
	}

	if (g_relayOffset + kRelayStubSize > kRelayPageSize) {
		HookLog("Network call-site relay page exhausted for %s", name);
		return nullptr;
	}

	unsigned char* stub = g_relayPage + g_relayOffset;
	g_relayOffset += kRelayStubSize;

	// jmp qword ptr [rip+0]; dq detour
	stub[0] = 0xFF;
	stub[1] = 0x25;
	stub[2] = 0x00;
	stub[3] = 0x00;
	stub[4] = 0x00;
	stub[5] = 0x00;
	*reinterpret_cast<uintptr_t*>(stub + 6) = reinterpret_cast<uintptr_t>(detour);
	FlushInstructionCache(GetCurrentProcess(), stub, kRelayStubSize);

	HookLog("Network %s relay stub=%p detour=%p", name, stub, detour);
	return stub;
}

bool PatchDirectCall(uintptr_t networkBase, uintptr_t callRva, uintptr_t expectedTarget, void* relay, const char* name)
{
	if (g_callPatchCount >= _countof(g_callPatches)) {
		HookLog("Network %s call-site patch table full", name);
		return false;
	}

	unsigned char* call = reinterpret_cast<unsigned char*>(networkBase + callRva);
	unsigned char original[5] = {};
	if (!SafeCopyRaw(original, call, sizeof(original)) || original[0] != 0xE8) {
		HookLog("Network %s call-site invalid rva=0x%llX address=%p first=%02X",
			name,
			static_cast<unsigned long long>(callRva),
			call,
			original[0]);
		return false;
	}

	int32_t oldRel = 0;
	memcpy(&oldRel, original + 1, sizeof(oldRel));
	uintptr_t oldTarget = reinterpret_cast<uintptr_t>(call + 5 + oldRel);
	if (oldTarget != expectedTarget) {
		HookLog("Network %s call-site target mismatch rva=0x%llX got=%p expected=%p",
			name,
			static_cast<unsigned long long>(callRva),
			reinterpret_cast<void*>(oldTarget),
			reinterpret_cast<void*>(expectedTarget));
		return false;
	}

	uintptr_t relayAddress = reinterpret_cast<uintptr_t>(relay);
	if (!IsRel32Reachable(reinterpret_cast<uintptr_t>(call), relayAddress)) {
		HookLog("Network %s call-site relay out of rel32 range call=%p relay=%p", name, call, relay);
		return false;
	}

	int64_t delta = static_cast<int64_t>(relayAddress) - static_cast<int64_t>(reinterpret_cast<uintptr_t>(call) + 5);
	int32_t newRel = static_cast<int32_t>(delta);
	unsigned char patch[5] = { 0xE8, 0, 0, 0, 0 };
	memcpy(patch + 1, &newRel, sizeof(newRel));

	DWORD oldProtect = 0;
	if (!VirtualProtect(call, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		HookLog("Network %s call-site protect failed rva=0x%llX error=%lu",
			name,
			static_cast<unsigned long long>(callRva),
			GetLastError());
		return false;
	}
	memcpy(call, patch, sizeof(patch));
	DWORD ignoredProtect = 0;
	VirtualProtect(call, sizeof(patch), oldProtect, &ignoredProtect);
	FlushInstructionCache(GetCurrentProcess(), call, sizeof(patch));

	CallSitePatch& saved = g_callPatches[g_callPatchCount++];
	saved.Address = call;
	memcpy(saved.Original, original, sizeof(saved.Original));
	saved.Installed = true;
	HookLog("Network %s call-site patched rva=0x%llX address=%p relay=%p",
		name,
		static_cast<unsigned long long>(callRva),
		call,
		relay);
	return true;
}

template <size_t Count>
size_t PatchDirectCalls(uintptr_t networkBase, const uintptr_t(&callRvas)[Count], uintptr_t targetRva, void* detour, const char* name)
{
	void* relay = CreateRelayStub(networkBase, detour, name);
	if (!relay) {
		return 0;
	}

	size_t patched = 0;
	uintptr_t expectedTarget = networkBase + targetRva;
	for (uintptr_t callRva : callRvas) {
		if (PatchDirectCall(networkBase, callRva, expectedTarget, relay, name)) {
			++patched;
		}
	}
	HookLog("Network %s call-site patch complete patched=%llu total=%llu",
		name,
		static_cast<unsigned long long>(patched),
		static_cast<unsigned long long>(Count));
	return patched;
}

size_t PatchDirectCalls(uintptr_t networkBase, const std::vector<uintptr_t>& callRvas, uintptr_t targetRva, void* detour, const char* name)
{
	if (callRvas.empty() || !targetRva) {
		HookLog("Network %s dynamic call-site patch skipped callsites=%llu targetRva=0x%llX",
			name,
			static_cast<unsigned long long>(callRvas.size()),
			static_cast<unsigned long long>(targetRva));
		return 0;
	}

	void* relay = CreateRelayStub(networkBase, detour, name);
	if (!relay) {
		return 0;
	}

	size_t patched = 0;
	uintptr_t expectedTarget = networkBase + targetRva;
	for (uintptr_t callRva : callRvas) {
		if (PatchDirectCall(networkBase, callRva, expectedTarget, relay, name)) {
			++patched;
		}
	}
	HookLog("Network %s dynamic call-site patch complete patched=%llu total=%llu targetRva=0x%llX",
		name,
		static_cast<unsigned long long>(patched),
		static_cast<unsigned long long>(callRvas.size()),
		static_cast<unsigned long long>(targetRva));
	return patched;
}

const TopKart551TraceLayout* ResolveTopKart551TraceLayout(const std::string& md5)
{
	for (const TopKart551TraceLayout& layout : kTopKart551TraceLayouts) {
		if (!md5.empty() && md5 == layout.Md5) {
			return &layout;
		}
	}
	return nullptr;
}

std::vector<uintptr_t> FindE8CallSitesToTarget(
	HMODULE module,
	uintptr_t textRva,
	size_t textSize,
	uintptr_t targetRva,
	const char* name)
{
	std::vector<uintptr_t> callRvas;
	if (!module || !textRva || textSize < 5 || !targetRva) {
		return callRvas;
	}

	const uintptr_t base = reinterpret_cast<uintptr_t>(module);
	const uintptr_t expectedTarget = base + targetRva;
	const unsigned char* text = reinterpret_cast<const unsigned char*>(base + textRva);
	for (size_t offset = 0; offset + 5 <= textSize; ++offset) {
		if (text[offset] != 0xE8) {
			continue;
		}

		int32_t rel = 0;
		memcpy(&rel, text + offset + 1, sizeof(rel));
		const intptr_t callAddress = static_cast<intptr_t>(base + textRva + offset);
		const uintptr_t target = static_cast<uintptr_t>(callAddress + 5 + rel);
		if (target == expectedTarget) {
			AddUniqueRva(callRvas, textRva + offset);
		}
	}

	HookLog("Top-Kart %s E8 scan targetRva=0x%llX found=%llu",
		name,
		static_cast<unsigned long long>(targetRva),
		static_cast<unsigned long long>(callRvas.size()));
	return callRvas;
}

template <size_t Count>
bool ValidateTopKartUniqueSignature(
	HMODULE module,
	uintptr_t textRva,
	size_t textSize,
	uintptr_t expectedRva,
	const int(&signature)[Count],
	const char* name)
{
	std::vector<uintptr_t> matches;
	const uintptr_t base = reinterpret_cast<uintptr_t>(module);
	const unsigned char* text = reinterpret_cast<const unsigned char*>(base + textRva);
	for (size_t offset = 0; offset + Count <= textSize; ++offset) {
		bool matched = true;
		for (size_t i = 0; i < Count; ++i) {
			if (signature[i] >= 0 && text[offset + i] != static_cast<unsigned char>(signature[i])) {
				matched = false;
				break;
			}
		}
		if (matched) {
			AddUniqueRva(matches, textRva + offset);
		}
	}

	const bool valid = matches.size() == 1 && matches[0] == expectedRva;
	char detail[1024] = {};
	snprintf(detail, sizeof(detail),
		"name=%s expectedRva=0x%llX matches=%llu firstRva=0x%llX valid=%d bytes=%s",
		name,
		static_cast<unsigned long long>(expectedRva),
		static_cast<unsigned long long>(matches.size()),
		static_cast<unsigned long long>(matches.empty() ? 0 : matches[0]),
		valid ? 1 : 0,
		ReadMemoryHex(base + expectedRva, Count).c_str());
	HookLog("Top-Kart 551 signature validation %s", detail);
	TraceTopKart551("static_signature_validation", 0, 0, detail, false);
	return valid;
}

void DumpTopKartE8CallWindow(
	HMODULE module,
	uintptr_t textRva,
	size_t textSize,
	const TopKartCallWindow& window)
{
	if (!module || !textRva || textSize < 5 || !window.CenterRva) {
		return;
	}

	const uintptr_t base = reinterpret_cast<uintptr_t>(module);
	const uintptr_t textEnd = textRva + textSize;
	uintptr_t startRva = window.CenterRva > window.BytesBefore ?
		window.CenterRva - window.BytesBefore : textRva;
	uintptr_t endRva = window.CenterRva + window.BytesAfter;
	if (startRva < textRva) {
		startRva = textRva;
	}
	if (endRva > textEnd) {
		endRva = textEnd;
	}
	if (endRva <= startRva + 5) {
		return;
	}

	size_t found = 0;
	constexpr size_t kMaxWindowCalls = 96;
	for (uintptr_t rva = startRva; rva + 5 <= endRva; ++rva) {
		const unsigned char* call = reinterpret_cast<const unsigned char*>(base + rva);
		if (*call != 0xE8) {
			continue;
		}

		int32_t rel = 0;
		memcpy(&rel, call + 1, sizeof(rel));
		const uintptr_t target = static_cast<uintptr_t>(
			static_cast<intptr_t>(base + rva + 5) + rel);
		const uintptr_t targetRva = (target >= base) ? target - base : 0;
		char detail[768] = {};
		snprintf(detail, sizeof(detail),
			"window=%s centerRva=0x%llX range=0x%llX-0x%llX callRva=0x%llX target=%s targetTopRva=0x%llX rel=%d",
			window.Name,
			static_cast<unsigned long long>(window.CenterRva),
			static_cast<unsigned long long>(startRva),
			static_cast<unsigned long long>(endRva),
			static_cast<unsigned long long>(rva),
			FormatModuleRva(target).c_str(),
			static_cast<unsigned long long>(targetRva),
			static_cast<int>(rel));
		TraceTopKart551("call_window_e8", 0, 0, detail, false);
		++found;
		if (found >= kMaxWindowCalls) {
			break;
		}
	}

	char summary[512] = {};
	snprintf(summary, sizeof(summary),
		"window=%s centerRva=0x%llX range=0x%llX-0x%llX found=%llu capped=%d",
		window.Name,
		static_cast<unsigned long long>(window.CenterRva),
		static_cast<unsigned long long>(startRva),
		static_cast<unsigned long long>(endRva),
		static_cast<unsigned long long>(found),
		found >= kMaxWindowCalls ? 1 : 0);
	HookLog("Top-Kart 551 %s", summary);
	TraceTopKart551("call_window_summary", 0, 0, summary, false);
}

void* CreateTopKartRelayStub(uintptr_t topKartBase, void* detour, const char* name)
{
	constexpr size_t kRelayPageSize = 0x10000;
	constexpr size_t kRelayStubSize = 14;
	if (!g_topKartRelayPage) {
		g_topKartRelayPage = AllocateRelayPageNear(topKartBase);
		if (!g_topKartRelayPage) {
			HookLog("Top-Kart %s relay allocation failed base=%p", name, reinterpret_cast<void*>(topKartBase));
			return nullptr;
		}
		HookLog("Top-Kart relay page=%p base=%p", g_topKartRelayPage, reinterpret_cast<void*>(topKartBase));
	}

	if (g_topKartRelayOffset + kRelayStubSize > kRelayPageSize) {
		HookLog("Top-Kart %s relay page exhausted", name);
		return nullptr;
	}

	unsigned char* stub = g_topKartRelayPage + g_topKartRelayOffset;
	g_topKartRelayOffset += kRelayStubSize;

	// jmp qword ptr [rip+0]; dq detour
	stub[0] = 0xFF;
	stub[1] = 0x25;
	stub[2] = 0x00;
	stub[3] = 0x00;
	stub[4] = 0x00;
	stub[5] = 0x00;
	*reinterpret_cast<uintptr_t*>(stub + 6) = reinterpret_cast<uintptr_t>(detour);
	FlushInstructionCache(GetCurrentProcess(), stub, kRelayStubSize);

	HookLog("Top-Kart %s relay stub=%p detour=%p", name, stub, detour);
	return stub;
}

void EmitU32(std::vector<unsigned char>& code, unsigned int value)
{
	for (int i = 0; i < 4; ++i) {
		code.push_back(static_cast<unsigned char>((value >> (i * 8)) & 0xFF));
	}
}

void EmitU64(std::vector<unsigned char>& code, uintptr_t value)
{
	for (int i = 0; i < 8; ++i) {
		code.push_back(static_cast<unsigned char>((value >> (i * 8)) & 0xFF));
	}
}

void EmitBytes(std::vector<unsigned char>& code, std::initializer_list<unsigned char> bytes)
{
	code.insert(code.end(), bytes.begin(), bytes.end());
}

void EmitMovMemRspReg64(std::vector<unsigned char>& code, unsigned char b1, unsigned char b2, unsigned char b3, unsigned int disp)
{
	EmitBytes(code, { b1, b2, b3, 0x24 });
	EmitU32(code, disp);
}

void EmitMovdquRspXmm(std::vector<unsigned char>& code, bool load, unsigned char modrm, unsigned int disp)
{
	EmitBytes(code, { 0xF3, 0x0F, static_cast<unsigned char>(load ? 0x6F : 0x7F), modrm, 0x24 });
	EmitU32(code, disp);
}

using TopKartPreCallLoggerFn = void(__fastcall*)(int, uintptr_t, uintptr_t, const TopKartPreCallContext*);

void* CreateTopKartPreCallTraceStub(
	uintptr_t topKartBase,
	int targetIndex,
	uintptr_t originalTarget,
	const char* name,
	TopKartPreCallLoggerFn logger)
{
	constexpr size_t kRelayPageSize = 0x10000;
	constexpr unsigned int kFrameSize = 0x1A8; // entry rsp%16=8, subtract 8 mod 16 before calling logger
	constexpr unsigned int kCtxOffset = 0x20;
	constexpr unsigned int kXmmOffset = 0x60;
	if (!g_topKartRelayPage) {
		g_topKartRelayPage = AllocateRelayPageNear(topKartBase);
		if (!g_topKartRelayPage) {
			HookLog("Top-Kart %s pre-call relay allocation failed base=%p", name, reinterpret_cast<void*>(topKartBase));
			return nullptr;
		}
		HookLog("Top-Kart relay page=%p base=%p", g_topKartRelayPage, reinterpret_cast<void*>(topKartBase));
	}

	std::vector<unsigned char> code;
	code.reserve(256);
	// sub rsp, kFrameSize
	EmitBytes(code, { 0x48, 0x81, 0xEC });
	EmitU32(code, kFrameSize);

	// Save volatile GP registers used by normal MS x64 argument passing.
	EmitMovMemRspReg64(code, 0x48, 0x89, 0x84, kCtxOffset + 0x00); // rax
	EmitMovMemRspReg64(code, 0x48, 0x89, 0x8C, kCtxOffset + 0x08); // rcx
	EmitMovMemRspReg64(code, 0x48, 0x89, 0x94, kCtxOffset + 0x10); // rdx
	EmitMovMemRspReg64(code, 0x4C, 0x89, 0x84, kCtxOffset + 0x18); // r8
	EmitMovMemRspReg64(code, 0x4C, 0x89, 0x8C, kCtxOffset + 0x20); // r9
	EmitMovMemRspReg64(code, 0x4C, 0x89, 0x94, kCtxOffset + 0x28); // r10
	EmitMovMemRspReg64(code, 0x4C, 0x89, 0x9C, kCtxOffset + 0x30); // r11

	// Save XMM0-XMM5 so float/vector args are not damaged by logging.
	for (unsigned char i = 0; i < 6; ++i) {
		EmitMovdquRspXmm(code, false, static_cast<unsigned char>(0x84 + (i << 3)), kXmmOffset + static_cast<unsigned int>(i) * 0x10);
	}

	// logger(targetIndex, returnAddress, entryRsp, &context)
	EmitBytes(code, { 0xB9 }); // mov ecx, imm32
	EmitU32(code, static_cast<unsigned int>(targetIndex));
	EmitBytes(code, { 0x48, 0x8B, 0x94, 0x24 }); // mov rdx, [rsp+kFrameSize]
	EmitU32(code, kFrameSize);
	EmitBytes(code, { 0x4C, 0x8D, 0x84, 0x24 }); // lea r8, [rsp+kFrameSize]
	EmitU32(code, kFrameSize);
	EmitBytes(code, { 0x4C, 0x8D, 0x8C, 0x24 }); // lea r9, [rsp+kCtxOffset]
	EmitU32(code, kCtxOffset);
	EmitBytes(code, { 0x48, 0xB8 }); // mov rax, logger
	EmitU64(code, reinterpret_cast<uintptr_t>(logger));
	EmitBytes(code, { 0xFF, 0xD0 }); // call rax

	// Restore XMM and GP registers, then tail-jump to the original target.
	for (unsigned char i = 0; i < 6; ++i) {
		EmitMovdquRspXmm(code, true, static_cast<unsigned char>(0x84 + (i << 3)), kXmmOffset + static_cast<unsigned int>(i) * 0x10);
	}
	EmitMovMemRspReg64(code, 0x4C, 0x8B, 0x9C, kCtxOffset + 0x30); // r11
	EmitMovMemRspReg64(code, 0x4C, 0x8B, 0x94, kCtxOffset + 0x28); // r10
	EmitMovMemRspReg64(code, 0x4C, 0x8B, 0x8C, kCtxOffset + 0x20); // r9
	EmitMovMemRspReg64(code, 0x4C, 0x8B, 0x84, kCtxOffset + 0x18); // r8
	EmitMovMemRspReg64(code, 0x48, 0x8B, 0x94, kCtxOffset + 0x10); // rdx
	EmitMovMemRspReg64(code, 0x48, 0x8B, 0x8C, kCtxOffset + 0x08); // rcx
	EmitMovMemRspReg64(code, 0x48, 0x8B, 0x84, kCtxOffset + 0x00); // rax
	EmitBytes(code, { 0x48, 0x81, 0xC4 }); // add rsp, kFrameSize
	EmitU32(code, kFrameSize);
	const size_t tailJumpOffset = code.size();
	EmitBytes(code, { 0xE9 }); // preserve a direct rel32 E9 tail-jump
	EmitU32(code, 0);

	g_topKartRelayOffset = (g_topKartRelayOffset + 0x0F) & ~static_cast<size_t>(0x0F);
	if (g_topKartRelayOffset + code.size() > kRelayPageSize) {
		HookLog("Top-Kart %s pre-call relay page exhausted need=%llu offset=0x%llX",
			name,
			static_cast<unsigned long long>(code.size()),
			static_cast<unsigned long long>(g_topKartRelayOffset));
		return nullptr;
	}

	unsigned char* stub = g_topKartRelayPage + g_topKartRelayOffset;
	unsigned char* tailJump = stub + tailJumpOffset;
	if (!IsRel32Reachable(reinterpret_cast<uintptr_t>(tailJump), originalTarget)) {
		HookLog(
			"Top-Kart %s pre-call original target out of E9 rel32 range stub=%p target=%p",
			name,
			stub,
			reinterpret_cast<void*>(originalTarget));
		return nullptr;
	}
	const int64_t tailDelta = static_cast<int64_t>(originalTarget) -
		static_cast<int64_t>(reinterpret_cast<uintptr_t>(tailJump) + 5);
	const int32_t tailRel = static_cast<int32_t>(tailDelta);
	memcpy(code.data() + tailJumpOffset + 1, &tailRel, sizeof(tailRel));
	memcpy(stub, code.data(), code.size());
	g_topKartRelayOffset += code.size();
	FlushInstructionCache(GetCurrentProcess(), stub, code.size());
	HookLog("Top-Kart %s pre-call trace stub=%p original=%p size=%llu",
		name,
		stub,
		reinterpret_cast<void*>(originalTarget),
		static_cast<unsigned long long>(code.size()));
	return stub;
}

bool PatchTopKartDirectJump(
	uintptr_t topKartBase,
	uintptr_t jumpRva,
	uintptr_t expectedTarget,
	void* relay,
	const unsigned char expectedBytes[5],
	const char* name)
{
	if (g_topKartAntiCheckerPatch.Installed) {
		HookLog("Top-Kart %s E9 patch already installed", name);
		return true;
	}

	unsigned char* jump = reinterpret_cast<unsigned char*>(topKartBase + jumpRva);
	unsigned char original[5] = {};
	if (!SafeCopyRaw(original, jump, sizeof(original)) ||
		memcmp(original, expectedBytes, sizeof(original)) != 0 ||
		original[0] != 0xE9)
	{
		HookLog(
			"Top-Kart %s E9 entry mismatch rva=0x%llX address=%p bytes=%02X %02X %02X %02X %02X",
			name,
			static_cast<unsigned long long>(jumpRva),
			jump,
			original[0],
			original[1],
			original[2],
			original[3],
			original[4]);
		return false;
	}

	int32_t oldRel = 0;
	memcpy(&oldRel, original + 1, sizeof(oldRel));
	const uintptr_t oldTarget = static_cast<uintptr_t>(
		reinterpret_cast<intptr_t>(jump) + 5 + oldRel);
	if (oldTarget != expectedTarget) {
		HookLog(
			"Top-Kart %s E9 target mismatch rva=0x%llX got=%p expected=%p",
			name,
			static_cast<unsigned long long>(jumpRva),
			reinterpret_cast<void*>(oldTarget),
			reinterpret_cast<void*>(expectedTarget));
		return false;
	}

	const uintptr_t relayAddress = reinterpret_cast<uintptr_t>(relay);
	if (!IsRel32Reachable(reinterpret_cast<uintptr_t>(jump), relayAddress)) {
		HookLog("Top-Kart %s E9 relay out of rel32 range jump=%p relay=%p", name, jump, relay);
		return false;
	}

	const int64_t delta = static_cast<int64_t>(relayAddress) -
		static_cast<int64_t>(reinterpret_cast<uintptr_t>(jump) + 5);
	const int32_t newRel = static_cast<int32_t>(delta);
	unsigned char patch[5] = { 0xE9, 0, 0, 0, 0 };
	memcpy(patch + 1, &newRel, sizeof(newRel));

	DWORD oldProtect = 0;
	if (!VirtualProtect(jump, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		HookLog(
			"Top-Kart %s E9 protect failed rva=0x%llX error=%lu",
			name,
			static_cast<unsigned long long>(jumpRva),
			GetLastError());
		return false;
	}

	memcpy(jump, patch, sizeof(patch));
	DWORD ignoredProtect = 0;
	VirtualProtect(jump, sizeof(patch), oldProtect, &ignoredProtect);
	FlushInstructionCache(GetCurrentProcess(), jump, sizeof(patch));

	g_topKartAntiCheckerPatch.Address = jump;
	memcpy(g_topKartAntiCheckerPatch.Original, original, sizeof(original));
	memcpy(g_topKartAntiCheckerPatch.Patched, patch, sizeof(patch));
	g_topKartAntiCheckerPatch.Installed = true;
	g_topKartAntiCheckerOriginalTarget = oldTarget;

	HookLog(
		"Top-Kart %s E9 pass-through installed rva=0x%llX entry=%p relay=%p original=%p",
		name,
		static_cast<unsigned long long>(jumpRva),
		jump,
		relay,
		reinterpret_cast<void*>(oldTarget));
	return true;
}

void RestoreTopKartAntiCheckerTrace()
{
	if (!g_topKartAntiCheckerPatch.Installed || !g_topKartAntiCheckerPatch.Address) {
		return;
	}

	unsigned char current[5] = {};
	if (!SafeCopyRaw(current, g_topKartAntiCheckerPatch.Address, sizeof(current)) ||
		memcmp(current, g_topKartAntiCheckerPatch.Patched, sizeof(current)) != 0)
	{
		HookLog(
			"Top-Kart AntiChecker E9 restore skipped because entry changed address=%p",
			g_topKartAntiCheckerPatch.Address);
		return;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(
		g_topKartAntiCheckerPatch.Address,
		sizeof(g_topKartAntiCheckerPatch.Original),
		PAGE_EXECUTE_READWRITE,
		&oldProtect))
	{
		HookLog(
			"Top-Kart AntiChecker E9 restore protect failed address=%p error=%lu",
			g_topKartAntiCheckerPatch.Address,
			GetLastError());
		return;
	}

	memcpy(
		g_topKartAntiCheckerPatch.Address,
		g_topKartAntiCheckerPatch.Original,
		sizeof(g_topKartAntiCheckerPatch.Original));
	DWORD ignoredProtect = 0;
	VirtualProtect(
		g_topKartAntiCheckerPatch.Address,
		sizeof(g_topKartAntiCheckerPatch.Original),
		oldProtect,
		&ignoredProtect);
	FlushInstructionCache(
		GetCurrentProcess(),
		g_topKartAntiCheckerPatch.Address,
		sizeof(g_topKartAntiCheckerPatch.Original));

	g_topKartAntiCheckerPatch.Installed = false;
	HookLog(
		"Top-Kart AntiChecker E9 restored address=%p original=%p",
		g_topKartAntiCheckerPatch.Address,
		reinterpret_cast<void*>(g_topKartAntiCheckerOriginalTarget));
}

bool InstallTopKartAntiCheckerTrace(HMODULE topKartModule)
{
	if (!IsAntiCheckerTraceEnabled() || !topKartModule) {
		return false;
	}
	if (InterlockedCompareExchange(&g_topKartAntiCheckerTraceInstallStarted, 1, 0) != 0) {
		return g_topKartAntiCheckerPatch.Installed;
	}

	const uintptr_t base = reinterpret_cast<uintptr_t>(topKartModule);
	g_topKartBase = base;
	const std::string md5 = GetModuleMd5(topKartModule);
	const size_t imageSize = GetModuleImageSize(topKartModule);
	if (md5 != kTopKart21487Md5 || imageSize != kTopKart21487ImageSize) {
		HookLog(
			"Top-Kart AntiChecker E9 skipped unsupported md5=%s imageSize=0x%llX expectedMd5=%s expectedImageSize=0x%llX",
			md5.empty() ? "<unavailable>" : md5.c_str(),
			static_cast<unsigned long long>(imageSize),
			kTopKart21487Md5,
			static_cast<unsigned long long>(kTopKart21487ImageSize));
		return false;
	}

	const uintptr_t originalTarget = base + kTopKart21487AntiCheckerOriginalTargetRva;
	void* relay = CreateTopKartPreCallTraceStub(
		base,
		0,
		originalTarget,
		"anticheck_execute",
		&RecordTopKartAntiCheckerExecute);
	if (!relay) {
		return false;
	}

	const bool installed = PatchTopKartDirectJump(
		base,
		kTopKart21487AntiCheckerExecuteRva,
		originalTarget,
		relay,
		kTopKart21487AntiCheckerEntryBytes,
		"AntiChecker Execute");
	HookLog(
		"Top-Kart AntiChecker E9 install complete result=%d profile=%s md5=%s imageSize=0x%llX",
		installed ? 1 : 0,
		DumpProfileName(),
		md5.c_str(),
		static_cast<unsigned long long>(imageSize));
	return installed;
}

bool PatchTopKartDirectCall(uintptr_t topKartBase, uintptr_t callRva, uintptr_t expectedTarget, void* relay, const char* name)
{
	if (g_callPatchCount >= _countof(g_callPatches)) {
		HookLog("Top-Kart %s call-site patch table full", name);
		return false;
	}

	unsigned char* call = reinterpret_cast<unsigned char*>(topKartBase + callRva);
	unsigned char original[5] = {};
	if (!SafeCopyRaw(original, call, sizeof(original)) || original[0] != 0xE8) {
		HookLog("Top-Kart %s call-site invalid rva=0x%llX address=%p first=%02X",
			name,
			static_cast<unsigned long long>(callRva),
			call,
			original[0]);
		return false;
	}

	int32_t oldRel = 0;
	memcpy(&oldRel, original + 1, sizeof(oldRel));
	const intptr_t callAddress = reinterpret_cast<intptr_t>(call);
	const uintptr_t oldTarget = static_cast<uintptr_t>(callAddress + 5 + oldRel);
	if (oldTarget != expectedTarget) {
		HookLog("Top-Kart %s call-site target mismatch rva=0x%llX got=%p expected=%p",
			name,
			static_cast<unsigned long long>(callRva),
			reinterpret_cast<void*>(oldTarget),
			reinterpret_cast<void*>(expectedTarget));
		return false;
	}

	uintptr_t relayAddress = reinterpret_cast<uintptr_t>(relay);
	if (!IsRel32Reachable(reinterpret_cast<uintptr_t>(call), relayAddress)) {
		HookLog("Top-Kart %s call-site relay out of rel32 range call=%p relay=%p", name, call, relay);
		return false;
	}

	int64_t delta = static_cast<int64_t>(relayAddress) - static_cast<int64_t>(reinterpret_cast<uintptr_t>(call) + 5);
	int32_t newRel = static_cast<int32_t>(delta);
	unsigned char patch[5] = { 0xE8, 0, 0, 0, 0 };
	memcpy(patch + 1, &newRel, sizeof(newRel));

	DWORD oldProtect = 0;
	if (!VirtualProtect(call, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		HookLog("Top-Kart %s call-site protect failed rva=0x%llX error=%lu",
			name,
			static_cast<unsigned long long>(callRva),
			GetLastError());
		return false;
	}
	memcpy(call, patch, sizeof(patch));
	DWORD ignoredProtect = 0;
	VirtualProtect(call, sizeof(patch), oldProtect, &ignoredProtect);
	FlushInstructionCache(GetCurrentProcess(), call, sizeof(patch));

	CallSitePatch& saved = g_callPatches[g_callPatchCount++];
	saved.Address = call;
	memcpy(saved.Original, original, sizeof(saved.Original));
	saved.Installed = true;
	HookLog("Top-Kart %s call-site patched rva=0x%llX address=%p relay=%p",
		name,
		static_cast<unsigned long long>(callRva),
		call,
		relay);
	return true;
}

size_t PatchTopKartDirectCalls(
	uintptr_t topKartBase,
	const std::vector<uintptr_t>& callRvas,
	uintptr_t targetRva,
	void* detour,
	const char* name)
{
	if (callRvas.empty() || !targetRva) {
		HookLog("Top-Kart %s E8 patch skipped callsites=%llu targetRva=0x%llX",
			name,
			static_cast<unsigned long long>(callRvas.size()),
			static_cast<unsigned long long>(targetRva));
		return 0;
	}

	void* relay = CreateTopKartRelayStub(topKartBase, detour, name);
	if (!relay) {
		return 0;
	}

	size_t patched = 0;
	uintptr_t expectedTarget = topKartBase + targetRva;
	for (uintptr_t callRva : callRvas) {
		if (PatchTopKartDirectCall(topKartBase, callRva, expectedTarget, relay, name)) {
			++patched;
		}
	}

	HookLog("Top-Kart %s E8 patch complete patched=%llu total=%llu targetRva=0x%llX",
		name,
		static_cast<unsigned long long>(patched),
		static_cast<unsigned long long>(callRvas.size()),
		static_cast<unsigned long long>(targetRva));
	return patched;
}

size_t InstallTopKartPreCallTraceTargets(uintptr_t topKartBase)
{
	size_t patched = 0;
	for (int i = 0; i < static_cast<int>(_countof(kTopKart21273PreCallTargets)); ++i) {
		const TopKartPreCallTraceTarget& target = kTopKart21273PreCallTargets[i];
		void* relay = CreateTopKartPreCallTraceStub(
			topKartBase,
			i,
			topKartBase + target.TargetRva,
			target.Name,
			&TraceTopKartPreCall);
		if (!relay) {
			continue;
		}
		if (PatchTopKartDirectCall(
			topKartBase,
			target.CallRva,
			topKartBase + target.TargetRva,
			relay,
			target.Name))
		{
			++patched;
		}
	}

	HookLog("Top-Kart 551 pre-call trace install complete patched=%llu total=%llu",
		static_cast<unsigned long long>(patched),
		static_cast<unsigned long long>(_countof(kTopKart21273PreCallTargets)));
	return patched;
}

size_t InstallTopKartModelPreCallTraceTargets(uintptr_t topKartBase)
{
	size_t patched = 0;
	for (int i = 0; i < static_cast<int>(_countof(kTopKart21273ModelPreCallTargets)); ++i) {
		const TopKartPreCallTraceTarget& target = kTopKart21273ModelPreCallTargets[i];
		void* relay = CreateTopKartPreCallTraceStub(
			topKartBase,
			i,
			topKartBase + target.TargetRva,
			target.Name,
			&TraceTopKartModelPreCall);
		if (!relay) {
			continue;
		}
		if (PatchTopKartDirectCall(
			topKartBase,
			target.CallRva,
			topKartBase + target.TargetRva,
			relay,
			target.Name))
		{
			++patched;
		}
	}

	HookLog("Top-Kart 100 model pre-call trace install complete patched=%llu total=%llu",
		static_cast<unsigned long long>(patched),
		static_cast<unsigned long long>(_countof(kTopKart21273ModelPreCallTargets)));
	return patched;
}

void WriteHexDump(std::ofstream& file, const unsigned char* data, size_t size)
{
	for (size_t offset = 0; offset < size; offset += 16) {
		char line[128] = { 0 };
		int pos = snprintf(line, sizeof(line), "%08llX  ", static_cast<unsigned long long>(offset));
		for (size_t i = 0; i < 16; ++i) {
			if (offset + i < size) {
				pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", data[offset + i]);
			}
			else {
				pos += snprintf(line + pos, sizeof(line) - pos, "   ");
			}
		}
		pos += snprintf(line + pos, sizeof(line) - pos, " ");
		for (size_t i = 0; i < 16 && offset + i < size; ++i) {
			unsigned char c = data[offset + i];
			line[pos++] = (c >= 0x20 && c <= 0x7E) ? static_cast<char>(c) : '.';
		}
		line[pos++] = '\n';
		line[pos] = 0;
		file << line;
	}
}

void WriteInlineHex(std::ofstream& file, const unsigned char* data, size_t size)
{
	static constexpr char kHex[] = "0123456789ABCDEF";
	for (size_t i = 0; i < size; ++i) {
		const unsigned char value = data[i];
		file.put(kHex[value >> 4]);
		file.put(kHex[value & 0x0F]);
	}
}

void FlushAntiCheckerTraceSamples()
{
	static LONG nextSequence = 1;
	static ULONGLONG previousTick = 0;
	static bool headerWritten = false;
	static bool overflowWritten = false;

	const LONG writeCount = InterlockedCompareExchange(&g_antiCheckerTraceWriteCount, 0, 0);
	const LONG upper = (std::min<LONG>)(
		writeCount,
		static_cast<LONG>(kAntiCheckerTraceSampleCapacity));
	if (nextSequence > upper &&
		(!InterlockedCompareExchange(&g_antiCheckerTraceOverflow, 0, 0) || overflowWritten))
	{
		return;
	}

	const std::string path = GetPacketDumpOutputDirectory()
		+ "anticheck_trace_"
		+ std::to_string(GetCurrentProcessId())
		+ ".log";
	std::ofstream file(path, std::ios::app);
	if (!file) {
		return;
	}

	if (!headerWritten) {
		LARGE_INTEGER frequency = {};
		QueryPerformanceFrequency(&frequency);
		file << "profile=" << DumpProfileName()
			<< " qpc_frequency=" << frequency.QuadPart
			<< " execute_rva=0x1870E0"
			<< " original_target_rva=0x2D3AF5A"
			<< "\n";
		headerWritten = true;
	}

	while (nextSequence <= upper) {
		AntiCheckerTraceSample& sample = g_antiCheckerTraceSamples[nextSequence - 1];
		if (InterlockedCompareExchange(&sample.Ready, 0, 0) == 0) {
			break;
		}

		const ULONGLONG deltaMs = previousTick ? sample.TickMs - previousTick : 0;
		file << "seq=" << sample.Sequence
			<< " tick=" << sample.TickMs
			<< " delta_ms=" << deltaMs
			<< " qpc=" << sample.Qpc
			<< " tid=" << sample.ThreadId
			<< " this=0x" << HexAddress(sample.ThisPtr)
			<< " return=0x" << HexAddress(sample.ReturnAddress)
			<< " entry_rsp=0x" << HexAddress(sample.EntryRsp)
			<< "\n";
		previousTick = sample.TickMs;
		++nextSequence;
	}

	if (!overflowWritten && InterlockedCompareExchange(&g_antiCheckerTraceOverflow, 0, 0)) {
		file << "overflow=1 capacity=" << kAntiCheckerTraceSampleCapacity
			<< " observed=" << writeCount << "\n";
		overflowWritten = true;
	}
}

void FlushLightPacketSamples()
{
	static LONG nextSequence = 1;
	static bool headerWritten = false;
	static bool overflowWritten = false;

	const LONG writeCount = InterlockedCompareExchange(&g_lightPacketWriteCount, 0, 0);
	const LONG upper = (std::min<LONG>)(
		writeCount,
		static_cast<LONG>(kLightPacketSampleCapacity));
	if (nextSequence > upper &&
		(!InterlockedCompareExchange(&g_lightPacketOverflow, 0, 0) || overflowWritten))
	{
		return;
	}

	const std::string path = GetPacketDumpOutputDirectory()
		+ "anticheck_packet_trace_"
		+ std::to_string(GetCurrentProcessId())
		+ ".log";
	std::ofstream file(path, std::ios::app);
	if (!file) {
		return;
	}

	if (!headerWritten) {
		file << "profile=" << DumpProfileName()
			<< " selected_msg_ids=538,15399"
			<< " max_payload=" << kLightPacketMaxPayload
			<< "\n";
		headerWritten = true;
	}

	while (nextSequence <= upper) {
		LightPacketSample& sample = g_lightPacketSamples[nextSequence - 1];
		if (InterlockedCompareExchange(&sample.Ready, 0, 0) == 0) {
			break;
		}

		const char* direction =
			sample.Direction == LightPacketDirection::SerializeBody ? "serialize_body" : "inner_dispatch";
		file << "seq=" << sample.Sequence
			<< " tick=" << sample.TickMs
			<< " tid=" << sample.ThreadId
			<< " direction=" << direction
			<< " msg=" << sample.MsgId
			<< " len=" << sample.DataLen
			<< " copied=" << sample.CopiedLen
			<< " data=";
		WriteInlineHex(file, sample.Data, sample.CopiedLen);
		file << "\n";
		++nextSequence;
	}

	if (!overflowWritten && InterlockedCompareExchange(&g_lightPacketOverflow, 0, 0)) {
		file << "overflow=1 capacity=" << kLightPacketSampleCapacity
			<< " observed=" << writeCount << "\n";
		overflowWritten = true;
	}
}

void FlushProbeSamples()
{
	FlushAntiCheckerTraceSamples();
	if (IsAntiCheckLightProfile()) {
		FlushLightPacketSamples();
	}
}

void FlushStreamingDumps()
{
	AcquireSRWLockExclusive(&g_dumpLock);
	if (g_framedPacketFile.is_open()) {
		g_framedPacketFile.flush();
	}
	if (g_networkDumpFile.is_open()) {
		g_networkDumpFile.flush();
	}
	if (g_networkTraceFile.is_open()) {
		g_networkTraceFile.flush();
	}
	if (g_officialOneStopFile.is_open()) {
		g_officialOneStopFile.flush();
	}
	ReleaseSRWLockExclusive(&g_dumpLock);
}

void AppendFramedPacketLocked(
	const char* direction,
	const unsigned char* data,
	size_t dataLen,
	size_t copiedLen,
	unsigned short msgId,
	long sequence)
{
	if (!IsFullPacketCaptureDirection(direction) || !data || copiedLen == 0) {
		return;
	}

	const std::string path = GetPacketDumpOutputDirectory()
		+ "network_packets_"
		+ std::to_string(GetCurrentProcessId())
		+ ".pdump";
	if (!g_framedPacketFile.is_open()) {
		g_framedPacketFile.clear();
		g_framedPacketFile.open(path, std::ios::binary | std::ios::app);
	}
	if (!g_framedPacketFile) {
		return;
	}

	const uint32_t magic = 0x31504450; // "PDP1"
	const uint16_t version = 1;
	const uint16_t directionCode =
		strcmp(direction, "parser_queue_dispatch_body") == 0 ? 1 : 2;
	const uint32_t recordSequence = static_cast<uint32_t>(sequence);
	const uint64_t tick = GetTickCount64();
	const uint32_t threadId = GetCurrentThreadId();
	const uint16_t reserved = 0;
	const uint32_t originalLength = static_cast<uint32_t>(
		(std::min<size_t>)(dataLen, (std::numeric_limits<uint32_t>::max)()));
	const uint32_t capturedLength = static_cast<uint32_t>(copiedLen);

	g_framedPacketFile.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
	g_framedPacketFile.write(reinterpret_cast<const char*>(&version), sizeof(version));
	g_framedPacketFile.write(reinterpret_cast<const char*>(&directionCode), sizeof(directionCode));
	g_framedPacketFile.write(reinterpret_cast<const char*>(&recordSequence), sizeof(recordSequence));
	g_framedPacketFile.write(reinterpret_cast<const char*>(&tick), sizeof(tick));
	g_framedPacketFile.write(reinterpret_cast<const char*>(&threadId), sizeof(threadId));
	g_framedPacketFile.write(reinterpret_cast<const char*>(&msgId), sizeof(msgId));
	g_framedPacketFile.write(reinterpret_cast<const char*>(&reserved), sizeof(reserved));
	g_framedPacketFile.write(reinterpret_cast<const char*>(&originalLength), sizeof(originalLength));
	g_framedPacketFile.write(reinterpret_cast<const char*>(&capturedLength), sizeof(capturedLength));
	g_framedPacketFile.write(reinterpret_cast<const char*>(data), capturedLength);
}

void DumpCopiedPacketLocked(
	const char* direction,
	const unsigned char* data,
	size_t dataLen,
	size_t copiedLen,
	unsigned short msgId,
	const char* detail,
	long sequence)
{
	std::string safeDirection = SanitizeFileNamePart(direction);
	AppendFramedPacketLocked(direction, data, dataLen, copiedLen, msgId, sequence);
	std::string textPath = GetPacketDumpOutputDirectory() + "network_dump_" + std::to_string(GetCurrentProcessId()) + ".log";
	if (!g_networkDumpFile.is_open()) {
		g_networkDumpFile.clear();
		g_networkDumpFile.open(textPath, std::ios::app);
	}
	if (g_networkDumpFile) {
		g_networkDumpFile << "[" << GetTickCount64() << "]"
			<< " seq=" << sequence
			<< " tid=" << GetCurrentThreadId()
			<< " dir=" << safeDirection
			<< " msg=" << msgId
			<< " len=" << dataLen
			<< " dumped=" << copiedLen;
		if (detail && detail[0]) {
			g_networkDumpFile << " " << detail;
		}
		g_networkDumpFile << "\n";
		WriteHexDump(g_networkDumpFile, data, std::min<size_t>(copiedLen, kMaxHexDumpBytes));
		if (copiedLen > kMaxHexDumpBytes) {
			g_networkDumpFile << "... hex dump truncated at " << kMaxHexDumpBytes << " bytes\n";
		}
		g_networkDumpFile << "\n";
	}

	if (ShouldWriteBinaryDump(msgId, direction)) {
		std::string quickRpcTag = BuildQuickRpcFileTag(msgId, data, copiedLen, detail);
		std::string binPath = GetPacketPayloadOutputDirectory()
			+ "network_bin_"
			+ std::to_string(GetCurrentProcessId())
			+ "_"
			+ std::to_string(sequence)
			+ "_"
			+ safeDirection
			+ "_msg"
			+ std::to_string(msgId)
			+ quickRpcTag
			+ "_len"
			+ std::to_string(dataLen)
			+ ".bin";
		std::ofstream bin(binPath, std::ios::binary);
		if (bin) {
			bin.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(copiedLen));
		}
	}
}

void DumpPacket(const char* direction, const void* data, int dataLen, unsigned short msgId, const char* detail)
{
	if (!data || dataLen <= 0) {
		return;
	}

	const bool writeBinary = ShouldWriteBinaryDump(msgId, direction);
	const bool fullPacketCapture = IsFullPacketCaptureDirection(direction);
	const size_t copyLimit = fullPacketCapture ? kMaxFramedPacketBytes :
		(writeBinary ? kMaxBinaryDumpBytes : kMaxHexDumpBytes);
	size_t copySize = std::min<size_t>(static_cast<size_t>(dataLen), copyLimit);
	std::unique_ptr<unsigned char[]> copy(new (std::nothrow) unsigned char[copySize]);
	if (!copy) {
		HookLog("Network dump skipped allocation failure len=%d", dataLen);
		return;
	}
	if (!SafeCopyRaw(copy.get(), data, copySize)) {
		HookLog("Network dump skipped unreadable %s buffer=%p len=%d", direction, data, dataLen);
		return;
	}

	AcquireSRWLockExclusive(&g_dumpLock);
	long sequence = InterlockedIncrement(&g_dumpSequence);
	DumpCopiedPacketLocked(direction, copy.get(), static_cast<size_t>(dataLen), copySize, msgId, detail, sequence);
	ReleaseSRWLockExclusive(&g_dumpLock);
}

void DumpBodyAfterClientHead(const char* direction, const unsigned char* fullData, int fullLen, unsigned short msgId, const char* detail)
{
	if (!fullData || fullLen <= static_cast<int>(kClientMsgHeadSize)) {
		return;
	}
	DumpPacket(direction, fullData + kClientMsgHeadSize, fullLen - static_cast<int>(kClientMsgHeadSize), msgId, detail);
}

void DumpLoginCacheBuffer(
	const char* direction,
	unsigned char* key,
	unsigned char* buffer,
	long transformIndex,
	const char* phase)
{
	if (!buffer) {
		return;
	}

	unsigned char keyBytes[4] = {};
	bool keyReadable = key && SafeCopyRaw(keyBytes, key, sizeof(keyBytes));
	unsigned short resultId = ReadU16Safe(buffer);
	unsigned char loginKeyLen = ReadU8Safe(buffer + 2);
	unsigned int uin = ReadU32Safe(buffer + 0x23);
	unsigned short connId = ReadU16Safe(buffer + 0x2B);
	unsigned int dword45 = ReadU32Safe(buffer + 45);
	unsigned char hasMore = ReadU8Safe(buffer + 315168);
	unsigned char optLenA = ReadU8Safe(buffer + 316033);
	unsigned char optLenB = ReadU8Safe(buffer + 316098);

	char detail[768] = {};
	snprintf(detail, sizeof(detail),
		"phase=%s index=%ld keyPtr=%p keyReadable=%d key=%02X%02X%02X%02X buffer=%p "
		"cacheLen=%llu resultId=%u loginKeyLen=%u uin=%u connId=%u dword45=%u hasMore315168=%u optLen316033=%u optLen316098=%u",
		phase ? phase : "",
		transformIndex,
		key,
		keyReadable ? 1 : 0,
		keyBytes[0],
		keyBytes[1],
		keyBytes[2],
		keyBytes[3],
		buffer,
		static_cast<unsigned long long>(kLoginCachePlainLen),
		static_cast<unsigned int>(resultId),
		static_cast<unsigned int>(loginKeyLen),
		uin,
		static_cast<unsigned int>(connId),
		dword45,
		static_cast<unsigned int>(hasMore),
		static_cast<unsigned int>(optLenA),
		static_cast<unsigned int>(optLenB));

	TracePacketEvent(direction, 100, static_cast<int>(kLoginCachePlainLen), detail);
	DumpPacket(direction, buffer, static_cast<int>(kLoginCachePlainLen), 100, detail);
}

void __fastcall HookedTopKartProcessLogin(void* cache, int type, const unsigned char* loginPlain)
{
	uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	uintptr_t returnAddressSlot = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
	LONG index = InterlockedIncrement(&g_topKart100ProcessLoginTraceCount);
	const bool dumpFullPlain = index <= kTopKart100LoginTraceFirstDetailed;
	TraceTopKartLoginPlainStage(
		"process_login",
		"enter",
		index,
		returnAddress,
		returnAddressSlot,
		cache,
		type,
		loginPlain,
		dumpFullPlain);

	if (g_originalTopKartProcessLogin) {
		g_originalTopKartProcessLogin(cache, type, loginPlain);
	}

	TraceTopKartLoginPlainStage(
		"process_login",
		"leave",
		index,
		returnAddress,
		returnAddressSlot,
		cache,
		type,
		loginPlain,
		false);
}

void __fastcall HookedTopKartProcessItemInfoOnLogin(void* cache, const unsigned char* loginPlain)
{
	uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	uintptr_t returnAddressSlot = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
	LONG index = InterlockedIncrement(&g_topKart100ProcessItemInfoTraceCount);
	const bool dumpFullPlain = index <= kTopKart100LoginTraceFirstDetailed;
	TraceTopKartLoginPlainStage(
		"process_item_info_on_login",
		"enter",
		index,
		returnAddress,
		returnAddressSlot,
		cache,
		0,
		loginPlain,
		dumpFullPlain);

	if (g_originalTopKartProcessItemInfoOnLogin) {
		g_originalTopKartProcessItemInfoOnLogin(cache, loginPlain);
	}

	TraceTopKartLoginPlainStage(
		"process_item_info_on_login",
		"leave",
		index,
		returnAddress,
		returnAddressSlot,
		cache,
		0,
		loginPlain,
		false);
}

__int64 __fastcall HookedLoginCacheDecode(unsigned char* key, unsigned char* buffer)
{
	uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	uintptr_t returnAddressSlot = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
	long index = InterlockedIncrement(&g_loginCacheDecodeDumpCount);
	bool shouldDump = index <= 8;
	if (shouldDump) {
		DumpLoginCacheBuffer("login_cache_decode_before", key, buffer, index, "before");
		DumpTopKart100LoginCacheContextSnapshots("before", index, returnAddress, returnAddressSlot, key, buffer, 0);
	}

	__int64 result = g_originalLoginCacheDecode ? g_originalLoginCacheDecode(key, buffer) : 0;

	if (shouldDump) {
		DumpLoginCacheBuffer("login_cache_decode_after", key, buffer, index, "after");
		DumpTopKart100LoginCacheContextSnapshots("after", index, returnAddress, returnAddressSlot, key, buffer, result);
	}
	return result;
}

unsigned char __fastcall HookedTopKartCompactDecode551(
	void* object,
	unsigned __int64* base,
	unsigned __int64 end,
	unsigned int* offset)
{
	uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	LONG index = 0;
	bool shouldTrace = ShouldTraceTopKart551(&g_topKart551DecodeTraceCount, index);
	const unsigned __int64 cursorBefore = ReadU64Safe(base);
	const unsigned int offsetBefore = ReadU32Safe(offset);

	if (shouldTrace) {
		char detail[768] = {};
		snprintf(detail, sizeof(detail),
			"object=%p basePtr=%p cursorBefore=0x%llX end=0x%llX offsetPtr=%p offsetBefore=%u",
			object,
			base,
			static_cast<unsigned long long>(cursorBefore),
			static_cast<unsigned long long>(end),
			offset,
			static_cast<unsigned int>(offsetBefore));
		TraceTopKart551("compact_decode551_enter", index, returnAddress, detail, true);
	}

	unsigned char result = g_originalTopKartCompactDecode551 ?
		g_originalTopKartCompactDecode551(object, base, end, offset) : 0;

	if (shouldTrace) {
		const unsigned __int64 cursorAfter = ReadU64Safe(base);
		const unsigned int offsetAfter = ReadU32Safe(offset);
		char detail[768] = {};
		snprintf(detail, sizeof(detail),
			"result=%u object=%p basePtr=%p cursorBefore=0x%llX cursorAfter=0x%llX end=0x%llX offsetBefore=%u offsetAfter=%u consumed=%lld",
			static_cast<unsigned int>(result),
			object,
			base,
			static_cast<unsigned long long>(cursorBefore),
			static_cast<unsigned long long>(cursorAfter),
			static_cast<unsigned long long>(end),
			static_cast<unsigned int>(offsetBefore),
			static_cast<unsigned int>(offsetAfter),
			static_cast<long long>(cursorAfter - cursorBefore));
		TraceTopKart551("compact_decode551_leave", index, returnAddress, detail, false);
	}

	return result;
}

__int64 __fastcall HookedTopKartBuild551RuntimeCache(__int64 arg1, __int64 arg2, __int64 arg3, __int64 arg4)
{
	uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	void* returnAddressSlot = _AddressOfReturnAddress();
	LONG index = 0;
	bool shouldTrace = ShouldTraceTopKart551(&g_topKart551BuildTraceCount, index);

	if (shouldTrace) {
		std::string stackSlots = ReadStackSlotsText(returnAddressSlot, 12);
		std::string arg1Preview = ReadPointerPreviewText("arg1", static_cast<uintptr_t>(arg1));
		std::string arg2Preview = ReadPointerPreviewText("arg2", static_cast<uintptr_t>(arg2));
		std::string arg3Preview = ReadPointerPreviewText("arg3", static_cast<uintptr_t>(arg3));
		std::string arg4Preview = ReadPointerPreviewText("arg4", static_cast<uintptr_t>(arg4));
		char detail[3000] = {};
		snprintf(detail, sizeof(detail),
			"rcx=0x%llX rdx=0x%llX r8=0x%llX r9=0x%llX returnSlot=%p stackSlots=%s %s %s %s %s",
			static_cast<unsigned long long>(arg1),
			static_cast<unsigned long long>(arg2),
			static_cast<unsigned long long>(arg3),
			static_cast<unsigned long long>(arg4),
			returnAddressSlot,
			stackSlots.c_str(),
			arg1Preview.c_str(),
			arg2Preview.c_str(),
			arg3Preview.c_str(),
			arg4Preview.c_str());
		TraceTopKart551("build_runtime_cache_enter", index, returnAddress, detail, true);
		DumpTopKartBuildSnapshots("enter", index, returnAddress, returnAddressSlot, arg1, arg2, arg3, arg4, 0);
	}

	__int64 result = g_originalTopKartBuild551RuntimeCache ?
		g_originalTopKartBuild551RuntimeCache(arg1, arg2, arg3, arg4) : 0;

	if (shouldTrace) {
		std::string stackSlots = ReadStackSlotsText(returnAddressSlot, 12);
		char detail[1536] = {};
		snprintf(detail, sizeof(detail),
			"result=0x%llX resultLow=%u rcx=0x%llX rdx=0x%llX r8=0x%llX r9=0x%llX returnSlot=%p stackSlots=%s",
			static_cast<unsigned long long>(result),
			static_cast<unsigned int>(result & 0xFF),
			static_cast<unsigned long long>(arg1),
			static_cast<unsigned long long>(arg2),
			static_cast<unsigned long long>(arg3),
			static_cast<unsigned long long>(arg4),
			returnAddressSlot,
			stackSlots.c_str());
		TraceTopKart551("build_runtime_cache_leave", index, returnAddress, detail, false);
		DumpTopKartBuildSnapshots("leave", index, returnAddress, returnAddressSlot, arg1, arg2, arg3, arg4, result);
	}

	return result;
}

__int64 __fastcall HookedTopKartNotifyKartPhys()
{
	uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	LONG index = 0;
	bool shouldTrace = ShouldTraceTopKart551(&g_topKart551NotifyTraceCount, index);

	if (shouldTrace) {
		TraceTopKart551("notify_kart_phys_enter", index, returnAddress, "", true);
	}

	__int64 result = g_originalTopKartNotifyKartPhys ?
		g_originalTopKartNotifyKartPhys() : 0;

	if (shouldTrace) {
		char detail[256] = {};
		snprintf(detail, sizeof(detail),
			"result=%lld",
			static_cast<long long>(result));
		TraceTopKart551("notify_kart_phys_leave", index, returnAddress, detail, false);
	}

	return result;
}

void DumpSafe28346InnerBody(
	const char* direction,
	const unsigned char* safePayload,
	int safePayloadLen,
	unsigned short fallbackMsgId,
	const char* detail)
{
	if (!safePayload || safePayloadLen < static_cast<int>(kSafe28346InnerBodyOffset)) {
		return;
	}

	unsigned short innerMsg = ReadU16Safe(safePayload + 0x08);
	unsigned short innerLen = ReadU16Safe(safePayload + 0x0A);
	if (innerLen == 0 || safePayloadLen < static_cast<int>(kSafe28346InnerBodyOffset + innerLen)) {
		return;
	}

	unsigned short dumpMsg = NormalizeMsgId(innerMsg);
	if (!dumpMsg) {
		dumpMsg = fallbackMsgId;
	}
	DumpPacket(direction, safePayload + kSafe28346InnerBodyOffset, innerLen, dumpMsg, detail);
}

int __fastcall HookedUnpack(
	const unsigned char* packet,
	unsigned int packetLen,
	const void* key1,
	const void* key2,
	void* csHead,
	void* msgHead,
	unsigned char* outData,
	int* outLen,
	bool allowSpecialDecode,
	bool extendedMode,
	void* extendedResult)
{
	void* returnAddress = _ReturnAddress();
	uintptr_t callSiteRva = GetCallSiteRvaFromReturnAddress(returnAddress);
	int result = g_originalUnpack(
		packet,
		packetLen,
		key1,
		key2,
		csHead,
		msgHead,
		outData,
		outLen,
		allowSpecialDecode,
		extendedMode,
		extendedResult);
	int originalResult = result;
	MsgHeadSnapshot head = ReadMsgHeadSnapshot(msgHead);
	unsigned short msgId = NormalizeMsgId(head.msgId);
	unsigned short packetMsgId = ReadPacketMsgId(packet, static_cast<int>(packetLen));
	if (!msgId) {
		msgId = packetMsgId;
	}
	ArmPostLoginDump(msgId, "unpack");

	int actualOutLen = ReadIntSafe(outLen, -1);
	int outLenMinusClientHead = actualOutLen >= static_cast<int>(kClientMsgHeadSize) ?
		actualOutLen - static_cast<int>(kClientMsgHeadSize) : -1;
	const bool forcedAllow = ShouldForceAllowDirectMsg100(
		callSiteRva,
		originalResult,
		head,
		msgId,
		packetMsgId,
		actualOutLen,
		allowSpecialDecode,
		outData,
		outLen);
	if (forcedAllow) {
		result = 1;
	}

	char specialDetail[256] = { 0 };
	if (packetMsgId == 28346 || msgId == 28346 || head.msgId == 28346) {
		const bool modeReadable = outData && actualOutLen > 0x7DD4;
		const bool innerReadable = outData && actualOutLen >= 0x0C;
		unsigned char mode = modeReadable ? ReadU8Safe(outData + 0x7DD4) : 0;
		unsigned short innerMsg = innerReadable ? ReadU16Safe(outData + 0x08) : 0;
		unsigned short innerLen = innerReadable ? ReadU16Safe(outData + 0x0A) : 0;
		bool innerRangeOk = innerReadable && actualOutLen >= static_cast<int>(0x0C + innerLen);
		bool rewrittenFrom28346 = packetMsgId == 28346 && msgId != 28346;
		const char* specialOutcome = "no_rewrite";
		if (rewrittenFrom28346) {
			specialOutcome = "rewritten_msg_head";
		}
		else if (modeReadable && mode == 1 && (innerMsg == 100 || innerMsg == 223)) {
			specialOutcome = "safe_login_candidate_not_accepted";
		}
		else if (modeReadable && mode == 1 && innerMsg == 551) {
			specialOutcome = "kart_phys_notify_shape";
		}
		else if (modeReadable && mode == 0) {
			specialOutcome = "encrypted_msg_container";
		}
		snprintf(specialDetail, sizeof(specialDetail),
			" packetMsg=%u specialModeReadable=%d specialMode=%u innerMsgAt8=%u innerLenAtA=%u innerRangeOk=%d rewrittenFrom28346=%d specialOutcome=%s",
			static_cast<unsigned int>(packetMsgId),
			modeReadable ? 1 : 0,
			static_cast<unsigned int>(mode),
			static_cast<unsigned int>(innerMsg),
			static_cast<unsigned int>(innerLen),
			innerRangeOk ? 1 : 0,
			rewrittenFrom28346 ? 1 : 0,
			specialOutcome);
	}
	char detail[1024] = { 0 };
	snprintf(detail, sizeof(detail),
		"callRva=0x%llX caller=%s result=%d originalResult=%d forcedAllowMsg100=%d callerAction=%s rawLen=%u outLen=%d allowSpecialDecode=%d "
		"headValid=%d packetMsg=%u msgRaw=%u msg=%u type=%d seq=%d srcFe=%u dstFe=%u srcId=%d dstId=%d clientHeadSize=%u outLenMinusClientHead=%d gate=%s%s",
		static_cast<unsigned long long>(callSiteRva),
		DescribeUnpackCallSite(callSiteRva),
		result,
		originalResult,
		forcedAllow ? 1 : 0,
		DescribeUnpackCallerAction(callSiteRva, result),
		packetLen,
		actualOutLen,
		allowSpecialDecode ? 1 : 0,
		head.valid ? 1 : 0,
		static_cast<unsigned int>(packetMsgId),
		static_cast<unsigned int>(head.msgId),
		static_cast<unsigned int>(msgId),
		static_cast<int>(head.msgType),
		head.msgSeq,
		static_cast<unsigned int>(head.srcFe),
		static_cast<unsigned int>(head.dstFe),
		static_cast<int>(head.srcId),
		static_cast<int>(head.dstId),
		static_cast<unsigned int>(kClientMsgHeadSize),
		outLenMinusClientHead,
		DescribeUnpackMsgGate(msgId),
		specialDetail);
	TracePacketEvent("unpack", msgId, actualOutLen, detail);
	if (forcedAllow) {
		TracePacketEvent("force_allow_msg100", msgId, actualOutLen, detail);
		HookLog("Network debug force allow direct msg100 callRva=0x%llX rawLen=%u outLen=%d srcFe=%u dstFe=%u srcId=%d dstId=%d",
			static_cast<unsigned long long>(callSiteRva),
			packetLen,
			actualOutLen,
			static_cast<unsigned int>(head.srcFe),
			static_cast<unsigned int>(head.dstFe),
			static_cast<int>(head.srcId),
			static_cast<int>(head.dstId));
	}

	if (ShouldDumpRawPacket(msgId, packetMsgId)) {
		DumpPacket("recv_raw_packet", packet, static_cast<int>(packetLen), packetMsgId ? packetMsgId : msgId, detail);
	}

	if (IsLoginChainMsg(msgId) || IsTopModelFlowMsg(msgId)) {
		if (result && actualOutLen > 0) {
			DumpPacket("recv_plain_full", outData, actualOutLen, msgId, detail);
			DumpBodyAfterClientHead("recv_plain_body", outData, actualOutLen, msgId, detail);
			if (msgId == 28346 || packetMsgId == 28346) {
				DumpSafe28346InnerBody("recv_plain_28346_inner_body_at12", outData, actualOutLen, 28346, detail);
			}
		}
		else {
			DumpPacket("recv_unpack_failed_raw_packet", packet, static_cast<int>(packetLen), packetMsgId ? packetMsgId : msgId, detail);
			if (actualOutLen > 0) {
				DumpPacket("recv_unpack_failed_out_full", outData, actualOutLen, msgId, detail);
			}
		}
	}
	return result;
}

int __fastcall HookedSpecialDecode(
	void* msgHead,
	const void* csHead,
	unsigned char* outData,
	int* outLen,
	void* aux)
{
	MsgHeadSnapshot beforeHead = ReadMsgHeadSnapshot(msgHead);
	int beforeOutLen = ReadIntSafe(outLen, -1);
	unsigned char mode = 0;
	unsigned short innerMsg = 0;
	unsigned short innerLen = 0;
	bool modeSafe = outData && SafeCopyRaw(&mode, outData + 0x7DD4, sizeof(mode));
	bool innerMsgSafe = outData && SafeCopyRaw(&innerMsg, outData + 0x08, sizeof(innerMsg));
	bool innerLenSafe = outData && SafeCopyRaw(&innerLen, outData + 0x0A, sizeof(innerLen));
	bool innerRangeOk = innerLenSafe && beforeOutLen >= static_cast<int>(0x0C + innerLen);
	bool modeInLen = beforeOutLen > 0x7DD4;
	const char* preShape = "unknown";
	if (modeSafe && mode == 0) {
		preShape = "encrypted_msg_mode0";
	}
	else if (modeSafe && mode == 1 && (innerMsg == 100 || innerMsg == 223)) {
		preShape = "safe_login_candidate";
	}
	else if (modeSafe && mode == 1 && innerMsg == 551) {
		preShape = "kart_phys_candidate";
	}
	else if (modeSafe && mode == 1) {
		preShape = "mode1_other_inner";
	}
	else if (modeSafe) {
		preShape = "mode_other";
	}

	char beforeDetail[512] = { 0 };
	snprintf(beforeDetail, sizeof(beforeDetail),
		"stage=before beforeMsgRaw=%u beforeMsg=%u beforeType=%d beforeOutLen=%d modeSafe=%d modeInLen=%d mode=%u innerMsgAt8=%u innerLenAtA=%u innerRangeOk=%d preShape=%s",
		static_cast<unsigned int>(beforeHead.msgId),
		static_cast<unsigned int>(NormalizeMsgId(beforeHead.msgId)),
		static_cast<int>(beforeHead.msgType),
		beforeOutLen,
		modeSafe ? 1 : 0,
		modeInLen ? 1 : 0,
		static_cast<unsigned int>(mode),
		static_cast<unsigned int>(innerMsg),
		static_cast<unsigned int>(innerLen),
		innerRangeOk ? 1 : 0,
		preShape);
	if (innerMsgSafe && innerMsg == 551) {
		TraceTopKart21681NetworkMilestone(
			"network_special_decode",
			innerMsg,
			"before",
			beforeOutLen,
			(std::numeric_limits<int>::min)());
	}
	if (beforeOutLen > 0) {
		DumpPacket("special28346_before_full", outData, beforeOutLen, 28346, beforeDetail);
		DumpSafe28346InnerBody("special28346_before_inner_body_at12", outData, beforeOutLen, 28346, beforeDetail);
	}

	int result = g_originalSpecialDecode(msgHead, csHead, outData, outLen, aux);

	MsgHeadSnapshot afterHead = ReadMsgHeadSnapshot(msgHead);
	unsigned short afterMsg = NormalizeMsgId(afterHead.msgId);
	int afterOutLen = ReadIntSafe(outLen, -1);
	bool rewrittenToInner = afterMsg != 28346 && (afterMsg == 100 || afterMsg == 223 || afterMsg == innerMsg);
	const char* postOutcome = "no_rewrite";
	if (rewrittenToInner) {
		postOutcome = "rewritten_to_inner";
	}
	else if (result == 0) {
		postOutcome = "special_decode_return0";
	}
	else if (modeSafe && mode == 1 && (innerMsg == 100 || innerMsg == 223)) {
		postOutcome = "candidate_not_verified";
	}

	char detail[768] = { 0 };
	snprintf(detail, sizeof(detail),
		"result=%d beforeMsgRaw=%u beforeMsg=%u beforeType=%d beforeOutLen=%d "
		"modeSafe=%d modeInLen=%d mode=%u innerMsgAt8=%u innerLenAtA=%u innerMsgSafe=%d innerLenSafe=%d innerRangeOk=%d preShape=%s "
		"afterMsgRaw=%u afterMsg=%u afterType=%d afterOutLen=%d rewrittenToInner=%d postOutcome=%s",
		result,
		static_cast<unsigned int>(beforeHead.msgId),
		static_cast<unsigned int>(NormalizeMsgId(beforeHead.msgId)),
		static_cast<int>(beforeHead.msgType),
		beforeOutLen,
		modeSafe ? 1 : 0,
		modeInLen ? 1 : 0,
		static_cast<unsigned int>(mode),
		static_cast<unsigned int>(innerMsg),
		static_cast<unsigned int>(innerLen),
		innerMsgSafe ? 1 : 0,
		innerLenSafe ? 1 : 0,
		innerRangeOk ? 1 : 0,
		preShape,
		static_cast<unsigned int>(afterHead.msgId),
		static_cast<unsigned int>(afterMsg),
		static_cast<int>(afterHead.msgType),
		afterOutLen,
		rewrittenToInner ? 1 : 0,
		postOutcome);
	TracePacketEvent("special28346", afterMsg ? afterMsg : 28346, afterOutLen, detail);
	if (innerMsgSafe && innerMsg == 551) {
		TraceTopKart21681NetworkMilestone(
			"network_special_decode",
			innerMsg,
			"after",
			afterOutLen,
			result);
	}

	if (afterOutLen > 0) {
		unsigned short dumpMsg = afterMsg ? afterMsg : 28346;
		DumpPacket("special28346_after_full", outData, afterOutLen, dumpMsg, detail);
		DumpBodyAfterClientHead("special28346_after_body", outData, afterOutLen, dumpMsg, detail);
		DumpSafe28346InnerBody("special28346_after_inner_body_at12", outData, afterOutLen, dumpMsg, detail);
	}
	return result;
}

int __fastcall HookedParserQueueDispatch(
	void* parser,
	unsigned int msgId,
	const void* body,
	int bodyLen)
{
	uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
	uintptr_t returnAddressSlot = reinterpret_cast<uintptr_t>(_AddressOfReturnAddress());
	unsigned short normalizedMsgId = NormalizeMsgId(static_cast<unsigned short>(msgId));
	const bool traceModelFlow = IsTopModelFlowMsg(normalizedMsgId);
	const bool traceOfficialInitFlow = IsOfficialOneStopMsg(normalizedMsgId);
	const bool traceTreasureBoxFlow = normalizedMsgId == 168 || normalizedMsgId == 190;
	LONG topKart100Index = 0;
	bool shouldTraceTopKart100 = normalizedMsgId == 100 &&
		ShouldTraceTopKart100(&g_topKart100ParserTraceCount, topKart100Index);
	const auto parserBytes = reinterpret_cast<const unsigned char*>(parser);
	ParserHandlerSnapshot beforeSnapshot = ReadParserHandlerSnapshot(parser, normalizedMsgId);
	uintptr_t queueBlocksBefore = parserBytes ? ReadPtrSafe(parserBytes + 0x20) : 0;
	uintptr_t queueMaskBefore = parserBytes ? ReadPtrSafe(parserBytes + 0x28) : 0;
	uintptr_t queueHeadBefore = parserBytes ? ReadPtrSafe(parserBytes + 0x30) : 0;
	uintptr_t queueCountBefore = parserBytes ? ReadPtrSafe(parserBytes + 0x38) : 0;
	if (shouldTraceTopKart100) {
		DumpTopKart100ParserDispatchSnapshots(
			"before",
			topKart100Index,
			returnAddress,
			returnAddressSlot,
			parser,
			msgId,
			normalizedMsgId,
			body,
			bodyLen,
			beforeSnapshot,
			queueBlocksBefore,
			queueMaskBefore,
			queueHeadBefore,
			queueCountBefore,
			0);
	}

	TraceTopKart21681NetworkMilestone(
		"network_parser_queue_dispatch",
		normalizedMsgId,
		"before",
		bodyLen,
		(std::numeric_limits<int>::min)());
	int result = g_originalParserQueueDispatch(parser, msgId, body, bodyLen);
	TraceTopKart21681NetworkMilestone(
		"network_parser_queue_dispatch",
		normalizedMsgId,
		"after",
		bodyLen,
		result);
#ifdef PACKETDUMP_OP4_COLLECTOR
	NotifyOp4CollectorResponse(normalizedMsgId, body, bodyLen);
#endif

	ParserHandlerSnapshot afterSnapshot = ReadParserHandlerSnapshot(parser, normalizedMsgId);
	uintptr_t queueBlocksAfter = parserBytes ? ReadPtrSafe(parserBytes + 0x20) : 0;
	uintptr_t queueMaskAfter = parserBytes ? ReadPtrSafe(parserBytes + 0x28) : 0;
	uintptr_t queueHeadAfter = parserBytes ? ReadPtrSafe(parserBytes + 0x30) : 0;
	uintptr_t queueCountAfter = parserBytes ? ReadPtrSafe(parserBytes + 0x38) : 0;
	bool innerEarlyReturnCandidate = result == 1 || result == -4 || result == -5;
	if (shouldTraceTopKart100) {
		DumpTopKart100ParserDispatchSnapshots(
			"after",
			topKart100Index,
			returnAddress,
			returnAddressSlot,
			parser,
			msgId,
			normalizedMsgId,
			body,
			bodyLen,
			afterSnapshot,
			queueBlocksAfter,
			queueMaskAfter,
			queueHeadAfter,
			queueCountAfter,
			result);
	}

	if (traceModelFlow || traceOfficialInitFlow || traceTreasureBoxFlow) {
		std::string bodyIdHits = traceModelFlow ? FindDebugModelItemHitsText(body, bodyLen) : std::string("-");
		char detail[3072] = { 0 };
		snprintf(detail, sizeof(detail),
			"parserDispatchResult=%d innerEarlyReturnCandidate=%d rawMsg=%u msg=%u body=%p bodyLen=%d parser=%p "
			"runtimeBefore=%p fastPathBefore=%u handlerBefore=%d nodeBefore=%p nodeMsgBefore=%u countBefore=%llu "
			"queueBlocksBefore=%p queueMaskBefore=0x%llX queueHeadBefore=%llu queueCountBefore=%llu "
			"runtimeAfter=%p fastPathAfter=%u handlerAfter=%d nodeAfter=%p nodeMsgAfter=%u countAfter=%llu "
			"queueBlocksAfter=%p queueMaskAfter=0x%llX queueHeadAfter=%llu queueCountAfter=%llu officialInitFlow=%d modelFlow=%d bodyIdHits=%s",
			result,
			innerEarlyReturnCandidate ? 1 : 0,
			msgId,
			static_cast<unsigned int>(normalizedMsgId),
			body,
			bodyLen,
			parser,
			reinterpret_cast<void*>(beforeSnapshot.runtime),
			static_cast<unsigned int>(beforeSnapshot.fastPathFlag),
			beforeSnapshot.handlerFound ? 1 : 0,
			reinterpret_cast<void*>(beforeSnapshot.node),
			beforeSnapshot.nodeMsg,
			static_cast<unsigned long long>(beforeSnapshot.count),
			reinterpret_cast<void*>(queueBlocksBefore),
			static_cast<unsigned long long>(queueMaskBefore),
			static_cast<unsigned long long>(queueHeadBefore),
			static_cast<unsigned long long>(queueCountBefore),
			reinterpret_cast<void*>(afterSnapshot.runtime),
			static_cast<unsigned int>(afterSnapshot.fastPathFlag),
			afterSnapshot.handlerFound ? 1 : 0,
			reinterpret_cast<void*>(afterSnapshot.node),
			afterSnapshot.nodeMsg,
			static_cast<unsigned long long>(afterSnapshot.count),
			reinterpret_cast<void*>(queueBlocksAfter),
			static_cast<unsigned long long>(queueMaskAfter),
			static_cast<unsigned long long>(queueHeadAfter),
			static_cast<unsigned long long>(queueCountAfter),
			traceOfficialInitFlow ? 1 : 0,
			traceModelFlow ? 1 : 0,
			bodyIdHits.c_str());
		TracePacketEvent("parser_queue_dispatch", normalizedMsgId, bodyLen, detail);
	}
	if (body && bodyLen > 0) {
		DumpPacket("parser_queue_dispatch_body", body, bodyLen, normalizedMsgId, "");
	}

	return result;
}

int __fastcall HookedInnerDispatch(
	short msgId,
	const unsigned char* body,
	int bodyLen,
	void* outData,
	void* outLen,
	void* aux,
	int arg7,
	bool extendedMode,
	int extendedFlags,
	void* extendedContext)
{
	const unsigned short normalizedMsgId = NormalizeMsgId(static_cast<unsigned short>(msgId));
	if (IsAntiCheckLightProfile()) {
		const int result = g_originalInnerDispatch(
			msgId,
			body,
			bodyLen,
			outData,
			outLen,
			aux,
			arg7,
			extendedMode,
			extendedFlags,
			extendedContext);
		if (IsLightProbeMsg(normalizedMsgId)) {
			RecordLightPacket(
				LightPacketDirection::InnerDispatch,
				normalizedMsgId,
				body,
				bodyLen);
		}
		return result;
	}

	// InnerDispatch runs on the receive path for every packet. Keep the normal
	// path free of synchronous trace/file I/O and only inspect the panel reply
	// that is missing from the parser-queue capture.
	if (normalizedMsgId != 122) {
		return g_originalInnerDispatch(
			msgId,
			body,
			bodyLen,
			outData,
			outLen,
			aux,
			arg7,
			extendedMode,
			extendedFlags,
			extendedContext);
	}

	void* returnAddress = _ReturnAddress();
	uintptr_t callSiteRva = GetCallSiteRvaFromReturnAddress(returnAddress);
	unsigned short firstBody16 = body && bodyLen >= 2 ? ReadU16Safe(body) : 0;
	unsigned short firstBody16Swap = Swap16(firstBody16);
	const uintptr_t directInnerDispatchCallRva =
		!g_networkLayout.innerDispatchCallRvas.empty() ? g_networkLayout.innerDispatchCallRvas[0] : kInnerDispatchCallRvas[0];
	const uintptr_t mode0InnerDispatchCallRva =
		g_networkLayout.innerDispatchCallRvas.size() > 1 ? g_networkLayout.innerDispatchCallRvas[1] : kInnerDispatchCallRvas[1];
	const bool traceModelFlow = IsTopModelFlowMsg(normalizedMsgId);
	const bool traceOfficialInitFlow = IsOfficialOneStopMsg(normalizedMsgId);
	std::string bodyIdHits = traceModelFlow ? FindDebugModelItemHitsText(body, bodyLen) : std::string("-");
	const bool traceParserState = callSiteRva == directInnerDispatchCallRva && (traceModelFlow || traceOfficialInitFlow);
	const uintptr_t parserGlobalAddress = g_networkBase ? g_networkBase + kParserGlobalRva : 0;
	uintptr_t parserBefore = 0;
	ParserHandlerSnapshot parserBeforeSnapshot = {};
	if (traceParserState && parserGlobalAddress) {
		parserBefore = ReadPtrSafe(reinterpret_cast<const void*>(parserGlobalAddress));
		parserBeforeSnapshot = ReadParserHandlerSnapshot(reinterpret_cast<void*>(parserBefore), normalizedMsgId);
	}
	if (traceParserState) {
		TraceInnerDispatchSwitchState("inner_dispatch_before_original", normalizedMsgId);
	}

	TraceTopKart21681NetworkMilestone(
		"network_inner_dispatch",
		normalizedMsgId,
		"before",
		bodyLen,
		(std::numeric_limits<int>::min)());
	int result = g_originalInnerDispatch(
		msgId,
		body,
		bodyLen,
		outData,
		outLen,
		aux,
		arg7,
		extendedMode,
		extendedFlags,
		extendedContext);
	int originalResult = result;
	TraceTopKart21681NetworkMilestone(
		"network_inner_dispatch",
		normalizedMsgId,
		"after",
		bodyLen,
		result);
	int actualOutLen = ReadIntSafe(static_cast<const int*>(outLen), -1);
	bool forcedInnerDispatch = false;
	bool forcedCopiedFullMsg = false;
	uintptr_t parserAfter = 0;
	ParserHandlerSnapshot parserAfterSnapshot = {};
	if (traceParserState && parserGlobalAddress) {
		parserAfter = ReadPtrSafe(reinterpret_cast<const void*>(parserGlobalAddress));
		parserAfterSnapshot = ReadParserHandlerSnapshot(reinterpret_cast<void*>(parserAfter), normalizedMsgId);
		DumpParserRegisteredIdsOnce(parserAfter, normalizedMsgId);
	}
	if (traceParserState) {
		TraceInnerDispatchSwitchState("inner_dispatch_after_original", normalizedMsgId);
	}

	if (kForceAllowInnerDispatchLoginDebug &&
		callSiteRva == mode0InnerDispatchCallRva &&
		(normalizedMsgId == 100 || normalizedMsgId == 223) &&
		originalResult == 0 &&
		body &&
		bodyLen > 0)
	{
		const unsigned char* fullMsg = body - kClientMsgHeadSize;
		const int fullMsgLen = bodyLen + static_cast<int>(kClientMsgHeadSize);
		if (outData && outLen && fullMsgLen > 0 && fullMsgLen <= kMaxForcedInnerDispatchCopy &&
			SafeCopyRaw(outData, fullMsg, static_cast<size_t>(fullMsgLen)))
		{
			*static_cast<int*>(outLen) = fullMsgLen;
			actualOutLen = fullMsgLen;
			forcedCopiedFullMsg = true;
		}
		result = 1;
		forcedInnerDispatch = true;
	}

	char detail[4096] = { 0 };
	if (traceParserState) {
		snprintf(detail, sizeof(detail),
			"callRva=0x%llX result=%d originalResult=%d forcedInnerDispatch=%d forcedCopiedFullMsg=%d rawMsg=%u msg=%u bodyLen=%d outLen=%d firstBody16Raw=%u firstBody16=%u aux=%p arg7=%d "
			"parserGlobal=%p parserBefore=%p parserRuntimeBefore=%p parserFastPathFlagBefore=%u handlerBefore=%d nodeBefore=%p nodeMsgBefore=%u countBefore=%llu maskBefore=0x%llX bucketsBefore=%p sentinelBefore=%p "
			"parserAfter=%p parserRuntimeAfter=%p parserFastPathFlagAfter=%u handlerAfter=%d nodeAfter=%p nodeMsgAfter=%u countAfter=%llu maskAfter=0x%llX bucketsAfter=%p sentinelAfter=%p "
			"officialInitFlow=%d modelFlow=%d bodyIdHits=%s",
			static_cast<unsigned long long>(callSiteRva),
			result,
			originalResult,
			forcedInnerDispatch ? 1 : 0,
			forcedCopiedFullMsg ? 1 : 0,
			static_cast<unsigned int>(static_cast<unsigned short>(msgId)),
			static_cast<unsigned int>(normalizedMsgId),
			bodyLen,
			actualOutLen,
			static_cast<unsigned int>(firstBody16),
			static_cast<unsigned int>(firstBody16Swap),
			aux,
			arg7,
			reinterpret_cast<void*>(parserGlobalAddress),
			reinterpret_cast<void*>(parserBefore),
			reinterpret_cast<void*>(parserBeforeSnapshot.runtime),
			static_cast<unsigned int>(parserBeforeSnapshot.fastPathFlag),
			parserBeforeSnapshot.handlerFound ? 1 : 0,
			reinterpret_cast<void*>(parserBeforeSnapshot.node),
			parserBeforeSnapshot.nodeMsg,
			static_cast<unsigned long long>(parserBeforeSnapshot.count),
			static_cast<unsigned long long>(parserBeforeSnapshot.mask),
			reinterpret_cast<void*>(parserBeforeSnapshot.buckets),
			reinterpret_cast<void*>(parserBeforeSnapshot.sentinel),
			reinterpret_cast<void*>(parserAfter),
			reinterpret_cast<void*>(parserAfterSnapshot.runtime),
			static_cast<unsigned int>(parserAfterSnapshot.fastPathFlag),
			parserAfterSnapshot.handlerFound ? 1 : 0,
			reinterpret_cast<void*>(parserAfterSnapshot.node),
			parserAfterSnapshot.nodeMsg,
			static_cast<unsigned long long>(parserAfterSnapshot.count),
			static_cast<unsigned long long>(parserAfterSnapshot.mask),
			reinterpret_cast<void*>(parserAfterSnapshot.buckets),
			reinterpret_cast<void*>(parserAfterSnapshot.sentinel),
			traceOfficialInitFlow ? 1 : 0,
			traceModelFlow ? 1 : 0,
			bodyIdHits.c_str());
	}
	else {
		snprintf(detail, sizeof(detail),
			"callRva=0x%llX result=%d originalResult=%d forcedInnerDispatch=%d forcedCopiedFullMsg=%d rawMsg=%u msg=%u bodyLen=%d outLen=%d firstBody16Raw=%u firstBody16=%u aux=%p arg7=%d officialInitFlow=%d modelFlow=%d bodyIdHits=%s",
			static_cast<unsigned long long>(callSiteRva),
			result,
			originalResult,
			forcedInnerDispatch ? 1 : 0,
			forcedCopiedFullMsg ? 1 : 0,
			static_cast<unsigned int>(static_cast<unsigned short>(msgId)),
			static_cast<unsigned int>(normalizedMsgId),
			bodyLen,
			actualOutLen,
			static_cast<unsigned int>(firstBody16),
			static_cast<unsigned int>(firstBody16Swap),
			aux,
			arg7,
			traceOfficialInitFlow ? 1 : 0,
			traceModelFlow ? 1 : 0,
			bodyIdHits.c_str());
	}
	TracePacketEvent("inner_dispatch", normalizedMsgId, bodyLen, detail);
	if (body && bodyLen > 0 && ShouldDumpPacket(normalizedMsgId)) {
		DumpPacket("inner_dispatch_body", body, bodyLen, normalizedMsgId, detail);
		if (callSiteRva == mode0InnerDispatchCallRva &&
			(IsLoginChainMsg(normalizedMsgId) || IsTopModelFlowMsg(normalizedMsgId)) &&
			bodyLen <= 0x400000 - static_cast<int>(kClientMsgHeadSize)) {
			DumpPacket("inner_dispatch_mode0_fullmsg_body_minus14", body - kClientMsgHeadSize, bodyLen + static_cast<int>(kClientMsgHeadSize), normalizedMsgId, detail);
		}
	}
	if (outData && actualOutLen > 0 && static_cast<size_t>(actualOutLen) <= kMaxBinaryDumpBytes) {
		DumpPacket("inner_dispatch_parsed_object", outData, actualOutLen, normalizedMsgId, detail);
	}
	return result;
}

int __fastcall HookedPack(
	unsigned char* outPacket,
	int* outPacketLen,
	unsigned int seq,
	unsigned int cmd,
	unsigned char* srcObject,
	int arg6,
	const void* key,
	void* extraData,
	unsigned char extraLen,
	unsigned short msgId,
	short msgType,
	char flag1,
	char flag2,
	short arg14,
	short arg15,
	const void* plainBody,
	int plainBodyLen,
	int arg18,
	char arg19)
{
	int previousDepth = g_packDepth;
	unsigned short previousMsgId = g_packMsgId;
	unsigned short normalizedMsgId = NormalizeMsgId(msgId);
	g_packDepth = previousDepth + 1;
	g_packMsgId = normalizedMsgId;
	ArmPostLoginDump(normalizedMsgId, "pack");

	char beforeDetail[384] = { 0 };
	snprintf(beforeDetail, sizeof(beforeDetail),
		"stage=before seq=%u cmd=%u msgRaw=%u msg=%u type=%d bodyLen=%d extraLen=%u flags=%d/%d arg6=%d arg14=%d arg15=%d arg18=%d arg19=%d",
		seq,
		cmd,
		static_cast<unsigned int>(msgId),
		static_cast<unsigned int>(normalizedMsgId),
		static_cast<int>(msgType),
		plainBodyLen,
		static_cast<unsigned int>(extraLen),
		static_cast<int>(flag1),
		static_cast<int>(flag2),
		arg6,
		static_cast<int>(arg14),
		static_cast<int>(arg15),
		arg18,
		static_cast<int>(arg19));
	if (plainBody && plainBodyLen > 0 && ShouldDumpPacket(normalizedMsgId)) {
		DumpPacket("pack_before_body", plainBody, plainBodyLen, normalizedMsgId, beforeDetail);
	}
	if (extraData && extraLen > 0 && ShouldDumpPacket(normalizedMsgId)) {
		DumpPacket("pack_before_extra", extraData, static_cast<int>(extraLen), normalizedMsgId, beforeDetail);
	}

	TraceTopKart21681NetworkMilestone(
		"network_pack",
		normalizedMsgId,
		"before",
		plainBodyLen,
		(std::numeric_limits<int>::min)());
	int result = g_originalPack(outPacket, outPacketLen, seq, cmd, srcObject, arg6, key, extraData, extraLen,
		msgId, msgType, flag1, flag2, arg14, arg15, plainBody, plainBodyLen, arg18, arg19);
	g_packDepth = previousDepth;
	g_packMsgId = previousMsgId;
	TraceTopKart21681NetworkMilestone(
		"network_pack",
		normalizedMsgId,
		"after",
		plainBodyLen,
		result);

	int actualOutPacketLen = ReadIntSafe(outPacketLen, -1);
	unsigned short packetMsgId = ReadPacketMsgId(outPacket, actualOutPacketLen);
	unsigned short dumpMsgId = packetMsgId ? packetMsgId : normalizedMsgId;
	char detail[384] = { 0 };
	snprintf(detail, sizeof(detail),
		"stage=after result=%d seq=%u cmd=%u msgRaw=%u msg=%u packetMsg=%u type=%d outLen=%d bodyLen=%d extraLen=%u flags=%d/%d arg6=%d arg14=%d arg15=%d arg18=%d arg19=%d",
		result,
		seq,
		cmd,
		static_cast<unsigned int>(msgId),
		static_cast<unsigned int>(normalizedMsgId),
		static_cast<unsigned int>(packetMsgId),
		msgType,
		actualOutPacketLen,
		plainBodyLen,
		static_cast<unsigned int>(extraLen),
		static_cast<int>(flag1),
		static_cast<int>(flag2),
		arg6,
		static_cast<int>(arg14),
		static_cast<int>(arg15),
		arg18,
		static_cast<int>(arg19));
	TracePacketEvent("pack", dumpMsgId, plainBodyLen, detail);

	if (srcObject && IsNativePackDiagnosticMsg(normalizedMsgId)) {
		DumpPacket("pack_src_object", srcObject, 0x100, normalizedMsgId, detail);
	}
	if (result && outPacket && actualOutPacketLen > 0 && IsNativePackDiagnosticMsg(normalizedMsgId)) {
		DumpPacket("pack_after_packet", outPacket, actualOutPacketLen, normalizedMsgId, detail);
	}
	if (result && outPacket && actualOutPacketLen > 0 && ShouldDumpRawPacket(normalizedMsgId, packetMsgId)) {
		DumpPacket("pack_after_packet", outPacket, actualOutPacketLen, dumpMsgId, detail);
	}
	return result;
}

int __fastcall HookedSafePacketBuilder(
	short msgId,
	int seq,
	const void* srcObject,
	unsigned char* outSafePayload)
{
	unsigned short normalizedBeforeMsgId = NormalizeMsgId(static_cast<unsigned short>(msgId));
	if (g_captureMsg123Only && normalizedBeforeMsgId != 123) {
		return g_originalSafePacketBuilder(msgId, seq, srcObject, outSafePayload);
	}
	const LONG msg123Index = normalizedBeforeMsgId == 123
		? InterlockedIncrement(&g_msg123SourceDumpCount) : 0;
	const bool captureCurrentMsg123 = msg123Index > 0;
	if (srcObject && captureCurrentMsg123) {
		char sourceDetail[192] = {};
		snprintf(sourceDetail, sizeof(sourceDetail),
			"stage=before msg123Index=%ld seq=%d plainObjectLen=42213",
			msg123Index,
			seq);
		DumpPacket("safe123_plain_source", srcObject, 42213, 123, sourceDetail);
	}
	else if (srcObject && ShouldDumpSafeBuilderSourceMsg(normalizedBeforeMsgId)) {
		std::string quickRpcHint = normalizedBeforeMsgId == 24251
			? ExtractQuickRpcNameFromBytes(reinterpret_cast<const unsigned char*>(srcObject), 0x200)
			: std::string();
		char beforeDetail[512] = { 0 };
		snprintf(beforeDetail, sizeof(beforeDetail),
			"stage=before seq=%d msgRaw=%u msg=%u srcObject=%p outSafePayload=%p payloadLen=%u%s%s",
			seq,
			static_cast<unsigned int>(static_cast<unsigned short>(msgId)),
			static_cast<unsigned int>(normalizedBeforeMsgId),
			srcObject,
			outSafePayload,
			static_cast<unsigned int>(kSafe28346PayloadSize),
			quickRpcHint.empty() ? "" : " quickrpc=",
			quickRpcHint.empty() ? "" : quickRpcHint.c_str());
		DumpPacket("safe28346_builder_src_object_before", srcObject, 0x200, normalizedBeforeMsgId, beforeDetail);
	}
	int result = g_originalSafePacketBuilder(msgId, seq, srcObject, outSafePayload);
	unsigned short normalizedMsgId = NormalizeMsgId(static_cast<unsigned short>(msgId));
	unsigned char mode = outSafePayload ? ReadU8Safe(outSafePayload + kSafe28346ModeOffset) : 0;
	unsigned short innerMsg = outSafePayload ? ReadU16Safe(outSafePayload + 0x08) : 0;
	unsigned short innerLen = outSafePayload ? ReadU16Safe(outSafePayload + 0x0A) : 0;
	bool innerRangeOk = outSafePayload && (kSafe28346InnerBodyOffset + innerLen <= kSafe28346PayloadSize);
	std::string quickRpcHint = normalizedMsgId == 24251 && srcObject
		? ExtractQuickRpcNameFromBytes(reinterpret_cast<const unsigned char*>(srcObject), 0x200)
		: std::string();
	char detail[512] = { 0 };
	snprintf(detail, sizeof(detail),
		"result=%d seq=%d msgRaw=%u msg=%u srcObject=%p outSafePayload=%p payloadLen=%u mode=%u innerMsgAt8=%u innerLenAtA=%u innerRangeOk=%d%s%s",
		result,
		seq,
		static_cast<unsigned int>(static_cast<unsigned short>(msgId)),
		static_cast<unsigned int>(normalizedMsgId),
		srcObject,
		outSafePayload,
		static_cast<unsigned int>(kSafe28346PayloadSize),
		static_cast<unsigned int>(mode),
		static_cast<unsigned int>(innerMsg),
		static_cast<unsigned int>(innerLen),
		innerRangeOk ? 1 : 0,
		quickRpcHint.empty() ? "" : " quickrpc=",
		quickRpcHint.empty() ? "" : quickRpcHint.c_str());
	TracePacketEvent("safe28346_builder", normalizedMsgId, result ? static_cast<int>(kSafe28346PayloadSize) : 0, detail);
	if (srcObject && normalizedMsgId != 123 && ShouldDumpSafeBuilderSourceMsg(normalizedMsgId)) {
		DumpPacket("safe28346_builder_src_object_after", srcObject, 0x200, normalizedMsgId, detail);
	}
	if (result && outSafePayload && (normalizedMsgId != 123 || captureCurrentMsg123)) {
		DumpPacket("safe28346_builder_payload_full", outSafePayload, static_cast<int>(kSafe28346PayloadSize), 28346, detail);
		DumpSafe28346InnerBody("safe28346_builder_inner_body_at12", outSafePayload, static_cast<int>(kSafe28346PayloadSize), normalizedMsgId, detail);
	}
	return result;
}

int __fastcall HookedSerializeBody(
	unsigned int msgId,
	const void* srcObject,
	unsigned char* outBody,
	unsigned short* outBodyLen,
	void* aux)
{
	if (IsAntiCheckLightProfile()) {
		const int result = g_originalSerializeBody(msgId, srcObject, outBody, outBodyLen, aux);
		const unsigned short normalizedMsgId = NormalizeMsgId(static_cast<unsigned short>(msgId));
		const unsigned short bodyLen = ReadU16Safe(outBodyLen);
		if (result && IsLightProbeMsg(normalizedMsgId)) {
			RecordLightPacket(
				LightPacketDirection::SerializeBody,
				normalizedMsgId,
				outBody,
				bodyLen);
		}
		return result;
	}

	unsigned short beforeBodyLen = ReadU16Safe(outBodyLen);
	const unsigned short normalizedBeforeMsgId = NormalizeMsgId(static_cast<unsigned short>(msgId));
	TraceTopKart21681NetworkMilestone(
		"network_serialize_body",
		normalizedBeforeMsgId,
		"before",
		beforeBodyLen,
		(std::numeric_limits<int>::min)());
	int result = g_originalSerializeBody(msgId, srcObject, outBody, outBodyLen, aux);
	unsigned short normalizedMsgId = NormalizeMsgId(static_cast<unsigned short>(msgId));
	unsigned short bodyLen = ReadU16Safe(outBodyLen);
	TraceTopKart21681NetworkMilestone(
		"network_serialize_body",
		normalizedMsgId,
		"after",
		bodyLen,
		result);
	const bool nativeDiag = IsNativePackDiagnosticMsg(normalizedMsgId);
	const bool officialInitFlow = IsOfficialOneStopMsg(normalizedMsgId);
	const bool nativeSerializeInvalidLen = nativeDiag && bodyLen == kSerializeBodyDefaultLen;
	if (IsLoginChainMsg(normalizedMsgId) || normalizedMsgId == 28346 || nativeDiag || officialInitFlow) {
		std::string quickRpcHint = normalizedMsgId == 24251 && srcObject
			? ExtractQuickRpcNameFromBytes(reinterpret_cast<const unsigned char*>(srcObject), 0x200)
			: std::string();
		char detail[640] = { 0 };
		snprintf(detail, sizeof(detail),
			"result=%d msgRaw=%u msg=%u srcObject=%p outBody=%p outBodyLenPtr=%p beforeBodyLen=%u bodyLen=%u nativeSerializeInvalidLen=%d officialInitFlow=%d aux=%p%s%s",
			result,
			msgId,
			static_cast<unsigned int>(normalizedMsgId),
			srcObject,
			outBody,
			outBodyLen,
			static_cast<unsigned int>(beforeBodyLen),
			static_cast<unsigned int>(bodyLen),
			nativeSerializeInvalidLen ? 1 : 0,
			officialInitFlow ? 1 : 0,
			aux,
			quickRpcHint.empty() ? "" : " quickrpc=",
			quickRpcHint.empty() ? "" : quickRpcHint.c_str());
		TracePacketEvent("serialize_body", normalizedMsgId, bodyLen, detail);
		if ((nativeDiag || officialInitFlow) && srcObject) {
			DumpPacket("serialize_src_object", srcObject, officialInitFlow ? 0x200 : 0x100, normalizedMsgId, detail);
		}
		if ((nativeDiag || officialInitFlow) && aux) {
			DumpPacket("serialize_aux_preview", aux, 0x100, normalizedMsgId, detail);
		}
		if ((nativeDiag || officialInitFlow) && outBody && bodyLen > 0) {
			const int previewLen = std::min<int>(static_cast<int>(bodyLen), 0x100);
			DumpPacket("serialize_body_output_preview", outBody, previewLen, normalizedMsgId, detail);
		}
		if (result && outBody && bodyLen > 0 && normalizedMsgId == 28346) {
			DumpSafe28346InnerBody("serialize_body_28346_inner_body_at12", outBody, bodyLen, 28346, detail);
		}
	}
	if (result && outBody && bodyLen > 0) {
		DumpPacket("serialize_body_plain", outBody, bodyLen, normalizedMsgId, "");
	}
	return result;
}

bool CreateAndEnableHook(void* target, void* detour, void** original, const char* name)
{
	HookLog("Network %s hook install start target=%p detour=%p", name, target, detour);

	MH_STATUS status = MH_CreateHook(target, detour, original);
	if (status != MH_OK && status != MH_ERROR_ALREADY_CREATED) {
		HookLog("Network %s hook create failed status=%s target=%p", name, MH_StatusToString(status), target);
		return false;
	}
	HookLog("Network %s hook create status=%s original=%p", name, MH_StatusToString(status), original ? *original : nullptr);

	status = MH_EnableHook(target);
	if (status != MH_OK && status != MH_ERROR_ENABLED) {
		HookLog("Network %s hook enable failed status=%s target=%p", name, MH_StatusToString(status), target);
		return false;
	}

	HookLog("Network %s hook installed target=%p", name, target);
	return true;
}

void InstallNetworkCallSiteHooks(HMODULE networkModule)
{
	auto base = reinterpret_cast<uintptr_t>(networkModule);
	g_networkBase = base;
	if (!g_networkLayout.coreReady) {
		HookLog("Network dynamic core layout is not ready; call-site hooks skipped");
		return;
	}

	g_originalUnpack = reinterpret_cast<NetworkUnpackFn>(base + g_networkLayout.unpackRva);
	g_originalPack = reinterpret_cast<NetworkPackFn>(base + g_networkLayout.packRva);
	g_originalSpecialDecode = reinterpret_cast<NetworkSpecialDecodeFn>(base + g_networkLayout.specialDecodeRva);
	g_originalInnerDispatch = reinterpret_cast<NetworkInnerDispatchFn>(base + g_networkLayout.innerDispatchRva);
	g_originalSerializeBody = reinterpret_cast<NetworkSerializeBodyFn>(base + g_networkLayout.serializeBodyRva);
	g_originalParserQueueDispatch = reinterpret_cast<NetworkParserQueueDispatchFn>(base + g_networkLayout.parserQueueDispatchRva);
	g_originalSafePacketBuilder = reinterpret_cast<NetworkSafePacketBuilderFn>(base + g_networkLayout.safePacketBuilderRva);
	g_captureMsg123Only = IsAntiCheckLightProfile();

	PatchDirectCalls(base, g_networkLayout.innerDispatchCallRvas, g_networkLayout.innerDispatchRva, reinterpret_cast<void*>(&HookedInnerDispatch), "Network inner dispatch");
	PatchDirectCalls(base, g_networkLayout.specialDecodeCallRvas, g_networkLayout.specialDecodeRva, reinterpret_cast<void*>(&HookedSpecialDecode), "Network special 28346 decode");
	PatchDirectCalls(base, g_networkLayout.parserQueueDispatchCallRvas, g_networkLayout.parserQueueDispatchRva, reinterpret_cast<void*>(&HookedParserQueueDispatch), "Network parser queue dispatch");
	PatchDirectCalls(base, g_networkLayout.serializeBodyCallRvas, g_networkLayout.serializeBodyRva, reinterpret_cast<void*>(&HookedSerializeBody), "Network body serializer");
	PatchDirectCalls(base, g_networkLayout.safePacketBuilderCallRvas, g_networkLayout.safePacketBuilderRva, reinterpret_cast<void*>(&HookedSafePacketBuilder), "Network safe packet capture");
	HookLog("PacketDump build=20260828-specialdecode-diagnostic-v1");
	HookLog("Network diagnostic capture installed recv=special28346+parser_queue+inner122 send=serialize_body");
}

void InstallNetworkDumpHooks(HMODULE networkModule)
{
	if (InterlockedCompareExchange(&g_installStarted, 1, 0) != 0) {
		return;
	}

	std::string md5 = GetModuleMd5(networkModule);

	auto base = reinterpret_cast<uintptr_t>(networkModule);
	g_networkBase = base;
	g_networkLayout = ResolveNetworkHookLayout(networkModule, md5);
	HookLog("Network.dll accepted for dynamic packet dump md5=%s base=%p coreReady=%d",
		md5.empty() ? "<unavailable>" : md5.c_str(),
		networkModule,
		g_networkLayout.coreReady ? 1 : 0);

	if (!IsAntiCheckLightProfile()) {
		DumpNetworkCodeSnapshot(networkModule);
		StartParserStateMonitor();
	}
	InstallNetworkCallSiteHooks(networkModule);
}

void InstallTopKart100TraceHooks(HMODULE topKartModule, uintptr_t textRva, size_t textSize)
{
	if (!topKartModule || !textRva || textSize < 5) {
		return;
	}
	if (InterlockedCompareExchange(&g_topKart100TraceInstallStarted, 1, 0) != 0) {
		return;
	}

	const uintptr_t base = reinterpret_cast<uintptr_t>(topKartModule);
	g_originalTopKartProcessLogin =
		reinterpret_cast<TopKartProcessLoginFn>(base + kTopKart21273ProcessLoginRva);
	g_originalTopKartProcessItemInfoOnLogin =
		reinterpret_cast<TopKartProcessItemInfoOnLoginFn>(base + kTopKart21273ProcessItemInfoOnLoginRva);

	std::vector<uintptr_t> processLoginCallRvas = FindE8CallSitesToTarget(
		topKartModule,
		textRva,
		textSize,
		kTopKart21273ProcessLoginRva,
		"process_login");
	std::vector<uintptr_t> processItemInfoCallRvas = FindE8CallSitesToTarget(
		topKartModule,
		textRva,
		textSize,
		kTopKart21273ProcessItemInfoOnLoginRva,
		"process_item_info_on_login");

	size_t processLoginPatched = PatchTopKartDirectCalls(
		base,
		processLoginCallRvas,
		kTopKart21273ProcessLoginRva,
		reinterpret_cast<void*>(&HookedTopKartProcessLogin),
		"process_login");
	size_t processItemInfoPatched = PatchTopKartDirectCalls(
		base,
		processItemInfoCallRvas,
		kTopKart21273ProcessItemInfoOnLoginRva,
		reinterpret_cast<void*>(&HookedTopKartProcessItemInfoOnLogin),
		"process_item_info_on_login");
	size_t modelPreCallPatched = InstallTopKartModelPreCallTraceTargets(base);

	char detail[1024] = {};
	snprintf(detail, sizeof(detail),
		"processLoginRva=0x%llX found=%llu patched=%llu processItemInfoRva=0x%llX found=%llu patched=%llu modelPreCallPatched=%llu modelPreCallTotal=%llu watchKart=%u watchSkin=%u",
		static_cast<unsigned long long>(kTopKart21273ProcessLoginRva),
		static_cast<unsigned long long>(processLoginCallRvas.size()),
		static_cast<unsigned long long>(processLoginPatched),
		static_cast<unsigned long long>(kTopKart21273ProcessItemInfoOnLoginRva),
		static_cast<unsigned long long>(processItemInfoCallRvas.size()),
		static_cast<unsigned long long>(processItemInfoPatched),
		static_cast<unsigned long long>(modelPreCallPatched),
		static_cast<unsigned long long>(_countof(kTopKart21273ModelPreCallTargets)),
		kDebugCurrentKartId,
		kDebugCurrentSkinId);
	HookLog("Top-Kart 100 E8 trace install complete %s", detail);
	TraceTopKart100("install_complete", 0, 0, detail, false);
}

void InstallTopKart21681ObservationHooks(HMODULE topKartModule, uintptr_t textRva, size_t textSize)
{
	const uintptr_t base = reinterpret_cast<uintptr_t>(topKartModule);
	const TopKart551ObservationLayout& layout = kTopKart21681ObservationLayout;

	for (const TopKartCallWindow& window : kTopKart21681ObservedWindows) {
		DumpTopKartE8CallWindow(topKartModule, textRva, textSize, window);
	}

	const bool compactSignatureValid = ValidateTopKartUniqueSignature(
		topKartModule,
		textRva,
		textSize,
		layout.CompactDecode551Rva,
		kTopKart21681CompactDecodeSignature,
		"21681_compact_decode");
	const bool runtimeSignatureValid = ValidateTopKartUniqueSignature(
		topKartModule,
		textRva,
		textSize,
		layout.Build551RuntimeCacheRva,
		kTopKart21681RuntimeBuildSignature,
		"21681_runtime_build");
	const bool consumerSignatureValid = ValidateTopKartUniqueSignature(
		topKartModule,
		textRva,
		textSize,
		layout.PostDecodeConsumerRva,
		kTopKart21681PostDecodeConsumerSignature,
		"21681_post_decode_consumer");

	const uintptr_t compactReference = ReadPtrSafe(
		reinterpret_cast<const void*>(base + layout.CompactDecode551ReferenceRva));
	const uintptr_t consumerReference = ReadPtrSafe(
		reinterpret_cast<const void*>(base + layout.PostDecodeConsumerReferenceRva));
	const bool compactReferenceValid = compactReference == base + layout.CompactDecode551Rva;
	const bool consumerReferenceValid = consumerReference == base + layout.PostDecodeConsumerRva;

	const std::vector<uintptr_t> compactCalls = FindE8CallSitesToTarget(
		topKartModule,
		textRva,
		textSize,
		layout.CompactDecode551Rva,
		"21681_compact_decode");
	const std::vector<uintptr_t> runtimeCalls = FindE8CallSitesToTarget(
		topKartModule,
		textRva,
		textSize,
		layout.Build551RuntimeCacheRva,
		"21681_runtime_build");
	const std::vector<uintptr_t> consumerCalls = FindE8CallSitesToTarget(
		topKartModule,
		textRva,
		textSize,
		layout.PostDecodeConsumerRva,
		"21681_post_decode_consumer");

	unsigned char runtimeCallBytes[sizeof(kTopKart21681RuntimeBuildCallBytes)] = {};
	const bool runtimeCallReadable = SafeCopyRaw(
		runtimeCallBytes,
		reinterpret_cast<const void*>(base + layout.Build551RuntimeCacheCallRva),
		sizeof(runtimeCallBytes));
	const bool runtimeCallBytesValid = runtimeCallReadable &&
		memcmp(
			runtimeCallBytes,
			kTopKart21681RuntimeBuildCallBytes,
			sizeof(runtimeCallBytes)) == 0;
	const bool callTopologyValid =
		compactCalls.empty() &&
		runtimeCalls.size() == 1 &&
		runtimeCalls[0] == layout.Build551RuntimeCacheCallRva &&
		consumerCalls.empty();

	char staticDetail[2048] = {};
	snprintf(staticDetail, sizeof(staticDetail),
		"layout=%s compactRva=0x%llX compactSignature=%d compactReferenceRva=0x%llX compactReference=%s compactReferenceValid=%d compactDirectCalls=%llu "
		"runtimeRva=0x%llX runtimeSignature=%d runtimeCallRva=0x%llX runtimeCallBytes=%s runtimeCallBytesValid=%d runtimeDirectCalls=%llu "
		"consumerRva=0x%llX consumerSignature=%d consumerReferenceRva=0x%llX consumerReference=%s consumerReferenceValid=%d consumerDirectCalls=%llu topologyValid=%d",
		layout.Name,
		static_cast<unsigned long long>(layout.CompactDecode551Rva),
		compactSignatureValid ? 1 : 0,
		static_cast<unsigned long long>(layout.CompactDecode551ReferenceRva),
		FormatModuleRva(compactReference).c_str(),
		compactReferenceValid ? 1 : 0,
		static_cast<unsigned long long>(compactCalls.size()),
		static_cast<unsigned long long>(layout.Build551RuntimeCacheRva),
		runtimeSignatureValid ? 1 : 0,
		static_cast<unsigned long long>(layout.Build551RuntimeCacheCallRva),
		ReadMemoryHex(base + layout.Build551RuntimeCacheCallRva, sizeof(runtimeCallBytes)).c_str(),
		runtimeCallBytesValid ? 1 : 0,
		static_cast<unsigned long long>(runtimeCalls.size()),
		static_cast<unsigned long long>(layout.PostDecodeConsumerRva),
		consumerSignatureValid ? 1 : 0,
		static_cast<unsigned long long>(layout.PostDecodeConsumerReferenceRva),
		FormatModuleRva(consumerReference).c_str(),
		consumerReferenceValid ? 1 : 0,
		static_cast<unsigned long long>(consumerCalls.size()),
		callTopologyValid ? 1 : 0);
	HookLog("Top-Kart 551 observation static proof %s", staticDetail);
	TraceTopKart551("observation_static_proof", 0, 0, staticDetail, false);

	const bool staticProofValid =
		compactSignatureValid &&
		runtimeSignatureValid &&
		consumerSignatureValid &&
		compactReferenceValid &&
		consumerReferenceValid &&
		runtimeCallBytesValid &&
		callTopologyValid;
	if (!staticProofValid) {
		HookLog("Top-Kart 551 observation entry hooks skipped: strict static proof failed layout=%s", layout.Name);
		TraceTopKart551("observation_fail_closed", 0, 0, staticDetail, false);
		return;
	}

	InterlockedExchange(&g_topKart21681ObservationActive, 0);
	char installDetail[768] = {};
	snprintf(installDetail, sizeof(installDetail),
		"layout=%s mode=static_proof_only entryHooks=0 runtimeDirectCallHook=0 "
		"networkMilestones=0 semanticOverrides=0 runtimeCallerAbiEvidence=rcx_rdx_r8_r9_plus_7_stack_slots",
		layout.Name);
	HookLog("Top-Kart 551 static-only validation complete %s", installDetail);
	TraceTopKart551("static_only_validation_complete", 0, 0, installDetail, false);
}

void InstallTopKart551TraceHooks(HMODULE topKartModule)
{
	if (!topKartModule) {
		return;
	}
	if (InterlockedCompareExchange(&g_topKart551TraceInstallStarted, 1, 0) != 0) {
		return;
	}

	const uintptr_t base = reinterpret_cast<uintptr_t>(topKartModule);
	g_topKartBase = base;
	std::string md5 = GetModuleMd5(topKartModule);
	const size_t imageSize = GetModuleImageSize(topKartModule);
	const bool is21681Observation =
		!md5.empty() &&
		_stricmp(md5.c_str(), kTopKart21681ObservationLayout.Md5) == 0;
	const TopKart551TraceLayout* layout =
		is21681Observation ? nullptr : ResolveTopKart551TraceLayout(md5);
	if (!layout && !is21681Observation) {
		HookLog("Top-Kart 551 E8 trace skipped unsupported md5=%s base=%p imageSize=0x%llX",
			md5.empty() ? "<unavailable>" : md5.c_str(),
			topKartModule,
			static_cast<unsigned long long>(imageSize));
		return;
	}
	if (is21681Observation && imageSize != kTopKart21681ObservationLayout.ImageSize) {
		HookLog("Top-Kart 551 observation skipped image size mismatch md5=%s imageSize=0x%llX expected=0x%llX",
			md5.c_str(),
			static_cast<unsigned long long>(imageSize),
			static_cast<unsigned long long>(kTopKart21681ObservationLayout.ImageSize));
		return;
	}

	uintptr_t textRva = 0;
	size_t textSize = 0;
	if (!GetModuleTextRange(topKartModule, textRva, textSize)) {
		HookLog("Top-Kart 551 E8 trace skipped .text not found md5=%s base=%p",
			md5.c_str(),
			topKartModule);
		return;
	}

	if (is21681Observation) {
		HookLog("Top-Kart 551 observation install start layout=%s md5=%s base=%p imageSize=0x%llX textRva=0x%llX textSize=0x%llX",
			kTopKart21681ObservationLayout.Name,
			md5.c_str(),
			topKartModule,
			static_cast<unsigned long long>(imageSize),
			static_cast<unsigned long long>(textRva),
			static_cast<unsigned long long>(textSize));
		InstallTopKart21681ObservationHooks(topKartModule, textRva, textSize);
		return;
	}

	HookLog("Top-Kart 551 E8 trace install start layout=%s md5=%s base=%p imageSize=0x%llX textRva=0x%llX textSize=0x%llX",
		layout->Name,
		md5.c_str(),
		topKartModule,
		static_cast<unsigned long long>(imageSize),
		static_cast<unsigned long long>(textRva),
		static_cast<unsigned long long>(textSize));

	if (strcmp(layout->Name, "21273") == 0) {
		for (const TopKartCallWindow& window : kTopKart21273ObservedWindows) {
			DumpTopKartE8CallWindow(topKartModule, textRva, textSize, window);
		}
		InstallTopKart100TraceHooks(topKartModule, textRva, textSize);
	}

	g_originalTopKartCompactDecode551 = reinterpret_cast<TopKartCompactDecode551Fn>(base + layout->CompactDecode551Rva);
	g_originalTopKartBuild551RuntimeCache = reinterpret_cast<TopKartBuild551RuntimeCacheFn>(base + layout->Build551RuntimeCacheRva);
	g_originalTopKartNotifyKartPhys = reinterpret_cast<TopKartNotifyKartPhysFn>(base + layout->NotifyKartPhysRva);

	std::vector<uintptr_t> decodeCallRvas = FindE8CallSitesToTarget(
		topKartModule,
		textRva,
		textSize,
		layout->CompactDecode551Rva,
		"compact_decode551");
	std::vector<uintptr_t> buildCallRvas = FindE8CallSitesToTarget(
		topKartModule,
		textRva,
		textSize,
		layout->Build551RuntimeCacheRva,
		"build_runtime_cache");
	std::vector<uintptr_t> notifyCallRvas = FindE8CallSitesToTarget(
		topKartModule,
		textRva,
		textSize,
		layout->NotifyKartPhysRva,
		"notify_kart_phys");

	size_t decodePatched = PatchTopKartDirectCalls(
		base,
		decodeCallRvas,
		layout->CompactDecode551Rva,
		reinterpret_cast<void*>(&HookedTopKartCompactDecode551),
		"compact_decode551");
	size_t buildPatched = PatchTopKartDirectCalls(
		base,
		buildCallRvas,
		layout->Build551RuntimeCacheRva,
		reinterpret_cast<void*>(&HookedTopKartBuild551RuntimeCache),
		"build_runtime_cache");
	size_t notifyPatched = PatchTopKartDirectCalls(
		base,
		notifyCallRvas,
		layout->NotifyKartPhysRva,
		reinterpret_cast<void*>(&HookedTopKartNotifyKartPhys),
		"notify_kart_phys");
	size_t preCallPatched = 0;
	if (strcmp(layout->Name, "21273") == 0) {
		preCallPatched = InstallTopKartPreCallTraceTargets(base);
	}

	char detail[1200] = {};
	snprintf(detail, sizeof(detail),
		"layout=%s md5=%s base=%p imageSize=0x%llX textRva=0x%llX textSize=0x%llX "
		"decodeRva=0x%llX decodeFound=%llu decodePatched=%llu "
		"buildRva=0x%llX buildFound=%llu buildPatched=%llu "
		"notifyRva=0x%llX notifyFound=%llu notifyPatched=%llu "
		"preCallPatched=%llu preCallTotal=%llu",
		layout->Name,
		md5.c_str(),
		topKartModule,
		static_cast<unsigned long long>(imageSize),
		static_cast<unsigned long long>(textRva),
		static_cast<unsigned long long>(textSize),
		static_cast<unsigned long long>(layout->CompactDecode551Rva),
		static_cast<unsigned long long>(decodeCallRvas.size()),
		static_cast<unsigned long long>(decodePatched),
		static_cast<unsigned long long>(layout->Build551RuntimeCacheRva),
		static_cast<unsigned long long>(buildCallRvas.size()),
		static_cast<unsigned long long>(buildPatched),
		static_cast<unsigned long long>(layout->NotifyKartPhysRva),
		static_cast<unsigned long long>(notifyCallRvas.size()),
		static_cast<unsigned long long>(notifyPatched),
		static_cast<unsigned long long>(preCallPatched),
		static_cast<unsigned long long>(strcmp(layout->Name, "21273") == 0 ? _countof(kTopKart21273PreCallTargets) : 0));
	HookLog("Top-Kart 551 E8 trace install complete %s", detail);
	TraceTopKart551("install_complete", 0, 0, detail, false);
}

DWORD WINAPI NetworkDumpWorker(LPVOID)
{
	DWORD waited = 0;
	while (InterlockedCompareExchange(&g_stopWorker, 0, 0) == 0 && waited <= kNetworkWaitMs) {
		HMODULE networkModule = GetModuleHandleW(L"Network.dll");
		if (networkModule) {
			InstallNetworkDumpHooks(networkModule);
			break;
		}
		Sleep(kNetworkPollMs);
		waited += kNetworkPollMs;
	}

	if (InterlockedCompareExchange(&g_stopWorker, 0, 0) == 0 && waited > kNetworkWaitMs) {
		HookLog("Network.dll was not loaded after %lu ms; packet dump skipped", kNetworkWaitMs);
	}

	return 0;
}

DWORD WINAPI TopKart551TraceWorker(LPVOID)
{
	DWORD waited = 0;
	while (InterlockedCompareExchange(&g_stopWorker, 0, 0) == 0 && waited <= kTopKartWaitMs) {
		HMODULE topKartModule = GetModuleHandleW(L"Top-Kart.dll");
		if (topKartModule) {
			if (IsAntiCheckerTraceEnabled()) {
				InstallTopKartAntiCheckerTrace(topKartModule);
			}
			if (!IsAntiCheckLightProfile()) {
				InstallTopKart551TraceHooks(topKartModule);
			}
			break;
		}
		Sleep(kTopKartPollMs);
		waited += kTopKartPollMs;
	}

	if (InterlockedCompareExchange(&g_stopWorker, 0, 0) == 0 && waited > kTopKartWaitMs) {
		HookLog("Top-Kart.dll was not loaded after %lu ms; 551 E8 trace skipped", kTopKartWaitMs);
	}

	return 0;
}

DWORD WINAPI ProbeFlushWorker(LPVOID)
{
	constexpr DWORD kProbeFlushPollMs = 250;
	while (InterlockedCompareExchange(&g_stopWorker, 0, 0) == 0) {
		if (IsAntiCheckerTraceEnabled()) {
			FlushProbeSamples();
		}
		FlushStreamingDumps();
		Sleep(kProbeFlushPollMs);
	}
	if (IsAntiCheckerTraceEnabled()) {
		FlushProbeSamples();
	}
	FlushStreamingDumps();
	return 0;
}

DWORD WINAPI SharedDataDumpWorker(LPVOID)
{
	constexpr DWORD kSharedDataWaitMs = 30000;
	constexpr DWORD kSharedDataPollMs = 1000;
	DWORD waited = 0;
	while (InterlockedCompareExchange(&g_stopWorker, 0, 0) == 0 && waited <= kSharedDataWaitMs) {
		if (InterlockedCompareExchange(&g_sharedDataDumped, 0, 0) == 2) {
			break;
		}
		DumpLauncherSharedData();
		if (InterlockedCompareExchange(&g_sharedDataDumped, 0, 0) == 2) {
			break;
		}
		Sleep(kSharedDataPollMs);
		waited += kSharedDataPollMs;
	}
	return 0;
}
}

void StartNetworkDumpWorker(HMODULE ownerModule)
{
	ConfigureDumpProfile(ownerModule);
	InterlockedExchange(&g_stopWorker, 0);
	HookLog("Packet dump profile selected profile=%s", DumpProfileName());

	HANDLE networkThread = CreateThread(nullptr, 0, NetworkDumpWorker, nullptr, 0, nullptr);
	if (!networkThread) {
		HookLog("Network dump worker create failed error=%lu", GetLastError());
		return;
	}
	CloseHandle(networkThread);

	if (!IsAntiCheckLightProfile()) {
		HANDLE sharedDataThread = CreateThread(nullptr, 0, SharedDataDumpWorker, nullptr, 0, nullptr);
		if (!sharedDataThread) {
			HookLog("Launcher shared data worker create failed error=%lu", GetLastError());
			return;
		}
		CloseHandle(sharedDataThread);
	}

	HANDLE topKartTraceThread = CreateThread(nullptr, 0, TopKart551TraceWorker, nullptr, 0, nullptr);
	if (!topKartTraceThread) {
		HookLog("Top-Kart 551 E8 trace worker create failed error=%lu", GetLastError());
		return;
	}
	CloseHandle(topKartTraceThread);

	HANDLE flushThread = CreateThread(nullptr, 0, ProbeFlushWorker, nullptr, 0, nullptr);
	if (!flushThread) {
		HookLog("Packet dump flush worker create failed error=%lu", GetLastError());
		return;
	}
	CloseHandle(flushThread);
}

void StopNetworkDumpWorker()
{
	InterlockedExchange(&g_stopWorker, 1);
	RestoreTopKartAntiCheckerTrace();
}

#else

void StartNetworkDumpWorker(HMODULE)
{
	HookLog("Network packet dump is only implemented for the current x64 IDA build");
}

void StopNetworkDumpWorker()
{
}

#endif

#endif
