#pragma once

#include <map>
#include <set>
#include <string>

// 分类 GM 按「这个号买的是哪一版」套限制；版本号来自 yml，不必改代码。
struct GmUinLimit
{
	unsigned int MaxItemID = 0; // 0=不限制物品ID；itemid >= MaxItemID 则拒绝
	std::set<unsigned int> DenyItems;
	std::string VersionName;
};

inline const GmUinLimit* FindGmUinLimit(const std::map<unsigned int, GmUinLimit>& limits, unsigned int uin)
{
	const auto it = limits.find(uin);
	if (it == limits.end())
		return nullptr;
	return &it->second;
}
