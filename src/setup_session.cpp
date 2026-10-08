#include "vut/setup_session.hpp"

namespace vut::setup {
SetupSession::SetupSession(Backend& backend, IdentifyPolicy policy)
    : backend_(backend), devices_(policy), pairing_(backend) {
    backend_.setEventCallback([this](const BackendEvent& event) {
        devices_.observe(event); // registry first, cleanup after poll returns
        pairing_.observe(event);
    });
}
SetupSession::~SetupSession() {
    try { (void)shutdown(); } catch (...) { /* Backend owns final close cleanup. */ }
    backend_.setEventCallback({});
}
void SetupSession::connected() { devices_.beginSession(backend_.transportGeneration()); }
bool SetupSession::pollOnce(std::int64_t now) {
    if (!backend_.pollOnce()) {
        pairing_.transportLost(backend_.lastError());
        devices_.endSession();
        return false;
    }
    pairing_.tick(now);
    return true;
}
bool SetupSession::shutdown() { return pairing_.shutdown(); }
}
