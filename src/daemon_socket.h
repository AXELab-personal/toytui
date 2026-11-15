// daemon_socket.h — client↔daemon transport over a Unix domain socket (stream): the socket location, the server
// (one connection = one request, serial serving, dedupe cache) and the client (attempts with
// a time limit, retries). The server gets the poll loop from the caller: `listenFd()` and `serveReady()`.
#pragma once

#include <sys/types.h>

#include <deque>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

#include "daemon_protocol.h"
#include "execution_port.h"

namespace toytui {

// The socket does not exist / does not accept connections (the daemon is gone): retrying is pointless.
class DaemonGone : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
// Every attempt ended with a timeout or a broken connection: the daemon is considered dead.
class DaemonUnresponsive : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
// One attempt failed and can be retried (thrown by `socketExchange`).
class AttemptFailed : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// New socket path: `$XDG_RUNTIME_DIR/toytui-<id>.sock`, or failing that `/tmp/toytui-<uid>/toytui-<id>.sock`
// (the directory is 0700, our own; it is created). ToytuiException if the directory is not safe.
std::string makeSocketPath();

struct SocketIdentity {
    dev_t dev = 0;
    ino_t ino = 0;
};

class DaemonServer {
public:
    DaemonServer(ExecutionPort& port, std::string socketPath) : port_(port), path_(std::move(socketPath)) {}
    DaemonServer(const DaemonServer&) = delete;
    DaemonServer& operator=(const DaemonServer&) = delete;
    ~DaemonServer(); // closes the listening socket (does NOT delete the file: see `shutdown`)

    // Creates the socket (0600) and listens. On failure false and `err`; never overwrites an existing file.
    bool start(std::string& err);
    int listenFd() const { return fd_; }
    // Waits up to `timeoutMs` for a connection; if one arrived, serves it (one request). true if there was a connection.
    // `extraFds` (e.g. program terminals) only wake the wait up early; they are not served here.
    bool serveReady(int timeoutMs, const std::vector<int>& extraFds = {});
    // Closes the listening socket and deletes the socket file (only if this instance created it).
    void shutdown();
    const std::string& path() const { return path_; }
    // Whether the socket file still exists (the same file we created).
    bool socketIntact() const;
    SocketIdentity identity() const { return identity_; }

private:
    void serveConnection(int conn);
    std::string respond(const std::string& payload);

    ExecutionPort& port_;
    std::string path_;
    int fd_ = -1;
    bool started_ = false;
    SocketIdentity identity_;
    std::deque<std::pair<std::uint64_t, std::string>> cache_; // request id -> stored response payload
};

// A single attempt: connect, send the request payload, wait for the response payload, within a total of
// `timeoutMs`. DaemonGone (no socket / refused), AttemptFailed (timeout, broken connection).
std::string socketExchange(const std::string& path, const std::string& payload, int timeoutMs);

// The `RemotePort` transport: at most `daemon::kMaxAttempts` attempts (each with
// `daemon::kAttemptTimeoutMs`), with the same payload (the same request id). DaemonGone passes
// straight through; after the last failed attempt DaemonUnresponsive.
RemotePort::Transport socketTransport(std::string path);

} // namespace toytui
