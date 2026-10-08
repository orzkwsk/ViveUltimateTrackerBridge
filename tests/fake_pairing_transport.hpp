#pragma once
#include "vut/backend_transport.hpp"
#include "vut/protocol.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <stdexcept>

namespace pairing_test {
inline std::int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline vut::DeviceId device(int slot = 2) {
    return {{0x23, static_cast<std::uint8_t>(0x30+slot), 0x03, 0x81, 0xfe, 0xec}, slot};
}
inline std::vector<std::uint8_t> pairReport(int slot = 2) {
    std::vector<std::uint8_t> report(12, 0);
    report[0] = vut::protocol::kResponsePairEvent; report[1] = 12;
    const auto d = device(slot);
    std::copy(d.mac.begin(),d.mac.end(),report.begin()+6);
    return report;
}
class Transport final : public vut::detail::BackendTransport {
public:
    bool open(std::uint16_t, std::uint16_t, bool exclusive, std::string&) override {
        openedExclusive = exclusive; closed = false; failRead = false; ++opens; return true;
    }
    void close() override { closed = true; }
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t> packet) override {
        if (closed) throw std::runtime_error("write after close");
        writes.emplace_back(packet.begin(), packet.end());
        enabling = packet[4] != 0;
        if (!enabling && failDisableWrites > 0) { --failDisableWrites; return false; }
        return true;
    }
    std::vector<std::uint8_t> getFeature() override {
        ++featureReads;
        if (throwEnableResponse && enabling) { throwEnableResponse = false; throw std::runtime_error("injected response exception"); }
        if ((enabling && uncertainEnable) || (!enabling && uncertainDisable)) return {};
        std::vector<std::uint8_t> response(65,0);
        response[1] = vut::protocol::kCmdRequestRfChangeBehavior; response[2] = 4;
        return response;
    }
    std::vector<std::uint8_t> read(int) override {
        if (throwRead) throw std::runtime_error("injected poll exception");
        if (reads.empty()) return {};
        auto report = std::move(reads.front()); reads.pop_front(); return report;
    }
    bool readFailed() const noexcept override { return failRead; }
    bool openedExclusive = false, closed = true, enabling = false;
    bool failRead = false, throwRead = false, uncertainEnable = false, uncertainDisable = false, throwEnableResponse = false;
    int failDisableWrites = 0, opens = 0, featureReads = 0;
    std::deque<std::vector<std::uint8_t>> reads;
    std::vector<std::vector<std::uint8_t>> writes;
};
}
