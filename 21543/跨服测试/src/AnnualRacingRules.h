#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace AnnualRacingRules
{
inline bool ContainsPool(const std::vector<int>& poolIndexes, int currentLevel)
{
	return std::find(
		poolIndexes.begin(),
		poolIndexes.end(),
		currentLevel) != poolIndexes.end();
}

inline bool ShouldGrantFirstLight(
	int oldLevel,
	int newLevel,
	std::uint32_t rewardMask,
	int rewardLevel)
{
	if (rewardLevel < 1 || rewardLevel > 31 ||
		newLevel <= oldLevel || newLevel != rewardLevel)
	{
		return false;
	}

	const std::uint32_t bit =
		std::uint32_t{ 1 } << (rewardLevel - 1);
	return (rewardMask & bit) == 0u;
}

inline bool CanDrawBlessingStone(int currentCount, int maxCount)
{
	return maxCount > 0 && currentCount < maxCount;
}

inline bool ShouldCountBlessingStone(
	std::uint32_t awardItemID,
	std::uint32_t blessingStoneItemID,
	int currentCount,
	int maxCount)
{
	return blessingStoneItemID != 0u &&
		awardItemID == blessingStoneItemID &&
		CanDrawBlessingStone(currentCount, maxCount);
}

inline bool IsMaxStarAdvancedBlessingStoneEligible(
	bool advanced,
	int currentLevel,
	int maxLevel,
	int currentCount,
	int maxCount)
{
	return advanced &&
		currentLevel >= maxLevel &&
		CanDrawBlessingStone(currentCount, maxCount);
}

inline bool HitsPerTenThousandChance(
	int roll,
	int chancePerTenThousand)
{
	return chancePerTenThousand > 0 &&
		roll >= 1 &&
		roll <= (std::min)(chancePerTenThousand, 10000);
}
}
