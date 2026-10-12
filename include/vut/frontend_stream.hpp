#pragma once
// Local, inherited stdin/stdout control boundary. No named public endpoint,
// network listener, HID access or mutable runtime reference enters the worker.
#include <windows.h>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <map>
#include <set>
#include <thread>

namespace vut::frontend {
using Json = nlohmann::json;
inline Json error(std::string code, std::string message) {
    return {{"version",1},{"ok",false},{"code",std::move(code)},{"message",std::move(message)}};
}
inline Json parse(const std::string& line) {
    std::map<int,std::set<std::string>> keys;
    auto value = Json::parse(line, [&](int depth, Json::parse_event_t event, Json& item) {
        if (depth > 8) throw std::invalid_argument("request depth exceeded");
        if (event == Json::parse_event_t::object_start) keys[depth + 1].clear();
        if (event == Json::parse_event_t::key && !keys[depth].insert(item.get<std::string>()).second)
            throw std::invalid_argument("duplicate field");
        return true;
    });
    if (!value.is_object() || !value.contains("version") || !value["version"].is_number_integer()
        || value["version"] != 1 || !value.contains("id") || !value["id"].is_string()
        || value["id"].get<std::string>().empty() || value["id"].get<std::string>().size() > 64 || !value.contains("operation")
        || !value["operation"].is_string()) throw std::invalid_argument("unsupported or invalid control request");
    return value;
}
class Stream {
    std::atomic_bool closing_{false}, connected_{true};
    std::mutex mutex_;
    std::condition_variable ready_;
    std::optional<Json> request_, response_;
    bool outstanding_ = false;
    std::thread worker_;
    void work() noexcept {
        try {
            const auto input = GetStdHandle(STD_INPUT_HANDLE), output = GetStdHandle(STD_OUTPUT_HANDLE);
            std::string line;
            bool oversized = false;
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!closing_) {
                DWORD available = 0;
                if (!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)) break;
                if (!available) {
                    if (!line.empty() && std::chrono::steady_clock::now() > deadline) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue;
                }
                char c = 0; DWORD count = 0;
                if (!ReadFile(input,&c,1,&count,nullptr) || !count) break;
                if (line.empty()) deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                if (c != '\n') {
                    if (line.size() < 4096) line += c; else oversized = true;
                    continue;
                }
                Json result;
                try {
                    if (oversized) throw std::invalid_argument("request exceeds 4096 bytes");
                    auto request = parse(line);
                    std::unique_lock lock(mutex_);
                    outstanding_ = true;
                    request_ = std::move(request);
                    ready_.wait(lock,[&]{ return closing_ || response_.has_value(); });
                    if (closing_) break;
                    result = std::move(*response_); response_.reset();
                } catch (const std::exception&) { result = error("InvalidRequest","Check control version, fields and frame limits."); }
                auto bytes = result.dump() + "\n";
                if (bytes.size() > 16384) bytes = error("TooLarge","Status exceeds control limits.").dump()+"\n";
                DWORD written = 0;
                if (closing_) break;
                if (!WriteFile(output,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) || written != bytes.size()) break;
                { std::lock_guard lock(mutex_); outstanding_ = false; }
                ready_.notify_all();
                line.clear(); oversized = false;
            }
        } catch (...) { }
        connected_ = false;
    }
public:
    Stream() : worker_([this]{ work(); }) {}
    ~Stream() { close(); }
    bool connected() const { return connected_; }
    void pump(const std::function<Json(const Json&)>& handler) {
        std::optional<Json> request;
        { std::lock_guard lock(mutex_); request.swap(request_); }
        if (!request) return;
        Json result;
        try { result = handler(*request); }
        catch (const std::exception&) { result = error("OperationFailed","The control operation failed. Open diagnostics and retry only after checking device ownership."); }
        result["version"] = 1; result["id"] = (*request)["id"];
        { std::lock_guard lock(mutex_); response_ = std::move(result); }
        ready_.notify_one();
    }
    void drain() {
        std::unique_lock lock(mutex_);
        ready_.wait_for(lock,std::chrono::seconds(1),[&]{return !outstanding_ || !connected_;});
    }
    void close() {
        if (closing_.exchange(true)) return;
        ready_.notify_all();
        if (worker_.joinable()) {
            // Repeat cancellation until exit to cover the race where a writer
            // enters synchronous I/O just after the first cancellation call.
            while (connected_) { CancelSynchronousIo(worker_.native_handle()); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
            worker_.join();
        }
    }
};
} // namespace vut::frontend
