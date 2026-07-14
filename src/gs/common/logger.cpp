#include "gs/common/logger.h"

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace gs {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::init(const std::string& name, int level) {
    if (_initialized) return;

    // Create a color console sink.
    auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console->set_level(static_cast<spdlog::level::level_enum>(level));

    _logger = std::make_shared<spdlog::logger>(name, console);
    _logger->set_level(static_cast<spdlog::level::level_enum>(level));

    // Pattern: [2024-01-01 12:00:00.123] [info] [module] [file:line] message
    _logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");

    _initialized = true;
}

void Logger::destroy() {
    if (!_initialized) return;
    _logger->flush();
    _logger.reset();
    _initialized = false;
}

void Logger::log(const std::string& module, int level,
                  const char* file, int line, const char* func,
                  const std::string& msg) {
    if (!_initialized) return;
    auto prefix = fmt::format("[{}][{}:{}] ", module, file, line);
    _logger->log(static_cast<spdlog::level::level_enum>(level),
                 prefix + msg);
}

}  // namespace gs
