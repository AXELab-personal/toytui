// daemon_process.h — the daemon process: in-memory session store, start-up (fork + setsid + "ready" signal),
// the serving loop. The daemon runs the `Engine` with a deferred pulse, through the `DaemonServer`.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>

#include "serializer.h"
#include "session_backend.h"

namespace toytui {

// The session held in the daemon's memory. No file: saving is copying into memory.
class DaemonBackend : public SessionBackend {
public:
    explicit DaemonBackend(Session initial) : state_(std::move(initial)) {}

    std::optional<Session> loadFull() override { return state_; }
    std::optional<Session> loadCore() override { return state_; }
    ElementLookup loadElement(const std::string& fullId, ElementParams& out) override;
    void saveCurrent(const Session& session) override { state_ = session; }
    // In daemon mode there is no storage location: `path` is only a core value (the relocation matters when leaving the mode).
    void saveRelocated(const std::string&, const Session& session) override { state_ = session; }
    std::string location() const override { return "(daemon)"; }

private:
    Session state_;
};

// Start time of the process (field 22 of /proc/<pid>/stat, clock ticks); 0 if unreadable.
std::uint64_t processStartTime(pid_t pid);
// Whether the process named in the marker is alive (same pid AND start time).
bool daemonProcessAlive(const DaemonMarker& marker);

// Handling an unresponsive / unreachable daemon (called by the client, the command does not run): stops the
// process (SIGTERM so that it can leave into the parked state; SIGKILL after waiting
// `kStopWaitSteps` × `kStopWaitStepMs`), then removes the leftovers (socket, the marker if still present). The
// returned text is the error message: it says whether the state is parked (acknowledgement needed) or the session is lost.
std::string reapDeadDaemon(const std::string& stateFile, const DaemonMarker& marker);

// Starts the daemon with the given session: fork, setsid, std fds to /dev/null, create the socket,
// write the marker (MODE==2) into `stateFile`, and only then signal "ready" to the parent. On success true, and
// `marker` is the written marker. On failure false and `err` (no daemon stays running, no marker is left).
bool startDaemon(const Session& initial, const std::string& stateFile, DaemonMarker& marker, std::string& err);

} // namespace toytui
