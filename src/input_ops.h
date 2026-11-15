// input_ops.h — key delivery according to the focus: the `core` `setfocus` parameter decides.
//   - `auto` (or pointing at an element that no longer exists): the key's
//     notation goes to stdout, the state is NOT modified.
//   - otherwise the focused element receives the key (`ElementKind::onKey`);
//     whether it modifies anything is the element's business — if so, a redraw + save is needed.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "element_registry.h"
#include "element_tree.h"

namespace toytui {

// The core's STORED `setfocus` value (`auto` if none). Readable from a core-only session too (it does not
// check that the target exists — the `del` hook maintains that, and delivery decides on the full tree).
std::string rawFocus(const ElementTree& tree);

// The value of the `core` `setfocus`; `auto` if there is no focus OR the target
// no longer exists (defensive: `del` normally already
// reset it to `auto`, but after an external modification we must not fail either).
std::string currentFocus(const ElementTree& tree);

struct KeyDelivery {
    bool toStdout = false;   // true: the caller writes out `stdoutText`
    bool changed = false;    // true: the element modified the state
    bool exited = false;     // true: a DECLARED exit key of the element arrived (stdoutText: `exit:<k> name:value...`)
    std::string stdoutText;  // only for toStdout
};

KeyDelivery deliverKey(ElementTree& tree, const ElementRegistry& registry, const std::string& key);

// --- `core get keyboard` / `core set keyboard:[...]` ---------------

// The value of `keys_exit:`: a list of ONE OR MORE keys, without separator
// (`[CTRL-Q]`, `[TAB] [ENTER]q`: Tab, space, Enter, q), in canonical form
// (`[ctrl-q]` -> `[CTRL-Q]`); repetition is not an error. nullopt: invalid or empty.
std::optional<std::vector<std::string>> parseExitKeys(const std::string& text);

// Concatenating keys without a separator (the inverse of `parseExitKeys`: `{"[TAB]","q"}` -> `[TAB]q`).
std::string joinKeys(const std::vector<std::string>& keys);

// Delivering a key sequence (simulated replay): through the SAME
// code path (`deliverKey`) as one step of the blocking loop. Without focus
// (`auto`) the keys' notations are concatenated into `stdoutText`
// (as in the `keyboard` loop); `changed` is true if any element
// modified the state (a redraw + save is then needed).
struct KeyReplay {
    std::string stdoutText;
    bool changed = false;
    bool exited = false; // an exit key interrupted the replay
};
KeyReplay replayKeys(ElementTree& tree, const ElementRegistry& registry,
                     const std::vector<std::string>& keys);

// Protection of the blocking loop ("focus + delete clash"): if the focus
// has changed since the loop started (typically: the target element was deleted meanwhile,
// so it reverted to `auto`), the next forwarding attempt
// reports an error. `expected`: the focus at the start of the loop.
void requireFocusUnchanged(const ElementTree& tree, const std::string& expected);

} // namespace toytui
