#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace vut::detail {

// Narrow HID boundary used by Backend. Production supplies the Windows
// HID/SetupAPI implementation; tests inject an in-memory transport so command
// sequencing can be verified without opening hardware.
class BackendTransport {
public:
    virtual ~BackendTransport() = default;

    virtual bool open(std::uint16_t vid,
                      std::uint16_t pid,
                      bool exclusive,
                      std::string& error) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual std::size_t featureLength() const noexcept = 0;
    virtual bool setFeature(std::span<const std::uint8_t> data) = 0;
    [[nodiscard]] virtual std::vector<std::uint8_t> getFeature() = 0;
    [[nodiscard]] virtual std::vector<std::uint8_t> read(int timeoutMs) = 0;
    [[nodiscard]] virtual bool readFailed() const noexcept = 0;
};

} // namespace vut::detail
