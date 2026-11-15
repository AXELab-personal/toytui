// subterm_element.h — the `subterm` element type: a program running on an emulated terminal
// (daemon mode only). The element stores parameters (`cmd`, `cwd`, `scrollback`, `keys_exit`);
// the running program, `exec`, `pid`, `exit_code`, `textout`, `send`, `signal` live in the
// SubtermRuntime. See the specification, 5.8 and 15.9.
#pragma once

#include <utility>

#include "element_registry.h"
#include "subterm_runtime.h"

namespace toytui {

// `host == nullptr` (file mode): `subterm` cannot be created, and runtime parameters are errors.
ElementKind subtermElementKind(SubtermRuntime* host);

// The terminal size (width, height) of the element: explicit `width`/`height`, otherwise the
// parent's content area minus the position; clamped to 1..512.
std::pair<int, int> subtermSize(const ElementTree& tree, const ElementRegistry& registry, const std::string& fullId);

} // namespace toytui
