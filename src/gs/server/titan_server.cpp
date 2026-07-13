#include "gs/server/titan_server.h"

#include <csignal>
#include <iostream>
#include <sstream>

namespace gs {

TitanServer::TitanServer(const ServerConfig& config) : _config(config) {}

void TitanServer::init() {
    std::cout << "[titan] init framework" << std::endl;
    _actor_system = std::make_unique<ActorSystem>();
    _actor_system->set_thread_pool(&_worker_pool);
}

// ============================================================================
// TimingWheel lifecycle — driven by bthread_timer
// ============================================================================

void TitanServer::wheel_tick_trampoline(void* arg) {
    auto& entry = *static_cast<WheelEntry*>(arg);
    entry.server->wheel_tick_impl(entry);
}

void TitanServer::wheel_tick_impl(WheelEntry& entry) {
    if (!_running.load(std::memory_order_relaxed)) return;
    if (_paused.load(std::memory_order_relaxed)) {
        // Re-schedule next tick without advancing the wheel.
        // The bthread_timer's kevent will sleep for interval_ms,
        // so this is not a busy loop.
        entry.reschedule_fn();
        return;
    }

    _master_tick.fetch_add(1, std::memory_order_relaxed);
    entry.wheel->tick();
    entry.reschedule_fn();
}

void TitanServer::ensure_wheel(int interval_ms) {
    if (_wheels.count(interval_ms)) return;

    auto wheel = std::make_unique<TimingWheel>(interval_ms,
                                                _config.tw_wheel_size);

    WheelEntry entry;
    entry.wheel = std::move(wheel);
    entry.interval_ms = interval_ms;
    entry.server = this;

    // Build the self-reschedule lambda.
    auto& stored = _wheels[interval_ms];
    stored.wheel = std::move(entry.wheel);
    stored.interval_ms = interval_ms;
    stored.server = this;

    stored.reschedule_fn = [this, &stored]() {
        if (!_running.load(std::memory_order_relaxed)) return;
        auto at = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(stored.interval_ms);
        stored.tick_task_id = _tick_timer.schedule(
            wheel_tick_trampoline, &stored, at);
    };

    // First tick.
    stored.reschedule_fn();
}

void TitanServer::schedule_tick(int interval_ms,
                                std::function<void()> callback) {
    ensure_wheel(interval_ms);
    auto* wheel = _wheels[interval_ms].wheel.get();

    auto task = std::make_shared<std::function<void()>>();
    *task = [wheel, interval_ms, task, cb = std::move(callback)]() {
        cb();
        wheel->addTask(interval_ms, *task);
    };
    wheel->addTask(interval_ms, *task);
}

// ============================================================================
// Snapshot timer — bthread_timer with DONT_COUNT_TIME
// ============================================================================

void TitanServer::snapshot_trampoline(void* arg) {
    auto* entry = static_cast<SnapshotEntry*>(arg);
    if (!entry->server || !entry->server->_running.load(
            std::memory_order_relaxed)) return;
    if (!entry->callback) return;

    entry->callback();

    // Self-reschedule on bthread_timer.
    auto at = std::chrono::steady_clock::now() +
              std::chrono::milliseconds(entry->interval_ms);
    entry->task_id = entry->server->_tick_timer.schedule(
        snapshot_trampoline, entry, at,
        bthread_timer::TASK_FLAG_DONT_COUNT_TIME);
}

void TitanServer::schedule_snapshot(int interval_ms,
                                     std::function<void()> callback) {
    auto entry = std::make_unique<SnapshotEntry>();
    entry->interval_ms = interval_ms;
    entry->callback = std::move(callback);
    entry->server = this;
    _snapshot_entry = std::move(entry);

    // Schedule the first snapshot.
    auto at = std::chrono::steady_clock::now() +
              std::chrono::milliseconds(interval_ms);
    _snapshot_entry->task_id = _tick_timer.schedule(
        snapshot_trampoline, _snapshot_entry.get(), at,
        bthread_timer::TASK_FLAG_DONT_COUNT_TIME);
}

// ============================================================================
// Tick control
// ============================================================================

void TitanServer::pause() {
    _paused.store(true);
    std::cout << "[titan] PAUSED — all wheels suspended\n";
}

void TitanServer::resume() {
    _paused.store(false);
    std::cout << "[titan] RESUMED — wheels running\n";
}

TimingWheel* TitanServer::wheel_for_interval(int interval_ms) {
    auto it = _wheels.find(interval_ms);
    return it != _wheels.end() ? it->second.wheel.get() : nullptr;
}

// ============================================================================
// Signal handling
// ============================================================================

static volatile sig_atomic_t g_signal_flag = 0;
static void signal_handler(int sig) {
    g_signal_flag = sig;
}

// ============================================================================
// Run / Stop
// ============================================================================

void TitanServer::run() {
    _running.store(true);

    // Start bthread_timer (drives all wheels on its dedicated thread).
    bthread_timer::TimerOptions opts;
    opts.num_buckets = _config.bt_num_buckets;
    opts.task_pool_size = _config.bt_task_pool_size;
    _tick_timer.start(opts);

    // Register signal handlers.
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[titan] starting (" << _wheels.size()
              << " wheels, bthread_timer driven)\n";

    // io_context runs on its own thread (network I/O, not tick scheduling).
    std::thread io_thread([this]() {
        _io_context.run();
    });

    // Main thread: poll signal flag.
    while (_running.load() && g_signal_flag == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (g_signal_flag != 0) {
        std::cout << "[titan] signal " << g_signal_flag
                  << ", shutting down\n";
    }

    stop();

    io_thread.join();

    // _tick_timer.stop_and_join() is called in stop().
    std::cout << "[titan] stopped.\n";
}

void TitanServer::stop() {
    _running.store(false);
    g_signal_flag = SIGTERM;

    // Let external code close io_context resources (TCP acceptor, etc.).
    if (_on_stop) _on_stop();

    // Stop the bthread_timer.  Tick callbacks in flight may still run
    // but will see _running==false and skip their work.
    _tick_timer.stop_and_join();

    _worker_pool.join();
}

}  // namespace gs
