#pragma once
#include "vut/pairing.hpp"
#include "vut/setup.hpp"

namespace vut::setup {
// Bring-up session boundary, reusable by a future service-owned control API.
// Backend is supplied by the host; this model never starts or kills a process.
class SetupSession {
public:
    explicit SetupSession(Backend&, IdentifyPolicy = {});
    ~SetupSession();
    SetupSession(const SetupSession&) = delete;
    SetupSession& operator=(const SetupSession&) = delete;
    void connected();
    bool pollOnce(std::int64_t nowNs);
    bool shutdown();
    [[nodiscard]] Devices& devices() noexcept { return devices_; }
    [[nodiscard]] PairingController& pairing() noexcept { return pairing_; }
private:
    Backend& backend_;
    Devices devices_;
    PairingController pairing_;
};
}
