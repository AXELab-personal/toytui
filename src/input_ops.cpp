#include "input_ops.h"

#include <algorithm>

#include "error.h"
#include "keycodec.h"

namespace toytui {

std::string rawFocus(const ElementTree& tree) {
    return tree.find("core@terminal")->params.text("setfocus", "auto");
}

std::string currentFocus(const ElementTree& tree) {
    const std::string f = rawFocus(tree);
    if (f == "auto" || !tree.exists(f)) {
        return "auto";
    }
    return f;
}

std::string joinKeys(const std::vector<std::string>& keys) {
    std::string s;
    for (const std::string& k : keys) {
        s += k;
    }
    return s;
}

KeyDelivery deliverKey(ElementTree& tree, const ElementRegistry& registry, const std::string& key) {
    KeyDelivery d;
    const std::string focus = currentFocus(tree);
    if (focus == "auto") {
        d.toStdout = true;
        d.stdoutText = key;
        return d;
    }
    const ElementKind& kind = registry.get(tree.find(focus)->type);
    if (kind.exitKeys && kind.onExit) {
        const std::vector<std::string> keys = kind.exitKeys(tree, focus);
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
            const ElementKind::ExitResult r = kind.onExit(tree, registry, focus, key);
            d.changed = r.changed;
            d.exited = true;
            d.toStdout = true;
            d.stdoutText = "exit:" + key;
            for (const auto& [name, value] : r.fields) {
                d.stdoutText += " " + name + ":" + value;
            }
            return d;
        }
    }
    if (kind.onKey) {
        const ElementKind::KeyResult r = kind.onKey(tree, registry, focus, key);
        d.changed = r.changed;
        if (r.out) {
            d.toStdout = true;
            d.stdoutText = *r.out;
        }
    } // no onKey: the element drops everything
    return d;
}

std::optional<std::vector<std::string>> parseExitKeys(const std::string& text) {
    auto keys = parseKeySequence(text);
    if (!keys || keys->empty()) {
        return std::nullopt;
    }
    return keys;
}

KeyReplay replayKeys(ElementTree& tree, const ElementRegistry& registry,
                     const std::vector<std::string>& keys) {
    KeyReplay r;
    for (const std::string& key : keys) {
        const KeyDelivery d = deliverKey(tree, registry, key);
        if (d.toStdout) {
            r.stdoutText += d.stdoutText;
        }
        r.changed = r.changed || d.changed;
        if (d.exited) {
            r.exited = true;
            break; // exit key: the remaining keystrokes are not replayed
        }
    }
    return r;
}

void requireFocusUnchanged(const ElementTree& tree, const std::string& expected) {
    const std::string now = currentFocus(tree);
    if (now == expected) {
        return;
    }
    throw ToytuiException(ToytuiError{
        "get", "core@terminal",
        now == "auto" ? "Focus target no longer exists: `" + expected + "`"
                      : "Focus changed during the keyboard session: `" + expected + "` -> `" +
                            now + "`"});
}

} // namespace toytui
