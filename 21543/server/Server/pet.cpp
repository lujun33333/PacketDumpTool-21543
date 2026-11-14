#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Message.h"
#include "Pet.h"
#include <array>
#include <map>
#include <algorithm>
#include <memory>
#include <vector>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <string>
#include <filesystem>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include "DBConnection.h"
namespace
{
using Byte = std::uint8_t;
using NodeMap = std::unordered_map<std::uint32_t, YAML::Node>;

class WireWriter
{
public:
	void U8(std::uint64_t value)
	{
		if (value > 0xFFu)
			throw std::runtime_error("wire u8 overflow");
		data_.push_back(static_cast<Byte>(value));
	}

	void U16(std::uint64_t value)
	{
		if (value > 0xFFFFu)
			throw std::runtime_error("wire u16 overflow");
		data_.push_back(static_cast<Byte>((value >> 8) & 0xFFu));
		data_.push_back(static_cast<Byte>(value & 0xFFu));
	}

	void I16(std::int64_t value)
	{
		if (value < -32768 || value > 32767)
			throw std::runtime_error("wire i16 overflow");
		U16(static_cast<std::uint16_t>(static_cast<std::int16_t>(value)));
	}

	void U32(std::uint64_t value)
	{
		if (value > 0xFFFFFFFFull)
			throw std::runtime_error("wire u32 overflow");
		data_.push_back(static_cast<Byte>((value >> 24) & 0xFFu));
		data_.push_back(static_cast<Byte>((value >> 16) & 0xFFu));
		data_.push_back(static_cast<Byte>((value >> 8) & 0xFFu));
		data_.push_back(static_cast<Byte>(value & 0xFFu));
	}

	void I32(std::int64_t value)
	{
		if (value < std::numeric_limits<std::int32_t>::min() ||
			value > std::numeric_limits<std::int32_t>::max())
			throw std::runtime_error("wire i32 overflow");
		U32(static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
	}

	void Bytes(const std::string& value)
	{
		data_.insert(data_.end(), value.begin(), value.end());
	}

	std::size_t BeginRecord()
	{
		const std::size_t start = data_.size();
		U16(0);
		return start;
	}

	void EndRecord(std::size_t start)
	{
		if (start + 2 > data_.size())
			throw std::runtime_error("invalid wire record start");
		const std::size_t length = data_.size() - start;
		if (length < 2 || length > 0xFFFFu)
			throw std::runtime_error("invalid wire record length");
		SetU16(start, static_cast<std::uint16_t>(length));
	}

	std::vector<Byte> FinishSelfLength()
	{
		if (data_.size() < 2 || data_.size() > 0xFFFFu)
			throw std::runtime_error("invalid wire selfLen");
		SetU16(0, static_cast<std::uint16_t>(data_.size()));
		return data_;
	}

private:
	void SetU16(std::size_t offset, std::uint16_t value)
	{
		data_[offset] = static_cast<Byte>((value >> 8) & 0xFFu);
		data_[offset + 1] = static_cast<Byte>(value & 0xFFu);
	}

	std::vector<Byte> data_;
};

std::string Context(const std::string& parent, const std::string& child)
{
	return parent.empty() ? child : parent + "." + child;
}

YAML::Node RequireNode(const YAML::Node& parent, const char* key, const std::string& context)
{
	if (!parent || !parent.IsMap() || !parent[key])
		throw std::runtime_error(Context(context, key) + " is required");
	return parent[key];
}

YAML::Node RequireSequence(const YAML::Node& parent, const char* key, const std::string& context)
{
	YAML::Node node = RequireNode(parent, key, context);
	if (!node.IsSequence())
		throw std::runtime_error(Context(context, key) + " must be a sequence");
	return node;
}

std::uint32_t RequireU32(const YAML::Node& parent, const char* key, const std::string& context)
{
	YAML::Node node = RequireNode(parent, key, context);
	try
	{
		const unsigned long long value = node.as<unsigned long long>();
		if (value > std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error(Context(context, key) + " exceeds u32");
		return static_cast<std::uint32_t>(value);
	}
	catch (const YAML::Exception& e)
	{
		throw std::runtime_error(Context(context, key) + " is not u32: " + e.what());
	}
}

std::uint16_t RequireU16(const YAML::Node& parent, const char* key, const std::string& context)
{
	const std::uint32_t value = RequireU32(parent, key, context);
	if (value > 0xFFFFu)
		throw std::runtime_error(Context(context, key) + " exceeds u16");
	return static_cast<std::uint16_t>(value);
}

std::uint8_t RequireU8(const YAML::Node& parent, const char* key, const std::string& context)
{
	const std::uint32_t value = RequireU32(parent, key, context);
	if (value > 0xFFu)
		throw std::runtime_error(Context(context, key) + " exceeds u8");
	return static_cast<std::uint8_t>(value);
}

std::int16_t RequireI16Scalar(const YAML::Node& node, const std::string& context)
{
	try
	{
		const long long value = node.as<long long>();
		if (value < -32768 || value > 32767)
			throw std::runtime_error(context + " exceeds i16");
		return static_cast<std::int16_t>(value);
	}
	catch (const YAML::Exception& e)
	{
		throw std::runtime_error(context + " is not i16: " + e.what());
	}
}

std::uint32_t RequireU32Scalar(const YAML::Node& node, const std::string& context)
{
	if (!node)
		throw std::runtime_error(context + " is required");
	try
	{
		const unsigned long long value = node.as<unsigned long long>();
		if (value > std::numeric_limits<std::uint32_t>::max())
			throw std::runtime_error(context + " exceeds u32");
		return static_cast<std::uint32_t>(value);
	}
	catch (const YAML::Exception& e)
	{
		throw std::runtime_error(context + " is not u32: " + e.what());
	}
}

bool RequireBool(const YAML::Node& parent, const char* key, const std::string& context)
{
	YAML::Node node = RequireNode(parent, key, context);
	try
	{
		return node.as<bool>();
	}
	catch (const YAML::Exception& e)
	{
		throw std::runtime_error(Context(context, key) + " is not bool: " + e.what());
	}
}

std::string RequireString(const YAML::Node& parent, const char* key, const std::string& context, bool allowEmpty)
{
	YAML::Node node = RequireNode(parent, key, context);
	try
	{
		const std::string value = node.as<std::string>();
		if (!allowEmpty && value.empty())
			throw std::runtime_error(Context(context, key) + " must not be empty");
		return value;
	}
	catch (const YAML::Exception& e)
	{
		throw std::runtime_error(Context(context, key) + " is not a string: " + e.what());
	}
}

std::string Utf8ToGb18030(const std::string& value, const std::string& context)
{
	if (value.empty())
		return std::string();
	const int wideLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
	if (wideLength <= 0)
		throw std::runtime_error(context + " is not valid UTF-8");
	std::wstring wide(static_cast<std::size_t>(wideLength), L'\0');
	if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), wideLength) != wideLength)
		throw std::runtime_error(context + " UTF-8 conversion failed");
	constexpr UINT kGb18030CodePage = 54936;
	const int outputLength = WideCharToMultiByte(kGb18030CodePage, 0, wide.data(), wideLength, nullptr, 0, nullptr, nullptr);
	if (outputLength <= 0)
		throw std::runtime_error(context + " cannot be encoded as GB18030");
	std::string output(static_cast<std::size_t>(outputLength), '\0');
	if (WideCharToMultiByte(kGb18030CodePage, 0, wide.data(), wideLength, output.data(), outputLength, nullptr, nullptr) != outputLength)
		throw std::runtime_error(context + " GB18030 conversion failed");
	return output;
}

void RequireExactCount(const YAML::Node& values, std::size_t expected, const std::string& context)
{
	if (!values || !values.IsSequence() || values.size() != expected)
	{
		std::ostringstream stream;
		stream << context << " must contain " << expected << " values";
		throw std::runtime_error(stream.str());
	}
}

struct RuntimeConfig
{
	bool Ready = false;
	YAML::Node BaseDocument;
	YAML::Node SkillDocument;
	YAML::Node StrengthDocument;
	YAML::Node HuanHuaDocument;
	YAML::Node PKDocument;
	NodeMap BaseByID;
	NodeMap SkillByID;
	NodeMap StrengthByID;
	NodeMap HuanHuaByID;
	NodeMap PKByID;
	std::vector<std::uint32_t> BaseOrder;
	std::vector<std::uint32_t> ShowOrder;
	std::vector<std::uint32_t> StrengthOrder;
	std::vector<std::uint32_t> StrengthDetailOrder;
	std::vector<std::uint32_t> HuanHuaOrder;
	std::vector<std::uint32_t> PKOrder;
	std::vector<std::uint32_t> LevelExperience;
	std::uint32_t StrengthenCardID = 0;
	std::uint32_t StrengthenMaxBatch = 0;
	std::uint32_t StrengthenFailAwardExp = 0;
	std::vector<Byte> StrengthHeader;
	std::vector<std::vector<Byte>> StrengthDetails;
	std::vector<Byte> StrengthPetIDs;
	std::vector<Byte> HuanHuaBody;
	std::vector<Byte> SkillList;
	std::vector<std::vector<Byte>> ShowTemplates;
	std::vector<std::vector<Byte>> PKMore;
	std::vector<Byte> PKGlobalTemplate;
};

RuntimeConfig g_Config;
std::mutex g_ConfigMutex;

std::vector<std::uint32_t> ReadPositiveSizes(const YAML::Node& document, const char* key, const std::string& context)
{
	YAML::Node values = RequireSequence(document, key, context);
	if (values.size() == 0)
		throw std::runtime_error(Context(context, key) + " must not be empty");
	std::vector<std::uint32_t> result;
	result.reserve(values.size());
	for (std::size_t i = 0; i < values.size(); ++i)
	{
		const std::uint32_t value = RequireU32Scalar(values[i], Context(context, key) + "[" + std::to_string(i) + "]");
		if (value == 0)
			throw std::runtime_error(Context(context, key) + " contains zero");
		result.push_back(value);
	}
	return result;
}

void IndexRows(
	const YAML::Node& rows,
	const std::string& context,
	const NodeMap& baseByID,
	bool isBase,
	NodeMap& output,
	std::vector<std::uint32_t>& order)
{
	if (!rows || !rows.IsSequence())
		throw std::runtime_error(context + " must be a sequence");
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const YAML::Node row = rows[i];
		const std::string rowContext = context + "[" + std::to_string(i) + "]";
		const std::uint32_t id = RequireU32(row, "ID", rowContext);
		if (id == 0)
			throw std::runtime_error(rowContext + ".ID must be positive");
		if (!isBase && baseByID.find(id) == baseByID.end())
			throw std::runtime_error(rowContext + ".ID is missing from PetBase");
		if (!output.emplace(id, row).second)
			throw std::runtime_error(context + " contains duplicate ID " + std::to_string(id));
		order.push_back(id);
	}
}

std::uint64_t Sum(const std::vector<std::uint32_t>& values)
{
	std::uint64_t result = 0;
	for (const std::uint32_t value : values)
		result += value;
	return result;
}

void PatchU32(std::vector<Byte>& body, std::size_t offset, std::uint32_t value)
{
	if (offset + 4 > body.size())
		throw std::runtime_error("packet template is too short for UIN");
	body[offset] = static_cast<Byte>((value >> 24) & 0xFFu);
	body[offset + 1] = static_cast<Byte>((value >> 16) & 0xFFu);
	body[offset + 2] = static_cast<Byte>((value >> 8) & 0xFFu);
	body[offset + 3] = static_cast<Byte>(value & 0xFFu);
}
}

namespace
{
void WriteStrengthLevel(WireWriter& writer, const YAML::Node& level, const std::string& context)
{
	const std::size_t start = writer.BeginRecord();
	writer.U32(RequireU32(level, "Level", context));
	writer.U32(RequireU32(level, "SuccProb", context));
	writer.U32(RequireU32(level, "ConsCardNum", context));
	writer.U32(RequireU32(level, "ConsSuperMoney", context));
	YAML::Node skills = RequireSequence(level, "Skills", context);
	writer.U32(skills.size());
	for (std::size_t i = 0; i < skills.size(); ++i)
	{
		const std::string skillContext = Context(context, "Skills") + "[" + std::to_string(i) + "]";
		const std::size_t skillStart = writer.BeginRecord();
		writer.U16(RequireU16(skills[i], "SkillID", skillContext));
		writer.U8(RequireU8(skills[i], "Status", skillContext));
		writer.U8(RequireU8(skills[i], "Active", skillContext));
		writer.U32(RequireU32(skills[i], "Value", skillContext));
		writer.EndRecord(skillStart);
	}
	writer.U8(RequireU8(level, "Type", context));
	for (const char* key : {
		"HuanHuaItemPeriod", "BeginDate", "BeginTime", "BonusProb", "BonusProbBeginDate",
		"BonusProbBeginTime", "BonusProbEndDate", "BonusProbEndTime", "BaojiBeginNum", "BaojiInc" })
	{
		writer.U32(RequireU32(level, key, context));
	}
	writer.EndRecord(start);
}

void WriteStrengthPet(WireWriter& writer, const YAML::Node& pet, const std::string& context)
{
	const std::size_t start = writer.BeginRecord();
	writer.U32(RequireU32(pet, "ID", context));
	YAML::Node levels = RequireSequence(pet, "Levels", context);
	writer.U32(levels.size());
	for (std::size_t i = 0; i < levels.size(); ++i)
		WriteStrengthLevel(writer, levels[i], Context(context, "Levels") + "[" + std::to_string(i) + "]");
	writer.U32(RequireU32(pet, "HuanHuaItemID", context));
	writer.U8(RequireU8(pet, "TailFlag1", context));
	writer.U8(RequireU8(pet, "TailFlag2", context));
	writer.EndRecord(start);
}

std::vector<Byte> BuildStrengthHeader(const RuntimeConfig& config)
{
	WireWriter writer;
	writer.U16(0);
	writer.U32(config.StrengthenCardID);
	writer.U32(0);
	writer.U8(config.StrengthDetailOrder.empty() ? 0 : 1);
	return writer.FinishSelfLength();
}

std::vector<std::vector<Byte>> BuildStrengthDetails(
	const RuntimeConfig& config,
	const std::vector<std::uint32_t>& packetSizes)
{
	if (Sum(packetSizes) != config.StrengthDetailOrder.size())
		throw std::runtime_error("PetStrength.DetailPacketSizes do not cover detailed pets");
	std::vector<std::vector<Byte>> bodies;
	std::size_t offset = 0;
	for (std::size_t packetIndex = 0; packetIndex < packetSizes.size(); ++packetIndex)
	{
		const std::uint32_t count = packetSizes[packetIndex];
		WireWriter writer;
		writer.U16(0);
		writer.U32(count);
		for (std::uint32_t i = 0; i < count; ++i)
		{
			const std::uint32_t petID = config.StrengthDetailOrder[offset + i];
			const auto found = config.StrengthByID.find(petID);
			if (found == config.StrengthByID.end())
				throw std::runtime_error("PetStrength detail ID is not indexed");
			WriteStrengthPet(writer, found->second, "PetStrength.Pets[ID=" + std::to_string(petID) + "]");
		}
		offset += count;
		writer.U8(packetIndex + 1 < packetSizes.size() ? 1 : 0);
		bodies.push_back(writer.FinishSelfLength());
	}
	return bodies;
}

std::vector<Byte> BuildStrengthPetIDs(const RuntimeConfig& config)
{
	WireWriter writer;
	writer.U16(0);
	writer.U32(config.StrengthOrder.size());
	for (const std::uint32_t petID : config.StrengthOrder)
		writer.U32(petID);
	return writer.FinishSelfLength();
}

std::vector<Byte> BuildHuanHua(const RuntimeConfig& config)
{
	const std::uint32_t cardID = RequireU32(config.HuanHuaDocument, "StrengthenCardID", "PetHuanHua");
	WireWriter writer;
	writer.U16(0);
	writer.U32(cardID);
	writer.U32(config.HuanHuaOrder.size());
	for (const std::uint32_t petID : config.HuanHuaOrder)
	{
		const YAML::Node pet = config.HuanHuaByID.at(petID);
		WriteStrengthPet(writer, pet, "PetHuanHua.Pets[ID=" + std::to_string(petID) + "]");
	}
	return writer.FinishSelfLength();
}

void WriteShowPet(WireWriter& writer, const RuntimeConfig& config, std::uint32_t petID)
{
	const YAML::Node baseRow = config.BaseByID.at(petID);
	const YAML::Node show = RequireNode(baseRow, "ShowConfig", "PetBase.Pets[ID=" + std::to_string(petID) + "]");
	if (!show.IsMap())
		throw std::runtime_error("PetBase show row is not a map");
	const YAML::Node skillRow = config.SkillByID.at(petID);
	const std::string context = "PetSkill.Pets[ID=" + std::to_string(petID) + "]";

	const std::size_t petStart = writer.BeginRecord();
	const std::size_t baseStart = writer.BeginRecord();
	writer.U32(petID);
	writer.U32(RequireU32(show, "FullLevel", Context(context, "ShowConfig")));
	writer.U32(RequireU32(show, "StrengthFullLevel", Context(context, "ShowConfig")));
	writer.U16(RequireU16(show, "CanRide", Context(context, "ShowConfig")));
	writer.U16(RequireU16(show, "RidePetType", Context(context, "ShowConfig")));
	YAML::Node infos = RequireSequence(skillRow, "ShowBaseInfos", context);
	writer.U16(infos.size());
	for (std::size_t i = 0; i < infos.size(); ++i)
	{
		const std::string infoContext = Context(context, "ShowBaseInfos") + "[" + std::to_string(i) + "]";
		const std::size_t infoStart = writer.BeginRecord();
		writer.U32(RequireU32(infos[i], "SkillID", infoContext));
		writer.U32(RequireU32(infos[i], "SkillFullLevelValue", infoContext));
		writer.U32(RequireU32(infos[i], "StrengthFullLevelValue", infoContext));
		writer.EndRecord(infoStart);
	}
	writer.EndRecord(baseStart);

	const std::size_t openStart = writer.BeginRecord();
	YAML::Node openSkills = RequireSequence(skillRow, "OpenNewSkills", context);
	writer.U32(openSkills.size() == 0 ? 0u : petID);
	writer.U16(openSkills.size());
	for (std::size_t i = 0; i < openSkills.size(); ++i)
	{
		const std::string openContext = Context(context, "OpenNewSkills") + "[" + std::to_string(i) + "]";
		const std::size_t skillStart = writer.BeginRecord();
		writer.U32(RequireU32(openSkills[i], "OpenLevel", openContext));
		writer.U16(RequireU16(openSkills[i], "SkillID", openContext));
		writer.U32(RequireU32(openSkills[i], "AddValue", openContext));
		writer.EndRecord(skillStart);
	}
	writer.EndRecord(openStart);
	writer.EndRecord(petStart);
}

std::vector<Byte> BuildShowBody(
	const RuntimeConfig& config,
	std::uint32_t uin,
	const std::vector<std::uint32_t>& ids,
	std::size_t offset,
	std::size_t count,
	bool hasNext)
{
	if (offset > ids.size() || count > ids.size() - offset)
		throw std::runtime_error("show packet range is invalid");
	WireWriter writer;
	writer.U16(0);
	writer.U32(uin);
	writer.U16(count);
	for (std::size_t i = 0; i < count; ++i)
		WriteShowPet(writer, config, ids[offset + i]);
	writer.U8(hasNext ? 1 : 0);
	return writer.FinishSelfLength();
}

std::vector<std::vector<Byte>> BuildShowTemplates(
	const RuntimeConfig& config,
	const std::vector<std::uint32_t>& packetSizes)
{
	if (Sum(packetSizes) != config.ShowOrder.size())
		throw std::runtime_error("PetBase.ShowPacketSizes do not cover show pets");
	std::vector<std::vector<Byte>> result;
	std::size_t offset = 0;
	for (std::size_t i = 0; i < packetSizes.size(); ++i)
	{
		const std::size_t count = packetSizes[i];
		result.push_back(BuildShowBody(config, 0, config.ShowOrder, offset, count, i + 1 < packetSizes.size()));
		offset += count;
	}
	return result;
}

std::vector<Byte> BuildSkillList(const RuntimeConfig& config)
{
	WireWriter writer;
	writer.U16(0);
	writer.U32(config.ShowOrder.size());
	for (const std::uint32_t petID : config.ShowOrder)
	{
		const YAML::Node row = config.SkillByID.at(petID);
		const std::string context = "PetSkill.Pets[ID=" + std::to_string(petID) + "]";
		YAML::Node slots = RequireSequence(row, "SkillSlots", context);
		if (slots.size() > 0xFFu)
			throw std::runtime_error(Context(context, "SkillSlots") + " exceeds u8 count");
		const std::size_t start = writer.BeginRecord();
		writer.U32(petID);
		writer.U8(slots.size());
		for (std::size_t i = 0; i < slots.size(); ++i)
		{
			const std::uint32_t skillID = RequireU32Scalar(slots[i], Context(context, "SkillSlots") + "[" + std::to_string(i) + "]");
			writer.U16(skillID);
		}
		writer.EndRecord(start);
	}
	return writer.FinishSelfLength();
}
}

namespace
{
void WritePKPet(WireWriter& writer, const YAML::Node& pet, const std::string& context)
{
	const std::size_t start = writer.BeginRecord();
	writer.U32(RequireU32(pet, "ID", context));
	writer.U8(RequireU8(pet, "ProfType", context));
	writer.U8(RequireU8(pet, "Rarity", context));
	YAML::Node skills = RequireSequence(pet, "Skills", context);
	writer.U32(skills.size());
	for (std::size_t i = 0; i < skills.size(); ++i)
	{
		const std::string skillContext = Context(context, "Skills") + "[" + std::to_string(i) + "]";
		const std::size_t skillStart = writer.BeginRecord();
		writer.U32(RequireU32(skills[i], "SkillID", skillContext));
		writer.U16(RequireU16(skills[i], "TackEffectLevel", skillContext));
		const std::string attach = Utf8ToGb18030(RequireString(skills[i], "Attach", skillContext, true), Context(skillContext, "Attach"));
		writer.U16(attach.size());
		writer.Bytes(attach);
		writer.EndRecord(skillStart);
	}
	writer.U32(RequireU32(pet, "FightPower", context));
	YAML::Node attributes = RequireSequence(pet, "Attributes", context);
	RequireExactCount(attributes, 5, Context(context, "Attributes"));
	for (std::size_t i = 0; i < attributes.size(); ++i)
		writer.U32(RequireU32Scalar(attributes[i], Context(context, "Attributes") + "[" + std::to_string(i) + "]"));
	writer.U8(RequireU8(pet, "HasSpecialPowerSkill", context));
	writer.U32(RequireU32(pet, "MaxFightPower", context));
	writer.EndRecord(start);
}

void WriteImproveLevels(WireWriter& writer, const YAML::Node& rows, const std::string& context)
{
	if (!rows || !rows.IsSequence())
		throw std::runtime_error(context + " must be a sequence");
	writer.U16(rows.size());
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const std::string rowContext = context + "[" + std::to_string(i) + "]";
		const std::size_t start = writer.BeginRecord();
		writer.U32(RequireU32(rows[i], "Level", rowContext));
		writer.U32(RequireU32(rows[i], "ItemID", rowContext));
		writer.U32(RequireU32(rows[i], "ItemNum", rowContext));
		writer.U32(RequireU32(rows[i], "PetCoinNum", rowContext));
		writer.U8(RequireU8(rows[i], "Probability", rowContext));
		writer.EndRecord(start);
	}
}

void WritePotentialGroup(
	WireWriter& writer,
	const YAML::Node& group,
	const char* valueKey,
	const std::string& context)
{
	const std::size_t start = writer.BeginRecord();
	writer.U8(RequireU8(group, "ProfType", context));
	YAML::Node rarities = RequireSequence(group, "Rarities", context);
	RequireExactCount(rarities, 5, Context(context, "Rarities"));
	for (std::size_t i = 0; i < rarities.size(); ++i)
	{
		const std::string rarityContext = Context(context, "Rarities") + "[" + std::to_string(i) + "]";
		const std::size_t rarityStart = writer.BeginRecord();
		writer.U8(RequireU8(rarities[i], "Rarity", rarityContext));
		YAML::Node values = RequireSequence(rarities[i], valueKey, rarityContext);
		RequireExactCount(values, 5, Context(rarityContext, valueKey));
		for (std::size_t j = 0; j < values.size(); ++j)
			writer.I16(RequireI16Scalar(values[j], Context(rarityContext, valueKey) + "[" + std::to_string(j) + "]"));
		writer.EndRecord(rarityStart);
	}
	writer.EndRecord(start);
}

void WriteBattleArrays(WireWriter& writer, const YAML::Node& model, const std::string& context)
{
	const std::size_t rootStart = writer.BeginRecord();
	writer.U8(RequireU8(model, "InitArrayID", context));
	YAML::Node arrays = RequireSequence(model, "Arrays", context);
	if (arrays.size() > 0xFFu)
		throw std::runtime_error(Context(context, "Arrays") + " exceeds u8 count");
	writer.U8(arrays.size());
	for (std::size_t arrayIndex = 0; arrayIndex < arrays.size(); ++arrayIndex)
	{
		const YAML::Node array = arrays[arrayIndex];
		const std::string arrayContext = Context(context, "Arrays") + "[" + std::to_string(arrayIndex) + "]";
		const std::size_t arrayStart = writer.BeginRecord();
		writer.U8(RequireU8(array, "ArrayID", arrayContext));
		const std::string name = Utf8ToGb18030(RequireString(array, "Name", arrayContext, true), Context(arrayContext, "Name"));
		const std::string desc = Utf8ToGb18030(RequireString(array, "Desc", arrayContext, true), Context(arrayContext, "Desc"));
		writer.U8(name.size());
		writer.Bytes(name);
		writer.U16(desc.size());
		writer.Bytes(desc);
		YAML::Node levels = RequireSequence(array, "Levels", arrayContext);
		if (levels.size() > 0xFFu)
			throw std::runtime_error(Context(arrayContext, "Levels") + " exceeds u8 count");
		writer.U8(levels.size());
		for (std::size_t levelIndex = 0; levelIndex < levels.size(); ++levelIndex)
		{
			const YAML::Node level = levels[levelIndex];
			const std::string levelContext = Context(arrayContext, "Levels") + "[" + std::to_string(levelIndex) + "]";
			const std::size_t levelStart = writer.BeginRecord();
			writer.U8(RequireU8(level, "Level", levelContext));
			YAML::Node costs = RequireSequence(level, "CostItems", levelContext);
			if (costs.size() > 0xFFu)
				throw std::runtime_error(Context(levelContext, "CostItems") + " exceeds u8 count");
			writer.U8(costs.size());
			for (std::size_t costIndex = 0; costIndex < costs.size(); ++costIndex)
			{
				const std::string costContext = Context(levelContext, "CostItems") + "[" + std::to_string(costIndex) + "]";
				const std::size_t costStart = writer.BeginRecord();
				writer.U32(RequireU32(costs[costIndex], "ItemID", costContext));
				writer.U32(RequireU32(costs[costIndex], "ConsumeNum", costContext));
				writer.EndRecord(costStart);
			}
			YAML::Node seats = RequireSequence(level, "Seats", levelContext);
			if (seats.size() > 0xFFu)
				throw std::runtime_error(Context(levelContext, "Seats") + " exceeds u8 count");
			writer.U8(seats.size());
			for (std::size_t seatIndex = 0; seatIndex < seats.size(); ++seatIndex)
			{
				const YAML::Node seat = seats[seatIndex];
				const std::string seatContext = Context(levelContext, "Seats") + "[" + std::to_string(seatIndex) + "]";
				const std::size_t seatStart = writer.BeginRecord();
				writer.U8(RequireU8(seat, "SeatID", seatContext));
				YAML::Node effects = RequireSequence(seat, "Effects", seatContext);
				if (effects.size() > 0xFFu)
					throw std::runtime_error(Context(seatContext, "Effects") + " exceeds u8 count");
				writer.U8(effects.size());
				for (std::size_t effectIndex = 0; effectIndex < effects.size(); ++effectIndex)
				{
					const YAML::Node effect = effects[effectIndex];
					const std::string effectContext = Context(seatContext, "Effects") + "[" + std::to_string(effectIndex) + "]";
					const std::size_t effectStart = writer.BeginRecord();
					writer.U8(RequireU8(effect, "ArraySeatEffectType", effectContext));
					YAML::Node paras = RequireSequence(effect, "Paras", effectContext);
					if (paras.size() > 0xFFu)
						throw std::runtime_error(Context(effectContext, "Paras") + " exceeds u8 count");
					writer.U8(paras.size());
					for (std::size_t i = 0; i < paras.size(); ++i)
						writer.U32(RequireU32Scalar(paras[i], Context(effectContext, "Paras") + "[" + std::to_string(i) + "]"));
					YAML::Node factors = RequireSequence(effect, "PetAttrEffectFactors", effectContext);
					RequireExactCount(factors, 5, Context(effectContext, "PetAttrEffectFactors"));
					for (std::size_t i = 0; i < factors.size(); ++i)
						writer.U32(RequireU32Scalar(factors[i], Context(effectContext, "PetAttrEffectFactors") + "[" + std::to_string(i) + "]"));
					writer.U32(RequireU32(effect, "MaxPetAttrEffectFactor", effectContext));
					writer.EndRecord(effectStart);
				}
				writer.U8(RequireU8(seat, "RecomPetType", seatContext));
				writer.EndRecord(seatStart);
			}
			writer.EndRecord(levelStart);
		}
		writer.EndRecord(arrayStart);
	}
	for (const char* key : { "BeginDate", "EndDate", "OpenTime", "CloseTime" })
		writer.U32(RequireU32(model, key, context));
	writer.EndRecord(rootStart);
}

std::vector<Byte> BuildPKGlobal(const RuntimeConfig& config)
{
	const YAML::Node root = config.PKDocument;
	const YAML::Node global = RequireNode(root, "Global", "PetPK");
	const std::uint32_t resultID = RequireU32(root, "ResultID", "PetPK");
	const std::string reason = Utf8ToGb18030(RequireString(root, "Reason", "PetPK", true), "PetPK.Reason");
	const std::uint8_t hasMore = RequireU8(root, "HasMoreInfo", "PetPK");

	WireWriter writer;
	writer.U16(0);
	writer.U32(0); // patched with request UIN
	writer.U16(resultID);
	writer.U16(reason.size());
	writer.Bytes(reason);
	const std::size_t configStart = writer.BeginRecord();

	YAML::Node expItems = RequireSequence(global, "ExpItems", "PetPK.Global");
	writer.U8(expItems.size());
	for (std::size_t i = 0; i < expItems.size(); ++i)
	{
		const std::string context = "PetPK.Global.ExpItems[" + std::to_string(i) + "]";
		const std::size_t start = writer.BeginRecord();
		writer.U32(RequireU32(expItems[i], "ExpItemID", context));
		writer.U32(RequireU32(expItems[i], "PetCoinNum", context));
		writer.EndRecord(start);
	}
	WriteImproveLevels(writer, RequireSequence(global, "StarLevels", "PetPK.Global"), "PetPK.Global.StarLevels");
	WriteImproveLevels(writer, RequireSequence(global, "NormalSkillLevels", "PetPK.Global"), "PetPK.Global.NormalSkillLevels");
	WriteImproveLevels(writer, RequireSequence(global, "PowerSkillLevels", "PetPK.Global"), "PetPK.Global.PowerSkillLevels");

	YAML::Node petConfigs = RequireSequence(global, "PetConfigs", "PetPK.Global");
	writer.U16(petConfigs.size());
	for (std::size_t i = 0; i < petConfigs.size(); ++i)
		WritePKPet(writer, petConfigs[i], "PetPK.Global.PetConfigs[" + std::to_string(i) + "]");

	YAML::Node skillConfigs = RequireSequence(global, "PetSkillConfigs", "PetPK.Global");
	writer.U16(skillConfigs.size());
	for (std::size_t i = 0; i < skillConfigs.size(); ++i)
	{
		const std::string context = "PetPK.Global.PetSkillConfigs[" + std::to_string(i) + "]";
		const std::size_t start = writer.BeginRecord();
		writer.U32(RequireU32(skillConfigs[i], "SkillID", context));
		writer.U8(RequireU8(skillConfigs[i], "Type", context));
		writer.U16(RequireU16(skillConfigs[i], "SkillStar", context));
		writer.EndRecord(start);
	}

	YAML::Node wake = RequireNode(global, "SkillWakeUp", "PetPK.Global");
	const std::size_t wakeStart = writer.BeginRecord();
	writer.U32(RequireU32(wake, "ItemID", "PetPK.Global.SkillWakeUp"));
	writer.U32(RequireU32(wake, "ItemNum", "PetPK.Global.SkillWakeUp"));
	writer.U16(RequireU16(wake, "FightLevel", "PetPK.Global.SkillWakeUp"));
	writer.EndRecord(wakeStart);

	YAML::Node attributePotentials = RequireSequence(global, "AttributePotentials", "PetPK.Global");
	RequireExactCount(attributePotentials, 5, "PetPK.Global.AttributePotentials");
	for (std::size_t i = 0; i < attributePotentials.size(); ++i)
		WritePotentialGroup(writer, attributePotentials[i], "PotentialAttributes", "PetPK.Global.AttributePotentials[" + std::to_string(i) + "]");

	YAML::Node starPotentials = RequireSequence(global, "StarPotentials", "PetPK.Global");
	RequireExactCount(starPotentials, 5, "PetPK.Global.StarPotentials");
	for (std::size_t i = 0; i < starPotentials.size(); ++i)
	{
		const std::string context = "PetPK.Global.StarPotentials[" + std::to_string(i) + "]";
		const std::size_t start = writer.BeginRecord();
		writer.U8(RequireU8(starPotentials[i], "Rarity", context));
		YAML::Node values = RequireSequence(starPotentials[i], "StarAttributes", context);
		RequireExactCount(values, 2, Context(context, "StarAttributes"));
		for (std::size_t j = 0; j < values.size(); ++j)
			writer.I16(RequireI16Scalar(values[j], Context(context, "StarAttributes") + "[" + std::to_string(j) + "]"));
		writer.EndRecord(start);
	}

	YAML::Node attributeConfigs = RequireSequence(global, "AttributeConfigs", "PetPK.Global");
	RequireExactCount(attributeConfigs, 5, "PetPK.Global.AttributeConfigs");
	for (std::size_t i = 0; i < attributeConfigs.size(); ++i)
		WritePotentialGroup(writer, attributeConfigs[i], "BaseAttributes", "PetPK.Global.AttributeConfigs[" + std::to_string(i) + "]");

	YAML::Node embattle = RequireNode(global, "Embattle", "PetPK.Global");
	const std::size_t embattleStart = writer.BeginRecord();
	for (const char* key : { "HealthAddition", "FrontRowDefenseAddition", "BackRowAttackAddition", "DamageTreatAddition" })
	{
		YAML::Node values = RequireSequence(embattle, key, "PetPK.Global.Embattle");
		RequireExactCount(values, 10, Context("PetPK.Global.Embattle", key));
		for (std::size_t i = 0; i < values.size(); ++i)
			writer.U32(RequireU32Scalar(values[i], Context("PetPK.Global.Embattle", key) + "[" + std::to_string(i) + "]"));
	}
	writer.U32(RequireU32(embattle, "EmbattlePetNum", "PetPK.Global.Embattle"));
	writer.EndRecord(embattleStart);

	YAML::Node motivate = RequireSequence(global, "AttributeMotivateLevels", "PetPK.Global");
	writer.U16(motivate.size());
	for (std::size_t i = 0; i < motivate.size(); ++i)
	{
		const std::string context = "PetPK.Global.AttributeMotivateLevels[" + std::to_string(i) + "]";
		const std::size_t start = writer.BeginRecord();
		for (const char* key : { "Level", "ItemID", "ItemNum", "PetCoinNum", "AttrInc" })
			writer.U32(RequireU32(motivate[i], key, context));
		writer.EndRecord(start);
	}
	WriteImproveLevels(writer, RequireSequence(global, "StarRefreshLevels", "PetPK.Global"), "PetPK.Global.StarRefreshLevels");
	WriteBattleArrays(writer, RequireNode(global, "BattleArrays", "PetPK.Global"), "PetPK.Global.BattleArrays");

	writer.EndRecord(configStart);
	writer.U8(hasMore);
	return writer.FinishSelfLength();
}

std::vector<std::vector<Byte>> BuildPKMore(
	const RuntimeConfig& config,
	const std::vector<std::uint32_t>& packetSizes)
{
	if (Sum(packetSizes) != config.PKOrder.size())
		throw std::runtime_error("PetPK.PetPacketSizes do not cover PK pets");
	std::vector<std::vector<Byte>> result;
	std::size_t offset = 0;
	for (std::size_t packetIndex = 0; packetIndex < packetSizes.size(); ++packetIndex)
	{
		const std::size_t count = packetSizes[packetIndex];
		WireWriter writer;
		writer.U16(0);
		writer.U16(count);
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::uint32_t petID = config.PKOrder[offset + i];
			WritePKPet(writer, config.PKByID.at(petID), "PetPK.Pets[ID=" + std::to_string(petID) + "]");
		}
		offset += count;
		writer.U8(packetIndex + 1 < packetSizes.size() ? 1 : 0);
		result.push_back(writer.FinishSelfLength());
	}
	return result;
}
}

namespace
{
RuntimeConfig LoadValidated(const std::string& petDirectory)
{
	if (petDirectory.empty())
		throw std::runtime_error("pet directory is empty");
	const std::filesystem::path directory = std::filesystem::u8path(petDirectory);
	if (!std::filesystem::is_directory(directory))
		throw std::runtime_error("pet directory does not exist: " + petDirectory);

	const std::set<std::string> expectedFiles = {
		"PetBase.yml", "PetSkill.yml", "PetStrength.yml", "PetHuanHua.yml", "PetPK.yml"
	};
	std::set<std::string> actualFiles;
	for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory))
	{
		if (entry.is_regular_file() && entry.path().extension() == ".yml")
			actualFiles.insert(entry.path().filename().u8string());
	}
	if (actualFiles != expectedFiles)
	{
		std::ostringstream stream;
		stream << "pet directory must contain exactly five yml tables; found=";
		bool first = true;
		for (const std::string& name : actualFiles)
		{
			if (!first) stream << ',';
			stream << name;
			first = false;
		}
		throw std::runtime_error(stream.str());
	}

	RuntimeConfig config;
	config.BaseDocument = YAML::LoadFile((directory / "PetBase.yml").string());
	config.SkillDocument = YAML::LoadFile((directory / "PetSkill.yml").string());
	config.StrengthDocument = YAML::LoadFile((directory / "PetStrength.yml").string());
	config.HuanHuaDocument = YAML::LoadFile((directory / "PetHuanHua.yml").string());
	config.PKDocument = YAML::LoadFile((directory / "PetPK.yml").string());

	IndexRows(
		RequireSequence(config.BaseDocument, "Pets", "PetBase"),
		"PetBase.Pets", NodeMap(), true, config.BaseByID, config.BaseOrder);
	if (config.BaseOrder.empty())
		throw std::runtime_error("PetBase.Pets must not be empty");
	for (const std::uint32_t petID : config.BaseOrder)
	{
		const YAML::Node row = config.BaseByID.at(petID);
		YAML::Node show = row["ShowConfig"];
		if (show && !show.IsNull())
		{
			if (!show.IsMap())
				throw std::runtime_error("PetBase ID " + std::to_string(petID) + " ShowConfig must be map or null");
			config.ShowOrder.push_back(petID);
		}
	}

	YAML::Node levels = RequireSequence(config.BaseDocument, "LevelExperience", "PetBase");
	if (levels.size() == 0)
		throw std::runtime_error("PetBase.LevelExperience must not be empty");
	std::uint32_t previousExp = 0;
	for (std::size_t i = 0; i < levels.size(); ++i)
	{
		const std::string context = "PetBase.LevelExperience[" + std::to_string(i) + "]";
		const std::uint32_t level = RequireU32(levels[i], "Level", context);
		const std::uint32_t experience = RequireU32(levels[i], "Exp", context);
		if (level != i + 1)
			throw std::runtime_error(context + ".Level must be contiguous from 1");
		if ((i == 0 && experience != 0) || (i > 0 && experience <= previousExp))
			throw std::runtime_error(context + ".Exp must be strictly increasing after level 1");
		config.LevelExperience.push_back(experience);
		previousExp = experience;
	}

	std::vector<std::uint32_t> skillOrder;
	IndexRows(
		RequireSequence(config.SkillDocument, "Pets", "PetSkill"),
		"PetSkill.Pets", config.BaseByID, false, config.SkillByID, skillOrder);
	if (config.ShowOrder != skillOrder)
		throw std::runtime_error("PetSkill order must exactly match PetBase show order");

	IndexRows(
		RequireSequence(config.StrengthDocument, "Pets", "PetStrength"),
		"PetStrength.Pets", config.BaseByID, false, config.StrengthByID, config.StrengthOrder);
	YAML::Node orderNode = RequireSequence(config.StrengthDocument, "PetOrder", "PetStrength");
	if (orderNode.size() != config.StrengthOrder.size())
		throw std::runtime_error("PetStrength.PetOrder count differs from Pets");
	for (std::size_t i = 0; i < orderNode.size(); ++i)
	{
		const std::uint32_t id = RequireU32Scalar(orderNode[i], "PetStrength.PetOrder[" + std::to_string(i) + "]");
		if (id != config.StrengthOrder[i])
			throw std::runtime_error("PetStrength.PetOrder differs from Pets order");
	}
	for (const std::uint32_t petID : config.StrengthOrder)
	{
		const YAML::Node row = config.StrengthByID.at(petID);
		const bool hasDetail = RequireBool(row, "HasDetail", "PetStrength.Pets[ID=" + std::to_string(petID) + "]");
		YAML::Node detailLevels = RequireSequence(row, "Levels", "PetStrength.Pets[ID=" + std::to_string(petID) + "]");
		if (hasDetail)
		{
			if (detailLevels.size() == 0)
				throw std::runtime_error("PetStrength detailed pet has no Levels");
		}
		else if (detailLevels.size() != 0)
		{
			throw std::runtime_error("PetStrength non-detailed pet contains Levels");
		}
	}
	YAML::Node detailOrderNode = RequireSequence(config.StrengthDocument, "DetailPetOrder", "PetStrength");
	std::set<std::uint32_t> detailedIDs;
	for (const std::uint32_t petID : config.StrengthOrder)
	{
		const YAML::Node row = config.StrengthByID.at(petID);
		if (RequireBool(row, "HasDetail", "PetStrength.Pets[ID=" + std::to_string(petID) + "]"))
			detailedIDs.insert(petID);
	}
	for (std::size_t i = 0; i < detailOrderNode.size(); ++i)
	{
		const std::uint32_t id = RequireU32Scalar(detailOrderNode[i], "PetStrength.DetailPetOrder[" + std::to_string(i) + "]");
		if (detailedIDs.erase(id) != 1)
			throw std::runtime_error("PetStrength.DetailPetOrder contains a duplicate or non-detailed ID");
		config.StrengthDetailOrder.push_back(id);
	}
	if (!detailedIDs.empty())
		throw std::runtime_error("PetStrength.DetailPetOrder is missing detailed IDs");
	config.StrengthenCardID = RequireU32(config.StrengthDocument, "StrengthenCardID", "PetStrength");
	if (config.StrengthenCardID == 0)
		throw std::runtime_error("PetStrength.StrengthenCardID must be positive");
	{
		const std::filesystem::path rulesPath = directory.parent_path() / "Config" / "Pet.yml";
		const YAML::Node runtimeRules = YAML::LoadFile(rulesPath.string());
		const YAML::Node strengthenRule = RequireNode(runtimeRules, "StrengthenRule", "Config.Pet");
		const YAML::Node petItems = RequireSequence(runtimeRules, "PetItem", "Config.Pet");
		if (petItems.size() == 0)
			throw std::runtime_error("Config.Pet.PetItem must not be empty");
		const std::uint32_t configuredCardID = RequireU32(petItems[0], "ItemId", "Config.Pet.PetItem[0]");
		if (configuredCardID != config.StrengthenCardID)
			throw std::runtime_error("pet strengthen card ID differs between PetStrength.yml and Config/Pet.yml");
		config.StrengthenMaxBatch = RequireU32(strengthenRule, "MaxBatch", "Config.Pet.StrengthenRule");
		config.StrengthenFailAwardExp = RequireU32(strengthenRule, "FailAwardExp", "Config.Pet.StrengthenRule");
		if (config.StrengthenMaxBatch == 0 || config.StrengthenMaxBatch > 255)
			throw std::runtime_error("Config.Pet.StrengthenRule.MaxBatch must be in 1..255");
	}

	IndexRows(
		RequireSequence(config.HuanHuaDocument, "Pets", "PetHuanHua"),
		"PetHuanHua.Pets", config.BaseByID, false, config.HuanHuaByID, config.HuanHuaOrder);
	IndexRows(
		RequireSequence(config.PKDocument, "Pets", "PetPK"),
		"PetPK.Pets", config.BaseByID, false, config.PKByID, config.PKOrder);

	const std::vector<std::uint32_t> showPacketSizes = ReadPositiveSizes(config.BaseDocument, "ShowPacketSizes", "PetBase");
	const std::vector<std::uint32_t> detailPacketSizes = ReadPositiveSizes(config.StrengthDocument, "DetailPacketSizes", "PetStrength");
	const std::vector<std::uint32_t> pkPacketSizes = ReadPositiveSizes(config.PKDocument, "PetPacketSizes", "PetPK");
	config.StrengthHeader = BuildStrengthHeader(config);
	config.StrengthDetails = BuildStrengthDetails(config, detailPacketSizes);
	config.StrengthPetIDs = BuildStrengthPetIDs(config);
	config.HuanHuaBody = BuildHuanHua(config);
	config.ShowTemplates = BuildShowTemplates(config, showPacketSizes);
	config.SkillList = BuildSkillList(config);
	config.PKGlobalTemplate = BuildPKGlobal(config);
	config.PKMore = BuildPKMore(config, pkPacketSizes);
	config.Ready = true;
	return config;
}

YAML::Node FindNode(const NodeMap& rows, std::uint32_t petID)
{
	const auto found = rows.find(petID);
	return found == rows.end() ? YAML::Node() : found->second;
}

const std::vector<std::uint32_t>& EmptyIDs()
{
	static const std::vector<std::uint32_t> empty;
	return empty;
}

const std::vector<Byte>& EmptyBody()
{
	static const std::vector<Byte> empty;
	return empty;
}

const std::vector<std::vector<Byte>>& EmptyBodies()
{
	static const std::vector<std::vector<Byte>> empty;
	return empty;
}
}

namespace QQSPetConfig
{
bool Load(const std::string& petDirectory, std::string& error)
{
	try
	{
		RuntimeConfig loaded = LoadValidated(petDirectory);
		std::lock_guard<std::mutex> lock(g_ConfigMutex);
		g_Config = std::move(loaded);
		error.clear();
		return true;
	}
	catch (const std::exception& e)
	{
		error = e.what();
		return false;
	}
}

bool IsReady() { return g_Config.Ready; }
bool HasPet(std::uint32_t petID) { return g_Config.Ready && g_Config.BaseByID.find(petID) != g_Config.BaseByID.end(); }
bool HasShow(std::uint32_t petID) { return g_Config.Ready && g_Config.SkillByID.find(petID) != g_Config.SkillByID.end(); }
bool HasStrength(std::uint32_t petID) { return g_Config.Ready && g_Config.StrengthByID.find(petID) != g_Config.StrengthByID.end(); }
bool HasHuanHua(std::uint32_t petID) { return g_Config.Ready && g_Config.HuanHuaByID.find(petID) != g_Config.HuanHuaByID.end(); }
bool HasPK(std::uint32_t petID) { return g_Config.Ready && g_Config.PKByID.find(petID) != g_Config.PKByID.end(); }

YAML::Node Base(std::uint32_t petID) { return g_Config.Ready ? FindNode(g_Config.BaseByID, petID) : YAML::Node(); }
YAML::Node SkillConfig(std::uint32_t petID) { return g_Config.Ready ? FindNode(g_Config.SkillByID, petID) : YAML::Node(); }
YAML::Node Strength(std::uint32_t petID) { return g_Config.Ready ? FindNode(g_Config.StrengthByID, petID) : YAML::Node(); }
YAML::Node HuanHua(std::uint32_t petID) { return g_Config.Ready ? FindNode(g_Config.HuanHuaByID, petID) : YAML::Node(); }
YAML::Node PK(std::uint32_t petID) { return g_Config.Ready ? FindNode(g_Config.PKByID, petID) : YAML::Node(); }
YAML::Node PKGlobal() { return g_Config.Ready ? RequireNode(g_Config.PKDocument, "Global", "PetPK") : YAML::Node(); }

const std::vector<std::uint32_t>& BaseOrder() { return g_Config.Ready ? g_Config.BaseOrder : EmptyIDs(); }
const std::vector<std::uint32_t>& ShowOrder() { return g_Config.Ready ? g_Config.ShowOrder : EmptyIDs(); }
const std::vector<std::uint32_t>& StrengthOrder() { return g_Config.Ready ? g_Config.StrengthOrder : EmptyIDs(); }
const std::vector<std::uint32_t>& StrengthDetailOrder() { return g_Config.Ready ? g_Config.StrengthDetailOrder : EmptyIDs(); }
const std::vector<std::uint32_t>& HuanHuaOrder() { return g_Config.Ready ? g_Config.HuanHuaOrder : EmptyIDs(); }
const std::vector<std::uint32_t>& PKOrder() { return g_Config.Ready ? g_Config.PKOrder : EmptyIDs(); }

bool GetName(std::uint32_t petID, std::string& utf8Name)
{
	utf8Name.clear();
	const YAML::Node row = Base(petID);
	if (!row || !row["Name"] || row["Name"].IsNull())
		return false;
	try
	{
		utf8Name = row["Name"].as<std::string>();
		return !utf8Name.empty();
	}
	catch (const YAML::Exception&)
	{
		utf8Name.clear();
		return false;
	}
}

bool GetBaseSkills(std::uint32_t petID, std::vector<Skill>& skills)
{
	skills.clear();
	const YAML::Node row = SkillConfig(petID);
	if (!row)
		return HasPet(petID);
	try
	{
		YAML::Node infos = RequireSequence(row, "ShowBaseInfos", "PetSkill.Pets[ID=" + std::to_string(petID) + "]");
		for (std::size_t i = 0; i < infos.size(); ++i)
		{
			const std::string context = "PetSkill.Pets[ID=" + std::to_string(petID) + "].ShowBaseInfos[" + std::to_string(i) + "]";
			const std::uint32_t skillID = RequireU32(infos[i], "SkillID", context);
			if (skillID == 0)
				continue;
			if (skillID > 0xFFFFu || i + 1 > 0xFFu)
				return false;
			Skill skill;
			skill.SkillID = static_cast<std::uint16_t>(skillID);
			skill.Status = static_cast<std::uint8_t>(i + 1);
			skill.Active = 1;
			skill.Value = RequireU32(infos[i], "SkillFullLevelValue", context);
			skills.push_back(skill);
		}
		return true;
	}
	catch (const std::exception&)
	{
		skills.clear();
		return false;
	}
}

bool GetLevelForExperience(std::uint32_t experience, std::uint32_t& level)
{
	level = 0;
	if (!g_Config.Ready || g_Config.LevelExperience.empty())
		return false;
	const auto upper = std::upper_bound(g_Config.LevelExperience.begin(), g_Config.LevelExperience.end(), experience);
	level = static_cast<std::uint32_t>(std::distance(g_Config.LevelExperience.begin(), upper));
	if (level == 0)
		return false;
	if (level > g_Config.LevelExperience.size())
		level = static_cast<std::uint32_t>(g_Config.LevelExperience.size());
	return true;
}

bool GetMaxExperience(std::uint32_t level, std::uint32_t& maxExperience)
{
	maxExperience = 0;
	if (!g_Config.Ready || level == 0 || level > g_Config.LevelExperience.size())
		return false;
	const std::size_t index = level < g_Config.LevelExperience.size() ? level : level - 1;
	maxExperience = g_Config.LevelExperience[index];
	return true;
}

std::uint32_t MaxLevel()
{
	return g_Config.Ready ? static_cast<std::uint32_t>(g_Config.LevelExperience.size()) : 0;
}

bool GetProtocolCapabilities(
	std::uint32_t petID,
	std::uint8_t& canHuanHua,
	std::uint8_t& canDrive)
{
	canHuanHua = canDrive = 0;
	const YAML::Node row = Base(petID);
	if (!row)
		return false;
	try
	{
		const YAML::Node show = row["ShowConfig"];
		if (!show || !show.IsMap())
			return false;
		canDrive = RequireU8(show, "CanRide", "PetBase runtime ShowConfig");
		canHuanHua = HasHuanHua(petID) ? 1 : 0;
		return true;
	}
	catch (const std::exception&)
	{
		canHuanHua = canDrive = 0;
		return false;
	}
}

bool GetStrengthFullLevel(std::uint32_t petID, std::uint32_t& fullLevel)
{
	fullLevel = 0;
	const YAML::Node row = Strength(petID);
	if (!row || !row["HasDetail"] || !row["HasDetail"].as<bool>())
		return false;
	YAML::Node levels = row["Levels"];
	if (!levels || !levels.IsSequence() || levels.size() == 0)
		return false;
	for (std::size_t i = 0; i < levels.size(); ++i)
		fullLevel = (std::max)(fullLevel, RequireU32(levels[i], "Level", "PetStrength runtime"));
	return fullLevel != 0;
}

bool GetStrengthLevelRule(
	std::uint32_t petID,
	std::uint32_t level,
	std::uint32_t& succProb,
	std::uint32_t& consCardNum,
	std::uint32_t& consSuperMoney)
{
	succProb = consCardNum = consSuperMoney = 0;
	const YAML::Node row = Strength(petID);
	if (!row || !row["HasDetail"] || !row["HasDetail"].as<bool>())
		return false;
	const YAML::Node levels = row["Levels"];
	if (!levels || !levels.IsSequence())
		return false;

	try
	{
		for (std::size_t i = 0; i < levels.size(); ++i)
		{
			if (RequireU32(levels[i], "Level", "PetStrength runtime") != level)
				continue;
			succProb = RequireU32(levels[i], "SuccProb", "PetStrength runtime");
			consCardNum = RequireU32(levels[i], "ConsCardNum", "PetStrength runtime");
			consSuperMoney = RequireU32(levels[i], "ConsSuperMoney", "PetStrength runtime");
			return succProb <= 10000;
		}
	}
	catch (const std::exception&)
	{
		succProb = consCardNum = consSuperMoney = 0;
	}
	return false;
}

std::uint32_t StrengthenCardID()
{
	return g_Config.Ready ? g_Config.StrengthenCardID : 0;
}

bool GetStrengthRuntimeRules(
	std::uint32_t& cardID,
	std::uint32_t& maxBatch,
	std::uint32_t& failAwardExp)
{
	cardID = maxBatch = failAwardExp = 0;
	if (!g_Config.Ready || g_Config.StrengthenCardID == 0 || g_Config.StrengthenMaxBatch == 0)
		return false;
	cardID = g_Config.StrengthenCardID;
	maxBatch = g_Config.StrengthenMaxBatch;
	failAwardExp = g_Config.StrengthenFailAwardExp;
	return true;
}

bool GetStrengthBonuses(
	std::uint32_t petID,
	std::uint32_t level,
	std::map<std::uint32_t, std::uint32_t>& bonuses)
{
	bonuses.clear();
	const YAML::Node row = Strength(petID);
	if (!row || !row["HasDetail"] || !row["HasDetail"].as<bool>())
		return false;
	YAML::Node levels = row["Levels"];
	if (!levels || !levels.IsSequence())
		return false;
	for (std::size_t i = 0; i < levels.size(); ++i)
	{
		const std::uint32_t configuredLevel = RequireU32(levels[i], "Level", "PetStrength runtime");
		if (configuredLevel > level)
			break;
		YAML::Node skills = RequireSequence(levels[i], "Skills", "PetStrength runtime");
		for (std::size_t j = 0; j < skills.size(); ++j)
			bonuses[RequireU32(skills[j], "SkillID", "PetStrength runtime")] += RequireU32(skills[j], "Value", "PetStrength runtime");
	}
	return true;
}

const std::vector<std::uint8_t>& StrengthHeaderBody() { return g_Config.Ready ? g_Config.StrengthHeader : EmptyBody(); }
const std::vector<std::vector<std::uint8_t>>& StrengthDetailBodies() { return g_Config.Ready ? g_Config.StrengthDetails : EmptyBodies(); }
const std::vector<std::uint8_t>& StrengthPetIDBody() { return g_Config.Ready ? g_Config.StrengthPetIDs : EmptyBody(); }
const std::vector<std::uint8_t>& HuanHuaBody() { return g_Config.Ready ? g_Config.HuanHuaBody : EmptyBody(); }
const std::vector<std::uint8_t>& SkillListBody() { return g_Config.Ready ? g_Config.SkillList : EmptyBody(); }
const std::vector<std::vector<std::uint8_t>>& PKMoreBodies() { return g_Config.Ready ? g_Config.PKMore : EmptyBodies(); }

bool BuildShowPackets(std::uint32_t uin, std::uint32_t petID, std::vector<StaticPacket>& packets, std::string& error)
{
	packets.clear();
	try
	{
		if (!g_Config.Ready)
			throw std::runtime_error("pet runtime config is not ready");
		if (petID != 0)
		{
			if (!HasShow(petID))
				throw std::runtime_error("PetID " + std::to_string(petID) + " has no show configuration");
			StaticPacket packet;
			packet.MsgID = 24426;
			packet.Body = BuildShowBody(g_Config, uin, std::vector<std::uint32_t>{ petID }, 0, 1, false);
			packets.push_back(std::move(packet));
		}
		else
		{
			for (std::size_t i = 0; i < g_Config.ShowTemplates.size(); ++i)
			{
				StaticPacket packet;
				packet.MsgID = i == 0 ? 24426 : 24527;
				packet.Body = g_Config.ShowTemplates[i];
				PatchU32(packet.Body, 2, uin);
				packets.push_back(std::move(packet));
			}
		}
		error.clear();
		return true;
	}
	catch (const std::exception& e)
	{
		error = e.what();
		packets.clear();
		return false;
	}
}

bool BuildPKGlobalBody(std::uint32_t uin, std::vector<std::uint8_t>& body, std::string& error)
{
	body.clear();
	try
	{
		if (!g_Config.Ready || g_Config.PKGlobalTemplate.empty())
			throw std::runtime_error("pet PK runtime config is not ready");
		body = g_Config.PKGlobalTemplate;
		PatchU32(body, 2, uin);
		error.clear();
		return true;
	}
	catch (const std::exception& e)
	{
		error = e.what();
		body.clear();
		return false;
	}
}
}


std::map<UINT, UINT> Levels;
std::map<UINT, UINT> PetStrengs;
std::map<UINT, UINT> PetHuanHuas;
std::vector<UINT> PetStrengOrder;
std::vector<UINT> PetStrengDetailOrder;

extern std::string GetItemName(UINT Itemid);

static std::mutex g_QQSYamlCacheMutex;
static std::map<std::string, YAML::Node> g_QQSYamlNodeCache;
static std::map<std::string, bool> g_QQSYamlFailedCache;

static std::mutex g_QQSPetPKHexCacheMutex;
static std::map<std::string, std::vector<BYTE>> g_QQSPetPKHexBodyCache;
static std::map<std::string, std::vector<std::vector<BYTE>>> g_QQSPetPKHexBodyListCache;
static std::map<std::string, bool> g_QQSPetPKHexFailedCache;

struct QQSPKFormationState
{
	std::array<UINT, 10> IDs{};
	UINT CaptainPetID = 0;
	UCHAR UsingArrayID = 0;
};

enum class QQSPKFormationLoadResult
{
	Loaded,
	Missing,
	Error
};

static std::mutex g_QQSPKFormationMutex;
static std::unordered_map<UINT, QQSPKFormationState> g_QQSPKFormationByUin;
static std::set<UINT> g_QQSMissingPKFormationUins;

static std::string QQS_EncodePKFormationIDs(const std::array<UINT, 10>& ids)
{
	static const char* digits = "0123456789ABCDEF";
	std::string hex;
	hex.reserve(80);
	for (const UINT petID : ids)
	{
		for (int shift = 28; shift >= 0; shift -= 4)
			hex.push_back(digits[(petID >> shift) & 0x0Fu]);
	}
	return hex;
}

static bool QQS_DecodePKFormationIDs(const std::string& hex, std::array<UINT, 10>& ids)
{
	if (hex.size() != 80)
		return false;
	ids.fill(0);
	for (size_t i = 0; i < ids.size(); ++i)
	{
		UINT value = 0;
		for (size_t j = 0; j < 8; ++j)
		{
			const char ch = hex[i * 8 + j];
			const int nibble =
				(ch >= '0' && ch <= '9') ? ch - '0' :
				(ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 :
				(ch >= 'a' && ch <= 'f') ? ch - 'a' + 10 : -1;
			if (nibble < 0)
				return false;
			value = (value << 4) | static_cast<UINT>(nibble);
		}
		ids[i] = value;
	}
	return true;
}

static QQSPKFormationLoadResult QQS_LoadPKFormationState(UINT uin, QQSPKFormationState& state)
{
	{
		std::lock_guard<std::mutex> lock(g_QQSPKFormationMutex);
		const auto it = g_QQSPKFormationByUin.find(uin);
		if (it != g_QQSPKFormationByUin.end())
		{
			state = it->second;
			return QQSPKFormationLoadResult::Loaded;
		}
		if (g_QQSMissingPKFormationUins.find(uin) != g_QQSMissingPKFormationUins.end())
			return QQSPKFormationLoadResult::Missing;
	}

	DBConnection connection(player);
	auto conn = connection.getConnection();
	if (!conn)
		return QQSPKFormationLoadResult::Error;
	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(conn->prepareStatement(
			"SELECT IDsHex,CaptainPetID,UsingArrayID "
			"FROM PetPKFormation WHERE Uin=? LIMIT 1;"));
		statement->setUInt(1, uin);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		if (!result->next())
		{
			std::lock_guard<std::mutex> lock(g_QQSPKFormationMutex);
			g_QQSMissingPKFormationUins.insert(uin);
			return QQSPKFormationLoadResult::Missing;
		}

		QQSPKFormationState loaded{};
		const UINT usingArrayID = result->getUInt("UsingArrayID");
		if (usingArrayID == 0 || usingArrayID > 0xFFu ||
			!QQS_DecodePKFormationIDs(result->getString("IDsHex"), loaded.IDs))
		{
			QQS_DebugPrintf10001("[PET-PK-ARRAY] invalid persisted formation Uin=%u\n", uin);
			return QQSPKFormationLoadResult::Error;
		}
		loaded.CaptainPetID = result->getUInt("CaptainPetID");
		loaded.UsingArrayID = static_cast<UCHAR>(usingArrayID);
		{
			std::lock_guard<std::mutex> lock(g_QQSPKFormationMutex);
			g_QQSPKFormationByUin[uin] = loaded;
			g_QQSMissingPKFormationUins.erase(uin);
		}
		state = loaded;
		return QQSPKFormationLoadResult::Loaded;
	}
	catch (const std::exception& e)
	{
		QQS_DebugPrintf10001("[PET-PK-ARRAY] load formation failed Uin=%u Reason=%s\n", uin, e.what());
		return QQSPKFormationLoadResult::Error;
	}
}

static bool QQS_StorePKFormationState(UINT uin, const QQSPKFormationState& state)
{
	if (uin == 0 || state.UsingArrayID == 0)
		return false;
	DBConnection connection(player);
	auto conn = connection.getConnection();
	if (!conn)
		return false;
	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(conn->prepareStatement(
			"INSERT INTO PetPKFormation "
			"(Uin,IDsHex,CaptainPetID,UsingArrayID) "
			"VALUES (?,?,?,?) ON DUPLICATE KEY UPDATE "
			"IDsHex=VALUES(IDsHex),CaptainPetID=VALUES(CaptainPetID),"
			"UsingArrayID=VALUES(UsingArrayID);"));
		statement->setUInt(1, uin);
		statement->setString(2, QQS_EncodePKFormationIDs(state.IDs));
		statement->setUInt(3, state.CaptainPetID);
		statement->setUInt(4, state.UsingArrayID);
		statement->executeUpdate();
		{
			std::lock_guard<std::mutex> lock(g_QQSPKFormationMutex);
			g_QQSPKFormationByUin[uin] = state;
			g_QQSMissingPKFormationUins.erase(uin);
		}
		return true;
	}
	catch (const std::exception& e)
	{
		QQS_DebugPrintf10001("[PET-PK-ARRAY] store formation failed Uin=%u Reason=%s\n", uin, e.what());
		return false;
	}
}

static UINT QQS_YamlUInt(const YAML::Node& node, const char* key, UINT def = 0)
{
	if (!node || !node[key])
		return def;
	try
	{
		return node[key].as<UINT>();
	}
	catch (const std::exception&)
	{
		return def;
	}
}

static UINT QQS_YamlScalarUInt(const YAML::Node& node, UINT def = 0)
{
	if (!node)
		return def;
	try
	{
		return node.as<UINT>();
	}
	catch (const std::exception&)
	{
		return def;
	}
}

static bool QQS_YamlBool(const YAML::Node& node, const char* key, bool def = false)
{
	if (!node || !node[key])
		return def;
	try
	{
		return node[key].as<bool>();
	}
	catch (const std::exception&)
	{
		try
		{
			return node[key].as<UINT>() != 0;
		}
		catch (const std::exception&)
		{
			return def;
		}
	}
}

static std::string QQS_YamlString(const YAML::Node& node, const char* key, const std::string& def = "")
{
	if (!node || !node[key])
		return def;
	try
	{
		return node[key].as<std::string>();
	}
	catch (const std::exception&)
	{
		return def;
	}
}


static bool QQS_LoadCachedYamlFile(const char* filePath, YAML::Node& out)
{
	if (!filePath || !filePath[0])
		return false;

	const std::string path(filePath);
	std::lock_guard<std::mutex> lock(g_QQSYamlCacheMutex);

	std::map<std::string, YAML::Node>::const_iterator iter = g_QQSYamlNodeCache.find(path);
	if (iter != g_QQSYamlNodeCache.end())
	{
		out = iter->second;
		return true;
	}

	if (g_QQSYamlFailedCache.find(path) != g_QQSYamlFailedCache.end())
		return false;

	try
	{
		out = YAML::LoadFile(path);
		g_QQSYamlNodeCache[path] = out;
		return true;
	}
	catch (const std::exception& e)
	{
		g_QQSYamlFailedCache[path] = true;
		printf("Load cached yaml failed: %s (%s)\n", path.c_str(), e.what());
		return false;
	}
}

static int QQS_HexCharToNibble(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool QQS_HexStringToBytes(const std::string& hex, std::vector<BYTE>& out)
{
	out.clear();
	int high = -1;
	for (size_t i = 0; i < hex.size(); ++i)
	{
		const unsigned char ch = static_cast<unsigned char>(hex[i]);
		if (std::isspace(ch))
			continue;

		const int value = QQS_HexCharToNibble(static_cast<char>(ch));
		if (value < 0)
			return false;

		if (high < 0)
		{
			high = value;
			continue;
		}

		out.push_back(static_cast<BYTE>((high << 4) | value));
		high = -1;
	}
	return high < 0;
}

static bool QQS_LoadPetPKHexBody(const char* key, std::vector<BYTE>& out)
{
	out.clear();
	if (!key || !key[0])
		return false;

	{
		std::lock_guard<std::mutex> lock(g_QQSPetPKHexCacheMutex);
		const auto iter = g_QQSPetPKHexBodyCache.find(key);
		if (iter != g_QQSPetPKHexBodyCache.end())
		{
			out = iter->second;
			return true;
		}
		if (g_QQSPetPKHexFailedCache.find(key) != g_QQSPetPKHexFailedCache.end())
			return false;
	}

	YAML::Node config;
	if (!QQS_LoadCachedYamlFile(".\\Config\\PetPK.yml", config))
		return false;

	const std::string hex = QQS_YamlString(config, key);
	std::vector<BYTE> body;
	const bool ok = !hex.empty() && QQS_HexStringToBytes(hex, body) && !body.empty();
	{
		std::lock_guard<std::mutex> lock(g_QQSPetPKHexCacheMutex);
		if (ok)
			g_QQSPetPKHexBodyCache[key] = body;
		else
			g_QQSPetPKHexFailedCache[key] = true;
	}
	if (!ok)
		return false;

	out = std::move(body);
	return true;
}

static bool QQS_LoadPetPKHexBodyList(const char* key, std::vector<std::vector<BYTE>>& out)
{
	out.clear();
	if (!key || !key[0])
		return false;

	{
		std::lock_guard<std::mutex> lock(g_QQSPetPKHexCacheMutex);
		const auto iter = g_QQSPetPKHexBodyListCache.find(key);
		if (iter != g_QQSPetPKHexBodyListCache.end())
		{
			out = iter->second;
			return true;
		}
		if (g_QQSPetPKHexFailedCache.find(key) != g_QQSPetPKHexFailedCache.end())
			return false;
	}

	YAML::Node config;
	if (!QQS_LoadCachedYamlFile(".\\Config\\PetPK.yml", config))
		return false;

	const YAML::Node list = config ? config[key] : YAML::Node();
	if (!list || !list.IsSequence())
	{
		std::lock_guard<std::mutex> lock(g_QQSPetPKHexCacheMutex);
		g_QQSPetPKHexFailedCache[key] = true;
		return false;
	}

	std::vector<std::vector<BYTE>> bodies;
	for (size_t i = 0; i < list.size(); ++i)
	{
		std::vector<BYTE> body;
		try
		{
			if (!QQS_HexStringToBytes(list[i].as<std::string>(), body) || body.empty())
				throw std::runtime_error("invalid hex body");
		}
		catch (const std::exception&)
		{
			std::lock_guard<std::mutex> lock(g_QQSPetPKHexCacheMutex);
			g_QQSPetPKHexFailedCache[key] = true;
			return false;
		}
		bodies.push_back(std::move(body));
	}
	if (bodies.empty())
	{
		std::lock_guard<std::mutex> lock(g_QQSPetPKHexCacheMutex);
		g_QQSPetPKHexFailedCache[key] = true;
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(g_QQSPetPKHexCacheMutex);
		g_QQSPetPKHexBodyListCache[key] = bodies;
	}
	out = std::move(bodies);
	return true;
}

static void QQS_PatchPetPKUin(BYTE* body, size_t len, DWORD uin)
{
	// 旧版正常链路与现有 21446 客户端都要求这里是当前玩家 UIN。
	if (!body || len < 6)
		return;
	body[2] = static_cast<BYTE>((uin >> 24) & 0xFFu);
	body[3] = static_cast<BYTE>((uin >> 16) & 0xFFu);
	body[4] = static_cast<BYTE>((uin >> 8) & 0xFFu);
	body[5] = static_cast<BYTE>(uin & 0xFFu);
}






static void QQS_DebugDumpPetFlow(std::shared_ptr<ClientNode> Client, USHORT msgID, const BYTE* body, size_t len, const char* tag, const char* extra = nullptr)
{
	if (!Client)
		return;

	CreateDirectoryA(".\\Logs", NULL);

	SYSTEMTIME st{};
	GetLocalTime(&st);

	const USHORT firstWord = (body && len >= 2) ? (USHORT)((body[0] << 8) | body[1]) : 0;
	const long long tailLen = (len >= 2 && firstWord <= len) ? (long long)(len - firstWord) : -1;

	FILE* fp = nullptr;
	if (fopen_s(&fp, ".\\Logs\\pet_flow_debug.txt", "ab") != 0 || !fp)
		return;

	fprintf(fp,
		"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [PET-FLOW] tag=%s uin=%u conn=%d msg=%u payload=%zu msgHeadBody=%zu firstWord=%u tail=%lld",
		st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
		tag ? tag : "",
		Client->Uin,
		Client->TransportConnID,
		(unsigned)msgID,
		len,
		len + sizeof(USHORT),
		(unsigned)firstWord,
		tailLen);
	if (extra && *extra)
		fprintf(fp, " %s", extra);

	fprintf(fp, " first16=");
	for (size_t i = 0; body && i < len && i < 16; ++i)
		fprintf(fp, "%02X", body[i]);
	fprintf(fp, " last16=");
	if (body)
	{
		const size_t tailStart = len > 16 ? len - 16 : 0;
		for (size_t i = tailStart; i < len; ++i)
			fprintf(fp, "%02X", body[i]);
	}
	fprintf(fp, "\n");
	fclose(fp);
}

static void QQS_WriteRawBytes(BYTE*& p, const BYTE* data, size_t len)
{
	if (!p || !data || len == 0)
		return;
	memcpy(p, data, len);
	p += len;
}

static BOOL QQS_SendPetStructuredBody(
	std::shared_ptr<ClientNode> Client,
	USHORT msgID,
	const BYTE* body,
	size_t len,
	const char* tag,
	short msgType)
{
	if (!Client || !body || len < sizeof(USHORT) || len > 0xFFFFu)
		return FALSE;

	const size_t logicalLen =
		(static_cast<size_t>(body[0]) << 8) | static_cast<size_t>(body[1]);
	if (logicalLen < sizeof(USHORT) || logicalLen > len)
	{
		QQS_DebugPrintf10001(
			"[PET-CFG] drop invalid selfLen MsgID=%u Uin=%u SelfLen=%zu BufferLen=%zu\n",
			static_cast<unsigned>(msgID), Client->Uin, logicalLen, len);
		return FALSE;
	}

	char extra[128] = {};
	sprintf_s(extra, "selfLen=%zu ignoredOverread=%zu bodyFlag=1 wireDst=%d",
		logicalLen, len - logicalLen, Client->TransportConnID);
	QQS_DebugDumpPetFlow(Client, msgID, body, logicalLen, tag, extra);

	// Official 21446 pet packets use encrypted MSGHEAD framing. The business
	// selfLen is carried by MSGHEAD.BodyLen, so do not duplicate it in the body.
	return SendToClient(
		Client,
		msgID,
		body + sizeof(USHORT),
		static_cast<DWORD>(logicalLen - sizeof(USHORT)),
		Client->ConnID,
		FE_PLAYER,
		Client->ConnID,
		msgType,
		1);
}

static void QQS_SendPetConfigNotify(std::shared_ptr<ClientNode> Client, USHORT msgID, const BYTE* body, size_t len, const char* tag)
{
	QQS_SendPetStructuredBody(Client, msgID, body, len, tag, Notify);
}








static void QQS_WriteYamlByteList(BYTE*& p, const YAML::Node& bytes)
{
	if (!bytes || !bytes.IsSequence())
		return;
	for (size_t i = 0; i < bytes.size(); ++i)
		Write8(p, (UCHAR)QQS_YamlScalarUInt(bytes[i]));
}


void NotifyPetHuanHuaCfgInfo(std::shared_ptr<ClientNode> Client)
{
	const std::vector<BYTE>& body = QQSPetConfig::HuanHuaBody();
	if (!Client || body.empty())
	{
		QQS_DebugPrintf10001("[PET-CONFIG] msg24073 rejected: runtime table is unavailable\n");
		return;
	}
	QQS_SendPetConfigNotify(Client, 24073, body.data(), body.size(), "SEND-24073-FIVE-TABLE");
}

static void QQS_WriteAvatarTempProbCfg(BYTE*& p, const YAML::Node& node)
{
	BYTE* pTemp = p;
	Write16(pTemp, 0); // len
	Write32(pTemp, QQS_YamlUInt(node, "Probality"));
	Write32(pTemp, QQS_YamlUInt(node, "ShowProbality"));
	Write32(pTemp, QQS_YamlUInt(node, "BeginDate"));
	Write32(pTemp, QQS_YamlUInt(node, "BeginTime"));
	Write32(pTemp, QQS_YamlUInt(node, "EndDate"));
	Write32(pTemp, QQS_YamlUInt(node, "EndTime"));
	size_t len = pTemp - p;
	Set16(p, (WORD)len);
	p += len;
}

static void QQS_WriteAvatarSynthesisComm(BYTE*& p, const YAML::Node& comm)
{
	BYTE* pComm = p;
	Write16(pComm, 0); // len
	const char* fields[] = {
		"LowSynItemId", "LowSynItemProb", "LowClientProb",
		"MiddSynItemId", "MiddSynItemProb", "MiddClientProb",
		"HighSynItemId", "HighSynItemProb", "HighClientProb",
		"PetLowSynId", "PetLowSynProb", "PetLowClientProb",
		"PetMiddSynId", "PetMiddSynProb", "PetMiddClientProb",
		"PetHighSynId", "PetHighSynProb", "PetHighClientProb",
		"LuckyStoneId", "MaxLuckyStoneNum", "LuckyStoneProb", "LuckyStoneClientProb",
		"AdvanceConsItemId"
	};
	for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
		Write32(pComm, QQS_YamlUInt(comm, fields[i]));

	YAML::Node blackList = comm ? comm["BlackList"] : YAML::Node();
	Write32(pComm, blackList && blackList.IsSequence() ? (UINT)blackList.size() : 0); // BlackListNum
	if (blackList && blackList.IsSequence())
	{
		for (size_t i = 0; i < blackList.size(); ++i)
			Write32(pComm, QQS_YamlScalarUInt(blackList[i]));
	}

	QQS_WriteAvatarTempProbCfg(pComm, comm["LowSynTempProbCfg"]);
	QQS_WriteAvatarTempProbCfg(pComm, comm["MidSynTempProbCfg"]);
	QQS_WriteAvatarTempProbCfg(pComm, comm["HighSynTempProbCfg"]);
	QQS_WriteAvatarTempProbCfg(pComm, comm["LowPetTempProbCfg"]);
	QQS_WriteAvatarTempProbCfg(pComm, comm["MidPetTempProbCfg"]);
	QQS_WriteAvatarTempProbCfg(pComm, comm["HighPetTempProbCfg"]);

	size_t len = pComm - p;
	Set16(p, (WORD)len);
	p += len;
}

static void QQS_WriteAvatarFormula(BYTE*& p, const YAML::Node& formula)
{
	BYTE* pFormula = p;
	Write16(pFormula, 0); // len
	Write32(pFormula, QQS_YamlUInt(formula, "SrcItemNum"));
	YAML::Node types = formula ? formula["TypeList"] : YAML::Node();
	Write32(pFormula, types && types.IsSequence() ? (UINT)types.size() : 0); // TypeNum
	if (types && types.IsSequence())
	{
		for (size_t i = 0; i < types.size(); ++i)
			Write32(pFormula, QQS_YamlScalarUInt(types[i]));
	}
	size_t len = pFormula - p;
	Set16(p, (WORD)len);
	p += len;
}

static void QQS_WriteAvatarComponent(BYTE*& p, const YAML::Node& component)
{
	BYTE* pComponent = p;
	Write16(pComponent, 0); // len
	Write32(pComponent, QQS_YamlUInt(component, "ComponentType"));
	Write32(pComponent, QQS_YamlUInt(component, "SuccProb"));
	Write32(pComponent, QQS_YamlUInt(component, "ClientProb"));
	Write32(pComponent, QQS_YamlUInt(component, "ProduceSynItemId"));
	Write32(pComponent, QQS_YamlUInt(component, "AvailPeriod"));
	YAML::Node formulas = component ? component["Formulas"] : YAML::Node();
	Write32(pComponent, formulas && formulas.IsSequence() ? (UINT)formulas.size() : 0); // FormulaNum
	if (formulas && formulas.IsSequence())
	{
		for (size_t i = 0; i < formulas.size(); ++i)
			QQS_WriteAvatarFormula(pComponent, formulas[i]);
	}
	size_t len = pComponent - p;
	Set16(p, (WORD)len);
	p += len;
}

static void QQS_WriteAvatarSynthesisRecord(BYTE*& p, const YAML::Node& avatar)
{
	BYTE* pAvatar = p;
	Write16(pAvatar, 0); // len
	Write32(pAvatar, QQS_YamlUInt(avatar, "DstItemId"));
	Write32(pAvatar, QQS_YamlUInt(avatar, "AdvancedItemId"));
	Write32(pAvatar, QQS_YamlUInt(avatar, "AdvanceConsNum"));
	Write32(pAvatar, QQS_YamlUInt(avatar, "AdvanceSuperMoney"));
	Write32(pAvatar, QQS_YamlUInt(avatar, "ReduceProb"));
	YAML::Node components = avatar ? avatar["Components"] : YAML::Node();
	Write32(pAvatar, components && components.IsSequence() ? (UINT)components.size() : 0); // ComponentsNum
	if (components && components.IsSequence())
	{
		for (size_t i = 0; i < components.size(); ++i)
			QQS_WriteAvatarComponent(pAvatar, components[i]);
	}
	size_t len = pAvatar - p;
	Set16(p, (WORD)len);
	p += len;
}

void NotifyAvatarSynthesisCfg(std::shared_ptr<ClientNode> Client)
{
	try
	{
		YAML::Node config;
		if (!QQS_LoadCachedYamlFile(".\\Config\\AvatarSynthesis.yml", config))
			return;
		YAML::Node avatars = config ? config["SynAvatars"] : YAML::Node();
		YAML::Node comm = config ? config["SynthesisComm"] : YAML::Node();
		if (!avatars || !avatars.IsSequence() || !comm)
			return;

		std::vector<BYTE> buf(819200); BYTE* p = buf.data(); size_t len;
		BYTE* pBody = p;
		Write16(p, 0); // CNotifyAvatarSynthesisCfg selfLen
		Write32(p, Client ? Client->Uin : 0); // Uin
		{
			BYTE* pAvatarCfg = p;
			Write16(pAvatarCfg, 0); // AvatarSynthesisCfg len
			QQS_WriteAvatarSynthesisComm(pAvatarCfg, comm);
			Write32(pAvatarCfg, (UINT)avatars.size()); // SynAvatarNum
			for (size_t i = 0; i < avatars.size(); ++i)
				QQS_WriteAvatarSynthesisRecord(pAvatarCfg, avatars[i]);

			len = pAvatarCfg - p;
			Set16(p, (WORD)len);
			p += len;
		}
		len = p - buf.data();
		Set16(pBody, (WORD)len);
		QQS_WriteYamlByteList(p, config["TailBytes"]);
		len = p - buf.data();
		QQS_SendPetConfigNotify(Client, 20103, buf.data(), len, "SEND-20103-STRUCT");
	}
	catch (const std::exception& e)
	{
		printf("NotifyAvatarSynthesisCfg load/build failed: %s\n", e.what());
	}
}

void LoadPetLevelConfig()
{
	std::string error;
	if (!QQSPetConfig::Load(".\\Pet", error))
	{
		fprintf(stderr, "[PET-CONFIG][FATAL] %s\n", error.c_str());
		exit(EXIT_FAILURE);
	}
	printf(
		"Load Pet tables Success! Base=%zu Skill=%zu Strength=%zu HuanHua=%zu PK=%zu\n",
		QQSPetConfig::BaseOrder().size(),
		QQSPetConfig::ShowOrder().size(),
		QQSPetConfig::StrengthOrder().size(),
		QQSPetConfig::HuanHuaOrder().size(),
		QQSPetConfig::PKOrder().size());
}
PetInfo* GetPetItem(UINT PetID, UINT Uin)
{
	std::unique_ptr<PetInfo> pet(new PetInfo{});
	pet->ID = static_cast<UINT>(-1);
	if (Uin == 0 || PetID == 0 || !QQSPetConfig::HasPet(PetID))
	{
		QQS_DebugPrintf10001("[PET-DB] rejected lookup Uin=%u PetID=%u Configured=%u\n",
			Uin, PetID, QQSPetConfig::HasPet(PetID) ? 1 : 0);
		return pet.release();
	}

	DBConnection connection(player);
	auto conn = connection.getConnection();
	if (!conn)
		return pet.release();

	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(
			conn->prepareStatement("SELECT Name,Experience,PL,Status,StrengLevel FROM Pet WHERE Uin=? AND PetId=?;"));
		statement->setUInt(1, Uin);
		statement->setUInt(2, PetID);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		if (!result->next())
			return pet.release();

		const std::string name = UTF2GB(result->getString("Name").c_str());
		if (name.empty() || name.size() > sizeof(pet->Name))
		{
			QQS_DebugPrintf10001("[PET-DB] invalid name length rejected Uin=%u PetID=%u Bytes=%zu\n", Uin, PetID, name.size());
			return pet.release();
		}
		memcpy(pet->Name, name.data(), name.size());
		pet->ID = PetID;
		pet->Exp = result->getUInt("Experience");
		pet->PL = result->getUInt("PL");
		pet->Status = result->getUInt("Status");
		pet->StrengLevel = result->getUInt("StrengLevel");
		if (!QQS_CompletePetInfo(*pet))
		{
			QQS_DebugPrintf10001("[PET-DB] protocol state rejected Uin=%u PetID=%u Exp=%u Status=%u PL=%u Strength=%u\n",
				Uin, PetID, pet->Exp, pet->Status, pet->PL, pet->StrengLevel);
			*pet = PetInfo{};
			pet->ID = static_cast<UINT>(-1);
		}
	}
	catch (const std::exception& e)
	{
		QQS_DebugPrintf10001("[PET-DB] lookup failed Uin=%u PetID=%u Reason=%s\n", Uin, PetID, e.what());
		*pet = PetInfo{};
		pet->ID = static_cast<UINT>(-1);
	}
	return pet.release();
}

static UCHAR QQS_GetPetTopStatusForLevel(UINT level)
{
	if (level >= 20)
		return 3;
	if (level >= 11)
		return 2;
	return 1;
}

bool QQS_CompletePetInfo(PetInfo& pet)
{
	if (pet.ID == 0 || !QQSPetConfig::HasPet(pet.ID) || pet.Name[0] == 0)
		return false;
	if (pet.Status > 0xFFu || pet.PL > 0xFFFFu)
		return false;

	UINT level = 0;
	UINT maxExperience = 0;
	if (!QQSPetConfig::GetLevelForExperience(pet.Exp, level) ||
		!QQSPetConfig::GetMaxExperience(level, maxExperience) ||
		level > 0xFFFFu)
		return false;

	const UINT levelLimit = QQSPetConfig::MaxLevel();
	if (levelLimit == 0 || levelLimit > 0xFFFFu)
		return false;

	UCHAR canHuanHua = 0;
	UCHAR canDrive = 0;
	if (!QQSPetConfig::GetProtocolCapabilities(pet.ID, canHuanHua, canDrive))
		return false;

	if (pet.StrengLevel != 0)
	{
		UINT fullLevel = 0;
		if (!QQSPetConfig::GetStrengthFullLevel(pet.ID, fullLevel) || pet.StrengLevel > fullLevel)
			return false;
	}
	if (pet.HuanHuaLevel != 0 && !QQSPetConfig::HasHuanHua(pet.ID))
		return false;
	if (pet.Drive > 1 || pet.CanHuanHua > 1 || pet.CanDrive > 1)
		return false;

	pet.Level = level;
	pet.MaxExp = maxExperience;
	pet.TopStatus = QQS_GetPetTopStatusForLevel(level);
	pet.IsProper = 1;
	pet.LevelLimit = static_cast<USHORT>(levelLimit);
	pet.TotalPL = static_cast<USHORT>(pet.PL);
	pet.CanHuanHua = canHuanHua;
	pet.CanDrive = canDrive;
	if (!pet.CanDrive && pet.Drive != 0)
		return false;
	return true;
}

bool QQS_LoadPetInfo(UINT Uin, UINT PetID, PetInfo& pet)
{
	std::unique_ptr<PetInfo> loaded(GetPetItem(PetID, Uin));
	if (!loaded || loaded->ID == static_cast<UINT>(-1))
		return false;
	pet = *loaded;
	return true;
}


void GetPetInfo(std::shared_ptr<ClientNode> Client)
{
	if (!Client || !Client->Pet || Client->Pet->ID == 0)
		return;

	const UINT petID = Client->Pet->ID;
	std::unique_ptr<PetInfo> loaded(GetPetItem(petID, Client->Uin));
	if (!loaded || loaded->ID == static_cast<UINT>(-1))
	{
		QQS_DebugPrintf10001("[PET-DB] equipped pet row missing; rejected Uin=%u PetID=%u\n", Client->Uin, petID);
		*Client->Pet = PetInfo{};
		Client->PetID = 0;
		Client->PetStatus = 0;
		return;
	}

	*Client->Pet = *loaded;
	Client->PetID = loaded->ID;
	Client->PetStatus = static_cast<char>(loaded->Status);
}

UINT GetPetLevel(UINT Exp)
{
	UINT level = 0;
	if (!QQSPetConfig::GetLevelForExperience(Exp, level))
		QQS_DebugPrintf10001("[PET-CONFIG] level lookup rejected Exp=%u\n", Exp);
	return level;
}

UINT GetPetMaxExp(UINT Level)
{
	UINT maxExperience = 0;
	if (!QQSPetConfig::GetMaxExperience(Level, maxExperience))
		QQS_DebugPrintf10001("[PET-CONFIG] max-exp lookup rejected Level=%u\n", Level);
	return maxExperience;
}

std::map<UINT, UINT> GetPetStrengLevel(UINT PetID, UINT StrengLevel)
{
	std::map<UINT, UINT> bonuses;
	if (!QQSPetConfig::GetStrengthBonuses(PetID, StrengLevel, bonuses) && StrengLevel != 0)
		QQS_DebugPrintf10001("[PET-CONFIG] strength bonus lookup rejected PetID=%u Level=%u\n", PetID, StrengLevel);
	return bonuses;
}





static bool QQS_SendPetSkillListStructured(std::shared_ptr<ClientNode> Client)
{
	const std::vector<BYTE>& body = QQSPetConfig::SkillListBody();
	if (!Client || body.empty())
		return false;
	QQS_DebugDumpPetFlow(Client, 440, body.data(), body.size(), "SEND-440-FIVE-TABLE");
	return SendToClient440Official(Client, body.data(), static_cast<DWORD>(body.size()), Client->ConnID, FE_PLAYER, 1, Response) != FALSE;
}



static bool QQS_HandlePetLobbyShowRequest(std::shared_ptr<ClientNode> Client, UINT petID, USHORT getAll);
static bool QQS_HandlePetLobbyPKCfgRequest(std::shared_ptr<ClientNode> Client);

static bool QQS_SendOfficialPetStrengthCfgOnce(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return false;
	{
		std::shared_lock<std::shared_mutex> lock(Client->mtx);
		if (Client->PetStrengthCfgSent)
			return false;
	}
	const std::vector<BYTE>& body = QQSPetConfig::StrengthHeaderBody();
	if (body.empty())
		return false;

	NotifyPetStrengCfgInfo(Client);
	{
		std::unique_lock<std::shared_mutex> lock(Client->mtx);
		Client->PetStrengthCfgSent = true;
	}
	return true;
}

void RequestGetPetShowInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (!Client || !Body)
		return;

	BYTE* p = Body;
	BYTE* end = Body + BodyLen;
	if (BodyLen >= 2)
	{
		BYTE* afterLength = p;
		const USHORT selfLength = Read16(afterLength);
		if (selfLength == BodyLen)
			p = afterLength;
	}
	if (static_cast<size_t>(end - p) != 14)
	{
		QQS_DebugPrintf10001("[PET-SHOW] malformed request rejected Uin=%u BodyLen=%zu\n", Client->Uin, BodyLen);
		return;
	}
	const UINT requestUin = Read32(p);
	(void)Read32(p); // Time
	const UINT petID = Read32(p);
	const USHORT getAll = Read16(p);
	if (requestUin != Client->Uin || getAll > 1 || (getAll == 0 && (petID == 0 || !QQSPetConfig::HasShow(petID))))
	{
		QQS_DebugPrintf10001("[PET-SHOW] invalid request rejected Uin=%u RequestUin=%u PetID=%u GetAll=%u\n",
			Client->Uin, requestUin, petID, getAll);
		return;
	}
	ResponseGetPetShowInfo(Client, getAll == 0 ? petID : 0);
}

void ResponseGetPetShowInfo(std::shared_ptr<ClientNode> Client, UINT PETID)
{
	if (!Client)
		return;
	std::vector<QQSPetConfig::StaticPacket> packets;
	std::string error;
	if (!QQSPetConfig::BuildShowPackets(Client->Uin, PETID, packets, error))
	{
		QQS_DebugPrintf10001("[PET-SHOW] build rejected Uin=%u PetID=%u Reason=%s\n", Client->Uin, PETID, error.c_str());
		return;
	}
	for (const QQSPetConfig::StaticPacket& packet : packets)
	{
		QQS_SendPetStructuredBody(
			Client, packet.MsgID, packet.Body.data(), packet.Body.size(),
			"SEND-PETSHOW-FIVE-TABLE", Response);
	}
}

void ResponseGetPetShowMoreInfo(std::shared_ptr<ClientNode> Client, UINT PETID)
{
	if (!Client)
		return;
	std::vector<QQSPetConfig::StaticPacket> packets;
	std::string error;
	if (!QQSPetConfig::BuildShowPackets(Client->Uin, PETID, packets, error))
		return;
	const size_t first = PETID == 0 ? 1 : 0;
	for (size_t i = first; i < packets.size(); ++i)
	{
		const USHORT msgID = 24527;
		QQS_SendPetStructuredBody(
			Client, msgID, packets[i].Body.data(), packets[i].Body.size(),
			"SEND-PETSHOW-MORE-FIVE-TABLE", Response);
	}
}

static bool QQS_HandlePetLobbyPKCfgRequest(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return true;

	QQS_SendOfficialPetStrengthCfgOnce(Client);
	ResponseGetPKPetCfg(Client);
	return true;
}

static bool QQS_HandlePetLobbyShowRequest(std::shared_ptr<ClientNode> Client, UINT petID, USHORT getAll)
{
	if (!Client)
		return true;

	ResponseGetPetShowInfo(Client, (getAll == 0) ? petID : 0);
	return true;
}

void RequestGetPetSkillList(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	(void)Body;
	(void)BodyLen;
	ResponseGetPetSkillList(Client);
}

void ResponseGetPetSkillList(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;

	QQS_SendPetSkillListStructured(Client);
}

void RequestPayRebateCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	(void)Body;
	(void)BodyLen;
	ResponsePayRebateCfg(Client);
}

void ResponsePayRebateCfg(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;

	std::vector<BYTE> body(26);
	BYTE* p = body.data();
	Write16(p, 16); // official selfLen
	Write16(p, 0);
	Write32(p, Client->Uin);
	Write32(p, 0);
	Write32(p, 0);

	static const BYTE officialTail[] = {
		0x26, 0xC8, 0x00, 0x08, 0x00, 0x02, 0x00, 0x02, 0x1C, 0x3C
	};
	QQS_WriteRawBytes(p, officialTail, sizeof(officialTail));

	QQS_DebugDumpPetFlow(Client, 20077, body.data(), (size_t)(p - body.data()), "SEND-20077");
	SendToClientOfficial14(Client, 20077, body.data(), (DWORD)(p - body.data()), Client->ConnID, FE_PLAYER, 1, Response);
}

void RequestGetPKPetCfg(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	(void)Body;
	(void)BodyLen;
	QQS_HandlePetLobbyPKCfgRequest(Client);
}

void ResponseGetPKPetCfg(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;

	std::vector<BYTE> body;
	if (!QQS_LoadPetPKHexBody("Response21140Hex", body))
	{
		QQS_DebugPrintf10001(
			"[PET-PK-CFG] 21140 missing Config\\PetPK.yml Response21140Hex Uin=%u\n",
			Client->Uin);
		return;
	}

	QQS_PatchPetPKUin(body.data(), body.size(), static_cast<DWORD>(Client->Uin));
	QQS_SendPetStructuredBody(
		Client, 21140, body.data(), body.size(), "SEND-21140-BASELINE", Response);
	NotifyGetPKPetMoreCfg(Client);
}

void NotifyGetPKPetMoreCfg(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;

	std::vector<std::vector<BYTE>> bodies;
	if (!QQS_LoadPetPKHexBodyList("Response21149HexList", bodies))
	{
		QQS_DebugPrintf10001(
			"[PET-PK-CFG] 21149 missing Config\\PetPK.yml Response21149HexList Uin=%u\n",
			Client->Uin);
		return;
	}

	for (size_t i = 0; i < bodies.size(); ++i)
	{
		QQS_SendPetStructuredBody(
			Client, 21149, bodies[i].data(), bodies[i].size(),
			"SEND-21149-BASELINE", Response);
	}
}

static std::vector<PetInfo> QQS_LoadPlayerPKPetsFromDB(UINT uin)
{
	std::vector<PetInfo> pets;
	std::vector<UINT> petOrder;
	std::unordered_map<UINT, size_t> rowCountByPetID;
	std::unordered_map<UINT, PetInfo> firstPetByID;
	DBConnection connection(player);
	auto conn = connection.getConnection();
	if (!conn)
		return pets;

	try
	{
		std::unique_ptr<sql::PreparedStatement> statement(conn->prepareStatement(
			"SELECT P.PetId,P.Name,P.Experience,P.PL,P.Status,P.StrengLevel "
			"FROM Pet P WHERE P.Uin=? AND EXISTS ("
			"SELECT 1 FROM Item I WHERE I.Uin=P.Uin AND I.ItemID=P.PetId "
			"AND I.ItemNum>0 AND (I.AvailPeriod<=0 OR "
			"(I.AvailPeriod*60*60+I.ObtainTime)>UNIX_TIMESTAMP())) "
			"ORDER BY P.PetId;"));
		statement->setUInt(1, uin);
		std::unique_ptr<sql::ResultSet> result(statement->executeQuery());
		while (result->next())
		{
			const UINT petID = result->getUInt("PetId");
			size_t& rowCount = rowCountByPetID[petID];
			++rowCount;
			if (rowCount != 1)
				continue;
			petOrder.push_back(petID);
			if (!QQSPetConfig::HasPK(petID))
				continue;

			PetInfo pet{};
			const std::string name = UTF2GB(result->getString("Name").c_str());
			if (name.empty() || name.size() > sizeof(pet.Name))
				continue;
			memcpy(pet.Name, name.data(), name.size());
			pet.ID = petID;
			pet.Exp = result->getUInt("Experience");
			pet.PL = result->getUInt("PL");
			pet.Status = result->getUInt("Status");
			pet.StrengLevel = result->getUInt("StrengLevel");
			if (QQS_CompletePetInfo(pet))
				firstPetByID.emplace(petID, pet);
		}

		for (const UINT petID : petOrder)
		{
			const auto pet = firstPetByID.find(petID);
			if (rowCountByPetID[petID] == 1 && pet != firstPetByID.end())
				pets.push_back(pet->second);
		}
	}
	catch (const std::exception& e)
	{
		QQS_DebugPrintf10001("[PET-PK-INFO] load player pets failed Uin=%u Reason=%s\n", uin, e.what());
	}
	return pets;
}

static UCHAR QQS_PKSkillType(UINT skillID)
{
	if (skillID >= 3000)
		return 2;
	if (skillID >= 2000)
		return 1;
	return 0;
}

static USHORT QQS_PKSkillLevel(const YAML::Node& skill, UINT fightLevel, UCHAR skillType)
{
	if (skillType == 2)
		return 0;
	if (skillType == 1)
		return static_cast<USHORT>((fightLevel + 4) / 5);
	return fightLevel < QQS_YamlUInt(skill, "TackEffectLevel") ? 0 : static_cast<USHORT>(fightLevel);
}

static void QQS_WritePKPetInfoSkill(WireWriter& writer, const YAML::Node& skill, UINT fightLevel)
{
	const size_t start = writer.BeginRecord();
	const UINT skillID = QQS_YamlUInt(skill, "SkillID");
	const UCHAR skillType = QQS_PKSkillType(skillID);
	const USHORT skillLevel = QQS_PKSkillLevel(skill, fightLevel, skillType);
	const UCHAR canLevelUp = skillType == 2 ? 0 : 1;

	writer.U32(skillID);
	writer.U16(skillLevel);
	writer.U8(skillType);
	writer.U8(canLevelUp);
	writer.U16(canLevelUp ? static_cast<USHORT>(skillLevel + 1) : 0);
	const std::string attach = QQS_YamlString(skill, "Attach");
	const std::string attachGB = attach.empty() ? std::string() : UTF2GB(attach.c_str());
	writer.U16(attachGB.size());
	writer.Bytes(attachGB);
	writer.EndRecord(start);
}

static UINT QQS_GetPKPetFightPower(const PetInfo& pet)
{
	return QQS_YamlUInt(QQSPetConfig::PK(pet.ID), "FightPower");
}

static void QQS_WritePKPetInfoBlock(WireWriter& writer, const PetInfo& pet)
{
	const YAML::Node pk = QQSPetConfig::PK(pet.ID);
	const UINT fightLevel = pet.Level;
	const size_t start = writer.BeginRecord();

	writer.U32(pet.ID);
	writer.U8(QQS_YamlUInt(pk, "ProfType"));
	writer.U16(fightLevel);
	writer.U16(QQS_YamlUInt(pk, "Rarity"));
	writer.U32(pet.Exp);
	writer.U32(pet.MaxExp);
	for (size_t i = 0; i < 5; ++i)
		writer.U16(0); // ExtraPotentialByStar[5]

	const YAML::Node skills = pk ? pk["Skills"] : YAML::Node();
	const size_t skillCount = skills && skills.IsSequence() ? skills.size() : 0;
	writer.U32(skillCount);
	for (size_t i = 0; i < skillCount; ++i)
		QQS_WritePKPetInfoSkill(writer, skills[i], fightLevel);

	writer.U32(QQS_GetPKPetFightPower(pet));
	const YAML::Node attributes = pk ? pk["Attributes"] : YAML::Node();
	for (size_t i = 0; i < 5; ++i)
		writer.U32(attributes && attributes.IsSequence() && i < attributes.size() ? QQS_YamlScalarUInt(attributes[i]) : 0);
	writer.U8(fightLevel >= QQSPetConfig::MaxLevel() ? 1 : 0);
	writer.U32(2); // official default HPIncByStatus
	writer.U8(5);  // official default HPIncPercent
	writer.U8(pet.Status);
	writer.U32(QQS_YamlUInt(pk, "MaxFightPower"));
	for (size_t i = 0; i < 5; ++i)
		writer.U16(0); // AttrMotivateLevel[5]
	writer.EndRecord(start);
}

struct QQSPKArraySeat
{
	size_t Slot = 0;
	UCHAR RecommendedPetType = 0;
};

static bool QQS_GetPKArraySeats(UCHAR arrayID, std::vector<QQSPKArraySeat>& seats, size_t& embattlePetNum)
{
	seats.clear();
	embattlePetNum = 0;
	const YAML::Node global = QQSPetConfig::PKGlobal();
	const YAML::Node embattle = global ? global["Embattle"] : YAML::Node();
	const YAML::Node battleArrays = global ? global["BattleArrays"] : YAML::Node();
	const YAML::Node arrays = battleArrays ? battleArrays["Arrays"] : YAML::Node();
	if (!embattle || !embattle["EmbattlePetNum"] || !arrays || !arrays.IsSequence())
		return false;

	try
	{
		embattlePetNum = embattle["EmbattlePetNum"].as<size_t>();
		for (size_t i = 0; i < arrays.size(); ++i)
		{
			if (!arrays[i]["ArrayID"] || arrays[i]["ArrayID"].as<UINT>() != arrayID)
				continue;
			const YAML::Node levels = arrays[i]["Levels"];
			if (!levels || !levels.IsSequence() || levels.size() == 0)
				return false;
			const YAML::Node configuredSeats = levels[0]["Seats"];
			if (!configuredSeats || !configuredSeats.IsSequence())
				return false;

			std::set<size_t> usedSlots;
			for (size_t j = 0; j < configuredSeats.size(); ++j)
			{
				const UINT slot = configuredSeats[j]["SeatID"].as<UINT>();
				const UINT petType = configuredSeats[j]["RecomPetType"].as<UINT>();
				if (slot >= 10 || petType > 0xFFu || !usedSlots.insert(slot).second)
					return false;
				seats.push_back(QQSPKArraySeat{ static_cast<size_t>(slot), static_cast<UCHAR>(petType) });
			}
			return embattlePetNum != 0 && embattlePetNum <= seats.size();
		}
	}
	catch (const std::exception&)
	{
		return false;
	}
	return false;
}

static bool QQS_GetInitialPKArrayID(UCHAR& arrayID)
{
	const YAML::Node global = QQSPetConfig::PKGlobal();
	const YAML::Node battleArrays = global ? global["BattleArrays"] : YAML::Node();
	if (!battleArrays || !battleArrays["InitArrayID"])
		return false;
	try
	{
		const UINT value = battleArrays["InitArrayID"].as<UINT>();
		if (value == 0 || value > 0xFFu)
			return false;
		arrayID = static_cast<UCHAR>(value);
		return true;
	}
	catch (const std::exception&)
	{
		return false;
	}
}

static const PetInfo* QQS_FindPKPet(const std::vector<PetInfo>& pets, UINT petID)
{
	const auto it = std::find_if(pets.begin(), pets.end(), [&](const PetInfo& pet) { return pet.ID == petID; });
	return it == pets.end() ? nullptr : &*it;
}

static bool QQS_SelectAutoPKCaptain(
	const std::vector<PetInfo>& pets,
	const std::array<UINT, 10>& ids,
	UINT& captainPetID)
{
	bool found = false;
	UINT bestInitiative = 0;
	for (const UINT petID : ids)
	{
		if (petID == 0)
			continue;
		const PetInfo* pet = QQS_FindPKPet(pets, petID);
		const YAML::Node attributes = pet ? QQSPetConfig::PK(petID)["Attributes"] : YAML::Node();
		if (!pet || !attributes || !attributes.IsSequence() || attributes.size() != 5)
			return false;
		try
		{
			const UINT initiative = attributes[3].as<UINT>();
			if (!found || initiative > bestInitiative)
			{
				found = true;
				bestInitiative = initiative;
				captainPetID = petID;
			}
		}
		catch (const std::exception&)
		{
			return false;
		}
	}
	return found;
}

static bool QQS_BuildAutoPKFormation(
	const std::vector<PetInfo>& pets,
	const std::vector<QQSPKArraySeat>& seats,
	size_t embattlePetNum,
	std::array<UINT, 10>& ids)
{
	ids.fill(0);
	if (pets.empty() || embattlePetNum == 0 || embattlePetNum > seats.size())
		return false;

	std::vector<const PetInfo*> selected;
	selected.reserve(pets.size());
	for (const PetInfo& pet : pets)
		selected.push_back(&pet);
	std::stable_sort(selected.begin(), selected.end(), [](const PetInfo* left, const PetInfo* right) {
		return QQS_GetPKPetFightPower(*left) > QQS_GetPKPetFightPower(*right);
	});
	if (selected.size() > embattlePetNum)
		selected.resize(embattlePetNum);

	std::vector<bool> assigned(selected.size(), false);
	for (const QQSPKArraySeat& seat : seats)
	{
		for (size_t i = 0; i < selected.size(); ++i)
		{
			if (!assigned[i] && QQS_YamlUInt(QQSPetConfig::PK(selected[i]->ID), "ProfType") == seat.RecommendedPetType)
			{
				ids[seat.Slot] = selected[i]->ID;
				assigned[i] = true;
				break;
			}
		}
	}

	size_t nextPet = 0;
	for (const QQSPKArraySeat& seat : seats)
	{
		if (ids[seat.Slot] != 0)
			continue;
		while (nextPet < selected.size() && assigned[nextPet])
			++nextPet;
		if (nextPet == selected.size())
			break;
		ids[seat.Slot] = selected[nextPet]->ID;
		assigned[nextPet] = true;
	}
	return std::any_of(ids.begin(), ids.end(), [](UINT petID) { return petID != 0; });
}

static UINT QQS_SumPKArrayFightPower(const std::vector<PetInfo>& pets, const std::vector<UINT>& slots)
{
	UINT total = 0;
	for (size_t i = 0; i < slots.size(); ++i)
	{
		const auto it = std::find_if(pets.begin(), pets.end(), [&](const PetInfo& pet) { return pet.ID == slots[i]; });
		if (it != pets.end())
			total += QQS_GetPKPetFightPower(*it);
	}
	return total;
}

static void QQS_WritePKPetArrayInfo(
	WireWriter& writer,
	const std::array<UINT, 10>& slots,
	UINT captainPetID,
	UINT totalFightPower,
	UCHAR usingArrayID)
{
	const size_t arrayInfoStart = writer.BeginRecord();
	writer.U8(usingArrayID);
	writer.U8(1); // ArrayNum
	const size_t arrayStart = writer.BeginRecord();
	writer.U8(usingArrayID);
	writer.U8(1); // Level
	writer.U32(captainPetID);
	writer.U32(totalFightPower);
	for (size_t i = 0; i < 10; ++i)
		writer.U32(slots[i]);
	writer.EndRecord(arrayStart);
	writer.EndRecord(arrayInfoStart);
}

void RequestGetPKPetInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	// Current 21446 request: selfLen + Uin + Time + DstUin + Flag + PetID + two reserved u32.
	if (!Client || !Body || BodyLen != 27)
	{
		QQS_DebugPrintf10001("[PET-PK-INFO] 21133 rejected: expected 27 bytes, got %zu Uin=%u\n",
			BodyLen, Client ? Client->Uin : 0);
		return;
	}

	BYTE* p = Body;
	const USHORT selfLen = Read16(p);
	const UINT requestUin = Read32(p);
	const UINT requestTime = Read32(p);
	const UINT dstUin = Read32(p);
	const char flag = static_cast<char>(Read8(p));
	const int petID = static_cast<int>(Read32(p));
	const UINT reserved0 = Read32(p);
	const UINT reserved1 = Read32(p);
	if (selfLen != BodyLen || requestUin != Client->Uin || reserved0 != 0 || reserved1 != 0)
	{
		QQS_DebugPrintf10001(
			"[PET-PK-INFO] 21133 rejected: selfLen=%u bodyLen=%zu requestUin=%u clientUin=%u reserved=%u/%u\n",
			selfLen, BodyLen, requestUin, Client->Uin, reserved0, reserved1);
		return;
	}

	char extra[160] = {};
	sprintf_s(extra, "requestTime=%u dstUin=%u flag=%d petID=%d",
		requestTime, dstUin, static_cast<int>(flag), petID);
	QQS_DebugDumpPetFlow(Client, 21133, Body, BodyLen, "RECV-21133-CURRENT", extra);
	ResponseGetPKPetInfo(Client, dstUin, flag, petID);
}

void ResponseGetPKPetInfo(std::shared_ptr<ClientNode> Client, UINT DstUin, char Flag, int PetId)
{
	if (!Client)
		return;

	const UINT targetUin = DstUin ? DstUin : Client->Uin;
	std::vector<PetInfo> pets = QQS_LoadPlayerPKPetsFromDB(targetUin);
	(void)PetId;
	if (pets.size() > 128)
		pets.resize(128);

	QQSPKFormationState formation{};
	const QQSPKFormationLoadResult formationLoad = QQS_LoadPKFormationState(targetUin, formation);
	if (formationLoad == QQSPKFormationLoadResult::Error)
		return;
	const bool hasFormation = formationLoad == QQSPKFormationLoadResult::Loaded;
	if (!hasFormation && !QQS_GetInitialPKArrayID(formation.UsingArrayID))
	{
		QQS_DebugPrintf10001("[PET-PK-INFO] 21133 rejected: initial array config unavailable Uin=%u\n", targetUin);
		return;
	}
	std::set<UINT> formationPetIDs;
	for (const UINT petID : formation.IDs)
	{
		if (petID == 0)
			continue;
		if (!formationPetIDs.insert(petID).second || !QQS_FindPKPet(pets, petID))
		{
			QQS_DebugPrintf10001("[PET-PK-INFO] 21133 rejected: stale formation Uin=%u PetID=%u\n", targetUin, petID);
			return;
		}
	}
	if (formation.CaptainPetID != 0 && formationPetIDs.find(formation.CaptainPetID) == formationPetIDs.end())
	{
		QQS_DebugPrintf10001("[PET-PK-INFO] 21133 rejected: captain is not deployed Uin=%u Captain=%u\n",
			targetUin, formation.CaptainPetID);
		return;
	}
	const std::vector<UINT> slots(formation.IDs.begin(), formation.IDs.end());
	const UINT totalFightPower = QQS_SumPKArrayFightPower(pets, slots);

	try
	{
		WireWriter writer;
		writer.U16(0); // selfLen
		writer.U32(Client->Uin);
		writer.U32(0); // Time
		writer.U16(0); // ResultID
		writer.U16(0); // ReasonLen
		writer.U32(DstUin);
		writer.U8(static_cast<UCHAR>(Flag));
		for (size_t i = 0; i < 10; ++i)
			writer.U32(i < slots.size() ? slots[i] : 0);
		writer.U16(pets.size());
		for (size_t i = 0; i < pets.size(); ++i)
			QQS_WritePKPetInfoBlock(writer, pets[i]);
		writer.U32(totalFightPower);
		writer.U32(formation.CaptainPetID);
		writer.U32(0); // RankedMatchLevel: no persisted ranked state
		writer.U32(0); // RankedMatchScore
		writer.U32(0); // RankedMatchRank
		writer.U8(0);  // HasMoreInfo
		writer.U32(0); // WeeklyPetStarRefreshFreeLimit: no persisted refresh state
		writer.U32(0); // WeeklyPetStarRefreshFreeCnt
		writer.U8(pets.empty() ? 0 : 1); // HasPkArrayInfo
		if (!pets.empty())
			QQS_WritePKPetArrayInfo(writer, formation.IDs, formation.CaptainPetID, totalFightPower, formation.UsingArrayID);
		for (size_t i = 0; i < 12; ++i)
			writer.U8(0); // official in-self trailing bytes

		const std::vector<BYTE> body = writer.FinishSelfLength();
		QQS_DebugPrintf10001(
			"[PET-PK-INFO] 21133 response Uin=%u Target=%u PetNum=%zu Captain=%u TotalFP=%u Len=%zu\n",
			Client->Uin, targetUin, pets.size(), formation.CaptainPetID, totalFightPower, body.size());
		QQS_SendPetStructuredBody(Client, 21133, body.data(), body.size(), "SEND-21133-CURRENT", Response);
	}
	catch (const std::exception& e)
	{
		QQS_DebugPrintf10001("[PET-PK-INFO] 21133 build failed Uin=%u Reason=%s\n", Client->Uin, e.what());
	}
}

void RequestEmbattle(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (!Client || !Body || BodyLen != 58)
	{
		QQS_DebugPrintf10001("[PET-PK-ARRAY] 21120 rejected: expected 58 bytes, got %zu Uin=%u\n",
			BodyLen, Client ? Client->Uin : 0);
		return;
	}

	BYTE* p = Body;
	const USHORT selfLen = Read16(p);
	const UINT requestUin = Read32(p);
	const UINT requestTime = Read32(p);
	std::array<UINT, 10> requestIDs{};
	for (UINT& petID : requestIDs)
		petID = Read32(p);
	const UINT requestCaptain = Read32(p);
	const USHORT autoEmbattle = Read16(p);
	const USHORT requestArrayID = Read16(p);
	if (selfLen != BodyLen || requestUin != Client->Uin || autoEmbattle > 1 || requestArrayID > 0xFFu)
	{
		QQS_DebugPrintf10001(
			"[PET-PK-ARRAY] 21120 rejected: selfLen=%u bodyLen=%zu requestUin=%u clientUin=%u auto=%u array=%u\n",
			selfLen, BodyLen, requestUin, Client->Uin,
			static_cast<unsigned>(autoEmbattle), static_cast<unsigned>(requestArrayID));
		return;
	}

	std::vector<PetInfo> pets = QQS_LoadPlayerPKPetsFromDB(Client->Uin);
	if (pets.empty())
	{
		QQS_DebugPrintf10001("[PET-PK-ARRAY] 21120 rejected: no owned PK pets Uin=%u\n", Client->Uin);
		return;
	}

	QQSPKFormationState previous{};
	const QQSPKFormationLoadResult previousLoad = QQS_LoadPKFormationState(Client->Uin, previous);
	if (previousLoad == QQSPKFormationLoadResult::Error)
		return;
	const bool hadPrevious = previousLoad == QQSPKFormationLoadResult::Loaded;
	UCHAR usingArrayID = static_cast<UCHAR>(requestArrayID);
	if (usingArrayID == 0)
	{
		if (hadPrevious && previous.UsingArrayID != 0)
			usingArrayID = previous.UsingArrayID;
		else if (!QQS_GetInitialPKArrayID(usingArrayID))
		{
			QQS_DebugPrintf10001("[PET-PK-ARRAY] 21120 rejected: initial array config unavailable Uin=%u\n", Client->Uin);
			return;
		}
	}

	std::vector<QQSPKArraySeat> seats;
	size_t embattlePetNum = 0;
	if (!QQS_GetPKArraySeats(usingArrayID, seats, embattlePetNum))
	{
		QQS_DebugPrintf10001("[PET-PK-ARRAY] 21120 rejected: array config unavailable Uin=%u ArrayID=%u\n",
			Client->Uin, static_cast<unsigned>(usingArrayID));
		return;
	}

	QQSPKFormationState next = previous;
	next.UsingArrayID = usingArrayID;
	if (autoEmbattle != 0)
	{
		if (requestCaptain != 0 || std::any_of(requestIDs.begin(), requestIDs.end(), [](UINT id) { return id != 0; }) ||
			!QQS_BuildAutoPKFormation(pets, seats, embattlePetNum, next.IDs))
		{
			QQS_DebugPrintf10001("[PET-PK-ARRAY] 21120 auto rejected: request/state mismatch Uin=%u\n", Client->Uin);
			return;
		}

		if (!QQS_SelectAutoPKCaptain(pets, next.IDs, next.CaptainPetID))
		{
			QQS_DebugPrintf10001("[PET-PK-ARRAY] 21120 auto rejected: captain attributes unavailable Uin=%u\n", Client->Uin);
			return;
		}
	}
	else
	{
		std::set<size_t> allowedSlots;
		for (const QQSPKArraySeat& seat : seats)
			allowedSlots.insert(seat.Slot);
		std::set<UINT> deployed;
		for (size_t i = 0; i < requestIDs.size(); ++i)
		{
			const UINT petID = requestIDs[i];
			if (petID == 0)
				continue;
			if (allowedSlots.find(i) == allowedSlots.end() || !deployed.insert(petID).second || !QQS_FindPKPet(pets, petID))
			{
				QQS_DebugPrintf10001("[PET-PK-ARRAY] 21120 manual rejected: invalid slot/pet Uin=%u Slot=%zu PetID=%u\n",
					Client->Uin, i, petID);
				return;
			}
		}
		if (deployed.size() > embattlePetNum ||
			(requestCaptain != 0 && deployed.find(requestCaptain) == deployed.end()) ||
			(requestCaptain == 0 && !deployed.empty()))
		{
			QQS_DebugPrintf10001("[PET-PK-ARRAY] 21120 manual rejected: invalid count/captain Uin=%u Pets=%zu Captain=%u\n",
				Client->Uin, deployed.size(), requestCaptain);
			return;
		}
		next.IDs = requestIDs;
		next.CaptainPetID = requestCaptain;
	}

	const std::vector<UINT> previousSlots(previous.IDs.begin(), previous.IDs.end());
	const std::vector<UINT> nextSlots(next.IDs.begin(), next.IDs.end());
	const UINT previousFightPower = hadPrevious ? QQS_SumPKArrayFightPower(pets, previousSlots) : 0;
	const UINT nextFightPower = QQS_SumPKArrayFightPower(pets, nextSlots);
	const std::int64_t fightPowerDiff =
		static_cast<std::int64_t>(nextFightPower) - static_cast<std::int64_t>(previousFightPower);

	try
	{
		WireWriter writer;
		writer.U16(0); // selfLen
		writer.U32(Client->Uin);
		writer.U32(0); // official response Time
		writer.U16(0); // ResultID
		for (const UINT petID : next.IDs)
			writer.U32(petID);
		writer.U32(next.CaptainPetID);
		writer.I32(fightPowerDiff);
		writer.U32(nextFightPower);
		writer.U16(requestArrayID);
		const std::vector<BYTE> body = writer.FinishSelfLength();

		if (!QQS_StorePKFormationState(Client->Uin, next))
			return;
		QQS_DebugPrintf10001(
			"[PET-PK-ARRAY] 21120 committed Uin=%u Time=%u Auto=%u Array=%u Captain=%u TotalFP=%u Diff=%lld\n",
			Client->Uin, requestTime, static_cast<unsigned>(autoEmbattle),
			static_cast<unsigned>(usingArrayID), next.CaptainPetID, nextFightPower,
			static_cast<long long>(fightPowerDiff));
		QQS_SendPetStructuredBody(Client, 21120, body.data(), body.size(), "SEND-21120-CURRENT", Response);
	}
	catch (const std::exception& e)
	{
		QQS_DebugPrintf10001("[PET-PK-ARRAY] 21120 build failed Uin=%u Reason=%s\n", Client->Uin, e.what());
	}
}

void RequestPetTrialInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	// Current 21446 request: selfLen(12) + Uin + Time + TeamID.
	if (!Client || !Body || BodyLen != 12)
	{
		QQS_DebugPrintf10001("[PET-TRIAL] 21150 rejected: expected 12 bytes, got %zu Uin=%u\n",
			BodyLen, Client ? Client->Uin : 0);
		return;
	}

	BYTE* p = Body;
	const USHORT selfLen = Read16(p);
	const UINT requestUin = Read32(p);
	const UINT requestTime = Read32(p);
	const USHORT teamID = Read16(p);
	if (selfLen != BodyLen || requestUin != Client->Uin || teamID < 1 || teamID > 5)
	{
		QQS_DebugPrintf10001(
			"[PET-TRIAL] 21150 rejected: selfLen=%u bodyLen=%zu requestUin=%u clientUin=%u TeamID=%u\n",
			selfLen, BodyLen, requestUin, Client->Uin, static_cast<unsigned>(teamID));
		return;
	}

	char extra[96] = {};
	sprintf_s(extra, "requestTime=%u teamID=%u", requestTime, static_cast<unsigned>(teamID));
	QQS_DebugDumpPetFlow(Client, 21150, Body, BodyLen, "RECV-21150-CURRENT", extra);
	ResponsePetTrialInfo(Client, teamID);
}

void ResponsePetTrialInfo(std::shared_ptr<ClientNode> Client, USHORT TeamID)
{
	if (!Client)
		return;

	QQS_DebugPrintf10001("[PET-TRIAL] 21150 not sent: current selected-team dynamic records are unavailable Uin=%u TeamID=%u\n",
		Client->Uin, (unsigned)TeamID);
}

void RequestUpdatePetName(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	// Current 21446 request object: selfLen + Uin + Time + Name[23].
	if (!Client || !Body || BodyLen != 33 || !Client->Pet || Client->Pet->ID == 0)
	{
		QQS_DebugPrintf10001("[PET-NAME] 178 rejected: expected 33 bytes and an equipped pet, got %zu Uin=%u\n",
			BodyLen, Client ? Client->Uin : 0);
		return;
	}

	BYTE* p = Body;
	const USHORT selfLen = Read16(p);
	const UINT requestUin = Read32(p);
	const UINT requestTime = Read32(p);
	char petName[24] = {};
	memcpy(petName, p, 23);

	const char* const terminator = static_cast<const char*>(memchr(petName, '\0', 23));
	const size_t nameLength = terminator ? static_cast<size_t>(terminator - petName) : 23;
	if (selfLen != BodyLen || requestUin != Client->Uin || nameLength == 0)
	{
		QQS_DebugPrintf10001(
			"[PET-NAME] 178 rejected: selfLen=%u bodyLen=%zu requestUin=%u clientUin=%u nameBytes=%zu\n",
			selfLen, BodyLen, requestUin, Client->Uin, nameLength);
		return;
	}

	std::unique_ptr<char[]> utf8Name(G2U(petName));
	if (!utf8Name || utf8Name[0] == '\0')
	{
		QQS_DebugPrintf10001("[PET-NAME] 178 rejected: name conversion failed Uin=%u PetID=%u\n",
			Client->Uin, Client->Pet->ID);
		return;
	}

	DBConnection myDBConnection(player);
	auto conn = myDBConnection.getConnection();
	if (!conn)
		return;
	try
	{
		std::unique_ptr<sql::PreparedStatement> prep_stmt(conn->prepareStatement("UPDATE Pet SET Name=? WHERE Uin = ? AND PetId = ?;"));
		prep_stmt->setString(1, utf8Name.get());
		prep_stmt->setUInt(2, Client->Uin);
		prep_stmt->setUInt(3, Client->Pet->ID);
		const int affectedRows = prep_stmt->executeUpdate();
		if (affectedRows != 1)
		{
			QQS_DebugPrintf10001("[PET-NAME] 178 rejected: database affectedRows=%d Uin=%u PetID=%u\n",
				affectedRows, Client->Uin, Client->Pet->ID);
			return;
		}
	}
	catch (const std::exception& e)
	{
		QQS_DebugPrintf10001("[PET-NAME] 178 database error Uin=%u PetID=%u Reason=%s\n",
			Client->Uin, Client->Pet->ID, e.what());
		return;
	}

	memset(Client->Pet->Name, 0, sizeof(Client->Pet->Name));
	memcpy(Client->Pet->Name, petName, nameLength);
	QQS_DebugPrintf10001("[PET-NAME] 178 committed Uin=%u PetID=%u Time=%u NameBytes=%zu\n",
		Client->Uin, Client->Pet->ID, requestTime, nameLength);
	ResponseUpdatePetName(Client);
	NotifyPetUpdateName(Client, Client->Pet->ID);
}

void ResponseUpdatePetName(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;
	std::vector<BYTE> buf(6);
	BYTE* p = buf.data();
	BYTE* const outer = p;
	Write16(p, 0); // CResponseUpdatePetNamePara selfLen
	Write32(p, 0); // Result
	const size_t len = static_cast<size_t>(p - buf.data());
	Set16(outer, static_cast<WORD>(len));
	SendToClientOfficial14(Client, 178, buf.data(), static_cast<DWORD>(len), Client->ConnID, FE_PLAYER, 1, Response);
}

void NotifyPetUpdateName(std::shared_ptr<ClientNode> Client, UINT PetID)
{
	if (!Client || !Client->Pet || PetID == 0 || Client->Pet->ID != PetID)
		return;

	std::vector<BYTE> buf(33);
	BYTE* p = buf.data();
	BYTE* const outer = p;
	Write16(p, 0); // CNotifyPetUpdateNamePara selfLen
	Write32(p, Client->Uin);
	Write32(p, PetID);
	memcpy(p, Client->Pet->Name, sizeof(Client->Pet->Name));
	p += sizeof(Client->Pet->Name);
	const size_t len = static_cast<size_t>(p - buf.data());
	Set16(outer, static_cast<WORD>(len));
	QQS_SendPetStructuredBody(Client, 590, buf.data(), len, "SEND-590-CURRENT", Notify);
}

void ResponseChangePetEquipStatus(std::shared_ptr<ClientNode> Client, int NewStatus, USHORT ResultID)
{
	if (!Client)
		return;

	std::vector<BYTE> buf(5);
	BYTE* p = buf.data();
	Write16(p, ResultID);
	Write8(p, static_cast<UCHAR>(NewStatus));
	Write16(p, 0); // ReasonLen
	SendToClientWithWireDstID(Client, 15398, buf.data(), static_cast<DWORD>(p - buf.data()),
		Client->ConnID, FE_PLAYER, Client->ConnID, Response);
}

void RequestChangePetEquipStatus(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (!Client || !Body || BodyLen < 13 || !Client->IsLogin)
		return;

	BYTE* p = Body;
	BYTE* const end = Body + BodyLen;
	if (BodyLen >= 15)
	{
		BYTE* afterLength = p;
		const USHORT wireLength = Read16(afterLength);
		if (wireLength == BodyLen || wireLength + sizeof(USHORT) == BodyLen)
			p = afterLength;
	}
	if (p + 13 > end)
	{
		ResponseChangePetEquipStatus(Client, 0, 1);
		return;
	}

	const UINT requestUin = Read32(p);
	(void)Read32(p); // Time
	const UINT petID = Read32(p);
	const UINT newStatus = Read8(p);
	if (requestUin != Client->Uin || petID == 0 || newStatus < 1 || newStatus > 5 ||
		!QQSPetConfig::HasPet(petID))
	{
		QQS_DebugPrintf10001(
			"[PET-STATUS] rejected Uin=%u AuthUin=%u PetID=%u Status=%u BodyLen=%zu\n",
			requestUin, Client->Uin, petID, newStatus, BodyLen);
		ResponseChangePetEquipStatus(Client, 0, 1);
		return;
	}

	PetInfo pet{};
	if (!QQS_LoadPetInfo(Client->Uin, petID, pet))
	{
		QQS_DebugPrintf10001("[PET-STATUS] unowned/missing pet Uin=%u PetID=%u\n",
			Client->Uin, petID);
		ResponseChangePetEquipStatus(Client, 0, 1);
		return;
	}

	if (!QQS_IsPetFormStatusAllowed(newStatus, pet.StrengLevel))
	{
		QQS_DebugPrintf10001(
			"[PET-STATUS] locked form rejected Uin=%u PetID=%u Status=%u Strength=%u\n",
			Client->Uin, petID, newStatus, pet.StrengLevel);
		ResponseChangePetEquipStatus(Client, 0, 1);
		return;
	}

	if (pet.Status != newStatus)
	{
		DBConnection connection(player);
		auto conn = connection.getConnection();
		if (!conn)
		{
			ResponseChangePetEquipStatus(Client, 0, 1);
			return;
		}

		try
		{
			std::unique_ptr<sql::PreparedStatement> statement(
				conn->prepareStatement("UPDATE Pet SET Status=? WHERE Uin=? AND PetID=? AND Status=?;"));
			statement->setUInt(1, newStatus);
			statement->setUInt(2, Client->Uin);
			statement->setUInt(3, petID);
			statement->setUInt(4, pet.Status);
			if (statement->executeUpdate() != 1)
				throw std::runtime_error("pet status conditional update failed");
		}
		catch (const std::exception& e)
		{
			QQS_DebugPrintf10001("[PET-STATUS] update failed Uin=%u PetID=%u Reason=%s\n",
				Client->Uin, petID, e.what());
			ResponseChangePetEquipStatus(Client, 0, 1);
			return;
		}
	}

	if (!QQS_LoadPetInfo(Client->Uin, petID, pet))
	{
		ResponseChangePetEquipStatus(Client, 0, 1);
		return;
	}

	{
		std::unique_lock<std::shared_mutex> lock(Client->mtx);
		if ((Client->Pet && Client->Pet->ID == petID) || Client->PetID == petID)
		{
			if (!Client->Pet)
				Client->Pet = std::make_shared<PetInfo>();
			*Client->Pet = pet;
			Client->PetID = petID;
			Client->PetStatus = static_cast<char>(pet.Status);
		}
	}

	QQS_DebugPrintf10001("[PET-STATUS] changed Uin=%u PetID=%u Status=%u\n",
		Client->Uin, petID, pet.Status);
	ResponseChangePetEquipStatus(Client, static_cast<int>(pet.Status), 0);
	NotifyBroadcastPetEvolution(Client, petID, pet.Status);
}

void NotifyBroadcastPetEvolution(std::shared_ptr<ClientNode> Client, UINT PetID, UINT Status)
{
	if (!Client || PetID == 0 || Status > 0xFFu)
		return;

	PetInfo pet{};
	if (Client->Pet && Client->Pet->ID == PetID)
		pet = *Client->Pet;
	else if (!QQS_LoadPetInfo(Client->Uin, PetID, pet))
		return;
	pet.Status = Status;
	if (!QQS_CompletePetInfo(pet))
	{
		QQS_DebugPrintf10001("[PET] 587 rejected invalid pet state Uin=%u PetID=%u Status=%u\n", Client->Uin, PetID, Status);
		return;
	}
	if (Client->Pet && Client->Pet->ID == PetID)
		*Client->Pet = pet;

	std::vector<BYTE> buf(4096);
	BYTE* p = buf.data();
	BYTE* const outer = p;
	Write16(p, 0); // CNotifyBroadcastPetEvolutionPara selfLen
	if (!QQS_WriteVisiblePetInfo(p, Client->Uin, pet))
		return;
	const size_t len = static_cast<size_t>(p - buf.data());
	Set16(outer, static_cast<WORD>(len));
	QQS_SendPetStructuredBody(Client, 587, buf.data(), len, "SEND-587-CURRENT", Notify);
}
void NotifyPetStrengCfgInfo(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;
	const std::vector<BYTE>& body = QQSPetConfig::StrengthHeaderBody();
	if (body.empty())
	{
		QQS_DebugPrintf10001("[PET-CONFIG] msg20100 rejected: runtime table is unavailable\n");
		return;
	}
	QQS_SendPetConfigNotify(Client, 20100, body.data(), body.size(), "SEND-20100-FIVE-TABLE");
	NotifyPetStrengCfgMoreInfo(Client);
	NotifyPetStrengPetID(Client);
	NotifyPetHuanHuaCfgInfo(Client);
	NotifyAvatarSynthesisCfg(Client);
}
void NotifyPetStrengCfgMoreInfo(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;
	const std::vector<std::vector<BYTE>>& bodies = QQSPetConfig::StrengthDetailBodies();
	if (bodies.empty())
	{
		QQS_DebugPrintf10001("[PET-CONFIG] msg28228 rejected: no configured detail packets\n");
		return;
	}
	for (const std::vector<BYTE>& body : bodies)
		QQS_SendPetConfigNotify(Client, 28228, body.data(), body.size(), "SEND-28228-FIVE-TABLE");
}
void NotifyPetStrengPetID(std::shared_ptr<ClientNode> Client)
{
	const std::vector<BYTE>& body = QQSPetConfig::StrengthPetIDBody();
	if (!Client || body.empty())
	{
		QQS_DebugPrintf10001("[PET-CONFIG] msg24074 rejected: runtime table is unavailable\n");
		return;
	}
	QQS_SendPetConfigNotify(Client, 24074, body.data(), body.size(), "SEND-24074-FIVE-TABLE");
}

static bool QQS_GetPetNormalSkills(UINT petID, std::vector<QQSPetConfig::Skill>& skills)
{
	if (!QQSPetConfig::GetBaseSkills(petID, skills) || skills.size() > 16)
	{
		skills.clear();
		return false;
	}
	return true;
}

static void QQS_WritePetSkillInfo(BYTE*& p, const QQSPetConfig::Skill& skill)
{
	BYTE* const record = p;
	Write16(p, 0);
	Write16(p, skill.SkillID);
	Write8(p, skill.Status);
	Write8(p, skill.Active);
	Write32(p, skill.Value);
	Set16(record, static_cast<WORD>(p - record));
}

static void QQS_WritePetCommonInfo(BYTE*& p, const PetInfo& pet)
{
	BYTE* const record = p;
	Write16(p, 0);
	Write32(p, pet.HuanHuaLevel);
	Write8(p, pet.CanHuanHua);
	Write8(p, pet.CanDrive);
	Write8(p, pet.Drive);
	Set16(record, static_cast<WORD>(p - record));
}

static void QQS_WriteU64(BYTE*& p, unsigned long long value)
{
	Write32(p, static_cast<UINT>(value >> 32));
	Write32(p, static_cast<UINT>(value & 0xFFFFFFFFull));
}

bool QQS_WriteFullPetInfo(BYTE*& p, UINT Uin, const PetInfo& source)
{
	PetInfo pet = source;
	if (Uin == 0 || !QQS_CompletePetInfo(pet))
		return false;

	std::vector<QQSPetConfig::Skill> skills;
	if (!QQS_GetPetNormalSkills(pet.ID, skills))
		return false;

	BYTE* const record = p;
	Write16(p, 0);
	Write32(p, Uin);
	Write32(p, pet.ID);
	memcpy(p, pet.Name, sizeof(pet.Name));
	p += sizeof(pet.Name);
	Write8(p, pet.IsProper);
	Write16(p, pet.LevelLimit);
	Write16(p, static_cast<USHORT>(pet.Level));
	Write32(p, pet.Exp);
	Write32(p, pet.MaxExp);
	Write8(p, static_cast<UCHAR>(pet.Status));
	Write16(p, static_cast<USHORT>(pet.PL));
	Write16(p, pet.TotalPL);
	Write16(p, static_cast<USHORT>(skills.size()));
	for (const QQSPetConfig::Skill& skill : skills)
		QQS_WritePetSkillInfo(p, skill);
	Write32(p, pet.StrengLevel);
	Write8(p, pet.TopStatus);
	QQS_WritePetCommonInfo(p, pet);

	const size_t recordLen = static_cast<size_t>(p - record);
	if (recordLen > 0xFFFFu)
		return false;
	Set16(record, static_cast<WORD>(recordLen));
	return true;
}

bool QQS_WriteVisiblePetInfo(BYTE*& p, UINT Uin, const PetInfo& source)
{
	PetInfo pet = source;
	if (Uin == 0 || !QQS_CompletePetInfo(pet))
		return false;

	std::vector<QQSPetConfig::Skill> skills;
	if (!QQS_GetPetNormalSkills(pet.ID, skills))
		return false;

	BYTE* const record = p;
	Write16(p, 0);
	Write32(p, Uin);
	Write32(p, pet.ID);
	memcpy(p, pet.Name, sizeof(pet.Name));
	p += sizeof(pet.Name);
	Write8(p, pet.IsProper);
	Write16(p, static_cast<USHORT>(pet.Level));
	Write8(p, static_cast<UCHAR>(pet.Status));
	Write16(p, static_cast<USHORT>(skills.size()));
	for (const QQSPetConfig::Skill& skill : skills)
		QQS_WritePetSkillInfo(p, skill);
	Write32(p, pet.StrengLevel);
	Write8(p, pet.TopStatus);
	QQS_WritePetCommonInfo(p, pet);
	Write16(p, pet.PKStar);
	Write32(p, pet.PetSkinID);
	QQS_WriteU64(p, pet.RoleID);

	const size_t recordLen = static_cast<size_t>(p - record);
	if (recordLen > 0xFFFFu)
		return false;
	Set16(record, static_cast<WORD>(recordLen));
	return true;
}

void NotifyPetEvolution(std::shared_ptr<ClientNode> Client, UINT Result, UCHAR PreStatus)
{
	if (!Client || !Client->Pet || Client->Pet->ID == 0)
		return;

	PetInfo pet = *Client->Pet;
	if (!QQS_CompletePetInfo(pet))
	{
		if (!QQS_LoadPetInfo(Client->Uin, Client->Pet->ID, pet))
			return;
	}
	*Client->Pet = pet;

	std::vector<BYTE> buf(4096);
	BYTE* p = buf.data();
	BYTE* const outer = p;
	Write16(p, 0); // CNotifyPetEvolutionPara selfLen
	Write32(p, Result);
	Write8(p, PreStatus);
	if (!QQS_WriteFullPetInfo(p, Client->Uin, pet))
		return;
	Write16(p, 0); // MsgLen

	const size_t len = static_cast<size_t>(p - buf.data());
	if (len > 0xFFFFu)
		return;
	Set16(outer, static_cast<WORD>(len));
	QQS_DebugDumpPetFlow(Client, 584, buf.data(), len, "SEND-584-CURRENT");
	QQS_SendPetStructuredBody(Client, 584, buf.data(), len, "SEND-584-CURRENT", Response);
}

static void QQS_ResponseGetPetInfoList(std::shared_ptr<ClientNode> Client, const std::vector<PetInfo>& Pets)
{
	if (!Client || Pets.size() > 32)
		return;
	std::vector<PetInfo> validated = Pets;
	for (PetInfo& pet : validated)
	{
		if (!QQS_CompletePetInfo(pet))
			return;
	}

	std::vector<BYTE> buf(819200);
	BYTE* p = buf.data();
	BYTE* const outer = p;
	Write16(p, 0); // CResponseGetPetInfoPara selfLen
	Write32(p, Client->Uin);
	Write16(p, static_cast<USHORT>(validated.size()));
	for (const PetInfo& pet : validated)
	{
		if (!QQS_WriteFullPetInfo(p, Client->Uin, pet))
			return;
	}

	const size_t len = static_cast<size_t>(p - buf.data());
	if (len > 0xFFFFu)
		return;
	Set16(outer, static_cast<WORD>(len));
	{
		char extra[96] = {};
		const UINT firstPet = validated.empty() ? 0 : validated.front().ID;
		sprintf_s(extra, "petNum=%zu firstPet=%u", validated.size(), firstPet);
		QQS_DebugDumpPetFlow(Client, 24528, buf.data(), len, "SEND-24528-CURRENT", extra);
	}
	QQS_SendPetStructuredBody(Client, 24528, buf.data(), len, "SEND-24528-CURRENT", Response);
}

void RequestGetPetInfo(std::shared_ptr<ClientNode> Client, BYTE* Body, size_t BodyLen)
{
	if (!Client || !Body || BodyLen < 12)
		return;

	BYTE* p = Body;
	const USHORT selfLen = Read16(p);
	const UINT requestUin = Read32(p);
	const UINT requestTime = Read32(p);
	const USHORT petNum = Read16(p);
	const size_t expectedLen = 12u + static_cast<size_t>(petNum) * 4u;
	if (selfLen != BodyLen || expectedLen != BodyLen || requestUin != Client->Uin || petNum > 32)
	{
		QQS_DebugPrintf10001(
			"[PET] 24528 rejected selfLen=%u bodyLen=%zu expected=%zu requestUin=%u clientUin=%u petNum=%u\n",
			selfLen, BodyLen, expectedLen, requestUin, Client->Uin, petNum);
		return;
	}

	std::vector<PetInfo> pets;
	std::set<UINT> requestedIDs;
	pets.reserve(petNum);
	for (USHORT i = 0; i < petNum; ++i)
	{
		const UINT petId = Read32(p);
		PetInfo pet{};
		if (petId == 0 || !requestedIDs.insert(petId).second || !QQS_LoadPetInfo(Client->Uin, petId, pet))
		{
			QQS_DebugPrintf10001("[PET] 24528 rejected unavailable/duplicate PetID=%u Uin=%u\n", petId, Client->Uin);
			return;
		}
		pets.push_back(pet);
	}

	QQS_DebugPrintf10001("[PET] 24528 accepted Uin=%u Time=%u PetNum=%u\n", Client->Uin, requestTime, petNum);
	QQS_ResponseGetPetInfoList(Client, pets);
}

void ResponseGetPetInfo(std::shared_ptr<ClientNode> Client)
{
	if (!Client || !Client->Pet || Client->Pet->ID == 0)
		return;
	PetInfo pet{};
	if (!QQS_LoadPetInfo(Client->Uin, Client->Pet->ID, pet))
		return;
	*Client->Pet = pet;
	QQS_ResponseGetPetInfoList(Client, std::vector<PetInfo>{ pet });
}
