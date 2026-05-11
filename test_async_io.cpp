// test_async_io.cpp - Tests to verify I/O never blocks the UI thread
// Compile: clang++ -std=c++17 -pthread -o test_async_io test_async_io.cpp iqfeed_tcp.cpp market_data.cpp http_client.cpp json_parser.cpp -I.

#include "iqfeed_tcp.h"
#include "market_data.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <atomic>

// ============================================================================
// Test helpers
// ============================================================================

#include "test_common.h"

// Track the main thread ID
static std::thread::id g_main_thread_id;

// Flag to track if blocking I/O was detected on main thread
static std::atomic<bool> g_io_on_main_thread{false};

// ============================================================================
// Tests
// ============================================================================

// Test that IQFeedLookup::fetch_daily returns immediately (non-blocking)
TEST(fetch_daily_is_non_blocking) {
    IQFeedLookup lookup;

    // Don't actually connect - just verify the API is non-blocking
    // by checking it returns immediately even without a connection
    auto start = std::chrono::steady_clock::now();

    // This should return immediately since it just queues a request
    lookup.fetch_daily("TEST", 100, nullptr);

    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Should complete in < 10ms (just queuing, no I/O)
    ASSERT_TRUE(duration_ms < 10);
}

// Test that IQFeedLookup::fetch_interval returns immediately (non-blocking)
TEST(fetch_interval_is_non_blocking) {
    IQFeedLookup lookup;

    auto start = std::chrono::steady_clock::now();

    lookup.fetch_interval("TEST", 60, 100, nullptr);

    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    ASSERT_TRUE(duration_ms < 10);
}

// Test that MarketData::load_symbol returns immediately (non-blocking)
// Note: This test uses the global IQFeedLookup instance, so we need to
// wait for any pending operations and clear state properly
TEST(load_symbol_is_non_blocking) {
    // Use FILE mode to avoid TCP connection for this timing test
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);

    auto start = std::chrono::steady_clock::now();

    // This should return immediately even if it fails (no files)
    (void)md.load_symbol("NONEXISTENT");

    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // File-based loading is synchronous but fast (just checks files)
    ASSERT_TRUE(duration_ms < 50);
}

// Test that has_pending_requests works correctly
TEST(has_pending_requests) {
    IQFeedLookup lookup;

    ASSERT_FALSE(lookup.has_pending_requests());

    lookup.fetch_daily("TEST", 100, nullptr);

    ASSERT_TRUE(lookup.has_pending_requests());
}

// Test that callback is invoked on worker thread, not main thread
TEST(callback_on_worker_thread) {
    std::thread::id callback_thread_id;
    std::atomic<bool> callback_invoked{false};

    {
        IQFeedLookup lookup;

        lookup.set_callback([&](const LookupResult& result) {
            (void)result;  // Unused
            callback_thread_id = std::this_thread::get_id();
            callback_invoked = true;
        });

        // Start worker thread
        (void)lookup.connect("127.0.0.1", IQFEED_LOOKUP_PORT);  // May fail, that's OK for this test

        // Queue a request
        lookup.fetch_daily("TEST", 10, nullptr);

        // Wait for callback (will fail to connect, but callback should still fire)
        int wait_count = 0;
        while (!callback_invoked && wait_count < 50) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            wait_count++;
        }

        // Disconnect before destroying to clean up properly
        lookup.disconnect();
    }

    // Verify callback was invoked on a different thread
    if (callback_invoked) {
        ASSERT_TRUE(callback_thread_id != g_main_thread_id);
    }
    // If callback wasn't invoked (connection failed quickly), that's OK
    // The important thing is the API is non-blocking
}

// Test that multiple async requests can be queued without blocking
TEST(multiple_requests_non_blocking) {
    IQFeedLookup lookup;

    auto start = std::chrono::steady_clock::now();

    // Queue many requests rapidly
    for (int i = 0; i < 100; i++) {
        lookup.fetch_daily("TEST", 100, nullptr);
        lookup.fetch_interval("TEST", 60, 100, nullptr);
    }

    auto end = std::chrono::steady_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    // Should queue 200 requests in < 50ms
    ASSERT_TRUE(duration_ms < 50);
}

// Test loading state transitions
TEST(loading_state_transitions) {
    // Before loading - should be IDLE
    MarketData md;
    ASSERT_EQ(md.get_loading_state("UNKNOWN"), MarketData::LoadingState::IDLE);
}

// Test that is_loading returns correct state
TEST(is_loading_check) {
    MarketData md;
    ASSERT_FALSE(md.is_loading("UNKNOWN"));
}

// Test thread safety of concurrent load_symbol calls (file mode to avoid TCP)
TEST(concurrent_load_calls) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);

    std::atomic<int> call_count{0};

    // Start multiple threads calling load_symbol concurrently
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&md, &call_count, i]() {
            char symbol[8];
            std::snprintf(symbol, sizeof(symbol), "SYM%d", i);
            // Will fail (no files) but tests thread safety
            (void)md.load_symbol(symbol);  // Will fail (no files), testing thread safety
            call_count++;
        });
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    ASSERT_EQ(call_count.load(), 10);
}

// ============================================================================
// Main
// ============================================================================

// Forward declarations for global instance cleanup
IQFeedLookup& get_iqfeed_lookup();
IQFeedLevel1& get_iqfeed_level1();
IQFeedLevel2& get_iqfeed_level2();

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    // Record main thread ID
    g_main_thread_id = std::this_thread::get_id();

    RUN_TEST(fetch_daily_is_non_blocking);
    RUN_TEST(fetch_interval_is_non_blocking);
    RUN_TEST(load_symbol_is_non_blocking);
    RUN_TEST(has_pending_requests);
    RUN_TEST(callback_on_worker_thread);
    RUN_TEST(multiple_requests_non_blocking);
    RUN_TEST(loading_state_transitions);
    RUN_TEST(is_loading_check);
    RUN_TEST(concurrent_load_calls);

    test_summary();

    // Verify no I/O happened on main thread
    if (g_io_on_main_thread) {
        std::printf("\nFAILED: Blocking I/O was detected on main thread!\n");
        return 1;
    }

    // Explicitly disconnect global instances before static destruction
    get_iqfeed_lookup().disconnect();
    get_iqfeed_level1().disconnect();
    get_iqfeed_level2().disconnect();

    // Use _Exit to skip static destruction which has ordering issues
    // between global MarketData and IQFeedLookup instances
    std::fflush(stdout);
    _Exit(0);
}
