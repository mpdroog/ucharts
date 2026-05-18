// toast.cpp - Toast notification system implementation
#include "toast.h"
#ifndef TOAST_TEST_MODE
#include "imgui.h"
#include "chart_widget.h"  // For make_color helper
#endif
#include <chrono>
#include <algorithm>

// Global toast manager instance
static ToastManager g_toast_manager;

ToastManager& get_toast_manager() {
    return g_toast_manager;
}

static int64_t get_current_time_ms() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return ms.count();
}

ToastManager::ToastManager() {
    m_toasts.reserve(MAX_TOASTS);
}

void ToastManager::show(const char* message, ToastType type, int duration_ms) {
    if (message == nullptr || message[0] == '\0') return;

    int64_t expire = get_current_time_ms() + duration_ms;

    // Remove oldest if at capacity
    if (m_toasts.size() >= MAX_TOASTS) {
        m_toasts.erase(m_toasts.begin());
    }

    m_toasts.emplace_back(message, type, expire);
}

void ToastManager::info(const char* message, int duration_ms) {
    show(message, ToastType::INFO, duration_ms);
}

void ToastManager::success(const char* message, int duration_ms) {
    show(message, ToastType::SUCCESS, duration_ms);
}

void ToastManager::warning(const char* message, int duration_ms) {
    show(message, ToastType::WARNING, duration_ms);
}

void ToastManager::error(const char* message, int duration_ms) {
    show(message, ToastType::ERROR, duration_ms);
}

void ToastManager::update() {
    int64_t now = get_current_time_ms();

    // Remove expired toasts
    m_toasts.erase(
        std::remove_if(m_toasts.begin(), m_toasts.end(),
            [now](const Toast& t) { return now >= t.expire_time_ms; }),
        m_toasts.end()
    );
}

void ToastManager::clear() {
    m_toasts.clear();
}

void ToastManager::render() {
    // First update to remove expired toasts
    update();

#ifndef TOAST_TEST_MODE
    if (m_toasts.empty()) return;

    // Get viewport size for positioning
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float padding = 20.0f;
    float toast_width = 350.0f;
    float toast_height = 50.0f;
    float spacing = 10.0f;

    // Position toasts in top-right corner
    float x = viewport->WorkPos.x + viewport->WorkSize.x - toast_width - padding;
    float y = viewport->WorkPos.y + padding;

    int64_t now = get_current_time_ms();

    for (size_t i = 0; i < m_toasts.size(); ++i) {
        const Toast& toast = m_toasts[i];

        // Calculate fade out (last 500ms)
        float alpha = 1.0f;
        int64_t remaining = toast.expire_time_ms - now;
        if (remaining < 500) {
            alpha = static_cast<float>(remaining) / 500.0f;
            if (alpha < 0.0f) alpha = 0.0f;
        }

        // Set colors based on type
        unsigned char alpha_byte = static_cast<unsigned char>(255.0f * alpha);
        unsigned char bg_alpha = static_cast<unsigned char>(220.0f * alpha);
        ImU32 bg_color;
        ImU32 border_color;
        const char* icon;
        switch (toast.type) {
            case ToastType::SUCCESS:
                bg_color = make_color(20, 80, 20, bg_alpha);
                border_color = make_color(40, 160, 40, alpha_byte);
                icon = "[OK]";
                break;
            case ToastType::WARNING:
                bg_color = make_color(100, 80, 0, bg_alpha);
                border_color = make_color(200, 160, 0, alpha_byte);
                icon = "[!]";
                break;
            case ToastType::ERROR:
                bg_color = make_color(100, 20, 20, bg_alpha);
                border_color = make_color(200, 40, 40, alpha_byte);
                icon = "[X]";
                break;
            case ToastType::INFO:
                bg_color = make_color(30, 50, 80, bg_alpha);
                border_color = make_color(60, 100, 160, alpha_byte);
                icon = "[i]";
                break;
	    default:
		LOG_W("toast", "render: unhandled ToastType %d", static_cast<int>(toast.type));
		break;
        }

        // Create unique window name
        char window_name[32];
        snprintf(window_name, sizeof(window_name), "##Toast%zu", i);

        // Set window position and size
        ImGui::SetNextWindowPos(ImVec2(x, y + static_cast<float>(i) * (toast_height + spacing)));
        ImGui::SetNextWindowSize(ImVec2(toast_width, toast_height));
        ImGui::SetNextWindowBgAlpha(0.0f);  // We'll draw our own background

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                  ImGuiWindowFlags_NoInputs |
                                  ImGuiWindowFlags_NoNav |
                                  ImGuiWindowFlags_NoFocusOnAppearing |
                                  ImGuiWindowFlags_NoSavedSettings |
                                  ImGuiWindowFlags_AlwaysAutoResize;

        if (ImGui::Begin(window_name, nullptr, flags)) {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 pos = ImGui::GetWindowPos();
            ImVec2 size = ImGui::GetWindowSize();

            // Draw background with rounded corners
            draw_list->AddRectFilled(
                pos,
                ImVec2(pos.x + size.x, pos.y + size.y),
                bg_color,
                8.0f  // Corner rounding
            );

            // Draw border
            draw_list->AddRect(
                pos,
                ImVec2(pos.x + size.x, pos.y + size.y),
                border_color,
                8.0f,
                0,
                2.0f  // Border thickness
            );

            // Draw icon and text
            ImGui::SetCursorPos(ImVec2(12.0f, 15.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, make_color(255, 255, 255, alpha_byte));
            ImGui::Text("%s %s", icon, toast.message.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::End();
    }
#endif  // TOAST_TEST_MODE
}
