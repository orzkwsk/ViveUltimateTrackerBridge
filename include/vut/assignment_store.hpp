#pragma once
#include "vut/provisional_assignments.hpp"
#include <filesystem>
#include <memory>

namespace vut::setup::provisional {
// Internal legacy v1 sidecar, NOT a production body-role storage contract.
// Existing service/map configuration is never rewritten.
// Parse failure throws, unknown JSON members survive edits, stale writers fail.
class AssignmentStore {
public:
    AssignmentStore(std::filesystem::path path, std::vector<std::string> acceptedLabels);
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
