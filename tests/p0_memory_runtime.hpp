#pragma once
// Test-only memory HID. Production Backend/parser/default ServiceRuntime stage/
// Publisher/codec remain real; no socket, OS HID, feature command or injected admit.
#include "vut/backend_transport.hpp"
#include "vut/protocol.hpp"
#include "vut/service_runtime.hpp"
#include <chrono>
#include <deque>
#include <fstream>
#include <iterator>
#include <stdexcept>
namespace p0 {
inline void require(bool ok, const char *text) {
    if (!ok)
        throw std::runtime_error(text);
}
inline std::int64_t now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
inline std::vector<std::uint8_t> fixture() {
    std::ifstream in(std::string(FIXTURE_DIR) + "/pose37.bin", std::ios::binary);
    std::vector<std::uint8_t> bytes{std::istreambuf_iterator<char>(in), {}};
    require(bytes.size() == 49, "recorded vendor fixture missing");
    return bytes;
}
class MemoryHid final : public vut::detail::BackendTransport {
  public:
    std::deque<std::vector<std::uint8_t>> queue;
    int opens = 0, closes = 0, writes = 0;
    bool open(std::uint16_t, std::uint16_t, bool, std::string &) override {
        ++opens;
        return true;
    }
    void close() override { ++closes; }
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t>) override {
        ++writes;
        return true;
    }
    std::vector<std::uint8_t> getFeature() override { return {}; }
    std::vector<std::uint8_t> read(int) override {
        if (queue.empty())
            return {};
        auto bytes = std::move(queue.front());
        queue.pop_front();
        return bytes;
    }
    bool readFailed() const noexcept override { return false; }
};
inline vut::DeviceId device(const std::vector<std::uint8_t> &bytes) {
    const auto envelope = vut::protocol::parseIncomingEnvelope(bytes);
    require(bool(envelope), "vendor envelope");
    const auto pose = vut::protocol::parsePose(*envelope, now());
    require(bool(pose), "vendor decoder");
    return pose->device;
}
inline monaka::protocol::v2::TrackerObservation decode(std::string_view bytes) {
    monaka::protocol::v2::Envelope envelope;
    monaka::protocol::v2::Error error;
    require(monaka::protocol::v2::DecodeEnvelope(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size(),
                                                 envelope, error),
            "Publisher codec decode");
    require(std::holds_alternative<monaka::protocol::v2::TrackerObservation>(envelope), "expected published pose");
    return std::get<monaka::protocol::v2::TrackerObservation>(envelope);
}
inline void feed(vut::ServiceRuntime &runtime, MemoryHid &hid, const std::vector<std::uint8_t> &fixture, int rf,
                 int status) {
    auto raw = fixture;
    raw[12] = static_cast<std::uint8_t>(rf);
    raw[48] = static_cast<std::uint8_t>(status);
    hid.queue.push_back(std::move(raw));
    require(runtime.pollOnce(), "default ServiceRuntime poll");
}
} // namespace p0
