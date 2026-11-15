// text_filter.h — turns a terminal output byte stream into plain text (for `textout`).
//
// Stateful: sequences and UTF-8 characters may be split across `feed` calls.
// Dropped: escape sequences (CSI, OSC, DCS, APC, PM, SOS, other ESC sequences), C0 controls
// except '\n' and '\t' ('\r' too, so "\r\n" becomes "\n"), DEL, C1 controls (U+0080..U+009F).
// Invalid UTF-8 becomes U+FFFD.
#pragma once

#include <cstddef>
#include <string>

namespace toytui {

class TextFilter {
public:
    // Appends the plain text of `n` input bytes to `out`.
    void feed(const char* data, std::size_t n, std::string& out);

private:
    enum class State { Normal, Esc, EscInter, Csi, Str, StrEsc };
    void normal(unsigned char b, std::string& out);

    State state_ = State::Normal;
    unsigned cp_ = 0;       // code point being decoded
    int need_ = 0;          // continuation bytes still missing
    int total_ = 0;         // length of the sequence being decoded
    unsigned min_ = 0;      // smallest valid code point for that length (overlong check)
};

// Keeps at most `cap` bytes of `buf`: the oldest are dropped, at a character boundary.
void dropOldest(std::string& buf, std::size_t cap);

} // namespace toytui
