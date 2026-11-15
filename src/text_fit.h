// text_fit.h — shared text-fitting helper: cutting/padding a label to the width given by `w:`.
// CELL-based (per-character style is kept), not a byte-based string.
#pragma once

#include <vector>

#include "cell.h"

namespace toytui {

// Returns a result exactly `width` cells long: if `cells` is longer or
// equal, the first `width` cells (counted from the START) remain (the END
// is cut off); if shorter, it is padded at the end with `filler` cells.
// Empty when `width<=0`. This is the only
// implementation of this logic.
//
std::vector<Cell> fitCellsLeft(const std::vector<Cell>& cells, int width, const Cell& filler);

} // namespace toytui
