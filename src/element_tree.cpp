#include "element_tree.h"

#include <algorithm>

#include "cli_parser.h" // isValidParamName - the id character set is the same
#include "error.h"
#include "param_types.h"

namespace toytui {

namespace {
constexpr std::size_t kMaxIdLen = 23; // 3.4
constexpr const char* kRoot = "core@terminal";
} // namespace

ElementTree::ElementTree() {
    ElementNode root;
    root.type = "core";
    root.parentFullId = ""; // root: has no parent
    nodes_[kRoot] = std::move(root);
}

bool ElementTree::exists(const std::string& fullId) const {
    return nodes_.count(fullId) != 0;
}

const ElementNode* ElementTree::find(const std::string& fullId) const {
    auto it = nodes_.find(fullId);
    return it == nodes_.end() ? nullptr : &it->second;
}

ElementNode* ElementTree::findMut(const std::string& fullId) {
    auto it = nodes_.find(fullId);
    return it == nodes_.end() ? nullptr : &it->second;
}

std::string ElementTree::addElement(const std::string& type,
                                     const std::string& shortId,
                                     const std::string& parentFullId,
                                     ElementParams initialParams) {
    if (!isValidParamName(shortId)) {
        throw ToytuiException(
            ToytuiError{"new", "-", "Malformed id: `" + shortId + "`"});
    }
    if (shortId.size() > kMaxIdLen) {
        throw ToytuiException(ToytuiError{
            "new", "-",
            "id too long (max " + std::to_string(kMaxIdLen) +
                " characters): `" + shortId + "`"});
    }

    const std::string fullId = type + "@" + shortId;
    if (nodes_.count(fullId)) {
        throw ToytuiException(ToytuiError{
            "new", fullId, "Element already exists: `" + fullId + "`"});
    }

    const std::string parent = parentFullId.empty() ? kRoot : parentFullId;
    if (!exists(parent)) {
        throw ToytuiException(ToytuiError{
            "new", fullId, "Parent does not exist: `" + parent + "`"});
    }

    if (depthOf(parent) + 1 > kMaxTreeDepth) {
        throw ToytuiException(ToytuiError{
            "new", fullId,
            "Nesting too deep (max " + std::to_string(kMaxTreeDepth) + " levels): `" +
                parent + "`"});
    }

    ElementNode node;
    node.type = type;
    node.parentFullId = parent;
    node.params = std::move(initialParams);
    nodes_[fullId] = std::move(node);
    nodes_[parent].childFullIds.push_back(fullId);
    return fullId;
}

std::vector<std::string> ElementTree::allIds() const {
    std::vector<std::string> out;
    std::vector<std::string> stack{kRoot};
    while (!stack.empty()) {
        const std::string id = stack.back();
        stack.pop_back();
        out.push_back(id);
        const auto& children = nodes_.at(id).childFullIds;
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            stack.push_back(*it);
        }
    }
    return out;
}

std::vector<std::string> ElementTree::subtree(const std::string& fullId) const {
    std::vector<std::string> out;
    if (!exists(fullId)) {
        return out;
    }
    // iterative, so that deep trees hit no recursion limit
    std::vector<std::pair<std::string, bool>> stack{{fullId, false}};
    while (!stack.empty()) {
        auto [id, expanded] = stack.back();
        stack.pop_back();
        if (expanded) {
            out.push_back(id);
            continue;
        }
        stack.emplace_back(id, true);
        const auto& children = nodes_.at(id).childFullIds;
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            stack.emplace_back(*it, false);
        }
    }
    return out;
}

void ElementTree::removeCascade(const std::string& fullId) {
    if (fullId == kRoot) {
        throw ToytuiException(
            ToytuiError{"del", fullId, "core@terminal cannot be deleted"});
    }
    auto it = nodes_.find(fullId);
    if (it == nodes_.end()) {
        throw ToytuiException(ToytuiError{
            "del", fullId, "Element does not exist: `" + fullId + "`"});
    }

    // Iterative (the post-order of `subtree`: children before the parent), so
    // deletion is non-recursive regardless of depth.
    const std::string parent = it->second.parentFullId;
    for (const auto& id : subtree(fullId)) {
        nodes_.erase(id);
    }
    auto pit = nodes_.find(parent);
    if (pit != nodes_.end()) {
        auto& siblings = pit->second.childFullIds;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), fullId),
                        siblings.end());
    }
}

int ElementTree::depthOf(const std::string& fullId) const {
    int depth = 0;
    std::string cur = fullId;
    while (cur != kRoot) {
        auto it = nodes_.find(cur);
        if (it == nodes_.end()) {
            break;
        }
        cur = it->second.parentFullId;
        ++depth;
    }
    return depth;
}

int ElementTree::subtreeHeight(const std::string& fullId) const {
    if (!exists(fullId)) {
        return 0;
    }
    int height = 0;
    std::vector<std::pair<std::string, int>> stack{{fullId, 1}};
    while (!stack.empty()) {
        auto [id, level] = stack.back();
        stack.pop_back();
        height = std::max(height, level);
        for (const auto& child : nodes_.at(id).childFullIds) {
            stack.emplace_back(child, level + 1);
        }
    }
    return height;
}

bool ElementTree::isAncestorOrSelf(const std::string& candidateAncestor,
                                   const std::string& startId) const {
    std::string cur = startId;
    while (!cur.empty()) {
        if (cur == candidateAncestor) {
            return true;
        }
        auto it = nodes_.find(cur);
        if (it == nodes_.end()) {
            return false;
        }
        cur = it->second.parentFullId;
    }
    return false;
}

void ElementTree::setParent(const std::string& fullId,
                             const std::string& newParentFullId) {
    if (fullId == kRoot) {
        throw ToytuiException(
            ToytuiError{"set", fullId, "core@terminal has no parent"});
    }
    auto it = nodes_.find(fullId);
    if (it == nodes_.end()) {
        throw ToytuiException(ToytuiError{
            "set", fullId, "Element does not exist: `" + fullId + "`"});
    }
    if (!exists(newParentFullId)) {
        throw ToytuiException(
            ToytuiError{"set", fullId,
                        "Parent does not exist: `" + newParentFullId + "`"});
    }
    // Cycle: the new parent is fullId itself, OR a current
    // descendant of fullId -> it would make fullId a child of its
    // own descendant.
    if (isAncestorOrSelf(fullId, newParentFullId)) {
        throw ToytuiException(ToytuiError{
            "set", fullId,
            "Parent change would create a cycle: `" + newParentFullId + "`"});
    }

    const std::string oldParent = it->second.parentFullId;
    if (oldParent == newParentFullId) {
        return; // no actual change
    }
    if (depthOf(newParentFullId) + subtreeHeight(fullId) > kMaxTreeDepth) {
        throw ToytuiException(ToytuiError{
            "set", fullId,
            "Nesting too deep (max " + std::to_string(kMaxTreeDepth) + " levels): `" +
                newParentFullId + "`"});
    }

    auto oldIt = nodes_.find(oldParent);
    if (oldIt != nodes_.end()) {
        auto& siblings = oldIt->second.childFullIds;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), fullId),
                        siblings.end());
    }
    nodes_[newParentFullId].childFullIds.push_back(fullId);
    it->second.parentFullId = newParentFullId;
}

std::string ElementTree::resolveInheritable(const std::string& fullId,
                                             const std::string& paramName) const {
    std::string cur = fullId;
    while (true) {
        auto it = nodes_.find(cur);
        if (it == nodes_.end()) {
            throw ToytuiException(ToytuiError{
                "-", fullId,
                "Internal: element not found during resolution: `" + cur +
                    "`"});
        }
        const ParamValue* v = it->second.params.find(paramName);
        if (v != nullptr && v->state == ParamState::Set) {
            return v->value;
        }
        if (cur == kRoot) {
            // core@terminal never has unset w/h/bg/fg —
            // if we got this far and it is unset anyway, that is an internal error.
            throw ToytuiException(ToytuiError{
                "-", fullId,
                "Internal: core@terminal has no concrete value for `" +
                    paramName + "`"});
        }
        cur = it->second.parentFullId;
    }
}

int ElementTree::resolveInheritableInt(const std::string& fullId,
                                       const std::string& paramName) const {
    return storedInt(resolveInheritable(fullId, paramName));
}

std::uint8_t ElementTree::resolveInheritableColor(const std::string& fullId,
                                                  const std::string& paramName) const {
    return storedColor(resolveInheritable(fullId, paramName));
}

std::string ElementTree::resolveComponentColor(
    const std::string& fullId, const std::string& componentParamName,
    const std::string& elementColorParamName) const {
    const ElementNode* node = find(fullId);
    if (node == nullptr) {
        throw ToytuiException(ToytuiError{
            "-", fullId, "Element does not exist: `" + fullId + "`"});
    }
    const ParamValue* v = node->params.find(componentParamName);
    if (v != nullptr && v->state == ParamState::Set) {
        return v->value;
    }
    return resolveInheritable(fullId, elementColorParamName);
}

} // namespace toytui
