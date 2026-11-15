#include "terminal_size.h"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace toytui {

std::optional<std::pair<int, int>> queryTerminalSize(const std::string& ttyPath) {
    const int fd = ::open(ttyPath.c_str(), O_RDONLY | O_NOCTTY);
    if (fd < 0) {
        return std::nullopt;
    }
    struct winsize ws {};
    const int rc = ::ioctl(fd, TIOCGWINSZ, &ws);
    ::close(fd);
    if (rc != 0 || ws.ws_col == 0 || ws.ws_row == 0) {
        return std::nullopt;
    }
    return std::make_pair(static_cast<int>(ws.ws_col), static_cast<int>(ws.ws_row));
}

} // namespace toytui
