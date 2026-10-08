#pragma once

#include "vut/types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vut {

namespace detail {
class BackendTransport;
}

struct BackendConfig {
    std::string wifiCountry = "JP";
    int readTimeoutMs = 50;
    bool configureTrackingOnPair = true;
    // Explicit probe/HIL opt-in; normal service never enables active SLAM queries.
    bool diagnosticSlamQueries = false;
    // Setup tools use exclusive ownership so an active service/Hub fails closed.
    // Normal service/probe behavior remains shared for compatibility.
    bool exclusiveHidAccess = false;
};

class Backend {
public:
    using EventCallback = std::function<void(const BackendEvent&)>;

    Backend();
    // Injectable transport boundary for hardware-free command-sequence tests.
    explicit Backend(std::unique_ptr<detail::BackendTransport> transport);
    ~Backend();
    Backend(Backend&&) noexcept;
    Backend& operator=(Backend&&) noexcept;
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    bool start(const BackendConfig& config = {});
    void stop();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

    // Explicit setup operation only. start() never calls this. Slot is 0..4,
    // or -1 for all selectors, matching buildRfBehaviorPayload semantics.
    bool beginPairing(int slot = -1);
    bool endPairing();
    // Shared bounded cleanup for setup controllers and transport-loss close.
    bool endPairingWithRetry(unsigned attempts = 2);
    [[nodiscard]] bool pairingActive() const noexcept;

    bool pollOnce();
    [[nodiscard]] std::uint64_t transportGeneration() const noexcept;
    void setEventCallback(EventCallback callback);
    [[nodiscard]] std::vector<TrackerState> snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vut
