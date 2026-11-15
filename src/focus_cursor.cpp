#include "focus_cursor.h"

#include "compositor.h"
#include "input_ops.h"

namespace toytui {

FocusCursor focusCursorOf(const ElementTree& tree, const ElementRegistry& registry) {
    FocusCursor c;
    const std::string focus = currentFocus(tree);
    if (focus == "auto") {
        return c;
    }
    const ElementNode* node = tree.find(focus);
    if (node == nullptr) {
        return c;
    }
    const ElementKind& kind = registry.get(node->type);
    if (!kind.cursorSpot) {
        return c;
    }
    const auto spot = kind.cursorSpot(tree, focus);
    if (!spot) {
        return c;
    }
    c.hasSpot = true;
    c.shape = spot->shape;
    if (const auto pos = screenPositionOf(tree, registry, focus, spot->x, spot->y)) {
        c.visible = true;
        c.col = pos->first;
        c.row = pos->second;
    }
    return c;
}

std::string cursorShowSequence(const FocusCursor& c) {
    if (!c.hasSpot) {
        return std::string();
    }
    if (!c.visible) {
        return "\x1b[?25l";
    }
    return "\x1b[" + std::to_string(c.shape) + " q\x1b[" + std::to_string(c.row + 1) + ";" +
           std::to_string(c.col + 1) + "H\x1b[?25h";
}

std::string cursorResetSequence(bool visible) {
    return std::string("\x1b[0 q") + (visible ? "\x1b[?25h" : "\x1b[?25l");
}

} // namespace toytui
