#include "text_layout.h"

#include <algorithm>

#include "ansi_handler.h"

namespace toytui {

namespace {

bool isChar(const Cell& c, char ch) { return c.ch[0] == static_cast<unsigned char>(ch) && c.ch[1] == 0; }

bool isSpaceCell(const Cell& c) { return isChar(c, ' '); }

bool isBreakPunct(const Cell& c) {
    return c.ch[1] == 0 && (c.ch[0] == ',' || c.ch[0] == '.' || c.ch[0] == '!' || c.ch[0] == '?' ||
                            c.ch[0] == ':' || c.ch[0] == ';');
}

struct Col {
    Cell cell;
    std::size_t src; // index of the original cell (the tab's filling belongs to the tab)
    bool fill;       // tab filler space
};

// The filling: with the tab cell's style, as a space.
Cell fillCell(const Cell& tab) {
    Cell c = tab;
    setCellChar(c, " ");
    return c;
}

// A column suitable for breaking in the full line, searching backwards; -1: none.
int findBreak(const std::vector<Col>& cur) {
    int firstNonBlank = -1;
    for (std::size_t k = 0; k < cur.size(); ++k) {
        if (!isSpaceCell(cur[k].cell)) {
            firstNonBlank = static_cast<int>(k);
            break;
        }
    }
    for (int j = static_cast<int>(cur.size()) - 1; j >= 0; --j) {
        const Col& col = cur[static_cast<std::size_t>(j)];
        if (col.fill) {
            continue;
        }
        if (isSpaceCell(col.cell)) {
            if (firstNonBlank >= 0 && firstNonBlank < j) {
                return j; // the part before the break contains a visible character
            }
        } else if (isBreakPunct(col.cell)) {
            return j;
        }
    }
    return -1;
}

void layoutParagraph(const std::vector<Cell>& cells, std::size_t b, std::size_t e, int W, int ts,
                     WrapMode wrap, std::vector<DisplayLine>& out) {
    std::vector<Col> cur;
    bool anyLine = false;

    auto emit = [&](std::size_t count, bool soft) {
        DisplayLine line;
        line.soft = soft;
        line.cells.reserve(count);
        int firstNonBlank = -1;
        for (std::size_t k = 0; k < count; ++k) {
            line.cells.push_back(cur[k].cell);
            if (firstNonBlank < 0 && !isSpaceCell(cur[k].cell)) {
                firstNonBlank = static_cast<int>(k);
            }
        }
        if (firstNonBlank >= 0) {
            for (std::size_t k = static_cast<std::size_t>(firstNonBlank); k < count; ++k) {
                if (cur[k].fill) {
                    line.innerTab = true;
                    break;
                }
            }
        }
        out.push_back(std::move(line));
        anyLine = true;
        cur.clear();
    };

    std::size_t i = b;
    while (i < e) {
        const Cell& c = cells[i];
        if (isChar(c, '\t')) {
            const int col = static_cast<int>(cur.size());
            const int stop = (col / ts + 1) * ts;
            if (wrap != WrapMode::Off && stop > W) {
                if (col == 0) {
                    ++i; // dropped at the start of a line
                } else {
                    emit(cur.size(), true); // mid-line: the tab is a line break
                    ++i;
                }
                continue;
            }
            for (int k = col; k < stop; ++k) {
                cur.push_back({fillCell(c), i, true});
            }
            ++i;
            continue;
        }
        if (isWideLeft(c) && wrap != WrapMode::Off && W < 2) {
            i += 2; // the two-cell character never fits in the line: dropped (its right half too)
            continue;
        }
        // A wide character (left + right half) is indivisible: it needs two columns; its right half always
        // fits, because it was reserved with the left half.
        const int need = isWideLeft(c) ? 2 : 1;
        if (wrap != WrapMode::Off && static_cast<int>(cur.size()) + need > W) {
            if (wrap == WrapMode::Full) {
                if (isSpaceCell(c)) {
                    emit(cur.size(), true); // a space arriving after a full line is the break
                    ++i;
                    continue;
                }
                const int j = findBreak(cur);
                if (j >= 0) {
                    const Col& bp = cur[static_cast<std::size_t>(j)];
                    const std::size_t resume = bp.src + 1;
                    const std::size_t count =
                        isSpaceCell(bp.cell) ? static_cast<std::size_t>(j) : static_cast<std::size_t>(j) + 1;
                    emit(count, true);
                    i = resume;
                    continue;
                }
            }
            emit(cur.size(), true); // forced break; the cell is processed on the new line
            continue;
        }
        cur.push_back({c, i, false});
        ++i;
    }
    if (!cur.empty() || !anyLine) {
        emit(cur.size(), false);
    }
}

} // namespace

std::vector<Cell> prepareText(const std::string& raw) {
    std::vector<Cell> cells = parseAnsiText(raw);
    for (auto& c : cells) {
        if (isChar(c, '\v') || isChar(c, '\f')) {
            setCellChar(c, " ");
        }
    }
    dropControlChars(cells, true); // `\r` is a control character too: dropped
    return cells;
}

std::vector<DisplayLine> breakLines(const std::vector<Cell>& cells, int width, int tabsize,
                                    WrapMode wrap) {
    const int W = std::max(1, width);
    const int ts = std::clamp(tabsize, 1, 16);
    std::vector<std::pair<std::size_t, std::size_t>> paras;
    std::size_t start = 0;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (isChar(cells[i], '\n')) {
            paras.emplace_back(start, i);
            start = i + 1;
        }
    }
    paras.emplace_back(start, cells.size());
    if (paras.size() > 1 && paras.back().first == paras.back().second) {
        paras.pop_back(); // the trailing `\n` is a terminator, not a new (empty) line
    }
    std::vector<DisplayLine> out;
    for (const auto& p : paras) {
        layoutParagraph(cells, p.first, p.second, W, ts, wrap, out);
    }
    return out;
}

std::vector<int> distributeJustify(std::vector<int> gaps, int extra) {
    const int k = static_cast<int>(gaps.size());
    if (k == 0) {
        return gaps;
    }
    const int leftCount = (k + 1) / 2; // left half: the first ceil(k/2) gaps
    int L = 1;
    int guard = 0;
    while (extra > 0) {
        bool any = false;
        // one cycle: left-right-outer-inner; each step widens at most one gap of length L
        auto step = [&](int from, int to, int dir) {
            if (extra <= 0) {
                return;
            }
            for (int idx = from; idx != to; idx += dir) {
                if (gaps[static_cast<std::size_t>(idx)] == L) {
                    ++gaps[static_cast<std::size_t>(idx)];
                    --extra;
                    any = true;
                    return;
                }
            }
        };
        step(0, leftCount, 1);          // (1) from the left towards the middle
        step(k - 1, leftCount - 1, -1); // (2) from the right towards the middle
        step(leftCount - 1, -1, -1);    // (3) from the middle to the left
        step(leftCount, k, 1);          // (4) from the middle to the right
        if (!any) {
            ++L; // no more gaps of length L
        }
        if (++guard > 4 * (1 << 21)) {
            break; // defensive (in theory unreachable)
        }
    }
    return gaps;
}

std::vector<Cell> alignLine(const DisplayLine& line, int width, AlignMode align) {
    const int W = std::max(1, width);
    const Cell blank = neutralBlankCell();
    const std::vector<Cell>& src = line.cells;
    std::vector<Cell> out;
    out.reserve(static_cast<std::size_t>(W));

    auto padLeftZ = [&](const std::vector<Cell>& cells, int n) { // the first n elements of `cells`
        out.insert(out.end(), cells.begin(), cells.begin() + n);
    };
    auto fillTo = [&]() {
        while (static_cast<int>(out.size()) < W) {
            out.push_back(blank);
        }
    };

    // visible length: without the trailing spaces
    int vis = static_cast<int>(src.size());
    while (vis > 0 && isSpaceCell(src[static_cast<std::size_t>(vis - 1)])) {
        --vis;
    }

    if (align == AlignMode::Left || (align == AlignMode::Justify && (!line.soft || line.innerTab))) {
        padLeftZ(src, std::min(static_cast<int>(src.size()), W));
        fillTo();
        return out;
    }
    if (vis >= W) {
        padLeftZ(src, W); // the right end is cut
        return out;
    }
    if (align == AlignMode::Right) {
        out.assign(static_cast<std::size_t>(W - vis), blank);
        out.insert(out.end(), src.begin(), src.begin() + vis);
        return out;
    }
    if (align == AlignMode::Center) {
        const int rest = W - vis;
        out.assign(static_cast<std::size_t>((rest + 1) / 2), blank); // odd remainder: to the left
        out.insert(out.end(), src.begin(), src.begin() + vis);
        fillTo();
        return out;
    }
    // Justify: inter-word space runs
    int firstNonBlank = 0;
    while (firstNonBlank < vis && isSpaceCell(src[static_cast<std::size_t>(firstNonBlank)])) {
        ++firstNonBlank;
    }
    struct Gap {
        int pos;
        int len;
    };
    std::vector<Gap> gaps;
    for (int p = firstNonBlank; p < vis;) {
        if (isSpaceCell(src[static_cast<std::size_t>(p)])) {
            int q = p;
            while (q < vis && isSpaceCell(src[static_cast<std::size_t>(q)])) {
                ++q;
            }
            gaps.push_back({p, q - p});
            p = q;
        } else {
            ++p;
        }
    }
    if (gaps.empty()) {
        padLeftZ(src, vis);
        fillTo();
        return out;
    }
    std::vector<int> lens;
    for (const auto& g : gaps) {
        lens.push_back(g.len);
    }
    const std::vector<int> grown = distributeJustify(lens, W - vis);
    int p = 0;
    for (std::size_t gi = 0; gi < gaps.size(); ++gi) {
        out.insert(out.end(), src.begin() + p, src.begin() + gaps[gi].pos); // text before the gap
        const Cell style = src[static_cast<std::size_t>(gaps[gi].pos)];
        for (int k = 0; k < grown[gi]; ++k) {
            out.push_back(style); // inherits the gap's style
        }
        p = gaps[gi].pos + gaps[gi].len;
    }
    out.insert(out.end(), src.begin() + p, src.begin() + vis);
    fillTo();
    return out;
}

Block renderLines(const std::vector<DisplayLine>& lines, int width, AlignMode align, int firstRow,
                  int rowCount, int firstCol, int colCount) {
    Block b;
    b.width = std::max(0, colCount);
    b.height = std::max(0, rowCount);
    const Cell blank = neutralBlankCell();
    b.cells.assign(static_cast<std::size_t>(b.width) * static_cast<std::size_t>(b.height), blank);
    for (int r = 0; r < b.height; ++r) {
        const std::size_t li = static_cast<std::size_t>(firstRow + r);
        if (firstRow + r < 0 || li >= lines.size()) {
            continue;
        }
        const std::vector<Cell> row = alignLine(lines[li], width, align);
        for (int x = 0; x < b.width; ++x) {
            const int sx = firstCol + x;
            if (sx >= 0 && sx < static_cast<int>(row.size())) {
                b.at(x, r) = row[static_cast<std::size_t>(sx)];
            }
        }
        repairWidePairs(&b.at(0, r), b.width); // window/line-end clipping: a wide character cut in two is dropped
    }
    return b;
}

} // namespace toytui
