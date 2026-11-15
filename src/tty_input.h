// tty_input.h — reading keys from the terminal.
//
// INPUT CHANNEL: the file descriptor on which the call actually
// runs — by default stdin (fd 0). If it is a terminal, we switch it to raw mode
// (echo/canonical mode/ISIG off) and restore it on EXIT (destructor,
// and on a fatal signal too). If it is not a terminal (pipe/file), we read it
// the same way without raw mode — so it can be fed from a script/test. `[CTRL-C]` without
// ISIG is a plain key (in blocking mode it interrupts only the loop, not the program).
//
// Reads BYTE BY BYTE (no read-ahead buffering), so that a `keypress` call leaves
// FURTHER, not yet processed keystrokes in the kernel queue for the
// next call (entering/leaving raw mode does not flush the queue either).
//
// KNOWN LIMITS (not errors, terminal-nature, see keycodec.h):
//   - release events and a lone modifier ([CTRL]) cannot be sensed;
//   - with an unknown/double ESC after an ESC, the byte read in excess
//     in one call is lost at the end of the call (ESC ESC).
//
#pragma once

#include <string>

namespace toytui {

// "No key" notation (`noblock` empty line, or `timeout` expired).
// Clearly distinguishable from every real key (keycodec notates the 0x00
// byte as [CTRL-SPACE], never [NUL]).
inline constexpr const char* kNoKeyToken = "[NUL]";

// Time to wait to tell an ESC sequence from a lone ESC
// (the other bytes come from a real terminal typically within 1-5 ms, over SSH
// more slowly; the 25-50 ms range is customary — the exact value is
// a free choice).
inline constexpr int kEscWindowMs = 40;

struct KeyReadOptions {
    int timeoutMs = -1;    // -1: wait without limit; otherwise this many ms
    bool noblock = false;  // true: does not wait for a key (immediate return)
};

enum class KeyReadStatus {
    Key,   // `token` is a key
    NoKey, // noblock/timeout: no key arrived
    Eof,   // the input is closed (end of pipe / terminal gone)
};

struct KeyReadResult {
    KeyReadStatus status = KeyReadStatus::NoKey;
    std::string token;
};

class InputSession {
public:
    explicit InputSession(int fd = 0);
    ~InputSession();
    InputSession(const InputSession&) = delete;
    InputSession& operator=(const InputSession&) = delete;

    bool isTerminal() const { return raw_; }

    KeyReadResult readKey(const KeyReadOptions& opts);

private:
    int fd_;
    bool raw_ = false;
    std::string pending_; // bytes left over from an earlier read
};

} // namespace toytui
