#include "button_element.h"

#include <utility>

#include "error.h"
#include "input_ops.h"
#include "label_element.h"
#include "param_spec.h"
#include "param_types.h"

namespace toytui {

namespace {

const SpecTable& buttonSpecs() {
    static const SpecTable t = concatSpecs(
        labelSpecTable(),
        {{"focus_bgcol", Kind::Color, true, nullptr},
         {"focus_fgcol", Kind::Color, true, nullptr},
         {"pressed_bgcol", Kind::Color, true, nullptr},
         {"pressed_fgcol", Kind::Color, true, nullptr},
         {"delay", Kind::Int, false, "150", 0, nullptr, nullptr, 0, kMaxButtonDelayMs},
         // an action, not state: not stored (buttonNew/Set remove the token), absent from get
         {"trigger", Kind::Bool, false, nullptr, 0, nullptr, nullptr, 0, 0, nullptr, true}});
    return t;
}

// Removing the `trigger` tokens (validated: on/off); they do not affect the stored state.
// Returns the tokens without them; `requested` = the value of the last trigger (on).
std::vector<ParamToken> withoutTrigger(const std::vector<ParamToken>& params, const std::string& method,
                                       const std::string& target, bool& requested) {
    std::vector<ParamToken> rest;
    rest.reserve(params.size());
    requested = false;
    for (const ParamToken& tok : params) {
        if (tok.name != "trigger") {
            rest.push_back(tok);
            continue;
        }
        const std::optional<bool> b = tok.value ? parseBoolValue(*tok.value) : std::nullopt;
        if (!b) {
            throw ToytuiException(ToytuiError{method, target, "Invalid `trigger` (expected on/off)"});
        }
        requested = *b;
    }
    return rest;
}

std::vector<ParamToken> withText(std::vector<ParamToken> params, const std::optional<std::string>& freeText) {
    if (freeText.has_value()) {
        params.push_back(ParamToken{"text_raw", *freeText});
    }
    return params;
}

ColorPair currentLook(const ElementTree& t, const std::string& id) {
    const ElementParams& p = t.find(id)->params;
    ColorPair look{t.resolveInheritableColor(id, "bgcol"), t.resolveInheritableColor(id, "fgcol")};
    if (currentFocus(t) == id) {
        look = applyStatePair(p, "focus", look, true);
    }
    if (p.isOn("pressed")) { // internal, transient (not stored) marker
        look = applyStatePair(p, "pressed", look, true);
    }
    return look;
}

} // namespace

ElementParams makeDefaultButtonParams() {
    ElementParams p = makeDefaultLabelParams();
    p.set("delay", "150");
    return p;
}

ElementParams buttonNew(const std::vector<ParamToken>& params, const std::optional<std::string>& freeText) {
    ElementParams p = makeDefaultButtonParams();
    const std::string target = elementTarget("button", params);
    bool requested = false;
    const auto rest = withoutTrigger(params, "new", target, requested);
    if (requested) {
        throw ToytuiException(ToytuiError{"new", target, "`trigger:on` is only valid with `set`"});
    }
    applyParams(p, withText(rest, freeText), buttonSpecs(), true, target);
    return p;
}

void buttonSet(ElementParams& p, const std::vector<ParamToken>& params,
               const std::optional<std::string>& freeText) {
    const std::string target = elementTarget("button", p);
    bool requested = false;
    const auto rest = withoutTrigger(params, "set", target, requested);
    applyParams(p, withText(rest, freeText), buttonSpecs(), false, target);
}

ElementKind buttonElementKind() {
    ElementKind k = labelElementKind(); // basis of ownVisual/contentArea (leaf)
    k.ownVisual = [](const ElementTree& t, const std::string& id) {
        const ColorPair look = currentLook(t, id);
        return buildLabelBlock(t, id, look.bg, look.fg);
    };
    k.create = [](const ParsedCommand& c) { return buttonNew(c.params, c.freeText); };
    k.set = [](ElementParams& p, const ParsedCommand& c) { buttonSet(p, c.params, c.freeText); };
    k.get = [](const ElementParams& p, const ParsedCommand& c) {
        return getParams(p, c.params, buttonSpecs(), elementTarget("button", p));
    };
    k.paramSpecs = buttonSpecs();
    // When focused, the received key's notation goes to the shell unmodified (the button's state does not change).
    k.onKey = [](ElementTree&, const ElementRegistry&, const std::string&, const std::string& key) {
        return ElementKind::KeyResult(false, key);
    };
    k.pulseMs = [](const ElementParams& updated, const ParsedCommand& c) -> std::optional<int> {
        bool on = false;
        for (const ParamToken& tok : c.params) {
            if (tok.name == "trigger") {
                on = tok.value && parseBoolValue(*tok.value).value_or(false);
            }
        }
        if (!on) {
            return std::nullopt;
        }
        return paramInt(updated, "delay", 150);
    };
    return k;
}

} // namespace toytui
