// file_source.h — filling a text buffer from a file (textarea `src`).
//
// REGULAR files only (S_ISREG; including files under /proc that report size 0). The file's
// content is NOT inspected (strict UTF-8 applies at display time).
// Opening does not block (O_NONBLOCK: not even a FIFO without a writer can hang it), the type
// is decided AFTER opening, with `fstat` (no check-then-use window), and reading goes
// at most up to `maxBytes + 1` bytes (a growing or falsely sized file cannot slip through either).
#pragma once

#include <cstddef>
#include <string>

namespace toytui {

// Success: true, `out` = the whole file content. Failure: false, `why` = human-readable reason
// (e.g. "no such file or directory", "permission denied", "is a directory",
// "not a regular file", "too large (900000 > 524288 bytes)"), `out` empty.
bool readRegularFile(const std::string& path, std::size_t maxBytes, std::string& out,
                     std::string& why);

} // namespace toytui
