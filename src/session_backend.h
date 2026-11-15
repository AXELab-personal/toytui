// session_backend.h — abstraction of storing the session (ElementTree + last image) for the executor
// (engine.h). The executor loads and saves only through it, so the same code runs with file-based
// storage (FileBackend) and with the session held in the daemon's memory.
#pragma once

#include <optional>
#include <string>

#include "serializer.h"

namespace toytui {

enum class ElementLookup { NoState, Missing, Found };

class SessionBackend {
public:
    virtual ~SessionBackend() = default;

    // The whole session; nullopt if there is no stored state yet. Returned by value: the caller works on the copy
    // and saves it only after a successful operation (on failure the stored state is untouched).
    virtual std::optional<Session> loadFull() = 0;
    // The core is enough for reading (the header + the core overrides); file-based storage reads this quickly.
    virtual std::optional<Session> loadCore() = 0;
    // The raw parameters of one element without building the whole tree (where the storage supports it).
    virtual ElementLookup loadElement(const std::string& fullId, ElementParams& out) = 0;
    // Save to the current location, or, when the `core` `path` changed, to the new one (relocation).
    virtual void saveCurrent(const Session& session) = 0;
    virtual void saveRelocated(const std::string& newPath, const Session& session) = 0;
    // The storage location (for error messages).
    virtual std::string location() const = 0;
};

// The `toytui.state` file of the startup directory (session.h).
class FileBackend : public SessionBackend {
public:
    explicit FileBackend(std::string startupDir) : startupDir_(std::move(startupDir)) {}

    std::optional<Session> loadFull() override;
    std::optional<Session> loadCore() override;
    ElementLookup loadElement(const std::string& fullId, ElementParams& out) override;
    void saveCurrent(const Session& session) override;
    void saveRelocated(const std::string& newPath, const Session& session) override;
    std::string location() const override { return startupDir_; }

private:
    std::string startupDir_;
};

} // namespace toytui
