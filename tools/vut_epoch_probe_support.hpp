#pragma once

#include "vut/backend.hpp"

#include <algorithm>
#include <array>
#include <ostream>
#include <string_view>
#include <tuple>

namespace vut::epoch_probe {

inline BackendConfig fixedConfig() {
    BackendConfig config;
    config.automaticVendorActionPolicy = AutomaticVendorActionPolicy::ObserveOnly;
    config.exclusiveHidAccess = true;
    config.configureTrackingOnPair = false;
    config.diagnosticSlamQueries = false;
    return config;
}

inline std::string_view phaseName(ConnectionPhase value) {
    switch (value) {
    case ConnectionPhase::Associated: return "Associated";
    case ConnectionPhase::InfoCollecting: return "InfoCollecting";
    case ConnectionPhase::InfoComplete: return "InfoComplete";
    case ConnectionPhase::PoseObserved: return "PoseObserved";
    case ConnectionPhase::Disconnected: return "Disconnected";
    case ConnectionPhase::Invalidated: return "Invalidated";
    }
    return "Unknown";
}
inline std::string_view reasonName(ConnectionEpochReason value) {
    switch (value) {
    case ConnectionEpochReason::None: return "None";
    case ConnectionEpochReason::DeviceUnpaired: return "DeviceUnpaired";
    case ConnectionEpochReason::BackendStopped: return "BackendStopped";
    case ConnectionEpochReason::TransportLost: return "TransportLost";
    case ConnectionEpochReason::TransportGenerationChanged: return "TransportGenerationChanged";
    case ConnectionEpochReason::Superseded: return "Superseded";
    case ConnectionEpochReason::DuplicateToken: return "DuplicateToken";
    case ConnectionEpochReason::OutOfOrderOrMissingToken: return "OutOfOrderOrMissingToken";
    case ConnectionEpochReason::MalformedToken: return "MalformedToken";
    case ConnectionEpochReason::UnknownInfoToken: return "UnknownInfoToken";
    case ConnectionEpochReason::DestinationConflict: return "DestinationConflict";
    case ConnectionEpochReason::PoseBeforeInfo: return "PoseBeforeInfo";
    case ConnectionEpochReason::EpochCounterExhausted: return "EpochCounterExhausted";
    case ConnectionEpochReason::ProgressExhausted: return "ProgressExhausted";
    }
    return "Unknown";
}
inline std::string_view ackLabel(std::string_view text) {
    constexpr std::array<std::string_view, 9> labels{
        "NANA", "NADS", "NASS", "NASI", "NAPI", "NAV1", "NARI", "NAGN", "NAZZ"};
    for (auto label : labels) if (text.starts_with(label)) return label;
    return text.starts_with("N") ? "ACK_UNKNOWN_N" : "ACK_OTHER";
}
inline auto fields(const ConnectionEpochSnapshot& s) {
    return std::tuple{s.connectionEpochId, s.transportGeneration, s.slot, s.phase,
        s.infoState, s.expectedIndex, s.seenMask, s.active, s.infoComplete,
        s.initializationEligible, s.poseObserved, s.reason};
}

enum class Mark {
    Started, ExclusiveOpenSucceeded, ExclusiveOpenFailed, BOnRequested,
    BOnConfirmed, ObservationFinished, Stopped, InvalidArguments,
    ReceiptUnavailable, ControlUnavailable, RuntimeFailure
};
inline std::string_view markName(Mark value) {
    switch (value) {
    case Mark::Started: return "runner_started";
    case Mark::ExclusiveOpenSucceeded: return "exclusive_open_succeeded";
    case Mark::ExclusiveOpenFailed: return "exclusive_open_failed";
    case Mark::BOnRequested: return "b_on_requested";
    case Mark::BOnConfirmed: return "b_on_confirmed";
    case Mark::ObservationFinished: return "observation_finished";
    case Mark::Stopped: return "backend_stopped";
    case Mark::InvalidArguments: return "invalid_arguments";
    case Mark::ReceiptUnavailable: return "receipt_unavailable";
    case Mark::ControlUnavailable: return "control_unavailable";
    case Mark::RuntimeFailure: return "runtime_failure";
    }
    return "unknown";
}
enum class Result { Pending, FreshBurst, NoAssociation, IncompleteInfo, Violation, Ambiguous };
inline std::string_view resultName(Result result) {
    switch (result) {
    case Result::Pending: return "Pending";
    case Result::FreshBurst: return "PASSIVE_FRESH_BURST_CONFIRMED";
    case Result::NoAssociation: return "PASSIVE_REACQUISITION_NOT_ESTABLISHED";
    case Result::IncompleteInfo: return "PASSIVE_ASSOCIATION_WITHOUT_COMPLETE_INFO";
    case Result::Violation: return "EPOCH_PROTOCOL_VIOLATION";
    case Result::Ambiguous: return "OWNERSHIP_OR_CAPTURE_AMBIGUOUS";
    }
    return "OWNERSHIP_OR_CAPTURE_AMBIGUOUS";
}

// Only enum labels and numeric/boolean metadata reach either stream. No raw
// text, DeviceId formatter, exception text, paths or pose values are serialized.
class Receipt {
public:
    Receipt(std::ostream& output, std::ostream* jsonl = nullptr) : output_(output), jsonl_(jsonl) {}
    void time(std::int64_t elapsed) { elapsed_ = elapsed; }
    void mark(Mark value, std::uint64_t generation = 0) {
        write("runtime", generation, -1, 0, markName(value));
    }
    void result(Result value, std::uint64_t generation) {
        write("result", generation, -1, 0, resultName(value));
    }
    void event(const BackendEvent& e) {
        switch (e.kind) {
        case BackendEvent::Kind::DevicePaired:
            write("DevicePaired", e.transportGeneration, e.device.slot, 0, "None"); break;
        case BackendEvent::Kind::DeviceUnpaired:
            write("DeviceUnpaired", e.transportGeneration, e.device.slot, 0, "None"); break;
        case BackendEvent::Kind::Ack:
            write("ack", e.transportGeneration, e.device.slot, 0, ackLabel(e.text)); break;
        default: break; // pose arrival is recorded once through the epoch transition
        }
    }
    void epoch(const ConnectionEpochSnapshot& s) {
        auto render = [&](std::ostream& out) {
            prefix(out, "epoch", s.transportGeneration, s.slot, s.connectionEpochId);
            out << ",\"phase\":\"" << phaseName(s.phase) << "\",\"progress\":" << unsigned(s.expectedIndex)
                << ",\"seen_mask\":" << s.seenMask << ",\"active\":" << boolean(s.active)
                << ",\"info_complete\":" << boolean(s.infoComplete)
                << ",\"initialization_eligible\":" << boolean(s.initializationEligible)
                << ",\"pose_observed\":" << boolean(s.poseObserved)
                << ",\"reason\":\"" << reasonName(s.reason) << "\"}\n";
        };
        ++seq_; render(output_); if (jsonl_) render(*jsonl_); flush();
    }
    bool good() const { return output_.good() && (!jsonl_ || jsonl_->good()); }
private:
    static const char* boolean(bool value) { return value ? "true" : "false"; }
    void prefix(std::ostream& out, std::string_view kind, std::uint64_t generation, int slot, std::uint64_t epoch) {
        out << "{\"seq\":" << seq_ << ",\"elapsed_ms\":" << elapsed_ << ",\"kind\":\"" << kind
            << "\",\"generation\":" << generation << ",\"epoch\":" << epoch << ",\"slot\":" << slot;
    }
    void write(std::string_view kind, std::uint64_t generation, int slot, std::uint64_t epoch, std::string_view label) {
        auto render = [&](std::ostream& out) {
            prefix(out, kind, generation, slot, epoch);
            out << (kind == "ack" ? ",\"label\":\"" : ",\"reason\":\"") << label << "\"}\n";
        };
        ++seq_; render(output_); if (jsonl_) render(*jsonl_); flush();
    }
    void flush() { output_.flush(); if (jsonl_) jsonl_->flush(); }
    std::ostream& output_;
    std::ostream* jsonl_;
    std::uint64_t seq_ = 0;
    std::int64_t elapsed_ = 0;
};

// One fresh Backend instance per run, no pending explicit setup operation.
// The result is receive evidence only; hardware PASS also requires the private
// capture audit and sole-owner evidence. Failure is latched across new epochs.
class Session {
public:
    Session(Backend& backend, Receipt& receipt) : backend_(backend), receipt_(receipt) {
        backend_.setEventCallback([this](const BackendEvent& event) {
            receipt_.event(event);
            if (event.kind == BackendEvent::Kind::DevicePaired) {
                if (!requested_) ambiguous_ = true;
                if (slot_ != -1 && slot_ != event.device.slot) ambiguous_ = true;
                slot_ = event.device.slot;
                paired_ = true;
            }
        });
    }
    ~Session() { backend_.setEventCallback({}); }
    bool start() {
        receipt_.mark(Mark::Started);
        const bool opened = backend_.start(fixedConfig());
        receipt_.mark(opened ? Mark::ExclusiveOpenSucceeded : Mark::ExclusiveOpenFailed, backend_.transportGeneration());
        if (!opened) result_ = Result::Ambiguous;
        return opened;
    }
    bool request(std::int64_t elapsed) {
        if (requested_ || elapsed < 5000 || result_ != Result::Pending) return false;
        requested_ = true;
        receipt_.mark(Mark::BOnRequested, backend_.transportGeneration());
        return true;
    }
    bool confirm(std::int64_t elapsed) {
        if (!requested_ || confirmed_ || result_ != Result::Pending) return false;
        confirmed_ = true; confirmedAt_ = elapsed;
        receipt_.mark(Mark::BOnConfirmed, backend_.transportGeneration());
        return true;
    }
    void poll() {
        if (result_ != Result::Pending) return;
        if (!backend_.pollOnce()) ambiguous_ = true;
        transitions();
        if (violation_) finish(Result::Violation);
        else if (ambiguous_) finish(Result::Ambiguous);
    }
    void tick(std::int64_t elapsed, int seconds) {
        if (result_ != Result::Pending) return;
        if (confirmed_ && elapsed - confirmedAt_ >= seconds * 1000LL) {
            const auto snapshots = backend_.connectionEpochSnapshot();
            const bool complete = paired_ && std::any_of(snapshots.begin(), snapshots.end(), [&](const auto& s) {
                return s.slot == slot_ && s.transportGeneration == backend_.transportGeneration()
                    && s.active && s.infoComplete && s.initializationEligible
                    && s.expectedIndex == 9 && s.seenMask == 511 && s.reason == ConnectionEpochReason::None;
            });
            finish(complete ? Result::FreshBurst : paired_ ? Result::IncompleteInfo : Result::NoAssociation);
        } else if (elapsed >= 900000) finish(Result::Ambiguous); // bounded operator wait
    }
    void abort() { if (result_ == Result::Pending) finish(Result::Ambiguous); }
    void stop() {
        backend_.stop(); transitions();
        receipt_.mark(Mark::Stopped, backend_.transportGeneration());
    }
    Result result() const { return result_; }
private:
    void transitions() {
        const auto snapshots = backend_.connectionEpochSnapshot();
        for (const auto& s : snapshots) {
            const auto previous = std::find_if(last_.begin(), last_.end(), [&](const auto& p) {
                return p.connectionEpochId == s.connectionEpochId;
            });
            if (previous == last_.end() || fields(*previous) != fields(s)) receipt_.epoch(s);
            switch (s.reason) {
            case ConnectionEpochReason::DuplicateToken:
            case ConnectionEpochReason::OutOfOrderOrMissingToken:
            case ConnectionEpochReason::MalformedToken:
            case ConnectionEpochReason::UnknownInfoToken:
            case ConnectionEpochReason::DestinationConflict:
            case ConnectionEpochReason::PoseBeforeInfo:
            case ConnectionEpochReason::EpochCounterExhausted:
            case ConnectionEpochReason::ProgressExhausted: violation_ = true; break;
            case ConnectionEpochReason::TransportLost:
            case ConnectionEpochReason::TransportGenerationChanged: ambiguous_ = true; break;
            default: break;
            }
        }
        last_ = snapshots;
    }
    void finish(Result result) {
        result_ = result;
        receipt_.mark(Mark::ObservationFinished, backend_.transportGeneration());
        receipt_.result(result, backend_.transportGeneration());
    }
    Backend& backend_;
    Receipt& receipt_;
    std::vector<ConnectionEpochSnapshot> last_;
    Result result_ = Result::Pending;
    bool requested_ = false, confirmed_ = false, paired_ = false, violation_ = false, ambiguous_ = false;
    int slot_ = -1;
    std::int64_t confirmedAt_ = 0;
};
} // namespace vut::epoch_probe
