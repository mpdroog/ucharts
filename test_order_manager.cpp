// test_order_manager.cpp - Unit tests for order manager module
// Compile: clang++ -std=c++11 -o test_order_manager test_order_manager.cpp order_manager.cpp database.cpp market_data.cpp -lsqlite3

#include "order_manager.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <sys/stat.h>

// ============================================================================
// Test helpers
// ============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;
static const char* TEST_DB = "test_order_manager.db";
static const char* TEST_DATA_DIR = "test_data_om";

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    g_tests_run++; \
    std::printf("Running %s... ", #name); \
    test_##name(); \
    g_tests_passed++; \
    std::printf("PASSED\n"); \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::printf("FAILED: %s is false (line %d)\n", #cond, __LINE__); \
        std::exit(1); \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::printf("FAILED: %s != %s (line %d)\n", #a, #b, __LINE__); \
        std::exit(1); \
    } \
} while(0)

#define ASSERT_STREQ(a, b) do { \
    if (std::strcmp((a), (b)) != 0) { \
        std::printf("FAILED: \"%s\" != \"%s\" (line %d)\n", (a), (b), __LINE__); \
        std::exit(1); \
    } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (std::fabs((a) - (b)) > (eps)) { \
        std::printf("FAILED: %s (%.4f) != %s (%.4f) (line %d)\n", #a, (double)(a), #b, (double)(b), __LINE__); \
        std::exit(1); \
    } \
} while(0)

// Setup test environment
static Database g_test_db;
static MarketData g_test_market;

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

static void init_test_env() {
    unlink(TEST_DB);
    g_test_db.init(TEST_DB);
    g_test_market.set_data_source(DataSourceMode::FILE);
    g_test_market.set_data_dir(TEST_DATA_DIR);
    (void)g_test_market.load_symbol("TEST");  // May fail if no test data, that's OK
}

// ============================================================================
// Tests
// ============================================================================

TEST(buy_order_creates_pending) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    int64_t id = om.buy("TEST", 100, 100.10f);
    ASSERT_TRUE(id > 0);

    const auto& pending = om.get_pending_orders();
    ASSERT_EQ(pending.size(), 1u);
    ASSERT_STREQ(pending[0].symbol, "TEST");
    ASSERT_EQ(pending[0].side, OrderSide::BUY);
    ASSERT_EQ(pending[0].quantity, 100);
    ASSERT_FLOAT_EQ(pending[0].price, 100.10f, 0.01f);
    ASSERT_EQ(pending[0].status, OrderStatus::PENDING);
}

TEST(buy_order_invalid_params) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    ASSERT_EQ(om.buy("", 100, 100.00f), -1);
    ASSERT_EQ(om.buy(nullptr, 100, 100.00f), -1);
    ASSERT_EQ(om.buy("TEST", 0, 100.00f), -1);
    ASSERT_EQ(om.buy("TEST", -1, 100.00f), -1);
    ASSERT_EQ(om.buy("TEST", 100, 0.0f), -1);
    ASSERT_EQ(om.buy("TEST", 100, -1.0f), -1);
}

TEST(sell_without_position_fails) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // No position, so sell should fail
    int64_t id = om.sell("TEST", 100, 100.00f);
    ASSERT_EQ(id, -1);
}

TEST(cancel_order) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    int64_t id = om.buy("TEST", 100, 100.10f);
    ASSERT_TRUE(id > 0);
    ASSERT_EQ(om.get_pending_orders().size(), 1u);

    ASSERT_TRUE(om.cancel_order(id));
    ASSERT_EQ(om.get_pending_orders().size(), 0u);
}

TEST(cancel_nonexistent_order) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    ASSERT_FALSE(om.cancel_order(999));
}

TEST(cancel_all_orders) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    om.buy("TEST", 100, 100.10f);
    om.buy("TEST", 200, 100.20f);
    om.buy("TEST", 300, 100.30f);
    ASSERT_EQ(om.get_pending_orders().size(), 3u);

    ASSERT_TRUE(om.cancel_all_orders());
    ASSERT_EQ(om.get_pending_orders().size(), 0u);
}

TEST(buy_fills_when_price_crosses_ask) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // Place buy order at ask price (100.05)
    int64_t id = om.buy("TEST", 100, 100.05f);
    ASSERT_TRUE(id > 0);
    ASSERT_EQ(om.get_pending_orders().size(), 1u);

    // Process fills - should fill because price >= ask
    om.process_fills();

    // Order should be filled and removed from pending
    ASSERT_EQ(om.get_pending_orders().size(), 0u);

    // Position should be created
    const auto& positions = om.get_open_positions();
    ASSERT_EQ(positions.size(), 1u);
    ASSERT_STREQ(positions[0].symbol, "TEST");
    ASSERT_EQ(positions[0].quantity, 100);
}

TEST(buy_doesnt_fill_below_ask) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // Place buy order below ask price
    int64_t id = om.buy("TEST", 100, 100.00f);  // Ask is 100.05
    ASSERT_TRUE(id > 0);

    // Process fills - should NOT fill
    om.process_fills();

    // Order should still be pending
    ASSERT_EQ(om.get_pending_orders().size(), 1u);
    ASSERT_EQ(om.get_open_positions().size(), 0u);
}

TEST(sell_fills_when_price_crosses_bid) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // First buy to get a position
    om.buy("TEST", 100, 100.10f);
    om.process_fills();
    ASSERT_EQ(om.get_open_positions().size(), 1u);

    // Now sell at or below bid (99.95)
    int64_t id = om.sell("TEST", 50, 99.95f);
    ASSERT_TRUE(id > 0);

    om.process_fills();

    // Sell should fill
    ASSERT_EQ(om.get_pending_orders().size(), 0u);

    // Position should be reduced
    const auto& positions = om.get_open_positions();
    ASSERT_EQ(positions.size(), 1u);
    ASSERT_EQ(positions[0].quantity, 50);

    // Closed position should be recorded
    ASSERT_EQ(om.get_closed_positions().size(), 1u);
}

TEST(sell_entire_position) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // Buy to get position
    om.buy("TEST", 100, 100.10f);
    om.process_fills();

    // Sell all
    om.sell("TEST", 100, 99.95f);
    om.process_fills();

    // Position should be removed
    ASSERT_EQ(om.get_open_positions().size(), 0u);

    // Closed position should be recorded
    ASSERT_EQ(om.get_closed_positions().size(), 1u);
}

TEST(calculate_sell_quantity) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // Buy 100 shares
    om.buy("TEST", 100, 100.10f);
    om.process_fills();

    ASSERT_EQ(om.calculate_sell_quantity("TEST", 25), 25);
    ASSERT_EQ(om.calculate_sell_quantity("TEST", 50), 50);
    ASSERT_EQ(om.calculate_sell_quantity("TEST", 75), 75);
    ASSERT_EQ(om.calculate_sell_quantity("TEST", 100), 100);
}

TEST(calculate_sell_quantity_rounds_down) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // Buy 100 shares
    om.buy("TEST", 100, 100.10f);
    om.process_fills();

    // 33% of 100 = 33
    ASSERT_EQ(om.calculate_sell_quantity("TEST", 33), 33);

    // 10% of 100 = 10
    ASSERT_EQ(om.calculate_sell_quantity("TEST", 10), 10);
}

TEST(calculate_sell_quantity_minimum_one) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // Buy 3 shares
    om.buy("TEST", 3, 100.10f);
    om.process_fills();

    // 25% of 3 = 0.75, should round to at least 1
    ASSERT_EQ(om.calculate_sell_quantity("TEST", 25), 1);
}

TEST(position_avg_price_calculation) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // Buy 100 at 100.00
    om.buy("TEST", 100, 100.10f);
    om.process_fills();

    Position* pos = om.find_position("TEST");
    ASSERT_TRUE(pos != nullptr);
    ASSERT_FLOAT_EQ(pos->avg_price, 100.10f, 0.01f);

    // Buy another 100 at 101.00
    om.buy("TEST", 100, 101.10f);
    om.process_fills();

    pos = om.find_position("TEST");
    ASSERT_TRUE(pos != nullptr);
    ASSERT_EQ(pos->quantity, 200);
    // Avg should be (100*100.10 + 100*101.10) / 200 = 100.60
    ASSERT_FLOAT_EQ(pos->avg_price, 100.60f, 0.01f);
}

TEST(closed_position_pnl) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    // Buy 100 at 100.10
    om.buy("TEST", 100, 100.10f);
    om.process_fills();

    // Sell 100 at 99.95 (assuming fill at sell price for test)
    om.sell("TEST", 100, 99.95f);
    om.process_fills();

    const auto& closed = om.get_closed_positions();
    ASSERT_EQ(closed.size(), 1u);
    ASSERT_EQ(closed[0].quantity, 100);
    ASSERT_FLOAT_EQ(closed[0].entry_price, 100.10f, 0.01f);
    ASSERT_FLOAT_EQ(closed[0].exit_price, 99.95f, 0.01f);
    // P&L = 100 * (99.95 - 100.10) = -15
    ASSERT_FLOAT_EQ(closed[0].pnl_usd(), -15.0f, 0.01f);
}

TEST(find_position) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    ASSERT_TRUE(om.find_position("TEST") == nullptr);

    om.buy("TEST", 100, 100.10f);
    om.process_fills();

    Position* pos = om.find_position("TEST");
    ASSERT_TRUE(pos != nullptr);
    ASSERT_STREQ(pos->symbol, "TEST");
}

TEST(find_order) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    int64_t id = om.buy("TEST", 100, 99.00f);  // Won't fill (below ask)

    Order* order = om.find_order(id);
    ASSERT_TRUE(order != nullptr);
    ASSERT_EQ(order->id, id);

    Order* nonexistent = om.find_order(999);
    ASSERT_TRUE(nonexistent == nullptr);
}

TEST(order_callback) {
    init_test_env();
    OrderManager om;
    om.init(&g_test_db, &g_test_market);

    int callback_count = 0;
    om.set_order_callback([&callback_count](const Order&) {
        callback_count++;
    });

    om.buy("TEST", 100, 100.10f);
    ASSERT_EQ(callback_count, 1);

    om.process_fills();
    ASSERT_EQ(callback_count, 2);  // Called again on fill
}

TEST(persistence) {
    // First session - create orders and positions
    {
        unlink(TEST_DB);
        Database db;
        db.init(TEST_DB);

        MarketData market;
        market.set_data_source(DataSourceMode::FILE);
        market.set_data_dir(TEST_DATA_DIR);
        ASSERT_TRUE(market.load_symbol("TEST"));

        OrderManager om;
        om.init(&db, &market);

        om.buy("TEST", 100, 100.10f);
        om.process_fills();

        om.save_to_database();
        db.close();
    }

    // Second session - verify data loaded
    {
        Database db;
        db.init(TEST_DB);

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

int main() {
    std::printf("Running order manager tests...\n\n");

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
    RUN_TEST(order_callback);
    RUN_TEST(persistence);

    cleanup_test_data();

    std::printf("\n%d/%d tests passed.\n", g_tests_passed, g_tests_run);

    // Use _Exit to avoid static destruction order issues with global instances
    std::fflush(stdout);
    _Exit(0);
}
