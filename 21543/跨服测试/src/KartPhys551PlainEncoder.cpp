#include "KartPhys551PlainEncoder.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace QQSpeedKartPhys551
{
namespace
{
using i64 = long long;

struct PairParam
{
	i64 m_iKey = 0;
	i64 m_iValue = 0;
	std::vector<i64> m_aiReserved20;
};

struct PhysInfo
{
	i64 m_uiPID = 0;
	i64 m_uiAffectID = 0;
	i64 m_iParam = 0;
	i64 m_iLifeTime = 0;
	std::vector<i64> m_iCommonPara;
	std::vector<i64> m_aiReserved84;
};

struct CompensateParam
{
	i64 m_iPercent = 0;
	i64 m_iValue = 0;
	std::vector<i64> m_aiReserved20;
};

struct CollisionBoxParam
{
	i64 m_iBoxLength = 0;
	i64 m_iBoxWidth = 0;
	i64 m_iBoxHeight = 0;
	i64 m_iAdjustOffset = 0;
	i64 m_iAdjustPickGap = 0;
	i64 m_iAdjustRemoteOffset = 0;
	i64 m_iAdjustBevelOffset = 0;
	i64 m_iControllerOffset = 0;
	std::vector<i64> m_aiReserved20;
	std::vector<i64> m_aiReservedD4;
};

struct CarToCarCollisionParam
{
	i64 m_iCollisionRadius = 0;
	i64 m_iCollisionLength = 0;
	i64 m_iCollisionWidth = 0;
	i64 m_iCollisionHeight = 0;
	i64 m_iCollisionLostCtrlTime = 0;
	std::vector<i64> m_aiReserved38;
};

struct AntiCollisionCfg
{
	std::vector<i64> m_aiPosition;
	std::vector<i64> m_aiReserved20;
};

struct BoatParam
{
	i64 m_iDirKeyForceParamC = 0;
	i64 m_iDirKeyForceParamD = 0;
	i64 m_iDTFLimitAngle = 0;
	i64 m_iDTFRatio = 0;
	i64 m_iUpKeyVecForce = 0;
	i64 m_iUpKeyVecForceParamA = 0;
	i64 m_iUpKeyVecForceParamB = 0;
	i64 m_iLRKeyVecForce = 0;
	i64 m_iLRKeyVecForceParamA = 0;
	i64 m_iLRKeyVecForceParamB = 0;
	i64 m_iVecForce = 0;
	i64 m_iVecForceParamA = 0;
	i64 m_iVecForceParamB = 0;
	std::vector<i64> m_iBoatShape;
	i64 m_iWecEffect = 0;
	i64 m_iBannerKeyTwist = 0;
	i64 m_iBannerKeyTwistParamA = 0;
	i64 m_iBannerKeyTwistParamB = 0;
	i64 m_iSuaiJianTwist = 0;
	i64 m_iBoatHeighRatio = 0;
	i64 m_iBannerTwist = 0;
	i64 m_iBannerTwistParamA = 0;
	i64 m_iDirUpKeyForce = 0;
	i64 m_iDirUpKeyForceParamA = 0;
	i64 m_iDirUpKeyForceParamB = 0;
	i64 m_iDirKeyForce = 0;
	i64 m_iDirKeyForceParamA = 0;
	i64 m_iDirKeyForceParamB = 0;
	i64 m_iAddNO2PerSec = 0;
	i64 m_iBoatAirValue = 0;
	i64 m_ifSlidFricForce = 0;
	std::vector<i64> m_aiReserved10;
	std::vector<i64> m_aiReservedBC;
	std::vector<i64> m_aiReserved168;
	std::vector<i64> m_aiReserved214;
	std::vector<i64> m_aiReserved2C0;
	std::vector<i64> m_aiReserved36C;
	std::vector<i64> m_aiReserved430;
};

struct KartPhys551Element
{
	i64 m_field26F4 = 0;
	std::vector<PhysInfo> m_astPhysInfo;
	std::vector<CompensateParam> m_astCompensateParam;
	CollisionBoxParam m_stCollisionBoxParam;
	CarToCarCollisionParam m_stCarToCarCollisionParam;

	i64 m_iBaseTurnRate = 0;
	i64 m_iVolatileTurnRate = 0;
	i64 m_iMaxTurnSpeed = 0;
	i64 m_iMinTurnSpeed = 0;
	i64 m_iMaxAccuTime = 0;
	i64 m_iBaseAccuRate = 0;
	i64 m_iMaxAffectSpeed = 0;
	i64 m_iGravity = 0;
	i64 m_iAdditionalLocalZSpeed = 0;
	i64 m_iStartVec = 0;
	i64 m_iEndVecFist = 0;
	i64 m_iEndVecSecon = 0;
	i64 m_iDirKeyForce = 0;
	i64 m_iDirKeyTwist = 0;
	i64 m_iBannerTwist = 0;
	i64 m_iBannerKeyTwist = 0;
	i64 m_iBannerVecForce = 0;
	i64 m_iBannerHeadForce = 0;
	i64 m_iSlidFricForce = 0;
	i64 m_iRollFricForce = 0;
	i64 m_iStartWec = 0;
	i64 m_iMaxWec = 0;
	i64 m_iSuaiJianTwist = 0;
	i64 m_iDirUpKeyForce = 0;

	std::vector<i64> m_aiAccelStatus;
	std::vector<PairParam> m_astForwardAccel;
	std::vector<PairParam> m_astForwardDecel;
	std::vector<PairParam> m_astBackwardAccel;
	std::vector<PairParam> m_astBackwardDecel;

	i64 m_iCrashCompensatePower = 0;
	i64 m_iDefenseRate = 0;
	i64 m_uiUin = 0;
	i64 m_iSpeedupCardGenRate = 0;
	i64 m_u32ExtraFuncFlag = 0;

	size_t m_hasSkateParaCount = 0;
	size_t m_astBattleModeSkillInfoListCount = 0;

	i64 m_iEnergyConvert = 0;
	i64 m_usSuperN2ORate = 0;
	i64 m_ushCollisionLoseRate = 0;
	i64 m_uchNotInterruptDrift = 0;

	std::vector<i64> m_auchAddWeight;
	std::vector<i64> m_auchWeight;
	std::vector<AntiCollisionCfg> m_astAntiCollistionCfg;
	std::vector<BoatParam> m_astBoatParam;
	std::vector<PairParam> m_astBoatForwardAccel;

	i64 m_iDirUpKeyForceParamA = 0;
	i64 m_iDirUpKeyForceParamB = 0;

	std::vector<i64> m_topSmallList;
	std::vector<i64> m_list1398;
	std::vector<i64> m_list2700;
	std::vector<i64> m_list28C4;
	std::vector<i64> m_list35F0;
	std::vector<i64> m_list3CD0;
	std::vector<i64> m_array4C34;
	std::vector<i64> m_array4CE8;
	std::vector<i64> m_array7460;
	std::vector<i64> m_array750C;
	std::vector<i64> m_array7700;
	std::vector<i64> m_array8A58;
};

struct KartPhys551Plain : KartPhys551Element
{
	i64 srcType = 1;
	i64 m_uiKartID = 0;
	std::string name;
	i64 m_topScalar = 976905698; // official wire scalar; not KartID.
	std::vector<KartPhys551Element> m_group0Elements;
	std::vector<KartPhys551Element> m_group1Elements;
	std::vector<KartPhys551Element> m_group2Elements;
	i64 m_topFinalScalar = 0;
	std::vector<i64> m_topFinalList;
};

static std::string LowerCopy(std::string v)
{
	std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return v;
}

static bool IsPresentNode(const YAML::Node& node)
{
	return node && !node.IsNull();
}

static YAML::Node NodeGetAny(const YAML::Node& node, const std::vector<std::string>& names)
{
	if (!IsPresentNode(node) || !node.IsMap())
		return YAML::Node();
	std::vector<std::string> loweredNames;
	loweredNames.reserve(names.size());
	for (const auto& name : names)
		loweredNames.push_back(LowerCopy(name));
	for (const auto& kv : node)
	{
		if (!kv.first.IsScalar())
			continue;
		const std::string key = LowerCopy(kv.first.as<std::string>());
		for (const auto& name : loweredNames)
		{
			if (key == name)
				return kv.second;
		}
	}
	return YAML::Node();
}

static std::vector<std::string> Names(const char* noM, const char* shortName = nullptr, const char* shortName2 = nullptr)
{
	std::vector<std::string> out;
	out.push_back(std::string("m_") + noM);
	out.push_back(noM);
	if (shortName && *shortName)
		out.push_back(shortName);
	if (shortName2 && *shortName2)
		out.push_back(shortName2);
	return out;
}

static YAML::Node FieldNode(const YAML::Node& node, const char* noM, const char* shortName = nullptr, const char* shortName2 = nullptr)
{
	return NodeGetAny(node, Names(noM, shortName, shortName2));
}

static YAML::Node PlainRoot(const YAML::Node& config)
{
	YAML::Node plain = config["Plain551"];
	if (IsPresentNode(plain) && plain.IsMap())
		return plain;
	return config;
}

static i64 AsInt(const YAML::Node& node, i64 defaultValue = 0)
{
	if (!IsPresentNode(node))
		return defaultValue;
	try
	{
		if (node.IsScalar())
		{
			const std::string text = node.as<std::string>();
			if (text.empty())
				return defaultValue;
			char* end = nullptr;
			const long long v = std::strtoll(text.c_str(), &end, 0);
			if (end && *end == '\0')
				return v;
		}
		return node.as<long long>();
	}
	catch (...)
	{
		return defaultValue;
	}
}

static std::string AsString(const YAML::Node& node, const std::string& defaultValue = std::string())
{
	if (!IsPresentNode(node))
		return defaultValue;
	try
	{
		return node.as<std::string>();
	}
	catch (...)
	{
		return defaultValue;
	}
}

static i64 ReadInt(const YAML::Node& node, const char* noM, const char* shortName = nullptr, i64 defaultValue = 0)
{
	return AsInt(FieldNode(node, noM, shortName), defaultValue);
}

static std::string ReadString(const YAML::Node& node, const char* noM, const char* shortName = nullptr)
{
	return AsString(FieldNode(node, noM, shortName));
}

static std::vector<i64> ReadIntListNode(const YAML::Node& node)
{
	std::vector<i64> out;
	if (!IsPresentNode(node) || !node.IsSequence())
		return out;
	for (const auto& v : node)
		out.push_back(AsInt(v));
	return out;
}

static std::vector<i64> ReadIntList(const YAML::Node& node, const char* noM, const char* shortName = nullptr)
{
	return ReadIntListNode(FieldNode(node, noM, shortName));
}

static std::vector<i64> ReadIntListAny(const YAML::Node& node, const std::vector<std::string>& names)
{
	return ReadIntListNode(NodeGetAny(node, names));
}

static i64 ReadPlainOrGroupInt(const YAML::Node& plain, const char* group, const char* noM, const char* shortName = nullptr, i64 defaultValue = 0)
{
	YAML::Node v = FieldNode(plain, noM, shortName);
	if (IsPresentNode(v))
		return AsInt(v, defaultValue);
	YAML::Node g = NodeGetAny(plain, { group });
	if (IsPresentNode(g) && g.IsMap())
		return ReadInt(g, noM, shortName, defaultValue);
	return defaultValue;
}

static size_t SequenceCount(const YAML::Node& node)
{
	if (!IsPresentNode(node) || !node.IsSequence())
		return 0;
	return node.size();
}

static std::vector<PairParam> ReadPairRows(const YAML::Node& rows)
{
	std::vector<PairParam> out;
	if (!IsPresentNode(rows) || !rows.IsSequence())
		return out;
	for (const auto& row : rows)
	{
		if (!IsPresentNode(row) || !row.IsMap())
			continue;
		PairParam r;
		r.m_iKey = ReadInt(row, "iKey", "Key");
		r.m_iValue = ReadInt(row, "iValue", "Value");
		r.m_aiReserved20 = ReadIntListAny(row, { "m_aiReserved20", "aiReserved20", "Reserved20", "ExtraValues", "extra_values", "array_20_values", "Array20Values", "Extra" });
		out.push_back(r);
	}
	return out;
}

static std::vector<PhysInfo> ReadPhysInfoRows(const YAML::Node& rows)
{
	std::vector<PhysInfo> out;
	if (!IsPresentNode(rows) || !rows.IsSequence())
		return out;
	for (const auto& row : rows)
	{
		if (!IsPresentNode(row) || !row.IsMap())
			continue;
		PhysInfo r;
		r.m_uiPID = ReadInt(row, "uiPID", "PID");
		r.m_uiAffectID = ReadInt(row, "uiAffectID", "AffectID");
		r.m_iParam = ReadInt(row, "iParam", "Param");
		r.m_iLifeTime = ReadInt(row, "iLifeTime", "LifeTime");
		r.m_iCommonPara = ReadIntList(row, "iCommonPara", "CommonPara");
		r.m_aiReserved84 = ReadIntListAny(row, { "m_aiReserved84", "aiReserved84", "Reserved84", "Extra84Values", "extra84_values", "array_84_values" });
		out.push_back(r);
	}
	return out;
}

static std::vector<CompensateParam> ReadCompensateRows(const YAML::Node& rows)
{
	std::vector<CompensateParam> out;
	if (!IsPresentNode(rows) || !rows.IsSequence())
		return out;
	for (const auto& row : rows)
	{
		if (!IsPresentNode(row) || !row.IsMap())
			continue;
		CompensateParam r;
		r.m_iPercent = ReadInt(row, "iPercent", "Percent");
		r.m_iValue = ReadInt(row, "iValue", "Value");
		r.m_aiReserved20 = ReadIntListAny(row, { "m_aiReserved20", "aiReserved20", "Reserved20", "ExtraValues", "extra_values", "array_20_values", "Array20Values", "Extra" });
		out.push_back(r);
	}
	return out;
}

static std::vector<AntiCollisionCfg> ReadAntiCollisionRows(const YAML::Node& rows)
{
	std::vector<AntiCollisionCfg> out;
	if (!IsPresentNode(rows) || !rows.IsSequence())
		return out;
	for (const auto& row : rows)
	{
		if (!IsPresentNode(row) || !row.IsMap())
			continue;
		AntiCollisionCfg r;
		r.m_aiPosition = ReadIntListAny(row, { "m_aiPosition", "aiPosition", "Positions", "positions", "list_10_values" });
		r.m_aiReserved20 = ReadIntListAny(row, { "m_aiReserved20", "aiReserved20", "Reserved20", "ExtraValues", "extra_values", "list_74_values", "array_20_values" });
		out.push_back(r);
	}
	return out;
}

static BoatParam ReadBoatParam(const YAML::Node& row)
{
	BoatParam r;
	r.m_iDirKeyForceParamC = ReadInt(row, "iDirKeyForceParamC", "DirKeyForceParamC");
	r.m_iDirKeyForceParamD = ReadInt(row, "iDirKeyForceParamD", "DirKeyForceParamD");
	r.m_iDTFLimitAngle = ReadInt(row, "iDTFLimitAngle", "DTFLimitAngle");
	r.m_iDTFRatio = ReadInt(row, "iDTFRatio", "DTFRatio");
	r.m_iUpKeyVecForce = ReadInt(row, "iUpKeyVecForce", "UpKeyVecForce");
	r.m_iUpKeyVecForceParamA = ReadInt(row, "iUpKeyVecForceParamA", "UpKeyVecForceParamA");
	r.m_iUpKeyVecForceParamB = ReadInt(row, "iUpKeyVecForceParamB", "UpKeyVecForceParamB");
	r.m_iLRKeyVecForce = ReadInt(row, "iLRKeyVecForce", "LRKeyVecForce");
	r.m_iLRKeyVecForceParamA = ReadInt(row, "iLRKeyVecForceParamA", "LRKeyVecForceParamA");
	r.m_iLRKeyVecForceParamB = ReadInt(row, "iLRKeyVecForceParamB", "LRKeyVecForceParamB");
	r.m_iVecForce = ReadInt(row, "iVecForce", "VecForce");
	r.m_iVecForceParamA = ReadInt(row, "iVecForceParamA", "VecForceParamA");
	r.m_iVecForceParamB = ReadInt(row, "iVecForceParamB", "VecForceParamB");
	r.m_iBoatShape = ReadIntList(row, "iBoatShape", "BoatShape");
	if (r.m_iBoatShape.size() > 3)
		r.m_iBoatShape.resize(3);
	while (r.m_iBoatShape.size() < 3)
		r.m_iBoatShape.push_back(0);
	r.m_iWecEffect = ReadInt(row, "iWecEffect", "WecEffect");
	r.m_iBannerKeyTwist = ReadInt(row, "iBannerKeyTwist", "BannerKeyTwist");
	r.m_iBannerKeyTwistParamA = ReadInt(row, "iBannerKeyTwistParamA", "BannerKeyTwistParamA");
	r.m_iBannerKeyTwistParamB = ReadInt(row, "iBannerKeyTwistParamB", "BannerKeyTwistParamB");
	r.m_iSuaiJianTwist = ReadInt(row, "iSuaiJianTwist", "SuaiJianTwist");
	r.m_iBoatHeighRatio = ReadInt(row, "iBoatHeighRatio", "BoatHeighRatio");
	r.m_iBannerTwist = ReadInt(row, "iBannerTwist", "BannerTwist");
	r.m_iBannerTwistParamA = ReadInt(row, "iBannerTwistParamA", "BannerTwistParamA");
	r.m_iDirUpKeyForce = ReadInt(row, "iDirUpKeyForce", "DirUpKeyForce");
	r.m_iDirUpKeyForceParamA = ReadInt(row, "iDirUpKeyForceParamA", "DirUpKeyForceParamA");
	r.m_iDirUpKeyForceParamB = ReadInt(row, "iDirUpKeyForceParamB", "DirUpKeyForceParamB");
	r.m_iDirKeyForce = ReadInt(row, "iDirKeyForce", "DirKeyForce");
	r.m_iDirKeyForceParamA = ReadInt(row, "iDirKeyForceParamA", "DirKeyForceParamA");
	r.m_iDirKeyForceParamB = ReadInt(row, "iDirKeyForceParamB", "DirKeyForceParamB");
	r.m_iAddNO2PerSec = ReadInt(row, "iAddNO2PerSec", "AddNO2PerSec");
	r.m_iBoatAirValue = ReadInt(row, "iBoatAirValue", "BoatAirValue");
	r.m_ifSlidFricForce = ReadInt(row, "ifSlidFricForce", "SlidFricForce");
	r.m_aiReserved10 = ReadIntListAny(row, { "m_aiReserved10", "aiReserved10", "Reserved10", "List10", "list_10_values" });
	r.m_aiReservedBC = ReadIntListAny(row, { "m_aiReservedBC", "aiReservedBC", "ReservedBC", "ArrayBC", "array_bc_values" });
	r.m_aiReserved168 = ReadIntListAny(row, { "m_aiReserved168", "aiReserved168", "Reserved168", "Array168", "array_168_values" });
	r.m_aiReserved214 = ReadIntListAny(row, { "m_aiReserved214", "aiReserved214", "Reserved214", "Array214", "array_214_values" });
	r.m_aiReserved2C0 = ReadIntListAny(row, { "m_aiReserved2C0", "aiReserved2C0", "Reserved2C0", "Array2C0", "array_2c0_values" });
	r.m_aiReserved36C = ReadIntListAny(row, { "m_aiReserved36C", "aiReserved36C", "Reserved36C", "Array36C", "array_36c_values" });
	r.m_aiReserved430 = ReadIntListAny(row, { "m_aiReserved430", "aiReserved430", "Reserved430", "Array430", "array_430_values" });
	return r;
}

static std::vector<BoatParam> ReadBoatRows(const YAML::Node& node)
{
	std::vector<BoatParam> out;
	if (!IsPresentNode(node))
		return out;
	if (node.IsMap())
	{
		out.push_back(ReadBoatParam(node));
		return out;
	}
	if (!node.IsSequence())
		return out;
	for (const auto& row : node)
	{
		if (IsPresentNode(row) && row.IsMap())
			out.push_back(ReadBoatParam(row));
	}
	return out;
}

static void ReadElementFields(const YAML::Node& plain, KartPhys551Element& out)
{
	out.m_field26F4 = AsInt(NodeGetAny(plain, { "Field26F4", "field26F4", "field_26f4", "m_field26F4" }), out.m_field26F4);
	YAML::Node wire = NodeGetAny(plain, { "WireScalars", "wireScalars", "wire_scalars", "OfficialWire", "official_wire" });
	if (IsPresentNode(wire) && wire.IsMap())
		out.m_field26F4 = AsInt(NodeGetAny(wire, { "Field26F4", "field26F4", "field_26f4", "m_field26F4" }), out.m_field26F4);

	out.m_astPhysInfo = ReadPhysInfoRows(FieldNode(plain, "astPhysInfo", "PhysInfo"));
	out.m_astCompensateParam = ReadCompensateRows(FieldNode(plain, "astCompensateParam", "CompensateParam"));

	YAML::Node box = FieldNode(plain, "stCollisionBoxParam", "CollisionBoxParam");
	out.m_stCollisionBoxParam.m_iBoxLength = ReadInt(box, "iBoxLength", "BoxLength");
	out.m_stCollisionBoxParam.m_iBoxWidth = ReadInt(box, "iBoxWidth", "BoxWidth");
	out.m_stCollisionBoxParam.m_iBoxHeight = ReadInt(box, "iBoxHeight", "BoxHeight");
	out.m_stCollisionBoxParam.m_iAdjustOffset = ReadInt(box, "iAdjustOffset", "AdjustOffset");
	out.m_stCollisionBoxParam.m_iAdjustPickGap = ReadInt(box, "iAdjustPickGap", "AdjustPickGap");
	out.m_stCollisionBoxParam.m_iAdjustRemoteOffset = ReadInt(box, "iAdjustRemoteOffset", "AdjustRemoteOffset");
	out.m_stCollisionBoxParam.m_iAdjustBevelOffset = ReadInt(box, "iAdjustBevelOffset", "AdjustBevelOffset");
	out.m_stCollisionBoxParam.m_iControllerOffset = ReadInt(box, "iControllerOffset", "ControllerOffset");
	out.m_stCollisionBoxParam.m_aiReserved20 = ReadIntListAny(box, { "m_aiReserved20", "aiReserved20", "Reserved20", "Extra20Values", "array_20_values" });
	out.m_stCollisionBoxParam.m_aiReservedD4 = ReadIntListAny(box, { "m_aiReservedD4", "aiReservedD4", "ReservedD4", "ExtraD4Values", "array_d4_values" });

	YAML::Node car = FieldNode(plain, "stCarToCarCollisionParam", "CarToCarCollisionParam");
	out.m_stCarToCarCollisionParam.m_iCollisionRadius = ReadInt(car, "iCollisionRadius", "CollisionRadius");
	out.m_stCarToCarCollisionParam.m_iCollisionLength = ReadInt(car, "iCollisionLength", "CollisionLength");
	out.m_stCarToCarCollisionParam.m_iCollisionWidth = ReadInt(car, "iCollisionWidth", "CollisionWidth");
	out.m_stCarToCarCollisionParam.m_iCollisionHeight = ReadInt(car, "iCollisionHeight", "CollisionHeight");
	out.m_stCarToCarCollisionParam.m_iCollisionLostCtrlTime = ReadInt(car, "iCollisionLostCtrlTime", "CollisionLostCtrlTime");
	out.m_stCarToCarCollisionParam.m_aiReserved38 = ReadIntListAny(car, { "m_aiReserved38", "aiReserved38", "Reserved38", "ExtraValues", "array_38_values" });

	out.m_iBaseTurnRate = ReadPlainOrGroupInt(plain, "Scalars", "iBaseTurnRate", "BaseTurnRate");
	out.m_iVolatileTurnRate = ReadPlainOrGroupInt(plain, "Scalars", "iVolatileTurnRate", "VolatileTurnRate");
	out.m_iMaxTurnSpeed = ReadPlainOrGroupInt(plain, "Scalars", "iMaxTurnSpeed", "MaxTurnSpeed");
	out.m_iMinTurnSpeed = ReadPlainOrGroupInt(plain, "Scalars", "iMinTurnSpeed", "MinTurnSpeed");
	out.m_iMaxAccuTime = ReadPlainOrGroupInt(plain, "Scalars", "iMaxAccuTime", "MaxAccuTime");
	out.m_iBaseAccuRate = ReadPlainOrGroupInt(plain, "Scalars", "iBaseAccuRate", "BaseAccuRate");
	out.m_iMaxAffectSpeed = ReadPlainOrGroupInt(plain, "Scalars", "iMaxAffectSpeed", "MaxAffectSpeed");
	out.m_iGravity = ReadPlainOrGroupInt(plain, "Scalars", "iGravity", "Gravity");
	out.m_iAdditionalLocalZSpeed = ReadPlainOrGroupInt(plain, "Scalars", "iAdditionalLocalZSpeed", "AdditionalLocalZSpeed");
	out.m_iStartVec = ReadPlainOrGroupInt(plain, "Scalars", "iStartVec", "StartVec");
	out.m_iEndVecFist = ReadPlainOrGroupInt(plain, "Scalars", "iEndVecFist", "EndVecFist");
	out.m_iEndVecSecon = ReadPlainOrGroupInt(plain, "Scalars", "iEndVecSecon", "EndVecSecon");
	out.m_iDirKeyForce = ReadPlainOrGroupInt(plain, "Scalars", "iDirKeyForce", "DirKeyForce");
	out.m_iDirKeyTwist = ReadPlainOrGroupInt(plain, "Scalars", "iDirKeyTwist", "DirKeyTwist");
	out.m_iBannerTwist = ReadPlainOrGroupInt(plain, "Scalars", "iBannerTwist", "BannerTwist");
	out.m_iBannerKeyTwist = ReadPlainOrGroupInt(plain, "Scalars", "iBannerKeyTwist", "BannerKeyTwist");
	out.m_iBannerVecForce = ReadPlainOrGroupInt(plain, "Scalars", "iBannerVecForce", "BannerVecForce");
	out.m_iBannerHeadForce = ReadPlainOrGroupInt(plain, "Scalars", "iBannerHeadForce", "BannerHeadForce");
	out.m_iSlidFricForce = ReadPlainOrGroupInt(plain, "Scalars", "iSlidFricForce", "SlidFricForce");
	out.m_iRollFricForce = ReadPlainOrGroupInt(plain, "Scalars", "iRollFricForce", "RollFricForce");
	out.m_iStartWec = ReadPlainOrGroupInt(plain, "Scalars", "iStartWec", "StartWec");
	out.m_iMaxWec = ReadPlainOrGroupInt(plain, "Scalars", "iMaxWec", "MaxWec");
	out.m_iSuaiJianTwist = ReadPlainOrGroupInt(plain, "Scalars", "iSuaiJianTwist", "SuaiJianTwist");
	out.m_iDirUpKeyForce = ReadPlainOrGroupInt(plain, "Scalars", "iDirUpKeyForce", "DirUpKeyForce");

	out.m_aiAccelStatus = ReadIntList(plain, "aiAccelStatus", "AccelStatus");
	out.m_astForwardAccel = ReadPairRows(FieldNode(plain, "astForwardAccel", "ForwardAccel"));
	out.m_astForwardDecel = ReadPairRows(FieldNode(plain, "astForwardDecel", "ForwardDecel"));
	out.m_astBackwardAccel = ReadPairRows(FieldNode(plain, "astBackwardAccel", "BackwardAccel"));
	out.m_astBackwardDecel = ReadPairRows(FieldNode(plain, "astBackwardDecel", "BackwardDecel"));

	out.m_iCrashCompensatePower = ReadPlainOrGroupInt(plain, "Tail", "iCrashCompensatePower", "CrashCompensatePower");
	out.m_iDefenseRate = ReadPlainOrGroupInt(plain, "Tail", "iDefenseRate", "DefenseRate");
	out.m_uiUin = ReadPlainOrGroupInt(plain, "Tail", "uiUin", "Uin", out.m_uiUin);
	out.m_iSpeedupCardGenRate = ReadPlainOrGroupInt(plain, "Tail", "iSpeedupCardGenRate", "SpeedupCardGenRate");
	out.m_u32ExtraFuncFlag = ReadPlainOrGroupInt(plain, "Tail", "u32ExtraFuncFlag", "ExtraFuncFlag");
	out.m_hasSkateParaCount = SequenceCount(FieldNode(plain, "hasSkatePara"));
	out.m_astBattleModeSkillInfoListCount = SequenceCount(FieldNode(plain, "astBattleModeSkillInfoList"));
	out.m_iEnergyConvert = ReadPlainOrGroupInt(plain, "Tail", "iEnergyConvert", "EnergyConvert");
	out.m_usSuperN2ORate = ReadPlainOrGroupInt(plain, "Tail", "usSuperN2ORate", "SuperN2ORate");
	out.m_ushCollisionLoseRate = ReadPlainOrGroupInt(plain, "Tail", "ushCollisionLoseRate", "CollisionLoseRate");
	out.m_uchNotInterruptDrift = ReadPlainOrGroupInt(plain, "Tail", "uchNotInterruptDrift", "NotInterruptDrift");

	out.m_auchAddWeight = ReadIntList(plain, "auchAddWeight", "AddWeight");
	out.m_auchWeight = ReadIntList(plain, "auchWeight", "Weight");
	out.m_astAntiCollistionCfg = ReadAntiCollisionRows(FieldNode(plain, "astAntiCollistionCfg", "AntiCollisionCfg"));
	out.m_astBoatParam = ReadBoatRows(FieldNode(plain, "astBoatParam", "BoatParam"));
	out.m_astBoatForwardAccel = ReadPairRows(FieldNode(plain, "astBoatForwardAccel", "BoatForwardAccel"));
	out.m_iDirUpKeyForceParamA = ReadPlainOrGroupInt(plain, "Tail", "iDirUpKeyForceParamA", "DirUpKeyForceParamA");
	out.m_iDirUpKeyForceParamB = ReadPlainOrGroupInt(plain, "Tail", "iDirUpKeyForceParamB", "DirUpKeyForceParamB");

	YAML::Node zero = NodeGetAny(plain, { "ZeroArrays", "zeroArrays", "zero_arrays" });
	if (!IsPresentNode(zero) || !zero.IsMap())
	{
		YAML::Node nested = NodeGetAny(wire, { "ZeroArrays", "zeroArrays", "zero_arrays" });
		zero = (IsPresentNode(nested) && nested.IsMap()) ? nested : wire;
	}
	out.m_topSmallList = ReadIntListAny(zero, { "TopSmallList", "topSmallList", "top_small_list" });
	out.m_list1398 = ReadIntListAny(zero, { "List1398", "list1398", "list_1398" });
	out.m_list2700 = ReadIntListAny(zero, { "List2700", "list2700", "list_2700" });
	out.m_list28C4 = ReadIntListAny(zero, { "List28C4", "list28C4", "list_28c4" });
	out.m_list35F0 = ReadIntListAny(zero, { "List35F0", "list35F0", "list_35f0" });
	out.m_list3CD0 = ReadIntListAny(zero, { "List3CD0", "list3CD0", "list_3cd0" });
	out.m_array4C34 = ReadIntListAny(zero, { "Array4C34", "array4C34", "array_4c34" });
	out.m_array4CE8 = ReadIntListAny(zero, { "Array4CE8", "array4CE8", "array_4ce8" });
	out.m_array7460 = ReadIntListAny(zero, { "Array7460", "array7460", "array_7460" });
	out.m_array750C = ReadIntListAny(zero, { "Array750C", "array750C", "array_750c" });
	out.m_array7700 = ReadIntListAny(zero, { "Array7700", "array7700", "array_7700" });
	out.m_array8A58 = ReadIntListAny(zero, { "Array8A58", "array8A58", "array_8a58" });
}

static bool Normalize(const YAML::Node& config, KartPhys551Plain& out)
{
	YAML::Node plain = PlainRoot(config);
	YAML::Node meta = config["Meta"];
	if (IsPresentNode(meta) && meta.IsMap())
	{
		out.srcType = AsInt(NodeGetAny(meta, { "SrcType", "src_type" }), 1);
		out.m_uiKartID = AsInt(NodeGetAny(meta, { "KartID", "kart_id", "LoadID", "load_id" }), 0);
		out.m_uiUin = AsInt(NodeGetAny(meta, { "Uin", "uin" }), 0);
	}

	out.srcType = ReadInt(plain, "srcType", "SrcType", out.srcType);
	out.m_uiKartID = ReadInt(plain, "uiKartID", "KartID", out.m_uiKartID);
	out.name = ReadString(plain, "name", "Name");
	out.m_field26F4 = out.m_uiKartID;

	YAML::Node wire = NodeGetAny(plain, { "WireScalars", "wireScalars", "wire_scalars", "OfficialWire", "official_wire" });
	if (IsPresentNode(wire) && wire.IsMap())
	{
		out.m_topScalar = AsInt(NodeGetAny(wire, { "TopScalar", "topScalar", "top_scalar", "m_topScalar" }), out.m_topScalar);
		out.m_field26F4 = AsInt(NodeGetAny(wire, { "Field26F4", "field26F4", "field_26f4", "m_field26F4" }), out.m_field26F4);
	}
	out.m_topScalar = AsInt(NodeGetAny(plain, { "TopScalar", "topScalar", "top_scalar", "m_topScalar" }), out.m_topScalar);
	out.m_field26F4 = AsInt(NodeGetAny(plain, { "Field26F4", "field26F4", "field_26f4", "m_field26F4" }), out.m_field26F4);

	out.m_astPhysInfo = ReadPhysInfoRows(FieldNode(plain, "astPhysInfo", "PhysInfo"));
	out.m_astCompensateParam = ReadCompensateRows(FieldNode(plain, "astCompensateParam", "CompensateParam"));

	YAML::Node box = FieldNode(plain, "stCollisionBoxParam", "CollisionBoxParam");
	out.m_stCollisionBoxParam.m_iBoxLength = ReadInt(box, "iBoxLength", "BoxLength");
	out.m_stCollisionBoxParam.m_iBoxWidth = ReadInt(box, "iBoxWidth", "BoxWidth");
	out.m_stCollisionBoxParam.m_iBoxHeight = ReadInt(box, "iBoxHeight", "BoxHeight");
	out.m_stCollisionBoxParam.m_iAdjustOffset = ReadInt(box, "iAdjustOffset", "AdjustOffset");
	out.m_stCollisionBoxParam.m_iAdjustPickGap = ReadInt(box, "iAdjustPickGap", "AdjustPickGap");
	out.m_stCollisionBoxParam.m_iAdjustRemoteOffset = ReadInt(box, "iAdjustRemoteOffset", "AdjustRemoteOffset");
	out.m_stCollisionBoxParam.m_iAdjustBevelOffset = ReadInt(box, "iAdjustBevelOffset", "AdjustBevelOffset");
	out.m_stCollisionBoxParam.m_iControllerOffset = ReadInt(box, "iControllerOffset", "ControllerOffset");
	out.m_stCollisionBoxParam.m_aiReserved20 = ReadIntListAny(box, { "m_aiReserved20", "aiReserved20", "Reserved20", "Extra20Values", "array_20_values" });
	out.m_stCollisionBoxParam.m_aiReservedD4 = ReadIntListAny(box, { "m_aiReservedD4", "aiReservedD4", "ReservedD4", "ExtraD4Values", "array_d4_values" });

	YAML::Node car = FieldNode(plain, "stCarToCarCollisionParam", "CarToCarCollisionParam");
	out.m_stCarToCarCollisionParam.m_iCollisionRadius = ReadInt(car, "iCollisionRadius", "CollisionRadius");
	out.m_stCarToCarCollisionParam.m_iCollisionLength = ReadInt(car, "iCollisionLength", "CollisionLength");
	out.m_stCarToCarCollisionParam.m_iCollisionWidth = ReadInt(car, "iCollisionWidth", "CollisionWidth");
	out.m_stCarToCarCollisionParam.m_iCollisionHeight = ReadInt(car, "iCollisionHeight", "CollisionHeight");
	out.m_stCarToCarCollisionParam.m_iCollisionLostCtrlTime = ReadInt(car, "iCollisionLostCtrlTime", "CollisionLostCtrlTime");
	out.m_stCarToCarCollisionParam.m_aiReserved38 = ReadIntListAny(car, { "m_aiReserved38", "aiReserved38", "Reserved38", "ExtraValues", "array_38_values" });

	out.m_iBaseTurnRate = ReadPlainOrGroupInt(plain, "Scalars", "iBaseTurnRate", "BaseTurnRate");
	out.m_iVolatileTurnRate = ReadPlainOrGroupInt(plain, "Scalars", "iVolatileTurnRate", "VolatileTurnRate");
	out.m_iMaxTurnSpeed = ReadPlainOrGroupInt(plain, "Scalars", "iMaxTurnSpeed", "MaxTurnSpeed");
	out.m_iMinTurnSpeed = ReadPlainOrGroupInt(plain, "Scalars", "iMinTurnSpeed", "MinTurnSpeed");
	out.m_iMaxAccuTime = ReadPlainOrGroupInt(plain, "Scalars", "iMaxAccuTime", "MaxAccuTime");
	out.m_iBaseAccuRate = ReadPlainOrGroupInt(plain, "Scalars", "iBaseAccuRate", "BaseAccuRate");
	out.m_iMaxAffectSpeed = ReadPlainOrGroupInt(plain, "Scalars", "iMaxAffectSpeed", "MaxAffectSpeed");
	out.m_iGravity = ReadPlainOrGroupInt(plain, "Scalars", "iGravity", "Gravity");
	out.m_iAdditionalLocalZSpeed = ReadPlainOrGroupInt(plain, "Scalars", "iAdditionalLocalZSpeed", "AdditionalLocalZSpeed");
	out.m_iStartVec = ReadPlainOrGroupInt(plain, "Scalars", "iStartVec", "StartVec");
	out.m_iEndVecFist = ReadPlainOrGroupInt(plain, "Scalars", "iEndVecFist", "EndVecFist");
	out.m_iEndVecSecon = ReadPlainOrGroupInt(plain, "Scalars", "iEndVecSecon", "EndVecSecon");
	out.m_iDirKeyForce = ReadPlainOrGroupInt(plain, "Scalars", "iDirKeyForce", "DirKeyForce");
	out.m_iDirKeyTwist = ReadPlainOrGroupInt(plain, "Scalars", "iDirKeyTwist", "DirKeyTwist");
	out.m_iBannerTwist = ReadPlainOrGroupInt(plain, "Scalars", "iBannerTwist", "BannerTwist");
	out.m_iBannerKeyTwist = ReadPlainOrGroupInt(plain, "Scalars", "iBannerKeyTwist", "BannerKeyTwist");
	out.m_iBannerVecForce = ReadPlainOrGroupInt(plain, "Scalars", "iBannerVecForce", "BannerVecForce");
	out.m_iBannerHeadForce = ReadPlainOrGroupInt(plain, "Scalars", "iBannerHeadForce", "BannerHeadForce");
	out.m_iSlidFricForce = ReadPlainOrGroupInt(plain, "Scalars", "iSlidFricForce", "SlidFricForce");
	out.m_iRollFricForce = ReadPlainOrGroupInt(plain, "Scalars", "iRollFricForce", "RollFricForce");
	out.m_iStartWec = ReadPlainOrGroupInt(plain, "Scalars", "iStartWec", "StartWec");
	out.m_iMaxWec = ReadPlainOrGroupInt(plain, "Scalars", "iMaxWec", "MaxWec");
	out.m_iSuaiJianTwist = ReadPlainOrGroupInt(plain, "Scalars", "iSuaiJianTwist", "SuaiJianTwist");
	out.m_iDirUpKeyForce = ReadPlainOrGroupInt(plain, "Scalars", "iDirUpKeyForce", "DirUpKeyForce");

	out.m_aiAccelStatus = ReadIntList(plain, "aiAccelStatus", "AccelStatus");
	out.m_astForwardAccel = ReadPairRows(FieldNode(plain, "astForwardAccel", "ForwardAccel"));
	out.m_astForwardDecel = ReadPairRows(FieldNode(plain, "astForwardDecel", "ForwardDecel"));
	out.m_astBackwardAccel = ReadPairRows(FieldNode(plain, "astBackwardAccel", "BackwardAccel"));
	out.m_astBackwardDecel = ReadPairRows(FieldNode(plain, "astBackwardDecel", "BackwardDecel"));

	out.m_iCrashCompensatePower = ReadPlainOrGroupInt(plain, "Tail", "iCrashCompensatePower", "CrashCompensatePower");
	out.m_iDefenseRate = ReadPlainOrGroupInt(plain, "Tail", "iDefenseRate", "DefenseRate");
	out.m_uiUin = ReadPlainOrGroupInt(plain, "Tail", "uiUin", "Uin", out.m_uiUin);
	out.m_iSpeedupCardGenRate = ReadPlainOrGroupInt(plain, "Tail", "iSpeedupCardGenRate", "SpeedupCardGenRate");
	out.m_u32ExtraFuncFlag = ReadPlainOrGroupInt(plain, "Tail", "u32ExtraFuncFlag", "ExtraFuncFlag");

	out.m_hasSkateParaCount = SequenceCount(FieldNode(plain, "hasSkatePara"));
	out.m_astBattleModeSkillInfoListCount = SequenceCount(FieldNode(plain, "astBattleModeSkillInfoList"));

	out.m_iEnergyConvert = ReadPlainOrGroupInt(plain, "Tail", "iEnergyConvert", "EnergyConvert");
	out.m_usSuperN2ORate = ReadPlainOrGroupInt(plain, "Tail", "usSuperN2ORate", "SuperN2ORate");
	out.m_ushCollisionLoseRate = ReadPlainOrGroupInt(plain, "Tail", "ushCollisionLoseRate", "CollisionLoseRate");
	out.m_uchNotInterruptDrift = ReadPlainOrGroupInt(plain, "Tail", "uchNotInterruptDrift", "NotInterruptDrift");

	out.m_auchAddWeight = ReadIntList(plain, "auchAddWeight", "AddWeight");
	out.m_auchWeight = ReadIntList(plain, "auchWeight", "Weight");
	out.m_astAntiCollistionCfg = ReadAntiCollisionRows(FieldNode(plain, "astAntiCollistionCfg", "AntiCollisionCfg"));
	out.m_astBoatParam = ReadBoatRows(FieldNode(plain, "astBoatParam", "BoatParam"));
	out.m_astBoatForwardAccel = ReadPairRows(FieldNode(plain, "astBoatForwardAccel", "BoatForwardAccel"));

	out.m_iDirUpKeyForceParamA = ReadPlainOrGroupInt(plain, "Tail", "iDirUpKeyForceParamA", "DirUpKeyForceParamA");
	out.m_iDirUpKeyForceParamB = ReadPlainOrGroupInt(plain, "Tail", "iDirUpKeyForceParamB", "DirUpKeyForceParamB");

	YAML::Node zero = NodeGetAny(plain, { "ZeroArrays", "zeroArrays", "zero_arrays" });
	if (!IsPresentNode(zero) || !zero.IsMap())
	{
		YAML::Node nested = NodeGetAny(wire, { "ZeroArrays", "zeroArrays", "zero_arrays" });
		zero = (IsPresentNode(nested) && nested.IsMap()) ? nested : wire;
	}
	out.m_topSmallList = ReadIntListAny(zero, { "TopSmallList", "topSmallList", "top_small_list" });
	out.m_list1398 = ReadIntListAny(zero, { "List1398", "list1398", "list_1398" });
	out.m_list2700 = ReadIntListAny(zero, { "List2700", "list2700", "list_2700" });
	out.m_list28C4 = ReadIntListAny(zero, { "List28C4", "list28C4", "list_28c4" });
	out.m_list35F0 = ReadIntListAny(zero, { "List35F0", "list35F0", "list_35f0" });
	out.m_list3CD0 = ReadIntListAny(zero, { "List3CD0", "list3CD0", "list_3cd0" });
	out.m_array4C34 = ReadIntListAny(zero, { "Array4C34", "array4C34", "array_4c34" });
	out.m_array4CE8 = ReadIntListAny(zero, { "Array4CE8", "array4CE8", "array_4ce8" });
	out.m_array7460 = ReadIntListAny(zero, { "Array7460", "array7460", "array_7460" });
	out.m_array750C = ReadIntListAny(zero, { "Array750C", "array750C", "array_750c" });
	out.m_array7700 = ReadIntListAny(zero, { "Array7700", "array7700", "array_7700" });
	out.m_array8A58 = ReadIntListAny(zero, { "Array8A58", "array8A58", "array_8a58" });
	out.m_topFinalScalar = AsInt(NodeGetAny(plain, { "TopFinalScalar", "topFinalScalar", "top_final_scalar" }), out.m_topFinalScalar);
	out.m_topFinalScalar = AsInt(NodeGetAny(zero, { "TopFinalScalar", "topFinalScalar", "top_final_scalar" }), out.m_topFinalScalar);
	out.m_topFinalList = ReadIntListAny(zero, { "TopFinalList", "topFinalList", "top_final_list" });

	out.m_group0Elements.clear();
	YAML::Node group0 = NodeGetAny(plain, { "Group0Elements", "group0Elements", "group0_elements", "TopGroup0Elements" });
	if (IsPresentNode(group0) && group0.IsSequence())
	{
		for (const auto& row : group0)
		{
			if (!IsPresentNode(row) || !row.IsMap())
				continue;
			KartPhys551Element elem;
			elem.m_field26F4 = out.m_field26F4;
			ReadElementFields(row, elem);
			out.m_group0Elements.push_back(elem);
		}
	}

	out.m_group1Elements.clear();
	YAML::Node group1 = NodeGetAny(plain, { "Group1Elements", "group1Elements", "group1_elements", "TopGroup1Elements" });
	if (IsPresentNode(group1) && group1.IsSequence())
	{
		for (const auto& row : group1)
		{
			if (!IsPresentNode(row) || !row.IsMap())
				continue;
			KartPhys551Element elem;
			elem.m_field26F4 = out.m_field26F4;
			ReadElementFields(row, elem);
			out.m_group1Elements.push_back(elem);
		}
	}

	out.m_group2Elements.clear();
	YAML::Node group2 = NodeGetAny(plain, { "Group2Elements", "group2Elements", "group2_elements", "TopGroup2Elements" });
	if (IsPresentNode(group2) && group2.IsSequence())
	{
		for (const auto& row : group2)
		{
			if (!IsPresentNode(row) || !row.IsMap())
				continue;
			KartPhys551Element elem;
			elem.m_field26F4 = out.m_field26F4;
			ReadElementFields(row, elem);
			out.m_group2Elements.push_back(elem);
		}
	}
	return true;
}

#include "KartPhys551InverseTables.inc"

static const unsigned short* FindTable(const QQS551InvTable* tables, size_t count, const char* name)
{
	for (size_t i = 0; i < count; ++i)
	{
		if (std::strcmp(tables[i].name, name) == 0)
			return tables[i].values;
	}
	return nullptr;
}

class Encoder
{
public:
	bool Encode(const KartPhys551Plain& p, std::vector<unsigned char>& payload, std::string& error)
	{
		out_.clear();
		error_.clear();
		if (p.m_astPhysInfo.size() > 10) return Fail("m_astPhysInfo count > 10");
		if (p.m_astCompensateParam.size() > 20) return Fail("m_astCompensateParam count > 20");
		if (p.m_aiAccelStatus.size() > 6) return Fail("m_aiAccelStatus count > 6");
		if (p.m_astForwardAccel.size() > 30) return Fail("m_astForwardAccel count > 30");
		if (p.m_astForwardDecel.size() > 30) return Fail("m_astForwardDecel count > 30");
		if (p.m_astBackwardAccel.size() > 30) return Fail("m_astBackwardAccel count > 30");
		if (p.m_astBackwardDecel.size() > 30) return Fail("m_astBackwardDecel count > 30");
		if (p.m_astBoatForwardAccel.size() > 30) return Fail("m_astBoatForwardAccel count > 30");
		if (p.m_auchAddWeight.size() > 9) return Fail("m_auchAddWeight count > 9");
		if (p.m_auchWeight.size() > 9) return Fail("m_auchWeight count > 9");
		if (p.m_astAntiCollistionCfg.size() > 1) return Fail("m_astAntiCollistionCfg count > 1");
		if (p.m_astBoatParam.size() > 1) return Fail("m_astBoatParam count > 1");
		if (p.m_hasSkateParaCount != 0) return Fail("m_hasSkatePara non-empty records are not encoded yet");
		if (p.m_astBattleModeSkillInfoListCount != 0) return Fail("m_astBattleModeSkillInfoList non-empty records are not encoded yet");
		for (const auto& elem : p.m_group0Elements)
		{
			if (elem.m_astPhysInfo.size() > 10) return Fail("group0 m_astPhysInfo count > 10");
			if (elem.m_astCompensateParam.size() > 20) return Fail("group0 m_astCompensateParam count > 20");
			if (elem.m_aiAccelStatus.size() > 6) return Fail("group0 m_aiAccelStatus count > 6");
			if (elem.m_astForwardAccel.size() > 30) return Fail("group0 m_astForwardAccel count > 30");
			if (elem.m_astForwardDecel.size() > 30) return Fail("group0 m_astForwardDecel count > 30");
			if (elem.m_astBackwardAccel.size() > 30) return Fail("group0 m_astBackwardAccel count > 30");
			if (elem.m_astBackwardDecel.size() > 30) return Fail("group0 m_astBackwardDecel count > 30");
			if (elem.m_astBoatForwardAccel.size() > 30) return Fail("group0 m_astBoatForwardAccel count > 30");
			if (elem.m_auchAddWeight.size() > 9) return Fail("group0 m_auchAddWeight count > 9");
			if (elem.m_auchWeight.size() > 9) return Fail("group0 m_auchWeight count > 9");
			if (elem.m_astAntiCollistionCfg.size() > 1) return Fail("group0 m_astAntiCollistionCfg count > 1");
			if (elem.m_astBoatParam.size() > 1) return Fail("group0 m_astBoatParam count > 1");
			if (elem.m_hasSkateParaCount != 0) return Fail("group0 m_hasSkatePara non-empty records are not encoded yet");
			if (elem.m_astBattleModeSkillInfoListCount != 0) return Fail("group0 m_astBattleModeSkillInfoList non-empty records are not encoded yet");
		}
		for (const auto& elem : p.m_group1Elements)
		{
			if (elem.m_astPhysInfo.size() > 10) return Fail("group1 m_astPhysInfo count > 10");
			if (elem.m_astCompensateParam.size() > 20) return Fail("group1 m_astCompensateParam count > 20");
			if (elem.m_aiAccelStatus.size() > 6) return Fail("group1 m_aiAccelStatus count > 6");
			if (elem.m_astForwardAccel.size() > 30) return Fail("group1 m_astForwardAccel count > 30");
			if (elem.m_astForwardDecel.size() > 30) return Fail("group1 m_astForwardDecel count > 30");
			if (elem.m_astBackwardAccel.size() > 30) return Fail("group1 m_astBackwardAccel count > 30");
			if (elem.m_astBackwardDecel.size() > 30) return Fail("group1 m_astBackwardDecel count > 30");
			if (elem.m_astBoatForwardAccel.size() > 30) return Fail("group1 m_astBoatForwardAccel count > 30");
			if (elem.m_auchAddWeight.size() > 9) return Fail("group1 m_auchAddWeight count > 9");
			if (elem.m_auchWeight.size() > 9) return Fail("group1 m_auchWeight count > 9");
			if (elem.m_astAntiCollistionCfg.size() > 1) return Fail("group1 m_astAntiCollistionCfg count > 1");
			if (elem.m_astBoatParam.size() > 1) return Fail("group1 m_astBoatParam count > 1");
			if (elem.m_hasSkateParaCount != 0) return Fail("group1 m_hasSkatePara non-empty records are not encoded yet");
			if (elem.m_astBattleModeSkillInfoListCount != 0) return Fail("group1 m_astBattleModeSkillInfoList non-empty records are not encoded yet");
		}
		for (const auto& elem : p.m_group2Elements)
		{
			if (elem.m_astPhysInfo.size() > 10) return Fail("group2 m_astPhysInfo count > 10");
			if (elem.m_astCompensateParam.size() > 20) return Fail("group2 m_astCompensateParam count > 20");
			if (elem.m_aiAccelStatus.size() > 6) return Fail("group2 m_aiAccelStatus count > 6");
			if (elem.m_astForwardAccel.size() > 30) return Fail("group2 m_astForwardAccel count > 30");
			if (elem.m_astForwardDecel.size() > 30) return Fail("group2 m_astForwardDecel count > 30");
			if (elem.m_astBackwardAccel.size() > 30) return Fail("group2 m_astBackwardAccel count > 30");
			if (elem.m_astBackwardDecel.size() > 30) return Fail("group2 m_astBackwardDecel count > 30");
			if (elem.m_astBoatForwardAccel.size() > 30) return Fail("group2 m_astBoatForwardAccel count > 30");
			if (elem.m_auchAddWeight.size() > 9) return Fail("group2 m_auchAddWeight count > 9");
			if (elem.m_auchWeight.size() > 9) return Fail("group2 m_auchWeight count > 9");
			if (elem.m_astAntiCollistionCfg.size() > 1) return Fail("group2 m_astAntiCollistionCfg count > 1");
			if (elem.m_astBoatParam.size() > 1) return Fail("group2 m_astBoatParam count > 1");
			if (elem.m_hasSkateParaCount != 0) return Fail("group2 m_hasSkatePara non-empty records are not encoded yet");
			if (elem.m_astBattleModeSkillInfoListCount != 0) return Fail("group2 m_astBattleModeSkillInfoList non-empty records are not encoded yet");
		}
		if (!EncodeOfficialCompact551(p)) { error = error_; return false; }
		payload = out_;
		return true;
	}

private:
	std::vector<unsigned char> out_;
	std::string error_;

	bool Fail(const std::string& msg)
	{
		error_ = msg;
		return false;
	}

	bool EmitByte(const char* transform, i64 value)
	{
		const unsigned short* table = FindTable(kQQS551InvByteTables, sizeof(kQQS551InvByteTables) / sizeof(kQQS551InvByteTables[0]), transform);
		if (!table)
			return Fail(std::string("missing byte transform ") + transform);
		const unsigned short raw = table[static_cast<unsigned char>(value & 0xFF)];
		if (raw == 0xFFFF)
			return Fail(std::string("byte transform has no raw for ") + transform);
		out_.push_back(static_cast<unsigned char>(raw));
		return true;
	}

	bool EmitVar(const char* transform, i64 value)
	{
		const unsigned short* table = FindTable(kQQS551InvTupleTables, sizeof(kQQS551InvTupleTables) / sizeof(kQQS551InvTupleTables[0]), transform);
		if (!table)
			return Fail(std::string("missing tuple transform ") + transform);
		std::uint64_t n = value < 0 ? static_cast<std::uint32_t>(value) : static_cast<std::uint64_t>(value);
		for (;;)
		{
			const unsigned int chunk = static_cast<unsigned int>(n & 0x7F);
			n >>= 7;
			const bool stop = (n == 0);
			const unsigned int index = chunk | (stop ? 0x80u : 0u);
			const unsigned short raw = table[index];
			if (raw == 0xFFFF)
				return Fail(std::string("tuple transform has no raw for ") + transform);
			out_.push_back(static_cast<unsigned char>(raw));
			if (stop)
				return true;
		}
	}

	bool EmitVarMinChunks(const char* transform, i64 value, size_t minChunks)
	{
		const unsigned short* table = FindTable(kQQS551InvTupleTables, sizeof(kQQS551InvTupleTables) / sizeof(kQQS551InvTupleTables[0]), transform);
		if (!table)
			return Fail(std::string("missing tuple transform ") + transform);
		std::uint64_t n = value < 0 ? static_cast<std::uint32_t>(value) : static_cast<std::uint64_t>(value);
		std::vector<unsigned int> chunks;
		for (;;)
		{
			chunks.push_back(static_cast<unsigned int>(n & 0x7F));
			n >>= 7;
			if (n == 0)
				break;
		}
		while (chunks.size() < minChunks)
			chunks.push_back(0);
		for (size_t i = 0; i < chunks.size(); ++i)
		{
			const unsigned int index = chunks[i] | ((i + 1 == chunks.size()) ? 0x80u : 0u);
			const unsigned short raw = table[index];
			if (raw == 0xFFFF)
				return Fail(std::string("tuple transform has no raw for ") + transform);
			out_.push_back(static_cast<unsigned char>(raw));
		}
		return true;
	}

	bool EmitDirKeyTwist(i64 value)
	{
		const size_t start = out_.size();
		if (!EmitVar("t_18078c260", value)) return false;
		// Official 21043 packets use the 0x7C raw variant for the first
		// non-final chunk when the low 7-bit chunk is 0x4D. The transform
		// aliases this with the canonical inverse table entry, so preserve
		// the observed wire form for skin packets such as 130777/135168.
		if (out_.size() > start + 1 && (static_cast<std::uint64_t>(value < 0 ? static_cast<std::uint32_t>(value) : value) & 0x7Fu) == 0x4Du && out_[start] == 0x7E)
			out_[start] = 0x7C;
		return true;
	}

	bool EmitVarList(const char* countTf, const char* valueTf, const std::vector<i64>& values, size_t limit = 16)
	{
		if (values.size() > limit) return Fail(std::string("too many values for ") + countTf);
		if (!EmitByte(countTf, static_cast<i64>(values.size()))) return false;
		for (i64 v : values)
			if (!EmitVar(valueTf, v)) return false;
		return true;
	}

	bool EncodePairArray(const char* countTf, const std::vector<PairParam>& rows, size_t limit = 30)
	{
		if (rows.size() > limit) return Fail(std::string("too many pair rows for ") + countTf);
		if (!EmitByte(countTf, static_cast<i64>(rows.size()))) return false;
		for (const auto& row : rows)
		{
			if (!EmitVar("t_c200_0c", row.m_iKey)) return false;
			if (!EmitVar("t_c200_14", row.m_iValue)) return false;
			if (!EmitVarList("f_c200_count", "t_c200_array", row.m_aiReserved20)) return false;
		}
		return true;
	}

	bool EncodeWeight(const char* countTf, const char* valueTf, const std::vector<i64>& values, size_t limit)
	{
		if (values.size() > limit) return Fail(std::string(valueTf) + " count over limit");
		if (!EmitByte(countTf, static_cast<i64>(values.size()))) return false;
		for (i64 v : values)
			if (!EmitByte(valueTf, v)) return false;
		return true;
	}

	bool EncodeElement(const KartPhys551Element& p)
	{
		if (!EmitVar("t_elem_0c", p.m_iDirUpKeyForceParamB)) return false;
		if (!EncodeCarToCarCollisionParam(p)) return false;
		if (!EmitByte("f_18078bfb0", p.m_uchNotInterruptDrift)) return false;
		if (!EncodePairArray("f_18078bf50", p.m_astForwardAccel)) return false;
		if (!EmitVarList("f_18078cbc0", "t_1807856b0", p.m_list1398)) return false;
		if (!EmitVar("t_1807848f0", p.m_iRollFricForce)) return false;
		if (!EncodePairArray("f_18078c750", p.m_astForwardDecel)) return false;

		if (!EmitVar("t_18078ccd0", p.m_iMinTurnSpeed)) return false;
		if (!EmitVar("t_180784c30", p.m_iDefenseRate)) return false;
		if (!EmitVar("t_1807847a0", p.m_field26F4)) return false;
		if (!EmitVarList("f_18078c710", "t_2700_inline", p.m_list2700)) return false;
		if (!EmitVar("t_180784600", p.m_u32ExtraFuncFlag)) return false;
		if (!EmitVar("t_18078c6a0", p.m_iMaxWec)) return false;
		if (!EmitVar("t_18078c650", p.m_iGravity)) return false;
		if (!EncodeAntiCollistionCfg(p)) return false;
		if (!EmitByte("f_18078c5e0", static_cast<i64>(p.m_aiAccelStatus.size()))) return false;
		for (i64 v : p.m_aiAccelStatus) if (!EmitVar("t_2890_inline", v)) return false;
		if (!EmitVarList("f_18078c080", "t_18078c0c0", p.m_list28C4)) return false;
		if (!EmitVar("t_180784b10", p.m_iEndVecSecon)) return false;
		if (!EncodeCompensateParam(p)) return false;

		if (!EmitVar("t_18078c560", p.m_uiUin)) return false;
		if (!EmitVar("t_35dc_inline", p.m_iSpeedupCardGenRate)) return false;
		if (!EmitVar("t_18078c510", p.m_iSuaiJianTwist)) return false;
		if (!EmitVarList("f_18078c4c0", "t_180784ff0", p.m_list35F0)) return false;
		if (!EmitVar("t_1807843d0", p.m_iStartWec)) return false;
		if (!EmitVar("t_18078c480", p.m_iBaseAccuRate)) return false;
		if (!EmitByte("f_18078c040", static_cast<i64>(p.m_astBattleModeSkillInfoListCount))) return false;

		if (!EmitVar("t_18078cec0", p.m_iDirKeyForce)) return false;
		if (!EmitVar("t_18078bea0", p.m_iBannerHeadForce)) return false;
		if (!EmitVarList("f_18078be40", "t_18078bc30", p.m_list3CD0)) return false;
		if (!EncodePhysInfo(p)) return false;

		if (!EmitVar("t_4754_inline", p.m_iBannerTwist)) return false;
		if (!EncodeBoatParam(p)) return false;

		if (!EmitVar("t_18078c3d0", p.m_iMaxAccuTime)) return false;
		if (!EncodeWeight("t_18078c380", "t_outer_4c1b_byte", p.m_auchWeight, 9)) return false;
		if (!EmitVarList("t_18078cd70", "t_18078c340", p.m_array4C34)) return false;
		if (!EmitVar("t_1807851c0", p.m_iSlidFricForce)) return false;
		if (!EmitVar("t_18078c110", p.m_usSuperN2ORate)) return false;
		if (!EncodeWeight("t_18078c2e0", "t_18078cc90", p.m_auchAddWeight, 9)) return false;
		if (!EmitDirKeyTwist(p.m_iDirKeyTwist)) return false;
		if (!EmitVar("t_outer_4cdc", p.m_iAdditionalLocalZSpeed)) return false;
		if (!EmitVarList("t_18078c200", "t_outer_4ce8", p.m_array4CE8, 255)) return false;
		if (!EmitVar("t_18078bff0", p.m_iDirUpKeyForce)) return false;
		if (!EncodeCollisionBoxParam(p)) return false;
		if (!EncodePairArray("t_outer_count_4ec1", p.m_astBackwardAccel)) return false;
		if (!EncodePairArray("t_18078c1b0", p.m_astBoatForwardAccel)) return false;

		if (!EmitVar("t_18078c170", p.m_iStartVec)) return false;
		if (!EmitVarList("t_18078cde0", "t_outer_7460", p.m_array7460)) return false;
		if (!EmitVar("t_outer_74e0", p.m_iBaseTurnRate)) return false;
		if (!EmitVar("t_outer_74e8", p.m_iBannerVecForce)) return false;
		if (!EmitVar("t_outer_74f0", p.m_iMaxAffectSpeed)) return false;
		if (!EmitVar("t_180783fc0", p.m_iEnergyConvert)) return false;
		if (!EmitVar("t_outer_7500", p.m_iEndVecFist)) return false;
		if (!EmitVarList("t_outer_count_7505", "t_18078bef0", p.m_array750C)) return false;
		if (!EmitVar("t_outer_758a", p.m_ushCollisionLoseRate)) return false;
		if (!EmitVar("t_outer_7590", p.m_iVolatileTurnRate)) return false;
		if (!EmitByte("t_outer_count_7595", static_cast<i64>(p.m_hasSkateParaCount))) return false;
		if (!EmitVar("t_outer_76ec", p.m_iMaxTurnSpeed)) return false;
		if (!EmitVar("t_outer_76f4", p.m_iBannerKeyTwist)) return false;
		if (!EmitVarList("t_outer_count_76f9", "t_outer_7700", p.m_array7700)) return false;
		if (!EmitVar("t_18078bb80", p.m_iCrashCompensatePower)) return false;
		if (!EmitVar("t_outer_7788", p.m_iDirUpKeyForceParamA)) return false;
		if (!EncodePairArray("t_outer_count_778d", p.m_astBackwardDecel)) return false;
		return EmitVarList("t_outer_count_8a51", "t_outer_8a58", p.m_array8A58);
	}

	bool EncodeOfficialCompact551(const KartPhys551Plain& p)
	{
		if (p.m_group0Elements.size() >= 21) return Fail("group0_count >= 21");
		if (!EmitVar("t_top_group0_count", static_cast<i64>(p.m_group0Elements.size()))) return false;
		for (const auto& elem : p.m_group0Elements)
			if (!EncodeElement(elem)) return false;
		if (!EmitVar("t_top_scalar", p.m_topScalar)) return false;
		if (!EmitVarList("decode_top_small_count", "t_top_small_list", p.m_topSmallList)) return false;
		const size_t group1Count = 1 + p.m_group1Elements.size();
		if (group1Count >= 21) return Fail("group1_count >= 21");
		if (!EmitVar("t_top_group1_count", static_cast<i64>(group1Count))) return false;
		if (!EncodeElement(p)) return false;
		for (const auto& elem : p.m_group1Elements)
			if (!EncodeElement(elem)) return false;
		if (p.m_group2Elements.size() >= 21) return Fail("group2_count >= 21");
		if (!EmitVar("t_top_group2_count", static_cast<i64>(p.m_group2Elements.size()))) return false;
		for (const auto& elem : p.m_group2Elements)
			if (!EncodeElement(elem)) return false;
		if (!EmitVar("t_top_final_scalar", p.m_topFinalScalar)) return false;
		return EmitVarList("decode_top_final_count", "t_top_final_list", p.m_topFinalList);
	}

	bool EncodeCarToCarCollisionParam(const KartPhys551Element& p)
	{
		const CarToCarCollisionParam& c = p.m_stCarToCarCollisionParam;
		if (!EmitVar("t_1807853b0", c.m_iCollisionHeight)) return false;
		if (!EmitVar("t_1807852c0", c.m_iCollisionLength)) return false;
		if (!EmitVar("t_be90_1c", c.m_iCollisionWidth)) return false;
		if (!EmitVar("t_be90_24", c.m_iCollisionRadius)) return false;
		if (!EmitVar("t_1807851c0", c.m_iCollisionLostCtrlTime)) return false;
		return EmitVarList("f_18078cf60", "t_be90_array", c.m_aiReserved38);
	}

	bool EncodeAntiCollistionCfg(const KartPhys551Element& p)
	{
		if (!EmitByte("f_180787460", static_cast<i64>(p.m_astAntiCollistionCfg.size()))) return false;
		for (const auto& row : p.m_astAntiCollistionCfg)
		{
			if (row.m_aiPosition.size() > 12)
				return Fail("m_astAntiCollistionCfg.m_aiPosition count > 12");
			if (!EmitByte("f_d2d0_count1", static_cast<i64>(row.m_aiPosition.size()))) return false;
			for (i64 v : row.m_aiPosition) if (!EmitVar("t_d2d0_list10", v)) return false;
			if (!EmitVarList("f_d2d0_count2", "t_d2d0_list74", row.m_aiReserved20)) return false;
		}
		return true;
	}

	bool EncodeCompensateParam(const KartPhys551Element& p)
	{
		if (!EmitByte("f_18078c590", static_cast<i64>(p.m_astCompensateParam.size()))) return false;
		for (const auto& row : p.m_astCompensateParam)
		{
			if (!EmitVar("t_18077b280_0c", row.m_iPercent)) return false;
			if (!EmitVar("t_18077b280_14", row.m_iValue)) return false;
			if (!EmitVarList("f_18077b280_count", "t_18077b280_array", row.m_aiReserved20)) return false;
		}
		return true;
	}

	bool EncodePhysInfo(const KartPhys551Element& p)
	{
		if (!EmitByte("f_18078c420", static_cast<i64>(p.m_astPhysInfo.size()))) return false;
		for (const auto& row : p.m_astPhysInfo)
		{
			if (row.m_iCommonPara.size() >= 11) return Fail("m_astPhysInfo.m_iCommonPara count must be < 11");
			if (!EmitVar("t_18077ac50_0c", row.m_iParam)) return false;
			if (!EmitVar("t_18077ac50_14", row.m_iLifeTime)) return false;
			if (!EmitVar("t_18077ac50_1c", row.m_uiAffectID)) return false;
			if (!EmitByte("f_18077ac50_count28", static_cast<i64>(row.m_iCommonPara.size()))) return false;
			for (i64 v : row.m_iCommonPara) if (!EmitVar("t_18077ac50_array28", v)) return false;
			if (!EmitVar("t_18077ac50_78", row.m_uiPID)) return false;
			if (!EmitVarList("f_18077ac50_count84", "t_18078cb20", row.m_aiReserved84)) return false;
		}
		return true;
	}

	bool EncodeBoatParam(const KartPhys551Element& p)
	{
		if (!EmitByte("f_4759_count", static_cast<i64>(p.m_astBoatParam.size()))) return false;
		if (p.m_astBoatParam.empty()) return true;
		const BoatParam& r = p.m_astBoatParam[0];
		if (!EmitVarList("f_18078cae0", "t_d640_list10", r.m_aiReserved10)) return false;
		if (!EmitVar("t_d640_90", r.m_ifSlidFricForce)) return false;
		if (!EmitVar("t_18078cd10", r.m_iBoatAirValue)) return false;
		if (!EmitVar("t_18078bd20", r.m_iLRKeyVecForce)) return false;
		if (!EmitVar("t_18078cab0", r.m_iBannerKeyTwist)) return false;
		if (!EmitVar("t_18078ca50", r.m_iUpKeyVecForce)) return false;
		if (!EmitVarList("f_18078b400", "t_180784e30", r.m_aiReservedBC)) return false;
		if (!EmitVar("t_1807852c0", r.m_iVecForceParamA)) return false;
		if (!EmitVar("t_d640_144", r.m_iVecForceParamB)) return false;
		if (!EmitVar("t_18078bd80", r.m_iDirUpKeyForce)) return false;
		if (!EmitVar("t_18078c9f0", r.m_iLRKeyVecForceParamB)) return false;
		if (!EmitVar("t_18078c9a0", r.m_iBoatHeighRatio)) return false;
		if (!EmitVarList("t_18078cf10", "t_18078c960", r.m_aiReserved168)) return false;
		if (!EmitVar("t_18078bde0", r.m_iDirKeyForceParamB)) return false;
		if (!EmitVar("t_180785500", r.m_iDTFRatio)) return false;
		if (!EmitVar("t_d640_1f8", r.m_iVecForce)) return false;
		if (!EmitVar("t_18078c900", r.m_iWecEffect)) return false;
		if (!EmitVarMinChunks("t_d640_208", r.m_iDirKeyForceParamC, 2)) return false;
		if (!EmitVarList("t_18078c8a0", "t_d640_214", r.m_aiReserved214)) return false;
		if (!EmitVar("t_d640_294", r.m_iDirKeyForce)) return false;
		if (!EmitVar("t_d640_29c", r.m_iBannerTwist)) return false;
		if (!EmitVar("t_d640_2a4", r.m_iUpKeyVecForceParamB)) return false;
		if (!EmitVar("t_18078c850", r.m_iDirKeyForceParamD)) return false;
		if (!EmitVar("t_d640_2b4", r.m_iBannerKeyTwistParamA)) return false;
		if (!EmitVarList("t_d640_count_2b9", "t_18078c7f0", r.m_aiReserved2C0)) return false;
		if (!EmitVar("t_180784c30", r.m_iAddNO2PerSec)) return false;
		if (!EmitVar("t_d640_348", r.m_iBannerTwistParamA)) return false;
		if (!EmitVar("t_180784b10", r.m_iBannerKeyTwistParamB)) return false;
		if (!EmitVar("t_d640_358", r.m_iUpKeyVecForceParamA)) return false;
		if (!EmitVarList("t_d640_count_365", "t_d640_36c", r.m_aiReserved36C)) return false;
		for (size_t i = 0; i < 3; ++i)
		{
			const i64 v = (i < r.m_iBoatShape.size()) ? r.m_iBoatShape[i] : 0;
			if (!EmitVar("t_18078bc80", v)) return false;
		}
		if (!EmitVar("t_18078c780", r.m_iDTFLimitAngle)) return false;
		if (!EmitVar("t_d640_40c", r.m_iLRKeyVecForceParamA)) return false;
		if (!EmitVar("t_d640_414", r.m_iDirUpKeyForceParamB)) return false;
		if (!EmitVar("t_d640_41c", r.m_iDirUpKeyForceParamA)) return false;
		if (!EmitVar("t_d640_424", r.m_iDirKeyForceParamA)) return false;
		return EmitVarList("t_d640_count_429", "t_d640_430", r.m_aiReserved430);
	}

	bool EncodeCollisionBoxParam(const KartPhys551Element& p)
	{
		const CollisionBoxParam& b = p.m_stCollisionBoxParam;
		if (!EmitVar("t_b570_0c", b.m_iAdjustRemoteOffset)) return false;
		if (!EmitVar("t_b570_14", b.m_iBoxLength)) return false;
		if (!EmitVarList("t_b570_count_19", "t_b570_array_20", b.m_aiReserved20)) return false;
		if (!EmitVar("t_b570_a0", b.m_iBoxHeight)) return false;
		if (!EmitVar("t_b570_a8", b.m_iBoxWidth)) return false;
		if (!EmitVar("t_b570_b0", b.m_iAdjustPickGap)) return false;
		if (!EmitVar("t_b570_b8", b.m_iAdjustOffset)) return false;
		if (!EmitVar("t_b570_c0", b.m_iControllerOffset)) return false;
		if (!EmitVar("t_b570_c8", b.m_iAdjustBevelOffset)) return false;
		return EmitVarList("t_b570_count_cd", "t_b570_array_d4", b.m_aiReservedD4);
	}
};

} // namespace

bool HasPlain551(const YAML::Node& config)
{
	YAML::Node plain = PlainRoot(config);
	return plain && plain.IsMap() && (
		FieldNode(plain, "uiKartID", "KartID") ||
		FieldNode(plain, "astPhysInfo", "PhysInfo") ||
		FieldNode(plain, "astForwardAccel", "ForwardAccel") ||
		FieldNode(plain, "stCollisionBoxParam", "CollisionBoxParam") ||
		FieldNode(plain, "stCarToCarCollisionParam", "CarToCarCollisionParam"));
}

bool BuildCompactDataFromPlainYaml(const YAML::Node& config, std::vector<unsigned char>& outData, std::string* error)
{
	outData.clear();
	KartPhys551Plain plain;
	if (!Normalize(config, plain))
	{
		if (error) *error = "normalize failed";
		return false;
	}
	if (plain.srcType < 0 || plain.srcType > 2)
	{
		if (error) *error = "invalid SrcType";
		return false;
	}
	std::vector<unsigned char> payload;
	std::string encodeError;
	Encoder encoder;
	if (!encoder.Encode(plain, payload, encodeError))
	{
		if (error) *error = encodeError;
		return false;
	}
	if (payload.empty() || payload.size() > 0xFFFF)
	{
		if (error) *error = "invalid payload length";
		return false;
	}
	outData.reserve(payload.size() + 3);
	outData.push_back(static_cast<unsigned char>((payload.size() >> 8) & 0xFF));
	outData.push_back(static_cast<unsigned char>(payload.size() & 0xFF));
	outData.insert(outData.end(), payload.begin(), payload.end());
	outData.push_back(static_cast<unsigned char>(plain.srcType));
	return true;
}

} // namespace QQSpeedKartPhys551
