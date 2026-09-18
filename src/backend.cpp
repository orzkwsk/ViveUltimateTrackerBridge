#include "vut/backend.hpp"
#include "vut/protocol.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
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
class WinHidDevice {
public:
    ~WinHidDevice() { close(); }

    bool open(std::uint16_t vid, std::uint16_t pid, std::string& error) {
        GUID hidGuid{};
        HidD_GetHidGuid(&hidGuid);
        HDEVINFO info = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (info == INVALID_HANDLE_VALUE) {
            error = "SetupDiGetClassDevsW failed";
            return false;
        }

        std::wstring fallback;
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
            if (h == INVALID_HANDLE_VALUE) continue;
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
            error = "VIVE Ultimate Tracker dongle HID interface not found (VID 0x0bb4 PID 0x0350)";
            return false;
        }

        handle_ = CreateFileW(fallback.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            error = "CreateFileW failed for dongle HID interface";
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

    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(handle_, nullptr);
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    std::size_t featureLength() const noexcept { return featureLength_; }

    bool setFeature(std::span<const std::uint8_t> data) {
        std::vector<std::uint8_t> buffer(featureLength_, 0);
        std::copy_n(data.begin(), std::min(data.size(), buffer.size()), buffer.begin());
        return HidD_SetFeature(handle_, buffer.data(), static_cast<ULONG>(buffer.size())) != FALSE;
    }

    std::vector<std::uint8_t> getFeature() {
        std::vector<std::uint8_t> buffer(featureLength_, 0);
        if (!HidD_GetFeature(handle_, buffer.data(), static_cast<ULONG>(buffer.size()))) return {};
        return buffer;
    }

    bool readFailed() const { return readFailed_; }

    std::vector<std::uint8_t> read(int timeoutMs) {
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

} // namespace

struct Backend::Impl {
    BackendConfig config;
    EventCallback callback;
    mutable std::mutex mutex;
    std::map<int, TrackerState> states;
    std::map<int, DeviceId> devices;
    std::map<int, std::uint16_t> buttons;
    int hostSlot = -1;
    bool isRunning = false;
    std::uint64_t generation = 0;
    std::int64_t receivedNs = 0;
    std::string error;
#ifdef _WIN32
    WinHidDevice hid;
#endif

    void emit(BackendEvent event) {
        event.observedMonotonicNs = receivedNs;
        event.transportGeneration = generation;
        if (callback) callback(event);
    }

#ifdef _WIN32
    static std::string bytesToHex(std::span<const std::uint8_t> bytes) {
        std::ostringstream out;
        out << std::hex << std::setfill('0');
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            if (i != 0) out << ' ';
            out << std::setw(2) << static_cast<unsigned>(bytes[i]);
        }
        return out.str();
    }

    DebugCommandResult sendCommandDetailed(std::uint8_t command, std::span<const std::uint8_t> payload) {
        DebugCommandResult result;
        result.command = command;
        result.payload.assign(payload.begin(), payload.end());

        auto packet = protocol::buildFeatureCommand(command, payload, hid.featureLength());
        if (packet.empty()) {
            result.error = "feature command does not fit report length";
            return result;
        }
        if (!hid.setFeature(packet)) {
            result.error = "HidD_SetFeature failed";
            return result;
        }
        result.sent = true;

        for (int i = 0; i < 10; ++i) {
            auto response = hid.getFeature();
            if (response.size() < 5) continue;
            if (response[1] != command) continue;
            const std::size_t declared = response[2];
            if (declared < 4) {
                result.error = "matching feature response had invalid declared length";
                return result;
            }
            const std::size_t len = std::min<std::size_t>(declared - 4, response.size() - 5);
            result.response.assign(response.begin() + 5, response.begin() + 5 + len);
            result.responseMatched = true;
            return result;
        }

        result.error = "feature write succeeded but no matching feature response was observed";
        return result;
    }

    std::vector<std::uint8_t> sendCommand(std::uint8_t command, std::span<const std::uint8_t> payload) {
        return sendCommandDetailed(command, payload).response;
    }

    DebugCommandResult runDebugCommand(std::uint8_t command, std::span<const std::uint8_t> payload) {
        receivedNs = monotonicNs();
        auto result = sendCommandDetailed(command, payload);

        std::ostringstream text;
        text << "debug-command tx_cmd=0x"
             << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(command)
             << " payload=[" << bytesToHex(payload) << ']'
             << " sent=" << std::dec << result.sent
             << " response_matched=" << result.responseMatched;
        if (!result.response.empty()) text << " response=[" << bytesToHex(result.response) << ']';
        if (!result.error.empty()) text << " note=" << result.error;
        emit({BackendEvent::Kind::Diagnostic, {}, std::nullopt, text.str()});
        return result;
    }

    DebugCommandResult sendRfBehavior(DebugRfBehavior behavior, bool enabled, int slot) {
        DebugCommandResult invalid;
        invalid.command = protocol::kCmdRequestRfChangeBehavior;

        if (slot < -1 || slot >= 5) {
            invalid.error = "slot must be 0..4 or -1 for all selectors";
            return invalid;
        }

        // Public RE and observed Hub captures agree on:
        //   byte0 = RF behavior
        //   byte1 = enable
        //   byte2..6 = five CR selectors.
        // Slot0 and slot2 RestartRf selectors are HIL-observed. The other
        // one-hot selectors remain inference until captured independently.
        std::array<std::uint8_t, 7> payload = {
            static_cast<std::uint8_t>(behavior),
            static_cast<std::uint8_t>(enabled ? 1 : 0),
            0, 0, 0, 0, 0
        };
        if (slot == -1) {
            std::fill(payload.begin() + 2, payload.end(), 1);
        } else {
            payload[2 + static_cast<std::size_t>(slot)] = 1;
        }
        return runDebugCommand(protocol::kCmdRequestRfChangeBehavior, payload);
    }

    void debugDelay() const {
        const int delayMs = std::max(config.debugInterCommandDelayMs, 0);
        if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
    }

    void applyUnpairRecovery(int slot) {
        constexpr std::uint8_t kRfBehaviorRestartRf = 0x02;

        switch (config.unpairRecoveryMode) {
        case UnpairRecoveryMode::None:
            return;

        case UnpairRecoveryMode::ColdStartRestartRf: {
            const std::array<std::uint8_t, 7> payload = {
                kRfBehaviorRestartRf, 1, 0, 1, 1, 1, 1
            };
            (void)runDebugCommand(protocol::kCmdRequestRfChangeBehavior, payload);
            return;
        }

        case UnpairRecoveryMode::SlotSpecificRestartRf:
            (void)sendRfBehavior(DebugRfBehavior::RestartRf, true, slot);
            return;

        case UnpairRecoveryMode::DisablePwsThenRestartRf:
            (void)sendRfBehavior(DebugRfBehavior::RxPowerSaving, false, slot);
            debugDelay();
            (void)sendRfBehavior(DebugRfBehavior::RestartRf, true, slot);
            return;

        case UnpairRecoveryMode::DisablePairThenRestartRf:
            (void)sendRfBehavior(DebugRfBehavior::PairDevice, false, slot);
            debugDelay();
            (void)sendRfBehavior(DebugRfBehavior::RestartRf, true, slot);
            return;

        case UnpairRecoveryMode::DisablePairAndPwsThenRestartRf:
            (void)sendRfBehavior(DebugRfBehavior::PairDevice, false, slot);
            debugDelay();
            (void)sendRfBehavior(DebugRfBehavior::RxPowerSaving, false, slot);
            debugDelay();
            (void)sendRfBehavior(DebugRfBehavior::RestartRf, true, slot);
            return;

        case UnpairRecoveryMode::RfRebootApp: {
            // Public RE/USBTool-informed candidate: direct dongle command 0x1C
            // with payload {0, 3} selects the APP side. This is not a vendor-
            // confirmed semantic and remains experiment-only.
            const std::array<std::uint8_t, 2> payload = {0, 3};
            (void)runDebugCommand(0x1c, payload);
            return;
        }

        case UnpairRecoveryMode::RfRebootAppThenRestartRf: {
            const std::array<std::uint8_t, 2> payload = {0, 3};
            (void)runDebugCommand(0x1c, payload);
            debugDelay();
            (void)sendRfBehavior(DebugRfBehavior::RestartRf, true, slot);
            return;
        }
        }
    }

    bool sendAck(const DeviceId& device, const std::string& ack) {
        auto tx = protocol::buildAckTx(device, ack);
        if (tx.empty()) return false;
        (void)sendCommand(protocol::kCmdTx, tx);
        return true;
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
#endif
};

Backend::Backend() : impl_(std::make_unique<Impl>()) {}
Backend::~Backend() = default;
Backend::Backend(Backend&&) noexcept = default;
Backend& Backend::operator=(Backend&&) noexcept = default;

bool Backend::start(const BackendConfig& config) {
    stop();
    impl_->config = config;
    impl_->error.clear();
    if (impl_->generation == std::numeric_limits<std::uint64_t>::max()) {
        impl_->error = "transport generation exhausted"; return false;
    }
    ++impl_->generation;
#ifdef _WIN32
    if (!impl_->hid.open(protocol::kVendorIdVive, protocol::kProductIdUltimateTrackerDongle, impl_->error)) return false;
    // Experiment: reproduce the VIVE Hub cold-start RF behavior captured during
    // hardware testing. 0x02 is RestartRf; the remaining bytes are copied from
    // the observed Hub payload and intentionally remain uninterpreted here.
    // This branch exists to test whether RestartRf is the missing prerequisite
    // for reconnecting an already paired tracker after power-cycle.
    constexpr std::uint8_t kRfBehaviorRestartRf = 0x02;
    const std::array<std::uint8_t, 7> restartRf = {
        kRfBehaviorRestartRf, 1, 0, 1, 1, 1, 1
    };
    (void)impl_->sendCommand(protocol::kCmdRequestRfChangeBehavior, restartRf);
    impl_->isRunning = true;
    return true;
#else
    impl_->error = "VIVE dongle HID backend is currently implemented for Windows only";
    return false;
#endif
}

void Backend::stop() {
#ifdef _WIN32
    impl_->hid.close();
#endif
    impl_->isRunning = false;
    impl_->hostSlot = -1;
    std::scoped_lock lock(impl_->mutex);
    impl_->states.clear();
    impl_->devices.clear();
    impl_->buttons.clear();
}

std::uint64_t Backend::transportGeneration() const noexcept { return impl_->generation; }

bool Backend::running() const noexcept { return impl_->isRunning; }
const std::string& Backend::lastError() const noexcept { return impl_->error; }
void Backend::setEventCallback(EventCallback callback) { impl_->callback = std::move(callback); }

std::vector<TrackerState> Backend::snapshot() const {
    std::scoped_lock lock(impl_->mutex);
    std::vector<TrackerState> result;
    result.reserve(impl_->states.size());
    for (const auto& [slot, state] : impl_->states) {
        (void)slot;
        result.push_back(state);
    }
    return result;
}

DebugCommandResult Backend::debugSendRaw(std::uint8_t command, std::span<const std::uint8_t> payload) {
#ifdef _WIN32
    if (!impl_->isRunning) {
        DebugCommandResult result;
        result.command = command;
        result.payload.assign(payload.begin(), payload.end());
        result.error = "backend is not running";
        return result;
    }
    return impl_->runDebugCommand(command, payload);
#else
    DebugCommandResult result;
    result.command = command;
    result.payload.assign(payload.begin(), payload.end());
    result.error = "debug dongle commands require the Windows HID backend";
    return result;
#endif
}

DebugCommandResult Backend::debugSendRfBehavior(DebugRfBehavior behavior, bool enabled, int slot) {
#ifdef _WIN32
    if (!impl_->isRunning) {
        DebugCommandResult result;
        result.command = protocol::kCmdRequestRfChangeBehavior;
        result.error = "backend is not running";
        return result;
    }
    return impl_->sendRfBehavior(behavior, enabled, slot);
#else
    DebugCommandResult result;
    result.command = protocol::kCmdRequestRfChangeBehavior;
    result.error = "debug dongle commands require the Windows HID backend";
    return result;
#endif
}

DebugCommandResult Backend::debugQueryRfStatus() {
    constexpr std::uint8_t kCmdQueryRfStatus = 0x1e;
    return debugSendRaw(kCmdQueryRfStatus, {});
}

DebugCommandResult Backend::debugRfRebootApp() {
    constexpr std::uint8_t kCmdRfRebootCandidate = 0x1c;
    const std::array<std::uint8_t, 2> payload = {0, 3};
    return debugSendRaw(kCmdRfRebootCandidate, payload);
}

void Backend::setUnpairRecoveryMode(UnpairRecoveryMode mode, int interCommandDelayMs) {
    impl_->config.unpairRecoveryMode = mode;
    impl_->config.debugInterCommandDelayMs = std::max(interCommandDelayMs, 0);
}

UnpairRecoveryMode Backend::unpairRecoveryMode() const noexcept {
    return impl_->config.unpairRecoveryMode;
}

int Backend::debugInterCommandDelayMs() const noexcept {
    return impl_->config.debugInterCommandDelayMs;
}

bool Backend::pollOnce() {
    if (!impl_->isRunning) return false;
#ifdef _WIN32
    auto report = impl_->hid.read(impl_->config.readTimeoutMs);
    impl_->receivedNs = monotonicNs();
    if (impl_->hid.readFailed()) {
        impl_->error = "HID read failed: transport session lost";
        stop();
        return false;
    }
    if (report.empty()) return true;

    if (auto rfStatus = protocol::parseRfStatus(report)) {
        std::ostringstream text;
        text << "rf-status"
             << " report=0x" << std::hex << std::setw(2) << std::setfill('0')
             << static_cast<unsigned>(rfStatus->responseId)
             << " tag=0x" << std::setw(2) << static_cast<unsigned>(rfStatus->innerTag)
             << " header=0x" << std::setw(4) << rfStatus->unknownHeader;
        for (std::size_t slot = 0; slot < rfStatus->slotState.size(); ++slot) {
            text << " slot" << std::dec << slot
                 << "=0x" << std::hex << std::setw(8) << std::setfill('0')
                 << rfStatus->slotState[slot];
        }
        impl_->emit({BackendEvent::Kind::Diagnostic, {}, std::nullopt, text.str()});
        return true;
    }

    if (auto pair = protocol::parsePairEvent(report)) {
        if (pair->device.slot >= 0) {
            if (pair->unpair) {
                {
                    std::scoped_lock lock(impl_->mutex);
                    impl_->devices.erase(pair->device.slot);
                    impl_->states.erase(pair->device.slot);
                    impl_->buttons.erase(pair->device.slot);
                    if (impl_->hostSlot == pair->device.slot) impl_->hostSlot = -1;
                }
                impl_->applyUnpairRecovery(pair->device.slot);
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
        impl_->emit({BackendEvent::Kind::Pose, state->device, *state, {}});
    }
    return true;
#else
    return false;
#endif
}

} // namespace vut