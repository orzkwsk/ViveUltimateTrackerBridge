#include "vut/agent_control.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
namespace vut {
namespace {
using Json = nlohmann::json;
const char* connection(control::ConnectionState value) {
    switch (value) {
    case control::ConnectionState::Observed: return "Observed";
    case control::ConnectionState::Connected: return "Paired event observed";
    case control::ConnectionState::Disconnected: return "Disconnected";
    default: return "Ambiguous";
    }
}
Json status(ServiceRuntime& runtime) {
    const auto s = runtime.snapshot({500000000, 1});
    Json devices = Json::array();
    for (bool current : {true, false}) for (auto it = s->control->registry.devices.rbegin(); it != s->control->registry.devices.rend(); ++it) {
        const auto& d = *it;
        if (d.actionable != current || devices.size() == 5) continue;
        const auto rf = DeviceId{d.binding.rf, d.binding.slot}.toString();
        const auto age = d.lastAcceptedPoseAgeNs;
        const bool fresh = d.actionable && age && *age >= 0 && *age <= 500000000;
        Json position = nullptr, orientation = nullptr;
        std::string modality = age ? "STALE" : "UNKNOWN";
        if (fresh && d.binding.slot >= 0 && d.binding.slot < 5) {
            const auto& p = s->admittedPoses[d.binding.slot];
            if (p && p->device_id == rf) {
                position = p->validity.position; orientation = p->validity.orientation;
                modality = p->modality == "full" ? "Pos+Q" : p->modality == "rotation_only" ? "Q-only" : "NONE";
            }
        }
        devices.push_back({{"displayName", "Observed tracker"}, {"observedId", rf}, {"stableId", nullptr},
            {"connection", connection(d.connection)}, {"tracking",orientation.is_null() ? "Unknown" : d.tracking == control::TrackingState::Tracking ? "Tracked" : d.tracking == control::TrackingState::Lost ? "Lost/degraded" : d.tracking == control::TrackingState::Initializing ? "Initializing" : "Unknown"},
            {"modality", modality}, {"positionValid", position}, {"orientationValid", orientation},
            {"ageMs", age ? Json(*age / 1000000.0) : Json(nullptr)}, {"battery", nullptr}});
    }
    const auto& pair = s->pairing;
    const bool active = pair.state == setup::PairingState::Pairing || pair.state == setup::PairingState::Enabling;
    return {{"version",1},{"ok",true},{"code","Ok"},{"message",pair.reason},
        {"backend",s->state == ServiceRuntimeState::Running ? "Agent connected" : "Agent unavailable"},
        {"pairing",pair.state == setup::PairingState::Paired ? "Paired event observed" : setup::pairingStateName(pair.state)},
        {"pairingActive",active},{"pairingAvailable",s->pairingAvailable},
        {"cleanupConfirmed",pair.cleanupConfirmed},{"cleanupError",pair.cleanupError},
        {"identifying",s->identifying},{"identifyCandidates",s->identifyCandidates},
        {"lastError",s->backendError},{"devices",devices},
        {"output","Observation sent: " + std::to_string(s->statistics.publication.sent) + "; downstream visibility unknown"},
        {"physicalGate","HIL_PENDING; stable identity unavailable; Identify thresholds provisional"}};
}
}
std::string agentControlRequest(ServiceRuntime& runtime, const std::string& bytes) {
    std::string id;
    try {
        if (bytes.size() > 4096) throw std::invalid_argument("request too large");
        const auto request = Json::parse(bytes);
        if (!request.is_object() || !request.contains("id") || !request["id"].is_string()) throw std::invalid_argument("invalid request id");
        id = request["id"].get<std::string>();
        if (id.empty() || id.size() > 64 || !request.contains("version") || request["version"] != 1 ||
            !request.contains("operation") || !request["operation"].is_string()) throw std::invalid_argument("invalid request/version");
        const auto op = request["operation"].get<std::string>();
        for (auto it = request.begin(); it != request.end(); ++it)
            if (it.key() != "id" && it.key() != "version" && it.key() != "operation" &&
                !(op == "begin_pairing" && (it.key() == "selector" || it.key() == "timeoutSeconds")))
                throw std::invalid_argument("unknown request field");
        control::Outcome outcome;
        if (op == "begin_pairing") {
            if (!request.contains("selector") || !request["selector"].is_number_integer() ||
                !request.contains("timeoutSeconds") || !request["timeoutSeconds"].is_number_integer()) throw std::invalid_argument("pairing arguments required");
            outcome = runtime.beginPairing(request["selector"].get<int>(), std::chrono::seconds(request["timeoutSeconds"].get<int>()));
        } else if (op == "cancel_pairing") outcome = runtime.cancelPairing();
        else if (op == "identify") outcome = runtime.beginIdentify();
        else if (op != "get_status" && op != "refresh") throw std::invalid_argument("unsupported operation");
        auto response = status(runtime); response["id"] = id; response["ok"] = outcome.ok();
        if (!outcome.ok()) {
            response["code"] = std::to_string(static_cast<int>(outcome.code));
            response["message"] = outcome.code == control::ErrorCode::Unsupported ? "Pairing during tracking is HIL_PENDING and disabled in this product configuration." :
                "Operation rejected; check transport, active operation and cleanup status.";
        }
        return response.dump();
    } catch (const std::exception& error) {
        return Json{{"version",1},{"id",id},{"ok",false},{"code","InvalidOrFailed"},{"message",std::string(error.what()).substr(0,512)}}.dump();
    }
}
}
