// renderer.h — a SIMPLE, NON-diff-based, full-screen-repaint renderer.
//
// Compositing is the job of compositor.h; this module only writes
// the FINISHED image, always as a full clear + repaint (diff-based
// output is the job of diff.h).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cell.h"

namespace toytui {

// A width*height "empty" screen buffer of a single colour (bg/fg),
// filled with spaces.
std::vector<Cell> makeBlankScreen(int width, int height, std::uint8_t bg,
                                   std::uint8_t fg);

// Full screen clear + cursor home, then row-by-row ANSI encoding
// grouped into colour runs (256-colour SGR). There is no alternate screen
// buffer (explicit requirement).
std::string encodeFullRepaint(int width, int height,
                               const std::vector<Cell>& screen);

// Write the output of encodeFullRepaint to the given target (a tty
// path or any EXISTING, writable file/device; it does not create
// a new file). ToytuiException on failure (non-existent/unopenable
// target, failed write).
void writeToTty(const std::string& path, const std::string& content);

// One-time cursor on/off of the terminal (`CSI ?25h` / `CSI ?25l`, like `tput cnorm` /
// `tput civis`) when the `core` `term_cursor` parameter is GIVEN. Acts only on a TERMINAL target (`isatty`): to a file or
// pipe it writes no control sequence (captured output stays clean). Best-effort, does not throw
// (rendering has already checked the target). Returns whether the sequence was written. The program does NOT
// restore it on exit and does not repeat it on every start.
bool setTerminalCursor(const std::string& ttyPath, bool visible);

// Write an arbitrary (control) sequence, but only to a terminal (`isatty`) target; to a file or pipe it writes nothing.
// Best-effort, does not throw. Returns whether it was written.
bool writeIfTerminal(const std::string& ttyPath, const std::string& sequence);

} // namespace toytui
