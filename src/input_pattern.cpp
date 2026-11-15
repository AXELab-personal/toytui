#include "input_pattern.h"

#include <algorithm>
#include <cctype>

#include "ansi_handler.h"
#include "unicode_width.h"
#include "utf_escape.h"

namespace toytui {

// ---------------------------------------------------------------------------------------------
// UTF-8 helpers
// ---------------------------------------------------------------------------------------------

namespace {

// Length of a UTF-8 character from its lead byte (invalid: 0).
std::size_t utf8Len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 0;
}

// Decoding one character from `s[i]`; invalid/truncated byte: U+FFFD, 1 byte.
char32_t decodeAt(const std::string& s, std::size_t i, std::size_t& len) {
    const std::size_t n = utf8Len(static_cast<unsigned char>(s[i]));
    if (n == 0 || i + n > s.size()) {
        len = 1;
        return 0xFFFD;
    }
    for (std::size_t k = 1; k < n; ++k) {
        if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) {
            len = 1;
            return 0xFFFD;
        }
    }
    len = n;
    return decodeCodePoint(std::string_view(s).substr(i, n));
}

bool isControl(char32_t cp) { return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp < 0xA0); }

// A typeable character occupying exactly one column.
bool isPlainCell(char32_t cp) { return !isControl(cp) && !isWideCodePoint(cp) && !isZeroWidthCodePoint(cp) && cp != 0xFFFD; }

bool inRanges(const std::vector<CharRange>& r, char32_t cp) {
    for (const auto& x : r) {
        if (cp >= x.lo && cp <= x.hi) return true;
    }
    return false;
}

bool isHexDigit(char32_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

} // namespace

std::u32string toU32(const std::string& s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        std::size_t len = 1;
        out.push_back(decodeAt(s, i, len));
        i += len;
    }
    return out;
}

std::string fromU32(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) out += encodeUtf8(c);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Uppercasing
// ---------------------------------------------------------------------------------------------

char32_t toUpperCodePoint(char32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c < 0xB5) return c;
    if (c == 0xB5) return 0x39C;                                    // µ -> Μ
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;         // Latin-1
    if (c == 0xFF) return 0x178;
    if (c >= 0x100 && c <= 0x137) return (c & 1) ? c - 1 : c;       // Latin Ext-A: even = uppercase
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? c : c - 1;       // odd = uppercase
    if (c >= 0x14A && c <= 0x177) return (c & 1) ? c - 1 : c;
    if (c >= 0x179 && c <= 0x17E) return (c & 1) ? c : c - 1;
    if (c == 0x3C2) return 0x3A3;                                   // final sigma
    if (c >= 0x3B1 && c <= 0x3C9) return c - 32;                    // Greek
    if (c >= 0x3AC && c <= 0x3AC) return 0x386;
    if (c >= 0x430 && c <= 0x44F) return c - 32;                    // Cyrillic
    if (c >= 0x450 && c <= 0x45F) return c - 80;
    return c;
}

char32_t toLowerCodePoint(char32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c < 0xC0) return c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x137) return (c & 1) ? c : c + 1;
    if (c >= 0x139 && c <= 0x148) return (c & 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177) return (c & 1) ? c : c + 1;
    if (c == 0x178) return 0xFF;
    if (c >= 0x179 && c <= 0x17E) return (c & 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;
    if (c >= 0x400 && c <= 0x40F) return c + 80;
    return c;
}

// ---------------------------------------------------------------------------------------------
// Character rules
// ---------------------------------------------------------------------------------------------

std::optional<char32_t> acceptCodePoint(const InputBlock& b, const std::u32string& text, std::size_t index,
                                        char32_t cp) {
    switch (b.type) {
        case InputType::Str:
            if (isPlainCell(cp)) return cp;
            return std::nullopt;
        case InputType::StrUpper:
            if (isPlainCell(cp)) return toUpperCodePoint(cp);
            return std::nullopt;
        case InputType::Unsigned:
            if (cp >= '0' && cp <= '9') return cp;
            return std::nullopt;
        case InputType::Signed:
        case InputType::Float: {
            const bool hasMinus = !text.empty() && text[0] == U'-';
            if (cp == U'-') {
                // only in the very first position, and only once
                if (index == 0 && !hasMinus) return cp;
                return std::nullopt;
            }
            if (hasMinus && index == 0) return std::nullopt; // nothing is inserted before the `-`
            if (cp >= '0' && cp <= '9') return cp;
            if (b.type == InputType::Float && cp == U'.') {
                if (text.find(U'.') == std::u32string::npos) return cp;
            }
            return std::nullopt;
        }
        case InputType::Hex:
            if (isHexDigit(cp)) return cp;
            return std::nullopt;
        case InputType::HexUpper:
            if (isHexDigit(cp)) return toUpperCodePoint(cp);
            return std::nullopt;
        case InputType::Custom:
            if (isPlainCell(cp) && inRanges(b.charset, cp)) return cp;
            return std::nullopt;
        case InputType::CustomUpper: {
            if (!isPlainCell(cp)) return std::nullopt;
            const char32_t up = toUpperCodePoint(cp);
            if (inRanges(b.charset, cp) || inRanges(b.charset, up) || inRanges(b.charset, toLowerCodePoint(cp))) return up;
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool blockTextValid(const InputBlock& b, const std::u32string& text) {
    if (text.size() < b.minLen || text.size() > b.maxLen) return false;
    std::u32string built;
    for (char32_t c : text) {
        const auto a = acceptCodePoint(b, built, built.size(), c);
        if (!a || *a != c) return false; // the stored text must already be in converted form
        built.push_back(c);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Pattern parser
// ---------------------------------------------------------------------------------------------

namespace {

struct Parser {
    const std::string& s;
    std::size_t i = 0;
    std::string defaultPh;
    Pattern out;
    std::string err;
    std::size_t finiteTotal = 0;

    bool fail(const std::string& m) {
        err = m;
        return false;
    }

    // Reading one SGR sequence from the BODY of the introducer (after `e[`, `033[`, `x1b[`, `x1B[` or after ESC[).
    // An invalid sequence (not just digits/`;` + `m`) is dropped without effect (as in the texts).
    void readSgr(std::size_t& p) {
        std::string params;
        std::size_t q = p;
        while (q < s.size() && ((s[q] >= '0' && s[q] <= '9') || s[q] == ';')) params += s[q++];
        if (q < s.size() && s[q] == 'm') {
            PatternSegment seg;
            seg.kind = PatternSegment::Kind::Sgr;
            seg.text = "\x1b[" + params + "m";
            out.segments.push_back(seg);
            p = q + 1;
            return;
        }
        // invalid: consume the parameter bytes after `[` and the final byte
        if (q < s.size() && static_cast<unsigned char>(s[q]) >= 0x40 && static_cast<unsigned char>(s[q]) <= 0x7E) ++q;
        p = q;
    }

    // Length of the introducer body at `p` (`e[`: 2, `033[`: 4, `x1b[`/`x1B[`: 4), otherwise 0.
    std::size_t bodyLen(std::size_t p) const {
        auto starts = [&](const char* t) { return s.compare(p, std::char_traits<char>::length(t), t) == 0; };
        if (starts("e[")) return 2;
        if (starts("033[") || starts("x1b[") || starts("x1B[")) return 4;
        return 0;
    }

    bool addLiteral(char32_t cp, const std::string& utf8) {
        if (isControl(cp) || cp == 0xFFFD) return fail("Control character in pattern");
        if (isZeroWidthCodePoint(cp)) {
            // merges into the preceding literal (if it fits in the 4 bytes), otherwise dropped
            if (!out.segments.empty() && out.segments.back().kind == PatternSegment::Kind::Literal &&
                out.segments.back().text.size() + utf8.size() <= 4) {
                out.segments.back().text += utf8;
            }
            return true;
        }
        PatternSegment seg;
        seg.kind = PatternSegment::Kind::Literal;
        seg.text = utf8;
        seg.width = isWideCodePoint(cp) ? 2 : 1;
        out.segments.push_back(seg);
        return true;
    }

    // `\u[HEX]` at `i` (the `\` is already consumed, `s[i+1] == 'u'`): the code point, ALWAYS a plain character in the pattern (never a
    // metacharacter: `\u[7b]` is a literal `{`). An invalid form is an error (an unknown escape is forbidden in the pattern).
    bool unicodeEscape(char32_t& cp, std::string& utf8) {
        const auto esc = decodeUnicodeEscape(s, i);
        if (!esc) return fail("Invalid `\\u[HEX]` escape in pattern");
        std::size_t len = 1;
        cp = decodeAt(esc->utf8, 0, len);
        utf8 = esc->utf8;
        i += esc->consumed;
        return true;
    }

    // Handling a `\` sequence at `i` (outside a block). The pairs before the introducer are literal `\`,
    // the odd remainder is the introducer (the same rule as for the texts).
    bool outsideBackslash() {
        const std::size_t run = backslashRunBeforeIntroducerBody(s, i);
        if (run > 0) {
            for (std::size_t k = 0; k < run / 2; ++k) addLiteral('\\', "\\");
            i += run;
            if (run % 2 == 1) {
                i += bodyLen(i);
                readSgr(i);
            }
            // even: the body (`e[...`) is plain text in the next pass
            return true;
        }
        if (i + 1 >= s.size()) return fail("Trailing backslash in pattern");
        const char n = s[i + 1];
        if (n == 'u') {
            char32_t cp = 0;
            std::string u;
            if (!unicodeEscape(cp, u)) return false;
            if (cp == 0x1b) { // a real ESC (as in the texts): SGR introducer
                if (i >= s.size() || s[i] != '[') return fail("Control character in pattern");
                ++i;
                readSgr(i);
                return true;
            }
            return addLiteral(cp, u);
        }
        if (n == '\\' || n == '{' || n == '}' || n == '%') {
            addLiteral(static_cast<unsigned char>(n), std::string(1, n));
            i += 2;
            return true;
        }
        return fail(std::string("Invalid escape in pattern: `\\") + n + "`");
    }

    // Reading one character inside a block (with escapes); `\ ` is a space too.
    bool blockChar(char32_t& cp, std::string& utf8) {
        if (i >= s.size()) return fail("Unterminated block (missing `}`)");
        if (s[i] == '\\') {
            if (i + 1 >= s.size()) return fail("Trailing backslash in pattern");
            const char n = s[i + 1];
            if (n == 'u') return unicodeEscape(cp, utf8);
            if (n == '\\' || n == '{' || n == '}' || n == '%' || n == ']' || n == '-' || n == ' ') {
                cp = static_cast<unsigned char>(n);
                utf8 = std::string(1, n);
                i += 2;
                return true;
            }
            return fail(std::string("Invalid escape in block: `\\") + n + "`");
        }
        std::size_t len = 1;
        cp = decodeAt(s, i, len);
        utf8 = s.substr(i, len);
        i += len;
        return true;
    }

    bool parseNumber(std::size_t& v, bool& any) {
        any = false;
        v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            any = true;
            v = v * 10 + static_cast<std::size_t>(s[i] - '0');
            if (v > 100000) v = 100000;
            ++i;
        }
        return true;
    }

    bool parseLength(InputBlock& b) {
        std::size_t lo = 0, hi = 0;
        bool hasLo = false, hasHi = false, dash = false;
        parseNumber(lo, hasLo);
        if (i < s.size() && s[i] == '-') {
            dash = true;
            ++i;
            parseNumber(hi, hasHi);
        }
        if (!hasLo && !hasHi) return fail("Missing length in block (expected `N`, `N-`, `-M` or `N-M`)");
        if (!dash) {
            hi = lo;
            hasHi = true;
        }
        if (!hasLo) lo = 0;
        if (hasHi) {
            if (hi == 0) return fail("Block maximum length must be at least 1");
            if (hi < lo) return fail("Block length range is inverted (`N-M` needs N <= M)");
            if (hi > kInputMaxChars) return fail("Block length exceeds the 512 character limit");
            b.maxLen = hi;
            b.unlimited = false;
        } else {
            b.maxLen = kInputMaxChars;
            b.unlimited = true;
            if (lo > kInputMaxChars) return fail("Block length exceeds the 512 character limit");
        }
        b.minLen = lo;
        return true;
    }

    bool parseCharset(InputBlock& b) {
        // `[` already consumed
        for (;;) {
            if (i >= s.size()) return fail("Unterminated character set (missing `]`)");
            if (s[i] == ']') {
                ++i;
                break;
            }
            char32_t lo = 0;
            std::string u;
            if (!setChar(lo, u)) return false;
            char32_t hi = lo;
            if (i + 1 < s.size() && s[i] == '-' && s[i + 1] != ']') {
                ++i;
                std::string u2;
                if (!setChar(hi, u2)) return false;
                if (hi < lo) return fail("Character set range is inverted");
            }
            b.charset.push_back({lo, hi});
        }
        if (b.charset.empty()) return fail("Empty character set");
        return true;
    }

    bool setChar(char32_t& cp, std::string& utf8) {
        if (i >= s.size()) return fail("Unterminated character set (missing `]`)");
        if (s[i] == '\\') {
            if (i + 1 >= s.size()) return fail("Trailing backslash in pattern");
            const char n = s[i + 1];
            if (n == 'u') return unicodeEscape(cp, utf8);
            if (n == '\\' || n == ']' || n == '-' || n == '}' || n == '{' || n == '%' || n == ' ') {
                cp = static_cast<unsigned char>(n);
                utf8 = std::string(1, n);
                i += 2;
                return true;
            }
            return fail(std::string("Invalid escape in character set: `\\") + n + "`");
        }
        std::size_t len = 1;
        cp = decodeAt(s, i, len);
        utf8 = s.substr(i, len);
        i += len;
        return true;
    }

    bool parseBlock() {
        // `{` already consumed
        InputBlock b;
        b.placeholder = defaultPh;
        bool haveSpec = false;
        for (;;) {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
            if (i >= s.size()) return fail("Unterminated block (missing `}`)");
            if (s[i] == '}') {
                ++i;
                break;
            }
            if (s[i] != '%') return fail("Unexpected text in block (expected `%type`, `%h`, or an option)");
            ++i;
            if (i >= s.size()) return fail("Unterminated block (missing `}`)");
            const char t = s[i];
            if (t == 'h') { // placeholder: the next character
                ++i;
                char32_t cp = 0;
                std::string u;
                if (i < s.size() && (s[i] == ' ' || s[i] == '}')) {
                    return fail("Missing placeholder character after `%h` (use `\\ ` for a space)");
                }
                if (!blockChar(cp, u)) return false;
                if (!isPlainCell(cp)) return fail("Placeholder must be a single-column printable character");
                b.placeholder = u;
                continue;
            }
            if (t == 'c' || t == 'C') {
                if (i + 1 < s.size() && s[i + 1] == '[') {
                    if (haveSpec) return fail("A block can have only one type");
                    i += 2;
                    b.type = t == 'c' ? InputType::Custom : InputType::CustomUpper;
                    if (!parseCharset(b) || !parseLength(b)) return false;
                    haveSpec = true;
                    continue;
                }
            }
            static const std::string types = "sSuifxX";
            if (types.find(t) != std::string::npos &&
                !(i + 1 < s.size() && std::isalpha(static_cast<unsigned char>(s[i + 1])))) {
                if (haveSpec) return fail("A block can have only one type");
                ++i;
                switch (t) {
                    case 's': b.type = InputType::Str; break;
                    case 'S': b.type = InputType::StrUpper; break;
                    case 'u': b.type = InputType::Unsigned; break;
                    case 'i': b.type = InputType::Signed; break;
                    case 'f': b.type = InputType::Float; break;
                    case 'x': b.type = InputType::Hex; break;
                    default: b.type = InputType::HexUpper; break;
                }
                if (!parseLength(b)) return false;
                haveSpec = true;
                continue;
            }
            // option: %name:value
            std::string name;
            while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) name += s[i++];
            if (name.empty() || i >= s.size() || s[i] != ':') {
                return fail("Unknown block item: `%" + (name.empty() ? std::string(1, t) : name) + "`");
            }
            ++i;
            std::string value;
            while (i < s.size() && (std::isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_')) value += s[i++];
            if (value.empty()) return fail("Missing value for block option `%" + name + "`");
            if (name == "csr") {
                b.csr = value;
            } else if (name == "mode") {
                b.mode = value;
            } else if (name == "adv") {
                b.adv = value;
            } else {
                return fail("Unknown block option: `%" + name + "`");
            }
        }
        if (!haveSpec) return fail("Block without a type (e.g. `{%s0-}`)");
        if (!b.unlimited) finiteTotal += b.maxLen;
        if (finiteTotal > kInputMaxChars) return fail("The pattern exceeds the 512 character limit");
        PatternSegment seg;
        seg.kind = PatternSegment::Kind::Block;
        seg.block = out.blocks.size();
        out.blocks.push_back(std::move(b));
        out.segments.push_back(seg);
        return true;
    }

    bool run() {
        while (i < s.size()) {
            const char c = s[i];
            if (c == '\\') {
                if (!outsideBackslash()) return false;
            } else if (c == '{') {
                ++i;
                if (!parseBlock()) return false;
            } else if (c == '\x1b') {
                if (i + 1 < s.size() && s[i + 1] == '[') {
                    i += 2;
                    readSgr(i);
                } else {
                    return fail("Control character in pattern");
                }
            } else {
                std::size_t len = 1;
                const char32_t cp = decodeAt(s, i, len);
                if (!addLiteral(cp, s.substr(i, len))) return false;
                i += len;
            }
        }
        if (out.blocks.empty()) return fail("The pattern needs at least one block (e.g. `{%s0-}`)");
        return true;
    }
};

} // namespace

std::optional<Pattern> parsePattern(const std::string& text, const std::string& defaultPlaceholder,
                                    std::string& err) {
    Parser p{text, 0, defaultPlaceholder, {}, {}, 0};
    // checking the default placeholder
    const std::u32string ph = toU32(defaultPlaceholder);
    if (ph.size() != 1 || !isPlainCell(ph[0])) {
        err = "Placeholder must be a single-column printable character";
        return std::nullopt;
    }
    if (!p.run()) {
        err = p.err;
        return std::nullopt;
    }
    return std::move(p.out);
}

} // namespace toytui
