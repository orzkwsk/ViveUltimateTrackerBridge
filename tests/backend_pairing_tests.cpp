#include "vut/backend.hpp"
#include "vut/backend_transport.hpp"
#include "vut/protocol.hpp"

#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#define CHECK(value) do { if (!(value)) throw std::runtime_error("CHECK failed: " #value); } while (false)

namespace {

class FakeTransport final : public vut::detail::BackendTransport {
public:
    bool open(std::uint16_t vid, std::uint16_t pid, bool exclusive,
              std::string&) override {
        opened = true;
        openedVid = vid;
        openedPid = pid;
        openedExclusive = exclusive;
        return openSucceeds;
    }

    void close() override { closed = true; }
    std::size_t featureLength() const noexcept override { return 65; }

    bool setFeature(std::span<const std::uint8_t> data) override {
        writes.emplace_back(data.begin(), data.end());
        lastCommand = data.size() > 1 ? data[1] : 0;
        if (failSetCount > 0) {
            --failSetCount;
            return false;
        }
        return true;
    }

    std::vector<std::uint8_t> getFeature() override {
        if (!respond) return {};
        std::vector<std::uint8_t> response(65, 0);
        response[1] = lastCommand;
        response[2] = 4;
        return response;
    }

    std::vector<std::uint8_t> read(int) override {
        if (reads.empty()) return {};
        auto result = std::move(reads.front());
        reads.pop_front();
        return result;
    }

    bool readFailed() const noexcept override { return failRead; }

    bool openSucceeds = true;
    bool opened = false;
    bool openedExclusive = false;
    bool closed = false;
    bool respond = true;
    bool failRead = false;
    int failSetCount = 0;
    std::uint16_t openedVid = 0;
    std::uint16_t openedPid = 0;
    std::uint8_t lastCommand = 0;
    std::vector<std::vector<std::uint8_t>> writes;
    std::deque<std::vector<std::uint8_t>> reads;
};

std::vector<std::uint8_t> pairReport(const std::array<std::uint8_t, 6>& address) {
    std::vector<std::uint8_t> report(12, 0);
    report[0] = vut::protocol::kResponsePairEvent;
    report[1] = 12;
    std::memcpy(report.data() + 6, address.data(), address.size());
    return report;
}

void checkPairCommand(const std::vector<std::uint8_t>& packet,
                      bool enabled,
                      int slot) {
    CHECK(packet.size() == 65);
    CHECK(packet[1] == vut::protocol::kCmdRequestRfChangeBehavior);
    CHECK(packet[2] == 9);
    CHECK(packet[3] == vut::protocol::kRfBehaviorPairDevice);
    CHECK(packet[4] == static_cast<std::uint8_t>(enabled ? 1 : 0));
    for (int selector = 0; selector < 5; ++selector)
        CHECK(packet[5 + selector] == static_cast<std::uint8_t>(slot == -1 || slot == selector));
}

} // namespace

int main() try {
    // Normal start opens the transport but emits no PairDevice command.
    auto transport = std::make_unique<FakeTransport>();
    auto* fake = transport.get();
    vut::Backend backend(std::move(transport));
    vut::BackendConfig config;
    config.configureTrackingOnPair = false;
    CHECK(backend.start(config));
    CHECK(fake->opened && !fake->openedExclusive);
    CHECK(fake->openedVid == vut::protocol::kVendorIdVive);
    CHECK(fake->openedPid == vut::protocol::kProductIdUltimateTrackerDongle);
    CHECK(fake->writes.empty());
    CHECK(backend.start(config));
    CHECK(fake->writes.empty()); // transport reconnect/start never replays PairDevice

    CHECK(!backend.beginPairing(-2));
    CHECK(!backend.beginPairing(5));
    CHECK(fake->writes.empty());
    CHECK(backend.beginPairing(-1));
    CHECK(backend.pairingEnableExchange().matched());
    CHECK(backend.pairingEnableExchange().effect == vut::SemanticEffect::Unverified);
    CHECK(fake->writes.size() == 1);
    checkPairCommand(fake->writes[0], true, -1);
    CHECK(backend.beginPairing(-1));
    CHECK(fake->writes.size() == 1);
    CHECK(!backend.beginPairing(0));
    CHECK(fake->writes.size() == 1);
    CHECK(backend.endPairing());
    CHECK(fake->writes.size() == 2);
    checkPairCommand(fake->writes[1], false, -1);
    CHECK(backend.endPairing());
    CHECK(fake->writes.size() == 2);
    backend.stop();

    // A parsed pair event returns the exact observed six-byte address.
    auto eventTransport = std::make_unique<FakeTransport>();
    auto* eventFake = eventTransport.get();
    const std::array<std::uint8_t, 6> address{0x23, 0x32, 0x03, 0x81, 0xfe, 0xec};
    eventFake->reads.push_back(pairReport(address));
    vut::Backend eventBackend(std::move(eventTransport));
    std::optional<vut::DeviceId> paired;
    eventBackend.setEventCallback([&](const vut::BackendEvent& event) {
        if (event.kind == vut::BackendEvent::Kind::DevicePaired) paired = event.device;
    });
    CHECK(eventBackend.start(config));
    CHECK(eventBackend.beginPairing(2));
    CHECK(eventBackend.pollOnce());
    CHECK(paired && paired->slot == 2);
    CHECK(paired->toString() == "23:32:03:81:fe:ec");
    CHECK(eventBackend.endPairing());

    // Timeout/cancel cleanup is Backend::stop: disable before closing HID.
    auto timeoutTransport = std::make_unique<FakeTransport>();
    auto* timeoutFake = timeoutTransport.get();
    vut::Backend timeoutBackend(std::move(timeoutTransport));
    CHECK(timeoutBackend.start(config));
    CHECK(timeoutBackend.beginPairing(1));
    timeoutBackend.stop();
    CHECK(timeoutFake->writes.size() == 2);
    checkPairCommand(timeoutFake->writes[1], false, 1);
    CHECK(timeoutFake->closed);

    // A transport read error follows the same disable-before-close path.
    auto errorTransport = std::make_unique<FakeTransport>();
    auto* errorFake = errorTransport.get();
    vut::Backend errorBackend(std::move(errorTransport));
    CHECK(errorBackend.start(config));
    CHECK(errorBackend.beginPairing(3));
    errorFake->failRead = true;
    CHECK(!errorBackend.pollOnce());
    CHECK(errorFake->writes.size() == 2);
    checkPairCommand(errorFake->writes[1], false, 3);
    CHECK(errorFake->closed);

    // If enable was written but its response was lost, state remains cleanup-
    // eligible and stop still sends the disabling command.
    auto uncertainTransport = std::make_unique<FakeTransport>();
    auto* uncertainFake = uncertainTransport.get();
    vut::Backend uncertainBackend(std::move(uncertainTransport));
    CHECK(uncertainBackend.start(config));
    uncertainFake->respond = false;
    CHECK(!uncertainBackend.beginPairing(4));
    CHECK(uncertainBackend.pairingActive());
    uncertainFake->respond = true;
    uncertainBackend.stop();
    CHECK(uncertainFake->writes.size() == 2);
    checkPairCommand(uncertainFake->writes[1], false, 4);

    // An unmatched disable must not become an enable exchange match in the next
    // generation. Reopen observes a fresh cleanup exchange, not vendor effect.
    auto recoveryTransport = std::make_unique<FakeTransport>();
    auto* recoveryFake = recoveryTransport.get();
    vut::Backend recoveryBackend(std::move(recoveryTransport));
    CHECK(recoveryBackend.start(config));
    CHECK(recoveryBackend.beginPairing(1));
    recoveryFake->respond = false;
    recoveryBackend.stop();
    CHECK(recoveryBackend.pairingActive());
    const auto oldGeneration = recoveryBackend.transportGeneration();
    recoveryFake->respond = true;
    CHECK(recoveryBackend.start(config));
    CHECK(recoveryBackend.transportGeneration() > oldGeneration);
    CHECK(!recoveryBackend.pairingActive());
    CHECK(recoveryBackend.pairingCleanupEvidence().state == vut::CleanupExchangeState::Matched);
    CHECK(recoveryBackend.pairingCleanupEvidence().disable.transportGeneration > oldGeneration);
    CHECK(recoveryBackend.pairingCleanupEvidence().effect == vut::SemanticEffect::Unverified);
    CHECK(recoveryFake->writes.size() == 3);
    checkPairCommand(recoveryFake->writes.back(), false, 1);
    CHECK(recoveryBackend.beginPairing(1));
    CHECK(recoveryFake->writes.size() == 4);
    checkPairCommand(recoveryFake->writes.back(), true, 1);
    recoveryBackend.stop();

    // Pairing tool requests exclusive ownership; the transport sees that flag.
    auto exclusiveTransport = std::make_unique<FakeTransport>();
    auto* exclusiveFake = exclusiveTransport.get();
    vut::Backend exclusiveBackend(std::move(exclusiveTransport));
    config.exclusiveHidAccess = true;
    CHECK(exclusiveBackend.start(config));
    CHECK(exclusiveFake->openedExclusive);

    std::cout << "PASS backend pairing: passive start, exact enable/event/disable, idempotency, selector bounds, timeout/error cleanup, exclusive ownership\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
