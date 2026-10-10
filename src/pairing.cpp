#include "vut/pairing.hpp"
#include <limits>
#include <stdexcept>

namespace vut::setup {
const char* pairingStateName(PairingState state) noexcept {
    switch (state) {
    case PairingState::Idle: return "Idle";
    case PairingState::Enabling: return "Enabling";
    case PairingState::Pairing: return "Pairing";
    case PairingState::Paired: return "Paired";
    case PairingState::Cancelling: return "Cancelling";
    case PairingState::TimedOut: return "TimedOut";
    case PairingState::Failed: return "Failed";
    case PairingState::CleanupFailed: return "CleanupFailed";
    }
    return "Failed";
}
PairingController::~PairingController() {
    try { (void)shutdown(); } catch (...) { /* Backend destructor has a final cleanup path. */ }
}
bool PairingController::active() const noexcept {
    return result_.state == PairingState::Enabling || result_.state == PairingState::Pairing;
}
bool PairingController::begin(std::int64_t now, int slot, std::chrono::seconds timeout) {
    if (active() || backend_.pairingActive())
        throw std::invalid_argument("pairing is active or cleanup is unresolved; cancel/recover first");
    if (slot < -1 || slot > 4 || timeout.count() <= 0 || timeout.count() > 3600 || now < 0 ||
        now > std::numeric_limits<std::int64_t>::max() - timeout.count()*1000000000LL)
        throw std::invalid_argument("invalid pairing selector/time/timeout");
    shutdownAttempted_ = false;
    result_ = {}; result_.state = PairingState::Enabling;
    slot_ = slot; generation_ = backend_.transportGeneration();
    startedNs_ = now; deadlineNs_ = now + timeout.count()*1000000000LL;
    result_.cleanupConfirmed = false;
    result_.cleanup.state = CleanupExchangeState::Unknown;
    try {
        if (!backend_.beginPairing(slot)) {
            result_.enableExchange = backend_.pairingEnableExchange();
            const auto reason = backend_.lastError();
            (void)finish(PairingState::Failed, reason);
            return false;
        }
    } catch (...) {
        result_.enableExchange = backend_.pairingEnableExchange();
        (void)finish(PairingState::Failed, "exception while enabling pairing");
        throw;
    }
    result_.enableExchange = backend_.pairingEnableExchange();
    result_.state = PairingState::Pairing;
    return true;
}
void PairingController::observe(const BackendEvent& event) {
    if (result_.state != PairingState::Pairing || result_.paired ||
        event.kind != BackendEvent::Kind::DevicePaired || event.transportGeneration != generation_ ||
        event.observedMonotonicNs < startedNs_ || event.observedMonotonicNs >= deadlineNs_ ||
        event.device.slot < 0 || event.device.slot > 4 || (slot_ != -1 && slot_ != event.device.slot)) return;
    result_.paired = event.device;
    result_.associationEffect = SemanticEffect::ConfirmedByIndependentEvidence;
}
void PairingController::tick(std::int64_t now) {
    if (result_.state == PairingState::CleanupFailed && backend_.running() &&
        backend_.transportGeneration() != generation_ && !backend_.pairingActive()) {
        // A reopened handle may have matched a NEW exchange. It is not an old
        // generation cleanup exchange, and never proves vendor cleanup effect.
        updateCleanupEvidence();
    }
    if (!active()) return;
    if (!backend_.running() || backend_.transportGeneration() != generation_) {
        transportLost("transport session lost or changed"); return;
    }
    if (result_.paired) (void)finish(PairingState::Paired, "DevicePaired received");
    else if (now >= deadlineNs_) (void)finish(PairingState::TimedOut, "pairing timed out");
}
bool PairingController::finish(PairingState state, const std::string& reason) {
    result_.reason = reason;
    // Two bounded attempts, matching the previous standalone tool. Backend::stop
    // retains its final best-effort attempt before closing the transport.
    if (backend_.pairingActive() && backend_.transportGeneration() == generation_)
        (void)backend_.endPairingWithRetry();
    updateCleanupEvidence();
    if (!result_.cleanupConfirmed) {
        if (result_.cleanupError.empty()) result_.cleanupError = backend_.lastError();
        result_.state = PairingState::CleanupFailed;
    } else {
        result_.cleanupError.clear(); result_.state = state;
    }
    return result_.cleanupConfirmed;
}
void PairingController::updateCleanupEvidence() {
    result_.cleanup = backend_.pairingCleanupEvidence();
    const bool noWrite = result_.enableExchange.write == FeatureWriteState::NotAttempted ||
                         result_.enableExchange.write == FeatureWriteState::Failed;
    if (noWrite && !backend_.pairingActive()) {
        result_.cleanup.state = CleanupExchangeState::NotNeeded;
    } else if (result_.cleanup.enableGeneration != generation_ ||
               (result_.cleanup.state == CleanupExchangeState::Matched &&
                result_.cleanup.disable.transportGeneration != generation_)) {
        result_.cleanup.state = CleanupExchangeState::Unknown;
    }
    result_.cleanup.effect = SemanticEffect::Unverified;
    result_.cleanupConfirmed = result_.cleanup.state == CleanupExchangeState::NotNeeded ||
                               result_.cleanup.state == CleanupExchangeState::Matched;
    if (!result_.cleanupConfirmed) {
        result_.cleanupError = backend_.transportGeneration() != generation_
            ? "transport generation changed; old cleanup exchange unknown"
            : backend_.lastError();
        if (result_.cleanupError.empty()) result_.cleanupError = "cleanup exchange unresolved";
    }
}
bool PairingController::cancel(const std::string& reason) {
    // A finished operation does not own a later externally-started enable.
    if (!active() && result_.cleanupConfirmed) return true;
    if (!active() && !backend_.pairingActive()) return result_.cleanupConfirmed;
    result_.state = PairingState::Cancelling;
    return finish(PairingState::Cancelling, reason);
}
void PairingController::transportLost(const std::string& reason) {
    if (active() || !result_.cleanupConfirmed) (void)finish(PairingState::Failed, reason);
}
bool PairingController::shutdown() {
    if (shutdownAttempted_) return result_.cleanupConfirmed;
    shutdownAttempted_ = true;
    return cancel("setup/tool shutdown");
}
}
