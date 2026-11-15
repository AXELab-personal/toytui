// garbage.h — the garbage collector: carrying out the `del`
// cascade delete, INDEPENDENT OF ELEMENT TYPE.
//
// Walks the affected subtree in post-order (children before their parent),
// calls the optional `onDelete` hook of each element — if its type
// defines one — and only then removes the subtree from the
// tree (and with it from the parameter store). The built-in types have no
// hook; the sub-terminal will be the first to clean up.
//
// Atomicity: ALL hooks run before the actual deletion; if a
// hook throws, the tree is left untouched (the effect of hooks that
// already ran naturally cannot be undone — that is the hook's responsibility).
#pragma once

#include <string>
#include <vector>

#include "element_registry.h"
#include "element_tree.h"

namespace toytui {

// Full ids of the deleted elements, in deletion (post-order) order
// — from this the caller can, e.g., reset the focus target to unset.
// ToytuiException for `core@terminal` or a non-existent element.
std::vector<std::string> collectGarbage(ElementTree& tree, const ElementRegistry& registry,
                                         const std::string& fullId);

} // namespace toytui
