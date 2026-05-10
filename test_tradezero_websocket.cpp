// test_tradezero_websocket.cpp - Tests for TradeZero WebSocket client
// Compile: See Makefile test target

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <atomic>
#include "tradezero_websocket.h"
#include "types.h"

// Test helper macros
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL: %s (line %d): %s\n", __func__, __LINE__, message); \
            return false; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        std::printf("PASS: %s\n", __func__); \
        return true; \
    } while(0)

// ============================================================================
// Mock Data Structures for Testing
// ============================================================================

static std::atomic<bool> g_pnl_snapshot_called{false};
static std::atomic<bool> g_agg_update_called{false};
static std::atomic<bool> g_position_pnl_called{false};
static std::atomic<bool> g_order_called{false};
static std::atomic<bool> g_position_called{false};

static TZPnLSnapshot g_last_pnl_snapshot;
static TZAggUpdate g_last_agg_update;
static TZPositionPnL g_last_position_pnl;
static TZOrderUpdate g_last_order;
static TZPositionUpdate g_last_position;

// Reset test state
void reset_test_state() {
    g_pnl_snapshot_called = false;
    g_agg_update_called = false;
    g_position_pnl_called = false;
    g_order_called = false;
    g_position_called = false;
}

// ============================================================================
// Tests for Data Structures
// ============================================================================

bool test_pnl_snapshot_initialization() {
    TZPnLSnapshot snapshot;

    TEST_ASSERT(snapshot.account_value == 0.0f, "account_value should be 0");
    TEST_ASSERT(snapshot.available_cash == 0.0f, "available_cash should be 0");
    TEST_ASSERT(snapshot.buying_power == 0.0f, "buying_power should be 0");
    TEST_ASSERT(snapshot.day_pnl == 0.0f, "day_pnl should be 0");
    TEST_ASSERT(snapshot.positions.size() == 0, "positions should be empty");
    TEST_PASS();
}

bool test_agg_update_initialization() {
    TZAggUpdate update;

    TEST_ASSERT(update.account_value == 0.0f, "account_value should be 0");
    TEST_ASSERT(update.day_pnl == 0.0f, "day_pnl should be 0");
    TEST_ASSERT(update.exposure == 0.0f, "exposure should be 0");
    TEST_PASS();
}

bool test_position_pnl_initialization() {
    TZPositionPnL pos;

    TEST_ASSERT(pos.position_id[0] == '\0', "position_id should be empty");
    TEST_ASSERT(pos.symbol[0] == '\0', "symbol should be empty");
    TEST_ASSERT(pos.unrealized_pnl == 0.0f, "unrealized_pnl should be 0");
    TEST_ASSERT(pos.realized_pnl == 0.0f, "realized_pnl should be 0");
    TEST_PASS();
}

bool test_order_update_initialization() {
    TZOrderUpdate order;

    TEST_ASSERT(order.account_id[0] == '\0', "account_id should be empty");
    TEST_ASSERT(order.client_order_id[0] == '\0', "client_order_id should be empty");
    TEST_ASSERT(order.symbol[0] == '\0', "symbol should be empty");
    TEST_ASSERT(order.order_quantity == 0, "order_quantity should be 0");
    TEST_ASSERT(order.executed == 0, "executed should be 0");
    TEST_PASS();
}

bool test_position_update_initialization() {
    TZPositionUpdate pos;

    TEST_ASSERT(pos.id[0] == '\0', "id should be empty");
    TEST_ASSERT(pos.symbol[0] == '\0', "symbol should be empty");
    TEST_ASSERT(pos.shares == 0.0f, "shares should be 0");
    TEST_ASSERT(pos.price_avg == 0.0f, "price_avg should be 0");
    TEST_PASS();
}

bool test_websocket_initialization() {
    TradeZeroWebSocket ws;

    TEST_ASSERT(!ws.is_connected(), "WebSocket should not be connected initially");
    TEST_PASS();
}

bool test_websocket_credentials() {
    TradeZeroWebSocket ws;

    ws.set_credentials("test_key", "test_secret", "test_account");

    // Credentials are set (no public getter to verify, but no crash is good)
    TEST_PASS();
}

bool test_websocket_callbacks() {
    TradeZeroWebSocket ws;
    reset_test_state();

    // Set callbacks
    ws.set_pnl_snapshot_callback([](const TZPnLSnapshot& snapshot) {
        g_pnl_snapshot_called = true;
        g_last_pnl_snapshot = snapshot;
    });

    ws.set_agg_update_callback([](const TZAggUpdate& update) {
        g_agg_update_called = true;
        g_last_agg_update = update;
    });

    ws.set_position_pnl_callback([](const TZPositionPnL& pos) {
        g_position_pnl_called = true;
        g_last_position_pnl = pos;
    });

    ws.set_order_callback([](const TZOrderUpdate& order) {
        g_order_called = true;
        g_last_order = order;
    });

    ws.set_position_callback([](const TZPositionUpdate& pos) {
        g_position_called = true;
        g_last_position = pos;
    });

    // Callbacks are set (no public way to verify, but no crash is good)
    TEST_PASS();
}

bool test_pnl_snapshot_copy() {
    TZPnLSnapshot snapshot1;
    snapshot1.account_value = 50000.0f;
    snapshot1.day_pnl = 500.0f;
    snapshot1.buying_power = 100000.0f;

    TZPnLSnapshot snapshot2 = snapshot1;

    TEST_ASSERT(snapshot2.account_value == 50000.0f, "account_value should be copied");
    TEST_ASSERT(snapshot2.day_pnl == 500.0f, "day_pnl should be copied");
    TEST_ASSERT(snapshot2.buying_power == 100000.0f, "buying_power should be copied");
    TEST_PASS();
}

bool test_order_update_copy() {
    TZOrderUpdate order1;
    std::strncpy(order1.symbol, "AAPL", sizeof(order1.symbol));
    std::strncpy(order1.client_order_id, "order123", sizeof(order1.client_order_id));
    order1.order_quantity = 100;
    order1.executed = 50;

    TZOrderUpdate order2 = order1;

    TEST_ASSERT(std::strcmp(order2.symbol, "AAPL") == 0, "symbol should be copied");
    TEST_ASSERT(std::strcmp(order2.client_order_id, "order123") == 0, "client_order_id should be copied");
    TEST_ASSERT(order2.order_quantity == 100, "order_quantity should be copied");
    TEST_ASSERT(order2.executed == 50, "executed should be copied");
    TEST_PASS();
}

bool test_position_update_copy() {
    TZPositionUpdate pos1;
    std::strncpy(pos1.symbol, "TSLA", sizeof(pos1.symbol));
    pos1.shares = 100.0f;
    pos1.price_avg = 200.50f;

    TZPositionUpdate pos2 = pos1;

    TEST_ASSERT(std::strcmp(pos2.symbol, "TSLA") == 0, "symbol should be copied");
    TEST_ASSERT(pos2.shares == 100.0f, "shares should be copied");
    TEST_ASSERT(pos2.price_avg == 200.50f, "price_avg should be copied");
    TEST_PASS();
}

bool test_pnl_snapshot_with_positions() {
    TZPnLSnapshot snapshot;
    snapshot.account_value = 100000.0f;

    TZPositionPnL pos1;
    std::strncpy(pos1.symbol, "AAPL", sizeof(pos1.symbol));
    pos1.unrealized_pnl = 500.0f;

    TZPositionPnL pos2;
    std::strncpy(pos2.symbol, "TSLA", sizeof(pos2.symbol));
    pos2.unrealized_pnl = -200.0f;

    snapshot.positions.push_back(pos1);
    snapshot.positions.push_back(pos2);

    TEST_ASSERT(snapshot.positions.size() == 2, "Should have 2 positions");
    TEST_ASSERT(std::strcmp(snapshot.positions[0].symbol, "AAPL") == 0, "First position should be AAPL");
    TEST_ASSERT(std::strcmp(snapshot.positions[1].symbol, "TSLA") == 0, "Second position should be TSLA");
    TEST_ASSERT(snapshot.positions[0].unrealized_pnl == 500.0f, "AAPL P&L should be 500");
    TEST_ASSERT(snapshot.positions[1].unrealized_pnl == -200.0f, "TSLA P&L should be -200");
    TEST_PASS();
}

bool test_stream_enum_values() {
    TZStream pnl = TZStream::PNL;
    TZStream portfolio = TZStream::PORTFOLIO;

    TEST_ASSERT(pnl == TZStream::PNL, "PNL stream should match");
    TEST_ASSERT(portfolio == TZStream::PORTFOLIO, "PORTFOLIO stream should match");
    TEST_ASSERT(pnl != portfolio, "Streams should be different");
    TEST_PASS();
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    int passed = 0;
    int failed = 0;

    std::printf("Running TradeZero WebSocket Tests...\n\n");

    // Data structure initialization tests
    if (test_pnl_snapshot_initialization()) passed++; else failed++;
    if (test_agg_update_initialization()) passed++; else failed++;
    if (test_position_pnl_initialization()) passed++; else failed++;
    if (test_order_update_initialization()) passed++; else failed++;
    if (test_position_update_initialization()) passed++; else failed++;

    // WebSocket client tests
    if (test_websocket_initialization()) passed++; else failed++;
    if (test_websocket_credentials()) passed++; else failed++;
    if (test_websocket_callbacks()) passed++; else failed++;

    // Copy and data tests
    if (test_pnl_snapshot_copy()) passed++; else failed++;
    if (test_order_update_copy()) passed++; else failed++;
    if (test_position_update_copy()) passed++; else failed++;
    if (test_pnl_snapshot_with_positions()) passed++; else failed++;
    if (test_stream_enum_values()) passed++; else failed++;

    std::printf("\n========================================\n");
    std::printf("Test Results: %d passed, %d failed\n", passed, failed);
    std::printf("========================================\n");

    return (failed == 0) ? 0 : 1;
}
