#include "vut/provisional_assignments.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace vut::setup::provisional {
Assignments::Assignments(std::vector<std::string> labels) : acceptedLabels_(std::move(labels)) {
    if (acceptedLabels_.empty() || acceptedLabels_.size() > 256)
        throw std::invalid_argument("explicit provisional label policy required");
    std::set<std::string> unique;
    for (const auto& label : acceptedLabels_) {
        if (label.empty() || label.size() > 256 || !unique.insert(label).second ||
            std::any_of(label.begin(), label.end(), [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
            throw std::invalid_argument("invalid provisional label policy");
    }
}
void Assignments::assign(const StableIdentity& identity, const std::string& value, ConflictAction action) {
    const auto key = identity.key();
    if (std::find(acceptedLabels_.begin(), acceptedLabels_.end(), value) == acceptedLabels_.end())
        throw std::invalid_argument("unknown provisional label: " + value);
    if (action != ConflictAction::Cancel && action != ConflictAction::Replace && action != ConflictAction::Swap)
        throw std::invalid_argument("invalid conflict action");
    const auto occupied = std::find_if(entries_.begin(), entries_.end(),
        [&](const auto& e) { return e.second == value && e.first != key; });
    if (occupied != entries_.end()) {
        if (action == ConflictAction::Cancel)
            throw std::invalid_argument("role already assigned to " + occupied->first + "; choose Swap / Replace / Cancel");
        const auto previous = role(identity);
        if (action == ConflictAction::Swap && !previous.empty()) occupied->second = previous;
        else entries_.erase(occupied); // swapping with unassigned transfers the role
    }
    entries_[key] = value;
}
void Assignments::unassign(const StableIdentity& id) { entries_.erase(id.key()); }
void Assignments::swap(const StableIdentity& a, const StableIdentity& b) {
    const auto ka = a.key(), kb = b.key();
    if (ka == kb) return;
    const auto ra = role(a), rb = role(b);
    unassign(a); unassign(b);
    if (!rb.empty()) entries_[ka] = rb;
    if (!ra.empty()) entries_[kb] = ra;
}
void Assignments::clear() { entries_.clear(); }
std::string Assignments::role(const StableIdentity& id) const {
    const auto it = entries_.find(id.key());
    return it == entries_.end() ? std::string{} : it->second;
}
}
