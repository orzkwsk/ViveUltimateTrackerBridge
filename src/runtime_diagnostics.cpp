#include "vut/runtime_diagnostics.hpp"
#include "vut/diagnostic_build.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace vut {
namespace {
std::int64_t monotonic() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::string utc() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::ostringstream out; out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.' << std::setw(3) << std::setfill('0') << ms << 'Z'; return out.str();
}
std::string identity(std::string_view id) {
    return "{\"domain\":\"backend_device_id\",\"value\":" + diagnosticQuote(id) +
        ",\"stable_scope\":\"vive-rf-device-address\",\"source\":\"Backend.DeviceId.full_mac\"}";
}
std::string validity(bool p, bool o) {
    return std::string("{\"pose_present\":true,\"position_valid\":") + (p ? "true" : "false") +
        ",\"orientation_valid\":" + (o ? "true" : "false") + ",\"fully_valid\":" + (p && o ? "true" : "false") + "}";
}
}
std::string diagnosticQuote(std::string_view text) {
    std::ostringstream out; out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 0x20) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec;
        else out << c;
    }
    out << '"'; return out.str();
}
std::string diagnosticBuildJson() {
    return "{\"component_name\":\"ViveUltimateTrackerBridge\",\"version\":" + diagnosticQuote(VUT_BUILD_VERSION) +
        ",\"git_commit\":" + diagnosticQuote(VUT_BUILD_COMMIT) + ",\"git_branch_or_ref\":" + diagnosticQuote(VUT_BUILD_REF) +
        ",\"dirty_at_build\":" + VUT_BUILD_DIRTY + ",\"build_timestamp_utc\":" + diagnosticQuote(VUT_BUILD_UTC) +
        ",\"binary_sha256\":null,\"build_system\":\"CMake\"}";
}
RuntimeDiagnostics::RuntimeDiagnostics(Sink sink, std::size_t capacity)
    : enabled_(bool(sink)), sink_(std::move(sink)), capacity_(capacity < 2 ? 2 : capacity) {
    if (enabled_) { ring_.resize(capacity_); thread_ = std::thread([this] { worker(); }); }
}
RuntimeDiagnostics::~RuntimeDiagnostics() { close(); }
void RuntimeDiagnostics::close() {
    if (!thread_.joinable()) return;
    closing_ = true;
    wake_.notify_one(); thread_.join();
}
RuntimeDiagnostics::Counters RuntimeDiagnostics::counters() const noexcept {
    return {accepted_.load(), written_.load(), droppedPose_.load(), droppedCritical_.load(), writeErrors_.load(), maxWriteNs_.load()};
}
std::string RuntimeDiagnostics::line(std::string_view type, std::string_view details, std::string_view id, std::string_view valid) {
    const std::string session = "{\"session_id\":" + (sessionId_.empty() ? std::string("null") : diagnosticQuote(sessionId_)) +
        ",\"session_token\":null,\"state\":" + diagnosticQuote(sessionState_) + ",\"generation\":" + std::to_string(generation_) + ",\"peer\":null}";
    return "{\"schema_version\":1,\"timestamp_utc\":" + diagnosticQuote(utc()) +
        ",\"monotonic_ns\":" + std::to_string(monotonic()) + ",\"monotonic_domain\":\"process-steady-clock\",\"sequence\":" + std::to_string(sequence_++) +
        ",\"component\":\"ViveUltimateTrackerBridge\",\"event_type\":" + diagnosticQuote(type) +
        ",\"source\":\"vut_service.admitted_observation\",\"tracker_identity\":" + std::string(id) +
        ",\"role\":null,\"session\":" + session + ",\"validity\":" + std::string(valid) +
        ",\"build\":" + diagnosticBuildJson() + ",\"details\":" + std::string(details) + "}\n";
}
void RuntimeDiagnostics::event(std::string_view type, std::string details, std::string id, std::string valid, bool critical) noexcept {
    if (!enabled_) return;
    try { enqueue(line(type, details, id, valid), critical, type == "fatal_error"); }
    catch (...) { if (critical) ++droppedCritical_; else ++droppedPose_; }
}
void RuntimeDiagnostics::enqueue(std::string bytes, bool critical, bool fatal) noexcept {
    if (closing_) { if (critical) ++droppedCritical_; else ++droppedPose_; return; }
    if (fatal) {
        try {
            auto* reserved = new std::string(std::move(bytes));
            std::string* empty = nullptr;
            if (fatal_.compare_exchange_strong(empty, reserved)) { ++accepted_; wake_.notify_one(); }
            else { delete reserved; ++droppedCritical_; }
        } catch (...) { ++droppedCritical_; }
        return;
    }
    try {
        if (closing_) { if (critical) ++droppedCritical_; else ++droppedPose_; return; }
        const auto head = head_.load(std::memory_order_relaxed);
        const auto tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= (critical ? capacity_ : capacity_ / 2)) { if (critical) ++droppedCritical_; else ++droppedPose_; return; }
        ring_[head % capacity_] = std::move(bytes);
        head_.store(head + 1, std::memory_order_release); ++accepted_;
    } catch (...) { if (critical) ++droppedCritical_; else ++droppedPose_; return; }
    wake_.notify_one();
}
void RuntimeDiagnostics::worker() {
    for (;;) {
        std::string bytes;
        { std::unique_lock lock(mutex_); wake_.wait_for(lock, std::chrono::milliseconds(100), [&] { return closing_ || fatal_.load() || head_.load() != tail_.load(); });
          // Queue order preserves timestamps; reserved fatal follows accepted queue.
          const auto tail = tail_.load(std::memory_order_relaxed);
          if (head_.load(std::memory_order_acquire) != tail) { bytes = std::move(ring_[tail % capacity_]); tail_.store(tail + 1, std::memory_order_release); }
          else if (auto* reserved = fatal_.exchange(nullptr)) { bytes = std::move(*reserved); delete reserved; }
          else if (closing_) break;
          else continue;
        }
        const auto start = monotonic();
        try { if (sink_(bytes)) ++written_; else ++writeErrors_; } catch (...) { ++writeErrors_; }
        const auto elapsed = static_cast<std::uint64_t>(monotonic() - start);
        if (elapsed > maxWriteNs_) maxWriteNs_ = elapsed;
    }
    const auto c = counters();
    // Flush counters through the independent writer, even after producer overflow.
    try { const bool wrote = sink_("{\"schema_version\":1,\"timestamp_utc\":" + diagnosticQuote(utc()) + ",\"monotonic_ns\":" + std::to_string(monotonic()) +
        ",\"component\":\"ViveUltimateTrackerBridge\",\"event_type\":\"diagnostic_warning\",\"source\":\"diagnostic_writer\",\"tracker_identity\":null,\"role\":null,\"session\":null,\"validity\":null,\"build\":" + diagnosticBuildJson() +
        ",\"details\":{\"reason\":\"observer_closed\",\"dropped_events\":" + std::to_string(c.droppedPose + c.droppedCritical) + ",\"critical_dropped_events\":" + std::to_string(c.droppedCritical) +
        ",\"write_errors\":" + std::to_string(c.writeErrors) + ",\"max_write_ns\":" + std::to_string(c.maxWriteNs) + ",\"observation_complete\":" + (c.droppedCritical || c.writeErrors ? "false" : "true") + "}}\n"); if (!wrote) ++writeErrors_; } catch (...) { ++writeErrors_; }
}
void RuntimeDiagnostics::beginSession(std::string id, std::uint64_t generation) {
    if (!enabled_) return;
    sessionId_ = std::move(id); generation_ = generation; trackers_.clear(); sessionState("connected");
}
void RuntimeDiagnostics::sessionState(std::string_view state) {
    if (!enabled_) return;
    sessionState_ = state; event("session_" + std::string(state));
}
void RuntimeDiagnostics::observeBackend(const BackendEvent& e) noexcept {
    if (!enabled_) return;
    try {
        if (e.kind == BackendEvent::Kind::DevicePaired)
            event("tracker_discovered", "{\"role_unavailable\":\"backend has no body-role assignment\",\"slot_is_address_only\":true}", identity(e.device.toString()));
        // Unconfigured/rejected inputs remain observable without implying publication.
        else if (e.kind == BackendEvent::Kind::DeviceUnpaired)
            event("backend_tracker_unpaired", "{}", identity(e.device.toString()));
    } catch (...) { ++droppedCritical_; }
}
void RuntimeDiagnostics::observe(const monaka::protocol::v2::Envelope& envelope) noexcept {
    if (!enabled_) return;
    try {
        std::visit([&](const auto& o) {
            using T = std::decay_t<decltype(o)>;
            if constexpr (std::is_same_v<T, monaka::protocol::v2::TrackerObservation> || std::is_same_v<T, monaka::protocol::v2::ObservationDeviceState>) {
                const auto id = identity(o.device_id);
                if (!trackers_.contains(o.device_id) && trackers_.size() >= 256) { ++droppedCritical_; return; }
                auto& tracker = trackers_[o.device_id];
                if constexpr (std::is_same_v<T, monaka::protocol::v2::ObservationDeviceState>) {
                    tracker.registered = o.presence != "absent";
                    event(tracker.registered ? "tracker_registered" : "tracker_unregistered", "{\"presence\":" + diagnosticQuote(o.presence) + "}", id);
                } else {
                    const bool p = o.validity.position, r = o.validity.orientation;
                    const auto valid = validity(p, r);
                    if (!tracker.registered) { tracker.registered = true; event("tracker_registered", "{\"registration_source\":\"first admitted pose\"}", id); }
                    if (!tracker.firstPose) { tracker.firstPose = true; event("first_pose", "{}", id, valid); }
                    if (p && !tracker.firstPosition) { tracker.firstPosition = true; event("first_position_valid", "{}", id, valid); }
                    if (!tracker.position || *tracker.position != p) event("position_valid_changed", "{}", id, valid);
                    if (!tracker.orientation || *tracker.orientation != r) event("orientation_valid_changed", "{}", id, valid);
                    if (!tracker.position || !tracker.orientation || (*tracker.position && *tracker.orientation) != (p && r)) event("fully_valid_changed", "{}", id, valid);
                    tracker.position = p; tracker.orientation = r;
                    event("pose_observed", "{\"observation_sequence\":" + std::to_string(o.sequence) + ",\"sample_timestamp_ns\":" + std::to_string(o.timestamp_ns) + ",\"role_unavailable\":\"backend has no body-role assignment\"}", id, valid, false);
                }
            }
        }, envelope);
    } catch (...) { ++droppedCritical_; }
}
}
