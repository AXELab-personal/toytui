// pty_process.h — a child process on a pseudo-terminal: start, non-blocking I/O,
// window size, signals, reaping.
//
// The child runs `/bin/sh -c <cmd>` as the leader of its own session with the pty
// slave as its controlling terminal (stdin/stdout/stderr), so job control, `tty`
// and full-screen programs work. The master side is non-blocking: the owner (the
// daemon's poll loop) watches `fd()`.
#pragma once

#include <sys/types.h>

#include <cstddef>
#include <memory>
#include <string>

namespace toytui {

class PtyProcess {
public:
    enum class ReadStatus {
        Data,  // `n` bytes were read
        Again, // nothing to read now
        Eof,   // the child side is closed (the process has exited or closed the terminal)
    };

    // Upper bound of the queued, not yet written input.
    static constexpr std::size_t kMaxQueued = 1u << 20;

    // Starts `cmd` in `cwd` (empty: the current directory) on a `rows` x `cols` terminal.
    // TERM=xterm-256color, COLUMNS and LINES are set. Failures to start (no pty, fork or
    // chdir failure, /bin/sh cannot be executed) are reported synchronously: nullptr and `err`.
    static std::unique_ptr<PtyProcess> start(const std::string& cmd, const std::string& cwd, int rows, int cols,
                                             std::string& err);
    ~PtyProcess(); // a still running process is killed and reaped
    PtyProcess(const PtyProcess&) = delete;
    PtyProcess& operator=(const PtyProcess&) = delete;

    int fd() const { return master_; } // -1 after the terminal was closed
    pid_t pid() const { return pid_; }

    // Reads up to `max` bytes without blocking.
    ReadStatus read(char* buf, std::size_t max, std::size_t& n);

    // Input for the program: queued, written without blocking by `flush`. false (nothing queued)
    // if the queue would exceed kMaxQueued.
    bool queueWrite(const std::string& data);
    // Writes as much of the queue as the pty accepts. false if the terminal is gone.
    bool flush();
    bool wantsWrite() const { return !queue_.empty() && master_ >= 0; }

    void resize(int rows, int cols); // TIOCSWINSZ (the program gets SIGWINCH)

    // Signal to the process group of the child.
    void signal(int sig);

    // Collects the exit status without blocking. true if the process has exited (now or before).
    bool reap();
    bool running() const { return !exited_; }
    // Exit code, or 128+signal if the process was killed by a signal; meaningful once !running().
    int exitCode() const { return exitCode_; }

    // Stops the process: SIGHUP to the group, up to 500 ms of waiting (2 ms steps), then SIGKILL; reaped on return.
    void terminate();

    // Closes the master side (after the process is gone and its output is read).
    void closeTerminal();

private:
    PtyProcess() = default;

    int master_ = -1;
    pid_t pid_ = -1;
    bool exited_ = false;
    int exitCode_ = 0;
    std::string queue_;
};

} // namespace toytui
