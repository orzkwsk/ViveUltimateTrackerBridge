#include "vut/backend.hpp"
#include "vut/backend_transport.hpp"
#include "vut/loss_diagnostics.hpp"
#include "vut/protocol.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <cwctype>
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "setupapi.lib")
#endif

namespace vut {
namespace {

std::int64_t monotonicNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

#ifdef _WIN32
class WinHidDevice final : public detail::BackendTransport {
public:
    ~WinHidDevice() { close(); }

    bool open(std::uint16_t vid, std::uint16_t pid, bool exclusive, std::string& error) override {
        GUID hidGuid{};
        HidD_GetHidGuid(&hidGuid);
        HDEVINFO info = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (info == INVALID_HANDLE_VALUE) {
            error = "SetupDiGetClassDevsW failed";
            return false;
        }

        std::wstring fallback;
        bool matchingPathWasBusy = false;
        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA iface{};
            iface.cbSize = sizeof(iface);
            if (!SetupDiEnumDeviceInterfaces(info, nullptr, &hidGuid, index, &iface)) break;

            DWORD required = 0;
            SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &required, nullptr);
            if (!required) continue;
            std::vector<std::byte> storage(required);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, required, nullptr, nullptr)) continue;

            HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OVERLAPPED, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                std::wstring path = detail->DevicePath;
                std::transform(path.begin(), path.end(), path.begin(), ::towlower);
                const std::wstring identity = L"vid_0bb4&pid_0350";
                if (vid == protocol::kVendorIdVive && pid == protocol::kProductIdUltimateTrackerDongle &&
                    path.find(identity) != std::wstring::npos) matchingPathWasBusy = true;
                continue;
            }
            HIDD_ATTRIBUTES attr{};
            attr.Size = sizeof(attr);
            const bool match = HidD_GetAttributes(h, &attr) && attr.VendorID == vid && attr.ProductID == pid;
            CloseHandle(h);
            if (!match) continue;

            std::wstring path = detail->DevicePath;
            std::wstring lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            if (lower.find(L"mi_00") != std::wstring::npos) {
                fallback = path;
                break;
            }
            if (fallback.empty()) fallback = path;
        }
        SetupDiDestroyDeviceInfoList(info);

        if (fallback.empty()) {
            error = matchingPathWasBusy
                ? "VIVE Ultimate Tracker dongle HID interface is busy; stop vut_service and VIVE Hub / VIVE Streaming Hub"
                : "VIVE Ultimate Tracker dongle HID interface not found (VID 0x0bb4 PID 0x0350)";
            return false;
        }

        handle_ = CreateFileW(fallback.c_str(), GENERIC_READ | GENERIC_WRITE,
                              exclusive ? 0 : (FILE_SHARE_READ | FILE_SHARE_WRITE), nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            error = exclusive
                ? "exclusive dongle HID open failed; stop vut_service and VIVE Hub / VIVE Streaming Hub"
                : "CreateFileW failed for dongle HID interface";
            return false;
        }

        PHIDP_PREPARSED_DATA preparsed = nullptr;
        HIDP_CAPS caps{};
        if (HidD_GetPreparsedData(handle_, &preparsed)) {
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
                featureLength_ = caps.FeatureReportByteLength ? caps.FeatureReportByteLength : 65;
                inputLength_ = std::max<std::size_t>(caps.InputReportByteLength, 0x400);
            }
            HidD_FreePreparsedData(preparsed);
        }
        return true;
    }

    void close() override {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(handle_, nullptr);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    std::size_t featureLength() const noexcept override { return featureLength_; }

    bool setFeature(std::span<const std::uint8_t> data) override {
        std::vector<std::uint8_t> buffer(featureLength_, 0);
        std::copy_n(data.begin(), std::min(data.size(), buffer.size()), buffer.begin());
        return HidD_SetFeature(handle_, buffer.data(), static_cast<ULONG>(buffer.size())) != FALSE;
    }

    std::vector<std::uint8_t> getFeature() override {
        std::vector<std::uint8_t> buffer(featureLength_, 0);
        if (!HidD_GetFeature(handle_, buffer.data(), static_cast<ULONG>(buffer.size()))) return {};
        return buffer;
    }

    bool readFailed() const noexcept override { return readFailed_; }

    std::vector<std::uint8_t> read(int timeoutMs) override {
        readFailed_ = false;
        std::vector<std::uint8_t> buffer(inputLength_, 0);
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) { readFailed_ = true; return {}; }

        DWORD readBytes = 0;
        const BOOL immediate = ReadFile(handle_, buffer.data(), static_cast<DWORD>(buffer.size()), &readBytes, &ov);
        if (!immediate && GetLastError() != ERROR_IO_PENDING) {
            readFailed_ = true;
            CloseHandle(ov.hEvent);
            return {};
        }
        if (!immediate) {
            const DWORD wait = WaitForSingleObject(ov.hEvent, static_cast<DWORD>(std::max(timeoutMs, 0)));
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(handle_, &ov);
                // OVERLAPPED and buffer must live until cancellation completes.
                GetOverlappedResult(handle_, &ov, &readBytes, TRUE);
                readFailed_ = wait != WAIT_TIMEOUT;
                CloseHandle(ov.hEvent);
                return {};
            }
            if (!GetOverlappedResult(handle_, &ov, &readBytes, FALSE)) {
                readFailed_ = true;
                CloseHandle(ov.hEvent);
                return {};
            }
        }
        CloseHandle(ov.hEvent);
        buffer.resize(readBytes);
        return buffer;
    }

private:
    bool readFailed_ = false;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    std::size_t featureLength_ = 65;
    std::size_t inputLength_ = 0x400;
};
#endif

std::unique_ptr<detail::BackendTransport> makePlatformTransport() {
#ifdef _WIN32
    return std::make_unique<WinHidDevice>();
#else
    return {};
#endif
}

} // namespace

struct Backend::Impl {
    explicit Impl(std::unique_ptr<detail::BackendTransport> value)
        : hid(std::move(value)) {}

    struct CommandResult {
        FeatureExchangeEvidence evidence;
        std::vector<std::uint8_t> response;
    };

    BackendConfig config;
    EventCallback callback;
    mutable std::mutex mutex;
    std::map<int, TrackerState> states;
    std::map<int, DeviceId> devices;
    std::map<int, std::uint16_t> buttons;
    detail::LossDiagnostics lossDiagnostics;
    int hostSlot = -1;
    bool isRunning = false;
    std::uint64_t generation = 0;
    std::int64_t receivedNs = 0;
    std::string error;
    std::unique_ptr<detail::BackendTransport> hid;
    bool pairingMayBeEnabled = false;
    bool pairingEnableExchangeMatched = false;
    int pairingSlot = -1;
    FeatureExchangeEvidence lastExchange, enableExchange;
    std::optional<FeatureExchangeEvidence> lastFailedExchange;
    PairingCleanupEvidence cleanup;

    void emit(BackendEvent event) {
        event.observedMonotonicNs = receivedNs;
        event.transportGeneration = generation;
        if (callback) callback(event);
    }

    void recordExchange(const FeatureExchangeEvidence& evidence) {
        lastExchange = evidence;
        if (evidence.error == FeatureExchangeError::None) return;
        // Keep failures even when a later configureOnPair send matches. No
        // raw response bytes or exception.what() enter diagnostics.
        lastFailedExchange = evidence;
        switch (evidence.error) {
        case FeatureExchangeError::TransportUnavailable: error = "feature transport unavailable"; break;
        case FeatureExchangeError::PayloadBuildFailed: error = "feature payload build failed"; break;
        case FeatureExchangeError::WriteRejected: error = "feature write rejected"; break;
        case FeatureExchangeError::WriteException: error = "feature write raised an exception; write outcome unknown"; break;
        case FeatureExchangeError::ResponseException: error = "feature response raised an exception after accepted write"; break;
        case FeatureExchangeError::NoResponse: error = "no feature response observed"; break;
        case FeatureExchangeError::WrongCommandOnly: error = "only unrelated feature responses observed"; break;
        case FeatureExchangeError::MalformedResponse:
            error = evidence.matchingMalformedResponses ? "matching feature response malformed: " : "feature response malformed: ";
            switch (evidence.framingError) {
            case FeatureFramingError::TooShort: error += "shorter than header"; break;
            case FeatureFramingError::InvalidDeclaredLength: error += "invalid declared length"; break;
            case FeatureFramingError::DeclaredLengthExceedsBuffer: error += "declared response length exceeds buffer"; break;
            default: break;
            }
            break;
        default: break;
        }
    }

    CommandResult sendCommandDetailed(std::uint8_t command, std::span<const std::uint8_t> payload) {
        CommandResult result;
        auto& evidence = result.evidence;
        evidence.command = command;
        evidence.transportGeneration = generation;
        lastExchange = evidence; // never copy stale metadata after a pre-write exception
        const auto finish = [&]() { recordExchange(evidence); return result; };
        if (!hid) { evidence.error = FeatureExchangeError::TransportUnavailable; return finish(); }
        auto packet = protocol::buildFeatureCommand(command, payload, hid->featureLength());
        if (packet.empty()) { evidence.error = FeatureExchangeError::PayloadBuildFailed; return finish(); }
        evidence.write = FeatureWriteState::OutcomeUnknown;
        try {
            if (!hid->setFeature(packet)) {
                evidence.write = FeatureWriteState::Failed;
                evidence.error = FeatureExchangeError::WriteRejected;
                return finish();
            }
        } catch (...) {
            evidence.error = FeatureExchangeError::WriteException;
            recordExchange(evidence);
            throw std::runtime_error(error); // do not forward transport payload/identifier text
        }
        evidence.write = FeatureWriteState::AcceptedByTransportApi;
        for (int i = 0; i < 10; ++i) {
            ++evidence.responseAttempts;
            std::vector<std::uint8_t> response;
            try { response = hid->getFeature(); }
            catch (...) {
                evidence.error = FeatureExchangeError::ResponseException;
                recordExchange(evidence);
                throw std::runtime_error(error);
            }
            if (response.empty()) {
                ++evidence.emptyResponses;
                evidence.lastObservation = FeatureResponseObservation::Empty;
                continue;
            }
            FeatureFramingError framing = FeatureFramingError::None;
            if (response.size() < 5) framing = FeatureFramingError::TooShort;
            else if (response[2] < 4) framing = FeatureFramingError::InvalidDeclaredLength;
            else if (static_cast<std::size_t>(response[2]) + 1 > response.size())
                framing = FeatureFramingError::DeclaredLengthExceedsBuffer;
            if (framing != FeatureFramingError::None) {
                ++evidence.malformedResponses;
                if (response.size() >= 2 && response[1] == command) ++evidence.matchingMalformedResponses;
                evidence.response = FeatureResponseState::Malformed;
                evidence.lastObservation = FeatureResponseObservation::Malformed;
                evidence.framingError = framing;
                // Preserve the previous immediate failure for a matching bad
                // declaration, and reject truncation instead of clipping it.
                if (response.size() >= 5 && response[1] == command) {
                    evidence.error = FeatureExchangeError::MalformedResponse;
                    return finish();
                }
                continue;
            }
            if (response[1] != command) {
                ++evidence.wrongCommandResponses;
                if (!evidence.malformedResponses) evidence.response = FeatureResponseState::WrongCommandOnly;
                evidence.lastObservation = FeatureResponseObservation::WrongCommand;
                continue;
            }
            const std::size_t declared = response[2];
            const std::size_t len = declared - 4;
            result.response.assign(response.begin() + 5, response.begin() + 5 + len);
            evidence.response = FeatureResponseState::Matched;
            evidence.lastObservation = FeatureResponseObservation::Matched;
            return finish();
        }
        evidence.error = evidence.malformedResponses ? FeatureExchangeError::MalformedResponse :
                         evidence.wrongCommandResponses ? FeatureExchangeError::WrongCommandOnly :
                         FeatureExchangeError::NoResponse;
        return finish();
    }

    std::vector<std::uint8_t> sendCommand(std::uint8_t command, std::span<const std::uint8_t> payload) {
        return sendCommandDetailed(command, payload).response;
    }

    void setRxPowerSaving(bool enabled, int slot) {
        const auto payload = protocol::buildRfBehaviorPayload(protocol::kRfBehaviorRxPowerSaving, enabled, slot);
        if (!payload) return;
        (void)sendCommand(protocol::kCmdRequestRfChangeBehavior, *payload);
    }

    bool sendAck(const DeviceId& device, const std::string& ack) {
        auto tx = protocol::buildAckTx(device, ack);
        if (tx.empty()) {
            FeatureExchangeEvidence evidence;
            evidence.command = protocol::kCmdTx;
            evidence.transportGeneration = generation;
            evidence.error = FeatureExchangeError::PayloadBuildFailed;
            recordExchange(evidence);
            return false;
        }
        // Host exchange result only. Incoming tracker ACKs are a separate
        // event stream; configureOnPair retains its original send order.
        return sendCommandDetailed(protocol::kCmdTx, tx).evidence.matched();
    }

    void configureOnPair(const DeviceId& device) {
        if (!config.configureTrackingOnPair || device.slot < 0) return;
        sendAck(device, "ARI1");
        sendAck(device, "ATM-1");

        if (hostSlot < 0 || hostSlot == device.slot) {
            hostSlot = device.slot;
            sendAck(device, "Wc" + config.wifiCountry);
            sendAck(device, "ATH1");
            sendAck(device, "AWH1");
            sendAck(device, "ANI0");
            sendAck(device, "ATM20");
        } else {
            sendAck(device, "ATH0");
            sendAck(device, "AWH0");
            sendAck(device, "ANI" + std::to_string(device.slot));
            sendAck(device, "ATM11");
        }
    }
};

Backend::Backend() : Backend(makePlatformTransport()) {}
Backend::Backend(std::unique_ptr<detail::BackendTransport> transport)
    : impl_(std::make_unique<Impl>(std::move(transport))) {}
Backend::~Backend() { stop(); }
Backend::Backend(Backend&&) noexcept = default;
Backend& Backend::operator=(Backend&& other) noexcept {
    if (this != &other) {
        stop();
        impl_ = std::move(other.impl_);
    }
    return *this;
}

bool Backend::start(const BackendConfig& config) {
    if (!impl_) return false;
    stop();
    impl_->config = config;
    impl_->error.clear();
    if (impl_->generation == std::numeric_limits<std::uint64_t>::max()) {
        impl_->error = "transport generation exhausted"; return false;
    }
    ++impl_->generation;
#ifdef _WIN32
    if (!impl_->hid || !impl_->hid->open(protocol::kVendorIdVive,
                                         protocol::kProductIdUltimateTrackerDongle,
                                         config.exclusiveHidAccess,
                                         impl_->error)) return false;

    // Normal service startup must not enter pairing mode. PairDevice changes
    // receiver behavior and caused a restart loop during hardware validation:
    // config reload required a backend restart, the restart re-enabled pairing,
    // and the already-paired tracker would not resume normal pose traffic until
    // paired again (changing the session RF address yet again).
    //
    // Pairing is therefore an explicit operation outside normal startup.
    // Runtime DeviceUnpaired recovery remains slot-local RxPowerSaving(false).
    impl_->isRunning = true;
    // A previous explicit setup operation may have closed with uncertain
    // disable exchange. Observe a new exchange on the reopened handle before
    // admitting another operation; this does not verify the old vendor effect.
    if (impl_->pairingMayBeEnabled && !endPairing()) {
        const auto error = impl_->error;
        stop();
        impl_->error = error;
        return false;
    }
    return true;
#else
    impl_->error = "VIVE dongle HID backend is currently implemented for Windows only";
    return false;
#endif
}

void Backend::stop() noexcept {
    if (!impl_) return;
#ifdef _WIN32
    if (impl_->pairingMayBeEnabled) (void)endPairingWithRetry(1);
    try { if (impl_->hid) impl_->hid->close(); }
    catch (...) {
        // A successful close cannot confirm cleanup; a close exception must
        // not erase a failed disable exchange or escape a destructor.
        try { impl_->error += "; HID close raised an exception"; } catch (...) {}
    }
#endif
    impl_->pairingEnableExchangeMatched = false;
    impl_->isRunning = false;
    impl_->hostSlot = -1;
    try {
        std::scoped_lock lock(impl_->mutex);
        impl_->states.clear();
        impl_->devices.clear();
        impl_->buttons.clear();
        impl_->lossDiagnostics.clear();
    } catch (...) { /* best-effort noexcept stop/destructor boundary */ }
}

std::uint64_t Backend::transportGeneration() const noexcept {
    return impl_ ? impl_->generation : 0;
}

bool Backend::running() const noexcept { return impl_ && impl_->isRunning; }
const std::string& Backend::lastError() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->error : empty;
}
void Backend::setEventCallback(EventCallback callback) {
    if (impl_) impl_->callback = std::move(callback);
}

bool Backend::beginPairing(int slot) {
    if (impl_ && !impl_->pairingMayBeEnabled) {
        impl_->enableExchange = {};
        impl_->enableExchange.command = protocol::kCmdRequestRfChangeBehavior;
        impl_->enableExchange.transportGeneration = impl_->generation;
        impl_->cleanup = {};
    }
    if (!impl_ || !impl_->isRunning) {
        if (impl_) impl_->error = "cannot begin pairing: backend is not running";
        return false;
    }
    const auto payload = protocol::buildRfBehaviorPayload(protocol::kRfBehaviorPairDevice, true, slot);
    if (!payload) {
        impl_->error = "cannot begin pairing: slot must be 0..4 or -1 for all selectors";
        return false;
    }
    if (impl_->pairingMayBeEnabled) {
        if (impl_->pairingSlot != slot) {
            impl_->error = "cannot change pairing selector while pairing is active";
            return false;
        }
        return impl_->pairingEnableExchangeMatched;
    }
    // Conservatively retain cleanup eligibility if a transport call throws
    // after writing the enable but before returning its response.
    impl_->pairingMayBeEnabled = true;
    impl_->pairingSlot = slot;
    impl_->cleanup.state = CleanupExchangeState::Unknown;
    impl_->cleanup.enableGeneration = impl_->generation;
    Impl::CommandResult result;
    try { result = impl_->sendCommandDetailed(protocol::kCmdRequestRfChangeBehavior, *payload); }
    catch (...) {
        impl_->enableExchange = impl_->lastExchange;
        throw; // may have written; cleanup eligibility intentionally remains
    }
    impl_->enableExchange = result.evidence;
    if (result.evidence.write == FeatureWriteState::NotAttempted || result.evidence.write == FeatureWriteState::Failed) {
        impl_->pairingMayBeEnabled = false;
        impl_->pairingSlot = -1;
        impl_->cleanup.state = CleanupExchangeState::NotNeeded;
    }
    if (!result.evidence.matched()) {
        impl_->error = "PairDevice enable exchange failed: " + impl_->error;
        return false;
    }
    impl_->pairingEnableExchangeMatched = true;
    return true;
}

bool Backend::endPairing() {
    if (!impl_) return false;
    if (!impl_->pairingMayBeEnabled) return true;
    if (!impl_->isRunning) {
        impl_->error = "transport lost before cleanup exchange; prior cleanup evidence retained";
        return false;
    }
    const auto payload = protocol::buildRfBehaviorPayload(
        protocol::kRfBehaviorPairDevice, false, impl_->pairingSlot);
    if (!payload) {
        impl_->error = "cannot disable pairing: saved selector is invalid";
        return false;
    }
    Impl::CommandResult result;
    try { result = impl_->sendCommandDetailed(protocol::kCmdRequestRfChangeBehavior, *payload); }
    catch (...) {
        impl_->cleanup.disable = impl_->lastExchange;
        impl_->cleanup.state = CleanupExchangeState::Unknown;
        throw;
    }
    impl_->cleanup.disable = result.evidence;
    impl_->cleanup.state = result.evidence.matched() ? CleanupExchangeState::Matched : CleanupExchangeState::Failed;
    if (!result.evidence.matched()) {
        impl_->error = "PairDevice disable exchange failed: " + impl_->error;
        return false;
    }
    impl_->pairingMayBeEnabled = false;
    impl_->pairingEnableExchangeMatched = false;
    impl_->pairingSlot = -1;
    return true;
}

bool Backend::pairingActive() const noexcept {
    return impl_ && impl_->pairingMayBeEnabled;
}

const FeatureExchangeEvidence& Backend::lastFeatureExchange() const noexcept {
    static const FeatureExchangeEvidence empty;
    return impl_ ? impl_->lastExchange : empty;
}
const std::optional<FeatureExchangeEvidence>& Backend::lastFailedFeatureExchange() const noexcept {
    static const std::optional<FeatureExchangeEvidence> empty;
    return impl_ ? impl_->lastFailedExchange : empty;
}
const FeatureExchangeEvidence& Backend::pairingEnableExchange() const noexcept {
    static const FeatureExchangeEvidence empty;
    return impl_ ? impl_->enableExchange : empty;
}
const PairingCleanupEvidence& Backend::pairingCleanupEvidence() const noexcept {
    static const PairingCleanupEvidence empty;
    return impl_ ? impl_->cleanup : empty;
}

bool Backend::endPairingWithRetry(unsigned attempts) {
    if (!pairingActive()) return true;
    for (unsigned i = 0; i < attempts && i < 2; ++i) {
        try { if (endPairing()) return true; }
        catch (...) { /* sanitized exchange error and write uncertainty retained */ }
    }
    return false;
}

std::vector<TrackerState> Backend::snapshot() const {
    if (!impl_) return {};
    std::scoped_lock lock(impl_->mutex);
    std::vector<TrackerState> result;
    result.reserve(impl_->states.size());
    for (const auto& [slot, state] : impl_->states) {
        (void)slot;
        result.push_back(state);
    }
    return result;
}

bool Backend::pollOnce() {
    if (!impl_ || !impl_->isRunning) return false;
#ifdef _WIN32
    auto report = impl_->hid->read(impl_->config.readTimeoutMs);
    impl_->receivedNs = monotonicNs();
    if (impl_->hid->readFailed()) {
        (void)endPairingWithRetry();
        stop();
        const auto cleanupError = impl_->pairingMayBeEnabled ? impl_->error : std::string{};
        impl_->error = "HID read failed: transport session lost";
        if (!cleanupError.empty()) impl_->error += "; cleanup exchange unresolved: " + cleanupError;
        return false;
    }
    if (report.empty()) return true;

    if (auto pair = protocol::parsePairEvent(report)) {
        if (pair->device.slot >= 0) {
            if (pair->unpair) {
                std::scoped_lock lock(impl_->mutex);
                impl_->devices.erase(pair->device.slot);
                impl_->states.erase(pair->device.slot);
                impl_->buttons.erase(pair->device.slot);
                impl_->lossDiagnostics.forget(pair->device.slot);
                if (impl_->hostSlot == pair->device.slot) impl_->hostSlot = -1;
            } else {
                {
                    std::scoped_lock lock(impl_->mutex);
                    impl_->devices[pair->device.slot] = pair->device;
                }
                impl_->configureOnPair(pair->device);
            }
        }
        impl_->emit({pair->unpair ? BackendEvent::Kind::DeviceUnpaired : BackendEvent::Kind::DevicePaired,
                     pair->device, std::nullopt, {}});
        if (pair->unpair && pair->device.slot >= 0) {
            // Keep recovery single-action: no RestartRf, no PairDevice replay,
            // and no reboot. Normal tracker power-on should reconnect afterward.
            impl_->setRxPowerSaving(false, pair->device.slot);
        }
        return true;
    }

    auto incoming = protocol::parseIncomingEnvelope(report);
    if (!incoming) return true;

    if (incoming->type == protocol::kIncomingAck) {
        const std::string text(incoming->payload.begin(), incoming->payload.end());
        if (text.rfind("NAZZ", 0) == 0) impl_->sendAck(incoming->device, "FW1");
        impl_->emit({BackendEvent::Kind::Ack, incoming->device, std::nullopt, text});
        return true;
    }

    if (incoming->type == protocol::kIncomingPose) {
        auto state = protocol::parsePose(*incoming, impl_->receivedNs);
        if (!state) return true;
        {
            std::scoped_lock lock(impl_->mutex);
            auto& combined = impl_->buttons[state->device.slot];
            const auto raw = static_cast<std::uint8_t>(state->buttons & 0xff);
            if (raw & 0x80u) combined = static_cast<std::uint16_t>(((raw & 0x7fu) << 8) | (combined & 0x00ffu));
            else combined = static_cast<std::uint16_t>(raw | (combined & 0xff00u));
            state->buttons = combined;
            impl_->devices[state->device.slot] = state->device;
            impl_->states[state->device.slot] = *state;
        }

        // Active HID requests, NOT passive logging. Only the explicit probe/HIL
        // option enables this; the production service always leaves it disabled.
        if (impl_->lossDiagnostics.queryDue(*state, impl_->config.diagnosticSlamQueries)) {
            impl_->emit({BackendEvent::Kind::Diagnostic, state->device, std::nullopt,
                         "diagnostic-only: tracking lost >=5s; querying SLAM keys 0,4,5,6"});
            impl_->sendAck(state->device, "P63:0");
            impl_->sendAck(state->device, "P63:4");
            impl_->sendAck(state->device, "P63:5");
            impl_->sendAck(state->device, "P63:6");
        }
        impl_->emit({BackendEvent::Kind::Pose, state->device, *state, {}});
    }
    return true;
#else
    return false;
#endif
}

} // namespace vut
