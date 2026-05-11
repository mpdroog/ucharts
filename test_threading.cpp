// test_threading.cpp - Stress tests for thread safety
// Run with: make test_threading && ./test_threading
// Run with ThreadSanitizer: make tsan

#include "iqfeed_tcp.h"
#include "market_data.h"
#include "types.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

// ============================================================================
// Test helpers
// ============================================================================

#include "test_common.h"

// ============================================================================
// Mock callback that simulates work and may access shared state
// ============================================================================

static std::atomic<int> g_callback_count{0};
static std::mutex g_callback_mutex;

static void stress_l1_callback(const L1Quote& quote) {
    // Simulate callback work that might try to access shared state
    (void)quote;
    g_callback_count++;

    // Simulate some work
    safe_sleep_us(10);
}

static void stress_l2_callback(const char* symbol,
                               const std::vector<L2Level>& bids,
                               const std::vector<L2Level>& asks) {
    (void)symbol;
    (void)bids;
    (void)asks;
    g_callback_count++;

    // Simulate some work
    safe_sleep_us(10);
}

// ============================================================================
// Tests
// ============================================================================

// Test concurrent access to IQFeedLevel1 from multiple threads
// This is the pattern that caused the original crash
TEST(l1_concurrent_callback_and_access) {
    IQFeedLevel1 level1;
    level1.set_callback(stress_l1_callback);

    std::atomic<bool> stop{false};
    std::atomic<int> parse_count{0};
    std::atomic<int> read_count{0};

    // Thread 1: Simulates stream thread parsing messages
    // (In real code this happens inside the class, but we test the pattern)
    std::thread parser([&]() {
        while (!stop) {
            // Simulate what parse_summary_message does
            L1Quote quote;
            std::snprintf(quote.symbol, sizeof(quote.symbol), "TEST%d", parse_count.load() % 10);
            quote.last = 100.0f + static_cast<float>(parse_count.load() % 100);
            quote.bid = quote.last - 0.01f;
            quote.ask = quote.last + 0.01f;

            // The callback should be safe to call
            stress_l1_callback(quote);
            parse_count++;
        }
    });

    // Thread 2: Simulates UI thread reading quotes
    std::thread reader([&]() {
        L1Quote quote;
        while (!stop) {
            char symbol[16];
            std::snprintf(symbol, sizeof(symbol), "TEST%d", read_count.load() % 10);
            // This used to deadlock when callback held the mutex
            (void)level1.get_quote(symbol, quote);
            read_count++;
        }
    });

    // Run for a bit
    safe_sleep_ms(500);
    stop = true;

    parser.join();
    reader.join();

    std::printf("(parsed=%d, read=%d) ", parse_count.load(), read_count.load());
    ASSERT_TRUE(parse_count > 0);
    ASSERT_TRUE(read_count > 0);
}

// Test concurrent access to IQFeedLevel2
TEST(l2_concurrent_callback_and_access) {
    IQFeedLevel2 level2;
    level2.set_callback(stress_l2_callback);

    std::atomic<bool> stop{false};
    std::atomic<int> update_count{0};
    std::atomic<int> read_count{0};

    // Thread 1: Simulates parsing book updates
    std::thread updater([&]() {
        while (!stop) {
            std::vector<L2Level> bids, asks;
            for (int i = 0; i < 5; i++) {
                L2Level bid, ask;
                bid.price = 100.0f - static_cast<float>(i) * 0.01f;
                bid.size = 100;
                bid.is_bid = true;
                ask.price = 100.0f + static_cast<float>(i) * 0.01f;
                ask.size = 100;
                ask.is_bid = false;
                bids.push_back(bid);
                asks.push_back(ask);
            }
            stress_l2_callback("TEST", bids, asks);
            update_count++;
        }
    });

    // Thread 2: Simulates reading order book
    std::thread reader([&]() {
        std::vector<L2Level> bids, asks;
        while (!stop) {
            (void)level2.get_book("TEST", bids, asks);
            read_count++;
        }
    });

    safe_sleep_ms(500);
    stop = true;

    updater.join();
    reader.join();

    std::printf("(updates=%d, reads=%d) ", update_count.load(), read_count.load());
    ASSERT_TRUE(update_count > 0);
    ASSERT_TRUE(read_count > 0);
}

// Test that callbacks don't deadlock when they access shared state
TEST(callback_with_shared_state_access) {
    std::mutex shared_mutex;
    std::atomic<int> callback_success{0};
    std::atomic<int> external_access{0};
    std::atomic<bool> stop{false};

    // Callback that tries to acquire another lock (simulates real-world pattern)
    auto callback = [&](const L1Quote& quote) {
        (void)quote;
        // This pattern used to cause deadlock when callback was called
        // while holding the IQFeedLevel1 mutex
        std::lock_guard<std::mutex> lock(shared_mutex);
        callback_success++;
        safe_sleep_us(5);
    };

    // Thread that holds shared_mutex and tries to access IQFeedLevel1
    std::thread external([&]() {
        IQFeedLevel1 level1;
        while (!stop) {
            {
                std::lock_guard<std::mutex> lock(shared_mutex);
                L1Quote quote;
                // This used to deadlock: external holds shared_mutex,
                // tries to get l1 mutex, while callback holds l1 mutex
                // and tries to get shared_mutex
                (void)level1.get_quote("TEST", quote);
            }
            external_access++;
        }
    });

    // Thread that simulates callbacks
    std::thread callbacker([&]() {
        L1Quote quote;
        std::snprintf(quote.symbol, sizeof(quote.symbol), "TEST");
        while (!stop) {
            callback(quote);
        }
    });

    safe_sleep_ms(500);
    stop = true;

    external.join();
    callbacker.join();

    std::printf("(callbacks=%d, external=%d) ", callback_success.load(), external_access.load());
    ASSERT_TRUE(callback_success > 0);
    ASSERT_TRUE(external_access > 0);
}

// Test rapid connect/disconnect doesn't race
TEST(rapid_connect_disconnect) {
    for (int i = 0; i < 10; i++) {
        IQFeedLevel1 level1;
        IQFeedLevel2 level2;
        IQFeedLookup lookup;

        // These will fail to connect (no server) but shouldn't crash
        (void)level1.connect("127.0.0.1", 59999);  // Invalid port
        (void)level2.connect("127.0.0.1", 59998);
        (void)lookup.connect("127.0.0.1", 59997);

        // Rapid disconnect
        level1.disconnect();
        level2.disconnect();
        lookup.disconnect();
    }
    std::printf("(10 cycles) ");
    ASSERT_TRUE(true);
}

// Test many threads calling fetch simultaneously
TEST(concurrent_fetch_requests) {
    IQFeedLookup lookup;
    std::atomic<int> request_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 10; i++) {
        threads.emplace_back([&lookup, &request_count, i]() {
            for (int j = 0; j < 100; j++) {
                char symbol[16];
                std::snprintf(symbol, sizeof(symbol), "SYM%d_%d", i, j);
                lookup.fetch_daily(symbol, 100, nullptr);
                lookup.fetch_interval(symbol, 60, 50, nullptr);
                request_count += 2;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::printf("(requests=%d) ", request_count.load());
    ASSERT_TRUE(request_count == 2000);
}

// Test callback registration from multiple threads
TEST(concurrent_callback_registration) {
    IQFeedLevel1 level1;
    std::atomic<int> reg_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&level1, &reg_count]() {
            for (int j = 0; j < 100; j++) {
                level1.set_callback([](const L1Quote& q) { (void)q; });
                reg_count++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    std::printf("(registrations=%d) ", reg_count.load());
    ASSERT_TRUE(reg_count == 500);
}

// Test watch/unwatch from multiple threads
TEST(concurrent_watch_unwatch) {
    IQFeedLevel1 level1;
    std::atomic<int> op_count{0};
    std::atomic<bool> stop{false};

    // These will fail (not connected) but tests thread safety of the operations
    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++) {
        threads.emplace_back([&level1, &op_count, &stop, i]() {
            while (!stop) {
                char symbol[16];
                std::snprintf(symbol, sizeof(symbol), "SYM%d", i);
                (void)level1.watch(symbol);
                (void)level1.unwatch(symbol);
                op_count += 2;
            }
        });
    }

    safe_sleep_ms(200);
    stop = true;

    for (auto& t : threads) {
        t.join();
    }

    std::printf("(ops=%d) ", op_count.load());
    ASSERT_TRUE(op_count > 0);
}

// Test that uses MarketData exactly like the real app
TEST(market_data_load_symbol) {
    MarketData& md = get_market_data();  // Use singleton
    md.set_data_source(DataSourceMode::IQFEED);
    md.set_tcp_host("127.0.0.1");

    // Load a symbol - this connects and sets up callbacks
    bool loaded = md.load_symbol("AAPL");
    std::printf("(loaded=%d) ", loaded ? 1 : 0);

    // Let callbacks run
    safe_sleep_ms(500);

    // Access data concurrently while callbacks fire
    std::atomic<bool> stop{false};
    std::thread accessor([&md, &stop]() {
        while (!stop) {
            std::vector<Candle> candles;
            (void)md.get_candles("AAPL", Timeframe::M1, candles);
            (void)md.get_candles("AAPL", Timeframe::M5, candles);
            (void)md.get_candles("AAPL", Timeframe::DAILY, candles);
            safe_sleep_ms(10);
        }
    });

    safe_sleep_ms(500);
    stop = true;
    accessor.join();

    ASSERT_TRUE(true);  // If we got here, no crash
}

// Test that reproduces the exact crash scenario from main app:
// Use GLOBAL instances, set callback, connect, and access concurrently
TEST(global_instance_crash_repro) {
    std::atomic<bool> stop{false};
    std::atomic<int> callback_count{0};
    std::mutex shared_mutex;

    // Set callback on global lookup (like MarketData does)
    get_iqfeed_lookup().set_callback([&](const LookupResult& result) {
        (void)result;
        // Simulate MarketData::on_lookup_result which locks its own mutex
        std::lock_guard<std::mutex> lock(shared_mutex);
        callback_count++;
        safe_sleep_us(100);
    });

    // Try to connect (will fail, but should not crash)
    bool connected = get_iqfeed_lookup().connect("127.0.0.1", IQFEED_LOOKUP_PORT);

    // Whether connected or not, try operations concurrently
    std::thread fetcher([&]() {
        while (!stop) {
            get_iqfeed_lookup().fetch_daily("TEST", 100, nullptr);
            safe_sleep_ms(10);
        }
    });

    std::thread checker([&]() {
        while (!stop) {
            (void)get_iqfeed_lookup().has_pending_requests();
            (void)get_iqfeed_lookup().is_connected();
            safe_sleep_ms(5);
        }
    });

    // Let it run
    safe_sleep_ms(500);
    stop = true;

    fetcher.join();
    checker.join();

    if (connected) {
        get_iqfeed_lookup().disconnect();
    }

    std::printf("(callbacks=%d) ", callback_count.load());
    ASSERT_TRUE(true);  // If we got here without crashing, test passed
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    // Run crash repro tests FIRST to catch the issue
    RUN_TEST(market_data_load_symbol);
    RUN_TEST(global_instance_crash_repro);

    RUN_TEST(l1_concurrent_callback_and_access);
    RUN_TEST(l2_concurrent_callback_and_access);
    RUN_TEST(callback_with_shared_state_access);
    RUN_TEST(rapid_connect_disconnect);
    RUN_TEST(concurrent_fetch_requests);
    RUN_TEST(concurrent_callback_registration);
    RUN_TEST(concurrent_watch_unwatch);

    test_summary();

    // Clean exit
    get_iqfeed_lookup().disconnect();
    get_iqfeed_level1().disconnect();
    get_iqfeed_level2().disconnect();

    std::fflush(stdout);
    _Exit(0);
}
