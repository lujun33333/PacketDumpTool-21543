#include "LauncherRuntimeAdmission.h"

#include <iostream>
#include <utility>

namespace {

bool Require(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << message << "\n";
    return false;
}

} // namespace

int main() {
    using launcher_runtime::AdmissionDecision;
    using launcher_runtime::PreloginAdmission;
    using launcher_runtime::RequestGateDecision;

    if (!Require(
            launcher_runtime::EvaluateRequestGate(0, 0, true, true) ==
                RequestGateDecision::AllowPreloginV2,
            "fresh anonymous v2 was rejected") ||
        !Require(
            launcher_runtime::EvaluateRequestGate(2, 0, true, true) ==
                RequestGateDecision::RejectPreloginV2,
            "repeated v2 was accepted") ||
        !Require(
            launcher_runtime::EvaluateRequestGate(0, 10001, true, true) ==
                RequestGateDecision::RejectPreloginV2,
            "authenticated v2 was accepted") ||
        !Require(
            launcher_runtime::EvaluateRequestGate(0, 0, true, false) ==
                RequestGateDecision::RejectPreloginV2,
            "malformed v2 was accepted") ||
        !Require(
            launcher_runtime::EvaluateRequestGate(0, 10001, false, false) ==
                RequestGateDecision::AllowAuthenticatedV1,
            "fresh authenticated v1 was rejected") ||
        !Require(
            launcher_runtime::EvaluateRequestGate(2, 10001, false, false) ==
                RequestGateDecision::IgnoreLegacyUnauthenticated,
            "login reopened an already-consumed material connection") ||
        !Require(
            launcher_runtime::EvaluateRequestGate(0, 0, false, false) ==
                RequestGateDecision::IgnoreLegacyUnauthenticated,
            "legacy unauthenticated behavior changed")) {
        return 1;
    }

    PreloginAdmission limiter({ 2, 100, 2, 8, 1000 });
    PreloginAdmission::Lease first;
    PreloginAdmission::Lease second;
    PreloginAdmission::Lease third;
    if (!Require(limiter.TryAcquire("203.0.113.1", 1000, first) == AdmissionDecision::Allowed,
            "first token was rejected") ||
        !Require(limiter.TryAcquire("203.0.113.1", 1000, second) == AdmissionDecision::Allowed,
            "capacity boundary rejected") ||
        !Require(limiter.TryAcquire("203.0.113.2", 1000, third) == AdmissionDecision::Busy,
            "global concurrency boundary was not enforced")) {
        return 2;
    }
    if (!Require(limiter.TryAcquire("203.0.113.2", 1000, third) == AdmissionDecision::Busy,
            "second busy attempt was not classified") ||
        !Require(limiter.TryAcquire("203.0.113.2", 1000, third) == AdmissionDecision::RateLimited,
            "busy attempts did not spend IP tokens")) {
        return 2;
    }
    second = {};
    if (!Require(limiter.TryAcquire("203.0.113.1", 1099, third) == AdmissionDecision::RateLimited,
            "token refilled too early") ||
        !Require(limiter.TryAcquire("203.0.113.1", 1100, third) == AdmissionDecision::Allowed,
            "token did not refill on boundary")) {
        return 3;
    }

    PreloginAdmission natLimiter({ 32, 1000, 64, 64, 10000 });
    for (std::size_t index = 0; index < 32; ++index) {
        PreloginAdmission::Lease lease;
        if (!Require(natLimiter.TryAcquire("198.51.100.10", 5000, lease) == AdmissionDecision::Allowed,
                "reasonable NAT burst was rejected")) {
            return 4;
        }
    }
    PreloginAdmission::Lease natOverflow;
    if (!Require(natLimiter.TryAcquire("198.51.100.10", 5000, natOverflow) == AdmissionDecision::RateLimited,
            "NAT burst limit was not enforced")) {
        return 4;
    }

    PreloginAdmission trackedLimiter({ 1, 100, 2, 1, 1000 });
    PreloginAdmission::Lease tracked;
    if (!Require(trackedLimiter.TryAcquire("192.0.2.1", 1000, tracked) == AdmissionDecision::Allowed,
            "tracked-IP fixture setup failed")) {
        return 5;
    }
    tracked = {};
    if (!Require(trackedLimiter.TryAcquire("192.0.2.2", 1500, tracked) == AdmissionDecision::RateLimited,
            "tracked-IP cap was not enforced") ||
        !Require(trackedLimiter.TryAcquire("192.0.2.2", 2000, tracked) == AdmissionDecision::Allowed,
            "expired tracked IP was not pruned")) {
        return 5;
    }

    launcher_runtime::IntervalGate logGate(5000);
    if (!Require(logGate.ShouldEmit(100), "first log was suppressed") ||
        !Require(!logGate.ShouldEmit(5099), "log interval opened too early") ||
        !Require(logGate.ShouldEmit(5100), "log interval did not open on boundary")) {
        return 6;
    }

    std::cout << "LAUNCHER_RUNTIME_ADMISSION_TEST_OK\n";
    return 0;
}
