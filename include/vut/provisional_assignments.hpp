#pragma once
#include "vut/setup.hpp"

// Internal experimentation only. No production body-role vocabulary, apply API,
// or schema compatibility promise. Caller supplies the accepted opaque labels.
namespace vut::setup::provisional {
enum class ConflictAction { Cancel, Replace, Swap };
class Assignments {
public:
    explicit Assignments(std::vector<std::string> acceptedLabels);
    void assign(const StableIdentity&, const std::string& label,
                ConflictAction = ConflictAction::Cancel);
    void unassign(const StableIdentity&);
    void swap(const StableIdentity&, const StableIdentity&);
    void clear();
    [[nodiscard]] std::string role(const StableIdentity&) const; // legacy internal accessor
    [[nodiscard]] const std::map<std::string, std::string>& entries() const { return entries_; }
private:
    std::vector<std::string> acceptedLabels_;
    std::map<std::string, std::string> entries_;
};
}
