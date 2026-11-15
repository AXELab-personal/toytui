#include "daemon_process.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "daemon_config.h"
#include "daemon_socket.h"
#include "engine.h"
#include "error.h"
#include "session.h"
#include "subterm_runtime.h"

namespace toytui {

namespace fs = std::filesystem;

namespace {

volatile sig_atomic_t g_terminate = 0;

// The daemon's lifecycle commands on top of the executor: `core set mode:file` (the other parameters take effect first,
// then the state goes in place of `toytui.state`, and the daemon exits after sending the response). Everything
// else goes to the executor.
class LifecyclePort : public ExecutionPort {
public:
    LifecyclePort(Engine& engine, DaemonBackend& backend, std::string stateFile, SubtermRuntime& subterms)
        : engine_(engine), backend_(backend), stateFile_(std::move(stateFile)), subterms_(subterms) {}

    bool exitRequested() const { return exit_; }
    // Leaving with a parked state (watchers): the reason shows up as an error at the next start until acknowledged.
    void leaveAndExit(const std::string& reason) {
        subterms_.terminateAll();
        leave(true, reason);
        exit_ = true;
    }
    // Leaving `destroy`-like, without writing the state (the state file vanished): the cursor is reset.
    void vanishAndExit() {
        subterms_.terminateAll();
        engine_.resetTerminalCursor();
        exit_ = true;
    }
    std::string ttyPath() const {
        const auto s = backend_.loadCore();
        return s ? s->tree.find("core@terminal")->params.text("tty") : std::string();
    }

    ExecResult redraw() override { return engine_.redraw(); }
    ExecResult execute(const ParsedCommand& cmd) override {
        if (cmd.method == "set" && cmd.elementType == "core") {
            for (const ParamToken& p : cmd.params) {
                if (p.name == "mode") {
                    return setMode(cmd);
                }
            }
        }
        return engine_.execute(cmd);
    }
    ExecResult keypress(const std::optional<std::string>& t) override { return engine_.keypress(t); }
    KeyboardStart keyboardStart(bool w) override { return engine_.keyboardStart(w); }
    ExecResult keyboardKey(const std::string& f, const std::string& t, std::string& e) override {
        return engine_.keyboardKey(f, t, e);
    }
    ExecResult keyboardEnd(const std::string& f) override { return engine_.keyboardEnd(f); }

private:
    ExecResult fail(const std::string& msg) {
        ExecResult r;
        r.rc = 1;
        r.err = ToytuiError{"set", "core@terminal", msg}.format() + "\n";
        return r;
    }

    ExecResult setMode(const ParsedCommand& cmd) {
        ParsedCommand rest = cmd;
        rest.params.clear();
        std::optional<std::string> value;
        for (const ParamToken& p : cmd.params) {
            if (p.name == "mode") {
                value = p.value;
            } else {
                rest.params.push_back(p);
            }
        }
        if (!value || (*value != "file" && *value != "daemon" && *value != "exit" && *value != "destroy")) {
            return fail("Invalid mode (file, daemon, exit, destroy): `mode:" + value.value_or("") + "`");
        }
        if (*value == "destroy") { // the other parameters are meaningless: the session ceases to exist
            subterms_.terminateAll();
            engine_.resetTerminalCursor(); // the terminal content stays
            fs::remove(stateFile_);
            exit_ = true;
            return ExecResult{};
        }
        if (!rest.params.empty()) {
            const ExecResult r = engine_.execute(rest);
            if (r.rc != 0) {
                return r;
            }
        }
        if (*value == "daemon") {
            return ExecResult{};
        }
        if (*value == "file" && subterms_.runningCount() > 0) { // file mode cannot keep programs running
            std::string names;
            for (const std::string& id : subterms_.runningIds()) {
                names += (names.empty() ? "" : ", ") + id;
            }
            return fail("Cannot leave the daemon mode: running program(s): " + names + " (stop them first: `exec:stop`)");
        }
        subterms_.terminateAll(); // `exit`: the programs end with the daemon
        ExecResult r = leave(*value == "exit", "");
        exit_ = r.rc == 0;
        return r;
    }

public:
    // Writing the state in place of `toytui.state` (in place of the marker): `intent` = at the next start the daemon
    // starts automatically; `abnormal` = the reason of an abnormal exit (empty: regular). On failure the daemon keeps running.
    ExecResult leave(bool intent, const std::string& abnormal) {
        try {
            if (engine_.pulseDue()) {
                engine_.tick(); // the pending released image is also drawn: the state leaves with the final image
            }
            Session session = backend_.loadFull().value();
            session.daemonIntent = intent;
            session.abnormal = abnormal;
            const std::string startupDir = fs::path(stateFile_).parent_path().string();
            saveLeavingDaemon(startupDir, session.tree.find("core@terminal")->params.text("path"), session);
        } catch (const ToytuiException& e) {
            ExecResult r;
            r.rc = 1;
            r.err = e.err.format() + "\n";
            return r; // the daemon keeps running, the marker is untouched
        }
        return ExecResult{};
    }

private:

    Engine& engine_;
    DaemonBackend& backend_;
    std::string stateFile_;
    SubtermRuntime& subterms_;
    bool exit_ = false;
};

void onTerminate(int) { g_terminate = 1; }

constexpr int kReadyTimeoutMs = 5000;

// Conditions checked periodically (`kWatchIntervalMs`): the socket file exists; the state file is still our
// marker; `tty` exists. A failure is a parked exit (`socket-missing`, `tty-lost`), the absence of the state file
// is a `destroy`-like exit.
void watch(LifecyclePort& life, DaemonServer& server, const std::string& stateFile, std::uint32_t pid) {
    const auto marker = [&]() -> std::optional<DaemonMarker> {
        try {
            return readDaemonMarker(stateFile);
        } catch (const ToytuiException&) {
            return std::nullopt;
        }
    }();
    if (!marker || marker->pid != pid) {
        life.vanishAndExit();
        return;
    }
    if (!server.socketIntact()) {
        life.leaveAndExit("socket-missing");
        return;
    }
    const std::string tty = life.ttyPath();
    if (!tty.empty() && ::access(tty.c_str(), F_OK) != 0) {
        life.leaveAndExit("tty-lost");
    }
}

// The serving loop: requests on the socket; releasing the deferred pulse when due; the watchers; SIGTERM
// stops it. How to leave (parked / `file` / `destroy`) is decided by the `LifecyclePort` and the caller.
void serveLoop(Engine& engine, LifecyclePort& life, DaemonServer& server, const std::string& stateFile,
               std::uint32_t pid, SubtermRuntime& subterms) {
    constexpr int kRefreshMs = 30; // coalescing of the redraws caused by program output
    bool dirty = false;
    auto lastRefresh = std::chrono::steady_clock::now();
    auto nextWatch = std::chrono::steady_clock::now() + std::chrono::milliseconds(daemon::kWatchIntervalMs);
    auto nextResize = std::chrono::steady_clock::now() + std::chrono::milliseconds(daemon::kResizePollMs);
    while (g_terminate == 0 && !life.exitRequested()) {
        int timeout = daemon::kResizePollMs;
        if (const auto due = engine.pulseDue()) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  *due - std::chrono::steady_clock::now())
                                  .count();
            timeout = left <= 0 ? 0 : static_cast<int>(left < timeout ? left : timeout);
        }
        if (dirty) { // a redraw is waiting: do not sleep past its time
            timeout = std::min(timeout, kRefreshMs);
        }
        if (subterms.busy()) {
            timeout = std::min(timeout, 10);
        }
        std::vector<int> fds;
        subterms.pollFds(fds);
        server.serveReady(timeout, fds);
        if (subterms.pump()) {
            dirty = true;
        }
        if (dirty && !engine.pulseDue() &&
            std::chrono::steady_clock::now() - lastRefresh >= std::chrono::milliseconds(kRefreshMs)) {
            dirty = false;
            lastRefresh = std::chrono::steady_clock::now();
            engine.tick(); // redraws the current state (program output is not stored in the state file)
        }
        if (const auto due = engine.pulseDue(); due && std::chrono::steady_clock::now() >= *due) {
            engine.tick();
        }
        if (std::chrono::steady_clock::now() >= nextResize) { // the daemon has no SIGWINCH: periodic polling
            nextResize = std::chrono::steady_clock::now() + std::chrono::milliseconds(daemon::kResizePollMs);
            if (engine.terminalResized()) {
                engine.tick();
            }
        }
        if (std::chrono::steady_clock::now() >= nextWatch) {
            nextWatch = std::chrono::steady_clock::now() + std::chrono::milliseconds(daemon::kWatchIntervalMs);
            watch(life, server, stateFile, pid);
        }
    }
}

[[noreturn]] void childMain(const Session& initial, const std::string& stateFile, int readyFd) {
    auto reply = [&](char status, const std::string& msg) {
        const std::string out = std::string(1, status) + msg;
        (void)!::write(readyFd, out.data(), out.size());
        ::close(readyFd);
    };
    try {
        ::setsid();
        const int nul = ::open("/dev/null", O_RDWR);
        if (nul >= 0) {
            ::dup2(nul, 0);
            ::dup2(nul, 1);
            ::dup2(nul, 2);
            if (nul > 2) {
                ::close(nul);
            }
        }
        struct sigaction sa {};
        sa.sa_handler = onTerminate; // without SA_RESTART: poll is interrupted, the loop notices
        sigemptyset(&sa.sa_mask);
        ::sigaction(SIGTERM, &sa, nullptr);
        ::sigaction(SIGHUP, &sa, nullptr);
        ::signal(SIGPIPE, SIG_IGN);

        SubtermRuntime subterms;
        const ElementRegistry registry = makeBuiltinRegistry(&subterms);
        DaemonBackend backend(initial);
        Engine engine(backend, registry);
        engine.setDeferredPulse(true);
        LifecyclePort life(engine, backend, stateFile, subterms);
        DaemonServer server(life, makeSocketPath());
        std::string err;
        if (!server.start(err)) {
            reply('N', err);
            ::_exit(1);
        }
        DaemonMarker marker;
        marker.socketPath = server.path();
        marker.pid = static_cast<std::uint32_t>(::getpid());
        marker.startTime = processStartTime(::getpid());
        writeDaemonMarker(stateFile, marker);
        reply('Y', marker.socketPath);

        serveLoop(engine, life, server, stateFile, marker.pid, subterms);

        if (!life.exitRequested()) { // SIGTERM / SIGHUP: parked state, with the reason recorded (an error until acknowledged)
            life.leave(true, "terminated");
        }
        subterms.terminateAll();
        server.shutdown();
        // The marker is to be deleted only if it is still ours (regular exits are handled separately).
        if (const auto cur = readDaemonMarker(stateFile); cur && cur->pid == marker.pid) {
            ::unlink(stateFile.c_str());
        }
        ::_exit(0);
    } catch (const ToytuiException& e) {
        reply('N', e.err.format());
        ::_exit(1);
    } catch (const std::exception& e) {
        reply('N', e.what());
        ::_exit(1);
    }
}

} // namespace

ElementLookup DaemonBackend::loadElement(const std::string& fullId, ElementParams& out) {
    const ElementNode* n = state_.tree.find(fullId);
    if (n == nullptr) {
        return ElementLookup::Missing;
    }
    out = n->params;
    return ElementLookup::Found;
}

std::uint64_t processStartTime(pid_t pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(f, line)) {
        return 0;
    }
    const auto close = line.rfind(')'); // the name is in parentheses, may contain spaces too
    if (close == std::string::npos) {
        return 0;
    }
    std::istringstream rest(line.substr(close + 1));
    std::string field;
    for (int i = 3; i <= 22; ++i) { // the first field after `)` is the 3rd
        if (!(rest >> field)) {
            return 0;
        }
    }
    try {
        return std::stoull(field);
    } catch (...) {
        return 0;
    }
}

bool daemonProcessAlive(const DaemonMarker& marker) {
    if (marker.pid == 0 || ::kill(static_cast<pid_t>(marker.pid), 0) != 0) {
        return false;
    }
    { // a zombie (exited, but not yet reaped by the parent/init) is not alive
        std::ifstream f("/proc/" + std::to_string(marker.pid) + "/stat");
        std::string line;
        if (std::getline(f, line)) {
            const auto close = line.rfind(')');
            if (close != std::string::npos && close + 2 < line.size() && line[close + 2] == 'Z') {
                return false;
            }
        }
    }
    const std::uint64_t start = processStartTime(static_cast<pid_t>(marker.pid));
    return start != 0 && start == marker.startTime;
}

namespace {
void sleepMs(int ms) {
    timespec ts{ms / 1000, (ms % 1000) * 1000000L};
    ::nanosleep(&ts, nullptr);
}
} // namespace

std::string reapDeadDaemon(const std::string& stateFile, const DaemonMarker& marker) {
    bool killed = false;
    bool existed = daemonProcessAlive(marker);
    if (existed) {
        ::kill(static_cast<pid_t>(marker.pid), SIGTERM); // gently: the daemon can leave into the parked state
        for (int i = 0; i < daemon::kStopWaitSteps && daemonProcessAlive(marker); ++i) {
            sleepMs(daemon::kStopWaitStepMs);
        }
        if (daemonProcessAlive(marker)) {
            ::kill(static_cast<pid_t>(marker.pid), SIGKILL);
            killed = true;
            for (int i = 0; i < daemon::kStopWaitSteps && daemonProcessAlive(marker); ++i) {
                sleepMs(daemon::kStopWaitStepMs / 2);
            }
        }
    }
    // Leftovers: the socket file, and the marker (if the daemon did not replace it with a parked state).
    ::unlink(marker.socketPath.c_str());
    bool markerLeft = false;
    try {
        const auto cur = readDaemonMarker(stateFile);
        markerLeft = cur && cur->pid == marker.pid;
    } catch (const ToytuiException&) {
    }
    if (markerLeft) {
        ::unlink(stateFile.c_str());
    }
    std::string reason;
    try {
        if (!markerLeft) {
            const auto s = tryLoadCoreSession(fs::path(stateFile).parent_path().string());
            if (s && !s->abnormal.empty()) {
                reason = s->abnormal;
            }
        }
    } catch (const ToytuiException&) {
    }
    if (!reason.empty()) {
        return "The daemon did not respond and was stopped; its state is parked (`" + reason +
               "`), acknowledge with `core set ack:" + reason + "`";
    }
    if (!markerLeft) {
        return "The daemon did not respond and exited; the command was not run";
    }
    return std::string(killed ? "The daemon did not respond and was killed"
                              : existed ? "The daemon did not respond" : "The daemon process is gone") +
           "; the session is lost (state and socket cleaned up), the command was not run";
}

bool startDaemon(const Session& initial, const std::string& stateFile, DaemonMarker& marker, std::string& err) {
    int p[2];
    if (::pipe2(p, O_CLOEXEC) != 0) {
        err = std::string("pipe: ") + std::strerror(errno);
        return false;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        err = std::string("fork: ") + std::strerror(errno);
        ::close(p[0]);
        ::close(p[1]);
        return false;
    }
    if (pid == 0) {
        ::close(p[0]);
        childMain(initial, stateFile, p[1]);
    }
    ::close(p[1]);
    std::string msg;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kReadyTimeoutMs);
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                              deadline - std::chrono::steady_clock::now())
                              .count();
        pollfd pf{p[0], POLLIN, 0};
        const int n = ::poll(&pf, 1, left < 0 ? 0 : static_cast<int>(left));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            err = "The daemon did not become ready in time";
            ::kill(pid, SIGKILL);
            break;
        }
        char buf[4096];
        const ssize_t r = ::read(p[0], buf, sizeof(buf));
        if (r > 0) {
            msg.append(buf, static_cast<std::size_t>(r));
            continue;
        }
        break; // the child closed the pipe (or exited)
    }
    ::close(p[0]);
    if (!msg.empty() && msg[0] == 'Y') {
        marker.socketPath = msg.substr(1);
        marker.pid = static_cast<std::uint32_t>(pid);
        marker.startTime = processStartTime(pid);
        return true;
    }
    int st = 0;
    ::waitpid(pid, &st, 0);
    if (err.empty()) {
        err = msg.size() > 1 ? "Cannot start the daemon: " + msg.substr(1) : "The daemon exited during startup";
    }
    return false;
}

} // namespace toytui
