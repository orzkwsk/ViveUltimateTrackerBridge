#pragma once

#include "vut/types.hpp"
#include <map>

namespace vut::detail {
// Vendor diagnostic scheduling only. Does not change observation validity or recovery.
class LossDiagnostics {
    struct Slot { std::int64_t since; bool sent = false; };
    std::map<int, Slot> slots_;
public:
    void clear() { slots_.clear(); }
    void forget(int slot) { slots_.erase(slot); }
    bool queryDue(const TrackerState& state, bool enabled) {
        if (!enabled) { clear(); return false; }
        const auto slot = state.device.slot;
        if (slot < 0 || slot >= 5 || state.observedMonotonicNs < 0) return false;
        const bool lost = state.trackingStatus == TrackingStatus::Lost ||
                          state.trackingStatus == TrackingStatus::RecentlyLost;
        if (!lost) { forget(slot); return false; }
        auto [it, inserted] = slots_.try_emplace(slot, Slot{state.observedMonotonicNs});
        auto& interval = it->second;
        if (state.observedMonotonicNs < interval.since) {
            interval = Slot{state.observedMonotonicNs}; return false;
        }
        if (inserted || interval.sent || state.observedMonotonicNs - interval.since < 5'000'000'000LL) return false;
        interval.sent = true;
        return true;
    }
};
}
