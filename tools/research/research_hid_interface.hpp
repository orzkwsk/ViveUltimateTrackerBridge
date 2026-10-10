#pragma once
#include <algorithm>
#include <cwctype>
#include <string>
#include <string_view>

namespace vut::active_hil {
inline bool qualifiesDongleInterface(std::wstring_view path) {
    std::wstring lower(path); std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    return lower.find(L"vid_0bb4&pid_0350") != std::wstring::npos &&
        (lower.find(L"&mi_") == std::wstring::npos || lower.find(L"&mi_00") != std::wstring::npos);
}
}
