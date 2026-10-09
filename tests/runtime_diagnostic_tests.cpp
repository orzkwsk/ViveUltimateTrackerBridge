#include "vut/runtime_diagnostics.hpp"
#include "vut/service_options.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif
#define CHECK(x) do { if (!(x)) throw std::runtime_error("check failed: " #x); } while (false)
using namespace std::chrono;
namespace {
monaka::protocol::v2::TrackerObservation pose(bool p, bool r) {
    monaka::protocol::v2::TrackerObservation o;
    o.device_id = "01:02:03:04:05:06"; o.validity = {p, r}; return o;
}
std::size_t count(const std::vector<std::string>& lines, std::string_view type) {
    return std::count_if(lines.begin(), lines.end(), [&](const auto& s) { return s.find("\"event_type\":\"" + std::string(type) + "\"") != std::string::npos; });
}
void fixture(bool emit) {
    std::vector<std::string> lines;
    vut::RuntimeDiagnostics d([&](const auto& line) { lines.push_back(line); return true; });
    d.event("component_started"); d.sessionState("connecting"); d.beginSession("fixture-a", 1);
    vut::BackendEvent discovered; discovered.kind = vut::BackendEvent::Kind::DevicePaired;
    discovered.device.mac = {1,2,3,4,5,6}; d.observeBackend(discovered);
    d.observe(pose(false, false)); d.observe(pose(false, true)); d.observe(pose(true, true)); d.observe(pose(true, true));
    monaka::protocol::v2::ObservationDeviceState absent; absent.device_id = "01:02:03:04:05:06"; absent.presence = "absent"; d.observe(absent);
    d.sessionState("disconnected"); d.sessionState("reconnecting"); d.beginSession("fixture-b", 2);
    d.observe(pose(true, true));
    d.event("future_extension", "{\"note\":\"unknown event is preserved\"}");
    d.event("fatal_error", "{\"message\":\"fixture fatal\\nquoted \\\"message\\\"\"}");
    d.close();
    CHECK(count(lines,"first_pose") == 2); CHECK(count(lines,"first_position_valid") == 2);
    CHECK(count(lines,"tracker_discovered") == 1); CHECK(count(lines,"tracker_unregistered") == 1);
    CHECK(count(lines,"position_valid_changed") == 3); CHECK(count(lines,"orientation_valid_changed") == 3);
    CHECK(count(lines,"session_connected") == 2); CHECK(count(lines,"session_reconnecting") == 1);
    CHECK(count(lines,"fatal_error") == 1); CHECK(d.counters().droppedCritical == 0);
    for (const auto& line : lines) { CHECK(line.find("\"role\":null") != std::string::npos); CHECK(line.find("\"build\":{") != std::string::npos); CHECK(line.find("\"diagnostic_delivery_age_ns\":") != std::string::npos); }
    if (emit) for (const auto& line : lines) std::cout << line;
}
void pressure() {
    std::atomic_bool entered{false}, release{false}; std::vector<std::string> lines;
    vut::RuntimeDiagnostics d([&](const auto& line) {
        entered = true; while (!release) std::this_thread::yield(); lines.push_back(line); return true;
    }, 8);
    d.event("component_started"); while (!entered) std::this_thread::yield();
    const auto start = steady_clock::now();
    for (int i = 0; i < 1000; ++i) d.event("pose_observed", "{}", "null", "null", false);
    for (int i = 0; i < 1000; ++i) d.event("session_connected");
    d.event("fatal_error", "{\"message\":\"preserved despite full bounded queue\"}");
    CHECK(duration_cast<seconds>(steady_clock::now() - start).count() < 2);
    release = true; d.close();
    CHECK(d.counters().droppedPose > 0); CHECK(d.counters().droppedCritical > 0);
    CHECK(count(lines, "fatal_error") == 1);
    CHECK(lines.back().find("\"observation_complete\":false") != std::string::npos);
    const auto queued = lines.at(1);
    const auto age = queued.find("\"diagnostic_delivery_age_ns\":") + std::string_view("\"diagnostic_delivery_age_ns\":").size();
    CHECK(std::stoll(queued.substr(age)) > 0);
}
std::uint64_t memoryBytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pm{}; GetProcessMemoryInfo(GetCurrentProcess(), &pm, sizeof(pm)); return pm.WorkingSetSize;
#else
    return 0;
#endif
}
std::uint64_t cpuNs() {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
    ULARGE_INTEGER k{}, u{}; k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
    return (k.QuadPart + u.QuadPart) * 100;
#else
    return std::uint64_t(1000000000.0 * std::clock() / CLOCKS_PER_SEC);
#endif
}
void smoke() {
    for (const auto mode : {"off", "on_idle", "on_active"}) {
        const bool enabled = std::string_view(mode) != "off";
        std::ofstream file;
        if (enabled) file.open(std::string("build/rr00b-smoke-") + mode + ".jsonl", std::ios::binary | std::ios::trunc);
        CHECK(!enabled || bool(file));
        vut::RuntimeDiagnostics d(enabled ? vut::RuntimeDiagnostics::Sink([&](const auto& bytes) { file << bytes; file.flush(); return bool(file); }) : vut::RuntimeDiagnostics::Sink{});
        d.beginSession("smoke", 1);
        const auto cpu = cpuNs(); const auto start = steady_clock::now(); const auto memory = memoryBytes();
        std::vector<long long> latency; latency.reserve(50000);
        if (std::string_view(mode) == "on_idle") std::this_thread::sleep_for(milliseconds(250));
        else for (int i = 0; i < (enabled ? 1000 : 50000); ++i) {
            const auto t = steady_clock::now(); d.observe(pose(true, true));
            latency.push_back(duration_cast<nanoseconds>(steady_clock::now() - t).count());
            if (enabled) std::this_thread::sleep_for(milliseconds(1));
        }
        const auto elapsed = duration_cast<nanoseconds>(steady_clock::now() - start).count();
        const auto usedCpuNs = cpuNs() - cpu;
        const auto finalMemory = memoryBytes();
        d.close(); const auto c = d.counters(); std::sort(latency.begin(), latency.end());
        CHECK(c.writeErrors == 0 && c.droppedPose == 0 && c.droppedCritical == 0);
        std::cout << "{\"mode\":\"" << mode << "\",\"duration_ns\":" << elapsed << ",\"cpu_ns\":" << usedCpuNs
                  << ",\"cpu_measurement\":\"process kernel+user CPU\",\"memory_start_bytes\":" << memory << ",\"memory_end_bytes\":" << finalMemory
                  << ",\"observer_calls\":" << latency.size() << ",\"event_rate_per_second\":" << c.written * 1000000000.0 / elapsed
                  << ",\"producer_p99_ns\":" << (latency.empty() ? 0 : latency[latency.size()*99/100])
                  << ",\"producer_max_ns\":" << (latency.empty() ? 0 : latency.back()) << ",\"max_write_ns\":" << c.maxWriteNs
                  << ",\"dropped_events\":" << c.droppedPose+c.droppedCritical << ",\"critical_dropped_events\":" << c.droppedCritical << "}\n";
    }
}
}
int main(int argc, char** argv) try {
    if (argc == 2 && std::string_view(argv[1]) == "--fixture") { fixture(true); return 0; }
    if (argc == 2 && std::string_view(argv[1]) == "--smoke") { smoke(); return 0; }
    vut::RuntimeDiagnostics off; off.beginSession("ignored", 1); off.observe(pose(true, true)); off.event("fatal_error"); CHECK(off.counters().accepted == 0); CHECK(!off.enabled());
    CHECK(vut::diagnosticQuote("a\n\"\\") == "\"a\\u000a\\\"\\\\\"");
    const std::string_view arg = "--runtime-diagnostic-jsonl=fixture.jsonl";
    CHECK(vut::parseServiceOptions(std::span(&arg,1)).runtimeDiagnosticPath == "fixture.jsonl");
    fixture(false); pressure();
    vut::RuntimeDiagnostics failing([](const auto&) { return false; }); failing.event("component_started"); failing.close(); CHECK(failing.counters().writeErrors == 2);
    std::cout << "PASS default OFF, schema fields, identity, roles unavailable, validity, lifecycle/generation, first signals, bounded pressure, fatal reserve, sink failure\n"; return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
