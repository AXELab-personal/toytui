// text_layout.h — formatting multi-line text (textarea).
//
// PURE, stateless functions (no tree, no parameter store): the
// textarea element binds its parameters to them. Three phases building on each other:
//   1. prepareText   — raw text -> cells (SGR processing, control characters),
//   2. breakLines    — cells -> display lines (tab, wrapping),
//   3. alignLine     — aligning one line to the width (lazily, line by line,
//                      so that only the visible lines are built from a large text).
// `renderLines` builds the requested window (rectangle) as a Block.
// One cell = one column; a character above the BMP is two cells (WIDE_LEFT+WIDE_RIGHT), indivisible (cell.h).
#pragma once

#include <string>
#include <vector>

#include "cell.h"

namespace toytui {

enum class WrapMode { Full, Semi, Off };
enum class AlignMode { Left, Right, Center, Justify };

// Phase 1. `raw` -> cells: SGR processing (`parseAnsiText`); every `\r`
// dropped (so `\r\n` is one line break, a lone `\r` vanishes); `\v`/`\f` ->
// space; the other control characters (C0 except `\n`/`\t`, DEL, C1) removed.
// `\n` and `\t` are kept as cells.
std::vector<Cell> prepareText(const std::string& raw);

// One display line (the result of phase 2).
struct DisplayLine {
    std::vector<Cell> cells; // its length with `wrap:off` may exceed the width
    bool soft = false;       // ended because of wrapping (not `\n`/end of text) -> justify may stretch it
    bool innerTab = false;   // has a tab AFTER content (not as a leading indent) -> justify does not stretch it
};

// Phase 2. `width` >= 1, `tabsize` 1..16 (validated by the caller; out-of-range values are clamped).
//   - `\n` is always a hard line break; a trailing `\n` is a terminator: "a\n" = 1 line, "a\n\n" = 2 lines,
//     empty text = 1 empty line.
//   - `\t`: spaces up to the next multiple of tabsize; if the stop falls
//     beyond the width: in mid-line a line break (the tab is used up), at the start of a line
//     it is dropped; a stop falling exactly on the right edge fits. `Off`: no wrapping, the tab
//     fills the same way (the line may be longer than the width).
//   - `Full`: when the line is full, search backwards for the first break point ([space , . ! ? : ;];
//     it breaks AFTER a punctuation mark, at a space the space disappears; a tab and its filling are not break points;
//     the break must not produce a line that merely looks empty), otherwise a forced break.
//     If a space arrives after a full line, that is the break (it is absorbed).
//   - `Semi`: forced break only.
std::vector<DisplayLine> breakLines(const std::vector<Cell>& cells, int width, int tabsize,
                                    WrapMode wrap);

// Phase 3: exactly `width` cells (padded with the neutral cell).
//   left: flush left; right/center: trailing spaces do not count towards the length, the odd
//   remaining space goes to the LEFT side; justify: stretches only `soft` and `!innerTab` lines,
//   the inter-word space runs (the "left-right-outer-inner" algorithm), the others are set flush left.
//   A line longer than the width is cut at its right end for every alignment.
std::vector<Cell> alignLine(const DisplayLine& line, int width, AlignMode align);

// The justify gap distribution on its own (for testability): `gaps` are the lengths of the inter-word gaps
// (left to right); distributes `extra` spaces. Returns the widened lengths.
std::vector<int> distributeJustify(std::vector<int> gaps, int extra);

// The window (the `window` rectangle of the full image, aligned line by line) as a Block; after the
// end of the lines neutral cells. `window` lies within the column range [0,width).
Block renderLines(const std::vector<DisplayLine>& lines, int width, AlignMode align,
                  int firstRow, int rowCount, int firstCol, int colCount);

} // namespace toytui
