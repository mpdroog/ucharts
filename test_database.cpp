// test_database.cpp - Unit tests for database module
// Compile: clang++ -std=c++11 -o test_database test_database.cpp database.cpp -lsqlite3

#include "database.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h>

// ============================================================================
// Test helpers
// ============================================================================

#include "test_common.h"
static const char* TEST_DB = "test_ucharts.db";

static void cleanup_test_db() {
    unlink(TEST_DB);
}

// ============================================================================
// Tests
// ============================================================================

TEST(database_init) {
    cleanup_test_db();
    Database db;
    ASSERT_FALSE(db.is_open());

    ASSERT_TRUE(db.init(TEST_DB));
    ASSERT_TRUE(db.is_open());

    db.close();
    ASSERT_FALSE(db.is_open());
    cleanup_test_db();
}

TEST(database_init_creates_tables) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    // Should be able to init again without errors (tables exist)
    db.close();
    ASSERT_TRUE(db.init(TEST_DB));
    db.close();
    cleanup_test_db();
}

TEST(save_and_load_tickers) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    const char* symbols[NUM_TICKERS] = {"AAPL", "MSFT", "GOOGL", "AMZN"};
    ASSERT_TRUE(db.save_tickers(symbols));

    char loaded[NUM_TICKERS][8];
    ASSERT_TRUE(db.load_tickers(loaded));

    ASSERT_STREQ(loaded[0], "AAPL");
    ASSERT_STREQ(loaded[1], "MSFT");
    ASSERT_STREQ(loaded[2], "GOOGL");
    ASSERT_STREQ(loaded[3], "AMZN");

    db.close();
    cleanup_test_db();
}

TEST(save_tickers_with_empty_slots) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    const char* symbols[NUM_TICKERS] = {"AAPL", "", "GOOGL", nullptr};
    ASSERT_TRUE(db.save_tickers(symbols));

    char loaded[NUM_TICKERS][8];
    ASSERT_TRUE(db.load_tickers(loaded));

    ASSERT_STREQ(loaded[0], "AAPL");
    ASSERT_STREQ(loaded[1], "");  // Empty slot
    ASSERT_STREQ(loaded[2], "GOOGL");
    ASSERT_STREQ(loaded[3], "");  // Null treated as empty

    db.close();
    cleanup_test_db();
}

TEST(tickers_persist_across_sessions) {
    cleanup_test_db();

    // First session - save tickers
    {
        Database db;
        ASSERT_TRUE(db.init(TEST_DB));
        const char* symbols[NUM_TICKERS] = {"AAPL", "MSFT", "GOOGL", "AMZN"};
        ASSERT_TRUE(db.save_tickers(symbols));
        db.close();
    }

    // Second session - load tickers
    {
        Database db;
        ASSERT_TRUE(db.init(TEST_DB));
        char loaded[NUM_TICKERS][8];
        ASSERT_TRUE(db.load_tickers(loaded));
        ASSERT_STREQ(loaded[0], "AAPL");
        ASSERT_STREQ(loaded[3], "AMZN");
        db.close();
    }

    cleanup_test_db();
}

TEST(save_and_load_order) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    Order order;
    std::strcpy(order.symbol, "AAPL");
    order.side = OrderSide::BUY;
    order.quantity = 100;
    order.filled = 0;
    order.price = 150.50f;
    order.status = OrderStatus::PENDING;
    order.created_at = 1704067200;  // 2024-01-01

    int64_t id = db.save_order(order);
    ASSERT_TRUE(id > 0);

    std::vector<Order> orders;
    ASSERT_TRUE(db.load_pending_orders(orders));
    ASSERT_EQ(orders.size(), 1u);
    ASSERT_STREQ(orders[0].symbol, "AAPL");
    ASSERT_EQ(orders[0].side, OrderSide::BUY);
    ASSERT_EQ(orders[0].quantity, 100);
    ASSERT_FLOAT_EQ(orders[0].price, 150.50f, 0.01f);

    db.close();
    cleanup_test_db();
}

TEST(update_order_status) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    Order order;
    std::strcpy(order.symbol, "AAPL");
    order.side = OrderSide::BUY;
    order.quantity = 100;
    order.filled = 0;
    order.price = 150.50f;
    order.status = OrderStatus::PENDING;
    order.created_at = 1704067200;

    order.id = db.save_order(order);
    ASSERT_TRUE(order.id > 0);

    // Update to filled
    order.filled = 100;
    order.status = OrderStatus::FILLED;
    ASSERT_TRUE(db.update_order(order));

    // Should no longer be in pending
    std::vector<Order> pending;
    ASSERT_TRUE(db.load_pending_orders(pending));
    ASSERT_EQ(pending.size(), 0u);

    // Should be in history
    std::vector<Order> history;
    ASSERT_TRUE(db.load_order_history(history));
    ASSERT_EQ(history.size(), 1u);
    ASSERT_EQ(history[0].filled, 100);
    ASSERT_EQ(history[0].status, OrderStatus::FILLED);

    db.close();
    cleanup_test_db();
}

TEST(save_and_load_position) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    Position pos;
    std::strcpy(pos.symbol, "AAPL");
    pos.quantity = 100;
    pos.avg_price = 150.25f;

    ASSERT_TRUE(db.save_position(pos));

    std::vector<Position> positions;
    ASSERT_TRUE(db.load_open_positions(positions));
    ASSERT_EQ(positions.size(), 1u);
    ASSERT_STREQ(positions[0].symbol, "AAPL");
    ASSERT_EQ(positions[0].quantity, 100);
    ASSERT_FLOAT_EQ(positions[0].avg_price, 150.25f, 0.01f);

    db.close();
    cleanup_test_db();
}

TEST(update_position) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    Position pos;
    std::strcpy(pos.symbol, "AAPL");
    pos.quantity = 100;
    pos.avg_price = 150.25f;
    ASSERT_TRUE(db.save_position(pos));

    // Update position
    pos.quantity = 200;
    pos.avg_price = 151.00f;
    ASSERT_TRUE(db.update_position(pos));

    std::vector<Position> positions;
    ASSERT_TRUE(db.load_open_positions(positions));
    ASSERT_EQ(positions.size(), 1u);  // Should still be one position
    ASSERT_EQ(positions[0].quantity, 200);
    ASSERT_FLOAT_EQ(positions[0].avg_price, 151.00f, 0.01f);

    db.close();
    cleanup_test_db();
}

TEST(delete_position) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    Position pos;
    std::strcpy(pos.symbol, "AAPL");
    pos.quantity = 100;
    pos.avg_price = 150.25f;
    ASSERT_TRUE(db.save_position(pos));

    ASSERT_TRUE(db.delete_position("AAPL"));

    std::vector<Position> positions;
    ASSERT_TRUE(db.load_open_positions(positions));
    ASSERT_EQ(positions.size(), 0u);

    db.close();
    cleanup_test_db();
}

TEST(save_and_load_closed_position) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    ClosedPosition pos;
    std::strcpy(pos.symbol, "AAPL");
    pos.quantity = 100;
    pos.entry_price = 150.00f;
    pos.exit_price = 155.00f;
    pos.entry_time = 1704067200;
    pos.exit_time = 1704153600;

    ASSERT_TRUE(db.save_closed_position(pos));

    std::vector<ClosedPosition> positions;
    ASSERT_TRUE(db.load_closed_positions(positions));
    ASSERT_EQ(positions.size(), 1u);
    ASSERT_STREQ(positions[0].symbol, "AAPL");
    ASSERT_EQ(positions[0].quantity, 100);
    ASSERT_FLOAT_EQ(positions[0].entry_price, 150.00f, 0.01f);
    ASSERT_FLOAT_EQ(positions[0].exit_price, 155.00f, 0.01f);
    ASSERT_FLOAT_EQ(positions[0].pnl_usd(), 500.00f, 0.01f);

    db.close();
    cleanup_test_db();
}

TEST(save_and_load_hlines) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    std::vector<HLine> lines;
    lines.push_back(HLine(100.0f, 0xFF0000FF, LineStyle::SOLID));
    lines.push_back(HLine(105.0f, 0x00FF00FF, LineStyle::DASHED));
    lines.push_back(HLine(110.0f, 0x0000FFFF, LineStyle::DOTTED));

    ASSERT_TRUE(db.save_hlines("AAPL", lines));

    std::vector<HLine> loaded;
    ASSERT_TRUE(db.load_hlines("AAPL", loaded));
    ASSERT_EQ(loaded.size(), 3u);
    ASSERT_FLOAT_EQ(loaded[0].price, 100.0f, 0.01f);
    ASSERT_EQ(loaded[0].color, 0xFF0000FFu);
    ASSERT_EQ(loaded[0].style, LineStyle::SOLID);
    ASSERT_EQ(loaded[1].style, LineStyle::DASHED);
    ASSERT_EQ(loaded[2].style, LineStyle::DOTTED);

    db.close();
    cleanup_test_db();
}

TEST(hlines_per_symbol) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    std::vector<HLine> aapl_lines;
    aapl_lines.push_back(HLine(100.0f, 0xFF0000FF, LineStyle::SOLID));

    std::vector<HLine> msft_lines;
    msft_lines.push_back(HLine(200.0f, 0x00FF00FF, LineStyle::DASHED));
    msft_lines.push_back(HLine(210.0f, 0x00FF00FF, LineStyle::DASHED));

    ASSERT_TRUE(db.save_hlines("AAPL", aapl_lines));
    ASSERT_TRUE(db.save_hlines("MSFT", msft_lines));

    std::vector<HLine> loaded_aapl;
    ASSERT_TRUE(db.load_hlines("AAPL", loaded_aapl));
    ASSERT_EQ(loaded_aapl.size(), 1u);

    std::vector<HLine> loaded_msft;
    ASSERT_TRUE(db.load_hlines("MSFT", loaded_msft));
    ASSERT_EQ(loaded_msft.size(), 2u);

    // Empty symbol
    std::vector<HLine> loaded_empty;
    ASSERT_TRUE(db.load_hlines("GOOGL", loaded_empty));
    ASSERT_EQ(loaded_empty.size(), 0u);

    db.close();
    cleanup_test_db();
}

TEST(save_and_load_trendlines) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    std::vector<TrendLine> lines;
    TrendLine tl;
    tl.candle_start = 10;
    tl.candle_end = 20;
    tl.price_start = 100.0f;
    tl.price_end = 110.0f;
    tl.color = 0xFFFFFFFF;
    tl.style = LineStyle::SOLID;
    lines.push_back(tl);

    ASSERT_TRUE(db.save_trendlines("AAPL", lines));

    std::vector<TrendLine> loaded;
    ASSERT_TRUE(db.load_trendlines("AAPL", loaded));
    ASSERT_EQ(loaded.size(), 1u);
    ASSERT_EQ(loaded[0].candle_start, 10);
    ASSERT_EQ(loaded[0].candle_end, 20);
    ASSERT_FLOAT_EQ(loaded[0].price_start, 100.0f, 0.01f);
    ASSERT_FLOAT_EQ(loaded[0].price_end, 110.0f, 0.01f);

    db.close();
    cleanup_test_db();
}

TEST(save_and_load_indicator_settings) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    IndicatorSettings settings;
    settings.sma_enabled = true;
    settings.sma_period = 50;
    settings.ema_enabled = true;
    settings.ema_period = 12;
    settings.boll_enabled = false;
    settings.boll_period = 20;
    settings.volume_enabled = false;

    ASSERT_TRUE(db.save_indicator_settings("AAPL", settings));

    IndicatorSettings loaded;
    ASSERT_TRUE(db.load_indicator_settings("AAPL", loaded));
    ASSERT_TRUE(loaded.sma_enabled);
    ASSERT_EQ(loaded.sma_period, 50);
    ASSERT_TRUE(loaded.ema_enabled);
    ASSERT_EQ(loaded.ema_period, 12);
    ASSERT_FALSE(loaded.boll_enabled);
    ASSERT_FALSE(loaded.volume_enabled);

    db.close();
    cleanup_test_db();
}

TEST(indicator_settings_defaults) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    // Load settings for symbol with no saved settings
    IndicatorSettings settings;
    ASSERT_TRUE(db.load_indicator_settings("UNKNOWN", settings));

    // Should have defaults
    ASSERT_FALSE(settings.sma_enabled);
    ASSERT_EQ(settings.sma_period, 20);
    ASSERT_FALSE(settings.ema_enabled);
    ASSERT_EQ(settings.ema_period, 9);
    ASSERT_FALSE(settings.boll_enabled);
    ASSERT_EQ(settings.boll_period, 20);
    ASSERT_TRUE(settings.volume_enabled);

    db.close();
    cleanup_test_db();
}

TEST(indicator_settings_per_symbol) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    IndicatorSettings aapl_settings;
    aapl_settings.sma_enabled = true;
    aapl_settings.sma_period = 20;

    IndicatorSettings msft_settings;
    msft_settings.sma_enabled = true;
    msft_settings.sma_period = 50;

    ASSERT_TRUE(db.save_indicator_settings("AAPL", aapl_settings));
    ASSERT_TRUE(db.save_indicator_settings("MSFT", msft_settings));

    IndicatorSettings loaded_aapl;
    ASSERT_TRUE(db.load_indicator_settings("AAPL", loaded_aapl));
    ASSERT_EQ(loaded_aapl.sma_period, 20);

    IndicatorSettings loaded_msft;
    ASSERT_TRUE(db.load_indicator_settings("MSFT", loaded_msft));
    ASSERT_EQ(loaded_msft.sma_period, 50);

    db.close();
    cleanup_test_db();
}

TEST(multiple_orders_in_pending) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    for (int i = 0; i < 5; i++) {
        Order order;
        std::snprintf(order.symbol, sizeof(order.symbol), "SYM%d", i);
        order.side = (i % 2 == 0) ? OrderSide::BUY : OrderSide::SELL;
        order.quantity = 100 * (i + 1);
        order.filled = 0;
        order.price = 100.0f + static_cast<float>(i);
        order.status = OrderStatus::PENDING;
        order.created_at = 1704067200 + i;
        ASSERT_TRUE(db.save_order(order) > 0);
    }

    std::vector<Order> pending;
    ASSERT_TRUE(db.load_pending_orders(pending));
    ASSERT_EQ(pending.size(), 5u);

    // Should be ordered by created_at DESC
    ASSERT_STREQ(pending[0].symbol, "SYM4");
    ASSERT_STREQ(pending[4].symbol, "SYM0");

    db.close();
    cleanup_test_db();
}

TEST(order_history_limit) {
    cleanup_test_db();
    Database db;
    ASSERT_TRUE(db.init(TEST_DB));

    for (int i = 0; i < 10; i++) {
        Order order;
        std::strcpy(order.symbol, "AAPL");
        order.side = OrderSide::BUY;
        order.quantity = 100;
        order.filled = 100;
        order.price = 150.0f;
        order.status = OrderStatus::FILLED;
        order.created_at = 1704067200 + i;
        ASSERT_TRUE(db.save_order(order) > 0);
    }

    std::vector<Order> history;
    ASSERT_TRUE(db.load_order_history(history, 5));
    ASSERT_EQ(history.size(), 5u);

    db.close();
    cleanup_test_db();
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    RUN_TEST(database_init);
    RUN_TEST(database_init_creates_tables);
    RUN_TEST(save_and_load_tickers);
    RUN_TEST(save_tickers_with_empty_slots);
    RUN_TEST(tickers_persist_across_sessions);
    RUN_TEST(save_and_load_order);
    RUN_TEST(update_order_status);
    RUN_TEST(save_and_load_position);
    RUN_TEST(update_position);
    RUN_TEST(delete_position);
    RUN_TEST(save_and_load_closed_position);
    RUN_TEST(save_and_load_hlines);
    RUN_TEST(hlines_per_symbol);
    RUN_TEST(save_and_load_trendlines);
    RUN_TEST(save_and_load_indicator_settings);
    RUN_TEST(indicator_settings_defaults);
    RUN_TEST(indicator_settings_per_symbol);
    RUN_TEST(multiple_orders_in_pending);
    RUN_TEST(order_history_limit);

    test_summary();
    cleanup_test_db();
    return 0;
}
