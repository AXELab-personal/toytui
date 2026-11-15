// element_registry.h — element type registration.
//
// The compositor knows NOTHING about concrete element types: every type
// (core, box, label, later plugin, ...) hooks in through the same two
// entry points. A new type = one `add(...)` call; the
// compositor/dispatch code does not change.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cell.h"
#include "cli_parser.h"
#include "param_spec.h"
#include "element_tree.h"
#include "param_store.h"

namespace toytui {

class ElementRegistry;

// Side effects of a call that must happen only if the WHOLE call succeeds (e.g. starting a program
// for `subterm`). The element handlers only RECORD them; `Engine` (`mutateAndRender`) calls `discard`
// before the operation, and `commit` right after it succeeded (before rendering). `commit` may throw a
// ToytuiException (the call then fails and nothing is saved).
class EffectSink {
public:
    virtual ~EffectSink() = default;
    virtual void discard() = 0;
    virtual void commit(const ElementTree& tree, const ElementRegistry& registry) = 0;
};

struct ElementKind {
    // --- CLI methods: dispatch is type-independent and calls the
    // element type through these. `create == nullptr`: the type has no `new`
    // (core). The `get` handler receives the parameters WITHOUT the `id:` token.
    std::function<ElementParams(const ParsedCommand&)> create;
    std::function<void(ElementParams&, const ParsedCommand&)> set;
    std::function<std::vector<std::string>(const ElementParams&, const ParsedCommand&)> get;

    // The list of parameters known to the type, in canonical (get all)
    // order — `core get param_list` uses it to tell
    // which type "has" a given parameter, and whether a
    // value is Kind::Text (CSV quoting + "only the last item may be one",
    // see element_ops.h::execParamList). `core` has one too (next to its bespoke
    // `coreGet`/`coreSet`, EXCLUSIVELY for this
    // introspection).
    SpecTable paramSpecs;

    // The type's free-text parameters: from the very first argument with such a
    // prefix on, the parser appends everything to the value. Default: `text`;
    // textarea: `text`, `text_add`.
    std::vector<std::string> freeTextParams = {"text"};

    // Optional: deliver a key to the FOCUSED element
    // (`core set setfocus:`). `key` is the uniform notation (keycodec.h:
    // `a`, `[UP]`, `[CTRL-Q]`...). Return value (`KeyResult`): `changed` =
    // the element modified its STATE (a redraw + save then
    // follow); `out` = the element asks for text on stdout (e.g. `button`
    // gives the notation of the received key to the shell). The element decides
    // what it interprets — what it does not, it drops (`false`). Implicitly
    // convertible from `bool` (`true` = changed). nullptr: the element type handles no
    // keys (drops everything).
    struct KeyResult {
        bool changed = false;
        std::optional<std::string> out;
        KeyResult() = default;
        KeyResult(bool c) : changed(c) {} // NOLINT: intentionally implicit
        KeyResult(bool c, std::string o) : changed(c), out(std::move(o)) {}
    };
    std::function<KeyResult(ElementTree&, const ElementRegistry&, const std::string& fullId,
                            const std::string& key)>
        onKey;

    // Optional: the `set` call asks for a TRANSIENT, non-stored appearance (e.g. the
    // `button`'s `trigger:on`): returns the length of the transient state in ms
    // (nullopt: no request). Called AFTER the `set` (the rest of the call's parameters
    // are already valid). The caller marks the transient state in the tree with the internal `pressed=on`
    // parameter, renders, waits, takes it back, and saves only at the end.
    std::function<std::optional<int>(const ElementParams& updated, const ParsedCommand&)> pulseMs;

    // Optional: type-specific check BEFORE a `set` is FINALISED,
    // on the already validated (copy) parameters, but knowing the WHOLE tree
    // (e.g. core: whether the target of `setfocus` exists). On failure a
    // ToytuiException — nothing is modified then (the `set` stays atomic).
    std::function<void(const ElementTree&, const std::string& fullId,
                       const ElementParams& updated)>
        checkSet;

    // Optional: called for EVERY surviving element after a `del` (cascade),
    // with the list of deleted elements (full ids) — to repair
    // references POINTING AT the deleted element (core: if
    // `setfocus` pointed at a deleted element, it reverts to `auto`).
    std::function<void(ElementTree&, const std::string& selfId,
                       const std::vector<std::string>& deleted)>
        onElementsDeleted;

    // Optional, type-specific `del` hook: called by the
    // garbage collector before the element is actually deleted.
    // None for the built-in types (nullptr).
    std::function<void(const ElementTree&, const std::string& fullId)> onDelete;

    // The element's OWN (childless) visual buffer, at full
    // (resolved) size — e.g. box: background + border + title.
    std::function<Block(const ElementTree&, const std::string& fullId)> ownVisual;

    // Optional: the WIDTH of the content area meant for children, without building the visual
    // buffer (a child's `width:auto` value, textarea).
    // nullptr or nullopt: the element type takes no children / has no such
    // data. Equals the width of `contentArea` (box: border and vertical
    // scrollbar subtracted; core: the full width).
    std::function<std::optional<int>(const ElementTree&, const std::string& fullId)> contentWidth;

    // Optional: the HEIGHT of the content area meant for children (counterpart of `contentWidth`; the select's
    // automatic height is limited to the parent's content area). Box: border/scrollbar subtracted;
    // core: the full height.
    std::function<std::optional<int>(const ElementTree&, const std::string& fullId)> contentHeight;

    // Optional: exit keys DECLARED by the element (select: the union of `keys_exit:` and `keys_accept:`;
    // input: the same;
    // in canonical notation). The `keyboard` loop/`keypress`/replay treat these as an exit, but
    // deliver them to the element through `onExit` (not `onKey`). Empty/nullptr: no declared exit keys.
    std::function<std::vector<std::string>(const ElementTree&, const std::string& fullId)> exitKeys;

    // Delivering one of the `exitKeys`: a state change (changed) + further `name:value`
    // fields of the output (e.g. `selected:3`), which go to stdout after `exit:<key>`.
    struct ExitResult {
        bool changed = false;
        std::vector<std::pair<std::string, std::string>> fields;
    };
    std::function<ExitResult(ElementTree&, const ElementRegistry&, const std::string& fullId,
                             const std::string& key)>
        onExit;

    // Optional, for WINDOWED element types (textarea): the element's size
    // (width, height) without building cells. If `measure` exists, the element is a
    // LEAF (contentArea = nullopt), and for children the compositor calls not
    // `ownVisual` but `ownVisualWindow`, for the visible part only.
    // The child extent (`childExtent`, scroll bounds) comes from `measure`.
    std::function<std::pair<int, int>(const ElementTree&, const ElementRegistry&, const std::string& fullId)>
        measure;

    // The visible part of a windowed type: `window` is in the element's OWN coordinates
    // (always within the element's [0,w)x[0,h) rectangle, non-empty); the returned
    // Block's size is exactly window.width x window.height, its content the corresponding excerpt
    // of the element's full image (identical to building the whole thing
    // and cutting it out).
    std::function<Block(const ElementTree&, const ElementRegistry&, const std::string& fullId,
                        const Rect& window)>
        ownVisualWindow;

    // Optional: DERIVED (not stored) parameters whose value needs knowledge of the TREE (e.g. the select's
    // `visualCursor` depends on the parent's size). `derivedParams`: the names; `deriveParams`: before a `get`, writes the
    // computed values into a COPY of the stored parameters (nothing is written back: `auto` on reading); the fast
    // partial read of the state file (without the full tree) is usable only if the query touches no derived name.
    std::vector<std::string> derivedParams;
    std::function<void(const ElementTree&, const ElementRegistry&, const std::string& fullId, ElementParams& view)>
        deriveParams;

    // Optional: processing of WRITABLE derived parameters during `new`/`set`, AFTER the type's `create`/`set`
    // handler (the other parameters are already valid), knowing the WHOLE tree: converts the derived values
    // received in the call into stored state and removes the marker from the parameters (it never ends up in
    // the state file). For `new` the element is not yet in the tree.
    std::function<void(const ElementTree&, const ElementRegistry&, ElementParams& params)> resolveDerived;

    // Optional: the position of the FOCUSED element's terminal cursor (input field): in the element's OWN coordinates, plus the
    // cursor shape (DECSCUSR code: 1 blinking block, 2 steady block, 3 blinking underline, 4 steady underline, 5 blinking
    // bar, 6 steady bar). nullopt/nullptr: the element has no cursor. After rendering, the `focus_cursor` module
    // converts it to screen coordinates and a terminal sequence (terminal targets only).
    struct CursorSpot {
        int x = 0;
        int y = 0;
        int shape = 6;
    };
    std::function<std::optional<CursorSpot>(const ElementTree&, const std::string& fullId)> cursorSpot;

    // The area that accepts children, in the element's OWN coordinates;
    // nullopt: the element type takes no children (e.g. label).
    std::function<std::optional<Rect>(const ElementTree&, const std::string& fullId,
                                      const Block& own)>
        contentArea;

    // Optional (nullptr: no scroll). The EFFECTIVE (clamped to the bounds)
    // offset based on the children's extent.
    std::function<std::pair<int, int>(const ElementTree&, const std::string& fullId,
                                      const Rect& area, const Extent& childExtent)>
        scroll;

    // Optional: elements to paint over the element's own buffer AFTER the children
    // are composited (e.g. the scrollbar thumb).
    std::function<void(const ElementTree&, const std::string& fullId, Block& out,
                       const Rect& area, const Extent& childExtent)>
        decorate;
};

class ElementRegistry {
public:
    void add(const std::string& type, ElementKind kind);
    // ToytuiException for an unknown type.
    const ElementKind& get(const std::string& type) const;
    bool has(const std::string& type) const;
    // Listing order of the registered types (param_list): `core`
    // first, the rest alphabetical. Nothing special to do for a new type.
    std::vector<std::string> typeNames() const;
    bool hostsChildren(const ElementTree& tree, const std::string& fullId) const;

    // Optional sink of deferred side effects (daemon mode, `subterm`); not owned.
    void setEffectSink(EffectSink* sink) { sink_ = sink; }
    EffectSink* effectSink() const { return sink_; }

private:
    std::map<std::string, ElementKind> kinds_;
    EffectSink* sink_ = nullptr;
};

// Adding an element to the tree with the type rules checked: whether the parent's
// type can take children (e.g. a label is a leaf). `id`/`parent` come from
// the finished (validated) parameters. Returns the full id.
std::string createElement(ElementTree& tree, const ElementRegistry& registry,
                           const std::string& type, const ElementParams& params);

// Width (`height=false`) or height (`height=true`) of the PARENT's content area from the element's
// parameters (`parent`): the parent type's `contentWidth`/`contentHeight` hook. nullopt: there is no
// (valid) parent, the parent does not exist, or the type has no such hook / no data. Subtracting `posX`/`posY`,
// clamping to 0 and the auto-size rules are the element type's business (select, textarea).
std::optional<int> parentContentSize(const ElementTree& tree, const ElementRegistry& registry,
                                     const ElementParams& params, bool height);

class SubtermRuntime;

// The built-in types: core, box, label, textarea, button, select, input, subterm. `subterms` is the
// daemon's runtime of the `subterm` programs; nullptr (file mode, tests): `subterm` cannot be created.
ElementRegistry makeBuiltinRegistry(SubtermRuntime* subterms = nullptr);

} // namespace toytui
