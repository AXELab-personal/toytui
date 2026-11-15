// execution_port.h — the interface for executing commands, through which the client (main.cpp) works.
// It has two implementations: the `Engine` (in place, in the same process) and the `RemotePort`
// (daemon_protocol.h: the request goes to the daemon, where the same Engine does the execution). The interface
// does not throw: errors arrive in the `ExecResult`.
#pragma once

#include <optional>
#include <string>

#include "cli_parser.h"

namespace toytui {

struct ExecResult {
    int rc = 0;
    std::string out; // bytes destined for stdout (with line ends)
    std::string err; // bytes destined for stderr (with line ends)
};

class ExecutionPort {
public:
    virtual ~ExecutionPort() = default;

    // Call without parameters: full redraw of the stored state (not a diff).
    virtual ExecResult redraw() = 0;

    // `new`/`set`/`get`/`del` (except the blocking `core get keypress`/`keyboard`).
    virtual ExecResult execute(const ParsedCommand& cmd) = 0;

    // `core get keypress` AFTER the key was read: `token` is the key notation, nullopt = no key.
    virtual ExecResult keypress(const std::optional<std::string>& token) = 0;

    // `core get keyboard`: starting the loop (the focus; `wantDeclared`: whether the element declares its own exit
    // keys), one step per key, then the closing (cursor reset).
    struct KeyboardStart {
        std::string focus;
        bool elementExits = false;
        ExecResult status; // rc != 0: starting failed, the loop cannot begin
    };
    virtual KeyboardStart keyboardStart(bool wantDeclared) = 0;
    // Delivering one keystroke. If the element received its exit key, `exitLine` is filled in.
    virtual ExecResult keyboardKey(const std::string& startFocus, const std::string& token,
                                   std::string& exitLine) = 0;
    virtual ExecResult keyboardEnd(const std::string& startFocus) = 0;
};

} // namespace toytui
