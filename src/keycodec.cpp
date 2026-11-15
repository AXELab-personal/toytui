#include "keycodec.h"

#include <array>
#include <cstdlib>

namespace toytui {

namespace {

constexpr unsigned kCtrl = 1;
constexpr unsigned kAlt = 2;
constexpr unsigned kShift = 4;
constexpr unsigned kMeta = 8;

// Internal form of a decoded key, before output.
struct Key {
    unsigned mods = 0;
    std::string base;      // named (UP, ENTER...) or a character (UTF-8)
    bool named = false;    // true: `base` is a NAME
    bool literal = false;  // true: `base` is the character actually typed (case matters)
};

unsigned modsFromXterm(int m) { // xterm: parameter = 1 + bitmask (1 shift, 2 alt, 4 ctrl, 8 meta)
    if (m < 2) {
        return 0;
    }
    const unsigned bits = static_cast<unsigned>(m - 1);
    return ((bits & 1) ? kShift : 0u) | ((bits & 2) ? kAlt : 0u) | ((bits & 4) ? kCtrl : 0u) |
           ((bits & 8) ? kMeta : 0u);
}

std::string modPrefix(unsigned mods) {
    std::string s;
    if (mods & kCtrl) {
        s += "CTRL-";
    }
    if (mods & kAlt) {
        s += "ALT-";
    }
    if (mods & kShift) {
        s += "SHIFT-";
    }
    if (mods & kMeta) {
        s += "META-";
    }
    return s;
}

bool isLower(char c) { return c >= 'a' && c <= 'z'; }
bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }

// Name of a character base for the modifier notation (characters that
// would confuse the notation get a name).
std::string charBaseName(const std::string& ch) {
    if (ch == " ") {
        return "SPACE";
    }
    if (ch == "[") {
        return "LBRACKET";
    }
    if (ch == "]") {
        return "RBRACKET";
    }
    if (ch == "-") {
        return "MINUS";
    }
    if (ch.size() == 1 && isLower(ch[0])) {
        return std::string(1, static_cast<char>(ch[0] - 'a' + 'A'));
    }
    return ch;
}

std::string format(const Key& k) {
    if (k.literal && k.mods == 0) {
        return k.base == "[" ? "[LBRACKET]" : k.base;
    }
    return "[" + modPrefix(k.mods) + (k.named ? k.base : charBaseName(k.base)) + "]";
}

Key named(const char* n, unsigned mods = 0) {
    Key k;
    k.mods = mods;
    k.base = n;
    k.named = true;
    return k;
}

Key literalChar(const std::string& ch) {
    Key k;
    k.base = ch;
    k.literal = true;
    return k;
}

// A literal character to its modifier form (Alt prefix etc.): letter ->
// uppercase (+SHIFT if it was uppercase), another character unchanged.
Key withModifier(Key k, unsigned extra) {
    if (k.literal) {
        k.literal = false;
        if (k.base.size() == 1 && isUpper(k.base[0])) {
            k.mods |= kShift;
        } else if (k.base.size() == 1 && isLower(k.base[0])) {
            k.base[0] = static_cast<char>(k.base[0] - 'a' + 'A');
        }
    }
    k.mods |= extra;
    return k;
}

const char* tildeName(int code) {
    switch (code) {
    case 1: case 7: return "HOME";
    case 2: return "INS";
    case 3: return "DEL";
    case 4: case 8: return "END";
    case 5: return "PGUP";
    case 6: return "PGDN";
    case 11: return "F1";
    case 12: return "F2";
    case 13: return "F3";
    case 14: return "F4";
    case 15: return "F5";
    case 17: return "F6";
    case 18: return "F7";
    case 19: return "F8";
    case 20: return "F9";
    case 21: return "F10";
    case 23: return "F11";
    case 24: return "F12";
    case 25: return "F13";
    case 26: return "F14";
    case 28: return "F15";
    case 29: return "F16";
    case 31: return "F17";
    case 32: return "F18";
    case 33: return "F19";
    case 34: return "F20";
    default: return nullptr;
    }
}

const char* finalName(char f) { // CSI/SS3 final letter -> name
    switch (f) {
    case 'A': return "UP";
    case 'B': return "DOWN";
    case 'C': return "RIGHT";
    case 'D': return "LEFT";
    case 'H': return "HOME";
    case 'F': return "END";
    case 'P': return "F1";
    case 'Q': return "F2";
    case 'R': return "F3";
    case 'S': return "F4";
    default: return nullptr;
    }
}

std::string utf8Encode(unsigned cp) {
    std::string s;
    if (cp < 0x80) {
        s += static_cast<char>(cp);
    } else if (cp < 0x800) {
        s += static_cast<char>(0xC0 | (cp >> 6));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

struct Step {
    bool needMore = false;
    bool ignore = false;
    Key key;
    std::size_t consumed = 0;
};

Step needMore() {
    Step s;
    s.needMore = true;
    return s;
}
Step ignored(std::size_t n) {
    Step s;
    s.ignore = true;
    s.consumed = n;
    return s;
}
Step keyStep(Key k, std::size_t n) {
    Step s;
    s.key = std::move(k);
    s.consumed = n;
    return s;
}

Step decodeOne(std::string_view buf, bool final);

std::vector<int> parseParams(std::string_view p) {
    std::vector<int> v;
    std::size_t i = 0;
    while (i <= p.size()) {
        const std::size_t semi = p.find(';', i);
        const std::string_view tok = p.substr(i, semi == std::string_view::npos ? std::string_view::npos : semi - i);
        v.push_back(tok.empty() ? 0 : std::atoi(std::string(tok).c_str()));
        if (semi == std::string_view::npos) {
            break;
        }
        i = semi + 1;
    }
    return v;
}

Step decodeCsi(std::string_view buf, bool final) { // buf: ESC [ ...
    if (buf.size() < 3) {
        return final ? ignored(buf.size()) : needMore();
    }
    if (buf[2] == '[') { // Linux console: ESC [ [ A..E = F1..F5
        if (buf.size() < 4) {
            return final ? ignored(buf.size()) : needMore();
        }
        if (buf[3] >= 'A' && buf[3] <= 'E') {
            const std::array<const char*, 5> f = {"F1", "F2", "F3", "F4", "F5"};
            return keyStep(named(f[static_cast<std::size_t>(buf[3] - 'A')]), 4);
        }
        return ignored(4);
    }
    std::size_t j = 2;
    while (j < buf.size() && static_cast<unsigned char>(buf[j]) >= 0x20 &&
           static_cast<unsigned char>(buf[j]) <= 0x3F) {
        ++j; // parameter and intermediate bytes
    }
    if (j >= buf.size()) {
        return final ? ignored(buf.size()) : needMore();
    }
    const char fin = buf[j];
    const std::size_t len = j + 1;
    const std::vector<int> p = parseParams(buf.substr(2, j - 2));
    const unsigned mods = modsFromXterm(p.size() >= 2 ? p[1] : 0);

    if (fin == 'Z') {
        return keyStep(named("TAB", kShift), len);
    }
    if (fin == '~') {
        if (const char* n = tildeName(p.empty() ? 0 : p[0])) {
            return keyStep(named(n, mods), len);
        }
        return ignored(len);
    }
    if (fin == 'u') { // CSI u / kitty-like: code[;modifier]
        const int code = p.empty() ? 0 : p[0];
        const unsigned m = modsFromXterm(p.size() >= 2 ? p[1] : 0);
        if (code == 13) { return keyStep(named("ENTER", m), len); }
        if (code == 9) { return keyStep(named("TAB", m), len); }
        if (code == 27) { return keyStep(named("ESC", m), len); }
        if (code == 127) { return keyStep(named("BS", m), len); }
        if (code >= 32) {
            Key k = literalChar(utf8Encode(static_cast<unsigned>(code)));
            if (m == 0) {
                return keyStep(std::move(k), len);
            }
            Key mk = withModifier(std::move(k), m);
            return keyStep(std::move(mk), len);
        }
        return ignored(len);
    }
    if (const char* n = finalName(fin)) {
        return keyStep(named(n, mods), len);
    }
    return ignored(len); // unknown CSI (e.g. mouse, focus event): dropped
}

Step decodeEsc(std::string_view buf, bool final) { // buf[0] == ESC
    if (buf.size() == 1) {
        return final ? keyStep(named("ESC"), 1) : needMore();
    }
    const char c = buf[1];
    if (c == '[') {
        return decodeCsi(buf, final);
    }
    if (c == 'O') { // SS3
        if (buf.size() < 3) {
            return final ? ignored(buf.size()) : needMore();
        }
        if (const char* n = finalName(buf[2])) {
            return keyStep(named(n), 3);
        }
        return ignored(3);
    }
    if (c == '\x1b') {
        return keyStep(named("ESC"), 1); // ESC ESC: the first lone ESC
    }
    // ESC + key = Alt+key
    Step inner = decodeOne(buf.substr(1), final);
    if (inner.needMore) {
        return inner;
    }
    if (inner.ignore) {
        return ignored(1 + inner.consumed);
    }
    return keyStep(withModifier(std::move(inner.key), kAlt), 1 + inner.consumed);
}

Step decodeOne(std::string_view buf, bool final) {
    if (buf.empty()) {
        return needMore();
    }
    const unsigned char b = static_cast<unsigned char>(buf[0]);
    if (b == 0x1B) {
        return decodeEsc(buf, final);
    }
    if (b == 0x00) {
        return keyStep(named("SPACE", kCtrl), 1);
    }
    if (b == 0x09) { return keyStep(named("TAB"), 1); }
    if (b == 0x0D) { return keyStep(named("ENTER"), 1); }
    if (b == 0x08 || b == 0x7F) { return keyStep(named("BS"), 1); }
    if (b >= 0x01 && b <= 0x1A) { // Ctrl+letter (except the above; 0x0A -> CTRL-J)
        Key k;
        k.mods = kCtrl;
        k.base = std::string(1, static_cast<char>('A' + b - 1));
        return keyStep(std::move(k), 1);
    }
    if (b >= 0x1C && b <= 0x1F) {
        Key k;
        k.mods = kCtrl;
        k.base = std::string(1, "\\]^_"[b - 0x1C]);
        return keyStep(std::move(k), 1);
    }
    if (b < 0x80) {
        return keyStep(literalChar(std::string(1, static_cast<char>(b))), 1);
    }
    // UTF-8
    std::size_t need = 0;
    if ((b & 0xE0) == 0xC0) { need = 2; }
    else if ((b & 0xF0) == 0xE0) { need = 3; }
    else if ((b & 0xF8) == 0xF0) { need = 4; }
    else { return ignored(1); } // invalid lead byte
    if (buf.size() < need) {
        return final ? ignored(buf.size()) : needMore();
    }
    for (std::size_t i = 1; i < need; ++i) {
        if ((static_cast<unsigned char>(buf[i]) & 0xC0) != 0x80) {
            return ignored(1);
        }
    }
    return keyStep(literalChar(std::string(buf.substr(0, need))), need);
}

} // namespace

DecodeResult decodeKey(std::string_view buf, bool final) {
    const Step s = decodeOne(buf, final);
    DecodeResult r;
    if (s.needMore) {
        r.status = DecodeStatus::NeedMore;
        return r;
    }
    r.status = DecodeStatus::Ok;
    r.consumed = s.consumed;
    r.token = s.ignore ? std::string() : format(s.key);
    return r;
}

std::vector<std::string> decodeAllKeys(std::string_view bytes) {
    std::vector<std::string> out;
    while (!bytes.empty()) {
        const DecodeResult r = decodeKey(bytes, true);
        if (r.consumed == 0) {
            break; // defensive: can never happen with final=true
        }
        if (!r.token.empty()) {
            out.push_back(r.token);
        }
        bytes.remove_prefix(r.consumed);
    }
    return out;
}

namespace {

bool isModName(const std::string& s) { return s == "CTRL" || s == "ALT" || s == "SHIFT" || s == "META"; }

bool isNamedBase(const std::string& s) {
    static const char* names[] = {"ESC", "ENTER", "TAB", "BS", "DEL", "INS", "HOME", "END",
                                  "PGUP", "PGDN", "UP", "DOWN", "LEFT", "RIGHT"};
    for (const char* n : names) {
        if (s == n) {
            return true;
        }
    }
    if (s.size() >= 2 && s.size() <= 3 && s[0] == 'F') {
        const int n = std::atoi(s.c_str() + 1);
        return n >= 1 && n <= 24 && s.find_first_not_of("0123456789", 1) == std::string::npos;
    }
    return false;
}

std::size_t utf8CharLen(std::string_view s) {
    if (s.empty()) {
        return 0;
    }
    const unsigned char b = static_cast<unsigned char>(s[0]);
    std::size_t n = 1;
    if ((b & 0xE0) == 0xC0) { n = 2; }
    else if ((b & 0xF0) == 0xE0) { n = 3; }
    else if ((b & 0xF8) == 0xF0) { n = 4; }
    return n <= s.size() ? n : 0;
}

} // namespace

std::optional<std::string> normalizeKeyToken(std::string_view token) {
    if (token.empty()) {
        return std::nullopt;
    }
    if (token.front() != '[') { // plain character: exactly one (UTF-8) character
        const std::size_t n = utf8CharLen(token);
        return n == token.size() ? std::optional<std::string>(std::string(token)) : std::nullopt;
    }
    if (token.size() < 3 || token.back() != ']') {
        return std::nullopt;
    }
    std::string inner(token.substr(1, token.size() - 2));
    for (char& c : inner) {
        if (isLower(c)) {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    std::vector<std::string> seg;
    std::size_t i = 0;
    while (true) {
        const std::size_t dash = inner.find('-', i);
        seg.push_back(inner.substr(i, dash == std::string::npos ? std::string::npos : dash - i));
        if (dash == std::string::npos) {
            break;
        }
        i = dash + 1;
    }
    unsigned mods = 0;
    for (std::size_t k = 0; k + 1 < seg.size(); ++k) {
        if (!isModName(seg[k])) {
            return std::nullopt;
        }
        const unsigned bit = seg[k] == "CTRL" ? kCtrl : seg[k] == "ALT" ? kAlt : seg[k] == "SHIFT" ? kShift : kMeta;
        if (mods & bit) {
            return std::nullopt; // repeated modifier
        }
        mods |= bit;
    }
    std::string base = seg.back();
    if (base.empty()) {
        return std::nullopt; // e.g. "[CTRL-]" — the `-` must be written with the name MINUS
    }
    const bool isNamed = isNamedBase(base);
    const bool isSymbolName = base == "SPACE" || base == "LBRACKET" || base == "RBRACKET" || base == "MINUS";
    const bool isSingleChar = !isNamed && !isSymbolName && utf8CharLen(base) == base.size() &&
                              base != "[" && base != "]" && base != "-";
    if (!isNamed && !isSymbolName && !isSingleChar) {
        return std::nullopt;
    }
    if (mods == 0) {
        if (isNamed) {
            return "[" + base + "]";
        }
        if (base == "SPACE") { return std::string(" "); }
        if (base == "RBRACKET") { return std::string("]"); }
        if (base == "MINUS") { return std::string("-"); }
        if (base == "LBRACKET") { return std::string("[LBRACKET]"); }
        return std::nullopt; // a single-character base without a modifier is not a notation
    }
    return "[" + modPrefix(mods) + base + "]";
}

std::optional<std::vector<std::string>> parseKeySequence(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '[') {
            const std::size_t close = text.find(']', i);
            if (close == std::string_view::npos) {
                return std::nullopt;
            }
            // "[...]": up to the FIRST `]`; a `]` with a modifier gets a name (RBRACKET)
            const auto tok = normalizeKeyToken(text.substr(i, close - i + 1));
            if (!tok) {
                return std::nullopt;
            }
            out.push_back(*tok);
            i = close + 1;
            continue;
        }
        const unsigned char b = static_cast<unsigned char>(text[i]);
        if (b < 0x20 || b == 0x7F) {
            const DecodeResult r = decodeKey(text.substr(i, 1), true);
            if (!r.token.empty()) {
                out.push_back(r.token);
            }
            i += 1;
            continue;
        }
        std::size_t n = utf8CharLen(text.substr(i));
        if (n == 0) {
            n = 1;
        }
        out.emplace_back(text.substr(i, n));
        i += n;
    }
    return out;
}

} // namespace toytui
