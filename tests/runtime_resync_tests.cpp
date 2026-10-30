#include "p0_memory_runtime.hpp"
#include "vut/agent_control.hpp"
#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>
#define CHECK(x) p0::require(bool(x), "check failed: " #x)
using namespace vut;
namespace p = monaka::protocol::v2;
namespace vut {
struct ServiceRuntimeTestAccess {
    static void event(ServiceRuntime &r, const BackendEvent &e) { r.dispatch(e); }
};
} // namespace vut
namespace {
using Json = nlohmann::json;
struct Runtime {
    std::vector<std::uint8_t> raw = p0::fixture(), peer = raw;
    DeviceId a = p0::device(raw), b;
    p0::MemoryHid *hid;
    std::vector<std::string> wire;
    std::unique_ptr<ServiceRuntime> runtime;
    Runtime() {
        peer[3] = 0x24;
        peer[4] = 0x31;
        b = p0::device(peer);
        auto transport = std::make_unique<p0::MemoryHid>();
        hid = transport.get();
        ServiceRuntimeConfig config;
        config.observation.sourceId = "runtime-resync";
        config.observation.spaces = {{a.toString(), "map-a"}, {b.toString(), "map-b"}};
        ServiceRuntimeDependencies deps;
        deps.clock = p0::now;
        deps.advanceRevision = [] { return 7; };
        deps.makeSessionId = [] { return "00000000-0000-4000-8000-000000000001"; };
        deps.send = [&](auto bytes) {
            wire.emplace_back(bytes);
            return true;
        };
        // No observationFactory override: this is the DEFAULT production stage.
        runtime = std::make_unique<ServiceRuntime>(Backend(std::move(transport)), config, deps);
        CHECK(runtime->start().ok());
    }
    void feed(int rf, int status = 0x02, bool other = false) {
        p0::feed(*runtime, *hid, other ? peer : raw, rf, status);
    }
    std::shared_ptr<const ServiceRuntimeSnapshot> snapshot() { return runtime->snapshot({500000000, 1}); }
    Json ui() {
        return Json::parse(agentControlRequest(*runtime, R"({"version":1,"id":"resync","operation":"get_status"})"));
    }
    Json deviceUi() {
        const auto status = ui();
        CHECK(status["ok"] == true);
        for (const auto &d : status["devices"])
            if (d["observedId"] == a.toString())
                return d;
        throw std::runtime_error("missing device UI row");
    }
    const control::DeviceSnapshot &device(const ServiceRuntimeSnapshot &s) {
        const auto it = std::find_if(s.control->registry.devices.begin(), s.control->registry.devices.end(),
                                     [&](const auto &d) { return d.actionable && d.binding.rf == a.mac; });
        CHECK(it != s.control->registry.devices.end());
        return *it;
    }
};
void realGapAndPeer() {
    Runtime f;
    f.feed(42);
    f.feed(250, 0x02, true);
    const auto initial = f.snapshot();
    const auto session = *initial->observationSession;
    const auto initialPose = *initial->admittedPoses[f.a.slot];
    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(210));
        f.feed(251 + i, 0x02, true);
    }
    CHECK(f.runtime->pollOnce()); // empty HID is transport-live, no automatic restart
    const auto before = f.wire.size();
    f.feed(200, 0x04); // ambiguous pre-gap phase intentionally independent
    auto s = f.snapshot();
    CHECK(f.wire.size() == before && s->statistics.admission.resyncPending == 1);
    CHECK(s->statistics.admission.acceptedPose == 5 && s->statistics.admission.rejectPoseGap == 1);
    CHECK(s->admittedPoses[f.a.slot]->timestamp_ns == initialPose.timestamp_ns);
    CHECK(f.device(*s).lastAcceptedPoseAgeNs && *f.device(*s).lastAcceptedPoseAgeNs > 500000000);
    CHECK(f.device(*s).admission == control::Admission::Rejected);
    auto ui = f.deviceUi();
    CHECK(ui["modality"] == "STALE" && ui["tracking"] == "Unknown");
    CHECK(ui["positionValid"].is_null() && ui["orientationValid"].is_null());
    CHECK(f.ui()["output"].get<std::string>().find("RF resync pending: 1") != std::string::npos);
    f.feed(201, 0x04);
    CHECK(f.wire.size() == before);
    CHECK(f.snapshot()->admittedPoses[f.a.slot]->sequence == 0);
    f.feed(254, 0x02, true);
    CHECK(f.wire.size() == before + 1);
    f.feed(202, 0x04);
    CHECK(f.wire.size() == before + 2);
    const auto recovered = p0::decode(f.wire.back());
    CHECK(recovered.sequence == 1 && recovered.timestamp_ns > initialPose.timestamp_ns + 600000000);
    CHECK(recovered.modality == "rotation_only" && !recovered.validity.position && recovered.validity.orientation);
    CHECK(recovered.orientation_evidence == "device");
    for (int i = 3; i < 20; ++i)
        f.feed(200 + i, i < 10 ? 0x04 : 0x03);
    s = f.snapshot();
    CHECK(s->state == ServiceRuntimeState::Running && s->statistics.admission.acceptedPose == 24);
    CHECK(s->statistics.admission.resyncCompleted == 1 && s->statistics.admission.resyncPending == 0);
    CHECK(s->observationSession->sessionId == session.sessionId && s->observationSession->revision == session.revision);
    CHECK(f.hid->opens == 1 && f.hid->writes == 0);
    std::int64_t seqA = -1, seqB = -1, timeA = -1, timeB = -1;
    for (const auto &bytes : f.wire) {
        const auto p = p0::decode(bytes);
        auto &seq = p.device_id == f.a.toString() ? seqA : seqB;
        auto &time = p.device_id == f.a.toString() ? timeA : timeB;
        CHECK(p.sequence == ++seq && p.timestamp_ns > time);
        time = p.timestamp_ns;
        CHECK(p.session_id == session.sessionId && p.coordinate_space.revision == 7);
        CHECK(p.coordinate_space.id == (p.device_id == f.a.toString() ? "map-a" : "map-b"));
    }
    CHECK(seqA == 18 && seqB == 4);
    const auto lost = p0::decode(f.wire.back());
    CHECK(lost.modality == "none" && !lost.orientation && !lost.position);
    const auto duplicateCount = s->statistics.admission.rejectDuplicateRf;
    const auto orderCount = s->statistics.admission.rejectOldOrAmbiguousRf;
    const auto sent = f.wire.size();
    f.feed(219);
    f.feed(218);
    f.feed(219 + 128);
    CHECK(f.wire.size() == sent);
    CHECK(f.snapshot()->statistics.admission.rejectDuplicateRf == duplicateCount + 1);
    CHECK(f.snapshot()->statistics.admission.rejectOldOrAmbiguousRf == orderCount + 2);
    f.feed(220);
    CHECK(f.wire.size() == sent + 1);
    std::cout << "actual_runtime before_gap_A=1 after20_A=19 rejected_gap=2 peer_frames=5 same_session=1 hid_opens=1 "
                 "hid_writes=0 UI_pending=STALE/Unknown\n";
}
void rawRecoveryAndNumeric() {
    for (int status : {0x03, 0x04}) {
        Runtime f;
        f.feed(42);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        f.feed(254, status);
        f.feed(255, status);
        CHECK(f.wire.size() == 1);
        f.feed(0, status);
        CHECK(f.wire.size() == 2);
        const auto p = p0::decode(f.wire.back());
        CHECK(p.modality == (status == 0x03 ? "none" : "rotation_only"));
        CHECK(p.orientation_evidence == (status == 0x03 ? "none" : "device"));
        auto invalid = f.raw;
        invalid[12] = 1;
        invalid[48] = 0x04;
        invalid[26] = 0;
        invalid[27] = 0x7c; // real half decoder +Infinity w
        f.hid->queue.push_back(invalid);
        CHECK(f.runtime->pollOnce());
        const auto none = p0::decode(f.wire.back());
        CHECK(none.modality == "none" && !none.orientation);
    }
}
void ownershipAndCleanup() {
    Runtime f;
    f.feed(1);
    const auto session = *f.snapshot()->observationSession;
    auto event = [&](DeviceId d, int rf) {
        const auto t = p0::now();
        TrackerState pose;
        pose.device = d;
        pose.packetIndex = static_cast<std::uint8_t>(rf);
        pose.observedMonotonicNs = t;
        pose.trackingStatus = TrackingStatus::Ok;
        return BackendEvent{BackendEvent::Kind::Pose, d, pose, {}, t, session.transportGeneration};
    };
    auto mismatch = event(f.a, 2);
    ++mismatch.pose->device.slot;
    ServiceRuntimeTestAccess::event(*f.runtime, mismatch);
    CHECK(f.runtime->pollOnce());
    CHECK(f.wire.size() == 1 && f.snapshot()->statistics.admission.rejectPoseDeviceMismatch == 1);
    // Simultaneous slot/address conflict is not a legitimate explicit rebind.
    auto moved = f.a;
    moved.slot = f.b.slot;
    ServiceRuntimeTestAccess::event(*f.runtime, event(moved, 2));
    CHECK(f.runtime->pollOnce());
    CHECK(f.snapshot()->statistics.rejectedUnboundPose == 1 && f.wire.size() == 1);
    CHECK(f.snapshot()->statistics.admission.acceptedPose == 1 &&
          f.snapshot()->statistics.admission.resyncStarted == 0);
    // No early-return clock leak: next valid peer event binds, admits and ages normally.
    f.feed(1, 0x02, true);
    CHECK(f.wire.size() == 2);
    const auto peer = p0::decode(f.wire.back());
    CHECK(peer.device_id == f.b.toString() && peer.sequence == 0);
    const auto old = event(f.b, 2);
    auto retired = old;
    --retired.transportGeneration;
    ServiceRuntimeTestAccess::event(*f.runtime, retired);
    CHECK(f.runtime->pollOnce());
    CHECK(f.snapshot()->statistics.admission.rejectGeneration == 1 && f.wire.size() == 2);
    auto beforeEpoch = event(f.b, 2);
    beforeEpoch.observedMonotonicNs = beforeEpoch.pose->observedMonotonicNs = session.epochNs - 1;
    ServiceRuntimeTestAccess::event(*f.runtime, beforeEpoch);
    CHECK(f.runtime->pollOnce());
    CHECK(f.snapshot()->statistics.admission.rejectBeforeEpoch == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    f.feed(2, 0x02, true);
    CHECK(f.wire.size() == 3 && f.snapshot()->statistics.admission.acceptedPose == 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    const auto s = f.snapshot();
    const auto &d = s->control->registry.devices.back();
    CHECK(d.lastAcceptedPoseAgeNs && *d.lastAcceptedPoseAgeNs > 0);
    CHECK(s->state == ServiceRuntimeState::Running && f.hid->writes == 0);
}
} // namespace
int main() try {
    realGapAndPeer();
    rawRecoveryAndNumeric();
    ownershipAndCleanup();
    std::cout << "PASS real memory-HID/default-runtime/Publisher resync, peer continuity, raw loss validity, identity "
                 "and timestamp cleanup; PHYSICAL_HIL=NOT_RUN\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
