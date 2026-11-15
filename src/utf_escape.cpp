#include "utf_escape.h"

#include <cstdint>

namespace toytui {

std::string encodeUtf8(char32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return out;
}

namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

std::optional<UnicodeEscape> decodeUnicodeEscape(std::string_view s, std::size_t pos) {
    if (pos + 3 > s.size() || s[pos] != '\\' || s[pos + 1] != 'u' || s[pos + 2] != '[') {
        return std::nullopt;
    }
    std::size_t i = pos + 3;
    std::uint32_t value = 0;
    int digits = 0;
    while (i < s.size() && hexValue(s[i]) >= 0) {
        if (++digits > 8) {
            return std::nullopt; // 9+ digits: not a code
        }
        value = (value << 4) | static_cast<std::uint32_t>(hexValue(s[i]));
        ++i;
    }
    if (digits < 2 || i >= s.size() || s[i] != ']') {
        return std::nullopt;
    }
    if (value == 0 || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
        return std::nullopt;
    }
    return UnicodeEscape{encodeUtf8(static_cast<char32_t>(value)), i + 1 - pos};
}

std::string decodeUnicodeEscapesInFile(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] == '\\') {
            if (i + 1 < s.size() && s[i + 1] == '\\') {
                if (const auto esc = decodeUnicodeEscape(s, i + 1)) {
                    out.append(s.substr(i + 1, esc->consumed)); // `\\u[HEX]` -> literal `\u[HEX]`
                    i += 1 + esc->consumed;
                    continue;
                }
            } else if (const auto esc = decodeUnicodeEscape(s, i)) {
                out += esc->utf8;
                i += esc->consumed;
                continue;
            }
        }
        out.push_back(s[i]);
        ++i;
    }
    return out;
}

} // namespace toytui
