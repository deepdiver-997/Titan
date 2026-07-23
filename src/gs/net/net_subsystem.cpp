#include "gs/net/net_subsystem.h"
#include "gs/net/channel.h"
#include "gs/server/titan_server.h"

namespace gs {

// ---- FlushGroup timer ------------------------------------------------------

void NetSubsystem::FlushGroup::trampoline(void* arg) {
    static_cast<FlushGroup*>(arg)->tick();
}

void NetSubsystem::FlushGroup::tick() {
    if (!server) return;

    // 1. Swap out the dirty list under lock.
    std::vector<std::weak_ptr<Channel>> batch;
    {
        std::lock_guard lk(sink.mtx);
        batch.swap(sink.dirty);
    }

    // 2. Flush each still-alive channel.
    for (auto& wp : batch) {
        auto ch = wp.lock();
        if (ch) ch->flush_and_reset();
    }

    // 3. Self-reschedule on bthread_timer.
    auto at = std::chrono::steady_clock::now() +
              std::chrono::milliseconds(interval_ms);
    server->tick_timer().schedule(trampoline, this, at);
}

// ---- NetSubsystem ----------------------------------------------------------

NetSubsystem::NetSubsystem(TitanServer& server) : _server(server) {}

DirtySink* NetSubsystem::get_sink(int interval_ms) {
    auto it = _groups.find(interval_ms);
    if (it != _groups.end()) return &it->second->sink;

    auto g = std::make_unique<FlushGroup>();
    g->interval_ms = interval_ms;
    g->server = &_server;

    // Schedule the first tick directly on bthread_timer.
    auto at = std::chrono::steady_clock::now() +
              std::chrono::milliseconds(interval_ms);
    _server.tick_timer().schedule(&FlushGroup::trampoline, g.get(), at);

    auto* sink = &g->sink;
    _groups[interval_ms] = std::move(g);
    return sink;
}

}  // namespace gs
