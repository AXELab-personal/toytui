#include "tty_input.h"

#include <cerrno>
#include <csignal>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include "keycodec.h"

namespace toytui {

namespace {

// On a fatal signal the terminal must be restored (otherwise the
// user's terminal stays in raw mode). Uses only async-signal-safe
// calls (tcsetattr, signal, raise).
termios g_saved;
int g_savedFd = -1;
struct sigaction g_oldTerm, g_oldHup, g_oldInt, g_oldQuit;

void onFatalSignal(int sig) {
    if (g_savedFd >= 0) {
        ::tcsetattr(g_savedFd, TCSANOW, &g_saved);
    }
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

void installSignalHandlers() {
    struct sigaction sa {};
    sa.sa_handler = onFatalSignal;
    ::sigemptyset(&sa.sa_mask);
    ::sigaction(SIGTERM, &sa, &g_oldTerm);
    ::sigaction(SIGHUP, &sa, &g_oldHup);
    ::sigaction(SIGINT, &sa, &g_oldInt);
    ::sigaction(SIGQUIT, &sa, &g_oldQuit);
}

void restoreSignalHandlers() {
    ::sigaction(SIGTERM, &g_oldTerm, nullptr);
    ::sigaction(SIGHUP, &g_oldHup, nullptr);
    ::sigaction(SIGINT, &g_oldInt, nullptr);
    ::sigaction(SIGQUIT, &g_oldQuit, nullptr);
}

// 1: readable; 0: time ran out; -1: error
int waitReadable(int fd, int timeoutMs) {
    pollfd p{};
    p.fd = fd;
    p.events = POLLIN;
    while (true) {
        const int r = ::poll(&p, 1, timeoutMs);
        if (r < 0 && errno == EINTR) {
            continue;
        }
        if (r < 0) {
            return -1;
        }
        return r == 0 ? 0 : 1; // also 1 for POLLHUP/POLLERR: read() decides (EOF)
    }
}

// 1: one byte read; 0: EOF; -1: error
int readByte(int fd, char& c) {
    while (true) {
        const ssize_t n = ::read(fd, &c, 1);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return n < 0 ? -1 : static_cast<int>(n);
    }
}

} // namespace

InputSession::InputSession(int fd) : fd_(fd) {
    if (::isatty(fd_) == 0) {
        return;
    }
    termios t;
    if (::tcgetattr(fd_, &t) != 0) {
        return;
    }
    g_saved = t;
    g_savedFd = fd_;
    installSignalHandlers();

    // Raw input: no echo, canonical mode, signal generation (ISIG:
    // Ctrl-C/Z/\ are plain keys), CR->NL translation, XON/XOFF. OUTPUT
    // processing (OPOST) is left untouched, so that line breaks
    // written to stdout still display correctly.
    t.c_iflag &= ~static_cast<tcflag_t>(ICRNL | INLCR | IGNCR | IXON | IXOFF | ISTRIP | BRKINT |
                                         PARMRK | INPCK);
    t.c_lflag &= ~static_cast<tcflag_t>(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    // TCSANOW: does not flush keystrokes already typed (not yet read).
    if (::tcsetattr(fd_, TCSANOW, &t) == 0) {
        raw_ = true;
    } else {
        g_savedFd = -1;
        restoreSignalHandlers();
    }
}

InputSession::~InputSession() {
    if (raw_) {
        ::tcsetattr(fd_, TCSANOW, &g_saved);
        g_savedFd = -1;
        restoreSignalHandlers();
    }
}

KeyReadResult InputSession::readKey(const KeyReadOptions& opts) {
    KeyReadResult res;
    auto finish = [&](bool final) -> bool { // true: a key is ready
        const DecodeResult d = decodeKey(pending_, final);
        if (d.status != DecodeStatus::Ok) {
            return false;
        }
        pending_.erase(0, d.consumed);
        if (d.token.empty()) {
            return false; // dropped (unknown) input
        }
        res.status = KeyReadStatus::Key;
        res.token = d.token;
        return true;
    };
    while (true) {
        // 1) can a key already be produced from the bytes left over from before?
        if (!pending_.empty() && finish(false)) {
            return res;
        }
        // 2) a new byte is needed: for the FIRST byte the caller's time limit applies,
        // for the continuation of an already started sequence the ESC window.
        const bool midSequence = !pending_.empty();
        const int wait = midSequence ? kEscWindowMs : (opts.noblock ? 0 : opts.timeoutMs);
        const int ready = waitReadable(fd_, wait);
        if (ready < 0) {
            res.status = KeyReadStatus::Eof;
            return res;
        }
        if (ready == 0) {
            if (midSequence) { // the window expired: what we have so far is final
                if (finish(true)) {
                    return res;
                }
                continue;
            }
            res.status = KeyReadStatus::NoKey;
            res.token = kNoKeyToken;
            return res;
        }
        char c = 0;
        if (readByte(fd_, c) <= 0) {
            if (midSequence && finish(true)) { // EOF in the middle of a truncated sequence: we close what we have
                return res;
            }
            res.status = KeyReadStatus::Eof;
            return res;
        }
        pending_.push_back(c);
    }
}

} // namespace toytui
