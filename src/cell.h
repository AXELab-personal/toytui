// cell.h — the atomic unit of toytui rendering, plus the colour and
// attribute constants that go with it.
//
// IMPORTANT SCOPE NOTE: this file knows ONLY the primary attribute
// names (bold, underline, ...) and the 16 base colour names. CLI-side
// PARAMETER-NAME aliases (x/posX, w/width, ...) and attrib-VALUE aliases
// (ul/underline, strike/strikethrough, reverse/inverse) do NOT belong
// here — they live in the alias tables of other modules (param store,
// attrib value parser).
// This file only defines the "canonical" names
// and the bit field.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toytui {

// --- Cell: 8 bytes per cell ------------------------------------
//   character: 4 bytes of UTF-8, right-padded with zeros
//   bg, fg:    1 byte each, colour index 0-255 (the "unset" marker
//              lives in the attrib bitmask, see attrib_bits::BG_UNSET/FG_UNSET)
//   attrib:    2-byte (16-bit) bitmask, see attrib_bits
struct Cell {
    unsigned char ch[4]{0, 0, 0, 0};
    std::uint8_t bg = 0;
    std::uint8_t fg = 0;
    std::uint16_t attrib = 0;
};
static_assert(sizeof(Cell) == 8, "Cell must be exactly 8 bytes (spec 6.2)");

inline bool operator==(const Cell& a, const Cell& b) {
    return a.ch[0] == b.ch[0] && a.ch[1] == b.ch[1] && a.ch[2] == b.ch[2] &&
           a.ch[3] == b.ch[3] && a.bg == b.bg && a.fg == b.fg && a.attrib == b.attrib;
}
inline bool operator!=(const Cell& a, const Cell& b) { return !(a == b); }

// Rectangular cell array (row-major) — the finished visual buffer
// of an element, or the result of compositing.
struct Block {
    int width = 0;
    int height = 0;
    std::vector<Cell> cells; // width*height entries

    Cell& at(int x, int y) {
        return cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)];
    }
    const Cell& at(int x, int y) const {
        return cells[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)];
    }
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Combined extent of the children relative to the origin of the
// parent's content area, without offset (for the scroll bounds).
struct Extent {
    bool any = false;
    int x0 = 0, x1 = 0; // [x0, x1)
    int y0 = 0, y1 = 0;
};

// --- Attribute bitmask -----------------------------------------
// Character attributes from the LSB, technical flag bits from the MSB.
namespace attrib_bits {
constexpr std::uint16_t BOLD          = 1u << 0;
constexpr std::uint16_t UNDERLINE     = 1u << 1;
constexpr std::uint16_t STRIKETHROUGH = 1u << 2;
constexpr std::uint16_t BLINK         = 1u << 3;
constexpr std::uint16_t INVERSE       = 1u << 4;
constexpr std::uint16_t ITALIC        = 1u << 5;
constexpr std::uint16_t DIM           = 1u << 6;
// bits 7, 8-9: CURRENTLY UNUSED, free. Deliberately not
// pre-assigned: storage is 16 bits anyway, so a future cell-flag
// need can be allocated here "for free".
//
// WIDE_LEFT / WIDE_RIGHT (bits 10 and 11): CHARACTER-STRUCTURE flags, not
// style attributes. Code points above the BMP (U+10000..) occupy TWO cells
// (two columns): the left cell carries the whole (4-byte) UTF-8 character and
// WIDE_LEFT, the right cell has an empty character and WIDE_RIGHT (with the
// same style). The pair is INDIVISIBLE: clipping, wrapping, compositing and
// output must treat it as a unit; an orphan half cannot exist (`repairWidePairs`).
// These bits do NOT depend on ATTRIB_UNSET, and no SGR/inheritance step
// (resolveUnsetCells, `& 0x7F` masks) touches them.
//
// ATTRIB_UNSET (bit 12): meaningful only for the STYLE attributes (bits 0-6, ANSI SGR):
// "no own SGR attribute". It does not apply to the structure flags (bits 10-11).
// `attrib` IS inheritable too, just like the colours (bg/fg/width/height).
// It has a dual role, following exactly the BG_UNSET/FG_UNSET pattern:
//   - At ElementParams level: an element's OWN `attrib` parameter,
//     if unset, is inherited through the tree (ElementTree::resolveInheritable)
//     up to `core@terminal` (which therefore ALWAYS has a concrete,
//     Set `attrib`, like w/h/bg/fg).
//   - At cell level (ansi_handler): in a character's initial/reset
//     state this bit is Set — "no explicit attribute information for
//     this character, inherit the (already resolved) attrib of the
//     host element". As soon as an SGR code actually touches an
//     attribute bit, this flag is cleared (from then on the character's
//     attribute is ENTIRELY its own and is not mixed with the host's —
//     the same "overwrite, don't add" principle as for colours). This
//     also prepares the future plugin/CHR protocol: a plugin can state
//     per cell "no opinion on this attribute, inherit from the host".
constexpr std::uint16_t ATTRIB_UNSET  = 1u << 12;
constexpr std::uint16_t BG_UNSET      = 1u << 13;
constexpr std::uint16_t FG_UNSET      = 1u << 14;
// WIDE_CHAR (bit 15): CURRENTLY UNUSED. Wide characters are marked by bits 10-11
// (WIDE_LEFT/WIDE_RIGHT); this bit may be released conditionally (free or
// to be assigned once the width rule is finalised).
constexpr std::uint16_t WIDE_CHAR     = 1u << 15;
constexpr std::uint16_t WIDE_LEFT     = 1u << 10;
constexpr std::uint16_t WIDE_RIGHT    = 1u << 11;
// Bits belonging to the style (SGR 0-6 + the three UNSET flags): style equality looks at these.
constexpr std::uint16_t STYLE_MASK    = 0x7Fu | (1u << 12) | (1u << 13) | (1u << 14);
} // namespace attrib_bits

// Name and bit of a primary (canonical, non-alias) character attribute.
// The attrib:"name,name" CLI form is built on these names,
// but alias resolution (ul/strike/reverse -> here) is NOT the job
// of this file.
struct NamedAttrib {
    std::string_view name;
    std::uint16_t bit;
};
constexpr NamedAttrib kNamedAttribs[] = {
    {"bold", attrib_bits::BOLD},
    {"underline", attrib_bits::UNDERLINE},
    {"strikethrough", attrib_bits::STRIKETHROUGH},
    {"blink", attrib_bits::BLINK},
    {"inverse", attrib_bits::INVERSE},
    {"italic", attrib_bits::ITALIC},
    {"dim", attrib_bits::DIM},
};

// Canonical (primary) attribute name -> bit. Recognises only the
// documented (bits 0-6) names above; otherwise nullopt.
std::optional<std::uint16_t> namedAttribBit(std::string_view canonicalName);

// --- 256-colour palette alias table ----------------------------
// Case-sensitive names for the base colours 0-15. 16-231 (colour cube)
// and 232-255 (greyscale) are reachable by numeric index only and
// have no named alias.
std::optional<std::uint8_t> colorNameToIndex(std::string_view name);

// Write a UTF-8 character (max 4 bytes) into a cell, right-padded
// with zeros; longer input is truncated (the caller is responsible
// for passing valid, single-character input).
void setCellChar(Cell& cell, std::string_view utf8Char);

// UTF-8 string of a cell's character (empty cell -> space).
std::string getCellChar(const Cell& cell);

// Halves of a wide (two-cell) character (see attrib_bits::WIDE_LEFT/WIDE_RIGHT).
inline bool isWideLeft(const Cell& c) { return (c.attrib & attrib_bits::WIDE_LEFT) != 0; }
inline bool isWideRight(const Cell& c) { return (c.attrib & attrib_bits::WIDE_RIGHT) != 0; }

// Restore pair integrity of one cell row: every WIDE_LEFT must be directly
// followed by a WIDE_RIGHT, and every WIDE_RIGHT preceded by a WIDE_LEFT. An
// ORPHAN half becomes a space (the style is kept, the structure flags are
void repairWidePairs(Cell* row, int count);
void repairWidePairs(std::vector<Cell>& cells);

} // namespace toytui
