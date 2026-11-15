// focus_cursor.h — the terminal cursor of the focused element (input field): position, shape, sequences.
//
// AFTER rendering, the focused element's `cursorSpot` hook gives the cursor position in the element's coordinates; this is
// converted to screen coordinates by `screenPositionOf`. The sequences (shape `CSI N SP q`, position `CSI r;c H`,
// visibility `CSI ?25h/l`) go out only to a TERMINAL target (`writeIfTerminal`), never to a file or pipe.
// While input is active the cursor is always visible (`term_cursor:off` does not apply then); on leaving the field
// the shape is reset (`CSI 0 SP q`), and `term_cursor` decides the visibility.
#pragma once

#include <string>

#include "element_registry.h"
#include "element_tree.h"

namespace toytui {

struct FocusCursor {
    bool hasSpot = false;  // the focused element has a cursor
    bool visible = false;  // and it is visible on screen (not clipped)
    int row = 0;           // 0-based screen row
    int col = 0;           // 0-based screen column
    int shape = 6;         // DECSCUSR code
};

FocusCursor focusCursorOf(const ElementTree& tree, const ElementRegistry& registry);

// Display sequence: for a visible cursor shape + position + `?25h`; if there is a cursor but it is not visible: `?25l`;
// if there is no cursor: empty.
std::string cursorShowSequence(const FocusCursor& c);

// Reset: `CSI 0 SP q` + (`visible` ? `?25h` : `?25l`).
std::string cursorResetSequence(bool visible);

} // namespace toytui
