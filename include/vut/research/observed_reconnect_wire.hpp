#pragma once

#include "vut/protocol.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace vut::research {

// Observed in official reconnect wire evidence. Vendor semantic and safety
// remain HIL_UNVERIFIED. DO NOT SEND from production code. These pure values
// have no transport, executor, Backend operation or automatic-action hook.
inline constexpr std::uint8_t kRfBehaviorObservedCode2 = 0x02;
inline constexpr std::uint8_t kCmdObservedQuery1e = 0x1e;
using ObservedFeatureReport = std::array<std::uint8_t, 65>; // ID 0 + 64 wire bytes

// Only the observed single-selector form with state byte 1 is represented.
// Broadcast (-1), arbitrary selector masks and other state bytes are unqualified.
[[nodiscard]] inline std::optional<ObservedFeatureReport> buildObservedBehavior2Report(int slot) {
    if (slot < 0 || slot >= 5) return std::nullopt;
    const auto payload = protocol::buildRfBehaviorPayload(kRfBehaviorObservedCode2, true, slot);
    if (!payload) return std::nullopt;
    const auto report = protocol::buildFeatureCommand(protocol::kCmdRequestRfChangeBehavior, *payload, 65);
    if (report.size() != 65) return std::nullopt;
    ObservedFeatureReport result{};
    std::copy(report.begin(), report.end(), result.begin());
    return result;
}

// Literal observed special form: 1e 01 + 62 zero bytes on USB, preceded by
// report ID 0 in the host representation. Byte 01 is not the generic builder's
// payload-size-plus-two convention. No generic feature-command behavior changes.
[[nodiscard]] constexpr ObservedFeatureReport buildObservedQuery1eReport() noexcept {
    return {0, kCmdObservedQuery1e, 1};
}

// Shape of this saved response only, not a general vendor response specification.
// Header bytes 01 00 02 17 and the 23-byte record are kept opaque: no inferred
// count, state, RF semantics, identity or readiness is published.
struct Observed1eResponseFrame {
    std::array<std::uint8_t, 23> opaqueRecord;
};

[[nodiscard]] inline std::optional<Observed1eResponseFrame>
matchObserved1eResponseFrame(std::span<const std::uint8_t> report) noexcept {
    if (report.size() == 65 && report[0] == 0) report = report.subspan(1);
    constexpr std::array<std::uint8_t, 6> prefix{kCmdObservedQuery1e, 29, 1, 0, 2, 23};
    if (report.size() != 64 || !std::equal(prefix.begin(), prefix.end(), report.begin()) ||
        !std::all_of(report.begin() + 29, report.end(), [](auto byte) { return byte == 0; }))
        return std::nullopt;
    Observed1eResponseFrame result{};
    std::copy_n(report.begin() + 6, result.opaqueRecord.size(), result.opaqueRecord.begin());
    return result;
}

} // namespace vut::research
