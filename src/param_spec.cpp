#include "param_spec.h"

#include <algorithm>

#include "error.h"
#include "param_types.h"

namespace toytui {

namespace {


// Splitting `canonical=alias1,alias2;...`; returns the canonical form, or an empty
// string if the value matches none of the items (case-sensitive).
std::string canonicalEnum(const char* choices, const std::string& v) {
    std::string spec = choices != nullptr ? choices : "";
    std::size_t pos = 0;
    while (pos <= spec.size()) {
        std::size_t end = spec.find(';', pos);
        if (end == std::string::npos) {
            end = spec.size();
        }
        const std::string item = spec.substr(pos, end - pos);
        const std::size_t eq = item.find('=');
        const std::string canon = item.substr(0, eq);
        if (v == canon) {
            return canon;
        }
        if (eq != std::string::npos) {
            std::size_t a = eq + 1;
            while (a <= item.size()) {
                std::size_t b = item.find(',', a);
                if (b == std::string::npos) {
                    b = item.size();
                }
                if (item.substr(a, b - a) == v) {
                    return canon;
                }
                a = b + 1;
            }
        }
        pos = end + 1;
    }
    return "";
}

// For the error message: listing the canonical forms and their aliases.
std::string enumAllowed(const char* choices) {
    std::string out;
    std::string spec = choices != nullptr ? choices : "";
    std::size_t pos = 0;
    while (pos <= spec.size()) {
        std::size_t end = spec.find(';', pos);
        if (end == std::string::npos) {
            end = spec.size();
        }
        std::string item = spec.substr(pos, end - pos);
        for (char& c : item) {
            if (c == '=') {
                c = '/';
            } else if (c == ',') {
                c = '/';
            }
        }
        if (!out.empty()) {
            out += ", ";
        }
        out += item;
        pos = end + 1;
    }
    return out;
}

[[noreturn]] void badValue(const std::string& method, const std::string& target,
                            const ParamSpec& s, const std::string& v, const std::string& why) {
    throwError(method, target, why + ": `" + s.name + ":" + v + "`");
}

} // namespace

const SpecTable kGeneralSpecs = {
    {"id", Kind::Id, false, nullptr},
    {"parent", Kind::Parent, false, "core@terminal"},
    {"posX", Kind::Signed, false, "0"},
    {"posY", Kind::Signed, false, "0"},
    {"width", Kind::Size, true, nullptr},
    {"height", Kind::Size, true, nullptr},
    {"bgcol", Kind::Color, true, nullptr},
    {"fgcol", Kind::Color, true, nullptr},
    {"attrib", Kind::Attrib, true, nullptr},
    {"z_index", Kind::ZIndex, false, "1"},
};

SpecTable concatSpecs(const SpecTable& a, const SpecTable& b) {
    SpecTable r = a;
    r.insert(r.end(), b.begin(), b.end());
    return r;
}

const ParamSpec* findSpec(const SpecTable& specs, const std::string& name) {
    for (const auto& s : specs) {
        if (name == s.name) {
            return &s;
        }
    }
    return nullptr;
}

std::string canonicalize(const std::string& method, const std::string& target,
                          const ParamSpec& s, const std::string& v) {
    switch (s.kind) {
    case Kind::Id:
        return v;
    case Kind::Parent: {
        const auto at = v.find('@');
        const bool ok = at != std::string::npos && at > 0 && at + 1 < v.size() &&
                        v.find('@', at + 1) == std::string::npos &&
                        isValidParamName(v.substr(0, at)) &&
                        isValidParamName(v.substr(at + 1));
        if (!ok) {
            badValue(method, target, s, v, "Invalid parent (expected `type@id`)");
        }
        return v;
    }
    case Kind::Size: {
        auto n = parseStrictInt(v);
        const int hi = s.maxValue != 0 ? s.maxValue : kMaxSizeValue;
        if (!n || *n < s.minValue || *n > hi) {
            badValue(method, target, s, v,
                     "Out of range (" + std::to_string(s.minValue) + "-" + std::to_string(hi) + ")");
        }
        return std::to_string(*n);
    }
    case Kind::Signed: {
        auto n = parseStrictInt(v);
        if (!n || *n < -kMaxSignedValue || *n > kMaxSignedValue) {
            badValue(method, target, s, v,
                     "Out of range (-" + std::to_string(kMaxSignedValue) + ".." +
                         std::to_string(kMaxSignedValue) + ")");
        }
        return std::to_string(*n);
    }
    case Kind::Int: {
        auto n = parseStrictInt(v);
        if (!n || *n < s.minValue || *n > s.maxValue) {
            badValue(method, target, s, v,
                     "Out of range (" + std::to_string(s.minValue) + ".." +
                         std::to_string(s.maxValue) + ")");
        }
        return std::to_string(*n);
    }
    case Kind::Enum: {
        const std::string canon = canonicalEnum(s.choices, v);
        if (canon.empty()) {
            badValue(method, target, s, v,
                     "Invalid value (allowed: " + enumAllowed(s.choices) + ")");
        }
        return canon;
    }
    case Kind::ZIndex: {
        auto n = parseStrictInt(v);
        if (!n || *n < 1) {
            badValue(method, target, s, v, "Invalid z_index (must be >= 1)");
        }
        return std::to_string(*n);
    }
    case Kind::Color: {
        auto c = parseColorValue(v);
        if (!c) {
            badValue(method, target, s, v, "Invalid color");
        }
        return std::to_string(static_cast<int>(*c));
    }
    case Kind::Border:
        if (v == "0" || v == "off") {
            return "off";
        }
        if (v == "1" || v == "2") {
            return v;
        }
        badValue(method, target, s, v, "Invalid border (off/0, 1, 2)");
    case Kind::TitlePos:
        if (v == "left" || v == "0") {
            return "left";
        }
        if (v == "center" || v == "1") {
            return "center";
        }
        if (v == "right" || v == "2") {
            return "right";
        }
        badValue(method, target, s, v, "Invalid titlePos (left/0, center/1, right/2)");
    case Kind::Bool: {
        auto b = parseBoolValue(v);
        if (!b) {
            badValue(method, target, s, v, "Invalid boolean (on/off)");
        }
        return *b ? "on" : "off";
    }
    case Kind::TextView:
        return v; // derived view: the limit applies to the raw field
    case Kind::Text:
        {
            const std::size_t limit = s.maxLen != 0 ? s.maxLen : kDefaultTextLimit;
            if (v.size() > limit) {
                throwError(method, target,
                     "Text too long (" + std::to_string(v.size()) + " > " +
                         std::to_string(limit) + " bytes): `" + s.name + "`");
            }
        }
        return v;
    case Kind::Attrib: {
        auto a = parseAttribValue(v);
        if (!a) {
            badValue(method, target, s, v,
                     "Invalid attrib (binary 0b.../hex 0x.../named list)");
        }
        return std::to_string(static_cast<int>(*a));
    }
    }
    return v; // unreachable (-Wreturn-type)
}

void applyParams(ElementParams& elem, const std::vector<ParamToken>& params,
                  const SpecTable& specs, bool isNew, const std::string& target) {
    const std::string method = isNew ? "new" : "set";
    if (isNew) {
        const bool hasId = std::any_of(params.begin(), params.end(), [](const ParamToken& p) {
            return p.name == "id" && p.value.has_value();
        });
        if (!hasId) {
            throwError(method, target, "Missing required parameter: `id`");
        }
    }

    struct Change {
        std::string name;
        std::string value;
        bool unset;
    };
    std::vector<Change> pending;
    for (const auto& p : params) {
        const ParamSpec* spec = findSpec(specs, p.name);
        if (spec == nullptr) {
            continue; // not recognised -> silently dropped
        }
        if (!p.value.has_value()) {
            throwError(method, target, "Missing value for `" + p.name + "`");
        }
        // Derived view (e.g. `text`): writing it sets the RAW field,
        // the view is not stored — from here on the raw spec applies.
        if (spec->derive != nullptr) {
            const ParamSpec* rawSpec = findSpec(specs, spec->derivedFrom);
            if (rawSpec == nullptr) {
                continue;
            }
            spec = rawSpec;
        }
        if (!isNew && spec->kind == Kind::Id) {
            continue; // the id identifies, it is not modified by `set`
        }
        if (*p.value == "auto") {
            if (spec->unsettable) {
                pending.push_back({p.name, "", true});
                continue;
            }
            if (spec->fixedDefault != nullptr) {
                pending.push_back({p.name, spec->fixedDefault, false});
                continue;
            }
            if (!isTextKind(spec->kind)) {
                throwError(method, target, "`auto` is not applicable for `" + p.name + "`");
            }
            // Kind::Text: "auto" is the literal text -> it goes through the
            // normal canonicalize (which for Text only checks the
            // length limit and returns the text unchanged).
        }
        pending.push_back({spec->name, canonicalize(method, target, *spec, *p.value), false});
    }
    for (auto& c : pending) {
        if (c.unset) {
            elem.markUnset(c.name);
        } else {
            elem.set(c.name, std::move(c.value));
        }
    }
}

std::optional<std::string> paramView(const ElementParams& elem, const ParamSpec& spec) {
    const char* source = spec.derive != nullptr ? spec.derivedFrom : spec.name;
    const std::string* v = elem.getSet(source);
    if (v == nullptr) {
        return std::nullopt;
    }
    return spec.derive != nullptr ? spec.derive(*v) : *v;
}

std::vector<std::string> getParams(const ElementParams& elem,
                                    const std::vector<ParamToken>& params,
                                    const SpecTable& specs, const std::string& target) {
    std::vector<std::string> lines;
    auto emit = [&](const std::string& name) {
        const ParamSpec* spec = findSpec(specs, name);
        const std::optional<std::string> v = spec ? paramView(elem, *spec) : std::nullopt;
        lines.push_back(name + ":" + (v ? *v : std::string("auto")));
    };

    if (params.size() == 1 && params[0].name == "all" && !params[0].value.has_value()) {
        for (const auto& s : specs) {
            if (!s.writeOnly) {
                emit(s.name);
            }
        }
        return lines;
    }
    for (const auto& p : params) {
        if (p.value.has_value()) {
            throwError("get", target, "Unexpected value for query parameter: `" + p.name + "`");
        }
        const ParamSpec* spec = findSpec(specs, p.name);
        if (spec == nullptr) {
            throwError("get", target, "Unknown parameter: `" + p.name + "`");
        }
        if (spec->writeOnly) {
            throwError("get", target, "Write-only parameter: `" + p.name + "`");
        }
        emit(p.name);
    }
    return lines;
}

std::string elementTarget(const std::string& type, const std::vector<ParamToken>& params) {
    for (const auto& p : params) {
        if (p.name == "id" && p.value.has_value()) {
            return type + "@" + *p.value;
        }
    }
    return "-";
}

std::string elementTarget(const std::string& type, const ElementParams& elem) {
    const std::string* id = elem.getSet("id");
    return id != nullptr ? type + "@" + *id : "-";
}

} // namespace toytui
