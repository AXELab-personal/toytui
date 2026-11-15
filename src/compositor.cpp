#include "compositor.h"

#include <algorithm>

#include "param_types.h"

namespace toytui {

namespace {

int intParam(const ElementNode& n, const char* name, int fallback) {
    return paramInt(n.params, name, fallback);
}

// Paste `src` into `dst` at position (x,y), clipped to the `clip` rectangle
// (and to the bounds of dst).
void blit(Block& dst, const Rect& clip, int x, int y, const Block& src) {
    const int x0 = std::max({clip.x, 0, x});
    const int y0 = std::max({clip.y, 0, y});
    const int x1 = std::min({clip.x + clip.width, dst.width, x + src.width});
    const int y1 = std::min({clip.y + clip.height, dst.height, y + src.height});
    for (int dy = y0; dy < y1; ++dy) {
        for (int dx = x0; dx < x1; ++dx) {
            dst.at(dx, dy) = src.at(dx - x, dy - y);
        }
        if (x0 < x1) {
            // Wide (two-cell) character: a pair left half-way at the edge of a clip or occlusion is dropped as a whole.
            repairWidePairs(&dst.at(0, dy), dst.width);
        }
    }
}

} // namespace

namespace {

struct Child {
    int x, y;
    int width, height;
    std::string id;
    bool windowed; // true: the `block` is empty, the visible part is built only at compositing (measure/ownVisualWindow)
    Block block;
};

// Compositing the children (sorted by z_index) + their extent relative to the
// content origin. Shared by composeElement and childExtent
// so that the extent used for the scroll bounds can NEVER differ
// from what compositing actually sees.
Extent composeChildren(const ElementTree& tree, const ElementRegistry& registry,
                       const ElementNode& node, std::vector<Child>& children) {
    // childFullIds is in creation order -> stable_sort keeps it
    // for equal z_index (the later-created one ends up on top).
    std::vector<std::string> order = node.childFullIds;
    std::stable_sort(order.begin(), order.end(), [&](const std::string& a, const std::string& b) {
        return intParam(*tree.find(a), "z_index", 1) < intParam(*tree.find(b), "z_index", 1);
    });

    Extent ext;
    for (const auto& childId : order) {
        const ElementNode& child = *tree.find(childId);
        const ElementKind& childKind = registry.get(child.type);
        Child c{intParam(child, "posX", 0), intParam(child, "posY", 0), 0, 0, childId, false, Block{}};
        if (childKind.measure) {
            // windowed element: only its size is needed, the cells come later, for the visible part
            const auto [w, h] = childKind.measure(tree, registry, childId);
            c.width = w;
            c.height = h;
            c.windowed = true;
        } else {
            c.block = composeElement(tree, registry, childId);
            c.width = c.block.width;
            c.height = c.block.height;
        }
        ext.x0 = ext.any ? std::min(ext.x0, c.x) : c.x;
        ext.y0 = ext.any ? std::min(ext.y0, c.y) : c.y;
        ext.x1 = ext.any ? std::max(ext.x1, c.x + c.width) : c.x + c.width;
        ext.y1 = ext.any ? std::max(ext.y1, c.y + c.height) : c.y + c.height;
        ext.any = true;
        children.push_back(std::move(c));
    }
    return ext;
}

} // namespace

Block composeElement(const ElementTree& tree, const ElementRegistry& registry,
                      const std::string& fullId) {
    const ElementNode& node = *tree.find(fullId);
    const ElementKind& kind = registry.get(node.type);

    if (kind.measure) {
        // windowed leaf element on its own: the full image (the window = the whole element)
        const auto [w, h] = kind.measure(tree, registry, fullId);
        if (w <= 0 || h <= 0) {
            return Block{};
        }
        return kind.ownVisualWindow(tree, registry, fullId, Rect{0, 0, w, h});
    }
    Block out = kind.ownVisual(tree, fullId);
    const std::optional<Rect> area = kind.contentArea(tree, fullId, out);
    if (!area) {
        return out;
    }

    std::vector<Child> children;
    const Extent ext = composeChildren(tree, registry, node, children);

    const auto [offX, offY] = kind.scroll ? kind.scroll(tree, fullId, *area, ext)
                                          : std::pair<int, int>(0, 0);
    // For windowed children the visible part: the intersection of the parent's content area
    // and the buffer (at most the size of the content area — the textarea
    // never builds more than what is visible).
    const Rect clip{std::max(area->x, 0), std::max(area->y, 0),
                    std::min(area->x + area->width, out.width) - std::max(area->x, 0),
                    std::min(area->y + area->height, out.height) - std::max(area->y, 0)};
    for (const auto& c : children) {
        const int px = area->x + c.x + offX;
        const int py = area->y + c.y + offY;
        if (!c.windowed) {
            blit(out, *area, px, py, c.block);
            continue;
        }
        const int x0 = std::max(clip.x, px);
        const int y0 = std::max(clip.y, py);
        const int x1 = std::min(clip.x + clip.width, px + c.width);
        const int y1 = std::min(clip.y + clip.height, py + c.height);
        if (x0 >= x1 || y0 >= y1) {
            continue; // not visible: nothing to build
        }
        const ElementKind& childKind = registry.get(tree.find(c.id)->type);
        const Block wb = childKind.ownVisualWindow(tree, registry, c.id, Rect{x0 - px, y0 - py, x1 - x0, y1 - y0});
        blit(out, *area, x0, y0, wb);
    }
    if (kind.decorate) {
        kind.decorate(tree, fullId, out, *area, ext);
    }
    return out;
}

Extent childExtent(const ElementTree& tree, const ElementRegistry& registry,
                   const std::string& fullId) {
    std::vector<Child> children;
    return composeChildren(tree, registry, *tree.find(fullId), children);
}

std::optional<std::pair<int, int>> screenPositionOf(const ElementTree& tree, const ElementRegistry& registry,
                                                    const std::string& id, int lx, int ly) {
    int x = lx;
    int y = ly;
    std::string cur = id;
    while (cur != "core@terminal") {
        const ElementNode* node = tree.find(cur);
        if (node == nullptr || node->parentFullId.empty()) {
            return std::nullopt;
        }
        const std::string& pid = node->parentFullId;
        const ElementNode* parent = tree.find(pid);
        if (parent == nullptr) {
            return std::nullopt;
        }
        const ElementKind& pk = registry.get(parent->type);
        const Block pown = pk.ownVisual(tree, pid);
        const std::optional<Rect> area = pk.contentArea(tree, pid, pown);
        if (!area) {
            return std::nullopt;
        }
        int offX = 0;
        int offY = 0;
        if (pk.scroll) {
            const Extent ext = childExtent(tree, registry, pid);
            const auto off = pk.scroll(tree, pid, *area, ext);
            offX = off.first;
            offY = off.second;
        }
        x += area->x + intParam(*node, "posX", 0) + offX;
        y += area->y + intParam(*node, "posY", 0) + offY;
        const int x0 = std::max(area->x, 0);
        const int y0 = std::max(area->y, 0);
        const int x1 = std::min(area->x + area->width, pown.width);
        const int y1 = std::min(area->y + area->height, pown.height);
        if (x < x0 || x >= x1 || y < y0 || y >= y1) {
            return std::nullopt;
        }
        cur = pid;
    }
    return std::make_pair(x, y);
}

Block composeScreen(const ElementTree& tree, const ElementRegistry& registry) {
    return composeElement(tree, registry, "core@terminal");
}

} // namespace toytui
