// core_element.h — the `core` element type.
//
// `path`: the target directory for relocating the state file (the actual
// relocation is done by the Session); only format/writability validation
// happens here, the value is stored in absolute, normalised form. Default: "." (the
// startup directory).
#pragma once

#include <string>
#include <vector>

#include "cli_parser.h"
#include "element_registry.h"
#include "param_store.h"

namespace toytui {

// System defaults of core@terminal: 80x25, bg:black (0),
// fg:iwhite (15), tty:/dev/tty. It deliberately does NOT try to query the real
// terminal size (ioctl) — that is the job of resize.
ElementParams makeDefaultCoreParams();

// Hooking core@terminal into the registry (get/set + ownVisual/
// contentArea for the full terminal size) — the registry only
// collects it (`makeBuiltinRegistry`), the logic lives here.
ElementKind coreElementKind();

// The parameters handled by core (the order of get all) — the state file uses this
// to know which core parameter overrides need storing.
const std::vector<std::string>& coreParamNames();

// core get: for the listed (value-less) parameter names returns "name:value"
// lines, in the order given. The "all" pseudo-
// parameter returns every known core parameter, in a fixed, documented
// order.
//
// An unknown parameter name or a value-bearing token in `get` throws an
// ERROR here (not silently dropped) — rationale: the result of a `get` call
// is itself the output, and a silent "nothing" would be indistinguishable
// from a typo; for `set`/`new`, in contrast, the general rule
// explicitly prescribes silent dropping.
std::vector<std::string> coreGet(const ElementParams& core,
                                  const std::vector<ParamToken>& params);

// core set: validated setting of width/height/bgcol/fgcol/tty. Parameters not
// interpreted here are silently dropped. The
// validation happens ENTIRELY before any modification of the store
// ("we write only when we are sure it succeeded"): on error
// `core` stays unchanged.
void coreSet(ElementParams& core, const std::vector<ParamToken>& params);

} // namespace toytui
