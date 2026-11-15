// main.cpp — the client side: command-line processing (CLI parser), blocking key reading
// (`core get keypress` / `core get keyboard`) and printing the result (stdout/stderr, exit code).
// Execution (load -> modify -> composite -> render -> save) is the job of the `Engine` (engine.h),
// session storage is provided by the `SessionBackend` (the `toytui.state` file of the startup directory, or the daemon's memory).
//
// Note: resize detection does not run on the `get` paths — see engine.cpp `applyResize`.
#include <cstdlib>
#include <thread>
#include <chrono>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "cli_parser.h"
#include "daemon_process.h"
#include "daemon_protocol.h"
#include "daemon_socket.h"
#include "engine.h"
#include "execution_port.h"
#include "element_registry.h"
#include "error.h"
#include "input_ops.h"
#include "keycodec.h"
#include "param_types.h"
#include "session.h"
#include "session_backend.h"
#include "tty_input.h"

namespace {

using namespace toytui;
namespace fs = std::filesystem;

// One line to stdout, byte-exact (puts would stop at the first NUL: the buffer may contain any byte).
void writeLine(const std::string& s) {
    std::fwrite(s.data(), 1, s.size(), stdout);
    std::fputc('\n', stdout);
}

int fail(const std::string& method, const std::string& target, const std::string& msg) {
    std::fputs(ToytuiError{method, target, msg}.format().append("\n").c_str(), stderr);
    return 1;
}

// Printing the result of the execution.
int emit(const ExecResult& r) {
    std::fwrite(r.out.data(), 1, r.out.size(), stdout);
    std::fwrite(r.err.data(), 1, r.err.size(), stderr);
    return r.rc;
}

// `core get keypress [timeout:<ms>] [noblock:<value>]`: reading one key, then delivery to the
// Engine (see engine.cpp `Engine::keypress`).
int runKeypress(ExecutionPort& engine, const ParsedCommand& cmd) {
    KeyReadOptions opts;
    for (const ParamToken& p : cmd.params) {
        if (&p == &cmd.params[0]) {
            continue; // the `keypress` function token itself (always the first)
        }
        if (p.name == "timeout" || p.name == "noblock") {
            if (!p.value.has_value()) {
                return fail("get", "core@terminal", "Missing value for `" + p.name + "`");
            }
            if (p.name == "timeout") {
                const auto ms = parseStrictInt(*p.value);
                if (!ms || *ms < 0 || *ms > 3600000) {
                    return fail("get", "core@terminal",
                                "Invalid timeout (0-3600000 ms): `timeout:" + *p.value + "`");
                }
                opts.timeoutMs = *ms;
            } else {
                const auto b = parseBoolValue(*p.value);
                if (!b) {
                    return fail("get", "core@terminal",
                                "Invalid boolean (on/off): `noblock:" + *p.value + "`");
                }
                opts.noblock = *b;
            }
        } // every other token: not a parameter of `keypress` -> silently dropped
    }

    KeyReadResult key;
    {
        InputSession input(0); // raw mode until the end of the block; it is restored before rendering
        key = input.readKey(opts);
    }
    if (key.status == KeyReadStatus::Eof) {
        return fail("get", "core@terminal", "End of input (no key available)");
    }
    return emit(engine.keypress(key.status == KeyReadStatus::NoKey ? std::nullopt
                                                                    : std::optional<std::string>(key.token)));
}

// `core get keyboard [keys_exit:<keys>]`: a blocking loop until an exit
// key (default: `[CTRL-C]`; `keys_exit:` may list several keys, without a separator)
// arrives — the exit key NEVER reaches the target. The focus is read
// at the START of the loop. Output (one line, at the end of the loop):
//   - `auto`: `exit:<exit key> out:"<keystrokes>"` (the keystrokes' notations concatenated,
//     with `"` and `\` escaped; e.g. `exit:[TAB] out:"hello[BS]o world"`); no
//     state access, no drawing. On an error (e.g. EOF) nothing is written.
//   - element: every keystroke is a short transaction (load -> deliver -> if modified:
//     render + save); the output is only `exit:<exit key>` (no `out:`), so that the
//     caller knows WHICH key ended it (e.g. [ENTER] accept vs. [ESC] reject).
//     If the focus changes meanwhile (the target was deleted), the next forwarding is an error.
// End of input (EOF) before the exit key is an ERROR (as for `keypress`);
// the effect of keystrokes already delivered to the element stays (each was a separate,
// closed transaction).
std::string escapeOut(const std::string& s) {
    std::string r;
    for (const char c : s) {
        if (c == '"' || c == '\\') {
            r += '\\';
        }
        r += c;
    }
    return r;
}

int runKeyboard(ExecutionPort& engine, const ParsedCommand& cmd) {
    std::vector<std::string> exitKeys;
    bool explicitExit = false;
    for (const ParamToken& p : cmd.params) {
        if (p.name != "keys_exit") {
            continue; // not a parameter of `keyboard` -> silently dropped
        }
        if (!p.value.has_value()) {
            return fail("get", "core@terminal", "Missing value for `keys_exit`");
        }
        const auto k = parseExitKeys(*p.value);
        if (!k) {
            return fail("get", "core@terminal",
                        "Invalid exit keys (expected one or more keys, e.g. `[CTRL-Q]`, "
                        "`[TAB][ENTER]` or `q`): `keys_exit:" + *p.value + "`");
        }
        exitKeys = *k;
        explicitExit = true;
    }

    // The default `[CTRL-C]` applies only if there is no explicit `keys_exit:` and the focused element declares
    // no exit keys of its own either (those are the loop's exit keys too).
    const ExecutionPort::KeyboardStart start = engine.keyboardStart(!explicitExit);
    if (start.status.rc != 0) {
        return emit(start.status);
    }
    const std::string& startFocus = start.focus;
    if (!explicitExit && !start.elementExits) {
        exitKeys = {"[CTRL-C]"};
    }

    std::string collected;
    std::string exitUsed;
    std::string exitLine; // the element's declared exit key: `exit:<k> name:value...`
    ExecResult last;
    {
        InputSession input(0); // raw mode until the end of the loop; it is restored before the output
        while (true) {
            const KeyReadResult key = input.readKey(KeyReadOptions{});
            if (key.status != KeyReadStatus::Key) { // we read blocking: only EOF is possible
                const std::string keys = joinKeys(exitKeys);
                std::fputs(ToytuiError{"get", "core@terminal",
                                       keys.empty() ? std::string("End of input before an exit key")
                                                    : "End of input before the exit key `" + keys + "`"}
                               .format()
                               .append("\n")
                               .c_str(),
                           stderr);
                last.rc = 1;
                break;
            }
            if (std::find(exitKeys.begin(), exitKeys.end(), key.token) != exitKeys.end()) {
                exitUsed = key.token;
                break;
            }
            if (startFocus == "auto") {
                collected += key.token;
                continue;
            }
            last = engine.keyboardKey(startFocus, key.token, exitLine);
            if (last.rc != 0 || !exitLine.empty()) {
                break;
            }
        }
    }
    const ExecResult end = engine.keyboardEnd(startFocus); // cursor-bearing element: reset at the end of the loop
    std::fwrite(last.err.data(), 1, last.err.size(), stderr);
    if (end.rc != 0) {
        return emit(end);
    }
    if (last.rc == 0 && !exitLine.empty()) {
        writeLine(exitLine);
    } else if (last.rc == 0) {
        std::string line = "exit:" + exitUsed;
        if (startFocus == "auto") {
            line += " out:\"" + escapeOut(collected) + "\"";
        }
        writeLine(line);
    }
    return last.rc;
}

bool hasParam(const ParsedCommand& cmd, const std::string& name) {
    return std::any_of(cmd.params.begin(), cmd.params.end(), [&](const ParamToken& p) { return p.name == name; });
}

// `core get mode` / `core get socket`: decided on the client side (the mode is visible from the shape of the state file). Only
// these can be queried in one call; mixing them with other `core` parameters is an error.
ExecResult getLocalCoreParams(const ParsedCommand& cmd, const std::optional<DaemonMarker>& marker,
                              const std::optional<Session>& head) {
    ExecResult r;
    bool anyOther = false;
    for (const ParamToken& p : cmd.params) {
        if (p.name != "mode" && p.name != "socket" && p.name != "abnormal") {
            anyOther = true;
        }
    }
    if (anyOther) {
        r.rc = 1;
        r.err = ToytuiError{"get", "core@terminal",
                            "`mode`, `socket` and `abnormal` cannot be queried together with other parameters"}
                    .format() + "\n";
        return r;
    }
    for (const ParamToken& p : cmd.params) {
        if (p.name == "mode") {
            r.out += std::string("mode:") + (marker ? "daemon" : "file") + "\n";
        } else if (p.name == "abnormal") {
            r.out += "abnormal:" + (head && !head->abnormal.empty() ? head->abnormal : std::string("none")) + "\n";
        } else if (marker) {
            r.out += "socket:" + marker->socketPath + "\n";
        } else {
            r.rc = 1;
            r.out.clear();
            r.err = ToytuiError{"get", "core@terminal", "Parameter `socket` exists only in daemon mode"}.format() + "\n";
            return r;
        }
    }
    return r;
}

// Handing the session over to a newly started daemon (the state file becomes the marker). The flags of the parked state
// (intent, abnormal reason) no longer live in the daemon's memory. The relocated data file is deleted.
bool launchDaemon(const std::string& startupDir, Session session, DaemonMarker& started, std::string& err) {
    session.daemonIntent = false;
    session.abnormal.clear();
    { // The daemon has no controlling terminal: the `/dev/tty` default is replaced with the client's actual terminal.
        ElementParams& core = session.tree.findMut("core@terminal")->params;
        if (core.text("tty") == "/dev/tty") {
            for (const int fd : {0, 1, 2}) {
                if (::isatty(fd) != 0) {
                    if (const char* name = ::ttyname(fd)) {
                        core.set("tty", name);
                    }
                    break;
                }
            }
        }
    }
    const std::optional<std::string> oldData = currentDataFilePath(startupDir); // before the marker!
    const std::string stateFile = stateFilePath(startupDir);
    if (!startDaemon(session, stateFile, started, err)) {
        return false;
    }
    std::error_code ec;
    if (oldData && fs::path(*oldData) != fs::path(stateFile)) {
        fs::remove(*oldData, ec);
    }
    return true;
}

std::optional<std::string> modeValue(const ParsedCommand& cmd) {
    for (const ParamToken& p : cmd.params) {
        if (p.name == "mode") {
            return p.value.value_or("");
        }
    }
    return std::nullopt;
}

// `core set mode:daemon|file|exit|destroy`: switching the mode. The remaining parameters without a `mode` token take effect FIRST
// (except for `destroy`: the session ceases to exist). Returns the finished result, or nullopt if
// there is no `mode` token (the call may continue on the usual path).
std::optional<ExecResult> setModeCommand(ExecutionPort& port, Engine& local, const ParsedCommand& cmd,
                                         const std::string& startupDir,
                                         const std::optional<DaemonMarker>& marker) {
    const std::optional<std::string> mv = modeValue(cmd);
    if (!mv) {
        return std::nullopt;
    }
    const std::string value = *mv;
    ParsedCommand rest = cmd;
    rest.params.clear();
    for (const ParamToken& p : cmd.params) {
        if (p.name != "mode") {
            rest.params.push_back(p);
        }
    }
    auto failed = [&](const std::string& msg) {
        ExecResult r;
        r.rc = 1;
        r.err = ToytuiError{"set", "core@terminal", msg}.format() + "\n";
        return r;
    };
    if (value != "daemon" && value != "file" && value != "exit" && value != "destroy") {
        return failed("Invalid mode (file, daemon, exit, destroy): `mode:" + value + "`");
    }
    // In daemon mode the daemon does the leaving (writing the state / deleting), then exits; the whole call goes to it.
    auto forward = [&]() -> ExecResult {
        try {
            return port.execute(cmd);
        } catch (const DaemonGone&) {
            // The response may have been lost while the daemon already exited: if the marker vanished, the switch has happened.
            if (!readDaemonMarker(stateFilePath(startupDir))) {
                return ExecResult{};
            }
            throw;
        } catch (const DaemonUnresponsive&) {
            if (!readDaemonMarker(stateFilePath(startupDir))) {
                return ExecResult{};
            }
            throw;
        }
    };
    if (value == "destroy") {
        if (marker) {
            return forward();
        }
        local.resetTerminalCursor(); // the terminal content stays
        destroySession(startupDir);
        return ExecResult{};
    }
    if (value == "exit") {
        return marker ? forward() : failed("`mode:exit` is valid only in daemon mode");
    }
    if (value == "file") {
        if (marker) {
            return forward();
        }
        if (!rest.params.empty()) {
            const ExecResult r = port.execute(rest);
            if (r.rc != 0) {
                return r;
            }
        }
        FileBackend backend(startupDir); // the parked "daemon intent" ceases: stays in file mode
        if (auto s = backend.loadFull(); s && s->daemonIntent) {
            s->daemonIntent = false;
            backend.saveCurrent(*s);
        }
        return ExecResult{};
    }
    // mode:daemon
    if (marker) { // already in daemon mode: nothing to do, the other parameters go to the daemon
        return rest.params.empty() ? ExecResult{} : port.execute(rest);
    }
    if (!rest.params.empty()) {
        const ExecResult r = port.execute(rest);
        if (r.rc != 0) {
            return r;
        }
    }
    FileBackend backend(startupDir);
    DaemonMarker started;
    std::string err;
    if (!launchDaemon(startupDir, backend.loadFull().value_or(makeFreshSession()), started, err)) {
        return failed(err);
    }
    return ExecResult{};
}

bool onlyParams(const ParsedCommand& cmd, const std::string& name) {
    return !cmd.params.empty() &&
           std::all_of(cmd.params.begin(), cmd.params.end(), [&](const ParamToken& p) { return p.name == name; });
}

} // namespace

// `subterm get id:<x> textout timeout:<ms> idle:<ms>`: polling (client side; the daemon never blocks) until
// no new text has arrived for `idle` ms (default 100) or `timeout` ms passed (default: no limit). The text is
// collected over the polls (each poll empties the daemon's buffer) and printed as one `textout:` line.
bool wantsTextoutWait(const ParsedCommand& cmd) {
    return cmd.elementType == "subterm" && cmd.method == "get" && hasParam(cmd, "textout") &&
           (hasParam(cmd, "timeout") || hasParam(cmd, "idle"));
}

int runTextoutWait(ExecutionPort& engine, const ParsedCommand& cmd) {
    ParsedCommand poll = cmd;
    poll.params.clear();
    long timeoutMs = -1;
    long idleMs = 100;
    for (const ParamToken& p : cmd.params) {
        if (p.name == "timeout" || p.name == "idle") {
            char* end = nullptr;
            const long v = p.value ? std::strtol(p.value->c_str(), &end, 10) : -1;
            if (!p.value || p.value->empty() || *end != '\0' || v < 0 || v > 86400000L) {
                return fail("get", "-", "Invalid value: `" + p.name + ":" + p.value.value_or("") + "` (milliseconds)");
            }
            (p.name == "timeout" ? timeoutMs : idleMs) = v;
        } else {
            poll.params.push_back(p);
        }
    }
    const bool onlyTextout = std::all_of(poll.params.begin(), poll.params.end(), [](const ParamToken& p) {
        return p.name == "id" || (p.name == "textout" && !p.value.has_value());
    });
    if (!onlyTextout) {
        return fail("get", "-", "`timeout:`/`idle:` can only be combined with `textout`");
    }
    using Clock = std::chrono::steady_clock;
    const auto start = Clock::now();
    auto lastData = start;
    std::string text;
    for (;;) {
        const ExecResult r = engine.execute(poll);
        if (r.rc != 0) {
            return emit(r);
        }
        std::string chunk = r.out;
        if (chunk.rfind("textout:", 0) == 0) {
            chunk.erase(0, 8);
        }
        if (!chunk.empty() && chunk.back() == '\n') {
            chunk.pop_back();
        }
        if (!chunk.empty()) {
            text += chunk;
            lastData = Clock::now();
        }
        const auto now = Clock::now();
        const auto since = [&](Clock::time_point t) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(now - t).count();
        };
        if (since(lastData) >= idleMs || (timeoutMs >= 0 && since(start) >= timeoutMs)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    text = "textout:" + text + "\n";
    std::fwrite(text.data(), 1, text.size(), stdout);
    return 0;
}

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    const std::string startupDir = fs::current_path().string();
    std::optional<DaemonMarker> marker; // a running daemon (the error handler uses it too)

    try {
        const ElementRegistry registry = makeBuiltinRegistry();
        FileBackend backend(startupDir);
        Engine localEngine(backend, registry);

        ParsedCommand cmd;
        const bool haveCmd = !args.empty();
        if (haveCmd) {
            cmd = registry.has(args[0]) ? parseCommandLine(args, registry.get(args[0]).freeTextParams)
                                        : parseCommandLine(args);
        }
        const bool isCore = haveCmd && cmd.elementType == "core";

        // The shape of the state: marker = running daemon; otherwise the flags of the (parked) file (core read, cheap).
        marker = readDaemonMarker(stateFilePath(startupDir));
        std::optional<Session> head;
        if (!marker) {
            head = backend.loadCore();
        }

        // Unacknowledged abnormal exit: every call gives an error, except the acknowledgement, the query and `destroy`.
        if (head && !head->abnormal.empty()) {
            const std::optional<std::string> mv = isCore ? modeValue(cmd) : std::nullopt;
            const bool allowed = isCore && ((cmd.method == "get" && onlyParams(cmd, "abnormal")) ||
                                            (cmd.method == "set" && (hasParam(cmd, "ack") || mv == "destroy")));
            if (!allowed) {
                return fail("-", "-",
                            "The daemon stopped abnormally (`" + head->abnormal + "`); acknowledge with `core set ack:" +
                                head->abnormal + "`");
            }
        }
        if (isCore && cmd.method == "set" && hasParam(cmd, "ack")) {
            ParsedCommand rest = cmd;
            rest.params.clear();
            std::string reason;
            for (const ParamToken& p : cmd.params) {
                if (p.name == "ack") {
                    reason = p.value.value_or("");
                } else {
                    rest.params.push_back(p);
                }
            }
            if (!head || head->abnormal.empty()) {
                return fail("set", "core@terminal", "Nothing to acknowledge");
            }
            if (reason != head->abnormal) {
                return fail("set", "core@terminal",
                            "Wrong acknowledgement `ack:" + reason + "` (pending: `" + head->abnormal + "`)");
            }
            Session full = backend.loadFull().value();
            full.abnormal.clear();
            backend.saveCurrent(full);
            head->abnormal.clear();
            cmd = rest;
            if (cmd.params.empty()) {
                return 0;
            }
        }

        // Parked "daemon intent": at the next start the daemon starts by itself (unless the call itself
        // asks to stay in file mode or to destroy).
        const std::optional<std::string> mv = isCore && cmd.method == "set" ? modeValue(cmd) : std::nullopt;
        if (head && head->daemonIntent && head->abnormal.empty() && !(mv == "file" || mv == "destroy")) {
            DaemonMarker started;
            std::string err;
            if (!launchDaemon(startupDir, backend.loadFull().value(), started, err)) {
                return fail("-", "-", err);
            }
            marker = started;
            head.reset();
        }

        // In daemon mode every execution goes to the daemon over the socket; otherwise in place.
        std::optional<RemotePort> remote;
        if (marker) {
            remote.emplace(socketTransport(marker->socketPath));
        }
        ExecutionPort& engine = marker ? static_cast<ExecutionPort&>(*remote) : localEngine;

        if (!haveCmd) {
            return emit(engine.redraw());
        }

        // The blocking function calls are the FIRST parameters after `core get` (without a value).
        if (cmd.method == "get" && cmd.elementType == "core" && !cmd.params.empty() &&
            !cmd.params[0].value.has_value()) {
            if (cmd.params[0].name == "keypress") {
                return runKeypress(engine, cmd);
            }
            if (cmd.params[0].name == "keyboard") {
                return runKeyboard(engine, cmd);
            }
        }
        if (isCore) {
            if (cmd.method == "get" && (hasParam(cmd, "mode") || hasParam(cmd, "socket") || hasParam(cmd, "abnormal"))) {
                return emit(getLocalCoreParams(cmd, marker, head));
            }
            if (cmd.method == "set") {
                if (const auto done = setModeCommand(engine, localEngine, cmd, startupDir, marker)) {
                    return emit(*done);
                }
            }
        }
        if (wantsTextoutWait(cmd)) {
            return runTextoutWait(engine, cmd);
        }
        return emit(engine.execute(cmd));
    } catch (const DaemonGone& e) {
        return fail("-", "-", marker ? reapDeadDaemon(stateFilePath(startupDir), *marker)
                                     : std::string("The daemon is gone: ") + e.what());
    } catch (const DaemonUnresponsive& e) {
        return fail("-", "-", marker ? reapDeadDaemon(stateFilePath(startupDir), *marker)
                                     : std::string("The daemon does not respond: ") + e.what());
    } catch (const ToytuiException& e) {
        std::fputs((e.err.format() + "\n").c_str(), stderr);
        return 1;
    }
}
