#pragma once
#include <memory>
namespace vut {
enum class AgentTrayAction { None, Open, Exit };
enum class AgentTrayState { Starting, Visible, Failed, Stopped };
class AgentTray {
public:
    AgentTray();
    ~AgentTray();
    AgentTray(const AgentTray&) = delete;
    AgentTray& operator=(const AgentTray&) = delete;
    AgentTrayAction takeAction() noexcept;
    AgentTrayState state() const noexcept;
    bool responsive() const noexcept;
private:
    struct Native;
    std::unique_ptr<Native> native_;
};
}
