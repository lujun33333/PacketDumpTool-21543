#include "pch.h"

#include "topkart/topkart_mapid_probe.h"

#include "logging/hook_log.h"
#include "manual_fls_slot.h"
#include "network/code_patch.h"

#include <intrin.h>
#include <atomic>
#include <cstring>
#include <cstdio>
#include <memory>
#include <new>

namespace
{
constexpr DWORD kTopKartWaitMs = 120000;
constexpr DWORD kTopKartPollMs = 250;
constexpr bool kEnableRacePlacementProbe = false;

constexpr std::uintptr_t kPrepareGameBeginRva = 0x12041D0;
constexpr std::uintptr_t kLevelManagerEnterRva = 0x119C940;
constexpr std::uintptr_t kAuroraUgcCheckRva = 0x191310;
constexpr std::uintptr_t kCurrentMapInfoRva = 0x114EB20;
constexpr std::uintptr_t kRoadMapProcessorResetRva = 0xE27050;
constexpr std::uintptr_t kLoadMapResRva = 0xE2DE60;
constexpr std::uintptr_t kLoadMapDescRva = 0xE31580;
constexpr std::uintptr_t kLoadScRva = 0xE2CF60;
constexpr std::uintptr_t kApplySceneObjectsRva = 0xE2F840;
constexpr std::uintptr_t kLoadScCacheFetchRva = 0xE385B0;
constexpr std::uintptr_t kLoadScCacheExtractRva = 0xE2DD80;
constexpr std::uintptr_t kNifLoadRva = 0x8F2DF0;
constexpr std::uintptr_t kNifLoadScCoroutineCallRva = 0x1E2DFE2;
constexpr std::uintptr_t kNifLoadPickCallRva = 0xE2E16E;
constexpr std::uintptr_t kNifLoadPickShadowCallRva = 0xE2E669;
constexpr std::uintptr_t kNifNormalizePathRva = 0x8F4070;
constexpr std::uintptr_t kNifFetchResourceRva = 0x8F52B0;
constexpr std::uintptr_t kNifExtractRootRva = 0x8F4680;
constexpr std::uintptr_t kLuaFieldLookupRva = 0x15C65E0;
constexpr std::uintptr_t kRaceEnterPhySceneKartPhysRva = 0x120A270;
constexpr std::uintptr_t kRoadMapStartTransformRva = 0x1482AA0;
constexpr std::uintptr_t kRoadMapStartTransformVariantRva = 0x1482C50;
constexpr std::uintptr_t kProtectedIntEncodeRva = 0x92AA0;

constexpr std::uintptr_t kPrepareGameBeginCallRvas[] = {
	0x12074C3,
	0x1207E87,
};

constexpr std::uintptr_t kLevelManagerEnterCallRvas[] = {
	0x717B56,
	0x8479E7,
	0x8FF827,
	0x10619AB,
	0x120778C,
	0x120815B,
	0x1391422,
};

constexpr std::uintptr_t kAuroraUgcCheckCallRvas[] = {
	0xE2CFA9,
	0xE2DF73,
	0x1198085,
};

constexpr std::uintptr_t kRoadMapProcessorResetCallRvas[] = {
	0xE27855,
};

constexpr std::uintptr_t kLoadMapResCallRvas[] = {
	0xE27413,
};

constexpr std::uintptr_t kLoadScCallRvas[] = {
	0xE2DF21,
};

constexpr std::uintptr_t kLoadMapDescCallRvas[] = {
	0xE2DED9,
};

constexpr std::uintptr_t kLoadScCacheFetchCallRvas[] = {
	0xE2D7A0,
};

constexpr std::uintptr_t kLoadScCacheExtractCallRvas[] = {
	0xE2D7C4,
};

constexpr std::uintptr_t kApplySceneObjectsCallRvas[] = {
	0xE2DB27,
};

constexpr std::uintptr_t kNifLoadCallRvas[] = {
	0xE2D845,
	kNifLoadPickCallRva,
	kNifLoadPickShadowCallRva,
	0xE2F9BB,
	kNifLoadScCoroutineCallRva,
};

constexpr std::uintptr_t kNifNormalizePathCallRvas[] = {
	0x8F2E40,
};

constexpr std::uintptr_t kNifFetchResourceCallRvas[] = {
	0x8F2E6A,
};

constexpr std::uintptr_t kNifExtractRootCallRvas[] = {
	0x8F2E7F,
};

constexpr std::uintptr_t kMapDescFieldLookupCallRvas[] = {
	0xE31E34,
	0xE31E67,
};

constexpr std::uintptr_t kRaceEnterPhySceneKartPhysCallRvas[] = {
	0x1201E5F,
};

constexpr std::uintptr_t kRoadMapStartTransformCallRvas[] = {
	0x1208F68,
	0x1209399,
	0x1209610,
};

constexpr std::uintptr_t kRoadMapStartTransformVariantCallRvas[] = {
	0x12092DF,
};

constexpr std::uintptr_t kEnterPhySceneSetTransformCallRvas[] = {
	0x120A66F,
};

constexpr unsigned char kEnterPhySceneSetTransformExpectedBytes[] = {
	0xFF, 0x90, 0x70, 0x01, 0x00, 0x00,
};

constexpr std::size_t kNotifyTopMapOffset = 0x4;
constexpr std::size_t kNotifyGameIdOffset = 0x28;
constexpr std::size_t kNotifySeedOffset = 0x2C;
constexpr std::size_t kNotifyPlayerCountOffset = 0x30;
constexpr std::size_t kNotifyRaceTrackOrdersOffset = 0x31;
constexpr std::size_t kNotifyOuterBaseOffset = 0x8E65;
constexpr std::size_t kNotifyOuterSubOffset = 0x8E67;
constexpr std::size_t kNotifyShuttleMapNumOffset = 0x3450;
constexpr std::size_t kNotifyShuttleMapListOffset = 0x3451;
constexpr std::size_t kNotifyShuttleMapEntrySize = 25;
constexpr std::size_t kNotifyTimeShuttleGenOffset = 0x9F6A;
constexpr std::size_t kNotifyHasCustomSchemeOffset = 0xA7C3;
constexpr std::size_t kNotifyCustomSchemeMapNumOffset = 43106;
constexpr std::size_t kNotifyCustomSchemeMapListOffset = 43107;

constexpr std::size_t kRaceTrackOrderCount = 20;
constexpr std::size_t kLoggedShuttleMapCount = 8;
constexpr std::size_t kLoggedCustomMapCount = 3;
constexpr std::size_t kGlobalGameModeRva = 0x28276F8;
constexpr std::size_t kGlobalUgcByteRva = 0x2920937;
constexpr std::size_t kGameModeModeOffset = 0x2C80E;
constexpr std::size_t kGameModeSubModeOffset = 0x2C810;
constexpr std::size_t kCurrentMapConfigOffset = 0x68;
constexpr std::size_t kMapConfigMapIdOffset = 0x4;
constexpr std::size_t kMapConfigUgcWorkOffset = 43120;
constexpr std::size_t kMapPreLoadFlagOffset = 0x38;
constexpr std::size_t kMapPreLoadRootOffset = 0x40;
constexpr std::size_t kMapObjectListBeginOffset = 0x598;
constexpr std::size_t kMapObjectListEndOffset = 0x5A0;
constexpr std::size_t kMapObjectRecordSize = 0x60;
constexpr std::size_t kSceneAttachListBeginOffset = 0xC8;
constexpr std::size_t kSceneAttachListEndOffset = 0xD0;
constexpr std::size_t kStdStringLengthOffset = 0x18;
constexpr std::size_t kStdStringInlineCapacity = 0x0F;
constexpr std::size_t kLoggedPathBytes = 260;
constexpr std::size_t kLoggedStdStringBytes = 160;
constexpr std::size_t kRoadMapLinkOffset = 0x388;
constexpr std::size_t kRoadMapFromLinkOffset = 0x638;
constexpr std::size_t kRoadMapStartTableOffset = 0x858;
constexpr std::size_t kRoadMapStartTableVariant1Offset = 0xAE8;
constexpr std::size_t kRoadMapStartTableVariant2Offset = 0xB00;
constexpr std::size_t kRoadMapStartTransformStride = 0x34;
constexpr std::size_t kGlobalRaceSceneRva = 0x2827EE0;
constexpr std::size_t kEnterPhySceneSetTransformVtableOffset = 0x170;
constexpr std::size_t kPlayerRaceIdOffset = 0x8210;
constexpr std::size_t kPlayerSeatFieldOffset = 0x93CE;
constexpr std::size_t kProtectedLoadScMapKeyRva = 0x2A9D240;
constexpr std::size_t kProtectedLoadScMapValuePtrRva = 0x2A9D248;
constexpr std::size_t kProtectedLoadMapResMapKeyRva = 0x2A9D250;
constexpr std::size_t kProtectedLoadMapResMapValuePtrRva = 0x2A9D258;

using PrepareGameBeginFn = int(__fastcall*)(void* self, void* notify);
using LevelManagerEnterFn = __int64(__fastcall*)(void* levelMgr, void* notify, int a3, unsigned char a4, int a5);
using AuroraUgcCheckFn = bool(__fastcall*)();
using CurrentMapInfoFn = std::uintptr_t(__fastcall*)();
using RoadMapProcessorResetFn = void(__fastcall*)(void* processor);
using LoadMapResFn = unsigned char(__fastcall*)(void* processor, void* mapInfo, unsigned int a3, unsigned char a4, unsigned char a5);
using LoadMapDescFn = unsigned char(__fastcall*)(void* processor, unsigned int mapId, void* outInfo, void* mapInfo);
using LoadScFn = void(__fastcall*)(void* processor, void* mapInfo, unsigned char a3, unsigned char a4);
using ApplySceneObjectsFn = void(__fastcall*)(void* processor, void* mapInfo);
using LoadScCacheFetchFn = void*(__fastcall*)(void* cacheMgr, void* outValue, void* host, void* request);
using LoadScCacheExtractFn = void*(__fastcall*)(void* outSmartPtr, void* cacheResult);
using NifLoadFn = __int64(__fastcall*)(void* manager, void* outSmartPtr, const char* path, unsigned char a4, unsigned char a5);
using NifNormalizePathFn = void(__fastcall*)(void* manager, const char* path, void* outStdString);
using NifFetchResourceFn = void*(__fastcall*)(void* manager, void* outResourceSmartPtr, const char* normalizedPath, void* a4, unsigned char a5);
using NifExtractRootFn = void*(__fastcall*)(void* manager, void* outRootSmartPtr, void* resource, unsigned char flags);
using LuaFieldLookupFn = void*(__fastcall*)(void* table, void* key);
using RaceEnterPhySceneKartPhysFn = __int64(__fastcall*)();
using RoadMapStartTransformFn = void*(__fastcall*)(void* roadMap, void* outTransform, unsigned int index, int direction, float distance);
using RoadMapStartTransformVariantFn = void*(__fastcall*)(void* roadMap, void* outTransform, unsigned int index, __int64 a4, int a5, int variant);
using EnterPhySceneSetTransformFn = __int64(__fastcall*)(void* player, void* transform);
using ProtectedIntEncodeFn = __int64(__fastcall*)(void* key, void* valuePtr);

struct ProbeThreadContext
{
	qqspeed::config::HookConfig config = {};
};

struct MapIdSnapshot
{
	bool present = false;
	unsigned int topMap = 0;
	unsigned int gameId = 0;
	unsigned int seed = 0;
	unsigned char playerCount = 0;
	unsigned int raceTrackOrders[kRaceTrackOrderCount] = {};
	unsigned short outerBase = 0;
	unsigned short outerSub = 0;
	unsigned char shuttleMapNum = 0;
	unsigned int shuttleMaps[kLoggedShuttleMapCount] = {};
	unsigned char timeShuttleGen = 0;
	unsigned char hasCustomScheme = 0;
	unsigned char customMapNum = 0;
	unsigned int customMaps[kLoggedCustomMapCount] = {};
};

struct SceneModeSnapshot
{
	unsigned char ugcByte = 0;
	void* gameMode = nullptr;
	unsigned short mode = 0;
	unsigned short subMode = 0;
	void* currentInfo = nullptr;
	void* mapConfig = nullptr;
	unsigned int configMapId = 0;
	std::uintptr_t ugcWork = 0;
};

std::uintptr_t g_topKartBase = 0;
volatile LONG g_installState = 0;
std::size_t g_patchedCalls = 0;
bool g_verboseProbeLogs = false;

PrepareGameBeginFn g_originalPrepareGameBegin = nullptr;
LevelManagerEnterFn g_originalLevelManagerEnter = nullptr;
AuroraUgcCheckFn g_originalAuroraUgcCheck = nullptr;
RoadMapProcessorResetFn g_originalRoadMapProcessorReset = nullptr;
LoadMapResFn g_originalLoadMapRes = nullptr;
LoadMapDescFn g_originalLoadMapDesc = nullptr;
LoadScFn g_originalLoadSc = nullptr;
ApplySceneObjectsFn g_originalApplySceneObjects = nullptr;
LoadScCacheFetchFn g_originalLoadScCacheFetch = nullptr;
LoadScCacheExtractFn g_originalLoadScCacheExtract = nullptr;
NifLoadFn g_originalNifLoad = nullptr;
NifNormalizePathFn g_originalNifNormalizePath = nullptr;
NifFetchResourceFn g_originalNifFetchResource = nullptr;
NifExtractRootFn g_originalNifExtractRoot = nullptr;
LuaFieldLookupFn g_originalLuaFieldLookup = nullptr;
RaceEnterPhySceneKartPhysFn g_originalRaceEnterPhySceneKartPhys = nullptr;
RoadMapStartTransformFn g_originalRoadMapStartTransform = nullptr;
RoadMapStartTransformVariantFn g_originalRoadMapStartTransformVariant = nullptr;

struct MapIdThreadState
{
	bool traceMapNifLoader = false;
	void* traceMapNifResource = nullptr;
	unsigned int activeLoadScMapId = 0;
	unsigned int activeLoadScDepth = 0;
	unsigned int activeLoadMapResMapId = 0;
	unsigned int activeLoadMapResDepth = 0;
	void* activeLoadMapResMapInfo = nullptr;
};

qqspeed::runtime::ManualFlsSlot<MapIdThreadState> g_mapIdThreadState;
std::atomic<unsigned int> g_activeLoadScMapId{ 0 };
std::atomic<unsigned int> g_activeLoadScDepth{ 0 };
std::atomic<unsigned int> g_activeLoadMapResMapId{ 0 };
std::atomic<unsigned int> g_activeLoadMapResDepth{ 0 };
std::atomic<std::uintptr_t> g_activeLoadMapResMapInfo{ 0 };

template <typename T>
T ReadSafe(const void* ptr)
{
	T value = {};
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

struct ProtectedMapPairSnapshot
{
	void* keyAddress = nullptr;
	void* valuePointerAddress = nullptr;
	void* valuePointer = nullptr;
	unsigned int key = 0;
	unsigned int encoded = 0;
	unsigned int decoded = 0;
	bool hasValuePointer = false;
};

unsigned int DecodeProtectedInt(unsigned int key, unsigned int encoded)
{
	unsigned char keyBytes[4] = {};
	unsigned char encodedBytes[4] = {};
	unsigned char decodedBytes[4] = {};
	memcpy(keyBytes, &key, sizeof(keyBytes));
	memcpy(encodedBytes, &encoded, sizeof(encodedBytes));

	decodedBytes[3] = static_cast<unsigned char>(encodedBytes[0] ^ keyBytes[3]);
	decodedBytes[2] = static_cast<unsigned char>(encodedBytes[3] ^ keyBytes[2]);
	decodedBytes[1] = static_cast<unsigned char>(encodedBytes[2] ^ keyBytes[1]);
	decodedBytes[0] = static_cast<unsigned char>(encodedBytes[1] ^ keyBytes[0]);

	unsigned int decoded = 0;
	memcpy(&decoded, decodedBytes, sizeof(decoded));
	return decoded;
}

ProtectedMapPairSnapshot ReadProtectedMapPair(std::size_t keyRva, std::size_t valuePtrRva)
{
	ProtectedMapPairSnapshot snapshot = {};
	if (!g_topKartBase) {
		return snapshot;
	}

	snapshot.keyAddress = reinterpret_cast<void*>(g_topKartBase + keyRva);
	snapshot.valuePointerAddress = reinterpret_cast<void*>(g_topKartBase + valuePtrRva);
	snapshot.key = ReadSafe<unsigned int>(snapshot.keyAddress);
	snapshot.valuePointer = ReadSafe<void*>(snapshot.valuePointerAddress);
	snapshot.hasValuePointer = snapshot.valuePointer != nullptr;
	if (snapshot.valuePointer) {
		snapshot.encoded = ReadSafe<unsigned int>(snapshot.valuePointer);
		snapshot.decoded = DecodeProtectedInt(snapshot.key, snapshot.encoded);
	}
	return snapshot;
}

bool ResetProtectedMapPairToZero(std::size_t keyRva, std::size_t valuePtrRva)
{
	if (!g_topKartBase) {
		return false;
	}

	void* keyAddress = reinterpret_cast<void*>(g_topKartBase + keyRva);
	void* valuePointer = ReadSafe<void*>(reinterpret_cast<const void*>(g_topKartBase + valuePtrRva));
	if (!keyAddress || !valuePointer) {
		return false;
	}

	unsigned int zero = 0;
	if (!qqspeed::network::patch::SafeCopyRaw(valuePointer, &zero, sizeof(zero))) {
		return false;
	}

	const ProtectedIntEncodeFn encode =
		reinterpret_cast<ProtectedIntEncodeFn>(g_topKartBase + kProtectedIntEncodeRva);
	if (!encode) {
		return false;
	}

	__try
	{
		encode(keyAddress, valuePointer);
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

std::uintptr_t CurrentReturnRva()
{
	const std::uintptr_t ret = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
	if (!g_topKartBase || ret < g_topKartBase) {
		return 0;
	}
	return ret - g_topKartBase;
}

std::uintptr_t CurrentCallRva()
{
	const std::uintptr_t retRva = CurrentReturnRva();
	return retRva >= 5 ? retRva - 5 : 0;
}

const char* CallSiteName(std::uintptr_t callRva)
{
	switch (callRva) {
	case 0x717B56:
		return "level_enter_call_717B56";
	case 0x8479E7:
		return "level_enter_call_8479E7";
	case 0x8FF827:
		return "level_enter_call_8FF827";
	case 0x10619AB:
		return "level_enter_call_10619AB";
	case 0x120778C:
		return "level_enter_prepare_flow_1";
	case 0x120815B:
		return "level_enter_prepare_flow_2";
	case 0x1391422:
		return "level_enter_call_1391422";
	case 0x12074C3:
		return "prepare_game_begin_flow_1";
	case 0x1207E87:
		return "prepare_game_begin_flow_2";
	case 0xE2CFA9:
		return "scene_ugc_check_load_sc";
	case 0xE2DF73:
		return "scene_ugc_check_load_map_res";
	case 0x1198085:
		return "scene_ugc_check_init_level";
	case 0xE27855:
		return "scene_roadmap_reset_from_load_maps";
	case 0xE27413:
		return "scene_load_map_res_from_init_level";
	case 0xE2DED9:
		return "scene_load_map_desc_from_load_map_res";
	case 0xE2DF21:
		return "scene_load_sc_from_load_map_res";
	case 0xE2D7A0:
		return "scene_load_sc_cache_fetch";
	case 0xE2D7C4:
		return "scene_load_sc_cache_extract";
	case 0xE2DB27:
		return "scene_apply_objects_from_load_map_res";
	case 0xE2D845:
		return "scene_nif_load_sc";
	case kNifLoadPickCallRva:
		return "scene_nif_load_pick_from_load_map_res";
	case kNifLoadPickShadowCallRva:
		return "scene_nif_load_pick_shadow_from_load_map_res";
	case 0xE2F9BB:
		return "scene_nif_load_attached_object";
	case kNifLoadScCoroutineCallRva:
		return "scene_nif_load_sc_coroutine";
	case 0x8F2E40:
		return "nif_normalize_path_from_loader";
	case 0x8F2E6A:
		return "nif_fetch_resource_from_loader";
	case 0x8F2E7F:
		return "nif_extract_root_from_loader";
	case 0xE31E34:
		return "scene_map_desc_lookup_motion_object_1";
	case 0xE31E67:
		return "scene_map_desc_lookup_motion_object_2";
	case 0x1201E5F:
		return "race_enter_physcene_kart_phys";
	case 0x1208F68:
		return "race_start_transform_on_notify_race_begin_1";
	case 0x12092DF:
		return "race_start_transform_variant_on_notify_race_begin";
	case 0x1209399:
		return "race_start_transform_on_notify_race_begin_2";
	case 0x1209610:
		return "race_start_transform_on_notify_race_begin_3";
	case 0x120A66F:
		return "race_enter_physcene_final_set_transform";
	default:
		return "unknown";
	}
}

const unsigned char* AddOffset(const void* base, std::size_t offset)
{
	return base ? static_cast<const unsigned char*>(base) + offset : nullptr;
}

void CopyCStringSafe(const char* src, char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}

	out[0] = 0;
	if (!src) {
		std::snprintf(out, outSize, "<null>");
		return;
	}

	const std::size_t copySize =
		(outSize - 1) < kLoggedPathBytes ? (outSize - 1) : kLoggedPathBytes;
	if (!qqspeed::network::patch::SafeCopyRaw(out, src, copySize)) {
		std::snprintf(out, outSize, "<unreadable:%p>", src);
		return;
	}
	out[copySize] = 0;
}

void CopyStdStringSafe(const void* stringObject, char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}
	out[0] = 0;
	if (!stringObject) {
		std::snprintf(out, outSize, "<null>");
		return;
	}

	const std::size_t length = ReadSafe<std::size_t>(AddOffset(stringObject, kStdStringLengthOffset));
	if (length == 0) {
		return;
	}
	if (length > 0x10000) {
		std::snprintf(out, outSize, "<bad-len:%llu>", static_cast<unsigned long long>(length));
		return;
	}

	const char* text = length > kStdStringInlineCapacity ?
		ReadSafe<const char*>(stringObject) :
		reinterpret_cast<const char*>(stringObject);
	if (!text) {
		std::snprintf(out, outSize, "<null-text>");
		return;
	}

	const std::size_t copySize =
		length < (outSize - 1) ? (length < kLoggedStdStringBytes ? length : kLoggedStdStringBytes) : (outSize - 1);
	if (!qqspeed::network::patch::SafeCopyRaw(out, text, copySize)) {
		std::snprintf(out, outSize, "<unreadable:%p>", text);
		return;
	}
	out[copySize] = 0;
}

char LowerAscii(char ch)
{
	return (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch - 'A' + 'a') : ch;
}

bool ContainsAsciiNoCase(const char* text, const char* needle)
{
	if (!text || !needle || !*needle) {
		return false;
	}

	for (const char* p = text; *p; ++p) {
		const char* h = p;
		const char* n = needle;
		while (*h && *n && LowerAscii(*h) == LowerAscii(*n)) {
			++h;
			++n;
		}
		if (!*n) {
			return true;
		}
	}
	return false;
}

bool IsMapResourcePath(const char* path)
{
	return ContainsAsciiNoCase(path, "map/common map/") ||
		ContainsAsciiNoCase(path, "map\\common map\\");
}

char NormalizePathChar(char ch)
{
	if (ch == '\\') {
		return '/';
	}
	return LowerAscii(ch);
}

bool PathEqualsAsciiNoCase(const char* lhs, const char* rhs)
{
	if (!lhs || !rhs) {
		return lhs == rhs;
	}

	while (*lhs && *rhs) {
		if (NormalizePathChar(*lhs) != NormalizePathChar(*rhs)) {
			return false;
		}
		++lhs;
		++rhs;
	}
	return *lhs == 0 && *rhs == 0;
}

void FormatExpectedScPath(unsigned int mapId, char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}
	out[0] = 0;
	if (mapId == 0) {
		return;
	}
	std::snprintf(out, outSize, "Map/Common Map/Map%02u/sc.nif", mapId);
}

void FormatExpectedPickPath(unsigned int mapId, bool shadow, char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}
	out[0] = 0;
	if (mapId == 0) {
		return;
	}
	std::snprintf(
		out,
		outSize,
		"Map/Common Map/Map%02u/%s",
		mapId,
		shadow ? "pick_shadow.nif" : "pick.nif");
}

unsigned int ExtractMapIdFromPathText(const char* text)
{
	if (!text) {
		return 0;
	}

	unsigned int lastMapId = 0;
	for (const char* p = text; p[0] && p[1] && p[2]; ++p) {
		if (LowerAscii(p[0]) != 'm' || LowerAscii(p[1]) != 'a' || LowerAscii(p[2]) != 'p') {
			continue;
		}

		const char* digits = p + 3;
		if (*digits < '0' || *digits > '9') {
			continue;
		}

		unsigned int value = 0;
		while (*digits >= '0' && *digits <= '9') {
			value = (value * 10) + static_cast<unsigned int>(*digits - '0');
			++digits;
		}
		lastMapId = value;
	}

	return lastMapId;
}

bool IsLoadMapResPickCallsite(std::uintptr_t callRva)
{
	return callRva == kNifLoadPickCallRva || callRva == kNifLoadPickShadowCallRva;
}

std::size_t CountContiguousEntries(std::uintptr_t begin, std::uintptr_t end, std::size_t stride)
{
	if (!begin || end < begin || stride == 0) {
		return 0;
	}
	return static_cast<std::size_t>((end - begin) / stride);
}

void* ReadPointerField(const void* base, std::size_t offset)
{
	return ReadSafe<void*>(AddOffset(base, offset));
}

struct StartTransformSnapshot
{
	bool valid = false;
	float values[13] = {};
};

StartTransformSnapshot ReadStartTransform(const void* transform)
{
	StartTransformSnapshot snapshot = {};
	if (transform && qqspeed::network::patch::SafeCopyRaw(snapshot.values, transform, sizeof(snapshot.values))) {
		snapshot.valid = true;
	}
	return snapshot;
}

float StartTransformValue(const StartTransformSnapshot& transform, std::size_t index)
{
	return transform.valid && index < _countof(transform.values) ? transform.values[index] : 0.0f;
}

void* CurrentRaceScene()
{
	return g_topKartBase ?
		ReadSafe<void*>(reinterpret_cast<const void*>(g_topKartBase + kGlobalRaceSceneRva)) :
		nullptr;
}

void* CurrentRaceRoadMap()
{
	void* scene = CurrentRaceScene();
	void* link = ReadPointerField(scene, kRoadMapLinkOffset);
	return ReadPointerField(link, kRoadMapFromLinkOffset);
}

unsigned int CurrentConfigMapId()
{
	if (!g_topKartBase) {
		return 0;
	}
	const CurrentMapInfoFn currentMapInfo =
		reinterpret_cast<CurrentMapInfoFn>(g_topKartBase + kCurrentMapInfoRva);
	const std::uintptr_t currentInfo = currentMapInfo ? currentMapInfo() : 0;
	void* mapConfig = ReadPointerField(reinterpret_cast<const void*>(currentInfo), kCurrentMapConfigOffset);
	return ReadSafe<unsigned int>(AddOffset(mapConfig, kMapConfigMapIdOffset));
}

struct LoadScContextScope
{
	MapIdThreadState* state = nullptr;
	unsigned int oldMapId = 0;
	unsigned int oldDepth = 0;
	unsigned int oldGlobalMapId = 0;

	explicit LoadScContextScope(unsigned int mapId)
		: state(g_mapIdThreadState.GetOrCreate()),
		  oldGlobalMapId(g_activeLoadScMapId.load(std::memory_order_acquire))
	{
		if (state) {
			oldMapId = state->activeLoadScMapId;
			oldDepth = state->activeLoadScDepth;
			state->activeLoadScDepth = oldDepth + 1;
		}
		if (mapId != 0) {
			if (state) {
				state->activeLoadScMapId = mapId;
			}
			g_activeLoadScMapId.store(mapId, std::memory_order_release);
		}
		g_activeLoadScDepth.fetch_add(1, std::memory_order_acq_rel);
	}

	~LoadScContextScope()
	{
		if (state) {
			state->activeLoadScMapId = oldMapId;
			state->activeLoadScDepth = oldDepth;
		}
		g_activeLoadScMapId.store(oldGlobalMapId, std::memory_order_release);
		g_activeLoadScDepth.fetch_sub(1, std::memory_order_acq_rel);
	}
};

struct LoadMapResContextScope
{
	MapIdThreadState* state = nullptr;
	unsigned int oldMapId = 0;
	unsigned int oldDepth = 0;
	void* oldMapInfo = nullptr;
	unsigned int oldGlobalMapId = 0;
	std::uintptr_t oldGlobalMapInfo = 0;

	LoadMapResContextScope(unsigned int mapId, void* mapInfo)
		: state(g_mapIdThreadState.GetOrCreate()),
		  oldGlobalMapId(g_activeLoadMapResMapId.load(std::memory_order_acquire)),
		  oldGlobalMapInfo(g_activeLoadMapResMapInfo.load(std::memory_order_acquire))
	{
		if (state) {
			oldMapId = state->activeLoadMapResMapId;
			oldDepth = state->activeLoadMapResDepth;
			oldMapInfo = state->activeLoadMapResMapInfo;
			state->activeLoadMapResDepth = oldDepth + 1;
		}
		if (mapId != 0) {
			if (state) {
				state->activeLoadMapResMapId = mapId;
				state->activeLoadMapResMapInfo = mapInfo;
			}
			g_activeLoadMapResMapId.store(mapId, std::memory_order_release);
			g_activeLoadMapResMapInfo.store(reinterpret_cast<std::uintptr_t>(mapInfo), std::memory_order_release);
		}
		g_activeLoadMapResDepth.fetch_add(1, std::memory_order_acq_rel);
	}

	~LoadMapResContextScope()
	{
		if (state) {
			state->activeLoadMapResMapId = oldMapId;
			state->activeLoadMapResDepth = oldDepth;
			state->activeLoadMapResMapInfo = oldMapInfo;
		}
		g_activeLoadMapResMapId.store(oldGlobalMapId, std::memory_order_release);
		g_activeLoadMapResMapInfo.store(oldGlobalMapInfo, std::memory_order_release);
		g_activeLoadMapResDepth.fetch_sub(1, std::memory_order_acq_rel);
	}
};

unsigned int ActiveLoadScMapId()
{
	MapIdThreadState* state = g_mapIdThreadState.Get();
	if (state && state->activeLoadScDepth > 0 && state->activeLoadScMapId != 0) {
		return state->activeLoadScMapId;
	}
	if (g_activeLoadScDepth.load(std::memory_order_acquire) > 0) {
		return g_activeLoadScMapId.load(std::memory_order_acquire);
	}
	return 0;
}

unsigned int ActiveLoadMapResMapId()
{
	MapIdThreadState* state = g_mapIdThreadState.Get();
	if (state && state->activeLoadMapResDepth > 0 && state->activeLoadMapResMapId != 0) {
		return state->activeLoadMapResMapId;
	}
	if (g_activeLoadMapResDepth.load(std::memory_order_acquire) > 0) {
		return g_activeLoadMapResMapId.load(std::memory_order_acquire);
	}
	return 0;
}

void* ActiveLoadMapResMapInfo()
{
	MapIdThreadState* state = g_mapIdThreadState.Get();
	if (state && state->activeLoadMapResDepth > 0 && state->activeLoadMapResMapInfo) {
		return state->activeLoadMapResMapInfo;
	}
	if (g_activeLoadMapResDepth.load(std::memory_order_acquire) > 0) {
		return reinterpret_cast<void*>(g_activeLoadMapResMapInfo.load(std::memory_order_acquire));
	}
	return nullptr;
}

MapIdSnapshot TakeSnapshot(const void* notify)
{
	MapIdSnapshot snapshot = {};
	if (!notify) {
		return snapshot;
	}

	snapshot.present = true;
	snapshot.topMap = ReadSafe<unsigned int>(AddOffset(notify, kNotifyTopMapOffset));
	snapshot.gameId = ReadSafe<unsigned int>(AddOffset(notify, kNotifyGameIdOffset));
	snapshot.seed = ReadSafe<unsigned int>(AddOffset(notify, kNotifySeedOffset));
	snapshot.playerCount = ReadSafe<unsigned char>(AddOffset(notify, kNotifyPlayerCountOffset));
	snapshot.outerBase = ReadSafe<unsigned short>(AddOffset(notify, kNotifyOuterBaseOffset));
	snapshot.outerSub = ReadSafe<unsigned short>(AddOffset(notify, kNotifyOuterSubOffset));
	snapshot.shuttleMapNum = ReadSafe<unsigned char>(AddOffset(notify, kNotifyShuttleMapNumOffset));
	snapshot.timeShuttleGen = ReadSafe<unsigned char>(AddOffset(notify, kNotifyTimeShuttleGenOffset));
	snapshot.hasCustomScheme = ReadSafe<unsigned char>(AddOffset(notify, kNotifyHasCustomSchemeOffset));
	snapshot.customMapNum = ReadSafe<unsigned char>(AddOffset(notify, kNotifyCustomSchemeMapNumOffset));

	for (std::size_t i = 0; i < kRaceTrackOrderCount; ++i) {
		snapshot.raceTrackOrders[i] = ReadSafe<unsigned int>(
			AddOffset(notify, kNotifyRaceTrackOrdersOffset + i * sizeof(unsigned int)));
	}
	for (std::size_t i = 0; i < kLoggedShuttleMapCount; ++i) {
		snapshot.shuttleMaps[i] = ReadSafe<unsigned int>(
			AddOffset(notify, kNotifyShuttleMapListOffset + i * kNotifyShuttleMapEntrySize));
	}
	for (std::size_t i = 0; i < kLoggedCustomMapCount; ++i) {
		snapshot.customMaps[i] = ReadSafe<unsigned int>(
			AddOffset(notify, kNotifyCustomSchemeMapListOffset + i * sizeof(unsigned int)));
	}

	return snapshot;
}

SceneModeSnapshot TakeSceneModeSnapshot()
{
	SceneModeSnapshot snapshot = {};
	if (!g_topKartBase) {
		return snapshot;
	}

	snapshot.ugcByte = ReadSafe<unsigned char>(reinterpret_cast<const void*>(g_topKartBase + kGlobalUgcByteRva));
	snapshot.gameMode = ReadSafe<void*>(reinterpret_cast<const void*>(g_topKartBase + kGlobalGameModeRva));
	snapshot.mode = ReadSafe<unsigned short>(AddOffset(snapshot.gameMode, kGameModeModeOffset));
	snapshot.subMode = ReadSafe<unsigned short>(AddOffset(snapshot.gameMode, kGameModeSubModeOffset));

	// The game only consults the current map config for this branch when the global UGC byte is set.
	if (snapshot.ugcByte != 0) {
		const CurrentMapInfoFn currentMapInfo =
			reinterpret_cast<CurrentMapInfoFn>(g_topKartBase + kCurrentMapInfoRva);
		const std::uintptr_t currentInfo = currentMapInfo ? currentMapInfo() : 0;
		snapshot.currentInfo = reinterpret_cast<void*>(currentInfo);
		snapshot.mapConfig = ReadPointerField(snapshot.currentInfo, kCurrentMapConfigOffset);
		snapshot.configMapId = ReadSafe<unsigned int>(AddOffset(snapshot.mapConfig, kMapConfigMapIdOffset));
		snapshot.ugcWork = ReadSafe<std::uintptr_t>(AddOffset(snapshot.mapConfig, kMapConfigUgcWorkOffset));
	}

	return snapshot;
}

void FormatU32List(const unsigned int* values, std::size_t count, char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}

	std::size_t pos = 0;
	int written = std::snprintf(out + pos, outSize - pos, "[");
	if (written < 0) {
		out[0] = 0;
		return;
	}
	if (static_cast<std::size_t>(written) >= outSize - pos) {
		out[outSize - 1] = 0;
		return;
	}
	pos += static_cast<std::size_t>(written);

	for (std::size_t i = 0; i < count && pos < outSize; ++i) {
		written = std::snprintf(out + pos, outSize - pos, "%s%u", i == 0 ? "" : ",", values[i]);
		if (written < 0) {
			break;
		}
		if (static_cast<std::size_t>(written) >= outSize - pos) {
			pos = outSize - 1;
			break;
		}
		pos += static_cast<std::size_t>(written);
	}

	if (pos + 1 < outSize) {
		out[pos++] = ']';
		out[pos] = 0;
	}
	else {
		out[outSize - 1] = 0;
	}
}

std::size_t ClampCount(std::size_t value, std::size_t maxValue)
{
	return value < maxValue ? value : maxValue;
}

void LogSnapshot(
	const char* stage,
	std::uintptr_t callRva,
	void* self,
	void* notify,
	const MapIdSnapshot& snapshot)
{
	if (!snapshot.present) {
		qqspeed::logging::Logf(
			"topkart_mapid stage=%s callRva=0x%llX site=%s self=%p notify=%p missing_notify=1",
			stage ? stage : "unknown",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			self,
			notify);
		return;
	}

	char raceOrders[384] = {};
	char shuttleMaps[160] = {};
	char customMaps[96] = {};
	FormatU32List(snapshot.raceTrackOrders, kRaceTrackOrderCount, raceOrders, sizeof(raceOrders));
	FormatU32List(
		snapshot.shuttleMaps,
		ClampCount(snapshot.shuttleMapNum, kLoggedShuttleMapCount),
		shuttleMaps,
		sizeof(shuttleMaps));
	FormatU32List(
		snapshot.customMaps,
		ClampCount(snapshot.customMapNum, kLoggedCustomMapCount),
		customMaps,
		sizeof(customMaps));

	const bool customRoomPath =
		snapshot.outerBase == 42 && snapshot.outerSub == 27 && snapshot.hasCustomScheme != 0;
	const unsigned int raceFirst = snapshot.raceTrackOrders[0];
	const unsigned int customFirst = customRoomPath && snapshot.customMapNum > 0 ? snapshot.customMaps[0] : 0;

	qqspeed::logging::Logf(
		"topkart_mapid stage=%s callRva=0x%llX site=%s self=%p notify=%p gameId=%u seed=%u playerCount=%u "
		"expectedTopMap=%u expectedRaceFirst=%u expectedCustomFirst=%u customRoomPath=%u "
		"actualMap=%u actualBase=%u actualSub=%u actualResource=Map%u_%u_%u "
		"outerBase=%u outerSub=%u hasCustom=%u customMapNum=%u customMaps=%s "
		"shuttleMapNum=%u timeShuttleGen=%u shuttleMaps=%s raceTrackOrders=%s",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		self,
		notify,
		snapshot.gameId,
		snapshot.seed,
		static_cast<unsigned int>(snapshot.playerCount),
		snapshot.topMap,
		raceFirst,
		customFirst,
		customRoomPath ? 1 : 0,
		snapshot.topMap,
		static_cast<unsigned int>(snapshot.outerBase),
		static_cast<unsigned int>(snapshot.outerSub),
		snapshot.topMap,
		static_cast<unsigned int>(snapshot.outerBase),
		static_cast<unsigned int>(snapshot.outerSub),
		static_cast<unsigned int>(snapshot.outerBase),
		static_cast<unsigned int>(snapshot.outerSub),
		static_cast<unsigned int>(snapshot.hasCustomScheme),
		static_cast<unsigned int>(snapshot.customMapNum),
		customMaps,
		static_cast<unsigned int>(snapshot.shuttleMapNum),
		static_cast<unsigned int>(snapshot.timeShuttleGen),
		shuttleMaps,
		raceOrders);
}

void LogSceneMode(
	const char* stage,
	std::uintptr_t callRva,
	bool result,
	const SceneModeSnapshot& snapshot)
{
	const bool modeMatch = snapshot.gameMode && snapshot.mode == 19 && snapshot.subMode == 5;
	const bool configMatch = snapshot.ugcByte != 0 && snapshot.mapConfig && snapshot.ugcWork != 0;
	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s result=%u modeMatch=%u configMatch=%u "
		"ugcByte=%u gameMode=%p mode=%u subMode=%u currentInfo=%p mapConfig=%p configMapId=%u ugcWork=0x%llX",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		result ? 1 : 0,
		modeMatch ? 1 : 0,
		configMatch ? 1 : 0,
		static_cast<unsigned int>(snapshot.ugcByte),
		snapshot.gameMode,
		static_cast<unsigned int>(snapshot.mode),
		static_cast<unsigned int>(snapshot.subMode),
		snapshot.currentInfo,
		snapshot.mapConfig,
		snapshot.configMapId,
		static_cast<unsigned long long>(snapshot.ugcWork));
}

void LogRoadMapResetProtectedMaps(
	std::uintptr_t callRva,
	void* processor,
	const ProtectedMapPairSnapshot& beforeLoadSc,
	const ProtectedMapPairSnapshot& beforeLoadMapRes,
	const ProtectedMapPairSnapshot& afterLoadSc,
	const ProtectedMapPairSnapshot& afterLoadMapRes,
	bool originalCalled,
	bool clearLoadSc,
	bool clearLoadMapRes)
{
	qqspeed::logging::Logf(
		"topkart_scene stage=roadmap_reset_map_override callRva=0x%llX site=%s processor=%p "
		"originalCalled=%u clearLoadSc=%u clearLoadMapRes=%u "
		"loadScKeyAddr=%p loadScValuePtrAddr=%p loadScValuePtr=%p loadScBeforeKey=0x%08X "
		"loadScBeforeEncoded=0x%08X loadScBeforeDecoded=%u loadScAfterKey=0x%08X "
		"loadScAfterEncoded=0x%08X loadScAfterDecoded=%u "
		"loadMapResKeyAddr=%p loadMapResValuePtrAddr=%p loadMapResValuePtr=%p loadMapResBeforeKey=0x%08X "
		"loadMapResBeforeEncoded=0x%08X loadMapResBeforeDecoded=%u loadMapResAfterKey=0x%08X "
		"loadMapResAfterEncoded=0x%08X loadMapResAfterDecoded=%u",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		processor,
		originalCalled ? 1 : 0,
		clearLoadSc ? 1 : 0,
		clearLoadMapRes ? 1 : 0,
		beforeLoadSc.keyAddress,
		beforeLoadSc.valuePointerAddress,
		beforeLoadSc.valuePointer,
		beforeLoadSc.key,
		beforeLoadSc.encoded,
		beforeLoadSc.decoded,
		afterLoadSc.key,
		afterLoadSc.encoded,
		afterLoadSc.decoded,
		beforeLoadMapRes.keyAddress,
		beforeLoadMapRes.valuePointerAddress,
		beforeLoadMapRes.valuePointer,
		beforeLoadMapRes.key,
		beforeLoadMapRes.encoded,
		beforeLoadMapRes.decoded,
		afterLoadMapRes.key,
		afterLoadMapRes.encoded,
		afterLoadMapRes.decoded);
}

void LogLoadSc(
	const char* stage,
	std::uintptr_t callRva,
	void* processor,
	void* mapInfo,
	unsigned char a3,
	unsigned char a4)
{
	const unsigned int infoMap = ReadSafe<unsigned int>(mapInfo);
	const unsigned char loadFlag = ReadSafe<unsigned char>(AddOffset(mapInfo, kMapPreLoadFlagOffset));
	void* root = ReadPointerField(mapInfo, kMapPreLoadRootOffset);
	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s processor=%p mapInfo=%p infoMap=%u "
		"loadFlag38=%u scRoot=%p a3=%u a4=%u expectedSc=Map/Common Map/Map%02u/sc.nif",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		processor,
		mapInfo,
		infoMap,
		static_cast<unsigned int>(loadFlag),
		root,
		static_cast<unsigned int>(a3),
		static_cast<unsigned int>(a4),
		infoMap);
}

void LogLoadMapRes(
	const char* stage,
	std::uintptr_t callRva,
	void* processor,
	void* mapInfo,
	unsigned int a3,
	unsigned char a4,
	unsigned char a5,
	int result)
{
	const unsigned int infoMap = ReadSafe<unsigned int>(mapInfo);
	const unsigned char loadFlag = ReadSafe<unsigned char>(AddOffset(mapInfo, kMapPreLoadFlagOffset));
	void* root = ReadPointerField(mapInfo, kMapPreLoadRootOffset);
	void* processorRuntime = ReadPointerField(processor, 0xF0);
	void* processorScene = ReadPointerField(processor, 0xF8);
	const std::uintptr_t objectBegin = ReadSafe<std::uintptr_t>(AddOffset(mapInfo, kMapObjectListBeginOffset));
	const std::uintptr_t objectEnd = ReadSafe<std::uintptr_t>(AddOffset(mapInfo, kMapObjectListEndOffset));
	const std::size_t objectCount = CountContiguousEntries(objectBegin, objectEnd, kMapObjectRecordSize);
	const std::uintptr_t attachBegin = ReadSafe<std::uintptr_t>(AddOffset(processor, kSceneAttachListBeginOffset));
	const std::uintptr_t attachEnd = ReadSafe<std::uintptr_t>(AddOffset(processor, kSceneAttachListEndOffset));
	const std::size_t attachCount = CountContiguousEntries(attachBegin, attachEnd, sizeof(void*));

	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s processor=%p mapInfo=%p infoMap=%u "
		"a3=%u a4=%u a5=%u result=%d loadFlag38=%u scRoot=%p objectCount=%llu "
		"attachCount=%llu processorRuntime=%p processorScene=%p expectedSc=Map/Common Map/Map%02u/sc.nif",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		processor,
		mapInfo,
		infoMap,
		a3,
		static_cast<unsigned int>(a4),
		static_cast<unsigned int>(a5),
		result,
		static_cast<unsigned int>(loadFlag),
		root,
		static_cast<unsigned long long>(objectCount),
		static_cast<unsigned long long>(attachCount),
		processorRuntime,
		processorScene,
		infoMap);
}

void LogLoadMapDesc(
	const char* stage,
	std::uintptr_t callRva,
	void* processor,
	unsigned int mapId,
	void* outInfo,
	void* mapInfo,
	int result)
{
	const unsigned int infoMap = ReadSafe<unsigned int>(mapInfo);
	const std::uintptr_t objectBegin = ReadSafe<std::uintptr_t>(AddOffset(mapInfo, kMapObjectListBeginOffset));
	const std::uintptr_t objectEnd = ReadSafe<std::uintptr_t>(AddOffset(mapInfo, kMapObjectListEndOffset));
	const std::size_t objectCount = CountContiguousEntries(objectBegin, objectEnd, kMapObjectRecordSize);

	char descName[192] = {};
	char descScPath[192] = {};
	char descField40[192] = {};
	char descField60[192] = {};
	CopyStdStringSafe(outInfo, descName, sizeof(descName));
	CopyStdStringSafe(AddOffset(outInfo, 0x20), descScPath, sizeof(descScPath));
	CopyStdStringSafe(AddOffset(outInfo, 0x40), descField40, sizeof(descField40));
	CopyStdStringSafe(AddOffset(outInfo, 0x60), descField60, sizeof(descField60));

	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s processor=%p mapInfo=%p outInfo=%p "
		"mapId=%u infoMap=%u result=%d objectBegin=%p objectEnd=%p objectCount=%llu "
		"desc0=%s desc20=%s desc40=%s desc60=%s mapBase=Map/Common Map/Map%02u/ "
		"mapDesc=data\\Map\\Common Map\\Map%02u\\map_desc.lua",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		processor,
		mapInfo,
		outInfo,
		mapId,
		infoMap,
		result,
		reinterpret_cast<void*>(objectBegin),
		reinterpret_cast<void*>(objectEnd),
		static_cast<unsigned long long>(objectCount),
		descName,
		descScPath,
		descField40,
		descField60,
		mapId,
		mapId);
}

void LogLoadScCache(
	const char* stage,
	std::uintptr_t callRva,
	void* arg1,
	void* arg2,
	void* arg3,
	void* arg4,
	void* result)
{
	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s arg1=%p arg2=%p arg3=%p arg4=%p "
		"result=%p out1=%p out2=%p out3=%p",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		arg1,
		arg2,
		arg3,
		arg4,
		result,
		ReadSafe<void*>(arg1),
		ReadSafe<void*>(arg2),
		ReadSafe<void*>(arg4));
}

void LogApplySceneObjects(
	const char* stage,
	std::uintptr_t callRva,
	void* processor,
	void* mapInfo)
{
	const unsigned int infoMap = ReadSafe<unsigned int>(mapInfo);
	void* root = ReadPointerField(mapInfo, kMapPreLoadRootOffset);
	void* rootVtable = ReadPointerField(root, 0);
	const std::uintptr_t objectBegin = ReadSafe<std::uintptr_t>(AddOffset(mapInfo, kMapObjectListBeginOffset));
	const std::uintptr_t objectEnd = ReadSafe<std::uintptr_t>(AddOffset(mapInfo, kMapObjectListEndOffset));
	const std::size_t objectCount = CountContiguousEntries(objectBegin, objectEnd, kMapObjectRecordSize);
	const std::uintptr_t attachBegin = ReadSafe<std::uintptr_t>(AddOffset(processor, kSceneAttachListBeginOffset));
	const std::uintptr_t attachEnd = ReadSafe<std::uintptr_t>(AddOffset(processor, kSceneAttachListEndOffset));
	const std::size_t attachCount = CountContiguousEntries(attachBegin, attachEnd, sizeof(void*));

	char firstNode[192] = {};
	char firstPath[192] = {};
	char firstChild[192] = {};
	char lastNode[192] = {};
	char lastPath[192] = {};
	char lastChild[192] = {};
	if (objectCount > 0 && objectCount < 0x10000) {
		const auto firstRecord = reinterpret_cast<const void*>(objectBegin);
		CopyStdStringSafe(firstRecord, firstNode, sizeof(firstNode));
		CopyStdStringSafe(AddOffset(firstRecord, 0x20), firstPath, sizeof(firstPath));
		CopyStdStringSafe(AddOffset(firstRecord, 0x40), firstChild, sizeof(firstChild));

		const auto lastRecord = reinterpret_cast<const void*>(objectBegin + ((objectCount - 1) * kMapObjectRecordSize));
		CopyStdStringSafe(lastRecord, lastNode, sizeof(lastNode));
		CopyStdStringSafe(AddOffset(lastRecord, 0x20), lastPath, sizeof(lastPath));
		CopyStdStringSafe(AddOffset(lastRecord, 0x40), lastChild, sizeof(lastChild));
	}

	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s processor=%p mapInfo=%p infoMap=%u "
		"scRoot=%p scRootVtable=%p objectBegin=%p objectEnd=%p objectCount=%llu "
		"attachBegin=%p attachEnd=%p attachCount=%llu firstNode=%s firstPath=%s firstChild=%s "
		"lastNode=%s lastPath=%s lastChild=%s",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		processor,
		mapInfo,
		infoMap,
		root,
		rootVtable,
		reinterpret_cast<void*>(objectBegin),
		reinterpret_cast<void*>(objectEnd),
		static_cast<unsigned long long>(objectCount),
		reinterpret_cast<void*>(attachBegin),
		reinterpret_cast<void*>(attachEnd),
		static_cast<unsigned long long>(attachCount),
		firstNode,
		firstPath,
		firstChild,
		lastNode,
		lastPath,
		lastChild);
}

void LogStartTransform(
	const char* stage,
	std::uintptr_t callRva,
	void* roadMap,
	void* transform,
	unsigned int index,
	int direction,
	float distance,
	int variant,
	void* result)
{
	const StartTransformSnapshot snapshot = ReadStartTransform(transform);
	qqspeed::logging::Logf(
		"topkart_spawn stage=%s callRva=0x%llX site=%s roadMap=%p transform=%p result=%p "
		"index=%u direction=%d distance=%.3f variant=%d valid=%u activeLoadScMap=%u configMap=%u "
		"pos=(%.3f,%.3f,%.3f) dir=(%.3f,%.3f,%.3f) "
		"row0=(%.3f,%.3f,%.3f,%.3f) row1=(%.3f,%.3f,%.3f,%.3f) "
		"row2=(%.3f,%.3f,%.3f,%.3f) extra=%.3f",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		roadMap,
		transform,
		result,
		index,
		direction,
		static_cast<double>(distance),
		variant,
		snapshot.valid ? 1u : 0u,
		ActiveLoadScMapId(),
		CurrentConfigMapId(),
		static_cast<double>(StartTransformValue(snapshot, 9)),
		static_cast<double>(StartTransformValue(snapshot, 10)),
		static_cast<double>(StartTransformValue(snapshot, 11)),
		static_cast<double>(StartTransformValue(snapshot, 0)),
		static_cast<double>(StartTransformValue(snapshot, 3)),
		static_cast<double>(StartTransformValue(snapshot, 6)),
		static_cast<double>(StartTransformValue(snapshot, 0)),
		static_cast<double>(StartTransformValue(snapshot, 1)),
		static_cast<double>(StartTransformValue(snapshot, 2)),
		static_cast<double>(StartTransformValue(snapshot, 3)),
		static_cast<double>(StartTransformValue(snapshot, 4)),
		static_cast<double>(StartTransformValue(snapshot, 5)),
		static_cast<double>(StartTransformValue(snapshot, 6)),
		static_cast<double>(StartTransformValue(snapshot, 7)),
		static_cast<double>(StartTransformValue(snapshot, 8)),
		static_cast<double>(StartTransformValue(snapshot, 9)),
		static_cast<double>(StartTransformValue(snapshot, 10)),
		static_cast<double>(StartTransformValue(snapshot, 11)),
		static_cast<double>(StartTransformValue(snapshot, 12)));
}

void LogEnterPhySceneSetTransform(
	const char* stage,
	std::uintptr_t callRva,
	void* player,
	void* transform,
	void* vtable,
	void* setter,
	__int64 result)
{
	const StartTransformSnapshot snapshot = ReadStartTransform(transform);
	void* roadMap = CurrentRaceRoadMap();
	const unsigned int raceId = ReadSafe<unsigned int>(AddOffset(player, kPlayerRaceIdOffset));
	const unsigned short seatField = ReadSafe<unsigned short>(AddOffset(player, kPlayerSeatFieldOffset));
	qqspeed::logging::Logf(
		"topkart_spawn stage=%s callRva=0x%llX site=%s player=%p vtable=%p setter=%p "
		"transform=%p roadMap=%p result=0x%llX activeLoadScMap=%u configMap=%u "
		"playerRaceId=%u seatField93CE=%u valid=%u "
		"pos=(%.3f,%.3f,%.3f) dir=(%.3f,%.3f,%.3f) "
		"row0=(%.3f,%.3f,%.3f,%.3f) row1=(%.3f,%.3f,%.3f,%.3f) "
		"row2=(%.3f,%.3f,%.3f,%.3f) extra=%.3f",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		player,
		vtable,
		setter,
		transform,
		roadMap,
		static_cast<unsigned long long>(result),
		ActiveLoadScMapId(),
		CurrentConfigMapId(),
		raceId,
		seatField,
		snapshot.valid ? 1u : 0u,
		static_cast<double>(StartTransformValue(snapshot, 9)),
		static_cast<double>(StartTransformValue(snapshot, 10)),
		static_cast<double>(StartTransformValue(snapshot, 11)),
		static_cast<double>(StartTransformValue(snapshot, 0)),
		static_cast<double>(StartTransformValue(snapshot, 3)),
		static_cast<double>(StartTransformValue(snapshot, 6)),
		static_cast<double>(StartTransformValue(snapshot, 0)),
		static_cast<double>(StartTransformValue(snapshot, 1)),
		static_cast<double>(StartTransformValue(snapshot, 2)),
		static_cast<double>(StartTransformValue(snapshot, 3)),
		static_cast<double>(StartTransformValue(snapshot, 4)),
		static_cast<double>(StartTransformValue(snapshot, 5)),
		static_cast<double>(StartTransformValue(snapshot, 6)),
		static_cast<double>(StartTransformValue(snapshot, 7)),
		static_cast<double>(StartTransformValue(snapshot, 8)),
		static_cast<double>(StartTransformValue(snapshot, 9)),
		static_cast<double>(StartTransformValue(snapshot, 10)),
		static_cast<double>(StartTransformValue(snapshot, 11)),
		static_cast<double>(StartTransformValue(snapshot, 12)));
}

void LogRoadMapStartTable(const char* stage, std::uintptr_t callRva)
{
	void* scene = CurrentRaceScene();
	void* link = ReadPointerField(scene, kRoadMapLinkOffset);
	void* roadMap = ReadPointerField(link, kRoadMapFromLinkOffset);
	void* variant1 = ReadPointerField(roadMap, kRoadMapStartTableVariant1Offset);
	void* variant2 = ReadPointerField(roadMap, kRoadMapStartTableVariant2Offset);
	qqspeed::logging::Logf(
		"topkart_spawn stage=%s callRva=0x%llX site=%s scene=%p link=%p roadMap=%p "
		"variant1=%p variant2=%p activeLoadScMap=%u configMap=%u",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		scene,
		link,
		roadMap,
		variant1,
		variant2,
		ActiveLoadScMapId(),
		CurrentConfigMapId());

	for (unsigned int i = 0; roadMap && i < 12; ++i) {
		void* transform = const_cast<unsigned char*>(
			AddOffset(roadMap, kRoadMapStartTableOffset + (i * kRoadMapStartTransformStride)));
		LogStartTransform("enter_physcene_start_table", callRva, roadMap, transform, i, 0, 0.0f, 0, transform);
	}
}

void LogNifLoad(
	const char* stage,
	std::uintptr_t callRva,
	void* manager,
	void* outSmartPtr,
	const char* path,
	unsigned char a4,
	unsigned char a5,
	__int64 result)
{
	if (!g_verboseProbeLogs) {
		return;
	}

	char safePath[320] = {};
	CopyCStringSafe(path, safePath, sizeof(safePath));
	void* outValue = ReadSafe<void*>(outSmartPtr);
	void* outValueVtable = ReadPointerField(outValue, 0);
	MapIdThreadState* threadState = g_mapIdThreadState.Get();
	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s manager=%p outSmartPtr=%p outValue=%p "
		"outValueVtable=%p path=%s a4=%u a5=%u threadMap=%u threadDepth=%u globalMap=%u "
		"globalDepth=%u result=%p",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		manager,
		outSmartPtr,
		outValue,
		outValueVtable,
		safePath,
		static_cast<unsigned int>(a4),
		static_cast<unsigned int>(a5),
		threadState ? threadState->activeLoadScMapId : 0,
		threadState ? threadState->activeLoadScDepth : 0,
		g_activeLoadScMapId.load(std::memory_order_acquire),
		g_activeLoadScDepth.load(std::memory_order_acquire),
		reinterpret_cast<void*>(result));
}

void LogNifLoadPathOverride(
	std::uintptr_t callRva,
	const char* originalPath,
	const char* overridePath,
	unsigned int activeMapId)
{
	char safeOriginal[320] = {};
	char safeOverride[320] = {};
	CopyCStringSafe(originalPath, safeOriginal, sizeof(safeOriginal));
	CopyCStringSafe(overridePath, safeOverride, sizeof(safeOverride));
	MapIdThreadState* threadState = g_mapIdThreadState.Get();
	qqspeed::logging::Logf(
		"topkart_scene stage=nif_load_path_override callRva=0x%llX site=%s activeInfoMap=%u "
		"threadMap=%u threadDepth=%u globalMap=%u globalDepth=%u originalPath=%s overridePath=%s",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		activeMapId,
		threadState ? threadState->activeLoadScMapId : 0,
		threadState ? threadState->activeLoadScDepth : 0,
		g_activeLoadScMapId.load(std::memory_order_acquire),
		g_activeLoadScDepth.load(std::memory_order_acquire),
		safeOriginal,
		safeOverride);
}

void LogLoadMapResPickNifProbe(
	const char* stage,
	std::uintptr_t callRva,
	void* manager,
	void* outSmartPtr,
	const char* path,
	unsigned char a4,
	unsigned char a5,
	__int64 result)
{
	char safePath[320] = {};
	char expectedPath[320] = {};
	CopyCStringSafe(path, safePath, sizeof(safePath));

	const unsigned int pathMap = ExtractMapIdFromPathText(safePath);
	const unsigned int activeMap = ActiveLoadMapResMapId();
	void* activeMapInfo = ActiveLoadMapResMapInfo();
	const unsigned int mapInfoMap = ReadSafe<unsigned int>(activeMapInfo);
	const unsigned int expectedMap = activeMap != 0 ? activeMap : mapInfoMap;
	FormatExpectedPickPath(
		expectedMap,
		callRva == kNifLoadPickShadowCallRva,
		expectedPath,
		sizeof(expectedPath));

	void* outValue = ReadSafe<void*>(outSmartPtr);
	void* outValueVtable = ReadPointerField(outValue, 0);
	const bool mismatch = pathMap != 0 && expectedMap != 0 && pathMap != expectedMap;
	if (!mismatch && !g_verboseProbeLogs) {
		return;
	}
	MapIdThreadState* threadState = g_mapIdThreadState.Get();

	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s manager=%p outSmartPtr=%p outValue=%p "
		"outValueVtable=%p path=%s pathMap=%u expectedMap=%u expectedPath=%s mismatch=%u "
		"loadMapResThreadMap=%u loadMapResThreadDepth=%u loadMapResThreadInfo=%p "
		"loadMapResGlobalMap=%u loadMapResGlobalDepth=%u loadMapResGlobalInfo=%p "
		"mapInfoMap=%u activeLoadScMap=%u a4=%u a5=%u result=%p",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		manager,
		outSmartPtr,
		outValue,
		outValueVtable,
		safePath,
		pathMap,
		expectedMap,
		expectedPath,
		mismatch ? 1u : 0u,
		threadState ? threadState->activeLoadMapResMapId : 0,
		threadState ? threadState->activeLoadMapResDepth : 0,
		threadState ? threadState->activeLoadMapResMapInfo : nullptr,
		g_activeLoadMapResMapId.load(std::memory_order_acquire),
		g_activeLoadMapResDepth.load(std::memory_order_acquire),
		reinterpret_cast<void*>(g_activeLoadMapResMapInfo.load(std::memory_order_acquire)),
		mapInfoMap,
		ActiveLoadScMapId(),
		static_cast<unsigned int>(a4),
		static_cast<unsigned int>(a5),
		reinterpret_cast<void*>(result));
}

bool LogNifNormalizePath(
	std::uintptr_t callRva,
	void* manager,
	const char* path,
	void* outStdString)
{
	if (!g_verboseProbeLogs) {
		return false;
	}

	char safePath[320] = {};
	char normalizedPath[320] = {};
	CopyCStringSafe(path, safePath, sizeof(safePath));
	CopyStdStringSafe(outStdString, normalizedPath, sizeof(normalizedPath));
	if (!IsMapResourcePath(safePath) && !IsMapResourcePath(normalizedPath)) {
		return false;
	}

	qqspeed::logging::Logf(
		"topkart_scene stage=nif_path_normalized callRva=0x%llX site=%s manager=%p outStdString=%p "
		"path=%s normalized=%s",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		manager,
		outStdString,
		safePath,
		normalizedPath);
	return true;
}

void LogNifFetchResource(
	const char* stage,
	std::uintptr_t callRva,
	void* manager,
	void* outResourceSmartPtr,
	const char* normalizedPath,
	void* a4,
	unsigned char a5,
	void* result)
{
	if (!g_verboseProbeLogs) {
		return;
	}

	char safePath[320] = {};
	CopyCStringSafe(normalizedPath, safePath, sizeof(safePath));
	MapIdThreadState* threadState = g_mapIdThreadState.Get();
	if ((!threadState || !threadState->traceMapNifLoader) && !IsMapResourcePath(safePath)) {
		return;
	}

	void* resource = ReadSafe<void*>(outResourceSmartPtr);
	void* resourceVtable = ReadPointerField(resource, 0);
	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s manager=%p outResourceSmartPtr=%p "
		"resource=%p resourceVtable=%p normalized=%s a4=%p a5=%u result=%p",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		manager,
		outResourceSmartPtr,
		resource,
		resourceVtable,
		safePath,
		a4,
		static_cast<unsigned int>(a5),
		result);
}

void LogNifExtractRoot(
	const char* stage,
	std::uintptr_t callRva,
	void* manager,
	void* outRootSmartPtr,
	void* resource,
	unsigned char flags,
	void* result)
{
	if (!g_verboseProbeLogs) {
		return;
	}

	MapIdThreadState* threadState = g_mapIdThreadState.Get();
	const bool shouldLog = threadState &&
		(threadState->traceMapNifLoader || (resource && resource == threadState->traceMapNifResource));
	if (!shouldLog) {
		return;
	}

	void* root = ReadSafe<void*>(outRootSmartPtr);
	void* rootVtable = ReadPointerField(root, 0);
	void* resourceVtable = ReadPointerField(resource, 0);
	qqspeed::logging::Logf(
		"topkart_scene stage=%s callRva=0x%llX site=%s manager=%p outRootSmartPtr=%p "
		"resource=%p resourceVtable=%p root=%p rootVtable=%p flags=%u result=%p",
		stage ? stage : "unknown",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		manager,
		outRootSmartPtr,
		resource,
		resourceVtable,
		root,
		rootVtable,
		static_cast<unsigned int>(flags),
		result);
}

void LogMapDescFieldLookup(
	std::uintptr_t callRva,
	void* table,
	void* key,
	void* result)
{
	char keyText[192] = {};
	CopyStdStringSafe(key, keyText, sizeof(keyText));
	qqspeed::logging::Logf(
		"topkart_scene stage=map_desc_field_lookup callRva=0x%llX site=%s table=%p keyObj=%p key=%s result=%p",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		table,
		key,
		keyText,
		result);
}

int __fastcall HookedPrepareGameBegin(void* self, void* notify)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LogSnapshot("prepare_game_begin", callRva, self, notify, TakeSnapshot(notify));

	PrepareGameBeginFn original = g_originalPrepareGameBegin;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<PrepareGameBeginFn>(g_topKartBase + kPrepareGameBeginRva);
	}
	return original ? original(self, notify) : 0;
}

__int64 __fastcall HookedLevelManagerEnter(void* levelMgr, void* notify, int a3, unsigned char a4, int a5)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LogSnapshot("level_enter", callRva, levelMgr, notify, TakeSnapshot(notify));

	LevelManagerEnterFn original = g_originalLevelManagerEnter;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LevelManagerEnterFn>(g_topKartBase + kLevelManagerEnterRva);
	}
	const __int64 result = original ? original(levelMgr, notify, a3, a4, a5) : 0;
	qqspeed::logging::Logf(
		"topkart_mapid stage=level_enter_result callRva=0x%llX site=%s levelMgr=%p notify=%p a3=%d a4=%u a5=%d result=%lld",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		levelMgr,
		notify,
		a3,
		static_cast<unsigned int>(a4),
		a5,
		static_cast<long long>(result));
	return result;
}

bool __fastcall HookedAuroraUgcCheck()
{
	const std::uintptr_t callRva = CurrentCallRva();
	AuroraUgcCheckFn original = g_originalAuroraUgcCheck;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<AuroraUgcCheckFn>(g_topKartBase + kAuroraUgcCheckRva);
	}

	const bool result = original ? original() : false;
	LogSceneMode("ugc_check", callRva, result, TakeSceneModeSnapshot());
	return result;
}

void __fastcall HookedRoadMapProcessorReset(void* processor)
{
	const std::uintptr_t callRva = CurrentCallRva();
	const ProtectedMapPairSnapshot beforeLoadSc =
		ReadProtectedMapPair(kProtectedLoadScMapKeyRva, kProtectedLoadScMapValuePtrRva);
	const ProtectedMapPairSnapshot beforeLoadMapRes =
		ReadProtectedMapPair(kProtectedLoadMapResMapKeyRva, kProtectedLoadMapResMapValuePtrRva);

	RoadMapProcessorResetFn original = g_originalRoadMapProcessorReset;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<RoadMapProcessorResetFn>(g_topKartBase + kRoadMapProcessorResetRva);
	}
	if (original) {
		original(processor);
	}

	const bool clearLoadSc =
		ResetProtectedMapPairToZero(kProtectedLoadScMapKeyRva, kProtectedLoadScMapValuePtrRva);
	const bool clearLoadMapRes =
		ResetProtectedMapPairToZero(kProtectedLoadMapResMapKeyRva, kProtectedLoadMapResMapValuePtrRva);

	const ProtectedMapPairSnapshot afterLoadSc =
		ReadProtectedMapPair(kProtectedLoadScMapKeyRva, kProtectedLoadScMapValuePtrRva);
	const ProtectedMapPairSnapshot afterLoadMapRes =
		ReadProtectedMapPair(kProtectedLoadMapResMapKeyRva, kProtectedLoadMapResMapValuePtrRva);
	LogRoadMapResetProtectedMaps(
		callRva,
		processor,
		beforeLoadSc,
		beforeLoadMapRes,
		afterLoadSc,
		afterLoadMapRes,
		original != nullptr,
		clearLoadSc,
		clearLoadMapRes);
}

unsigned char __fastcall HookedLoadMapRes(void* processor, void* mapInfo, unsigned int a3, unsigned char a4, unsigned char a5)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LoadMapResContextScope loadMapResContext(ReadSafe<unsigned int>(mapInfo), mapInfo);
	LogLoadMapRes("load_map_res_begin", callRva, processor, mapInfo, a3, a4, a5, -1);

	LoadMapResFn original = g_originalLoadMapRes;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LoadMapResFn>(g_topKartBase + kLoadMapResRva);
	}
	const unsigned char result = original ? original(processor, mapInfo, a3, a4, a5) : 0;

	LogLoadMapRes("load_map_res_end", callRva, processor, mapInfo, a3, a4, a5, result ? 1 : 0);
	return result;
}

unsigned char __fastcall HookedLoadMapDesc(void* processor, unsigned int mapId, void* outInfo, void* mapInfo)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LogLoadMapDesc("load_map_desc_begin", callRva, processor, mapId, outInfo, mapInfo, -1);

	LoadMapDescFn original = g_originalLoadMapDesc;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LoadMapDescFn>(g_topKartBase + kLoadMapDescRva);
	}
	const unsigned char result = original ? original(processor, mapId, outInfo, mapInfo) : 0;

	LogLoadMapDesc("load_map_desc_end", callRva, processor, mapId, outInfo, mapInfo, result ? 1 : 0);
	return result;
}

void __fastcall HookedLoadSc(void* processor, void* mapInfo, unsigned char a3, unsigned char a4)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LoadScContextScope loadScContext(ReadSafe<unsigned int>(mapInfo));
	LogLoadSc("load_sc_begin", callRva, processor, mapInfo, a3, a4);

	LoadScFn original = g_originalLoadSc;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LoadScFn>(g_topKartBase + kLoadScRva);
	}
	if (original) {
		original(processor, mapInfo, a3, a4);
	}

	LogLoadSc("load_sc_end", callRva, processor, mapInfo, a3, a4);
}

void __fastcall HookedApplySceneObjects(void* processor, void* mapInfo)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LogApplySceneObjects("apply_objects_begin", callRva, processor, mapInfo);

	ApplySceneObjectsFn original = g_originalApplySceneObjects;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<ApplySceneObjectsFn>(g_topKartBase + kApplySceneObjectsRva);
	}
	if (original) {
		original(processor, mapInfo);
	}

	LogApplySceneObjects("apply_objects_end", callRva, processor, mapInfo);
}

void* __fastcall HookedLoadScCacheFetch(void* cacheMgr, void* outValue, void* host, void* request)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LoadScCacheFetchFn original = g_originalLoadScCacheFetch;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LoadScCacheFetchFn>(g_topKartBase + kLoadScCacheFetchRva);
	}
	void* result = original ? original(cacheMgr, outValue, host, request) : nullptr;
	LogLoadScCache("load_sc_cache_fetch", callRva, cacheMgr, outValue, host, request, result);
	return result;
}

void* __fastcall HookedLoadScCacheExtract(void* outSmartPtr, void* cacheResult)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LoadScCacheExtractFn original = g_originalLoadScCacheExtract;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LoadScCacheExtractFn>(g_topKartBase + kLoadScCacheExtractRva);
	}
	void* result = original ? original(outSmartPtr, cacheResult) : nullptr;
	LogLoadScCache("load_sc_cache_extract", callRva, outSmartPtr, cacheResult, nullptr, nullptr, result);
	return result;
}

__int64 __fastcall HookedNifLoad(void* manager, void* outSmartPtr, const char* path, unsigned char a4, unsigned char a5)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LogNifLoad("nif_load_begin", callRva, manager, outSmartPtr, path, a4, a5, 0);
	if (IsLoadMapResPickCallsite(callRva)) {
		LogLoadMapResPickNifProbe(
			"load_map_res_pick_nif_begin",
			callRva,
			manager,
			outSmartPtr,
			path,
			a4,
			a5,
			0);
	}

	char overridePath[320] = {};
	const char* effectivePath = path;
	const unsigned int activeMapId = ActiveLoadScMapId();
	if (callRva == kNifLoadScCoroutineCallRva && activeMapId != 0) {
		FormatExpectedScPath(activeMapId, overridePath, sizeof(overridePath));
		char safeOriginal[320] = {};
		CopyCStringSafe(path, safeOriginal, sizeof(safeOriginal));
		if (overridePath[0] && !PathEqualsAsciiNoCase(safeOriginal, overridePath)) {
			LogNifLoadPathOverride(callRva, path, overridePath, activeMapId);
			effectivePath = overridePath;
		}
	}

	NifLoadFn original = g_originalNifLoad;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<NifLoadFn>(g_topKartBase + kNifLoadRva);
	}
	const __int64 result = original ? original(manager, outSmartPtr, effectivePath, a4, a5) : 0;

	if (IsLoadMapResPickCallsite(callRva)) {
		LogLoadMapResPickNifProbe(
			"load_map_res_pick_nif_end",
			callRva,
			manager,
			outSmartPtr,
			effectivePath,
			a4,
			a5,
			result);
	}
	LogNifLoad("nif_load_end", callRva, manager, outSmartPtr, effectivePath, a4, a5, result);
	return result;
}

void __fastcall HookedNifNormalizePath(void* manager, const char* path, void* outStdString)
{
	const std::uintptr_t callRva = CurrentCallRva();
	NifNormalizePathFn original = g_originalNifNormalizePath;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<NifNormalizePathFn>(g_topKartBase + kNifNormalizePathRva);
	}
	if (original) {
		original(manager, path, outStdString);
	}

	if (MapIdThreadState* threadState = g_mapIdThreadState.GetOrCreate()) {
		threadState->traceMapNifResource = nullptr;
		threadState->traceMapNifLoader = LogNifNormalizePath(callRva, manager, path, outStdString);
	}
}

void* __fastcall HookedNifFetchResource(void* manager, void* outResourceSmartPtr, const char* normalizedPath, void* a4, unsigned char a5)
{
	const std::uintptr_t callRva = CurrentCallRva();
	MapIdThreadState* threadState = g_mapIdThreadState.GetOrCreate();
	char safePath[320] = {};
	CopyCStringSafe(normalizedPath, safePath, sizeof(safePath));
	if (threadState && IsMapResourcePath(safePath)) {
		threadState->traceMapNifLoader = true;
	}

	NifFetchResourceFn original = g_originalNifFetchResource;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<NifFetchResourceFn>(g_topKartBase + kNifFetchResourceRva);
	}
	void* result = original ? original(manager, outResourceSmartPtr, normalizedPath, a4, a5) : nullptr;
	if (threadState) {
		threadState->traceMapNifResource = ReadSafe<void*>(outResourceSmartPtr);
	}

	LogNifFetchResource("nif_resource_fetch_end", callRva, manager, outResourceSmartPtr, normalizedPath, a4, a5, result);
	return result;
}

void* __fastcall HookedNifExtractRoot(void* manager, void* outRootSmartPtr, void* resource, unsigned char flags)
{
	const std::uintptr_t callRva = CurrentCallRva();

	NifExtractRootFn original = g_originalNifExtractRoot;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<NifExtractRootFn>(g_topKartBase + kNifExtractRootRva);
	}
	void* result = original ? original(manager, outRootSmartPtr, resource, flags) : nullptr;

	LogNifExtractRoot("nif_root_extract_end", callRva, manager, outRootSmartPtr, resource, flags, result);
	if (MapIdThreadState* threadState = g_mapIdThreadState.Get()) {
		threadState->traceMapNifLoader = false;
		threadState->traceMapNifResource = nullptr;
	}
	return result;
}

void* __fastcall HookedMapDescFieldLookup(void* table, void* key)
{
	const std::uintptr_t callRva = CurrentCallRva();
	LuaFieldLookupFn original = g_originalLuaFieldLookup;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LuaFieldLookupFn>(g_topKartBase + kLuaFieldLookupRva);
	}
	void* result = original ? original(table, key) : nullptr;
	LogMapDescFieldLookup(callRva, table, key, result);
	return result;
}

__int64 __fastcall HookedRaceEnterPhySceneKartPhys()
{
	const std::uintptr_t callRva = CurrentCallRva();
	LogRoadMapStartTable("enter_physcene_kart_phys_begin", callRva);

	RaceEnterPhySceneKartPhysFn original = g_originalRaceEnterPhySceneKartPhys;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<RaceEnterPhySceneKartPhysFn>(g_topKartBase + kRaceEnterPhySceneKartPhysRva);
	}
	const __int64 result = original ? original() : 0;

	LogRoadMapStartTable("enter_physcene_kart_phys_end", callRva);
	return result;
}

void* __fastcall HookedRoadMapStartTransform(
	void* roadMap,
	void* outTransform,
	unsigned int index,
	int direction,
	float distance)
{
	const std::uintptr_t callRva = CurrentCallRva();
	RoadMapStartTransformFn original = g_originalRoadMapStartTransform;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<RoadMapStartTransformFn>(g_topKartBase + kRoadMapStartTransformRva);
	}
	void* result = original ? original(roadMap, outTransform, index, direction, distance) : nullptr;
	LogStartTransform(
		"start_transform_result",
		callRva,
		roadMap,
		outTransform,
		index,
		direction,
		distance,
		0,
		result);
	return result;
}

void* __fastcall HookedRoadMapStartTransformVariant(
	void* roadMap,
	void* outTransform,
	unsigned int index,
	__int64 a4,
	int a5,
	int variant)
{
	const std::uintptr_t callRva = CurrentCallRva();
	RoadMapStartTransformVariantFn original = g_originalRoadMapStartTransformVariant;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<RoadMapStartTransformVariantFn>(g_topKartBase + kRoadMapStartTransformVariantRva);
	}
	void* result = original ? original(roadMap, outTransform, index, a4, a5, variant) : nullptr;
	LogStartTransform(
		"start_transform_variant_result",
		callRva,
		roadMap,
		outTransform,
		index,
		a5,
		0.0f,
		variant,
		result);
	return result;
}

__int64 __fastcall HookedEnterPhySceneSetTransform(void* player, void* transform)
{
	const std::uintptr_t callRva = CurrentCallRva();
	void* vtable = ReadPointerField(player, 0);
	EnterPhySceneSetTransformFn original =
		ReadSafe<EnterPhySceneSetTransformFn>(AddOffset(vtable, kEnterPhySceneSetTransformVtableOffset));

	LogEnterPhySceneSetTransform(
		"enter_physcene_final_set_transform_begin",
		callRva,
		player,
		transform,
		vtable,
		reinterpret_cast<void*>(original),
		0);

	const __int64 result = original ? original(player, transform) : 0;

	LogEnterPhySceneSetTransform(
		"enter_physcene_final_set_transform_end",
		callRva,
		player,
		transform,
		vtable,
		reinterpret_cast<void*>(original),
		result);
	return result;
}

std::size_t PatchMapIdCalls(std::uintptr_t topKartBase)
{
	std::size_t patched = 0;
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kPrepareGameBeginCallRvas,
		_countof(kPrepareGameBeginCallRvas),
		kPrepareGameBeginRva,
		reinterpret_cast<void*>(&HookedPrepareGameBegin),
		"TopKartMapId prepare_game_begin");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kLevelManagerEnterCallRvas,
		_countof(kLevelManagerEnterCallRvas),
		kLevelManagerEnterRva,
		reinterpret_cast<void*>(&HookedLevelManagerEnter),
		"TopKartMapId level_enter");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kAuroraUgcCheckCallRvas,
		_countof(kAuroraUgcCheckCallRvas),
		kAuroraUgcCheckRva,
		reinterpret_cast<void*>(&HookedAuroraUgcCheck),
		"TopKartMapId scene_ugc_check");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kRoadMapProcessorResetCallRvas,
		_countof(kRoadMapProcessorResetCallRvas),
		kRoadMapProcessorResetRva,
		reinterpret_cast<void*>(&HookedRoadMapProcessorReset),
		"TopKartMapId scene_roadmap_reset");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kLoadMapResCallRvas,
		_countof(kLoadMapResCallRvas),
		kLoadMapResRva,
		reinterpret_cast<void*>(&HookedLoadMapRes),
		"TopKartMapId scene_load_map_res");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kLoadMapDescCallRvas,
		_countof(kLoadMapDescCallRvas),
		kLoadMapDescRva,
		reinterpret_cast<void*>(&HookedLoadMapDesc),
		"TopKartMapId scene_load_map_desc");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kLoadScCallRvas,
		_countof(kLoadScCallRvas),
		kLoadScRva,
		reinterpret_cast<void*>(&HookedLoadSc),
		"TopKartMapId scene_load_sc");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kLoadScCacheFetchCallRvas,
		_countof(kLoadScCacheFetchCallRvas),
		kLoadScCacheFetchRva,
		reinterpret_cast<void*>(&HookedLoadScCacheFetch),
		"TopKartMapId scene_load_sc_cache_fetch");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kLoadScCacheExtractCallRvas,
		_countof(kLoadScCacheExtractCallRvas),
		kLoadScCacheExtractRva,
		reinterpret_cast<void*>(&HookedLoadScCacheExtract),
		"TopKartMapId scene_load_sc_cache_extract");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kApplySceneObjectsCallRvas,
		_countof(kApplySceneObjectsCallRvas),
		kApplySceneObjectsRva,
		reinterpret_cast<void*>(&HookedApplySceneObjects),
		"TopKartMapId scene_apply_objects");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kNifLoadCallRvas,
		_countof(kNifLoadCallRvas),
		kNifLoadRva,
		reinterpret_cast<void*>(&HookedNifLoad),
		"TopKartMapId scene_nif_load");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kNifNormalizePathCallRvas,
		_countof(kNifNormalizePathCallRvas),
		kNifNormalizePathRva,
		reinterpret_cast<void*>(&HookedNifNormalizePath),
		"TopKartMapId nif_normalize_path");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kNifFetchResourceCallRvas,
		_countof(kNifFetchResourceCallRvas),
		kNifFetchResourceRva,
		reinterpret_cast<void*>(&HookedNifFetchResource),
		"TopKartMapId nif_fetch_resource");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kNifExtractRootCallRvas,
		_countof(kNifExtractRootCallRvas),
		kNifExtractRootRva,
		reinterpret_cast<void*>(&HookedNifExtractRoot),
		"TopKartMapId nif_extract_root");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kMapDescFieldLookupCallRvas,
		_countof(kMapDescFieldLookupCallRvas),
		kLuaFieldLookupRva,
		reinterpret_cast<void*>(&HookedMapDescFieldLookup),
		"TopKartMapId scene_map_desc_field_lookup");
	if (kEnableRacePlacementProbe) {
		patched += qqspeed::network::patch::PatchDirectCalls(
			topKartBase,
			kRaceEnterPhySceneKartPhysCallRvas,
			_countof(kRaceEnterPhySceneKartPhysCallRvas),
			kRaceEnterPhySceneKartPhysRva,
			reinterpret_cast<void*>(&HookedRaceEnterPhySceneKartPhys),
			"TopKartMapId race_enter_physcene_kart_phys");
		patched += qqspeed::network::patch::PatchDirectCalls(
			topKartBase,
			kRoadMapStartTransformCallRvas,
			_countof(kRoadMapStartTransformCallRvas),
			kRoadMapStartTransformRva,
			reinterpret_cast<void*>(&HookedRoadMapStartTransform),
			"TopKartMapId race_start_transform");
		patched += qqspeed::network::patch::PatchDirectCalls(
			topKartBase,
			kRoadMapStartTransformVariantCallRvas,
			_countof(kRoadMapStartTransformVariantCallRvas),
			kRoadMapStartTransformVariantRva,
			reinterpret_cast<void*>(&HookedRoadMapStartTransformVariant),
			"TopKartMapId race_start_transform_variant");
		patched += qqspeed::network::patch::PatchCallSitesWithRelay(
			topKartBase,
			kEnterPhySceneSetTransformCallRvas,
			_countof(kEnterPhySceneSetTransformCallRvas),
			kEnterPhySceneSetTransformExpectedBytes,
			sizeof(kEnterPhySceneSetTransformExpectedBytes),
			reinterpret_cast<void*>(&HookedEnterPhySceneSetTransform),
			"TopKartMapId race_enter_physcene_final_set_transform");
	}
	else {
		qqspeed::logging::Logf("TopKartMapId race placement probes disabled");
	}
	return patched;
}

DWORD WINAPI MapIdProbeThread(void* param)
{
	std::unique_ptr<ProbeThreadContext> context(static_cast<ProbeThreadContext*>(param));
	if (!context) {
		return 0;
	}

	DWORD waited = 0;
	while (waited <= kTopKartWaitMs) {
		HMODULE topKartModule = GetModuleHandleW(context->config.topKartModuleName);
		if (topKartModule) {
			qqspeed::topkart::InstallMapIdProbe(topKartModule, context->config);
			return 0;
		}
		Sleep(kTopKartPollMs);
		waited += kTopKartPollMs;
	}

	qqspeed::logging::Logf("topkart_mapid probe skipped: Top-Kart.dll not loaded after %lu ms", kTopKartWaitMs);
	return 0;
}
}

namespace qqspeed::topkart
{
bool InstallMapIdProbe(HMODULE topKartModule, const config::HookConfig& config)
{
	if (!config.features.enableTopKartMapIdProbe) {
		qqspeed::logging::Logf("topkart_mapid probe disabled");
		return false;
	}
	if (!topKartModule) {
		return false;
	}
	if (InterlockedCompareExchange(&g_installState, 0, 0) == 2) {
		return g_patchedCalls > 0;
	}
	if (InterlockedCompareExchange(&g_installState, 1, 0) != 0) {
		return InterlockedCompareExchange(&g_installState, 0, 0) == 2 && g_patchedCalls > 0;
	}

	g_topKartBase = reinterpret_cast<std::uintptr_t>(topKartModule);
	g_verboseProbeLogs = config.features.enableVerboseProbeLogs;
	g_originalPrepareGameBegin = reinterpret_cast<PrepareGameBeginFn>(g_topKartBase + kPrepareGameBeginRva);
	g_originalLevelManagerEnter = reinterpret_cast<LevelManagerEnterFn>(g_topKartBase + kLevelManagerEnterRva);
	g_originalAuroraUgcCheck = reinterpret_cast<AuroraUgcCheckFn>(g_topKartBase + kAuroraUgcCheckRva);
	g_originalRoadMapProcessorReset = reinterpret_cast<RoadMapProcessorResetFn>(g_topKartBase + kRoadMapProcessorResetRva);
	g_originalLoadMapRes = reinterpret_cast<LoadMapResFn>(g_topKartBase + kLoadMapResRva);
	g_originalLoadMapDesc = reinterpret_cast<LoadMapDescFn>(g_topKartBase + kLoadMapDescRva);
	g_originalLoadSc = reinterpret_cast<LoadScFn>(g_topKartBase + kLoadScRva);
	g_originalApplySceneObjects = reinterpret_cast<ApplySceneObjectsFn>(g_topKartBase + kApplySceneObjectsRva);
	g_originalLoadScCacheFetch = reinterpret_cast<LoadScCacheFetchFn>(g_topKartBase + kLoadScCacheFetchRva);
	g_originalLoadScCacheExtract = reinterpret_cast<LoadScCacheExtractFn>(g_topKartBase + kLoadScCacheExtractRva);
	g_originalNifLoad = reinterpret_cast<NifLoadFn>(g_topKartBase + kNifLoadRva);
	g_originalNifNormalizePath = reinterpret_cast<NifNormalizePathFn>(g_topKartBase + kNifNormalizePathRva);
	g_originalNifFetchResource = reinterpret_cast<NifFetchResourceFn>(g_topKartBase + kNifFetchResourceRva);
	g_originalNifExtractRoot = reinterpret_cast<NifExtractRootFn>(g_topKartBase + kNifExtractRootRva);
	g_originalLuaFieldLookup = reinterpret_cast<LuaFieldLookupFn>(g_topKartBase + kLuaFieldLookupRva);

	g_patchedCalls = PatchMapIdCalls(g_topKartBase);
	InterlockedExchange(&g_installState, 2);

	qqspeed::logging::Logf(
		"topkart_mapid probe installed=%d patchedCalls=%llu base=%p prepareRva=0x%llX levelEnterRva=0x%llX "
		"ugcCheckRva=0x%llX loadScRva=0x%llX nifLoadRva=0x%llX nifNormalizeRva=0x%llX "
		"nifFetchRva=0x%llX nifExtractRva=0x%llX",
		g_patchedCalls > 0 ? 1 : 0,
		static_cast<unsigned long long>(g_patchedCalls),
		reinterpret_cast<void*>(g_topKartBase),
		static_cast<unsigned long long>(kPrepareGameBeginRva),
		static_cast<unsigned long long>(kLevelManagerEnterRva),
		static_cast<unsigned long long>(kAuroraUgcCheckRva),
		static_cast<unsigned long long>(kLoadScRva),
		static_cast<unsigned long long>(kNifLoadRva),
		static_cast<unsigned long long>(kNifNormalizePathRva),
		static_cast<unsigned long long>(kNifFetchResourceRva),
		static_cast<unsigned long long>(kNifExtractRootRva));
	return g_patchedCalls > 0;
}

void StartMapIdProbeThread(const config::HookConfig& config)
{
	if (!config.features.enableTopKartMapIdProbe) {
		qqspeed::logging::Logf("topkart_mapid probe disabled");
		return;
	}

	auto* context = new (std::nothrow) ProbeThreadContext();
	if (!context) {
		qqspeed::logging::Logf("topkart_mapid probe thread create skipped: alloc failed");
		return;
	}
	context->config = config;

	HANDLE thread = CreateThread(nullptr, 0, &MapIdProbeThread, context, 0, nullptr);
	if (!thread) {
		delete context;
		qqspeed::logging::Logf("topkart_mapid probe thread create failed error=%lu", GetLastError());
		return;
	}
	CloseHandle(thread);
}
}
