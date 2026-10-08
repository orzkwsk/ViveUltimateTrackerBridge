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
    try {
        if (!backend_.beginPairing(slot)) {
            const auto reason = backend_.lastError();
            (void)finish(PairingState::Failed, reason);
            return false;
        }
    } catch (...) {
        (void)finish(PairingState::Failed, "exception while enabling pairing");
        throw;
    }
    result_.state = PairingState::Pairing;
    return true;
}
void PairingController::observe(const BackendEvent& event) {
    if (result_.state != PairingState::Pairing || result_.paired ||
        event.kind != BackendEvent::Kind::DevicePaired || event.transportGeneration != generation_ ||
        event.observedMonotonicNs < startedNs_ || event.observedMonotonicNs >= deadlineNs_ ||
        event.device.slot < 0 || event.device.slot > 4 || (slot_ != -1 && slot_ != event.device.slot)) return;
    result_.paired = event.device;
}
void PairingController::tick(std::int64_t now) {
    if (result_.state == PairingState::CleanupFailed && backend_.running() &&
        backend_.transportGeneration() != generation_ && !backend_.pairingActive()) {
        // Successful Backend::start on a new handle has resolved pending
        // disable. Keep the old operation failed, but report its cleanup truth.
        result_.cleanupConfirmed = true;
        result_.cleanupError.clear();
        result_.reason += "; pending disable confirmed after transport reopen";
        result_.state = PairingState::Failed;
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
    result_.cleanupConfirmed = !backend_.pairingActive();
    // Two bounded attempts, matching the previous standalone tool. Backend::stop
    // retains its final best-effort attempt before closing the transport.
    if (!result_.cleanupConfirmed) result_.cleanupConfirmed = backend_.endPairingWithRetry();
    if (!result_.cleanupConfirmed) {
        if (result_.cleanupError.empty()) result_.cleanupError = backend_.lastError();
        result_.state = PairingState::CleanupFailed;
    } else {
        result_.cleanupError.clear(); result_.state = state;
    }
    return result_.cleanupConfirmed;
}
bool PairingController::cancel(const std::string& reason) {
    if (!active() && !backend_.pairingActive()) return result_.cleanupConfirmed;
    result_.state = PairingState::Cancelling;
    return finish(PairingState::Cancelling, reason);
}
void PairingController::transportLost(const std::string& reason) {
    if (active() || backend_.pairingActive()) (void)finish(PairingState::Failed, reason);
}
bool PairingController::shutdown() {
    if (shutdownAttempted_) return result_.cleanupConfirmed;
    shutdownAttempted_ = true;
    return cancel("setup/tool shutdown");
}
}
