#include "box_element.h"

#include <algorithm>
#include <utility>

#include "ansi_handler.h"
#include "compositor.h"
#include "param_spec.h"
#include "param_types.h"

namespace toytui {

namespace {

// The box's own parameters; the general ones come from kGeneralSpecs.
const SpecTable& boxSpecs() {
    static const SpecTable t = concatSpecs(
        kGeneralSpecs,
        {
            {"border", Kind::Border, false, "off"},
            {"border_fgcol", Kind::Color, true, nullptr},
            {"border_bgcol", Kind::Color, true, nullptr},
            {"title", Kind::TextView, false, nullptr, 0, "title_raw", processedSingleLine}, // derived view (not stored)
            {"title_raw", Kind::Text, false, nullptr}, // the stored raw value (same pattern as the label's text_raw)
            {"titlePos", Kind::TitlePos, false, "left"},
            {"scrollbarX", Kind::Bool, false, "off"},
            {"scrollbarY", Kind::Bool, false, "off"},
            {"scrollbarX_pos", Kind::Size, true, nullptr},
            {"scrollbarY_pos", Kind::Size, true, nullptr},
            {"scroll_fgcol", Kind::Color, true, nullptr},
            {"scroll_bgcol", Kind::Color, true, nullptr},
            {"offsetX", Kind::Signed, false, "0"},
            {"offsetY", Kind::Signed, false, "0"},
            {"minOffsetX", Kind::Signed, true, nullptr},
            {"minOffsetY", Kind::Signed, true, nullptr},
            {"maxOffsetX", Kind::Signed, true, nullptr},
            {"maxOffsetY", Kind::Signed, true, nullptr},
            {"overscroll", Kind::Bool, false, "off"},
        });
    return t;
}

} // namespace

ElementParams makeDefaultBoxParams() {
    ElementParams p;
    p.set("parent", "core@terminal");
    p.set("posX", "0");
    p.set("posY", "0");
    p.set("z_index", "1");
    p.set("border", "off");
    p.set("titlePos", "left");
    p.set("scrollbarX", "off");
    p.set("scrollbarY", "off");
    p.set("offsetX", "0");
    p.set("offsetY", "0");
    p.set("overscroll", "off");
    return p;
}

namespace {

} // namespace

ElementParams boxNew(const std::vector<ParamToken>& params) {
    ElementParams box = makeDefaultBoxParams();
    applyParams(box, params, boxSpecs(), true, elementTarget("box", params));
    return box;
}

void boxSet(ElementParams& box, const std::vector<ParamToken>& params) {
    applyParams(box, params, boxSpecs(), false, elementTarget("box", box));
}

std::vector<std::string> boxGet(const ElementParams& box,
                                 const std::vector<ParamToken>& params) {
    return getParams(box, params, boxSpecs(), elementTarget("box", box));
}

ContentArea computeContentArea(int boxWidth, int boxHeight, bool border,
                                bool scrollX, bool scrollY) {
    const int b = border ? 1 : 0;
    ContentArea a;
    a.originX = b; // the Y scrollbar is on the RIGHT edge (it does not shift the origin)
    a.originY = b;
    a.width = std::max(0, boxWidth - 2 * b - (scrollY ? 1 : 0));
    a.height = std::max(0, boxHeight - 2 * b - (scrollX ? 1 : 0));
    return a;
}

BoxColors resolveBoxColors(const ElementTree& tree,
                            const std::string& boxFullId) {
    BoxColors c;
    c.bg = tree.resolveInheritableColor(boxFullId, "bgcol");
    c.fg = tree.resolveInheritableColor(boxFullId, "fgcol");
    c.borderFg = storedColor(tree.resolveComponentColor(boxFullId, "border_fgcol", "fgcol"));
    c.borderBg = storedColor(tree.resolveComponentColor(boxFullId, "border_bgcol", "bgcol"));

    // scrollbar: component -> the box's (already resolved) BORDER colour.
    const ElementNode* node = tree.find(boxFullId);
    const std::string* sf = node->params.getSet("scroll_fgcol");
    const std::string* sb = node->params.getSet("scroll_bgcol");
    c.scrollFg = sf != nullptr ? storedColor(*sf) : c.borderFg;
    c.scrollBg = sb != nullptr ? storedColor(*sb) : c.borderBg;
    return c;
}

std::vector<Cell> renderBoxFrame(int width, int height, int borderMode,
                                  const std::string& title,
                                  const std::string& titlePos, bool scrollX,
                                  bool scrollY, const BoxColors& colors) {
    if (width <= 0 || height <= 0) {
        return {};
    }
    std::vector<Cell> canvas(static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height));
    // The cells of the background/border/scrollbar track are all marked
    // ATTRIB_UNSET ("no own attribute, inherit the box's") — the
    // actual resolution happens in boxElementKind(), after the tree-level
    // attrib has been resolved. The title's cells are exceptions (see
    // below): they carry on their OWN (SGR-derived)
    // ATTRIB_UNSET state.
    auto paint = [&](int x, int y, std::string_view ch, std::uint8_t fg,
                     std::uint8_t bg) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        Cell& c = canvas[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)];
        setCellChar(c, ch);
        c.fg = fg;
        c.bg = bg;
        c.attrib = attrib_bits::ATTRIB_UNSET;
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            paint(x, y, " ", colors.fg, colors.bg);
        }
    }

    const bool border = borderMode != 0 && width >= 2 && height >= 2;
    if (border) {
        const bool dbl = borderMode == 2;
        const char* h = dbl ? "═" : "─";
        const char* v = dbl ? "║" : "│";
        auto bp = [&](int x, int y, const char* ch) {
            paint(x, y, ch, colors.borderFg, colors.borderBg);
        };
        for (int x = 1; x < width - 1; ++x) {
            bp(x, 0, h);
            bp(x, height - 1, h);
        }
        for (int y = 1; y < height - 1; ++y) {
            bp(0, y, v);
            bp(width - 1, y, v);
        }
        bp(0, 0, dbl ? "╔" : "┌");
        bp(width - 1, 0, dbl ? "╗" : "┐");
        bp(0, height - 1, dbl ? "╚" : "└");
        bp(width - 1, height - 1, dbl ? "╝" : "┘");

        // title: `[text]` on the top border, between the corners. `title`
        // is RAW (it may contain escapes) — split into cells
        // with the ansi handler, keeping the per-character
        // style of the text part; the brackets are always plain cells
        // in the border colour.
        // INTENTIONALLY not the shared `fitCellsLeft`: there is no padding here, and because of the brackets 2 cells
        // are subtracted from the border width (`fitCellsLeft` and this are two separate rules).
        const int inner = width - 2;
        std::vector<Cell> titleCells = parseAnsiText(title);
        cleanSingleLine(titleCells);
        if (!titleCells.empty() && inner >= 2) {
            const auto budget = static_cast<std::size_t>(inner - 2);
            if (titleCells.size() > budget) {
                titleCells.resize(budget); // only the text is clipped, the brackets stay
                repairWidePairs(titleCells); // a wide character cut in two at the clip edge is dropped
            }
            const int len = static_cast<int>(titleCells.size()) + 2; // + "[" "]"
            int start = 0; // left
            if (titlePos == "center") {
                start = (inner - len) / 2;
            } else if (titlePos == "right") {
                start = inner - len;
            }
            paint(1 + start, 0, "[", colors.borderFg, colors.borderBg);
            paint(1 + start + len - 1, 0, "]", colors.borderFg, colors.borderBg);
            for (std::size_t i = 0; i < titleCells.size(); ++i) {
                Cell c = titleCells[i];
                if (c.attrib & attrib_bits::FG_UNSET) {
                    c.fg = colors.borderFg;
                }
                if (c.attrib & attrib_bits::BG_UNSET) {
                    c.bg = colors.borderBg;
                }
                c.attrib = static_cast<std::uint16_t>(
                    c.attrib & ~(attrib_bits::FG_UNSET | attrib_bits::BG_UNSET));
                const int x = 1 + start + 1 + static_cast<int>(i);
                if (x >= 0 && x < width) {
                    canvas[static_cast<std::size_t>(x)] = c; // y=0, the row of the top border
                }
            }
        }
    }

    // Scrollbar track (the thumb/arrows are painted on by
    // paintScrollbars after compositing).
    if (scrollY) {
        const ScrollTrack t = scrollTrackY(width, height, border, scrollX);
        for (int i = 0; i < t.len; ++i) {
            paint(t.x, t.y + i, "│", colors.scrollFg, colors.scrollBg);
        }
    }
    if (scrollX) {
        const ScrollTrack t = scrollTrackX(width, height, border, scrollY);
        for (int i = 0; i < t.len; ++i) {
            paint(t.x + i, t.y, "─", colors.scrollFg, colors.scrollBg);
        }
    }
    return canvas;
}

// --- Scroll ---------------------------------------------------------------

OffsetRange computeOffsetRange(int areaSize, bool anyChild, int extMin, int extMax,
                                bool overscroll, std::optional<int> explicitMin,
                                std::optional<int> explicitMax) {
    OffsetRange r;
    if (anyChild) {
        int lo = 0;
        int hi = 0;
        if (overscroll) {
            lo = -extMax;
            hi = areaSize - extMin;
        } else {
            const int a = areaSize - extMax;
            const int b = -extMin;
            lo = std::min(a, b);
            hi = std::max(a, b);
        }
        // The NEUTRAL (0, "no offset") position is ALWAYS allowed —
        // the computed range only limits how far one can
        // DEPART from it. Otherwise,
        // if a child's position already lay outside the
        // content area on its own (e.g. `posX:-1`), 0 would drop out of the [lo,hi]
        // range and `clampOffset(0, ...)` would SHIFT the whole
        // box content — EVERY child, not just the protruding one. Each
        // child's own (possibly negative) position on its own
        // only clips/pushes out THAT child at the edge of the content area — the `offsetX/Y`
        // parameter, which is a SEPARATE, explicit shift of the whole
        // content, must not (and need not) be touched for that.
        r.lo = std::min(0, lo);
        r.hi = std::max(0, hi);
    }
    if (explicitMin) {
        r.lo = *explicitMin;
    }
    if (explicitMax) {
        r.hi = *explicitMax;
    }
    r.hi = std::max(r.hi, r.lo);
    return r;
}

int clampOffset(int offset, const OffsetRange& range) {
    return std::clamp(offset, range.lo, range.hi);
}

namespace {

} // namespace

BoxScroll resolveBoxScroll(const ElementParams& box, const Rect& area, const Extent& ext) {
    const bool over = box.isOn("overscroll");
    BoxScroll s;
    s.rangeX = computeOffsetRange(area.width, ext.any, ext.x0, ext.x1, over,
                                   paramInt(box, "minOffsetX"), paramInt(box, "maxOffsetX"));
    s.rangeY = computeOffsetRange(area.height, ext.any, ext.y0, ext.y1, over,
                                   paramInt(box, "minOffsetY"), paramInt(box, "maxOffsetY"));
    s.offsetX = clampOffset(paramInt(box, "offsetX").value_or(0), s.rangeX);
    s.offsetY = clampOffset(paramInt(box, "offsetY").value_or(0), s.rangeY);
    return s;
}

bool navigateByKey(const std::string& key, const BoxScroll& cur, int areaWidth,
                   int areaHeight, int& newX, int& newY) {
    newX = cur.offsetX;
    newY = cur.offsetY;
    const int pageX = std::max(1, areaWidth);
    const int pageY = std::max(1, areaHeight);
    if (key == "[UP]") {
        newY = cur.offsetY + 1;
    } else if (key == "[DOWN]") {
        newY = cur.offsetY - 1;
    } else if (key == "[LEFT]") {
        newX = cur.offsetX + 1;
    } else if (key == "[RIGHT]") {
        newX = cur.offsetX - 1;
    } else if (key == "[PGUP]") {
        newY = cur.offsetY + pageY;
    } else if (key == "[PGDN]") {
        newY = cur.offsetY - pageY;
    } else if (key == "[HOME]") {
        newY = cur.rangeY.hi;
    } else if (key == "[END]") {
        newY = cur.rangeY.lo;
    } else if (key == "[SHIFT-PGUP]") {
        newX = cur.offsetX + pageX;
    } else if (key == "[SHIFT-PGDN]") {
        newX = cur.offsetX - pageX;
    } else if (key == "[SHIFT-HOME]") {
        newX = cur.rangeX.hi;
    } else if (key == "[SHIFT-END]") {
        newX = cur.rangeX.lo;
    } else {
        return false;
    }
    newX = clampOffset(newX, cur.rangeX);
    newY = clampOffset(newY, cur.rangeY);
    return true;
}

ScrollTrack scrollTrackX(int w, int h, bool border, bool scrollY) {
    const int b = border ? 1 : 0;
    ScrollTrack t;
    t.x = b; // the Y scrollbar is on the RIGHT edge, it does not shift the X bar
    t.y = h - 1 - b;
    t.len = t.y >= b ? std::max(0, (w - 1 - b - (scrollY ? 1 : 0)) - t.x + 1) : 0;
    return t;
}

ScrollTrack scrollTrackY(int w, int h, bool border, bool scrollX) {
    const int b = border ? 1 : 0;
    ScrollTrack t;
    t.x = w - 1 - b; // right edge, inside the border
    t.y = b;
    t.len = t.x >= b ? std::max(0, (h - 1 - b - (scrollX ? 1 : 0)) - t.y + 1) : 0;
    return t;
}

std::vector<std::string> scrollbarGlyphs(bool vertical, int trackLen, int offset,
                                          const OffsetRange& r, int explicitPos) {
    if (trackLen <= 0) {
        return {};
    }
    std::vector<std::string> g(static_cast<std::size_t>(trackLen), vertical ? "│" : "─");
    int idx = 0;
    if (explicitPos > 0) {
        idx = std::min(explicitPos, trackLen) - 1;
    } else if (r.hi > r.lo) {
        // How far through the range the view is: hi (start) -> 0.
        const int span = r.hi - r.lo;
        idx = std::clamp(((r.hi - offset) * (trackLen - 1) + span / 2) / span, 0, trackLen - 1);
    }
    g[static_cast<std::size_t>(idx)] = "■";
    if (trackLen >= 2) {
        if (offset < r.hi && idx != 0) {
            g.front() = vertical ? "▲" : "◀";
        }
        if (offset > r.lo && idx != trackLen - 1) {
            g.back() = vertical ? "▼" : "▶";
        }
    }
    return g;
}

void paintScrollbars(Block& out, const ElementParams& box, const BoxColors& colors,
                      const BoxScroll& sc) {
    const std::string* bv = box.getSet("border");
    const bool border = bv != nullptr && *bv != "off" &&
                        out.width >= 2 && out.height >= 2;
    const bool sx = box.isOn("scrollbarX");
    const bool sy = box.isOn("scrollbarY");

    auto put = [&](int x, int y, const std::string& glyph) {
        if (x < 0 || y < 0 || x >= out.width || y >= out.height) {
            return;
        }
        Cell& c = out.at(x, y);
        setCellChar(c, glyph);
        c.fg = colors.scrollFg;
        c.bg = colors.scrollBg;
    };
    if (sy) {
        const ScrollTrack t = scrollTrackY(out.width, out.height, border, sx);
        const auto g = scrollbarGlyphs(true, t.len, sc.offsetY, sc.rangeY,
                                       paramInt(box, "scrollbarY_pos").value_or(0));
        for (int i = 0; i < t.len; ++i) {
            put(t.x, t.y + i, g[static_cast<std::size_t>(i)]);
        }
    }
    if (sx) {
        const ScrollTrack t = scrollTrackX(out.width, out.height, border, sy);
        const auto g = scrollbarGlyphs(false, t.len, sc.offsetX, sc.rangeX,
                                       paramInt(box, "scrollbarX_pos").value_or(0));
        for (int i = 0; i < t.len; ++i) {
            put(t.x + i, t.y, g[static_cast<std::size_t>(i)]);
        }
    }
}

namespace {

std::string ownParam(const ElementTree& t, const std::string& id, const char* name,
                      const std::string& fallback = "") {
    return t.find(id)->params.text(name, fallback);
}

} // namespace

ElementKind boxElementKind() {
    ElementKind k;
    k.ownVisual = [](const ElementTree& t, const std::string& id) {
        Block b;
        b.width = t.resolveInheritableInt(id, "width");
        b.height = t.resolveInheritableInt(id, "height");
        const std::string border = ownParam(t, id, "border", "off");
        // `title_raw` (if present) carries any embedded SGR
        // (uniform with the label's `text:`) — `renderBoxFrame` processes it (for a plain
        // title the result is the same byte for byte).
        const std::string titleRaw = ownParam(t, id, "title_raw");
        b.cells = renderBoxFrame(b.width, b.height, border == "off" ? 0 : std::stoi(border),
                                  titleRaw, ownParam(t, id, "titlePos", "left"),
                                  ownParam(t, id, "scrollbarX") == "on",
                                  ownParam(t, id, "scrollbarY") == "on", resolveBoxColors(t, id));
        // `attrib` is inheritable (see cell.h
        // ATTRIB_UNSET) — following the colour pattern: it overwrites only those cells
        // that carry NO attribute of their own (from SGR in the title)
        // (marked ATTRIB_UNSET); the rest (background/border/
        // scrollbar track) are all like that, see `renderBoxFrame::paint`.
        const auto attr = static_cast<std::uint16_t>(t.resolveInheritableInt(id, "attrib") & 0x7F);
        for (auto& c : b.cells) {
            if (c.attrib & attrib_bits::ATTRIB_UNSET) {
                c.attrib = static_cast<std::uint16_t>(
                    (c.attrib & ~(attrib_bits::ATTRIB_UNSET | 0x7Fu)) | attr);
            }
        }
        return b;
    };
    k.contentArea = [](const ElementTree& t, const std::string& id, const Block& own) {
        // `own` may be zero-size (hostsChildren) — the computation
        // still makes sense, only the sizes are 0.
        const ContentArea a = computeContentArea(
            own.width, own.height, ownParam(t, id, "border", "off") != "off",
            ownParam(t, id, "scrollbarX") == "on", ownParam(t, id, "scrollbarY") == "on");
        return std::optional<Rect>(Rect{a.originX, a.originY, a.width, a.height});
    };
    k.contentWidth = [](const ElementTree& t, const std::string& id) {
        return std::optional<int>(computeContentArea(
                                      t.resolveInheritableInt(id, "width"),
                                      t.resolveInheritableInt(id, "height"),
                                      ownParam(t, id, "border", "off") != "off",
                                      ownParam(t, id, "scrollbarX") == "on",
                                      ownParam(t, id, "scrollbarY") == "on")
                                      .width);
    };
    k.contentHeight = [](const ElementTree& t, const std::string& id) {
        return std::optional<int>(computeContentArea(
                                      t.resolveInheritableInt(id, "width"),
                                      t.resolveInheritableInt(id, "height"),
                                      ownParam(t, id, "border", "off") != "off",
                                      ownParam(t, id, "scrollbarX") == "on",
                                      ownParam(t, id, "scrollbarY") == "on")
                                      .height);
    };
    k.create = [](const ParsedCommand& c) { return boxNew(c.params); };
    k.set = [](ElementParams& p, const ParsedCommand& c) { boxSet(p, c.params); };
    k.get = [](const ElementParams& p, const ParsedCommand& c) { return boxGet(p, c.params); };
    k.scroll = [](const ElementTree& t, const std::string& id, const Rect& area, const Extent& ext) {
        const BoxScroll s = resolveBoxScroll(t.find(id)->params, area, ext);
        return std::make_pair(s.offsetX, s.offsetY);
    };
    k.decorate = [](const ElementTree& t, const std::string& id, Block& out, const Rect& area,
                    const Extent& ext) {
        const ElementParams& p = t.find(id)->params;
        paintScrollbars(out, p, resolveBoxColors(t, id), resolveBoxScroll(p, area, ext));
    };
    k.onKey = [](ElementTree& t, const ElementRegistry& reg, const std::string& id,
                 const std::string& key) {
        const ContentArea a = computeContentArea(
            t.resolveInheritableInt(id, "width"), t.resolveInheritableInt(id, "height"),
            ownParam(t, id, "border", "off") != "off", ownParam(t, id, "scrollbarX") == "on",
            ownParam(t, id, "scrollbarY") == "on");
        const BoxScroll cur = resolveBoxScroll(t.find(id)->params, Rect{a.originX, a.originY, a.width, a.height},
                                               childExtent(t, reg, id));
        int nx = 0;
        int ny = 0;
        if (!navigateByKey(key, cur, a.width, a.height, nx, ny)) {
            return false; // not a navigation key: dropped
        }
        ElementParams& p = t.findMut(id)->params;
        bool changed = false;
        if (nx != paramInt(p, "offsetX").value_or(0)) {
            p.set("offsetX", std::to_string(nx));
            changed = true;
        }
        if (ny != paramInt(p, "offsetY").value_or(0)) {
            p.set("offsetY", std::to_string(ny));
            changed = true;
        }
        return changed;
    };
    k.paramSpecs = boxSpecs();
    return k;
}

} // namespace toytui
