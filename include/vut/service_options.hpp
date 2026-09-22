#pragma once

#include <charconv>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>

namespace vut {
struct ServiceOptions {
    std::uint16_t port = 29810;
    bool diagnostics = false;
    bool interactiveDebug = false;
};
inline ServiceOptions parseServiceOptions(std::span<const std::string_view> arguments) {
    ServiceOptions result;
    bool hasPort = false;
    for (const auto argument : arguments) {
        if (argument == "--diagnostics" && !result.diagnostics) {
            result.diagnostics = true;
        } else if (argument == "--interactive-debug" && !result.interactiveDebug) {
            result.interactiveDebug = true;
        } else {
            unsigned int port = 0;
            const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), port);
            if (hasPort || parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size() || port == 0 || port > 65535)
                throw std::invalid_argument("use CONFIG REVISION_JOURNAL [PORT] [--diagnostics] [--interactive-debug]");
            result.port = static_cast<std::uint16_t>(port); hasPort = true;
        }
    }
    return result;
}
}
