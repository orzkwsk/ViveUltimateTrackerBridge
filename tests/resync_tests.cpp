// Focused offline admission tests: actual adapter, vendor fixture decoder and codec.
#include "vut/observation_adapter.hpp"
#include "vut/protocol.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#define CHECK(x)                                                                                                       \
    do {                                                                                                               \
        if (!(x))                                                                                                      \
            throw std::runtime_error("check failed: " #x);                                                             \
    } while (false)
using namespace vut;
namespace p = monaka::protocol::v2;
namespace {
BackendEvent sample(std::int64_t ns, int rf, int rawStatus = 0x02) {
    std::ifstream in(std::string(FIXTURE_DIR) + "/pose37.bin", std::ios::binary);
    std::vector<std::uint8_t> raw{std::istreambuf_iterator<char>(in), {}};
    CHECK(raw.size() == 49);
    raw[12] = static_cast<std::uint8_t>(rf);
    raw[48] = static_cast<std::uint8_t>(rawStatus);
    const auto envelope = protocol::parseIncomingEnvelope(raw);
    CHECK(envelope);
    const auto pose = protocol::parsePose(*envelope, ns);
    CHECK(pose);
    return {BackendEvent::Kind::Pose, pose->device, pose, {}, ns, 1};
}
ObservationContext context() {
    return {"resync-test", "00000000-0000-4000-8000-000000000001",       1, 7,
            100,           {{sample(110, 0).device.toString(), "map-a"}}};
}
p::TrackerObservation accepted(ViveObservationAdapter &a, const BackendEvent &e) {
    const auto out = a.admit(e);
    CHECK(out);
    std::string wire;
    p::Error error;
    CHECK(p::EncodeEnvelope(*out, wire, error));
    p::Envelope decoded;
    CHECK(p::DecodeEnvelope(reinterpret_cast<const std::uint8_t *>(wire.data()), wire.size(), decoded, error));
    return std::get<p::TrackerObservation>(decoded);
}
void allPhasesAndBounds() {
    // No relation to stale RF phase is assumed; all 256 first post-gap values recover.
    for (int anchor = 0; anchor < 256; ++anchor) {
        ViveObservationAdapter a(context());
        CHECK(accepted(a, sample(110, 42)).sequence == 0);
        CHECK(!a.admit(sample(600000110, anchor)));
        CHECK(a.diagnostics().resyncPending == 1 && a.diagnostics().lastAcceptedRf == 42);
        CHECK(!a.admit(sample(620000110, anchor + 1)));
        CHECK(a.diagnostics().acceptedPose == 1 && a.diagnostics().resyncCompleted == 0);
        const auto current = accepted(a, sample(640000110, anchor + 2));
        CHECK(current.sequence == 1 && current.timestamp_ns == 640000010);
        CHECK(current.coordinate_space.revision == 7 && current.session_id == context().sessionId);
        CHECK(a.diagnostics().resyncCompleted == 1 && a.diagnostics().resyncPending == 0);
        CHECK(accepted(a, sample(660000110, anchor + 3)).sequence == 2);
    }
    // The existing 500ms continuous bound is unchanged, including equality.
    ViveObservationAdapter a(context());
    accepted(a, sample(110, 254));
    CHECK(accepted(a, sample(500000110, 255)).sequence == 1);
    CHECK(accepted(a, sample(500000111, 0)).sequence == 2);
    CHECK(a.diagnostics().resyncStarted == 0);
    CHECK(!a.admit(sample(500000112, 0)));
    CHECK(!a.admit(sample(500000113, 128)));
    CHECK(!a.admit(sample(500000114, 255)));
    CHECK(a.diagnostics().rejectDuplicateRf == 1 && a.diagnostics().rejectOldOrAmbiguousRf == 2);
}
void badCandidatesAndValidation() {
    for (int delta : {0, -1, 128}) {
        ViveObservationAdapter a(context());
        accepted(a, sample(110, 10));
        CHECK(!a.admit(sample(600000110, 200)));
        CHECK(!a.admit(sample(620000110, 201)));
        CHECK(!a.admit(sample(640000110, 201 + delta)));
        CHECK(a.diagnostics().resyncReset == 1 && a.diagnostics().resyncPending == 0);
        CHECK(a.diagnostics().acceptedPose == 1);
        CHECK(!a.admit(sample(660000110, 254)));
        CHECK(!a.admit(sample(680000110, 255)));
        CHECK(accepted(a, sample(700000110, 0)).sequence == 1);
        CHECK(a.diagnostics().resyncCompleted == 1);
    }
    // Regression against the candidate (including a timestamp within the old
    // admitted window) resets quarantine and cannot fall back to normal admission.
    for (auto regression : {610000110LL, 400000110LL}) {
        ViveObservationAdapter a(context());
        accepted(a, sample(110, 10));
        CHECK(!a.admit(sample(600000110, 20)));
        CHECK(!a.admit(sample(620000110, 21)));
        CHECK(!a.admit(sample(regression, 22)));
        CHECK(a.diagnostics().resyncReset == 1 && a.diagnostics().acceptedPose == 1);
        CHECK(!a.admit(sample(640000110, 30)));
        CHECK(!a.admit(sample(660000110, 31)));
        CHECK(accepted(a, sample(680000110, 32)).sequence == 1);
    }
    ViveObservationAdapter a(context());
    accepted(a, sample(110, 10));
    CHECK(!a.admit(sample(600000110, 20)));
    CHECK(!a.admit(sample(620000110, 21)));
    CHECK(!a.admit(sample(1200000111, 250))); // another silence reseeds, not confirms
    CHECK(a.diagnostics().resyncPending == 1 && a.diagnostics().resyncReset == 1);
    CHECK(!a.admit(sample(1220000111, 251)));
    CHECK(accepted(a, sample(1240000111, 252)).sequence == 1);
    // Invalid provenance/identity/timestamps never mutate or complete a pending chain.
    for (int invalid = 0; invalid < 7; ++invalid) {
        ViveObservationAdapter b(context());
        accepted(b, sample(110, 10));
        CHECK(!b.admit(sample(600000110, 200)));
        auto bad = sample(620000110, 201);
        switch (invalid) {
        case 0:
            bad.transportGeneration = 0;
            break;
        case 1:
            bad.observedMonotonicNs = bad.pose->observedMonotonicNs = 99;
            break;
        case 2:
            bad.device.mac[0] ^= 1;
            bad.pose->device = bad.device;
            break;
        case 3:
            bad.pose->device.mac[0] ^= 1;
            break;
        case 4:
            ++bad.pose->device.slot;
            break;
        case 5:
            --bad.pose->observedMonotonicNs;
            break;
        case 6:
            bad.pose.reset();
            break;
        }
        CHECK(!b.admit(bad));
        CHECK(b.diagnostics().acceptedPose == 1 && b.diagnostics().resyncPending == 1);
        CHECK(!b.admit(sample(640000110, 202))); // only first valid confirmation
        CHECK(accepted(b, sample(660000110, 203)).sequence == 1);
    }
}
void absenceAndValidity() {
    for (int raw : {0x02, 0x12, 0x04, 0x14, 0x03, 0x13, 0x22}) {
        ViveObservationAdapter a(context());
        accepted(a, sample(110, 10));
        CHECK(!a.admit(sample(600000110, 250, raw)));
        CHECK(!a.admit(sample(620000110, 251, raw)));
        const auto out = accepted(a, sample(640000110, 252, raw));
        const bool full = raw == 0x02 || raw == 0x12, rotation = raw == 0x04 || raw == 0x14;
        CHECK(out.modality == (full ? "full" : rotation ? "rotation_only" : "none"));
        CHECK(out.validity.position == full && out.validity.orientation == (full || rotation));
        CHECK(out.orientation_evidence == (full || rotation ? "device" : "none"));
    }
    ViveObservationAdapter a(context());
    accepted(a, sample(110, 10));
    CHECK(!a.admit(sample(600000110, 20)));
    CHECK(!a.admit(sample(620000110, 21)));
    auto invalid = sample(640000110, 22, 0x04);
    invalid.pose->orientationXyzw.w = std::numeric_limits<float>::infinity();
    const auto out = accepted(a, invalid);
    CHECK(out.modality == "none" && !out.orientation && !out.position);
    CHECK(out.orientation_evidence == "none");
    CHECK(!a.admit(sample(1240000111, 30)));
    auto absent = sample(1260000111, 31);
    absent.kind = BackendEvent::Kind::DeviceUnpaired;
    absent.pose.reset();
    CHECK(a.admit(absent));
    CHECK(a.diagnostics().resyncPending == 0);
    CHECK(!a.admit(sample(1280000111, 32)) && a.diagnostics().rejectAbsent == 1);
    absent.kind = BackendEvent::Kind::DevicePaired;
    absent.observedMonotonicNs += 40000000;
    CHECK(!a.admit(absent) && a.restartRequired());
}
} // namespace
int main() try {
    allPhasesAndBounds();
    badCandidatesAndValidation();
    absenceAndValidity();
    std::cout << "PASS actual adapter: all256 RF anchors, three-packet recovery, 500ms bound, wrap, invalid "
                 "chains/provenance/identity, absence and raw validity\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
