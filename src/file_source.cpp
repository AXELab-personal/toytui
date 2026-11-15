#include "file_source.h"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace toytui {

namespace {

std::string errnoText(int e) {
    std::string s = std::strerror(e);
    if (!s.empty()) {
        s[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[0])));
    }
    return s;
}

struct Fd {
    int fd;
    ~Fd() {
        if (fd >= 0) {
            ::close(fd);
        }
    }
};

} // namespace

bool readRegularFile(const std::string& path, std::size_t maxBytes, std::string& out,
                     std::string& why) {
    out.clear();
    if (path.empty()) {
        why = "empty path";
        return false;
    }
    Fd f{::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)};
    if (f.fd < 0) {
        why = errnoText(errno);
        return false;
    }
    struct stat st {};
    if (::fstat(f.fd, &st) != 0) {
        why = errnoText(errno);
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        why = "is a directory";
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        why = "not a regular file";
        return false;
    }
    if (st.st_size > 0 && static_cast<std::uint64_t>(st.st_size) > maxBytes) {
        why = "too large (" + std::to_string(static_cast<long long>(st.st_size)) + " > " +
              std::to_string(maxBytes) + " bytes)";
        return false;
    }
    char buf[65536];
    for (;;) {
        const ssize_t n = ::read(f.fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            why = errnoText(errno);
            out.clear();
            return false;
        }
        if (n == 0) {
            return true;
        }
        out.append(buf, static_cast<std::size_t>(n));
        if (out.size() > maxBytes) {
            why = "too large (more than " + std::to_string(maxBytes) + " bytes)";
            out.clear();
            return false;
        }
    }
}

} // namespace toytui
