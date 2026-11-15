// serializer.h — (de)serialization of `toytui.state`.
//
// File format (the concrete byte layout is documented here):
//   MAGIC "TOYT" | VERSION u8 | MODE u8 (0=direct,1=pointer,2=daemon)
//   MODE==1: pointerPath (string) — the path of the actual data file;
//            there is NO pointer->pointer chain (the target is always MODE==0).
//   MODE==0: daemonIntent u8 | abnormal (string; empty = none) | core overrides (only the core
//            parameters that DIFFER from the default(s)) | elements (PRE-ORDER, WITHOUT core@terminal —
//            the core block covers that; every parameter, the explicit
//            unset too) | last rendered image (RLE-compressed,
//            the basis of the diff) | index table (element id -> byte offset,
//            to the start of the record, for fast single-element reads).
//   MODE==2: socketPath (string) | pid (u32) | starttime (u64) — the marker of the
//            running daemon (it holds no snapshot: the state is in the daemon's
//            memory). `starttime` is field 22 of /proc/<pid>/stat,
//            to rule out pid reuse.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "cell.h"
#include "element_tree.h"

namespace toytui {

struct Session {
    ElementTree tree;
    bool hasScreen = false;
    Block lastScreen;
    // These are meaningful only for a state stored in a file (parked): at the next start the daemon starts automatically
    // (`daemonIntent`), and `abnormal` is the reason of the previous daemon's abnormal exit, until acknowledged.
    bool daemonIntent = false;
    std::string abnormal;
};

// The byte range of one element's parameters in the file (from the
// index table) — for a fast `get` WITHOUT building the whole tree.
struct ElementIndexEntry {
    std::string fullId;
    std::uint64_t offset;
};

// Resolving the `path` pointer (MODE==1): the path of the actual data file.
// For a non-pointer (MODE==0) file `path` itself. nullopt for a missing file.
// ToytuiException for a daemon marker (MODE==2) (the caller asks `readDaemonMarker` first).
std::optional<std::string> resolveDataFilePath(const std::string& path);

// The marker of the running daemon (MODE==2).
struct DaemonMarker {
    std::string socketPath;
    std::uint32_t pid = 0;
    std::uint64_t startTime = 0;
};
// Atomic write to `path`.
void writeDaemonMarker(const std::string& path, const DaemonMarker& marker);
// Reading the marker; nullopt if the file does not exist or is not MODE==2. ToytuiException for a corrupt file.
std::optional<DaemonMarker> readDaemonMarker(const std::string& path);

// Reading the whole session from a MODE==0 (direct) data file. The file
// must not be a pointer/daemon marker — that is resolved first by `resolveDataFilePath`.
// ToytuiException for a corrupt file.
Session loadSession(const std::string& dataFilePath);

// A session containing only core@terminal (stops after the header + the core overrides:
// without reading the other elements and the screen). It puts the stored overrides on top of the
// cold-start core defaults. For reading only:
// used by `core get` and the focus query.
Session loadCoreSession(const std::string& dataFilePath);

// Only the index table + the raw parameters of the requested element, WITHOUT building the
// WHOLE tree ("no need to read the whole file"). nullopt
// if the element does not exist.
std::optional<ElementParams> loadElementParams(const std::string& dataFilePath,
                                                const std::string& fullId);

// Writing the whole session (always as MODE==0, to the given path).
void saveSession(const std::string& dataFilePath, const Session& session);

// Writing a MODE==1 (pointer) file to `path`, pointing at `targetDataFilePath`.
void writePointerFile(const std::string& path, const std::string& targetDataFilePath);

} // namespace toytui
