// textarea_element.h — the `textarea` element type: multi-line, wrapped,
// aligned text; the formatting lives in the `text_layout` module, the element only
// binds the parameters to it. A leaf element, windowed (`measure` + `ownVisualWindow`).
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "cli_parser.h"
#include "element_registry.h"
#include "param_store.h"

namespace toytui {

constexpr int kMaxTextareaHeight = 1 << 20; // more than the line count resulting from 512 KiB of text

// Defaults: parent core@terminal, posX/posY 0, z_index 1, tabsize 4, wrap full,
// align left; width/height/bgcol/fgcol/attrib/text_raw unset.
ElementParams makeDefaultTextareaParams();

// `freeText` + `freeTextName` are the command's free-text block (`text:` or
// `text_add:`). `text:` CLEARS the raw buffer and reloads it; `text_add:`
// appends to the END of the buffer (the size after the operation is checked, on error nothing
// is modified). `new`/`set` are atomic through the shared table engine (`applyParams`).
ElementParams textareaNew(const std::vector<ParamToken>& params,
                          const std::optional<std::string>& freeText,
                          const std::string& freeTextName);
void textareaSet(ElementParams& p, const std::vector<ParamToken>& params,
                 const std::optional<std::string>& freeText, const std::string& freeTextName);
std::vector<std::string> textareaGet(const ElementParams& p, const std::vector<ParamToken>& params);

ElementKind textareaElementKind();

} // namespace toytui
