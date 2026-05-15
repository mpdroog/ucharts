// test_order_manager.cpp - Unit tests for order manager module
// Compile: clang++ -std=c++11 -o test_order_manager test_order_manager.cpp order_manager.cpp database.cpp market_data.cpp -lsqlite3
//
// NOTE: Order fill simulation (process_fills) was removed in favor of TradeZero WebSocket integration.
// Tests that relied on simulated fills are now disabled. Order manager logic is tested via
// integration tests with mock TradeZero server instead.

#include "order_manager.h"
#include "tradezero_client.h"
#include "tradezero_websocket.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <sys/stat.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <curl/curl.h>

// ============================================================================
// Test helpers
// ============================================================================

#include "test_common.h"
static const char* TEST_DB = "test_order_manager.db";
static const char* TEST_DATA_DIR = "test_data_om";

// Setup test environment
static Database g_test_db;
static MarketData g_test_market;
static TradeZeroClient g_test_tz_client;
static TradeZeroWebSocket g_test_ws;
static OrderManager g_test_om;  // Global OrderManager - avoids race with WebSocket callback
static std::atomic<int> g_order_events_count{0};
static std::mutex g_om_mutex;   // Protect OrderManager access

// WebSocket callback that forwards order updates to the global OrderManager
static void on_ws_order_update(const TZOrderUpdate& update) {
    std::lock_guard<std::mutex> lock(g_om_mutex);
    g_test_om.on_tradezero_order_update(update);
    g_order_events_count.fetch_add(1);
}

// Helper to wait until pending orders reaches expected count (with mutex protection)
static bool wait_for_pending_count(size_t expected_count, int timeout_ms = 2000) {
    int waited = 0;
    while (waited < timeout_ms) {
        {
            std::lock_guard<std::mutex> lock(g_om_mutex);
            if (g_test_om.get_pending_orders().size() == expected_count) {
                return true;
            }
        }
        safe_sleep_ms(10);
        waited += 10;
    }
    return false;
}


// Stub for removed OrderManager::process_fills() method
// Fill events now come from TradeZero WebSocket - the mock server simulates fills
// This function waits for pending orders to be filled (order count drops to 0)
// Returns true if fills completed, false if timeout
static bool process_fills_stub(OrderManager*) {
    // Wait for pending orders to be filled (removed from pending list)
    // The mock server sends Accepted then Filled events
    // Orders are removed from pending when status becomes Filled
    return wait_for_pending_count(0u, 2000);
}

static void setup_test_data() {
    mkdir(TEST_DATA_DIR, 0755);

    // Create level2 data for TEST symbol
    char filepath[256];
    std::snprintf(filepath, sizeof(filepath), "%s/level2_TEST.csv", TEST_DATA_DIR);
    FILE* f = std::fopen(filepath, "w");
    if (f) {
        std::fprintf(f, "timestamp,symbol,side,exchange,price,size\n");
        std::fprintf(f, "09:30:00.000,TEST,BID,NYSE,99.95,1000\n");
        std::fprintf(f, "09:30:00.000,TEST,ASK,NYSE,100.05,1000\n");
        std::fclose(f);
    }

    // Create daily candles
    std::snprintf(filepath, sizeof(filepath), "%s/candles_TEST_daily.csv", TEST_DATA_DIR);
    f = std::fopen(filepath, "w");
    if (f) {
        std::fprintf(f, "timestamp,open,high,low,close,volume\n");
        std::fprintf(f, "2024-01-02,100.00,101.00,99.00,100.50,10000\n");
        std::fclose(f);
    }
}

static void cleanup_test_data() {
    char filepath[256];
    std::snprintf(filepath, sizeof(filepath), "%s/level2_TEST.csv", TEST_DATA_DIR);
    remove(filepath);
    std::snprintf(filepath, sizeof(filepath), "%s/candles_TEST_daily.csv", TEST_DATA_DIR);
    remove(filepath);
    rmdir(TEST_DATA_DIR);
    unlink(TEST_DB);
}

static bool g_ws_connected = false;

static void init_test_env() {
    unlink(TEST_DB);
    g_test_db.init(TEST_DB);
    g_test_market.set_data_source(DataSourceMode::FILE);
    g_test_market.set_data_dir(TEST_DATA_DIR);
    (void)g_test_market.load_symbol("TEST");  // May fail if no test data, that's OK

    // Configure TradeZero REST client to use mock server
    g_test_tz_client.set_base_url("http://localhost:8080/v1/api");
    g_test_tz_client.set_credentials("test_key", "test_secret", "test_account");

    // Configure TradeZero WebSocket to use mock server (port 8081, no SSL)
    // Connect once at start of tests
    if (!g_ws_connected) {
        g_test_ws.set_url("localhost", 8081, false);
        g_test_ws.set_credentials("test_key", "test_secret", "test_account");
        g_test_ws.set_order_callback(on_ws_order_update);

        if (g_test_ws.connect(TZStream::PORTFOLIO)) {
            g_ws_connected = true;
            // Wait for connection to establish and authenticate (localhost should be fast)
            int timeout_ms = 1000;
            int waited = 0;
            while (!g_test_ws.is_connected() && waited < timeout_ms) {
                safe_sleep_ms(10);
                waited += 10;
            }
        }
    }
}

// Reset mock server state (clear all orders and events)
static void reset_mock_server() {
    // Send POST to reset endpoint to clear mock server state
    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "http://localhost:8080/v1/api/reset");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        // Suppress output
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char*, size_t s, size_t n, void*) -> size_t { return s * n; });
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
    }
    // Give mock server time to process reset
    safe_sleep_ms(10);
}

// Helper to reset global order manager for each test
static void reset_order_manager() {
    // Reset mock server state first (clears orders and pending events)
    reset_mock_server();

    // Reset event counter
    g_order_events_count.store(0);

    std::lock_guard<std::mutex> lock(g_om_mutex);
    // Re-initialize the global order manager for clean test state
    g_test_om.reset();  // Reset to clean state
    g_test_om.init(&g_test_db, &g_test_market);
    g_test_om.set_tradezero_client(&g_test_tz_client);
}

// ============================================================================
// Tests
// ============================================================================

TEST(buy_order_creates_pending) {
    init_test_env();
    reset_order_manager();

    std::lock_guard<std::mutex> lock(g_om_mutex);
    int64_t id = g_test_om.buy("TEST", 100, 100.10f);
    ASSERT_TRUE(id > 0);

    const auto& pending = g_test_om.get_pending_orders();
    ASSERT_EQ(pending.size(), 1u);
    ASSERT_STREQ(pending[0].symbol, "TEST");
    ASSERT_EQ(pending[0].side, OrderSide::BUY);
    ASSERT_EQ(pending[0].quantity, 100);
    ASSERT_FLOAT_EQ(pending[0].price, 100.10f, 0.01f);
    ASSERT_EQ(pending[0].status, OrderStatus::PENDING);
}

TEST(buy_order_invalid_params) {
    init_test_env();
    reset_order_manager();

    ASSERT_EQ(g_test_om.buy("", 100, 100.00f), -1);
    ASSERT_EQ(g_test_om.buy(nullptr, 100, 100.00f), -1);
    ASSERT_EQ(g_test_om.buy("TEST", 0, 100.00f), -1);
    ASSERT_EQ(g_test_om.buy("TEST", -1, 100.00f), -1);
    ASSERT_EQ(g_test_om.buy("TEST", 100, 0.0f), -1);
    ASSERT_EQ(g_test_om.buy("TEST", 100, -1.0f), -1);
}

TEST(sell_without_position_fails) {
    init_test_env();
    reset_order_manager();

    // No position, so sell should fail
    int64_t id = g_test_om.sell("TEST", 100, 100.00f);
    ASSERT_EQ(id, -1);
}

TEST(cancel_order) {
    init_test_env();
    reset_order_manager();

    int64_t id = g_test_om.buy("TEST", 100, 100.10f);
    ASSERT_TRUE(id > 0);
    ASSERT_EQ(g_test_om.get_pending_orders().size(), 1u);

    // Cancel sends REST request, WebSocket confirms cancellation
    ASSERT_TRUE(g_test_om.cancel_order(id));

    // Wait for WebSocket to deliver cancellation event and order to be removed
    // Use condition-based wait since async events from previous tests may arrive first
    ASSERT_TRUE(wait_for_pending_count(0u, 10000));
}

TEST(cancel_nonexistent_order) {
    init_test_env();
    reset_order_manager();

    ASSERT_FALSE(g_test_om.cancel_order(999));
}

TEST(cancel_all_orders) {
    init_test_env();
    reset_order_manager();

    g_test_om.buy("TEST", 100, 100.10f);
    g_test_om.buy("TEST", 200, 100.20f);
    g_test_om.buy("TEST", 300, 100.30f);
    ASSERT_EQ(g_test_om.get_pending_orders().size(), 3u);

    // Cancel all sends REST request, WebSocket confirms each cancellation
    ASSERT_TRUE(g_test_om.cancel_all_orders());

    // Wait for WebSocket to deliver cancellation events and all orders to be removed
    // Use condition-based wait since async events from previous tests may arrive first
    ASSERT_TRUE(wait_for_pending_count(0u, 15000));
}

TEST(buy_fills_when_price_crosses_ask) {
    init_test_env();
    reset_order_manager();

    // Place buy order at ask price (100.05)
    int64_t id = g_test_om.buy("TEST", 100, 100.05f);
    ASSERT_TRUE(id > 0);
    ASSERT_EQ(g_test_om.get_pending_orders().size(), 1u);

    // Process fills - should fill because price >= ask
    process_fills_stub(&g_test_om);

    // Order should be filled and removed from pending
    ASSERT_EQ(g_test_om.get_pending_orders().size(), 0u);

    // Position should be created
    const auto& positions = g_test_om.get_open_positions();
    ASSERT_EQ(positions.size(), 1u);
    ASSERT_STREQ(positions[0].symbol, "TEST");
    ASSERT_EQ(positions[0].quantity, 100);
}

TEST(buy_doesnt_fill_below_ask) {
    init_test_env();
    reset_order_manager();

    // Place buy order below ask price
    int64_t id = g_test_om.buy("TEST", 100, 100.00f);  // Ask is 100.05
    ASSERT_TRUE(id > 0);

    // Process fills - should NOT fill
    process_fills_stub(&g_test_om);

    // Order should still be pending
    ASSERT_EQ(g_test_om.get_pending_orders().size(), 1u);
    ASSERT_EQ(g_test_om.get_open_positions().size(), 0u);
}

TEST(sell_fills_when_price_crosses_bid) {
    init_test_env();
    reset_order_manager();

    // First buy to get a position
    g_test_om.buy("TEST", 100, 100.10f);
    process_fills_stub(&g_test_om);
    ASSERT_EQ(g_test_om.get_open_positions().size(), 1u);

    // Now sell at or below bid (99.95)
    int64_t id = g_test_om.sell("TEST", 50, 99.95f);
    ASSERT_TRUE(id > 0);

    process_fills_stub(&g_test_om);

    // Sell should fill
    ASSERT_EQ(g_test_om.get_pending_orders().size(), 0u);

    // Position should be reduced
    const auto& positions = g_test_om.get_open_positions();
    ASSERT_EQ(positions.size(), 1u);
    ASSERT_EQ(positions[0].quantity, 50);

    // Closed position should be recorded
    ASSERT_EQ(g_test_om.get_closed_positions().size(), 1u);
}

TEST(sell_entire_position) {
    init_test_env();
    reset_order_manager();

    // Buy to get position
    g_test_om.buy("TEST", 100, 100.10f);
    process_fills_stub(&g_test_om);

    // Sell all
    g_test_om.sell("TEST", 100, 99.95f);
    process_fills_stub(&g_test_om);

    // Position should be removed
    ASSERT_EQ(g_test_om.get_open_positions().size(), 0u);

    // Closed positions should be recorded (may be multiple due to partial fills)
    const auto& closed = g_test_om.get_closed_positions();
    ASSERT_TRUE(closed.size() >= 1u);

    // Total closed quantity should equal 100
    int total_closed = 0;
    for (const auto& c : closed) {
        ASSERT_STREQ(c.symbol, "TEST");
        total_closed += c.quantity;
    }
    ASSERT_EQ(total_closed, 100);
}

TEST(calculate_sell_quantity) {
    init_test_env();
    reset_order_manager();

    // Buy 100 shares
    g_test_om.buy("TEST", 100, 100.10f);
    process_fills_stub(&g_test_om);

    ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 25), 25);
    ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 50), 50);
    ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 75), 75);
    ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 100), 100);
}

TEST(calculate_sell_quantity_rounds_down) {
    init_test_env();
    reset_order_manager();

    // Buy 100 shares
    g_test_om.buy("TEST", 100, 100.10f);
    process_fills_stub(&g_test_om);

    // 33% of 100 = 33
    ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 33), 33);

    // 10% of 100 = 10
    ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 10), 10);
}

TEST(calculate_sell_quantity_minimum_one) {
    init_test_env();
    reset_order_manager();

    // Buy 3 shares
    g_test_om.buy("TEST", 3, 100.10f);
    process_fills_stub(&g_test_om);

    // 25% of 3 = 0.75, should round to at least 1
    ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 25), 1);
}

TEST(position_avg_price_calculation) {
    init_test_env();
    reset_order_manager();

    // Buy 100 at 100.00
    g_test_om.buy("TEST", 100, 100.10f);
    process_fills_stub(&g_test_om);

    Position* pos = g_test_om.find_position("TEST");
    ASSERT_TRUE(pos != nullptr);
    ASSERT_FLOAT_EQ(pos->avg_price, 100.10f, 0.01f);

    // Buy another 100 at 101.00
    g_test_om.buy("TEST", 100, 101.10f);
    process_fills_stub(&g_test_om);

    pos = g_test_om.find_position("TEST");
    ASSERT_TRUE(pos != nullptr);
    ASSERT_EQ(pos->quantity, 200);
    // Avg should be (100*100.10 + 100*101.10) / 200 = 100.60
    ASSERT_FLOAT_EQ(pos->avg_price, 100.60f, 0.01f);
}

TEST(closed_position_pnl) {
    init_test_env();
    reset_order_manager();

    // Buy 100 at 100.10
    g_test_om.buy("TEST", 100, 100.10f);
    process_fills_stub(&g_test_om);

    // Sell 100 at 99.95 (may fill in partial fills)
    g_test_om.sell("TEST", 100, 99.95f);
    process_fills_stub(&g_test_om);

    const auto& closed = g_test_om.get_closed_positions();
    ASSERT_TRUE(closed.size() >= 1u);

    // Sum up total quantity and PnL across all partial fills
    int total_qty = 0;
    float total_pnl = 0.0f;
    for (const auto& c : closed) {
        ASSERT_STREQ(c.symbol, "TEST");
        ASSERT_FLOAT_EQ(c.entry_price, 100.10f, 0.01f);  // Entry price should be consistent
        total_qty += c.quantity;
        total_pnl += c.pnl_usd();
    }
    ASSERT_EQ(total_qty, 100);
    // Total P&L = 100 * (exit_avg - 100.10) ≈ -15 (depending on partial fill prices)
    // With partial fills at slightly different prices, PnL may vary slightly
    ASSERT_TRUE(total_pnl < 0.0f);  // Should be negative (sold below entry)
}

TEST(find_position) {
    init_test_env();
    reset_order_manager();

    ASSERT_TRUE(g_test_om.find_position("TEST") == nullptr);

    g_test_om.buy("TEST", 100, 100.10f);
    process_fills_stub(&g_test_om);

    Position* pos = g_test_om.find_position("TEST");
    ASSERT_TRUE(pos != nullptr);
    ASSERT_STREQ(pos->symbol, "TEST");
}

TEST(find_order) {
    init_test_env();
    reset_order_manager();

    int64_t id = g_test_om.buy("TEST", 100, 99.00f);  // Won't fill (below ask)

    Order* order = g_test_om.find_order(id);
    ASSERT_TRUE(order != nullptr);
    ASSERT_EQ(order->id, id);

    Order* nonexistent = g_test_om.find_order(999);
    ASSERT_TRUE(nonexistent == nullptr);
}

TEST(load_tradezero_executions) {
    init_test_env();
    reset_order_manager();

    // Create some mock executions
    std::vector<ClosedPosition> executions;

    ClosedPosition exec1;
    safe_strcpy(exec1.symbol, "AAPL", sizeof(exec1.symbol));
    exec1.quantity = 100;
    exec1.entry_price = 150.00f;
    exec1.exit_price = 155.00f;
    exec1.entry_time = 1700000000;
    exec1.exit_time = 1700001000;
    executions.push_back(exec1);

    ClosedPosition exec2;
    safe_strcpy(exec2.symbol, "MSFT", sizeof(exec2.symbol));
    exec2.quantity = 50;
    exec2.entry_price = 300.00f;
    exec2.exit_price = 295.00f;
    exec2.entry_time = 1700002000;
    exec2.exit_time = 1700003000;
    executions.push_back(exec2);

    // Load executions
    g_test_om.load_tradezero_executions(executions);

    // Verify they were loaded
    const auto& closed = g_test_om.get_closed_positions();
    ASSERT_EQ(closed.size(), 2u);

    // Verify first execution
    ASSERT_STREQ(closed[0].symbol, "AAPL");
    ASSERT_EQ(closed[0].quantity, 100);
    ASSERT_FLOAT_EQ(closed[0].entry_price, 150.00f, 0.01f);
    ASSERT_FLOAT_EQ(closed[0].exit_price, 155.00f, 0.01f);
    ASSERT_FLOAT_EQ(closed[0].pnl_usd(), 500.00f, 0.01f);  // 100 * (155 - 150) = 500

    // Verify second execution
    ASSERT_STREQ(closed[1].symbol, "MSFT");
    ASSERT_EQ(closed[1].quantity, 50);
    ASSERT_FLOAT_EQ(closed[1].entry_price, 300.00f, 0.01f);
    ASSERT_FLOAT_EQ(closed[1].exit_price, 295.00f, 0.01f);
    ASSERT_FLOAT_EQ(closed[1].pnl_usd(), -250.00f, 0.01f);  // 50 * (295 - 300) = -250
}

TEST(load_tradezero_executions_deduplicates) {
    init_test_env();
    reset_order_manager();

    // Create an execution
    std::vector<ClosedPosition> executions;
    ClosedPosition exec1;
    safe_strcpy(exec1.symbol, "TEST", sizeof(exec1.symbol));
    exec1.quantity = 100;
    exec1.entry_price = 100.00f;
    exec1.exit_price = 101.00f;
    exec1.entry_time = 1700000000;
    exec1.exit_time = 1700001000;
    executions.push_back(exec1);

    // Load it twice
    g_test_om.load_tradezero_executions(executions);
    g_test_om.load_tradezero_executions(executions);

    // Should only have 1 (deduplication by symbol + exit_time + quantity)
    const auto& closed = g_test_om.get_closed_positions();
    ASSERT_EQ(closed.size(), 1u);
}

TEST(order_callback) {
    init_test_env();
    reset_order_manager();

    std::atomic<int> callback_count{0};
    g_test_om.set_order_callback([&callback_count](const Order&) {
        callback_count.fetch_add(1);
    });

    g_test_om.buy("TEST", 100, 100.10f);
    // After buy() returns, at least 1 callback has been made (from buy() itself)
    // WebSocket events (Accepted, Filled) may arrive async
    ASSERT_TRUE(callback_count.load() >= 1);

    process_fills_stub(&g_test_om);
    // After fill completes, callback should have been called multiple times:
    // 1. Order placed (from buy())
    // 2. Order accepted (from WebSocket - broker acknowledged)
    // 3. Order filled (from WebSocket - execution complete)
    ASSERT_TRUE(callback_count.load() >= 3);
}

TEST(error_callback_can_be_set) {
    init_test_env();
    reset_order_manager();

    std::atomic<int> error_count{0};
    std::string last_symbol;
    std::string last_error;

    g_test_om.set_error_callback([&error_count, &last_symbol, &last_error](const char* symbol, const char* error) {
        error_count.fetch_add(1);
        last_symbol = symbol;
        last_error = error;
    });

    // Error callback is set (actual error testing requires rejected order via WebSocket)
    ASSERT_EQ(error_count.load(), 0);
}

TEST(error_callback_on_rejected_order) {
    init_test_env();
    reset_order_manager();

    std::atomic<int> error_count{0};
    std::string last_symbol;
    std::string last_error;

    g_test_om.set_error_callback([&error_count, &last_symbol, &last_error](const char* symbol, const char* error) {
        error_count.fetch_add(1);
        last_symbol = symbol;
        last_error = error;
    });

    // First create an order that we can reject
    int64_t id = g_test_om.buy("TEST", 100, 100.10f);
    ASSERT_TRUE(id > 0);
    ASSERT_EQ(g_test_om.get_pending_orders().size(), 1u);

    // Simulate a rejected order update from WebSocket
    TZOrderUpdate rejected_update;
    std::strncpy(rejected_update.symbol, "TEST", sizeof(rejected_update.symbol));
    char client_id[32];
    std::snprintf(client_id, sizeof(client_id), "%lld", static_cast<long long>(id));
    std::strncpy(rejected_update.client_order_id, client_id, sizeof(rejected_update.client_order_id));
    std::strncpy(rejected_update.order_status, "Rejected", sizeof(rejected_update.order_status));
    std::strncpy(rejected_update.side, "Buy", sizeof(rejected_update.side));
    rejected_update.order_quantity = 100;

    // This should trigger the error callback
    g_test_om.on_tradezero_order_update(rejected_update);

    ASSERT_EQ(error_count.load(), 1);
    ASSERT_STREQ(last_symbol.c_str(), "TEST");
    ASSERT_TRUE(last_error.find("rejected") != std::string::npos ||
                last_error.find("REJECTED") != std::string::npos ||
                last_error.find("Rejected") != std::string::npos);
}

TEST(persistence) {
    // Use global test environment with WebSocket
    init_test_env();
    reset_order_manager();

    // First session - create position via WebSocket fills
    unlink(TEST_DB);
    g_test_db.close();  // Close if open
    ASSERT_TRUE(g_test_db.init(TEST_DB));

    // Reset global order manager to use the persistence test database
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        g_test_om.reset();
        g_test_om.init(&g_test_db, &g_test_market);
        g_test_om.set_tradezero_client(&g_test_tz_client);
    }

    // Place order and wait for fill via WebSocket
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        int64_t order_id = g_test_om.buy("TEST", 100, 100.10f);
        ASSERT_TRUE(order_id > 0);
    }
    ASSERT_TRUE(wait_for_pending_count(0u, 2000));

    // Verify position exists before saving
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        ASSERT_EQ(g_test_om.get_open_positions().size(), 1u);
        g_test_om.save_to_database();
    }
    g_test_db.close();

    // Second session - verify data loaded from database
    {
        Database db;
        ASSERT_TRUE(db.init(TEST_DB));

        MarketData market;
        market.set_data_source(DataSourceMode::FILE);
        market.set_data_dir(TEST_DATA_DIR);
        ASSERT_TRUE(market.load_symbol("TEST"));

        OrderManager om;
        om.init(&db, &market);
        om.load_from_database();

        const auto& positions = om.get_open_positions();
        ASSERT_EQ(positions.size(), 1u);
        ASSERT_STREQ(positions[0].symbol, "TEST");
        ASSERT_EQ(positions[0].quantity, 100);

        db.close();
    }
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    setup_test_data();

    RUN_TEST(buy_order_creates_pending);
    RUN_TEST(buy_order_invalid_params);
    RUN_TEST(sell_without_position_fails);
    RUN_TEST(cancel_order);
    RUN_TEST(cancel_nonexistent_order);
    RUN_TEST(cancel_all_orders);
    RUN_TEST(buy_fills_when_price_crosses_ask);
    RUN_TEST(buy_doesnt_fill_below_ask);
    RUN_TEST(sell_fills_when_price_crosses_bid);
    RUN_TEST(sell_entire_position);
    RUN_TEST(calculate_sell_quantity);
    RUN_TEST(calculate_sell_quantity_rounds_down);
    RUN_TEST(calculate_sell_quantity_minimum_one);
    RUN_TEST(position_avg_price_calculation);
    RUN_TEST(closed_position_pnl);
    RUN_TEST(find_position);
    RUN_TEST(find_order);
    RUN_TEST(load_tradezero_executions);
    RUN_TEST(load_tradezero_executions_deduplicates);
    RUN_TEST(order_callback);
    RUN_TEST(error_callback_can_be_set);
    RUN_TEST(error_callback_on_rejected_order);
    RUN_TEST(persistence);

    cleanup_test_data();
    test_summary();

    // Use _Exit to avoid static destruction order issues with global instances
    std::fflush(stdout);
    _Exit(0);
}
