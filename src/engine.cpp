#include "engine.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <thread>
#include <vector>

#include "compositor.h"
#include "core_element.h"
#include "diff.h"
#include "element_ops.h"
#include "error.h"
#include "focus_cursor.h"
#include "input_ops.h"
#include "keycodec.h"
#include "renderer.h"
#include "terminal_size.h"
#include "tty_input.h"

namespace toytui {

namespace {

void addLine(std::string& out, const std::string& s) {
    out += s;
    out += '\n';
}

ExecResult failResult(const std::string& method, const std::string& target, const std::string& msg) {
    ExecResult r;
    r.rc = 1;
    addLine(r.err, ToytuiError{method, target, msg}.format());
    return r;
}

ExecResult errorResult(const ToytuiException& e) {
    ExecResult r;
    r.rc = 1;
    addLine(r.err, e.err.format());
    return r;
}

Session freshSession() {
    Session s;
    s.tree.findMut("core@terminal")->params = makeDefaultCoreParams();
    return s;
}

// On every start we re-query the physical terminal size on the core's `tty`; if it differs from the stored one, the
// size of core@terminal is updated — the compositor cascades this automatically to EVERY element of unset size
// (resolveInheritable reads live, no separate child walk). Elements of explicit (non-unset) size
// do not change.
//
// Why it runs only on the mutating paths, not on `get`: `get` neither renders nor saves —
// PERSISTING a size change is a write operation that a purely querying call does not perform.
void applyResize(Session& session) {
    const ElementParams& core = session.tree.find("core@terminal")->params;
    const auto size = queryTerminalSize(core.find("tty")->value);
    if (!size) {
        return; // not a terminal (e.g. a regular file) -> nothing to do
    }
    const int cols = std::clamp(size->first, 1, 512);
    const int rows = std::clamp(size->second, 1, 512);
    if (std::to_string(cols) != core.find("width")->value ||
        std::to_string(rows) != core.find("height")->value) {
        ElementParams& mutableCore = session.tree.findMut("core@terminal")->params;
        mutableCore.set("width", std::to_string(cols));
        mutableCore.set("height", std::to_string(rows));
    }
}

// The current (stored) value of the core `path` parameter.
std::string corePath(const ElementTree& tree) {
    return tree.find("core@terminal")->params.text("path");
}

// The ONE-OFF action of the `core` `term_cursor`: it runs only if the `core set` call
// GAVE the parameter (not on every start, and it is not restored on exit either). The request is read AFTER the `set`, from the already valid
// (validated, to-be-saved) value and `tty`; the action runs AFTER a successful call (for a faulty call
// nothing happens). `auto` resets to the default (`on`).
struct CursorRequest {
    std::string tty;
    bool visible = true;
};

std::optional<CursorRequest> cursorRequestOf(const ParsedCommand& cmd, const ElementTree& tree) {
    if (cmd.elementType != "core" ||
        std::none_of(cmd.params.begin(), cmd.params.end(),
                     [](const ParamToken& p) { return p.name == "term_cursor"; })) {
        return std::nullopt;
    }
    const ElementParams& core = tree.find("core@terminal")->params;
    return CursorRequest{core.text("tty"), core.isOn("term_cursor")};
}

void applyCursorRequest(const std::optional<CursorRequest>& req) {
    if (req) {
        setTerminalCursor(req->tty, req->visible);
    }
}

// The terminal cursor of the focused element (input field): AFTER rendering it puts it at the image position and makes it visible
// (shape + position + `?25h`), on a terminal target only. On leaving/losing focus the shape is reset, and
// the `core` `term_cursor` decides the visibility.
bool coreTermCursorOn(const ElementTree& tree) {
    const ElementParams& core = tree.find("core@terminal")->params;
    return core.getSet("term_cursor") == nullptr || core.isOn("term_cursor");
}

std::string coreTty(const ElementTree& tree) {
    return tree.find("core@terminal")->params.find("tty")->value;
}

void showFocusCursor(const ElementTree& tree, const ElementRegistry& registry) {
    writeIfTerminal(coreTty(tree), cursorShowSequence(focusCursorOf(tree, registry)));
}

void resetCursorForFocus(SessionBackend& backend, const ElementRegistry& registry, const std::string& focusId) {
    if (focusId.empty() || focusId == "auto") {
        return;
    }
    const auto session = backend.loadFull();
    if (!session) {
        return;
    }
    const ElementNode* node = session->tree.find(focusId);
    if (node == nullptr || !registry.get(node->type).cursorSpot) {
        return;
    }
    writeIfTerminal(coreTty(session->tree), cursorResetSequence(coreTermCursorOn(session->tree)));
}

// Requesting a transient (non-stored) appearance after the `set` (ElementKind::pulseMs): the element gets the
// internal `pressed=on` marker in the tree while the rendered image is visible for `ms`.
struct Pulse {
    std::string fullId;
    int ms = 0;
};

// Loading/creating the whole tree + the requested modification + re-render + diff output + save —
// the shared path of `new`/`set`/`del`. `op` reports with the `changed` flag whether it actually modified anything
// (default: true) — if false there is NO redraw and save (e.g. a focused element dropped a key).
ExecResult mutateAndRender(SessionBackend& backend, const ElementRegistry& registry,
                           const std::function<std::string(ElementTree&, bool& changed)>& op,
                           const std::optional<Pulse>* pulse = nullptr, PulseState* pulseState = nullptr) {
    ExecResult res;
    if (pulseState != nullptr) {
        pulseState->due.reset(); // every modifying command closes a pending release (the new image contains it)
    }
    try {
        Session session = backend.loadFull().value_or(freshSession());
        applyResize(session);
        const bool hadSpot = focusCursorOf(session.tree, registry).hasSpot; // whether there was a cursor-bearing focus before the modification
        // A change of `path` is decided by comparing the core's REAL value before/after (not by the mere
        // presence of the raw tokens): a `box set ... path:X` (the box drops it) or a `core set path:` with the current
        // value does NOT move a data file.
        const std::string pathBefore = corePath(session.tree);
        bool changed = true;
        EffectSink* const effects = registry.effectSink();
        if (effects != nullptr) {
            effects->discard();
        }
        const std::string result = op(session.tree, changed); // empty if there is no stdout output (set/del)
        if (effects != nullptr) {
            effects->commit(session.tree, registry); // the recorded side effects, only after the operation succeeded
        }
        if (!changed) {
            if (!result.empty()) {
                addLine(res.out, result); // e.g. the echo of simulated keys without focus
            }
            return res;
        }

        const bool pathChanged = corePath(session.tree) != pathBefore;

        const std::string tty = session.tree.find("core@terminal")->params.find("tty")->value;
        std::optional<Block> onTerminal =
            session.hasScreen ? std::optional<Block>(session.lastScreen) : std::nullopt;
        std::optional<Block> pressedScreen; // only for a deferred pulse: the image remaining on the terminal
        if (pulse != nullptr && pulse->has_value() && session.tree.exists((*pulse)->fullId)) {
            // Transient state: drawing without saving, waiting, taking the marker back.
            session.tree.findMut((*pulse)->fullId)->params.set("pressed", "on");
            const Block pressed = composeScreen(session.tree, registry);
            writeToTty(tty, encodeDiff(onTerminal, pressed));
            onTerminal = pressed;
            if (pulseState != nullptr && pulseState->deferred) {
                pressedScreen = pressed;
                pulseState->due = std::chrono::steady_clock::now() + std::chrono::milliseconds((*pulse)->ms);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds((*pulse)->ms));
            }
            session.tree.findMut((*pulse)->fullId)->params.remove("pressed");
        }
        const Block screen = pressedScreen ? *pressedScreen : composeScreen(session.tree, registry);
        if (!pressedScreen) {
            writeToTty(tty, encodeDiff(onTerminal, screen));
        }
        {
            const FocusCursor fc = focusCursorOf(session.tree, registry);
            if (fc.hasSpot) {
                writeIfTerminal(tty, cursorShowSequence(fc));
            } else if (hadSpot) { // the cursor-bearing element lost the focus / was deleted: reset
                writeIfTerminal(tty, cursorResetSequence(coreTermCursorOn(session.tree)));
            }
        }

        session.hasScreen = true;
        session.lastScreen = screen;
        // A single save: on a path change to the new location (first the data, then the pointer, finally deleting the old file
        // — see session.h), otherwise to the current location.
        if (pathChanged) {
            backend.saveRelocated(corePath(session.tree), session);
        } else {
            backend.saveCurrent(session);
        }

        if (!result.empty()) {
            addLine(res.out, result);
        }
        return res;
    } catch (const ToytuiException& e) {
        return errorResult(e);
    }
}

// `core set keyboard:[SEQUENCE]`: simulated, non-blocking replay on the same code path (`deliverKey`)
// as one step of the loop. Any further `core set` parameters (e.g. `setfocus:`) take effect FIRST
// (in the same atomic transaction), the keys only afterwards — so
// `core set setfocus:box@b keyboard:[DOWN]` works in one call too. On an invalid notation / error
// nothing is modified. A single render + save at the end (the image is deterministic from the final state).
// Without focus (`auto`) the keys' notations go to stdout (as in the `keyboard`
// loop) — so a forgotten `setfocus` is immediately visible, not a silent no-op.
ExecResult runKeyboardReplay(SessionBackend& backend, const ElementRegistry& registry, const ParsedCommand& cmd) {
    ParsedCommand rest = cmd;
    rest.params.clear();
    std::vector<std::string> keys;
    for (const ParamToken& p : cmd.params) {
        if (p.name != "keyboard") {
            rest.params.push_back(p);
            continue;
        }
        if (!p.value.has_value()) {
            return failResult("set", "core@terminal", "Missing value for `keyboard`");
        }
        const auto parsed = parseKeySequence(*p.value);
        if (!parsed) {
            return failResult("set", "core@terminal", "Invalid key sequence: `keyboard:" + *p.value + "`");
        }
        keys.insert(keys.end(), parsed->begin(), parsed->end());
    }
    std::optional<CursorRequest> cursor;
    std::string exitFocus;
    ExecResult res = mutateAndRender(backend, registry, [&](ElementTree& t, bool& changed) {
        // `changed` comes from comparing the ACTUAL before/after values of the core's affected parameters (a silently
        // dropped / same-valued token does not force a needless render+save).
        const auto snapshot = [&]() {
            std::vector<std::optional<ParamValue>> v;
            const ElementParams& core = t.find("core@terminal")->params;
            for (const ParamToken& p : rest.params) {
                const ParamValue* x = core.find(p.name);
                v.push_back(x != nullptr ? std::optional<ParamValue>(*x) : std::nullopt);
            }
            return v;
        };
        const auto sameValue = [](const std::optional<ParamValue>& a, const std::optional<ParamValue>& b) {
            if (a.has_value() != b.has_value()) {
                return false;
            }
            return !a || (a->state == b->state && a->value == b->value);
        };
        bool setChanged = false;
        if (!rest.params.empty()) {
            const auto before = snapshot();
            execSet(t, registry, rest);
            cursor = cursorRequestOf(rest, t);
            const auto after = snapshot();
            for (std::size_t i = 0; i < before.size(); ++i) {
                setChanged = setChanged || !sameValue(before[i], after[i]);
            }
        }
        const KeyReplay r = replayKeys(t, registry, keys);
        changed = setChanged || r.changed;
        if (r.exited) {
            exitFocus = currentFocus(t);
        }
        return r.stdoutText;
    });
    if (res.rc == 0) {
        applyCursorRequest(cursor); // the one-off action of the given `term_cursor`, after the successful call
        if (!exitFocus.empty()) {
            try {
                resetCursorForFocus(backend, registry, exitFocus); // exit key: the cursor is reset
            } catch (const ToytuiException& e) {
                return errorResult(e);
            }
        }
    }
    return res;
}

// `get`: the non-blocking queries (`keypress`/`keyboard` are the caller's business).
ExecResult execGetCommand(SessionBackend& backend, const ElementRegistry& registry, const ParsedCommand& cmd) {
    ExecResult res;
    // For a non-core element via the index table, WITHOUT building the WHOLE tree — only the requested element's parameters
    // are read. A derived parameter (e.g. select `visualCursor`) needs knowledge of the tree: then (or for
    // `all`) we load the whole tree and use `execGet`.
    const auto needsTree = [&]() {
        const ElementKind& kind = registry.get(cmd.elementType);
        if (!kind.deriveParams) {
            return false;
        }
        return std::any_of(cmd.params.begin(), cmd.params.end(), [&](const ParamToken& p) {
            return p.name == "all" || std::find(kind.derivedParams.begin(), kind.derivedParams.end(), p.name) !=
                                          kind.derivedParams.end();
        });
    };
    const std::string noState = "No `toytui.state` found in `" + backend.location() + "`";
    if (cmd.elementType != "core" && needsTree()) {
        const std::string fullId = targetFullId(cmd);
        auto session = backend.loadFull();
        if (!session) {
            return failResult("get", fullId, noState);
        }
        applyResize(*session);
        for (const auto& line : execGet(session->tree, registry, cmd)) {
            addLine(res.out, line);
        }
        return res;
    }
    if (cmd.elementType != "core") {
        const std::string fullId = targetFullId(cmd);
        ElementParams params;
        switch (backend.loadElement(fullId, params)) {
            case ElementLookup::NoState:
                return failResult("get", fullId, noState);
            case ElementLookup::Missing:
                return failResult("get", fullId, "Element does not exist: `" + fullId + "`");
            case ElementLookup::Found:
                break;
        }
        for (const auto& line : getFromParams(registry, cmd.elementType, params, cmd)) {
            addLine(res.out, line);
        }
        return res;
    }
    // Function call: the FIRST parameter after `get`, without a value. If the first parameter is not a function,
    // a parameter query follows (unknown name -> error, e.g. `get bgcol keypress`).
    const bool firstIsBare = !cmd.params.empty() && !cmd.params[0].value.has_value();
    const bool isParamList = firstIsBare && cmd.params[0].name == "param_list";
    // `param_list` lists the whole tree; for a plain core query the core suffices (the header + the
    // core overrides, without reading the other elements).
    Session session = (isParamList ? backend.loadFull() : backend.loadCore()).value_or(freshSession());
    // `core get` (e.g. width/height) returns the LIVE physical terminal size even if there is no
    // `toytui.state` yet — only an IN-MEMORY update, it does NOT save (`get` never writes), no needless file creation.
    applyResize(session);

    if (isParamList) {
        const std::vector<ParamToken> rest(cmd.params.begin() + 1, cmd.params.end());
        for (const auto& line : execParamList(session.tree, registry, rest)) {
            addLine(res.out, line);
        }
        return res;
    }
    for (const auto& line : execGet(session.tree, registry, cmd)) {
        addLine(res.out, line);
    }
    return res;
}

} // namespace

Session makeFreshSession() { return freshSession(); }

// Drawing the pending released image (daemon, deferred pulse): the diff between the stored (pressed) image and the tree.
ExecResult Engine::tick() {
    return mutateAndRender(
        backend_, registry_, [](ElementTree&, bool&) { return std::string(); }, nullptr, &pulse_);
}

bool Engine::terminalResized() {
    try {
        const auto session = backend_.loadCore();
        if (!session) {
            return false;
        }
        const ElementParams& core = session->tree.find("core@terminal")->params;
        const auto size = queryTerminalSize(core.find("tty")->value);
        if (!size) {
            return false;
        }
        return std::to_string(std::clamp(size->first, 1, 512)) != core.find("width")->value ||
               std::to_string(std::clamp(size->second, 1, 512)) != core.find("height")->value;
    } catch (const ToytuiException&) {
        return false;
    }
}

void Engine::resetTerminalCursor() {
    try {
        const auto session = backend_.loadFull();
        if (session) {
            writeIfTerminal(coreTty(session->tree), cursorResetSequence(coreTermCursorOn(session->tree)));
        }
    } catch (const ToytuiException&) {
    }
}

ExecResult Engine::redraw() {
    // On a start without parameters the stored state is re-rendered IN FULL (not a diff!) — this call exists
    // precisely so that the terminal's whole content can be refreshed even without a "redraw" parameter (e.g.
    // if something else wrote into the terminal). A diff would give empty output (nothing changed
    // since the stored image), defeating the call's only purpose. If there is no state, an error.
    try {
        auto session = backend_.loadFull();
        if (!session) {
            return failResult("-", "-", "No `toytui.state` found in `" + backend_.location() + "`");
        }
        applyResize(*session);
        const Block screen = composeScreen(session->tree, registry_);
        const std::string ansi = encodeFullRepaint(screen.width, screen.height, screen.cells);
        writeToTty(coreTty(session->tree), ansi);
        showFocusCursor(session->tree, registry_);
        session->hasScreen = true;
        session->lastScreen = screen;
        backend_.saveCurrent(*session);
        return ExecResult{};
    } catch (const ToytuiException& e) {
        return errorResult(e);
    }
}

ExecResult Engine::execute(const ParsedCommand& cmd) {
    try {
        if (!registry_.has(cmd.elementType)) {
            return failResult(cmd.method, "-", "Unknown element type: `" + cmd.elementType + "`");
        }
        if (cmd.method == "get") {
            return execGetCommand(backend_, registry_, cmd);
        }
        if (cmd.method == "new") {
            return mutateAndRender(backend_, registry_,
                                   [&](ElementTree& t, bool&) { return execNew(t, registry_, cmd); },
                                   nullptr, &pulse_);
        }
        if (cmd.method == "set") {
            if (cmd.elementType == "core" &&
                std::any_of(cmd.params.begin(), cmd.params.end(),
                            [](const ParamToken& p) { return p.name == "keyboard"; })) {
                return runKeyboardReplay(backend_, registry_, cmd);
            }
            std::optional<Pulse> pulse;
            std::optional<CursorRequest> cursor;
            ExecResult res = mutateAndRender(
                backend_, registry_,
                [&](ElementTree& t, bool&) {
                    execSet(t, registry_, cmd);
                    cursor = cursorRequestOf(cmd, t);
                    const std::string id = targetFullId(cmd);
                    if (const auto& hook = registry_.get(t.find(id)->type).pulseMs) {
                        if (const auto ms = hook(t.find(id)->params, cmd)) {
                            pulse = Pulse{id, *ms};
                        }
                    }
                    return std::string();
                },
                &pulse, &pulse_);
            if (res.rc == 0) {
                applyCursorRequest(cursor); // the one-off action of the given `term_cursor`, after the successful call
            }
            return res;
        }
        if (cmd.method == "del") {
            return mutateAndRender(
                backend_, registry_,
                [&](ElementTree& t, bool&) {
                    execDel(t, registry_, cmd);
                    return std::string();
                },
                nullptr, &pulse_);
        }
        return failResult(cmd.method, "-", "Unknown method: `" + cmd.method + "` (new, set, get, del)");
    } catch (const ToytuiException& e) {
        return errorResult(e);
    }
}

// `core get keypress` after the key was read. No focus (`auto`): the key's notation goes to stdout
// (no state modification, no file write). With focus: the focused element gets it, and if it modifies,
// a redraw + save happens (stdout is silent). "No key" (noblock/timeout): `[NUL]` to stdout
// (without focus), with exit code 0 — so the caller can poll.
ExecResult Engine::keypress(const std::optional<std::string>& token) {
    try {
        // The focus is read from the STORED state (a read call: it does not save).
        const Session session = backend_.loadCore().value_or(freshSession());
        const bool stdoutMode = rawFocus(session.tree) == "auto";
        ExecResult res;
        if (!token) {
            if (stdoutMode) {
                addLine(res.out, kNoKeyToken);
            }
            return res;
        }
        if (stdoutMode) {
            addLine(res.out, *token);
            return res;
        }
        // Focused element: we deliver on the fresh state, on modification render + save.
        std::string exitFocus;
        res = mutateAndRender(backend_, registry_, [&](ElementTree& t, bool& changed) {
            const KeyDelivery d = deliverKey(t, registry_, *token);
            changed = d.changed;
            if (d.exited) {
                exitFocus = currentFocus(t);
            }
            return d.toStdout ? d.stdoutText : std::string(); // the element may request stdout output
        });
        if (res.rc == 0 && !exitFocus.empty()) {
            resetCursorForFocus(backend_, registry_, exitFocus); // exit key: the cursor is reset
        }
        return res;
    } catch (const ToytuiException& e) {
        return errorResult(e);
    }
}

Engine::KeyboardStart Engine::keyboardStart(bool wantDeclared) {
    KeyboardStart start;
    try {
        const Session initial = backend_.loadCore().value_or(freshSession());
        start.focus = rawFocus(initial.tree);
        if (wantDeclared && start.focus != "auto") {
            if (const auto full = backend_.loadFull()) {
                if (const ElementNode* n = full->tree.find(start.focus)) {
                    const ElementKind& kind = registry_.get(n->type);
                    start.elementExits = kind.exitKeys && !kind.exitKeys(full->tree, start.focus).empty();
                }
            }
        }
    } catch (const ToytuiException& e) {
        start.status = errorResult(e);
    }
    return start;
}

ExecResult Engine::keyboardKey(const std::string& startFocus, const std::string& token, std::string& exitLine) {
    return mutateAndRender(backend_, registry_, [&](ElementTree& t, bool& changed) {
        requireFocusUnchanged(t, startFocus);
        const KeyDelivery d = deliverKey(t, registry_, token);
        changed = d.changed; // the element's stdout request does not appear here
        if (d.exited) {
            exitLine = d.stdoutText;
        }
        return std::string();
    });
}

ExecResult Engine::keyboardEnd(const std::string& startFocus) {
    try {
        resetCursorForFocus(backend_, registry_, startFocus); // cursor-bearing element: reset at the end of the loop
    } catch (const ToytuiException& e) {
        return errorResult(e);
    }
    return ExecResult{};
}

} // namespace toytui
