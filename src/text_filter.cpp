#include "text_filter.h"

namespace toytui {

namespace {

void appendCp(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

} // namespace

void TextFilter::normal(unsigned char b, std::string& out) {
    if (need_ > 0) {
        if ((b & 0xC0) == 0x80) {
            cp_ = (cp_ << 6) | (b & 0x3F);
            if (--need_ == 0) {
                const bool bad = cp_ < min_ || cp_ > 0x10FFFF || (cp_ >= 0xD800 && cp_ <= 0xDFFF);
                if (bad) {
                    appendCp(out, 0xFFFD);
                } else if (cp_ < 0x80 || cp_ > 0x9F) { // C1 controls are dropped
                    appendCp(out, cp_);
                }
            }
            return;
        }
        need_ = 0; // interrupted sequence: replacement, and the byte is processed anew
        appendCp(out, 0xFFFD);
    }
    if (b == 0x1B) {
        state_ = State::Esc;
    } else if (b == '\n' || b == '\t') {
        out += static_cast<char>(b);
    } else if (b < 0x20 || b == 0x7F) {
        // dropped
    } else if (b < 0x80) {
        out += static_cast<char>(b);
    } else if (b >= 0xC2 && b <= 0xF4) {
        total_ = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
        need_ = total_ - 1;
        cp_ = b & (0x7F >> total_);
        min_ = total_ == 2 ? 0x80 : total_ == 3 ? 0x800 : 0x10000;
    } else {
        appendCp(out, 0xFFFD);
    }
}

void TextFilter::feed(const char* data, std::size_t n, std::string& out) {
    for (std::size_t i = 0; i < n; ++i) {
        const auto b = static_cast<unsigned char>(data[i]);
        switch (state_) {
        case State::Normal:
            normal(b, out);
            break;
        case State::Esc:
            if (need_ > 0) { // an escape inside a multibyte character
                need_ = 0;
                appendCp(out, 0xFFFD);
            }
            if (b == '[') {
                state_ = State::Csi;
            } else if (b == ']' || b == 'P' || b == 'X' || b == '^' || b == '_') {
                state_ = State::Str;
            } else if (b >= 0x20 && b <= 0x2F) {
                state_ = State::EscInter;
            } else if (b == 0x1B) {
                // stays
            } else if (b < 0x20) {
                // a control inside the sequence: dropped
            } else {
                state_ = State::Normal; // two-byte sequence (ESC 7, ESC =, ESC c, ...)
            }
            break;
        case State::EscInter:
            if (b < 0x20 || (b >= 0x20 && b <= 0x2F)) {
                if (b == 0x1B) state_ = State::Esc;
            } else {
                state_ = State::Normal;
            }
            break;
        case State::Csi:
            if (b >= 0x40 && b <= 0x7E) {
                state_ = State::Normal;
            } else if (b == 0x1B) {
                state_ = State::Esc;
            } else if (b == 0x18 || b == 0x1A) {
                state_ = State::Normal;
            } else if (b >= 0x80) {
                state_ = State::Normal;
                normal(b, out);
            }
            break;
        case State::Str:
            if (b == 0x07 || b == 0x18 || b == 0x1A) {
                state_ = State::Normal;
            } else if (b == 0x1B) {
                state_ = State::StrEsc;
            }
            break;
        case State::StrEsc:
            if (b == '\\') {
                state_ = State::Normal;
            } else {
                state_ = State::Esc;
                --i; // the byte starts a new escape sequence
            }
            break;
        }
    }
}

void dropOldest(std::string& buf, std::size_t cap) {
    if (buf.size() <= cap) {
        return;
    }
    std::size_t cut = buf.size() - cap;
    while (cut < buf.size() && (static_cast<unsigned char>(buf[cut]) & 0xC0) == 0x80) {
        ++cut; // not in the middle of a character
    }
    buf.erase(0, cut);
}

} // namespace toytui
