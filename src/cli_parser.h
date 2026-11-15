// cli_parser.h — command-line processing.
//
// Responsibilities (strictly kept):
//   - recognising the element type/method positions,
//   - splitting off the "text:" free-text block — ALWAYS BEFORE the
//     param:val split (explicit ordering rule),
//   - bringing param:val tokens to a uniform form,
//   - resolving PARAMETER-NAME aliases (the alias table of the specification, BUT without
//     the three rows that are really attrib-VALUE aliases: ul/strike/
//     reverse — see the comment in cell.h),
//   - filtering malformed parameter names (this is the
//     only case the parser decides itself, because it is context-independent).
//
// What this module EXPLICITLY does NOT do (the job of other layers):
//   - colour/attribute conversion,
//   - deciding whether a given parameter is mandatory/invalid/to be ignored
//     in the context of the given element type/method
//     — that needs element type knowledge, which is not
//     available here.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace toytui {

// A param:val (or value-less) token, alias already resolved.
struct ParamToken {
    std::string name;                 // canonical (alias-resolved) name
    std::optional<std::string> value; // nullopt = value-less token
                                       // (deciding
                                       // "error vs. expected form"
                                       // does not happen here)
};

struct ParsedCommand {
    std::string elementType; // 1st position
    std::string method;      // 2nd position

    std::vector<ParamToken> params; // in order, WITHOUT the block
                                     // starting from "text:"

    // The cleaned free text swallowing everything from the token with the "text:" prefix on
    // (escape-resolved, but NOT YET newline/tab-replaced — that is the
    // job of the ansi handler/element). nullopt if the
    // call had no "text:" token.
    std::optional<std::string> freeText;
    // Which free-text parameter swallowed the rest: "text"
    // by default; for the textarea it can be "text_add" too. Empty if there is no freeText.
    std::string freeTextName = "text";
};

// Parameter-NAME alias (not an attrib-value alias!) — those rows of the
// specification's alias table that refer to real top-level param:val keys.
struct ParamAlias {
    const char* alias;
    const char* canonical;
};
extern const std::vector<ParamAlias> kParamNameAliases;

// The list of argv items WITHOUT the program name (argv[0]) — so at least
// [element type, method, ...]. Throws an error for fewer than 2 items.
// The "start without parameters" case (args.size()==0, i.e. the
// program invoked without arguments) does not even call this function — the
// caller (main) handles it in a separate branch, because it is not an element command but
// the mere re-rendering of the stored state.
// `freeTextNames`: the free-text parameters of the element type (default: {"text"},
// `ElementKind::freeTextParams`); the FIRST argument with such a prefix swallows
// all further arguments.
ParsedCommand parseCommandLine(const std::vector<std::string>& args,
                               const std::vector<std::string>& freeTextNames = {"text"});

// Resolving the escaped \" \' \\ \n \t sequences inside a single value —
// a backslash before any other character stays unchanged.
// `unicode == true` (text input only: `text`, `text_add`, `title`): in the same
// pass `\u[HEX]` is resolved too (utf_escape.h); `\\u[HEX]` is a literal `\u[HEX]`.
std::string unescapeValue(const std::string& raw, bool unicode = false);

// Parameter-name character set check: [0-9a-zA-Z_-], case-sensitive.
bool isValidParamName(const std::string& name);

} // namespace toytui
