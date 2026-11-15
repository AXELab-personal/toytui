#include "param_types.h"

#include <charconv>

#include "cell.h"

namespace toytui {

std::optional<int> parseStrictInt(const std::string& s) {
    if (s.empty()) {
        return std::nullopt;
    }
    int value = 0;
    auto res = std::from_chars(s.data(), s.data() + s.size(), value);
    if (res.ec != std::errc() || res.ptr != s.data() + s.size()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::uint8_t> parseColorValue(const std::string& s) {
    if (auto named = colorNameToIndex(s)) {
        return named;
    }
    if (auto n = parseStrictInt(s); n && *n >= 0 && *n <= 255) {
        return static_cast<std::uint8_t>(*n);
    }
    return std::nullopt;
}

std::optional<bool> parseBoolValue(const std::string& s) {
    if (s == "on" || s == "true" || s == "1") {
        return true;
    }
    if (s == "off" || s == "false" || s == "0") {
        return false;
    }
    return std::nullopt;
}

namespace {

std::optional<std::uint16_t> parseAttribBinary(const std::string& digits) {
    if (digits.empty() || digits.size() > 16) {
        return std::nullopt;
    }
    std::uint16_t value = 0;
    for (char c : digits) {
        if (c != '0' && c != '1') {
            return std::nullopt;
        }
        value = static_cast<std::uint16_t>((value << 1) | (c - '0'));
    }
    return value;
}

std::optional<std::uint16_t> parseAttribHex(const std::string& digits) {
    if (digits.empty() || digits.size() > 4) {
        return std::nullopt;
    }
    std::uint16_t value = 0;
    for (char c : digits) {
        std::uint16_t nibble;
        if (c >= '0' && c <= '9') {
            nibble = static_cast<std::uint16_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nibble = static_cast<std::uint16_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            nibble = static_cast<std::uint16_t>(c - 'A' + 10);
        } else {
            return std::nullopt;
        }
        value = static_cast<std::uint16_t>((value << 4) | nibble);
    }
    return value;
}

// VALUE-level attrib name aliases — valid ONLY inside the
// `attrib:"..."` list (see the header comment of param_types.h);
// not to be mixed with cli_parser's parameter-name tables.
std::optional<std::string> canonicalAttribName(const std::string& name) {
    if (name == "ul") {
        return std::string("underline");
    }
    if (name == "strike") {
        return std::string("strikethrough");
    }
    if (name == "reverse") {
        return std::string("inverse");
    }
    return name;
}

std::string trim(const std::string& s) {
    std::size_t b = s.find_first_not_of(' ');
    std::size_t e = s.find_last_not_of(' ');
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::optional<std::uint16_t> parseAttribNamedList(const std::string& s) {
    std::uint16_t value = 0;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t comma = s.find(',', start);
        const std::string raw = trim(s.substr(start, comma == std::string::npos
                                                          ? std::string::npos
                                                          : comma - start));
        if (raw.empty()) {
            return std::nullopt;
        }
        const auto canonical = canonicalAttribName(raw);
        const auto bit = canonical ? namedAttribBit(*canonical) : std::nullopt;
        if (!bit) {
            return std::nullopt; // unknown name, or a technical (13-15) bit by name
        }
        value = static_cast<std::uint16_t>(value | *bit);
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return value;
}

} // namespace

std::optional<std::uint16_t> parseAttribValue(const std::string& s) {
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) {
        return parseAttribBinary(s.substr(2));
    }
    if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        return parseAttribHex(s.substr(2));
    }
    return parseAttribNamedList(s);
}

int storedInt(const std::string& stored) { return *parseStrictInt(stored); }

std::optional<int> paramInt(const ElementParams& p, const std::string& name) {
    const std::string* v = p.getSet(name);
    return v != nullptr ? parseStrictInt(*v) : std::nullopt;
}

int paramInt(const ElementParams& p, const std::string& name, int fallback) {
    return paramInt(p, name).value_or(fallback);
}

ColorPair applyStatePair(const ElementParams& p, const std::string& prefix, ColorPair under, bool swapWhenUnset) {
    const std::string* bg = p.getSet(prefix + "_bgcol");
    const std::string* fg = p.getSet(prefix + "_fgcol");
    if (bg == nullptr && fg == nullptr) {
        return swapWhenUnset ? ColorPair{under.fg, under.bg} : under;
    }
    return ColorPair{bg != nullptr ? storedColor(*bg) : under.bg, fg != nullptr ? storedColor(*fg) : under.fg};
}

std::uint8_t storedColor(const std::string& stored) {
    return static_cast<std::uint8_t>(storedInt(stored));
}

} // namespace toytui
