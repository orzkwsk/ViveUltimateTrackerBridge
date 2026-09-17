#pragma once

#include "vut/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vut::protocol {

constexpr std::uint16_t kVendorIdVive = 0x0bb4;
constexpr std::uint16_t kProductIdUltimateTrackerDongle = 0x0350;
constexpr std::uint16_t kProductIdUltimateTracker = 0x06a3;

constexpr std::uint8_t kResponsePairEvent = 0x18;
constexpr std::uint8_t kResponseRfStatusChanged = 0x1d;
constexpr std::uint8_t kResponseRfStatus = 0x1e;
constexpr std::uint8_t kResponseTrackerIncoming = 0x28;
constexpr std::uint16_t kIncomingAck = 0x0101;
constexpr std::uint16_t kIncomingPose = 0x0110;

constexpr std::uint8_t kCmdTx = 0x18;
constexpr std::uint8_t kCmdRequestRfChangeBehavior = 0x1d;
constexpr std::uint8_t kRfBehaviorPairDevice = 0x00;
constexpr std::uint8_t kTxAckToPartialMac = 0x04;

struct PairEvent {
    bool unpair = false;
    DeviceId device;
};

struct RfStatusReport {
    std::uint8_t responseId = 0;
    std::uint8_t innerTag = 0;
    std::uint16_t unknownHeader = 0;
    std::array<std::uint32_t, 5> slotState{};
};

struct IncomingEnvelope {
    std::uint8_t command = 0;
    std::uint16_t packetIndex = 0;
    DeviceId device;
    std::uint16_t type = 0;
    std::span<const std::uint8_t> payload;
};

[[nodiscard]] int slotFromMac(const std::array<std::uint8_t, 6>& mac) noexcept;
[[nodiscard]] float halfToFloat(std::uint16_t value) noexcept;
[[nodiscard]] std::optional<PairEvent> parsePairEvent(std::span<const std::uint8_t> report);
[[nodiscard]] std::optional<RfStatusReport> parseRfStatus(std::span<const std::uint8_t> report);
[[nodiscard]] std::optional<IncomingEnvelope> parseIncomingEnvelope(std::span<const std::uint8_t> report);
[[nodiscard]] std::optional<TrackerState> parsePose(const IncomingEnvelope& envelope, std::int64_t observedMonotonicNs);
[[nodiscard]] std::vector<std::uint8_t> buildFeatureCommand(std::uint8_t command, std::span<const std::uint8_t> payload, std::size_t reportLength = 65);
[[nodiscard]] std::vector<std::uint8_t> buildAckTx(const DeviceId& device, const std::string& ack);

} // namespace vut::protocol
