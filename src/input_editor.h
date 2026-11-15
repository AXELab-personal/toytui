// input_editor.h — the state machine of the input field editor (without a screen): per-block text, cursor,
// insert/overwrite, automatic jump, producing and reading back the value (`value`).
//
// The cursor always stands in one block (`blockIndex`) at the `offset` position of the block text (0..length). SGR in the pattern
// is display only: `value` never contains it, `display` gives it as separate pieces.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "input_pattern.h"

namespace toytui {

struct InputEditorOptions {
    bool overwrite = false; // the element's `inputMode` default (insert = false)
    bool advance = true;    // the element's `advance` default
};

// One piece to display: a cell (character or placeholder) or an SGR sequence (width 0).
struct DisplayPiece {
    std::string text;       // UTF-8 (`ESC[...m` for SGR)
    int width = 1;
    int block = -1;         // index of the block; -1: literal or SGR
    bool placeholder = false;
    bool sgr = false;
};

// Checking the block-level options (`%mode:`, `%adv:`). On error false and `err` filled in.
bool validateBlockOptions(const Pattern& p, std::string& err);

class InputEditor {
public:
    InputEditor(Pattern pattern, InputEditorOptions opts);

    // Editing: true if the field's content or the cursor changed.
    bool insertChar(char32_t cp);
    bool backspace();
    bool del();
    bool left();
    bool right();
    bool home();     // to the start of the field (first block, 0)
    bool end();      // to the end of the field (last block, end of the text)
    bool nextBlock(); // to the start of the next block
    bool prevBlock(); // to the start of the previous block
    void toggleMode(); // insert <-> overwrite (the current deviation from the configured one)
    void clear();

    // Value: the visible text formatted according to the pattern (literals + blocks; a bounded block filled with its
    // placeholder; without SGR). `setValue` reads this form back (empty text = empty field).
    std::string value() const;
    bool setValue(const std::string& v, std::string& err);

    // Restoring the state (stored block texts + cursor). For invalid text (a character or length that does not
    // fit the block) false, and the editor stays empty; the cursor is clamped to the bounds.
    bool restore(const std::vector<std::string>& blocks, std::size_t block, std::size_t offset);

    // The raw text of the blocks (without placeholders and literals).
    std::vector<std::string> blockValues() const;
    // Whether every block reaches its minimum length.
    bool complete() const;

    std::vector<DisplayPiece> display() const;
    std::size_t totalWidth() const;
    std::size_t cursorColumn() const;

    std::size_t blockIndex() const { return block_; }
    std::size_t offset() const { return offset_; }
    std::size_t blockCount() const { return pattern_.blocks.size(); }
    const Pattern& pattern() const { return pattern_; }
    bool effectiveOverwrite() const;
    bool effectiveAdvance() const;

private:
    std::size_t totalChars() const;
    bool matchFrom(std::size_t seg, const std::u32string& v, std::size_t pos,
                   std::vector<std::u32string>& out) const;

    Pattern pattern_;
    InputEditorOptions opts_;
    std::vector<std::u32string> texts_;
    std::size_t block_ = 0;
    std::size_t offset_ = 0;
    bool toggled_ = false;
};

} // namespace toytui
