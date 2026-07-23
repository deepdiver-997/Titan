#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace gs {

class TitanServer;
class Channel;

// A dirty-set that Channels enqueue into on first write after flush.
// Owned by NetSubsystem's FlushGroup — the timer task swaps the dirty
// list each tick and calls flush_and_reset() on each still-alive channel.
struct DirtySink {
    std::mutex mtx;
    std::vector<std::weak_ptr<Channel>> dirty;
};

// Manages flush groups for network output.
//
// Each flush group has a timer task that periodically swaps its DirtySink's
// dirty list and flushes only channels that were written to (incremental).
// Channels hold a DirtySink* and enqueue themselves on first write — the
// group does not maintain a long-lived channel registry.
//
// Typical usage:
//   auto sink = server.net().get_sink(33);  // ~30 Hz
//   auto ch = std::make_shared<Channel>(session, 0, Append, sink);
//   ch->write(data);
class NetSubsystem {
public:
    explicit NetSubsystem(TitanServer& server);

    NetSubsystem(const NetSubsystem&) = delete;
    NetSubsystem& operator=(const NetSubsystem&) = delete;

    // Get or create a flush group at `interval_ms`. Returns its DirtySink,
    // which Channel uses to enqueue itself on first write.
    DirtySink* get_sink(int interval_ms);

private:
    struct FlushGroup {
        int interval_ms;
        DirtySink sink;
        TitanServer* server = nullptr;

        void tick();
        static void trampoline(void* arg);
    };

    TitanServer& _server;
    std::unordered_map<int, std::unique_ptr<FlushGroup>> _groups;
};

}  // namespace gs
