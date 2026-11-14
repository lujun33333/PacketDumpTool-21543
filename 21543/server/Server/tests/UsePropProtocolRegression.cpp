#include "../UsePropProtocol.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
using qqspeed::useprop::AuthoritativeUsePropResult;
using qqspeed::useprop::MakeNotify517;
using qqspeed::useprop::Notify517;
using qqspeed::useprop::OtherPropPosition;
using qqspeed::useprop::ParseRequest165;
using qqspeed::useprop::Request165;
using qqspeed::useprop::Request165Flavor;
using qqspeed::useprop::SerializeNotify517;

int failures = 0;

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void Append8(std::vector<std::uint8_t>& out, std::uint8_t value)
{
    out.push_back(value);
}

void Append16(std::vector<std::uint8_t>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void Append32(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void AppendSigned32(std::vector<std::uint8_t>& out, std::int32_t value)
{
    Append32(out, static_cast<std::uint32_t>(value));
}

std::vector<std::uint8_t> MakeBoostRequest165()
{
    std::vector<std::uint8_t> body;
    Append32(body, 0x9DDD9D84u); // Uin
    Append32(body, 0x01020304u); // Time
    Append16(body, 1);          // PropID
    Append32(body, 0x9DDD9D84u); // DstUin
    AppendSigned32(body, -139);
    AppendSigned32(body, -105);
    AppendSigned32(body, 18);
    Append32(body, 1);  // PropIndex
    Append16(body, 0);  // NewID
    Append8(body, 0);   // IsClearProp
    Append8(body, 0);   // DstType
    Append32(body, 0);  // MyUseItemID
    Append32(body, 0);  // DstUseItemID
    Append8(body, 0);   // IsConvertMode
    AppendSigned32(body, -1); // StoneSkillType
    Append32(body, 0);  // LapTime
    Append8(body, 0);   // DstUinNum
    Append8(body, 0);   // PropUseMode
    Append8(body, 0);   // QueryUinNum
    Append8(body, 0);   // CurrentSelfRank
    Append8(body, 0);   // CurrentTeammateRank
    Append8(body, 0);   // Position
    Append8(body, 0);   // PropSecType
    Append8(body, 0);   // PropPosIdx
    Append32(body, 0);  // NPCUin
    Append8(body, 0);   // UsePropProtoMode
    Append16(body, 0);  // SubPropID
    Append8(body, 0);   // OtherPropPosNum
    return body;
}

void TestOfficialBoostFixture()
{
    // First official 21487 Msg517 sample from PID 15588. The two-byte wire
    // BodyLen (0x0037) is intentionally excluded; this is the 53-byte payload.
    const std::vector<std::uint8_t> expected{
        0x9D, 0xDD, 0x9D, 0x84, 0x00, 0x01, 0x9D, 0xDD,
        0x9D, 0x84, 0xFF, 0xFF, 0xFF, 0x75, 0xFF, 0xFF,
        0xFF, 0x97, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00};

    const std::vector<std::uint8_t> requestBody = MakeBoostRequest165();
    Request165 request;
    std::string error;
    Check(ParseRequest165(
              requestBody.data(),
              requestBody.size(),
              Request165Flavor::QqSpeed21487,
              request,
              &error),
        error.c_str());

    AuthoritativeUsePropResult result;
    const Notify517 notify = MakeNotify517(request, result);
    std::vector<std::uint8_t> actual;
    error.clear();
    Check(SerializeNotify517(notify, actual, &error), error.c_str());
    Check(actual == expected, "official boost Msg517 payload mismatch");
    Check(actual.size() == 53, "zero-list Msg517 payload must naturally be 53 bytes");
}

std::vector<std::uint8_t> MakeDynamicRequest165()
{
    std::vector<std::uint8_t> body;
    Append32(body, 0x01020304u);
    Append32(body, 0x11223344u);
    Append16(body, 777);
    Append32(body, 0xA1A2A3A4u);
    AppendSigned32(body, -1);
    AppendSigned32(body, 2);
    AppendSigned32(body, -3);
    Append32(body, 0x55667788u);
    Append16(body, 0x99AAu);
    Append8(body, 1);
    Append8(body, 2);
    Append32(body, 0x10203040u);
    Append32(body, 0x50607080u);
    Append8(body, 1);
    AppendSigned32(body, -7);
    Append32(body, 0xAABBCCDDu);
    Append8(body, 2);
    Append32(body, 0x11111111u);
    Append32(body, 0x22222222u);
    Append8(body, 3);
    Append8(body, 1);
    Append32(body, 0x33333333u);
    Append8(body, 4);
    Append8(body, 5);
    Append8(body, 6);
    Append8(body, 0xFEu);
    Append8(body, 7);
    Append32(body, 0x44444444u);
    Append8(body, 8);
    Append16(body, 0x1234u);
    Append8(body, 2);

    Append16(body, 14); // length includes this BE16
    Append32(body, 0xABCDEF01u);
    AppendSigned32(body, -100);
    AppendSigned32(body, 200);

    Append16(body, 6);
    Append32(body, 0x13572468u);
    return body;
}

void TestDynamicListsAndExactFieldPropagation()
{
    const std::vector<std::uint8_t> body = MakeDynamicRequest165();
    Request165 request;
    std::string error;
    Check(ParseRequest165(
              body.data(), body.size(), Request165Flavor::QqSpeed21487, request, &error),
        error.c_str());
    Check(request.propId == 777, "PropID 777 must not be aliased");
    Check(request.dstUins.size() == 2 && request.dstUins[1] == 0x22222222u,
        "DstUin array was not parsed");
    Check(request.queryUins.size() == 1 && request.queryUins[0] == 0x33333333u,
        "QueryUin array was not parsed");
    Check(request.otherPropPositions.size() == 2,
        "OtherPropPos records were not parsed");
    Check(request.otherPropPositions[0].positions.size() == 2 &&
            request.otherPropPositions[0].positions[0] == -100,
        "OtherPropPos coordinates were not parsed");

    AuthoritativeUsePropResult result;
    result.parameters = {0x01010101, -2};
    result.status = 9;
    result.passedCheckPointIds = {0x2468u, 0xACE0u};

    const Notify517 notify = MakeNotify517(request, result);
    std::vector<std::uint8_t> actual;
    error.clear();
    Check(SerializeNotify517(notify, actual, &error), error.c_str());

    std::vector<std::uint8_t> expected;
    Append32(expected, 0x01020304u);
    Append16(expected, 777);
    Append32(expected, 0xA1A2A3A4u);
    AppendSigned32(expected, -1);
    AppendSigned32(expected, 2);
    AppendSigned32(expected, -3);
    Append32(expected, 0x55667788u);
    Append16(expected, 0x99AAu);
    Append32(expected, 0x10203040u);
    AppendSigned32(expected, -7);
    Append32(expected, 0xAABBCCDDu);
    Append8(expected, 2);
    Append32(expected, 0x11111111u);
    Append32(expected, 0x22222222u);
    Append8(expected, 3);
    Append8(expected, 1);
    Append32(expected, 0x33333333u);
    Append8(expected, 2);
    Append8(expected, 6);
    Append8(expected, 2);
    AppendSigned32(expected, 0x01010101);
    AppendSigned32(expected, -2);
    Append8(expected, 9);
    Append8(expected, 7);
    Append8(expected, 0xFEu);
    Append8(expected, 2);
    Append16(expected, 0x2468u);
    Append16(expected, 0xACE0u);
    Append16(expected, 0x1234u);
    Append8(expected, 2);
    Append16(expected, 14);
    Append32(expected, 0xABCDEF01u);
    AppendSigned32(expected, -100);
    AppendSigned32(expected, 200);
    Append16(expected, 6);
    Append32(expected, 0x13572468u);

    Check(actual == expected, "dynamic Msg517 field order or length mismatch");
    Check(actual.size() == 97, "dynamic Msg517 payload length mismatch");
}

void TestMalformedRequestsAreRejected()
{
    Request165 request;
    std::string error;

    std::vector<std::uint8_t> truncated = MakeBoostRequest165();
    truncated.pop_back();
    Check(!ParseRequest165(
              truncated.data(), truncated.size(), Request165Flavor::QqSpeed21487, request, &error),
        "truncated request was accepted");

    std::vector<std::uint8_t> trailing = MakeBoostRequest165();
    trailing.push_back(0xCCu);
    error.clear();
    Check(!ParseRequest165(
              trailing.data(), trailing.size(), Request165Flavor::QqSpeed21487, request, &error),
        "request with trailing bytes was accepted");

    std::vector<std::uint8_t> invalidRecord = MakeDynamicRequest165();
    // The first OtherPropPos record begins 14 bytes from the end in this fixture.
    const size_t firstRecordOffset = invalidRecord.size() - 20;
    invalidRecord[firstRecordOffset] = 0;
    invalidRecord[firstRecordOffset + 1] = 7;
    error.clear();
    Check(!ParseRequest165(
              invalidRecord.data(), invalidRecord.size(), Request165Flavor::QqSpeed21487, request, &error),
        "misaligned OtherPropPos record was accepted");

    Notify517 oversized;
    oversized.dstUins.resize(256);
    std::vector<std::uint8_t> output{0xAAu};
    error.clear();
    Check(!SerializeNotify517(oversized, output, &error),
        "Msg517 count overflow was truncated instead of rejected");
    Check(output.empty(), "failed serialization left a partial payload");
}
} // namespace

int main()
{
    TestOfficialBoostFixture();
    TestDynamicListsAndExactFieldPropagation();
    TestMalformedRequestsAreRejected();

    if (failures != 0)
    {
        std::cerr << failures << " UseProp protocol regression(s) failed\n";
        return 1;
    }

    std::cout << "UsePropProtocolRegression passed\n";
    return 0;
}
