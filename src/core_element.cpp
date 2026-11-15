#include "core_element.h"

#include <algorithm>
#include <filesystem>
#include <unistd.h>

#include "cli_parser.h"
#include "error.h"
#include "param_types.h"
#include "renderer.h"
#include "terminal_size.h"

namespace toytui {

namespace {

constexpr int kMinSize = 1;
constexpr int kMaxSize = 512; // every element is at most 512x512

// The SINGLE source of the core's parameters: the order of `get all` /
// `param_list`, the known names, the system defaults (`auto`
// and cold start). The core has no unset: it is not `unsettable`; `auto`
// goes to the `fixedDefault` (the default of width/height is the physical
// terminal size, see coreSet). The handlers are hand-written (a documented
// exception: the core also triggers function calls). `keypress`/
// `keyboard` never go here (they are not parameters).
const SpecTable& coreSpecs() {
    static const SpecTable specs = {
        {"width", Kind::Size, false, "80"},   {"height", Kind::Size, false, "25"},
        {"bgcol", Kind::Color, false, "0"},   {"fgcol", Kind::Color, false, "15"},
        {"attrib", Kind::Attrib, false, "0"}, {"tty", Kind::Text, false, "/dev/tty"},
        {"path", Kind::Text, false, "."},     {"setfocus", Kind::Text, false, "auto"},
        {"term_cursor", Kind::Bool, false, "on"},
    };
    return specs;
}

const std::vector<std::string>& coreAllOrder() {
    static const std::vector<std::string> order = [] {
        std::vector<std::string> o;
        for (const auto& sp : coreSpecs()) {
            o.push_back(sp.name);
        }
        return o;
    }();
    return order;
}

} // namespace

const std::vector<std::string>& coreParamNames() { return coreAllOrder(); }

ElementParams makeDefaultCoreParams() {
    ElementParams p;
    // Cold start: the system defaults of the table (attrib: inheritable like the
    // colours, the root ALWAYS has a concrete value;
    // setfocus `auto` = no focus).
    for (const auto& sp : coreSpecs()) {
        p.set(sp.name, sp.fixedDefault);
    }
    return p;
}

std::vector<std::string> coreGet(const ElementParams& core,
                                  const std::vector<ParamToken>& params) {
    std::vector<std::string> lines;

    auto emit = [&](const std::string& name) {
        const ParamValue* v = core.find(name);
        if (v == nullptr || v->state != ParamState::Set) {
            // The current minimal set of core parameters is always Set
            // — if this ever did not hold, it would be an internal error, not a
            // user error.
            throw ToytuiException(
                ToytuiError{"get", "core@terminal",
                            "Internal: parameter `" + name +
                                "` unexpectedly unset"});
        }
        lines.push_back(name + ":" + v->value);
    };

    if (params.size() == 1 && params[0].name == "all" &&
        !params[0].value.has_value()) {
        for (const auto& name : coreAllOrder()) {
            emit(name);
        }
        return lines;
    }

    for (const auto& p : params) {
        if (p.value.has_value()) {
            throw ToytuiException(
                ToytuiError{"get", "core@terminal",
                            "Unexpected value for query parameter: `" +
                                p.name + "`"});
        }
        const auto& order = coreAllOrder();
        if (std::find(order.begin(), order.end(), p.name) == order.end()) {
            throw ToytuiException(ToytuiError{
                "get", "core@terminal", "Unknown parameter: `" + p.name + "`"});
        }
        emit(p.name);
    }
    return lines;
}

void coreSet(ElementParams& core, const std::vector<ParamToken>& params) {
    struct PendingChange {
        std::string name;
        std::string value;
    };
    std::vector<PendingChange> pending;

    // `auto`: the core has NO parent and no unset — `auto` resets the
    // parameter to its SYSTEM DEFAULT (the same as the value of
    // makeDefaultCoreParams). Exception: width/height: the default is the
    // physical terminal size, with 80x25 as a fallback — this is resolved
    // before the save step, knowing the final `tty`.
    static const char* const kAutoSize = "\x01auto-size";

    for (const auto& p : params) {
        if (p.value.has_value() && *p.value == "auto") {
            if (p.name == "width" || p.name == "height") {
                pending.push_back({p.name, kAutoSize});
                continue;
            }
            const ParamSpec* sp = findSpec(coreSpecs(), p.name);
            if (sp != nullptr && p.name != "setfocus") {
                pending.push_back({p.name, sp->fixedDefault});
                continue;
            }
            // `setfocus:auto` continues on its own (valid value) branch.
        }
        if (p.name == "width" || p.name == "height") {
            if (!p.value.has_value()) {
                throw ToytuiException(ToytuiError{
                    "set", "core@terminal",
                    "Missing value for `" + p.name + "`"});
            }
            auto n = parseStrictInt(*p.value);
            if (!n || *n < kMinSize || *n > kMaxSize) {
                throw ToytuiException(ToytuiError{
                    "set", "core@terminal",
                    "Out of range: `" + p.name + ":" + *p.value + "` (" +
                        std::to_string(kMinSize) + "-" +
                        std::to_string(kMaxSize) + ")"});
            }
            pending.push_back({p.name, std::to_string(*n)});
        } else if (p.name == "bgcol" || p.name == "fgcol") {
            if (!p.value.has_value()) {
                throw ToytuiException(ToytuiError{
                    "set", "core@terminal",
                    "Missing value for `" + p.name + "`"});
            }
            auto c = parseColorValue(*p.value);
            if (!c) {
                throw ToytuiException(ToytuiError{
                    "set", "core@terminal",
                    "Invalid color: `" + p.name + ":" + *p.value + "`"});
            }
            pending.push_back({p.name, std::to_string(static_cast<int>(*c))});
        } else if (p.name == "tty") {
            if (!p.value.has_value()) {
                throw ToytuiException(
                    ToytuiError{"set", "core@terminal",
                                "Missing value for `tty`"});
            }
            if (::access(p.value->c_str(), W_OK) != 0) {
                throw ToytuiException(ToytuiError{
                    "set", "core@terminal",
                    "Target not writable: `tty:" + *p.value + "`"});
            }
            pending.push_back({"tty", *p.value});
        } else if (p.name == "attrib") {
            if (!p.value.has_value()) {
                throw ToytuiException(
                    ToytuiError{"set", "core@terminal", "Missing value for `attrib`"});
            }
            auto a = parseAttribValue(*p.value);
            if (!a) {
                throw ToytuiException(ToytuiError{
                    "set", "core@terminal", "Invalid attrib: `attrib:" + *p.value + "`"});
            }
            pending.push_back({"attrib", std::to_string(static_cast<int>(*a))});
        } else if (p.name == "setfocus") {
            if (!p.value.has_value()) {
                throw ToytuiException(
                    ToytuiError{"set", "core@terminal", "Missing value for `setfocus`"});
            }
            const std::string& v = *p.value;
            const auto at = v.find('@');
            const bool formatOk = at != std::string::npos && at > 0 && at + 1 < v.size() &&
                                  v.find('@', at + 1) == std::string::npos &&
                                  isValidParamName(v.substr(0, at)) &&
                                  isValidParamName(v.substr(at + 1));
            if (v != "auto" && !formatOk) {
                throw ToytuiException(ToytuiError{
                    "set", "core@terminal",
                    "Invalid focus target (expected `type@id` or `auto`): `setfocus:" + v + "`"});
            }
            pending.push_back({"setfocus", v});
        } else if (p.name == "term_cursor") {
            if (!p.value.has_value()) {
                throw ToytuiException(
                    ToytuiError{"set", "core@terminal", "Missing value for `term_cursor`"});
            }
            const auto b = parseBoolValue(*p.value);
            if (!b) {
                throw ToytuiException(ToytuiError{
                    "set", "core@terminal",
                    "Invalid boolean (on/off): `term_cursor:" + *p.value + "`"});
            }
            pending.push_back({"term_cursor", *b ? "on" : "off"});
        } else if (p.name == "path") {
            if (!p.value.has_value()) {
                throw ToytuiException(
                    ToytuiError{"set", "core@terminal", "Missing value for `path`"});
            }
            namespace fs = std::filesystem;
            std::error_code ec;
            std::string abs = fs::absolute(*p.value, ec).lexically_normal().string();
            while (abs.size() > 1 && abs.back() == '/') {
                abs.pop_back();
            }
            if (ec || !fs::is_directory(abs, ec) || ::access(abs.c_str(), W_OK | X_OK) != 0) {
                throw ToytuiException(ToytuiError{
                    "set", "core@terminal",
                    "Not a writable directory: `path:" + *p.value + "`"});
            }
            pending.push_back({"path", abs});
        }
        // Every other parameter is not interpreted here -> silently dropped.
    }

    // The final value of `tty` (the last one given in the call, otherwise the current one).
    std::string effectiveTty = core.find("tty")->value;
    for (const auto& c : pending) {
        if (c.name == "tty") {
            effectiveTty = c.value;
        }
    }
    for (auto& c : pending) {
        if (c.value == kAutoSize) {
            const auto size = queryTerminalSize(effectiveTty);
            const int fallback = c.name == "width" ? 80 : 25;
            const int v = size ? (c.name == "width" ? size->first : size->second) : fallback;
            c.value = std::to_string(std::clamp(v, kMinSize, kMaxSize));
        }
    }
    for (auto& c : pending) {
        core.set(c.name, c.value);
    }
}

ElementKind coreElementKind() {
    ElementKind k;
    // core@terminal has no parent of its own, so resolveInheritable
    // always returns its own (always Set) value directly —
    // the same helper can be used as for child elements.
    k.ownVisual = [](const ElementTree& t, const std::string& id) {
        Block b;
        b.width = t.resolveInheritableInt(id, "width");
        b.height = t.resolveInheritableInt(id, "height");
        b.cells = makeBlankScreen(b.width, b.height, t.resolveInheritableColor(id, "bgcol"),
                                   t.resolveInheritableColor(id, "fgcol"));
        return b;
    };
    k.contentArea = [](const ElementTree&, const std::string&, const Block& own) {
        return std::optional<Rect>(Rect{0, 0, own.width, own.height});
    };
    k.contentWidth = [](const ElementTree& t, const std::string& id) {
        return std::optional<int>(t.resolveInheritableInt(id, "width"));
    };
    k.contentHeight = [](const ElementTree& t, const std::string& id) {
        return std::optional<int>(t.resolveInheritableInt(id, "height"));
    };
    // core@terminal: no `new` (`create` stays nullptr),
    // get/set on the implicit single instance.
    k.set = [](ElementParams& p, const ParsedCommand& c) { coreSet(p, c.params); };
    k.get = [](const ElementParams& p, const ParsedCommand& c) { return coreGet(p, c.params); };
    k.paramSpecs = coreSpecs(); // `param_list` + `auto` defaults (one source)
    // The focus target must EXIST (whether it can handle keys is
    // not examined, but setting it to a non-existent element is meaningless).
    k.checkSet = [](const ElementTree& t, const std::string&, const ElementParams& updated) {
        const std::string* f = updated.getSet("setfocus");
        if (f != nullptr && *f != "auto" && !t.exists(*f)) {
            throw ToytuiException(ToytuiError{
                "set", "core@terminal", "Focus target does not exist: `setfocus:" + *f + "`"});
        }
    };
    // If the focused element was deleted, the focus automatically becomes `auto`.
    k.onElementsDeleted = [](ElementTree& t, const std::string& self,
                             const std::vector<std::string>& deleted) {
        ElementParams& p = t.findMut(self)->params;
        const std::string* f = p.getSet("setfocus");
        if (f != nullptr && std::find(deleted.begin(), deleted.end(), *f) != deleted.end()) {
            p.set("setfocus", "auto");
        }
    };
    return k;
}

} // namespace toytui
