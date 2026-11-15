#include "text_fit.h"

#include <algorithm>

namespace toytui {

std::vector<Cell> fitCellsLeft(const std::vector<Cell>& cells, int width, const Cell& filler) {
    if (width <= 0) {
        return {};
    }
    const auto w = static_cast<std::size_t>(width);
    std::vector<Cell> out(cells.begin(),
                          cells.begin() + static_cast<std::ptrdiff_t>(std::min(w, cells.size())));
    out.resize(w, filler);
    repairWidePairs(out); // a wide character cut in two at the edge is dropped as a whole (space)
    return out;
}

} // namespace toytui
