#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace vut {
enum class AgentState { Starting, Running, Stopping, Stopped, Faulted };
enum class AgentChildState { None, Running, Exited };
enum class AgentChildVisibility { Hidden, Normal };
struct AgentIdentity {
    std::filesystem::path config;
    std::filesystem::path journal;
    std::uint16_t port = 29810;
};

// Windows canonical absolute paths, invariant case folding, versioned SHA-256.
// Diagnostics, RF/slot and process identifiers are deliberately not inputs.
std::wstring agentInstanceName(const AgentIdentity&);

// Runtime plus its journal/sender are one owner-thread resource aggregate.
// The factory is invoked ONLY after the native instance lock has been acquired.
class AgentRuntime {
public:
    virtual ~AgentRuntime() = default;
    virtual bool start() = 0;
    virtual bool pollOnce() = 0;
    virtual bool shutdown() noexcept = 0;
};

// All methods/destruction except requestStop belong to the constructing thread.
// A future tray thread may request stop, but cannot access tracking or children.
// No restart, process adoption, discovery, UI protocol or live fake-runtime CLI.
class AgentLifecycle {
public:
    AgentLifecycle();
    ~AgentLifecycle() noexcept;
    AgentLifecycle(const AgentLifecycle&) = delete;
    AgentLifecycle& operator=(const AgentLifecycle&) = delete;
    bool start(const AgentIdentity&, const std::function<std::unique_ptr<AgentRuntime>()>&);
    bool pollOnce();
    void requestStop() noexcept;
    void shutdown() noexcept;
    AgentState state() const;
    const std::string& error() const;
    bool canSpawnOwnedChild() const;
    std::uint32_t spawnOwnedChild(const std::filesystem::path&, const std::vector<std::wstring>&,
                                 AgentChildVisibility = AgentChildVisibility::Hidden);
    bool showOwnedChild();
    AgentChildState childState() const;
    bool hasOwnedChild() const;
private:
    struct Native;
    std::unique_ptr<Native> native_;
    std::unique_ptr<AgentRuntime> runtime_;
    std::thread::id owner_;
    std::atomic_bool stop_{false};
    AgentState state_ = AgentState::Stopped;
    std::string error_;
    bool started_ = false;
    bool working_ = false;
    void checkOwner() const;
    void finish(bool fault) noexcept;
#ifdef VUT_AGENT_TESTING
    friend struct AgentLifecycleTestAccess;
#endif
};

#ifdef VUT_AGENT_TESTING
// Compiled into the hardware-free test executable only, never the product.
struct AgentLifecycleTestAccess {
    static void rejectChildAssignment(AgentLifecycle&);
};
#endif
} // namespace vut
