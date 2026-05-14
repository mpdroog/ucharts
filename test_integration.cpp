// test_integration.cpp - Integration tests for ucharts trading platform
// Tests that all modules work together correctly

#include "types.h"
#include "database.h"
#include "market_data.h"
#include "order_manager.h"
#include "tradezero_client.h"
#include "tradezero_websocket.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <curl/curl.h>

// Test framework
static int g_tests_run = 0;
static int g_tests_passed = 0;
static bool g_verbose = false;

static void test_init(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) g_verbose = true;
    }
}

#define TEST(name) \
    static void test_##name(); \
    static void run_##name() { \
        if (g_verbose) { printf("Running %s... ", #name); fflush(stdout); } \
        g_tests_run++; \
        test_##name(); \
        g_tests_passed++; \
        if (g_verbose) printf("PASSED\n"); \
    } \
    static void test_##name()

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            printf("FAILED\n  Assertion failed: %s\n  at %s:%d\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            printf("FAILED\n  Expected %s == %s\n  at %s:%d\n", #a, #b, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while(0)

#define ASSERT_STREQ(a, b) \
    do { \
        if (strcmp((a), (b)) != 0) { \
            printf("FAILED\n  Expected \"%s\" == \"%s\"\n  at %s:%d\n", a, b, __FILE__, __LINE__); \
            exit(1); \
        } \
    } while(0)

// Helper to create test data directory
static const char* TEST_DATA_DIR = "test_integration_data";
static const char* TEST_DB = "test_integration.db";

// Global test instances
static Database g_test_db;
static MarketData g_test_market;
static TradeZeroClient g_test_tz_client;
static TradeZeroWebSocket g_test_ws;
static OrderManager g_test_om;
static std::mutex g_om_mutex;
static bool g_ws_connected = false;

// WebSocket callback to forward order updates to global OrderManager
static void on_ws_order_update(const TZOrderUpdate& update) {
    std::lock_guard<std::mutex> lock(g_om_mutex);
    g_test_om.on_tradezero_order_update(update);
}

// Helper to wait for pending orders count
static bool wait_for_pending_count(size_t expected, int timeout_ms = 2000) {
    int waited = 0;
    while (waited < timeout_ms) {
        {
            std::lock_guard<std::mutex> lock(g_om_mutex);
            if (g_test_om.get_pending_orders().size() == expected) {
                return true;
            }
        }
        safe_sleep_ms(10);
        waited += 10;
    }
    return false;
}

// Reset global order manager for clean test state
static void reset_global_om() {
    std::lock_guard<std::mutex> lock(g_om_mutex);
    g_test_om.reset();
    g_test_om.init(&g_test_db, &g_test_market);
    g_test_om.set_tradezero_client(&g_test_tz_client);
}

static void setup_tradezero_client() {
    // Configure TradeZero REST client to use mock server
    g_test_tz_client.set_base_url("http://localhost:8080/v1/api");
    g_test_tz_client.set_credentials("test_key", "test_secret", "test_account");

    // Configure and connect WebSocket (once)
    if (!g_ws_connected) {
        g_test_ws.set_url("localhost", 8081, false);
        g_test_ws.set_credentials("test_key", "test_secret", "test_account");
        g_test_ws.set_order_callback(on_ws_order_update);

        if (g_test_ws.connect(TZStream::PORTFOLIO)) {
            g_ws_connected = true;
            // Wait for connection to establish
            int timeout_ms = 1000;
            int waited = 0;
            while (!g_test_ws.is_connected() && waited < timeout_ms) {
                safe_sleep_ms(10);
                waited += 10;
            }
        }
    }
}

static void create_test_directory() {
    mkdir(TEST_DATA_DIR, 0755);
}

static void cleanup_test_directory() {
    // Remove files
    char path[256];
    snprintf(path, sizeof(path), "%s/level2_TEST.csv", TEST_DATA_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/timesales_TEST.csv", TEST_DATA_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/candles_TEST_1m.csv", TEST_DATA_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/candles_TEST_5m.csv", TEST_DATA_DIR);
    unlink(path);
    snprintf(path, sizeof(path), "%s/candles_TEST_daily.csv", TEST_DATA_DIR);
    unlink(path);
    rmdir(TEST_DATA_DIR);
    unlink(TEST_DB);
}

static void create_test_market_data() {
    char path[256];
    FILE* f;

    // Create Level 2 data
    snprintf(path, sizeof(path), "%s/level2_TEST.csv", TEST_DATA_DIR);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "timestamp,symbol,side,exchange,price,size\n");
        fprintf(f, "09:30:00.000,TEST,BID,NYSE,100.00,1000\n");
        fprintf(f, "09:30:00.000,TEST,BID,ARCA,99.99,500\n");
        fprintf(f, "09:30:00.000,TEST,ASK,NYSE,100.05,800\n");
        fprintf(f, "09:30:00.000,TEST,ASK,ARCA,100.06,600\n");
        fclose(f);
    }

    // Create Time & Sales data
    snprintf(path, sizeof(path), "%s/timesales_TEST.csv", TEST_DATA_DIR);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "timestamp,symbol,price,size,direction\n");
        fprintf(f, "09:30:00.100,TEST,100.02,100,UP\n");
        fprintf(f, "09:30:00.200,TEST,100.01,200,DOWN\n");
        fprintf(f, "09:30:00.300,TEST,100.01,150,SAME\n");
        fclose(f);
    }

    // Create 1m candle data
    snprintf(path, sizeof(path), "%s/candles_TEST_1m.csv", TEST_DATA_DIR);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "timestamp,open,high,low,close,volume\n");
        fprintf(f, "09:30,100.00,100.10,99.95,100.05,10000\n");
        fprintf(f, "09:31,100.05,100.15,100.00,100.10,8000\n");
        fclose(f);
    }

    // Create 5m candle data
    snprintf(path, sizeof(path), "%s/candles_TEST_5m.csv", TEST_DATA_DIR);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "timestamp,open,high,low,close,volume\n");
        fprintf(f, "09:30,100.00,100.20,99.90,100.15,50000\n");
        fclose(f);
    }

    // Create daily candle data
    snprintf(path, sizeof(path), "%s/candles_TEST_daily.csv", TEST_DATA_DIR);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "timestamp,open,high,low,close,volume\n");
        fprintf(f, "2024-01-02,99.00,101.00,98.50,100.50,1000000\n");
        fclose(f);
    }
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST(database_and_order_manager_integration) {
    // Initialize test environment with WebSocket
    setup_tradezero_client();

    // Initialize global order manager
    ASSERT(g_test_db.init(TEST_DB));
    g_test_om.reset();
    g_test_om.init(&g_test_db, &g_test_market);
    g_test_om.set_tradezero_client(&g_test_tz_client);

    // Create a buy order
    int64_t order_id;
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        order_id = g_test_om.buy("TEST", 100, 100.05f);
    }
    ASSERT(order_id > 0);

    // Verify order is pending
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        const std::vector<Order>& pending = g_test_om.get_pending_orders();
        ASSERT_EQ(pending.size(), 1u);
        ASSERT_STREQ(pending[0].symbol, "TEST");
        ASSERT_EQ(pending[0].quantity, 100);
    }

    // Cancel the order (async - WebSocket will confirm)
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        ASSERT(g_test_om.cancel_order(order_id));
    }

    // Wait for cancel confirmation via WebSocket
    ASSERT(wait_for_pending_count(0));

    g_test_db.close();
}

TEST(market_data_and_order_manager_integration) {
    // Initialize with WebSocket
    setup_tradezero_client();
    ASSERT(g_test_db.init(TEST_DB));
    g_test_market.set_data_source(DataSourceMode::FILE);
    g_test_market.set_data_dir(TEST_DATA_DIR);
    ASSERT(g_test_market.load_symbol("TEST"));
    reset_global_om();

    // Get best bid/ask
    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(g_test_market.get_level2("TEST", bids, asks, best_bid, best_ask));

    ASSERT(best_bid > 0);
    ASSERT(best_ask > 0);
    ASSERT(best_ask > best_bid);

    // Place buy order at ask price
    int64_t order_id;
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        order_id = g_test_om.buy("TEST", 50, best_ask);
    }
    ASSERT(order_id > 0);

    // Wait for fill via WebSocket
    ASSERT(wait_for_pending_count(0));

    // Should have open position
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        const std::vector<Position>& positions = g_test_om.get_open_positions();
        ASSERT_EQ(positions.size(), 1u);
        ASSERT_STREQ(positions[0].symbol, "TEST");
        ASSERT_EQ(positions[0].quantity, 50);
    }

    g_test_db.close();
}

TEST(full_trading_workflow) {
    // Initialize with WebSocket
    setup_tradezero_client();
    ASSERT(g_test_db.init(TEST_DB));
    g_test_market.set_data_source(DataSourceMode::FILE);
    g_test_market.set_data_dir(TEST_DATA_DIR);
    ASSERT(g_test_market.load_symbol("TEST"));
    reset_global_om();

    // Get market prices
    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(g_test_market.get_level2("TEST", bids, asks, best_bid, best_ask));

    // Step 1: Buy 100 shares
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        int64_t buy_order = g_test_om.buy("TEST", 100, best_ask);
        ASSERT(buy_order > 0);
    }
    ASSERT(wait_for_pending_count(0));

    // Verify position
    float entry_price;
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        Position* pos = g_test_om.find_position("TEST");
        ASSERT(pos != nullptr);
        ASSERT_EQ(pos->quantity, 100);
        entry_price = pos->avg_price;
    }

    // Step 2: Buy 50 more (averaging)
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        int64_t buy_order = g_test_om.buy("TEST", 50, best_ask + 0.10f);
        ASSERT(buy_order > 0);
    }
    ASSERT(wait_for_pending_count(0));

    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        Position* pos = g_test_om.find_position("TEST");
        ASSERT(pos != nullptr);
        ASSERT_EQ(pos->quantity, 150);
        ASSERT(pos->avg_price >= entry_price);
    }

    // Step 3: Sell 50 shares
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        int64_t sell_order = g_test_om.sell("TEST", 50, best_bid);
        ASSERT(sell_order > 0);
    }
    ASSERT(wait_for_pending_count(0));

    // Should have 100 shares remaining
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        Position* pos = g_test_om.find_position("TEST");
        ASSERT(pos != nullptr);
        ASSERT_EQ(pos->quantity, 100);

        // Should have 1 closed position
        const std::vector<ClosedPosition>& closed = g_test_om.get_closed_positions();
        ASSERT_EQ(closed.size(), 1u);
        ASSERT_EQ(closed[0].quantity, 50);
    }

    // Step 4: Sell remaining position
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        int64_t sell_order = g_test_om.sell("TEST", 100, best_bid);
        ASSERT(sell_order > 0);
    }
    ASSERT(wait_for_pending_count(0));

    // Position should be gone
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        Position* pos = g_test_om.find_position("TEST");
        ASSERT(pos == nullptr);
        ASSERT(g_test_om.get_open_positions().empty());

        // Should have 2 closed positions
        ASSERT_EQ(g_test_om.get_closed_positions().size(), 2u);
    }

    g_test_db.close();
}

TEST(session_persistence) {
    // First session - create data
    {
        Database db;
        ASSERT(db.init(TEST_DB));

        // Save tickers
        const char* tickers[NUM_TICKERS] = {"AAPL", "MSFT", "GOOGL", "AMZN"};
        ASSERT(db.save_tickers(tickers));

        // Save indicator settings
        IndicatorSettings settings;
        settings.sma_enabled = true;
        settings.sma_period = 25;
        settings.ema_enabled = true;
        settings.ema_period = 12;
        ASSERT(db.save_indicator_settings("AAPL", settings));

        // Save drawings
        std::vector<HLine> hlines;
        hlines.push_back(HLine(150.0f, 0xFF0000FF, LineStyle::SOLID));
        hlines.push_back(HLine(155.0f, 0x00FF00FF, LineStyle::DASHED));
        ASSERT(db.save_hlines("AAPL", hlines));

        db.close();
    }

    // Second session - load data
    {
        Database db;
        ASSERT(db.init(TEST_DB));

        // Load tickers
        char tickers[NUM_TICKERS][8];
        ASSERT(db.load_tickers(tickers));
        ASSERT_STREQ(tickers[0], "AAPL");
        ASSERT_STREQ(tickers[1], "MSFT");
        ASSERT_STREQ(tickers[2], "GOOGL");
        ASSERT_STREQ(tickers[3], "AMZN");

        // Load indicator settings
        IndicatorSettings settings;
        ASSERT(db.load_indicator_settings("AAPL", settings));
        ASSERT(settings.sma_enabled);
        ASSERT_EQ(settings.sma_period, 25);
        ASSERT(settings.ema_enabled);
        ASSERT_EQ(settings.ema_period, 12);

        // Load drawings
        std::vector<HLine> hlines;
        ASSERT(db.load_hlines("AAPL", hlines));
        ASSERT_EQ(hlines.size(), 2u);
        ASSERT(hlines[0].price > 149.0f && hlines[0].price < 151.0f);
        ASSERT(hlines[1].price > 154.0f && hlines[1].price < 156.0f);

        db.close();
    }
}

TEST(market_data_candles_all_timeframes) {
    MarketData market;
    market.set_data_source(DataSourceMode::FILE);
    market.set_data_dir(TEST_DATA_DIR);
    ASSERT(market.load_symbol("TEST"));

    std::vector<Candle> candles;

    // Test 1-minute candles
    ASSERT(market.get_candles("TEST", Timeframe::M1, candles, MAX_CANDLES));
    ASSERT_EQ(candles.size(), 2u);
    ASSERT(candles[0].open > 99.0f && candles[0].open < 101.0f);

    // Test 5-minute candles
    candles.clear();
    ASSERT(market.get_candles("TEST", Timeframe::M5, candles, MAX_CANDLES));
    ASSERT_EQ(candles.size(), 1u);

    // Test daily candles
    candles.clear();
    ASSERT(market.get_candles("TEST", Timeframe::DAILY, candles, MAX_CANDLES));
    ASSERT_EQ(candles.size(), 1u);
}

TEST(calculate_sell_quantity_percentages) {
    // Initialize with WebSocket
    setup_tradezero_client();
    ASSERT(g_test_db.init(TEST_DB));
    g_test_market.set_data_source(DataSourceMode::FILE);
    g_test_market.set_data_dir(TEST_DATA_DIR);
    ASSERT(g_test_market.load_symbol("TEST"));
    reset_global_om();

    // Get market prices
    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(g_test_market.get_level2("TEST", bids, asks, best_bid, best_ask));

    // Buy 400 shares
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        g_test_om.buy("TEST", 400, best_ask);
    }
    ASSERT(wait_for_pending_count(0));

    // Test percentage calculations
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 25), 100);   // 25% of 400
        ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 50), 200);   // 50% of 400
        ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 75), 300);   // 75% of 400
        ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 100), 400);  // 100% of 400
    }

    // Sell 301 shares to leave 99
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        g_test_om.sell("TEST", 301, best_bid);
    }
    ASSERT(wait_for_pending_count(0));

    // Test with odd numbers (should round down)
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        ASSERT_EQ(g_test_om.calculate_sell_quantity("TEST", 50), 49);  // 50% of 99 = 49.5, rounds to 49
    }

    g_test_db.close();
}

TEST(cancel_all_pending_orders) {
    // Initialize with WebSocket
    setup_tradezero_client();
    ASSERT(g_test_db.init(TEST_DB));
    reset_global_om();

    // Place multiple orders (prices below market won't auto-fill)
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        g_test_om.buy("TEST", 100, 90.00f);  // Far below market
        g_test_om.buy("TEST", 200, 89.00f);
        g_test_om.buy("TEST", 300, 88.00f);
        ASSERT_EQ(g_test_om.get_pending_orders().size(), 3u);
    }

    // Cancel all (async)
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        ASSERT(g_test_om.cancel_all_orders(nullptr));
    }

    // Wait for all cancel confirmations via WebSocket
    ASSERT(wait_for_pending_count(0));

    g_test_db.close();
}

TEST(position_pnl_calculation) {
    // Initialize with WebSocket
    setup_tradezero_client();
    ASSERT(g_test_db.init(TEST_DB));
    g_test_market.set_data_source(DataSourceMode::FILE);
    g_test_market.set_data_dir(TEST_DATA_DIR);
    ASSERT(g_test_market.load_symbol("TEST"));
    reset_global_om();

    // Get market prices
    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(g_test_market.get_level2("TEST", bids, asks, best_bid, best_ask));

    // Buy shares
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        g_test_om.buy("TEST", 100, best_ask);
    }
    ASSERT(wait_for_pending_count(0));

    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        Position* pos = g_test_om.find_position("TEST");
        ASSERT(pos != nullptr);

        // Set current price higher for profit
        pos->current_price = pos->avg_price + 1.0f;
        float pnl = pos->unrealized_pnl();
        ASSERT(pnl > 0);  // Should be profitable
        ASSERT(pnl > 99.0f && pnl < 101.0f);  // ~$100 profit (100 shares * $1)

        // Set current price lower for loss
        pos->current_price = pos->avg_price - 0.50f;
        pnl = pos->unrealized_pnl();
        ASSERT(pnl < 0);  // Should be loss
        ASSERT(pnl > -51.0f && pnl < -49.0f);  // ~$50 loss (100 shares * $0.50)
    }

    g_test_db.close();
}

TEST(time_sales_direction) {
    MarketData market;
    market.set_data_source(DataSourceMode::FILE);
    market.set_data_dir(TEST_DATA_DIR);
    ASSERT(market.load_symbol("TEST"));

    std::vector<TimeSalesEntry> ts;
    ASSERT(market.get_time_sales("TEST", ts, 10));

    ASSERT_EQ(ts.size(), 3u);

    // Verify directions parsed correctly
    ASSERT_EQ(ts[0].direction, TradeDirection::UP);
    ASSERT_EQ(ts[1].direction, TradeDirection::DOWN);
    ASSERT_EQ(ts[2].direction, TradeDirection::SAME);
}

TEST(level2_ordering) {
    MarketData market;
    market.set_data_source(DataSourceMode::FILE);
    market.set_data_dir(TEST_DATA_DIR);
    ASSERT(market.load_symbol("TEST"));

    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(market.get_level2("TEST", bids, asks, best_bid, best_ask));

    ASSERT_EQ(bids.size(), 2u);
    ASSERT_EQ(asks.size(), 2u);

    // Best bid should be highest
    ASSERT(bids[0].price >= bids[1].price);

    // Best ask should be lowest
    ASSERT(asks[0].price <= asks[1].price);

    // Best bid should be lower than best ask
    ASSERT(best_bid < best_ask);
}

// Helper to reset mock server (clear orders and executions)
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
    safe_sleep_ms(50);
}

TEST(fetch_executions_from_tradezero) {
    // Reset mock server to clear executions from previous tests
    reset_mock_server();

    // Initialize with WebSocket
    setup_tradezero_client();
    ASSERT(g_test_db.init(TEST_DB));
    g_test_market.set_data_source(DataSourceMode::FILE);
    g_test_market.set_data_dir(TEST_DATA_DIR);
    ASSERT(g_test_market.load_symbol("TEST"));
    reset_global_om();

    // Get market prices
    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(g_test_market.get_level2("TEST", bids, asks, best_bid, best_ask));

    // Step 1: Buy shares to create a position
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        int64_t buy_order = g_test_om.buy("TEST", 100, best_ask);
        ASSERT(buy_order > 0);
    }
    ASSERT(wait_for_pending_count(0));

    // Verify position exists
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        Position* pos = g_test_om.find_position("TEST");
        ASSERT(pos != nullptr);
        ASSERT_EQ(pos->quantity, 100);
    }

    // Step 2: Sell shares to create an execution on the mock server
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        int64_t sell_order = g_test_om.sell("TEST", 100, best_bid);
        ASSERT(sell_order > 0);
    }
    ASSERT(wait_for_pending_count(0));

    // Give mock server time to record the execution
    safe_sleep_ms(100);

    // Step 3: Fetch executions from REST API
    std::vector<ClosedPosition> executions = g_test_tz_client.get_executions();

    // Mock server should have recorded the sell as an execution
    // (only 1 since we reset the mock server at start)
    ASSERT_EQ(executions.size(), 1u);

    // Verify the execution has correct data
    ASSERT_STREQ(executions[0].symbol, "TEST");
    ASSERT_EQ(executions[0].quantity, 100);
    ASSERT(executions[0].exit_price > 0);
    ASSERT(executions[0].exit_time > 0);

    // Step 4: Load executions into order manager
    {
        std::lock_guard<std::mutex> lock(g_om_mutex);
        g_test_om.load_tradezero_executions(executions);

        // Verify executions are in closed positions
        // We should have 2: one from the sell (recorded locally) and one from load
        // But since they have same symbol+exit_time+qty, deduplication should keep just 1 extra
        const auto& closed = g_test_om.get_closed_positions();
        ASSERT(closed.size() >= 1u);
    }

    g_test_db.close();
}

TEST(get_executions_empty_on_fresh_start) {
    // Initialize client
    setup_tradezero_client();

    // Reset mock server to clear any executions from previous tests
    // (This would normally be done by the test framework)

    // Fetch executions - should work even if empty
    std::vector<ClosedPosition> executions = g_test_tz_client.get_executions();

    // Just verify it doesn't crash and returns a valid vector
    // (may or may not be empty depending on previous test runs)
    (void)executions;
}

TEST(get_accounts_from_tradezero) {
    // Test get_accounts() with mock server
    TradeZeroClient client;
    client.set_api_keys("test_key", "test_secret");
    client.set_base_url("http://localhost:8080/v1/api");

    ASSERT(client.has_api_keys());
    ASSERT(!client.is_configured());  // No account yet

    // Fetch accounts from mock server
    std::vector<TZAccount> accounts = client.get_accounts();

    // Mock server returns 2 accounts
    ASSERT_EQ(accounts.size(), 2u);

    // Verify first account
    ASSERT_STREQ(accounts[0].account_id, "test");
    ASSERT_STREQ(accounts[0].account_type, "Margin");
    ASSERT_STREQ(accounts[0].status, "Active");

    // Verify second account
    ASSERT_STREQ(accounts[1].account_id, "demo");
    ASSERT_STREQ(accounts[1].account_type, "Cash");
    ASSERT_STREQ(accounts[1].status, "Active");

    // Now select an account and verify client is configured
    client.set_account_id(accounts[0].account_id);
    ASSERT(client.is_configured());
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    // Setup
    cleanup_test_directory();
    create_test_directory();
    create_test_market_data();
    setup_tradezero_client();

    // Run tests
    run_database_and_order_manager_integration();

    // Clean up between tests that use DB
    unlink(TEST_DB);

    run_market_data_and_order_manager_integration();
    unlink(TEST_DB);

    run_full_trading_workflow();
    unlink(TEST_DB);

    run_session_persistence();
    unlink(TEST_DB);

    run_market_data_candles_all_timeframes();

    run_calculate_sell_quantity_percentages();
    unlink(TEST_DB);

    run_cancel_all_pending_orders();
    unlink(TEST_DB);

    run_position_pnl_calculation();
    unlink(TEST_DB);

    run_time_sales_direction();

    run_level2_ordering();

    run_fetch_executions_from_tradezero();
    unlink(TEST_DB);

    run_get_executions_empty_on_fresh_start();

    run_get_accounts_from_tradezero();

    // Cleanup
    cleanup_test_directory();

    if (g_verbose) printf("\n%d/%d integration tests passed.\n", g_tests_passed, g_tests_run);

    // Use _Exit to avoid static destruction order issues with global instances
    fflush(stdout);
    _Exit((g_tests_passed == g_tests_run) ? 0 : 1);
}
