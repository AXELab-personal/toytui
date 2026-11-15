// param_types.h — shared helpers for parsing colour/int values. Several
// element types (core, label, box) need the same logic —
// this is the one place where it is implemented (shared,
// reusable helper functions, not per-element-type duplication).
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "param_store.h"

namespace toytui {

// Strict integer parse: the WHOLE string must be a valid integer,
// with no extra characters (e.g. "12x" -> nullopt).
std::optional<int> parseStrictInt(const std::string& s);

// Resolving a colour: first the 16 base colour names (cell.h), otherwise a
// numeric 0-255 index.
std::optional<std::uint8_t> parseColorValue(const std::string& s);

// Resolving a bool-like value alias: on/true/1 -> true, off/false/0
// -> false, otherwise nullopt. This is a VALUE-level alias (not a
// parameter-name alias, see cli_parser.h).
std::optional<bool> parseBoolValue(const std::string& s);

// Resolving the `attrib:` value — three forms:
//   1. binary: "0bxxxxxxxxxxxxxxxx" (MSB on the left), a shorter one is
//      accepted too (the missing upper bits count as 0).
//   2. hexadecimal: "0xefff", "0x12".
//   3. by name, as a comma-separated list: "bold, underline"
//      — only the named (bits 0-6) attributes; those not
//      listed count as 0 (off). VALUE-level
//      aliases (see cli_parser.h, NOT parameter-name
//      aliases): `ul`=underline, `strike`=strikethrough,
//      `reverse`=inverse — this table lives ONLY here, it is not mixed
//      with cli_parser's parameter-name alias table.
// The technical bits (13-15) are reachable with the binary/hex form (an
// unofficial, undocumented mode), but not with the
// name-list form.
std::optional<std::uint16_t> parseAttribValue(const std::string& s);

// Reading a STORED (already validated, canonical) integer/colour value — for the stored
// form of resolved parameters (e.g. `ElementTree::resolveInheritable`). Not validating: a
// corrupt stored value is an internal error (the input was
// already checked at `set`/`new`).
int storedInt(const std::string& stored);
std::uint8_t storedColor(const std::string& stored);

// Background/foreground colour pair (resolved, 0-255 index).
struct ColorPair {
    std::uint8_t bg;
    std::uint8_t fg;
};

// Applying a state colour pair (`<prefix>_bgcol`/`<prefix>_fgcol`, e.g. `focus`, `pressed`, `cursor`, `sel`) to the
// `under` appearance: both members unset -> with `swapWhenUnset` bg/fg are swapped, otherwise
// `under` is unchanged; one given -> the explicit one wins, the other stays that of `under`.
ColorPair applyStatePair(const ElementParams& p, const std::string& prefix, ColorPair under, bool swapWhenUnset);

// Reading an integer parameter from an element: nullopt if it is missing / Unset / not a valid integer
// (the stored value should always be valid; for a corrupt value this is NOT undefined behaviour
// but "no value"). The variant with a `fallback` returns that in this case.
std::optional<int> paramInt(const ElementParams& p, const std::string& name);
int paramInt(const ElementParams& p, const std::string& name, int fallback);

} // namespace toytui
