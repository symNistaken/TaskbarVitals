#pragma once
// Portable parsing, formatting, and editing helpers for the IgnoreDevices VID:PID list.
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace ignore_devices {

struct VidPid {
    std::uint16_t vid = 0;
    std::uint16_t pid = 0;
    bool operator==(const VidPid& o) const { return vid == o.vid && pid == o.pid; }
    bool operator!=(const VidPid& o) const { return !(*this == o); }
};

namespace detail {

inline bool IsSeparator(wchar_t c) {
    return c == L',' || c == L';' || c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

inline bool ParseHex16(const std::wstring& s, std::uint16_t& out) {
    size_t pos = 0;
    if (s.size() >= 2 && s[0] == L'0' && (s[1] == L'x' || s[1] == L'X')) pos = 2;
    size_t digits = s.size() - pos;
    if (digits == 0 || digits > 4) return false;
    std::uint32_t val = 0;
    for (size_t i = pos; i < s.size(); ++i) {
        wchar_t c = s[i];
        std::uint32_t d = 0;
        if (c >= L'0' && c <= L'9') d = static_cast<std::uint32_t>(c - L'0');
        else if (c >= L'a' && c <= L'f') d = static_cast<std::uint32_t>(c - L'a' + 10);
        else if (c >= L'A' && c <= L'F') d = static_cast<std::uint32_t>(c - L'A' + 10);
        else return false;
        val = (val << 4) | d;
    }
    out = static_cast<std::uint16_t>(val);
    return true;
}

inline std::wstring Hex4(std::uint16_t v) {
    static constexpr wchar_t kDigits[] = L"0123456789ABCDEF";
    std::wstring s(4, L'0');
    s[0] = kDigits[(v >> 12) & 0xF];
    s[1] = kDigits[(v >> 8) & 0xF];
    s[2] = kDigits[(v >> 4) & 0xF];
    s[3] = kDigits[v & 0xF];
    return s;
}

}  // namespace detail

inline bool Contains(const std::vector<VidPid>& list, std::uint16_t vid, std::uint16_t pid) {
    for (const auto& item : list)
        if (item.vid == vid && item.pid == pid) return true;
    return false;
}

inline std::vector<VidPid> Parse(const std::wstring& raw) {
    std::vector<VidPid> out;
    size_t i = 0;
    while (i < raw.size()) {
        while (i < raw.size() && detail::IsSeparator(raw[i])) ++i;
        if (i >= raw.size()) break;
        size_t end = i;
        while (end < raw.size() && !detail::IsSeparator(raw[end])) ++end;
        std::wstring token = raw.substr(i, end - i);
        i = end;
        size_t colon = token.find(L':');
        if (colon == std::wstring::npos || token.find(L':', colon + 1) != std::wstring::npos) continue;
        std::uint16_t vid = 0, pid = 0;
        if (!detail::ParseHex16(token.substr(0, colon), vid)) continue;
        if (!detail::ParseHex16(token.substr(colon + 1), pid)) continue;
        if (vid == 0 && pid == 0) continue;
        if (!Contains(out, vid, pid)) out.push_back({ vid, pid });
    }
    return out;
}

inline std::wstring FormatPair(std::uint16_t vid, std::uint16_t pid) {
    return detail::Hex4(vid) + L":" + detail::Hex4(pid);
}

inline std::wstring FormatList(const std::vector<VidPid>& items) {
    std::wstring out;
    for (const auto& item : items) {
        if (!out.empty()) out += L", ";
        out += FormatPair(item.vid, item.pid);
    }
    return out;
}

inline std::wstring Add(const std::wstring& raw, std::uint16_t vid, std::uint16_t pid) {
    auto items = Parse(raw);
    if (vid != 0 && pid != 0 && !Contains(items, vid, pid)) items.push_back({ vid, pid });
    return FormatList(items);
}

inline std::wstring Remove(const std::wstring& raw, std::uint16_t vid, std::uint16_t pid) {
    auto items = Parse(raw);
    items.erase(std::remove(items.begin(), items.end(), VidPid{ vid, pid }), items.end());
    return FormatList(items);
}

}  // namespace ignore_devices
