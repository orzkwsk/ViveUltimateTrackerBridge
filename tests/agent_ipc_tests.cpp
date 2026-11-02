#include "vut/agent_ipc.hpp"
#include <windows.h>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#define CHECK(x) do { if (!(x)) throw std::runtime_error("CHECK failed: " #x); } while (false)
struct Handle { HANDLE h; ~Handle() { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
DWORD io(HANDLE pipe, bool writing, char* bytes, DWORD size) {
    Handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    OVERLAPPED overlapped{}; overlapped.hEvent = event.h;
    DWORD count = 0;
    const BOOL done = writing ? WriteFile(pipe, bytes, size, &count, &overlapped) : ReadFile(pipe, bytes, size, &count, &overlapped);
    if (!done) {
        if (GetLastError() != ERROR_IO_PENDING) throw std::runtime_error("pipe disconnected");
        if (WaitForSingleObject(event.h, 2000) != WAIT_OBJECT_0) {
            CancelIoEx(pipe, &overlapped); GetOverlappedResult(pipe, &overlapped, &count, TRUE);
            throw std::runtime_error("pipe I/O timeout");
        }
        if (!GetOverlappedResult(pipe, &overlapped, &count, FALSE)) throw std::runtime_error("pipe I/O failed");
    }
    return count;
}
std::string request(const std::wstring& name, std::string bytes) {
    const auto path = L"\\\\.\\pipe\\" + name;
    Handle pipe{CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr)};
    CHECK(pipe.h != INVALID_HANDLE_VALUE);
    ULONG server = 0; CHECK(GetNamedPipeServerProcessId(pipe.h, &server) && server == GetCurrentProcessId());
    bytes += '\n'; CHECK(io(pipe.h, true, bytes.data(), static_cast<DWORD>(bytes.size())) == bytes.size());
    std::string response;
    while (response.find('\n') == std::string::npos) {
        char chunk[1024]; const auto count = io(pipe.h, false, chunk, sizeof(chunk));
        CHECK(count > 0 && response.size() + count <= 16385); response.append(chunk, count);
    }
    response.pop_back(); return response;
}
int main() try {
    vut::AgentIpc server;
    server.allowOwnedChild(GetCurrentProcessId());
    int calls = 0;
    auto client = std::async(std::launch::async, [&] { return request(server.name(), "hello"); });
    while (client.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        server.pump([&](const std::string& bytes) { ++calls; return "reply:" + bytes; });
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(client.get() == "reply:hello" && calls == 1 && server.healthy());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.allowOwnedChild(0);
    bool rejected = false;
    try { (void)request(server.name(), "unowned"); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    server.allowOwnedChild(GetCurrentProcessId());
    rejected = false;
    try { (void)request(server.name(), std::string(4097, 'x')); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected && calls == 1);
    std::cout << "PASS unique native IPC, exact allowed PID, owner dispatch, frame bound\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
