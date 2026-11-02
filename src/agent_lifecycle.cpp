#include "vut/agent_lifecycle.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <array>
#include <limits>
#include <stdexcept>

namespace vut {
namespace {
class Handle {
public:
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE handle) : value(handle) {}
    ~Handle() { reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset(HANDLE handle = nullptr) noexcept {
        if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value);
        value = handle;
    }
};
std::wstring canonicalKey(const std::filesystem::path& input) {
    if (input.empty()) throw std::invalid_argument("empty agent identity path");
    auto canonical = std::filesystem::weakly_canonical(std::filesystem::absolute(input));
    auto existing = canonical;
    std::filesystem::path tail;
    while (!std::filesystem::exists(existing)) {
        if (existing == existing.parent_path()) throw std::runtime_error("agent identity root unavailable");
        tail = existing.filename() / tail;
        existing = existing.parent_path();
    }
    Handle file(CreateFileW(existing.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error("agent identity path unavailable");
    const auto sizeNeeded = GetFinalPathNameByHandleW(file.value, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!sizeNeeded || sizeNeeded > 32760) throw std::runtime_error("agent identity final path unavailable");
    std::wstring finalPath(sizeNeeded, L'\0');
    const auto written = GetFinalPathNameByHandleW(file.value, finalPath.data(), sizeNeeded, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!written || written >= sizeNeeded) throw std::runtime_error("agent identity final path changed");
    finalPath.resize(written);
    if (finalPath.starts_with(L"\\\\?\\UNC\\")) finalPath = L"\\\\" + finalPath.substr(8);
    else if (finalPath.starts_with(L"\\\\?\\")) finalPath.erase(0, 4);
    auto path = (std::filesystem::path(finalPath) / tail).lexically_normal().native();
    // An empty tail must not introduce a trailing separator after a file name.
    if (!tail.empty() && path.ends_with(L'\\')) path.pop_back();
    if (tail.empty()) path = finalPath;
    if (path.size() > 32760) throw std::invalid_argument("agent identity path too long");
    const int size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, path.data(),
        static_cast<int>(path.size()), nullptr, 0, nullptr, nullptr, 0);
    if (!size) throw std::runtime_error("agent path case normalization failed");
    std::wstring folded(size, L'\0');
    if (LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, path.data(),
        static_cast<int>(path.size()), folded.data(), size, nullptr, nullptr, 0) != size)
        throw std::runtime_error("agent path case normalization failed");
    return folded;
}
std::wstring quote(const std::wstring& value) {
    if (value.find(L'\0') != std::wstring::npos) throw std::invalid_argument("NUL child argument");
    std::wstring result = L"\"";
    std::size_t slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        result.append(c == L'\"' ? slashes * 2 + 1 : slashes, L'\\');
        slashes = 0;
        result += c;
    }
    result.append(slashes * 2, L'\\');
    return result + L"\"";
}
class Attributes {
    std::vector<unsigned char> storage_;
public:
    LPPROC_THREAD_ATTRIBUTE_LIST list = nullptr;
    Attributes() {
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        if (!size) throw std::runtime_error("child attribute sizing failed");
        storage_.resize(size);
        list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(list, 1, 0, &size)) {
            list = nullptr;
            throw std::runtime_error("child attribute initialization failed");
        }
    }
    ~Attributes() { if (list) DeleteProcThreadAttributeList(list); }
};
struct WorkGuard {
    bool& flag;
    explicit WorkGuard(bool& value) : flag(value) {
        if (flag) throw std::logic_error("recursive agent lifecycle work");
        flag = true;
    }
    ~WorkGuard() { flag = false; }
};
}

std::wstring agentInstanceName(const AgentIdentity& identity) {
    if (!identity.port) throw std::invalid_argument("zero agent port");
    const auto config = canonicalKey(identity.config);
    const auto journal = canonicalKey(identity.journal);
    // Length-prefix the fields, so separators in valid file names cannot alias.
    const auto material = std::to_wstring(config.size()) + L":" + config + L"|" +
        std::to_wstring(journal.size()) + L":" + journal + L"|" + std::to_wstring(identity.port);
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("agent identity SHA-256 unavailable");
    std::array<unsigned char, 32> digest{};
    const auto status = BCryptHash(algorithm, nullptr, 0,
        reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(material.data())),
        static_cast<ULONG>(material.size() * sizeof(wchar_t)), digest.data(), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (status < 0) throw std::runtime_error("agent identity SHA-256 failed");
    std::wstring name = L"Global\\Monaka.Altra.Agent.v1.";
    constexpr wchar_t hex[] = L"0123456789abcdef";
    for (auto byte : digest) { name += hex[byte >> 4]; name += hex[byte & 15]; }
    return name;
}

struct AgentLifecycle::Native {
    Handle mutex, job, child;
    bool ownsMutex = false;
#ifdef VUT_AGENT_TESTING
    bool rejectAssignment = false;
#endif
    void acquire(const AgentIdentity& identity) {
        const auto name = agentInstanceName(identity);
        mutex.reset(CreateMutexW(nullptr, TRUE, name.c_str()));
        const auto error = GetLastError();
        if (!mutex.value) throw std::runtime_error("agent instance mutex unavailable");
        if (error == ERROR_ALREADY_EXISTS) {
            mutex.reset();
            throw std::runtime_error("another agent owns this installation");
        }
        ownsMutex = true;
        job.reset(CreateJobObjectW(nullptr, nullptr));
        if (!job.value) throw std::runtime_error("owned-child job unavailable");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
        limits.BasicLimitInformation.ActiveProcessLimit = 1;
        if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            throw std::runtime_error("owned-child job limits failed");
    }
    bool closeChild() noexcept {
        // Last handle is non-inheritable. Closing the job also covers owner death.
        job.reset();
        const bool closed = !child.value || WaitForSingleObject(child.value, 5000) == WAIT_OBJECT_0;
        if (closed) child.reset(); // Keep exact handle and mutex if exit is unconfirmed.
        return closed;
    }
    void release() noexcept {
        if (ownsMutex) ReleaseMutex(mutex.value);
        ownsMutex = false;
        mutex.reset();
    }
    ~Native() {
        if (!closeChild()) std::terminate(); // Never advertise successful release with a live child.
        release();
    }
};

AgentLifecycle::AgentLifecycle() : native_(std::make_unique<Native>()), owner_(std::this_thread::get_id()) {}
AgentLifecycle::~AgentLifecycle() noexcept { shutdown(); }
void AgentLifecycle::checkOwner() const {
    if (owner_ != std::this_thread::get_id()) throw std::logic_error("agent owner thread mismatch");
}
bool AgentLifecycle::start(const AgentIdentity& identity, const std::function<std::unique_ptr<AgentRuntime>()>& factory) {
    checkOwner();
    WorkGuard work(working_);
    if (started_ || stop_.load()) return false;
    started_ = true;
    state_ = AgentState::Starting;
    try {
        native_->acquire(identity);
        if (stop_.load()) { finish(false); return false; }
        runtime_ = factory(); // No journal mutation or HID construction before ownership.
        if (stop_.load()) { finish(false); return false; }
        if (!runtime_ || !runtime_->start()) throw std::runtime_error("agent runtime unavailable; explicit restart required");
        state_ = AgentState::Running;
        if (stop_.load()) { finish(false); return false; }
        return true;
    } catch (const std::exception& error) {
        error_ = error.what();
    } catch (...) {
        error_ = "agent startup failed";
    }
    finish(true);
    return false;
}
void AgentLifecycle::requestStop() noexcept { stop_.store(true); }
bool AgentLifecycle::pollOnce() {
    checkOwner();
    WorkGuard work(working_);
    if (state_ != AgentState::Running) return false;
    if (stop_.load()) { finish(false); return false; }
    try {
        if (runtime_->pollOnce()) return true;
        error_ = "agent runtime unavailable; explicit restart required";
    } catch (const std::exception& error) { error_ = error.what(); }
    catch (...) { error_ = "agent runtime fault"; }
    finish(true);
    return false;
}
void AgentLifecycle::finish(bool fault) noexcept {
    if (state_ == AgentState::Stopped || state_ == AgentState::Faulted) return;
    stop_.store(true); // Reject all future child/control work first.
    state_ = AgentState::Stopping;
    if (runtime_) {
        if (!runtime_->shutdown()) fault = true;
        runtime_.reset(); // Runtime, sender, journal: BEFORE job/lock release.
    }
    if (!native_->closeChild()) {
        state_ = AgentState::Faulted;
        return; // Retain instance ownership until destruction can confirm child exit.
    }
    native_->release();
    state_ = fault ? AgentState::Faulted : AgentState::Stopped;
}
void AgentLifecycle::shutdown() noexcept {
    // Destruction on a foreign thread would violate Runtime and mutex ownership.
    if (owner_ != std::this_thread::get_id() || working_) std::terminate();
    finish(state_ == AgentState::Faulted);
}
AgentState AgentLifecycle::state() const { checkOwner(); return state_; }
const std::string& AgentLifecycle::error() const { checkOwner(); return error_; }
AgentChildState AgentLifecycle::childState() const {
    checkOwner();
    if (!native_->child.value) return AgentChildState::None;
    const auto wait = WaitForSingleObject(native_->child.value, 0);
    if (wait == WAIT_OBJECT_0) return AgentChildState::Exited;
    if (wait == WAIT_TIMEOUT) return AgentChildState::Running;
    throw std::runtime_error("owned child status unavailable");
}
bool AgentLifecycle::hasOwnedChild() const { return childState() == AgentChildState::Running; }
bool AgentLifecycle::canSpawnOwnedChild() const {
    checkOwner();
    return state_ == AgentState::Running && !stop_.load() && !hasOwnedChild();
}
std::uint32_t AgentLifecycle::spawnOwnedChild(const std::filesystem::path& executable, const std::vector<std::wstring>& arguments) {
    checkOwner();
    WorkGuard work(working_);
    if (!canSpawnOwnedChild()) throw std::logic_error("agent cannot launch an owned child now");
    const auto path = std::filesystem::canonical(executable).native();
    std::wstring command = quote(path);
    for (const auto& argument : arguments) command += L" " + quote(argument);
    if (command.size() >= 32767) throw std::invalid_argument("owned child command too long");
    Attributes attributes;
    HANDLE job = native_->job.value;
#ifdef VUT_AGENT_TESTING
    if (native_->rejectAssignment) job = INVALID_HANDLE_VALUE;
#endif
    if (!UpdateProcThreadAttribute(attributes.list, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
        &job, sizeof(job), nullptr, nullptr)) throw std::runtime_error("child job assignment setup failed");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.lpAttributeList = attributes.list;
    PROCESS_INFORMATION process{};
    // Atomic job membership: there is no create-then-assign orphan window.
    if (!CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW,
        nullptr, nullptr, &startup.StartupInfo, &process))
        throw std::runtime_error("owned child creation/job assignment failed");
    Handle child(process.hProcess), thread(process.hThread);
    BOOL inJob = FALSE;
    if (!IsProcessInJob(child.value, native_->job.value, &inJob) || !inJob || stop_.load() || ResumeThread(thread.value) == DWORD(-1)) {
        TerminateProcess(child.value, 1); // Exact handle returned by OUR CreateProcess.
        WaitForSingleObject(child.value, 5000);
        throw std::runtime_error("owned child admission failed");
    }
    native_->child.reset(child.value);
    child.value = nullptr;
    return process.dwProcessId;
}
#ifdef VUT_AGENT_TESTING
void AgentLifecycleTestAccess::rejectChildAssignment(AgentLifecycle& agent) {
    agent.checkOwner();
    agent.native_->rejectAssignment = true;
}
#endif
} // namespace vut
