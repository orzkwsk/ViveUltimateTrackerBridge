#pragma once
#include "vut/frontend_stream.hpp"
#include "vut/service_runtime.hpp"
#include <sstream>
#include <iomanip>
namespace vut::frontend {
inline Json status(const ServiceRuntimeSnapshot& snapshot) {
    Json devices = Json::array();
    if (snapshot.control) for (const auto& device : snapshot.control->registry.devices) {
        if (!device.runtimeHandle || devices.size() == 5) continue;
        std::ostringstream address;
        for (auto byte : device.binding.rf) { if (address.tellp() > 0) address << ':'; address << std::hex << std::setw(2) << std::setfill('0') << unsigned(byte); }
        const auto age = device.lastAcceptedPoseAgeNs;
        const auto tracking = device.tracking == control::TrackingState::Tracking ? "Tracking" :
            device.tracking == control::TrackingState::Lost ? "Lost" :
            device.tracking == control::TrackingState::Initializing ? "Initializing" : "Unknown";
        devices.push_back({{"displayName","Observed VIVE tracker"},{"observedId",address.str()},
            {"stableId",nullptr},{"connection",device.connection == control::ConnectionState::Connected ? "Observed connected" : "Unknown"},
            {"tracking",device.freshness == control::PoseFreshness::Stale ? "Stale" : tracking},
            {"positionValid",nullptr},{"orientationValid",nullptr},{"ageMs",age ? Json(*age/1e6) : Json(nullptr)},{"battery",nullptr}});
    }
    return {{"ok",true},{"code","Ok"},{"message",""},{"backend",snapshot.state == ServiceRuntimeState::Running ? "Tracking service running" : "Tracking service unavailable"},
        {"pairing","Unsupported while tracking"},{"pairingActive",false},{"cleanupConfirmed",true},
        {"lastError",snapshot.backendError},{"devices",devices},{"stableIdentity","Unavailable"},
        {"assignment","Unsupported: body assignment belongs to MonakaVR"},
        {"output",snapshot.statistics.publication.sendErrors ? "Output send errors" : "Observation publisher active"}};
}
} // namespace vut::frontend
