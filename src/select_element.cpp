#include "select_element.h"

#include <algorithm>
#include <utility>

#include "ansi_handler.h"
#include "box_element.h"
#include "error.h"
#include "input_ops.h"
#include "list_value.h"
#include "param_spec.h"
#include "param_types.h"
#include "text_fit.h"

namespace toytui {

namespace {

const SpecTable& selectSpecs() {
    static const SpecTable t = concatSpecs(
        kGeneralSpecs,
        {{"border", Kind::Border, false, "off"},
         {"border_fgcol", Kind::Color, true, nullptr},
         {"border_bgcol", Kind::Color, true, nullptr},
         {"title", Kind::TextView, false, nullptr, 0, "title_raw", processedSingleLine},
         {"title_raw", Kind::Text, false, nullptr},
         {"titlePos", Kind::TitlePos, false, "left"},
         {"options", Kind::Text, false, nullptr, kTextParamLimit},
         {"keys_exit", Kind::Text, false, nullptr},
         {"keys_accept", Kind::Text, false, nullptr},
         {"cursor", Kind::Int, false, "1", 0, nullptr, nullptr, 1, kMaxSelectIndex},
         {"selected", Kind::Int, false, "0", 0, nullptr, nullptr, 0, kMaxSelectIndex},
         {"top", Kind::Int, false, "0", 0, nullptr, nullptr, 0, kMaxSelectIndex},
         {"visualCursor", Kind::Int, true, nullptr, 0, nullptr, nullptr, 1, kMaxSelectIndex},
         {"cursor_bgcol", Kind::Color, true, nullptr},
         {"cursor_fgcol", Kind::Color, true, nullptr},
         {"sel_bgcol", Kind::Color, true, nullptr},
         {"sel_fgcol", Kind::Color, true, nullptr}});
    return t;
}

std::vector<std::string> optionsOf(const ElementParams& p) {
    return splitEscapedList(p.text("options"), '|', true);
}

std::vector<std::string> keysOf(const ElementParams& p, const char* name) {
    const std::string* s = p.getSet(name);
    if (s == nullptr || s->empty()) {
        return {};
    }
    const auto k = parseExitKeys(*s);
    return k ? *k : std::vector<std::string>{};
}

// The cells visible in one row (cleaned, following the label's rules).
std::vector<Cell> optionCells(const std::string& raw) {
    std::vector<Cell> cells = parseAnsiText(raw);
    cleanSingleLine(cells);
    return cells;
}

// ---- layout ----

struct Layout {
    int w = 0;
    int h = 0;
    int border = 0;  // 0/1
    int cw = 0;      // row width (content)
    int rows = 0;    // number of visible rows
    std::vector<std::string> options;
};

Layout selectLayoutOf(const ElementParams& p, const ElementTree& t, const ElementRegistry& reg) {
    Layout L;
    L.options = optionsOf(p);
    const std::string border = p.text("border", "off");
    L.border = border == "off" ? 0 : 1;
    const int count = static_cast<int>(L.options.size());

    if (const auto w = paramInt(p, "width")) {
        L.w = *w;
    } else {
        int widest = 0;
        for (const std::string& o : L.options) {
            widest = std::max(widest, static_cast<int>(optionCells(o).size()));
        }
        L.w = widest + 2 * L.border;
        if (const auto pw = parentContentSize(t, reg, p, false)) {
            L.w = std::min(L.w, *pw - paramInt(p, "posX", 0)); // the automatic size never sticks out
        }
    }
    if (const auto h = paramInt(p, "height")) {
        L.h = *h;
    } else {
        L.h = count + 2 * L.border;
        if (const auto ph = parentContentSize(t, reg, p, true)) {
            L.h = std::min(L.h, *ph - paramInt(p, "posY", 0));
        }
    }
    L.w = std::max(0, L.w);
    L.h = std::max(0, L.h);
    L.cw = std::max(0, L.w - 2 * L.border);
    L.rows = std::max(0, L.h - 2 * L.border);
    return L;
}

Layout selectLayout(const ElementTree& t, const ElementRegistry& reg, const std::string& id) {
    return selectLayoutOf(t.find(id)->params, t, reg);
}

// Index of the first visible item: the stored `top` clamped to the range, with the cursor made visible (minimal scrolling).
int effectiveTop(const Layout& L, int cursorIndex, int top) {
    const int count = static_cast<int>(L.options.size());
    if (L.rows <= 0 || count == 0) {
        return 0;
    }
    top = std::clamp(top, 0, std::max(0, count - L.rows));
    if (cursorIndex < top) {
        top = cursorIndex;
    } else if (cursorIndex >= top + L.rows) {
        top = cursorIndex - L.rows + 1;
    }
    return top;
}

int cursorIndexOf(const Layout& L, const ElementParams& p) {
    const int count = static_cast<int>(L.options.size());
    return count == 0 ? 0 : std::clamp(paramInt(p, "cursor", 1) - 1, 0, count - 1);
}

Block buildSelect(const ElementTree& t, const ElementRegistry& reg, const std::string& id) {
    const ElementParams& p = t.find(id)->params;
    const Layout L = selectLayout(t, reg, id);
    Block b;
    if (L.w <= 0 || L.h <= 0) {
        return b;
    }
    b.width = L.w;
    b.height = L.h;
    const BoxColors colors = resolveBoxColors(t, id);
    const std::string border = p.text("border", "off");
    b.cells = renderBoxFrame(L.w, L.h, border == "off" ? 0 : std::stoi(border),
                             p.text("title_raw"),
                             p.text("titlePos", "left"), false, false, colors);
    const auto attr = static_cast<std::uint16_t>(t.resolveInheritableInt(id, "attrib") & 0x7F);
    for (auto& c : b.cells) {
        if (c.attrib & attrib_bits::ATTRIB_UNSET) {
            c.attrib = static_cast<std::uint16_t>((c.attrib & ~(attrib_bits::ATTRIB_UNSET | 0x7Fu)) | attr);
        }
    }

    const int count = static_cast<int>(L.options.size());
    const int cur = cursorIndexOf(L, p);
    const int top = effectiveTop(L, cur, paramInt(p, "top", 0));
    const int selected = paramInt(p, "selected", 0);
    const ColorPair base{colors.bg, colors.fg};
    for (int r = 0; r < L.rows; ++r) {
        const int idx = top + r;
        if (idx >= count) {
            break;
        }
        ColorPair look = base;
        if (idx == cur) {
            look = applyStatePair(p, "cursor", base, true);
            if (selected > 0 && selected - 1 == idx) {
                look = applyStatePair(p, "sel", look, false);
            }
        }
        std::vector<Cell> cells = fitCellsLeft(optionCells(L.options[static_cast<std::size_t>(idx)]), L.cw,
                                               neutralBlankCell());
        resolveUnsetCells(cells, look.bg, look.fg, attr);
        for (int x = 0; x < L.cw; ++x) {
            b.at(L.border + x, L.border + r) = cells[static_cast<std::size_t>(x)];
        }
    }
    // Scrollability markers on the right-hand border: △ below the top / ▽ above the bottom border-row segment
    // (below / above the corner). Only the character is replaced, the border's colour/attribute stays. Without a border,
    // or if there are fewer than two content rows (the two markers would collide), there is no marker.
    if (L.border > 0 && L.w >= 2 && L.rows >= 2) {
        const auto setGlyph = [&](int y, const unsigned char (&g)[3]) {
            Cell& c = b.at(L.w - 1, y);
            c.ch[0] = g[0];
            c.ch[1] = g[1];
            c.ch[2] = g[2];
            c.ch[3] = 0;
        };
        static const unsigned char kUp[3] = {0xE2, 0x96, 0xB3};   // U+25B3 △
        static const unsigned char kDown[3] = {0xE2, 0x96, 0xBD}; // U+25BD ▽
        if (top > 0) {
            setGlyph(1, kUp);
        }
        if (top + L.rows < count) {
            setGlyph(L.h - 2, kDown);
        }
    }
    return b;
}

// Cutting out (NOT the compositor's `blit`: that pastes in, this cuts out) — the windowed `ownVisualWindow` gets the visible part from this.
Block cropBlock(const Block& src, const Rect& win) {
    Block out;
    out.width = win.width;
    out.height = win.height;
    out.cells.assign(static_cast<std::size_t>(std::max(0, win.width)) * static_cast<std::size_t>(std::max(0, win.height)),
                     Cell{});
    for (int y = 0; y < win.height; ++y) {
        for (int x = 0; x < win.width; ++x) {
            const int sx = win.x + x;
            const int sy = win.y + y;
            if (sx >= 0 && sy >= 0 && sx < src.width && sy < src.height) {
                out.at(x, y) = src.at(sx, sy);
            }
        }
        repairWidePairs(&out.at(0, y), out.width); // a wide character cut in two at the window edge is dropped
    }
    return out;
}

// ---- validation ----

bool hasToken(const std::vector<ParamToken>& params, const char* name) {
    return std::any_of(params.begin(), params.end(), [&](const ParamToken& t) { return t.name == name; });
}

// Canonicalising/checking the key lists, keeping the cursor and the selection consistent with options.
void finalize(ElementParams& p, const std::vector<ParamToken>& tokens, bool isNew, const std::string& method,
              const std::string& target) {
    for (const char* name : {"keys_exit", "keys_accept"}) {
        const std::string* found = p.getSet(name);
        if (found == nullptr) {
            continue;
        }
        const std::string v = *found; // copy: `p.set` below overwrites the stored value
        if (std::string(name) == "keys_accept" && v.empty()) {
            continue; // empty keys_accept: none
        }
        const auto keys = parseExitKeys(v);
        if (!keys) {
            throwError(method, target,
                     std::string("Invalid key list (expected one or more keys, e.g. `[ENTER]`, `[ESC][CTRL-Q]`): `") +
                         name + ":" + v + "`");
        }
        p.set(name, joinKeys(*keys));
    }
    if (p.getSet("keys_exit") == nullptr) {
        throwError(method, target, isNew ? "Missing required parameter: `keys_exit`" : "Missing `keys_exit`");
    }
    const int count = static_cast<int>(optionsOf(p).size());
    const int maxCursor = std::max(1, count);
    int cursor = paramInt(p, "cursor", 1);
    if (cursor > maxCursor) {
        if (hasToken(tokens, "cursor")) {
            throwError(method, target,
                     "Cursor out of range: `cursor:" + std::to_string(cursor) + "` (1.." + std::to_string(maxCursor) + ")");
        }
        p.set("cursor", std::to_string(maxCursor)); // after options are replaced the cursor is clamped into the range
        cursor = maxCursor;
    }
    int selected = paramInt(p, "selected", 0);
    if (selected > count) {
        if (hasToken(tokens, "selected")) {
            throwError(method, target,
                     "Selected out of range: `selected:" + std::to_string(selected) + "` (0.." + std::to_string(count) + ")");
        }
        p.set("selected", "0");
    }
    // Moving the cursor by hand clears the selection (unless `selected` was also set in the same call).
    if (hasToken(tokens, "cursor") && !hasToken(tokens, "selected")) {
        p.set("selected", "0");
    }
}

// `visualCursor` (write): the DESIRED row of the cursor in the content area (1-based, the border does not count). It sets the
// visible window (`top`) so that the cursor lands there; if the element's content does not allow this (start/end of the list), the nearest
// possible position remains — `visualCursor` is only a hint, there is no error. It never changes the position of `cursor`.
// It removes the transient marker (`visualCursor` is not a stored parameter).
void resolveVisualCursor(const ElementTree& t, const ElementRegistry& reg, ElementParams& p) {
    if (p.getSet("visualCursor") == nullptr) {
        p.remove("visualCursor"); // `auto` (explicit unset) / not given: no request
        return;
    }
    const int want = paramInt(p, "visualCursor", 1);
    p.remove("visualCursor");
    const Layout L = selectLayoutOf(p, t, reg);
    const int count = static_cast<int>(L.options.size());
    if (L.rows <= 0 || count == 0) {
        return;
    }
    const int row = std::clamp(want, 1, L.rows);
    const int top = std::clamp(cursorIndexOf(L, p) - (row - 1), 0, std::max(0, count - L.rows));
    p.set("top", std::to_string(top));
}

} // namespace

ElementParams makeDefaultSelectParams() {
    ElementParams p;
    p.set("parent", "core@terminal");
    p.set("posX", "0");
    p.set("posY", "0");
    p.set("z_index", "1");
    p.set("border", "off");
    p.set("titlePos", "left");
    p.set("cursor", "1");
    p.set("selected", "0");
    p.set("top", "0");
    return p;
}

ElementParams selectNew(const std::vector<ParamToken>& params) {
    ElementParams p = makeDefaultSelectParams();
    const std::string target = elementTarget("select", params);
    applyParams(p, params, selectSpecs(), true, target);
    finalize(p, params, true, "new", target);
    return p;
}

void selectSet(ElementParams& p, const std::vector<ParamToken>& params) {
    const std::string target = elementTarget("select", p);
    applyParams(p, params, selectSpecs(), false, target);
    finalize(p, params, false, "set", target);
}

ElementKind selectElementKind() {
    ElementKind k;
    k.freeTextParams = {}; // no free-text parameter: `options` is an ordinary `name:value`
    k.contentArea = [](const ElementTree&, const std::string&, const Block&) {
        return std::optional<Rect>(); // leaf
    };
    k.ownVisual = [](const ElementTree&, const std::string&) { return Block{}; }; // the compositor builds via the windowed path
    k.measure = [](const ElementTree& t, const ElementRegistry& reg, const std::string& id) {
        const Layout L = selectLayout(t, reg, id);
        return std::make_pair(L.w, L.h);
    };
    k.ownVisualWindow = [](const ElementTree& t, const ElementRegistry& reg, const std::string& id,
                           const Rect& window) { return cropBlock(buildSelect(t, reg, id), window); };
    k.create = [](const ParsedCommand& c) { return selectNew(c.params); };
    k.set = [](ElementParams& p, const ParsedCommand& c) { selectSet(p, c.params); };
    k.get = [](const ElementParams& p, const ParsedCommand& c) {
        return getParams(p, c.params, selectSpecs(), elementTarget("select", p));
    };
    k.paramSpecs = selectSpecs();
    k.derivedParams = {"visualCursor"};
    k.deriveParams = [](const ElementTree& t, const ElementRegistry& reg, const std::string& id, ElementParams& view) {
        const Layout L = selectLayout(t, reg, id);
        if (L.rows <= 0 || L.options.empty()) {
            return; // nothing visible: `auto`
        }
        const int cur = cursorIndexOf(L, view);
        const int top = effectiveTop(L, cur, paramInt(view, "top", 0));
        view.set("visualCursor", std::to_string(cur - top + 1));
    };
    k.resolveDerived = resolveVisualCursor;

    k.onKey = [](ElementTree& t, const ElementRegistry& reg, const std::string& id, const std::string& key) {
        const Layout L = selectLayout(t, reg, id);
        const int count = static_cast<int>(L.options.size());
        ElementParams& p = t.findMut(id)->params;
        if (count == 0) {
            return false;
        }
        const int cur = cursorIndexOf(L, p);
        const int page = std::max(1, L.rows);
        int next = cur;
        if (key == "[UP]") {
            next = cur - 1;
        } else if (key == "[DOWN]") {
            next = cur + 1;
        } else if (key == "[PGUP]") {
            next = cur - page;
        } else if (key == "[PGDN]") {
            next = cur + page;
        } else if (key == "[HOME]") {
            next = 0;
        } else if (key == "[END]") {
            next = count - 1;
        } else {
            return false; // not a navigation key: dropped
        }
        next = std::clamp(next, 0, count - 1); // no wrap-around
        const int top = effectiveTop(L, next, paramInt(p, "top", 0));
        if (next == cur && top == paramInt(p, "top", 0)) {
            return false;
        }
        p.set("cursor", std::to_string(next + 1));
        p.set("top", std::to_string(top));
        if (next != cur) {
            p.set("selected", "0"); // the cursor moved again: the selection is cleared
        }
        return true;
    };

    k.exitKeys = [](const ElementTree& t, const std::string& id) {
        const ElementParams& p = t.find(id)->params;
        std::vector<std::string> keys = keysOf(p, "keys_exit");
        for (const std::string& s : keysOf(p, "keys_accept")) {
            if (std::find(keys.begin(), keys.end(), s) == keys.end()) {
                keys.push_back(s);
            }
        }
        return keys;
    };
    k.onExit = [](ElementTree& t, const ElementRegistry& reg, const std::string& id, const std::string& key) {
        const Layout L = selectLayout(t, reg, id);
        ElementParams& p = t.findMut(id)->params;
        const int count = static_cast<int>(L.options.size());
        const int value = count == 0 ? 0 : cursorIndexOf(L, p) + 1;
        const std::vector<std::string> sel = keysOf(p, "keys_accept");
        const bool mark = std::find(sel.begin(), sel.end(), key) != sel.end();
        const int newSelected = mark ? value : 0;
        ElementKind::ExitResult r;
        if (newSelected != paramInt(p, "selected", 0)) {
            p.set("selected", std::to_string(newSelected));
            r.changed = true;
        }
        r.fields.emplace_back("selected", std::to_string(value));
        return r;
    };
    return k;
}

} // namespace toytui
