// subterm_runtime.h — the daemon's in-memory side of the `subterm` elements: the running programs
// (pty + terminal emulation), their `textout` buffers, and the deferred effects of a call.
//
// Not persistent: the state file stores only the parameters. One slot per element (full id,
// e.g. `subterm@t`). The element handlers RECORD effects (plan*), `commit` executes them after the
// whole call succeeded (EffectSink); the daemon's serve loop calls `pollFds` / `pump`.
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "element_registry.h"
#include "pty_process.h"
#include "text_filter.h"
#include "vterm_screen.h"

namespace toytui {

class SubtermRuntime : public EffectSink {
public:
    static constexpr int kMaxInstances = 16;                   // programs running at the same time
    static constexpr std::size_t kTextoutCap = 1u << 20;       // bytes kept in a `textout` buffer

    // The launch parameters: what a running program was started with.
    struct Config {
        std::string cmd;
        std::string cwd;
        int scrollback = 0;
        bool operator==(const Config& o) const {
            return cmd == o.cmd && cwd == o.cwd && scrollback == o.scrollback;
        }
    };

    SubtermRuntime() = default;
    ~SubtermRuntime() override { terminateAll(); }
    SubtermRuntime(const SubtermRuntime&) = delete;
    SubtermRuntime& operator=(const SubtermRuntime&) = delete;

    // --- queries (full element id)
    bool running(const std::string& id) const;
    int runningCount() const;
    std::vector<std::string> runningIds() const;
    std::optional<Config> launched(const std::string& id) const; // only while running
    std::optional<int> pid(const std::string& id) const;         // only while running
    std::optional<int> exitCode(const std::string& id) const;    // after the program ended
    const VtermScreen* screen(const std::string& id) const;
    // Returns the plain text collected since `textout:on` and empties the buffer.
    std::string takeTextout(const std::string& id);

    // --- recording of effects (executed by `commit`)
    void planStart(const std::string& id, const Config& cfg);
    void planStop(const std::string& id);
    void planSend(const std::string& id, const std::string& data);
    void planSignal(const std::string& id, int sig);
    void planKey(const std::string& id, const std::string& key);
    void planTextout(const std::string& id, bool on);

    // EffectSink
    void discard() override { pending_.clear(); }
    void commit(const ElementTree& tree, const ElementRegistry& registry) override;

    // --- the daemon's loop
    void pollFds(std::vector<int>& out) const; // pty fds to wait for
    bool busy() const;                         // queued input: poll again soon
    // Reads the output of the programs, answers terminal queries, detects exits. true if anything
    // visible/observable changed (the caller redraws).
    bool pump();
    void terminateAll();

private:
    struct Slot {
        std::unique_ptr<PtyProcess> pty;
        std::unique_ptr<VtermScreen> screen;
        Config launched;
        int rows = 0;
        int cols = 0;
        bool textout = false;
        std::string buf;
        TextFilter filter;
        bool exited = false;
        int exitCode = 0;
    };
    struct Effect {
        enum class Kind { Start, Stop, Send, Signal, Key, Textout } kind;
        std::string id;
        Config cfg;
        std::string data;
        int sig = 0;
        bool on = false;
    };

    bool readAvailable(Slot& s); // true if data was read
    void finish(Slot& s);        // the process ended: last output, exit code, terminal closed
    void forward(Slot& s);       // terminal replies/keys -> the program
    void stopSlot(Slot& s);

    std::map<std::string, Slot> slots_;
    std::vector<Effect> pending_;
};

} // namespace toytui
