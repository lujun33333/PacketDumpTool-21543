#include "Msg100LoginPlain.h"

#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace qqspeed::login100
{
namespace
{
constexpr int kMaxForcedInnerDispatchCopy = 0x400000;
constexpr size_t kLoginPlainPlayerBaseOffset = 0x35;
constexpr size_t kLoginPlainItemStride = 23;
constexpr size_t kLoginPlainRecomCountryOffset = 0x33BB8;
constexpr size_t kLoginPlainServerTimeOffset = 0x33BB9;
constexpr size_t kLoginPlainReasonLenOffset = 0x33BBD;
constexpr size_t kRelationSrcUinOffset = 0x33D4E;
constexpr size_t kRelationFlagOffset = 0x33D52;
constexpr size_t kRelationUinOffset = 0x33D56;
constexpr size_t kRelationNickOffset = 0x33D5A;
constexpr size_t kRelationNickCapacity = 17;
constexpr size_t kRelationEngageTimeOffset = 0x33D6B;
constexpr size_t kRelationNextBookingOffset = 0x33D6F;
constexpr size_t kRelationBeginHoneyMoonOffset = 0x33D73;
constexpr size_t kRelationEndHoneyMoonOffset = 0x33D77;
constexpr size_t kRelationEngageFlagOffset = 0x33D7B;
constexpr size_t kRelationPostOffset = 0x33D7C;
constexpr size_t kLoginPlainMapArrayOffset = 0x33D8E;
constexpr size_t kLoginPlainMapStride = 12;
constexpr size_t kLoginPlainPetArrayOffset = kLoginPlainPetNumOffset + 2;
constexpr size_t kLoginPlainPetStride = 191;
constexpr size_t kLoginPlainTailOffset = 0x4CA0C;
constexpr size_t kLoginPlainOverdueArrayOffset = kLoginPlainOverdueNumOffset + 1;
constexpr size_t kLoginPlainOverdueStride = 12;

static_assert(
	kRelationPostOffset + 16 == kLoginPlainMapNumOffset,
	"relation layout must end at MapNum");

// d249 CResponseLoginPara tail layout recovered from Network.dll RVA
// 0x35BFF0.  The wire fields remain in protocol order, while the expanded
// object contains fixed-capacity arrays between several scalar fields.
constexpr size_t kLoginPlainStoneKartNumOffset = 0x4CEDF;
constexpr size_t kLoginPlainLockedMapIdOffset = 0x4CF13;
constexpr size_t kLoginPlainTotalGuildProsperityOffset = 0x4CF17;
constexpr size_t kLoginPlainClientStatusContextOffset = 0x4CF1B;
constexpr size_t kLoginPlainIsHoneyMoonOffset = 0x4CF1F;
constexpr size_t kLoginPlainNobleIdOffset = 0x4CF23;
constexpr size_t kLoginPlainNobleLevelOffset = 0x4CF27;
constexpr size_t kLoginPlainNoblePointOffset = 0x4CF28;
constexpr size_t kLoginPlainNobleLeftDaysOffset = 0x4CF2C;
constexpr size_t kLoginPlainExtendItemNumOffset = 0x4CF30;
constexpr size_t kLoginPlainOnlineInfoIntervalOffset = 0x4CF32;
constexpr size_t kLoginPlainGuildVipLevelOffset = 0x4CF33;
constexpr size_t kLoginPlainGuildVipPointOffset = 0x4CF34;
constexpr size_t kLoginPlainGuildVipLeftDaysOffset = 0x4CF38;
constexpr size_t kLoginPlainGuildVipGiftOffset = 0x4CF3C;
constexpr size_t kLoginPlainHasLdmOffset = 0x4CF3D;
constexpr size_t kLoginPlainForbiddenModeFreeTimeOffset = 0x4CF4B;
constexpr size_t kLoginPlainForbiddenModeBitSetOffset = 0x4CF4F;
constexpr size_t kLoginPlainLoverVipLevelOffset = 0x4CF53;
constexpr size_t kLoginPlainLoverVipPointOffset = 0x4CF54;
constexpr size_t kLoginPlainLoverVipGrowRateOffset = 0x4CF58;
constexpr size_t kLoginPlainLoverVipLeftDaysOffset = 0x4CF59;
constexpr size_t kLoginPlainLoverVipGiftOffset = 0x4CF5D;
constexpr size_t kLoginPlainLoverVipExpireTipsOffset = 0x4CF5E;
constexpr size_t kLoginPlainSkateTaskLevelOffset = 0x4CF5F;
constexpr size_t kLoginPlainSkateCoinOffset = 0x4CF60;
constexpr size_t kLoginPlainSkateSkillLevelOffset = 0x4CF64;
constexpr size_t kLoginPlainSkateDailyCoinOffset = 0x4CF65;
constexpr size_t kLoginPlainSkateHistoryCoinOffset = 0x4CF67;
constexpr size_t kLoginPlainMaxDayPveScoreOffset = 0x4CF6B;
constexpr size_t kLoginPlainMaxHistoryPveScoreOffset = 0x4CF6F;
constexpr size_t kLoginPlainLoveValueOffset = 0x4CF73;
constexpr size_t kLoginPlainHasCheerAdditionOffset = 0x4CF77;
constexpr size_t kLoginPlainGardenLevelOffset = 0x4CF7C;
constexpr size_t kLoginPlainGardenPointOffset = 0x4CF7D;
constexpr size_t kLoginPlainGardenGrowRateOffset = 0x4CF81;
constexpr size_t kLoginPlainGardenExpireTimeOffset = 0x4CF82;
constexpr size_t kLoginPlainGardenWeekPopularityOffset = 0x4CF86;
constexpr size_t kLoginPlainGardenTotalPopularityOffset = 0x4CF8A;
constexpr size_t kLoginPlainGardenLastUpdateOffset = 0x4CF8E;
constexpr size_t kLoginPlainGardenPrivateTypeOffset = 0x4CF92;
constexpr size_t kLoginPlainConsumeVipLevelOffset = 0x4CF93;
constexpr size_t kLoginPlainConsumeVipCharmMonthOffset = 0x4CF97;
constexpr size_t kLoginPlainConsumeVipSearchOffset = 0x4CF9B;
constexpr size_t kLoginPlainConsumeVipGetOffset = 0x4CF9F;
constexpr size_t kLoginPlainConsumeVipCareerOffset = 0x4CFA3;
constexpr size_t kLoginPlainConsumeVipShowOffset = 0x4CFA7;
constexpr size_t kLoginPlainEmperorLevelOffset = 0x4CFA8;
constexpr size_t kLoginPlainEmperorPointOffset = 0x4CFA9;
constexpr size_t kLoginPlainEmperorLeftDaysOffset = 0x4CFAD;
constexpr size_t kLoginPlainEmperorGrowRateOffset = 0x4CFB1;
constexpr size_t kLoginPlainEmperorExpiredTimeOffset = 0x4CFB2;
constexpr size_t kLoginPlainEmperorExpireTipsOffset = 0x4CFB6;
constexpr size_t kLoginPlainShuttleScoreOffset = 0x4CFB7;
constexpr size_t kLoginPlainActivityTotalOffset = 0x4CFBB;
constexpr size_t kLoginPlainActivityLevelOffset = 0x4CFBF;
constexpr size_t kLoginPlainGansterSeasonOffset = 0x4CFC3;
constexpr size_t kLoginPlainGansterScoreOffset = 0x4CFC7;
constexpr size_t kLoginPlainPoliceScoreOffset = 0x4CFCB;
constexpr size_t kLoginPlainTotalGansterScoreOffset = 0x4CFCF;
constexpr size_t kLoginPlainOlympicIdOffset = 0x4CFD3;
constexpr size_t kLoginPlainNpcWinTimesOffset = 0x4CFD7;
constexpr size_t kLoginPlainBorderSeasonOffset = 0x4CFDB;
constexpr size_t kLoginPlainBorderZhanxunOffset = 0x4CFDF;
constexpr size_t kLoginPlainBorderSeasonZhanxunOffset = 0x4CFE3;
constexpr size_t kLoginPlainReduceReturnRateOffset = 0x4CFE7;
constexpr size_t kLoginPlainReduceReturnPeriodOffset = 0x4CFEB;
constexpr size_t kLoginPlainSpecialActivityStatusOffset = 0x4CFEF;
constexpr size_t kLoginPlainThemeHouseDressOffset = 0x4CFF3;
constexpr size_t kLoginPlainRoom3dEnabledOffset = 0x4CFF7;
constexpr size_t kLoginPlainRoom3dObOffset = 0x4CFF8;
constexpr size_t kLoginPlainBuyPopupOpenOffset = 0x4CFF9;
constexpr size_t kLoginPlainBuyPopupLimitOffset = 0x4CFFA;
constexpr size_t kLoginPlainReconnectOptOffset = 0x4CFFE;
constexpr size_t kLoginPlainHasRankOffset = 0x4CFFF;
constexpr size_t kLoginPlainRankInfoOffset = 0x4D000;
constexpr size_t kLoginPlainHasHuanLingOffset = 0x4D014;
constexpr size_t kLoginPlainEquipSealTypeOffset = 0x4D01A;
constexpr size_t kLoginPlainCreditStarOffset = 0x4D01B;
constexpr size_t kLoginPlainRankFrameOffset = 0x4D01C;
constexpr size_t kLoginPlainVersionUrlLenOffset = 0x4D01D;
constexpr size_t kLoginPlainVersionUrlOffset = 0x4D01E;
constexpr size_t kLoginPlainVersionUrlCapacity = 200;
constexpr size_t kLoginPlainPrivacyUrlLenOffset = 0x4D0E6;
constexpr size_t kLoginPlainPrivacyUrlOffset = 0x4D0E7;
constexpr size_t kLoginPlainPrivacyUrlCapacity = 200;
constexpr size_t kLoginPlainRankShowTagOffset = 0x4D1AF;
constexpr size_t kLoginPlainLeftDeletingDaysOffset = 0x4D1B0;
constexpr size_t kLoginPlainOnlyEquippedOffset = 0x4D1B2;
constexpr size_t kLoginPlainLoginSwitchFlagOffset = 0x4D1B3;
constexpr size_t kLoginPlainLeftUploadPlogOffset = 0x4D1B7;
constexpr size_t kLoginPlainIpCityLenOffset = 0x4D1B8;
constexpr size_t kLoginPlainIpCityOffset = 0x4D1B9;
constexpr size_t kLoginPlainIpCityCapacity = 200;
constexpr size_t kLoginPlainOpenIdLenOffset = 0x4D281;
constexpr size_t kLoginPlainOpenIdOffset = 0x4D282;
constexpr size_t kLoginPlainOpenIdCapacity = 64;
constexpr size_t kLoginPlainAccessTokenLenOffset = 0x4D2C2;
constexpr size_t kLoginPlainAccessTokenOffset = 0x4D2C3;
constexpr size_t kLoginPlainAccessTokenCapacity = 192;
constexpr size_t kLoginPlainPara1Offset = 0x4D383;
constexpr size_t kLoginPlainPara2Offset = 0x4D387;
constexpr size_t kLoginPlainPara3Offset = 0x4D38B;

bool SafeCopyRaw(void* dst, const void* src, size_t size)
{
	if (!dst || (!src && size != 0)) {
		return false;
	}
	memcpy(dst, src, size);
	return true;
}

unsigned short ReadBE16Raw(const unsigned char* p)
{
	return static_cast<unsigned short>((static_cast<unsigned short>(p[0]) << 8) | p[1]);
}

unsigned int ReadBE32Raw(const unsigned char* p)
{
	return (static_cast<unsigned int>(p[0]) << 24) |
		(static_cast<unsigned int>(p[1]) << 16) |
		(static_cast<unsigned int>(p[2]) << 8) |
		static_cast<unsigned int>(p[3]);
}

void WriteLE16Raw(unsigned char* p, unsigned short value)
{
	p[0] = static_cast<unsigned char>(value & 0xFF);
	p[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
}

void WriteLE32Raw(unsigned char* p, unsigned int value)
{
	p[0] = static_cast<unsigned char>(value & 0xFF);
	p[1] = static_cast<unsigned char>((value >> 8) & 0xFF);
	p[2] = static_cast<unsigned char>((value >> 16) & 0xFF);
	p[3] = static_cast<unsigned char>((value >> 24) & 0xFF);
}

bool ReadBE16At(const unsigned char* stream, size_t streamLen, size_t offset, unsigned short& value)
{
	if (!stream || offset + 2 > streamLen) {
		return false;
	}
	value = ReadBE16Raw(stream + offset);
	return true;
}

bool ReadBE32At(const unsigned char* stream, size_t streamLen, size_t offset, unsigned int& value)
{
	if (!stream || offset + 4 > streamLen) {
		return false;
	}
	value = ReadBE32Raw(stream + offset);
	return true;
}

bool ReadU8FromStream(const unsigned char* stream, size_t streamLen, size_t& pos, unsigned char& value)
{
	if (!stream || pos + 1 > streamLen) {
		return false;
	}
	value = stream[pos++];
	return true;
}

bool ReadBE16FromStream(const unsigned char* stream, size_t streamLen, size_t& pos, unsigned short& value)
{
	if (!stream || pos + 2 > streamLen) {
		return false;
	}
	value = ReadBE16Raw(stream + pos);
	pos += 2;
	return true;
}

bool ReadBE32FromStream(const unsigned char* stream, size_t streamLen, size_t& pos, unsigned int& value)
{
	if (!stream || pos + 4 > streamLen) {
		return false;
	}
	value = ReadBE32Raw(stream + pos);
	pos += 4;
	return true;
}

bool ReadBytesFromStream(const unsigned char* stream, size_t streamLen, size_t& pos, const unsigned char*& value, size_t size)
{
	if (!stream || pos + size > streamLen) {
		return false;
	}
	value = stream + pos;
	pos += size;
	return true;
}

bool BeginLenRecord(
	const unsigned char* stream,
	size_t limit,
	size_t& pos,
	size_t& recordEnd)
{
	unsigned short recordLen = 0;
	if (!ReadBE16FromStream(stream, limit, pos, recordLen) || recordLen < 2) {
		return false;
	}
	recordEnd = pos + static_cast<size_t>(recordLen) - 2;
	return recordEnd <= limit;
}

bool FinishLenRecord(size_t recordEnd, size_t& pos)
{
	if (pos > recordEnd) {
		return false;
	}
	// A newer server may append fields inside a length-delimited record.  They
	// are safe to ignore because the expanded object is zero-initialized.
	pos = recordEnd;
	return true;
}

bool WritePlayerRelationInfoPlain(
	const unsigned char* stream,
	size_t streamLen,
	size_t& pos,
	unsigned char* plain)
{
	if (!plain) {
		return false;
	}

	size_t recordEnd = 0;
	if (!BeginLenRecord(stream, streamLen, pos, recordEnd)) {
		return false;
	}

	auto readRecord32 = [&](size_t outputOffset) -> bool {
		unsigned int value = 0;
		if (!ReadBE32FromStream(stream, recordEnd, pos, value)) {
			return false;
		}
		WriteLE32Raw(plain + outputOffset, value);
		return true;
	};

	if (!readRecord32(kRelationSrcUinOffset) ||
		!readRecord32(kRelationFlagOffset) ||
		!readRecord32(kRelationUinOffset)) {
		return false;
	}

	unsigned char nickLen = 0;
	if (!ReadU8FromStream(stream, recordEnd, pos, nickLen) ||
		nickLen > kRelationNickCapacity) {
		return false;
	}

	const unsigned char* nick = nullptr;
	if (!ReadBytesFromStream(stream, recordEnd, pos, nick, nickLen)) {
		return false;
	}
	memset(plain + kRelationNickOffset, 0, kRelationNickCapacity);
	if (nickLen != 0) {
		memcpy(plain + kRelationNickOffset, nick, nickLen);
	}

	if (!readRecord32(kRelationEngageTimeOffset) ||
		!readRecord32(kRelationNextBookingOffset) ||
		!readRecord32(kRelationBeginHoneyMoonOffset) ||
		!readRecord32(kRelationEndHoneyMoonOffset)) {
		return false;
	}

	unsigned char engageFlag = 0;
	if (!ReadU8FromStream(stream, recordEnd, pos, engageFlag)) {
		return false;
	}
	plain[kRelationEngageFlagOffset] = engageFlag;

	if (!FinishLenRecord(recordEnd, pos)) {
		return false;
	}
	for (size_t i = 0; i < 4; ++i) {
		unsigned int value = 0;
		if (!ReadBE32FromStream(stream, streamLen, pos, value)) {
			return false;
		}
		WriteLE32Raw(plain + kRelationPostOffset + i * 4, value);
	}

	return true;
}

bool ReadU8StringToPlain(
	const unsigned char* stream,
	size_t streamLen,
	size_t& pos,
	unsigned char* plain,
	size_t lenOffset,
	size_t dataOffset,
	size_t capacity)
{
	unsigned char len = 0;
	if (!ReadU8FromStream(stream, streamLen, pos, len) || len > capacity) {
		return false;
	}
	const unsigned char* data = nullptr;
	if (!ReadBytesFromStream(stream, streamLen, pos, data, len)) {
		return false;
	}
	plain[lenOffset] = len;
	if (len != 0) {
		memcpy(plain + dataOffset, data, len);
	}
	return true;
}

bool WritePostOverdueTailPlain(
	const unsigned char* stream,
	size_t streamLen,
	size_t& pos,
	unsigned char* plain,
	Msg100ExpandSnapshot& snapshot)
{
	auto fail = [&](const char* reason) {
		snapshot.finalStreamPos = pos;
		snapshot.remaining = pos <= streamLen ? streamLen - pos : 0;
		strncpy_s(snapshot.reason, reason, _TRUNCATE);
		return false;
	};
	auto read8 = [&](size_t offset) {
		unsigned char value = 0;
		if (!ReadU8FromStream(stream, streamLen, pos, value)) return false;
		plain[offset] = value;
		return true;
	};
	auto read16 = [&](size_t offset) {
		unsigned short value = 0;
		if (!ReadBE16FromStream(stream, streamLen, pos, value)) return false;
		WriteLE16Raw(plain + offset, value);
		return true;
	};
	auto read32 = [&](size_t offset) {
		unsigned int value = 0;
		if (!ReadBE32FromStream(stream, streamLen, pos, value)) return false;
		WriteLE32Raw(plain + offset, value);
		return true;
	};

	// StoneKartNum is followed by a fixed-capacity stone array in the expanded
	// object.  This direct Msg100 expander does not implement those variable
	// records, so fail closed instead of exposing a non-zero count over a zeroed
	// array and letting Top-Kart walk beyond initialized state.
	unsigned int stoneKartNum = 0;
	if (!ReadBE32FromStream(stream, streamLen, pos, stoneKartNum))
		return fail("stone_kart_count_short");
	if (stoneKartNum != 0)
		return fail("stone_kart_unsupported");
	WriteLE32Raw(plain + kLoginPlainStoneKartNumOffset, stoneKartNum);

	if (!read32(kLoginPlainLockedMapIdOffset) ||
		!read32(kLoginPlainTotalGuildProsperityOffset) ||
		!read32(kLoginPlainClientStatusContextOffset) ||
		!read8(kLoginPlainIsHoneyMoonOffset))
	{
		return fail("post_overdue_header_short");
	}

	unsigned char value8 = 0;
	unsigned short value16 = 0;
	unsigned int value32 = 0;
	if (!ReadU8FromStream(stream, streamLen, pos, value8))
		return fail("has_more_short");
	snapshot.hasMoreInfo = value8 ? 1 : 0;
	plain[kLoginPlainHasMoreInfoOffset] = snapshot.hasMoreInfo;
	if (!ReadBE16FromStream(stream, streamLen, pos, value16))
		return fail("cur_svr_short");
	snapshot.curSvrId = value16;
	WriteLE16Raw(plain + kLoginPlainCurSvrIdOffset, value16);
	snapshot.tailStartPos = pos;

	size_t recordEnd = 0;
	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read32(kLoginPlainNobleIdOffset) ||
		!read8(kLoginPlainNobleLevelOffset) ||
		!read32(kLoginPlainNoblePointOffset) ||
		!read32(kLoginPlainNobleLeftDaysOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("noble_record_invalid");
	}
	if (!read16(kLoginPlainExtendItemNumOffset) || !read8(kLoginPlainOnlineInfoIntervalOffset))
		return fail("extend_item_or_interval_short");

	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read8(kLoginPlainGuildVipLevelOffset) ||
		!read32(kLoginPlainGuildVipPointOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("guild_vip_base_invalid");
	}
	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read32(kLoginPlainGuildVipLeftDaysOffset) ||
		!read8(kLoginPlainGuildVipGiftOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("guild_vip_other_invalid");
	}
	if (!ReadU8FromStream(stream, streamLen, pos, value8))
		return fail("ldm_flag_short");
	plain[kLoginPlainHasLdmOffset] = value8 ? 1 : 0;
	if (value8 != 0)
		return fail("ldm_payload_unsupported");
	if (!read32(kLoginPlainForbiddenModeFreeTimeOffset) ||
		!read32(kLoginPlainForbiddenModeBitSetOffset))
	{
		return fail("forbidden_mode_short");
	}

	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read8(kLoginPlainLoverVipLevelOffset) ||
		!read32(kLoginPlainLoverVipPointOffset) ||
		!read8(kLoginPlainLoverVipGrowRateOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("lover_vip_base_invalid");
	}
	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read32(kLoginPlainLoverVipLeftDaysOffset) ||
		!read8(kLoginPlainLoverVipGiftOffset) ||
		!read8(kLoginPlainLoverVipExpireTipsOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("lover_vip_other_invalid");
	}
	if (!read8(kLoginPlainSkateTaskLevelOffset) ||
		!read32(kLoginPlainSkateCoinOffset) ||
		!read8(kLoginPlainSkateSkillLevelOffset) ||
		!read16(kLoginPlainSkateDailyCoinOffset) ||
		!read32(kLoginPlainSkateHistoryCoinOffset) ||
		!read32(kLoginPlainMaxDayPveScoreOffset) ||
		!read32(kLoginPlainMaxHistoryPveScoreOffset) ||
		!read32(kLoginPlainLoveValueOffset))
	{
		return fail("skate_or_love_short");
	}
	if (!ReadU8FromStream(stream, streamLen, pos, value8))
		return fail("cheer_flag_short");
	plain[kLoginPlainHasCheerAdditionOffset] = value8 ? 1 : 0;
	if (value8 != 0)
		return fail("cheer_payload_unsupported");

	size_t gardenEnd = 0;
	if (!BeginLenRecord(stream, streamLen, pos, gardenEnd) ||
		!read8(kLoginPlainGardenLevelOffset) ||
		!read32(kLoginPlainGardenPointOffset) ||
		!read8(kLoginPlainGardenGrowRateOffset) ||
		!read32(kLoginPlainGardenExpireTimeOffset))
	{
		return fail("garden_base_invalid");
	}
	size_t simpleGardenEnd = 0;
	if (!BeginLenRecord(stream, gardenEnd, pos, simpleGardenEnd) ||
		!read32(kLoginPlainGardenWeekPopularityOffset) ||
		!read32(kLoginPlainGardenTotalPopularityOffset) ||
		!read32(kLoginPlainGardenLastUpdateOffset) ||
		!read8(kLoginPlainGardenPrivateTypeOffset) ||
		!FinishLenRecord(simpleGardenEnd, pos) ||
		!FinishLenRecord(gardenEnd, pos))
	{
		return fail("garden_simple_invalid");
	}

	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read32(kLoginPlainConsumeVipLevelOffset) ||
		!read32(kLoginPlainConsumeVipCharmMonthOffset) ||
		!read32(kLoginPlainConsumeVipSearchOffset) ||
		!read32(kLoginPlainConsumeVipGetOffset) ||
		!read32(kLoginPlainConsumeVipCareerOffset) ||
		!read8(kLoginPlainConsumeVipShowOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("consume_vip_invalid");
	}
	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read8(kLoginPlainEmperorLevelOffset) ||
		!read32(kLoginPlainEmperorPointOffset) ||
		!read32(kLoginPlainEmperorLeftDaysOffset) ||
		!read8(kLoginPlainEmperorGrowRateOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("emperor_base_invalid");
	}
	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read32(kLoginPlainEmperorExpiredTimeOffset) ||
		!read8(kLoginPlainEmperorExpireTipsOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("emperor_other_invalid");
	}
	if (!read32(kLoginPlainShuttleScoreOffset))
		return fail("shuttle_score_short");
	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read32(kLoginPlainActivityTotalOffset) ||
		!read32(kLoginPlainActivityLevelOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("activity_info_invalid");
	}
	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read32(kLoginPlainGansterSeasonOffset) ||
		!read32(kLoginPlainGansterScoreOffset) ||
		!read32(kLoginPlainPoliceScoreOffset) ||
		!read32(kLoginPlainTotalGansterScoreOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("ganster_info_invalid");
	}
	if (!read32(kLoginPlainOlympicIdOffset) || !read32(kLoginPlainNpcWinTimesOffset))
		return fail("olympic_or_npc_short");
	if (!BeginLenRecord(stream, streamLen, pos, recordEnd) ||
		!read32(kLoginPlainBorderSeasonOffset) ||
		!read32(kLoginPlainBorderZhanxunOffset) ||
		!read32(kLoginPlainBorderSeasonZhanxunOffset) ||
		!FinishLenRecord(recordEnd, pos))
	{
		return fail("border_info_invalid");
	}
	if (!read32(kLoginPlainReduceReturnRateOffset) ||
		!read32(kLoginPlainReduceReturnPeriodOffset))
	{
		return fail("reduce_return_short");
	}
	if (!ReadU8FromStream(stream, streamLen, pos, value8))
		return fail("special_activity_short");
	WriteLE32Raw(plain + kLoginPlainSpecialActivityStatusOffset, value8);
	if (!read32(kLoginPlainThemeHouseDressOffset))
		return fail("theme_house_short");
	if (!ReadBE32FromStream(stream, streamLen, pos, value32))
		return fail("room3d_short");
	plain[kLoginPlainRoom3dEnabledOffset] = value32 ? 1 : 0;
	if (!read8(kLoginPlainRoom3dObOffset) ||
		!read8(kLoginPlainBuyPopupOpenOffset) ||
		!read32(kLoginPlainBuyPopupLimitOffset) ||
		!read8(kLoginPlainReconnectOptOffset))
	{
		return fail("room_or_popup_short");
	}

	if (!ReadU8FromStream(stream, streamLen, pos, value8))
		return fail("rank_flag_short");
	plain[kLoginPlainHasRankOffset] = value8 ? 1 : 0;
	if (value8 != 0)
	{
		if (!BeginLenRecord(stream, streamLen, pos, recordEnd))
			return fail("rank_record_short");
		for (size_t i = 0; i < 5; ++i)
		{
			if (!read32(kLoginPlainRankInfoOffset + i * sizeof(unsigned int)))
				return fail("rank_record_invalid");
		}
		if (!FinishLenRecord(recordEnd, pos))
			return fail("rank_record_overflow");
	}
	if (!ReadU8FromStream(stream, streamLen, pos, value8))
		return fail("huanling_flag_short");
	plain[kLoginPlainHasHuanLingOffset] = value8 ? 1 : 0;
	if (value8 != 0)
		return fail("huanling_payload_unsupported");
	if (!read8(kLoginPlainEquipSealTypeOffset) ||
		!read8(kLoginPlainCreditStarOffset) ||
		!read8(kLoginPlainRankFrameOffset))
	{
		return fail("panel_flags_short");
	}
	if (!ReadU8StringToPlain(stream, streamLen, pos, plain,
		kLoginPlainVersionUrlLenOffset, kLoginPlainVersionUrlOffset, kLoginPlainVersionUrlCapacity) ||
		!ReadU8StringToPlain(stream, streamLen, pos, plain,
			kLoginPlainPrivacyUrlLenOffset, kLoginPlainPrivacyUrlOffset, kLoginPlainPrivacyUrlCapacity) ||
		!read8(kLoginPlainRankShowTagOffset) ||
		!read16(kLoginPlainLeftDeletingDaysOffset) ||
		!read8(kLoginPlainOnlyEquippedOffset) ||
		!read32(kLoginPlainLoginSwitchFlagOffset) ||
		!read8(kLoginPlainLeftUploadPlogOffset))
	{
		return fail("url_or_login_flags_invalid");
	}
	if (!ReadU8StringToPlain(stream, streamLen, pos, plain,
		kLoginPlainIpCityLenOffset, kLoginPlainIpCityOffset, kLoginPlainIpCityCapacity) ||
		!ReadU8StringToPlain(stream, streamLen, pos, plain,
			kLoginPlainOpenIdLenOffset, kLoginPlainOpenIdOffset, kLoginPlainOpenIdCapacity) ||
		!ReadU8StringToPlain(stream, streamLen, pos, plain,
			kLoginPlainAccessTokenLenOffset, kLoginPlainAccessTokenOffset, kLoginPlainAccessTokenCapacity) ||
		!read32(kLoginPlainPara1Offset) ||
		!read32(kLoginPlainPara2Offset) ||
		!read32(kLoginPlainPara3Offset))
	{
		return fail("identity_strings_or_para_invalid");
	}

	// The private direct-wire schema carries TssTicketLen as U8.  The d249
	// expanded object stores that same value as WORD at 0x4D38F; it does not add
	// a second length field after the three legacy DWORDs.
	unsigned char legacyExtLen = 0;
	if (!ReadU8FromStream(stream, streamLen, pos, legacyExtLen))
		return fail("legacy_tss_len_short");
	const unsigned char* legacyExtData = nullptr;
	if (legacyExtLen != 0 &&
		!ReadBytesFromStream(stream, streamLen, pos, legacyExtData, legacyExtLen))
	{
		return fail("legacy_tss_data_short");
	}
	for (int i = 0; i < 3; ++i)
	{
		if (!ReadBE32FromStream(stream, streamLen, pos, value32))
			return fail("post_tss_dword_short");
	}
	snapshot.tailConsumed = pos - snapshot.tailStartPos;
	snapshot.extLen = legacyExtLen;
	WriteLE16Raw(plain + kLoginPlainExtLenOffset, legacyExtLen);
	if (legacyExtLen != 0)
		memcpy(plain + kLoginPlainExtDataOffset, legacyExtData, legacyExtLen);

	snapshot.finalStreamPos = pos;
	snapshot.remaining = pos <= streamLen ? streamLen - pos : 0;
	if (pos != streamLen)
		return fail("trailing_stream_bytes");
	return true;
}

bool WritePlayerBaseInfoPlain(
	const unsigned char* stream,
	size_t streamLen,
	size_t& pos,
	unsigned char* plain,
	Msg100ExpandSnapshot& snapshot)
{
	unsigned short recordLen = 0;
	if (!ReadBE16FromStream(stream, streamLen, pos, recordLen) || recordLen < 2) {
		return false;
	}
	snapshot.playerBaseLen = recordLen;
	const size_t recordEnd = pos + static_cast<size_t>(recordLen) - 2;
	if (recordEnd > streamLen) {
		return false;
	}

	size_t out = kLoginPlainPlayerBaseOffset;
	const unsigned char* bytes = nullptr;
	if (!ReadBytesFromStream(stream, streamLen, pos, bytes, 17)) return false;
	memcpy(plain + out, bytes, 17);
	out += 17;

	unsigned char byteValue = 0;
	if (!ReadU8FromStream(stream, streamLen, pos, byteValue)) return false;
	plain[out++] = byteValue;
	if (!ReadU8FromStream(stream, streamLen, pos, byteValue)) return false;
	plain[out++] = byteValue;

	for (int i = 0; i < 23; ++i) {
		unsigned int value = 0;
		if (!ReadBE32FromStream(stream, streamLen, pos, value)) return false;
		WriteLE32Raw(plain + out, value);
		out += 4;
	}

	if (!ReadBytesFromStream(stream, streamLen, pos, bytes, 64)) return false;
	memcpy(plain + out, bytes, 64);
	out += 64;

	const unsigned char tailFieldSizes[] = {
		4, 4, 2, 1, 4, 4, 2, 2, 2, 1, 4, 4, 4, 1, 4, 4, 1,
		4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 2, 4, 4, 4
	};
	for (unsigned char size : tailFieldSizes) {
		if (size == 1) {
			if (!ReadU8FromStream(stream, streamLen, pos, byteValue)) return false;
			plain[out++] = byteValue;
		}
		else if (size == 2) {
			unsigned short value = 0;
			if (!ReadBE16FromStream(stream, streamLen, pos, value)) return false;
			WriteLE16Raw(plain + out, value);
			out += 2;
		}
		else {
			unsigned int value = 0;
			if (!ReadBE32FromStream(stream, streamLen, pos, value)) return false;
			WriteLE32Raw(plain + out, value);
			out += 4;
		}
	}

	return pos == recordEnd && out == kLoginPlainItemNumOffset;
}

bool WriteItemRecordsPlain(
	const unsigned char* stream,
	size_t streamLen,
	size_t& pos,
	unsigned char* plain,
	Msg100ExpandSnapshot& snapshot)
{
	unsigned short count = 0;
	if (!ReadBE16FromStream(stream, streamLen, pos, count)) {
		return false;
	}
	snapshot.itemNum = count;

	const size_t maxWritable = (kLoginPlainRecomCountryOffset - kLoginPlainItemArrayOffset) / kLoginPlainItemStride;
	if (count > maxWritable) {
		return false;
	}
	WriteLE16Raw(plain + kLoginPlainItemNumOffset, count);
	for (unsigned int i = 0; i < count; ++i) {
		unsigned short recordLen = 0;
		if (!ReadBE16FromStream(stream, streamLen, pos, recordLen) || recordLen < 25) {
			return false;
		}
		const size_t recordEnd = pos + static_cast<size_t>(recordLen) - 2;
		if (recordEnd > streamLen) {
			return false;
		}

		if (i < maxWritable) {
			size_t out = kLoginPlainItemArrayOffset + static_cast<size_t>(i) * kLoginPlainItemStride;
			unsigned int value32 = 0;
			unsigned short value16 = 0;
			unsigned char value8 = 0;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(plain + out, value32); out += 4;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(plain + out, value32); out += 4;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(plain + out, value32); out += 4;
			if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
			plain[out++] = value8;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(plain + out, value32); out += 4;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(plain + out, value32); out += 4;
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			WriteLE16Raw(plain + out, value16);
			++snapshot.itemWritten;
		}
		pos = recordEnd;
	}
	return true;
}

bool WriteMapRecordsPlain(
	const unsigned char* stream,
	size_t streamLen,
	size_t& pos,
	unsigned char* plain,
	Msg100ExpandSnapshot& snapshot)
{
	unsigned short count = 0;
	if (!ReadBE16FromStream(stream, streamLen, pos, count)) {
		return false;
	}
	snapshot.mapNum = count;

	const size_t maxWritable = (kLoginPlainPetNumOffset - kLoginPlainMapArrayOffset) / kLoginPlainMapStride;
	if (count > maxWritable) {
		return false;
	}
	WriteLE16Raw(plain + kLoginPlainMapNumOffset, count);
	for (unsigned int i = 0; i < count; ++i) {
		unsigned short recordLen = 0;
		if (!ReadBE16FromStream(stream, streamLen, pos, recordLen) || recordLen < 10) {
			return false;
		}
		const size_t recordEnd = pos + static_cast<size_t>(recordLen) - 2;
		if (recordEnd > streamLen) {
			return false;
		}
		if (i < maxWritable) {
			size_t out = kLoginPlainMapArrayOffset + static_cast<size_t>(i) * kLoginPlainMapStride;
			unsigned int value32 = 0;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(plain + out, value32); out += 4;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(plain + out, value32); out += 4;
			plain[out + 0] = 0xFF;
			plain[out + 1] = 0x00;
			plain[out + 2] = 0xFF;
			plain[out + 3] = 0xFF;
			++snapshot.mapWritten;
		}
		pos = recordEnd;
	}
	return true;
}

void InitPetRecordDefaults(unsigned char* out)
{
	if (!out) {
		return;
	}

	for (size_t offset = 75; offset + 3 < 179; offset += 4) {
		out[offset + 0] = 0x00;
		out[offset + 1] = 0xFF;
		out[offset + 2] = 0xFF;
		out[offset + 3] = 0xFF;
	}
	out[189] = 0xFF;
	out[190] = 0xFF;
}

bool WritePetRecordsPlain(
	const unsigned char* stream,
	size_t streamLen,
	size_t& pos,
	unsigned char* plain,
	Msg100ExpandSnapshot& snapshot)
{
	unsigned short count = 0;
	if (!ReadBE16FromStream(stream, streamLen, pos, count)) {
		return false;
	}
	snapshot.petNum = count;

	const size_t maxWritable = (kLoginPlainTailOffset - kLoginPlainPetArrayOffset) / kLoginPlainPetStride;
	if (count > maxWritable) {
		return false;
	}
	WriteLE16Raw(plain + kLoginPlainPetNumOffset, count);
	for (unsigned int i = 0; i < count; ++i) {
		unsigned short recordLen = 0;
		if (!ReadBE16FromStream(stream, streamLen, pos, recordLen) || recordLen < 95) {
			return false;
		}
		const size_t recordEnd = pos + static_cast<size_t>(recordLen) - 2;
		if (recordEnd > streamLen) {
			return false;
		}

		if (i < maxWritable) {
			unsigned char* out = plain + kLoginPlainPetArrayOffset + static_cast<size_t>(i) * kLoginPlainPetStride;
			InitPetRecordDefaults(out);

			unsigned int value32 = 0;
			unsigned short value16 = 0;
			unsigned char value8 = 0;
			const unsigned char* bytes = nullptr;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 0, value32);
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 4, value32);
			if (!ReadBytesFromStream(stream, streamLen, pos, bytes, 24)) return false;
			memcpy(out + 8, bytes, 24);
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			WriteLE16Raw(out + 32, value16);
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			WriteLE16Raw(out + 34, value16);
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 36, value32);
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 40, value32);
			if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
			out[44] = value8;
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			WriteLE16Raw(out + 45, value16);
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			WriteLE16Raw(out + 47, value16);
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			WriteLE16Raw(out + 49, value16);
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			WriteLE16Raw(out + 51, value16);
			if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
			out[53] = value8;
			if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
			out[54] = value8;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 55, value32);
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			WriteLE16Raw(out + 59, value16);
			if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
			out[61] = value8;
			if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
			out[62] = value8;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 63, value32);
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			if (!ReadBE16FromStream(stream, streamLen, pos, value16)) return false;
			WriteLE16Raw(out + 67, value16);
			if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
			out[69] = value8;
			if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
			out[70] = value8;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 71, value32);
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 179, value32);
			if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
			out[183] = value8;
			if (!ReadBytesFromStream(stream, streamLen, pos, bytes, 2)) return false;
			if (!ReadBytesFromStream(stream, streamLen, pos, bytes, 5)) return false;
			memcpy(out + 184, bytes, 5);
		}
		pos = recordEnd;
	}
	return true;
}

bool WriteOverdueRecordsPlain(
	const unsigned char* stream,
	size_t streamLen,
	size_t& pos,
	unsigned char* plain,
	Msg100ExpandSnapshot& snapshot)
{
	unsigned char count = 0;
	if (!ReadU8FromStream(stream, streamLen, pos, count)) {
		return false;
	}
	snapshot.overdueNum = count;

	const size_t maxWritable = (kLoginPlainPostOverdueOffset - kLoginPlainOverdueArrayOffset) / kLoginPlainOverdueStride;
	if (count > maxWritable) {
		return false;
	}
	plain[kLoginPlainOverdueNumOffset] = count;
	for (unsigned int i = 0; i < count; ++i) {
		unsigned short recordLen = 0;
		if (!ReadBE16FromStream(stream, streamLen, pos, recordLen) || recordLen < 14) {
			return false;
		}
		const size_t recordEnd = pos + static_cast<size_t>(recordLen) - 2;
		if (recordEnd > streamLen) {
			return false;
		}

		if (i < maxWritable) {
			unsigned char* out = plain + kLoginPlainOverdueArrayOffset + static_cast<size_t>(i) * kLoginPlainOverdueStride;
			unsigned int value32 = 0;
			const unsigned char* bytes = nullptr;
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 0, value32);
			if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
			WriteLE32Raw(out + 4, value32);
			if (!ReadBytesFromStream(stream, streamLen, pos, bytes, 4)) return false;
			memcpy(out + 8, bytes, 4);
		}
		pos = recordEnd;
	}
	return true;
}
}

LoginPlainSnapshot ReadLoginPlainSnapshot(const unsigned char* outData, int outLen)
{
	LoginPlainSnapshot snapshot = {};
	if (!outData || outLen < kLoginPlainMinLen) {
		return snapshot;
	}

	unsigned char header[kLoginPlainMinLen] = {};
	if (!SafeCopyRaw(header, outData, sizeof(header))) {
		return snapshot;
	}

	snapshot.readable = true;
	memcpy(&snapshot.resultId, header + kLoginPlainResultIdOffset, sizeof(snapshot.resultId));
	snapshot.keyLen = header[kLoginPlainKeyLenOffset];
	if (snapshot.keyLen <= 16) {
		memcpy(snapshot.key, header + kLoginPlainKeyOffset, snapshot.keyLen);
		snapshot.key[snapshot.keyLen] = 0;
	}
	memcpy(&snapshot.uin, header + kLoginPlainUinOffset, sizeof(snapshot.uin));
	memcpy(&snapshot.identity, header + kLoginPlainIdentityOffset, sizeof(snapshot.identity));
	memcpy(&snapshot.connId, header + kLoginPlainConnIdOffset, sizeof(snapshot.connId));
	memcpy(&snapshot.loginType, header + kLoginPlainLoginTypeOffset, sizeof(snapshot.loginType));
	memcpy(&snapshot.gameSvrIdentity, header + kLoginPlainGameSvrIdentityOffset, sizeof(snapshot.gameSvrIdentity));

	snapshot.shapeOk =
		snapshot.resultId <= 1 &&
		snapshot.keyLen == 16 &&
		snapshot.key[0] != 0 &&
		snapshot.uin != 0 &&
		snapshot.identity != 0 &&
		snapshot.gameSvrIdentity != 0;
	return snapshot;
}

bool TryExpandDirectMsg100LoginPlain(
	const unsigned char* body,
	int bodyLen,
	void* outData,
	void* outLen,
	Msg100ExpandSnapshot& snapshot)
{
	snapshot = {};
	snapshot.attempted = true;
	if (!body || bodyLen < 2 || !outData || !outLen || bodyLen > kMaxForcedInnerDispatchCopy) {
		strncpy_s(snapshot.reason, "bad_args", _TRUNCATE);
		return false;
	}

	std::vector<unsigned char> bodyCopy(static_cast<size_t>(bodyLen));
	if (!SafeCopyRaw(bodyCopy.data(), body, bodyCopy.size())) {
		strncpy_s(snapshot.reason, "body_unreadable", _TRUNCATE);
		return false;
	}

	const unsigned short streamLen16 = ReadBE16Raw(bodyCopy.data());
	// The BE16 length includes the two-byte length field itself.  The old code
	// treated it as payload length and silently admitted two bytes of outer
	// padding as Msg100 data.
	if (streamLen16 < 2 + 0x25 || streamLen16 > bodyCopy.size()) {
		strncpy_s(snapshot.reason, "bad_stream_len", _TRUNCATE);
		return false;
	}
	snapshot.streamLen = streamLen16;
	const size_t streamLen = static_cast<size_t>(streamLen16) - 2;

	const unsigned char* stream = bodyCopy.data() + 2;
	unsigned short resultId = 0;
	unsigned int value32 = 0;
	if (!ReadBE16At(stream, streamLen, 0x00, resultId)) return false;
	snapshot.resultId = resultId;
	snapshot.keyLen = stream[0x02];
	if (snapshot.keyLen == 0 || snapshot.keyLen > 16 || 0x03 + snapshot.keyLen > streamLen) {
		strncpy_s(snapshot.reason, "bad_key_len", _TRUNCATE);
		return false;
	}
	memcpy(snapshot.key, stream + 0x03, snapshot.keyLen);
	snapshot.key[snapshot.keyLen] = 0;
	if (!ReadBE32At(stream, streamLen, 0x13, snapshot.uin) ||
		!ReadBE32At(stream, streamLen, 0x17, snapshot.identity) ||
		!ReadBE16At(stream, streamLen, 0x1B, snapshot.connId) ||
		!ReadBE32At(stream, streamLen, 0x1D, snapshot.loginType) ||
		!ReadBE32At(stream, streamLen, 0x21, snapshot.gameSvrIdentity))
	{
		strncpy_s(snapshot.reason, "short_header", _TRUNCATE);
		return false;
	}

	std::unique_ptr<unsigned char[]> plain(new (std::nothrow) unsigned char[kMsg100LoginPlainLen]);
	if (!plain) {
		strncpy_s(snapshot.reason, "alloc_plain_failed", _TRUNCATE);
		return false;
	}
	memset(plain.get(), 0, kMsg100LoginPlainLen);

	WriteLE16Raw(plain.get() + kLoginPlainResultIdOffset, resultId);
	plain[kLoginPlainKeyLenOffset] = snapshot.keyLen;
	memcpy(plain.get() + kLoginPlainKeyOffset, stream + 0x03, snapshot.keyLen);
	WriteLE32Raw(plain.get() + kLoginPlainUinOffset, snapshot.uin);
	WriteLE32Raw(plain.get() + kLoginPlainIdentityOffset, snapshot.identity);
	WriteLE16Raw(plain.get() + kLoginPlainConnIdOffset, snapshot.connId);
	WriteLE32Raw(plain.get() + kLoginPlainLoginTypeOffset, snapshot.loginType);
	WriteLE32Raw(plain.get() + kLoginPlainGameSvrIdentityOffset, snapshot.gameSvrIdentity);

	size_t pos = 0x25;
	if (!WritePlayerBaseInfoPlain(stream, streamLen, pos, plain.get(), snapshot)) {
		strncpy_s(snapshot.reason, "player_base_failed", _TRUNCATE);
		return false;
	}
	if (!WriteItemRecordsPlain(stream, streamLen, pos, plain.get(), snapshot)) {
		strncpy_s(snapshot.reason, "items_failed", _TRUNCATE);
		return false;
	}

	unsigned short taskNum = 0;
	if (!ReadBE16FromStream(stream, streamLen, pos, taskNum)) {
		strncpy_s(snapshot.reason, "task_count_failed", _TRUNCATE);
		return false;
	}
	snapshot.taskNum = taskNum;
	if (taskNum != 0) {
		strncpy_s(snapshot.reason, "task_payload_unsupported", _TRUNCATE);
		return false;
	}

	unsigned char value8 = 0;
	if (!ReadU8FromStream(stream, streamLen, pos, value8)) {
		strncpy_s(snapshot.reason, "recom_failed", _TRUNCATE);
		return false;
	}
	plain[kLoginPlainRecomCountryOffset] = value8;
	if (!ReadBE32FromStream(stream, streamLen, pos, value32)) {
		strncpy_s(snapshot.reason, "server_time_failed", _TRUNCATE);
		return false;
	}
	WriteLE32Raw(plain.get() + kLoginPlainServerTimeOffset, value32);
	if (!ReadU8FromStream(stream, streamLen, pos, value8)) {
		strncpy_s(snapshot.reason, "reason_len_failed", _TRUNCATE);
		return false;
	}
	plain[kLoginPlainReasonLenOffset] = value8;
	if (value8 != 0) {
		strncpy_s(snapshot.reason, "reason_payload_unsupported", _TRUNCATE);
		return false;
	}
	if (pos + value8 > streamLen) {
		strncpy_s(snapshot.reason, "reason_short", _TRUNCATE);
		return false;
	}
	pos += value8;

	if (!WritePlayerRelationInfoPlain(stream, streamLen, pos, plain.get())) {
		strncpy_s(snapshot.reason, "relation_failed", _TRUNCATE);
		return false;
	}

	if (!WriteMapRecordsPlain(stream, streamLen, pos, plain.get(), snapshot)) {
		strncpy_s(snapshot.reason, "maps_failed", _TRUNCATE);
		return false;
	}

	if (!WritePetRecordsPlain(stream, streamLen, pos, plain.get(), snapshot)) {
		strncpy_s(snapshot.reason, "pets_failed", _TRUNCATE);
		return false;
	}

	size_t out = kLoginPlainTailOffset;
	if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
	plain[out++] = value8;
	if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
	WriteLE32Raw(plain.get() + out, value32); out += 4;
	if (!ReadBE32FromStream(stream, streamLen, pos, value32)) return false;
	WriteLE32Raw(plain.get() + out, value32); out += 4;
	if (!ReadU8FromStream(stream, streamLen, pos, value8)) return false;
	plain[out++] = value8;
	const unsigned char* bytes = nullptr;
	if (!ReadBytesFromStream(stream, streamLen, pos, bytes, 24)) return false;
	memcpy(plain.get() + out, bytes, 24);

	if (!WriteOverdueRecordsPlain(stream, streamLen, pos, plain.get(), snapshot)) {
		strncpy_s(snapshot.reason, "overdue_failed", _TRUNCATE);
		return false;
	}

	if (!WritePostOverdueTailPlain(stream, streamLen, pos, plain.get(), snapshot)) {
		return false;
	}

	if (!SafeCopyRaw(outData, plain.get(), kMsg100LoginPlainLen)) {
		strncpy_s(snapshot.reason, "out_copy_failed", _TRUNCATE);
		return false;
	}
	const int plainLen = kMsg100LoginPlainLen;
	if (!SafeCopyRaw(outLen, &plainLen, sizeof(plainLen))) {
		strncpy_s(snapshot.reason, "out_len_write_failed", _TRUNCATE);
		return false;
	}

	snapshot.ok = true;
	strncpy_s(snapshot.reason, "ok", _TRUNCATE);
	return true;
}
}
