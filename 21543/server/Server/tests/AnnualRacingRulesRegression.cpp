#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../AnnualRacingRules.h"

namespace
{
void Expect(const char* name, bool actual, bool expected)
{
	if (actual == expected)
		return;

	std::cerr << name << ": expected " << expected << ", got " << actual << '\n';
	std::exit(1);
}

}

int main()
{
	const std::vector<int> sharedPools{ 3, 4, 5, 6 };
	Expect(
		"shared reward is available in level three",
		AnnualRacingRules::ContainsPool(sharedPools, 3),
		true);
	Expect(
		"shared reward is available in level six",
		AnnualRacingRules::ContainsPool(sharedPools, 6),
		true);
	Expect(
		"shared reward is absent before level three",
		AnnualRacingRules::ContainsPool(sharedPools, 2),
		false);

	Expect(
		"first upward lighting grants its node reward",
		AnnualRacingRules::ShouldGrantFirstLight(2, 3, 0u, 3),
		true);
	Expect(
		"relighting after a downgrade does not grant twice",
		AnnualRacingRules::ShouldGrantFirstLight(
			2,
			3,
			std::uint32_t{ 1 } << 2,
			3),
		false);
	Expect(
		"same-level draw has no node reward",
		AnnualRacingRules::ShouldGrantFirstLight(3, 3, 0u, 3),
		false);
	Expect(
		"downgrade has no node reward",
		AnnualRacingRules::ShouldGrantFirstLight(3, 2, 0u, 2),
		false);
	Expect(
		"reward must match the newly lit level",
		AnnualRacingRules::ShouldGrantFirstLight(2, 3, 0u, 2),
		false);

	Expect(
		"blessing stone remains drawable below its event cap",
		AnnualRacingRules::CanDrawBlessingStone(59, 60),
		true);
	Expect(
		"blessing stone is removed after reaching its event cap",
		AnnualRacingRules::CanDrawBlessingStone(60, 60),
		false);
	Expect(
		"random blessing-stone hit increments its counter",
		AnnualRacingRules::ShouldCountBlessingStone(
			163289u,
			163289u,
			0,
			60),
		true);
	Expect(
		"non-stone draw never increments the stone counter",
		AnnualRacingRules::ShouldCountBlessingStone(
			98263u,
			163289u,
			0,
			60),
		false);
	Expect(
		"stone counter never exceeds its event cap",
		AnnualRacingRules::ShouldCountBlessingStone(
			163289u,
			163289u,
			60,
			60),
		false);

	Expect(
		"max-star advanced draw is eligible for the configured stone roll",
		AnnualRacingRules::IsMaxStarAdvancedBlessingStoneEligible(
			true,
			6,
			6,
			0,
			60),
		true);
	Expect(
		"normal draw is not eligible for the max-star stone roll",
		AnnualRacingRules::IsMaxStarAdvancedBlessingStoneEligible(
			false,
			6,
			6,
			0,
			60),
		false);
	Expect(
		"pre-max draw is not eligible for the max-star stone roll",
		AnnualRacingRules::IsMaxStarAdvancedBlessingStoneEligible(
			true,
			5,
			6,
			0,
			60),
		false);
	Expect(
		"event stone cap disables the max-star stone roll",
		AnnualRacingRules::IsMaxStarAdvancedBlessingStoneEligible(
			true,
			6,
			6,
			60,
			60),
		false);
	Expect(
		"ten-percent boundary roll hits",
		AnnualRacingRules::HitsPerTenThousandChance(1000, 1000),
		true);
	Expect(
		"roll immediately above ten-percent boundary misses",
		AnnualRacingRules::HitsPerTenThousandChance(1001, 1000),
		false);
	Expect(
		"zero configured chance never hits",
		AnnualRacingRules::HitsPerTenThousandChance(1, 0),
		false);

	std::cout << "AnnualRacingRulesRegression passed\n";
	return 0;
}
