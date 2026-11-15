// element_ops.h — type-independent execution of the CLI methods (new/set/get/del)
// over the ElementTree + ElementRegistry. There is no
// `if (type == "box")` here: every element type speaks through its registered
// handlers.
//
// `set` is COMPREHENSIVELY atomic ("we write only when we are sure it
// succeeded"): the type's validation and all checks of a possible parent change
// (exists, can take children, no cycle) run before the first
// modification.
#pragma once

#include <string>
#include <vector>

#include "cli_parser.h"
#include "element_registry.h"
#include "element_tree.h"

namespace toytui {

// Full id of the command's target element: `core` -> `core@terminal`;
// otherwise from the `id:` parameter — in short (`b1`) and full
// (`box@b1`) form alike; the type of the full form must match the call's
// element type. Missing `id:` / type mismatch -> error.
std::string targetFullId(const ParsedCommand& cmd);

// `new`: returns the full id.
std::string execNew(ElementTree& tree, const ElementRegistry& registry, const ParsedCommand& cmd);

// `set`: the type's `set` handler + re-parenting (`parent:`). An error
// for a non-existent element.
void execSet(ElementTree& tree, const ElementRegistry& registry, const ParsedCommand& cmd);

// The shared core of `get`, from an element's STORED parameters (leaving out the `id:` target
// token) — the index-based fast read of the state file calls this too,
// without building the whole tree.
std::vector<std::string> getFromParams(const ElementRegistry& registry, const std::string& type,
                                        const ElementParams& params, const ParsedCommand& cmd);

// `get`: the `name:value` lines of the requested parameters. An error for a non-existent element.
std::vector<std::string> execGet(const ElementTree& tree, const ElementRegistry& registry,
                                  const ParsedCommand& cmd);

// `del`: cascade delete through the garbage collector; the full ids of the
// deleted elements (post-order). Besides `id:` it uses no other parameter
// (the rest are silently ignored).
std::vector<std::string> execDel(ElementTree& tree, const ElementRegistry& registry,
                                  const ParsedCommand& cmd);

// `core get param_list <params/all>`: lists ALL elements of the TREE grouped by
// type (in the built-in registration order: core,
// box, label, ...), within that in creation order, one line =
// one element, in `fullId;name:value;name:value;...` CSV form. Only those
// parameters go into a line that the ELEMENT'S TYPE knows (from the
// requested list); if none, the element's line is omitted. For an explicit
// name list the order is the one given on the CLI; for `all` it is the
// type's canonical (general, then element-specific) order.
// Kind::Text values are quoted, and ALWAYS go to the END of the line
// (if there are several, their relative order is kept) — because
// they may contain `;`. `params[0]` is the caller's responsibility ("param_list"
// bare token); the tokens AFTER `param_list` go here.
std::vector<std::string> execParamList(const ElementTree& tree, const ElementRegistry& registry,
                                        const std::vector<ParamToken>& params);

} // namespace toytui
