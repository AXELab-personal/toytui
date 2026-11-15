// compositor.h — compositing.
//
// An element's finished visual buffer = its own (ownVisual) buffer + its
// children, fitted to the content area in z_index order (ascending), by
// creation order for equal z_index, and CLIPPED to the content area.
// A child's position is relative to the parent's content area: posX + offsetX
// (e.g. `offsX:-2` -> the child's 3rd column lands on the left edge); the offset
// comes from the parent's scroll hook (bounds: box_element.h).
// Element types are known only through the ElementRegistry.
#pragma once

#include <optional>
#include <string>
#include <utility>

#include "cell.h"
#include "element_registry.h"
#include "element_tree.h"

namespace toytui {

Block composeElement(const ElementTree& tree, const ElementRegistry& registry,
                      const std::string& fullId);

// Combined extent of the children (relative to the content origin, without
// offset) — the SAME computation that compositing uses for the scroll
// bounds. For key handlers (box navigation): stepping
// must stay within the actual range.
Extent childExtent(const ElementTree& tree, const ElementRegistry& registry,
                   const std::string& fullId);

// SCREEN coordinate (x, y) of the local cell (lx, ly) of element `id`, through the parent chain (position + scroll offset),
// or nullopt if the cell is not visible (it falls outside the parent's content area/buffer). Occlusion (a sibling with a higher
// z_index) is not examined.
std::optional<std::pair<int, int>> screenPositionOf(const ElementTree& tree, const ElementRegistry& registry,
                                                    const std::string& id, int lx, int ly);

// The final output: the composited buffer of core@terminal.
Block composeScreen(const ElementTree& tree, const ElementRegistry& registry);

} // namespace toytui
