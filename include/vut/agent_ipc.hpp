#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace vut {
class AgentIpc {
public:
    AgentIpc();
    ~AgentIpc();
    AgentIpc(const AgentIpc&) = delete;
    AgentIpc& operator=(const AgentIpc&) = delete;
    const std::wstring& name() const noexcept;
    void allowOwnedChild(std::uint32_t pid) noexcept;
    void pump(const std::function<std::string(const std::string&)>&);
    bool healthy() const noexcept;
private:
    struct Native;
    std::unique_ptr<Native> native_;
};
}
