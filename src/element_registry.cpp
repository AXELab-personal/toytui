#include "element_registry.h"

#include "box_element.h"
#include "core_element.h"
#include "error.h"
#include "button_element.h"
#include "label_element.h"
#include "input_element.h"
#include "select_element.h"
#include "subterm_element.h"
#include "textarea_element.h"

namespace toytui {

std::vector<std::string> ElementRegistry::typeNames() const {
    std::vector<std::string> out;
    if (kinds_.count("core") != 0) {
        out.push_back("core");
    }
    for (const auto& kv : kinds_) { // std::map: alphabetical
        if (kv.first != "core") {
            out.push_back(kv.first);
        }
    }
    return out;
}


std::optional<int> parentContentSize(const ElementTree& tree, const ElementRegistry& registry,
                                     const ElementParams& params, bool height) {
    const std::string* parent = params.getSet("parent");
    if (parent == nullptr) {
        return std::nullopt;
    }
    const ElementNode* pn = tree.find(*parent);
    if (pn == nullptr) {
        return std::nullopt;
    }
    const ElementKind& pk = registry.get(pn->type);
    const auto& hook = height ? pk.contentHeight : pk.contentWidth;
    return hook ? hook(tree, *parent) : std::nullopt;
}

void ElementRegistry::add(const std::string& type, ElementKind kind) {
    kinds_[type] = std::move(kind);
}

const ElementKind& ElementRegistry::get(const std::string& type) const {
    auto it = kinds_.find(type);
    if (it == kinds_.end()) {
        throw ToytuiException(
            ToytuiError{"-", "-", "Unknown element type: `" + type + "`"});
    }
    return it->second;
}

bool ElementRegistry::has(const std::string& type) const {
    return kinds_.count(type) != 0;
}

bool ElementRegistry::hostsChildren(const ElementTree& tree,
                                    const std::string& fullId) const {
    const ElementNode* node = tree.find(fullId);
    if (node == nullptr) {
        return false;
    }
    // We evaluate contentArea on a zero-size buffer: only
    // whether it is nullopt matters (whether the type can take children).
    Block dummy;
    return get(node->type).contentArea(tree, fullId, dummy).has_value();
}

std::string createElement(ElementTree& tree, const ElementRegistry& registry,
                           const std::string& type, const ElementParams& params) {
    const std::string& parent = params.find("parent")->value;
    if (tree.exists(parent) && !registry.hostsChildren(tree, parent)) {
        throw ToytuiException(ToytuiError{
            "new", "-", "Parent cannot host children: `" + parent + "`"});
    }
    return tree.addElement(type, params.find("id")->value, parent, params);
}

// Collecting the built-in types — the actual logic of each lives in
// its own module (see the *ElementKind() functions); this
// function only registers.
ElementRegistry makeBuiltinRegistry(SubtermRuntime* subterms) {
    ElementRegistry r;
    r.add("core", coreElementKind());
    r.add("box", boxElementKind());
    r.add("label", labelElementKind());
    r.add("textarea", textareaElementKind());
    r.add("button", buttonElementKind());
    r.add("select", selectElementKind());
    r.add("input", inputElementKind());
    r.add("subterm", subtermElementKind(subterms));
    r.setEffectSink(subterms);
    return r;
}

} // namespace toytui
