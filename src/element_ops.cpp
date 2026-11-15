#include "element_ops.h"

#include <utility>

#include "error.h"
#include "garbage.h"
#include "param_spec.h"

namespace toytui {

namespace {

const ParamToken* findId(const ParsedCommand& cmd) {
    for (const auto& p : cmd.params) {
        if (p.name == "id") {
            return &p;
        }
    }
    return nullptr;
}

// The short form of the `id:` value; for a full form it checks the type.
std::string shortIdOf(const ParsedCommand& cmd, const std::string& value) {
    const auto at = value.find('@');
    if (at == std::string::npos) {
        return value;
    }
    if (value.substr(0, at) != cmd.elementType) {
        throwError(cmd.method, "-",
             "Element type mismatch: `id:" + value + "` (call is for `" + cmd.elementType + "`)");
    }
    return value.substr(at + 1);
}

// Removes the `id:value` token that identifies the target; the bare `id` (query:
// "give back the id") stays.
ParsedCommand withoutId(const ParsedCommand& cmd) {
    ParsedCommand c = cmd;
    c.params.clear();
    for (const auto& p : cmd.params) {
        if (!(p.name == "id" && p.value.has_value())) {
            c.params.push_back(p);
        }
    }
    return c;
}

// `method` is intentionally const char*: next to a temporary std::string
// parameter GCC would report a (false) dangling-reference error.
const ElementNode& existing(const ElementTree& tree, const char* method,
                             const std::string& fullId) {
    const ElementNode* node = tree.find(fullId);
    if (node == nullptr) {
        throwError(method, fullId, "Element does not exist: `" + fullId + "`");
    }
    return *node;
}

} // namespace

std::string targetFullId(const ParsedCommand& cmd) {
    if (cmd.elementType == "core") {
        return "core@terminal";
    }
    const ParamToken* id = findId(cmd);
    if (id == nullptr || !id->value.has_value()) {
        throwError(cmd.method, "-", "Missing required parameter: `id`");
    }
    return cmd.elementType + "@" + shortIdOf(cmd, *id->value);
}

std::string execNew(ElementTree& tree, const ElementRegistry& registry, const ParsedCommand& cmd) {
    const ElementKind& kind = registry.get(cmd.elementType);
    if (!kind.create) {
        throwError("new", "-", "`" + cmd.elementType + "` has no `new` method");
    }
    // The stored `id` is always the short form.
    ParsedCommand c = cmd;
    for (auto& p : c.params) {
        if (p.name == "id" && p.value.has_value()) {
            p.value = shortIdOf(cmd, *p.value);
        }
    }
    ElementParams created = kind.create(c);
    if (kind.resolveDerived) {
        kind.resolveDerived(tree, registry, created);
    }
    return createElement(tree, registry, cmd.elementType, created);
}

void execSet(ElementTree& tree, const ElementRegistry& registry, const ParsedCommand& cmd) {
    const std::string fullId = targetFullId(cmd);
    const ElementNode& node = existing(tree, "set", fullId);

    // 1) validation on a COPY — the store is not touched yet.
    ElementParams updated = node.params;
    registry.get(node.type).set(updated, cmd);
    if (const auto& resolve = registry.get(node.type).resolveDerived) {
        resolve(tree, registry, updated); // derived parameters (e.g. select `visualCursor`) are converted to stored state
    }
    if (const auto& check = registry.get(node.type).checkSet) {
        check(tree, fullId, updated); // type-specific, tree-level check (on failure: nothing is modified)
    }

    // 2) the parent-change checks (tree.setParent itself modifies only on
    // success).
    const std::string* np = updated.getSet("parent");
    if (np != nullptr && *np != node.parentFullId) {
        if (tree.exists(*np) && !registry.hostsChildren(tree, *np)) {
            throwError("set", fullId, "Parent cannot host children: `" + *np + "`");
        }
        tree.setParent(fullId, *np);
    }

    // 3) finalisation — from here on there can be no error.
    tree.findMut(fullId)->params = std::move(updated);
}

std::vector<std::string> getFromParams(const ElementRegistry& registry, const std::string& type,
                                        const ElementParams& params, const ParsedCommand& cmd) {
    return registry.get(type).get(params, withoutId(cmd));
}

std::vector<std::string> execGet(const ElementTree& tree, const ElementRegistry& registry,
                                  const ParsedCommand& cmd) {
    const std::string fullId = targetFullId(cmd);
    const ElementNode& node = existing(tree, "get", fullId);
    if (const auto& derive = registry.get(node.type).deriveParams) {
        ElementParams view = node.params; // copy: the derived values do not get into the tree
        derive(tree, registry, fullId, view);
        return getFromParams(registry, node.type, view, cmd);
    }
    return getFromParams(registry, node.type, node.params, cmd);
}

std::vector<std::string> execDel(ElementTree& tree, const ElementRegistry& registry,
                                  const ParsedCommand& cmd) {
    return collectGarbage(tree, registry, targetFullId(cmd));
}

namespace {

// CSV quoting (for Kind::Text values): following the existing CLI escape
// convention (`\"`, `\\`), so that quotes/backslashes
// remain unambiguously decodable too.
std::string csvQuote(const std::string& v) {
    std::string out = "\"";
    for (char c : v) {
        // One element = one line: newline/tab/CR are written escaped.
        if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            if (c == '"' || c == '\\') {
                out += '\\';
            }
            out += c;
        }
    }
    out += '"';
    return out;
}

} // namespace

std::vector<std::string> execParamList(const ElementTree& tree, const ElementRegistry& registry,
                                        const std::vector<ParamToken>& params) {
    bool wantAll = false;
    std::vector<std::string> names;
    for (const auto& p : params) {
        if (p.value.has_value()) {
            throwError("get", "core@terminal",
                 "Unexpected value for `param_list` argument: `" + p.name + "`");
        }
        if (p.name == "all") {
            wantAll = true;
        } else {
            names.push_back(p.name);
        }
    }
    if (params.empty()) {
        throwError("get", "core@terminal", "Missing parameter name(s) for `param_list` (or `all`)");
    }
    if (wantAll && params.size() != 1) {
        throwError("get", "core@terminal", "`all` must be the only argument to `param_list`");
    }

    // Type order: core first, the rest alphabetical (from the registry).
    const std::vector<std::string> typeOrder = registry.typeNames();

    std::vector<std::string> lines;
    for (const auto& type : typeOrder) {
        if (!registry.has(type)) {
            continue;
        }
        const SpecTable& specs = registry.get(type).paramSpecs;

        std::vector<std::string> effectiveNames;
        if (wantAll) {
            for (const auto& s : specs) {
                if (std::string(s.name) != "id" && !s.writeOnly) { // the line carries the id
                    effectiveNames.push_back(s.name);
                }
            }
        } else {
            effectiveNames = names;
        }

        for (const auto& id : tree.allIds()) {
            const ElementNode& node = *tree.find(id);
            if (node.type != type) {
                continue;
            }

            // Derived parameters (e.g. select `visualCursor`): we read from the view copy, like `get`.
            ElementParams view = node.params;
            if (const auto& derive = registry.get(type).deriveParams) {
                derive(tree, registry, id, view);
            }
            std::vector<std::pair<std::string, std::string>> plain, text;
            for (const auto& name : effectiveNames) {
                const ParamSpec* spec = findSpec(specs, name);
                if (spec == nullptr || spec->writeOnly) {
                    continue; // this type does not "have" it (or it is write-only)
                }
                const std::optional<std::string> value = paramView(view, *spec);
                const bool isSet = value.has_value();
                // "auto" (the unset marker) is NEVER quoted — only a
                // Kind::Text value that was ACTUALLY set is (otherwise it would be
                // indistinguishable from the literal text "auto").
                if (isTextKind(spec->kind) && isSet) {
                    text.emplace_back(name, csvQuote(*value));
                } else if (isTextKind(spec->kind)) {
                    text.emplace_back(name, "auto");
                } else {
                    plain.emplace_back(name, isSet ? *value : std::string("auto"));
                }
            }
            if (plain.empty() && text.empty()) {
                continue; // has none of the requested parameters -> omitted
            }

            std::string line = id;
            for (const auto& [name, value] : plain) {
                line += ";" + name + ":" + value;
            }
            for (const auto& [name, value] : text) { // Kind::Text: ALWAYS at the end of the line
                line += ";" + name + ":" + value;
            }
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

} // namespace toytui
