#include "session.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>

#include "error.h"

namespace fs = std::filesystem;

namespace toytui {

namespace {

std::string randomSuffix() {
    static std::mt19937_64 rng{std::random_device{}()};
    static const char kHex[] = "0123456789abcdef";
    std::string s(8, '0');
    std::uniform_int_distribution<int> d(0, 15);
    for (char& c : s) {
        c = kHex[d(rng)];
    }
    return s;
}

} // namespace

std::string stateFilePath(const std::string& startupDir) {
    return (fs::path(startupDir) / "toytui.state").string();
}

std::optional<std::string> currentDataFilePath(const std::string& startupDir) {
    const std::string entry = stateFilePath(startupDir);
    if (!fs::exists(entry)) {
        return std::nullopt;
    }
    return resolveDataFilePath(entry);
}

std::optional<Session> tryLoadSession(const std::string& startupDir) {
    auto data = currentDataFilePath(startupDir);
    if (!data) {
        return std::nullopt;
    }
    return loadSession(*data);
}

std::optional<Session> tryLoadCoreSession(const std::string& startupDir) {
    auto data = currentDataFilePath(startupDir);
    if (!data) {
        return std::nullopt;
    }
    return loadCoreSession(*data);
}

void saveToCurrentLocation(const std::string& startupDir, const Session& session) {
    auto data = currentDataFilePath(startupDir);
    saveSession(data ? *data : stateFilePath(startupDir), session);
}

void saveRelocated(const std::string& startupDir, const std::string& newPath,
                   const Session& session) {
    const std::string entry = stateFilePath(startupDir);
    const auto current = currentDataFilePath(startupDir); // nullopt: no data yet

    std::error_code ec;
    const bool toStartupDir = fs::equivalent(newPath, startupDir, ec) && !ec;

    std::string target;
    if (toStartupDir) {
        target = entry;
    } else {
        // If the current data file is already in the requested directory, it stays
        // in place (no needless rename / new random suffix).
        ec.clear();
        if (current && *current != entry && fs::equivalent(fs::path(*current).parent_path(), newPath, ec) &&
            !ec) {
            target = *current;
        } else {
            target = (fs::path(newPath) / ("toytui.state" + randomSuffix())).string();
        }
    }

    saveSession(target, session); // 1) complete, atomic save of the new data
    if (target != entry) {
        writePointerFile(entry, target); // 2) the pointer (atomic)
    }
    if (current && *current != target && *current != entry) {
        fs::remove(*current, ec); // 3) the old data file; its failure is not fatal
    }
}

void saveLeavingDaemon(const std::string& startupDir, const std::string& relocateTo, const Session& session) {
    const std::string entry = stateFilePath(startupDir);
    std::error_code ec;
    if (relocateTo.empty() || (fs::equivalent(relocateTo, startupDir, ec) && !ec)) {
        saveSession(entry, session);
        return;
    }
    const std::string target = (fs::path(relocateTo) / ("toytui.state" + randomSuffix())).string();
    saveSession(target, session);       // 1) complete, atomic save of the data
    try {
        writePointerFile(entry, target); // 2) atomic replacement of the marker in place
    } catch (...) {
        fs::remove(target, ec);          // the marker stays valid: no orphan data file
        throw;
    }
}

void destroySession(const std::string& startupDir) {
    const std::string entry = stateFilePath(startupDir);
    std::error_code ec;
    try {
        const auto data = currentDataFilePath(startupDir);
        if (data && *data != entry) {
            fs::remove(*data, ec);
            fs::remove(*data + ".tmp", ec);
        }
    } catch (const ToytuiException&) { // marker or corrupt file: only the entry file is deleted
    }
    fs::remove(entry, ec);
    fs::remove(entry + ".tmp", ec);
}

} // namespace toytui
