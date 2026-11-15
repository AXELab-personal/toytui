// label_element.h — the `label` element type (with ANSI processing).
//
// TEXT CLEANING AND STORAGE: TWO parameters derive from the raw (escape-containing)
// input of `text:`:
//   - `text`: the CLEANED (ANSI-free, tab/newline->space) text —
//     this is the PUBLISHED value, queryable with `get`, and the length
//     for the `width` rule is computed from it too.
//   - `text_raw` (INTERNAL, undocumented, but technically reachable via
//     `get`/`set`): the ORIGINAL input containing escapes, so that the
//     per-character colour/attribute overrides ("the effect of a given
//     sequence stays in force for every following character")
//     survive LATER renders too (reloaded from the state file), not
//     only the immediate render at `new`/`set` time. Next to the cleaned `text`
//     the extra `text_raw` field is needed so that colours
//     do not vanish at a subsequent, independent render.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cli_parser.h"
#include "element_registry.h"
#include "element_tree.h"
#include "cell.h"
#include "param_spec.h"
#include "param_store.h"

namespace toytui {

// Defaults of the label: position 0, z_index 1, parent core@terminal (real
// defaults); text/width/height/bgcol/fgcol Unset —
// exactly the label's "empty" base state (no own concrete
// colour/size until the user gives one).
ElementParams makeDefaultLabelParams();

// label new: `id` is mandatory. `freeText` is the `text:` block separated by cli_parser
// — nullopt if the call had no `text:` (this is the "bar"/progress-bar
// mode).
ElementParams labelNew(const std::vector<ParamToken>& params,
                        const std::optional<std::string>& freeText);

// label set: modifies only the fields actually given. Validation
// runs entirely before any modification of the store (the same
// principle as for core_element: on error there is no partial
// application).
void labelSet(ElementParams& label, const std::vector<ParamToken>& params,
              const std::optional<std::string>& freeText);

// label get: shows the STORED (not the composited/resolved) level —
// for an explicitly unset parameter the literal text "auto" is returned
// (the same word that `set` accepts for resetting to
// unset; `set ...:unset` is NOT accepted, only `auto`). Why `get` returns the unset
// state: `unset` is meaningful only at the level of stored parameters
// (the final visual output can never
// remain unset) — so `get`, which queries the stored state,
// may rightly return this state. An unknown parameter name/
// value token -> error
// (same as for core_element,
// see the rationale there).
std::vector<std::string> labelGet(const ElementParams& label,
                                   const std::vector<ParamToken>& params);

// Computing the text actually displayed and the effective width
// (width rule): explicit `w:` vs. the length of `text`, clipping or
// padding with spaces. Always returns a `displayText` of exactly `width`
// characters.
// Always returns exactly `width` cells. Overwriting the `unset` (bg/fg
// bitmask-marked, see cell.h) cells is the caller's responsibility (with the label's OWN
// resolved bg/fg) — this function only applies the per-character
// style produced from `text_raw` (if present) by the ansi handler and the
// 5.3 width rule; it has no access to element-level
// colour resolution (it does not know the tree).
struct LabelVisual {
    std::vector<Cell> cells;
    int width = 0; // == cells.size(), convenience member
};
LabelVisual resolveLabelVisual(const ElementParams& label);

// The label's parameter table (the `button` builds on it) and the label's cell block with the
// given (already resolved) element-level colours: the label's own `ownVisual`
// calls it with the inherited colours, the button with the colours swapped by state.
const SpecTable& labelSpecTable();
Block buildLabelBlock(const ElementTree& tree, const std::string& fullId, std::uint8_t bg,
                      std::uint8_t fg);

// Hooking `label` into the registry: its own buffer from `resolveLabelVisual`
// + inherited bg/fg; a leaf (no `contentArea`, no `scroll`/
// `decorate`).
ElementKind labelElementKind();

} // namespace toytui
