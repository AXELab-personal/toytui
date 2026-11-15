#include "serializer.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core_element.h"
#include "error.h"
#include "param_types.h"

namespace toytui {

namespace {

constexpr char kMagic[4] = {'T', 'O', 'Y', 'T'};
constexpr std::uint8_t kVersion = 1;
enum Mode : std::uint8_t { kDirect = 0, kPointer = 1, kDaemon = 2 };

// --- Binary writer/reader helpers ------------------------------------------

void wU8(std::ostream& o, std::uint8_t v) { o.put(static_cast<char>(v)); }
void wU32(std::ostream& o, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        o.put(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}
void wU64(std::ostream& o, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        o.put(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}
void wStr(std::ostream& o, const std::string& s) {
    wU32(o, static_cast<std::uint32_t>(s.size()));
    o.write(s.data(), static_cast<std::streamsize>(s.size()));
}

[[noreturn]] void corrupt(const std::string& path, const std::string& why) {
    throw ToytuiException(
        ToytuiError{"-", "-", "Corrupt state file `" + path + "`: " + why});
}

std::uint8_t rU8(std::istream& in, const std::string& path) {
    const int c = in.get();
    if (c == EOF) {
        corrupt(path, "unexpected end of file");
    }
    return static_cast<std::uint8_t>(c);
}
std::uint32_t rU32(std::istream& in, const std::string& path) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<std::uint32_t>(rU8(in, path)) << (8 * i);
    }
    return v;
}
std::uint64_t rU64(std::istream& in, const std::string& path) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(rU8(in, path)) << (8 * i);
    }
    return v;
}
std::string rStr(std::istream& in, const std::string& path) {
    const std::uint32_t n = rU32(in, path);
    std::string s(n, '\0');
    if (n > 0) {
        in.read(s.data(), static_cast<std::streamsize>(n));
        if (!in) {
            corrupt(path, "unexpected end of file (string)");
        }
    }
    return s;
}

void wParams(std::ostream& o, const ElementParams& p) {
    wU32(o, static_cast<std::uint32_t>(p.all().size()));
    for (const auto& [name, v] : p.all()) {
        wStr(o, name);
        wU8(o, v.state == ParamState::Set ? 1 : 0);
        if (v.state == ParamState::Set) {
            wStr(o, v.value);
        }
    }
}

ElementParams rParams(std::istream& in, const std::string& path) {
    ElementParams p;
    const std::uint32_t n = rU32(in, path);
    for (std::uint32_t i = 0; i < n; ++i) {
        std::string name = rStr(in, path);
        const bool set = rU8(in, path) != 0;
        if (set) {
            p.set(name, rStr(in, path));
        } else {
            p.markUnset(name);
        }
    }
    return p;
}

// RLE: (run length:u32, Cell:8 bytes) pairs until width*height cells
// are used up — "fast, minimal compression", a simple, dependency-free
// implementation.
void wScreen(std::ostream& o, const Block& b) {
    wU32(o, static_cast<std::uint32_t>(b.width));
    wU32(o, static_cast<std::uint32_t>(b.height));
    std::size_t i = 0;
    while (i < b.cells.size()) {
        std::size_t run = 1;
        while (i + run < b.cells.size() && b.cells[i + run] == b.cells[i]) {
            ++run;
        }
        wU32(o, static_cast<std::uint32_t>(run));
        const Cell& c = b.cells[i];
        o.write(reinterpret_cast<const char*>(&c), sizeof(Cell));
        i += run;
    }
}

Block rScreen(std::istream& in, const std::string& path) {
    Block b;
    b.width = static_cast<int>(rU32(in, path));
    b.height = static_cast<int>(rU32(in, path));
    const std::size_t total =
        static_cast<std::size_t>(b.width) * static_cast<std::size_t>(b.height);
    b.cells.reserve(total);
    while (b.cells.size() < total) {
        const std::uint32_t run = rU32(in, path);
        Cell c{};
        in.read(reinterpret_cast<char*>(&c), sizeof(Cell));
        if (!in) {
            corrupt(path, "unexpected end of file (screen)");
        }
        for (std::uint32_t k = 0; k < run; ++k) {
            b.cells.push_back(c);
        }
    }
    if (b.cells.size() != total) {
        corrupt(path, "screen RLE size mismatch");
    }
    return b;
}

// The parameters that DIFFER (are overridden) from core@terminal's defaults.
ElementParams coreOverrides(const ElementParams& core) {
    const ElementParams def = makeDefaultCoreParams();
    ElementParams diff;
    for (const auto& [name, v] : core.all()) {
        if (v.state != ParamState::Set) {
            continue; // for the core this should not occur
        }
        const ParamValue* d = def.find(name);
        if (d == nullptr || d->state != ParamState::Set || d->value != v.value) {
            diff.set(name, v.value);
        }
    }
    return diff;
}

} // namespace

std::optional<std::string> resolveDataFilePath(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, kMagic, 4) != 0) {
        corrupt(path, "bad magic");
    }
    const std::uint8_t version = rU8(in, path);
    if (version != kVersion) {
        corrupt(path, "unsupported version " + std::to_string(version));
    }
    const std::uint8_t mode = rU8(in, path);
    if (mode == kDirect) {
        return path;
    }
    if (mode == kPointer) {
        return rStr(in, path);
    }
    throw ToytuiException(ToytuiError{"-", "-", "State file `" + path + "` belongs to a running daemon"});
}

std::optional<DaemonMarker> readDaemonMarker(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, kMagic, 4) != 0) {
        corrupt(path, "bad magic");
    }
    if (rU8(in, path) != kVersion) {
        corrupt(path, "unsupported version");
    }
    if (rU8(in, path) != kDaemon) {
        return std::nullopt;
    }
    DaemonMarker m;
    m.socketPath = rStr(in, path);
    m.pid = rU32(in, path);
    m.startTime = rU64(in, path);
    return m;
}

namespace {

// ATOMIC file write: we write to a temporary (`<path>.tmp`) file, check the
// stream state, and only after a successful write `rename` it to the
// final place — an interrupted/failed save (e.g. a full
// disk) thus never leaves a truncated `toytui.state` ("we write
// only when we are sure it succeeded").
template <typename Fn>
void atomicWriteFile(const std::string& path, Fn&& writer) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            throw ToytuiException(ToytuiError{"-", "-", "Cannot write state file: `" + path + "`"});
        }
        writer(o);
        o.flush();
        if (!o) {
            o.close();
            std::error_code ec;
            std::filesystem::remove(tmp, ec);
            throw ToytuiException(ToytuiError{"-", "-", "Write failed on state file: `" + path + "`"});
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw ToytuiException(ToytuiError{"-", "-", "Cannot write state file: `" + path + "`"});
    }
}

void writeSessionBody(std::ofstream& o, const Session& session);

} // namespace

void writePointerFile(const std::string& path, const std::string& targetDataFilePath) {
    atomicWriteFile(path, [&](std::ofstream& o) {
        o.write(kMagic, 4);
        wU8(o, kVersion);
        wU8(o, kPointer);
        wStr(o, targetDataFilePath);
    });
}

void writeDaemonMarker(const std::string& path, const DaemonMarker& marker) {
    atomicWriteFile(path, [&](std::ofstream& o) {
        o.write(kMagic, 4);
        wU8(o, kVersion);
        wU8(o, kDaemon);
        wStr(o, marker.socketPath);
        wU32(o, marker.pid);
        wU64(o, marker.startTime);
    });
}

void saveSession(const std::string& dataFilePath, const Session& session) {
    atomicWriteFile(dataFilePath, [&](std::ofstream& o) { writeSessionBody(o, session); });
}

namespace {

void writeSessionBody(std::ofstream& o, const Session& session) {
    o.write(kMagic, 4);
    wU8(o, kVersion);
    wU8(o, kDirect);
    wU8(o, session.daemonIntent ? 1 : 0);
    wStr(o, session.abnormal);
    wParams(o, coreOverrides(session.tree.find("core@terminal")->params));

    const std::vector<std::string> ids = session.tree.allIds(); // core first
    std::vector<std::pair<std::string, std::uint64_t>> index;
    wU32(o, static_cast<std::uint32_t>(ids.size() - 1)); // without core
    for (const auto& id : ids) {
        if (id == "core@terminal") {
            continue;
        }
        index.emplace_back(id, static_cast<std::uint64_t>(o.tellp()));
        const ElementNode& n = *session.tree.find(id);
        wStr(o, id);
        wStr(o, n.type);
        wStr(o, n.parentFullId);
        wParams(o, n.params);
    }

    wU8(o, session.hasScreen ? 1 : 0);
    if (session.hasScreen) {
        wScreen(o, session.lastScreen);
    }

    const auto indexStart = static_cast<std::uint64_t>(o.tellp());
    wU32(o, static_cast<std::uint32_t>(index.size()));
    for (const auto& [id, off] : index) {
        wStr(o, id);
        wU64(o, off);
    }
    // Trailer: the start offset of the index table, in a FIXED 8 bytes at the
    // very end of the file — this lets `loadElementParams` find the index
    // by jumping straight to it, without reading/parsing the whole
    // file.
    wU64(o, indexStart);
}

// Reads the header up to the core block and leaves the stream there.
ElementParams readHeaderAndCore(std::istream& in, const std::string& path, Session& flags) {
    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, kMagic, 4) != 0) {
        corrupt(path, "bad magic");
    }
    if (rU8(in, path) != kVersion) {
        corrupt(path, "unsupported version");
    }
    if (rU8(in, path) != kDirect) {
        corrupt(path, "expected a direct (non-pointer) data file");
    }
    flags.daemonIntent = rU8(in, path) != 0;
    flags.abnormal = rStr(in, path);
    return rParams(in, path);
}

} // namespace

namespace {
// Reading the core block after the header: the default + the stored overrides.
void applyCoreOverrides(Session& s, std::istream& in, const std::string& path) {
    ElementParams core = makeDefaultCoreParams();
    const ElementParams overrides = readHeaderAndCore(in, path, s);
    for (const auto& [name, v] : overrides.all()) {
        if (v.state == ParamState::Set) {
            core.set(name, v.value);
        }
    }
    s.tree.findMut("core@terminal")->params = std::move(core);
}
} // namespace

Session loadCoreSession(const std::string& dataFilePath) {
    std::ifstream in(dataFilePath, std::ios::binary);
    if (!in) {
        throw ToytuiException(
            ToytuiError{"-", "-", "Cannot read state file: `" + dataFilePath + "`"});
    }
    Session s;
    applyCoreOverrides(s, in, dataFilePath);
    return s;
}

Session loadSession(const std::string& dataFilePath) {
    std::ifstream in(dataFilePath, std::ios::binary);
    if (!in) {
        throw ToytuiException(
            ToytuiError{"-", "-", "Cannot read state file: `" + dataFilePath + "`"});
    }
    Session s;
    applyCoreOverrides(s, in, dataFilePath);

    const std::uint32_t n = rU32(in, dataFilePath);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::string fullId = rStr(in, dataFilePath);
        const std::string type = rStr(in, dataFilePath);
        const std::string parent = rStr(in, dataFilePath);
        ElementParams params = rParams(in, dataFilePath);
        const auto at = fullId.find('@');
        if (at == std::string::npos) {
            corrupt(dataFilePath, "malformed element id `" + fullId + "`");
        }
        s.tree.addElement(type, fullId.substr(at + 1), parent, std::move(params));
    }

    s.hasScreen = rU8(in, dataFilePath) != 0;
    if (s.hasScreen) {
        s.lastScreen = rScreen(in, dataFilePath);
    }
    return s; // loadSession does not use the index table (it needs the full tree)
}

std::optional<ElementParams> loadElementParams(const std::string& dataFilePath,
                                                const std::string& fullId) {
    std::ifstream in(dataFilePath, std::ios::binary | std::ios::ate);
    if (!in) {
        throw ToytuiException(
            ToytuiError{"-", "-", "Cannot read state file: `" + dataFilePath + "`"});
    }
    const auto fileSize = static_cast<std::uint64_t>(in.tellg());
    if (fileSize < 8) {
        corrupt(dataFilePath, "too short (no trailer)");
    }

    // 1) trailer -> start of the index table (a direct jump, WITHOUT reading
    // the records — this is the actual benefit of the "fast access").
    in.seekg(static_cast<std::streamoff>(fileSize - 8));
    const std::uint64_t indexStart = rU64(in, dataFilePath);
    in.seekg(static_cast<std::streamoff>(indexStart));

    std::optional<std::uint64_t> recordOffset;
    const std::uint32_t n = rU32(in, dataFilePath);
    for (std::uint32_t i = 0; i < n; ++i) {
        const std::string id = rStr(in, dataFilePath);
        const std::uint64_t off = rU64(in, dataFilePath);
        if (id == fullId) {
            recordOffset = off; // we read through the remaining entries too
        }                       // (simplicity; the index itself is small)
    }
    if (!recordOffset) {
        return std::nullopt;
    }

    // 2) a direct jump to the record, deserializing only that (and none
    // of the earlier ones).
    in.seekg(static_cast<std::streamoff>(*recordOffset));
    rStr(in, dataFilePath); // fullId (identical)
    rStr(in, dataFilePath); // type
    rStr(in, dataFilePath); // parent
    return rParams(in, dataFilePath);
}

} // namespace toytui
