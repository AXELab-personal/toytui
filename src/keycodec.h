// keycodec.h — uniform text notation of keys: bytes arriving from the terminal ->
// uniform notation, and the reverse direction: processing a notation sequence (e.g.
// `keys_exit:[CTRL-Q]`, `core set keyboard:[HOME][DOWN]`).
//
// NOTATION:
//   - plain character: itself (UTF-8 too), e.g. `a`, `é`, ` ` (space)
//   - special key: `[NAME]`, in UPPERCASE: [ESC] [ENTER] [TAB]
//     [BS] [DEL] [INS] [HOME] [END] [PGUP] [PGDN] [UP] [DOWN] [LEFT]
//     [RIGHT] [F1]..[F24]
//   - with a modifier: `[CTRL-Q]`, `[ALT-F3]`, `[CTRL-ALT-SHIFT-UP]`
//     (canonical order: CTRL, ALT, SHIFT, META). A letter base is always
//     uppercase; an ESC-prefixed uppercase letter (Alt+Shift+a) -> `[ALT-SHIFT-A]`.
//   - characters that would confuse the notation get a NAME: a plain `[` ->
//     `[LBRACKET]` (otherwise it could be mistaken for a notation),
//     next to a modifier `]` -> RBRACKET, `-` -> MINUS, space -> SPACE.
//
// GENERAL (terminal-nature) LIMITS that are NOT coding errors:
//   - the terminal conveys only key PRESSES (and auto-repeat), not releases:
//     a modifier ([CTRL]) held down on its own is not visible;
//   - Ctrl+I = [TAB], Ctrl+M = [ENTER], Ctrl+H = [BS], Ctrl+[ = [ESC]
//     are indistinguishable (old ASCII heritage); 0x0A (LF) ->
//     [CTRL-J] (in raw mode Enter is CR, i.e. [ENTER]).
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace toytui {

enum class DecodeStatus {
    Ok,
    NeedMore, // the buffer holds the start of a not yet finished sequence
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::NeedMore;
    std::string token;        // Ok: the key's notation; EMPTY = input to be
                              // ignored (unknown/malformed)
    std::size_t consumed = 0; // Ok: this many bytes were consumed
};

// Decoding one key from the start of the buffer. `final == false`: input at the
// end of the buffer that may still continue (a lone ESC,
// a truncated CSI/UTF-8) gives NeedMore — the caller calls again after a short time window (about 25-50
// ms) with `final == true`, which closes everything (a
// lone ESC is then `[ESC]`).
DecodeResult decodeKey(std::string_view buf, bool final);

// Decoding a complete byte sequence (final=true), e.g. for tests/simulation.
std::vector<std::string> decodeAllKeys(std::string_view bytes);

// Bringing a single notation to canonical form (lowercase input, order,
// aliases: `[ctrl-q]` -> `[CTRL-Q]`, `[SPACE]` -> ` `). nullopt: invalid.
std::optional<std::string> normalizeKeyToken(std::string_view token);

// Splitting a notation sequence into canonical items: "[HOME][DOWN]ab" ->
// {"[HOME]","[DOWN]","a","b"}. An invalid `[...]` notation -> nullopt.
std::optional<std::vector<std::string>> parseKeySequence(std::string_view text);

} // namespace toytui
