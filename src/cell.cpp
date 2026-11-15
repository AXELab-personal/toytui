#include "cell.h"

#include <algorithm>
#include <array>

namespace toytui {

std::optional<std::uint16_t> namedAttribBit(std::string_view canonicalName) {
    for (const auto& na : kNamedAttribs) {
        if (na.name == canonicalName) {
            return na.bit;
        }
    }
    return std::nullopt;
}

std::optional<std::uint8_t> colorNameToIndex(std::string_view name) {
    struct NamedColor {
        std::string_view name;
        std::uint8_t index;
    };
    static constexpr std::array<NamedColor, 16> kColors{{
        {"black", 0}, {"red", 1}, {"green", 2}, {"yellow", 3},
        {"blue", 4}, {"magenta", 5}, {"cyan", 6}, {"white", 7},
        {"iblack", 8}, {"ired", 9}, {"igreen", 10}, {"iyellow", 11},
        {"iblue", 12}, {"imagenta", 13}, {"icyan", 14}, {"iwhite", 15},
    }};
    for (const auto& c : kColors) {
        if (c.name == name) {
            return c.index;
        }
    }
    return std::nullopt;
}

void setCellChar(Cell& cell, std::string_view utf8Char) {
    const std::size_t n = std::min<std::size_t>(utf8Char.size(), 4);
    for (std::size_t i = 0; i < 4; ++i) {
        cell.ch[i] = i < n ? static_cast<unsigned char>(utf8Char[i]) : 0;
    }
}

std::string getCellChar(const Cell& cell) {
    if (cell.ch[0] == 0) {
        return " ";
    }
    std::string s;
    for (unsigned char b : cell.ch) {
        if (b == 0) {
            break;
        }
        s.push_back(static_cast<char>(b));
    }
    return s;
}

void repairWidePairs(Cell* row, int count) {
    const auto blank = [](Cell& c) {
        setCellChar(c, " ");
        c.attrib = static_cast<std::uint16_t>(c.attrib & ~(attrib_bits::WIDE_LEFT | attrib_bits::WIDE_RIGHT));
    };
    for (int i = 0; i < count; ++i) {
        if (isWideLeft(row[i])) {
            if (i + 1 < count && isWideRight(row[i + 1]) && !isWideLeft(row[i + 1])) {
                ++i; // intact pair
            } else {
                blank(row[i]);
            }
        } else if (isWideRight(row[i])) {
            blank(row[i]);
        }
    }
}

void repairWidePairs(std::vector<Cell>& cells) {
    repairWidePairs(cells.data(), static_cast<int>(cells.size()));
}

} // namespace toytui
