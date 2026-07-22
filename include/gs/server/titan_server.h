#pragma once

#include "gs/actor/actor_system.h"
#include "gs/common/config.h"
#include "gs/debug/trace_event.h"
#include "third_party/bthread_timer/timer.h"
#include "third_party/timing_wheel/timing_wheel.h"

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace gs {
namespace debug {
struct RecordedEvent;
}
class Channel;
}

namespace gs {

// Minimal framework — io_context + ActorSystem + per-frequency TimingWheels
// driven by a single bthread_timer (dedicated thread, kqueue-based).
//
// Debug is provided as a C++ API (#ifdef TITAN_DEBUG) — not a stdin console.
//   - SnapshotManager: capture all Actor state at a tagged point
//   - Recorder:       record external inputs (TCP / peer messages) for replay
//   - Replayer:       replay a recorded session deterministically
//
// Modeled after the Simulation Battle System's mode step / debug break pattern.
class TitanServer {
public:
    explicit TitanServer(const ServerConfig& config);

    TitanServer(const TitanServer&) = delete;
    TitanServer& operator=(const TitanServer&) = delete;

    void init();

    // Register a self-rescheduling tick callback at `interval_ms`.
    // Internally uses bthread_timer (not steady_timer) — no io_context jitter.
    void schedule_tick(int interval_ms, std::function<void()> callback);

    // ---- IO frequency groups (decoupled from actor system) ----------------
    //
    // Like actor tick groups, but for network IO. Channels register
    // themselves with an IO group; the group's timer task calls
    // Channel::flush() on every tick.
    //
    // Usage:
    //   auto io_grp = server.create_io_group(33);  // ~30 Hz flush
    //   server.add_to_io_group(io_grp, channel);
    using IoGroupHandle = int;
    IoGroupHandle create_io_group(int interval_ms);
    void add_to_io_group(IoGroupHandle handle, std::shared_ptr<Channel> ch);
    void remove_from_io_group(IoGroupHandle handle, Channel* ch);

    // Register a repeating snapshot callback directly on bthread_timer.
    // The callback is scheduled with TASK_FLAG_DONT_COUNT_TIME — its
    // wall-clock execution time is subtracted from virtual time, so all
    // TimingWheels appear frozen while the snapshot runs.
    //
    // Typical use:
    //   server.schedule_snapshot(10, []() { SNAPSHOT("periodic"); });
    void schedule_snapshot(int interval_ms, std::function<void()> callback);

    // ---- Tick control (programmatic API, not stdin) -----------------------

    void pause();   // suspend all wheel ticks
    void resume();  // resume all wheel ticks
    bool is_paused() const { return _paused.load(); }

    // Register a callback invoked during stop(), before canceling timers.
    void on_stop(std::function<void()> cb) { _on_stop = std::move(cb); }

    void run();
    void stop();

    boost::asio::io_context& io_context() { return _io_context; }
    ActorSystem& actor_system() { return *_actor_system; }
    const ServerConfig& config() const { return _config; }
    TimingWheel* wheel_for_interval(int interval_ms);
    bthread_timer::Timer& tick_timer() { return _tick_timer; }

    // Monotonic tick counter, incremented on every wheel tick.
    uint32_t master_tick() const { return _master_tick.load(std::memory_order_relaxed); }



    // ---- Disaster recovery ------------------------------------------------

    // Reload server state from a snapshot and replay recorded events.
    // Called during startup, before any network I/O or timer scheduling.
    // After reload_state() completes, the server is ready for live operation
    // with _master_tick set to max(events) + 1.
    void reload_state(const debug::ServerSnapshot& snapshot,
                      const std::vector<gs::debug::RecordedEvent>& events);

private:
    struct WheelEntry {
        std::unique_ptr<TimingWheel> wheel;
        bthread_timer::TaskId tick_task_id{bthread_timer::INVALID_TASK_ID};
        int interval_ms = 0;
        std::function<void()> reschedule_fn;
        TitanServer* server = nullptr;  // back-pointer for trampoline
    };

    void ensure_wheel(int interval_ms);

    struct SnapshotEntry {
        bthread_timer::TaskId task_id{bthread_timer::INVALID_TASK_ID};
        int interval_ms = 0;
        std::function<void()> callback;
        TitanServer* server = nullptr;
    };

    // Trampoline: bthread_timer C-callback → WheelEntry
    static void wheel_tick_trampoline(void* arg);
    void wheel_tick_impl(WheelEntry& entry);

    // Trampoline: bthread_timer C-callback → SnapshotEntry
    static void snapshot_trampoline(void* arg);

    struct IoGroup {
        int interval_ms;
        std::mutex mtx;
        std::vector<std::weak_ptr<Channel>> channels;
        TitanServer* server = nullptr;
        IoGroupHandle handle = 0;

        static void trampoline(void* arg);
        void tick();
    };

    // Registered tick callbacks (ordered by registration).

    const ServerConfig& _config;
    boost::asio::io_context _io_context;
    std::unique_ptr<ActorSystem> _actor_system;
    std::unordered_map<int, WheelEntry> _wheels;
    std::atomic<bool> _running{false};

    // Thread pool for parallel actor execution (shared across tick groups).
    boost::asio::thread_pool _worker_pool{4};

    // Single bthread_timer driving all TimingWheels.
    bthread_timer::Timer _tick_timer;

    // Tick control.
    std::atomic<bool> _paused{false};

    // Monotonic master tick, incremented on every wheel tick.
    std::atomic<uint32_t> _master_tick{0};

    // Optional repeating snapshot timer (direct on bthread_timer).
    std::unique_ptr<SnapshotEntry> _snapshot_entry;

    // IO frequency groups.
    IoGroupHandle _next_io_handle = 1;
    std::unordered_map<IoGroupHandle, std::unique_ptr<IoGroup>> _io_groups;

    // Session management.

    std::function<void()> _on_stop;
};

}  // namespace gs
