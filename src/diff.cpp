#include "diff.h"

#include "ansi_handler.h"
#include "renderer.h"

namespace toytui {

std::string encodeDiff(const std::optional<Block>& prev, const Block& next) {
    if (!prev || prev->width != next.width || prev->height != next.height) {
        return encodeFullRepaint(next.width, next.height, next.cells);
    }

    std::string out;
    for (int row = 0; row < next.height; ++row) {
        int col = 0;
        while (col < next.width) {
            const auto idx = [&](int c) {
                return static_cast<std::size_t>(row) * static_cast<std::size_t>(next.width) +
                       static_cast<std::size_t>(c);
            };
            if (prev->cells[idx(col)] == next.cells[idx(col)]) {
                ++col;
                continue;
            }
            // Wide (two-cell) character: a segment always starts at the LEFT half of the pair and contains both
            // halves (the right half is never emitted alone; the left half's 4-byte character covers two columns).
            int start = col;
            if (start > 0 && isWideRight(next.cells[idx(start)])) {
                --start;
            }
            col = start;
            std::string seg;
            bool haveLast = false;
            bool afterLeft = false;
            Cell last{};
            // `sgrCodesForCell` (ansi_handler.h) — the same basis as
            // the full-repaint path (encodeFullRepaint), so that
            // attributes (bold/underline/etc.) show up here too,
            // not just colours.
            while (col < next.width &&
                   (col == start || afterLeft || prev->cells[idx(col)] != next.cells[idx(col)])) {
                const Cell& c = next.cells[idx(col)];
                afterLeft = isWideLeft(c);
                ++col;
                if (isWideRight(c)) {
                    continue; // the left half's character has already covered this column
                }
                if (!haveLast || !sameCellStyle(c, last)) {
                    seg += sgrResetAndSet(c);
                    last = c;
                    haveLast = true;
                }
                seg += getCellChar(c);
            }
            out += "\x1b[" + std::to_string(row + 1) + ";" + std::to_string(start + 1) + "H";
            out += seg;
            out += "\x1b[0m";
        }
    }
    return out;
}

} // namespace toytui
