#pragma once

#include "vut/connection_epoch.hpp"
#include "vut/types.hpp"

#include <array>
#include <optional>
#include <string_view>
#include <vector>

namespace vut::detail {

// Internal pure state boundary; tests can seed corrupt progress/counter values.
// There is no runtime config or public Backend counter injection.
struct InfoBurstProgress {
    InfoCollectionState state = InfoCollectionState::Collecting;
    std::uint8_t expectedIndex = 0;
    std::uint16_t seenMask = 0;
};
[[nodiscard]] ConnectionEpochReason advanceInfoBurst(InfoBurstProgress& progress,
                                                     std::string_view text) noexcept;

// Bounded storage: current and immediately preceding epoch per vendor slot.
// Historical snapshots retain no destination. Backend synchronizes its collector;
// ServiceRuntime owns a separate provenance/registry-gated collector on its executor.
class ConnectionEpochTracker {
public:
    explicit ConnectionEpochTracker(std::uint64_t lastAssigned = 0) noexcept
        : lastAssigned_(lastAssigned) {}
    void transportOpened(std::uint64_t generation) noexcept;
    [[nodiscard]] bool paired(std::uint64_t generation, const DeviceId& device) noexcept;
    void unpaired(std::uint64_t generation, const DeviceId& device) noexcept;
    void ack(std::uint64_t generation, const DeviceId& device, std::string_view text) noexcept;
    void pose(std::uint64_t generation, const DeviceId& device) noexcept;
    void invalidateAll(ConnectionEpochReason reason) noexcept;
    void observe(const BackendEvent& event) noexcept;
    void retainBindings(std::uint64_t generation,
                        const std::array<std::optional<DeviceId>, 5>& bindings) noexcept;
    [[nodiscard]] std::vector<ConnectionEpochSnapshot> snapshot() const;
    [[nodiscard]] bool exhausted() const noexcept { return exhausted_; }

private:
    struct Record {
        DeviceId destination;
        ConnectionEpochSnapshot evidence;
        InfoBurstProgress info;
    };
    struct Slot {
        std::optional<Record> current;
        std::optional<ConnectionEpochSnapshot> previous;
    };
    std::array<Slot, 5> slots_{};
    std::uint64_t generation_ = 0, lastAssigned_ = 0;
    bool exhausted_ = false;
    [[nodiscard]] Record* matching(std::uint64_t generation, const DeviceId& device) noexcept;
    static void close(Record& record, ConnectionEpochReason reason) noexcept;
    static void publishProgress(Record& record) noexcept;
};

} // namespace vut::detail
