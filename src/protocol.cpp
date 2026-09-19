#include "vut/protocol.hpp"

#include <bit>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace vut {

std::string DeviceId::toString() const {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < mac.size(); ++i) {
        if (i != 0) out << ':';
        out << std::setw(2) << static_cast<unsigned>(mac[i]);
    }
    return out.str();
}

} // namespace vut

namespace vut::protocol {
namespace {

template <typename T>
T readLe(const std::uint8_t* p) {
    T value{};
    for (std::size_t i = 0; i < sizeof(T); ++i)
        value |= static_cast<T>(static_cast<T>(p[i]) << (8 * i));
    return value;
}

float readFloat32Le(const std::uint8_t* p) {
    const auto bits = readLe<std::uint32_t>(p);
    return std::bit_cast<float>(bits);
}

std::span<const std::uint8_t> stripOptionalReportId(std::span<const std::uint8_t> report) {
    if (report.size() >= 2 && report[0] == 0x00 &&
        (report[1] == kResponsePairEvent || report[1] == kResponseTrackerIncoming || report[1] == 0x1d || report[1] == 0x1e)) {
        return report.subspan(1);
    }
    return report;
}

} // namespace

int slotFromMac(const std::array<std::uint8_t, 6>& mac) noexcept {
    const int slot = mac[1] & 0x0f;
    return slot >= 0 && slot < 5 ? slot : -1;
}

float halfToFloat(std::uint16_t h) noexcept {
    const std::uint32_t sign = (h & 0x8000u) << 16;
    std::uint32_t exponent = (h >> 10) & 0x1fu;
    std::uint32_t mantissa = h & 0x03ffu;
    std::uint32_t out = 0;

    if (exponent == 0) {
        if (mantissa == 0) {
            out = sign;
        } else {
            int shift = 0;
            while ((mantissa & 0x0400u) == 0) {
                mantissa <<= 1;
                ++shift;
            }
            mantissa &= 0x03ffu;
            const std::uint32_t exp32 = static_cast<std::uint32_t>(127 - 14 - shift);
            out = sign | (exp32 << 23) | (mantissa << 13);
        }
    } else if (exponent == 0x1fu) {
        out = sign | 0x7f800000u | (mantissa << 13);
    } else {
        exponent += 127 - 15;
        out = sign | (exponent << 23) | (mantissa << 13);
    }
    return std::bit_cast<float>(out);
}

std::optional<PairEvent> parsePairEvent(std::span<const std::uint8_t> rawReport) {
    const auto report = stripOptionalReportId(rawReport);
    if (report.size() < 12 || report[0] != kResponsePairEvent) return std::nullopt;

    const std::uint8_t declared = report[1];
    if (declared < 12 || report.size() < declared) return std::nullopt;

    const std::size_t payloadLen = declared - 4;
    if (payloadLen < 8) return std::nullopt;
    const auto payload = report.subspan(4, payloadLen);

    PairEvent event;
    event.unpair = payload[1] != 0;
    std::memcpy(event.device.mac.data(), payload.data() + 2, event.device.mac.size());
    event.device.slot = slotFromMac(event.device.mac);
    return event;
}

std::optional<IncomingEnvelope> parseIncomingEnvelope(std::span<const std::uint8_t> rawReport) {
    const auto report = stripOptionalReportId(rawReport);
    if (report.size() < 12 || report[0] != kResponseTrackerIncoming) return std::nullopt;

    IncomingEnvelope envelope;
    envelope.command = report[0];
    envelope.packetIndex = readLe<std::uint16_t>(report.data() + 1);
    std::memcpy(envelope.device.mac.data(), report.data() + 3, envelope.device.mac.size());
    envelope.device.slot = slotFromMac(envelope.device.mac);
    envelope.type = readLe<std::uint16_t>(report.data() + 9);
    const std::size_t payloadLen = report[11];
    if (report.size() < 12 + payloadLen) return std::nullopt;
    envelope.payload = report.subspan(12, payloadLen);
    return envelope;
}

std::optional<TrackerState> parsePose(const IncomingEnvelope& envelope, std::int64_t observedMonotonicNs) {
    if (envelope.type != kIncomingPose) return std::nullopt;
    const auto data = envelope.payload;
    if (data.size() != 0x25 && data.size() != 0x27) return std::nullopt;

    TrackerState state;
    state.device = envelope.device;
    state.packetIndex = data[0];
    state.buttons = data[1];

    state.positionMeters.x = readFloat32Le(data.data() + 2);
    state.positionMeters.y = readFloat32Le(data.data() + 6);
    state.positionMeters.z = readFloat32Le(data.data() + 10);

    const float q0w = halfToFloat(readLe<std::uint16_t>(data.data() + 14));
    const float q1z = halfToFloat(readLe<std::uint16_t>(data.data() + 16));
    const float q2y = halfToFloat(readLe<std::uint16_t>(data.data() + 18));
    const float q3x = halfToFloat(readLe<std::uint16_t>(data.data() + 20));
    state.orientationXyzw = {q3x, q2y, q1z, q0w};

    state.linearAccelerationRaw = {
        halfToFloat(readLe<std::uint16_t>(data.data() + 22)),
        halfToFloat(readLe<std::uint16_t>(data.data() + 24)),
        halfToFloat(readLe<std::uint16_t>(data.data() + 26)),
    };
    for (std::size_t i = 0; i < 4; ++i) {
        state.angularVelocityRaw[i] = halfToFloat(readLe<std::uint16_t>(data.data() + 28 + i * 2));
    }

    const auto statusRaw = data[36];
    // HIL captures show the tracker ORs auxiliary flags into the high nibble:
    // 0x12 carries a valid moving 6DoF pose and 0x13 carries the same
    // rotation-only LOST semantics as 0x03. The low nibble remains the
    // documented/observed pose-state value (0..5).
    const auto poseStatus = static_cast<std::uint8_t>(statusRaw & 0x0f);
    state.trackingStatus = poseStatus <= 5 ? static_cast<TrackingStatus>(poseStatus) : TrackingStatus::Unknown;
    state.positionValid = state.trackingStatus == TrackingStatus::Ok;
    state.orientationValid = state.trackingStatus == TrackingStatus::Ok ||
                             state.trackingStatus == TrackingStatus::Lost ||
                             state.trackingStatus == TrackingStatus::RecentlyLost;
    state.observedMonotonicNs = observedMonotonicNs;
    return state;
}

std::vector<std::uint8_t> buildFeatureCommand(std::uint8_t command, std::span<const std::uint8_t> payload, std::size_t reportLength) {
    if (reportLength < 3 + payload.size()) return {};
    std::vector<std::uint8_t> out(reportLength, 0);
    out[0] = 0;
    out[1] = command;
    out[2] = static_cast<std::uint8_t>(payload.size() + 2);
    std::memcpy(out.data() + 3, payload.data(), payload.size());
    return out;
}

std::optional<std::array<std::uint8_t, 7>> buildRfBehaviorPayload(std::uint8_t behavior, bool enabled, int slot) {
    if (slot < -1 || slot >= 5) return std::nullopt;
    std::array<std::uint8_t, 7> payload = {
        behavior, static_cast<std::uint8_t>(enabled ? 1 : 0), 0, 0, 0, 0, 0
    };
    if (slot == -1) {
        std::fill(payload.begin() + 2, payload.end(), 1);
    } else {
        payload[2 + static_cast<std::size_t>(slot)] = 1;
    }
    return payload;
}

std::vector<std::uint8_t> buildAckTx(const DeviceId& device, const std::string& ack) {
    if (ack.size() > 0x2c || device.slot < 0 || device.slot >= 5) return {};
    std::vector<std::uint8_t> payload;
    payload.reserve(10 + ack.size());
    payload.push_back(kTxAckToPartialMac);
    payload.insert(payload.end(), device.mac.begin(), device.mac.end());
    payload.push_back(0);
    payload.push_back(1);
    payload.push_back(static_cast<std::uint8_t>(ack.size()));
    payload.insert(payload.end(), ack.begin(), ack.end());
    return payload;
}

} // namespace vut::protocol
