#include "LauncherRuntimeAdmission.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <utility>

namespace launcher_runtime {
namespace {

std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right) {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

} // namespace

RequestGateDecision EvaluateRequestGate(
    std::uint8_t materialState,
    std::uint32_t authenticatedUin,
    bool claimsPreloginV2,
    bool validPreloginShape) {
    if (claimsPreloginV2) {
        if (!validPreloginShape || materialState != 0 || authenticatedUin != 0) {
            return RequestGateDecision::RejectPreloginV2;
        }
        return RequestGateDecision::AllowPreloginV2;
    }
    if (materialState != 0 || authenticatedUin <= 10000) {
        return RequestGateDecision::IgnoreLegacyUnauthenticated;
    }
    return RequestGateDecision::AllowAuthenticatedV1;
}

PreloginAdmission::Lease::Lease(PreloginAdmission* owner) noexcept
    : owner_(owner) {}

PreloginAdmission::Lease::~Lease() {
    Reset();
}

PreloginAdmission::Lease::Lease(Lease&& other) noexcept
    : owner_(std::exchange(other.owner_, nullptr)) {}

PreloginAdmission::Lease& PreloginAdmission::Lease::operator=(Lease&& other) noexcept {
    if (this != &other) {
        Reset();
        owner_ = std::exchange(other.owner_, nullptr);
    }
    return *this;
}

PreloginAdmission::Lease::operator bool() const noexcept {
    return owner_ != nullptr;
}

void PreloginAdmission::Lease::Reset() noexcept {
    if (!owner_) return;
    auto* owner = std::exchange(owner_, nullptr);
    owner->Release();
}

PreloginAdmission::PreloginAdmission(Config config)
    : config_(config) {
    config_.perIpCapacity = std::max<std::size_t>(1, config_.perIpCapacity);
    config_.refillIntervalMs = std::max<std::uint64_t>(1, config_.refillIntervalMs);
    config_.globalMaxConcurrent = std::max<std::size_t>(1, config_.globalMaxConcurrent);
    config_.maxTrackedIps = std::max<std::size_t>(1, config_.maxTrackedIps);
    config_.entryTtlMs = std::max<std::uint64_t>(1, config_.entryTtlMs);
}

AdmissionDecision PreloginAdmission::TryAcquire(
    const std::string& ip,
    std::uint64_t nowMs,
    Lease& lease) {
    lease.Reset();
    if (ip.empty()) return AdmissionDecision::RateLimited;

    std::lock_guard<std::mutex> lock(mutex_);
    auto iterator = buckets_.find(ip);
    if (iterator == buckets_.end()) {
        if (buckets_.size() >= config_.maxTrackedIps) {
            PruneExpired(nowMs);
            if (buckets_.size() >= config_.maxTrackedIps) {
                return AdmissionDecision::RateLimited;
            }
        }
        Bucket bucket;
        bucket.tokens = config_.perIpCapacity;
        bucket.lastRefillMs = nowMs;
        bucket.lastSeenMs = nowMs;
        iterator = buckets_.emplace(ip, bucket).first;
    }

    auto& bucket = iterator->second;
    if (nowMs >= bucket.lastRefillMs) {
        const std::uint64_t elapsed = nowMs - bucket.lastRefillMs;
        const std::uint64_t refillCount = elapsed / config_.refillIntervalMs;
        if (refillCount > 0) {
            const std::size_t boundedRefill = refillCount > config_.perIpCapacity
                ? config_.perIpCapacity
                : static_cast<std::size_t>(refillCount);
            const std::size_t available = config_.perIpCapacity - bucket.tokens;
            bucket.tokens += std::min(available, boundedRefill);
            bucket.lastRefillMs = SaturatingAdd(
                bucket.lastRefillMs,
                refillCount * config_.refillIntervalMs);
        }
    }
    bucket.lastSeenMs = nowMs;

    if (bucket.tokens == 0) return AdmissionDecision::RateLimited;
    // Busy attempts also spend an IP token so a saturated global pool cannot
    // be hammered by unlimited reconnect/retry traffic from one address.
    --bucket.tokens;
    if (active_ >= config_.globalMaxConcurrent) return AdmissionDecision::Busy;

    ++active_;
    lease = Lease(this);
    return AdmissionDecision::Allowed;
}

void PreloginAdmission::Release() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ > 0) --active_;
}

void PreloginAdmission::PruneExpired(std::uint64_t nowMs) {
    for (auto iterator = buckets_.begin(); iterator != buckets_.end();) {
        const bool expired = nowMs >= iterator->second.lastSeenMs &&
            nowMs - iterator->second.lastSeenMs >= config_.entryTtlMs;
        if (expired) iterator = buckets_.erase(iterator);
        else ++iterator;
    }
}

IntervalGate::IntervalGate(std::uint64_t intervalMs)
    : intervalMs_(std::max<std::uint64_t>(1, intervalMs)) {}

bool IntervalGate::ShouldEmit(std::uint64_t nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!emitted_ || nowMs >= nextEmitMs_) {
        emitted_ = true;
        nextEmitMs_ = SaturatingAdd(nowMs, intervalMs_);
        return true;
    }
    return false;
}

std::uint64_t MonotonicNowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace launcher_runtime
