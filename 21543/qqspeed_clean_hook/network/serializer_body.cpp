#include "pch.h"

#include "network/serializer_body.h"

#include "network/code_patch.h"

#include <algorithm>
#include <cstring>

namespace
{
constexpr bool kForceCreateRoomDanceModeForHook103 = false;

unsigned char ReadU8Safe(const void* ptr)
{
	unsigned char value = 0;
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

unsigned int ReadU32Safe(const void* ptr)
{
	unsigned int value = 0;
	if (ptr) {
		qqspeed::network::patch::SafeCopyRaw(&value, ptr, sizeof(value));
	}
	return value;
}

void AppendBE16(std::vector<unsigned char>& out, unsigned short value)
{
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
	out.push_back(static_cast<unsigned char>(value & 0xFF));
}

void AppendBE32(std::vector<unsigned char>& out, unsigned int value)
{
	out.push_back(static_cast<unsigned char>((value >> 24) & 0xFF));
	out.push_back(static_cast<unsigned char>((value >> 16) & 0xFF));
	out.push_back(static_cast<unsigned char>((value >> 8) & 0xFF));
	out.push_back(static_cast<unsigned char>(value & 0xFF));
}

bool PrependBE16TotalLen(std::vector<unsigned char>& out)
{
	const std::size_t totalLen = out.size() + sizeof(unsigned short);
	if (totalLen > 0xFFFF) {
		out.clear();
		return false;
	}

	out.insert(out.begin(), 2, 0);
	out[0] = static_cast<unsigned char>((totalLen >> 8) & 0xFF);
	out[1] = static_cast<unsigned char>(totalLen & 0xFF);
	return true;
}

bool AppendStructBytes(std::vector<unsigned char>& out, const void* srcObject, std::size_t offset, std::size_t count)
{
	if (!srcObject) {
		return false;
	}

	const std::size_t oldSize = out.size();
	out.resize(oldSize + count);
	if (!qqspeed::network::patch::SafeCopyRaw(out.data() + oldSize, static_cast<const unsigned char*>(srcObject) + offset, count)) {
		out.resize(oldSize);
		return false;
	}
	return true;
}

unsigned int ReadStructU32Or(const void* srcObject, std::size_t offset, unsigned int fallback)
{
	if (!srcObject) {
		return fallback;
	}

	const unsigned int value = ReadU32Safe(static_cast<const unsigned char*>(srcObject) + offset);
	return value ? value : fallback;
}

unsigned short ReadStructU16(const void* srcObject, std::size_t offset)
{
	if (!srcObject) {
		return 0;
	}
	return ReadU16Safe(static_cast<const unsigned char*>(srcObject) + offset);
}

unsigned int ReadLikelyUinFromStruct(const void* srcObject, unsigned int fallback)
{
	const unsigned int value = ReadStructU32Or(srcObject, 0, fallback);
	if (value < 1000 || value == 0xFFFFFFFFU) {
		return fallback;
	}
	return value;
}

bool IsUinTimeSerializerMsg(unsigned short msgId)
{
	switch (msgId) {
	case 110:
	case 112:
	case 120:
	case 271:
	case 437:
		return true;
	default:
		return false;
	}
}

int BuildCreateRoomBody(const void* srcObject, std::vector<unsigned char>& out)
{
	constexpr std::size_t kRoomNameOffset = 0x0E;
	constexpr std::size_t kRoomNameLen = 22;
	constexpr std::size_t kSeatNumOffset = 0x24;
	constexpr std::size_t kPasswordOffset = 0x25;
	constexpr std::size_t kPasswordLen = 16;
	constexpr std::size_t kIsNewerOpenOffset = 0x35;
	constexpr std::size_t kMoreInfoNumOffset = 0x3A;
	constexpr std::size_t kCreateRoomTypeOffset = 0x3B;
	constexpr std::size_t kServerMatchTypeOffset = 0x3D;
	constexpr std::size_t kTeamMatchTypeOffset = 0x3F;
	constexpr std::size_t kChatRoomTypeOffset = 0x45;
	constexpr std::size_t kSceneIdOffset = 0x46;
	constexpr std::size_t kPresetObNumOffset = 0x48;
	constexpr std::size_t kAllowAutoStartOffset = 0x49;
	constexpr std::size_t kTalkRoomBgIdOffset = 0x4A;
	constexpr std::size_t kValueNumOffset = 0x4B;
	constexpr std::size_t kValueNumPaddingOffset = 0x4C;
	constexpr std::size_t kValueArrayOffset = 0x4D;
	constexpr unsigned char kMaxCreateRoomValueNum = 32;

	out.clear();
	if (!srcObject) {
		return 0;
	}

	const auto* src = static_cast<const unsigned char*>(srcObject);
	const unsigned char moreInfoNum = ReadU8Safe(src + kMoreInfoNumOffset);
	const unsigned char valueNum = ReadU8Safe(src + kValueNumOffset);
	if (moreInfoNum != 0 || valueNum > kMaxCreateRoomValueNum) {
		return 0;
	}

	unsigned short baseGameMode = ReadStructU16(src, 0x08);
	unsigned short subGameMode = ReadStructU16(src, 0x0A);
	if (kForceCreateRoomDanceModeForHook103) {
		baseGameMode = 4;
		subGameMode = 1;
	}

	AppendBE32(out, ReadU32Safe(src + 0x00));
	AppendBE32(out, ReadU32Safe(src + 0x04));
	AppendBE16(out, baseGameMode);
	AppendBE16(out, subGameMode);
	AppendBE16(out, ReadStructU16(src, 0x0C));
	if (!AppendStructBytes(out, srcObject, kRoomNameOffset, kRoomNameLen)) {
		out.clear();
		return 0;
	}
	out.push_back(ReadU8Safe(src + kSeatNumOffset));
	if (!AppendStructBytes(out, srcObject, kPasswordOffset, kPasswordLen)) {
		out.clear();
		return 0;
	}
	for (std::size_t offset = kIsNewerOpenOffset; offset <= kMoreInfoNumOffset; ++offset) {
		out.push_back(ReadU8Safe(src + offset));
	}
	AppendBE16(out, ReadStructU16(src, kCreateRoomTypeOffset));
	AppendBE16(out, ReadStructU16(src, kServerMatchTypeOffset));
	AppendBE16(out, ReadStructU16(src, kTeamMatchTypeOffset));
	out.push_back(ReadU8Safe(src + kChatRoomTypeOffset));
	AppendBE16(out, ReadStructU16(src, kSceneIdOffset));
	out.push_back(ReadU8Safe(src + kPresetObNumOffset));
	out.push_back(ReadU8Safe(src + kAllowAutoStartOffset));
	out.push_back(ReadU8Safe(src + kTalkRoomBgIdOffset));
	out.push_back(valueNum);
	if (valueNum == 0) {
		out.push_back(ReadU8Safe(src + kValueNumPaddingOffset));
	}
	for (unsigned char i = 0; i < valueNum; ++i) {
		AppendBE32(out, ReadU32Safe(src + kValueArrayOffset + static_cast<std::size_t>(i) * sizeof(unsigned int)));
	}
	if (!PrependBE16TotalLen(out)) {
		return 0;
	}
	return static_cast<int>(out.size());
}

int BuildUinTimeBody(unsigned short msgId, const void* srcObject, unsigned int seq, std::vector<unsigned char>& out)
{
	out.clear();
	if (!srcObject) {
		return 0;
	}

	const unsigned int uin = ReadLikelyUinFromStruct(srcObject, seq);
	if (uin < 1000 || uin == 0xFFFFFFFFU) {
		return 0;
	}

	const unsigned int timeValue = ReadStructU32Or(srcObject, 4, GetTickCount());
	AppendBE32(out, uin);
	AppendBE32(out, timeValue);
	if (msgId == 120) {
		const auto* src = static_cast<const unsigned char*>(srcObject);
		AppendBE32(out, ReadU32Safe(src + 0x08));
		if (!AppendStructBytes(out, srcObject, 0x0C, 32)) {
			out.clear();
			return 0;
		}
		out.push_back(ReadU8Safe(src + 0x2C));
		out.push_back(ReadU8Safe(src + 0x2D));
	}
	else if (msgId == 112) {
		unsigned char tail[24] = {};
		const auto* src = static_cast<const unsigned char*>(srcObject);
		qqspeed::network::patch::SafeCopyRaw(tail, src + 0xD0, sizeof(tail));
		out.insert(out.end(), tail, tail + sizeof(tail));
	}
	if (!PrependBE16TotalLen(out)) {
		return 0;
	}
	return static_cast<int>(out.size());
}

int BuildReportCurrentInfoBody(const void* srcObject, unsigned int seq, std::vector<unsigned char>& out)
{
	constexpr std::size_t kPassedCheckPointNumOffset = 0x18;
	constexpr std::size_t kPassedCheckPointIdOffset = 0x19;
	constexpr std::size_t kLapTimeOffset = 0x145;
	constexpr std::size_t kMsgSequenceOffset = 0x1D6;
	constexpr std::size_t kFlagOffset = 0x76E;
	constexpr std::size_t kSkateDoubleJumpCountOffset = 0x2EB1;
	constexpr std::size_t kCurrMapIdOffset = 0x2EB5;
	constexpr std::size_t kCurrentInfoFlagOffset = 0x3005;
	constexpr std::size_t kTotalAccelerateFuelOffset = 0x3006;
	constexpr std::size_t kDstNpcIdOffset = 0x300A;
	constexpr std::size_t kDistanceToEndOffset = 0x300E;
	constexpr unsigned char kMaxPassedCheckPointNum = 150;

	out.clear();
	if (!srcObject) {
		return 0;
	}

	const auto* src = static_cast<const unsigned char*>(srcObject);
	const unsigned int uin = ReadLikelyUinFromStruct(srcObject, seq);
	if (uin < 1000 || uin == 0xFFFFFFFFU) {
		return 0;
	}

	AppendBE32(out, uin);
	AppendBE32(out, ReadStructU32Or(srcObject, 4, GetTickCount()));
	AppendBE32(out, ReadU32Safe(src + 0x08));
	AppendBE32(out, ReadU32Safe(src + 0x0C));
	AppendBE32(out, ReadU32Safe(src + 0x10));
	AppendBE32(out, ReadU32Safe(src + 0x14));

	unsigned char passedNum = ReadU8Safe(src + kPassedCheckPointNumOffset);
	if (passedNum > kMaxPassedCheckPointNum) {
		passedNum = 0;
	}
	out.push_back(passedNum);
	for (unsigned char i = 0; i < passedNum; ++i) {
		AppendBE16(out, ReadU16Safe(src + kPassedCheckPointIdOffset + static_cast<std::size_t>(i) * sizeof(unsigned short)));
	}

	AppendBE32(out, ReadU32Safe(src + kLapTimeOffset));
	out.push_back(0);
	out.push_back(ReadU8Safe(src + kMsgSequenceOffset));
	out.push_back(0);
	out.push_back(0);
	AppendBE32(out, ReadU32Safe(src + kFlagOffset));
	out.push_back(0);
	out.push_back(0);
	AppendBE32(out, 0);
	AppendBE32(out, 0);
	AppendBE32(out, ReadU32Safe(src + kSkateDoubleJumpCountOffset));
	AppendBE32(out, ReadU32Safe(src + kCurrMapIdOffset));
	out.push_back(0);
	out.push_back(ReadU8Safe(src + kCurrentInfoFlagOffset));
	AppendBE32(out, ReadU32Safe(src + kTotalAccelerateFuelOffset));
	AppendBE32(out, ReadU32Safe(src + kDstNpcIdOffset));
	AppendBE32(out, ReadU32Safe(src + kDistanceToEndOffset));
	if (!PrependBE16TotalLen(out)) {
		return 0;
	}
	return static_cast<int>(out.size());
}

int BuildStartSingleTaskBody(const void* srcObject, unsigned int seq, std::vector<unsigned char>& out)
{
	out.clear();
	if (!srcObject) {
		return 0;
	}

	const auto* src = static_cast<const unsigned char*>(srcObject);
	const unsigned int uin = ReadLikelyUinFromStruct(srcObject, seq);
	if (uin < 1000 || uin == 0xFFFFFFFFU) {
		return 0;
	}

	AppendBE32(out, uin);
	AppendBE32(out, ReadStructU32Or(srcObject, 4, GetTickCount()));
	AppendBE32(out, ReadU32Safe(src + 0x08));
	AppendBE32(out, ReadU32Safe(src + 0x0C));
	AppendBE32(out, ReadU32Safe(src + 0x10));
	if (!PrependBE16TotalLen(out)) {
		return 0;
	}
	return static_cast<int>(out.size());
}

int BuildQuickRpcBody(const void* srcObject, unsigned int seq, std::vector<unsigned char>& out)
{
	constexpr unsigned short kQuickRpcHeadLen = 13;
	constexpr std::size_t kQuickRpcMsgLenOffset = 0x10A;
	constexpr std::size_t kQuickRpcMsgOffset = 0x10C;
	constexpr unsigned short kQuickRpcMaxMsgLen = 0x7D00;

	out.clear();
	if (!srcObject) {
		return 0;
	}

	const unsigned int uin = ReadLikelyUinFromStruct(srcObject, seq);
	if (uin < 1000 || uin == 0xFFFFFFFFU) {
		return 0;
	}

	const unsigned short msgLen = ReadStructU16(srcObject, kQuickRpcMsgLenOffset);
	if (msgLen == 0 || msgLen > kQuickRpcMaxMsgLen) {
		return 0;
	}

	AppendBE16(out, kQuickRpcHeadLen);
	AppendBE32(out, uin);
	AppendBE32(out, ReadStructU32Or(srcObject, 4, GetTickCount()));
	out.push_back(0);
	out.push_back(0);
	out.push_back(0);

	AppendBE16(out, static_cast<unsigned short>(4 + msgLen));
	AppendBE16(out, msgLen);
	if (!AppendStructBytes(out, srcObject, kQuickRpcMsgOffset, msgLen)) {
		out.clear();
		return 0;
	}
	if (!PrependBE16TotalLen(out)) {
		return 0;
	}
	return static_cast<int>(out.size());
}
}

namespace qqspeed::network
{
bool IsSimulatedSerializerBodyMsg(unsigned short msgId)
{
	switch (msgId) {
	// Msg103 must use the native outer28346 -> inner103 path. Keep the
	// handwritten implementation below for reference, but do not register it.
	// case 103:
	case 110:
	case 112:
	case 120:
	case 123:
	case 138:
	case 271:
	case 437:
	case 24251:
		return true;
	default:
		return false;
	}
}

bool BuildSimulatedSerializerBody(
	unsigned short msgId,
	const void* srcObject,
	unsigned int seq,
	std::vector<unsigned char>& out,
	int& outLen)
{
	out.clear();
	outLen = 0;
	if (!srcObject) {
		return false;
	}

	// Disable the handwritten Msg103 serializer. Create-room packets must be
	// produced by the native outer28346 -> inner103 builder, with no plain fallback.
	// if (msgId == 103) {
	// 	outLen = BuildCreateRoomBody(srcObject, out);
	// 	return outLen > 0;
	// }

	if (IsUinTimeSerializerMsg(msgId)) {
		outLen = BuildUinTimeBody(msgId, srcObject, seq, out);
		return outLen > 0;
	}

	if (msgId == 123) {
		outLen = BuildReportCurrentInfoBody(srcObject, seq, out);
		return outLen > 0;
	}

	if (msgId == 138) {
		outLen = BuildStartSingleTaskBody(srcObject, seq, out);
		return outLen > 0;
	}

	if (msgId == 24251) {
		outLen = BuildQuickRpcBody(srcObject, seq, out);
		return outLen > 0;
	}

	return false;
}
}
