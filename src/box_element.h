// box_element.h — the `box` element type.
//
// Offset bounds (min/maxOffset, overscroll), the scrollbar thumb +
// direction markers — see below. Parameters (new/set/get, with validation), content-area
// computation, the box's OWN (childless) visual frame: background +
// border (1/2 lines) + title (left/center/right, with shared text clipping),
// and colour resolution through the tree (the scrollbar falls
// back to the border colours).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cell.h"
#include "cli_parser.h"
#include "element_registry.h"
#include "element_tree.h"
#include "param_store.h"

namespace toytui {

// Defaults: position "0", z_index "1", border "off",
// titlePos "left", scrollbarX/Y "off", overscroll "off", offsetX/Y "0"
// — these are REAL (non-inherited) defaults, hence in Set state.
// width/height/bgcol/fgcol, border_*/scroll_* colours, title, min/max
// offset: Unset.
ElementParams makeDefaultBoxParams();

// `id` is mandatory. `parent` (if not given: "core@terminal") goes into the
// stored parameters; its existence/acyclicity is checked by the caller
// (tree), only its format (`type@id`) is checked here.
ElementParams boxNew(const std::vector<ParamToken>& params);
void boxSet(ElementParams& box, const std::vector<ParamToken>& params);

// Gives the STORED level (unset -> the literal "auto"), see label.
std::vector<std::string> boxGet(const ElementParams& box,
                                 const std::vector<ParamToken>& params);

struct ContentArea {
    int originX = 0; // relative to the box's own (0,0)
    int originY = 0;
    int width = 0;
    int height = 0;
};
// box size - border (1 per side if border 1/2) - active
// scrollbars (1 per direction: Y on the RIGHT edge inside the border, X above the
// bottom border, inside). Never negative.
ContentArea computeContentArea(int boxWidth, int boxHeight, bool border,
                                bool scrollX, bool scrollY);

struct BoxColors {
    std::uint8_t bg = 0, fg = 0;
    std::uint8_t borderFg = 0, borderBg = 0;
    std::uint8_t scrollFg = 0, scrollBg = 0;
};
// Full colour resolution through the tree: bg/fg (inheritance chain), border_* (component
// -> own element -> tree), scroll_* (component -> the box's BORDER colour, which
// itself follows the previous chain).
BoxColors resolveBoxColors(const ElementTree& tree,
                            const std::string& boxFullId);

// The box's own visual frame (width*height, row-major). `borderMode`:
// 0 off, 1 single line, 2 double line. `titlePos`: "left"/"center"/
// "right". The title goes onto the top border as `[text]`; if it does not
// fit, the END of the TEXT is clipped (the brackets stay); if not even
// `[]` fits, there is no title.
//
// `title` is RAW (it may contain escapes) — it is processed by
// `ansi_handler` (uniform with the label's
// `text:`). For plain (escape-free) text the output is identical
// byte for byte to plain-string handling.
std::vector<Cell> renderBoxFrame(int width, int height, int borderMode,
                                  const std::string& title,
                                  const std::string& titlePos, bool scrollX,
                                  bool scrollY, const BoxColors& colors);

// --- Scroll --------------------------------------------------

// The permitted range [lo, hi] of the offset in one direction.
struct OffsetRange {
    int lo = 0;
    int hi = 0;
};

// The bound computed from the children's extent ([extMin, extMax), relative to
// the content origin, without offset) and from the content area
// size (`areaSize`). [0,0] without children. 0 (no offset) is
// ALWAYS part of the range — this only limits how far one can
// DEPART from the neutral state (see .cpp: a single child that on its own already sticks out of the content area
// must NOT shift the others).
//  - overscroll off: the child cannot be pushed out completely: its left/top edge
//    can reach at most the content's left/top edge, its right/bottom edge at least the
//    content's right/bottom edge (for a larger child); for a smaller
//    child it stays within the content area
//    -> [min(0, area-extMax, -extMin), max(0, ...)].
//  - overscroll on: can be pushed out until it has completely left the
//    area (but no further) -> [min(0,-extMax), max(0,area-extMin)].
// An explicit min/maxOffset OVERRIDES the corresponding computed bound; if
// that would make lo > hi, hi := lo (deterministic).
OffsetRange computeOffsetRange(int areaSize, bool anyChild, int extMin,
                                int extMax, bool overscroll,
                                std::optional<int> explicitMin,
                                std::optional<int> explicitMax);

int clampOffset(int offset, const OffsetRange& range);

struct BoxScroll {
    int offsetX = 0; // the stored offset clamped to the range (EFFECTIVE)
    int offsetY = 0;
    OffsetRange rangeX;
    OffsetRange rangeY;
};
// The stored `offsetX/Y` stays untouched (`get` returns it),
// compositing uses the value clamped to the range — the
// range depends on the state of the tree, so `set` does not reject it.
BoxScroll resolveBoxScroll(const ElementParams& box, const Rect& contentArea,
                            const Extent& childExtent);

// --- Key navigation (the framework leaves key interpretation to the element,
// this is the box's default) ---
//
// `offsetX/Y` is the shift of the CONTENT: increasing it moves the
// content right/down, i.e. the VIEW moves left/up (e.g. `offsX:-2` -> the
// content's 3rd column lands on the left edge). Hence:
//   [UP]    offsetY += 1        [DOWN]  offsetY -= 1
//   [LEFT]  offsetX += 1        [RIGHT] offsetX -= 1
//   [PGUP]/[PGDN]   offsetY += / -= the height of the content area
//   [HOME]/[END]    offsetY := hi (start) / lo (end)
//   [SHIFT-PGUP]/[SHIFT-PGDN]/[SHIFT-HOME]/[SHIFT-END]: the same on the X
//     axis (with the width of the content area).
// Every movement stays within the RANGE (min/maxOffset, overscroll, the children's
// extent), starting from the EFFECTIVE (not the raw stored) value —
// so there are no "dead" keypresses beyond the bound.
// Any other key (and a modified arrow): not part of navigation.
//
// Returns true if the key was a navigation key; then
// `newX/newY` is the desired new (range-clamped) offset — the caller
// decides whether anything changed.
bool navigateByKey(const std::string& key, const BoxScroll& cur, int areaWidth,
                   int areaHeight, int& newX, int& newY);

// The scrollbar track in the box's own coordinates.
// Y on the RIGHT edge inside the border (the usual
// terminal UI convention), X above the bottom border, inside.
struct ScrollTrack {
    int x = 0;
    int y = 0;
    int len = 0;
};
ScrollTrack scrollTrackX(int boxWidth, int boxHeight, bool border, bool scrollY);
ScrollTrack scrollTrackY(int boxWidth, int boxHeight, bool border, bool scrollX);

// The characters of the track, per cell: the line, a fixed 1-cell thumb `■`,
// and — where the offset can still move in that direction — a direction
// triangle at the end of the line (◀ ▶ / ▲ ▼). `explicitPos` (scrollbar*_pos):
// 1-based cell index, 0 = automatic (from the offset's proportion
// within the range). The direction marker does not cover the thumb.
std::vector<std::string> scrollbarGlyphs(bool vertical, int trackLen, int offset,
                                          const OffsetRange& range, int explicitPos);

// Painting the thumb + direction markers onto the finished (children-composited) box
// buffer.
void paintScrollbars(Block& out, const ElementParams& box, const BoxColors& colors,
                      const BoxScroll& scroll);

// Hooking `box` into the registry: all the pieces above (parameters,
// content area, frame + scrollbar track, offset resolution,
// thumb painting) are joined here into a single `ElementKind`.
ElementKind boxElementKind();

} // namespace toytui
