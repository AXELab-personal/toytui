#include "list_value.h"

#include "utf_escape.h"

namespace toytui {

std::vector<std::string> splitEscapedList(const std::string& raw, char separator, bool unicode) {
    std::vector<std::string> items;
    if (raw.empty()) {
        return items;
    }
    std::string cur;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (unicode && c == '\\' && i + 1 < raw.size() && raw[i + 1] == 'u') {
            if (const auto esc = decodeUnicodeEscape(raw, i)) {
                cur += esc->utf8;
                i += esc->consumed - 1;
                continue;
            }
        }
        if (c == '\\' && i + 1 < raw.size() && (raw[i + 1] == separator || raw[i + 1] == '\\')) {
            cur += raw[i + 1];
            ++i;
        } else if (c == separator) {
            items.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    items.push_back(std::move(cur));
    return items;
}

} // namespace toytui
