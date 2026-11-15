#include "input_editor.h"

#include <algorithm>

#include "param_types.h"

namespace toytui {

namespace {

std::optional<bool> parseOverwrite(const std::string& s) {
    if (s == "insert" || s == "ins") return false;
    if (s == "overwrite" || s == "owr") return true;
    return std::nullopt;
}

} // namespace

bool validateBlockOptions(const Pattern& p, std::string& err) {
    for (const auto& b : p.blocks) {
        if (b.mode && !parseOverwrite(*b.mode)) {
            err = "Invalid block option `%mode:" + *b.mode + "` (expected insert/overwrite/ins/owr)";
            return false;
        }
        if (b.adv && !parseBoolValue(*b.adv)) {
            err = "Invalid block option `%adv:" + *b.adv + "` (expected a boolean, e.g. on/off)";
            return false;
        }
    }
    return true;
}

InputEditor::InputEditor(Pattern pattern, InputEditorOptions opts)
    : pattern_(std::move(pattern)), opts_(opts), texts_(pattern_.blocks.size()) {}

bool InputEditor::effectiveOverwrite() const {
    const InputBlock& b = pattern_.blocks[block_];
    bool base = opts_.overwrite;
    if (b.mode) {
        if (const auto m = parseOverwrite(*b.mode)) base = *m;
    }
    return base != toggled_;
}

bool InputEditor::effectiveAdvance() const {
    const InputBlock& b = pattern_.blocks[block_];
    if (b.adv) {
        if (const auto v = parseBoolValue(*b.adv)) return *v;
    }
    return opts_.advance;
}

std::size_t InputEditor::totalChars() const {
    std::size_t n = 0;
    for (const auto& t : texts_) n += t.size();
    return n;
}

void InputEditor::toggleMode() { toggled_ = !toggled_; }

void InputEditor::clear() {
    for (auto& t : texts_) t.clear();
    block_ = 0;
    offset_ = 0;
}

bool InputEditor::insertChar(char32_t cp) {
    const InputBlock& b = pattern_.blocks[block_];
    std::u32string& text = texts_[block_];
    if (effectiveOverwrite() && offset_ < text.size()) {
        std::u32string rest = text;
        rest.erase(offset_, 1);
        const auto a = acceptCodePoint(b, rest, offset_, cp);
        if (!a) return false;
        text[offset_] = *a;
    } else {
        if (text.size() >= b.maxLen || totalChars() >= kInputMaxChars) return false;
        const auto a = acceptCodePoint(b, text, offset_, cp);
        if (!a) return false;
        text.insert(text.begin() + static_cast<std::ptrdiff_t>(offset_), *a);
    }
    ++offset_;
    if (effectiveAdvance() && !b.unlimited && texts_[block_].size() == b.maxLen && offset_ == b.maxLen &&
        block_ + 1 < pattern_.blocks.size()) {
        ++block_;
        offset_ = 0;
    }
    return true;
}

bool InputEditor::backspace() {
    if (offset_ > 0) {
        texts_[block_].erase(offset_ - 1, 1);
        --offset_;
        return true;
    }
    if (block_ == 0) return false;
    --block_;
    offset_ = texts_[block_].size();
    if (offset_ > 0) {
        texts_[block_].erase(offset_ - 1, 1);
        --offset_;
    }
    return true;
}

bool InputEditor::del() {
    if (offset_ >= texts_[block_].size()) return false;
    texts_[block_].erase(offset_, 1);
    return true;
}

bool InputEditor::left() {
    if (offset_ > 0) {
        --offset_;
        return true;
    }
    if (block_ == 0) return false;
    --block_;
    offset_ = texts_[block_].size();
    return true;
}

bool InputEditor::right() {
    if (offset_ < texts_[block_].size()) {
        ++offset_;
        return true;
    }
    if (block_ + 1 >= pattern_.blocks.size()) return false;
    ++block_;
    offset_ = 0;
    return true;
}

bool InputEditor::home() {
    const bool changed = block_ != 0 || offset_ != 0;
    block_ = 0;
    offset_ = 0;
    return changed;
}

bool InputEditor::end() {
    const std::size_t lastBlock = pattern_.blocks.size() - 1;
    const std::size_t lastOff = texts_[lastBlock].size();
    const bool changed = block_ != lastBlock || offset_ != lastOff;
    block_ = lastBlock;
    offset_ = lastOff;
    return changed;
}

bool InputEditor::nextBlock() {
    if (block_ + 1 >= pattern_.blocks.size()) return false;
    ++block_;
    offset_ = 0;
    return true;
}

bool InputEditor::prevBlock() {
    if (block_ == 0) return false;
    --block_;
    offset_ = 0;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Value
// ---------------------------------------------------------------------------------------------

std::string InputEditor::value() const {
    std::string out;
    for (const auto& seg : pattern_.segments) {
        if (seg.kind == PatternSegment::Kind::Literal) {
            out += seg.text;
        } else if (seg.kind == PatternSegment::Kind::Block) {
            const InputBlock& b = pattern_.blocks[seg.block];
            const std::u32string& t = texts_[seg.block];
            out += fromU32(t);
            if (!b.unlimited) {
                for (std::size_t k = t.size(); k < b.maxLen; ++k) out += b.placeholder;
            }
        }
    }
    return out;
}

bool InputEditor::restore(const std::vector<std::string>& blocks, std::size_t block, std::size_t offset) {
    clear();
    std::vector<std::u32string> t(pattern_.blocks.size());
    std::size_t total = 0;
    for (std::size_t k = 0; k < t.size() && k < blocks.size(); ++k) {
        const InputBlock& b = pattern_.blocks[k];
        const std::u32string raw = toU32(blocks[k]);
        if (raw.size() > b.maxLen) return false;
        std::u32string built;
        for (char32_t c : raw) {
            const auto a = acceptCodePoint(b, built, built.size(), c);
            if (!a || *a != c) return false;
            built.push_back(c);
        }
        total += built.size();
        t[k] = std::move(built);
    }
    if (total > kInputMaxChars) return false;
    texts_ = std::move(t);
    block_ = std::min(block, pattern_.blocks.size() - 1);
    offset_ = std::min(offset, texts_[block_].size());
    return true;
}

std::vector<std::string> InputEditor::blockValues() const {
    std::vector<std::string> v;
    for (const auto& t : texts_) v.push_back(fromU32(t));
    return v;
}

bool InputEditor::complete() const {
    for (std::size_t k = 0; k < texts_.size(); ++k) {
        if (texts_[k].size() < pattern_.blocks[k].minLen) return false;
    }
    return true;
}

bool InputEditor::matchFrom(std::size_t seg, const std::u32string& v, std::size_t pos,
                            std::vector<std::u32string>& out) const {
    if (seg == pattern_.segments.size()) return pos == v.size();
    const PatternSegment& s = pattern_.segments[seg];
    if (s.kind == PatternSegment::Kind::Sgr) return matchFrom(seg + 1, v, pos, out);
    if (s.kind == PatternSegment::Kind::Literal) {
        const std::u32string lit = toU32(s.text);
        if (v.compare(pos, lit.size(), lit) != 0 || pos + lit.size() > v.size()) return false;
        return matchFrom(seg + 1, v, pos + lit.size(), out);
    }
    const InputBlock& b = pattern_.blocks[s.block];
    if (!b.unlimited) {
        if (pos + b.maxLen > v.size()) return false;
        std::u32string cells = v.substr(pos, b.maxLen);
        const std::u32string ph = toU32(b.placeholder);
        while (!cells.empty() && cells.back() == ph[0]) cells.pop_back();
        if (!cells.empty() && !blockTextValid(b, cells)) return false; // an empty block is accepted with any minimum
        out[s.block] = cells;
        return matchFrom(seg + 1, v, pos + b.maxLen, out);
    }
    // unbounded block: we try backwards from the longest valid prefix (the continuation decides)
    std::u32string built;
    for (std::size_t k = pos; k < v.size() && built.size() < b.maxLen; ++k) {
        const auto a = acceptCodePoint(b, built, built.size(), v[k]);
        if (!a || *a != v[k]) break;
        built.push_back(v[k]);
    }
    for (std::size_t n = built.size() + 1; n-- > 0;) {
        if (n > 0 && n < b.minLen) break;
        out[s.block] = built.substr(0, n);
        if (matchFrom(seg + 1, v, pos + n, out)) return true;
    }
    return false;
}

bool InputEditor::setValue(const std::string& value, std::string& err) {
    if (value.empty()) {
        clear();
        return true;
    }
    const std::u32string v = toU32(value);
    std::vector<std::u32string> out(pattern_.blocks.size());
    if (!matchFrom(0, v, 0, out)) {
        err = "The value does not match the pattern: `" + value + "`";
        return false;
    }
    std::size_t total = 0;
    for (const auto& t : out) total += t.size();
    if (total > kInputMaxChars) {
        err = "The value exceeds the 512 character limit";
        return false;
    }
    texts_ = std::move(out);
    block_ = 0;
    offset_ = 0;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Display
// ---------------------------------------------------------------------------------------------

std::vector<DisplayPiece> InputEditor::display() const {
    std::vector<DisplayPiece> pieces;
    for (const auto& seg : pattern_.segments) {
        if (seg.kind == PatternSegment::Kind::Sgr) {
            DisplayPiece p;
            p.text = seg.text;
            p.width = 0;
            p.sgr = true;
            pieces.push_back(p);
        } else if (seg.kind == PatternSegment::Kind::Literal) {
            DisplayPiece p;
            p.text = seg.text;
            p.width = seg.width;
            pieces.push_back(p);
        } else {
            const InputBlock& b = pattern_.blocks[seg.block];
            const std::u32string& t = texts_[seg.block];
            for (char32_t c : t) {
                DisplayPiece p;
                p.text = fromU32(std::u32string(1, c));
                p.block = static_cast<int>(seg.block);
                pieces.push_back(p);
            }
            if (!b.unlimited) {
                for (std::size_t k = t.size(); k < b.maxLen; ++k) {
                    DisplayPiece p;
                    p.text = b.placeholder;
                    p.block = static_cast<int>(seg.block);
                    p.placeholder = true;
                    pieces.push_back(p);
                }
            }
        }
    }
    return pieces;
}

std::size_t InputEditor::totalWidth() const {
    std::size_t w = 0;
    for (const auto& p : display()) w += static_cast<std::size_t>(p.width);
    return w;
}

std::size_t InputEditor::cursorColumn() const {
    std::size_t col = 0;
    for (const auto& seg : pattern_.segments) {
        if (seg.kind == PatternSegment::Kind::Literal) {
            col += static_cast<std::size_t>(seg.width);
        } else if (seg.kind == PatternSegment::Kind::Block) {
            if (seg.block == block_) return col + offset_;
            const InputBlock& b = pattern_.blocks[seg.block];
            col += b.unlimited ? texts_[seg.block].size() : b.maxLen;
        }
    }
    return col;
}

} // namespace toytui
