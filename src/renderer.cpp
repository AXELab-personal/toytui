#include "renderer.h"

#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

#include "ansi_handler.h"
#include "error.h"

namespace toytui {

std::vector<Cell> makeBlankScreen(int width, int height, std::uint8_t bg,
                                   std::uint8_t fg) {
    std::vector<Cell> screen(static_cast<std::size_t>(width) *
                              static_cast<std::size_t>(height));
    for (auto& c : screen) {
        c.ch[0] = ' ';
        c.bg = bg;
        c.fg = fg;
    }
    return screen;
}

std::string encodeFullRepaint(int width, int height,
                               const std::vector<Cell>& screen) {
    std::string out;
    out += "\x1b[2J\x1b[H"; // full clear + cursor home (no
                            // alternate screen buffer)

    for (int row = 0; row < height; ++row) {
        out += "\x1b[" + std::to_string(row + 1) + ";1H";
        bool haveLast = false;
        Cell last{};
        for (int col = 0; col < width; ++col) {
            const Cell& c =
                screen[static_cast<std::size_t>(row) *
                           static_cast<std::size_t>(width) +
                       static_cast<std::size_t>(col)];
            if (isWideRight(c)) {
                continue; // the left half's character already covers two columns
            }
            if (!haveLast || !sameCellStyle(c, last)) {
                out += sgrResetAndSet(c);
                last = c;
                haveLast = true;
            }
            out += getCellChar(c); // complete UTF-8 sequence (max 4 bytes)
        }
        out += "\x1b[0m";
    }
    return out;
}

void writeToTty(const std::string& path, const std::string& content) {
    // NO O_CREAT: the program writes only to an EXISTING, writable target (consistent
    // with the `core set tty:` check, where a non-existent target is an error) —
    // a target that vanished midway is not silently re-created.
    // A regular file target (test) must be created beforehand.
    // O_TRUNC: every call is a "fresh, full repaint",
    // never an append.
    int fd = ::open(path.c_str(), O_WRONLY | O_TRUNC);
    if (fd < 0) {
        throw ToytuiException(
            ToytuiError{"-", "-",
                        "Cannot open target for writing: `" + path + "`"});
    }

    const char* data = content.data();
    std::size_t remaining = content.size();
    while (remaining > 0) {
        ssize_t written = ::write(fd, data, remaining);
        if (written < 0) {
            ::close(fd);
            throw ToytuiException(
                ToytuiError{"-", "-", "Write failed on target: `" + path + "`"});
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    ::close(fd);
}

bool writeIfTerminal(const std::string& ttyPath, const std::string& sequence) {
    if (sequence.empty()) {
        return false;
    }
    const int fd = ::open(ttyPath.c_str(), O_WRONLY | O_NOCTTY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    bool wrote = false;
    if (::isatty(fd) != 0) {
        std::size_t done = 0;
        while (done < sequence.size()) {
            const ssize_t n = ::write(fd, sequence.data() + done, sequence.size() - done);
            if (n < 0) {
                break;
            }
            done += static_cast<std::size_t>(n);
        }
        wrote = done == sequence.size();
    }
    ::close(fd);
    return wrote;
}

bool setTerminalCursor(const std::string& ttyPath, bool visible) {
    return writeIfTerminal(ttyPath, visible ? "\x1b[?25h" : "\x1b[?25l");
}

} // namespace toytui
