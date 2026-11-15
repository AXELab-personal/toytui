// session.h — file-level lifecycle of `toytui.state`: where to look for it in the startup directory, how it moves
// on `core set path:`, and the high-level load/save.
#pragma once

#include <optional>
#include <string>

#include "serializer.h"

namespace toytui {

// Path of the startup directory's `toytui.state`.
std::string stateFilePath(const std::string& startupDir);

// Path of the ACTUAL data file (pointer resolved); nullopt if there is
// no `toytui.state` yet (on first start the file is created
// on save; until then there is nothing to load).
std::optional<std::string> currentDataFilePath(const std::string& startupDir);

// Load the whole session; nullopt if there is no state (the caller decides:
// on a start without parameters this is an error, otherwise a fresh session starts).
std::optional<Session> tryLoadSession(const std::string& startupDir);
// Cheap variant that loads only the core (without the other elements; for reading).
std::optional<Session> tryLoadCoreSession(const std::string& startupDir);

// Save to the CURRENT actual location (for a pointer, to where it points;
// if there is no data file anywhere yet, directly into the startup directory).
void saveToCurrentLocation(const std::string& startupDir, const Session& session);

// Carrying out `core set path:` together with the save: the session goes to the new
// location, under `newPath/toytui.state<8 random hex>`, and only a pointer
// remains in the startup directory. If `newPath` is the SAME directory
// as the startup directory, the data goes directly there
// (no needless pointer indirection).
// ORDER OF OPERATIONS (fault tolerance): 1) complete (atomic) save of the new data file,
// 2) (atomic) rewrite of the pointer, 3) deletion of the old data file. If any
// step fails the old state stays usable (no pointer to a
// non-existent target, no lost data). Called with the same location (the
// directory of the current data file) there is no move, only a save.
void saveRelocated(const std::string& startupDir, const std::string& newPath,
                   const Session& session);

// Leaving daemon mode via `mode:file` / `mode:exit`: the state goes in place of `toytui.state` (atomic
// replacement: it takes the place of the daemon marker). If `relocateTo` is non-empty and not the startup directory, the data
// goes there (`toytui.state<8 random hex>`) and a pointer remains in the startup directory.
void saveLeavingDaemon(const std::string& startupDir, const std::string& relocateTo, const Session& session);

// Delete the whole session: the (possibly relocated) data file, the pointer / marker and the temporary files.
// A missing, corrupt or daemon-marker file is not an error (the marker is deleted too; the caller stops the daemon).
void destroySession(const std::string& startupDir);

} // namespace toytui
