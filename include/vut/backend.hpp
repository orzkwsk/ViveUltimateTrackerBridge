#pragma once

#include "vut/types.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace vut {

struct BackendConfig {
    std::string wifiCountry = "JP";
    int readTimeoutMs = 50;
    bool configureTrackingOnPair = true;
};

class Backend {
public:
    using EventCallback = std::function<void(const BackendEvent&)>;

    Backend();
    ~Backend();
    Backend(Backend&&) noexcept;
    Backend& operator=(Backend&&) noexcept;
    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    bool start(const BackendConfig& config = {});
    void stop();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] const std::string& lastError() const noexcept;

    bool pollOnce();
    void setEventCallback(EventCallback callback);
    [[nodiscard]] std::vector<TrackerState> snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vut
