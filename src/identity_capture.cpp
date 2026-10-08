#include "vut/identity_capture.hpp"
#include <algorithm>

namespace vut::diagnostic {
std::string hexBytes(std::string_view raw) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : raw) { out += digits[c >> 4]; out += digits[c & 15]; }
    return out;
}
ParsedAck parseIdentityAck(std::string_view raw) {
    ParsedAck out;
    // Longest prefix first. Neither length nor alphabet is a vendor contract.
    for (auto prefix : {"NADS", "NASS", "NASI", "NAPI", "NAV1", "NAV"}) {
        if (raw.starts_with(prefix)) {
            out.nameSpace = prefix; out.payload = raw.substr(out.nameSpace.size()); break;
        }
    }
    if (out.nameSpace.empty()) {
        out.nameSpace = raw.starts_with("N") ? "N_OTHER" : "OTHER";
        out.payload = raw.starts_with("N") ? raw.substr(1) : raw;
        out.notes.push_back("unrecognized_namespace");
    }
    if (out.payload.empty()) out.notes.push_back("empty_payload");
    if (std::any_of(out.payload.begin(), out.payload.end(), [](unsigned char c){return c < 0x20 || c == 0x7f;}))
        out.notes.push_back("control_bytes");
    if (std::any_of(out.payload.begin(), out.payload.end(), [](unsigned char c){return c >= 0x80;}))
        out.notes.push_back("non_ascii_bytes");
    if (std::any_of(out.payload.begin(), out.payload.end(), [](unsigned char c){return c == ' ';}))
        out.notes.push_back("whitespace_present");
    if (std::any_of(out.payload.begin(), out.payload.end(), [](unsigned char c){
        return !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'));
    })) out.notes.push_back("non_alphanumeric_observed_not_rejected");
    return out;
}
}
