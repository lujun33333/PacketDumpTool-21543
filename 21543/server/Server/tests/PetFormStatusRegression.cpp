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

namespace
{
void ExpectAllowed(const char* name, std::uint32_t status, std::uint32_t strengthLevel, bool expected)
{
	const bool actual = QQS_IsPetFormStatusAllowed(status, strengthLevel);
	if (actual == expected)
		return;

	std::cerr << name << ": expected " << expected << ", got " << actual << '\n';
	std::exit(1);
}
}

int main()
{
	ExpectAllowed("status zero is invalid", 0, 10, false);
	ExpectAllowed("normal form remains valid", 3, 0, true);
	ExpectAllowed("status four remains valid", 4, 0, true);
	ExpectAllowed("+10 form is locked below ten", 5, 9, false);
	ExpectAllowed("+10 form is valid at ten", 5, 10, true);
	ExpectAllowed("status above five is invalid", 6, 10, false);

	std::cout << "PetFormStatusRegression passed\n";
	return 0;
}
