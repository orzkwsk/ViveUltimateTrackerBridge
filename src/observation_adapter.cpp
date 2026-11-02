#include "vut/observation_adapter.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace vut {
namespace c1 = monaka::protocol::v2;
namespace {
template<class T> void header(T& out, const DeviceId& device, const ObservationContext& c,
                             std::int64_t seq, std::int64_t received) {
    out.version = {2, 0};
    out.modality = "none";
    out.source_id = c.sourceId;
    out.session_id = out.clock_id = c.sessionId;
    out.device_id = device.toString();
    out.sequence = seq;
    out.timestamp_ns = out.sent_at_ns = received - c.epochNs;
    out.timestamp_kind = "receive";
    out.coordinate_space = {c.spaces.at(out.device_id), "vut-native-v1", c.revision};
    out.capabilities = {"position", "orientation"};
}
}
ViveObservationAdapter::ViveObservationAdapter(ObservationContext c) : context_(std::move(c)) {
    if (context_.spaces.size() > 256 || context_.epochNs < 0) throw std::invalid_argument("invalid observation context");
    // Validate configured common fields with the fixed codec before polling HID.
    for (const auto& [mac, space] : context_.spaces) {
        c1::ObservationDeviceState state;
        state.version = {2, 0}; state.modality = "none"; state.source_id = context_.sourceId;
        state.session_id = state.clock_id = context_.sessionId;
        state.device_id = mac; state.timestamp_kind = "receive";
        state.presence = "unknown"; state.tracking_state = "unknown";
        state.coordinate_space = {space, "vut-native-v1", context_.revision};
        std::string bytes; c1::Error error;
        if (!c1::EncodeEnvelope(state, bytes, error)) throw std::invalid_argument(error.message);
    }
}
c1::TrackerObservation ViveObservationAdapter::mapPose(const TrackerState& p, const ObservationContext& c, std::int64_t seq) {
    c1::TrackerObservation out;
    header(out, p.device, c, seq, p.observedMonotonicNs);
    const auto status = p.trackingStatus;
    const auto& v = p.positionMeters;
    const auto& q = p.orientationXyzw;
    const bool finitePosition = std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    const double norm = std::hypot(std::hypot(double(q.x), double(q.y)), std::hypot(double(q.z), double(q.w)));
    const bool finiteOrientation = std::isfinite(norm) && norm >= 0.5 && norm <= 1.5;

    // HIL evidence distinguishes pre-localization Lost (0x03/0x13) from
    // post-localization RecentlyLost (0x04/0x14). Only the latter preserves a
    // usable orientation in the previously established native tracking frame.
    const bool full = status == TrackingStatus::Ok && finitePosition && finiteOrientation;
    const bool rotationOnly = status == TrackingStatus::RecentlyLost && finiteOrientation;
    out.validity = {full, full || rotationOnly};
    out.modality = full ? "full" : rotationOnly ? "rotation_only" : "none";
    if (out.validity.position) out.position = c1::Vec3{v.x, v.y, v.z};
    if (out.validity.orientation) out.orientation = c1::QuatXyzw{q.x, q.y, q.z, q.w};
    out.orientation_evidence = out.validity.orientation ? "device" : "none";
    if (full) out.tracking_state = "tracked";
    else if (rotationOnly) out.tracking_state = "degraded";
    else if (status == TrackingStatus::Ok || status == TrackingStatus::Lost ||
             status == TrackingStatus::RecentlyLost) out.tracking_state = "lost";
    else out.tracking_state = status == TrackingStatus::NoImagesYet || status == TrackingStatus::NotInitialized ||
        status == TrackingStatus::SystemNotReady ? "initializing" : "unknown";
    return out;
}
std::optional<c1::Envelope> ViveObservationAdapter::admit(const BackendEvent& event) {
    const auto reject = [&](std::uint64_t& reason) -> std::optional<c1::Envelope> {
        ++rejected_;
        ++reason;
        return {};
    };
    if (event.kind != BackendEvent::Kind::Pose && event.kind != BackendEvent::Kind::DevicePaired &&
        event.kind != BackendEvent::Kind::DeviceUnpaired) return {};
    if (event.transportGeneration != context_.generation) return reject(diagnostics_.rejectGeneration);
    if (event.observedMonotonicNs < context_.epochNs) return reject(diagnostics_.rejectBeforeEpoch);
    if (!context_.spaces.contains(event.device.toString())) {
        diagnostics_.lastUnknownDevice = event.device.toString();
        return reject(diagnostics_.rejectUnknownDevice);
    }

    auto& stream = streams_[event.device.toString()];
    if (event.kind == BackendEvent::Kind::Pose) {
        if (!event.pose) return reject(diagnostics_.rejectMissingPose);
        if (event.pose->device.mac != event.device.mac || event.pose->device.slot != event.device.slot)
            return reject(diagnostics_.rejectPoseDeviceMismatch);
        if (stream.absent) return reject(diagnostics_.rejectAbsent);

        const auto& p = *event.pose;
        if (p.observedMonotonicNs != event.observedMonotonicNs)
            return reject(diagnostics_.rejectPoseTimestampMismatch);
        if (p.observedMonotonicNs <= stream.lastPoseNs)
            return reject(diagnostics_.rejectPoseNonMonotonic);

        if (stream.lastPoseNs >= 0) {
            const auto gapNs = p.observedMonotonicNs - stream.lastPoseNs;
            const auto rejectRf = [&](std::uint64_t& reason) {
                diagnostics_.lastRejectedRf = static_cast<int>(p.packetIndex);
                diagnostics_.lastRejectedGapNs = gapNs;
                return reject(reason);
            };
            constexpr std::int64_t maximumGapNs = 500000000;
            if (stream.candidate || gapNs > maximumGapNs) {
                // The pre-gap 8-bit phase is ambiguous after silence. Quarantine
                // an independent anchor; never advance admitted time/RF/sequence
                // until three coherent packets establish a forward chain. This
                // is deterministic serial resynchronization, not a quality gate.
                const auto seed = [&] {
                    if (!stream.candidate) {
                        ++diagnostics_.resyncPending;
                        ++diagnostics_.resyncStarted;
                    }
                    stream.candidate = Stream::Candidate{p.observedMonotonicNs, p.packetIndex, 1};
                };
                const auto reset = [&] {
                    stream.candidate.reset();
                    --diagnostics_.resyncPending;
                    ++diagnostics_.resyncReset;
                };
                if (!stream.candidate) {
                    seed();
                    return rejectRf(diagnostics_.rejectPoseGap);
                }
                auto& candidate = *stream.candidate;
                if (p.observedMonotonicNs <= candidate.lastNs) {
                    reset();
                    return reject(diagnostics_.rejectPoseNonMonotonic);
                }
                if (p.observedMonotonicNs - candidate.lastNs > maximumGapNs) {
                    ++diagnostics_.resyncReset;
                    seed();
                    return rejectRf(diagnostics_.rejectPoseGap);
                }
                const auto delta = static_cast<std::uint8_t>(p.packetIndex - candidate.rf);
                if (delta == 0) {
                    reset();
                    return rejectRf(diagnostics_.rejectDuplicateRf);
                }
                if (delta >= 128) {
                    reset();
                    return rejectRf(diagnostics_.rejectOldOrAmbiguousRf);
                }
                candidate.lastNs = p.observedMonotonicNs;
                candidate.rf = p.packetIndex;
                if (++candidate.packets < 3) return rejectRf(diagnostics_.rejectPoseGap);
                stream.candidate.reset();
                --diagnostics_.resyncPending;
                ++diagnostics_.resyncCompleted;
                // Publish only this current sample; quarantined predecessors
                // never reach the publisher or refresh an admitted receipt.
            } else {
                const auto delta = static_cast<std::uint8_t>(p.packetIndex - stream.rf);
                // Half-range serial arithmetic in the continuously admitted stream.
                if (delta == 0) return rejectRf(diagnostics_.rejectDuplicateRf);
                if (delta >= 128) return rejectRf(diagnostics_.rejectOldOrAmbiguousRf);
            }
        }
        if (stream.poseSequence == std::numeric_limits<std::int64_t>::max()) {
            restartReason_ = RestartReason::PoseSequenceExhausted;
            return reject(diagnostics_.rejectPoseSequenceExhausted);
        }

        auto out = mapPose(p, context_, stream.poseSequence++);
        stream.lastPoseNs = p.observedMonotonicNs;
        stream.rf = p.packetIndex;
        diagnostics_.lastAcceptedRf = static_cast<int>(p.packetIndex);
        ++diagnostics_.acceptedPose;
        return out;
    }

    if (stream.stateSequence == std::numeric_limits<std::int64_t>::max()) {
        restartReason_ = RestartReason::StateSequenceExhausted;
        return reject(diagnostics_.rejectStateSequenceExhausted);
    }
    if (event.observedMonotonicNs <= stream.lastStateNs)
        return reject(diagnostics_.rejectStateNonMonotonic);
    if (event.kind == BackendEvent::Kind::DevicePaired && stream.absent) {
        // The service always starts a new transport session. Only the explicit
        // reconnect policy may preserve this exact configured device/map revision.
        restartReason_ = RestartReason::ConfiguredDevicePairedAfterAbsent;
        return reject(diagnostics_.rejectRepairAfterAbsent);
    }

    c1::ObservationDeviceState out;
    header(out, event.device, context_, stream.stateSequence++, event.observedMonotonicNs);
    stream.lastStateNs = event.observedMonotonicNs;
    const bool absent = event.kind == BackendEvent::Kind::DeviceUnpaired;
    // Do not reset pose counters or duplicate history on metadata events.
    stream.absent = absent;
    if (absent && stream.candidate) {
        stream.candidate.reset();
        --diagnostics_.resyncPending;
        ++diagnostics_.resyncReset;
    }
    out.presence = absent ? "absent" : "present";
    out.tracking_state = absent ? "disconnected" : "initializing";
    ++diagnostics_.acceptedState;
    return out;
}
}
