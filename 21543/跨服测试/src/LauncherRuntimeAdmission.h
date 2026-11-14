#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace launcher_runtime {

enum class AdmissionDecision {
    Allowed,
    RateLimited,
    Busy
};

enum class RequestGateDecision {
    AllowAuthenticatedV1,
    AllowPreloginV2,
    IgnoreLegacyUnauthenticated,
    RejectPreloginV2
};

RequestGateDecision EvaluateRequestGate(
    std::uint8_t materialState,
    std::uint32_t authenticatedUin,
    bool claimsPreloginV2,
    bool validPreloginShape);

class PreloginAdmission final {
public:
    struct Config {
        std::size_t perIpCapacity = 32;
        std::uint64_t refillIntervalMs = 125;
        std::size_t globalMaxConcurrent = 8;
        std::size_t maxTrackedIps = 8192;
        std::uint64_t entryTtlMs = 600000;
    };

    class Lease final {
    public:
        Lease() = default;
        ~Lease();
        Lease(const Lease&) = delete;
        Lease& operator=(const Lease&) = delete;
        Lease(Lease&& other) noexcept;
        Lease& operator=(Lease&& other) noexcept;
        explicit operator bool() const noexcept;

    private:
        friend class PreloginAdmission;
        explicit Lease(PreloginAdmission* owner) noexcept;
        void Reset() noexcept;
        PreloginAdmission* owner_ = nullptr;
    };

    explicit PreloginAdmission(Config config = {});
    AdmissionDecision TryAcquire(const std::string& ip, std::uint64_t nowMs, Lease& lease);

private:
    struct Bucket {
        std::size_t tokens = 0;
        std::uint64_t lastRefillMs = 0;
        std::uint64_t lastSeenMs = 0;
    };

    void Release() noexcept;
    void PruneExpired(std::uint64_t nowMs);

    Config config_;
    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
    std::size_t active_ = 0;
};

class IntervalGate final {
public:
    explicit IntervalGate(std::uint64_t intervalMs);
    bool ShouldEmit(std::uint64_t nowMs);

private:
    const std::uint64_t intervalMs_;
    std::mutex mutex_;
    bool emitted_ = false;
    std::uint64_t nextEmitMs_ = 0;
};

std::uint64_t MonotonicNowMs();

} // namespace launcher_runtime
