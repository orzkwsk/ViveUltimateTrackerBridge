#include "vut/service_runtime.hpp"

#include <stdexcept>

namespace vut {
namespace {
using namespace control;
struct FlagGuard {
    bool& flag;
    explicit FlagGuard(bool& value) : flag(value) { flag = true; }
    ~FlagGuard() { flag = false; }
};
class ExistingObservationStage final : public ServiceObservationStage {
    ViveObservationAdapter adapter_;
public:
    explicit ExistingObservationStage(ObservationContext c) : adapter_(std::move(c)) {}
    std::optional<monaka::protocol::v2::Envelope> admit(const BackendEvent& e) override { return adapter_.admit(e); }
    bool restartRequired() const override { return adapter_.restartRequired(); }
    bool coordinateContinuityUncertain() const override { return adapter_.coordinateContinuityUncertain(); }
    std::uint64_t rejected() const override { return adapter_.rejected(); }
    const ObservationAdmissionDiagnostics& diagnostics() const override { return adapter_.diagnostics(); }
};
TrackingState tracking(TrackingStatus status) {
    switch (status) {
    case TrackingStatus::Ok: return TrackingState::Tracking;
    case TrackingStatus::Lost:
    case TrackingStatus::RecentlyLost: return TrackingState::Lost;
    case TrackingStatus::NoImagesYet:
    case TrackingStatus::NotInitialized:
    case TrackingStatus::SystemNotReady: return TrackingState::Initializing;
    default: return TrackingState::Unknown;
    }
}
bool matchingPose(const BackendEvent& event) {
    return event.pose && event.pose->device.mac == event.device.mac && event.pose->device.slot == event.device.slot;
}
}
ServiceRuntime::ServiceRuntime(Backend backend, ServiceRuntimeConfig config, ServiceRuntimeDependencies deps)
    : backend_(std::move(backend)), config_(std::move(config)), dependencies_(std::move(deps)),
      control_([this] { return eventTime_ ? *eventTime_ : dependencies_.clock(); }, config_.registryLimits),
      coordinator_(config_.reconnectRevision), owner_(std::this_thread::get_id()) {
    if (!dependencies_.clock || !dependencies_.advanceRevision || !dependencies_.makeSessionId || !dependencies_.send)
        throw std::invalid_argument("incomplete service runtime dependencies");
    if (config_.backend.diagnosticSlamQueries)
        throw std::invalid_argument("passive service runtime cannot enable active SLAM queries");
    if (backend_.running() || backend_.pairingActive())
        throw std::invalid_argument("service runtime requires an unstarted passive Backend");
    if (!dependencies_.observationFactory)
        dependencies_.observationFactory = [](ObservationContext c) { return std::make_unique<ExistingObservationStage>(std::move(c)); };
    backend_.setEventCallback([this](const BackendEvent& e) { dispatch(e); });
    callbackRegistered_ = true;
    ++statistics_.callbackRegistrations;
}
ServiceRuntime::~ServiceRuntime() {
    if (owner_ != std::this_thread::get_id()) std::terminate();
    shutdownImpl();
}
void ServiceRuntime::checkOwner() const {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("service runtime owner thread mismatch");
}
void ServiceRuntime::checkWorkOwner() const {
    checkOwner();
    if (working_ || dispatching_) throw std::logic_error("recursive service runtime work");
}
void ServiceRuntime::fault(ServiceRuntimeFailure stage, control::Outcome outcome) {
    if (!deferredFault_) { failure_ = stage; outcome_ = outcome; }
    deferredFault_ = true;
}
void ServiceRuntime::checkModel(control::Outcome outcome) {
    if (outcome.code == control::ErrorCode::InternalConsistencyError ||
        outcome.reason == control::Reason::HistoryFull)
        fault(ServiceRuntimeFailure::Registry, outcome);
}
control::Outcome ServiceRuntime::start() {
    checkWorkOwner();
    FlagGuard work(working_);
    if (state_ == ServiceRuntimeState::Faulted) return outcome_;
    if (closed_) return {control::ErrorCode::Unsupported, control::Reason::CapabilityUnavailable};
    if (accepting_) return {control::ErrorCode::Busy, control::Reason::InvalidTransition};
    // Old session dependents remain alive throughout stop; the stable dispatcher
    // targets this runtime and refuses events while no session is accepting.
    session_.reset();
    try {
        if (!backend_.start(config_.backend)) {
            retireTransport(ServiceRuntimeState::TransportLost);
            if (deferredFault_) { shutdownImpl(); return outcome_; }
            outcome_ = {control::ErrorCode::TransportUnavailable, control::Reason::TransportBoundary};
            return outcome_;
        }
        epochs_.transportOpened(backend_.transportGeneration());
        sessionInfo_ = coordinator_.begin(
            {config_.observation.sourceId, config_.observation.spaces}, nextCause_,
            backend_.transportGeneration(), dependencies_.advanceRevision,
            dependencies_.makeSessionId, dependencies_.clock);
        auto context = config_.observation;
        context.sessionId = sessionInfo_->sessionId; context.epochNs = sessionInfo_->epochNs;
        context.generation = sessionInfo_->transportGeneration; context.revision = sessionInfo_->revision;
        session_ = std::make_unique<Session>();
        session_->adapter = dependencies_.observationFactory(context);
        if (!session_->adapter) throw std::runtime_error("null service observation stage");
        const auto epoch = context.epochNs;
        session_->publisher = std::make_unique<ObservationPublisher>(context.sessionId, dependencies_.send,
            [this, epoch] { return dependencies_.clock() - epoch; }, config_.publisherCapacity);
        const auto result = control_.beginTransport(backend_.transportGeneration());
        checkModel(result);
        if (!result.ok()) {
            fault(ServiceRuntimeFailure::Registry, result);
            shutdownImpl(); return outcome_;
        }
        current_ = {}; lastEventNs_ = context.epochNs;
        outcome_ = {}; failure_ = ServiceRuntimeFailure::None;
        state_ = ServiceRuntimeState::Running; accepting_ = true;
        return {};
    } catch (...) {
        fault(ServiceRuntimeFailure::Start, {control::ErrorCode::OperationFailed, control::Reason::PrimaryFailure});
        shutdownImpl(); return outcome_;
    }
}
std::optional<control::RuntimeHandle> ServiceRuntime::receive(const BackendEvent& e) {
    using namespace control;
    const auto slot = e.device.slot;
    if (slot < 0 || slot >= static_cast<int>(current_.size())) return {};
    auto& current = current_[slot];
    const auto instance = control_.scope().serviceInstance;
    const auto exact = current && current->rf == e.device.mac && e.observedMonotonicNs >= current->boundAtNs;
    if (e.kind == BackendEvent::Kind::DeviceUnpaired) {
        if (exact) { checkModel(control_.disconnect(instance, current->handle)); current.reset(); }
        return {};
    }
    if (e.kind == BackendEvent::Kind::DevicePaired || e.kind == BackendEvent::Kind::Pose) {
        // A malformed pose can refresh receipt on an already-current address,
        // but cannot discover a new actionable binding from unbound evidence.
        if (e.kind == BackendEvent::Kind::Pose && !exact &&
            (!matchingPose(e) || e.pose->observedMonotonicNs != e.observedMonotonicNs)) return {};
        if (e.kind == BackendEvent::Kind::DevicePaired || !exact) {
            const auto bound = control_.bind(control_.scope(), e.device.mac, slot,
                e.kind == BackendEvent::Kind::DevicePaired ? ConnectionState::Connected : ConnectionState::Observed);
            checkModel(bound.outcome);
            // Cache is current address routing only. Never repair a registry
            // failure, preserve a retired token, or infer persistent RF identity.
            for (auto& entry : current_) if (entry) {
                const auto result = control_.validate(instance, entry->handle);
                checkModel(result);
                if (!result.ok()) entry.reset();
            }
            if (!bound.ok() || !bound.value) return {};
            current = Current{e.device.mac, *bound.value, e.observedMonotonicNs};
        }
        const auto handle = current->handle;
        if (e.kind == BackendEvent::Kind::Pose)
            checkModel(control_.pose(instance, handle, matchingPose(e) ? tracking(e.pose->trackingStatus) : TrackingState::Unknown,
                matchingPose(e) ? e.pose->trackingStatusRaw : 0xff, Admission::Unknown));
        return handle;
    }
    if (exact) checkModel(control_.metadata(instance, current->handle));
    return {};
}
void ServiceRuntime::dispatch(const BackendEvent& e) noexcept {
    if (!accepting_ || !session_ || deferredFault_) return;
    if (dispatching_) { fault(ServiceRuntimeFailure::Dispatcher, {control::ErrorCode::InternalConsistencyError, control::Reason::InvalidTransition}); return; }
    FlagGuard dispatching(dispatching_);
    try {
        checkOwner();
        const bool sessionProvenance = e.transportGeneration == sessionInfo_->transportGeneration &&
            e.observedMonotonicNs >= sessionInfo_->epochNs;
        const bool orderedTime = e.observedMonotonicNs >= lastEventNs_ && e.observedMonotonicNs <= dependencies_.clock();
        const bool provenance = sessionProvenance && orderedTime;
        std::optional<control::RuntimeHandle> handle;
        if (provenance) {
            // Both reception and admission feedback use the immutable Backend
            // receive timestamp, not the later post-feature-exchange/poll time.
            eventTime_ = e.observedMonotonicNs;
            lastEventNs_ = e.observedMonotonicNs;
            epochs_.observe(e); // evidence FIRST, before registry/admission
            handle = receive(e); // registry FIRST, before actual adapter call
            std::array<std::optional<DeviceId>, 5> bindings{};
            // receive() already reconciles the five routing entries after bind;
            // faults retain no evidence. Do not rescan registry history per ACK.
            if (!deferredFault_)
                for (std::size_t i = 0; i < current_.size(); ++i) if (current_[i])
                    bindings[i] = DeviceId{current_[i]->rf, static_cast<int>(i)};
            epochs_.retainBindings(e.transportGeneration, bindings);
        } else {
            ++statistics_.ignoredProvenance;
            // Retired generation/epoch inputs can reach the adapter's existing
            // rejection counters. Never contaminate its current stream with an
            // impossible future or globally retired timestamp in this session.
            if (sessionProvenance && !orderedTime) return;
        }
        const auto before = session_->adapter->diagnostics().rejectUnknownDevice;
        auto observation = session_->adapter->admit(e); // unchanged real admission
        if (provenance && handle && e.kind == BackendEvent::Kind::Pose && !deferredFault_) {
            const auto admission = observation && std::holds_alternative<monaka::protocol::v2::TrackerObservation>(*observation)
                ? control::Admission::Admitted
                : session_->adapter->diagnostics().rejectUnknownDevice != before
                    ? control::Admission::Unconfigured : control::Admission::Rejected;
            checkModel(control_.pose(control_.scope().serviceInstance, *handle,
                matchingPose(e) ? tracking(e.pose->trackingStatus) : control::TrackingState::Unknown,
                matchingPose(e) ? e.pose->trackingStatusRaw : 0xff, admission));
        }
        if (observation && provenance) session_->publisher->enqueue(std::move(*observation));
        if (config_.diagnostics && diagnosticCount_ < diagnosticEvents_.size()) {
            BackendEvent copy{e.kind, e.device, e.pose, e.text.substr(0, 512), e.observedMonotonicNs, e.transportGeneration};
            diagnosticEvents_[diagnosticCount_++] = std::move(copy);
        }
    } catch (...) {
        // No Backend action, filesystem/logging, client callback or recursive poll
        // occurs here. Let the original poll finish before ordered teardown.
        fault(ServiceRuntimeFailure::Dispatcher, {control::ErrorCode::OperationFailed, control::Reason::PrimaryFailure});
    }
    eventTime_.reset();
}
void ServiceRuntime::rememberStatistics() {
    if (!session_) return;
    try {
        if (session_->adapter) {
            statistics_.admission = session_->adapter->diagnostics();
            statistics_.rejected = session_->adapter->rejected();
        }
        if (session_->publisher) {
            statistics_.publication = session_->publisher->counters();
            statistics_.pendingPublication = session_->publisher->pending();
        }
    } catch (...) {
        fault(ServiceRuntimeFailure::Dispatcher, {control::ErrorCode::OperationFailed, control::Reason::PrimaryFailure});
    }
}
void ServiceRuntime::retireTransport(ServiceRuntimeState state) {
    accepting_ = false;
    epochs_.invalidateAll(state == ServiceRuntimeState::RestartRequired
        ? ConnectionEpochReason::ObservationSessionRestarted : ConnectionEpochReason::TransportLost);
    checkModel(control_.loseTransport()); // invalidate BEFORE runtime-owned stop
    current_ = {};
    backend_.stop(); // Backend read-failure path has already stopped internally
    rememberStatistics();
    if (session_ && session_->publisher) session_->publisher->clear();
    statistics_.pendingPublication = 0;
    state_ = state;
}
bool ServiceRuntime::pollOnce() {
    checkWorkOwner();
    FlagGuard work(working_);
    if (!accepting_ || state_ != ServiceRuntimeState::Running) return false;
    try {
        const bool read = backend_.pollOnce();
        if (deferredFault_) { shutdownImpl(); return false; }
        if (!read) {
            nextCause_ = SessionStartCause::TransportReconnect;
            retireTransport(ServiceRuntimeState::TransportLost);
            if (deferredFault_) shutdownImpl();
            else outcome_ = {control::ErrorCode::TransportUnavailable, control::Reason::TransportBoundary};
            return false;
        }
        try { session_->publisher->flush(); }
        catch (...) {
            fault(ServiceRuntimeFailure::Publisher, {control::ErrorCode::OperationFailed, control::Reason::PrimaryFailure});
            shutdownImpl(); return false;
        }
        if (session_->adapter->restartRequired()) {
            nextCause_ = session_->adapter->coordinateContinuityUncertain()
                ? SessionStartCause::ContinuityUncertain : SessionStartCause::TransportReconnect;
            checkModel(control_.restartObservationSession());
            retireTransport(ServiceRuntimeState::RestartRequired);
            if (deferredFault_) shutdownImpl();
            return false;
        }
        return true;
    } catch (...) {
        fault(ServiceRuntimeFailure::Poll, {control::ErrorCode::OperationFailed, control::Reason::PrimaryFailure});
        shutdownImpl(); return false;
    }
}
void ServiceRuntime::shutdown() {
    checkWorkOwner();
    shutdownImpl();
}
void ServiceRuntime::shutdownImpl() {
    if (closed_) return;
    accepting_ = false; // no new work; public API cannot be called recursively
    epochs_.invalidateAll(ConnectionEpochReason::RuntimeShutdown);
    checkModel(control_.loseTransport());
    current_ = {};
    if (callbackRegistered_) {
        backend_.setEventCallback({}); callbackRegistered_ = false;
        ++statistics_.callbackClears;
    }
    try { backend_.stop(); }
    catch (...) { fault(ServiceRuntimeFailure::Poll, {control::ErrorCode::OperationFailed, control::Reason::PrimaryFailure}); }
    rememberStatistics();
    if (session_ && session_->publisher) session_->publisher->clear();
    statistics_.pendingPublication = 0;
    session_.reset(); // callback clear -> Backend stop -> dependent destruction
    closed_ = true;
    state_ = deferredFault_ ? ServiceRuntimeState::Faulted : ServiceRuntimeState::Stopped;
}
ServiceRuntimeStatistics ServiceRuntime::statistics() const {
    checkOwner();
    auto out = statistics_;
    if (session_ && session_->adapter) { out.admission = session_->adapter->diagnostics(); out.rejected = session_->adapter->rejected(); }
    if (session_ && session_->publisher) { out.publication = session_->publisher->counters(); out.pendingPublication = session_->publisher->pending(); }
    return out;
}
std::shared_ptr<const ServiceRuntimeSnapshot> ServiceRuntime::snapshot(control::FreshnessPolicy policy) const {
    checkOwner();
    auto out = std::make_shared<ServiceRuntimeSnapshot>();
    out->state = state_; out->failure = failure_; out->outcome = outcome_; out->backendError = backend_.lastError();
    out->observationSession = sessionInfo_; out->control = control_.snapshot(policy);
    out->statistics = statistics();
    out->connectionEpochs = epochs_.snapshot();
    return out;
}
std::vector<BackendEvent> ServiceRuntime::takeDiagnostics() {
    checkOwner();
    std::vector<BackendEvent> out;
    out.reserve(diagnosticCount_);
    for (std::size_t i = 0; i < diagnosticCount_; ++i) {
        out.push_back(std::move(*diagnosticEvents_[i])); diagnosticEvents_[i].reset();
    }
    diagnosticCount_ = 0;
    return out;
}
control::Outcome ServiceRuntime::validateHandle(const control::ServiceInstance& instance, const control::RuntimeHandle& handle) {
    checkWorkOwner();
    const auto result = control_.validate(instance, handle);
    checkModel(result);
    if (deferredFault_) shutdownImpl();
    return result;
}
} // namespace vut
