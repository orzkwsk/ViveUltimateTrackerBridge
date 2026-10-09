#include "vut_epoch_probe_support.hpp"

#include <charconv>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }
struct Options {
    std::filesystem::path control, stop, jsonl;
    int seconds = 90;
};
bool parse(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string_view name(argv[i]);
        if (i + 1 >= argc) return false;
        const std::string_view value(argv[++i]);
        if (name == "--control-file" && options.control.empty()) options.control = argv[i];
        else if (name == "--stop-file" && options.stop.empty()) options.stop = argv[i];
        else if (name == "--jsonl" && options.jsonl.empty()) options.jsonl = argv[i];
        else if (name == "--seconds") {
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), options.seconds);
            if (error != std::errc{} || end != value.data() + value.size() || options.seconds < 1 || options.seconds > 90) return false;
        } else return false;
    }
    if (options.control.empty() || options.stop.empty()) return false;
    const auto control = std::filesystem::absolute(options.control).lexically_normal();
    const auto stop = std::filesystem::absolute(options.stop).lexically_normal();
    return control != stop && (options.jsonl.empty() ||
        (std::filesystem::absolute(options.jsonl).lexically_normal() != control &&
         std::filesystem::absolute(options.jsonl).lexically_normal() != stop));
}
}

int main(int argc, char** argv) {
    using namespace vut::epoch_probe;
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "vut_epoch_probe --control-file PATH --stop-file PATH [--jsonl PATH] [--seconds 1..90]\n"
            "Fixed: ObserveOnly, exclusive HID, configure-on-pair=false, diagnostic-queries=false.\n"
            "Start capture and verify target traffic before launching. Keep A/B OFF and charging USB disconnected.\n"
            "After >=5 seconds OFF baseline, atomically write b_on_requested to control file before asking B ON.\n"
            "Write b_on_confirmed after operator confirmation; the bounded observation starts then.\n"
            "No pairing operations. Receive result requires separate capture/ownership audit for hardware PASS.\n"
            "After observation_finished, stop capture, then create stop file for normal backend shutdown.\n"
            "Failure freezes polling; stop-file/SIGINT aborts safely. Operator wait <=15 min, cleanup wait <=2 min.\n";
        return 0;
    }
    Receipt errors(std::cout);
    try {
        Options options;
        if (!parse(argc, argv, options)) { errors.mark(Mark::InvalidArguments); return 3; }
        // A stale operator signal must never arm a new live run.
        if (std::filesystem::exists(options.control) || std::filesystem::exists(options.stop) ||
            (!options.jsonl.empty() && std::filesystem::exists(options.jsonl))) {
            errors.mark(Mark::ControlUnavailable); return 3;
        }
        std::ofstream jsonl;
        if (!options.jsonl.empty()) {
            jsonl.open(options.jsonl, std::ios::out | std::ios::binary);
            if (!jsonl) { errors.mark(Mark::ReceiptUnavailable); return 3; }
        }
        Receipt receipt(std::cout, jsonl.is_open() ? &jsonl : nullptr);
        const auto started = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count(); };
        std::signal(SIGINT, interrupt); std::signal(SIGTERM, interrupt);
        vut::Backend backend;
        Session session(backend, receipt);
        try {
            if (!session.start()) { session.stop(); return 2; }
            std::int64_t finishedAt = -1;
            bool requested = false, confirmed = false;
            for (;;) {
                const auto now = elapsed(); receipt.time(now);
                if (interrupted || std::filesystem::exists(options.stop)) { session.abort(); break; }
                if (!receipt.good()) { session.abort(); break; }
                if (session.result() != Result::Pending) {
                    if (finishedAt < 0) finishedAt = now;
                    if (now - finishedAt >= 120000) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    continue; // hold sole ownership without reading after terminal result
                }
                if (std::filesystem::exists(options.control)) {
                    std::ifstream control(options.control);
                    std::string command;
                    if (!std::getline(control, command) || command.size() > 32) {
                        receipt.mark(Mark::ControlUnavailable); session.abort(); continue;
                    }
                    if (!command.empty() && command.back() == '\r') command.pop_back();
                    if (command == "b_on_requested") {
                        if (!requested) requested = session.request(now);
                    } else if (command == "b_on_confirmed" && requested) {
                        if (!confirmed) confirmed = session.confirm(now);
                    } else {
                        receipt.mark(Mark::ControlUnavailable); session.abort(); continue;
                    }
                }
                session.tick(now, options.seconds);
                session.poll();
            }
        } catch (...) {
            receipt.time(elapsed()); receipt.mark(Mark::RuntimeFailure, backend.transportGeneration()); session.abort();
        }
        receipt.time(elapsed()); session.stop();
        return session.result() == Result::FreshBurst && receipt.good() ? 0 : 2;
    } catch (...) { errors.mark(Mark::RuntimeFailure); return 3; }
}
