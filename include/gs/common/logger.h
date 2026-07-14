#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string>

namespace gs {

// Centralized logger with per-module categories.
//
// User must explicitly call init() and destroy() to control lifetime.
//
// Usage:
//   gs::Logger::instance().init("titan");
//   LOG_SRV_INFO("server started on port {}", port);
//   gs::Logger::instance().destroy();
//
// Level values: 0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=critical.
//
class Logger {
public:
    static Logger& instance();

    void init(const std::string& name = "titan", int level = 0);
    void destroy();

    bool is_initialized() const { return _initialized; }

    // Raw log (already formatted message).
    void log(const std::string& module, int level,
             const char* file, int line, const char* func,
             const std::string& msg);

    // Formatted log — called by LOG_* macros.
    template<typename... Args>
    void log_fmt(const std::string& module, int level,
                 const char* file, int line, const char* func,
                 const char* fmt_str,
                 Args&&... args) {
        if (!_initialized) return;
        auto formatted = fmt::format(fmt_str, std::forward<Args>(args)...);
        auto full = fmt::format("[{}][{}:{}:{}] {}",
                                module, file, line, func, formatted);
        _logger->log(static_cast<spdlog::level::level_enum>(level), full);
    }

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    bool _initialized = false;
    std::shared_ptr<spdlog::logger> _logger;
};

}  // namespace gs

// ---- LOG macros ------------------------------------------------------------
//
// LOG_{MODULE}_{LEVEL}(fmt, args...)
//
// Modules: ASYS, NET, PEER, SCENE, SRV, TIM, DBG, MAIN
// Levels:  TRACE(0), DEBUG(1), INFO(2), WARN(3), ERROR(4), CRITICAL(5)
//
// Expands to nothing when logger is not initialized.

#define TITAN_LOG(module, lvl, ...)                                           \
    do {                                                                      \
        auto& _tlg = ::gs::Logger::instance();                                \
        if (_tlg.is_initialized()) {                                          \
            _tlg.log_fmt(module, (lvl), __FILE__, __LINE__, __func__,         \
                         __VA_ARGS__);                                        \
        }                                                                     \
    } while (0)

// ActorSystem
#define LOG_ASYS_TRACE(...)    TITAN_LOG("ASys", 0, __VA_ARGS__)
#define LOG_ASYS_DEBUG(...)    TITAN_LOG("ASys", 1, __VA_ARGS__)
#define LOG_ASYS_INFO(...)     TITAN_LOG("ASys", 2, __VA_ARGS__)
#define LOG_ASYS_WARN(...)     TITAN_LOG("ASys", 3, __VA_ARGS__)
#define LOG_ASYS_ERROR(...)    TITAN_LOG("ASys", 4, __VA_ARGS__)

// Network (client-facing connections)
#define LOG_NET_TRACE(...)     TITAN_LOG("Net",  0, __VA_ARGS__)
#define LOG_NET_DEBUG(...)     TITAN_LOG("Net",  1, __VA_ARGS__)
#define LOG_NET_INFO(...)      TITAN_LOG("Net",  2, __VA_ARGS__)
#define LOG_NET_WARN(...)      TITAN_LOG("Net",  3, __VA_ARGS__)
#define LOG_NET_ERROR(...)     TITAN_LOG("Net",  4, __VA_ARGS__)

// Peer (server-to-server)
#define LOG_PEER_TRACE(...)    TITAN_LOG("Peer", 0, __VA_ARGS__)
#define LOG_PEER_DEBUG(...)    TITAN_LOG("Peer", 1, __VA_ARGS__)
#define LOG_PEER_INFO(...)     TITAN_LOG("Peer", 2, __VA_ARGS__)
#define LOG_PEER_WARN(...)     TITAN_LOG("Peer", 3, __VA_ARGS__)
#define LOG_PEER_ERROR(...)    TITAN_LOG("Peer", 4, __VA_ARGS__)

// Scene / AOI
#define LOG_SCENE_TRACE(...)   TITAN_LOG("Scene", 0, __VA_ARGS__)
#define LOG_SCENE_DEBUG(...)   TITAN_LOG("Scene", 1, __VA_ARGS__)
#define LOG_SCENE_INFO(...)    TITAN_LOG("Scene", 2, __VA_ARGS__)
#define LOG_SCENE_WARN(...)    TITAN_LOG("Scene", 3, __VA_ARGS__)
#define LOG_SCENE_ERROR(...)   TITAN_LOG("Scene", 4, __VA_ARGS__)

// TitanServer
#define LOG_SRV_TRACE(...)     TITAN_LOG("Srv",  0, __VA_ARGS__)
#define LOG_SRV_DEBUG(...)     TITAN_LOG("Srv",  1, __VA_ARGS__)
#define LOG_SRV_INFO(...)      TITAN_LOG("Srv",  2, __VA_ARGS__)
#define LOG_SRV_WARN(...)      TITAN_LOG("Srv",  3, __VA_ARGS__)
#define LOG_SRV_ERROR(...)     TITAN_LOG("Srv",  4, __VA_ARGS__)

// Timer (TimingWheel, bthread_timer)
#define LOG_TIM_TRACE(...)     TITAN_LOG("Tim",  0, __VA_ARGS__)
#define LOG_TIM_DEBUG(...)     TITAN_LOG("Tim",  1, __VA_ARGS__)
#define LOG_TIM_INFO(...)      TITAN_LOG("Tim",  2, __VA_ARGS__)
#define LOG_TIM_WARN(...)      TITAN_LOG("Tim",  3, __VA_ARGS__)
#define LOG_TIM_ERROR(...)     TITAN_LOG("Tim",  4, __VA_ARGS__)

// Debug framework
#define LOG_DBG_TRACE(...)     TITAN_LOG("Dbg",  0, __VA_ARGS__)
#define LOG_DBG_DEBUG(...)     TITAN_LOG("Dbg",  1, __VA_ARGS__)
#define LOG_DBG_INFO(...)      TITAN_LOG("Dbg",  2, __VA_ARGS__)
#define LOG_DBG_WARN(...)      TITAN_LOG("Dbg",  3, __VA_ARGS__)
#define LOG_DBG_ERROR(...)     TITAN_LOG("Dbg",  4, __VA_ARGS__)

// Main / example code
#define LOG_MAIN_TRACE(...)    TITAN_LOG("Main", 0, __VA_ARGS__)
#define LOG_MAIN_DEBUG(...)    TITAN_LOG("Main", 1, __VA_ARGS__)
#define LOG_MAIN_INFO(...)     TITAN_LOG("Main", 2, __VA_ARGS__)
#define LOG_MAIN_WARN(...)     TITAN_LOG("Main", 3, __VA_ARGS__)
#define LOG_MAIN_ERROR(...)    TITAN_LOG("Main", 4, __VA_ARGS__)
