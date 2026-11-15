// list_value.h — shared splitting of list-valued parameters (e.g. select `options`).
//
// The CLI parser has already resolved the `\"`, `\'`, `\\`, `\n`, `\t` notation; the splitter is a SECOND
// escape layer: a `\` before the separator makes the separator a literal character (`\|` -> `|`),
// `\\` is a single `\` (so that a literal `\` before the separator can be expressed: `\\\\|` on the CLI);
// every other backslash is left unchanged (e.g. the ANSI layer's `\e[1m` notation).
// Empty input: empty list; otherwise `a||b` is three items (the middle one empty), a trailing separator yields an empty item.
#pragma once

#include <string>
#include <vector>

namespace toytui {

// `unicode`: `\u[HEX]` (utf_escape.h) is resolved too, in the same left-to-right pass (the resolved
// character is never a separator: `\u[7c]` is a literal `|` in the item); `\\u[HEX]` is a literal `\u[HEX]`.
std::vector<std::string> splitEscapedList(const std::string& raw, char separator, bool unicode = false);

} // namespace toytui
