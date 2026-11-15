#include "cli_parser.h"

#include "error.h"
#include "ansi_handler.h"
#include "utf_escape.h"

namespace toytui {

// The specification's alias table — ONLY the real, top-level parameter-NAME
// aliases. The underline/ul, strikethrough/strike, inverse/reverse rows
// are intentionally missing here: those are aliases of attribute NAMES
// inside the `attrib:"..."` value list, not separate top-level param:val keys
// (see the header comment of cell.h).
const std::vector<ParamAlias> kParamNameAliases = {
    {"x", "posX"}, {"y", "posY"},
    {"w", "width"}, {"h", "height"},
    {"bg", "bgcol"}, {"fg", "fgcol"},
    {"brdfg", "border_fgcol"}, {"brdbg", "border_bgcol"},
    {"scrollX", "scrollbarX"}, {"scrollY", "scrollbarY"},
    {"cursor_bg", "cursor_bgcol"}, {"cursor_fg", "cursor_fgcol"}, {"cbg", "cursor_bgcol"}, {"cfg", "cursor_fgcol"},
    {"sel_bg", "sel_bgcol"}, {"sel_fg", "sel_fgcol"}, {"sbg", "sel_bgcol"}, {"sfg", "sel_fgcol"},
    {"focus_bg", "focus_bgcol"}, {"focus_fg", "focus_fgcol"},
    {"pressed_bg", "pressed_bgcol"}, {"pressed_fg", "pressed_fgcol"},
    {"scbg", "scroll_bgcol"}, {"scfg", "scroll_fgcol"},
    {"adv", "advance"},
    {"offsX", "offsetX"}, {"offsY", "offsetY"},
    {"minOffsX", "minOffsetX"}, {"minOffsY", "minOffsetY"},
    {"maxOffsX", "maxOffsetX"}, {"maxOffsY", "maxOffsetY"},
};

namespace {

std::string resolveAlias(const std::string& name) {
    for (const auto& a : kParamNameAliases) {
        if (name == a.alias) {
            return a.canonical;
        }
    }
    return name;
}

// Returns the name of the token (the part before the colon, or the whole token
// if there is no colon).
std::string tokenName(const std::string& token) {
    auto pos = token.find(':');
    return pos == std::string::npos ? token : token.substr(0, pos);
}

// Text-input parameters: here (and only here) the parser resolves the `\u[HEX]` escape. Only parameters
// whose value is TEXT DATA may go here: the resolved character must not get a structural role.
// (`input` `value`, `placeholder`: data / a single character.) Values that carry structure — the `input`
// `pattern` and the `select` `options` list — resolve `\u[HEX]` themselves in their own escape layer
// (`input_pattern`, `splitEscapedList`), otherwise `\u[7c]` (`|`) or `\u[7b]` (`{`) would rewrite the structure.
bool isTextInput(const std::string& canonicalName) {
    return canonicalName == "text" || canonicalName == "text_add" || canonicalName == "title" ||
           canonicalName == "value" || canonicalName == "placeholder";
}

} // namespace

bool isValidParamName(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    for (char c : name) {
        bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

std::string unescapeValue(const std::string& raw, bool unicode) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        // `\\e[` (and `\\033[`, `\\x1b[`): the protected (literal) ANSI introducer is resolved by the ANSI layer
        // (pairs become a literal `\`); if `\\` -> `\` happened here, the protection would be lost and the text would become an introducer again.
        if (raw[i] == '\\') {
            if (const std::size_t run = backslashRunBeforeIntroducerBody(raw, i); run >= 2) {
                out.append(raw, i, run);
                i += run - 1;
                continue;
            }
        }
        if (raw[i] == '\\' && i + 1 < raw.size() &&
            (raw[i + 1] == '"' || raw[i + 1] == '\'' || raw[i + 1] == '\\')) {
            out.push_back(raw[i + 1]);
            ++i;
        } else if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == 'n') {
            out.push_back('\n'); // `\n` -> newline (shell convention)
            ++i;
        } else if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == 't') {
            out.push_back('\t'); // `\t` -> tab
            ++i;
        } else if (unicode && raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == 'u') {
            if (const auto esc = decodeUnicodeEscape(raw, i)) {
                out += esc->utf8; // `\u[HEX]` -> the UTF-8 form of the code point
                i += esc->consumed - 1;
            } else {
                out.push_back(raw[i]); // not a valid code: stays literal
            }
        } else {
            out.push_back(raw[i]);
        }
    }
    return out;
}

ParsedCommand parseCommandLine(const std::vector<std::string>& args,
                               const std::vector<std::string>& freeTextNames) {
    if (args.size() < 2) {
        throw ToytuiException(
            ToytuiError{"-", "-",
                        "Missing required positional arguments: "
                        "`<elemtipus> <metodus>`"});
    }

    ParsedCommand cmd;
    cmd.elementType = args[0];
    cmd.method = args[1];

    // The "remaining" tokens, from position 2.
    std::vector<std::string> rest(args.begin() + 2, args.end());

    // Step 1 (MANDATORY ORDER): splitting off the block with the "text:" prefix
    // BEFORE interpreting anything else as param:val.
    std::size_t textIdx = rest.size();
    std::string textName = "text";
    for (std::size_t i = 0; i < rest.size() && textIdx == rest.size(); ++i) {
        for (const auto& name : freeTextNames) {
            const std::string prefix = name + ":";
            if (rest[i].rfind(prefix, 0) == 0) { // prefix match
                textIdx = i;
                textName = name;
                break;
            }
        }
    }

    if (textIdx < rest.size()) {
        cmd.freeTextName = textName;
        std::string joined = rest[textIdx].substr(textName.size() + 1); // "name:" cut off
        for (std::size_t i = textIdx + 1; i < rest.size(); ++i) {
            joined += ' ';
            joined += rest[i];
        }
        cmd.freeText = unescapeValue(joined, true); // `text`/`text_add`: text input
        rest.resize(textIdx); // only the part BEFORE text: remains as params
    }

    // Step 2: splitting the remaining tokens into param:val (or value-less)
    // pairs, with alias resolution.
    for (const auto& token : rest) {
        std::string name = tokenName(token);
        std::string method = cmd.method; // for the error message, if needed

        if (!isValidParamName(name)) {
            throw ToytuiException(ToytuiError{
                method, "-",
                "Malformed parameter name: `" + name + "`"});
        }

        std::string canonical = resolveAlias(name);
        auto colonPos = token.find(':');
        if (colonPos == std::string::npos) {
            // Value-less token — whether this is an error or
            // an expected form is NOT decided by this layer (it depends on the element type/
            // method).
            cmd.params.push_back(ParamToken{canonical, std::nullopt});
        } else {
            std::string rawValue = token.substr(colonPos + 1);
            cmd.params.push_back(
                ParamToken{canonical, unescapeValue(rawValue, isTextInput(canonical))});
        }
    }

    return cmd;
}

} // namespace toytui
