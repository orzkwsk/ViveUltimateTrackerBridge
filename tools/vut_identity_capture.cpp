#include "vut/backend.hpp"
#include "vut/identity_capture.hpp"
#include "vut/protocol.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

namespace {
std::atomic_bool running{true};
void stop(int) { running = false; }
// Preserve arbitrary input bytes as U+00xx text; hex fields remain normative.
std::string byteText(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        if(c < 0x80) out += char(c);
        else { out += char(0xc0 | (c >> 6)); out += char(0x80 | (c & 0x3f)); }
    }
    return out;
}
}
int main(int argc, char** argv) try {
    using nlohmann::json;
    std::string scenario;
    int seconds = 180;
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--help") {
            std::cout << "vut_identity_capture --scenario HIL-01 [--duration-seconds 180]\n"
                "Passive exclusive HID capture. NO feature commands, pairing, or queries.\n"
                "Raw private evidence saved in ./hil-private/<run-id>.ndjson.\n"
                "Create ./hil-private/<run-id>.stop to end capture cleanly.\n"
                "Physical labels require separate human-confirmed interval annotations.\n";
            return 0;
        }
        if(i+1>=argc) throw std::invalid_argument("missing option value");
        if(arg=="--scenario") scenario=argv[++i];
        else if(arg=="--duration-seconds") {
            const std::string value=argv[++i]; std::size_t used=0;
            seconds=std::stoi(value,&used);
            if(used!=value.size() || seconds<1 || seconds>3600) throw std::invalid_argument("duration must be 1..3600 seconds");
        } else throw std::invalid_argument("unknown option");
    }
    if(scenario.empty() || scenario.size()>80 || scenario.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")!=std::string::npos)
        throw std::invalid_argument("supply a short scenario marker, e.g. HIL-01");
    std::signal(SIGINT,stop); std::signal(SIGTERM,stop);
    const auto run=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    const auto root=std::filesystem::absolute("hil-private");
    std::filesystem::create_directories(root);
    const auto path=root/(run+".ndjson");
    const auto stopPath=root/(run+".stop");
    if(std::filesystem::exists(path)) throw std::runtime_error("capture exists; refusing to overwrite");
    std::ofstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("cannot create private evidence file");
    auto save=[&](json record) {
        record["capture_run_id"]=run; record["scenario"]=scenario;
        record["evidence_kind"]="hardware-passive";
        file << record.dump() << '\n'; file.flush();
        if(!file) throw std::runtime_error("capture write failed; stopping");
    };
    save({{"kind","capture_start"},{"physical_tracker_label",nullptr},{"passive",true},{"feature_commands_enabled",false},
          {"raw_ack_text_encoding","byte-to-unicode-U00xx; hex is exact"}});
    std::cout << "run=" << run << " scenario=" << scenario << " private_evidence=" << path.string() << std::endl;
    std::uint64_t rawIndex=0; std::string reference;
    std::map<std::string,json> firmware;
    vut::Backend backend;
    vut::BackendConfig config;
    config.configureTrackingOnPair=false; config.diagnosticSlamQueries=false;
    config.diagnosticPassiveCapture=true; config.exclusiveHidAccess=true;
    config.diagnosticReportCallback=[&](auto bytes, auto ns, auto generation) {
        ++rawIndex; reference=run+":raw:"+std::to_string(rawIndex);
        const auto envelope=vut::protocol::parseIncomingEnvelope(bytes);
        if(envelope && envelope->type==vut::protocol::kIncomingPose) return;
        const std::string raw(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        save({{"kind","raw_report"},{"raw_report_reference",reference},{"raw_report_index",rawIndex},
              {"transport_generation",generation},{"observed_monotonic_ns",ns},{"raw_report_hex",vut::diagnostic::hexBytes(raw)}});
    };
    backend.setEventCallback([&](const vut::BackendEvent& e) {
        const auto rf=e.device.toString();
        if(e.kind==vut::BackendEvent::Kind::DevicePaired || e.kind==vut::BackendEvent::Kind::DeviceUnpaired) {
            firmware.erase(rf);
            save({{"kind",e.kind==vut::BackendEvent::Kind::DevicePaired?"pair":"unpair"},
                {"physical_tracker_label",nullptr},{"rf_address",rf},{"slot",e.device.slot},
                {"transport_generation",e.transportGeneration},{"observed_monotonic_ns",e.observedMonotonicNs},
                {"raw_report_reference",reference},{"raw_report_index",rawIndex}});
            std::cout << "pair-event rf=" << rf << " slot=" << e.device.slot << std::endl;
            return;
        }
        if(e.kind!=vut::BackendEvent::Kind::Ack) return;
        const auto parsed=vut::diagnostic::parseIdentityAck(e.text);
        if(parsed.nameSpace=="NAV1" || parsed.nameSpace=="NAV")
            firmware[rf]={{"namespace",parsed.nameSpace},{"payload",byteText(parsed.payload)},
                {"raw_report_reference",reference},{"observed_monotonic_ns",e.observedMonotonicNs}};
        json record={{"kind","ack"},{"physical_tracker_label",nullptr},{"candidate_namespace",parsed.nameSpace},
            {"candidate_value",byteText(parsed.payload)},{"candidate_value_hex",vut::diagnostic::hexBytes(parsed.payload)},
            {"payload_length_bytes",parsed.payload.size()},{"parser_notes",parsed.notes},{"rf_address",rf},{"slot",e.device.slot},
            {"transport_generation",e.transportGeneration},{"observed_monotonic_ns",e.observedMonotonicNs},
            {"raw_ack_text",byteText(e.text)},{"raw_ack_hex",vut::diagnostic::hexBytes(e.text)},
            {"raw_report_reference",reference},{"raw_report_index",rawIndex},{"firmware_evidence",nullptr}};
        if(firmware.contains(rf)) record["firmware_evidence"]=firmware.at(rf);
        save(record);
        std::cout << "ack rf=" << rf << " slot=" << e.device.slot << " namespace=" << parsed.nameSpace
            << " bytes=" << parsed.payload.size() << " generation=" << e.transportGeneration << std::endl;
    });
    auto reopen=[&] {
        firmware.clear();
        const bool ok=backend.start(config);
        save({{"kind",ok?"transport_open":"transport_open_failed"},{"transport_generation",backend.transportGeneration()},
            {"error",ok?"":backend.lastError()}});
        return ok;
    };
    // Initial busy/not-found failure requires attention; do not silently capture nothing.
    if(!reopen()) { std::cerr << backend.lastError() << '\n'; return 2; }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(seconds);
    auto retryAt=std::chrono::steady_clock::now();
    while(running && std::chrono::steady_clock::now()<deadline && !std::filesystem::exists(stopPath)) {
        if(backend.running()) {
            if(!backend.pollOnce()) {
                save({{"kind","transport_lost"},{"transport_generation",backend.transportGeneration()},{"error",backend.lastError()}});
                firmware.clear(); retryAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            }
        } else {
            if(std::chrono::steady_clock::now()>=retryAt) {
                (void)reopen(); retryAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    backend.stop();
    save({{"kind","capture_stop"},{"transport_generation",backend.transportGeneration()}});
    return 0;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
