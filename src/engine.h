// engine.h — executing commands on the session: load, modify, composite, render
// (writing `tty:`), save. The output does NOT go directly to stdout/stderr but into the `ExecResult`:
// the caller (main.cpp) decides what happens with it. Input (CLI processing, key reading) does not
// belong here. Session storage is provided by the `SessionBackend` (session_backend.h).
#pragma once

#include <chrono>
#include <optional>

#include "element_registry.h"
#include "execution_port.h"
#include "session_backend.h"

namespace toytui {

// State of pulse handling. By default the pulse BLOCKS (sleeps between the pressed and the released image); in `deferred`
// mode it does not sleep: the pressed image goes to the terminal and into the stored `lastScreen`, the released one is
// drawn by `Engine::tick()` at the `due` time (or by the next modifying command, if that arrives first).
struct PulseState {
    bool deferred = false;
    std::optional<std::chrono::steady_clock::time_point> due;
};

// An empty session with the core defaults (no stored state yet).
Session makeFreshSession();

class Engine : public ExecutionPort {
public:
    Engine(SessionBackend& backend, const ElementRegistry& registry) : backend_(backend), registry_(registry) {}

    ExecResult redraw() override;
    ExecResult execute(const ParsedCommand& cmd) override;
    ExecResult keypress(const std::optional<std::string>& token) override;
    KeyboardStart keyboardStart(bool wantDeclared) override;
    ExecResult keyboardKey(const std::string& startFocus, const std::string& token, std::string& exitLine) override;
    ExecResult keyboardEnd(const std::string& startFocus) override;

    // Deferred pulse (daemon): `set ... trigger:on` does not sleep, the released image is drawn by `tick()`.
    // `pulseDue()`: when it falls due (none if no release is pending); the caller's loop waits for this.
    void setDeferredPulse(bool on) { pulse_.deferred = on; }
    std::optional<std::chrono::steady_clock::time_point> pulseDue() const { return pulse_.due; }
    ExecResult tick();
    // Whether the physical size of `tty` differs from the stored `width`/`height` (the daemon's periodic check; then
    // `tick()` redraws with the new size). Not a terminal / no state: false.
    bool terminalResized();
    // Reset the terminal cursor (the content is untouched): when the session is destroyed.
    void resetTerminalCursor();

private:
    SessionBackend& backend_;
    const ElementRegistry& registry_;
    PulseState pulse_;
};

} // namespace toytui
