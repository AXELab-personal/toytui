// input_element.h — the `input` element type (input field): single-line, pattern-driven (`pattern`)
// input with blocks. A leaf element.
//
// STORED STATE: the raw block texts (`edit_blocks`, an escaped list separated by `|`), the cursor
// (`edit_cursor` = `block:offset`) and the horizontal scroll (`edit_scroll`). These are INTERNAL, unlisted
// parameters (not shown by `get`/`param_list`). `value` and `blocks` are DERIVED parameters (not stored):
// `get` computes them from the pattern and the block texts; writing `value` (`new`/`set`) sets the block texts.
#pragma once

#include <string>

#include "element_registry.h"

namespace toytui {

// The effective value of `edit_scroll`: the stored offset adjusted so that the cursor column is visible in the
// `width`-wide window (minimal movement), and the window does not extend beyond the content. `total` is the content width.
int effectiveInputScroll(int stored, int cursorColumn, int width, int total);

ElementKind inputElementKind();

} // namespace toytui
