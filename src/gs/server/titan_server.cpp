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

void TitanServer::ensure_wheel(int interval_ms) {
    if (_wheels.count(interval_ms)) return;

    auto wheel = std::make_unique<TimingWheel>(interval_ms, _config.tw_wheel_size);
    auto timer = std::make_shared<boost::asio::steady_timer>(_io_context);
    auto* wheel_ptr = wheel.get();

    auto on_tick = std::make_shared<
        std::function<void(const boost::system::error_code&)>>();

    *on_tick = [this, timer, wheel_ptr, interval_ms, on_tick](
                   const boost::system::error_code& ec) {
        if (ec || !_running.load()) return;

        // Tick control: global pause / selective pause / step.
        bool wheel_paused = false;
        {
            std::lock_guard<std::mutex> lk(_paused_wheels_mutex);
            wheel_paused = _paused_wheels.count(interval_ms) > 0;
        }
        if (_paused.load() || wheel_paused) {
            timer->expires_after(std::chrono::milliseconds(interval_ms));
            timer->async_wait(*on_tick);
            return;
        }
        if (_steps_remaining.load() > 0) {
            _steps_remaining.fetch_sub(1);
            if (_steps_remaining.load() == 0) {
                _paused.store(true);
                _step_cv.notify_all();
            }
        }

        wheel_ptr->tick();
        timer->expires_after(std::chrono::milliseconds(interval_ms));
        timer->async_wait(*on_tick);
    };

    timer->expires_after(std::chrono::milliseconds(interval_ms));
    timer->async_wait(*on_tick);

    _wheels[interval_ms] = {std::move(wheel), timer};
}

void TitanServer::schedule_tick(int interval_ms, std::function<void()> callback) {
    ensure_wheel(interval_ms);
    auto* wheel = _wheels[interval_ms].wheel.get();

    auto task = std::make_shared<std::function<void()>>();
    auto cb = std::move(callback);
    *task = [wheel, interval_ms, task, cb = std::move(cb)]() {
        cb();
        wheel->addTask(interval_ms, *task);
    };
    wheel->addTask(interval_ms, *task);
}

void TitanServer::pause() {
    _paused.store(true);
    std::cout << "[titan] PAUSED — all wheels suspended\n";
}

void TitanServer::resume() {
    _paused.store(false);
    _steps_remaining.store(0);
    std::cout << "[titan] RESUMED — wheels running\n";
}

void TitanServer::pause_wheel(int interval_ms) {
    std::lock_guard<std::mutex> lk(_paused_wheels_mutex);
    _paused_wheels.insert(interval_ms);
    std::cout << "[titan] paused wheel @" << interval_ms << "ms\n";
}

void TitanServer::resume_wheel(int interval_ms) {
    std::lock_guard<std::mutex> lk(_paused_wheels_mutex);
    _paused_wheels.erase(interval_ms);
    std::cout << "[titan] resumed wheel @" << interval_ms << "ms\n";
}

void TitanServer::step(int n) {
    _paused.store(false);
    _steps_remaining.store(n);
    // Wait until steps complete.
    std::unique_lock<std::mutex> lk(_step_mutex);
    _step_cv.wait(lk, [this] { return _paused.load(); });
    std::cout << "[titan] stepped " << n << " tick(s)\n";
}

TimingWheel* TitanServer::wheel_for_interval(int interval_ms) {
    auto it = _wheels.find(interval_ms);
    return it != _wheels.end() ? it->second.wheel.get() : nullptr;
}

void TitanServer::handle_command(const std::string& cmd) {
    if (cmd == "list") {
        _actor_system->debug_list();
    } else if (cmd == "pause") {
        pause();
    } else if (cmd == "resume") {
        resume();
    } else if (cmd.rfind("step ", 0) == 0) {
        int n = std::stoi(cmd.substr(5));
        step(n);
    } else if (cmd == "stop") {
        stop();
    } else if (cmd.rfind("pwheel ", 0) == 0) {
        pause_wheel(std::stoi(cmd.substr(7)));
    } else if (cmd.rfind("rwheel ", 0) == 0) {
        resume_wheel(std::stoi(cmd.substr(7)));
    } else if (_on_cmd) {
        _on_cmd(cmd);
    } else if (!cmd.empty()) {
        std::cout << "[titan] commands: list pause resume step N "
                     "pwheel <ms> rwheel <ms> stop\n";
    }
}

void TitanServer::debug_console() {
    std::string line;
    while (_running.load()) {
        if (!std::getline(std::cin, line)) break;
        handle_command(line);
    }
}

// Signal flag — same pattern as ProtoRelay's smtps_test.cpp / imaps_test.cpp.
static volatile sig_atomic_t g_signal_flag = 0;
static void signal_handler(int sig) {
    g_signal_flag = sig;
}

void TitanServer::run() {
    _running.store(true);

    // Register signal handlers (flag-only, no complex logic).
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "[titan] starting (" << _wheels.size()
              << " wheels, stdin console)\n";
    std::cout << "[titan] commands: list | pause | resume | step N | stop\n";

    // io_context runs on a worker thread (non-blocking start).
    // Same pattern as ProtoRelay: g_server->start() is async.
    std::thread io_thread([this]() {
        _io_context.run();
    });

    // Start debug console.
    _console_thread = std::thread(&TitanServer::debug_console, this);

    // Main thread: poll signal flag (same as ProtoRelay's main loop).
    while (_running.load() && g_signal_flag == 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (g_signal_flag != 0) {
        std::cout << "[titan] signal " << g_signal_flag << ", shutting down\n";
    }

    stop();

    std::cout << "starting join io thread" << std::endl;
    io_thread.join();
    std::cout << "joined successful" << std::endl;
    // Don't join console thread — it's blocked on getline(stdin).
    // The process is exiting, OS will clean up.
    _console_thread.detach();
}

void TitanServer::stop() {
    _running.store(false);
    g_signal_flag = SIGTERM;
    // Let external code close its io_context resources (TCP acceptor, etc.).
    if (_on_stop) _on_stop();
    // Cancel all wheel timers. Their async_wait callbacks fire with
    // operation_aborted, check _running==false, and don't re-arm.
    for (auto& [_, entry] : _wheels) {
        entry.timer->cancel();
    }
    _worker_pool.join();
}

}  // namespace gs
