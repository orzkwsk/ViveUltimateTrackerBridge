#include "vut/agent_ipc.hpp"
#include <windows.h>
#include <objbase.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
namespace vut {
struct AgentIpc::Native {
    struct Request { std::string bytes; std::uint64_t connection; };
    std::wstring name;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    std::atomic_bool stop{false}, failed{false};
    std::atomic<DWORD> allowed{0};
    std::mutex mutex;
    std::optional<Request> request;
    std::optional<std::string> response;
    std::uint64_t connection = 0;
    std::thread worker;
    Native() {
        GUID id{}; wchar_t text[40]{};
        if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, text, 40)) throw std::runtime_error("Agent IPC identity unavailable");
        name = std::wstring(L"Monaka.Altra.Agent.v1.") + text;
        const auto path = L"\\\\.\\pipe\\" + name;
        pipe = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_NOWAIT | PIPE_REJECT_REMOTE_CLIENTS,
            1, 16384, 4096, 0, nullptr);
        if (pipe == INVALID_HANDLE_VALUE) throw std::runtime_error("Agent IPC creation failed");
        try { worker = std::thread([this] { try { loop(); } catch (...) { failed = true; } }); }
        catch (...) { CloseHandle(pipe); pipe = INVALID_HANDLE_VALUE; throw; }
    }
    ~Native() {
        stop = true;
        if (worker.joinable()) {
            if (WaitForSingleObject(worker.native_handle(), 5000) != WAIT_OBJECT_0) std::terminate();
            worker.join();
        }
        if (pipe != INVALID_HANDLE_VALUE) { DisconnectNamedPipe(pipe); CloseHandle(pipe); }
    }
    void retire() {
        DisconnectNamedPipe(pipe);
        std::lock_guard lock(mutex);
        ++connection; request.reset(); response.reset();
    }
    void loop() {
        bool connected = false, waiting = false;
        std::string input, output;
        std::size_t written = 0;
        ULONGLONG deadline = 0;
        while (!stop.load()) {
            if (!connected) {
                const bool attached = ConnectNamedPipe(pipe, nullptr) != FALSE;
                const auto error = GetLastError();
                if (!attached && error != ERROR_PIPE_CONNECTED) {
                    if (error != ERROR_PIPE_LISTENING && error != ERROR_NO_DATA) { failed = true; return; }
                    if (error == ERROR_NO_DATA) retire();
                    Sleep(2); continue;
                }
                ULONG pid = 0;
                if (!GetNamedPipeClientProcessId(pipe, &pid) || !allowed.load() || pid != allowed.load()) { retire(); Sleep(2); continue; }
                connected = true; waiting = false; input.clear(); output.clear(); written = 0;
                deadline = GetTickCount64() + 5000;
            }
            ULONG pid = 0; DWORD available = 0;
            if (!GetNamedPipeClientProcessId(pipe, &pid) || pid != allowed.load() ||
                !PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || GetTickCount64() > deadline) {
                retire(); connected = false; continue;
            }
            if (!waiting && available) {
                char buffer[1024]; DWORD read = 0;
                if (!ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr)) { retire(); connected = false; continue; }
                input.append(buffer, read);
                if (input.size() > 4096) { retire(); connected = false; continue; }
                if (const auto newline = input.find('\n'); newline != std::string::npos) {
                    if (newline != input.size() - 1) { retire(); connected = false; continue; }
                    input.pop_back();
                    std::lock_guard lock(mutex);
                    request = Request{std::move(input), connection}; waiting = true;
                }
            }
            if (waiting && output.empty()) {
                std::lock_guard lock(mutex);
                if (response) { output = std::move(*response); response.reset(); output += '\n'; }
            }
            if (!output.empty()) {
                DWORD count = 0;
                if (!WriteFile(pipe, output.data() + written, static_cast<DWORD>(output.size() - written), &count, nullptr)) {
                    retire(); connected = false; continue;
                }
                written += count;
                if (written == output.size()) {
                    input.clear(); output.clear(); written = 0; waiting = false;
                    deadline = GetTickCount64() + 5000;
                }
            }
            Sleep(2);
        }
    }
};
AgentIpc::AgentIpc() : native_(std::make_unique<Native>()) {}
AgentIpc::~AgentIpc() = default;
const std::wstring& AgentIpc::name() const noexcept { return native_->name; }
void AgentIpc::allowOwnedChild(std::uint32_t pid) noexcept { native_->allowed = pid; }
bool AgentIpc::healthy() const noexcept { return !native_->failed.load(); }
void AgentIpc::pump(const std::function<std::string(const std::string&)>& handle) {
    std::optional<Native::Request> request;
    { std::lock_guard lock(native_->mutex); request.swap(native_->request); }
    if (!request) return;
    const auto response = handle(request->bytes);
    if (response.empty() || response.size() > 16384 || response.find('\n') != std::string::npos)
        throw std::runtime_error("Agent IPC response out of bounds");
    std::lock_guard lock(native_->mutex);
    if (native_->connection == request->connection) native_->response = response;
}
}
