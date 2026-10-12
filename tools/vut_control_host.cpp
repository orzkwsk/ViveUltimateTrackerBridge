#include "vut/setup_configuration.hpp"
#include "vut/setup_session.hpp"
#include "vut/frontend_stream.hpp"
#include <chrono>
#include <iostream>

namespace {
std::int64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count(); }
vut::frontend::Json status(vut::Backend& backend, vut::setup::SetupSession& session) {
    using vut::frontend::Json;
    const auto now = nowNs();
    Json devices = Json::array();
    for (const auto& [rf, device] : session.devices().entries()) {
        if (devices.size() == 5) break;
        const auto age = device.lastPoseNs > 0 ? std::optional<double>((now-device.lastPoseNs)/1e6) : std::nullopt;
        const bool stale = !age || *age > 500;
        const bool usable = device.connected && !stale;
        devices.push_back({{"displayName","Observed VIVE tracker"},{"observedId",rf},
            {"stableId",nullptr},{"connection",device.connected ? "Observed connected" : "Disconnected"},
            {"tracking",!device.connected ? "Disconnected" : stale ? "Stale" : vut::trackingStatusName(device.tracking)},
            {"positionValid",device.previous && usable ? Json(device.previous->positionValid) : Json(nullptr)},
            {"orientationValid",device.previous && usable ? Json(device.previous->orientationValid) : Json(nullptr)},
            {"ageMs",age ? Json(*age) : Json(nullptr)},{"battery",nullptr}});
    }
    const auto& pair = session.pairing().result();
    return {{"version",1},{"ok",true},{"code","Ok"},{"message",pair.reason},
        {"backend",backend.running() ? "Setup owner connected" : "Dongle unavailable"},
        {"pairing",pair.state==vut::setup::PairingState::Cancelling && pair.cleanupConfirmed ? "Cancelled" : pair.state==vut::setup::PairingState::Paired ? "Paired event observed" : vut::setup::pairingStateName(pair.state)},{"pairingActive",session.pairing().active()},
        {"cleanupConfirmed",pair.cleanupConfirmed},{"cleanupError",pair.cleanupError},
        {"lastError",backend.lastError()},{"devices",devices},{"stableIdentity","Unavailable"},
        {"assignment","Unsupported: body assignment belongs to MonakaVR"},{"output","Not active in setup mode"}};
}
}
int main(int argc,char** argv) try {
    bool offline=false;std::filesystem::path configPath;
    for(int i=1;i<argc;++i) {
        const std::string option=argv[i];
        if(option=="--offline")offline=true;
        else if(option=="--config"&&i+1<argc)configPath=std::filesystem::absolute(argv[++i]);
        else {std::cerr<<"vut_control_host [--offline] [--config PATH]\n";return 1;}
    }
    vut::Backend backend;
    vut::setup::SetupSession session(backend);
    vut::frontend::Stream stream;
    bool running = true;
    while (running && stream.connected()) {
        if (backend.running()) {
            if (!backend.pollOnce()) { session.pairing().transportLost(backend.lastError()); session.devices().endSession(); }
            session.pairing().tick(nowNs()); // fresh clock after blocking HID poll
        }
        stream.pump([&](const auto& request) -> vut::frontend::Json {
            const auto op = request.at("operation").template get<std::string>();
            if (op == "get_status") return status(backend,session);
            if (op == "connect") {
                if (offline) return vut::frontend::error("Unavailable","Offline host never opens HID.");
                if (session.pairing().active()) return vut::frontend::error("Busy","Cancel pairing before reconnecting.");
                if (backend.running()) return status(backend,session);
                vut::BackendConfig config;
                config.exclusiveHidAccess = true;
                config.automaticVendorActionPolicy = vut::AutomaticVendorActionPolicy::ObserveOnly;
                if (!backend.start(config)) return vut::frontend::error("DongleUnavailable",
                    "Dongle could not be opened. Stop the tracking service and close VIVE Hub, connect the dongle, then retry.");
                session.connected(); return status(backend,session);
            }
            if (op == "begin_pairing") {
                if (!request.contains("selector") || !request["selector"].is_number_integer()
                    || !request.contains("timeoutSeconds") || !request["timeoutSeconds"].is_number_integer()
                    || request["selector"] < -1 || request["selector"] > 4 || request["timeoutSeconds"] < 5 || request["timeoutSeconds"] > 120)
                    return vut::frontend::error("InvalidArgument","Select all or a pair selector 0..4 and a timeout of 5..120 seconds.");
                const int selector = request["selector"].template get<int>(), timeout = request["timeoutSeconds"].template get<int>();
                if (selector < -1 || selector > 4 || timeout < 5 || timeout > 120)
                    return vut::frontend::error("InvalidArgument","Invalid pairing selector or timeout.");
                if (!backend.running()) return vut::frontend::error("Unavailable","Connect the setup owner before pairing.");
                if (session.pairing().active()) return vut::frontend::error("Busy","Pairing is already running.");
                if (!session.pairing().begin(nowNs(),selector,std::chrono::seconds(timeout)))
                    return vut::frontend::error("PairingFailed","Pairing enable or prior cleanup is unconfirmed. Check diagnostics before retrying.");
                return status(backend,session);
            }
            if (op == "save_map") {
                if(configPath.empty())return vut::frontend::error("Unavailable","Tracking configuration destination was not supplied by the launcher.");
                if(session.pairing().active())return vut::frontend::error("Busy","Finish or cancel pairing before saving a map.");
                const auto rf=request.value("observedId",std::string{}),map=request.value("mapId",std::string{});
                const auto found=session.devices().entries().find(rf);
                if(!backend.running()||found==session.devices().entries().end()||!found->second.connected)
                    return vut::frontend::error("Unavailable","Select a currently observed connected RF address. This is not a persistent hardware identity.");
                try {vut::frontend::saveMap(configPath,rf,map);}
                catch(const std::exception& e){return vut::frontend::error("PersistenceFailed",e.what());}
                auto result=status(backend,session);result["message"]="Explicit RF/map configuration saved. Close setup, then start tracking. Body assignment is still unavailable.";return result;
            }
            if (op == "cancel_pairing") { session.pairing().cancel(); return status(backend,session); }
            if (op == "disconnect" || op == "shutdown") {
                const bool clean = session.pairing().cancel("setup closing") && session.shutdown();
                session.devices().endSession(); backend.stop();
                if (op == "shutdown") running = false;
                auto result = status(backend,session); result["ok"] = clean;
                if (!clean) { result["code"] = "CleanupUnconfirmed"; result["message"] = "Disable exchange is unconfirmed; check the dongle before restarting setup."; }
                return result;
            }
            return vut::frontend::error("Unsupported","This setup operation is unsupported.");
        });
        if (!backend.running()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    // Allow the final response to leave the worker before joining it.
    if (!running) stream.drain();
    stream.close();
    const bool cleaned = session.shutdown(); backend.stop();
    return cleaned ? 0 : 2;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
