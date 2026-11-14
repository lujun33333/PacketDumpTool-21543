#include "LauncherRuntimeMaterial.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <utility>

#if defined(QQS_EMBEDDED_RELEASE_MATERIAL)
#include "LauncherReleaseMaterial.generated.inc"
#endif

namespace launcher_runtime {
namespace {

constexpr std::size_t kBundleIdBytes = 32;
constexpr std::size_t kClientNonceBytes = 16;
constexpr std::size_t kTokenIdBytes = 16;
constexpr std::size_t kFingerprintBytes = 32;
constexpr std::size_t kContentKeyBytes = 32;
constexpr std::size_t kCiphertextBytes = 256;
constexpr std::array<std::uint8_t, 8> kPlaintextMagic{ 'Q', 'Q', 'S', 'R', 'M', 'A', 'T', '1' };
constexpr std::array<std::uint8_t, 8> kPreloginPlaintextMagic{ 'Q', 'Q', 'S', 'R', 'M', 'A', 'T', '2' };
constexpr std::array<std::uint8_t, 15> kHkdfExpandInfo{
    'Q', 'Q', 'S', '-', 'R', 'U', 'N', 'T', 'I', 'M', 'E', '-', 'V', '1', 0x01
};

bool Succeeded(NTSTATUS status) {
    return status >= 0;
}

void Write16BE(std::uint8_t* output, std::uint16_t value) {
    output[0] = static_cast<std::uint8_t>(value >> 8);
    output[1] = static_cast<std::uint8_t>(value);
}

void Write32BE(std::uint8_t* output, std::uint32_t value) {
    output[0] = static_cast<std::uint8_t>(value >> 24);
    output[1] = static_cast<std::uint8_t>(value >> 16);
    output[2] = static_cast<std::uint8_t>(value >> 8);
    output[3] = static_cast<std::uint8_t>(value);
}

void Write64BE(std::uint8_t* output, std::uint64_t value) {
    for (int index = 7; index >= 0; --index) {
        output[index] = static_cast<std::uint8_t>(value);
        value >>= 8;
    }
}

std::uint16_t Read16BE(const std::uint8_t* input) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(input[0]) << 8) |
        static_cast<std::uint16_t>(input[1]));
}

bool IsAllZero(const std::uint8_t* bytes, std::size_t size) {
    std::uint8_t aggregate = 0;
    for (std::size_t index = 0; index < size; ++index) aggregate |= bytes[index];
    return aggregate == 0;
}

bool LoadEmbeddedReleaseMaterial(
    std::array<std::uint8_t, 32>& bundleId,
    std::array<std::uint8_t, 32>& releaseMaster) {
    bundleId.fill(0);
    releaseMaster.fill(0);
#if defined(QQS_EMBEDDED_RELEASE_MATERIAL)
    for (std::size_t index = 0; index < bundleId.size(); ++index) {
        bundleId[index] = kEmbeddedBundleId[index];
        releaseMaster[index] = static_cast<std::uint8_t>(
            kReleaseMasterShareA[index] ^
            kReleaseMasterShareB[index] ^
            kReleaseMasterShareC[index]);
    }
    return !IsAllZero(bundleId.data(), bundleId.size()) &&
        !IsAllZero(releaseMaster.data(), releaseMaster.size());
#else
    return false;
#endif
}

class AlgorithmHandle final {
public:
    ~AlgorithmHandle() {
        if (value) BCryptCloseAlgorithmProvider(value, 0);
    }
    BCRYPT_ALG_HANDLE value = nullptr;
};

class KeyHandle final {
public:
    ~KeyHandle() {
        if (value) BCryptDestroyKey(value);
    }
    BCRYPT_KEY_HANDLE value = nullptr;
};

class HashHandle final {
public:
    ~HashHandle() {
        if (value) BCryptDestroyHash(value);
    }
    BCRYPT_HASH_HANDLE value = nullptr;
};

bool Sha256(const std::uint8_t* input, std::size_t size, std::array<std::uint8_t, 32>& output) {
    if (!input || size > std::numeric_limits<ULONG>::max()) return false;
    AlgorithmHandle algorithm;
    if (!Succeeded(BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) return false;
    ULONG objectSize = 0;
    ULONG resultSize = 0;
    if (!Succeeded(BCryptGetProperty(algorithm.value, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &resultSize, 0)) || objectSize == 0) return false;
    std::vector<std::uint8_t> hashObject(objectSize);
    HashHandle hash;
    if (!Succeeded(BCryptCreateHash(algorithm.value, &hash.value, hashObject.data(), objectSize,
        nullptr, 0, 0)) ||
        !Succeeded(BCryptHashData(hash.value, const_cast<PUCHAR>(input), static_cast<ULONG>(size), 0)) ||
        !Succeeded(BCryptFinishHash(hash.value, output.data(), static_cast<ULONG>(output.size()), 0))) {
        SecureZeroMemory(hashObject.data(), hashObject.size());
        return false;
    }
    SecureZeroMemory(hashObject.data(), hashObject.size());
    return true;
}

bool HmacSha256(
    const std::uint8_t* key,
    std::size_t keySize,
    const std::uint8_t* input,
    std::size_t inputSize,
    std::array<std::uint8_t, 32>& output) {
    output.fill(0);
    if (!key || keySize == 0 || keySize > std::numeric_limits<ULONG>::max() ||
        !input || inputSize > std::numeric_limits<ULONG>::max()) {
        return false;
    }
    AlgorithmHandle algorithm;
    if (!Succeeded(BCryptOpenAlgorithmProvider(
        &algorithm.value,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        BCRYPT_ALG_HANDLE_HMAC_FLAG))) {
        return false;
    }
    ULONG objectSize = 0;
    ULONG resultSize = 0;
    if (!Succeeded(BCryptGetProperty(
        algorithm.value,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize),
        sizeof(objectSize),
        &resultSize,
        0)) || objectSize == 0) {
        return false;
    }
    std::vector<std::uint8_t> hashObject(objectSize);
    HashHandle hash;
    const bool succeeded =
        Succeeded(BCryptCreateHash(
            algorithm.value,
            &hash.value,
            hashObject.data(),
            objectSize,
            const_cast<PUCHAR>(key),
            static_cast<ULONG>(keySize),
            0)) &&
        Succeeded(BCryptHashData(
            hash.value,
            const_cast<PUCHAR>(input),
            static_cast<ULONG>(inputSize),
            0)) &&
        Succeeded(BCryptFinishHash(
            hash.value,
            output.data(),
            static_cast<ULONG>(output.size()),
            0));
    SecureZeroMemory(hashObject.data(), hashObject.size());
    if (!succeeded) output.fill(0);
    return succeeded;
}

bool EncryptOaepSha256(
    const std::uint8_t* publicWire,
    const std::uint8_t* plaintext,
    std::size_t plaintextSize,
    std::array<std::uint8_t, kCiphertextBytes>& ciphertext) {
    if (!publicWire || !plaintext || plaintextSize > std::numeric_limits<ULONG>::max()) return false;
    std::vector<std::uint8_t> keyBlob(sizeof(BCRYPT_RSAKEY_BLOB) + kPublicKeyBytes);
    auto* header = reinterpret_cast<BCRYPT_RSAKEY_BLOB*>(keyBlob.data());
    header->Magic = BCRYPT_RSAPUBLIC_MAGIC;
    header->BitLength = 2048;
    header->cbPublicExp = 3;
    header->cbModulus = 256;
    header->cbPrime1 = 0;
    header->cbPrime2 = 0;
    std::memcpy(keyBlob.data() + sizeof(BCRYPT_RSAKEY_BLOB), publicWire, kPublicKeyBytes);

    AlgorithmHandle algorithm;
    KeyHandle key;
    if (!Succeeded(BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_RSA_ALGORITHM, nullptr, 0)) ||
        !Succeeded(BCryptImportKeyPair(algorithm.value, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key.value,
            keyBlob.data(), static_cast<ULONG>(keyBlob.size()), 0))) {
        SecureZeroMemory(keyBlob.data(), keyBlob.size());
        return false;
    }
    SecureZeroMemory(keyBlob.data(), keyBlob.size());

    BCRYPT_OAEP_PADDING_INFO padding{};
    padding.pszAlgId = BCRYPT_SHA256_ALGORITHM;
    ULONG encryptedSize = 0;
    const NTSTATUS status = BCryptEncrypt(
        key.value,
        const_cast<PUCHAR>(plaintext),
        static_cast<ULONG>(plaintextSize),
        &padding,
        nullptr,
        0,
        ciphertext.data(),
        static_cast<ULONG>(ciphertext.size()),
        &encryptedSize,
        BCRYPT_PAD_OAEP);
    return Succeeded(status) && encryptedSize == ciphertext.size();
}

std::uint64_t CurrentUnixTimeMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

bool DeriveContentKey(
    const std::array<std::uint8_t, 32>& releaseMaster,
    const std::array<std::uint8_t, 32>& bundleId,
    std::array<std::uint8_t, 32>& contentKey) {
    contentKey.fill(0);
    if (IsAllZero(releaseMaster.data(), releaseMaster.size()) ||
        IsAllZero(bundleId.data(), bundleId.size())) {
        return false;
    }
    std::array<std::uint8_t, 32> pseudorandomKey{};
    const bool extracted = HmacSha256(
        bundleId.data(),
        bundleId.size(),
        releaseMaster.data(),
        releaseMaster.size(),
        pseudorandomKey);
    const bool expanded = extracted && HmacSha256(
        pseudorandomKey.data(),
        pseudorandomKey.size(),
        kHkdfExpandInfo.data(),
        kHkdfExpandInfo.size(),
        contentKey);
    SecureZeroMemory(pseudorandomKey.data(), pseudorandomKey.size());
    if (!expanded) contentKey.fill(0);
    return expanded;
}

RuntimeMaterialService::~RuntimeMaterialService() {
    Clear();
}

bool RuntimeMaterialService::ConfigureFromReleaseMaster(
    const std::array<std::uint8_t, 32>& bundleId,
    const std::array<std::uint8_t, 32>& releaseMaster,
    std::string& error) {
    if (IsAllZero(bundleId.data(), bundleId.size()) ||
        IsAllZero(releaseMaster.data(), releaseMaster.size())) {
        error = "embedded runtime release material is invalid";
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    SecureZeroMemory(bundleId_.data(), bundleId_.size());
    SecureZeroMemory(releaseMaster_.data(), releaseMaster_.size());
    bundleId_ = bundleId;
    releaseMaster_ = releaseMaster;
    embeddedRelease_ = false;
    configured_ = true;
    error.clear();
    return true;
}

bool RuntimeMaterialService::ConfigureFromEmbeddedRelease(std::string& error) {
    std::array<std::uint8_t, 32> bundleId{};
    std::array<std::uint8_t, 32> releaseMaster{};
    if (!LoadEmbeddedReleaseMaterial(bundleId, releaseMaster)) {
        SecureZeroMemory(bundleId.data(), bundleId.size());
        SecureZeroMemory(releaseMaster.data(), releaseMaster.size());
        error = "embedded runtime release material is unavailable";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SecureZeroMemory(bundleId_.data(), bundleId_.size());
        SecureZeroMemory(releaseMaster_.data(), releaseMaster_.size());
        bundleId_ = bundleId;
        embeddedRelease_ = true;
        configured_ = true;
    }
    SecureZeroMemory(bundleId.data(), bundleId.size());
    SecureZeroMemory(releaseMaster.data(), releaseMaster.size());
    error.clear();
    return true;
}

bool RuntimeMaterialService::IsConfigured() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return configured_;
}

bool RuntimeMaterialService::Issue(
    std::uint32_t authenticatedUin,
    const std::uint8_t* requestBody,
    std::size_t requestSize,
    std::uint64_t issuedAtMs,
    std::vector<std::uint8_t>& responseBody,
    std::string& error) const {
    responseBody.clear();
    if (authenticatedUin <= 10000) {
        error = "runtime material request identity or length is invalid";
        return false;
    }
    return IssueInternal(
        kProtocolVersion,
        authenticatedUin,
        false,
        requestBody,
        requestSize,
        issuedAtMs,
        responseBody,
        error);
}

bool RuntimeMaterialService::IssuePrelogin(
    const std::uint8_t* requestBody,
    std::size_t requestSize,
    std::uint64_t issuedAtMs,
    std::vector<std::uint8_t>& responseBody,
    std::string& error) const {
    return IssueInternal(
        kPreloginProtocolVersion,
        0,
        true,
        requestBody,
        requestSize,
        issuedAtMs,
        responseBody,
        error);
}

bool RuntimeMaterialService::IssueInternal(
    std::uint8_t protocolVersion,
    std::uint32_t subject,
    bool requireConfiguredBundle,
    const std::uint8_t* requestBody,
    std::size_t requestSize,
    std::uint64_t issuedAtMs,
    std::vector<std::uint8_t>& responseBody,
    std::string& error) const {
    responseBody.clear();
    if (!requestBody || requestSize != kRequestBytes) {
        error = "runtime material request identity or length is invalid";
        return false;
    }
    if (requestBody[0] != protocolVersion || requestBody[1] != kAlgorithmRsaOaepSha256 ||
        requestBody[2] != 0 || requestBody[3] != 0 || Read16BE(requestBody + 52) != kPublicKeyBytes ||
        requestBody[54] != 0x01 || requestBody[55] != 0x00 || requestBody[56] != 0x01 ||
        IsAllZero(requestBody + 57, 256)) {
        error = "runtime material request header or public key is invalid";
        return false;
    }

    std::array<std::uint8_t, 32> configuredBundleId{};
    std::array<std::uint8_t, 32> requestedBundleId{};
    std::array<std::uint8_t, 32> configuredReleaseMaster{};
    std::array<std::uint8_t, 32> derivedContentKey{};
    bool embeddedRelease = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!configured_) {
            error = "runtime material service is not configured";
            return false;
        }
        configuredBundleId = bundleId_;
        embeddedRelease = embeddedRelease_;
        if (!embeddedRelease) configuredReleaseMaster = releaseMaster_;
    }
    if (embeddedRelease) {
        std::array<std::uint8_t, 32> embeddedBundleId{};
        const bool loaded = LoadEmbeddedReleaseMaterial(embeddedBundleId, configuredReleaseMaster);
        const bool matches = loaded && embeddedBundleId == configuredBundleId;
        SecureZeroMemory(embeddedBundleId.data(), embeddedBundleId.size());
        if (!matches) {
            SecureZeroMemory(configuredBundleId.data(), configuredBundleId.size());
            SecureZeroMemory(configuredReleaseMaster.data(), configuredReleaseMaster.size());
            error = "embedded runtime release material changed or is unavailable";
            return false;
        }
    }
    std::memcpy(requestedBundleId.data(), requestBody + 4, requestedBundleId.size());
    if (IsAllZero(requestedBundleId.data(), requestedBundleId.size())) {
        SecureZeroMemory(configuredBundleId.data(), configuredBundleId.size());
        SecureZeroMemory(requestedBundleId.data(), requestedBundleId.size());
        SecureZeroMemory(configuredReleaseMaster.data(), configuredReleaseMaster.size());
        error = "runtime material bundle id is invalid";
        return false;
    }
    if (requireConfiguredBundle && requestedBundleId != configuredBundleId) {
        SecureZeroMemory(configuredBundleId.data(), configuredBundleId.size());
        SecureZeroMemory(requestedBundleId.data(), requestedBundleId.size());
        SecureZeroMemory(configuredReleaseMaster.data(), configuredReleaseMaster.size());
        error = "runtime material bundle id is not the configured release";
        return false;
    }

    const std::uint64_t issuedAt = issuedAtMs == 0 ? CurrentUnixTimeMs() : issuedAtMs;
    if (issuedAt == 0 || issuedAt > std::numeric_limits<std::uint64_t>::max() - kMaterialTtlMs) {
        SecureZeroMemory(configuredBundleId.data(), configuredBundleId.size());
        SecureZeroMemory(requestedBundleId.data(), requestedBundleId.size());
        SecureZeroMemory(configuredReleaseMaster.data(), configuredReleaseMaster.size());
        error = "runtime material timestamp is invalid";
        return false;
    }
    const std::uint64_t expiresAt = issuedAt + kMaterialTtlMs;
    std::array<std::uint8_t, kTokenIdBytes> tokenId{};
    std::array<std::uint8_t, kFingerprintBytes> fingerprint{};
    std::array<std::uint8_t, kPlaintextBytes> plaintext{};
    std::array<std::uint8_t, kCiphertextBytes> ciphertext{};
    bool success = false;
    do {
        if (!Succeeded(BCryptGenRandom(nullptr, tokenId.data(), static_cast<ULONG>(tokenId.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG)) || IsAllZero(tokenId.data(), tokenId.size())) {
            error = "runtime material token generation failed";
            break;
        }
        if (!Sha256(requestBody + 54, kPublicKeyBytes, fingerprint)) {
            error = "runtime material install fingerprint failed";
            break;
        }
        if (!DeriveContentKey(configuredReleaseMaster, requestedBundleId, derivedContentKey)) {
            error = "runtime material content key derivation failed";
            break;
        }
        const auto& plaintextMagic = protocolVersion == kPreloginProtocolVersion
            ? kPreloginPlaintextMagic
            : kPlaintextMagic;
        std::memcpy(plaintext.data(), plaintextMagic.data(), plaintextMagic.size());
        plaintext[8] = protocolVersion;
        plaintext[9] = kAlgorithmRsaOaepSha256;
        Write32BE(plaintext.data() + 12, subject);
        std::memcpy(plaintext.data() + 16, requestedBundleId.data(), requestedBundleId.size());
        std::memcpy(plaintext.data() + 48, tokenId.data(), tokenId.size());
        std::memcpy(plaintext.data() + 64, requestBody + 36, kClientNonceBytes);
        std::memcpy(plaintext.data() + 80, fingerprint.data(), fingerprint.size());
        Write64BE(plaintext.data() + 112, issuedAt);
        Write64BE(plaintext.data() + 120, expiresAt);
        std::memcpy(plaintext.data() + 128, derivedContentKey.data(), derivedContentKey.size());
        if (!EncryptOaepSha256(requestBody + 54, plaintext.data(), plaintext.size(), ciphertext)) {
            error = "runtime material RSA-OAEP wrap failed";
            break;
        }

        responseBody.assign(kResponseBytes, 0);
        responseBody[0] = protocolVersion;
        responseBody[1] = kAlgorithmRsaOaepSha256;
        std::memcpy(responseBody.data() + 4, requestedBundleId.data(), requestedBundleId.size());
        std::memcpy(responseBody.data() + 36, tokenId.data(), tokenId.size());
        std::memcpy(responseBody.data() + 52, requestBody + 36, kClientNonceBytes);
        Write64BE(responseBody.data() + 68, issuedAt);
        Write64BE(responseBody.data() + 76, expiresAt);
        Write16BE(responseBody.data() + 84, static_cast<std::uint16_t>(ciphertext.size()));
        std::memcpy(responseBody.data() + 86, ciphertext.data(), ciphertext.size());
        error.clear();
        success = true;
    } while (false);

    SecureZeroMemory(configuredBundleId.data(), configuredBundleId.size());
    SecureZeroMemory(requestedBundleId.data(), requestedBundleId.size());
    SecureZeroMemory(configuredReleaseMaster.data(), configuredReleaseMaster.size());
    SecureZeroMemory(derivedContentKey.data(), derivedContentKey.size());
    SecureZeroMemory(tokenId.data(), tokenId.size());
    SecureZeroMemory(fingerprint.data(), fingerprint.size());
    SecureZeroMemory(plaintext.data(), plaintext.size());
    SecureZeroMemory(ciphertext.data(), ciphertext.size());
    if (!success) responseBody.clear();
    return success;
}

void RuntimeMaterialService::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    SecureZeroMemory(bundleId_.data(), bundleId_.size());
    SecureZeroMemory(releaseMaster_.data(), releaseMaster_.size());
    embeddedRelease_ = false;
    configured_ = false;
}

RuntimeMaterialService& GetRuntimeMaterialService() {
    static RuntimeMaterialService service;
    return service;
}

} // namespace launcher_runtime
