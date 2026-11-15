// unicode_width.h — display width: which code points occupy TWO columns.
//
// An approximate, dependency-free table: the Unicode "East Asian Width" W/F categories and the emoji-presentation
// characters (colourful and wide by default), following the terminals used in practice (wcwidth). It does not handle
// zero-width and combining characters (documented limitation), and treats characters of ambiguous (A) width
// as narrow.
#pragma once

#include <string_view>

namespace toytui {

// Code point of a single character of valid UTF-8 (the caller has validated; 1–4 bytes).
char32_t decodeCodePoint(std::string_view validUtf8Char);

// true: the code point occupies two columns.
bool isWideCodePoint(char32_t cp);

// true: the code point occupies ZERO columns (combining mark, variation selector, zero-width format character:
// e.g. U+0301, Arabic harakat, U+FE0F, U+200D). `ansi_handler` merges these into the preceding character's cell
// (if it fits in the 4 bytes), otherwise drops them — so the cell count always equals the column count.
bool isZeroWidthCodePoint(char32_t cp);

} // namespace toytui
