#include "LauncherRuntimeMaterial.h"

#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::uint64_t kIssuedAt = 1750000000000ULL;

void Write16BE(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value >> 8);
    output[1] = static_cast<std::uint8_t>(value);
}

std::uint32_t Read32BE(const std::uint8_t* input) {
    return (static_cast<std::uint32_t>(input[0]) << 24) |
        (static_cast<std::uint32_t>(input[1]) << 16) |
        (static_cast<std::uint32_t>(input[2]) << 8) |
        static_cast<std::uint32_t>(input[3]);
}

std::uint64_t Read64BE(const std::uint8_t* input) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value = (value << 8) | input[i];
    return value;
}

struct RsaFixture {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_KEY_HANDLE key = nullptr;
    std::array<std::uint8_t, launcher_runtime::kPublicKeyBytes> publicWire{};

    ~RsaFixture() {
        if (key) BCryptDestroyKey(key);
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    }
};

bool CreateRsaFixture(RsaFixture& fixture) {
    if (BCryptOpenAlgorithmProvider(&fixture.algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGenerateKeyPair(fixture.algorithm, &fixture.key, 2048, 0) < 0 ||
        BCryptFinalizeKeyPair(fixture.key, 0) < 0) {
        return false;
    }
    ULONG size = 0;
    if (BCryptExportKey(fixture.key, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &size, 0) < 0) return false;
    std::vector<std::uint8_t> blob(size);
    if (BCryptExportKey(fixture.key, nullptr, BCRYPT_RSAPUBLIC_BLOB, blob.data(), size, &size, 0) < 0 ||
        size < sizeof(BCRYPT_RSAKEY_BLOB)) return false;
    auto* header = reinterpret_cast<const BCRYPT_RSAKEY_BLOB*>(blob.data());
    if (header->Magic != BCRYPT_RSAPUBLIC_MAGIC || header->BitLength != 2048 ||
        header->cbPublicExp != 3 || header->cbModulus != 256) return false;
    const auto* exponent = blob.data() + sizeof(BCRYPT_RSAKEY_BLOB);
    const auto* modulus = exponent + header->cbPublicExp;
    std::memcpy(fixture.publicWire.data(), exponent, 3);
    std::memcpy(fixture.publicWire.data() + 3, modulus, 256);
    SecureZeroMemory(blob.data(), blob.size());
    return true;
}

std::vector<std::uint8_t> MakeRequest(
    const std::array<std::uint8_t, 32>& bundleId,
    const std::array<std::uint8_t, 16>& clientNonce,
    const RsaFixture& rsa,
    std::uint8_t protocolVersion = launcher_runtime::kProtocolVersion) {
    std::vector<std::uint8_t> body(launcher_runtime::kRequestBytes, 0);
    body[0] = protocolVersion;
    body[1] = launcher_runtime::kAlgorithmRsaOaepSha256;
    std::memcpy(body.data() + 4, bundleId.data(), bundleId.size());
    std::memcpy(body.data() + 36, clientNonce.data(), clientNonce.size());
    Write16BE(body.data() + 52, static_cast<std::uint16_t>(rsa.publicWire.size()));
    std::memcpy(body.data() + 54, rsa.publicWire.data(), rsa.publicWire.size());
    return body;
}

bool DecryptMaterial(RsaFixture& rsa, const std::vector<std::uint8_t>& response, std::vector<std::uint8_t>& cleartext) {
    if (response.size() != launcher_runtime::kResponseBytes) return false;
    BCRYPT_OAEP_PADDING_INFO padding{};
    padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;
    ULONG outputBytes = 0;
    const auto* ciphertext = response.data() + 86;
    if (BCryptDecrypt(rsa.key, const_cast<PUCHAR>(ciphertext), 256, &padding,
        nullptr, 0, nullptr, 0, &outputBytes, BCRYPT_PAD_OAEP) < 0) return false;
    cleartext.resize(outputBytes);
    return BCryptDecrypt(rsa.key, const_cast<PUCHAR>(ciphertext), 256, &padding,
        nullptr, 0, cleartext.data(), static_cast<ULONG>(cleartext.size()), &outputBytes,
        BCRYPT_PAD_OAEP) >= 0;
}

bool AllZero(const std::uint8_t* bytes, std::size_t size) {
    std::uint8_t aggregate = 0;
    for (std::size_t i = 0; i < size; ++i) aggregate |= bytes[i];
    return aggregate == 0;
}

} // namespace

int main(int argc, char** argv) {
    RsaFixture rsa;
    if (!CreateRsaFixture(rsa)) {
        std::cerr << "RSA fixture generation failed\n";
        return 1;
    }
    std::array<std::uint8_t, 32> bundleId{};
    std::array<std::uint8_t, 32> releaseMaster{};
    std::array<std::uint8_t, 32> contentKey{};
    const std::array<std::uint8_t, 32> expectedContentKey{
        0xc1, 0xdb, 0x7a, 0xd1, 0x7a, 0x09, 0xe1, 0xce,
        0xa1, 0x00, 0x19, 0xad, 0xc1, 0xee, 0xba, 0x63,
        0xb4, 0xa1, 0xb2, 0x0f, 0x55, 0x16, 0xcf, 0x5a,
        0xdd, 0x6f, 0x50, 0x81, 0x1a, 0x4b, 0xa7, 0xd6
    };
    std::array<std::uint8_t, 16> clientNonce{};
    for (std::size_t i = 0; i < bundleId.size(); ++i) {
        bundleId[i] = static_cast<std::uint8_t>(0xa0 + i);
        releaseMaster[i] = static_cast<std::uint8_t>(i);
    }
    for (std::size_t i = 0; i < clientNonce.size(); ++i) clientNonce[i] = static_cast<std::uint8_t>(0x40 + i);
    if (!launcher_runtime::DeriveContentKey(releaseMaster, bundleId, contentKey) ||
        contentKey != expectedContentKey) {
        std::cerr << "HKDF derivation failed\n";
        return 2;
    }

    launcher_runtime::RuntimeMaterialService service;
    std::string error;
    if (argc != 1) {
        std::cerr << "Unexpected arguments\n";
        return 2;
    }
    const bool configured = service.ConfigureFromReleaseMaster(bundleId, releaseMaster, error);
    if (!configured) {
        std::cerr << "Configure failed: " << error << "\n";
        return 2;
    }
    auto request = MakeRequest(bundleId, clientNonce, rsa);
    std::vector<std::uint8_t> response;
    if (!service.Issue(10001, request.data(), request.size(), kIssuedAt, response, error)) {
        std::cerr << "Issue failed: " << error << "\n";
        return 3;
    }
    if (response.size() != launcher_runtime::kResponseBytes ||
        response[0] != launcher_runtime::kProtocolVersion ||
        response[1] != launcher_runtime::kAlgorithmRsaOaepSha256 ||
        std::memcmp(response.data() + 4, bundleId.data(), bundleId.size()) != 0 ||
        std::memcmp(response.data() + 52, clientNonce.data(), clientNonce.size()) != 0 ||
        AllZero(response.data() + 36, 16) ||
        Read64BE(response.data() + 68) != kIssuedAt ||
        Read64BE(response.data() + 76) != kIssuedAt + launcher_runtime::kMaterialTtlMs) {
        std::cerr << "Response binding failed\n";
        return 4;
    }
    std::vector<std::uint8_t> cleartext;
    if (!DecryptMaterial(rsa, response, cleartext) || cleartext.size() != launcher_runtime::kPlaintextBytes ||
        std::memcmp(cleartext.data(), "QQSRMAT1", 8) != 0 ||
        Read32BE(cleartext.data() + 12) != 10001 ||
        std::memcmp(cleartext.data() + 16, bundleId.data(), 32) != 0 ||
        std::memcmp(cleartext.data() + 48, response.data() + 36, 16) != 0 ||
        std::memcmp(cleartext.data() + 64, clientNonce.data(), 16) != 0 ||
        Read64BE(cleartext.data() + 112) != kIssuedAt ||
        Read64BE(cleartext.data() + 120) != kIssuedAt + launcher_runtime::kMaterialTtlMs ||
        std::memcmp(cleartext.data() + 128, contentKey.data(), 32) != 0) {
        std::cerr << "OAEP plaintext binding failed\n";
        return 5;
    }

    std::vector<std::uint8_t> anonymousResponse;
    if (service.Issue(0, request.data(), request.size(), kIssuedAt, anonymousResponse, error)) {
        std::cerr << "V1 accepted an anonymous subject\n";
        return 6;
    }

    auto preloadRequest = MakeRequest(
        bundleId,
        clientNonce,
        rsa,
        launcher_runtime::kPreloginProtocolVersion);
    if (service.Issue(
            10001,
            preloadRequest.data(),
            preloadRequest.size(),
            kIssuedAt,
            anonymousResponse,
            error) ||
        service.IssuePrelogin(
            request.data(),
            request.size(),
            kIssuedAt,
            anonymousResponse,
            error)) {
        std::cerr << "V1 and V2 issue APIs were not protocol-separated\n";
        return 6;
    }
    if (!service.IssuePrelogin(
            preloadRequest.data(),
            preloadRequest.size(),
            kIssuedAt,
            anonymousResponse,
            error)) {
        std::cerr << "V2 prelogin issue failed: " << error << "\n";
        return 6;
    }
    if (anonymousResponse.size() != launcher_runtime::kResponseBytes ||
        anonymousResponse[0] != launcher_runtime::kPreloginProtocolVersion ||
        anonymousResponse[1] != launcher_runtime::kAlgorithmRsaOaepSha256 ||
        std::memcmp(anonymousResponse.data() + 4, bundleId.data(), bundleId.size()) != 0 ||
        std::memcmp(anonymousResponse.data() + 52, clientNonce.data(), clientNonce.size()) != 0) {
        std::cerr << "V2 prelogin response binding failed\n";
        return 6;
    }
    std::vector<std::uint8_t> anonymousCleartext;
    if (!DecryptMaterial(rsa, anonymousResponse, anonymousCleartext) ||
        anonymousCleartext.size() != launcher_runtime::kPlaintextBytes ||
        std::memcmp(anonymousCleartext.data(), "QQSRMAT2", 8) != 0 ||
        anonymousCleartext[8] != launcher_runtime::kPreloginProtocolVersion ||
        Read32BE(anonymousCleartext.data() + 12) != 0 ||
        std::memcmp(anonymousCleartext.data() + 16, bundleId.data(), bundleId.size()) != 0 ||
        std::memcmp(anonymousCleartext.data() + 128, contentKey.data(), contentKey.size()) != 0) {
        std::cerr << "V2 prelogin plaintext binding failed\n";
        return 6;
    }
    SecureZeroMemory(anonymousCleartext.data(), anonymousCleartext.size());
    SecureZeroMemory(anonymousResponse.data(), anonymousResponse.size());

    auto malformed = request;
    malformed.push_back(0);
    if (service.Issue(10001, malformed.data(), malformed.size(), kIssuedAt, response, error) ||
        service.Issue(10000, request.data(), request.size(), kIssuedAt, response, error)) {
        std::cerr << "Invalid request accepted\n";
        return 6;
    }
    request[4] ^= 1;
    std::array<std::uint8_t, 32> alternateBundleId{};
    std::array<std::uint8_t, 32> alternateContentKey{};
    std::memcpy(alternateBundleId.data(), request.data() + 4, alternateBundleId.size());
    if (!launcher_runtime::DeriveContentKey(releaseMaster, alternateBundleId, alternateContentKey) ||
        !service.Issue(10001, request.data(), request.size(), kIssuedAt, response, error)) {
        std::cerr << "Stable release rejected an alternate bundle id\n";
        return 7;
    }
    std::vector<std::uint8_t> alternateCleartext;
    if (!DecryptMaterial(rsa, response, alternateCleartext) ||
        alternateCleartext.size() != launcher_runtime::kPlaintextBytes ||
        std::memcmp(alternateCleartext.data() + 16, alternateBundleId.data(), alternateBundleId.size()) != 0 ||
        std::memcmp(alternateCleartext.data() + 128, alternateContentKey.data(), alternateContentKey.size()) != 0) {
        std::cerr << "Stable release did not derive the requested bundle key\n";
        return 7;
    }
    SecureZeroMemory(alternateCleartext.data(), alternateCleartext.size());

    auto alternatePreloadRequest = MakeRequest(
        alternateBundleId,
        clientNonce,
        rsa,
        launcher_runtime::kPreloginProtocolVersion);
    if (service.IssuePrelogin(
            alternatePreloadRequest.data(),
            alternatePreloadRequest.size(),
            kIssuedAt,
            response,
            error)) {
        std::cerr << "V2 prelogin accepted a non-configured bundle id\n";
        return 7;
    }
    SecureZeroMemory(alternateContentKey.data(), alternateContentKey.size());
    SecureZeroMemory(alternateBundleId.data(), alternateBundleId.size());

    launcher_runtime::RuntimeMaterialService invalidService;
    std::array<std::uint8_t, 32> zeroMaster{};
    if (invalidService.ConfigureFromReleaseMaster(bundleId, zeroMaster, error)) {
        std::cerr << "Zero release master accepted\n";
        return 8;
    }

#if defined(QQS_EMBEDDED_RELEASE_MATERIAL)
    launcher_runtime::RuntimeMaterialService embeddedService;
    if (!embeddedService.ConfigureFromEmbeddedRelease(error)) {
        std::cerr << "Embedded configure failed: " << error << "\n";
        return 9;
    }
    auto embeddedRequest = MakeRequest(bundleId, clientNonce, rsa);
    std::vector<std::uint8_t> embeddedResponse;
    if (!embeddedService.Issue(
            10001,
            embeddedRequest.data(),
            embeddedRequest.size(),
            kIssuedAt,
            embeddedResponse,
            error)) {
        std::cerr << "Embedded issue failed: " << error << "\n";
        return 10;
    }
    std::vector<std::uint8_t> embeddedCleartext;
    if (!DecryptMaterial(rsa, embeddedResponse, embeddedCleartext) ||
        embeddedCleartext.size() != launcher_runtime::kPlaintextBytes ||
        std::memcmp(embeddedCleartext.data() + 16, bundleId.data(), bundleId.size()) != 0 ||
        AllZero(embeddedCleartext.data() + 128, contentKey.size())) {
        std::cerr << "Embedded material binding failed\n";
        return 11;
    }
    SecureZeroMemory(embeddedCleartext.data(), embeddedCleartext.size());
#endif

    SecureZeroMemory(cleartext.data(), cleartext.size());
    SecureZeroMemory(releaseMaster.data(), releaseMaster.size());
    SecureZeroMemory(contentKey.data(), contentKey.size());
    std::cout << "LAUNCHER_RUNTIME_MATERIAL_TEST_OK\n";
    return 0;
}
