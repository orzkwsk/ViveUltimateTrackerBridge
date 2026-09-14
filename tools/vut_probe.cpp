#include "vut/backend.hpp"

#include <atomic>
#include <csignal>
#include <iomanip>
#include <iostream>

namespace {
std::atomic_bool gRun{true};
void onSignal(int) { gRun = false; }
}

int main() {
    std::signal(SIGINT, onSignal);

    vut::Backend backend;
    backend.setEventCallback([](const vut::BackendEvent& event) {
        if (event.kind == vut::BackendEvent::Kind::DevicePaired) {
            std::cout << "[pair] slot=" << event.device.slot << " mac=" << event.device.toString() << '\n';
        } else if (event.kind == vut::BackendEvent::Kind::DeviceUnpaired) {
            std::cout << "[unpair] slot=" << event.device.slot << " mac=" << event.device.toString() << '\n';
        } else if (event.kind == vut::BackendEvent::Kind::Ack) {
            std::cout << "[ack] slot=" << event.device.slot << " " << event.text << '\n';
        } else if (event.kind == vut::BackendEvent::Kind::Pose && event.pose) {
            const auto& p = *event.pose;
            std::cout << std::fixed << std::setprecision(4)
                      << "[pose] slot=" << p.device.slot
                      << " status=" << static_cast<int>(p.trackingStatus)
                      << " pvalid=" << p.positionValid
                      << " ovalid=" << p.orientationValid
                      << " pos=(" << p.positionMeters.x << ',' << p.positionMeters.y << ',' << p.positionMeters.z << ')'
                      << " q=(" << p.orientationXyzw.x << ',' << p.orientationXyzw.y << ',' << p.orientationXyzw.z << ',' << p.orientationXyzw.w << ')'
                      << '\n';
        }
    });

    if (!backend.start()) {
        std::cerr << "start failed: " << backend.lastError() << '\n';
        return 1;
    }

    std::cout << "VIVE Ultimate Tracker dongle backend started. Ctrl+C to stop.\n";
    while (gRun && backend.running()) {
        if (!backend.pollOnce()) break;
    }
    backend.stop();
    return 0;
}
