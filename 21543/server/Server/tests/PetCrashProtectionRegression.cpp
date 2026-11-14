#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

struct ClientNode;
struct PetInfo;

#include "../pet.h"
#include "../KartPhys551PlainEncoder.h"

namespace
{
void ExpectEqual(const char* name, int actual, int expected)
{
	if (actual == expected)
		return;

	std::cerr << name << ": expected " << expected << ", got " << actual << '\n';
	std::exit(1);
}
}

int main(int argc, char** argv)
{
	ExpectEqual("snow-princess official pair", QQS_ApplyOfficialPetCrashProtection(485, 850), 923);
	ExpectEqual("qilin official pair", QQS_ApplyOfficialPetCrashProtection(435, 860), 921);
	ExpectEqual("same pet changes with pre-pet protection", QQS_ApplyOfficialPetCrashProtection(435, 850), 916);
	ExpectEqual("no pet keeps the pre-pet value", QQS_ApplyOfficialPetCrashProtection(435, 0), 435);
	ExpectEqual("full pre-pet protection stays full", QQS_ApplyOfficialPetCrashProtection(1000, 850), 1000);
	ExpectEqual("full pet protection reaches full", QQS_ApplyOfficialPetCrashProtection(0, 1000), 1000);

	YAML::Node canonicalTail(YAML::NodeType::Map);
	canonicalTail["m_iCrashCompensatePower"] = 374;
	YAML::Node canonicalCrash = QQS_PetCrashCompensatePowerNode(canonicalTail);
	ExpectEqual("canonical crash field is read", canonicalCrash.as<int>(), 374);
	canonicalCrash = QQS_ApplyOfficialPetCrashProtection(canonicalCrash.as<int>() + 61, 860);
	ExpectEqual("canonical crash field is updated", canonicalTail["m_iCrashCompensatePower"].as<int>(), 921);

	YAML::Node shortTail(YAML::NodeType::Map);
	shortTail["CrashCompensatePower"] = 435;
	YAML::Node shortCrash = QQS_PetCrashCompensatePowerNode(shortTail);
	shortCrash = QQS_ApplyOfficialPetCrashProtection(shortCrash.as<int>(), 860);
	ExpectEqual("short crash field stays compatible", shortTail["CrashCompensatePower"].as<int>(), 921);

	if (argc != 3)
	{
		std::cerr << "expected runtime 163043.yml and 109563.yml paths\n";
		return 1;
	}
	YAML::Node runtimeConfig = YAML::LoadFile(argv[1]);
	YAML::Node snowRuntimeConfig = YAML::LoadFile(argv[2]);
	std::vector<unsigned char> beforeData;
	std::vector<unsigned char> afterData;
	std::string encodeError;
	if (!QQSpeedKartPhys551::BuildCompactDataFromPlainYaml(runtimeConfig, beforeData, &encodeError))
	{
		std::cerr << "baseline runtime YAML encode failed: " << encodeError << '\n';
		return 1;
	}
	YAML::Node runtimeCrash = QQS_PetCrashCompensatePowerNode(runtimeConfig["Plain551"]["Tail"]);
	ExpectEqual("runtime canonical crash baseline", runtimeCrash.as<int>(), 374);
	runtimeCrash = QQS_ApplyOfficialPetCrashProtection(runtimeCrash.as<int>() + 61, 860);
	ExpectEqual("runtime canonical crash official result", runtimeConfig["Plain551"]["Tail"]["m_iCrashCompensatePower"].as<int>(), 921);
	ExpectEqual("qilin profile collision lose rate", QQS_OfficialCollisionLoseRateFromProfile(runtimeConfig["Plain551"], 29), 38);
	ExpectEqual("snow profile collision lose rate", QQS_OfficialCollisionLoseRateFromProfile(snowRuntimeConfig["Plain551"], 29), 37);
	ExpectEqual("missing profile collision fallback", QQS_OfficialCollisionLoseRateFromProfile(YAML::Node(YAML::NodeType::Map), 29), 37);
	YAML::Node runtimeCollisionLoseRate = QQS_551CollisionLoseRateNode(runtimeConfig["Plain551"]["Tail"]);
	runtimeCollisionLoseRate = QQS_OfficialCollisionLoseRateFromProfile(runtimeConfig["Plain551"], 29);
	ExpectEqual("runtime canonical collision lose rate", runtimeConfig["Plain551"]["Tail"]["m_ushCollisionLoseRate"].as<int>(), 38);
	if (!QQSpeedKartPhys551::BuildCompactDataFromPlainYaml(runtimeConfig, afterData, &encodeError))
	{
		std::cerr << "updated runtime YAML encode failed: " << encodeError << '\n';
		return 1;
	}
	if (beforeData == afterData)
	{
		std::cerr << "runtime encoder ignored the updated canonical crash field\n";
		return 1;
	}

	YAML::Node collisionOnlyConfig = YAML::LoadFile(argv[1]);
	std::vector<unsigned char> collisionBeforeData;
	std::vector<unsigned char> collisionAfterData;
	if (!QQSpeedKartPhys551::BuildCompactDataFromPlainYaml(collisionOnlyConfig, collisionBeforeData, &encodeError))
	{
		std::cerr << "collision baseline YAML encode failed: " << encodeError << '\n';
		return 1;
	}
	YAML::Node collisionOnlyNode = QQS_551CollisionLoseRateNode(collisionOnlyConfig["Plain551"]["Tail"]);
	collisionOnlyNode = QQS_OfficialCollisionLoseRateFromProfile(collisionOnlyConfig["Plain551"], 29);
	if (!QQSpeedKartPhys551::BuildCompactDataFromPlainYaml(collisionOnlyConfig, collisionAfterData, &encodeError))
	{
		std::cerr << "collision updated YAML encode failed: " << encodeError << '\n';
		return 1;
	}
	if (collisionBeforeData == collisionAfterData)
	{
		std::cerr << "runtime encoder ignored the updated canonical collision lose rate\n";
		return 1;
	}

	std::cout << "PetCrashProtectionRegression passed\n";
	return 0;
}
