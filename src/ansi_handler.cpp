#include "ansi_handler.h"
#include "unicode_width.h"

#include <algorithm>
#include <cstdlib>
#include <optional>

namespace toytui {

namespace {

// The four accepted introducers: the real ESC (0x1B) byte, OR the
// literally typed backslash sequences (so that it also works when typed
// by hand / with echo from a shell). The length of each includes the '['.
struct Introducer {
    const char* text;
    int len; // including the '['
};
constexpr Introducer kIntroducers[] = {
    {"\x1b[", 2}, // real ESC byte
    {"\\e[", 3},   {"\\033[", 5}, {"\\x1b[", 5}, {"\\x1B[", 5},
};

std::optional<int> matchIntroducer(const std::string& s, std::size_t pos) {
    for (const auto& intro : kIntroducers) {
        const std::size_t n = static_cast<std::size_t>(intro.len);
        if (s.compare(pos, n, intro.text) == 0) {
            return intro.len;
        }
    }
    return std::nullopt;
}

bool hasIntroducerBodyAt(const std::string& s, std::size_t pos) {
    for (const char* body : {"e[", "033[", "x1b[", "x1B["}) {
        if (s.compare(pos, std::char_traits<char>::length(body), body) == 0) {
            return true;
        }
    }
    return false;
}

bool isFinalByte(unsigned char c) { return c >= 0x40 && c <= 0x7E; }

// The initial/reset state: see neutralBlankCell() (the parse state's
// character is overwritten for every cell).
Cell blankState() { return neutralBlankCell(); }

void setAttribBit(Cell& state, std::uint16_t bit, bool on) {
    state.attrib = static_cast<std::uint16_t>(on ? (state.attrib | bit) : (state.attrib & ~bit));
}

// To be called for an SGR code that actually affects a character attribute: the
// character now has its OWN (non-inherited) attribute.
void markAttribTouched(Cell& state) {
    setAttribBit(state, attrib_bits::ATTRIB_UNSET, false);
}

} // namespace

// xterm 256-colour approximation (6x6x6 colour cube + grey ramp) of a
// true-colour (r,g,b, 0-255) value — true colour itself is not
// supported, but we round to the nearest 256-palette colour
// rather than drop it without effect.
std::uint8_t approximateTrueColor(int r, int g, int b) {
    const int avg = (r + g + b) / 3;
    const int spread = std::max({r, g, b}) - std::min({r, g, b});
    if (spread < 10) { // near grey -> the 24-step grey ramp (232-255)
        if (avg < 8) {
            return 16; // the cube's black corner is more accurate than the edge of the ramp
        }
        if (avg > 248) {
            return 231;
        }
        const int step = std::clamp((avg - 8) / 10, 0, 23);
        return static_cast<std::uint8_t>(232 + step);
    }
    auto toCube = [](int v) { return std::clamp((v * 6) / 256, 0, 5); };
    const int cr = toCube(r);
    const int cg = toCube(g);
    const int cb = toCube(b);
    return static_cast<std::uint8_t>(16 + 36 * cr + 6 * cg + cb);
}

namespace {

// Updates `state` based on `params` (semicolon-separated numbers).
void applySgr(Cell& state, const std::vector<int>& params) {
    if (params.empty()) {
        state = blankState(); // `\e[m` == `\e[0m`
        return;
    }
    for (std::size_t i = 0; i < params.size(); ++i) {
        const int code = params[i];
        switch (code) {
        case 0:
            state = blankState();
            break;
        case 1:
            setAttribBit(state, attrib_bits::BOLD, true);
            markAttribTouched(state);
            break;
        case 2:
            setAttribBit(state, attrib_bits::DIM, true);
            markAttribTouched(state);
            break;
        case 3:
            setAttribBit(state, attrib_bits::ITALIC, true);
            markAttribTouched(state);
            break;
        case 4:
            setAttribBit(state, attrib_bits::UNDERLINE, true);
            markAttribTouched(state);
            break;
        case 5:
            setAttribBit(state, attrib_bits::BLINK, true);
            markAttribTouched(state);
            break;
        case 7:
            setAttribBit(state, attrib_bits::INVERSE, true);
            markAttribTouched(state);
            break;
        case 9:
            setAttribBit(state, attrib_bits::STRIKETHROUGH, true);
            markAttribTouched(state);
            break;
        case 22:
            setAttribBit(state, attrib_bits::BOLD, false);
            setAttribBit(state, attrib_bits::DIM, false);
            markAttribTouched(state);
            break;
        case 23:
            setAttribBit(state, attrib_bits::ITALIC, false);
            markAttribTouched(state);
            break;
        case 24:
            setAttribBit(state, attrib_bits::UNDERLINE, false);
            markAttribTouched(state);
            break;
        case 25:
            setAttribBit(state, attrib_bits::BLINK, false);
            markAttribTouched(state);
            break;
        case 27:
            setAttribBit(state, attrib_bits::INVERSE, false);
            markAttribTouched(state);
            break;
        case 29:
            setAttribBit(state, attrib_bits::STRIKETHROUGH, false);
            markAttribTouched(state);
            break;
        case 39:
            setAttribBit(state, attrib_bits::FG_UNSET, true);
            break;
        case 49:
            setAttribBit(state, attrib_bits::BG_UNSET, true);
            break;
        case 38:
        case 48: {
            const bool isFg = (code == 38);
            if (i + 1 < params.size() && params[i + 1] == 5 && i + 2 < params.size()) {
                const int idx = params[i + 2];
                if (idx >= 0 && idx <= 255) {
                    if (isFg) {
                        state.fg = static_cast<std::uint8_t>(idx);
                        setAttribBit(state, attrib_bits::FG_UNSET, false);
                    } else {
                        state.bg = static_cast<std::uint8_t>(idx);
                        setAttribBit(state, attrib_bits::BG_UNSET, false);
                    }
                }
                i += 2;
            } else if (i + 1 < params.size() && params[i + 1] == 2 && i + 4 < params.size()) {
                // 38;2;r;g;b -- true colour is NOT supported, but we
                // APPROXIMATE to the nearest 256-palette colour (we do not
                // drop it without effect).
                const std::uint8_t approx = approximateTrueColor(
                    params[i + 2], params[i + 3], params[i + 4]);
                if (isFg) {
                    state.fg = approx;
                    setAttribBit(state, attrib_bits::FG_UNSET, false);
                } else {
                    state.bg = approx;
                    setAttribBit(state, attrib_bits::BG_UNSET, false);
                }
                i += 4;
            }
            break;
        }
        default:
            if (code >= 30 && code <= 37) {
                state.fg = static_cast<std::uint8_t>(code - 30);
                setAttribBit(state, attrib_bits::FG_UNSET, false);
            } else if (code >= 40 && code <= 47) {
                state.bg = static_cast<std::uint8_t>(code - 40);
                setAttribBit(state, attrib_bits::BG_UNSET, false);
            } else if (code >= 90 && code <= 97) {
                state.fg = static_cast<std::uint8_t>(8 + (code - 90));
                setAttribBit(state, attrib_bits::FG_UNSET, false);
            } else if (code >= 100 && code <= 107) {
                state.bg = static_cast<std::uint8_t>(8 + (code - 100));
                setAttribBit(state, attrib_bits::BG_UNSET, false);
            }
            // any other, unhandled code -> no effect (no error)
            break;
        }
    }
}

// Strict UTF-8 decoding (RFC 3629, with the error handling recommended by Unicode/WHATWG).
// `consumed`: number of bytes processed (>= 1); `valid`: whether it is a valid character.
// Invalid: lead bytes 80..C1 and F5..FF (overlong C0/C1, 5-6-byte forms), a missing/wrong
// continuation byte, an overlong 3-4-byte form (E0 <A0, F0 <90), a surrogate (ED A0..BF), a value
// above U+10FFFF (F4 >8F), a truncated sequence. For an invalid sequence the byte causing the error is NOT
// swallowed (it may start a new character, e.g. an ESC).
struct Utf8Step {
    std::size_t consumed;
    bool valid;
};

Utf8Step decodeUtf8(const std::string& s, std::size_t i) {
    const auto lead = static_cast<unsigned char>(s[i]);
    if (lead < 0x80) {
        return {1, true};
    }
    std::size_t need = 0;
    unsigned char lo = 0x80;
    unsigned char hi = 0xBF;
    if (lead >= 0xC2 && lead <= 0xDF) {
        need = 1;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        need = 2;
        if (lead == 0xE0) {
            lo = 0xA0;
        } else if (lead == 0xED) {
            hi = 0x9F;
        }
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        need = 3;
        if (lead == 0xF0) {
            lo = 0x90;
        } else if (lead == 0xF4) {
            hi = 0x8F;
        }
    } else {
        return {1, false}; // 80..C1, F5..FF: cannot be a lead byte
    }
    for (std::size_t k = 1; k <= need; ++k) {
        if (i + k >= s.size()) {
            return {k, false}; // truncated sequence
        }
        const auto b = static_cast<unsigned char>(s[i + k]);
        if (b < lo || b > hi) {
            return {k, false}; // the faulty byte is processed again
        }
        lo = 0x80;
        hi = 0xBF;
    }
    return {need + 1, true};
}

} // namespace

std::size_t backslashRunBeforeIntroducerBody(const std::string& s, std::size_t pos) {
    std::size_t end = pos;
    while (end < s.size() && s[end] == '\\') {
        ++end;
    }
    return end > pos && hasIntroducerBodyAt(s, end) ? end - pos : 0;
}

std::vector<Cell> parseAnsiText(const std::string& raw) {
    std::vector<Cell> out;
    Cell state = blankState();

    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '\\') {
            // `\\e[` protected form: the PAIRS of backslashes are literal `\`; the odd remaining backslash
            // (if any) is the introducer, in the even case the body (`e[...`) is plain text.
            if (const std::size_t run = backslashRunBeforeIntroducerBody(raw, i); run >= 2) {
                for (std::size_t k = 0; k < run / 2; ++k) {
                    Cell c = state;
                    setCellChar(c, "\\");
                    out.push_back(c);
                }
                i += (run / 2) * 2;
                continue;
            }
        }
        if (auto introLen = matchIntroducer(raw, i)) {
            std::size_t j = i + static_cast<std::size_t>(*introLen);
            const std::size_t paramStart = j;
            while (j < raw.size() && !isFinalByte(static_cast<unsigned char>(raw[j]))) {
                ++j;
            }
            if (j >= raw.size()) {
                // An unterminated sequence up to the end of the string cannot
                // be identified as any known/handled sequence,
                // so it is dropped ENTIRELY (zero-length
                // result) — "what we do not
                // recognise we drop", we do not try to display
                // its fragment literally.
                i = raw.size();
                continue;
            }
            const char finalByte = raw[j];
            if (finalByte == 'm') {
                // Processing the SGR parameters. STRICT: only digits (or an
                // empty token = 0) are accepted. For any other token (e.g. `!`,
                // `abc`, `?`, `:`) the WHOLE sequence is dropped
                // without effect (an unknown sequence has no effect).
                std::vector<int> params;
                bool valid = true;
                std::size_t k = paramStart;
                while (k <= j && valid) {
                    std::size_t semi = raw.find(';', k);
                    if (semi == std::string::npos || semi > j) {
                        semi = j;
                    }
                    const std::string tok = raw.substr(k, semi - k);
                    if (tok.size() > 5) { // absurdly long number: invalid
                        valid = false;
                        break;
                    }
                    int v = 0;
                    for (const char ch : tok) {
                        if (ch < '0' || ch > '9') {
                            valid = false;
                            break;
                        }
                        v = v * 10 + (ch - '0');
                    }
                    params.push_back(v);
                    if (semi == j) {
                        break;
                    }
                    k = semi + 1;
                }
                if (paramStart == j) {
                    params.clear(); // empty parameter list -> `\e[m` == reset
                }
                if (valid) {
                    applySgr(state, params);
                }
            }
            // a CSI sequence not ending in 'm' (unhandled) -> silently
            // dropped, without effect.
            i = j + 1;
            continue;
        }

        if (raw[i] == '\0') {
            ++i; // NUL byte: not a character, dropped (it cannot arrive via the CLI, but can from a file)
            continue;
        }
        // Strict UTF-8: an invalid sequence -> a single U+FFFD cell (carrying the style);
        // a raw (control or faulty) byte never gets into a cell, hence not to the terminal either.
        const Utf8Step step = decodeUtf8(raw, i);
        if (step.valid && step.consumed >= 2) {
            const std::string_view chunk = std::string_view(raw).substr(i, step.consumed);
            if (isZeroWidthCodePoint(decodeCodePoint(chunk))) {
                // A zero-width mark: merges into the preceding (non-control) character's cell if it fits in the 4 bytes;
                // otherwise dropped. So the cell count = the column count (the terminal gives it no space either).
                if (!out.empty()) {
                    Cell& base = isWideRight(out.back()) && out.size() >= 2 ? out[out.size() - 2] : out.back();
                    std::size_t n = 0;
                    while (n < 4 && base.ch[n] != 0) {
                        ++n;
                    }
                    if (n > 0 && base.ch[0] >= 0x20 && base.ch[0] != 0x7F && n + chunk.size() <= 4) {
                        for (std::size_t k = 0; k < chunk.size(); ++k) {
                            base.ch[n + k] = static_cast<unsigned char>(chunk[k]);
                        }
                    }
                }
                i += step.consumed;
                continue;
            }
        }
        Cell c = state;
        setCellChar(c, step.valid ? std::string_view(raw).substr(i, step.consumed)
                                  : std::string_view("\xEF\xBF\xBD"));
        if (step.valid && step.consumed >= 3 &&
            isWideCodePoint(decodeCodePoint(std::string_view(raw).substr(i, step.consumed)))) {
            // A two-column (wide) character: two cells (left: the character + WIDE_LEFT, right: empty + WIDE_RIGHT),
            // with the same style; the pair is indivisible (cell.h).
            Cell right = c;
            setCellChar(right, std::string_view());
            right.attrib = static_cast<std::uint16_t>(right.attrib | attrib_bits::WIDE_RIGHT);
            c.attrib = static_cast<std::uint16_t>(c.attrib | attrib_bits::WIDE_LEFT);
            out.push_back(c);
            out.push_back(right);
        } else {
            out.push_back(c);
        }
        i += step.consumed;
    }
    return out;
}

namespace {
bool isControlCell(const Cell& c, bool keepLineAndTab) {
    const auto b0 = static_cast<unsigned char>(c.ch[0]);
    if (c.ch[1] == 0) { // single-byte (ASCII) character
        if (b0 == 0) {
            return false; // empty cell: not a character
        }
        if (keepLineAndTab && (b0 == '\n' || b0 == '\t')) {
            return false;
        }
        return b0 < 0x20 || b0 == 0x7F;
    }
    // C1: U+0080–U+009F = C2 80 .. C2 9F
    return b0 == 0xC2 && c.ch[2] == 0 && static_cast<unsigned char>(c.ch[1]) >= 0x80 &&
           static_cast<unsigned char>(c.ch[1]) <= 0x9F;
}
} // namespace

void dropControlChars(std::vector<Cell>& cells, bool keepLineAndTab) {
    cells.erase(std::remove_if(cells.begin(), cells.end(),
                               [&](const Cell& c) { return isControlCell(c, keepLineAndTab); }),
                cells.end());
}

void resolveUnsetCells(std::vector<Cell>& cells, std::uint8_t bg, std::uint8_t fg,
                       std::uint16_t inheritedAttr) {
    for (auto& c : cells) {
        if (c.attrib & attrib_bits::FG_UNSET) {
            c.fg = fg;
        }
        if (c.attrib & attrib_bits::BG_UNSET) {
            c.bg = bg;
        }
        if (c.attrib & attrib_bits::ATTRIB_UNSET) {
            c.attrib = static_cast<std::uint16_t>(
                (c.attrib & ~(attrib_bits::ATTRIB_UNSET | 0x7Fu)) | inheritedAttr);
        }
        c.attrib = static_cast<std::uint16_t>(c.attrib &
                                              ~(attrib_bits::BG_UNSET | attrib_bits::FG_UNSET));
    }
}

void cleanSingleLine(std::vector<Cell>& cells) {
    for (auto& c : cells) {
        if (c.ch[1] == 0 && c.ch[0] != 0) {
            switch (c.ch[0]) {
            case '\t':
            case '\n':
            case '\r':
            case '\v':
            case '\f':
                setCellChar(c, " ");
                break;
            default:
                break;
            }
        }
    }
    dropControlChars(cells, false); // removing the remaining control characters (C0/DEL/C1)
}

std::string processedSingleLine(const std::string& raw) {
    std::vector<Cell> cells = parseAnsiText(raw);
    cleanSingleLine(cells);
    return encodeAnsiText(cells);
}

std::vector<std::string> sgrCodesForCell(const Cell& c) {
    std::vector<std::string> codes;
    if (c.attrib & attrib_bits::BOLD) {
        codes.push_back("1");
    }
    if (c.attrib & attrib_bits::DIM) {
        codes.push_back("2");
    }
    if (c.attrib & attrib_bits::ITALIC) {
        codes.push_back("3");
    }
    if (c.attrib & attrib_bits::UNDERLINE) {
        codes.push_back("4");
    }
    if (c.attrib & attrib_bits::BLINK) {
        codes.push_back("5");
    }
    if (c.attrib & attrib_bits::INVERSE) {
        codes.push_back("7");
    }
    if (c.attrib & attrib_bits::STRIKETHROUGH) {
        codes.push_back("9");
    }
    if (!(c.attrib & attrib_bits::FG_UNSET)) {
        codes.push_back("38;5;" + std::to_string(c.fg));
    }
    if (!(c.attrib & attrib_bits::BG_UNSET)) {
        codes.push_back("48;5;" + std::to_string(c.bg));
    }
    return codes;
}

bool sameCellStyle(const Cell& a, const Cell& b) {
    // Only the style bits matter (not the structural WIDE_LEFT/WIDE_RIGHT): the two halves of the pair and its
    // neighbours get the same SGR state, without needless resets.
    return (a.attrib & attrib_bits::STYLE_MASK) == (b.attrib & attrib_bits::STYLE_MASK) && a.bg == b.bg &&
           a.fg == b.fg;
}

std::string joinSgrCodes(const std::vector<std::string>& codes) {
    std::string s;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (i) {
            s += ';';
        }
        s += codes[i];
    }
    return s;
}

std::string sgrResetAndSet(const Cell& c) {
    const auto codes = sgrCodesForCell(c);
    return codes.empty() ? "\x1b[0m" : "\x1b[0m\x1b[" + joinSgrCodes(codes) + "m";
}

Cell neutralBlankCell() {
    Cell c{};
    c.attrib = static_cast<std::uint16_t>(attrib_bits::BG_UNSET | attrib_bits::FG_UNSET |
                                           attrib_bits::ATTRIB_UNSET);
    setCellChar(c, " ");
    return c;
}

std::string encodeAnsiText(const std::vector<Cell>& cells) {
    std::string out;
    bool inStyledState = false;

    // Literal backslashes followed by an introducer body (`e[`, `033[`, `x1b[`, `x1B[`) are doubled:
    // fed back in, the view gives the same text (`\\e[` is the protected form, see parseAnsiText).
    const auto isCh = [&](std::size_t p, char ch) {
        return p < cells.size() && !isWideRight(cells[p]) && cells[p].ch[0] == static_cast<unsigned char>(ch) &&
               cells[p].ch[1] == 0;
    };
    const auto bodyAt = [&](std::size_t p) {
        return (isCh(p, 'e') && isCh(p + 1, '[')) ||
               (isCh(p, '0') && isCh(p + 1, '3') && isCh(p + 2, '3') && isCh(p + 3, '[')) ||
               (isCh(p, 'x') && (isCh(p + 1, '1')) && (isCh(p + 2, 'b') || isCh(p + 2, 'B')) && isCh(p + 3, '['));
    };
    std::vector<bool> doubled(cells.size(), false);
    for (std::size_t p = 0; p < cells.size();) {
        if (isCh(p, '\\')) {
            std::size_t q = p;
            while (isCh(q, '\\')) {
                ++q;
            }
            if (bodyAt(q)) {
                for (std::size_t k = p; k < q; ++k) {
                    doubled[k] = true;
                }
            }
            p = q;
        } else {
            ++p;
        }
    }

    std::size_t i = 0;
    while (i < cells.size()) {
        std::size_t j = i;
        while (j < cells.size() && sameCellStyle(cells[j], cells[i])) {
            ++j;
        }
        if (!sgrCodesForCell(cells[i]).empty()) {
            out += sgrResetAndSet(cells[i]);
            inStyledState = true;
        } else if (inStyledState) {
            out += "\x1b[0m";
            inStyledState = false;
        }
        for (std::size_t k = i; k < j; ++k) {
            if (!isWideRight(cells[k])) { // the right half of a wide character is not a separate character
                out += getCellChar(cells[k]);
                if (doubled[k]) {
                    out += '\\';
                }
            }
        }
        i = j;
    }
    return out;
}

} // namespace toytui
