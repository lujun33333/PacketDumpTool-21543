#pragma once
// MaxConsumCfg
typedef struct KartShapeRefitLegendMaxConsumCfg
{
	DWORD ItemID;
	DWORD ItemNum;
	INT ItemPeriod;

} *LPKartShapeRefitLegendMaxConsumCfg;
// RefitEffectsCfg
typedef struct KartShapeRefitLegendRefitEffectsCfg
{
	WORD EffectID;
	BYTE Level;
	DWORD Value1;
	DWORD Value2;
	std::string Resource;
	std::string EffectRes;
	BYTE Grade;

} *LPKartShapeRefitLegendRefitEffectsCfg;
// LevelCfg
typedef struct KartShapeRefitLegendLevelCfg
{
	BYTE Level;
	// DWORD RefitItem[];
	std::vector<KartShapeRefitLegendRefitEffectsCfg> RefitEffectsInfo;
	WORD DressPoint;
	WORD CarLibraryCollectValue;
	// RefitModify
	// HasTurnModify
	DWORD ShapeSuitID;
	std::vector<KartShapeRefitLegendMaxConsumCfg> ConsumInfo;
	BYTE cTmp1 = 0;
	BYTE cTmp2 = 0;
} *LPKartShapeRefitLegendLevelCfg;
// KartShapeRefitLegendInfo
typedef struct KartShapeRefitLegendInfo
{
	DWORD KartID;
	std::string KarName;
	WORD Grade;
	std::string GradeResource;
	std::vector<KartShapeRefitLegendLevelCfg> LevelInfo;
	KartShapeRefitLegendMaxConsumCfg MaxConsumInfo;
	DWORD ShapeSuitID;
	BYTE LegendSkinType;
} *LPKartShapeRefitLegendInfo;


typedef struct ColorShapeListCfg
{
	DWORD ShapeSuitID;
	std::string KarName;
	WORD Grade;
	std::string GradeResource;
	WORD CarLibraryCollectValue;

} *LPColorShapeListCfg;

typedef struct KartShapeColorInfo
{
	DWORD MainShapeSuitID;
	WORD ColorShapeNum;
	std::vector<ColorShapeListCfg> ShapeList;
	
} *LPKartShapeColorInfo;

static std::vector<KartShapeColorInfo> getShapeColorCfg() {
	std::vector<KartShapeColorInfo> KartShapeColorCfg;
	YAML::Node config = YAML::LoadFile(".\\Config\\ShapeColorCfg.yml");
	for (const auto& colorCfg : config) {
		KartShapeColorInfo KartShapeColor;
		KartShapeColor.MainShapeSuitID = colorCfg["MainShapeSuitID"].as<DWORD>();
		KartShapeColor.ColorShapeNum = colorCfg["ColorShapeList"].size();

		for (const auto& shapeList : colorCfg["ColorShapeList"]) {
			ColorShapeListCfg shapeCfg;
			shapeCfg.ShapeSuitID = shapeList["ShapeSuitID"].as<DWORD>();
			shapeCfg.KarName = shapeList["KarName"].as<std::string>();
			shapeCfg.Grade = shapeList["Grade"].as<WORD>();
			shapeCfg.GradeResource = shapeList["GradeResource"].as<std::string>();
			shapeCfg.CarLibraryCollectValue = shapeList["CarLibraryCollectValue"].as<WORD>();
			
			KartShapeColor.ShapeList.push_back(shapeCfg);
		}
		KartShapeColorCfg.push_back(KartShapeColor);
	}
	return KartShapeColorCfg;
}

static std::vector<KartShapeRefitLegendInfo> getLegendarySkin() {
	std::vector<KartShapeRefitLegendInfo> KartShapeRefitLegendInfoCfg;
	YAML::Node config = YAML::LoadFile(".\\Config\\LegendarySkin.yml");
	for (const auto& legend : config) {
		KartShapeRefitLegendInfo KartShapeRefitLegend;
		KartShapeRefitLegend.KartID = legend["KartID"].as<DWORD>();
		KartShapeRefitLegend.KarName = legend["KarName"].as<std::string>();
		KartShapeRefitLegend.Grade = legend["Grade"].as<WORD>();
		KartShapeRefitLegend.GradeResource = legend["GradeResource"].as<std::string>();
		KartShapeRefitLegend.ShapeSuitID = legend["ShapeSuitID"].as<DWORD>();
		KartShapeRefitLegend.LegendSkinType = legend["LegendSkinType"].as<BYTE>();
		for (const auto& level : legend["LevelInfo"]) {
			KartShapeRefitLegendLevelCfg levelCfg;
			levelCfg.Level = level["Level"].as<BYTE>();
			levelCfg.DressPoint = level["DressPoint"].as<WORD>();
			levelCfg.CarLibraryCollectValue = level["CarLibraryCollectValue"].as<WORD>();
			levelCfg.ShapeSuitID = level["ShapeSuitID"].as<DWORD>();
			for (const auto& effect : level["RefitEffectsInfo"]) {
				KartShapeRefitLegendRefitEffectsCfg effectCfg;
				effectCfg.EffectID = effect["EffectID"].as<WORD>();
				effectCfg.Level = effect["Level"].as<BYTE>();
				effectCfg.Value1 = effect["Value1"].as<DWORD>();
				effectCfg.Value2 = effect["Value2"].as<DWORD>();
				effectCfg.Resource = effect["Resource"].as<std::string>();
				effectCfg.EffectRes = effect["EffectRes"].as<std::string>();
				effectCfg.Grade = effect["Grade"].as<BYTE>();
				levelCfg.RefitEffectsInfo.push_back(effectCfg);
			}
			for (const auto& consum : level["ConsumInfo"]) {
				KartShapeRefitLegendMaxConsumCfg consumCfg;
				consumCfg.ItemID = consum["ItemID"].as<DWORD>();
				consumCfg.ItemNum = consum["ItemNum"].as<DWORD>();
				consumCfg.ItemPeriod = consum["ItemPeriod"].as<INT>();
				levelCfg.ConsumInfo.push_back(consumCfg);
			}
			KartShapeRefitLegend.LevelInfo.push_back(levelCfg);
		}
		KartShapeRefitLegendMaxConsumCfg maxConsumCfg;
		maxConsumCfg.ItemID = legend["MaxConsumInfo"]["ItemID"].as<DWORD>();
		maxConsumCfg.ItemNum = legend["MaxConsumInfo"]["ItemNum"].as<DWORD>();
		maxConsumCfg.ItemPeriod = legend["MaxConsumInfo"]["ItemPeriod"].as<INT>();
		KartShapeRefitLegend.MaxConsumInfo = maxConsumCfg;
		KartShapeRefitLegendInfoCfg.push_back(KartShapeRefitLegend);
	}
	return KartShapeRefitLegendInfoCfg;
}

static KartShapeRefitLegendInfo getLegendaryShapeSuitID(int ShapeSuitID) {
	KartShapeRefitLegendInfo info;
	info.KartID = 0;
	std::vector<KartShapeRefitLegendInfo> KartShapeRefitLegendInfoCfg = getLegendarySkin();
	UCHAR KartShapeRefitLegendNum = KartShapeRefitLegendInfoCfg.size();
	for (size_t Ki = 0; Ki < KartShapeRefitLegendNum; Ki++) {
		if (KartShapeRefitLegendInfoCfg[Ki].ShapeSuitID == ShapeSuitID)
		{
			info = KartShapeRefitLegendInfoCfg[Ki];
		}
	}
	return info;
}
