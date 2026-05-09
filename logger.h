// logger.h - Simple stderr logging with timestamps
#ifndef LOGGER_H
#define LOGGER_H

#include <cstdio>
#include <ctime>
#include <cstdarg>

// Log levels
enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
};

// Current log level (can be changed at runtime)
inline LogLevel& get_log_level() {
    static LogLevel level = LOG_INFO;
    return level;
}

inline void set_log_level(LogLevel level) {
    get_log_level() = level;
}

// Log with timestamp and level
__attribute__((format(printf, 3, 4)))
inline void log_msg(LogLevel level, const char* component, const char* fmt, ...) {
    if (level < get_log_level()) return;

    const char* level_str = "???";
    switch (level) {
        case LOG_DEBUG: level_str = "DBG"; break;
        case LOG_INFO:  level_str = "INF"; break;
        case LOG_WARN:  level_str = "WRN"; break;
        case LOG_ERROR: level_str = "ERR"; break;
    }

    // Get current time
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_buf[20];
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);

    // Print prefix
    std::fprintf(stderr, "[%s][%s][%s] ", time_buf, level_str, component);

    // Print message
    va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    va_end(args);

    std::fprintf(stderr, "\n");
}

// Convenience macros
#define LOG_D(component, ...) log_msg(LOG_DEBUG, component, __VA_ARGS__)
#define LOG_I(component, ...) log_msg(LOG_INFO, component, __VA_ARGS__)
#define LOG_W(component, ...) log_msg(LOG_WARN, component, __VA_ARGS__)
#define LOG_E(component, ...) log_msg(LOG_ERROR, component, __VA_ARGS__)

#endif // LOGGER_H
