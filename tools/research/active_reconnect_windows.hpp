#pragma once
#include "vut/backend_transport.hpp"
#include "vut/protocol.hpp"
#include "research_hid_interface.hpp"
#include <windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <filesystem>
#include <stdexcept>

namespace vut::active_hil {
inline std::string hex(std::span<const std::uint8_t> bytes) {
    constexpr char digits[] = "0123456789abcdef"; std::string out;
    for (auto b : bytes) { out += digits[b >> 4]; out += digits[b & 15]; } return out;
}
inline std::string nonce() {
    std::array<std::uint8_t, 32> value{};
    if (BCryptGenRandom(nullptr, value.data(), static_cast<ULONG>(value.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
        throw std::runtime_error("NonceUnavailable");
    return hex(value);
}
inline std::string sha256(std::span<const std::uint8_t> value) {
    BCRYPT_ALG_HANDLE algorithm{}; std::array<std::uint8_t, 32> result{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("HashUnavailable");
    const auto status = BCryptHash(algorithm, nullptr, 0, const_cast<PUCHAR>(value.data()),
                                  static_cast<ULONG>(value.size()), result.data(), static_cast<ULONG>(result.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("HashUnavailable"); return hex(result);
}
// CREATE_NEW and FlushFileBuffers: failures block the sender before HID calls.
class DurableJournal {
public:
    explicit DurableJournal(const std::filesystem::path& path) {
        handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("JournalUnavailable");
    }
    ~DurableJournal() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    DurableJournal(const DurableJournal&) = delete;
    DurableJournal& operator=(const DurableJournal&) = delete;
    void write(std::string_view data) {
        DWORD written = 0;
        if (!WriteFile(handle_, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) ||
            written != data.size() || !FlushFileBuffers(handle_)) throw std::runtime_error("JournalWriteFailure");
    }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};
class ResearchHid final : public detail::BackendTransport {
public:
    ~ResearchHid() { close(); }
    bool open(std::uint16_t vid, std::uint16_t pid, bool exclusive, std::string& error) override {
        if (everOpened_ || !exclusive || vid != protocol::kVendorIdVive || pid != protocol::kProductIdUltimateTrackerDongle)
            return false;
        everOpened_ = true;
        GUID guid{}; HidD_GetHidGuid(&guid);
        auto info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (info == INVALID_HANDLE_VALUE) { error = "InterfaceEnumerationFailed"; return false; }
        std::vector<std::wstring> candidates;
        for (DWORD i = 0;; ++i) {
            SP_DEVICE_INTERFACE_DATA iface{}; iface.cbSize = sizeof(iface);
            if (!SetupDiEnumDeviceInterfaces(info, nullptr, &guid, i, &iface)) break;
            DWORD size = 0; SetupDiGetDeviceInterfaceDetailW(info, &iface, nullptr, 0, &size, nullptr);
            if (!size) continue;
            std::vector<std::byte> storage(size);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data()); detail->cbSize = sizeof(*detail);
            if (!SetupDiGetDeviceInterfaceDetailW(info, &iface, detail, size, nullptr, nullptr)) continue;
            std::wstring path = detail->DevicePath, lower = path;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
            if (qualifiesDongleInterface(lower))
                candidates.push_back(path);
        }
        SetupDiDestroyDeviceInfoList(info);
        if (candidates.size() != 1) { error = "AmbiguousDongleInterface"; return false; }
        // One exclusive open, no shared-open probe, fallback or reconnect loop.
        handle_ = CreateFileW(candidates[0].c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) { error = "ExclusiveOpenFailed"; return false; }
        HIDD_ATTRIBUTES attr{}; attr.Size = sizeof(attr);
        PHIDP_PREPARSED_DATA data{}; HIDP_CAPS caps{};
        const bool attributes = HidD_GetAttributes(handle_, &attr) && attr.VendorID == vid && attr.ProductID == pid;
        bool valid = false;
        if (HidD_GetPreparsedData(handle_, &data)) {
            valid = HidP_GetCaps(data, &caps) == HIDP_STATUS_SUCCESS && caps.FeatureReportByteLength == 65 && caps.InputReportByteLength;
            HidD_FreePreparsedData(data);
        }
        if (!attributes || !valid) { error = "DongleDescriptorMismatch"; close(); return false; }
        inputLength_ = std::max<std::size_t>(caps.InputReportByteLength, 1024); return true;
    }
    void close() override {
        if (handle_ != INVALID_HANDLE_VALUE) { CancelIoEx(handle_, nullptr); CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
    }
    std::size_t featureLength() const noexcept override { return 65; }
    bool setFeature(std::span<const std::uint8_t> report) override {
        if (handle_ == INVALID_HANDLE_VALUE || report.size() != 65) return false;
        std::array<std::uint8_t, 65> buffer{}; std::copy(report.begin(), report.end(), buffer.begin());
        return HidD_SetFeature(handle_, buffer.data(), static_cast<ULONG>(buffer.size())) != FALSE;
    }
    std::vector<std::uint8_t> getFeature() override {
        std::vector<std::uint8_t> buffer(65, 0);
        if (!HidD_GetFeature(handle_, buffer.data(), static_cast<ULONG>(buffer.size()))) return {}; return buffer;
    }
    std::vector<std::uint8_t> read(int timeout) override {
        failed_ = false; std::vector<std::uint8_t> buffer(inputLength_);
        OVERLAPPED ov{}; ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) { failed_ = true; return {}; }
        DWORD count = 0;
        if (!ReadFile(handle_, buffer.data(), static_cast<DWORD>(buffer.size()), &count, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) { failed_ = true; CloseHandle(ov.hEvent); return {}; }
            const auto wait = WaitForSingleObject(ov.hEvent, static_cast<DWORD>(std::max(timeout, 0)));
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(handle_, &ov); GetOverlappedResult(handle_, &ov, &count, TRUE);
                failed_ = wait != WAIT_TIMEOUT; CloseHandle(ov.hEvent); return {};
            }
            if (!GetOverlappedResult(handle_, &ov, &count, FALSE)) { failed_ = true; CloseHandle(ov.hEvent); return {}; }
        }
        CloseHandle(ov.hEvent); buffer.resize(count); return buffer;
    }
    bool readFailed() const noexcept override { return failed_; }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    bool everOpened_ = false, failed_ = false;
    std::size_t inputLength_ = 1024;
};
} // namespace vut::active_hil
