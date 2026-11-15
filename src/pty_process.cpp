#include "pty_process.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>

namespace toytui {

namespace {

void setWinsize(int fd, int rows, int cols) {
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(std::clamp(rows, 1, 65535));
    ws.ws_col = static_cast<unsigned short>(std::clamp(cols, 1, 65535));
    ::ioctl(fd, TIOCSWINSZ, &ws);
}

// In the child, after fork: report an error through the status pipe and leave.
[[noreturn]] void childFail(int statusFd, const char* what) {
    char msg[160];
    const int e = errno;
    const int len = ::snprintf(msg, sizeof msg, "%s: %s", what, ::strerror(e));
    if (len > 0) {
        (void)!::write(statusFd, msg, static_cast<std::size_t>(len));
    }
    ::_exit(127);
}

} // namespace

std::unique_ptr<PtyProcess> PtyProcess::start(const std::string& cmd, const std::string& cwd, int rows, int cols,
                                              std::string& err) {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (master < 0) {
        err = std::string("posix_openpt: ") + ::strerror(errno);
        return nullptr;
    }
    char slaveName[128];
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0 || ::ptsname_r(master, slaveName, sizeof slaveName) != 0) {
        err = std::string("pty setup: ") + ::strerror(errno);
        ::close(master);
        return nullptr;
    }
    setWinsize(master, rows, cols);
    int status[2];
    if (::pipe2(status, O_CLOEXEC) != 0) {
        err = std::string("pipe: ") + ::strerror(errno);
        ::close(master);
        return nullptr;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        err = std::string("fork: ") + ::strerror(errno);
        ::close(master);
        ::close(status[0]);
        ::close(status[1]);
        return nullptr;
    }
    if (pid == 0) { // child
        ::setsid();
        const int slave = ::open(slaveName, O_RDWR);
        if (slave < 0) {
            childFail(status[1], "open pty slave");
        }
        ::ioctl(slave, TIOCSCTTY, 0);
        ::dup2(slave, 0);
        ::dup2(slave, 1);
        ::dup2(slave, 2);
        // Every other descriptor (the daemon's socket, the master, ...) is closed; the status pipe
        // closes by itself on exec.
        const long maxFd = std::min<long>(::sysconf(_SC_OPEN_MAX), 65536);
        for (int fd = 3; fd < maxFd; ++fd) {
            if (fd != status[1]) {
                ::close(fd);
            }
        }
        // Default signal handling and an empty mask: the daemon's own dispositions must not leak.
        for (int sig = 1; sig < NSIG; ++sig) {
            if (sig != SIGKILL && sig != SIGSTOP) {
                ::signal(sig, SIG_DFL);
            }
        }
        sigset_t none;
        sigemptyset(&none);
        ::sigprocmask(SIG_SETMASK, &none, nullptr);
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) {
            childFail(status[1], ("chdir `" + cwd + "`").c_str());
        }
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLUMNS", std::to_string(std::max(cols, 1)).c_str(), 1);
        ::setenv("LINES", std::to_string(std::max(rows, 1)).c_str(), 1);
        ::execl("/bin/sh", "sh", "-c", cmd.c_str(), static_cast<char*>(nullptr));
        childFail(status[1], "exec /bin/sh");
    }

    // parent: wait for the exec (the pipe closes) or for the child's error report
    ::close(status[1]);
    std::string report;
    bool timedOut = false;
    for (;;) {
        struct pollfd p {};
        p.fd = status[0];
        p.events = POLLIN;
        const int rc = ::poll(&p, 1, 5000);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        if (rc == 0) {
            timedOut = true;
            break;
        }
        char buf[256];
        const ssize_t n = ::read(status[0], buf, sizeof buf);
        if (n > 0) {
            report.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        break; // EOF: exec happened (or the child reported and exited)
    }
    ::close(status[0]);
    if (!report.empty() || timedOut) {
        if (timedOut) {
            ::kill(pid, SIGKILL);
            report = "the child did not start in time";
        }
        int st = 0;
        ::waitpid(pid, &st, 0);
        ::close(master);
        err = report;
        return nullptr;
    }

    const int fl = ::fcntl(master, F_GETFL, 0);
    ::fcntl(master, F_SETFL, fl | O_NONBLOCK);
    std::unique_ptr<PtyProcess> p(new PtyProcess);
    p->master_ = master;
    p->pid_ = pid;
    return p;
}

PtyProcess::~PtyProcess() {
    if (!exited_ && pid_ > 0) {
        ::kill(-pid_, SIGKILL);
        ::kill(pid_, SIGKILL);
        int st = 0;
        ::waitpid(pid_, &st, 0);
    }
    closeTerminal();
}

PtyProcess::ReadStatus PtyProcess::read(char* buf, std::size_t max, std::size_t& n) {
    n = 0;
    if (master_ < 0) {
        return ReadStatus::Eof;
    }
    for (;;) {
        const ssize_t r = ::read(master_, buf, max);
        if (r > 0) {
            n = static_cast<std::size_t>(r);
            return ReadStatus::Data;
        }
        if (r == 0) {
            return ReadStatus::Eof;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return ReadStatus::Again;
        }
        return ReadStatus::Eof; // EIO: the slave side is closed
    }
}

bool PtyProcess::queueWrite(const std::string& data) {
    if (queue_.size() + data.size() > kMaxQueued) {
        return false;
    }
    queue_ += data;
    return true;
}

bool PtyProcess::flush() {
    while (!queue_.empty() && master_ >= 0) {
        const ssize_t w = ::write(master_, queue_.data(), queue_.size());
        if (w > 0) {
            queue_.erase(0, static_cast<std::size_t>(w));
            continue;
        }
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true; // the rest stays queued
        }
        queue_.clear(); // the terminal is gone
        return false;
    }
    return true;
}

void PtyProcess::resize(int rows, int cols) {
    if (master_ >= 0) {
        setWinsize(master_, rows, cols);
    }
}

void PtyProcess::signal(int sig) {
    if (!exited_ && pid_ > 0) {
        if (::kill(-pid_, sig) != 0) {
            ::kill(pid_, sig);
        }
    }
}

bool PtyProcess::reap() {
    if (exited_) {
        return true;
    }
    int st = 0;
    const pid_t r = ::waitpid(pid_, &st, WNOHANG);
    if (r != pid_) {
        if (r < 0 && errno == ECHILD) { // somebody else collected it: treat as gone
            exited_ = true;
            exitCode_ = 255;
            return true;
        }
        return false;
    }
    exited_ = true;
    exitCode_ = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    return true;
}

void PtyProcess::terminate() {
    if (exited_) {
        return;
    }
    ::kill(-pid_, SIGHUP);
    ::kill(-pid_, SIGCONT);
    for (int i = 0; i < 250; ++i) { // up to 500 ms, in 2 ms steps (most programs end within a few ms)
        if (reap()) {
            return;
        }
        ::usleep(2 * 1000);
    }
    if (reap()) {
        return;
    }
    ::kill(-pid_, SIGKILL);
    ::kill(pid_, SIGKILL);
    int st = 0;
    if (::waitpid(pid_, &st, 0) == pid_) {
        exited_ = true;
        exitCode_ = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    } else {
        exited_ = true;
        exitCode_ = 255;
    }
}

void PtyProcess::closeTerminal() {
    if (master_ >= 0) {
        ::close(master_);
        master_ = -1;
    }
    queue_.clear();
}

} // namespace toytui
