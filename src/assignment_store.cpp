#include "vut/assignment_store.hpp"
#include "vut/assignment_lock.hpp"
#include "nlohmann/json.hpp"
#include <chrono>
#include <fstream>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace vut::setup::provisional {
namespace {
using Json = nlohmann::json;
std::optional<std::string> read(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return {};
    if (std::filesystem::file_size(path) > 1024*1024) throw std::runtime_error("assignment file exceeds 1 MiB");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read assignments: " + path.string());
    std::string bytes{std::istreambuf_iterator<char>(in), {}};
    if (in.bad()) throw std::runtime_error("assignment read failed");
    return bytes;
}
Json parse(const std::string& bytes) {
    // nlohmann's default parser accepts duplicate keys; config must fail closed.
    std::vector<std::set<std::string>> keys;
    return Json::parse(bytes, [&](int depth, Json::parse_event_t event, Json& value) {
        if (depth > 16) throw std::invalid_argument("assignment JSON nesting exceeds 16");
        if (event == Json::parse_event_t::object_start) keys.emplace_back();
        else if (event == Json::parse_event_t::key && !keys.back().insert(value.get<std::string>()).second)
            throw std::invalid_argument("duplicate assignment JSON field");
        else if (event == Json::parse_event_t::object_end) keys.pop_back();
        return true;
    });
}
class ExclusiveFile {
public:
    explicit ExclusiveFile(std::filesystem::path path) : path_(std::move(path)) {
#ifdef _WIN32
        handle_ = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw std::runtime_error("assignment save lock/temp unavailable: " + path_.string());
#else
        handle_ = ::open(path_.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (handle_ < 0) throw std::runtime_error("assignment save lock/temp unavailable: " + path_.string());
#endif
    }
    ~ExclusiveFile() { close(); std::error_code ec; std::filesystem::remove(path_, ec); }
    void write(const std::string& bytes) {
#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) ||
            written != bytes.size() || !FlushFileBuffers(handle_)) throw std::runtime_error("assignment durable write failed");
#else
        if (::write(handle_, bytes.data(), bytes.size()) != static_cast<ssize_t>(bytes.size()) || ::fsync(handle_) != 0)
            throw std::runtime_error("assignment durable write failed");
#endif
    }
    void close() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
#else
        if (handle_ >= 0) { ::close(handle_); handle_ = -1; }
#endif
    }
private:
    std::filesystem::path path_;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
    int handle_ = -1;
#endif
};
}
struct AssignmentStore::Impl {
    explicit Impl(std::vector<std::string> labels) : values(std::move(labels)) {}
    std::filesystem::path path;
    std::optional<std::string> original;
    Json document;
    Assignments values;
};
AssignmentStore::AssignmentStore(std::filesystem::path path, std::vector<std::string> labels)
    : impl_(std::make_unique<Impl>(std::move(labels))) {
    impl_->path = std::move(path); impl_->original = read(impl_->path);
    auto& doc = impl_->document;
    doc = impl_->original ? parse(*impl_->original) : Json{{"schema_version", 1}, {"assignments", Json::object()}};
    if (!doc.is_object() || !doc.contains("schema_version") || !doc["schema_version"].is_number_integer() ||
        doc["schema_version"] != 1 || !doc.contains("assignments") || !doc["assignments"].is_object())
        throw std::invalid_argument("unsupported/invalid assignment schema; original file preserved");
    if (doc["assignments"].size() > 256) throw std::invalid_argument("too many assignments");
    for (const auto& [key, value] : doc["assignments"].items()) {
        if (!value.is_object() || !value.contains("role") || !value["role"].is_string())
            throw std::invalid_argument("invalid assignment: " + key);
        impl_->values.assign(parseStableIdentity(key), value["role"].get<std::string>());
    }
}
AssignmentStore::~AssignmentStore() = default;
const Assignments& AssignmentStore::assignments() const { return impl_->values; }
Assignments& AssignmentStore::assignments() { return impl_->values; }
void AssignmentStore::save() {
    if (impl_->values.entries().size() > 256) throw std::runtime_error("too many assignments");
    auto doc = impl_->document;
    auto records = Json::object();
    for (const auto& [id, role] : impl_->values.entries()) {
        records[id] = doc["assignments"].contains(id) ? doc["assignments"][id] : Json::object();
        records[id]["role"] = role;
    }
    doc["assignments"] = std::move(records);
    const auto bytes = doc.dump(2) + "\n";
    if (bytes.size() > 1024*1024) throw std::runtime_error("assignment output exceeds 1 MiB");
    WriterLock lock(impl_->path);
    if (read(impl_->path) != impl_->original) throw std::runtime_error("assignments changed on disk; reload before saving");
    auto tempPath = impl_->path;
    tempPath += ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    ExclusiveFile temporary(tempPath); temporary.write(bytes); temporary.close();
#ifdef _WIN32
    if (!MoveFileExW(tempPath.c_str(), impl_->path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("atomic assignment replacement failed; original file preserved");
#else
    std::filesystem::rename(tempPath, impl_->path);
#endif
    impl_->original = bytes; impl_->document = std::move(doc);
}
}
