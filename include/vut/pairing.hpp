#pragma once

#include "vut/backend.hpp"
#include <chrono>
#include <optional>

namespace vut::setup {
enum class PairingState { Idle, Enabling, Pairing, Paired, Cancelling, TimedOut, Failed, CleanupFailed };
const char* pairingStateName(PairingState) noexcept;

struct PairingResult {
    PairingState state = PairingState::Idle;
    std::optional<DeviceId> paired;
    std::string reason, cleanupError;
    // Compatibility projection: a same-generation disable exchange matched,
    // or no accepted/possibly-written enable needed cleanup. Never vendor effect.
    bool cleanupConfirmed = true;
    FeatureExchangeEvidence enableExchange;
    PairingCleanupEvidence cleanup;
    // Only a bounded DevicePaired event can confirm association. No Ready claim.
    SemanticEffect associationEffect = SemanticEffect::Unverified;
};

// Single-threaded control boundary. observe() only records an event; tick()
// performs cleanup after Backend::pollOnce returns, without callback reentrancy.
// The backend must outlive this object. Hardware polling belongs to its caller.
class PairingController {
public:
    explicit PairingController(Backend& backend) : backend_(backend) {}
    ~PairingController();
    PairingController(const PairingController&) = delete;
    PairingController& operator=(const PairingController&) = delete;
    bool begin(std::int64_t nowNs, int slot = -1,
               std::chrono::seconds timeout = std::chrono::seconds(60));
    void observe(const BackendEvent&);
    void tick(std::int64_t nowNs);
    bool cancel(const std::string& reason = "user cancelled");
    void transportLost(const std::string& reason);
    bool shutdown();
    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] const PairingResult& result() const noexcept { return result_; }
private:
    bool finish(PairingState state, const std::string& reason);
    void updateCleanupEvidence();
    Backend& backend_;
    PairingResult result_;
    int slot_ = -1;
    std::uint64_t generation_ = 0;
    std::int64_t startedNs_ = 0, deadlineNs_ = 0;
    bool shutdownAttempted_ = false;
};
}
