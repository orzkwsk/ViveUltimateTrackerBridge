#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace vut::diagnostic {
// Diagnostic classification only. No StableIdentity or shared wire model.
struct ParsedAck {
    std::string nameSpace;
    std::string payload;
    std::vector<std::string> notes;
};
ParsedAck parseIdentityAck(std::string_view raw);
std::string hexBytes(std::string_view raw);
}
