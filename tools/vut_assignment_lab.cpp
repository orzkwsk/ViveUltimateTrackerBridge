// Internal provisional experiment. Never launched or packaged as product setup.
#include "vut/assignment_store.hpp"
#include "vut/setup_console.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace vut::setup;
using namespace vut::setup::provisional;
ConflictAction conflict(const Command& command) {
    if (command.args.size() < 3 || command.args[2] == "cancel") return ConflictAction::Cancel;
    if (command.args[2] == "swap") return ConflictAction::Swap;
    if (command.args[2] == "replace") return ConflictAction::Replace;
    throw std::invalid_argument("choose Swap / Replace / Cancel");
}
}
int main(int argc, char** argv) try {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        std::cout << "INTERNAL / PROVISIONAL: no production body-role contract or apply API.\n"
                  << "vut_assignment_lab PATH --offline|--validate --labels LABEL [LABEL...]\n";
        return 0;
    }
    if (argc < 5 || std::string(argv[3]) != "--labels" ||
        (std::string(argv[2]) != "--offline" && std::string(argv[2]) != "--validate"))
        throw std::invalid_argument("explicit internal mode and label policy required; use --help");
    AssignmentStore store(argv[1], std::vector<std::string>(argv+4, argv+argc));
    std::cout << "INTERNAL PROVISIONAL legacy v1 sidecar; compatibility NOT promised.\n"
              << "Stable hardware identity acquisition and body assignment apply: BLOCKED.\n";
    if (std::string(argv[2]) == "--validate") return 0;
    bool failed = false, dirty = false;
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            const auto command = parseCommand(line);
            const auto& args = command.args;
            const auto arity = [&](std::size_t low, std::size_t high) {
                if (args.size() < low || args.size() > high) throw std::invalid_argument("unexpected command arguments");
            };
            auto& values = store.assignments();
            if (command.name.empty()) continue;
            if (command.name == "quit") { arity(0,0); break; }
            if (command.name == "reassign") {
                arity(2,3); const auto id = parseStableIdentity(args[0]);
                if (values.role(id).empty()) throw std::invalid_argument("unknown saved stable identity");
                values.assign(id, args[1], conflict(command)); dirty = true;
            } else if (command.name == "unassign") {
                arity(1,1); const auto id = parseStableIdentity(args[0]);
                if (values.role(id).empty()) throw std::invalid_argument("unknown saved stable identity");
                values.unassign(id); dirty = true;
            } else if (command.name == "swap") {
                arity(2,2); const auto a = parseStableIdentity(args[0]), b = parseStableIdentity(args[1]);
                if (values.role(a).empty() || values.role(b).empty()) throw std::invalid_argument("swap requires two saved identities");
                values.swap(a,b); dirty = true;
            } else if (command.name == "clear-all") {
                arity(1,1); if (args[0] != "confirm") throw std::invalid_argument("use clear-all confirm");
                values.clear(); dirty = true;
            } else if (command.name == "validate") { arity(0,0); std::cout << "Provisional label policy valid; apply BLOCKED.\n"; }
            else if (command.name == "refresh") {
                arity(0,0);
                for (const auto& [id, label] : values.entries())
                    std::cout << "saved_stable_id=" << id << " provisional_label=" << label << " unresolved_or_disconnected (retained)\n";
            } else if (command.name == "save") {
                arity(0,0); store.save(); dirty = false; std::cout << "Internal provisional sidecar saved.\n";
            } else throw std::invalid_argument("unknown internal command");
        } catch (const std::exception& e) { failed = true; std::cerr << "ERROR: " << e.what() << '\n'; }
    }
    if (std::cin.bad()) throw std::runtime_error("script input read failed");
    if (dirty) std::cerr << "Unsaved provisional changes were discarded.\n";
    return failed ? 1 : 0;
} catch (const std::exception& e) { std::cerr << "ERROR: " << e.what() << '\n'; return 1; }
