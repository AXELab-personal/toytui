#include "textarea_element.h"

#include <algorithm>

#include "ansi_handler.h"
#include "element_tree.h"
#include "error.h"
#include "file_source.h"
#include "utf_escape.h"
#include "param_spec.h"
#include "param_types.h"
#include "text_layout.h"

namespace toytui {

namespace {

// The computed view of `text`: phase 1 (SGR processing + control cleaning)
// re-encoded — multi-line (`\n` and `\t` are kept), not stored.
std::string textareaView(const std::string& raw) { return encodeAnsiText(prepareText(raw)); }

const SpecTable& textareaSpecs() {
    static const SpecTable t = [] {
        SpecTable general = kGeneralSpecs;
        for (auto& s : general) {
            if (std::string(s.name) == "width") {
                s.minValue = 1; // explicit width at least 1 (a line needs at least 1 character)
            } else if (std::string(s.name) == "height") {
                s.maxValue = kMaxTextareaHeight;
            }
        }
        return concatSpecs(
            general,
            {{"tabsize", Kind::Int, false, "4", 0, nullptr, nullptr, 1, 16},
             {"wrap", Kind::Enum, false, "full", 0, nullptr, nullptr, 0, 0,
              "full=1,on,auto;semi=2;off=0"},
             {"align", Kind::Enum, false, "left", 0, nullptr, nullptr, 0, 0,
              "left;right;center;justify"},
             {"text", Kind::TextView, false, nullptr, 0, "text_raw", textareaView},
             {"text_raw", Kind::Text, false, nullptr, kTextParamLimit},
             // write-only helper (to get around the CLI argument limit)
             {"text_add", Kind::Text, false, nullptr, kTextParamLimit, nullptr, nullptr, 0, 0,
              nullptr, true},
             // write-only: RELOADING the buffer from a file (a one-off load, not stored)
             {"src", Kind::Text, false, nullptr, kTextParamLimit, nullptr, nullptr, 0, 0, nullptr,
              true}});
    }();
    return t;
}

// One-off loading of the `src:<path>` tokens: the file content REPLACES the raw buffer
// (like `text_raw:`), so the token is turned in place into a `text_raw` token (later tokens —
// `text_raw:`, `text:`, `text_add:` — thus build on the loaded content, the last one wins).
// The file is read BEFORE the modification: on error (no file, permission, directory, not a regular file,
// above 512 KiB) nothing is modified. The content is not inspected; a relative path is relative to the
// process's working directory (the startup directory).
std::vector<ParamToken> withSourceFiles(const std::vector<ParamToken>& params,
                                        const std::string& method, const std::string& target) {
    std::vector<ParamToken> all;
    all.reserve(params.size());
    for (const auto& tok : params) {
        if (tok.name != "src" || !tok.value.has_value()) {
            all.push_back(tok);
            continue;
        }
        std::string content;
        std::string why;
        if (!readRegularFile(*tok.value, kTextParamLimit, content, why)) {
            throwError(method, target, "Cannot read source file: " + why + ": `" + *tok.value + "`");
        }
        // Only `\u[HEX]` is resolved (utf_escape); the file's other bytes stay unchanged.
        all.push_back(ParamToken{"text_raw", decodeUnicodeEscapesInFile(content)});
    }
    return all;
}

// Turning the free text into a token: `text:` -> overwriting the raw field; `text_add:` ->
// appending to the end of the buffer (as possibly modified by a `text_raw` given in the same command);
// an empty `text_add:` has no effect. The size check applies to the size AFTER THE OPERATION,
// before the modification (atomic).
std::vector<ParamToken> withFreeText(const std::vector<ParamToken>& params,
                                     const std::optional<std::string>& freeText,
                                     const std::string& freeTextName, const ElementParams* existing,
                                     const std::string& method, const std::string& target) {
    std::vector<ParamToken> all = params;
    if (!freeText.has_value()) {
        return all;
    }
    if (freeTextName != "text_add") {
        all.push_back(ParamToken{"text_raw", *freeText});
        return all;
    }
    if (freeText->empty()) {
        return all;
    }
    std::string base;
    if (existing != nullptr) {
        base = existing->text("text_raw");
    }
    for (const auto& tok : params) {
        if (tok.name == "text_raw" && tok.value.has_value()) {
            base = *tok.value;
        }
    }
    const std::size_t total = base.size() + freeText->size();
    if (total > kTextParamLimit) {
        throwError(method, target,
                 "Text too long (" + std::to_string(total) + " > " +
                     std::to_string(kTextParamLimit) + " bytes): `text_add`");
    }
    all.push_back(ParamToken{"text_raw", base + *freeText});
    return all;
}

WrapMode wrapOf(const ElementParams& p) {
    const std::string w = p.text("wrap", "full");
    return w == "off" ? WrapMode::Off : (w == "semi" ? WrapMode::Semi : WrapMode::Full);
}

AlignMode alignOf(const ElementParams& p) {
    const std::string a = p.text("align", "left");
    if (a == "right") {
        return AlignMode::Right;
    }
    if (a == "center") {
        return AlignMode::Center;
    }
    return a == "justify" ? AlignMode::Justify : AlignMode::Left;
}

// The logical width: explicit `width`, otherwise the width of the PARENT'S CONTENT AREA
// (not the inherited/full width). 0 = nothing is drawn.
int widthOf(const ElementTree& t, const ElementRegistry& reg, const std::string& id) {
    const ElementParams& p = t.find(id)->params;
    if (const auto w = paramInt(p, "width")) {
        return std::max(0, *w);
    }
    const std::optional<int> w = parentContentSize(t, reg, p, false);
    return w ? std::max(0, *w) : 0;
}

std::vector<DisplayLine> textareaLines(const ElementParams& p, int width) {
    const std::string text = p.text("text_raw");
    return breakLines(prepareText(text), width, paramInt(p, "tabsize").value_or(4), wrapOf(p));
}

} // namespace

ElementParams makeDefaultTextareaParams() {
    ElementParams p;
    p.set("parent", "core@terminal");
    p.set("posX", "0");
    p.set("posY", "0");
    p.set("z_index", "1");
    p.set("tabsize", "4");
    p.set("wrap", "full");
    p.set("align", "left");
    return p;
}

ElementParams textareaNew(const std::vector<ParamToken>& params,
                          const std::optional<std::string>& freeText,
                          const std::string& freeTextName) {
    ElementParams p = makeDefaultTextareaParams();
    const std::string target = elementTarget("textarea", params);
    applyParams(p,
                withFreeText(withSourceFiles(params, "new", target), freeText, freeTextName, nullptr,
                             "new", target),
                textareaSpecs(), true, target);
    return p;
}

void textareaSet(ElementParams& p, const std::vector<ParamToken>& params,
                 const std::optional<std::string>& freeText, const std::string& freeTextName) {
    const std::string target = elementTarget("textarea", p);
    applyParams(p,
                withFreeText(withSourceFiles(params, "set", target), freeText, freeTextName, &p, "set",
                             target),
                textareaSpecs(), false, target);
}

std::vector<std::string> textareaGet(const ElementParams& p, const std::vector<ParamToken>& params) {
    return getParams(p, params, textareaSpecs(), elementTarget("textarea", p));
}

ElementKind textareaElementKind() {
    ElementKind k;
    k.freeTextParams = {"text", "text_add"};
    k.measure = [](const ElementTree& t, const ElementRegistry& reg, const std::string& id) {
        const int w = widthOf(t, reg, id);
        if (w <= 0) {
            return std::make_pair(0, 0); // zero-width parent area: nothing is drawn
        }
        const ElementParams& p = t.find(id)->params;
        if (const auto h = paramInt(p, "height")) {
            return std::make_pair(w, *h); // explicit height: formatting is not needed
        }
        return std::make_pair(w, static_cast<int>(textareaLines(p, w).size()));
    };
    k.ownVisualWindow = [](const ElementTree& t, const ElementRegistry& reg, const std::string& id,
                           const Rect& win) {
        const ElementParams& p = t.find(id)->params;
        const int w = widthOf(t, reg, id);
        Block b = renderLines(textareaLines(p, w), w, alignOf(p), win.y, win.height, win.x, win.width);
        resolveUnsetCells(b.cells, t.resolveInheritableColor(id, "bgcol"),
                          t.resolveInheritableColor(id, "fgcol"),
                          static_cast<std::uint16_t>(t.resolveInheritableInt(id, "attrib") & 0x7F));
        return b;
    };
    // the compositor does not call it for a windowed element; a leaf
    k.ownVisual = [](const ElementTree&, const std::string&) { return Block{}; };
    k.contentArea = [](const ElementTree&, const std::string&, const Block&) {
        return std::optional<Rect>();
    };
    k.create = [](const ParsedCommand& c) {
        return textareaNew(c.params, c.freeText, c.freeTextName);
    };
    k.set = [](ElementParams& p, const ParsedCommand& c) {
        textareaSet(p, c.params, c.freeText, c.freeTextName);
    };
    k.get = [](const ElementParams& p, const ParsedCommand& c) { return textareaGet(p, c.params); };
    k.paramSpecs = textareaSpecs();
    return k;
}

} // namespace toytui
