// API-boundary fault injection around the actual production lifecycle.
// The fixture launches only itself, suspended in its own Job, and retains an
// independent exact handle for cleanup even when the regression is present.
#include <windows.h>
#include "vut/agent_lifecycle.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
HANDLE witness = nullptr;
bool injecting = false, denyTerminate = false, denyMembership = false;
DWORD waitFailure = WAIT_TIMEOUT;
int shutdowns = 0, longWaits = 0;
BOOL WINAPI TestCreateProcessW(LPCWSTR app, LPWSTR command, LPSECURITY_ATTRIBUTES processAttributes,
    LPSECURITY_ATTRIBUTES threadAttributes, BOOL inherit, DWORD flags, LPVOID env,
    LPCWSTR directory, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process) {
    const auto ok = ::CreateProcessW(app, command, processAttributes, threadAttributes,
        inherit, flags, env, directory, startup, process);
    if (ok && !DuplicateHandle(GetCurrentProcess(), process->hProcess, GetCurrentProcess(),
        &witness, 0, FALSE, DUPLICATE_SAME_ACCESS)) std::terminate();
    return ok;
}
DWORD WINAPI TestResumeThread(HANDLE thread) {
    if (injecting) { SetLastError(ERROR_ACCESS_DENIED); return DWORD(-1); }
    return ::ResumeThread(thread);
}
BOOL WINAPI TestIsProcessInJob(HANDLE process, HANDLE job, PBOOL result) {
    if (injecting && denyMembership) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return ::IsProcessInJob(process, job, result);
}
BOOL WINAPI TestTerminateProcess(HANDLE process, UINT code) {
    if (injecting && denyTerminate) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    return ::TerminateProcess(process, code);
}
DWORD WINAPI TestWait(HANDLE process, DWORD millis) {
    if (injecting && millis == 5000 && waitFailure != WAIT_OBJECT_0) {
        ++longWaits; SetLastError(ERROR_ACCESS_DENIED); return waitFailure;
    }
    return ::WaitForSingleObject(process, millis);
}
}
#define CreateProcessW TestCreateProcessW
#define ResumeThread TestResumeThread
#define IsProcessInJob TestIsProcessInJob
#define TerminateProcess TestTerminateProcess
#define WaitForSingleObject TestWait
#include "../src/agent_lifecycle.cpp"
#undef CreateProcessW
#undef ResumeThread
#undef IsProcessInJob
#undef TerminateProcess
#undef WaitForSingleObject

namespace {
struct Runtime : vut::AgentRuntime {
    bool start() override { return true; }
    bool pollOnce() override { return true; }
    bool shutdown() noexcept override { ++shutdowns; return true; }
};
void require(bool value, const char* reason) {
    if (!value) throw std::runtime_error(reason);
}
struct Cleanup {
    ~Cleanup() {
        injecting = false;
        if (witness) {
            if (::WaitForSingleObject(witness, 5000) != WAIT_OBJECT_0) {
                ::TerminateProcess(witness, 99);
                if (::WaitForSingleObject(witness, 5000) != WAIT_OBJECT_0) std::terminate();
            }
            ::CloseHandle(witness); witness = nullptr;
        }
    }
};
void run(const std::filesystem::path& exe, const std::filesystem::path& root,
    const char* name, bool terminateFailure, bool membershipFailure, DWORD failedWait) {
    auto dir = root / name; std::filesystem::create_directories(dir);
    std::ofstream(dir / "config") << "offline fixture";
    vut::AgentIdentity identity{dir / "config", dir / "journal", 29810};
    shutdowns = 0; longWaits = 0;
    Cleanup cleanup;
    vut::AgentLifecycle agent;
    require(agent.start(identity, [] { return std::make_unique<Runtime>(); }), "start rejected");
    injecting = true; denyTerminate = terminateFailure; denyMembership = membershipFailure; waitFailure = failedWait;
    bool rejected = false;
    try { agent.spawnOwnedChild(exe, {L"--child"}); } catch (const std::exception&) { rejected = true; }
    const bool needsConfirmation = failedWait != WAIT_OBJECT_0;
    const bool handleRetained = agent.childState() != vut::AgentChildState::None;
    const bool faulted = agent.state() == vut::AgentState::Faulted;
    const bool spawnBlocked = !agent.canSpawnOwnedChild();
    vut::AgentLifecycle contender;
    const bool contenderAccepted = contender.start(identity, [] { return std::make_unique<Runtime>(); });
    contender.shutdown();
    bool retryRetained = true;
    if (needsConfirmation) {
        agent.shutdown(); // A second unconfirmed wait must still retain ownership.
        retryRetained = agent.state() == vut::AgentState::Faulted &&
            agent.childState() != vut::AgentChildState::None;
    }
    std::cout << name << " rejected=" << rejected << " retained=" << handleRetained
              << " faulted=" << faulted << " spawn_blocked=" << spawnBlocked
              << " contender_accepted=" << contenderAccepted << " injected_waits=" << longWaits << '\n';
    // Remove injected failures before any assertion/destructor. The production
    // shutdown must then confirm its exact child and release ownership itself.
    injecting = false;
    agent.shutdown();
    const bool exactExit = witness && ::WaitForSingleObject(witness, 5000) == WAIT_OBJECT_0;
    const bool finalFault = agent.state() == vut::AgentState::Faulted;
    const int runtimeShutdowns = shutdowns;
    vut::AgentLifecycle retry;
    const bool reacquired = retry.start(identity, [] { return std::make_unique<Runtime>(); });
    retry.shutdown();
    require(rejected && exactExit, "admission rejection/owned exit absent");
    require(!contenderAccepted, "mutex released before confirmation");
    require(reacquired, "confirmed exit did not release mutex");
    require(runtimeShutdowns == 1, "runtime shutdown repeated or omitted");
    if (needsConfirmation) {
        require(handleRetained && faulted && spawnBlocked && retryRetained, "unconfirmed admission lost child/ownership fault");
        require(finalFault, "fault reported as clean stop");
    } else require(!handleRetained && !faulted && !spawnBlocked, "confirmed rejection needlessly faults tracking");
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring(argv[1]) == L"--child") { Sleep(INFINITE); return 10; }
    try {
        require(argc == 2, "expected isolated work directory");
        const auto exe = std::filesystem::absolute(argv[0]);
        const std::filesystem::path root = argv[1];
        run(exe, root, "resume-terminate-timeout", true, false, WAIT_TIMEOUT);
        run(exe, root, "membership-wait-failed", true, true, WAIT_FAILED);
        run(exe, root, "terminated-wait-timeout", false, false, WAIT_TIMEOUT);
        run(exe, root, "confirmed-admission-rejection", false, false, WAIT_OBJECT_0);
        std::cout << "4 admission confirmation cases PASS\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
