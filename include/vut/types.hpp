#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace vut {

enum class TrackingStatus : std::uint8_t {
    NoImagesYet = 0,
    NotInitialized = 1,
    Ok = 2,
    Lost = 3,
    RecentlyLost = 4,
    SystemNotReady = 5,
    Unknown = 0xff,
};

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Quatf {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

struct DeviceId {
    std::array<std::uint8_t, 6> mac{};
    int slot = -1;

    [[nodiscard]] std::string toString() const;
};

struct TrackerState {
    DeviceId device;
    std::uint8_t packetIndex = 0;
    std::uint16_t buttons = 0;
    TrackingStatus trackingStatus = TrackingStatus::Unknown;
    bool positionValid = false;
    bool orientationValid = false;

    Vec3f positionMeters;
    Quatf orientationXyzw;
    Vec3f linearAccelerationRaw;
    std::array<float, 4> angularVelocityRaw{};

    std::int64_t observedMonotonicNs = 0;
};

struct BackendEvent {
    enum class Kind {
        DevicePaired,
        DeviceUnpaired,
        Pose,
        Ack,
        Diagnostic,
    } kind = Kind::Diagnostic;

    DeviceId device;
    std::optional<TrackerState> pose;
    std::string text;
    std::int64_t observedMonotonicNs = 0;
    std::uint64_t transportGeneration = 0;
};

} // namespace vut
