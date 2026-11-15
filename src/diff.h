// diff.h — diff-based output: only the difference between the two COMPOSITED
// full images goes to the terminal, grouped into runs.
// On a size change (no `prev`, or a different size) a full clear +
// repaint (encodeFullRepaint) is done — the safe, always-correct
// base case.
#pragma once

#include <optional>
#include <string>

#include "cell.h"

namespace toytui {

std::string encodeDiff(const std::optional<Block>& prev, const Block& next);

} // namespace toytui
