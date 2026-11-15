// error.h — uniform error format:
//   Error: <method> <target>, <error description>
// Every module (cli_parser, param_store, validator, ...) signals errors with this
// same type, so that main.cpp can write them to stderr uniformly and
// end the process with exit(1).
#pragma once

#include <stdexcept>
#include <string>

namespace toytui {

struct ToytuiError {
    std::string method;      // e.g. "new"/"set"/"get"/"del"; "-" if not yet known
    std::string target;      // full "elementtype@id"; "-" if not yet known
    std::string description; // free text; quoting the literal
                              // parameter/value parts in backticks is the
                              // caller's responsibility (chapter 10)

    [[nodiscard]] std::string format() const {
        return "Error: " + method + " " + target + ", " + description;
    }
};

class ToytuiException : public std::runtime_error {
public:
    explicit ToytuiException(ToytuiError e)
        : std::runtime_error(e.format()), err(std::move(e)) {}

    ToytuiError err;
};

// Shared thrower: `Error: <method> <target>, <msg>` (chapter 10). Modules use this instead of their own
// `fail`/`failWith` wrappers. (`main.cpp::fail` is different: it writes to stderr and returns an exit code.)
[[noreturn]] inline void throwError(const std::string& method, const std::string& target,
                                    const std::string& msg) {
    throw ToytuiException(ToytuiError{method, target, msg});
}

} // namespace toytui
