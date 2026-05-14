// toast.h - Toast notification system for displaying temporary messages
#ifndef TOAST_H
#define TOAST_H

#include <vector>
#include <string>
#include <cstdint>

// Toast message types
enum class ToastType {
    INFO,
    SUCCESS,
    WARNING,
    ERROR
};

// Single toast notification
struct Toast {
    std::string message;
    ToastType type;
    int64_t expire_time_ms;  // When this toast should disappear

    Toast() : type(ToastType::INFO), expire_time_ms(0) {}
    Toast(const char* msg, ToastType t, int64_t expire)
        : message(msg), type(t), expire_time_ms(expire) {}
};

// Toast manager - handles queue of notifications
class ToastManager {
public:
    ToastManager();

    // Add a toast notification (duration in milliseconds, default 4 seconds)
    void show(const char* message, ToastType type = ToastType::INFO, int duration_ms = 4000);

    // Convenience methods
    void info(const char* message, int duration_ms = 4000);
    void success(const char* message, int duration_ms = 4000);
    void warning(const char* message, int duration_ms = 5000);
    void error(const char* message, int duration_ms = 6000);

    // Render all active toasts (call from main render loop)
    void render();

    // Remove expired toasts
    void update();

    // Clear all toasts
    void clear();

private:
    std::vector<Toast> m_toasts;
    static constexpr int MAX_TOASTS = 5;  // Maximum visible toasts
};

// Global toast manager instance
ToastManager& get_toast_manager();

#endif // TOAST_H
