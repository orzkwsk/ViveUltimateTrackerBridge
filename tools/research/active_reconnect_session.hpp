#pragma once

#include "vut/backend_transport.hpp"
#include "vut/detail/info_epoch.hpp"
#include "vut/research/observed_reconnect_wire.hpp"

#include <functional>
#include <cwctype>
#include <string>

namespace vut::active_hil {
// Research only. No production controller, retry, initialization or cleanup TX.
enum class Gate { B21, B22, Q1E };
inline const char* gateName(Gate g) { return g == Gate::B21 ? "B2-1" : g == Gate::B22 ? "B2-2" : "Q1E"; }
inline const char* flagName(Gate g) { return g == Gate::Q1E ? "--send-query1e-once" : "--send-behavior2-once"; }
struct Binding {
    DeviceId device;
    std::uint64_t generation = 0, observationEpoch = 0, connectionEpoch = 0;
};
struct Approval {
    Gate gate = Gate::B21;
    std::string flag, run, token, alias;
    Binding binding;
};
struct SendReceipt {
    Gate gate;
    Binding binding;
    std::int64_t elapsedMs;
    research::ObservedFeatureReport request;
    bool attempted = false, returned = false, accepted = false;
    const char* response = "NotObserved";
    std::vector<std::uint8_t> responseBytes;
};
using Audit = std::function<void(const SendReceipt&)>;
inline bool sameDevice(const DeviceId& a, const DeviceId& b) { return a.slot == b.slot && a.mac == b.mac; }
inline bool qualifiesDongleInterface(std::wstring_view path) {
    std::wstring lower(path); std::transform(lower.begin(),lower.end(),lower.begin(),::towlower);
    return lower.find(L"vid_0bb4&pid_0350") != std::wstring::npos &&
        (lower.find(L"&mi_") == std::wstring::npos || lower.find(L"&mi_00") != std::wstring::npos);
}

class Session {
public:
    Session(std::string run, std::array<std::string, 3> tokens, detail::BackendTransport& transport, Audit audit)
        : run_(std::move(run)), tokens_(std::move(tokens)), transport_(transport), audit_(std::move(audit)) {}
    void opened(std::uint64_t generation) {
        if (generation_ || !generation || transport_.featureLength() != 65) return stop("TransportInvariant");
        generation_ = generation; epochs_.transportOpened(generation);
    }
    void captureProof(std::string_view run, std::int64_t proofMs, std::int64_t now) {
        if (run != run_ || proofMs < 0 || proofMs > now) return stop("CaptureProofInvalid");
        captureAt_ = proofMs;
    }
    bool requestBOn(std::int64_t now) {
        if (stopped() || requested_ || now < 5000 || !generation_ || candidate_)
            return reject("PhysicalBaselineInvalid");
        requested_ = true; return true;
    }
    bool confirmBOn(std::int64_t now) {
        if (stopped() || !requested_ || confirmed_) return reject("BOnConfirmationInvalid");
        confirmed_ = true; bOnAt_ = now; return true;
    }
    // The operator must verify B, A OFF, existing registration, and the exact
    // candidate received AFTER B ON in THIS session. Historical RF/slot is insufficient.
    bool bindB(const Binding& value, bool humanBOnlyRegistered, std::int64_t now) {
        if (stopped() || bound_ || !confirmed_ || now - bOnAt_ < 10000 || !candidate_ ||
            !humanBOnlyRegistered || !sameDevice(value.device, *candidate_) ||
            value.generation != generation_ || value.observationEpoch != observationEpoch_ ||
            value.connectionEpoch != connectionEpoch() || now - candidateAt_ > 3000)
            return reject("CurrentBMappingUnavailable");
        binding_ = value; bound_ = true; return true;
    }
    void observe(std::span<const std::uint8_t> raw, std::int64_t now) {
        if (stopped() || raw.empty()) return;
        auto p = raw;
        if (p.size() >= 2 && p[0] == 0 && (p[1] == 0x18 || p[1] == 0x28 || p[1] == 0x1d || p[1] == 0x1e)) p = p.subspan(1);
        if (p[0] == 0x1d || p[0] == 0x1e) {
            // Opaque diagnostic words only. They cannot establish B identity.
            if (p.size() < 29 || p[1] != 29 || p[4] != 2 || p[5] != 23) return stop("MalformedRfStatus");
            std::array<std::uint8_t,23> record{}; std::copy_n(p.begin()+6,23,record.begin());
            if (rfRecord_ && *rfRecord_ != record) ++rfTransitions_;
            rfRecord_ = record; ++rfReports_; return;
        }
        if (p[0] == 0x18) {
            const auto event = protocol::parsePairEvent(raw);
            if (!event || p[1] != 12 || p[5] > 1) return stop("MalformedPairEvent");
            if (!acceptDevice(event->device, now)) return;
            if (event->unpair) {
                if (!attempts_[0] || ++unpairs_ > 1) return stop("UnexpectedUnpairOrRegistrationLoss");
                epochs_.unpaired(generation_, event->device);
            } else {
                if (++pairs_ > 1) return stop("UnexpectedPairStorm");
                if (!epochs_.paired(generation_, event->device)) return stop("EpochViolation");
            }
        } else if (p[0] == 0x28) {
            const auto incoming = protocol::parseIncomingEnvelope(raw);
            if (!incoming) return stop("MalformedIncoming");
            if (!acceptDevice(incoming->device, now)) return;
            if (incoming->type == protocol::kIncomingAck) {
                const std::string_view text(reinterpret_cast<const char*>(incoming->payload.data()), incoming->payload.size());
                if (text.starts_with("NADS") || text.starts_with("NASS")) freshIdentity_ = true;
                epochs_.ack(generation_, incoming->device, text);
            } else if (incoming->type == protocol::kIncomingPose) {
                const auto pose = protocol::parsePose(*incoming, now * 1000000);
                if (!pose) return stop("MalformedPose");
                pose_ = true; epochs_.pose(generation_, incoming->device);
            }
        } else return stop("UnexpectedReport");
        for (const auto& epoch : epochs_.snapshot()) {
            if (epoch.reason != ConnectionEpochReason::None && epoch.reason != ConnectionEpochReason::DeviceUnpaired)
                return stop("EpochViolation");
            if (epoch.active && epoch.infoComplete && epoch.initializationEligible && epoch.poseObserved) recovered_ = true;
        }
        if (recovered_) stop(attempts_[2] ? "HIL-C" : attempts_[1] ? "HIL-B" : attempts_[0] ? "HIL-A" : "NaturalReconnect");
        else if (pose_ && !attempts_[0]) stop("NaturalPoseAlreadyActive");
    }
    bool send(const Approval& approval, std::int64_t now) {
        const auto i = static_cast<std::size_t>(approval.gate);
        if (i >= 3 || stopped() || !bound_) return reject("GateUnavailable");
        if (attempts_[i]) return reject("DuplicateSendRejected");
        if (approval.flag != flagName(approval.gate) || approval.run != run_ || approval.alias != "B" ||
            tokens_[i].empty() || approval.token != tokens_[i]) return reject("MissingOrWrongApproval");
        if (!sameDevice(approval.binding.device, binding_.device) || approval.binding.device.slot < 0 ||
            approval.binding.device.slot >= 5 || approval.binding.generation != generation_ ||
            approval.binding.observationEpoch != observationEpoch_ ||
            approval.binding.connectionEpoch != connectionEpoch() || !candidate_ ||
            !sameDevice(*candidate_, binding_.device) || now - candidateAt_ > 3000)
            return reject("WrongOrStaleBinding");
        if (captureAt_ < 0 || now - captureAt_ > 30000) return reject("CaptureProofExpired");
        if (pose_ || freshIdentity_ || recovered_) return reject("AlreadyReceivingFreshData");
        if (now - bOnAt_ < 10000 || now < nextGateAt_ ||
            (approval.gate != Gate::B21 && !attempts_[0]) ||
            (approval.gate == Gate::B21 && (attempts_[1] || attempts_[2])) ||
            (approval.gate == Gate::B22 && attempts_[2])) return reject("WrongGateOrderOrWindow");
        auto request = approval.gate == Gate::Q1E ?
            std::optional{research::buildObservedQuery1eReport()} : research::buildObservedBehavior2Report(binding_.device.slot);
        if (!request) return reject("InvalidSelector");
        ++attempts_[i]; // consume BEFORE audit/API: failures and throws never rearm
        tokens_[i].clear(); nextGateAt_ = now + 10000;
        SendReceipt receipt{approval.gate, approval.binding, now, *request};
        try {
            audit_(receipt); // durable intent/one-trial journal must succeed first
            receipt.attempted = true;
            receipt.accepted = transport_.setFeature(*request); // EXACTLY ONE CALL; no exchange/retry helper
            receipt.returned = true;
            if (!receipt.accepted) stop("HidWriteFailure");
            else if (approval.gate == Gate::Q1E) {
                receipt.responseBytes = transport_.getFeature(); // EXACTLY ONE separately approved query read
                receipt.response = research::matchObserved1eResponseFrame(receipt.responseBytes) ? "MatchedOpaqueFraming" : "MalformedOrAbsent";
                if (std::string_view(receipt.response) != "MatchedOpaqueFraming") stop("MalformedQueryResponse");
            }
            audit_(receipt);
        } catch (...) {
            stop("AuditOrTransportException");
            try { audit_(receipt); } catch (...) {}
        }
        return !stopped();
    }
    void transportLost() { stop("DongleTransportLoss"); }
    void stop(const char* reason) { if (reason_.empty()) reason_ = reason; }
    bool stopped() const { return !reason_.empty(); }
    const std::string& reason() const { return reason_; }
    bool confirmed() const { return confirmed_; }
    bool bound() const { return bound_; }
    bool pose() const { return pose_; }
    bool freshIdentity() const { return freshIdentity_; }
    bool recovered() const { return recovered_; }
    std::optional<DeviceId> candidate() const { return candidate_; }
    Binding currentBinding() const { return {candidate_.value_or(DeviceId{}), generation_, observationEpoch_, connectionEpoch()}; }
    std::uint64_t connectionEpoch() const {
        std::uint64_t value = 0; for (const auto& e : epochs_.snapshot()) value = std::max(value, e.connectionEpochId); return value;
    }
    auto epochs() const { return epochs_.snapshot(); }
    auto attempts() const { return attempts_; }
    unsigned pairs() const { return pairs_; }
    unsigned unpairs() const { return unpairs_; }
    unsigned rfReports() const { return rfReports_; }
    unsigned rfTransitions() const { return rfTransitions_; }
    auto rfRecord() const { return rfRecord_; }
    std::int64_t nextGateAt() const { return nextGateAt_; }
    void sendReturnedAt(std::int64_t now) {
        // The real API may block. The observation window starts AFTER return,
        // rather than consuming that blocking time as passive observation.
        if (attempts_[0]) nextGateAt_ = std::max(nextGateAt_, now + 10000);
    }
private:
    bool reject(const char* reason) { stop(reason); return false; }
    bool acceptDevice(const DeviceId& device, std::int64_t now) {
        if (!requested_ || device.slot < 0 || device.slot >= 5) return reject("UnexpectedTrackerOrInvalidSlot");
        if (candidate_ && !sameDevice(*candidate_, device)) return reject("SlotRfMutationOrAmbiguousTarget");
        if (!candidate_) { candidate_ = device; ++observationEpoch_; }
        candidateAt_ = now; return true;
    }
    std::string run_, reason_;
    std::array<std::string, 3> tokens_;
    detail::BackendTransport& transport_;
    Audit audit_;
    detail::ConnectionEpochTracker epochs_;
    std::optional<DeviceId> candidate_;
    std::optional<std::array<std::uint8_t,23>> rfRecord_;
    Binding binding_;
    std::array<unsigned, 3> attempts_{};
    std::uint64_t generation_ = 0, observationEpoch_ = 0;
    std::int64_t bOnAt_ = 0, candidateAt_ = 0, captureAt_ = -1, nextGateAt_ = 0;
    unsigned pairs_ = 0, unpairs_ = 0, rfReports_ = 0, rfTransitions_ = 0;
    bool requested_ = false, confirmed_ = false, bound_ = false, freshIdentity_ = false, pose_ = false, recovered_ = false;
};
} // namespace vut::active_hil
