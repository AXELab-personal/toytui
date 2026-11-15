// button_element.h — the `button` element type.
//
// All parameters of `label` + state colours + `delay` + `trigger`:
//   focus_bgcol/focus_fgcol (alias: focus_bg/focus_fg), pressed_bgcol/pressed_fgcol
//   (pressed_bg/pressed_fg): the appearance when focused, and during `trigger`.
//   delay: the length of the pressed state in ms (0–10000, default 150).
//   trigger: a write-only action (`set ... trigger:on`), not stored.
// COLOUR RULE (after the colours are resolved): without focus the base `bg`/`fg`; when focused,
// if BOTH members of the `focus_*` pair are unset, the base bg/fg SWAPPED; if at least one is
// given, the explicit value wins and the other member stays that of the base (the user left
// it untouched on purpose). During `trigger` the same rule applies to the CURRENT appearance
// (the base without focus, the focus appearance when focused) and to the `pressed_*` pair.
// A key received while focused is passed on by the button, unmodified, as its notation on stdout.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cli_parser.h"
#include "element_registry.h"
#include "param_store.h"

namespace toytui {

constexpr int kMaxButtonDelayMs = 10000;

ElementParams makeDefaultButtonParams();
ElementParams buttonNew(const std::vector<ParamToken>& params,
                        const std::optional<std::string>& freeText);
void buttonSet(ElementParams& p, const std::vector<ParamToken>& params,
               const std::optional<std::string>& freeText);

ElementKind buttonElementKind();

} // namespace toytui
