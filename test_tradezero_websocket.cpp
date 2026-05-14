// test_tradezero_websocket.cpp - Tests for TradeZero WebSocket client
// Compile: See Makefile test target

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <atomic>
#include "test_common.h"
#include "tradezero_websocket.h"
#include "types.h"

// ============================================================================
// Mock Data Structures for Testing
// ============================================================================

static std::atomic<bool> g_pnl_snapshot_called{false};
static std::atomic<bool> g_agg_update_called{false};
static std::atomic<bool> g_position_pnl_called{false};
static std::atomic<bool> g_order_called{false};
static std::atomic<bool> g_position_called{false};
static std::atomic<bool> g_connection_callback_called{false};
static std::atomic<bool> g_last_connection_state{false};

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
    g_connection_callback_called = false;
    g_last_connection_state = false;
}

// ============================================================================
// Tests for Data Structures
// ============================================================================

TEST(pnl_snapshot_initialization) {
    TZPnLSnapshot snapshot;
    ASSERT_FLOAT_EQ(snapshot.account_value, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(snapshot.available_cash, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(snapshot.buying_power, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(snapshot.day_pnl, 0.0f, 0.01f);
    ASSERT_TRUE(snapshot.positions.size() == 0);
}

TEST(agg_update_initialization) {
    TZAggUpdate update;
    ASSERT_FLOAT_EQ(update.account_value, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(update.day_pnl, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(update.exposure, 0.0f, 0.01f);
}

TEST(position_pnl_initialization) {
    TZPositionPnL pos;
    ASSERT_TRUE(pos.position_id[0] == '\0');
    ASSERT_TRUE(pos.symbol[0] == '\0');
    ASSERT_FLOAT_EQ(pos.unrealized_pnl, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(pos.realized_pnl, 0.0f, 0.01f);
}

TEST(order_update_initialization) {
    TZOrderUpdate order;
    ASSERT_TRUE(order.account_id[0] == '\0');
    ASSERT_TRUE(order.client_order_id[0] == '\0');
    ASSERT_TRUE(order.symbol[0] == '\0');
    ASSERT_TRUE(order.order_quantity == 0);
    ASSERT_TRUE(order.executed == 0);
}

TEST(position_update_initialization) {
    TZPositionUpdate pos;
    ASSERT_TRUE(pos.id[0] == '\0');
    ASSERT_TRUE(pos.symbol[0] == '\0');
    ASSERT_FLOAT_EQ(pos.shares, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(pos.price_avg, 0.0f, 0.01f);
}

TEST(websocket_initialization) {
    TradeZeroWebSocket ws;
    ASSERT_TRUE(!ws.is_connected());
}

TEST(websocket_credentials) {
    TradeZeroWebSocket ws;
    ws.set_credentials("test_key", "test_secret", "test_account");
    // Credentials are set (no public getter to verify, but no crash is good)
}

TEST(websocket_callbacks) {
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

    ws.set_connection_callback([](bool connected) {
        g_connection_callback_called = true;
        g_last_connection_state = connected;
    });

    // Callbacks are set (no public way to verify, but no crash is good)
}

TEST(websocket_connection_callback_can_be_set) {
    TradeZeroWebSocket ws;
    reset_test_state();

    ws.set_connection_callback([](bool connected) {
        g_connection_callback_called = true;
        g_last_connection_state = connected;
    });

    // Callback is set (actual connection test requires mock server)
}

TEST(pnl_snapshot_copy) {
    TZPnLSnapshot snapshot1;
    snapshot1.account_value = 50000.0f;
    snapshot1.day_pnl = 500.0f;
    snapshot1.buying_power = 100000.0f;

    TZPnLSnapshot snapshot2 = snapshot1;

    ASSERT_FLOAT_EQ(snapshot2.account_value, 50000.0f, 0.01f);
    ASSERT_FLOAT_EQ(snapshot2.day_pnl, 500.0f, 0.01f);
    ASSERT_FLOAT_EQ(snapshot2.buying_power, 100000.0f, 0.01f);
}

TEST(order_update_copy) {
    TZOrderUpdate order1;
    std::strncpy(order1.symbol, "AAPL", sizeof(order1.symbol));
    std::strncpy(order1.client_order_id, "order123", sizeof(order1.client_order_id));
    order1.order_quantity = 100;
    order1.executed = 50;

    TZOrderUpdate order2 = order1;

    ASSERT_STREQ(order2.symbol, "AAPL");
    ASSERT_STREQ(order2.client_order_id, "order123");
    ASSERT_TRUE(order2.order_quantity == 100);
    ASSERT_TRUE(order2.executed == 50);
}

TEST(position_update_copy) {
    TZPositionUpdate pos1;
    std::strncpy(pos1.symbol, "TSLA", sizeof(pos1.symbol));
    pos1.shares = 100.0f;
    pos1.price_avg = 200.50f;

    TZPositionUpdate pos2 = pos1;

    ASSERT_STREQ(pos2.symbol, "TSLA");
    ASSERT_FLOAT_EQ(pos2.shares, 100.0f, 0.01f);
    ASSERT_FLOAT_EQ(pos2.price_avg, 200.50f, 0.01f);
}

TEST(pnl_snapshot_with_positions) {
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

    ASSERT_TRUE(snapshot.positions.size() == 2);
    ASSERT_STREQ(snapshot.positions[0].symbol, "AAPL");
    ASSERT_STREQ(snapshot.positions[1].symbol, "TSLA");
    ASSERT_FLOAT_EQ(snapshot.positions[0].unrealized_pnl, 500.0f, 0.01f);
    ASSERT_FLOAT_EQ(snapshot.positions[1].unrealized_pnl, -200.0f, 0.01f);
}

TEST(stream_enum_values) {
    TZStream pnl = TZStream::PNL;
    TZStream portfolio = TZStream::PORTFOLIO;

    ASSERT_TRUE(pnl == TZStream::PNL);
    ASSERT_TRUE(portfolio == TZStream::PORTFOLIO);
    ASSERT_TRUE(pnl != portfolio);
}

// ============================================================================
// JSON Parsing Tests (using handle_message)
// ============================================================================

TEST(parse_pnl_snapshot_json) {
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

    ASSERT_TRUE(g_pnl_snapshot_called);
    ASSERT_FLOAT_EQ(g_last_pnl_snapshot.account_value, 50000.50f, 0.01f);
    ASSERT_FLOAT_EQ(g_last_pnl_snapshot.available_cash, 25000.25f, 0.01f);
    ASSERT_FLOAT_EQ(g_last_pnl_snapshot.day_pnl, 201.00f, 0.01f);
    ASSERT_FLOAT_EQ(g_last_pnl_snapshot.buying_power, 200002.0f, 1.0f);
    ASSERT_TRUE(g_last_pnl_snapshot.positions.size() == 1);
    ASSERT_STREQ(g_last_pnl_snapshot.positions[0].symbol, "AAPL");
    ASSERT_FLOAT_EQ(g_last_pnl_snapshot.positions[0].unrealized_pnl, 500.0f, 0.01f);
}

TEST(parse_agg_update_json) {
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

    ASSERT_TRUE(g_agg_update_called);
    ASSERT_FLOAT_EQ(g_last_agg_update.account_value, 51000.75f, 0.01f);
    ASSERT_FLOAT_EQ(g_last_agg_update.exposure, 15000.0f, 0.01f);
    ASSERT_FLOAT_EQ(g_last_agg_update.day_pnl, 225.75f, 0.01f);
    ASSERT_FLOAT_EQ(g_last_agg_update.equity_ratio, 0.85f, 0.01f);
}

TEST(parse_position_pnl_json) {
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

    ASSERT_TRUE(g_position_pnl_called);
    ASSERT_STREQ(g_last_position_pnl.symbol, "TSLA");
    ASSERT_STREQ(g_last_position_pnl.position_id, "pos456");
    ASSERT_FLOAT_EQ(g_last_position_pnl.unrealized_pnl, -200.0f, 0.01f);
    ASSERT_FLOAT_EQ(g_last_position_pnl.day_unrealized_pnl, -50.0f, 0.01f);
}

TEST(parse_order_update_json) {
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

    ASSERT_TRUE(g_order_called);
    ASSERT_STREQ(g_last_order.symbol, "NVDA");
    ASSERT_STREQ(g_last_order.client_order_id, "order789");
    ASSERT_STREQ(g_last_order.side, "Buy");
    ASSERT_STREQ(g_last_order.order_status, "Filled");
    ASSERT_TRUE(g_last_order.order_quantity == 100);
    ASSERT_TRUE(g_last_order.executed == 100);
    ASSERT_FLOAT_EQ(g_last_order.limit_price, 450.50f, 0.01f);
}

TEST(parse_position_update_json) {
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

    ASSERT_TRUE(g_position_called);
    ASSERT_STREQ(g_last_position.symbol, "AMD");
    ASSERT_STREQ(g_last_position.id, "pos789");
    ASSERT_FLOAT_EQ(g_last_position.shares, 200.0f, 0.01f);
    ASSERT_FLOAT_EQ(g_last_position.price_avg, 120.50f, 0.01f);
    ASSERT_STREQ(g_last_position.side, "Long");
}

TEST(parse_invalid_json) {
    TradeZeroWebSocket ws;
    reset_test_state();

    ws.set_order_callback([](const TZOrderUpdate& order) {
        g_order_called = true;
        g_last_order = order;
    });

    // Invalid JSON - should not crash
    const char* invalid_json = "{invalid json here";
    ws.handle_message(invalid_json);

    ASSERT_TRUE(!g_order_called);
}

TEST(parse_missing_fields) {
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

    ASSERT_TRUE(g_order_called);
    ASSERT_STREQ(g_last_order.symbol, "AAPL");
    ASSERT_TRUE(g_last_order.order_quantity == 0);
}

// ============================================================================
// Message Queue Tests
// ============================================================================

TEST(message_queue) {
    TradeZeroWebSocket ws;

    ASSERT_TRUE(!ws.has_queued_messages());

    ws.queue_message("test message 1");
    ASSERT_TRUE(ws.has_queued_messages());

    ws.queue_message("test message 2");
    ws.queue_message("test message 3");

    std::string msg1 = ws.dequeue_message();
    ASSERT_STREQ(msg1.c_str(), "test message 1");

    std::string msg2 = ws.dequeue_message();
    ASSERT_STREQ(msg2.c_str(), "test message 2");

    ASSERT_TRUE(ws.has_queued_messages());

    std::string msg3 = ws.dequeue_message();
    ASSERT_STREQ(msg3.c_str(), "test message 3");

    ASSERT_TRUE(!ws.has_queued_messages());

    std::string empty = ws.dequeue_message();
    ASSERT_STREQ(empty.c_str(), "");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    // Data structure initialization tests
    RUN_TEST(pnl_snapshot_initialization);
    RUN_TEST(agg_update_initialization);
    RUN_TEST(position_pnl_initialization);
    RUN_TEST(order_update_initialization);
    RUN_TEST(position_update_initialization);

    // WebSocket client tests
    RUN_TEST(websocket_initialization);
    RUN_TEST(websocket_credentials);
    RUN_TEST(websocket_callbacks);
    RUN_TEST(websocket_connection_callback_can_be_set);

    // Copy and data tests
    RUN_TEST(pnl_snapshot_copy);
    RUN_TEST(order_update_copy);
    RUN_TEST(position_update_copy);
    RUN_TEST(pnl_snapshot_with_positions);
    RUN_TEST(stream_enum_values);

    // JSON parsing tests
    RUN_TEST(parse_pnl_snapshot_json);
    RUN_TEST(parse_agg_update_json);
    RUN_TEST(parse_position_pnl_json);
    RUN_TEST(parse_order_update_json);
    RUN_TEST(parse_position_update_json);
    RUN_TEST(parse_invalid_json);
    RUN_TEST(parse_missing_fields);

    // Message queue tests
    RUN_TEST(message_queue);

    test_summary();
    return 0;
}
