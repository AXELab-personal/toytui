#include "vterm_screen.h"

#include <vterm.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "ansi_handler.h"
#include "utf_escape.h"

namespace toytui {

struct VtermScreen::Impl {
    VTerm* vt = nullptr;
    VTermScreen* screen = nullptr;
    std::string out;
    bool cursorVisible = true;
    bool cursorBlink = true;
    int cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    bool alt = false;
};

namespace {

void onOutput(const char* s, size_t len, void* user) {
    static_cast<VtermScreen::Impl*>(user)->out.append(s, len);
}

int onTermProp(VTermProp prop, VTermValue* val, void* user) {
    auto* impl = static_cast<VtermScreen::Impl*>(user);
    switch (prop) {
    case VTERM_PROP_CURSORVISIBLE:
        impl->cursorVisible = val->boolean != 0;
        break;
    case VTERM_PROP_CURSORBLINK:
        impl->cursorBlink = val->boolean != 0;
        break;
    case VTERM_PROP_CURSORSHAPE:
        impl->cursorShape = val->number;
        break;
    case VTERM_PROP_ALTSCREEN:
        impl->alt = val->boolean != 0;
        break;
    default:
        break; // title, mouse, bell, ... are ignored
    }
    return 1;
}

const VTermScreenCallbacks kCallbacks = {
    nullptr, nullptr, nullptr, onTermProp, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

// Cell of the toytui model from a libvterm cell.
Cell convertCell(const VTermScreenCell& vc) {
    Cell c = neutralBlankCell();
    std::string text;
    for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && vc.chars[i] != 0; ++i) {
        const std::string piece = encodeUtf8(static_cast<char32_t>(vc.chars[i]));
        if (text.size() + piece.size() > 4) {
            break; // combining marks that do not fit in the 4 bytes are dropped
        }
        text += piece;
    }
    if (!text.empty()) {
        for (std::size_t i = 0; i < 4; ++i) {
            c.ch[i] = i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
        }
    }

    auto colour = [](const VTermColor& col, std::uint16_t unsetBit, std::uint8_t& out, std::uint16_t& attrib) {
        if (VTERM_COLOR_IS_DEFAULT_FG(&col) || VTERM_COLOR_IS_DEFAULT_BG(&col)) {
            return; // stays UNSET: inherits the host element's colour
        }
        attrib = static_cast<std::uint16_t>(attrib & ~unsetBit);
        if (VTERM_COLOR_IS_INDEXED(&col)) {
            out = col.indexed.idx;
        } else {
            out = approximateTrueColor(col.rgb.red, col.rgb.green, col.rgb.blue);
        }
    };
    colour(vc.fg, attrib_bits::FG_UNSET, c.fg, c.attrib);
    colour(vc.bg, attrib_bits::BG_UNSET, c.bg, c.attrib);

    std::uint16_t style = 0;
    if (vc.attrs.bold) style |= attrib_bits::BOLD;
    if (vc.attrs.underline) style |= attrib_bits::UNDERLINE;
    if (vc.attrs.italic) style |= attrib_bits::ITALIC;
    if (vc.attrs.blink) style |= attrib_bits::BLINK;
    if (vc.attrs.reverse) style |= attrib_bits::INVERSE;
    if (vc.attrs.strike) style |= attrib_bits::STRIKETHROUGH;
    if (style != 0) { // an own attribute: it replaces, never mixes with, the host's
        c.attrib = static_cast<std::uint16_t>((c.attrib & ~attrib_bits::ATTRIB_UNSET) | style);
    }
    return c;
}

struct ParsedKey {
    int mods = 0;          // VTermModifier bits
    VTermKey key = VTERM_KEY_NONE;
    char32_t ch = 0;       // a character key (when key == VTERM_KEY_NONE)
    bool chFromName = false; // ch given by a name (SPACE, MINUS...), not a letter
};

bool decodeUtf8Single(const std::string& s, char32_t& cp) {
    if (s.empty()) {
        return false;
    }
    const unsigned char b = static_cast<unsigned char>(s[0]);
    std::size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
    if (n == 0 || s.size() != n) {
        return false;
    }
    cp = n == 1 ? b : (b & (0xFFu >> (n + 1)));
    for (std::size_t i = 1; i < n; ++i) {
        const unsigned char cont = static_cast<unsigned char>(s[i]);
        if ((cont & 0xC0) != 0x80) {
            return false;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }
    return true;
}

bool parseKey(const std::string& token, ParsedKey& k) {
    if (token.empty()) {
        return false;
    }
    if (token.front() != '[' || token.size() == 1) {
        return decodeUtf8Single(token, k.ch) && k.ch >= 0x20 && k.ch != 0x7f; // "[" alone is a plain char
    }
    if (token.back() != ']') {
        return false;
    }
    std::string rest = token.substr(1, token.size() - 2);
    // Modifier prefixes: CTRL-, ALT-, SHIFT-, META- (META is treated as ALT).
    for (;;) {
        auto strip = [&](const char* name, int mod) {
            const std::string p = std::string(name) + "-";
            if (rest.compare(0, p.size(), p) == 0 && rest.size() > p.size()) {
                rest.erase(0, p.size());
                k.mods |= mod;
                return true;
            }
            return false;
        };
        if (strip("CTRL", VTERM_MOD_CTRL) || strip("ALT", VTERM_MOD_ALT) || strip("SHIFT", VTERM_MOD_SHIFT) ||
            strip("META", VTERM_MOD_ALT)) {
            continue;
        }
        break;
    }
    struct Named { const char* name; VTermKey key; };
    static const Named kNamed[] = {
        {"ESC", VTERM_KEY_ESCAPE}, {"ENTER", VTERM_KEY_ENTER}, {"TAB", VTERM_KEY_TAB},
        {"BS", VTERM_KEY_BACKSPACE}, {"DEL", VTERM_KEY_DEL}, {"INS", VTERM_KEY_INS},
        {"HOME", VTERM_KEY_HOME}, {"END", VTERM_KEY_END}, {"PGUP", VTERM_KEY_PAGEUP},
        {"PGDN", VTERM_KEY_PAGEDOWN}, {"UP", VTERM_KEY_UP}, {"DOWN", VTERM_KEY_DOWN},
        {"LEFT", VTERM_KEY_LEFT}, {"RIGHT", VTERM_KEY_RIGHT},
    };
    for (const auto& n : kNamed) {
        if (rest == n.name) {
            k.key = n.key;
            return true;
        }
    }
    if (rest.size() >= 2 && rest[0] == 'F' && rest.find_first_not_of("0123456789", 1) == std::string::npos) {
        const int n = std::stoi(rest.substr(1));
        if (n >= 1 && n <= 24) {
            k.key = static_cast<VTermKey>(VTERM_KEY_FUNCTION(n));
            return true;
        }
        return false;
    }
    k.chFromName = true;
    if (rest == "SPACE") { k.ch = U' '; return true; }
    if (rest == "LBRACKET") { k.ch = U'['; return true; }
    if (rest == "RBRACKET") { k.ch = U']'; return true; }
    if (rest == "MINUS") { k.ch = U'-'; return true; }
    k.chFromName = false;
    return decodeUtf8Single(rest, k.ch) && k.ch >= 0x20 && k.ch != 0x7f;
}

} // namespace

VtermScreen::VtermScreen(int rows, int cols) : impl_(new Impl) {
    impl_->vt = vterm_new(std::max(rows, 1), std::max(cols, 1));
    vterm_set_utf8(impl_->vt, 1);
    vterm_output_set_callback(impl_->vt, onOutput, impl_.get());
    impl_->screen = vterm_obtain_screen(impl_->vt);
    vterm_screen_enable_altscreen(impl_->screen, 1);
    vterm_screen_set_callbacks(impl_->screen, &kCallbacks, impl_.get());
    vterm_screen_reset(impl_->screen, 1);
}

VtermScreen::~VtermScreen() {
    if (impl_ && impl_->vt != nullptr) {
        vterm_free(impl_->vt);
    }
}

int VtermScreen::rows() const {
    int r = 0, c = 0;
    vterm_get_size(impl_->vt, &r, &c);
    return r;
}

int VtermScreen::cols() const {
    int r = 0, c = 0;
    vterm_get_size(impl_->vt, &r, &c);
    return c;
}

void VtermScreen::resize(int rows, int cols) {
    vterm_set_size(impl_->vt, std::max(rows, 1), std::max(cols, 1));
}

void VtermScreen::write(const char* data, std::size_t size) {
    vterm_input_write(impl_->vt, data, size);
}

bool VtermScreen::sendKey(const std::string& token) {
    ParsedKey k;
    if (!parseKey(token, k)) {
        return false;
    }
    const auto mods = static_cast<VTermModifier>(k.mods);
    if (k.key != VTERM_KEY_NONE) {
        vterm_keyboard_key(impl_->vt, k.key, mods);
        return true;
    }
    char32_t ch = k.ch;
    const bool isLetter = ch >= U'A' && ch <= U'Z';
    // Ctrl + a character of 0x40..0x7F or space: the classic control byte (Alt adds an ESC prefix).
    // This also keeps Ctrl-I/J/M as TAB/LF/CR, which libvterm would encode as CSI u.
    if ((k.mods & VTERM_MOD_CTRL) != 0 && ((ch >= 0x40 && ch < 0x80) || ch == U' ')) {
        const char byte = ch == U' ' ? '\0' : static_cast<char>(ch & 0x1f);
        if ((k.mods & VTERM_MOD_ALT) != 0) {
            impl_->out.push_back('\x1b');
        }
        impl_->out.push_back(byte);
        return true;
    }
    // A letter base is upper case in the notation; it is the lower-case character unless SHIFT is given.
    if (isLetter && (k.mods & VTERM_MOD_SHIFT) == 0 && !k.chFromName) {
        ch = ch - U'A' + U'a';
    }
    vterm_keyboard_unichar(impl_->vt, static_cast<uint32_t>(ch), mods);
    return true;
}

std::string VtermScreen::takeOutput() {
    std::string s;
    s.swap(impl_->out);
    return s;
}

Block VtermScreen::block() const {
    int rows = 0, cols = 0;
    vterm_get_size(impl_->vt, &rows, &cols);
    Block b;
    b.width = cols;
    b.height = rows;
    b.cells.assign(static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols), neutralBlankCell());
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            VTermScreenCell vc;
            VTermPos pos;
            pos.row = r;
            pos.col = c;
            if (vterm_screen_get_cell(impl_->screen, pos, &vc) == 0) {
                continue;
            }
            Cell cell = convertCell(vc);
            if (vc.chars[0] == static_cast<uint32_t>(-1)) { // right half of a wide character
                cell.ch[0] = cell.ch[1] = cell.ch[2] = cell.ch[3] = 0;
                cell.attrib = static_cast<std::uint16_t>(cell.attrib | attrib_bits::WIDE_RIGHT);
            } else if (vc.width == 2) {
                cell.attrib = static_cast<std::uint16_t>(cell.attrib | attrib_bits::WIDE_LEFT);
            }
            b.at(c, r) = cell;
        }
        repairWidePairs(&b.at(0, r), cols);
    }
    return b;
}

VtermScreen::Cursor VtermScreen::cursor() const {
    Cursor cur;
    VTermPos pos;
    vterm_state_get_cursorpos(vterm_obtain_state(impl_->vt), &pos);
    cur.row = pos.row;
    cur.col = pos.col;
    cur.visible = impl_->cursorVisible;
    switch (impl_->cursorShape) {
    case VTERM_PROP_CURSORSHAPE_UNDERLINE:
        cur.shape = impl_->cursorBlink ? 3 : 4;
        break;
    case VTERM_PROP_CURSORSHAPE_BAR_LEFT:
        cur.shape = impl_->cursorBlink ? 5 : 6;
        break;
    default:
        cur.shape = impl_->cursorBlink ? 1 : 2;
        break;
    }
    return cur;
}

bool VtermScreen::altScreen() const { return impl_->alt; }

} // namespace toytui
