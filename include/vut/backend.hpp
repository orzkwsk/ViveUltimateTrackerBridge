#pragma once

#include "vut/types.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace vut {

enum class UnpairRecoveryMode : std::uint8_t {
    SlotSpecificRestartRf,
    None,
    ColdStartRestartRf,
    DisablePws,
    DisablePwsThenRestartRf,
    DisablePairThenRestartRf,
    DisablePairAndPwsThenRestartRf,
    RfRebootApp,
    RfRebootAppThenRestartRf,
};

enum class DebugRfBehavior : std::uint8_t {
    PairDevice = 0x00,
    RxPowerSaving = 0x01,
    RestartRf = 0x02,
};

struct DebugCommandResult {
    bool sent = false;
    bool responseMatched = false;
    std::uint8_t command = 0;
    std::vector<std::uint8_t> payload;
    std::vector<std::uint8_t> response;
    std::string error;
};

struct BackendConfig {
    std::string wifiCountry = "JP";
    int readTimeoutMs = 50;
    bool configureTrackingOnPair = true;

    // Experimental reconnect policy. The default preserves the behavior of
    // experiment/vive-auto-reconnect; DebugConsole overrides this to None so
    // a captured stuck state can be stimulated manually.
    UnpairRecoveryMode unpairRecoveryMode = UnpairRecoveryMode::SlotSpecificRestartRf;
    int debugInterCommandDelayMs = 250;
};

class Backend {
public:
    using EventCallback = std::function<void(const BackendEvent&)>;

    Backend();
    ~Backend();
    Backend(Backend&&) noexcept;
    Backend& operator=(Backend&&) noexcept;
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    bool start(const BackendConfig& config = {});
    void stop();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

    bool pollOnce();
    [[nodiscard]] std::uint64_t transportGeneration() const noexcept;
    void setEventCallback(EventCallback callback);
    [[nodiscard]] std::vector<TrackerState> snapshot() const;

    // Experimental/debug-only direct dongle control. These APIs intentionally
    // stay in the vendor backend and must not leak into MonakaProtocol.
    DebugCommandResult debugSendRaw(std::uint8_t command, std::span<const std::uint8_t> payload);
    DebugCommandResult debugSendRfBehavior(DebugRfBehavior behavior, bool enabled, int slot);
    DebugCommandResult debugQueryRfStatus();
    DebugCommandResult debugRfRebootApp();

    void setUnpairRecoveryMode(UnpairRecoveryMode mode, int interCommandDelayMs = 250);
    [[nodiscard]] UnpairRecoveryMode unpairRecoveryMode() const noexcept;
    [[nodiscard]] int debugInterCommandDelayMs() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vut
