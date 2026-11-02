// Actual memory HID -> Backend/vendor decoder -> DEFAULT ServiceRuntime -> Publisher.
// Output envelopes are the exact Publisher Send bytes; never re-encoded.
#include "p0_memory_runtime.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <thread>
int main(int argc, char **argv) try {
    p0::require(argc == 4, "usage: vut_p0_runtime_replay OUTPUT SESSION_UUID REVISION");
    const auto path = std::filesystem::u8path(argv[1]);
    p0::require(!std::filesystem::exists(path), "refusing to replace existing replay evidence");
    const std::string session = argv[2];
    const auto revisionNumber = std::stoull(argv[3]);
    p0::require(revisionNumber <= UINT32_MAX, "revision outside uint32 range");
    const auto revision = static_cast<std::uint32_t>(revisionNumber);
    const auto raw = p0::fixture();
    const auto device = p0::device(raw);
    auto transport = std::make_unique<p0::MemoryHid>();
    auto *hid = transport.get();
    vut::ServiceRuntimeConfig config;
    config.observation.sourceId = "p0-synthetic-altra";
    config.observation.spaces = {{device.toString(), "p0-altra-map"}};
    std::ofstream output;
    std::int64_t epoch = 0, lastReceipt = -1, lastSequence = -1, maximumInterval = 0;
    std::uint64_t rows = 0;
    vut::ServiceRuntimeDependencies deps;
    deps.clock = p0::now;
    deps.advanceRevision = [=] { return revision; };
    deps.makeSessionId = [=] { return session; };
    deps.send = [&](std::string_view wire) {
        const auto pose = p0::decode(wire);
        const auto receipt = epoch + pose.timestamp_ns;
        p0::require(receipt > lastReceipt && pose.sequence == lastSequence + 1, "nonmonotonic Publisher pose");
        p0::require(pose.source_id == "p0-synthetic-altra" && pose.session_id == session &&
                        pose.coordinate_space.revision == revision,
                    "unexpected Publisher source/session/revision");
        if (lastReceipt >= 0)
            maximumInterval = std::max(maximumInterval, receipt - lastReceipt);
        output << "{\"received_at_ns\":" << receipt << ",\"envelope\":";
        output.write(wire.data(), static_cast<std::streamsize>(wire.size()));
        output << "}\n";
        p0::require(bool(output), "failed to write exact Publisher bytes");
        lastReceipt = receipt;
        lastSequence = pose.sequence;
        ++rows;
        return true;
    };
    vut::ServiceRuntime runtime(vut::Backend(std::move(transport)), config, deps);
    p0::require(runtime.start().ok(), "default runtime start rejected context");
    epoch = runtime.snapshot()->observationSession->epochNs;
    output.open(path);
    p0::require(bool(output), "cannot create replay output");
    for (int i = 0; i < 100; ++i) {
        if (i == 40)
            std::this_thread::sleep_for(std::chrono::milliseconds(600));
        const auto before = rows;
        p0::feed(runtime, *hid, raw, i, i < 40 || i >= 60 ? 0x02 : i < 50 ? 0x04 : 0x03);
        if (i == 40 || i == 41) {
            p0::require(rows == before && runtime.statistics().admission.resyncPending == 1,
                        "quarantine published or recovery not pending");
        } else
            p0::require(rows == before + 1, "fresh coherent frame not published");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto snapshot = runtime.snapshot();
    const auto &admission = snapshot->statistics.admission;
    output.close();
    p0::require(bool(output), "replay close failure");
    p0::require(rows == 98 && admission.acceptedPose == 98 && admission.rejectPoseGap == 2 &&
                    admission.resyncCompleted == 1 && admission.resyncPending == 0,
                "recovery totals");
    p0::require(maximumInterval >= 600000000 && snapshot->state == vut::ServiceRuntimeState::Running,
                "missing measured silence or runtime retired");
    p0::require(hid->opens == 1 && hid->writes == 0, "memory HID session changed or commands sent");
    std::cout << "actual_path=memory_HID_Backend_default_ServiceRuntime_Publisher source=p0-synthetic-altra device="
              << device.toString() << " input_frames=100 publisher_rows=" << rows
              << " accepted=" << admission.acceptedPose << " rejected=" << snapshot->statistics.rejected
              << " gap_rejected=" << admission.rejectPoseGap << " resync_pending=" << admission.resyncPending
              << " resync_completed=" << admission.resyncCompleted << " maximum_receipt_interval_ns=" << maximumInterval
              << " runtime=Running same_session=1 hid_opens=" << hid->opens << " hid_writes=" << hid->writes
              << " PHYSICAL_HIL=NOT_RUN\n";
    runtime.shutdown();
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}
