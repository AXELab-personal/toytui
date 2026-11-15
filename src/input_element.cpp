#include "input_element.h"

#include <algorithm>
#include <cstdint>

#include "ansi_handler.h"
#include "error.h"
#include "input_editor.h"
#include "input_ops.h"
#include "input_pattern.h"
#include "list_value.h"
#include "param_spec.h"
#include "param_types.h"

namespace toytui {

namespace {

constexpr const char* kDefaultPattern = "{%s0-}";
constexpr const char* kCursorChoices = "Block=B,2;bBlock=bB,1;Underline=U,4;bUnderline=bU,3;Bar=R,6;bBar=bR,5";
constexpr int kMaxScroll = 1 << 20;

const SpecTable& inputSpecs() {
    static const SpecTable t = concatSpecs(
        kGeneralSpecs,
        {{"pattern", Kind::Text, false, nullptr},
         {"value", Kind::Text, false, nullptr, kTextParamLimit},
         {"blocks", Kind::Text, false, nullptr, kTextParamLimit},
         {"placeholder", Kind::Text, false, nullptr},
         {"advance", Kind::Bool, false, "on"},
         {"cursorType", Kind::Enum, false, "bBar", 0, nullptr, nullptr, 0, 0, kCursorChoices},
         {"inputMode", Kind::Enum, false, "insert", 0, nullptr, nullptr, 0, 0, "insert=ins;overwrite=owr"},
         {"keys_exit", Kind::Text, true, nullptr},
         {"keys_accept", Kind::Text, false, nullptr},
         {"keys_navNext", Kind::Text, false, "[SHIFT-RIGHT]"},
         {"keys_navPrev", Kind::Text, false, "[SHIFT-LEFT]"},
         {"keys_navHome", Kind::Text, false, "[HOME]"},
         {"keys_navEnd", Kind::Text, false, "[END]"},
         // internal state (not listed)
         {"edit_blocks", Kind::Text, false, nullptr, kTextParamLimit, nullptr, nullptr, 0, 0, nullptr, true},
         {"edit_cursor", Kind::Text, false, nullptr, 0, nullptr, nullptr, 0, 0, nullptr, true},
         {"edit_scroll", Kind::Int, false, "0", 0, nullptr, nullptr, 0, kMaxScroll, nullptr, true}});
    return t;
}

// Encoding a list separated by `|` (the counterpart of `splitEscapedList`: `\` -> `\\`, `|` -> `\|`).
std::string encodeList(const std::vector<std::string>& items) {
    std::string out;
    for (std::size_t k = 0; k < items.size(); ++k) {
        if (k > 0) out += '|';
        for (char c : items[k]) {
            if (c == '\\' || c == '|') out += '\\';
            out += c;
        }
    }
    return out;
}

InputEditor makeEditor(const ElementParams& p) {
    std::string err;
    const std::string ph = p.text("placeholder", " ");
    auto pat = parsePattern(p.text("pattern", kDefaultPattern), ph, err);
    if (!pat) pat = parsePattern(kDefaultPattern, " ", err); // the stored value is always valid; defensive
    InputEditorOptions o;
    o.overwrite = p.text("inputMode", "insert") == "overwrite";
    o.advance = p.getSet("advance") == nullptr || p.isOn("advance");
    InputEditor ed(*pat, o);

    std::vector<std::string> blocks = splitEscapedList(p.text("edit_blocks"), '|');
    std::size_t blk = 0, off = 0;
    const std::string cur = p.text("edit_cursor");
    const auto colon = cur.find(':');
    if (colon != std::string::npos) {
        try {
            blk = static_cast<std::size_t>(std::stoul(cur.substr(0, colon)));
            off = static_cast<std::size_t>(std::stoul(cur.substr(colon + 1)));
        } catch (...) {
            blk = off = 0;
        }
    }
    ed.restore(blocks, blk, off);
    return ed;
}

void saveState(ElementParams& p, const InputEditor& ed) {
    p.set("edit_blocks", encodeList(ed.blockValues()));
    p.set("edit_cursor", std::to_string(ed.blockIndex()) + ":" + std::to_string(ed.offset()));
}

void clearState(ElementParams& p) {
    p.remove("edit_blocks");
    p.remove("edit_cursor");
    p.remove("edit_scroll");
}

ElementParams makeDefaultInputParams() {
    ElementParams p;
    p.set("parent", "core@terminal");
    p.set("posX", "0");
    p.set("posY", "0");
    p.set("z_index", "1");
    p.set("pattern", kDefaultPattern);
    p.set("placeholder", " ");
    p.set("advance", "on");
    p.set("cursorType", "bBar");
    p.set("inputMode", "insert");
    p.set("keys_navNext", "[SHIFT-RIGHT]");
    p.set("keys_navPrev", "[SHIFT-LEFT]");
    p.set("keys_navHome", "[HOME]");
    p.set("keys_navEnd", "[END]");
    return p;
}

const ParamSpec& cursorSpec() { return *findSpec(inputSpecs(), "cursorType"); }

// Checking the pattern and the block-level options (the shared work of `new`/`set`).
void validatePattern(const ElementParams& p, const char* method, const std::string& target) {
    std::string err;
    const auto pat = parsePattern(p.text("pattern", kDefaultPattern), p.text("placeholder", " "), err);
    if (!pat) throwError(method, target, "Invalid `pattern`: " + err);
    if (!validateBlockOptions(*pat, err)) throwError(method, target, err);
    for (const auto& b : pat->blocks) {
        if (b.csr) canonicalize(method, target, cursorSpec(), *b.csr); // an exception on error
    }
}

// Converting the writable derived parameters (`value`) to stored state; `blocks` is read-only.
void resolveInputDerived(const ElementTree&, const ElementRegistry&, ElementParams& p) {
    const std::string target = elementTarget("input", p);
    if (p.getSet("blocks") != nullptr) {
        p.remove("blocks");
        throwError("set", target, "`blocks` is read-only");
    }
    const std::string* v = p.getSet("value");
    if (v == nullptr) {
        p.remove("value");
        return;
    }
    const std::string value = *v;
    p.remove("value");
    InputEditor ed = makeEditor(p);
    std::string err;
    if (!ed.setValue(value, err)) throwError("set", target, "Invalid `value`: " + err);
    saveState(p, ed);
}

void deriveInputParams(const ElementTree&, const ElementRegistry&, const std::string&, ElementParams& view) {
    const InputEditor ed = makeEditor(view);
    view.set("value", ed.value());
    view.set("blocks", encodeList(ed.blockValues()));
}

int autoWidth(const InputEditor& ed) {
    const int total = static_cast<int>(ed.totalWidth());
    return std::max(1, std::max(total, static_cast<int>(ed.cursorColumn()) + 1));
}

int inputWidth(const ElementParams& p, const InputEditor& ed) {
    return paramInt(p, "width").value_or(autoWidth(ed));
}

// DECSCUSR code from the canonical cursor shape name.
int shapeCode(const std::string& canonical) {
    if (canonical == "bBlock") return 1;
    if (canonical == "Block") return 2;
    if (canonical == "bUnderline") return 3;
    if (canonical == "Underline") return 4;
    if (canonical == "bBar") return 5;
    return 6; // Bar
}

std::optional<ElementKind::CursorSpot> inputCursorSpot(const ElementTree& t, const std::string& id) {
    const ElementParams& p = t.find(id)->params;
    const InputEditor ed = makeEditor(p);
    const InputBlock& b = ed.pattern().blocks[ed.blockIndex()];
    std::string shape = p.text("cursorType", "bBar");
    if (b.csr) {
        try {
            shape = canonicalize("get", "-", cursorSpec(), *b.csr);
        } catch (const ToytuiException&) {
            // the stored value is always valid; defensive: the element-level shape stays
        }
    }
    const int w = inputWidth(p, ed);
    const int total = static_cast<int>(ed.totalWidth());
    const int col = static_cast<int>(ed.cursorColumn());
    const int off = effectiveInputScroll(paramInt(p, "edit_scroll", 0), col, w, total);
    ElementKind::CursorSpot spot;
    spot.x = col - off;
    spot.y = 0;
    spot.shape = shapeCode(shape);
    return spot;
}

Block buildInputBlock(const ElementTree& t, const std::string& id, std::uint8_t bg, std::uint8_t fg) {
    const ElementParams& p = t.find(id)->params;
    const InputEditor ed = makeEditor(p);

    // the pattern's SGR applies in output order (display only)
    std::string sgrAcc;
    std::vector<Cell> all;
    for (const auto& piece : ed.display()) {
        if (piece.sgr) {
            sgrAcc += "\\e" + piece.text.substr(1);
            continue;
        }
        std::vector<Cell> cs = parseAnsiText(sgrAcc + piece.text);
        all.insert(all.end(), cs.begin(), cs.end());
    }

    const int total = static_cast<int>(all.size());
    const int w = inputWidth(p, ed);
    const int off = effectiveInputScroll(paramInt(p, "edit_scroll", 0), static_cast<int>(ed.cursorColumn()), w, total);

    Block b;
    b.width = w;
    b.height = 1;
    b.cells.assign(static_cast<std::size_t>(w), neutralBlankCell());
    for (int i = 0; i < w; ++i) {
        const int idx = off + i;
        if (idx >= 0 && idx < total) b.cells[static_cast<std::size_t>(i)] = all[static_cast<std::size_t>(idx)];
    }
    repairWidePairs(b.cells);

    const std::uint16_t inheritedAttr =
        static_cast<std::uint16_t>(t.resolveInheritableInt(id, "attrib") & 0x7F);
    resolveUnsetCells(b.cells, bg, fg, inheritedAttr);
    return b;
}

// --- keys ---------------------------------------------------------------------------------------

std::vector<std::string> keysOf(const ElementParams& p, const char* name) {
    const std::string* s = p.getSet(name);
    if (s == nullptr || s->empty()) return {};
    const auto k = parseExitKeys(*s);
    return k ? *k : std::vector<std::string>{};
}

bool hasKey(const std::vector<std::string>& keys, const std::string& k) {
    return std::find(keys.begin(), keys.end(), k) != keys.end();
}

bool tokenExplicit(const std::vector<ParamToken>& tokens, const char* name) {
    for (const auto& t : tokens) {
        if (t.name == name) return !t.value.has_value() || *t.value != "auto";
    }
    return false;
}

// Canonicalising the key lists and checking for conflicts: `keys_exit` and `keys_accept` must not overlap
// (error); the default of the nav keys is overridden by exit/accept (and by the other, explicitly given nav list), but
// a clash of an explicitly given nav key in the same call is an error.
void finalizeKeys(ElementParams& p, const std::vector<ParamToken>& tokens, bool isNew, const char* method,
                  const std::string& target) {
    static const char* kAll[] = {"keys_exit", "keys_accept", "keys_navNext", "keys_navPrev", "keys_navHome", "keys_navEnd"};
    static const char* kNav[] = {"keys_navNext", "keys_navPrev", "keys_navHome", "keys_navEnd"};
    for (const char* name : kAll) {
        const std::string* found = p.getSet(name);
        if (found == nullptr) continue;
        const std::string v = *found;
        if (v.empty() && std::string(name) != "keys_accept") continue; // empty: no key
        const auto keys = parseExitKeys(v);
        if (!keys) {
            throwError(method, target,
                       std::string("Invalid key list (expected one or more keys, e.g. `[ENTER]`, `[ESC][CTRL-Q]`): `") +
                           name + ":" + v + "`");
        }
        std::vector<std::string> uniq;
        for (const auto& k : *keys) {
            if (!hasKey(uniq, k)) uniq.push_back(k);
        }
        p.set(name, joinKeys(uniq));
    }
    if (p.getSet("keys_accept") == nullptr) {
        throwError(method, target, isNew ? "Missing required parameter: `keys_accept`" : "Missing `keys_accept`");
    }
    const auto ex = keysOf(p, "keys_exit");
    const auto ac = keysOf(p, "keys_accept");
    for (const auto& k : ex) {
        if (hasKey(ac, k)) {
            throwError(method, target, "Key conflict: `" + k + "` is in both `keys_exit` and `keys_accept`");
        }
    }
    for (const char* name : kNav) {
        std::vector<std::string> keys = keysOf(p, name);
        const bool expl = tokenExplicit(tokens, name);
        std::vector<std::string> kept;
        for (const auto& k : keys) {
            bool drop = false;
            if (hasKey(ex, k) || hasKey(ac, k)) {
                if (expl) throwError(method, target, "Key conflict: `" + k + "` in `" + name + "` is already an exit/accept key");
                drop = true;
            }
            for (const char* other : kNav) {
                if (std::string(other) == name || !hasKey(keysOf(p, other), k)) continue;
                const bool otherExpl = tokenExplicit(tokens, other);
                if (expl && otherExpl) throwError(method, target, "Key conflict: `" + k + "` in both `" + name + "` and `" + other + "`");
                if (otherExpl) drop = true; // the explicitly given one wins
                else if (!expl && std::string(name) > std::string(other)) drop = true; // deterministic decision
            }
            if (!drop) kept.push_back(k);
        }
        if (kept.size() != keys.size()) p.set(name, joinKeys(kept));
    }
}

std::optional<char32_t> typedChar(const std::string& key) {
    if (key == "[LBRACKET]") return U'[';
    if (key.empty() || key[0] == '[') return std::nullopt;
    const std::u32string u = toU32(key);
    if (u.size() != 1 || u[0] < 0x20 || u[0] == 0x7F) return std::nullopt;
    return u[0];
}

std::string quoteOut(const std::string& v) {
    std::string out = "\"";
    for (char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

// Saving the editor state + adjusting the scroll to the cursor.
void commit(ElementParams& p, const InputEditor& ed) {
    saveState(p, ed);
    const int w = inputWidth(p, ed);
    p.set("edit_scroll", std::to_string(effectiveInputScroll(paramInt(p, "edit_scroll", 0), static_cast<int>(ed.cursorColumn()),
                                                           w, static_cast<int>(ed.totalWidth()))));
}

std::string stateKey(const ElementParams& p) {
    return p.text("edit_blocks") + "\n" + p.text("edit_cursor") + "\n" + p.text("edit_scroll") + "\n" + p.text("inputMode");
}

ElementKind::KeyResult inputOnKey(ElementTree& t, const ElementRegistry&, const std::string& id, const std::string& key) {
    ElementParams& p = t.findMut(id)->params;
    const std::string before = stateKey(p);
    InputEditor ed = makeEditor(p);
    if (hasKey(keysOf(p, "keys_navNext"), key)) {
        ed.nextBlock();
    } else if (hasKey(keysOf(p, "keys_navPrev"), key)) {
        ed.prevBlock();
    } else if (hasKey(keysOf(p, "keys_navHome"), key)) {
        ed.home();
    } else if (hasKey(keysOf(p, "keys_navEnd"), key)) {
        ed.end();
    } else if (key == "[LEFT]") {
        ed.left();
    } else if (key == "[RIGHT]") {
        ed.right();
    } else if (key == "[BS]") {
        ed.backspace();
    } else if (key == "[DEL]") {
        ed.del();
    } else if (key == "[INS]") {
        p.set("inputMode", p.text("inputMode", "insert") == "overwrite" ? "insert" : "overwrite");
    } else if (const auto c = typedChar(key)) {
        ed.insertChar(*c);
    } else {
        return false; // not an interpreted key: dropped
    }
    commit(p, ed);
    return stateKey(p) != before;
}

std::vector<std::string> inputExitKeys(const ElementTree& t, const std::string& id) {
    const ElementParams& p = t.find(id)->params;
    std::vector<std::string> keys = keysOf(p, "keys_exit");
    for (const auto& k : keysOf(p, "keys_accept")) {
        if (!hasKey(keys, k)) keys.push_back(k);
    }
    return keys;
}

ElementKind::ExitResult inputOnExit(ElementTree& t, const ElementRegistry&, const std::string& id, const std::string& key) {
    const ElementParams& p = t.find(id)->params;
    const InputEditor ed = makeEditor(p);
    ElementKind::ExitResult r;
    r.fields.emplace_back("accepted", hasKey(keysOf(p, "keys_accept"), key) ? "on" : "off");
    r.fields.emplace_back("complete", ed.complete() ? "on" : "off");
    r.fields.emplace_back("value", quoteOut(ed.value()));
    return r;
}

void finalize(ElementParams& p, const std::vector<ParamToken>& tokens, bool isNew, const char* method,
              const std::string& target) {
    validatePattern(p, method, target);
    finalizeKeys(p, tokens, isNew, method, target);
}

} // namespace

int effectiveInputScroll(int stored, int cursorColumn, int width, int total) {
    if (width <= 0) return 0;
    const int needed = std::max(total, cursorColumn + 1);
    int off = std::clamp(stored, 0, kMaxScroll);
    if (cursorColumn < off) off = cursorColumn;
    if (cursorColumn >= off + width) off = cursorColumn - width + 1;
    off = std::min(off, std::max(0, needed - width));
    return std::max(0, off);
}

ElementKind inputElementKind() {
    ElementKind k;
    k.freeTextParams = {};
    k.paramSpecs = inputSpecs();
    k.derivedParams = {"value", "blocks"};
    k.deriveParams = deriveInputParams;
    k.resolveDerived = resolveInputDerived;
    k.create = [](const ParsedCommand& c) {
        ElementParams p = makeDefaultInputParams();
        const std::string target = elementTarget("input", c.params);
        applyParams(p, c.params, inputSpecs(), true, target);
        finalize(p, c.params, true, "new", target);
        return p;
    };
    k.set = [](ElementParams& p, const ParsedCommand& c) {
        const std::string target = elementTarget("input", p);
        const std::string before = p.text("pattern") + "\n" + p.text("placeholder");
        applyParams(p, c.params, inputSpecs(), false, target);
        finalize(p, c.params, false, "set", target);
        if (p.text("pattern") + "\n" + p.text("placeholder") != before) clearState(p); // new pattern: empty field
    };
    k.get = [](const ElementParams& p, const ParsedCommand& c) {
        return getParams(p, c.params, inputSpecs(), elementTarget("input", p));
    };
    k.ownVisual = [](const ElementTree& t, const std::string& id) {
        return buildInputBlock(t, id, t.resolveInheritableColor(id, "bgcol"),
                               t.resolveInheritableColor(id, "fgcol"));
    };
    k.contentArea = [](const ElementTree&, const std::string&, const Block&) { return std::optional<Rect>(); };
    k.cursorSpot = inputCursorSpot;
    k.onKey = inputOnKey;
    k.exitKeys = inputExitKeys;
    k.onExit = inputOnExit;
    return k;
}

} // namespace toytui
