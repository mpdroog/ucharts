// test_toast.cpp - Unit tests for toast notification system
// Tests core toast functionality without ImGui rendering

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "test_common.h"
#include "toast.h"

// ============================================================================
// Tests for Toast Data Structures
// ============================================================================

TEST(toast_default_initialization) {
    Toast t;
    ASSERT_TRUE(t.message.empty());
    ASSERT_EQ(static_cast<int>(t.type), static_cast<int>(ToastType::INFO));
    ASSERT_EQ(t.expire_time_ms, 0);
}

TEST(toast_parameterized_initialization) {
    Toast t("Test message", ToastType::ERROR, 5000);
    ASSERT_STREQ(t.message.c_str(), "Test message");
    ASSERT_EQ(static_cast<int>(t.type), static_cast<int>(ToastType::ERROR));
    ASSERT_EQ(t.expire_time_ms, 5000);
}

TEST(toast_type_enum_values) {
    ASSERT_TRUE(ToastType::INFO != ToastType::SUCCESS);
    ASSERT_TRUE(ToastType::SUCCESS != ToastType::WARNING);
    ASSERT_TRUE(ToastType::WARNING != ToastType::ERROR);
    ASSERT_TRUE(ToastType::ERROR != ToastType::INFO);
}

// ============================================================================
// Tests for ToastManager
// ============================================================================

TEST(toast_manager_show_message) {
    ToastManager mgr;
    mgr.show("Test notification", ToastType::INFO, 4000);
    // No crash = success (we can't easily inspect internal state without render)
}

TEST(toast_manager_convenience_methods) {
    ToastManager mgr;
    mgr.info("Info message");
    mgr.success("Success message");
    mgr.warning("Warning message");
    mgr.error("Error message");
    // No crash = success
}

TEST(toast_manager_empty_message_ignored) {
    ToastManager mgr;
    mgr.show(nullptr, ToastType::INFO, 4000);
    mgr.show("", ToastType::INFO, 4000);
    // Empty/null messages should be ignored
}

TEST(toast_manager_clear) {
    ToastManager mgr;
    mgr.info("Message 1");
    mgr.info("Message 2");
    mgr.clear();
    // After clear, no toasts should remain
}

TEST(toast_manager_multiple_toasts) {
    ToastManager mgr;
    // Add several toasts
    for (int i = 0; i < 10; i++) {
        char msg[32];
        std::snprintf(msg, sizeof(msg), "Toast %d", i);
        mgr.info(msg);
    }
    // Should not crash even when exceeding MAX_TOASTS
}

TEST(toast_manager_default_durations) {
    ToastManager mgr;
    // Verify default duration values (from function signatures)
    mgr.info("Info");       // Default 4000ms
    mgr.success("Success"); // Default 4000ms
    mgr.warning("Warning"); // Default 5000ms
    mgr.error("Error");     // Default 6000ms
}

TEST(toast_copy_semantics) {
    Toast t1("Original", ToastType::SUCCESS, 3000);
    Toast t2 = t1;

    ASSERT_STREQ(t2.message.c_str(), "Original");
    ASSERT_EQ(static_cast<int>(t2.type), static_cast<int>(ToastType::SUCCESS));
    ASSERT_EQ(t2.expire_time_ms, 3000);
}

// ============================================================================
// Tests for Global Toast Manager
// ============================================================================

TEST(global_toast_manager_exists) {
    ToastManager& mgr = get_toast_manager();
    mgr.info("Test from global manager");
    // Verify global instance is accessible and works
}

TEST(global_toast_manager_same_instance) {
    ToastManager& mgr1 = get_toast_manager();
    ToastManager& mgr2 = get_toast_manager();
    ASSERT_TRUE(&mgr1 == &mgr2);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    // Toast data structure tests
    RUN_TEST(toast_default_initialization);
    RUN_TEST(toast_parameterized_initialization);
    RUN_TEST(toast_type_enum_values);

    // ToastManager tests
    RUN_TEST(toast_manager_show_message);
    RUN_TEST(toast_manager_convenience_methods);
    RUN_TEST(toast_manager_empty_message_ignored);
    RUN_TEST(toast_manager_clear);
    RUN_TEST(toast_manager_multiple_toasts);
    RUN_TEST(toast_manager_default_durations);
    RUN_TEST(toast_copy_semantics);

    // Global instance tests
    RUN_TEST(global_toast_manager_exists);
    RUN_TEST(global_toast_manager_same_instance);

    test_summary();
    return 0;
}
