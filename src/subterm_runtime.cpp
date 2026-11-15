#include "subterm_runtime.h"

#include <csignal>
#include <utility>

#include "element_tree.h"
#include "error.h"
#include "subterm_element.h"

namespace toytui {

namespace {
constexpr std::size_t kReadChunk = 16384;
constexpr int kReadsPerRound = 8; // bounds the time spent on one program per loop round
} // namespace

bool SubtermRuntime::running(const std::string& id) const {
    const auto it = slots_.find(id);
    return it != slots_.end() && it->second.pty && it->second.pty->running();
}

int SubtermRuntime::runningCount() const {
    int n = 0;
    for (const auto& [id, s] : slots_) {
        n += (s.pty && s.pty->running()) ? 1 : 0;
    }
    return n;
}

std::vector<std::string> SubtermRuntime::runningIds() const {
    std::vector<std::string> ids;
    for (const auto& [id, s] : slots_) {
        if (s.pty && s.pty->running()) {
            ids.push_back(id);
        }
    }
    return ids;
}

std::optional<SubtermRuntime::Config> SubtermRuntime::launched(const std::string& id) const {
    return running(id) ? std::optional<Config>(slots_.at(id).launched) : std::nullopt;
}

std::optional<int> SubtermRuntime::pid(const std::string& id) const {
    return running(id) ? std::optional<int>(static_cast<int>(slots_.at(id).pty->pid())) : std::nullopt;
}

std::optional<int> SubtermRuntime::exitCode(const std::string& id) const {
    const auto it = slots_.find(id);
    return (it != slots_.end() && it->second.exited) ? std::optional<int>(it->second.exitCode) : std::nullopt;
}

const VtermScreen* SubtermRuntime::screen(const std::string& id) const {
    const auto it = slots_.find(id);
    return it != slots_.end() ? it->second.screen.get() : nullptr;
}

std::string SubtermRuntime::takeTextout(const std::string& id) {
    const auto it = slots_.find(id);
    if (it == slots_.end()) {
        return std::string();
    }
    std::string out;
    out.swap(it->second.buf);
    return out;
}

void SubtermRuntime::planStart(const std::string& id, const Config& cfg) {
    Effect e{Effect::Kind::Start, id, cfg, {}, 0, false};
    pending_.push_back(std::move(e));
}
void SubtermRuntime::planStop(const std::string& id) {
    pending_.push_back(Effect{Effect::Kind::Stop, id, {}, {}, 0, false});
}
void SubtermRuntime::planSend(const std::string& id, const std::string& data) {
    pending_.push_back(Effect{Effect::Kind::Send, id, {}, data, 0, false});
}
void SubtermRuntime::planSignal(const std::string& id, int sig) {
    pending_.push_back(Effect{Effect::Kind::Signal, id, {}, {}, sig, false});
}
void SubtermRuntime::planKey(const std::string& id, const std::string& key) {
    pending_.push_back(Effect{Effect::Kind::Key, id, {}, key, 0, false});
}
void SubtermRuntime::planTextout(const std::string& id, bool on) {
    pending_.push_back(Effect{Effect::Kind::Textout, id, {}, {}, 0, on});
}

bool SubtermRuntime::readAvailable(Slot& s) {
    bool any = false;
    for (int i = 0; i < kReadsPerRound && s.pty && s.pty->fd() >= 0; ++i) {
        char buf[kReadChunk];
        std::size_t n = 0;
        const auto st = s.pty->read(buf, sizeof buf, n);
        if (st == PtyProcess::ReadStatus::Data) {
            any = true;
            s.screen->write(buf, n);
            if (s.textout) {
                s.filter.feed(buf, n, s.buf);
                dropOldest(s.buf, kTextoutCap);
            }
            continue;
        }
        if (st == PtyProcess::ReadStatus::Eof) {
            s.pty->closeTerminal(); // the program closed its terminal / exited
        }
        break;
    }
    return any;
}

void SubtermRuntime::forward(Slot& s) {
    if (!s.pty || s.pty->fd() < 0) {
        (void)s.screen->takeOutput();
        return;
    }
    const std::string out = s.screen->takeOutput();
    if (!out.empty()) {
        (void)s.pty->queueWrite(out);
    }
    if (s.pty->wantsWrite()) {
        (void)s.pty->flush();
    }
}

void SubtermRuntime::finish(Slot& s) {
    while (readAvailable(s)) {
    }
    s.exited = true;
    s.exitCode = s.pty->exitCode();
    s.pty->closeTerminal();
}

void SubtermRuntime::stopSlot(Slot& s) {
    if (!s.pty || !s.pty->running()) {
        return;
    }
    s.pty->terminate(); // SIGHUP, then SIGKILL; reaped on return
    finish(s);
}

void SubtermRuntime::commit(const ElementTree& tree, const ElementRegistry& registry) {
    std::vector<Effect> todo;
    todo.swap(pending_);
    for (const Effect& e : todo) {
        switch (e.kind) {
        case Effect::Kind::Textout: {
            Slot& s = slots_[e.id];
            s.textout = e.on;
            s.buf.clear();
            break;
        }
        case Effect::Kind::Start: {
            if (running(e.id)) {
                break; // validated earlier: an identical start of a running program is a no-op
            }
            if (runningCount() >= kMaxInstances) {
                throwError("set", e.id, "Too many running programs (maximum " + std::to_string(kMaxInstances) + ")");
            }
            if (!tree.exists(e.id)) {
                break;
            }
            const auto [w, h] = subtermSize(tree, registry, e.id);
            std::string err;
            auto pty = PtyProcess::start(e.cfg.cmd, e.cfg.cwd, h, w, err);
            if (!pty) {
                throwError("set", e.id, "Cannot start the program: " + err);
            }
            Slot& s = slots_[e.id];
            s.pty = std::move(pty);
            s.screen = std::make_unique<VtermScreen>(h, w);
            s.launched = e.cfg;
            s.rows = h;
            s.cols = w;
            s.exited = false;
            s.exitCode = 0;
            s.filter = TextFilter();
            break;
        }
        case Effect::Kind::Stop: {
            const auto it = slots_.find(e.id);
            if (it != slots_.end()) {
                stopSlot(it->second);
            }
            break;
        }
        case Effect::Kind::Send: {
            const auto it = slots_.find(e.id);
            if (it == slots_.end() || !it->second.pty || it->second.pty->fd() < 0) {
                throwError("set", e.id, "The program is not running");
            }
            if (!it->second.pty->queueWrite(e.data)) {
                throwError("set", e.id, "Too much unsent input (the program does not read it)");
            }
            (void)it->second.pty->flush();
            break;
        }
        case Effect::Kind::Signal: {
            const auto it = slots_.find(e.id);
            if (it == slots_.end() || !it->second.pty || !it->second.pty->running()) {
                throwError("set", e.id, "The program is not running");
            }
            it->second.pty->signal(e.sig);
            break;
        }
        case Effect::Kind::Key: {
            const auto it = slots_.find(e.id);
            if (it != slots_.end() && it->second.screen && it->second.pty && it->second.pty->fd() >= 0) {
                if (it->second.screen->sendKey(e.data)) {
                    forward(it->second);
                }
            }
            break;
        }
        }
    }
    // Elements that no longer exist (deleted): their programs are stopped. Sizes follow the element.
    for (auto it = slots_.begin(); it != slots_.end();) {
        Slot& s = it->second;
        if (!tree.exists(it->first)) {
            stopSlot(s);
            it = slots_.erase(it);
            continue;
        }
        if (s.screen) {
            const auto [w, h] = subtermSize(tree, registry, it->first);
            if (h != s.rows || w != s.cols) {
                s.rows = h;
                s.cols = w;
                s.screen->resize(h, w);
                if (s.pty && s.pty->running()) {
                    s.pty->resize(h, w);
                }
            }
        }
        ++it;
    }
}

void SubtermRuntime::pollFds(std::vector<int>& out) const {
    for (const auto& [id, s] : slots_) {
        if (s.pty && s.pty->fd() >= 0) {
            out.push_back(s.pty->fd());
        }
    }
}

bool SubtermRuntime::busy() const {
    for (const auto& [id, s] : slots_) {
        if (s.pty && s.pty->wantsWrite()) {
            return true;
        }
    }
    return false;
}

bool SubtermRuntime::pump() {
    bool changed = false;
    for (auto& [id, s] : slots_) {
        if (!s.pty || !s.screen) {
            continue;
        }
        if (readAvailable(s)) {
            changed = true;
        }
        forward(s);
        if (s.pty->running() && s.pty->reap()) {
            finish(s);
            changed = true;
        }
    }
    return changed;
}

void SubtermRuntime::terminateAll() {
    for (auto& [id, s] : slots_) {
        if (s.pty && s.pty->running()) {
            s.pty->terminate();
        }
    }
    slots_.clear();
}

} // namespace toytui
