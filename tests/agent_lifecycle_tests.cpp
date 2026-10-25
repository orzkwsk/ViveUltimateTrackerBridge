#include "vut/agent_lifecycle.hpp"
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace vut;
#define CHECK(value) do { if (!(value)) throw std::runtime_error("CHECK failed: " #value); } while (false)
namespace {
struct Counts {
    int factories = 0, starts = 0, shutdowns = 0, destroyed = 0, polls = 0, mutations = 0;
    bool startFails = false, pollFails = false, pollThrows = false;
    std::function<bool()> onShutdown;
    bool ordering = true;
    fs::path work, journal;
};
class FixtureRuntime final : public AgentRuntime {
    Counts& counts_;
public:
    explicit FixtureRuntime(Counts& counts) : counts_(counts) { ++counts_.factories; }
    ~FixtureRuntime() override { ++counts_.destroyed; }
    bool start() override {
        ++counts_.starts; // Fake HID open count: never links the tracking backend.
        if (!counts_.journal.empty()) {
            std::ofstream journal(counts_.journal, std::ios::app);
            if (!journal) throw std::runtime_error("fixture journal unavailable");
            journal << "fixture revision\n";
            ++counts_.mutations;
        }
        return !counts_.startFails;
    }
    bool pollOnce() override {
        ++counts_.polls;
        if (counts_.pollThrows) throw std::runtime_error("injected poll failure");
        return !counts_.pollFails;
    }
    bool shutdown() noexcept override {
        ++counts_.shutdowns;
        if (counts_.onShutdown) counts_.ordering = counts_.onShutdown();
        return true;
    }
};
void result(const fs::path& work, const Counts& c, AgentState state) {
    std::ofstream out(work / "result.json");
    out << "{\"factories\":" << c.factories << ",\"starts\":" << c.starts
        << ",\"shutdowns\":" << c.shutdowns << ",\"destroyed\":" << c.destroyed
        << ",\"mutations\":" << c.mutations << ",\"polls\":" << c.polls
        << ",\"ordering\":" << (c.ordering ? "true" : "false")
        << ",\"state\":" << static_cast<int>(state) << "}";
}
void unit(const fs::path& root) {
    fs::create_directories(root);
    AgentIdentity identity{root / "service.conf", root / "revision.log", 29810};
    std::ofstream(identity.config) << "fixture\n";
    const auto name = agentInstanceName(identity);
    CHECK(name.size() < MAX_PATH);
    auto alias = identity;
    alias.config = root / "." / "service.conf";
    CHECK(agentInstanceName(alias) == name);
    alias.config = root / "SERVICE.CONF";
    alias.journal = root / "REVISION.LOG";
    CHECK(agentInstanceName(alias) == name);
    alias = identity; alias.config = root / "other.conf";
    CHECK(agentInstanceName(alias) != name);
    alias = identity; alias.journal = root / "other.log";
    CHECK(agentInstanceName(alias) != name);
    alias = identity; ++alias.port;
    CHECK(agentInstanceName(alias) != name);
    // A nonexistent journal and its later creation must address the same lock.
    std::ofstream(identity.journal) << "";
    CHECK(agentInstanceName(identity) == name);
    Counts counts;
    AgentLifecycle agent;
    counts.onShutdown = [&] {
        Counts challenger;
        AgentLifecycle contender;
        return agent.state() == AgentState::Stopping && !agent.canSpawnOwnedChild()
            && !contender.start(identity, [&] { return std::make_unique<FixtureRuntime>(challenger); })
            && challenger.factories == 0;
    };
    CHECK(agent.start(identity, [&] { return std::make_unique<FixtureRuntime>(counts); }));
    CHECK(agent.state() == AgentState::Running);
    Counts duplicate;
    AgentLifecycle second;
    CHECK(!second.start(identity, [&] { return std::make_unique<FixtureRuntime>(duplicate); }));
    CHECK(duplicate.factories == 0 && duplicate.starts == 0 && duplicate.mutations == 0);
    CHECK(agent.pollOnce());
    bool wrongThreadRejected = false;
    std::thread request([&] {
        try { agent.pollOnce(); } catch (const std::logic_error&) { wrongThreadRejected = true; }
        agent.requestStop();
    });
    request.join();
    CHECK(wrongThreadRejected);
    CHECK(!agent.canSpawnOwnedChild());
    CHECK(!agent.pollOnce());
    agent.shutdown(); agent.shutdown();
    CHECK(agent.state() == AgentState::Stopped);
    CHECK(counts.shutdowns == 1 && counts.destroyed == 1 && counts.polls == 1 && counts.ordering);
    CHECK(!agent.start(identity, [&] { return std::make_unique<FixtureRuntime>(counts); }));
    for (int failure = 0; failure < 3; ++failure) {
        Counts faults;
        faults.startFails = failure == 0;
        faults.pollFails = failure == 1;
        faults.pollThrows = failure == 2;
        AgentLifecycle next;
        const bool started = next.start(identity, [&] { return std::make_unique<FixtureRuntime>(faults); });
        CHECK(started == (failure != 0));
        if (started) CHECK(!next.pollOnce());
        CHECK(next.state() == AgentState::Faulted);
        next.shutdown();
        CHECK(faults.shutdowns == 1 && faults.destroyed == 1);
    }
    Counts unwind;
    {
        AgentLifecycle next;
        CHECK(next.start(identity, [&] { return std::make_unique<FixtureRuntime>(unwind); }));
    }
    CHECK(unwind.shutdowns == 1 && unwind.destroyed == 1);
    std::cout << "PASS canonical identity, pre-factory exclusion, owner thread, stop, fault, teardown, reacquire\n";
}
int child(const fs::path& work) {
    std::ofstream(work / "child-started") << GetCurrentProcessId();
    for (;;) {
        if (fs::exists(work / "child-exit")) return 19;
        Sleep(10);
    }
}
int fixture(int argc, char** argv) {
    CHECK(argc >= 7);
    AgentIdentity identity{argv[2], argv[3], static_cast<std::uint16_t>(std::stoul(argv[4]))};
    const fs::path work = argv[5];
    const std::string mode = argv[6];
    Counts counts;
    counts.work = work; counts.journal = identity.journal;
    AgentLifecycle agent;
    counts.onShutdown = [&] {
        return agent.state() == AgentState::Stopping && !agent.canSpawnOwnedChild()
            && (mode != "child" || agent.childState() != AgentChildState::None);
    };
    if (!agent.start(identity, [&] { return std::make_unique<FixtureRuntime>(counts); })) {
        result(work, counts, agent.state());
        std::cerr << agent.error() << '\n';
        return 2;
    }
    if (mode == "jobfail") AgentLifecycleTestAccess::rejectChildAssignment(agent);
    if (mode == "child" || mode == "jobfail") {
        try {
            const auto pid = agent.spawnOwnedChild(fs::absolute(argv[0]), {L"--child", work.wstring()});
            std::ofstream(work / "child-pid") << pid;
            CHECK(mode != "jobfail");
        } catch (const std::exception&) {
            if (mode != "jobfail") throw;
            CHECK(!agent.hasOwnedChild());
            std::ofstream(work / "job-rejected") << "no child admitted";
        }
    }
    std::ofstream(work / "ready") << GetCurrentProcessId();
    for (;;) {
        if (fs::exists(work / "stop")) agent.requestStop();
        if (!agent.pollOnce()) break;
        std::ofstream status(work / "status.json");
        status << "{\"polls\":" << counts.polls << ",\"child\":" << static_cast<int>(agent.childState()) << "}";
        status.close(); // Make the observation available while the fixture sleeps.
        Sleep(10);
    }
    agent.shutdown(); agent.shutdown();
    result(work, counts, agent.state());
    CHECK(counts.shutdowns == 1);
    CHECK(counts.ordering);
    CHECK(agent.state() == AgentState::Stopped);
    return 0;
}
}
int main(int argc, char** argv) try {
    CHECK(argc >= 3);
    const std::string mode = argv[1];
    if (mode == "--unit") { unit(argv[2]); return 0; }
    if (mode == "--child") return child(argv[2]);
    if (mode == "--agent") return fixture(argc, argv);
    throw std::runtime_error("unknown fixture command");
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
