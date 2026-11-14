#include "pch.h"

#include "topkart/topkart_change_item_probe.h"

#include "logging/hook_log.h"
#include "network/code_patch.h"

#include <intrin.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace
{
constexpr DWORD kTopKartWaitMs = 120000;
constexpr DWORD kTopKartPollMs = 250;
constexpr std::size_t kMaxLoggedItemRecords = 12;

constexpr std::uintptr_t kApplyItemStatusRva = 0x1C6BB0;
constexpr std::uintptr_t kItemLookupRva = 0x20AB50;
constexpr std::uintptr_t kMapEquippedItemRva = 0x237670;
constexpr std::uintptr_t kSkinBaseLookupRva = 0xDD5150;
constexpr std::uintptr_t kBindModelRva = 0xAFBCC0;
constexpr std::uintptr_t kClearModelRva = 0xAFBF00;
constexpr std::uintptr_t kSetActiveKartRva = 0x9DB6F0;
constexpr std::uintptr_t kNotifyChangeKartModelRva = 0x9DD730;
constexpr std::uintptr_t kPrepareActiveKartRva = 0x9D97A0;
constexpr std::uintptr_t kApplyActiveKartRva = 0x9DA2E0;
constexpr std::uintptr_t kCheckDownloadKartRva = 0x232440;
constexpr std::uintptr_t kFileExistsRva = 0x0A4460;
constexpr std::uintptr_t kLoadKartAmRva = 0x9D6E00;
constexpr std::uintptr_t kBuildKartAmRva = 0x210690;
constexpr std::uintptr_t kAmCacheLookupRva = 0x20B1F0;
constexpr std::uintptr_t kCreateKartAmRva = 0x20F9F0;
constexpr std::uintptr_t kResolveAmModelIdRva = 0x24A270;
constexpr std::uintptr_t kFallbackModelIdRva = 0x230A30;
constexpr std::uintptr_t kItemTypeNameRva = 0x20D1C0;
constexpr std::uintptr_t kEncodeEncodedU32ObjectRva = 0x092AA0;

constexpr std::uintptr_t kGlobalItemMgrRva = 0x281A618;
constexpr std::uintptr_t kGlobalAmModelSeedKeyRva = 0x2830D38;
constexpr std::uintptr_t kGlobalAmModelSeedValuePtrRva = 0x2830D40;
constexpr std::uintptr_t kGlobalSkinBaseMgrRva = 0x2AC2EC0;
constexpr std::uintptr_t kGlobalOutdoorSceneRva = 0x2AC2FA0;
constexpr std::uintptr_t kGlobalOutdoorMgrRva = 0x2AC2FA8;

constexpr std::uintptr_t kResponse130OutdoorGateJumpRva = 0x1DF699;
constexpr unsigned char kResponse130OutdoorGateJumpOriginal[] = { 0x0F, 0x84, 0x96, 0x02, 0x00, 0x00 };
constexpr unsigned char kResponse130OutdoorGateJumpNops[] = { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 };

constexpr std::uintptr_t kApplyItemStatusCallRvas[] = {
	0x1DF8E5, // 130 response, qword_18249CF18 path, sorted records
	0x1DF961, // 130 response, no qword_18249CF18 path, sorted records
	0x1E26BD, // 527 notify, first-item-sensitive path
};
constexpr std::uintptr_t kItemLookupCallRvas[] = {
	0x1C6C98, // sub_1801C6BB0: status apply item metadata lookup
	0x1C6E49, // sub_1801C6BB0: status apply item metadata lookup
	0x1C72B1, // sub_1801C6BB0: status apply item metadata lookup
	0x1C7479, // sub_1801C6BB0: status apply item metadata lookup
	0x1C7A1B, // sub_1801C6BB0: status apply item metadata lookup
	0x1C806E, // sub_1801C6BB0: status apply item metadata lookup
	0x1C8C2D, // sub_1801C6BB0: status apply item metadata lookup
	0x1DF6B6, // 130 early branch: sorted record ItemID metadata lookup
	0x1DF832, // 130 early branch: mapped/base ItemID metadata lookup
	0x1DFA39, // 130 late aux branch: original record ItemID metadata lookup
	0x1DFB57, // 130 late aux branch: included item metadata lookup
	0x1E1C23, // 527 notify branch: item metadata lookup
	0x1E226F, // 527 notify branch: item metadata lookup
	0x1E22BE, // 527 notify branch: item metadata lookup
	0x1E270A, // 527 notify branch: item metadata lookup
	0x1E2804, // 527 notify branch: item metadata lookup
	0x1E28B4, // 527 notify branch: item metadata lookup
	0x1E2A33, // 527 notify branch: item metadata lookup
};
constexpr std::uintptr_t kMapEquippedItemCallRvas[] = {
	0x1DFE33, // 130 late branch: original-order record ItemID -> current equipped ID
};
constexpr std::uintptr_t kSkinBaseLookupCallRvas[] = {
	0x10F373, // display/player-info chain: current display kart -> runtime skin/base record
	0x1DF817, // 130 early branch: record ItemID -> base/skin model candidate
	0x1E000D, // 130 late branch: mapped current ID -> base/skin data
	0x1E25F8, // 527 first-item branch: first ItemID -> base/skin data
	0x9D67A8, // actor/model update chain: kart -> skin/base record
	0x9DA537, // SetActiveKart/update chain: kart -> skin/base record
	0x9ED832, // actor/model helper: kart -> skin/base record
	0x9ED923, // actor/model helper: kart -> skin/base record
	0xA068FC, // actor/model helper: kart -> skin/base record
	0xA06DC9, // actor/model helper: kart -> skin/base record
	0xDDC1D8, // shape/refit model refresh path
	0xDDC2AE, // shape/refit model refresh path
	0xDDD5E2, // shape/refit manager path
	0xDDF214, // OnResponseShapeRefit path
	0xDE097C, // shape/refit config consumer path
	0xDE0C05, // shape/refit config consumer path
	0xDE13E7, // shape/refit config consumer path
	0x125DDD1, // late display/model path
};
constexpr std::uintptr_t kBindModelCallRvas[] = {
	0x1DF84B, // 130 early branch: bind selected equipped record/base ID
	0x1E00A7, // 130 late branch: matched original record had NewStatus == 1
	0x1E22FB, // 527 notify branch: bind model
	0xB08C3A, // actor/model path: bind model
	0xBBF7D9, // display/model path: bind model
	0xBC5490, // display/model path: bind model
	0xDE0D15, // shape/refit config consumer path: bind model
	0x125DE13, // late display/model path: bind model
};
constexpr std::uintptr_t kClearModelCallRvas[] = {
	0x1DF861, // 130 early branch: no model candidate after equipped scan
	0x1E00C0, // 130 late branch: matched original record had NewStatus != 1
	0x1E22D4, // 527 notify branch: clear model
	0x1E8AF0, // change/status helper: clear model
	0xDE0CD6, // shape/refit config consumer path: clear model
	0x125DE85, // late display/model path: clear model
};
constexpr std::uintptr_t kSetActiveKartCallRvas[] = {
	0x1E0052, // 130 late branch: SetActiveKart(currentID)
	0x1E8ABC, // change/status helper: SetActiveKart
	0x9D37E2, // actor/model update path: SetActiveKart
	0x9DDA06, // NotifyChangeKartModel sync path: SetActiveKart
};
constexpr std::uintptr_t kNotifyChangeKartModelCallRvas[] = {
	0x1E2644, // 527 first-item branch: observer model refresh
	0x94EF4C, // observer/update path: NotifyChangeKartModel
	0x94F1D6, // observer/update path: NotifyChangeKartModel
	0x9DDE30, // actor/model wrapper path: NotifyChangeKartModel
	0xDDC304, // shape/refit model refresh path: NotifyChangeKartModel
	0xDE0C82, // shape/refit config consumer path: NotifyChangeKartModel
	0xDE1422, // shape/refit config consumer path: NotifyChangeKartModel
	0x116CF70, // late observer/display path: NotifyChangeKartModel
};
constexpr std::uintptr_t kPrepareActiveKartCallRvas[] = {
	0x9DBA6A, // SetActiveKart sync path: prepare selected kart/model ids
};
constexpr std::uintptr_t kApplyActiveKartCallRvas[] = {
	0x9DBC7D, // SetActiveKart sync path: apply selected kart/model ids
};
constexpr std::uintptr_t kCheckDownloadKartCallRvas[] = {
	0x9D6474, // SetActiveKart: check requested kart model resource
	0x9D6821, // SetActiveKart: check mapped dressing-up kart resource
	0x9D6852, // SetActiveKart: retry/check mapped dressing-up kart resource
	0x9D68C0, // SetActiveKart: verify requested kart before fallback
};
constexpr std::uintptr_t kFileExistsCallRvas[] = {
	0x23268C, // CheckDownloadKart: final data/Avatar/...kfm existence check
	0x20FF7C, // GetItemActMgr: actual data/Avatar/...kfm AM load existence check
};
constexpr std::uintptr_t kLoadKartAmCallRvas[] = {
	0x9D9E2D, // SetActiveKart: load active kart animation/model object
};
constexpr std::uintptr_t kBuildKartAmCallRvas[] = {
	0x9D6E60, // CNxPlayer::LoadKartAM -> CItemDataMgr::GetItemActMgr
};
constexpr std::uintptr_t kAmCacheLookupCallRvas[] = {
	0x210762, // GetItemActMgr: lookup cached AM for uin + item id
};
constexpr std::uintptr_t kCreateKartAmCallRvas[] = {
	0x210988, // GetItemActMgr: build AM when cache is absent/unusable
};
constexpr std::uintptr_t kResolveAmModelIdCallRvas[] = {
	0x20FC31, // BuildItemActMgr: derive the model id used in Avatar/%s/%u/%s.kfm
};
constexpr std::uintptr_t kFallbackModelIdCallRvas[] = {
	0x2100E0, // BuildItemActMgr: resolve fallback model id after direct path miss
};

using ApplyItemStatusFn = __int64(__fastcall*)(
	std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
	std::uintptr_t, std::uintptr_t, std::uintptr_t, std::uintptr_t,
	std::uintptr_t, std::uintptr_t, std::uintptr_t);
using ItemLookupFn = std::uintptr_t(__fastcall*)(void* itemMgr, unsigned int itemId);
using MapEquippedItemFn = unsigned int(__fastcall*)(void* itemMgr, unsigned int itemId);
using SkinBaseLookupFn = std::uintptr_t(__fastcall*)(void* mgr, unsigned int itemId);
using BindModelFn = void(__fastcall*)(void* actor, unsigned int modelId);
using ClearModelFn = __int64(__fastcall*)(void* actor);
using SetActiveKartFn = __int64(__fastcall*)(void* actor, unsigned int kartId, unsigned char flag);
using NotifyChangeKartModelFn = __int64(__fastcall*)(void* actor, int kartId);
using PrepareActiveKartFn = __int64(__fastcall*)(
	void* actor, unsigned int kartId, unsigned int* outModelKind, unsigned int* outSelectedId, void* flagPtr);
using ApplyActiveKartFn = __int64(__fastcall*)(
	void* actor, void* modelRef, unsigned int selectedId, int modelKind, unsigned char flag);
using CheckDownloadKartFn = __int64(__fastcall*)(void* itemMgr, unsigned int itemId);
using FileExistsFn = unsigned char(__fastcall*)(const char* path);
using LoadKartAmFn = __int64(__fastcall*)(void* actor, int uin, int kartId, void* desc, int a5);
using BuildKartAmFn = void* (__fastcall*)(void* itemMgr, void* outAm, unsigned int uin, unsigned int itemId, void* desc, unsigned char clone, int roleId);
using AmCacheLookupFn = std::uintptr_t(__fastcall*)(void* itemMgr, unsigned int uin, unsigned int itemId);
using CreateKartAmFn = void* (__fastcall*)(void* itemMgr, void* outAm, std::uintptr_t item, void* desc, int uin, unsigned int roleId);
using ResolveAmModelIdFn = void* (__fastcall*)(void* outEncoded, std::uintptr_t item, void* encodedItemId);
using FallbackModelIdFn = unsigned int(__fastcall*)(void* itemMgr, unsigned int itemType, unsigned char itemFlag80, unsigned char itemFlag616);
using ItemTypeNameFn = const char*(__fastcall*)(void* itemMgr, unsigned int itemType);
using EncodeEncodedU32ObjectFn = void(__fastcall*)(void* encodedObject, void* plainValuePtr);

std::uintptr_t g_topKartBase = 0;
volatile LONG g_installState = 0;
std::size_t g_patchedCalls = 0;
bool g_force130GateOpenPatched = false;
bool g_forceAmModelId = false;
bool g_clearAmGlobalSeed = false;
bool g_verboseProbeLogs = false;

ApplyItemStatusFn g_originalApplyItemStatus = nullptr;
ItemLookupFn g_originalItemLookup = nullptr;
MapEquippedItemFn g_originalMapEquippedItem = nullptr;
SkinBaseLookupFn g_originalSkinBaseLookup = nullptr;
BindModelFn g_originalBindModel = nullptr;
ClearModelFn g_originalClearModel = nullptr;
SetActiveKartFn g_originalSetActiveKart = nullptr;
NotifyChangeKartModelFn g_originalNotifyChangeKartModel = nullptr;
PrepareActiveKartFn g_originalPrepareActiveKart = nullptr;
ApplyActiveKartFn g_originalApplyActiveKart = nullptr;
CheckDownloadKartFn g_originalCheckDownloadKart = nullptr;
FileExistsFn g_originalFileExists = nullptr;
LoadKartAmFn g_originalLoadKartAm = nullptr;
BuildKartAmFn g_originalBuildKartAm = nullptr;
AmCacheLookupFn g_originalAmCacheLookup = nullptr;
CreateKartAmFn g_originalCreateKartAm = nullptr;
ResolveAmModelIdFn g_originalResolveAmModelId = nullptr;
FallbackModelIdFn g_originalFallbackModelId = nullptr;

struct ProbeThreadContext
{
	qqspeed::config::HookConfig config = {};
};

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

bool PatchResponse130OutdoorGate(std::uintptr_t topKartBase)
{
	if (!topKartBase) {
		return false;
	}

	unsigned char* jump = reinterpret_cast<unsigned char*>(topKartBase + kResponse130OutdoorGateJumpRva);
	unsigned char original[sizeof(kResponse130OutdoorGateJumpOriginal)] = {};
	if (!qqspeed::network::patch::SafeCopyRaw(original, jump, sizeof(original))) {
		qqspeed::logging::Logf(
			"topkart_change_item force130_gate_open read failed rva=0x%llX address=%p",
			static_cast<unsigned long long>(kResponse130OutdoorGateJumpRva),
			jump);
		return false;
	}

	if (memcmp(original, kResponse130OutdoorGateJumpNops, sizeof(original)) == 0) {
		qqspeed::logging::Logf(
			"topkart_change_item force130_gate_open already patched rva=0x%llX address=%p",
			static_cast<unsigned long long>(kResponse130OutdoorGateJumpRva),
			jump);
		return true;
	}

	if (memcmp(original, kResponse130OutdoorGateJumpOriginal, sizeof(original)) != 0) {
		qqspeed::logging::Logf(
			"topkart_change_item force130_gate_open skipped: unexpected bytes rva=0x%llX address=%p bytes=%02X %02X %02X %02X %02X %02X",
			static_cast<unsigned long long>(kResponse130OutdoorGateJumpRva),
			jump,
			original[0],
			original[1],
			original[2],
			original[3],
			original[4],
			original[5]);
		return false;
	}

	DWORD oldProtect = 0;
	if (!VirtualProtect(jump, sizeof(kResponse130OutdoorGateJumpNops), PAGE_EXECUTE_READWRITE, &oldProtect)) {
		qqspeed::logging::Logf(
			"topkart_change_item force130_gate_open protect failed rva=0x%llX address=%p error=%lu",
			static_cast<unsigned long long>(kResponse130OutdoorGateJumpRva),
			jump,
			GetLastError());
		return false;
	}

	memcpy(jump, kResponse130OutdoorGateJumpNops, sizeof(kResponse130OutdoorGateJumpNops));
	DWORD ignoredProtect = 0;
	VirtualProtect(jump, sizeof(kResponse130OutdoorGateJumpNops), oldProtect, &ignoredProtect);
	FlushInstructionCache(GetCurrentProcess(), jump, sizeof(kResponse130OutdoorGateJumpNops));

	qqspeed::logging::Logf(
		"topkart_change_item force130_gate_open patched rva=0x%llX address=%p original=%02X %02X %02X %02X %02X %02X patch=90 90 90 90 90 90",
		static_cast<unsigned long long>(kResponse130OutdoorGateJumpRva),
		jump,
		original[0],
		original[1],
		original[2],
		original[3],
		original[4],
		original[5]);
	return true;
}

const char* CallSiteName(std::uintptr_t callRva)
{
	switch (callRva) {
	case 0x1DF8E5:
		return "130_apply_sorted_qword";
	case 0x1DF961:
		return "130_apply_sorted_noqword";
	case 0x1E26BD:
		return "527_apply_notify";
	case 0x1C6C98:
		return "apply_item_lookup_1";
	case 0x1C6E49:
		return "apply_item_lookup_2";
	case 0x1C72B1:
		return "apply_item_lookup_3";
	case 0x1C7479:
		return "apply_item_lookup_4";
	case 0x1C7A1B:
		return "apply_item_lookup_5";
	case 0x1C806E:
		return "apply_item_lookup_6";
	case 0x1C8C2D:
		return "apply_item_lookup_7";
	case 0x1DF6B6:
		return "130_early_item_lookup";
	case 0x1DF832:
		return "130_early_base_item_lookup";
	case 0x1DFA39:
		return "130_late_aux_item_lookup";
	case 0x1DFB57:
		return "130_late_aux_include_lookup";
	case 0x1E1C23:
		return "527_item_lookup_1";
	case 0x1E226F:
		return "527_item_lookup_2";
	case 0x1E22BE:
		return "527_item_lookup_3";
	case 0x1E270A:
		return "527_item_lookup_4";
	case 0x1E2804:
		return "527_item_lookup_5";
	case 0x1E28B4:
		return "527_item_lookup_6";
	case 0x1E2A33:
		return "527_item_lookup_7";
	case 0x1DFE33:
		return "130_late_map_original";
	case 0x10F373:
		return "display_skinbase";
	case 0x1DF817:
		return "130_early_skinbase";
	case 0x1E000D:
		return "130_late_skinbase";
	case 0x1E25F8:
		return "527_first_skinbase";
	case 0x9D67A8:
		return "actor_update_skinbase_1";
	case 0x9DA537:
		return "set_active_skinbase";
	case 0x9ED832:
		return "actor_model_skinbase_1";
	case 0x9ED923:
		return "actor_model_skinbase_2";
	case 0xA068FC:
		return "actor_model_skinbase_3";
	case 0xA06DC9:
		return "actor_model_skinbase_4";
	case 0xDDC1D8:
		return "shape_refresh_skinbase_1";
	case 0xDDC2AE:
		return "shape_refresh_skinbase_2";
	case 0xDDD5E2:
		return "shape_mgr_skinbase";
	case 0xDDF214:
		return "shape_response_skinbase";
	case 0xDE097C:
		return "shape_cfg_skinbase_1";
	case 0xDE0C05:
		return "shape_cfg_skinbase_2";
	case 0xDE13E7:
		return "shape_cfg_notify_skinbase";
	case 0x125DDD1:
		return "late_display_skinbase";
	case 0x1DF84B:
		return "130_early_bind";
	case 0x1E00A7:
		return "130_late_bind_status1";
	case 0x1E22FB:
		return "527_bind";
	case 0xB08C3A:
		return "actor_model_bind";
	case 0xBBF7D9:
		return "display_model_bind_1";
	case 0xBC5490:
		return "display_model_bind_2";
	case 0xDE0D15:
		return "shape_cfg_bind";
	case 0x125DE13:
		return "late_display_bind";
	case 0x1DF861:
		return "130_early_clear";
	case 0x1E00C0:
		return "130_late_clear_status0";
	case 0x1E22D4:
		return "527_clear";
	case 0x1E8AF0:
		return "change_status_clear";
	case 0xDE0CD6:
		return "shape_cfg_clear";
	case 0x125DE85:
		return "late_display_clear";
	case 0x1E0052:
		return "130_late_set_active";
	case 0x1E8ABC:
		return "change_status_set_active";
	case 0x9D37E2:
		return "actor_update_set_active";
	case 0x9DDA06:
		return "notify_sync_set_active";
	case 0x1E2644:
		return "527_first_change_model";
	case 0x94EF4C:
		return "observer_change_model_1";
	case 0x94F1D6:
		return "observer_change_model_2";
	case 0x9DDE30:
		return "actor_wrapper_change_model";
	case 0xDDC304:
		return "shape_refresh_change_model";
	case 0xDE0C82:
		return "shape_cfg_change_model";
	case 0xDE1422:
		return "shape_cfg_notify_change_model";
	case 0x116CF70:
		return "late_observer_change_model";
	case 0x9DBA6A:
		return "set_active_prepare";
	case 0x9DBC7D:
		return "set_active_apply";
	case 0x9D6474:
		return "set_active_check_requested";
	case 0x9D6821:
		return "set_active_check_mapped_1";
	case 0x9D6852:
		return "set_active_check_mapped_2";
	case 0x9D68C0:
		return "set_active_check_requested_after_mapped_fail";
	case 0x23268C:
		return "check_download_file_exists";
	case 0x20FF7C:
		return "am_builder_file_exists";
	case 0x9D9E2D:
		return "set_active_load_kart_am";
	case 0x9D6E60:
		return "load_kart_am_build";
	case 0x210762:
		return "am_cache_lookup";
	case 0x210988:
		return "am_builder_create";
	case 0x20FC31:
		return "am_builder_derive_model";
	case 0x2100E0:
		return "am_builder_fallback_model";
	default:
		return "unknown";
	}
}

unsigned int ReadU32Safe(const void* ptr)
{
	unsigned int value = 0;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned short ReadU16Safe(const void* ptr)
{
	unsigned short value = 0;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned char ReadU8Safe(const void* ptr)
{
	unsigned char value = 0;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

std::uintptr_t ReadPtrSafe(const void* ptr)
{
	std::uintptr_t value = 0;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

unsigned int ReadNestedU32Safe(const void* ptrToPtr)
{
	const std::uintptr_t ptr = ReadPtrSafe(ptrToPtr);
	return ptr ? ReadU32Safe(reinterpret_cast<const void*>(ptr)) : 0;
}

bool WriteU32Safe(void* ptr, unsigned int value)
{
	if (!ptr) {
		return false;
	}
	__try
	{
		*static_cast<unsigned int*>(ptr) = value;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

unsigned int DecodeEncodedU32Raw(unsigned int key, unsigned int raw)
{
	return (((key >> 0) & 0xFFu) ^ ((raw >> 8) & 0xFFu))
		| ((((key >> 8) & 0xFFu) ^ ((raw >> 16) & 0xFFu)) << 8)
		| ((((key >> 16) & 0xFFu) ^ ((raw >> 24) & 0xFFu)) << 16)
		| ((((raw >> 0) & 0xFFu) ^ ((key >> 24) & 0xFFu)) << 24);
}

unsigned int EncodeEncodedU32Raw(unsigned int key, unsigned int value)
{
	const unsigned int raw0 = ((key >> 24) & 0xFFu) ^ ((value >> 24) & 0xFFu);
	const unsigned int raw1 = ((key >> 0) & 0xFFu) ^ ((value >> 0) & 0xFFu);
	const unsigned int raw2 = ((key >> 8) & 0xFFu) ^ ((value >> 8) & 0xFFu);
	const unsigned int raw3 = ((key >> 16) & 0xFFu) ^ ((value >> 16) & 0xFFu);
	return raw0 | (raw1 << 8) | (raw2 << 16) | (raw3 << 24);
}

bool DecodeEncodedU32Object(std::uintptr_t object, unsigned int* outValue, unsigned int* outRaw = nullptr, std::uintptr_t* outValuePtr = nullptr)
{
	if (!object || !outValue) {
		return false;
	}
	const std::uintptr_t valuePtr = ReadPtrSafe(reinterpret_cast<const void*>(object + 8));
	if (outValuePtr) {
		*outValuePtr = valuePtr;
	}
	if (!valuePtr) {
		*outValue = 0;
		if (outRaw) {
			*outRaw = 0;
		}
		return false;
	}

	const unsigned int key = ReadU32Safe(reinterpret_cast<const void*>(object));
	const unsigned int raw = ReadU32Safe(reinterpret_cast<const void*>(valuePtr));
	if (outRaw) {
		*outRaw = raw;
	}

	*outValue = DecodeEncodedU32Raw(key, raw);
	return true;
}

unsigned int ActorUin(void* actor)
{
	return actor ? ReadU32Safe(static_cast<const unsigned char*>(actor) + 27608) : 0;
}

unsigned int ActorCurrentKartId(void* actor)
{
	return actor ? ReadNestedU32Safe(static_cast<const unsigned char*>(actor) + 31760) : 0;
}

unsigned int ActorPreviousKartId(void* actor)
{
	return actor ? ReadNestedU32Safe(static_cast<const unsigned char*>(actor) + 31744) : 0;
}

unsigned int ActorModelId(void* actor)
{
	return actor ? ReadNestedU32Safe(static_cast<const unsigned char*>(actor) + 888) : 0;
}

std::uintptr_t ActorModelPtr(void* actor)
{
	return actor ? ReadPtrSafe(static_cast<const unsigned char*>(actor) + 544) : 0;
}

std::uintptr_t ActorModelInnerPtr(void* actor)
{
	const std::uintptr_t model = ActorModelPtr(actor);
	return model ? ReadPtrSafe(reinterpret_cast<const void*>(model + 24)) : 0;
}

std::uintptr_t ReadTopKartGlobalPtr(std::uintptr_t rva)
{
	return g_topKartBase ? ReadPtrSafe(reinterpret_cast<const void*>(g_topKartBase + rva)) : 0;
}

unsigned int ItemIdFromItemPtr(std::uintptr_t item)
{
	unsigned int itemId = 0;
	return DecodeEncodedU32Object(item, &itemId) ? itemId : 0;
}

bool IsWatchedKartItem(unsigned int itemId)
{
	return itemId == 10020 || itemId == 29370 || itemId == 109563 || itemId == 129099 || itemId == 163037 || itemId == 163041;
}

bool IsVehicleClassItem(unsigned int itemType)
{
	return (itemType & 0xFF00u) == 0x600u;
}

bool ShouldUseExplicitVehicleAmModelId(unsigned int itemId, unsigned int itemType)
{
	return itemId != 0 && IsVehicleClassItem(itemType);
}

bool IsWatchedAvatarPath(const char* path)
{
	if (!path) {
		return false;
	}
	return strstr(path, "Avatar/") != nullptr
		&& (strstr(path, "10020") != nullptr
			|| strstr(path, "29370") != nullptr
			|| strstr(path, "109563") != nullptr
			|| strstr(path, "129099") != nullptr
			|| strstr(path, "163037") != nullptr
			|| strstr(path, "163041") != nullptr);
}

void BuildCheckDownloadKartPath(void* itemMgr, unsigned int itemId, char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}
	out[0] = '\0';
	if (!itemMgr || !itemId) {
		return;
	}

	ItemLookupFn lookup = g_originalItemLookup;
	if (!lookup && g_topKartBase) {
		lookup = reinterpret_cast<ItemLookupFn>(g_topKartBase + kItemLookupRva);
	}
	ItemTypeNameFn typeName = g_topKartBase
		? reinterpret_cast<ItemTypeNameFn>(g_topKartBase + kItemTypeNameRva)
		: nullptr;
	const std::uintptr_t item = lookup ? lookup(itemMgr, itemId) : 0;
	if (!item || !typeName) {
		return;
	}

	const unsigned int itemType = ReadU32Safe(reinterpret_cast<const void*>(item + 32));
	const char* folder = typeName(itemMgr, itemType);
	const char* model = "model/one";
	if ((itemType & 0xFF00u) == 0x600u) {
		model = "model/car";
	} else if ((itemType & 0xFF00u) != 0x500u) {
		model = folder;
	}
	if (!folder || !*folder || !model || !*model) {
		return;
	}
	sprintf_s(out, outSize, "data/Avatar/%s/%u/%s.kfm", folder, itemId, model);
}

bool IsChangeItemHandlerLookup(std::uintptr_t callRva)
{
	return callRva >= 0x17A000 && callRva < 0x17E000;
}

void FormatItemStatusRecords(const unsigned char* records, unsigned int count, char* out, std::size_t outSize)
{
	if (!out || outSize == 0) {
		return;
	}
	out[0] = '\0';
	if (!records || count == 0) {
		strncpy_s(out, outSize, "[]", _TRUNCATE);
		return;
	}

	strncat_s(out, outSize, "[", _TRUNCATE);
	const unsigned int logged = count < kMaxLoggedItemRecords ? count : static_cast<unsigned int>(kMaxLoggedItemRecords);
	for (unsigned int i = 0; i < logged; ++i) {
		const unsigned char* record = records + static_cast<std::size_t>(i) * 5;
		const unsigned int itemId = ReadU32Safe(record);
		const unsigned int status = ReadU8Safe(record + 4);
		char item[48] = {};
		sprintf_s(item, "%s%u:%u", i ? "," : "", itemId, status);
		strncat_s(out, outSize, item, _TRUNCATE);
	}
	if (count > logged) {
		char suffix[48] = {};
		sprintf_s(suffix, ",...+%u", count - logged);
		strncat_s(out, outSize, suffix, _TRUNCATE);
	}
	strncat_s(out, outSize, "]", _TRUNCATE);
}

void LogApplyContext(std::uintptr_t callRva)
{
	const std::uintptr_t outdoorScene = ReadTopKartGlobalPtr(kGlobalOutdoorSceneRva);
	const std::uintptr_t outdoorMgr = ReadTopKartGlobalPtr(kGlobalOutdoorMgrRva);
	const std::uintptr_t itemMgr = ReadTopKartGlobalPtr(kGlobalItemMgrRva);
	const std::uintptr_t skinBaseMgr = ReadTopKartGlobalPtr(kGlobalSkinBaseMgrRva);
	const std::uintptr_t actor = outdoorMgr ? ReadPtrSafe(reinterpret_cast<const void*>(outdoorMgr + 0x10)) : 0;
	qqspeed::logging::Logf(
		"topkart_change_item apply_context callRva=0x%llX site=%s qword18249CF18=%p qword18249CF20=%p qword18249CE48=%p itemMgr=%p actor=%p actorUin=%u actorCurrent=%u actorModel=%u",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		reinterpret_cast<void*>(outdoorScene),
		reinterpret_cast<void*>(outdoorMgr),
		reinterpret_cast<void*>(skinBaseMgr),
		reinterpret_cast<void*>(itemMgr),
		reinterpret_cast<void*>(actor),
		ActorUin(reinterpret_cast<void*>(actor)),
		ActorCurrentKartId(reinterpret_cast<void*>(actor)),
		ActorModelId(reinterpret_cast<void*>(actor)));
}

void LogEarlyBranchCandidates(std::uintptr_t callRva, const unsigned char* records, unsigned int count)
{
	ItemLookupFn lookup = g_originalItemLookup;
	if (!lookup && g_topKartBase) {
		lookup = reinterpret_cast<ItemLookupFn>(g_topKartBase + kItemLookupRva);
	}
	const std::uintptr_t itemMgr = ReadTopKartGlobalPtr(kGlobalItemMgrRva);
	if (!lookup || !itemMgr || !records || count == 0) {
		return;
	}
	SkinBaseLookupFn skinBaseLookup = g_originalSkinBaseLookup;
	if (!skinBaseLookup && g_topKartBase) {
		skinBaseLookup = reinterpret_cast<SkinBaseLookupFn>(g_topKartBase + kSkinBaseLookupRva);
	}
	const std::uintptr_t skinBaseMgr = ReadTopKartGlobalPtr(kGlobalSkinBaseMgrRva);

	const unsigned int logged = count < kMaxLoggedItemRecords ? count : static_cast<unsigned int>(kMaxLoggedItemRecords);
	for (unsigned int i = 0; i < logged; ++i) {
		const unsigned char* record = records + static_cast<std::size_t>(i) * 5;
		const unsigned int itemId = ReadU32Safe(record);
		const unsigned int status = ReadU8Safe(record + 4);
		const std::uintptr_t item = lookup(reinterpret_cast<void*>(itemMgr), itemId);
		const unsigned int itemType = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 32)) : 0;
		const unsigned int itemType2nd = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 36)) : 0;
		const bool carClass = (itemType & 0xFF00u) == 0x600u;
		const bool specialModelClass = itemType == 0x12Fu;
		const bool earlyWouldSelect = status == 1 && (carClass || specialModelClass);
		const bool directBind = earlyWouldSelect && itemType2nd == 0x12Bu;
		const bool needsSkinBase = earlyWouldSelect && !directBind;
		std::uintptr_t skinBaseResult = 0;
		unsigned int mappedBase = 0;
		std::uintptr_t baseItem = 0;
		unsigned int baseItemType = 0;
		unsigned int baseItemType2nd = 0;
		if (needsSkinBase && skinBaseLookup && skinBaseMgr) {
			skinBaseResult = skinBaseLookup(reinterpret_cast<void*>(skinBaseMgr), itemId);
			mappedBase = skinBaseResult ? ReadU32Safe(reinterpret_cast<const void*>(skinBaseResult + 68)) : 0;
			if (mappedBase) {
				baseItem = lookup(reinterpret_cast<void*>(itemMgr), mappedBase);
				baseItemType = baseItem ? ReadU32Safe(reinterpret_cast<const void*>(baseItem + 32)) : 0;
				baseItemType2nd = baseItem ? ReadU32Safe(reinterpret_cast<const void*>(baseItem + 36)) : 0;
			}
		}
		if (IsWatchedKartItem(itemId) || earlyWouldSelect || carClass || specialModelClass) {
			qqspeed::logging::Logf(
				"topkart_change_item early_candidate callRva=0x%llX site=%s index=%u itemId=%u status=%u item=%p itemType=%u/0x%X itemType2nd=%u/0x%X carClass=%u specialClass=%u earlyWouldSelect=%u directBind=%u needsSkinBase=%u skinBaseMgr=%p skinBaseResult=%p mappedBase=%u baseItem=%p baseItemType=%u/0x%X baseItemType2nd=%u/0x%X baseBindable=%u",
				static_cast<unsigned long long>(callRva),
				CallSiteName(callRva),
				i,
				itemId,
				status,
				reinterpret_cast<void*>(item),
				itemType,
				itemType,
				itemType2nd,
				itemType2nd,
				carClass ? 1 : 0,
				specialModelClass ? 1 : 0,
				earlyWouldSelect ? 1 : 0,
				directBind ? 1 : 0,
				needsSkinBase ? 1 : 0,
				reinterpret_cast<void*>(skinBaseMgr),
				reinterpret_cast<void*>(skinBaseResult),
				mappedBase,
				reinterpret_cast<void*>(baseItem),
				baseItemType,
				baseItemType,
				baseItemType2nd,
				baseItemType2nd,
				baseItemType2nd == 0x12Bu ? 1 : 0);
		}
	}
}

void LogApplyPacket(std::uintptr_t packet)
{
	const std::uintptr_t callRva = CurrentCallRva();
	const unsigned int owner = ReadU32Safe(reinterpret_cast<const void*>(packet));
	const unsigned int tick = ReadU32Safe(reinterpret_cast<const void*>(packet + 4));
	const unsigned int itemNum = ReadU16Safe(reinterpret_cast<const void*>(packet + 8));
	char records[768] = {};
	FormatItemStatusRecords(reinterpret_cast<const unsigned char*>(packet + 10), itemNum, records, sizeof(records));
	qqspeed::logging::Logf(
		"topkart_change_item apply callRva=0x%llX site=%s packet=%p owner=%u tick=%u itemNum=%u records=%s",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		reinterpret_cast<void*>(packet),
		owner,
		tick,
		itemNum,
		records);
	LogApplyContext(callRva);
	if (itemNum > 0 && itemNum <= 200) {
		LogEarlyBranchCandidates(callRva, reinterpret_cast<const unsigned char*>(packet + 10), itemNum);
	}
}

__int64 __fastcall HookedApplyItemStatus(
	std::uintptr_t a1,
	std::uintptr_t packet,
	std::uintptr_t a3,
	std::uintptr_t a4,
	std::uintptr_t a5,
	std::uintptr_t a6,
	std::uintptr_t a7,
	std::uintptr_t a8,
	std::uintptr_t a9,
	std::uintptr_t a10,
	std::uintptr_t a11)
{
	LogApplyPacket(packet);
	ApplyItemStatusFn original = g_originalApplyItemStatus;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<ApplyItemStatusFn>(g_topKartBase + kApplyItemStatusRva);
	}
	return original ? original(a1, packet, a3, a4, a5, a6, a7, a8, a9, a10, a11) : 0;
}

std::uintptr_t __fastcall HookedItemLookup(void* itemMgr, unsigned int itemId)
{
	ItemLookupFn original = g_originalItemLookup;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<ItemLookupFn>(g_topKartBase + kItemLookupRva);
	}
	const std::uintptr_t result = original ? original(itemMgr, itemId) : 0;
	const std::uintptr_t callRva = CurrentCallRva();
	if (IsWatchedKartItem(itemId) || IsChangeItemHandlerLookup(callRva)) {
		const unsigned int itemType = result ? ReadU32Safe(reinterpret_cast<const void*>(result + 32)) : 0;
		const unsigned int itemType2nd = result ? ReadU32Safe(reinterpret_cast<const void*>(result + 36)) : 0;
		qqspeed::logging::Logf(
			"topkart_change_item item_lookup callRva=0x%llX site=%s itemId=%u result=%p itemType=%u/0x%X itemType2nd=%u/0x%X itemMgr=%p",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			itemId,
			reinterpret_cast<void*>(result),
			itemType,
			itemType,
			itemType2nd,
			itemType2nd,
			itemMgr);
	}
	return result;
}

unsigned int __fastcall HookedMapEquippedItem(void* itemMgr, unsigned int itemId)
{
	MapEquippedItemFn original = g_originalMapEquippedItem;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<MapEquippedItemFn>(g_topKartBase + kMapEquippedItemRva);
	}
	const unsigned int mapped = original ? original(itemMgr, itemId) : 0;
	const std::uintptr_t callRva = CurrentCallRva();
	qqspeed::logging::Logf(
		"topkart_change_item map_equipped callRva=0x%llX site=%s itemId=%u mapped=%u itemMgr=%p",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		itemId,
		mapped,
		itemMgr);
	return mapped;
}

std::uintptr_t __fastcall HookedSkinBaseLookup(void* mgr, unsigned int itemId)
{
	SkinBaseLookupFn original = g_originalSkinBaseLookup;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<SkinBaseLookupFn>(g_topKartBase + kSkinBaseLookupRva);
	}
	const std::uintptr_t result = original ? original(mgr, itemId) : 0;
	const unsigned int mappedBase = result ? ReadU32Safe(reinterpret_cast<const void*>(result + 68)) : 0;
	ItemLookupFn lookup = g_originalItemLookup;
	if (!lookup && g_topKartBase) {
		lookup = reinterpret_cast<ItemLookupFn>(g_topKartBase + kItemLookupRva);
	}
	const std::uintptr_t itemMgr = ReadTopKartGlobalPtr(kGlobalItemMgrRva);
	const std::uintptr_t item = (lookup && itemMgr)
		? lookup(reinterpret_cast<void*>(itemMgr), itemId)
		: 0;
	const unsigned int itemType = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 32)) : 0;
	const unsigned int itemType2nd = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 36)) : 0;
	const std::uintptr_t baseItem = (lookup && itemMgr && mappedBase)
		? lookup(reinterpret_cast<void*>(itemMgr), mappedBase)
		: 0;
	const unsigned int baseItemType = baseItem ? ReadU32Safe(reinterpret_cast<const void*>(baseItem + 32)) : 0;
	const unsigned int baseItemType2nd = baseItem ? ReadU32Safe(reinterpret_cast<const void*>(baseItem + 36)) : 0;
	const std::uintptr_t callRva = CurrentCallRva();
	qqspeed::logging::Logf(
		"topkart_change_item skinbase_lookup callRva=0x%llX site=%s itemId=%u item=%p itemType=%u/0x%X itemType2nd=%u/0x%X result=%p mappedBase=%u mgr=%p baseItem=%p baseItemType=%u/0x%X baseItemType2nd=%u/0x%X baseBindable=%u",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		itemId,
		reinterpret_cast<void*>(item),
		itemType,
		itemType,
		itemType2nd,
		itemType2nd,
		reinterpret_cast<void*>(result),
		mappedBase,
		mgr,
		reinterpret_cast<void*>(baseItem),
		baseItemType,
		baseItemType,
		baseItemType2nd,
		baseItemType2nd,
		baseItemType2nd == 0x12Bu ? 1 : 0);
	return result;
}

void __fastcall HookedBindModel(void* actor, unsigned int modelId)
{
	const std::uintptr_t callRva = CurrentCallRva();
	const unsigned int uin = ActorUin(actor);
	const unsigned int currentBefore = ActorCurrentKartId(actor);
	const unsigned int modelBefore = ActorModelId(actor);
	qqspeed::logging::Logf(
		"topkart_change_item bind_enter callRva=0x%llX site=%s actor=%p uin=%u currentBefore=%u modelBefore=%u bindModelId=%u",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		uin,
		currentBefore,
		modelBefore,
		modelId);

	BindModelFn original = g_originalBindModel;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<BindModelFn>(g_topKartBase + kBindModelRva);
	}
	if (original) {
		original(actor, modelId);
	}

	qqspeed::logging::Logf(
		"topkart_change_item bind_leave callRva=0x%llX site=%s actor=%p uin=%u currentAfter=%u modelAfter=%u bindModelId=%u",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		uin,
		ActorCurrentKartId(actor),
		ActorModelId(actor),
		modelId);
}

__int64 __fastcall HookedClearModel(void* actor)
{
	const std::uintptr_t callRva = CurrentCallRva();
	const unsigned int uin = ActorUin(actor);
	const unsigned int currentBefore = ActorCurrentKartId(actor);
	const unsigned int modelBefore = ActorModelId(actor);
	qqspeed::logging::Logf(
		"topkart_change_item clear_enter callRva=0x%llX site=%s actor=%p uin=%u currentBefore=%u modelBefore=%u",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		uin,
		currentBefore,
		modelBefore);

	ClearModelFn original = g_originalClearModel;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<ClearModelFn>(g_topKartBase + kClearModelRva);
	}
	const __int64 result = original ? original(actor) : 0;
	qqspeed::logging::Logf(
		"topkart_change_item clear_leave callRva=0x%llX site=%s actor=%p uin=%u currentAfter=%u modelAfter=%u result=%lld",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		uin,
		ActorCurrentKartId(actor),
		ActorModelId(actor),
		static_cast<long long>(result));
	return result;
}

__int64 __fastcall HookedSetActiveKart(void* actor, unsigned int kartId, unsigned char flag)
{
	const std::uintptr_t callRva = CurrentCallRva();
	const unsigned int uin = ActorUin(actor);
	const unsigned int currentBefore = ActorCurrentKartId(actor);
	const unsigned int previousBefore = ActorPreviousKartId(actor);
	const unsigned int modelBefore = ActorModelId(actor);
	qqspeed::logging::Logf(
		"topkart_change_item set_active_enter callRva=0x%llX site=%s actor=%p uin=%u currentBefore=%u previousBefore=%u modelBefore=%u kartId=%u flag=%u",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		uin,
		currentBefore,
		previousBefore,
		modelBefore,
		kartId,
		static_cast<unsigned int>(flag));

	SetActiveKartFn original = g_originalSetActiveKart;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<SetActiveKartFn>(g_topKartBase + kSetActiveKartRva);
	}
	const __int64 result = original ? original(actor, kartId, flag) : 0;
	qqspeed::logging::Logf(
		"topkart_change_item set_active_leave callRva=0x%llX site=%s actor=%p uin=%u currentAfter=%u previousAfter=%u modelAfter=%u kartId=%u result=%lld",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		uin,
		ActorCurrentKartId(actor),
		ActorPreviousKartId(actor),
		ActorModelId(actor),
		kartId,
		static_cast<long long>(result));
	return result;
}

__int64 __fastcall HookedNotifyChangeKartModel(void* actor, int kartId)
{
	const std::uintptr_t callRva = CurrentCallRva();
	const unsigned int uin = ActorUin(actor);
	const unsigned int currentBefore = ActorCurrentKartId(actor);
	const unsigned int previousBefore = ActorPreviousKartId(actor);
	const unsigned int modelBefore = ActorModelId(actor);
	qqspeed::logging::Logf(
		"topkart_change_item notify_change_model_enter callRva=0x%llX site=%s actor=%p uin=%u currentBefore=%u previousBefore=%u modelBefore=%u kartId=%d",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		uin,
		currentBefore,
		previousBefore,
		modelBefore,
		kartId);

	NotifyChangeKartModelFn original = g_originalNotifyChangeKartModel;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<NotifyChangeKartModelFn>(g_topKartBase + kNotifyChangeKartModelRva);
	}
	const __int64 result = original ? original(actor, kartId) : 0;
	qqspeed::logging::Logf(
		"topkart_change_item notify_change_model_leave callRva=0x%llX site=%s actor=%p uin=%u currentAfter=%u previousAfter=%u modelAfter=%u kartId=%d result=%lld",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		uin,
		ActorCurrentKartId(actor),
		ActorPreviousKartId(actor),
		ActorModelId(actor),
		kartId,
		static_cast<long long>(result));
	return result;
}

void LookupItemTypeForLog(unsigned int itemId, unsigned int* itemType, unsigned int* itemType2nd)
{
	if (itemType) {
		*itemType = 0;
	}
	if (itemType2nd) {
		*itemType2nd = 0;
	}
	ItemLookupFn lookup = g_originalItemLookup;
	if (!lookup && g_topKartBase) {
		lookup = reinterpret_cast<ItemLookupFn>(g_topKartBase + kItemLookupRva);
	}
	const std::uintptr_t itemMgr = ReadTopKartGlobalPtr(kGlobalItemMgrRva);
	const std::uintptr_t item = (lookup && itemMgr && itemId)
		? lookup(reinterpret_cast<void*>(itemMgr), itemId)
		: 0;
	if (item && itemType) {
		*itemType = ReadU32Safe(reinterpret_cast<const void*>(item + 32));
	}
	if (item && itemType2nd) {
		*itemType2nd = ReadU32Safe(reinterpret_cast<const void*>(item + 36));
	}
}

__int64 __fastcall HookedPrepareActiveKart(
	void* actor,
	unsigned int kartId,
	unsigned int* outModelKind,
	unsigned int* outSelectedId,
	void* flagPtr)
{
	const std::uintptr_t callRva = CurrentCallRva();
	const unsigned int flag = ReadU8Safe(flagPtr);
	qqspeed::logging::Logf(
		"topkart_change_item prepare_active_enter callRva=0x%llX site=%s actor=%p uin=%u kartId=%u flag=%u outModelKindBefore=%u outSelectedBefore=%u",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		ActorUin(actor),
		kartId,
		flag,
		outModelKind ? *outModelKind : 0,
		outSelectedId ? *outSelectedId : 0);

	PrepareActiveKartFn original = g_originalPrepareActiveKart;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<PrepareActiveKartFn>(g_topKartBase + kPrepareActiveKartRva);
	}
	const __int64 result = original ? original(actor, kartId, outModelKind, outSelectedId, flagPtr) : 0;
	const unsigned int selectedId = outSelectedId ? *outSelectedId : 0;
	unsigned int selectedType = 0;
	unsigned int selectedType2nd = 0;
	LookupItemTypeForLog(selectedId, &selectedType, &selectedType2nd);
	qqspeed::logging::Logf(
		"topkart_change_item prepare_active_leave callRva=0x%llX site=%s actor=%p uin=%u kartId=%u result=%lld ok=%u outModelKind=%u outSelected=%u outSelectedType=%u/0x%X outSelectedType2nd=%u/0x%X",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		ActorUin(actor),
		kartId,
		static_cast<long long>(result),
		(result & 0xFF) ? 1 : 0,
		outModelKind ? *outModelKind : 0,
		selectedId,
		selectedType,
		selectedType,
		selectedType2nd,
		selectedType2nd);
	return result;
}

__int64 __fastcall HookedApplyActiveKart(
	void* actor,
	void* modelRef,
	unsigned int selectedId,
	int modelKind,
	unsigned char flag)
{
	const std::uintptr_t callRva = CurrentCallRva();
	unsigned int selectedType = 0;
	unsigned int selectedType2nd = 0;
	LookupItemTypeForLog(selectedId, &selectedType, &selectedType2nd);
	qqspeed::logging::Logf(
		"topkart_change_item apply_active_enter callRva=0x%llX site=%s actor=%p uin=%u modelRef=%p selectedId=%u selectedType=%u/0x%X selectedType2nd=%u/0x%X modelKind=%d flag=%u",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		ActorUin(actor),
		modelRef,
		selectedId,
		selectedType,
		selectedType,
		selectedType2nd,
		selectedType2nd,
		modelKind,
		static_cast<unsigned int>(flag));

	ApplyActiveKartFn original = g_originalApplyActiveKart;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<ApplyActiveKartFn>(g_topKartBase + kApplyActiveKartRva);
	}
	const __int64 result = original ? original(actor, modelRef, selectedId, modelKind, flag) : 0;
	qqspeed::logging::Logf(
		"topkart_change_item apply_active_leave callRva=0x%llX site=%s actor=%p uin=%u selectedId=%u modelKind=%d flag=%u result=%lld",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		ActorUin(actor),
		selectedId,
		modelKind,
		static_cast<unsigned int>(flag),
		static_cast<long long>(result));
	return result;
}

__int64 __fastcall HookedCheckDownloadKart(void* itemMgr, unsigned int itemId)
{
	const std::uintptr_t callRva = CurrentCallRva();
	char expectedPath[512] = {};
	BuildCheckDownloadKartPath(itemMgr, itemId, expectedPath, sizeof(expectedPath));

	CheckDownloadKartFn original = g_originalCheckDownloadKart;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<CheckDownloadKartFn>(g_topKartBase + kCheckDownloadKartRva);
	}
	const __int64 result = original ? original(itemMgr, itemId) : 0;
	if (IsWatchedKartItem(itemId) || (callRva >= 0x939000 && callRva < 0x93A000)) {
		qqspeed::logging::Logf(
			"topkart_change_item check_download_kart callRva=0x%llX site=%s itemMgr=%p itemId=%u expectedPath=%s result=%lld ok=%u",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			itemMgr,
			itemId,
			expectedPath[0] ? expectedPath : "<unbuilt>",
			static_cast<long long>(result),
			(result & 0xFF) ? 1 : 0);
	}
	return result;
}

unsigned char __fastcall HookedFileExists(const char* path)
{
	const std::uintptr_t callRva = CurrentCallRva();
	FileExistsFn original = g_originalFileExists;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<FileExistsFn>(g_topKartBase + kFileExistsRva);
	}
	const unsigned char result = original ? original(path) : 0;
	if (callRva == 0x1CB9AC || callRva == 0x1AAC0C || IsWatchedAvatarPath(path)) {
		qqspeed::logging::Logf(
			"topkart_change_item file_exists callRva=0x%llX site=%s path=%s result=%u",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			path ? path : "<null>",
			static_cast<unsigned int>(result));
	}
	return result;
}

__int64 __fastcall HookedLoadKartAm(void* actor, int uin, int kartId, void* desc, int a5)
{
	const std::uintptr_t callRva = CurrentCallRva();
	qqspeed::logging::Logf(
		"topkart_change_item load_kart_am_enter callRva=0x%llX site=%s actor=%p uinArg=%d actorUin=%u kartId=%d currentBefore=%u previousBefore=%u modelBefore=%u modelPtrBefore=%p modelInnerBefore=%p a5=%d",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		uin,
		ActorUin(actor),
		kartId,
		ActorCurrentKartId(actor),
		ActorPreviousKartId(actor),
		ActorModelId(actor),
		reinterpret_cast<void*>(ActorModelPtr(actor)),
		reinterpret_cast<void*>(ActorModelInnerPtr(actor)),
		a5);
	LoadKartAmFn original = g_originalLoadKartAm;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<LoadKartAmFn>(g_topKartBase + kLoadKartAmRva);
	}
	const __int64 result = original ? original(actor, uin, kartId, desc, a5) : 0;
	qqspeed::logging::Logf(
		"topkart_change_item load_kart_am_leave callRva=0x%llX site=%s actor=%p actorUin=%u kartId=%d currentAfter=%u previousAfter=%u modelAfter=%u modelPtrAfter=%p modelInnerAfter=%p result=%lld",
		static_cast<unsigned long long>(callRva),
		CallSiteName(callRva),
		actor,
		ActorUin(actor),
		kartId,
		ActorCurrentKartId(actor),
		ActorPreviousKartId(actor),
		ActorModelId(actor),
		reinterpret_cast<void*>(ActorModelPtr(actor)),
		reinterpret_cast<void*>(ActorModelInnerPtr(actor)),
		static_cast<long long>(result));
	return result;
}

std::uintptr_t __fastcall HookedAmCacheLookup(void* itemMgr, unsigned int uin, unsigned int itemId)
{
	AmCacheLookupFn original = g_originalAmCacheLookup;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<AmCacheLookupFn>(g_topKartBase + kAmCacheLookupRva);
	}
	const std::uintptr_t result = original ? original(itemMgr, uin, itemId) : 0;
	if (IsWatchedKartItem(itemId)) {
		qqspeed::logging::Logf(
			"topkart_change_item am_cache_lookup callRva=0x%llX site=%s itemMgr=%p uin=%u itemId=%u result=%p cachedAm=%p",
			static_cast<unsigned long long>(CurrentCallRva()),
			CallSiteName(CurrentCallRva()),
			itemMgr,
			uin,
			itemId,
			reinterpret_cast<void*>(result),
			reinterpret_cast<void*>(result ? ReadPtrSafe(reinterpret_cast<const void*>(result + 40)) : 0));
	}
	return result;
}

void* __fastcall HookedCreateKartAm(
	void* itemMgr,
	void* outAm,
	std::uintptr_t item,
	void* desc,
	int uin,
	unsigned int roleId)
{
	const std::uintptr_t callRva = CurrentCallRva();
	const unsigned int itemId = ItemIdFromItemPtr(item);
	const unsigned int itemType = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 32)) : 0;
	const unsigned int itemType2nd = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 36)) : 0;
	const unsigned int itemModelKind = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 72)) : 0;
	const unsigned char itemFlag80 = item ? ReadU8Safe(reinterpret_cast<const void*>(item + 80)) : 0;
	const unsigned char itemFlag616 = item ? ReadU8Safe(reinterpret_cast<const void*>(item + 616)) : 0;
	const bool shouldLog = IsWatchedKartItem(itemId) || ((itemType & 0xFF00u) == 0x600u);
	if (shouldLog) {
		qqspeed::logging::Logf(
			"topkart_change_item am_builder_create_enter callRva=0x%llX site=%s itemMgr=%p outAm=%p item=%p itemId=%u itemType=%u/0x%X itemType2nd=%u/0x%X modelKind=%u flag80=%u flag616=0x%X uin=%d roleId=%u",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			itemMgr,
			outAm,
			reinterpret_cast<void*>(item),
			itemId,
			itemType,
			itemType,
			itemType2nd,
			itemType2nd,
			itemModelKind,
			static_cast<unsigned int>(itemFlag80),
			static_cast<unsigned int>(itemFlag616),
			uin,
			roleId);
	}
	CreateKartAmFn original = g_originalCreateKartAm;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<CreateKartAmFn>(g_topKartBase + kCreateKartAmRva);
	}
	void* result = original ? original(itemMgr, outAm, item, desc, uin, roleId) : outAm;
	if (shouldLog) {
		const std::uintptr_t am = ReadPtrSafe(outAm);
		qqspeed::logging::Logf(
			"topkart_change_item am_builder_create_leave callRva=0x%llX site=%s itemId=%u outAm=%p result=%p am=%p amInner=%p",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			itemId,
			outAm,
			result,
			reinterpret_cast<void*>(am),
			reinterpret_cast<void*>(am ? ReadPtrSafe(reinterpret_cast<const void*>(am + 24)) : 0));
	}
	return result;
}

void* __fastcall HookedBuildKartAm(
	void* itemMgr,
	void* outAm,
	unsigned int uin,
	unsigned int itemId,
	void* desc,
	unsigned char clone,
	int roleId)
{
	const std::uintptr_t callRva = CurrentCallRva();
	ItemLookupFn lookup = g_originalItemLookup;
	if (!lookup && g_topKartBase) {
		lookup = reinterpret_cast<ItemLookupFn>(g_topKartBase + kItemLookupRva);
	}
	const std::uintptr_t item = (lookup && itemMgr && itemId)
		? lookup(itemMgr, itemId)
		: 0;
	const unsigned int itemType = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 32)) : 0;
	const unsigned int itemType2nd = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 36)) : 0;
	const unsigned int itemModelKind = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 72)) : 0;
	const unsigned char itemFlag616 = item ? ReadU8Safe(reinterpret_cast<const void*>(item + 616)) : 0;
	if (IsWatchedKartItem(itemId)) {
		qqspeed::logging::Logf(
			"topkart_change_item build_kart_am_enter callRva=0x%llX site=%s itemMgr=%p outAm=%p uin=%u itemId=%u item=%p itemType=%u/0x%X itemType2nd=%u/0x%X modelKind=%u flag616=0x%X clone=%u roleId=%d",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			itemMgr,
			outAm,
			uin,
			itemId,
			reinterpret_cast<void*>(item),
			itemType,
			itemType,
			itemType2nd,
			itemType2nd,
			itemModelKind,
			static_cast<unsigned int>(itemFlag616),
			static_cast<unsigned int>(clone),
			roleId);
	}
	BuildKartAmFn original = g_originalBuildKartAm;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<BuildKartAmFn>(g_topKartBase + kBuildKartAmRva);
	}
	void* result = original ? original(itemMgr, outAm, uin, itemId, desc, clone, roleId) : outAm;
	if (IsWatchedKartItem(itemId)) {
		const std::uintptr_t am = ReadPtrSafe(outAm);
		qqspeed::logging::Logf(
			"topkart_change_item build_kart_am_leave callRva=0x%llX site=%s itemId=%u outAm=%p result=%p am=%p amInner=%p",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			itemId,
			outAm,
			result,
			reinterpret_cast<void*>(am),
			reinterpret_cast<void*>(am ? ReadPtrSafe(reinterpret_cast<const void*>(am + 24)) : 0));
	}
	return result;
}

void* __fastcall HookedResolveAmModelId(void* outEncoded, std::uintptr_t item, void* encodedItemId)
{
	const std::uintptr_t callRva = CurrentCallRva();
	unsigned int itemId = 0;
	unsigned int itemRaw = 0;
	std::uintptr_t itemValuePtr = 0;
	DecodeEncodedU32Object(item, &itemId, &itemRaw, &itemValuePtr);

	unsigned int seedId = 0;
	unsigned int seedRaw = 0;
	std::uintptr_t seedValuePtr = 0;
	DecodeEncodedU32Object(reinterpret_cast<std::uintptr_t>(encodedItemId), &seedId, &seedRaw, &seedValuePtr);

	const unsigned int itemType = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 32)) : 0;
	const unsigned int itemType2nd = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 36)) : 0;
	const unsigned int itemModelKind = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 72)) : 0;
	const unsigned int itemField76 = item ? ReadU32Safe(reinterpret_cast<const void*>(item + 76)) : 0;
	const unsigned char itemFlag80 = item ? ReadU8Safe(reinterpret_cast<const void*>(item + 80)) : 0;
	const unsigned char itemFlag616 = item ? ReadU8Safe(reinterpret_cast<const void*>(item + 616)) : 0;
	const unsigned int globalSeedKey = g_topKartBase
		? ReadU32Safe(reinterpret_cast<const void*>(g_topKartBase + kGlobalAmModelSeedKeyRva))
		: 0;
	const std::uintptr_t globalSeedValuePtr = g_topKartBase
		? ReadPtrSafe(reinterpret_cast<const void*>(g_topKartBase + kGlobalAmModelSeedValuePtrRva))
		: 0;
	const unsigned int globalSeedRawBefore = globalSeedValuePtr
		? ReadU32Safe(reinterpret_cast<const void*>(globalSeedValuePtr))
		: 0;
	const unsigned int globalSeedDecodedBefore = globalSeedValuePtr
		? DecodeEncodedU32Raw(globalSeedKey, globalSeedRawBefore)
		: 0;
	unsigned int globalSeedRawAfter = globalSeedRawBefore;
	unsigned int globalSeedDecodedAfter = globalSeedDecodedBefore;
	bool clearedGlobalSeed = false;
	if (g_clearAmGlobalSeed
		&& globalSeedValuePtr
		&& ShouldUseExplicitVehicleAmModelId(itemId, itemType)) {
		const unsigned int zeroRaw = EncodeEncodedU32Raw(globalSeedKey, 0);
		if (WriteU32Safe(reinterpret_cast<void*>(globalSeedValuePtr), zeroRaw)) {
			globalSeedRawAfter = ReadU32Safe(reinterpret_cast<const void*>(globalSeedValuePtr));
			globalSeedDecodedAfter = DecodeEncodedU32Raw(globalSeedKey, globalSeedRawAfter);
			clearedGlobalSeed = globalSeedDecodedAfter == 0;
		}
	}

	ResolveAmModelIdFn original = g_originalResolveAmModelId;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<ResolveAmModelIdFn>(g_topKartBase + kResolveAmModelIdRva);
	}
	void* result = original ? original(outEncoded, item, encodedItemId) : outEncoded;

	unsigned int derivedModelId = 0;
	unsigned int derivedRaw = 0;
	std::uintptr_t derivedValuePtr = 0;
	DecodeEncodedU32Object(reinterpret_cast<std::uintptr_t>(outEncoded), &derivedModelId, &derivedRaw, &derivedValuePtr);
	bool forcedModelId = false;
	unsigned int finalModelId = derivedModelId;
	if (g_forceAmModelId
		&& outEncoded
		&& derivedValuePtr
		&& ShouldUseExplicitVehicleAmModelId(itemId, itemType)
		&& derivedModelId != itemId) {
		EncodeEncodedU32ObjectFn encode = g_topKartBase
			? reinterpret_cast<EncodeEncodedU32ObjectFn>(g_topKartBase + kEncodeEncodedU32ObjectRva)
			: nullptr;
		if (encode && WriteU32Safe(reinterpret_cast<void*>(derivedValuePtr), itemId)) {
			encode(outEncoded, reinterpret_cast<void*>(derivedValuePtr));
			unsigned int forcedRaw = 0;
			std::uintptr_t forcedValuePtr = 0;
			if (DecodeEncodedU32Object(reinterpret_cast<std::uintptr_t>(outEncoded), &finalModelId, &forcedRaw, &forcedValuePtr)) {
				derivedRaw = forcedRaw;
				derivedValuePtr = forcedValuePtr;
				forcedModelId = finalModelId == itemId;
			}
		}
	}

	const bool shouldLog = IsWatchedKartItem(itemId)
		|| IsWatchedKartItem(seedId)
		|| IsWatchedKartItem(derivedModelId)
		|| IsWatchedKartItem(finalModelId)
		|| ((itemType & 0xFF00u) == 0x600u);
	if (g_verboseProbeLogs && shouldLog) {
		qqspeed::logging::Logf(
			"topkart_change_item am_builder_derive_model callRva=0x%llX site=%s item=%p itemId=%u itemRaw=0x%08X itemValuePtr=%p seed=%p seedId=%u seedRaw=0x%08X seedValuePtr=%p globalSeedPtr=%p globalSeedKey=0x%08X globalSeedRawBefore=0x%08X globalSeedDecodedBefore=%u globalSeedRawAfter=0x%08X globalSeedDecodedAfter=%u clearedGlobalSeed=%u out=%p derivedModelId=%u finalModelId=%u forcedModelId=%u derivedRaw=0x%08X derivedValuePtr=%p itemType=%u/0x%X itemType2nd=%u/0x%X modelKind=%u field76=%u flag80=%u flag616=0x%X result=%p",
			static_cast<unsigned long long>(callRva),
			CallSiteName(callRva),
			reinterpret_cast<void*>(item),
			itemId,
			itemRaw,
			reinterpret_cast<void*>(itemValuePtr),
			encodedItemId,
			seedId,
			seedRaw,
			reinterpret_cast<void*>(seedValuePtr),
			reinterpret_cast<void*>(globalSeedValuePtr),
			globalSeedKey,
			globalSeedRawBefore,
			globalSeedDecodedBefore,
			globalSeedRawAfter,
			globalSeedDecodedAfter,
			clearedGlobalSeed ? 1u : 0u,
			outEncoded,
			derivedModelId,
			finalModelId,
			forcedModelId ? 1u : 0u,
			derivedRaw,
			reinterpret_cast<void*>(derivedValuePtr),
			itemType,
			itemType,
			itemType2nd,
			itemType2nd,
			itemModelKind,
			itemField76,
			static_cast<unsigned int>(itemFlag80),
			static_cast<unsigned int>(itemFlag616),
			result);
	}
	return result;
}

unsigned int __fastcall HookedFallbackModelId(
	void* itemMgr,
	unsigned int itemType,
	unsigned char itemFlag80,
	unsigned char itemFlag616)
{
	FallbackModelIdFn original = g_originalFallbackModelId;
	if (!original && g_topKartBase) {
		original = reinterpret_cast<FallbackModelIdFn>(g_topKartBase + kFallbackModelIdRva);
	}
	const unsigned int result = original ? original(itemMgr, itemType, itemFlag80, itemFlag616) : 0;
	if ((itemType & 0xFF00u) == 0x600u || IsWatchedKartItem(result)) {
		qqspeed::logging::Logf(
			"topkart_change_item am_builder_fallback_model callRva=0x%llX site=%s itemMgr=%p itemType=%u/0x%X flag80=%u flag616=0x%X result=%u",
			static_cast<unsigned long long>(CurrentCallRva()),
			CallSiteName(CurrentCallRva()),
			itemMgr,
			itemType,
			itemType,
			static_cast<unsigned int>(itemFlag80),
			static_cast<unsigned int>(itemFlag616),
			result);
	}
	return result;
}

std::size_t PatchChangeItemCalls(std::uintptr_t topKartBase)
{
	std::size_t patched = 0;
	if (!g_verboseProbeLogs) {
		if (g_forceAmModelId || g_clearAmGlobalSeed) {
			patched += qqspeed::network::patch::PatchDirectCalls(
				topKartBase,
				kResolveAmModelIdCallRvas,
				_countof(kResolveAmModelIdCallRvas),
				kResolveAmModelIdRva,
				reinterpret_cast<void*>(&HookedResolveAmModelId),
				"TopKartChangeItem am_builder_derive_model");
		}
		return patched;
	}

	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kApplyItemStatusCallRvas,
		_countof(kApplyItemStatusCallRvas),
		kApplyItemStatusRva,
		reinterpret_cast<void*>(&HookedApplyItemStatus),
		"TopKartChangeItem apply_status");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kItemLookupCallRvas,
		_countof(kItemLookupCallRvas),
		kItemLookupRva,
		reinterpret_cast<void*>(&HookedItemLookup),
		"TopKartChangeItem item_lookup");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kMapEquippedItemCallRvas,
		_countof(kMapEquippedItemCallRvas),
		kMapEquippedItemRva,
		reinterpret_cast<void*>(&HookedMapEquippedItem),
		"TopKartChangeItem map_equipped");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kSkinBaseLookupCallRvas,
		_countof(kSkinBaseLookupCallRvas),
		kSkinBaseLookupRva,
		reinterpret_cast<void*>(&HookedSkinBaseLookup),
		"TopKartChangeItem skinbase_lookup");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kBindModelCallRvas,
		_countof(kBindModelCallRvas),
		kBindModelRva,
		reinterpret_cast<void*>(&HookedBindModel),
		"TopKartChangeItem bind_model");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kClearModelCallRvas,
		_countof(kClearModelCallRvas),
		kClearModelRva,
		reinterpret_cast<void*>(&HookedClearModel),
		"TopKartChangeItem clear_model");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kSetActiveKartCallRvas,
		_countof(kSetActiveKartCallRvas),
		kSetActiveKartRva,
		reinterpret_cast<void*>(&HookedSetActiveKart),
		"TopKartChangeItem set_active");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kNotifyChangeKartModelCallRvas,
		_countof(kNotifyChangeKartModelCallRvas),
		kNotifyChangeKartModelRva,
		reinterpret_cast<void*>(&HookedNotifyChangeKartModel),
		"TopKartChangeItem notify_change_model");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kPrepareActiveKartCallRvas,
		_countof(kPrepareActiveKartCallRvas),
		kPrepareActiveKartRva,
		reinterpret_cast<void*>(&HookedPrepareActiveKart),
		"TopKartChangeItem prepare_active");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kApplyActiveKartCallRvas,
		_countof(kApplyActiveKartCallRvas),
		kApplyActiveKartRva,
		reinterpret_cast<void*>(&HookedApplyActiveKart),
		"TopKartChangeItem apply_active");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kCheckDownloadKartCallRvas,
		_countof(kCheckDownloadKartCallRvas),
		kCheckDownloadKartRva,
		reinterpret_cast<void*>(&HookedCheckDownloadKart),
		"TopKartChangeItem check_download_kart");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kFileExistsCallRvas,
		_countof(kFileExistsCallRvas),
		kFileExistsRva,
		reinterpret_cast<void*>(&HookedFileExists),
		"TopKartChangeItem file_exists");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kLoadKartAmCallRvas,
		_countof(kLoadKartAmCallRvas),
		kLoadKartAmRva,
		reinterpret_cast<void*>(&HookedLoadKartAm),
		"TopKartChangeItem load_kart_am");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kBuildKartAmCallRvas,
		_countof(kBuildKartAmCallRvas),
		kBuildKartAmRva,
		reinterpret_cast<void*>(&HookedBuildKartAm),
		"TopKartChangeItem build_kart_am");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kAmCacheLookupCallRvas,
		_countof(kAmCacheLookupCallRvas),
		kAmCacheLookupRva,
		reinterpret_cast<void*>(&HookedAmCacheLookup),
		"TopKartChangeItem am_cache_lookup");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kCreateKartAmCallRvas,
		_countof(kCreateKartAmCallRvas),
		kCreateKartAmRva,
		reinterpret_cast<void*>(&HookedCreateKartAm),
		"TopKartChangeItem am_builder_create");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kResolveAmModelIdCallRvas,
		_countof(kResolveAmModelIdCallRvas),
		kResolveAmModelIdRva,
		reinterpret_cast<void*>(&HookedResolveAmModelId),
		"TopKartChangeItem am_builder_derive_model");
	patched += qqspeed::network::patch::PatchDirectCalls(
		topKartBase,
		kFallbackModelIdCallRvas,
		_countof(kFallbackModelIdCallRvas),
		kFallbackModelIdRva,
		reinterpret_cast<void*>(&HookedFallbackModelId),
		"TopKartChangeItem am_builder_fallback_model");
	return patched;
}

DWORD WINAPI ChangeItemProbeThread(void* param)
{
	std::unique_ptr<ProbeThreadContext> context(static_cast<ProbeThreadContext*>(param));
	if (!context) {
		return 0;
	}

	DWORD waited = 0;
	while (waited <= kTopKartWaitMs) {
		HMODULE topKartModule = GetModuleHandleW(context->config.topKartModuleName);
		if (topKartModule) {
			qqspeed::topkart::InstallChangeItemProbe(topKartModule, context->config);
			return 0;
		}
		Sleep(kTopKartPollMs);
		waited += kTopKartPollMs;
	}

	qqspeed::logging::Logf("topkart_change_item probe skipped: Top-Kart.dll not loaded after %lu ms", kTopKartWaitMs);
	return 0;
}
}

namespace qqspeed::topkart
{
bool InstallChangeItemProbe(HMODULE topKartModule, const config::HookConfig& config)
{
	if (!config.features.enableTopKartChangeItemProbe) {
		qqspeed::logging::Logf("topkart_change_item probe disabled");
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
	g_originalApplyItemStatus = reinterpret_cast<ApplyItemStatusFn>(g_topKartBase + kApplyItemStatusRva);
	g_originalItemLookup = reinterpret_cast<ItemLookupFn>(g_topKartBase + kItemLookupRva);
	g_originalMapEquippedItem = reinterpret_cast<MapEquippedItemFn>(g_topKartBase + kMapEquippedItemRva);
	g_originalSkinBaseLookup = reinterpret_cast<SkinBaseLookupFn>(g_topKartBase + kSkinBaseLookupRva);
	g_originalBindModel = reinterpret_cast<BindModelFn>(g_topKartBase + kBindModelRva);
	g_originalClearModel = reinterpret_cast<ClearModelFn>(g_topKartBase + kClearModelRva);
	g_originalSetActiveKart = reinterpret_cast<SetActiveKartFn>(g_topKartBase + kSetActiveKartRva);
	g_originalNotifyChangeKartModel =
		reinterpret_cast<NotifyChangeKartModelFn>(g_topKartBase + kNotifyChangeKartModelRva);
	g_originalPrepareActiveKart = reinterpret_cast<PrepareActiveKartFn>(g_topKartBase + kPrepareActiveKartRva);
	g_originalApplyActiveKart = reinterpret_cast<ApplyActiveKartFn>(g_topKartBase + kApplyActiveKartRva);
	g_originalCheckDownloadKart = reinterpret_cast<CheckDownloadKartFn>(g_topKartBase + kCheckDownloadKartRva);
	g_originalFileExists = reinterpret_cast<FileExistsFn>(g_topKartBase + kFileExistsRva);
	g_originalLoadKartAm = reinterpret_cast<LoadKartAmFn>(g_topKartBase + kLoadKartAmRva);
	g_originalBuildKartAm = reinterpret_cast<BuildKartAmFn>(g_topKartBase + kBuildKartAmRva);
	g_originalAmCacheLookup = reinterpret_cast<AmCacheLookupFn>(g_topKartBase + kAmCacheLookupRva);
	g_originalCreateKartAm = reinterpret_cast<CreateKartAmFn>(g_topKartBase + kCreateKartAmRva);
	g_originalResolveAmModelId = reinterpret_cast<ResolveAmModelIdFn>(g_topKartBase + kResolveAmModelIdRva);
	g_originalFallbackModelId = reinterpret_cast<FallbackModelIdFn>(g_topKartBase + kFallbackModelIdRva);
	g_forceAmModelId = config.features.enableTopKartChangeItemForceAmModelId;
	g_clearAmGlobalSeed = config.features.enableTopKartChangeItemClearAmGlobalSeed;
	g_verboseProbeLogs = config.features.enableVerboseProbeLogs;

	g_force130GateOpenPatched = config.features.enableTopKartChangeItemForce130GateOpen
		? PatchResponse130OutdoorGate(g_topKartBase)
		: false;
	if (!config.features.enableTopKartChangeItemForce130GateOpen) {
		qqspeed::logging::Logf("topkart_change_item force130_gate_open disabled");
	}
	qqspeed::logging::Logf(
		"topkart_change_item force_am_model_id %s",
		g_forceAmModelId ? "enabled" : "disabled");
	qqspeed::logging::Logf(
		"topkart_change_item clear_am_global_seed %s",
		g_clearAmGlobalSeed ? "enabled" : "disabled");

	g_patchedCalls = PatchChangeItemCalls(g_topKartBase);
	InterlockedExchange(&g_installState, 2);

	qqspeed::logging::Logf(
		"topkart_change_item probe installed=%d patchedCalls=%llu force130GateOpen=%d base=%p",
		g_patchedCalls > 0 ? 1 : 0,
		static_cast<unsigned long long>(g_patchedCalls),
		g_force130GateOpenPatched ? 1 : 0,
		reinterpret_cast<void*>(g_topKartBase));
	return g_patchedCalls > 0;
}

void StartChangeItemProbeThread(const config::HookConfig& config)
{
	if (!config.features.enableTopKartChangeItemProbe) {
		qqspeed::logging::Logf("topkart_change_item probe disabled");
		return;
	}

	auto* context = new (std::nothrow) ProbeThreadContext();
	if (!context) {
		qqspeed::logging::Logf("topkart_change_item probe thread create skipped: alloc failed");
		return;
	}
	context->config = config;

	HANDLE thread = CreateThread(nullptr, 0, &ChangeItemProbeThread, context, 0, nullptr);
	if (!thread) {
		delete context;
		qqspeed::logging::Logf("topkart_change_item probe thread create failed error=%lu", GetLastError());
		return;
	}
	CloseHandle(thread);
}
}
