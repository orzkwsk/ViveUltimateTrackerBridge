#pragma once

#include "vut/types.hpp"
#include "vut/command_exchange.hpp"
#include "vut/connection_epoch.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vut {

namespace detail {
class BackendTransport;
}

enum class AutomaticVendorActionPolicy { Legacy, ObserveOnly };

struct BackendConfig {
    std::string wifiCountry = "JP";
    int readTimeoutMs = 50;
    bool configureTrackingOnPair = true;
    // Explicit probe/HIL opt-in; normal service never enables active SLAM queries.
    bool diagnosticSlamQueries = false;
    // Setup tools use exclusive ownership so an active service/Hub fails closed.
    // Normal service/probe behavior remains shared for compatibility.
    bool exclusiveHidAccess = false;
    // ObserveOnly overrides both legacy configure and diagnostic-query flags.
    // Explicit begin/endPairing remain usable. Pending cleanup blocks passive
    // reopen; ObserveOnly start/stop/read loss never replay a vendor write.
    AutomaticVendorActionPolicy automaticVendorActionPolicy = AutomaticVendorActionPolicy::Legacy;
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
    void stop() noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

    // Bool success means a well-formed host feature exchange matched (or an
    // idempotent no-op), never pairing/disable vendor semantic confirmation.
    // Explicit setup operation only. start() never calls this. Slot is 0..4,
    // or -1 for all selectors, matching buildRfBehaviorPayload semantics.
    bool beginPairing(int slot = -1);
    bool endPairing();
    // Shared bounded cleanup for setup controllers and transport-loss close.
    bool endPairingWithRetry(unsigned attempts = 2);
    // Compatibility name: unresolved cleanup eligibility after a possible
    // enable write, not proof of the current vendor RF pairing-mode state.
    [[nodiscard]] bool pairingActive() const noexcept;
    // Metadata only: no raw feature payload or identifier is exported here.
    [[nodiscard]] const FeatureExchangeEvidence& lastFeatureExchange() const noexcept;
    [[nodiscard]] const std::optional<FeatureExchangeEvidence>& lastFailedFeatureExchange() const noexcept;
    [[nodiscard]] const FeatureExchangeEvidence& pairingEnableExchange() const noexcept;
    [[nodiscard]] const PairingCleanupEvidence& pairingCleanupEvidence() const noexcept;

    bool pollOnce();
    [[nodiscard]] std::uint64_t transportGeneration() const noexcept;
    void setEventCallback(EventCallback callback);
    [[nodiscard]] std::vector<TrackerState> snapshot() const;
    // Mutex-protected runtime evidence: at most current + previous per slot.
    // Active vendor-local candidate addressing only; no serial/token contents,
    // stable identity or Ready field. Service uses its registry-gated collector.
    [[nodiscard]] std::vector<ConnectionEpochSnapshot> connectionEpochSnapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vut
