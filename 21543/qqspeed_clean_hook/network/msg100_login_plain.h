#pragma once

#include <cstddef>

namespace qqspeed::network
{
constexpr int kLoginPlainMinLen = 0x35;
// 21932 appends a DWORD and one optional five-byte record (count + BYTE/DWORD)
// to the 21543 fixed layout.  The direct compact stream leaves both zeroed.
constexpr int kMsg100LoginPlainLen = 0x4D79B;
constexpr size_t kLoginPlainResultIdOffset = 0x00;
constexpr size_t kLoginPlainKeyLenOffset = 0x02;
constexpr size_t kLoginPlainKeyOffset = 0x03;
constexpr size_t kLoginPlainUinOffset = 0x23;
constexpr size_t kLoginPlainIdentityOffset = 0x27;
constexpr size_t kLoginPlainConnIdOffset = 0x2B;
constexpr size_t kLoginPlainLoginTypeOffset = 0x2D;
constexpr size_t kLoginPlainGameSvrIdentityOffset = 0x31;
constexpr size_t kLoginPlainItemNumOffset = 0x156;
constexpr size_t kLoginPlainItemArrayOffset = 0x158;
constexpr size_t kLoginPlainMapNumOffset = 0x33D8C;
constexpr size_t kLoginPlainPetNumOffset = 0x354FE;
constexpr size_t kLoginPlainOverdueNumOffset = 0x4CA2E;
constexpr size_t kLoginPlainPostOverdueOffset = 0x4CEDF;
constexpr size_t kLoginPlainHasMoreInfoOffset = 0x4CF20;
constexpr size_t kLoginPlainCurSvrIdOffset = 0x4CF21;
constexpr size_t kLoginPlainExtLenOffset = 0x4D38F;
constexpr size_t kLoginPlainExtDataOffset = 0x4D391;
constexpr size_t kLoginPlainExtCapacity = 0x400;

struct LoginPlainSnapshot
{
	bool readable = false;
	bool shapeOk = false;
	unsigned short resultId = 0xFFFF;
	unsigned char keyLen = 0;
	char key[17] = {};
	unsigned int uin = 0;
	unsigned int identity = 0;
	unsigned short connId = 0;
	unsigned int loginType = 0;
	unsigned int gameSvrIdentity = 0;
};

struct Msg100ExpandSnapshot
{
	bool attempted = false;
	bool ok = false;
	unsigned short streamLen = 0;
	unsigned short resultId = 0xFFFF;
	unsigned char keyLen = 0;
	char key[17] = {};
	unsigned int uin = 0;
	unsigned int identity = 0;
	unsigned short connId = 0;
	unsigned int loginType = 0;
	unsigned int gameSvrIdentity = 0;
	unsigned short playerBaseLen = 0;
	unsigned short itemNum = 0;
	unsigned short itemWritten = 0;
	unsigned short taskNum = 0;
	unsigned short mapNum = 0;
	unsigned short mapWritten = 0;
	unsigned short petNum = 0;
	unsigned char overdueNum = 0;
	unsigned char hasMoreInfo = 0;
	unsigned short curSvrId = 0;
	unsigned short extLen = 0;
	size_t tailStartPos = 0;
	size_t tailConsumed = 0;
	size_t remaining = 0;
	size_t finalStreamPos = 0;
	char reason[96] = {};
};

LoginPlainSnapshot ReadLoginPlainSnapshot(const unsigned char* outData, int outLen);
bool TryExpandDirectMsg100LoginPlain(
	const unsigned char* body,
	int bodyLen,
	void* outData,
	void* outLen,
	Msg100ExpandSnapshot& snapshot);
}
