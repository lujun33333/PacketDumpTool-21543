#include "../TopUIItemInfo25116.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

// The regression target only exercises the pure payload builder.  The
// production translation unit still references SendToClient, so provide a
// link-only stub that must never be called by this test.
BOOL SendToClient(
	std::shared_ptr<ClientNode>,
	short,
	const BYTE*,
	DWORD,
	short,
	char,
	short,
	short,
	char)
{
	std::cerr << "unexpected SendToClient call in 25116 regression\n";
	return FALSE;
}

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::cerr << "usage: TopUIItemInfo25116Regression <wire-body-fixture>\n";
		return 2;
	}

	std::ifstream input(argv[1], std::ios::binary);
	if (!input)
	{
		std::cerr << "cannot open fixture: " << argv[1] << "\n";
		return 3;
	}

	const std::vector<BYTE> fixture{
		std::istreambuf_iterator<char>(input),
		std::istreambuf_iterator<char>()};
	if (fixture.size() < 2)
	{
		std::cerr << "fixture is shorter than the two-byte wire length\n";
		return 4;
	}

	const size_t declaredWireLength =
		(static_cast<size_t>(fixture[0]) << 8) | fixture[1];
	if (declaredWireLength != fixture.size())
	{
		std::cerr << "wire length mismatch: declared=" << declaredWireLength
			<< " actual=" << fixture.size() << "\n";
		return 5;
	}

	const std::vector<BYTE> actual = BuildTopUIItemInfo25116Payload(10001);
	const std::vector<BYTE> expected(fixture.begin() + 2, fixture.end());
	if (actual != expected)
	{
		size_t firstDifference = 0;
		while (firstDifference < actual.size() &&
			firstDifference < expected.size() &&
			actual[firstDifference] == expected[firstDifference])
		{
			++firstDifference;
		}
		std::cerr << "25116 payload mismatch: actual=" << actual.size()
			<< " expected=" << expected.size()
			<< " firstDifference=" << firstDifference << "\n";
		return 1;
	}

	std::cout << "TopUIItemInfo25116Regression passed: payload="
		<< actual.size() << " wireBody=" << fixture.size() << "\n";
	return 0;
}
