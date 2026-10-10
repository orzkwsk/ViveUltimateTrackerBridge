#pragma once
#include "vut/backend_transport.hpp"
#include "vut/detail/info_epoch.hpp"
#include "vut/research/observed_reconnect_wire.hpp"
#include <functional>
#include <string>

namespace vut::q1e_hil {
struct Approval {
    std::string flag, run, token, physicalConfirmation;
    std::uint64_t generation = 0;
}; // Targetless host -> dongle request. No slot/address binding field.
struct SendReceipt {
    std::int64_t elapsedMs;
    std::uint64_t generation;
    research::ObservedFeatureReport request;
    bool attempted = false, returned = false, accepted = false;
    const char* responseClass = "NOT_RUN";
    std::vector<std::uint8_t> response;
};
using Audit = std::function<void(const SendReceipt&)>;

class Session {
public:
    Session(std::string run, std::string token, detail::BackendTransport& transport, Audit audit)
        : run_(std::move(run)), token_(std::move(token)), transport_(transport), audit_(std::move(audit)) {}
    void opened(std::uint64_t generation) {
        if (generation_ || !generation || transport_.featureLength() != 65) return stop("TransportInvariant");
        generation_ = generation; epochs_.transportOpened(generation);
    }
    bool requestBOn(std::int64_t now) {
        if (stopped() || requested_ || now < 10000 || !generation_ || !rfReports_ || candidate_)
            return reject("PhysicalBaselineInvalid");
        requested_ = true; return true;
    }
    bool confirmBOn(std::int64_t now, bool bOnlyAOffUsbDisconnected) {
        if (stopped() || !requested_ || confirmed_ || !bOnlyAOffUsbDisconnected)
            return reject("MissingPhysicalConfirmation");
        confirmed_ = true; bOnAt_ = now; return true;
    }
    void captureProof(std::string_view run, std::uint64_t generation, std::int64_t proofMs,
                      unsigned vendorSends, std::int64_t now) {
        if (run != run_ || generation != generation_ || proofMs < 0 || proofMs > now || vendorSends)
            return stop("CaptureProofInvalidOrAutomaticVendorSend");
        captureAt_ = proofMs;
    }
    bool eligible(std::int64_t now) const {
        return !stopped() && !attempts_ && confirmed_ && generation_ && now-bOnAt_ >= 30000 &&
            captureAt_ >= 0 && now-captureAt_ <= 30000 && !freshIdentity_ && !pose_ && !recovered_;
    }
    bool send(const Approval& approval, std::int64_t now) {
        if (attempts_) return reject("DuplicateQ1ERejected");
        if (stopped() || !eligible(now)) return reject("Q1EEligibilityUnavailable");
        if (approval.flag != "--q1e-once" || approval.run != run_ || token_.empty() || approval.token != token_ ||
            approval.generation != generation_ || approval.physicalConfirmation != "B_ONLY_A_OFF_USB_DISCONNECTED")
            return reject("MissingOrWrongApproval");
        const auto request = research::buildObservedQuery1eReport();
        candidateBeforeSend_ = bool(candidate_); rfTransitionsBeforeSend_ = rfTransitions_;
        ++attempts_; token_.clear(); // Consume before durable intent/API; never rearm.
        SendReceipt receipt{now, generation_, request};
        try {
            audit_(receipt);
            receipt.attempted = true;
            receipt.accepted = transport_.setFeature(request); // Exactly one qualified write; no retry.
            receipt.returned = true;
            if (!receipt.accepted) { receipt.responseClass = "TRANSPORT_ERROR"; stop("HidWriteFailure"); }
            else {
                receipt.response = transport_.getFeature(); // Exactly one read for this approved query.
                if (receipt.response.empty()) receipt.responseClass = "NO_RESPONSE";
                else if (const auto frame = research::matchObserved1eResponseFrame(receipt.response)) {
                    receipt.responseClass = "QUALIFIED_MATCH"; responseRecord_ = frame->opaqueRecord;
                } else {
                    auto p = std::span<const std::uint8_t>(receipt.response);
                    if (p.size() == 65 && p[0] == 0) p = p.subspan(1);
                    const bool differentLength = p.size() == 64 && p[0] == 0x1e && p[1] >= 2 && p[1] <= p.size() && p[1] != 29 &&
                        std::all_of(p.begin()+p[1], p.end(), [](auto b) { return b == 0; });
                    receipt.responseClass = differentLength ? "DIFFERENT_VALID_LENGTH" : "MALFORMED";
                    stop("UnqualifiedResponseFraming");
                }
            }
            responseClass_ = receipt.responseClass;
            audit_(receipt);
        } catch (...) {
            receipt.responseClass = "TRANSPORT_ERROR"; responseClass_ = receipt.responseClass;
            stop("AuditOrTransportException");
            try { audit_(receipt); } catch (...) {}
        }
        return !stopped();
    }
    void sendReturnedAt(std::int64_t now) { if (attempts_ && postAt_ < 0) postAt_ = now; }
    void tick(std::int64_t now) {
        if (stopped() || postAt_ < 0 || now-postAt_ < 30000) return;
        if (recovered_) stop("Q1E_ONLY_RECONNECT_CANDIDATE");
        else if (candidate_ && !candidateBeforeSend_) stop("Q1E_MAPPING_DISCOVERY_CANDIDATE");
        else if (candidate_) stop("Q1E_EXISTING_CANDIDATE_NO_RECONNECT");
        else if (responseRecord_) stop(rfTransitions_ != rfTransitionsBeforeSend_ ? "Q1E_RESPONSE_WITH_OPAQUE_RF_CHANGE" : "Q1E_RESPONSE_ONLY");
        else stop("Q1E_NO_RESPONSE");
    }
    void observe(std::span<const std::uint8_t> raw, std::int64_t now) {
        if (stopped() || raw.empty()) return;
        auto p = raw;
        if (p.size() >= 2 && p[0] == 0 && (p[1] == 0x18 || p[1] == 0x28 || p[1] == 0x1d || p[1] == 0x1e)) p = p.subspan(1);
        if (p[0] == 0x1d || p[0] == 0x1e) {
            if (p.size() < 29 || p[1] != 29 || p[4] != 2 || p[5] != 23) return stop("MalformedRfStatus");
            std::array<std::uint8_t,23> record{}; std::copy_n(p.begin()+6,23,record.begin());
            if (rfRecord_ && *rfRecord_ != record) ++rfTransitions_;
            rfRecord_ = record; ++rfReports_; return;
        }
        if (p[0] == 0x18) {
            const auto event = protocol::parsePairEvent(raw);
            if (!event || p[1] != 12 || p[5] > 1) return stop("MalformedPairEvent");
            if (!acceptDevice(event->device, now)) return;
            if (event->unpair) { ++unpairs_; return stop("DeviceUnpairedOrRegistrationLoss"); }
            if (++pairs_ > 1) return stop("UnexpectedPairStorm");
            if (!epochs_.paired(generation_, event->device)) return stop("EpochViolation");
        } else if (p[0] == 0x28) {
            const auto incoming = protocol::parseIncomingEnvelope(raw);
            if (!incoming) return stop("MalformedIncoming");
            if (!acceptDevice(incoming->device, now)) return;
            if (incoming->type == protocol::kIncomingAck) {
                const std::string_view text(reinterpret_cast<const char*>(incoming->payload.data()), incoming->payload.size());
                if (text.starts_with("NADS") || text.starts_with("NASS")) freshIdentity_ = true;
                epochs_.ack(generation_, incoming->device, text);
            } else if (incoming->type == protocol::kIncomingPose) {
                if (!protocol::parsePose(*incoming, now*1000000)) return stop("MalformedPose");
                pose_ = true; epochs_.pose(generation_, incoming->device);
            }
        } else return stop("UnexpectedReport");
        for (const auto& epoch : epochs_.snapshot()) {
            if (epoch.reason != ConnectionEpochReason::None) return stop("EpochViolation");
            if (epoch.active && epoch.infoComplete && epoch.initializationEligible && epoch.poseObserved) recovered_ = true;
        }
        if (!attempts_ && recovered_) stop("NaturalReconnectObserved");
        else if (!attempts_ && pose_) stop("PassivePoseAlreadyActive");
    }
    void stop(const char* reason) { if (reason_.empty()) reason_ = reason; }
    bool stopped() const { return !reason_.empty(); }
    const auto& reason() const { return reason_; }
    const auto& responseClass() const { return responseClass_; }
    auto responseRecord() const { return responseRecord_; }
    auto rfRecord() const { return rfRecord_; }
    auto candidate() const { return candidate_; }
    auto epochs() const { return epochs_.snapshot(); }
    auto generation() const { return generation_; }
    auto observationEpoch() const { return observationEpoch_; }
    auto attempts() const { return attempts_; }
    auto rfReports() const { return rfReports_; }
    auto rfTransitions() const { return rfTransitions_; }
    auto pairs() const { return pairs_; }
    auto unpairs() const { return unpairs_; }
    auto confirmed() const { return confirmed_; }
    auto freshIdentity() const { return freshIdentity_; }
    auto pose() const { return pose_; }
    auto recovered() const { return recovered_; }
    auto bOnAt() const { return bOnAt_; }
    auto postAt() const { return postAt_; }
    auto candidateBeforeSend() const { return candidateBeforeSend_; }
private:
    bool reject(const char* reason) { stop(reason); return false; }
    bool acceptDevice(const DeviceId& device, std::int64_t) {
        if (!requested_ || device.slot < 0 || device.slot >= 5) return reject("UnexpectedTrackerOrInvalidSlot");
        if (candidate_ && (candidate_->mac != device.mac || candidate_->slot != device.slot))
            return reject("SecondTrackerOrSlotRfMutation");
        if (!candidate_) { candidate_ = device; ++observationEpoch_; }
        return true;
    }
    std::string run_, token_, reason_, responseClass_ = "NOT_RUN";
    detail::BackendTransport& transport_; Audit audit_;
    detail::ConnectionEpochTracker epochs_;
    std::optional<DeviceId> candidate_;
    std::optional<std::array<std::uint8_t,23>> rfRecord_, responseRecord_;
    std::uint64_t generation_ = 0, observationEpoch_ = 0;
    std::int64_t bOnAt_ = -1, captureAt_ = -1, postAt_ = -1;
    unsigned attempts_ = 0, rfReports_ = 0, rfTransitions_ = 0, rfTransitionsBeforeSend_ = 0, pairs_ = 0, unpairs_ = 0;
    bool candidateBeforeSend_ = false;
    bool requested_ = false, confirmed_ = false, freshIdentity_ = false, pose_ = false, recovered_ = false;
};
}
