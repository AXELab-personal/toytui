// element_tree.h — the module of the element tree and
// unset resolution.
//
// BOUNDARY (important so that the module does not bloat): this handles ONLY
// the tree structure (parent-child relation, id uniqueness,
// acyclicity) and the MECHANISM of unset resolution.
// Element-type-specific validation (e.g. the value set of `box` `border`,
// range checks of `posX`/`z_index`) does NOT belong here — it is done by the
// element type layers (box_element, a shared
// general param validator), which use this tree only as a tool.
//
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "param_store.h"

namespace toytui {

struct ElementNode {
    std::string type;                      // "core", "box", "label", ...
    std::string parentFullId;              // "" ONLY for core@terminal
    std::vector<std::string> childFullIds; // creation / latest
                                            // re-parenting order
    ElementParams params;
};

// Maximum nesting depth (matching the system's limit of 512):
// a single `parent:` chain, core included, may not be
// deeper than this. Fixed in one place.
constexpr int kMaxTreeDepth = 512;

class ElementTree {
public:
    // The `core@terminal` root is created automatically.
    ElementTree();

    bool exists(const std::string& fullId) const;
    const ElementNode* find(const std::string& fullId) const;
    ElementNode* findMut(const std::string& fullId);

    // Adding a new element. `shortId` is the (short) identifier
    // given by the user; the return value is the full identifier
    // (`type@shortId`). For an empty `parentFullId` the parent is
    // `core@terminal` (implicit parent). Validates: id character set
    // + max. length of 23 characters, uniqueness within the type,
    // whether the given parent exists.
    std::string addElement(const std::string& type, const std::string& shortId,
                            const std::string& parentFullId,
                            ElementParams initialParams);

    // All elements (core@terminal too) in PRE-ORDER, following the order of
    // `childFullIds` — this order carries the creation/
    // re-parenting order (z_index tie-break), so the
    // serializer writes in this order and restores it the same way.
    std::vector<std::string> allIds() const;

    // The element and its whole subtree in POST-ORDER (children before
    // their parent); empty for a non-existent element. The garbage collector's
    // traversal is built on this.
    std::vector<std::string> subtree(const std::string& fullId) const;
    // Depth: core is 0, a child of core is 1, ... (the chain counted from the root).
    int depthOf(const std::string& fullId) const;
    // Height of the subtree: the fullId itself is 1, its children 2, ...
    int subtreeHeight(const std::string& fullId) const;

    // Cascade delete (`del`): the element and its whole subtree.
    // An error when called on `core@terminal` (it can never be deleted).
    void removeCascade(const std::string& fullId);

    // Setting a new parent, with a cycle check ("a cycle in the tree can
    // never arise").
    //
    // Design choice: on re-
    // parenting the element goes to the END of the NEW parent's child list
    // (as if it were being created there now) — it disappears from the old
    // parent's list. If creation order is meant to apply globally to the
    // WHOLE tree rather than as a per-parent list, change it here.
    void setParent(const std::string& fullId,
                    const std::string& newParentFullId);

    // `w`/`h`/`bg`/`fg`/`attrib` unset resolution: walks up
    // the tree until it finds a concrete (Set) value;
    // always stops at `core@terminal` (there these are never unset). The
    // inheritance of `attrib` is INTENTIONAL: it behaves the same as w/h/bg/fg
    // (see cell.h ATTRIB_UNSET). The other general parameters (posX/Y, z_index) are NOT
    // inherited like this (fixed default, not dependent on the parent), and it
    // cannot be called for them.
    std::string resolveInheritable(const std::string& fullId,
                                    const std::string& paramName) const;

    // The result of resolveInheritable as an integer / colour index
    // (the stored value is already validated) — shared helper of core/box/label.
    int resolveInheritableInt(const std::string& fullId,
                              const std::string& paramName) const;
    std::uint8_t resolveInheritableColor(const std::string& fullId,
                                         const std::string& paramName) const;

    // Two-step colour resolution for box subcomponents:
    //   1) `componentParamName` (e.g. "border_fgcol") on the element itself,
    //   2) if unset: the element's OWN `elementColorParamName`
    //      (e.g. "bgcol") resolved with resolveInheritable — which, if the
    //      element's own colour is unset too, continues the walk up
    //      the tree (that is resolveInheritable's internal logic).
    std::string
    resolveComponentColor(const std::string& fullId,
                           const std::string& componentParamName,
                           const std::string& elementColorParamName) const;

private:
    std::map<std::string, ElementNode> nodes_;

    // Whether `candidateAncestor` really is an ancestor of `startId` (or `startId` itself),
    // walking up the parentFullId chain? (For cycle checking.)
    bool isAncestorOrSelf(const std::string& candidateAncestor,
                          const std::string& startId) const;
};

} // namespace toytui
