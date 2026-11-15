// select_element.h — the `select` element type: a selection list.
//
// A (optionally) framed list, one `options` item per row; the cursor (`cursor`, 1-based) moves with keys
// ([UP] [DOWN] [PGUP] [PGDN] [HOME] [END], without wrap-around), the list scrolls with the cursor
// (`top`: 0-based index of the first visible item; vertical only). The exit keys (the union of `keys_exit:` and
// `keys_accept:`) are DECLARED by the element (ElementKind::exitKeys), the output is `exit:<k> selected:<n>`.
// A standalone, windowed leaf element: the frame is drawn by the same code as the box frame, the rows by the label's cell
// building (only the visible rows are built).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cli_parser.h"
#include "element_registry.h"
#include "param_store.h"

namespace toytui {

constexpr int kMaxSelectIndex = 1 << 20; // upper bound of cursor/selected/top

ElementParams makeDefaultSelectParams();
ElementParams selectNew(const std::vector<ParamToken>& params);
void selectSet(ElementParams& p, const std::vector<ParamToken>& params);

ElementKind selectElementKind();

} // namespace toytui
