#pragma once

#include "vut/backend.hpp"
#include "vut/detail/info_epoch.hpp"
#include "vut/observation_adapter.hpp"
#include "vut/observation_publisher.hpp"
#include "vut/service_control.hpp"
#include "vut/service_session.hpp"
#include "vut/pairing.hpp"
#include "vut/setup.hpp"
#include <array>
#include <thread>

namespace vut {

// Component seam for hardware-free ordering/exception tests. The production
// implementation delegates unchanged to ViveObservationAdapter. Not a client
// callback, controller, command queue or provider interface.
class ServiceObservationStage {
public:
    virtual ~ServiceObservationStage() = default;
    virtual std::optional<monaka::protocol::v2::Envelope> admit(const BackendEvent&) = 0;
    virtual bool restartRequired() const = 0;
    virtual bool coordinateContinuityUncertain() const = 0;
    virtual std::uint64_t rejected() const = 0;
    virtual const ObservationAdmissionDiagnostics& diagnostics() const = 0;
};
struct ServiceRuntimeDependencies {
    control::MonotonicClock clock;
    std::function<std::uint32_t()> advanceRevision;
    std::function<std::string()> makeSessionId;
    ObservationPublisher::Send send;
    std::function<std::unique_ptr<ServiceObservationStage>(ObservationContext)> observationFactory;
};
struct ServiceRuntimeConfig {
    ObservationContext observation;
    ReconnectRevisionPolicy reconnectRevision = ReconnectRevisionPolicy::Strict;
    BackendConfig backend = [] {
        BackendConfig config;
        config.automaticVendorActionPolicy = AutomaticVendorActionPolicy::ObserveOnly;
        return config;
    }(); // shared-open retained; Legacy is an explicit compatibility selection
    control::RegistryLimits registryLimits;
    std::size_t publisherCapacity = 512;
    bool diagnostics = false;
    // Production leaves this false until concurrent RF pairing is qualified.
    bool allowExplicitPairing = false;
};
enum class ServiceRuntimeState { Stopped, Running, TransportLost, RestartRequired, Faulted };
enum class ServiceRuntimeFailure { None, Start, Registry, Dispatcher, Poll, Publisher };
struct ServiceRuntimeStatistics {
    ObservationAdmissionDiagnostics admission;
    ObservationPublisher::Counters publication;
    std::uint64_t rejected = 0, ignoredProvenance = 0, rejectedUnboundPose = 0;
    std::uint64_t callbackRegistrations = 0, callbackClears = 0;
    std::size_t pendingPublication = 0;
};
struct ServiceRuntimeSnapshot {
    ServiceRuntimeState state = ServiceRuntimeState::Stopped;
    ServiceRuntimeFailure failure = ServiceRuntimeFailure::None;
    control::Outcome outcome;
    std::string backendError;
    std::optional<ServiceSession> observationSession;
    std::shared_ptr<const control::ControlSnapshot> control;
    ServiceRuntimeStatistics statistics;
    // Authoritative production evidence, reconciled with registry ownership.
    std::vector<ConnectionEpochSnapshot> connectionEpochs;
    bool pairingAvailable = false, identifying = false;
    setup::PairingResult pairing;
    std::vector<std::string> identifyCandidates;
    std::array<std::optional<monaka::protocol::v2::TrackerObservation>, 5> admittedPoses;
    static constexpr bool identityAcquisition = false;
};
struct ServiceRuntimeTestAccess;

// Construct, start, poll, read snapshots and destroy on the same owner thread.
// Future workers may only read already-detached snapshots. No Backend reference
// or mutable model escapes this boundary. The runtime cannot be moved/copied.
class ServiceRuntime {
public:
    ServiceRuntime(Backend, ServiceRuntimeConfig, ServiceRuntimeDependencies);
    ~ServiceRuntime();
    ServiceRuntime(const ServiceRuntime&) = delete;
    ServiceRuntime& operator=(const ServiceRuntime&) = delete;
    control::Outcome start();
    bool pollOnce();
    void shutdown();
    std::shared_ptr<const ServiceRuntimeSnapshot> snapshot(control::FreshnessPolicy = {}) const;
    ServiceRuntimeStatistics statistics() const;
    // Drain bounded diagnostics after poll, never log or invoke clients in dispatch.
    std::vector<BackendEvent> takeDiagnostics();
    control::Outcome validateHandle(const control::ServiceInstance&, const control::RuntimeHandle&);
    control::Outcome beginPairing(int selector, std::chrono::seconds timeout);
    control::Outcome cancelPairing();
    control::Outcome beginIdentify();
private:
    struct Current {
        control::RfAddress rf;
        control::RuntimeHandle handle;
        std::int64_t boundAtNs;
    };
    struct Session {
        std::unique_ptr<ServiceObservationStage> adapter;
        std::unique_ptr<ObservationPublisher> publisher;
    };
    Backend backend_; // destroyed last, only after callback has been cleared
    setup::PairingController pairing_;
    setup::Devices managementDevices_;
    std::array<std::optional<monaka::protocol::v2::TrackerObservation>, 5> admittedPoses_;
    ServiceRuntimeConfig config_;
    ServiceRuntimeDependencies dependencies_;
    std::optional<std::int64_t> eventTime_;
    control::ServiceControlModel control_;
    SessionRevisionCoordinator coordinator_;
    detail::ConnectionEpochTracker epochs_;
    std::thread::id owner_;
    std::unique_ptr<Session> session_;
    std::optional<ServiceSession> sessionInfo_;
    std::array<std::optional<Current>, 5> current_;
    std::array<std::optional<BackendEvent>, 8> diagnosticEvents_;
    std::size_t diagnosticCount_ = 0;
    ServiceRuntimeStatistics statistics_;
    ServiceRuntimeState state_ = ServiceRuntimeState::Stopped;
    ServiceRuntimeFailure failure_ = ServiceRuntimeFailure::None;
    control::Outcome outcome_;
    SessionStartCause nextCause_ = SessionStartCause::InitialStart;
    std::int64_t lastEventNs_ = 0;
    bool accepting_ = false, callbackRegistered_ = false, closed_ = false;
    bool deferredFault_ = false;
    bool working_ = false, dispatching_ = false;
    void checkOwner() const;
    void checkWorkOwner() const;
    void shutdownImpl();
    void dispatch(const BackendEvent&) noexcept;
    std::optional<control::RuntimeHandle> receive(const BackendEvent&);
    void checkModel(control::Outcome);
    void fault(ServiceRuntimeFailure, control::Outcome);
    void retireTransport(ServiceRuntimeState);
    void rememberStatistics();
    friend struct ServiceRuntimeTestAccess;
};
} // namespace vut
