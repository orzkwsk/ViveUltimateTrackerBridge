#pragma once
#include "vut/setup_session.hpp"
#include <ostream>
#include <string_view>

namespace vut::setup {
struct Command {
    std::string name;
    std::vector<std::string> args;
};
Command parseCommand(std::string_view);
void printSetupHelp(std::ostream&);
void printPairingResult(std::ostream&, const PairingResult&);
struct CommandResult { bool keepRunning = true, succeeded = true; };
class SetupConsole {
public:
    explicit SetupConsole(SetupSession& session) : session_(session) {}
    // Same command handling in scripted and interactive presentations; the
    // frontend chooses whether succeeded=false contributes to its exit code.
    CommandResult process(std::string_view, std::int64_t nowNs, std::ostream&, std::ostream& errors);
private:
    bool execute(const Command&, std::int64_t nowNs, std::ostream&);
    SetupSession& session_;
};
}
