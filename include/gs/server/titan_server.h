#pragma once

#include "gs/actor/actor_system.h"
#include "gs/common/config.h"
#include "third_party/timing_wheel/timing_wheel.h"

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/thread_pool.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace gs {

// Minimal framework — io_context + ActorSystem + per-frequency TimingWheels.
//
// Debug console (stdin):
//   list       — show all actors and tick groups
//   pause      — suspend all tick wheels (world frozen)
//   resume     — restart all tick wheels
//   step N     — run N ticks on all wheels, then pause
//   stop       — graceful shutdown
//
// Modeled after the Simulation Battle System's mode step / debug break pattern.
class TitanServer {
public:
    explicit TitanServer(const ServerConfig& config);

    TitanServer(const TitanServer&) = delete;
    TitanServer& operator=(const TitanServer&) = delete;

    void init();

    // Register a self-rescheduling tick callback at `interval_ms`.
    void schedule_tick(int interval_ms, std::function<void()> callback);

    // ---- Tick control (debug) --------------------------------------------

    void pause();                          // pause all wheels
    void resume();                         // resume all wheels
    void step(int n = 1);                  // run N ticks then pause

    void pause_wheel(int interval_ms);     // pause a specific wheel
    void resume_wheel(int interval_ms);    // resume a specific wheel

    // Register a callback invoked during stop(), before canceling timers.
    void on_stop(std::function<void()> cb) { _on_stop = std::move(cb); }

    // Register hook for custom debug commands (e.g., "drain 127.0.0.1 9001").
    void on_command(std::function<void(const std::string&)> cb) {
        _on_cmd = std::move(cb);
    }

    void run();
    void stop();

    boost::asio::io_context& io_context() { return _io_context; }
    ActorSystem& actor_system() { return *_actor_system; }
    const ServerConfig& config() const { return _config; }
    TimingWheel* wheel_for_interval(int interval_ms);

private:
    struct WheelEntry {
        std::unique_ptr<TimingWheel> wheel;
        std::shared_ptr<boost::asio::steady_timer> timer;
    };

    void ensure_wheel(int interval_ms);
    void debug_console();
    void handle_command(const std::string& cmd);

    const ServerConfig& _config;
    boost::asio::io_context _io_context;
    std::unique_ptr<ActorSystem> _actor_system;
    std::unordered_map<int, WheelEntry> _wheels;
    std::atomic<bool> _running{false};

    // Tick control.
    boost::asio::thread_pool _worker_pool{4};  // 4 worker threads
    std::atomic<bool> _paused{false};
    std::atomic<int> _steps_remaining{0};
    std::mutex _step_mutex;
    std::condition_variable _step_cv;
    std::mutex _paused_wheels_mutex;
    std::unordered_set<int> _paused_wheels;

    std::function<void()> _on_stop;
    std::function<void(const std::string&)> _on_cmd;
    std::thread _console_thread;
};

}  // namespace gs
