#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace launcher_runtime {

inline constexpr std::uint8_t kProtocolVersion = 1;
inline constexpr std::uint8_t kPreloginProtocolVersion = 2;
inline constexpr std::uint8_t kAlgorithmRsaOaepSha256 = 1;
inline constexpr std::size_t kPublicKeyBytes = 259;
inline constexpr std::size_t kRequestBytes = 313;
inline constexpr std::size_t kResponseBytes = 342;
inline constexpr std::size_t kPlaintextBytes = 160;
inline constexpr std::uint64_t kMaterialTtlMs = 30000;

bool DeriveContentKey(
    const std::array<std::uint8_t, 32>& releaseMaster,
    const std::array<std::uint8_t, 32>& bundleId,
    std::array<std::uint8_t, 32>& contentKey);

class RuntimeMaterialService final {
public:
    RuntimeMaterialService() = default;
    ~RuntimeMaterialService();
    RuntimeMaterialService(const RuntimeMaterialService&) = delete;
    RuntimeMaterialService& operator=(const RuntimeMaterialService&) = delete;

    bool ConfigureFromReleaseMaster(
        const std::array<std::uint8_t, 32>& bundleId,
        const std::array<std::uint8_t, 32>& releaseMaster,
        std::string& error);

    bool ConfigureFromEmbeddedRelease(std::string& error);
    bool IsConfigured() const;

    bool Issue(
        std::uint32_t authenticatedUin,
        const std::uint8_t* requestBody,
        std::size_t requestSize,
        std::uint64_t issuedAtMs,
        std::vector<std::uint8_t>& responseBody,
        std::string& error) const;

    // Intentional preload tradeoff: this API is anonymous. OAEP protects the
    // response in transit to the caller-supplied key; it does not authenticate
    // an installation. The configured-bundle allowlist and admission limiter
    // reduce exposure but do not make the content key secret from clients.
    bool IssuePrelogin(
        const std::uint8_t* requestBody,
        std::size_t requestSize,
        std::uint64_t issuedAtMs,
        std::vector<std::uint8_t>& responseBody,
        std::string& error) const;

    void Clear();

private:
    bool IssueInternal(
        std::uint8_t protocolVersion,
        std::uint32_t subject,
        bool requireConfiguredBundle,
        const std::uint8_t* requestBody,
        std::size_t requestSize,
        std::uint64_t issuedAtMs,
        std::vector<std::uint8_t>& responseBody,
        std::string& error) const;

    mutable std::mutex mutex_;
    std::array<std::uint8_t, 32> bundleId_{};
    std::array<std::uint8_t, 32> releaseMaster_{};
    bool embeddedRelease_ = false;
    bool configured_ = false;
};

RuntimeMaterialService& GetRuntimeMaterialService();

} // namespace launcher_runtime
