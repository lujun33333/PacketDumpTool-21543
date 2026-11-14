#include "TopUIItemInfo25116.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "yaml-cpp/yaml.h"

namespace {

constexpr const char* kConfigPath = ".\\Config\\TopUIItemInfo25116.yml";

struct TopItem
{
	BYTE Type;
	WORD ID;
	WORD Tag;
	std::string NameHex;
	std::string PromptHex;
	std::string TailHex;
};

struct Mode
{
	WORD ID;
	WORD Status;
	DWORD Privilege;
	std::string PromptHex;
	std::string TailHex;
};

struct Entry
{
	WORD ID;
	BYTE Status;
	std::string PromptHex;
	std::string TailHex;
};

struct MouseOver
{
	WORD ID;
	std::string PromptHex;
	std::string TailHex;
};

struct Config
{
	std::vector<TopItem> TopItems;
	std::vector<Mode> Modes;
	std::vector<Entry> Entries;
	std::vector<Entry> EntrySet0;
	std::vector<MouseOver> MouseOvers;
	size_t PayloadLength = 0;
};

std::string Hex(const YAML::Node& row, const char* key)
{
	const std::string value = row[key].as<std::string>();
	if ((value.size() & 1U) != 0)
		throw std::runtime_error(std::string(key) + " has odd hex length");
	return value;
}

size_t HexLength(const std::string& value)
{
	return value.size() / 2;
}

YAML::Node List(const YAML::Node& root, const char* key)
{
	const YAML::Node value = root[key];
	if (!value || !value.IsSequence())
		throw std::runtime_error(std::string("missing list: ") + key);
	return value;
}

Config LoadConfig()
{
	const YAML::Node root = YAML::LoadFile(kConfigPath);
	Config config;

	for (const YAML::Node& row : List(root, "top_items"))
	{
		config.TopItems.push_back({
			static_cast<BYTE>(row["type"].as<unsigned int>()),
			static_cast<WORD>(row["id"].as<unsigned int>()),
			static_cast<WORD>(row["tag"].as<unsigned int>()),
			Hex(row, "name_hex"),
			Hex(row, "prompt_hex"),
			Hex(row, "tail_hex") });
	}
	for (const YAML::Node& row : List(root, "recommend_modes"))
	{
		config.Modes.push_back({
			static_cast<WORD>(row["id"].as<unsigned int>()),
			static_cast<WORD>(row["status"].as<unsigned int>()),
			static_cast<DWORD>(row["privilege"].as<unsigned long>()),
			Hex(row, "prompt_hex"),
			Hex(row, "tail_hex") });
	}
	for (const YAML::Node& row : List(root, "entry_list"))
	{
		config.Entries.push_back({
			static_cast<WORD>(row["id"].as<unsigned int>()),
			static_cast<BYTE>(row["status"].as<unsigned int>()),
			Hex(row, "prompt_hex"),
			Hex(row, "tail_hex") });
	}
	for (const YAML::Node& row : List(root, "entry_set_0"))
	{
		config.EntrySet0.push_back({
			static_cast<WORD>(row["id"].as<unsigned int>()),
			static_cast<BYTE>(row["status"].as<unsigned int>()),
			Hex(row, "prompt_hex"),
			Hex(row, "tail_hex") });
	}
	for (const YAML::Node& row : List(root, "mouseover_tips"))
	{
		config.MouseOvers.push_back({
			static_cast<WORD>(row["id"].as<unsigned int>()),
			Hex(row, "prompt_hex"),
			Hex(row, "tail_hex") });
	}

	if (config.TopItems.size() > (std::numeric_limits<WORD>::max)() ||
		config.Modes.size() > (std::numeric_limits<WORD>::max)() ||
		config.Entries.size() > (std::numeric_limits<BYTE>::max)() ||
		config.EntrySet0.size() > (std::numeric_limits<BYTE>::max)() ||
		config.MouseOvers.size() > (std::numeric_limits<BYTE>::max)())
	{
		throw std::runtime_error("25116 YAML list is too large for the wire count");
	}

	size_t topLength = 0;
	for (const TopItem& row : config.TopItems)
		topLength += 11 + HexLength(row.NameHex) + HexLength(row.PromptHex) + HexLength(row.TailHex);
	size_t modeLength = 0;
	for (const Mode& row : config.Modes)
		modeLength += 12 + HexLength(row.PromptHex) + HexLength(row.TailHex);
	size_t entryLength = 0;
	for (const Entry& row : config.Entries)
		entryLength += 7 + HexLength(row.PromptHex) + HexLength(row.TailHex);
	size_t setLength = 0;
	for (const Entry& row : config.EntrySet0)
		setLength += 7 + HexLength(row.PromptHex) + HexLength(row.TailHex);
	size_t mouseLength = 0;
	for (const MouseOver& row : config.MouseOvers)
		mouseLength += 6 + HexLength(row.PromptHex) + HexLength(row.TailHex);

	config.PayloadLength =
		4 + 2 + topLength +
		4 + modeLength +
		1 + entryLength +
		1 + 3 + setLength +
		1 + mouseLength;
	if (config.PayloadLength + 2 > (std::numeric_limits<WORD>::max)())
		throw std::runtime_error("25116 YAML payload is too large");
	return config;
}

void WriteRawHex(BYTE*& p, const std::string& value)
{
	if (!value.empty())
		WriteHex(p, value.c_str());
}

void WriteHexField(BYTE*& p, const std::string& value)
{
	Write16(p, static_cast<WORD>(HexLength(value)));
	WriteRawHex(p, value);
}

void WriteTopItem(BYTE*& p, const TopItem& row)
{
	BYTE* block = p;
	Write16(p, 0);
	Write8(p, row.Type);
	Write16(p, row.ID);
	Write16(p, row.Tag);
	WriteHexField(p, row.NameHex);
	WriteHexField(p, row.PromptHex);
	WriteRawHex(p, row.TailHex);
	Set16(block, static_cast<WORD>(p - block));
}

void WriteMode(BYTE*& p, const Mode& row)
{
	BYTE* block = p;
	Write16(p, 0);
	Write16(p, row.ID);
	Write16(p, row.Status);
	Write32(p, row.Privilege);
	WriteHexField(p, row.PromptHex);
	WriteRawHex(p, row.TailHex);
	Set16(block, static_cast<WORD>(p - block));
}

void WriteEntry(BYTE*& p, const Entry& row)
{
	BYTE* block = p;
	Write16(p, 0);
	Write16(p, row.ID);
	Write8(p, row.Status);
	WriteHexField(p, row.PromptHex);
	WriteRawHex(p, row.TailHex);
	Set16(block, static_cast<WORD>(p - block));
}

void WriteMouseOver(BYTE*& p, const MouseOver& row)
{
	BYTE* block = p;
	Write16(p, 0);
	Write16(p, row.ID);
	WriteHexField(p, row.PromptHex);
	WriteRawHex(p, row.TailHex);
	Set16(block, static_cast<WORD>(p - block));
}

} // namespace

std::vector<BYTE> BuildTopUIItemInfo25116Payload(DWORD uin)
{
	const Config config = LoadConfig();
	std::vector<BYTE> payload(config.PayloadLength);
	BYTE* p = payload.data();

	Write32(p, uin);
	Write16(p, static_cast<WORD>(config.TopItems.size()));
	for (const TopItem& row : config.TopItems)
		WriteTopItem(p, row);

	BYTE* modeBlock = p;
	Write16(p, 0);
	Write16(p, static_cast<WORD>(config.Modes.size()));
	for (const Mode& row : config.Modes)
		WriteMode(p, row);
	Set16(modeBlock, static_cast<WORD>(p - modeBlock));

	Write8(p, static_cast<BYTE>(config.Entries.size()));
	for (const Entry& row : config.Entries)
		WriteEntry(p, row);

	Write8(p, 1);
	BYTE* setBlock = p;
	Write16(p, 0);
	Write8(p, static_cast<BYTE>(config.EntrySet0.size()));
	for (const Entry& row : config.EntrySet0)
		WriteEntry(p, row);
	Set16(setBlock, static_cast<WORD>(p - setBlock));

	Write8(p, static_cast<BYTE>(config.MouseOvers.size()));
	for (const MouseOver& row : config.MouseOvers)
		WriteMouseOver(p, row);
	return payload;
}

void NotifyTopUIItemInfoNew(std::shared_ptr<ClientNode> Client)
{
	if (!Client)
		return;

	try
	{
		const std::vector<BYTE> payload = BuildTopUIItemInfo25116Payload(Client->Uin);
		QQS_DebugPrintf10001("[25116-YML] send Uin=%u PayloadLen=%u WireBodyLen=%u\n",
			Client->Uin,
			static_cast<unsigned int>(payload.size()),
			static_cast<unsigned int>(payload.size() + 2));
		SendToClient(Client, 25116, payload.data(), static_cast<DWORD>(payload.size()), Client->ConnID, FE_PLAYER, 1, Notify);
	}
	catch (const std::exception& e)
	{
		QQS_DebugPrintf10001("[25116-YML] load failed Uin=%u Error=%s\n", Client->Uin, e.what());
	}
}
