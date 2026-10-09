#include "vut/detail/info_epoch.hpp"

#include <algorithm>
#include <limits>

namespace vut::detail {
namespace {
// Gate for the two official B traces at research checkpoint 4560a086, not a
// general vendor specification. Prefix payload lengths/shapes were inspected
// read-only; their contents are neither retained nor logged. Other firmware
// formats fail closed pending separate evidence. NAZZ is exactly four bytes.
constexpr std::array<std::string_view, 9> tokens{
    "NANA", "NADS", "NASS", "NASI", "NAPI", "NAV1", "NARI", "NAGN", "NAZZ"
};
constexpr std::array<std::size_t, 9> suffixLengths{3, 12, 13, 8, 8, 11, 1, 5, 0};
constexpr std::uint16_t fullMask = (1u << tokens.size()) - 1u;
bool validSlot(int slot) noexcept { return slot >= 0 && slot < 5; }
bool validShape(std::size_t token, std::string_view text) noexcept {
    const auto suffix = text.substr(4);
    if (suffix.size() != suffixLengths[token]) return false;
    return std::all_of(suffix.begin(), suffix.end(), [token](unsigned char c) {
        return token == 3 || token == 6 ? c >= '0' && c <= '9' : c >= 0x20 && c <= 0x7e;
    });
}
}

ConnectionEpochReason advanceInfoBurst(InfoBurstProgress& p, std::string_view text) noexcept {
    if (p.state == InfoCollectionState::Invalid) return ConnectionEpochReason::None;
    const auto fail = [&](ConnectionEpochReason reason) {
        p.state = InfoCollectionState::Invalid;
        return reason;
    };
    // Check before shifting/incrementing: corrupted or exhausted progress can
    // never wrap or reconstruct Complete. Even unrelated ACKs cannot hide it.
    if (p.expectedIndex > tokens.size() || (p.seenMask & ~fullMask) != 0 ||
        p.seenMask != static_cast<std::uint16_t>((1u << p.expectedIndex) - 1u) ||
        (p.state == InfoCollectionState::Complete) != (p.expectedIndex == tokens.size()))
        return fail(ConnectionEpochReason::ProgressExhausted);
    const auto token = std::find_if(tokens.begin(), tokens.end(), [text](auto prefix) { return text.starts_with(prefix); });
    if (token == tokens.end()) {
        // Ordinary non-N ACKs do not advance/break a burst. Unknown N* is
        // explicitly rejected throughout an active epoch, including Complete.
        return text.starts_with("N") ? fail(ConnectionEpochReason::UnknownInfoToken) : ConnectionEpochReason::None;
    }
    const auto index = static_cast<std::size_t>(token - tokens.begin());
    if (!validShape(index, text)) return fail(ConnectionEpochReason::MalformedToken);
    if ((p.seenMask & (1u << index)) != 0) return fail(ConnectionEpochReason::DuplicateToken);
    if (index != p.expectedIndex) return fail(ConnectionEpochReason::OutOfOrderOrMissingToken);
    p.seenMask = static_cast<std::uint16_t>(p.seenMask | (1u << index));
    ++p.expectedIndex; // guarded above; maximum nine, no accumulating counter
    if (p.expectedIndex == tokens.size()) p.state = InfoCollectionState::Complete;
    return ConnectionEpochReason::None;
}

void ConnectionEpochTracker::publishProgress(Record& r) noexcept {
    r.evidence.infoState = r.info.state;
    r.evidence.expectedIndex = r.info.expectedIndex;
    r.evidence.seenMask = r.info.seenMask;
    r.evidence.infoComplete = r.evidence.active && r.info.state == InfoCollectionState::Complete;
    r.evidence.initializationEligible = r.evidence.infoComplete && r.evidence.reason == ConnectionEpochReason::None;
}
void ConnectionEpochTracker::close(Record& r, ConnectionEpochReason reason) noexcept {
    if (!r.evidence.active) return; // late input/stop cannot rewrite prior failure
    r.evidence.active = false;
    r.evidence.reason = reason;
    r.evidence.phase = reason == ConnectionEpochReason::DeviceUnpaired ? ConnectionPhase::Disconnected : ConnectionPhase::Invalidated;
    r.info.state = InfoCollectionState::Invalid;
    publishProgress(r);
}
void ConnectionEpochTracker::invalidateAll(ConnectionEpochReason reason) noexcept {
    for (auto& slot : slots_) if (slot.current) close(*slot.current, reason);
}
void ConnectionEpochTracker::transportOpened(std::uint64_t generation) noexcept {
    if (generation_ != generation) invalidateAll(ConnectionEpochReason::TransportGenerationChanged);
    generation_ = generation;
}
bool ConnectionEpochTracker::paired(std::uint64_t generation, const DeviceId& device) noexcept {
    if (!validSlot(device.slot) || generation == 0 || generation != generation_) return false;
    if (exhausted_ || lastAssigned_ == std::numeric_limits<std::uint64_t>::max()) {
        exhausted_ = true;
        invalidateAll(ConnectionEpochReason::EpochCounterExhausted);
        return false; // never wrap and never create a new epoch
    }
    auto& slot = slots_[device.slot];
    if (slot.current) {
        close(*slot.current, ConnectionEpochReason::Superseded);
        slot.previous = slot.current->evidence;
    }
    Record record;
    record.destination = device; // transient full six-byte destination, private
    record.evidence.connectionEpochId = ++lastAssigned_;
    record.evidence.transportGeneration = generation;
    record.evidence.slot = device.slot;
    record.evidence.active = true;
    record.evidence.phase = ConnectionPhase::Associated;
    record.evidence.phase = ConnectionPhase::InfoCollecting;
    slot.current = record; // fresh progress/seen mask/pose, even same destination
    return true;
}
ConnectionEpochTracker::Record* ConnectionEpochTracker::matching(std::uint64_t generation, const DeviceId& device) noexcept {
    if (!validSlot(device.slot) || generation != generation_) return nullptr;
    auto& slot = slots_[device.slot];
    if (!slot.current || !slot.current->evidence.active || slot.current->evidence.transportGeneration != generation) return nullptr;
    auto& r = *slot.current;
    if (r.destination.mac != device.mac) {
        close(r, ConnectionEpochReason::DestinationConflict);
        return nullptr;
    }
    return &r;
}
void ConnectionEpochTracker::unpaired(std::uint64_t generation, const DeviceId& device) noexcept {
    if (auto* r = matching(generation, device)) close(*r, ConnectionEpochReason::DeviceUnpaired);
}
void ConnectionEpochTracker::ack(std::uint64_t generation, const DeviceId& device, std::string_view text) noexcept {
    auto* r = matching(generation, device);
    if (!r) return;
    const auto reason = advanceInfoBurst(r->info, text);
    if (reason != ConnectionEpochReason::None) { close(*r, reason); return; }
    publishProgress(*r);
    if (r->evidence.infoComplete && !r->evidence.poseObserved) r->evidence.phase = ConnectionPhase::InfoComplete;
}
void ConnectionEpochTracker::pose(std::uint64_t generation, const DeviceId& device) noexcept {
    auto* r = matching(generation, device);
    if (!r) return;
    r->evidence.poseObserved = true;
    if (!r->evidence.infoComplete) { close(*r, ConnectionEpochReason::PoseBeforeInfo); return; }
    r->evidence.phase = ConnectionPhase::PoseObserved;
}
std::vector<ConnectionEpochSnapshot> ConnectionEpochTracker::snapshot() const {
    std::vector<ConnectionEpochSnapshot> result;
    result.reserve(slots_.size() * 2);
    for (const auto& slot : slots_) {
        if (slot.previous) result.push_back(*slot.previous);
        if (slot.current) result.push_back(slot.current->evidence);
    }
    return result;
}

} // namespace vut::detail
