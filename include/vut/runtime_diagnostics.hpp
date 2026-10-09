#pragma once
#include "vut/observation_adapter.hpp"
#include <atomic>
#include <condition_variable>
#include <vector>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>

namespace vut {
std::string diagnosticQuote(std::string_view text);
std::string diagnosticBuildJson();

// The single producer never waits for a lock or performs sink I/O. A finite observer
// cannot preserve an unlimited transition burst: any overflow is explicit and
// invalidates completeness. The first pending fatal has a separate reserved slot.
class RuntimeDiagnostics {
public:
    using Sink = std::function<bool(const std::string&)>;
    struct Counters { std::uint64_t accepted, written, droppedPose, droppedCritical, writeErrors, maxWriteNs; };
    explicit RuntimeDiagnostics(Sink sink = {}, std::size_t capacity = 1024);
    ~RuntimeDiagnostics();
    bool enabled() const noexcept { return enabled_; }
    void event(std::string_view type, std::string details = "{}", std::string identity = "null",
               std::string validity = "null", bool critical = true) noexcept;
    void beginSession(std::string id, std::uint64_t generation);
    void sessionState(std::string_view state);
    void observeBackend(const BackendEvent& event) noexcept;
    void observe(const monaka::protocol::v2::Envelope& observation) noexcept;
    Counters counters() const noexcept;
    void close();
private:
    struct Tracker { bool registered = false, firstPose = false, firstPosition = false; std::optional<bool> position, orientation; };
    std::string line(std::string_view type, std::string_view details, std::string_view identity, std::string_view validity);
    void enqueue(std::string bytes, bool critical, bool fatal) noexcept;
    void worker();
    bool enabled_;
    Sink sink_;
    std::size_t capacity_;
    std::mutex mutex_;
    std::condition_variable wake_;
    // Single producer (service poll loop), single writer. No lock on enqueue.
    std::vector<std::string> ring_;
    std::atomic<std::size_t> head_{0}, tail_{0};
    std::atomic<std::string*> fatal_{nullptr};
    std::atomic_bool closing_{false};
    std::thread thread_;
    std::atomic<std::uint64_t> accepted_{0}, written_{0}, droppedPose_{0}, droppedCritical_{0}, writeErrors_{0}, maxWriteNs_{0};
    std::uint64_t sequence_ = 0, generation_ = 0;
    std::string sessionId_, sessionState_ = "unknown";
    std::map<std::string, Tracker> trackers_;
};
}
