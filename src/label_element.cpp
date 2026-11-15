#include "label_element.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "ansi_handler.h"
#include "param_spec.h"
#include "param_types.h"
#include "text_fit.h"

namespace toytui {

namespace {
const SpecTable& labelSpecs();
}

const SpecTable& labelSpecTable() { return labelSpecs(); }

namespace {

// label: the general parameters + `text` (derived view) + `text_raw`
// (the stored raw value). `text:` is separated by cli_parser as free text;
// writing `text` sets the raw field (`applyParams`), the view is
// computed from the raw one (`processedSingleLine`), it is not stored.
const SpecTable& labelSpecs() {
    static const SpecTable t = concatSpecs(
        kGeneralSpecs,
        {{"text", Kind::TextView, false, nullptr, 0, "text_raw", processedSingleLine},
         {"text_raw", Kind::Text, false, nullptr, kTextParamLimit}});
    return t;
}

// The free text sets the raw field (after the other tokens: the last one wins).
std::vector<ParamToken> withText(const std::vector<ParamToken>& params,
                                  const std::optional<std::string>& freeText) {
    std::vector<ParamToken> all = params;
    if (freeText.has_value()) {
        all.push_back(ParamToken{"text_raw", *freeText});
    }
    return all;
}

} // namespace

ElementParams makeDefaultLabelParams() {
    // Real (non-inherited) defaults: position 0, z_index 1, parent core.
    // width/height/bgcol/fgcol/attrib/text/text_raw Unset.
    ElementParams p;
    p.set("parent", "core@terminal");
    p.set("posX", "0");
    p.set("posY", "0");
    p.set("z_index", "1");
    return p;
}

ElementParams labelNew(const std::vector<ParamToken>& params,
                        const std::optional<std::string>& freeText) {
    ElementParams label = makeDefaultLabelParams();
    applyParams(label, withText(params, freeText), labelSpecs(), true, elementTarget("label", params));
    return label;
}

void labelSet(ElementParams& label, const std::vector<ParamToken>& params,
              const std::optional<std::string>& freeText) {
    applyParams(label, withText(params, freeText), labelSpecs(), false, elementTarget("label", label));
}

std::vector<std::string> labelGet(const ElementParams& label,
                                   const std::vector<ParamToken>& params) {
    return getParams(label, params, labelSpecs(), elementTarget("label", label));
}

LabelVisual resolveLabelVisual(const ElementParams& label) {
    std::vector<Cell> cells;
    if (const std::string* raw = label.getSet("text_raw")) {
        cells = parseAnsiText(*raw);
        cleanSingleLine(cells);
    }

    const std::optional<int> explicitWidthOpt = paramInt(label, "width");
    const bool hasExplicitWidth = explicitWidthOpt.has_value();
    const int explicitWidth = explicitWidthOpt.value_or(0);

    // 5.3 width rule: with an explicit `w:` it decides everything
    // (clipping from the START of the text / padding), otherwise the actual length
    // of the (cleaned) text. The filler is the shared neutral cell (everything
    // UNSET), so the padding also inherits the element's attributes.
    LabelVisual result;
    result.cells = hasExplicitWidth ? fitCellsLeft(cells, explicitWidth, neutralBlankCell())
                                    : std::move(cells);
    result.width = static_cast<int>(result.cells.size());
    return result;
}

Block buildLabelBlock(const ElementTree& t, const std::string& id, std::uint8_t bg, std::uint8_t fg) {
    {
        LabelVisual v = resolveLabelVisual(t.find(id)->params);
        // `attrib` is inheritable (see cell.h
        // ATTRIB_UNSET) — resolved through the tree just like bg/fg.
        const std::uint16_t inheritedAttr = static_cast<std::uint16_t>(
            t.resolveInheritableInt(id, "attrib") & 0x7F); // only bits 0-6 are meaningful here

        Block b;
        b.width = v.width;
        b.height = 1;
        b.cells = std::move(v.cells);
        // Following the colour pattern: if the character has no OWN (SGR-derived) attribute,
        // the element-level attrib (resolved through the tree) OVERWRITES (does not add to) it.
        resolveUnsetCells(b.cells, bg, fg, inheritedAttr);
        return b;
    }
}

ElementKind labelElementKind() {
    ElementKind k;
    k.ownVisual = [](const ElementTree& t, const std::string& id) {
        return buildLabelBlock(t, id, t.resolveInheritableColor(id, "bgcol"),
                               t.resolveInheritableColor(id, "fgcol"));
    };
    k.contentArea = [](const ElementTree&, const std::string&, const Block&) {
        return std::optional<Rect>(); // the label is a leaf
    };
    k.create = [](const ParsedCommand& c) { return labelNew(c.params, c.freeText); };
    k.set = [](ElementParams& p, const ParsedCommand& c) { labelSet(p, c.params, c.freeText); };
    k.get = [](const ElementParams& p, const ParsedCommand& c) { return labelGet(p, c.params); };
    k.paramSpecs = labelSpecs();
    return k;
}

} // namespace toytui
