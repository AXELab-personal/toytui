#include "garbage.h"

#include "error.h"

namespace toytui {

std::vector<std::string> collectGarbage(ElementTree& tree, const ElementRegistry& registry,
                                         const std::string& fullId) {
    if (fullId == "core@terminal") {
        throw ToytuiException(ToytuiError{"del", fullId, "core@terminal cannot be deleted"});
    }
    if (!tree.exists(fullId)) {
        throw ToytuiException(
            ToytuiError{"del", fullId, "Element does not exist: `" + fullId + "`"});
    }

    std::vector<std::string> doomed = tree.subtree(fullId);
    for (const auto& id : doomed) {
        const ElementKind& kind = registry.get(tree.find(id)->type);
        if (kind.onDelete) {
            kind.onDelete(tree, id);
        }
    }
    tree.removeCascade(fullId);

    // The surviving elements repair their references to the deleted elements
    // (e.g. core: setfocus -> auto).
    for (const auto& id : tree.allIds()) {
        const ElementKind& kind = registry.get(tree.find(id)->type);
        if (kind.onElementsDeleted) {
            kind.onElementsDeleted(tree, id, doomed);
        }
    }
    return doomed;
}

} // namespace toytui
