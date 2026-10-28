#include "vut/agent_lifecycle.hpp"
#include "vut/agent_tray.hpp"
#include "vut/agent_ipc.hpp"
#include "vut/agent_control.hpp"
#include "fake_pairing_transport.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
namespace fs = std::filesystem;
using namespace vut;
class FakeOwner final : public AgentRuntime {
    pairing_test::Transport* transport_;
    std::unique_ptr<ServiceRuntime> runtime_;
    std::vector<std::uint8_t> pose_;
    std::uint8_t packet_ = 0;
    std::uint32_t revision_ = 0;
    ULONGLONG nextPose_ = 0;
    fs::path root_;
public:
    std::uint64_t polls = 0;
    explicit FakeOwner(fs::path root) : root_(std::move(root)) {
        std::ifstream input(std::string(FIXTURE_DIR) + "/pose37.bin", std::ios::binary);
        pose_ = {std::istreambuf_iterator<char>(input), {}};
        if (pose_.size() < 49) throw std::runtime_error("offline pose fixture missing");
        auto transport = std::make_unique<pairing_test::Transport>(); transport_ = transport.get();
        ServiceRuntimeConfig config; config.allowExplicitPairing = true;
        config.observation.sourceId = "offline-gui-fixture";
        config.observation.spaces = {{"23:32:42:b7:82:d3","offline-map"},{pairing_test::device().toString(),"offline-pair-map"}};
        ServiceRuntimeDependencies deps;
        deps.clock = pairing_test::nowNs;
        deps.advanceRevision = [this] { return ++revision_; };
        deps.makeSessionId = [] { return "00000000-0000-4000-8000-000000000001"; };
        deps.send = [](std::string_view) { return true; };
        runtime_ = std::make_unique<ServiceRuntime>(Backend(std::move(transport)), config, deps);
    }
    bool start() override { return runtime_->start().ok(); }
    bool pollOnce() override {
        ++polls;
        if (GetTickCount64() >= nextPose_) {
            std::string state = "full"; std::ifstream(root_ / "pose-mode.txt") >> state;
            if (state != "stale") {
                auto bytes = pose_; bytes[12] = ++packet_; bytes[48] = state == "q" ? 0x14 : state == "none" ? 0x13 : 0x12;
                transport_->reads.push_back(std::move(bytes));
            }
            nextPose_ = GetTickCount64() + 100;
        }
        const bool result = runtime_->pollOnce();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return result;
    }
    bool shutdown() noexcept override {
        try { runtime_->shutdown(); return runtime_->snapshot()->state != ServiceRuntimeState::Faulted; }
        catch (...) { return false; }
    }
    std::string request(const std::string& bytes) {
        auto response=agentControlRequest(*runtime_, bytes);
        std::ofstream(root_/"status.json")<<response;
        return response;
    }
};
int main(int argc, char** argv) try {
    if (argc < 3 || argc > 4) throw std::runtime_error("agent_gui_host NEW_WORK_DIRECTORY GUI_EXE [--tray]");
    const fs::path root = fs::absolute(argv[1]), gui = fs::canonical(argv[2]);
    if (!fs::create_directories(root)) throw std::runtime_error("fixture directory must be new");
    std::ofstream(root / "agent.conf") << "offline-only fixture\n";
    std::unique_ptr<AgentTray> tray;
    AgentIpc ipc;
    std::string pipeName; for(auto c:ipc.name()){if(c>127)throw std::runtime_error("non-ASCII internal pipe");pipeName.push_back(static_cast<char>(c));}
    std::ofstream(root/"pipe.txt")<<pipeName;
    FakeOwner* owner = nullptr;
    AgentLifecycle agent;
    if (!agent.start({root / "agent.conf", root / "revision.log", 29810}, [&] {
        auto fixture = std::make_unique<FakeOwner>(root); owner = fixture.get(); return fixture;
    })) throw std::runtime_error(agent.error());
    auto open = [&] {
        if (agent.hasOwnedChild()) { agent.showOwnedChild(); return; }
        ipc.allowOwnedChild(0);
        const auto pid = agent.spawnOwnedChild(gui, {L"--agent-pipe",ipc.name(),L"--agent-pid",std::to_wstring(GetCurrentProcessId())}, AgentChildVisibility::Normal);
        ipc.allowOwnedChild(pid);
        std::ofstream(root / "child.pid") << pid;
    };
    if (argc == 4) {
        if (std::string(argv[3]) != "--tray") throw std::runtime_error("unknown fixture option");
        tray = std::make_unique<AgentTray>();
    }
    open();
    std::ofstream(root / "host.pid") << GetCurrentProcessId();
    std::ofstream(root / "ready.txt") << "TEST_ONLY_NEVER_DEPLOY; fake HID, no UDP, own IPC\n";
    const auto deadline = GetTickCount64() + 120000;
    ULONGLONG nextHeartbeat=0;
    while (GetTickCount64() < deadline && !fs::exists(root / "stop.flag")) {
        if (!ipc.healthy()) throw std::runtime_error("fixture IPC failed");
        if (tray) {
            if (!tray->responsive()) throw std::runtime_error("fixture tray failed");
            const auto action = tray->takeAction();
            if (action == AgentTrayAction::Exit) break;
            if (action == AgentTrayAction::Open) open();
        }
        ipc.pump([&](const std::string& bytes) { return owner->request(bytes); });
        if (!agent.pollOnce()) break;
        if(fs::exists(root/"open.flag")){fs::remove(root/"open.flag");open();}
        if(GetTickCount64()>=nextHeartbeat){std::ofstream(root/"polls.txt")<<owner->polls;nextHeartbeat=GetTickCount64()+250;}
    }
    const auto polls = agent.state() == AgentState::Running ? owner->polls : 0;
    agent.requestStop(); agent.shutdown();
    std::ofstream(root / "result.txt") << "polls=" << polls << " state=" << static_cast<int>(agent.state()) << " child=" << static_cast<int>(agent.childState()) << '\n';
    return agent.state() == AgentState::Stopped ? 0 : 1;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
