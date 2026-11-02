#pragma once
#include "vut/service_runtime.hpp"
#include <string>
namespace vut {
std::string agentControlRequest(ServiceRuntime&, const std::string&);
}
