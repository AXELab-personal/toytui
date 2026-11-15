#include "subterm_element.h"

#include <unistd.h>

#include <algorithm>
#include <climits>
#include <csignal>

#include "ansi_handler.h"
#include "error.h"
#include "input_ops.h"
#include "param_spec.h"
#include "param_types.h"

namespace toytui {

namespace {

constexpr std::size_t kCmdLimit = 64 * 1024;
constexpr std::size_t kCwdLimit = 4096;
constexpr std::size_t kSendLimit = 512 * 1024;
constexpr int kMaxScrollback = 100000;

const SpecTable& subtermSpecs() {
    static const SpecTable t = concatSpecs(
        kGeneralSpecs,
        {{"cmd", Kind::Text, false, nullptr, kCmdLimit},
         {"cwd", Kind::Text, false, nullptr, kCwdLimit},
         {"scrollback", Kind::Int, false, "0", 0, nullptr, nullptr, 0, kMaxScrollback},
         {"keys_exit", Kind::Text, false, nullptr},
         {"exec", Kind::Enum, false, nullptr, 0, nullptr, nullptr, 0, 0, "on=start;off=stop"},
         {"pid", Kind::Int, true, nullptr, 0, nullptr, nullptr, 0, INT_MAX},
         {"exit_code", Kind::Int, true, nullptr, 0, nullptr, nullptr, 0, 1000},
         {"textout", Kind::Bool, false, nullptr, 0, nullptr, nullptr, 0, 0, nullptr, true},
         {"send", Kind::Text, false, nullptr, kSendLimit, nullptr, nullptr, 0, 0, nullptr, true},
         {"signal", Kind::Enum, false, nullptr, 0, nullptr, nullptr, 0, 0, "hup;int;term;kill", true}});
    return t;
}

std::string fullIdOf(const ElementParams& p) { return "subterm@" + p.text("id", "new"); }

void checkKeysExit(ElementParams& p, const std::string& method, const std::string& target) {
    const std::string* v = p.getSet("keys_exit");
    if (v == nullptr) {
        p.set("keys_exit", "[CTRL-END]");
        return;
    }
    if (v->empty()) {
        return; // no declared exit key
    }
    const auto keys = parseExitKeys(*v);
    if (!keys) {
        throwError(method, target, "Invalid key list (expected one or more keys, e.g. `[CTRL-END]`): `keys_exit:" + *v + "`");
    }
    p.set("keys_exit", joinKeys(*keys));
}

[[noreturn]] void needDaemon(const std::string& method, const std::string& target) {
    throwError(method, target, "`subterm` requires daemon mode");
}

ElementParams defaults() {
    ElementParams p;
    p.set("parent", "core@terminal");
    p.set("posX", "0");
    p.set("posY", "0");
    p.set("z_index", "1");
    p.set("cmd", "");
    char buf[PATH_MAX];
    p.set("cwd", ::getcwd(buf, sizeof buf) != nullptr ? std::string(buf) : std::string("/"));
    p.set("scrollback", "0");
    p.set("keys_exit", "[CTRL-END]");
    return p;
}

int signalOf(const std::string& name) {
    return name == "hup" ? SIGHUP : name == "int" ? SIGINT : name == "term" ? SIGTERM : SIGKILL;
}

// Converts the runtime markers of a `new`/`set` call (exec, textout, send, signal) into recorded effects and
// removes them; all rules are checked here, before anything is executed.
void resolve(SubtermRuntime* host, ElementParams& p) {
    static const char* const kMarkers[] = {"exec", "textout", "send", "signal", "pid", "exit_code"};
    const std::string full = fullIdOf(p);
    const bool any = std::any_of(std::begin(kMarkers), std::end(kMarkers),
                                 [&](const char* m) { return p.find(m) != nullptr; });
    if (!any) {
        return;
    }
    if (host == nullptr) {
        needDaemon("set", full);
    }
    for (const char* ro : {"pid", "exit_code"}) {
        if (p.find(ro) != nullptr) {
            throwError("set", full, std::string("Read-only parameter: `") + ro + "`");
        }
    }
    const std::string* textout = p.getSet("textout");
    const std::string* exec = p.getSet("exec");
    const std::string* send = p.getSet("send");
    const std::string* signal = p.getSet("signal");

    if (textout != nullptr) {
        host->planTextout(full, *textout == "on");
    }
    bool starting = false;
    bool stopping = false;
    if (exec != nullptr) {
        const SubtermRuntime::Config cfg{p.text("cmd"), p.text("cwd"), paramInt(p, "scrollback", 0)};
        if (*exec == "on") {
            if (cfg.cmd.empty()) {
                throwError("set", full, "Nothing to run: `cmd` is empty");
            }
            if (host->running(full)) {
                if (!(*host->launched(full) == cfg)) {
                    throwError("set", full,
                               "The program is running with other `cmd`/`cwd`/`scrollback`; stop it first (`exec:stop`)");
                }
            } else {
                if (host->runningCount() >= SubtermRuntime::kMaxInstances) {
                    throwError("set", full, "Too many running programs (maximum " +
                                                std::to_string(SubtermRuntime::kMaxInstances) + ")");
                }
                host->planStart(full, cfg);
                starting = true;
            }
        } else if (host->running(full)) {
            host->planStop(full);
            stopping = true;
        }
    }
    const bool alive = (host->running(full) || starting) && !stopping;
    if (send != nullptr) {
        if (!alive) {
            throwError("set", full, "Cannot `send`: the program is not running");
        }
        host->planSend(full, *send);
    }
    if (signal != nullptr) {
        if (!(host->running(full) && !stopping)) {
            throwError("set", full, "Cannot `signal`: the program is not running");
        }
        host->planSignal(full, signalOf(*signal));
    }
    for (const char* m : kMarkers) {
        p.remove(m);
    }
}

} // namespace

std::pair<int, int> subtermSize(const ElementTree& tree, const ElementRegistry& registry, const std::string& fullId) {
    const ElementParams& p = tree.find(fullId)->params;
    const auto dim = [&](const char* name, const char* pos, bool height) {
        int v = 1;
        if (const auto w = paramInt(p, name)) {
            v = *w;
        } else if (const auto pc = parentContentSize(tree, registry, p, height)) {
            v = *pc - std::max(0, paramInt(p, pos, 0));
        }
        return std::clamp(v, 1, kMaxSizeValue);
    };
    return {dim("width", "posX", false), dim("height", "posY", true)};
}

ElementKind subtermElementKind(SubtermRuntime* host) {
    ElementKind k;
    k.freeTextParams = {};
    k.paramSpecs = subtermSpecs();
    k.derivedParams = {"exec", "pid", "exit_code"};

    k.create = [host](const ParsedCommand& c) {
        const std::string target = elementTarget("subterm", c.params);
        if (host == nullptr) {
            needDaemon("new", target);
        }
        ElementParams p = defaults();
        applyParams(p, c.params, subtermSpecs(), true, target);
        checkKeysExit(p, "new", target);
        return p;
    };
    k.set = [](ElementParams& p, const ParsedCommand& c) {
        const std::string target = elementTarget("subterm", p);
        applyParams(p, c.params, subtermSpecs(), false, target);
        checkKeysExit(p, "set", target);
    };
    k.resolveDerived = [host](const ElementTree&, const ElementRegistry&, ElementParams& p) { resolve(host, p); };
    k.deriveParams = [host](const ElementTree&, const ElementRegistry&, const std::string& full, ElementParams& view) {
        const bool run = host != nullptr && host->running(full);
        view.set("exec", run ? "on" : "off");
        const auto pid = host != nullptr ? host->pid(full) : std::nullopt;
        const auto code = host != nullptr ? host->exitCode(full) : std::nullopt;
        if (pid) view.set("pid", std::to_string(*pid)); else view.markUnset("pid");
        if (code) view.set("exit_code", std::to_string(*code)); else view.markUnset("exit_code");
    };
    k.get = [host](const ElementParams& p, const ParsedCommand& c) {
        const std::string target = elementTarget("subterm", p);
        std::vector<std::string> lines;
        const bool all = c.params.size() == 1 && c.params[0].name == "all" && !c.params[0].value.has_value();
        if (all) {
            return getParams(p, c.params, subtermSpecs(), target);
        }
        for (const ParamToken& t : c.params) {
            if (t.name == "textout" && !t.value.has_value()) { // the text since `textout:on`; reading empties it
                lines.push_back("textout:" + (host != nullptr ? host->takeTextout(fullIdOf(p)) : std::string()));
                continue;
            }
            for (auto& l : getParams(p, {t}, subtermSpecs(), target)) {
                lines.push_back(std::move(l));
            }
        }
        return lines;
    };

    k.contentArea = [](const ElementTree&, const std::string&, const Block&) { return std::optional<Rect>(); };
    k.ownVisual = [](const ElementTree&, const std::string&) { return Block{}; };
    k.measure = [](const ElementTree& t, const ElementRegistry& reg, const std::string& id) {
        return subtermSize(t, reg, id);
    };
    k.ownVisualWindow = [host](const ElementTree& t, const ElementRegistry& reg, const std::string& id,
                               const Rect& win) {
        const VtermScreen* screen = host != nullptr ? host->screen(id) : nullptr;
        const Block src = screen != nullptr ? screen->block() : Block{};
        Block out;
        out.width = win.width;
        out.height = win.height;
        out.cells.assign(static_cast<std::size_t>(win.width) * static_cast<std::size_t>(win.height),
                         neutralBlankCell());
        for (int y = 0; y < win.height; ++y) {
            for (int x = 0; x < win.width; ++x) {
                const int sx = win.x + x;
                const int sy = win.y + y;
                if (sx >= 0 && sy >= 0 && sx < src.width && sy < src.height) {
                    out.at(x, y) = src.at(sx, sy);
                }
            }
            repairWidePairs(&out.at(0, y), out.width);
        }
        (void)reg;
        resolveUnsetCells(out.cells, t.resolveInheritableColor(id, "bgcol"), t.resolveInheritableColor(id, "fgcol"),
                          static_cast<std::uint16_t>(t.resolveInheritableInt(id, "attrib") & 0x7F));
        return out;
    };
    k.cursorSpot = [host](const ElementTree&, const std::string& id) -> std::optional<ElementKind::CursorSpot> {
        const VtermScreen* screen = host != nullptr ? host->screen(id) : nullptr;
        if (screen == nullptr || !host->running(id)) {
            return std::nullopt;
        }
        const VtermScreen::Cursor c = screen->cursor();
        if (!c.visible || c.row < 0 || c.col < 0 || c.row >= screen->rows() || c.col >= screen->cols()) {
            return std::nullopt;
        }
        return ElementKind::CursorSpot{c.col, c.row, c.shape};
    };
    k.onKey = [host](ElementTree&, const ElementRegistry&, const std::string& id, const std::string& key) {
        if (host != nullptr && host->running(id)) {
            host->planKey(id, key); // executed by the commit; the echo arrives asynchronously
        }
        return ElementKind::KeyResult(false);
    };
    k.exitKeys = [](const ElementTree& t, const std::string& id) {
        const std::string* v = t.find(id)->params.getSet("keys_exit");
        const auto keys = v != nullptr ? parseExitKeys(*v) : std::nullopt;
        return keys ? *keys : std::vector<std::string>();
    };
    k.onExit = [](ElementTree&, const ElementRegistry&, const std::string&, const std::string&) {
        return ElementKind::ExitResult{};
    };
    return k;
}

} // namespace toytui
