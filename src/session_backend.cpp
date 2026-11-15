#include "session_backend.h"

#include "session.h"

namespace toytui {

std::optional<Session> FileBackend::loadFull() { return tryLoadSession(startupDir_); }

std::optional<Session> FileBackend::loadCore() { return tryLoadCoreSession(startupDir_); }

ElementLookup FileBackend::loadElement(const std::string& fullId, ElementParams& out) {
    const auto data = currentDataFilePath(startupDir_);
    if (!data) {
        return ElementLookup::NoState;
    }
    auto params = loadElementParams(*data, fullId);
    if (!params) {
        return ElementLookup::Missing;
    }
    out = std::move(*params);
    return ElementLookup::Found;
}

void FileBackend::saveCurrent(const Session& session) { saveToCurrentLocation(startupDir_, session); }

void FileBackend::saveRelocated(const std::string& newPath, const Session& session) {
    toytui::saveRelocated(startupDir_, newPath, session);
}

} // namespace toytui
