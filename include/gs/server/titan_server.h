#pragma once

#include "gs/actor/actor_system.h"
#include "gs/common/config.h"
#include "third_party/bthread_timer/timer.h"
#include "third_party/timing_wheel/timing_wheel.h"

#include <boost/asio.hpp>
#include <boost/asio/thread_pool.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

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

private:
    struct WheelEntry {
        std::unique_ptr<TimingWheel> wheel;
        bthread_timer::TaskId tick_task_id{bthread_timer::INVALID_TASK_ID};
        int interval_ms = 0;
        std::function<void()> reschedule_fn;
        TitanServer* server = nullptr;  // back-pointer for trampoline
    };

    void ensure_wheel(int interval_ms);

    // Trampoline: bthread_timer C-callback → WheelEntry
    static void wheel_tick_trampoline(void* arg);
    void wheel_tick_impl(WheelEntry& entry);

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

    std::function<void()> _on_stop;
};

}  // namespace gs
