// input_pattern.h — the pattern language of the input field (`input`): parser and character tools.
//
// Pattern grammar:
//   pattern ::= ( literal | sgr | block )*
//   literal ::= any character except `{`; `}` outside a block is a literal; `\` escapes: `\\ \{ \} \%`
//   sgr     ::= `\e[...m` (or `\033[`, `\x1b[`, a real ESC) — display only, not part of the data
//   block   ::= '{' ( spec | '%h'phchar | '%'option':'value' )+ '}'   (0+ spaces between the items)
//   spec    ::= '%' type length | '%c[' set ']' length | '%C[' set ']' length
//   type    ::= s | S | u | i | f | x | X
//   length  ::= N | N- | -M | N-M      (N, M decimal; `0-` = any number, at most 512)
//   option  ::= csr | mode | adv       (block-level override; the value is checked by the element)
//
// The module does not know the screen: it only converts to text, checks and formats.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace toytui {

constexpr std::size_t kInputMaxChars = 512; // the whole field stores at most this many typed characters

enum class InputType { Str, StrUpper, Unsigned, Signed, Float, Hex, HexUpper, Custom, CustomUpper };

struct CharRange {
    char32_t lo;
    char32_t hi;
};

struct InputBlock {
    InputType type = InputType::Str;
    std::vector<CharRange> charset;       // Custom/CustomUpper only
    std::size_t minLen = 0;
    std::size_t maxLen = kInputMaxChars;  // kInputMaxChars for an unbounded block
    bool unlimited = true;                // the `N-` form (no upper bound): no placeholder padding
    std::string placeholder;              // a 1-cell-wide character (UTF-8); the pattern's default if there is no `%h`
    std::optional<std::string> csr;       // raw `%csr:` value (checked by the element)
    std::optional<std::string> mode;      // raw `%mode:` value
    std::optional<std::string> adv;       // raw `%adv:` value
};

struct PatternSegment {
    enum class Kind { Literal, Sgr, Block } kind = Kind::Literal;
    std::string text;       // Literal: the UTF-8 text of one cell (base + zero-width marks); Sgr: `ESC[...m`
    int width = 1;          // Literal: number of columns (1 or 2)
    std::size_t block = 0;  // Block: index into `Pattern::blocks`
};

struct Pattern {
    std::vector<PatternSegment> segments;
    std::vector<InputBlock> blocks;
};

// Parsing the pattern. `defaultPlaceholder`: the element's `placeholder` parameter (a 1-cell-wide character).
// On error nullopt and `err` filled in (in English, the language of CLI errors).
std::optional<Pattern> parsePattern(const std::string& text, const std::string& defaultPlaceholder,
                                    std::string& err);

// Character rules ------------------------------------------------------------------------------------

// Uppercasing (ASCII, Latin-1, Latin Extended-A, Greek, Cyrillic); other code points unchanged.
char32_t toUpperCodePoint(char32_t cp);

// Lowercasing (the counterpart of `toUpperCodePoint`).
char32_t toLowerCodePoint(char32_t cp);

// The typed code point converted according to the block's rule (uppercasing), or nullopt if it cannot be accepted
// at position `index` of the block text `text`. It does NOT check the length limit (the editor does).
std::optional<char32_t> acceptCodePoint(const InputBlock& b, const std::u32string& text, std::size_t index,
                                        char32_t cp);

// Whether the whole block text is valid (every character acceptable in turn, length within [minLen, maxLen]).
bool blockTextValid(const InputBlock& b, const std::u32string& text);

// UTF-8 <-> code point sequence (invalid byte: U+FFFD).
std::u32string toU32(const std::string& utf8);
std::string fromU32(const std::u32string& s);

} // namespace toytui
