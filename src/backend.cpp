#include "vut/backend.hpp"
#include "vut/protocol.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
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

    std::vector<std::uint8_t> read(int timeoutMs) {
        std::vector<std::uint8_t> buffer(inputLength_, 0);
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) return {};

        DWORD readBytes = 0;
        const BOOL immediate = ReadFile(handle_, buffer.data(), static_cast<DWORD>(buffer.size()), &readBytes, &ov);
        if (!immediate && GetLastError() != ERROR_IO_PENDING) {
            CloseHandle(ov.hEvent);
            return {};
        }
        if (!immediate) {
            const DWORD wait = WaitForSingleObject(ov.hEvent, static_cast<DWORD>(std::max(timeoutMs, 0)));
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(handle_, &ov);
                CloseHandle(ov.hEvent);
                return {};
            }
            if (!GetOverlappedResult(handle_, &ov, &readBytes, FALSE)) {
                CloseHandle(ov.hEvent);
                return {};
            }
        }
        CloseHandle(ov.hEvent);
        buffer.resize(readBytes);
        return buffer;
    }

private:
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
    std::string error;
#ifdef _WIN32
    WinHidDevice hid;
#endif

    void emit(BackendEvent event) {
        if (callback) callback(event);
    }

#ifdef _WIN32
    std::vector<std::uint8_t> sendCommand(std::uint8_t command, std::span<const std::uint8_t> payload) {
        auto packet = protocol::buildFeatureCommand(command, payload, hid.featureLength());
        if (packet.empty() || !hid.setFeature(packet)) return {};
        for (int i = 0; i < 10; ++i) {
            auto response = hid.getFeature();
            if (response.size() < 5) continue;
            if (response[1] != command) continue;
            const std::size_t declared = response[2];
            if (declared < 4) return {};
            const std::size_t len = std::min<std::size_t>(declared - 4, response.size() - 5);
            return {response.begin() + 5, response.begin() + 5 + len};
        }
        return {};
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
#ifdef _WIN32
    if (!impl_->hid.open(protocol::kVendorIdVive, protocol::kProductIdUltimateTrackerDongle, impl_->error)) return false;
    const std::array<std::uint8_t, 7> enablePairing = {
        protocol::kRfBehaviorPairDevice, 1, 1, 1, 1, 0, 0
    };
    (void)impl_->sendCommand(protocol::kCmdRequestRfChangeBehavior, enablePairing);
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

bool Backend::pollOnce() {
    if (!impl_->isRunning) return false;
#ifdef _WIN32
    auto report = impl_->hid.read(impl_->config.readTimeoutMs);
    if (report.empty()) return true;

    if (auto pair = protocol::parsePairEvent(report)) {
        if (pair->device.slot >= 0) {
            if (pair->unpair) {
                std::scoped_lock lock(impl_->mutex);
                impl_->devices.erase(pair->device.slot);
                impl_->states.erase(pair->device.slot);
                impl_->buttons.erase(pair->device.slot);
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
        auto state = protocol::parsePose(*incoming, monotonicNs());
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
