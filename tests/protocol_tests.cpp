#include "vut/protocol.hpp"

#include <bit>
#include <stdexcept>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
void putFloat(std::vector<std::uint8_t>& b, std::size_t off, float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    b[off + 0] = static_cast<std::uint8_t>(bits);
    b[off + 1] = static_cast<std::uint8_t>(bits >> 8);
    b[off + 2] = static_cast<std::uint8_t>(bits >> 16);
    b[off + 3] = static_cast<std::uint8_t>(bits >> 24);
}
void putU16(std::vector<std::uint8_t>& b, std::size_t off, std::uint16_t value) {
    b[off] = static_cast<std::uint8_t>(value);
    b[off + 1] = static_cast<std::uint8_t>(value >> 8);
}
bool near(float a, float b) { return std::fabs(a - b) < 1e-5f; }
}

int main() try {
    CHECK(vut::protocol::halfToFloat(0x0001) == std::ldexp(1.0f, -24));
    CHECK(vut::protocol::halfToFloat(0x03ff) == std::ldexp(1023.0f, -24));
    CHECK(vut::protocol::halfToFloat(0x0400) == std::ldexp(1.0f, -14));
    CHECK(vut::protocol::halfToFloat(0x0000) == 0.0f);
    CHECK(!std::signbit(vut::protocol::halfToFloat(0x0000)));
    CHECK(std::signbit(vut::protocol::halfToFloat(0x8000)));
    CHECK(std::isinf(vut::protocol::halfToFloat(0x7c00)));
    CHECK(std::isinf(vut::protocol::halfToFloat(0xfc00)));
    CHECK(std::signbit(vut::protocol::halfToFloat(0xfc00)));
    CHECK(std::isnan(vut::protocol::halfToFloat(0x7e00)));

    CHECK(near(vut::protocol::halfToFloat(0x3c00), 1.0f));
    CHECK(near(vut::protocol::halfToFloat(0xc000), -2.0f));

    std::vector<std::uint8_t> report(12 + 0x25, 0);
    report[0] = vut::protocol::kResponseTrackerIncoming;
    putU16(report, 1, 7);
    const std::uint8_t mac[6] = {0x23, 0x32, 0x42, 0xb7, 0x82, 0xd3};
    std::memcpy(report.data() + 3, mac, 6);
    putU16(report, 9, vut::protocol::kIncomingPose);
    report[11] = 0x25;

    report[12] = 4;
    report[13] = 0x01;
    putFloat(report, 14, 1.25f);
    putFloat(report, 18, -0.5f);
    putFloat(report, 22, 2.0f);
    putU16(report, 26, 0x3c00);
    putU16(report, 28, 0x0000);
    putU16(report, 30, 0x0000);
    putU16(report, 32, 0x0000);
    report[48] = 2;

    const auto env = vut::protocol::parseIncomingEnvelope(report);
    CHECK(env);
    CHECK(env->device.slot == 2);
    const auto pose = vut::protocol::parsePose(*env, 1234);
    CHECK(pose);
    CHECK(pose->positionValid);
    CHECK(pose->orientationValid);
    CHECK(near(pose->positionMeters.x, 1.25f));
    CHECK(near(pose->positionMeters.y, -0.5f));
    CHECK(near(pose->positionMeters.z, 2.0f));
    CHECK(near(pose->orientationXyzw.w, 1.0f));
    CHECK(pose->observedMonotonicNs == 1234);

    std::cout << "protocol tests passed\n";
    return 0;
}

catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
