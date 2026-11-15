#include "daemon_socket.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <random>

#include "daemon_config.h"
#include "error.h"

namespace toytui {

namespace {

using Clock = std::chrono::steady_clock;

int remainingMs(Clock::time_point deadline) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return ms < 0 ? 0 : static_cast<int>(ms);
}

// Waits for an event on `fd` until the deadline; true if the event (or error/close) occurred.
bool waitFd(int fd, short events, Clock::time_point deadline) {
    for (;;) {
        pollfd p{fd, events, 0};
        const int n = ::poll(&p, 1, remainingMs(deadline));
        if (n > 0) {
            return true;
        }
        if (n == 0) {
            return false;
        }
        if (errno != EINTR) {
            return false;
        }
    }
}

class Fd {
public:
    explicit Fd(int fd) : fd_(fd) {}
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    ~Fd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    int get() const { return fd_; }

private:
    int fd_;
};

bool fillAddress(const std::string& path, sockaddr_un& addr) {
    if (path.empty() || path.size() >= sizeof(addr.sun_path)) {
        return false;
    }
    std::memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return true;
}

// Send everything by the deadline; false if the other side is gone or time ran out.
bool sendAll(int fd, const std::string& data, Clock::time_point deadline) {
    std::size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = ::send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {
            off += static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!waitFd(fd, POLLOUT, deadline)) {
                return false;
            }
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

std::string randomId() {
    static const char kAlphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::string id;
    for (int i = 0; i < 8; ++i) {
        id += kAlphabet[rd() % (sizeof(kAlphabet) - 1)];
    }
    return id;
}

[[noreturn]] void unsafeDir(const std::string& dir, const std::string& why) {
    throw ToytuiException(ToytuiError{"-", "-", "Unsafe socket directory `" + dir + "`: " + why});
}

// The directory is our own, a directory (not a symlink), and only we can access it.
void requireOwnPrivateDir(const std::string& dir) {
    struct stat st {};
    if (::lstat(dir.c_str(), &st) != 0) {
        unsafeDir(dir, std::strerror(errno));
    }
    if (!S_ISDIR(st.st_mode)) {
        unsafeDir(dir, "not a directory");
    }
    if (st.st_uid != ::geteuid()) {
        unsafeDir(dir, "not owned by the current user");
    }
    if ((st.st_mode & 077) != 0) {
        unsafeDir(dir, "accessible by others");
    }
}

} // namespace

std::string makeSocketPath() {
    std::string dir;
    const char* xdg = std::getenv("XDG_RUNTIME_DIR");
    if (xdg != nullptr && xdg[0] == '/') {
        dir = xdg;
        requireOwnPrivateDir(dir);
    } else {
        dir = "/tmp/toytui-" + std::to_string(::geteuid());
        if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
            unsafeDir(dir, std::strerror(errno));
        }
        requireOwnPrivateDir(dir);
    }
    return dir + "/toytui-" + randomId() + ".sock";
}

// --- Server -------------------------------------------------------------------------------------------

DaemonServer::~DaemonServer() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool DaemonServer::start(std::string& err) {
    sockaddr_un addr{};
    if (!fillAddress(path_, addr)) {
        err = "Socket path is empty or too long: `" + path_ + "`";
        return false;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        err = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    const mode_t oldMask = ::umask(077);
    const int rc = ::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    const int bindErrno = errno;
    ::umask(oldMask);
    if (rc != 0) {
        err = "bind `" + path_ + "`: " + std::strerror(bindErrno);
        ::close(fd);
        return false;
    }
    ::chmod(path_.c_str(), 0600);
    if (::listen(fd, 16) != 0) {
        err = std::string("listen: ") + std::strerror(errno);
        ::close(fd);
        ::unlink(path_.c_str());
        return false;
    }
    struct stat st {};
    if (::stat(path_.c_str(), &st) == 0) {
        identity_ = SocketIdentity{st.st_dev, st.st_ino};
    }
    fd_ = fd;
    started_ = true;
    return true;
}

bool DaemonServer::serveReady(int timeoutMs, const std::vector<int>& extraFds) {
    if (fd_ < 0) {
        return false;
    }
    std::vector<pollfd> fds;
    fds.push_back(pollfd{fd_, POLLIN, 0});
    for (const int fd : extraFds) {
        fds.push_back(pollfd{fd, POLLIN, 0});
    }
    if (::poll(fds.data(), fds.size(), timeoutMs) <= 0 || (fds[0].revents & POLLIN) == 0) {
        return false;
    }
    const int conn = ::accept4(fd_, nullptr, nullptr, SOCK_CLOEXEC);
    if (conn < 0) {
        return false;
    }
    Fd guard(conn);
    serveConnection(conn);
    return true;
}

void DaemonServer::shutdown() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    if (started_) {
        struct stat st {};
        // Only delete OUR OWN socket file (if another file has taken its place in the meantime, leave it).
        if (::stat(path_.c_str(), &st) == 0 && st.st_dev == identity_.dev && st.st_ino == identity_.ino) {
            ::unlink(path_.c_str());
        }
        started_ = false;
    }
}

bool DaemonServer::socketIntact() const {
    struct stat st {};
    return ::stat(path_.c_str(), &st) == 0 && st.st_dev == identity_.dev && st.st_ino == identity_.ino;
}

// One connection: same user only, read the frame with a time limit, reply, close. A faulty or
// stalled connection is closed without a reply (the client retries).
void DaemonServer::serveConnection(int conn) {
    ucred cred{};
    socklen_t len = sizeof(cred);
    if (::getsockopt(conn, SOL_SOCKET, SO_PEERCRED, &cred, &len) != 0 || cred.uid != ::geteuid()) {
        return;
    }
    const auto deadline = Clock::now() + std::chrono::milliseconds(daemon::kConnReadTimeoutMs);
    std::string buf, payload;
    char tmp[65536];
    for (;;) {
        const FrameStatus st = extractFrame(buf, payload);
        if (st == FrameStatus::Complete) {
            break;
        }
        if (st == FrameStatus::TooLarge) {
            return;
        }
        if (!waitFd(conn, POLLIN, deadline)) {
            return;
        }
        const ssize_t n = ::recv(conn, tmp, sizeof(tmp), MSG_DONTWAIT);
        if (n > 0) {
            buf.append(tmp, static_cast<std::size_t>(n));
        } else if (n == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
            return;
        }
    }
    const std::string reply = makeFrame(respond(payload));
    sendAll(conn, reply, Clock::now() + std::chrono::milliseconds(daemon::kConnReadTimeoutMs));
}

// Executing the request, with dedupe: for an already seen id we return the STORED response (the request is not
// executed again). The cache keeps the last `kDedupCacheSize` ids.
std::string DaemonServer::respond(const std::string& payload) {
    Request req;
    try {
        req = decodeRequest(payload);
    } catch (const ProtocolError&) {
        return handlePayload(port_, payload); // malformed request: an `rc = 1` response, not stored
    }
    for (const auto& entry : cache_) {
        if (entry.first == req.id) {
            return entry.second;
        }
    }
    std::string resp = encodeResponse(dispatch(port_, req));
    if (resp.size() > daemon::kMaxFrameBytes) { // does not fit in a frame: short error response with the same id
        Response tooBig;
        tooBig.id = req.id;
        tooBig.result.rc = 1;
        tooBig.result.err = ToytuiError{"-", "-", "Response too large"}.format() + "\n";
        resp = encodeResponse(tooBig);
    }
    cache_.emplace_back(req.id, resp);
    while (cache_.size() > daemon::kDedupCacheSize) {
        cache_.pop_front();
    }
    return resp;
}

// --- Client -------------------------------------------------------------------------------------------

std::string socketExchange(const std::string& path, const std::string& payload, int timeoutMs) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    sockaddr_un addr{};
    if (!fillAddress(path, addr)) {
        throw DaemonGone("socket path is empty or too long: `" + path + "`");
    }
    Fd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    if (fd.get() < 0) {
        throw AttemptFailed(std::string("socket: ") + std::strerror(errno));
    }
    for (;;) {
        if (::connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) {
            break;
        }
        const int e = errno;
        if (e == EINTR) {
            continue;
        }
        if (e == EAGAIN) { // the wait queue is full: the daemon is busy, we wait until the deadline
            if (remainingMs(deadline) == 0) {
                throw AttemptFailed("connect timed out (daemon busy)");
            }
            ::usleep(2000);
            continue;
        }
        if (e == ENOENT || e == ECONNREFUSED || e == ENOTSOCK || e == EACCES || e == EPERM) {
            throw DaemonGone("cannot connect to `" + path + "`: " + std::strerror(e));
        }
        throw AttemptFailed(std::string("connect: ") + std::strerror(e));
    }
    if (!sendAll(fd.get(), makeFrame(payload), deadline)) {
        throw AttemptFailed("send failed or timed out");
    }
    std::string buf, reply;
    char tmp[65536];
    for (;;) {
        const FrameStatus st = extractFrame(buf, reply);
        if (st == FrameStatus::Complete) {
            return reply;
        }
        if (st == FrameStatus::TooLarge) {
            throw ProtocolError("response frame too large");
        }
        if (!waitFd(fd.get(), POLLIN, deadline)) {
            throw AttemptFailed("response timed out");
        }
        const ssize_t n = ::recv(fd.get(), tmp, sizeof(tmp), MSG_DONTWAIT);
        if (n > 0) {
            buf.append(tmp, static_cast<std::size_t>(n));
        } else if (n == 0) {
            throw AttemptFailed("connection closed before the response");
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            throw AttemptFailed(std::string("recv: ") + std::strerror(errno));
        }
    }
}

RemotePort::Transport socketTransport(std::string path) {
    return [path = std::move(path)](const std::string& payload) {
        std::string last;
        for (int attempt = 0; attempt < daemon::kMaxAttempts; ++attempt) {
            try {
                return socketExchange(path, payload, daemon::kAttemptTimeoutMs);
            } catch (const AttemptFailed& e) {
                last = e.what();
            }
        }
        throw DaemonUnresponsive("no response from the daemon after " + std::to_string(daemon::kMaxAttempts) +
                                 " attempts (" + last + ")");
    };
}

} // namespace toytui
