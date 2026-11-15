// terminal_size.h — querying the physical terminal size (on every `./toytui`
// start, in file mode).
#pragma once

#include <optional>
#include <string>
#include <utility>

namespace toytui {

// Columns/rows (cols, rows) of the terminal at `ttyPath`, if the path
// REALLY is a terminal (ioctl TIOCGWINSZ succeeds and gives a positive
// size) — otherwise nullopt (regular file, unavailable
// device, etc.). Never throws: the caller treats nullopt as the
// "nothing to do, keep the stored size" signal.
std::optional<std::pair<int, int>> queryTerminalSize(const std::string& ttyPath);

} // namespace toytui
