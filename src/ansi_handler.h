// ansi_handler.h — two-way handling of ANSI/SGR escape sequences.
//
// A GENERAL module, INDEPENDENT of element type: it is needed not only for the `label`
// `text:` field; the future `plugin` and the multi-line internal
// element types also communicate through it "outwards" (shell-friendly ANSI), while
// "inwards" they conform to the cell model. THIS MODULE DOES NOT KNOW the
// calling element type and does NOT do line-semantic (single-/multi-line)
// processing — it returns newline/tab characters as unchanged cells.
// The single-line "text cleaning" (tab/newline ->
// space) is SEPARATE, the responsibility of the calling element (e.g. label), see
// `cleanSingleLine` below — it is deliberately not automatic here.
#pragma once

#include <string>
#include <vector>

#include "cell.h"

namespace toytui {

// Raw (escape-containing) text -> cell sequence. The initial state of every
// character: bg/fg UNSET (bitmask marker — "the host element's own colour
// is to be used until an SGR overrides it"), attrib 0. The effect of a given SGR stays in
// force for every following character, until a newer sequence overrides it,
// or `\e[0m` (or `\033[0m`/`\x1b[0m`/`\x1B[0m` — either as a REAL ESC
// byte or as a literally typed backslash sequence)
// restores the base state. Unknown/unhandled (not
// SGR, i.e. not ending in 'm') CSI sequences are dropped from the output
// without effect.
//
// Handled SGR codes: 0 (reset), 1/2/3/4/5/7/9 (bold/dim/italic/
// underline/blink/inverse/strikethrough on), 22/23/24/25/27/29
// (the same off), 30-37/90-97 (normal/bright fg), 40-47/100-107
// (normal/bright bg), 38;5;N/48;5;N (256-colour fg/bg), 39/49 (default
// fg/bg back). The parameters of `38;2;r;g;b`/`48;2;r;g;b` (truecolor) are
// consumed (so as not to confuse the following codes) and approximated to the
// nearest 256-palette colour.
std::vector<Cell> parseAnsiText(const std::string& raw);

// Protected (literal) introducer: whether an `e[`, `033[`, `x1b[` or `x1B[` body follows a backslash sequence of length L >= 1
// (i.e. a `\e[` ... form). Returns L if so, otherwise 0. `parseAnsiText` turns the sequence's
// PAIRS into literal `\` (the body stays text), the odd remaining backslash is the introducer; the CLI parser
// (`unescapeValue`) therefore leaves such a sequence unchanged (otherwise the protection would be lost), and
// `encodeAnsiText` doubles the literal backslashes (so the view can be fed back in).
std::size_t backslashRunBeforeIntroducerBody(const std::string& s, std::size_t pos);

// Text cleaning for single-line elements: the cells of tab/newline/other
// line-break-related control characters (ASCII \t, \n, \r, \v, \f)
// are replaced with a space (the cell's COLOUR/attribute is kept).
// To be called AFTER `parseAnsiText`; the caller (e.g. label) decides whether
// it is single-line — it does NOT run automatically in `parseAnsiText`.
void cleanSingleLine(std::vector<Cell>& cells);

// The PROCESSED view of single-line elements (label `text`, box `title`) from the raw
// value: SGR processing -> single-line cleaning -> re-encoding to ANSI.
// This is the output of `get text` / `get title`.
std::string processedSingleLine(const std::string& raw);

// Filtering control characters that disturb the terminal (REMOVING the cells):
// C0 (U+0000–U+001F), DEL (U+007F) and C1 (U+0080–U+009F; e.g. U+009B = CSI).
// `keepLineAndTab == true`: `\n` and `\t` are kept (multi-line elements).
// ESC sequences have already been processed by `parseAnsiText`; a lone ESC also
// counts as a control character. `cleanSingleLine` does this too.
void dropControlChars(std::vector<Cell>& cells, bool keepLineAndTab = false);

// The reverse direction: cell sequence -> compact, shell-friendly
// ANSI string (`get` replies, the future `CHR` plugin protocol).
// For UNSET (bg/fg bitmask-marked) cells it writes NO colour code — it writes
// SGR only where the cell actually overrides the host element's
// colour/attribute. At transitions between runs it writes
// `\e[0m` only when actually necessary (entering or leaving a style).
std::string encodeAnsiText(const std::vector<Cell>& cells);

// The SGR codes belonging to one cell's style (e.g. {"1","38;5;9"}) —
// the COMMON basis of the renderer writing to the terminal (`renderer.cpp`/`diff.cpp`) and
// `encodeAnsiText`, so that the actual output and the `get`/
// future `CHR` reply are NOT two separate, diverging implementations
// (with two implementations the attributes — bold/underline/etc. — would
// not appear on the terminal, only the colours). An empty list if the cell has no
// style differing from the host element's default.
std::vector<std::string> sgrCodesForCell(const Cell& c);

// STYLE equality of two cells (attrib, bg, fg — the character does not matter).
// A COMMON helper of the renderer, the diff and `encodeAnsiText`.
bool sameCellStyle(const Cell& a, const Cell& b);

// The SGR codes joined with `;` (e.g. {"1","38;5;9"} -> "1;38;5;9").
std::string joinSgrCodes(const std::vector<std::string>& codes);

// Setting a cell's style AFTER a `\e[0m` reset: `\e[0m` (if the
// cell has no code of its own) or `\e[0m\e[<codes>m`. The common SGR prefix of the full repaint,
// the diff and `encodeAnsiText`.
std::string sgrResetAndSet(const Cell& c);

// Resolving the UNSET-marked (bg/fg/attrib) cells with the element's already resolved
// colours/attribute (the shared step of label, textarea): an UNSET fg/bg
// takes the given colour; if the cell has no OWN (SGR-derived)
// attribute, the element-level attrib OVERWRITES (does not add to) it — "inherit
// OR overwrite, never mix". `inheritedAttr` is bits 0–6 (0x7F).
void resolveUnsetCells(std::vector<Cell>& cells, std::uint8_t bg, std::uint8_t fg,
                       std::uint16_t inheritedAttr);

// xterm 256-colour approximation (6x6x6 cube + grey ramp) of a true-colour value (components 0-255).
std::uint8_t approximateTrueColor(int r, int g, int b);

// The "neutral" cell: a space, bg/fg/attrib ALL UNSET (inherits the host
// element, see cell.h ATTRIB_UNSET). The initial state of the ANSI parser AND the
// label's padding cell — a single definition so that the two
// cannot diverge.
Cell neutralBlankCell();

} // namespace toytui
