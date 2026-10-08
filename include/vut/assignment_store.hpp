#pragma once
#include "vut/setup.hpp"
#include <filesystem>
#include <memory>

namespace vut::setup {
// Versioned sidecar: existing service/map configuration is never rewritten.
// Parse failure throws, unknown JSON members survive edits, stale writers fail.
class AssignmentStore {
public:
    explicit AssignmentStore(std::filesystem::path path);
    ~AssignmentStore();
    AssignmentStore(const AssignmentStore&) = delete;
    AssignmentStore& operator=(const AssignmentStore&) = delete;
    [[nodiscard]] const Assignments& assignments() const;
    Assignments& assignments();
    void save();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
