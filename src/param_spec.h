// param_spec.h — table-driven parameter validation (the generally
// enforceable rules in one place). The element types
// (box, label, ...) provide a ParamSpec table; canonicalisation,
// the (atomic) `new`/`set` application and `get` are shared here.
// The table of general parameters — kGeneralSpecs — applies to every
// element type.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cli_parser.h"
#include "param_store.h"

namespace toytui {

enum class Kind {
    Id, Parent, Size, Signed, ZIndex, Color, Border, TitlePos, Bool, Text,
    // An integer in the range [minValue, maxValue] (e.g. textarea `tabsize`).
    Int,
    // Enumeration: the `choices` table gives the canonical forms and aliases
    // (e.g. textarea `wrap`, `align`); the stored value is always the canonical form.
    Enum,
    // A processed text view DERIVED from a raw (`*_raw`) field (label `text`, box `title`):
    // the form (ANSI) re-encoded from the cell model — the length limit applies
    // to the RAW field, not to this. For `get`/`param_list` purposes it is text
    // (behaves as Kind::Text).
    TextView,
    Attrib
};

inline bool isTextKind(Kind k) { return k == Kind::Text || k == Kind::TextView; }

struct ParamSpec {
    const char* name;
    Kind kind;
    // The `auto` value: "for any parameter where it makes sense...
    // sets the parameter to unset... it behaves as a value derived from the parent OR
    // THE SYSTEM DEFAULT".
    // This module handles BOTH cases:
    //   - `unsettable == true`: the parameter's default is ITSELF
    //     unset (inherited: w/h/colours, scrollbar position, min/max
    //     offset) — `auto` -> explicit Unset state.
    //   - `unsettable == false`, but `fixedDefault != nullptr`: the
    //     parameter has a FIXED (non-inherited) system default
    //     (e.g. posX/posY:0, z_index:1, border:off) — `auto` writes that
    //     concrete value back (functionally the same as if it had
    //     never been modified).
    //   - both false/nullptr (e.g. `id`): `auto` is an invalid value.
    // `title`/`text` (Kind::Text) are INTENTIONALLY not unsettable and have no
    // fixedDefault: there `auto` is the literal text.
    bool unsettable = false;
    const char* fixedDefault = nullptr;
    // Length limit of Kind::Text in bytes; 0 = the general limit
    // (kDefaultTextLimit, 512 B). Exception: `text:` (label `text_raw`)
    // is 512 KiB — the general 512 B remains an operational limit
    // for the parameters of new elements regardless.
    std::size_t maxLen = 0;
    // Derived view (Kind::TextView): NOT stored; the `derive` function computes it
    // from the raw field named by `derivedFrom`. Writing it
    // (`set text:X`) sets the raw field; reading it (`get`, `param_list`)
    // gives the view computed from the raw one. Only the raw (`Kind::Text`) is stored.
    const char* derivedFrom = nullptr;
    std::string (*derive)(const std::string& raw) = nullptr;
    // Kind::Size: lower bound `minValue` (default 0) and upper bound `maxValue` (0 = the
    // general kMaxSizeValue, 512); Kind::Int: the range is [minValue, maxValue].
    // (Textarea: `width` min. 1, `height` max. 2^20, `tabsize` 1–16.)
    int minValue = 0;
    int maxValue = 0;
    // Kind::Enum: `canonical=alias1,alias2;canonical2=alias;...` (the
    // aliases are optional, the canonical form is always accepted). The first
    // item is NOT automatically the default: that is given by `fixedDefault`.
    const char* choices = nullptr;
    // Write-only parameter (e.g. textarea `text_add`): `get <name>` is an error,
    // `get all` and `param_list` do not list it.
    bool writeOnly = false;
};
constexpr int kMaxSizeValue = 512;            // general element size limit
constexpr int kMaxSignedValue = 1 << 20;      // Signed range ±2^20
constexpr std::size_t kDefaultTextLimit = 512;
constexpr std::size_t kTextParamLimit = 512 * 1024;
using SpecTable = std::vector<ParamSpec>;

// id, parent, posX, posY, width, height, bgcol, fgcol, attrib, z_index.
extern const SpecTable kGeneralSpecs;

// The readable value of a parameter: the stored value, or for a derived view the
// view computed from the raw one. nullopt = not set (unset).
std::optional<std::string> paramView(const ElementParams& elem, const ParamSpec& spec);

SpecTable concatSpecs(const SpecTable& a, const SpecTable& b);

// The order of `get all` is the table order.
const ParamSpec* findSpec(const SpecTable& specs, const std::string& name);

// Validating a value + bringing it to canonical (stored) form; on failure a
// ToytuiException (the error message quotes the literal parameter).
// `target` is the error message target (e.g. "box@b1"; "-" if not yet known).
std::string canonicalize(const std::string& method, const std::string& target,
                          const ParamSpec& spec, const std::string& value);

// The error message target: "<type>@<id>" for `new` from the tokens
// TO BE VALIDATED, for `set`/`get` from the ALREADY EXISTING element; "-" if the id is
// not yet known. Shared helper of box/label.
std::string elementTarget(const std::string& type, const std::vector<ParamToken>& params);
std::string elementTarget(const std::string& type, const ElementParams& elem);

// Shared `new`/`set` logic: ALL validation happens before the store is modified
// (atomic); `auto` behaves as per the rule above. A parameter with an unknown
// name is silently dropped; for `new` the `id` is mandatory;
// `set` does not modify the `id`. `target` is for error messages.
void applyParams(ElementParams& elem, const std::vector<ParamToken>& params,
                  const SpecTable& specs, bool isNew, const std::string& target);

// `get`: stored level, unset -> the literal "auto" (`get` returns the same word
// that `set` accepts for resetting to unset). An unknown name / value-bearing
// token -> error (see
// core_element.h). `target` is for error messages.
std::vector<std::string> getParams(const ElementParams& elem,
                                    const std::vector<ParamToken>& params,
                                    const SpecTable& specs, const std::string& target);

} // namespace toytui
