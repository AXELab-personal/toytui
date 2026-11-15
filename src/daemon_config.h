// daemon_config.h — the adjustable constants of daemon mode in one place (times in ms).
#pragma once

#include <cstddef>

namespace toytui::daemon {

// Client: the time limit of one attempt (connect + send + wait for the reply), and the number of attempts. After the last
// failed attempt the daemon is considered dead.
constexpr int kAttemptTimeoutMs = 100;
constexpr int kMaxAttempts = 4;

// Daemon: the read time limit of a connection (must not exceed the client's per-attempt limit,
// so that a stalled client cannot block the others), and the size of the dedupe cache (request id ->
// stored response).
constexpr int kConnReadTimeoutMs = 100;
constexpr std::size_t kDedupCacheSize = 16;

// Stopping a dead daemon: `SIGTERM`, then kStopWaitSteps × kStopWaitStepMs of waiting, then `SIGKILL`.
constexpr int kStopWaitStepMs = 100;
constexpr int kStopWaitSteps = 10;

// Daemon: polling the terminal size (and the existence of `tty:`), and watching the socket and the state file.
constexpr int kResizePollMs = 200;
constexpr int kWatchIntervalMs = 1000;

// Maximum payload of a frame (well above the textarea's 512 KiB limit).
constexpr std::size_t kMaxFrameBytes = 8u * 1024u * 1024u;

} // namespace toytui::daemon
