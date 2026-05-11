// test_integration.cpp - Integration tests for ucharts trading platform
// Tests that all modules work together correctly
//
// NOTE: Order fill simulation (process_fills) was removed in favor of TradeZero WebSocket integration.
// Tests that relied on simulated fills are now disabled.

#include "types.h"
#include "database.h"
#include "market_data.h"
#include "order_manager.h"
#include "tradezero_client.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>

// Stub for removed OrderManager::process_fills() method
static void process_fills_stub(OrderManager*) {
    // No-op: Order fills now come from TradeZero WebSocket callbacks
}

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

// Global TradeZero client for testing
static TradeZeroClient g_test_tz_client;

static void setup_tradezero_client() {
    // Configure TradeZero client to use mock server
    g_test_tz_client.set_base_url("http://localhost:8080/v1/api");
    g_test_tz_client.set_credentials("test_key", "test_secret", "test_account");
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
    Database db;
    MarketData market;
    OrderManager order_mgr;

    ASSERT(db.init(TEST_DB));
    order_mgr.init(&db, &market);
    order_mgr.set_tradezero_client(&g_test_tz_client);

    // Create a buy order (should fail without market data for fills)
    int64_t order_id = order_mgr.buy("TEST", 100, 100.05f);
    ASSERT(order_id > 0);

    // Verify order is pending
    const std::vector<Order>& pending = order_mgr.get_pending_orders();
    ASSERT_EQ(pending.size(), 1u);
    ASSERT_STREQ(pending[0].symbol, "TEST");
    ASSERT_EQ(pending[0].quantity, 100);

    // Cancel the order
    ASSERT(order_mgr.cancel_order(order_id));
    ASSERT(order_mgr.get_pending_orders().empty());

    db.close();
}

TEST(market_data_and_order_manager_integration) {
    Database db;
    MarketData market;
    OrderManager order_mgr;

    ASSERT(db.init(TEST_DB));
    market.set_data_source(DataSourceMode::FILE);
    market.set_data_dir(TEST_DATA_DIR);
    ASSERT(market.load_symbol("TEST"));
    order_mgr.init(&db, &market);
    order_mgr.set_tradezero_client(&g_test_tz_client);

    // Get best bid/ask
    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(market.get_level2("TEST", bids, asks, best_bid, best_ask));

    ASSERT(best_bid > 0);
    ASSERT(best_ask > 0);
    ASSERT(best_ask > best_bid);

    // Place buy order at ask price (should fill immediately)
    int64_t order_id = order_mgr.buy("TEST", 50, best_ask);
    ASSERT(order_id > 0);

    // Process fills
    process_fills_stub(&order_mgr);

    // Order should be filled
    ASSERT(order_mgr.get_pending_orders().empty());

    // Should have open position
    const std::vector<Position>& positions = order_mgr.get_open_positions();
    ASSERT_EQ(positions.size(), 1u);
    ASSERT_STREQ(positions[0].symbol, "TEST");
    ASSERT_EQ(positions[0].quantity, 50);

    db.close();
}

TEST(full_trading_workflow) {
    Database db;
    MarketData market;
    OrderManager order_mgr;

    ASSERT(db.init(TEST_DB));
    market.set_data_source(DataSourceMode::FILE);
    market.set_data_dir(TEST_DATA_DIR);
    ASSERT(market.load_symbol("TEST"));
    order_mgr.init(&db, &market);
    order_mgr.set_tradezero_client(&g_test_tz_client);

    // Get market prices
    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(market.get_level2("TEST", bids, asks, best_bid, best_ask));

    // Step 1: Buy 100 shares
    int64_t buy_order = order_mgr.buy("TEST", 100, best_ask);
    ASSERT(buy_order > 0);
    process_fills_stub(&order_mgr);

    // Verify position
    Position* pos = order_mgr.find_position("TEST");
    ASSERT(pos != nullptr);
    ASSERT_EQ(pos->quantity, 100);
    float entry_price = pos->avg_price;

    // Step 2: Buy 50 more (averaging)
    buy_order = order_mgr.buy("TEST", 50, best_ask + 0.10f);
    process_fills_stub(&order_mgr);

    pos = order_mgr.find_position("TEST");
    ASSERT(pos != nullptr);
    ASSERT_EQ(pos->quantity, 150);
    ASSERT(pos->avg_price >= entry_price);  // Avg price should be >= original

    // Step 3: Sell 50 shares
    int64_t sell_order = order_mgr.sell("TEST", 50, best_bid);
    ASSERT(sell_order > 0);
    process_fills_stub(&order_mgr);

    // Should have 100 shares remaining
    pos = order_mgr.find_position("TEST");
    ASSERT(pos != nullptr);
    ASSERT_EQ(pos->quantity, 100);

    // Should have 1 closed position
    const std::vector<ClosedPosition>& closed = order_mgr.get_closed_positions();
    ASSERT_EQ(closed.size(), 1u);
    ASSERT_EQ(closed[0].quantity, 50);

    // Step 4: Sell remaining position
    sell_order = order_mgr.sell("TEST", 100, best_bid);
    process_fills_stub(&order_mgr);

    // Position should be gone
    pos = order_mgr.find_position("TEST");
    ASSERT(pos == nullptr);
    ASSERT(order_mgr.get_open_positions().empty());

    // Should have 2 closed positions
    ASSERT_EQ(order_mgr.get_closed_positions().size(), 2u);

    db.close();
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
    Database db;
    MarketData market;
    OrderManager order_mgr;

    ASSERT(db.init(TEST_DB));
    market.set_data_source(DataSourceMode::FILE);
    market.set_data_dir(TEST_DATA_DIR);
    ASSERT(market.load_symbol("TEST"));
    order_mgr.init(&db, &market);
    order_mgr.set_tradezero_client(&g_test_tz_client);

    // Buy 400 shares
    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(market.get_level2("TEST", bids, asks, best_bid, best_ask));

    order_mgr.buy("TEST", 400, best_ask);
    process_fills_stub(&order_mgr);

    // Test percentage calculations
    ASSERT_EQ(order_mgr.calculate_sell_quantity("TEST", 25), 100);   // 25% of 400
    ASSERT_EQ(order_mgr.calculate_sell_quantity("TEST", 50), 200);   // 50% of 400
    ASSERT_EQ(order_mgr.calculate_sell_quantity("TEST", 75), 300);   // 75% of 400
    ASSERT_EQ(order_mgr.calculate_sell_quantity("TEST", 100), 400);  // 100% of 400

    // Test with odd numbers (should round down)
    order_mgr.sell("TEST", 301, best_bid);  // Leave 99 shares
    process_fills_stub(&order_mgr);

    ASSERT_EQ(order_mgr.calculate_sell_quantity("TEST", 50), 49);  // 50% of 99 = 49.5, rounds to 49

    db.close();
}

TEST(cancel_all_pending_orders) {
    Database db;
    MarketData market;
    OrderManager order_mgr;

    ASSERT(db.init(TEST_DB));
    order_mgr.init(&db, &market);
    order_mgr.set_tradezero_client(&g_test_tz_client);

    // Place multiple orders that won't fill (no market data)
    order_mgr.buy("TEST", 100, 99.00f);  // Below market
    order_mgr.buy("TEST", 200, 98.00f);
    order_mgr.buy("TEST", 300, 97.00f);

    ASSERT_EQ(order_mgr.get_pending_orders().size(), 3u);

    // Cancel all
    ASSERT(order_mgr.cancel_all_orders(nullptr));
    ASSERT(order_mgr.get_pending_orders().empty());

    db.close();
}

TEST(position_pnl_calculation) {
    Database db;
    MarketData market;
    OrderManager order_mgr;

    ASSERT(db.init(TEST_DB));
    market.set_data_source(DataSourceMode::FILE);
    market.set_data_dir(TEST_DATA_DIR);
    ASSERT(market.load_symbol("TEST"));
    order_mgr.init(&db, &market);
    order_mgr.set_tradezero_client(&g_test_tz_client);

    // Buy shares
    std::vector<Level2Entry> bids, asks;
    float best_bid = 0, best_ask = 0;
    ASSERT(market.get_level2("TEST", bids, asks, best_bid, best_ask));

    order_mgr.buy("TEST", 100, best_ask);
    process_fills_stub(&order_mgr);

    Position* pos = order_mgr.find_position("TEST");
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

    db.close();
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

    // Cleanup
    cleanup_test_directory();

    if (g_verbose) printf("\n%d/%d integration tests passed.\n", g_tests_passed, g_tests_run);

    // Use _Exit to avoid static destruction order issues with global instances
    fflush(stdout);
    _Exit((g_tests_passed == g_tests_run) ? 0 : 1);
}
