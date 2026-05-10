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
// JSON Parsing Tests (using handle_message)
// ============================================================================

bool test_parse_pnl_snapshot_json() {
    TradeZeroWebSocket ws;
    reset_test_state();

    ws.set_pnl_snapshot_callback([](const TZPnLSnapshot& snapshot) {
        g_pnl_snapshot_called = true;
        g_last_pnl_snapshot = snapshot;
    });

    // Simulate P&L snapshot JSON from TradeZero
    const char* json = R"({
        "action": "init",
        "accountValue": 50000.50,
        "availableCash": 25000.25,
        "dayUnrealized": 150.75,
        "dayRealized": 50.25,
        "dayPnl": 201.00,
        "totalUnrealized": 300.50,
        "allowedLeverage": 4.0,
        "positions": [
            {
                "positionId": "pos123",
                "symbol": "AAPL",
                "pnlCalc": {
                    "unrealizedPnL": 500.0,
                    "dayUnrealizedPnL": 150.0,
                    "pctPnLMove": 2.5,
                    "dayPctPnLMove": 1.5,
                    "exposure": 10000.0
                },
                "realizedPnl": 100.0,
                "dayRealizedPnl": 50.0
            }
        ]
    })";

    ws.handle_message(json);

    TEST_ASSERT(g_pnl_snapshot_called, "P&L snapshot callback should be called");
    TEST_ASSERT(g_last_pnl_snapshot.account_value == 50000.50f, "account_value should be parsed");
    TEST_ASSERT(g_last_pnl_snapshot.available_cash == 25000.25f, "available_cash should be parsed");
    TEST_ASSERT(g_last_pnl_snapshot.day_pnl == 201.00f, "day_pnl should be parsed");
    TEST_ASSERT(g_last_pnl_snapshot.buying_power == 200002.0f, "buying_power should be calculated");
    TEST_ASSERT(g_last_pnl_snapshot.positions.size() == 1, "Should parse 1 position");
    TEST_ASSERT(std::strcmp(g_last_pnl_snapshot.positions[0].symbol, "AAPL") == 0, "Position symbol should be AAPL");
    TEST_ASSERT(g_last_pnl_snapshot.positions[0].unrealized_pnl == 500.0f, "Position unrealized P&L should be parsed");
    TEST_PASS();
}

bool test_parse_agg_update_json() {
    TradeZeroWebSocket ws;
    reset_test_state();

    ws.set_agg_update_callback([](const TZAggUpdate& update) {
        g_agg_update_called = true;
        g_last_agg_update = update;
    });

    const char* json = R"({
        "target": "aggCalcs",
        "accountValue": 51000.75,
        "exposure": 15000.0,
        "dayUnrealized": 175.50,
        "dayPnl": 225.75,
        "totalUnrealized": 350.25,
        "equityRatio": 0.85
    })";

    ws.handle_message(json);

    TEST_ASSERT(g_agg_update_called, "Aggregate update callback should be called");
    TEST_ASSERT(g_last_agg_update.account_value == 51000.75f, "account_value should be parsed");
    TEST_ASSERT(g_last_agg_update.exposure == 15000.0f, "exposure should be parsed");
    TEST_ASSERT(g_last_agg_update.day_pnl == 225.75f, "day_pnl should be parsed");
    TEST_ASSERT(g_last_agg_update.equity_ratio == 0.85f, "equity_ratio should be parsed");
    TEST_PASS();
}

bool test_parse_position_pnl_json() {
    TradeZeroWebSocket ws;
    reset_test_state();

    ws.set_position_pnl_callback([](const TZPositionPnL& pos) {
        g_position_pnl_called = true;
        g_last_position_pnl = pos;
    });

    const char* json = R"({
        "target": "position",
        "positionId": "pos456",
        "symbol": "TSLA",
        "pnlCalc": {
            "unrealizedPnL": -200.0,
            "dayUnrealizedPnL": -50.0,
            "pctPnLMove": -1.5,
            "dayPctPnLMove": -0.5,
            "exposure": 8000.0
        },
        "realizedPnl": 0.0,
        "dayRealizedPnl": 0.0
    })";

    ws.handle_message(json);

    TEST_ASSERT(g_position_pnl_called, "Position P&L callback should be called");
    TEST_ASSERT(std::strcmp(g_last_position_pnl.symbol, "TSLA") == 0, "symbol should be parsed");
    TEST_ASSERT(std::strcmp(g_last_position_pnl.position_id, "pos456") == 0, "position_id should be parsed");
    TEST_ASSERT(g_last_position_pnl.unrealized_pnl == -200.0f, "unrealized_pnl should be parsed");
    TEST_ASSERT(g_last_position_pnl.day_unrealized_pnl == -50.0f, "day_unrealized_pnl should be parsed");
    TEST_PASS();
}

bool test_parse_order_update_json() {
    TradeZeroWebSocket ws;
    reset_test_state();

    ws.set_order_callback([](const TZOrderUpdate& order) {
        g_order_called = true;
        g_last_order = order;
    });

    const char* json = R"({
        "subscription": "Order",
        "accountId": "acc123",
        "clientOrderId": "order789",
        "symbol": "NVDA",
        "side": "Buy",
        "orderStatus": "Filled",
        "orderType": "Limit",
        "orderQuantity": 100,
        "executed": 100,
        "leavesQuantity": 0,
        "limitPrice": 450.50,
        "priceAvg": 450.25,
        "lastPrice": 450.25,
        "lastQuantity": 100,
        "startTime": "2024-01-15T10:30:00Z",
        "lastUpdated": "2024-01-15T10:30:05Z"
    })";

    ws.handle_message(json);

    TEST_ASSERT(g_order_called, "Order callback should be called");
    TEST_ASSERT(std::strcmp(g_last_order.symbol, "NVDA") == 0, "symbol should be parsed");
    TEST_ASSERT(std::strcmp(g_last_order.client_order_id, "order789") == 0, "client_order_id should be parsed");
    TEST_ASSERT(std::strcmp(g_last_order.side, "Buy") == 0, "side should be parsed");
    TEST_ASSERT(std::strcmp(g_last_order.order_status, "Filled") == 0, "orderStatus should be parsed");
    TEST_ASSERT(g_last_order.order_quantity == 100, "orderQuantity should be parsed");
    TEST_ASSERT(g_last_order.executed == 100, "executed should be parsed");
    TEST_ASSERT(g_last_order.limit_price == 450.50f, "limitPrice should be parsed");
    TEST_PASS();
}

bool test_parse_position_update_json() {
    TradeZeroWebSocket ws;
    reset_test_state();

    ws.set_position_callback([](const TZPositionUpdate& pos) {
        g_position_called = true;
        g_last_position = pos;
    });

    const char* json = R"({
        "subscription": "Position",
        "id": "pos789",
        "accountId": "acc123",
        "symbol": "AMD",
        "shares": 200.0,
        "side": "Long",
        "priceAvg": 120.50,
        "priceOpen": 120.00,
        "priceClose": 122.00,
        "dayOvernight": "Day",
        "createdDate": "2024-01-15T09:30:00Z",
        "updatedDate": "2024-01-15T10:30:00Z"
    })";

    ws.handle_message(json);

    TEST_ASSERT(g_position_called, "Position callback should be called");
    TEST_ASSERT(std::strcmp(g_last_position.symbol, "AMD") == 0, "symbol should be parsed");
    TEST_ASSERT(std::strcmp(g_last_position.id, "pos789") == 0, "id should be parsed");
    TEST_ASSERT(g_last_position.shares == 200.0f, "shares should be parsed");
    TEST_ASSERT(g_last_position.price_avg == 120.50f, "priceAvg should be parsed");
    TEST_ASSERT(std::strcmp(g_last_position.side, "Long") == 0, "side should be parsed");
    TEST_PASS();
}

bool test_parse_invalid_json() {
    TradeZeroWebSocket ws;
    reset_test_state();

    ws.set_order_callback([](const TZOrderUpdate& order) {
        g_order_called = true;
        g_last_order = order;
    });

    // Invalid JSON - should not crash
    const char* invalid_json = "{invalid json here";
    ws.handle_message(invalid_json);

    TEST_ASSERT(!g_order_called, "Callback should not be called for invalid JSON");
    TEST_PASS();
}

bool test_parse_missing_fields() {
    TradeZeroWebSocket ws;
    reset_test_state();

    ws.set_order_callback([](const TZOrderUpdate& order) {
        g_order_called = true;
        g_last_order = order;
    });

    // Valid JSON but missing fields - should handle gracefully
    const char* json = R"({
        "subscription": "Order",
        "symbol": "AAPL"
    })";

    ws.handle_message(json);

    TEST_ASSERT(g_order_called, "Callback should be called even with missing fields");
    TEST_ASSERT(std::strcmp(g_last_order.symbol, "AAPL") == 0, "symbol should still be parsed");
    TEST_ASSERT(g_last_order.order_quantity == 0, "Missing fields should have default values");
    TEST_PASS();
}

// ============================================================================
// Message Queue Tests
// ============================================================================

bool test_message_queue() {
    TradeZeroWebSocket ws;

    TEST_ASSERT(!ws.has_queued_messages(), "Queue should be empty initially");

    ws.queue_message("test message 1");
    TEST_ASSERT(ws.has_queued_messages(), "Queue should have messages after queueing");

    ws.queue_message("test message 2");
    ws.queue_message("test message 3");

    std::string msg1 = ws.dequeue_message();
    TEST_ASSERT(msg1 == "test message 1", "First message should be dequeued first (FIFO)");

    std::string msg2 = ws.dequeue_message();
    TEST_ASSERT(msg2 == "test message 2", "Second message should be dequeued second");

    TEST_ASSERT(ws.has_queued_messages(), "Queue should still have one message");

    std::string msg3 = ws.dequeue_message();
    TEST_ASSERT(msg3 == "test message 3", "Third message should be dequeued third");

    TEST_ASSERT(!ws.has_queued_messages(), "Queue should be empty after dequeueing all");

    std::string empty = ws.dequeue_message();
    TEST_ASSERT(empty == "", "Dequeueing from empty queue should return empty string");

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

    // JSON parsing tests
    std::printf("\n--- JSON Parsing Tests ---\n");
    if (test_parse_pnl_snapshot_json()) passed++; else failed++;
    if (test_parse_agg_update_json()) passed++; else failed++;
    if (test_parse_position_pnl_json()) passed++; else failed++;
    if (test_parse_order_update_json()) passed++; else failed++;
    if (test_parse_position_update_json()) passed++; else failed++;
    if (test_parse_invalid_json()) passed++; else failed++;
    if (test_parse_missing_fields()) passed++; else failed++;

    // Message queue tests
    std::printf("\n--- Message Queue Tests ---\n");
    if (test_message_queue()) passed++; else failed++;

    std::printf("\n========================================\n");
    std::printf("Test Results: %d passed, %d failed\n", passed, failed);
    std::printf("========================================\n");

    return (failed == 0) ? 0 : 1;
}
