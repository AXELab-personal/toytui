// utf_escape.h — resolving the `\u[HEX]` input escape.
//
// A standalone module (not part of the ANSI layer): part of processing text input (`text:`, `text_add:`, `title:`),
// called by the parser from `unescapeValue`, in the same left-to-right, single-pass resolution.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace toytui {

// UTF-8 encoding of one code point (`cp` is valid: <= 0x10FFFF, not a surrogate).
std::string encodeUtf8(char32_t cp);

struct UnicodeEscape {
    std::string utf8;      // the UTF-8 bytes of the code point
    std::size_t consumed;  // number of bytes consumed (from `\u[` to `]`)
};

// Whether `s[pos..]` is a VALID `\u[HEX]` sequence: `\u[` + 2–8 hex digits + `]`, the value is 1..0x10FFFF and
// not a surrogate (U+D800..U+DFFF). Otherwise nullopt (the caller leaves the text literal).
std::optional<UnicodeEscape> decodeUnicodeEscape(std::string_view s, std::size_t pos);

// Processing file content (textarea `src:`): ONLY `\u[HEX]` is resolved; every other byte (the other
// backslashes too: `\n`, `\t`, `\\`) is unchanged. A literal `\u[HEX]` is written `\\u[HEX]` (only this
// pair resolves to `\u[HEX]` — a backslash is otherwise not an escape in the file). The result is never longer than the input.
std::string decodeUnicodeEscapesInFile(std::string_view content);

} // namespace toytui
