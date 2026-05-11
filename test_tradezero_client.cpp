// test_tradezero_client.cpp - Tests for TradeZero REST API client
// Compile: See Makefile test target

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include "test_common.h"
#include "tradezero_client.h"
#include "types.h"

// ============================================================================
// Tests for JSON Parsing
// ============================================================================

TEST(parse_empty_positions) {
    TradeZeroClient client;
    std::vector<Position> positions;

    std::string json = "[]";
    bool result = client.parse_positions(json, positions);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(positions.size() == 0);
}

TEST(parse_single_position) {
    TradeZeroClient client;
    std::vector<Position> positions;

    std::string json = R"([{
        "id": "pos123",
        "accountId": "acc456",
        "symbol": "AAPL",
        "shares": 100.0,
        "side": "Long",
        "priceAvg": 150.50,
        "priceOpen": 150.00,
        "priceClose": 151.00,
        "dayOvernight": "Day",
        "createdDate": "2024-01-15T10:30:00Z",
        "updatedDate": "2024-01-15T15:45:00Z"
    }])";

    bool result = client.parse_positions(json, positions);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(positions.size() == 1);
    ASSERT_STREQ(positions[0].symbol, "AAPL");
    ASSERT_TRUE(positions[0].quantity == 100);
    ASSERT_FLOAT_EQ(positions[0].avg_price, 150.50f, 0.01f);
    ASSERT_FLOAT_EQ(positions[0].current_price, 151.00f, 0.01f);
}

TEST(parse_multiple_positions) {
    TradeZeroClient client;
    std::vector<Position> positions;

    std::string json = R"([
        {
            "symbol": "AAPL",
            "shares": 100.0,
            "priceAvg": 150.50,
            "priceClose": 151.00
        },
        {
            "symbol": "TSLA",
            "shares": 50.0,
            "priceAvg": 200.00,
            "priceClose": 205.00
        }
    ])";

    bool result = client.parse_positions(json, positions);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(positions.size() == 2);
    ASSERT_STREQ(positions[0].symbol, "AAPL");
    ASSERT_STREQ(positions[1].symbol, "TSLA");
}

TEST(parse_empty_orders) {
    TradeZeroClient client;
    std::vector<Order> orders;

    std::string json = "[]";
    bool result = client.parse_orders(json, orders);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(orders.size() == 0);
}

TEST(parse_single_order) {
    TradeZeroClient client;
    std::vector<Order> orders;

    std::string json = R"([{
        "accountId": "acc456",
        "clientOrderId": "order123",
        "symbol": "AAPL",
        "side": "Buy",
        "orderStatus": "Filled",
        "orderType": "Limit",
        "orderQuantity": 100,
        "executed": 100,
        "leavesQuantity": 0,
        "limitPrice": 150.00,
        "priceAvg": 150.25,
        "lastPrice": 150.25,
        "lastQuantity": 100,
        "startTime": "2024-01-15T10:30:00Z",
        "lastUpdated": "2024-01-15T10:31:00Z"
    }])";

    bool result = client.parse_orders(json, orders);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(orders.size() == 1);
    ASSERT_STREQ(orders[0].symbol, "AAPL");
    ASSERT_STREQ(orders[0].client_order_id, "order123");
    ASSERT_TRUE(orders[0].side == OrderSide::BUY);
    ASSERT_TRUE(orders[0].quantity == 100);
    ASSERT_TRUE(orders[0].filled == 100);
    ASSERT_TRUE(orders[0].status == OrderStatus::FILLED);
}

TEST(parse_order_status_pending) {
    TradeZeroClient client;
    std::vector<Order> orders;

    std::string json = R"([{
        "clientOrderId": "order456",
        "symbol": "TSLA",
        "side": "Sell",
        "orderStatus": "Accepted",
        "orderQuantity": 50,
        "executed": 0,
        "limitPrice": 200.00
    }])";

    bool result = client.parse_orders(json, orders);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(orders.size() == 1);
    ASSERT_TRUE(orders[0].side == OrderSide::SELL);
    ASSERT_TRUE(orders[0].status == OrderStatus::PENDING);
    ASSERT_TRUE(orders[0].filled == 0);
}

TEST(parse_order_status_partial) {
    TradeZeroClient client;
    std::vector<Order> orders;

    std::string json = R"([{
        "clientOrderId": "order789",
        "symbol": "NVDA",
        "side": "Buy",
        "orderStatus": "PartiallyFilled",
        "orderQuantity": 100,
        "executed": 50,
        "limitPrice": 300.00
    }])";

    bool result = client.parse_orders(json, orders);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(orders.size() == 1);
    ASSERT_TRUE(orders[0].status == OrderStatus::PARTIAL);
    ASSERT_TRUE(orders[0].quantity == 100);
    ASSERT_TRUE(orders[0].filled == 50);
}

TEST(parse_order_status_cancelled) {
    TradeZeroClient client;
    std::vector<Order> orders;

    std::string json = R"([{
        "clientOrderId": "order999",
        "symbol": "AMD",
        "side": "Buy",
        "orderStatus": "Cancelled",
        "orderQuantity": 75,
        "executed": 0,
        "limitPrice": 100.00
    }])";

    bool result = client.parse_orders(json, orders);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(orders.size() == 1);
    ASSERT_TRUE(orders[0].status == OrderStatus::CANCELLED);
}

TEST(client_initialization) {
    TradeZeroClient client;

    ASSERT_TRUE(!client.is_configured());

    client.set_credentials("test_key_id", "test_secret_key", "test_account_id");

    ASSERT_TRUE(client.is_configured());
}

TEST(parse_malformed_json) {
    TradeZeroClient client;
    std::vector<Position> positions;

    std::string json = "{invalid json";
    bool result = client.parse_positions(json, positions);

    ASSERT_TRUE(result == false);
    ASSERT_TRUE(positions.size() == 0);
}

TEST(parse_position_with_short_shares) {
    TradeZeroClient client;
    std::vector<Position> positions;

    std::string json = R"([{
        "symbol": "AAPL",
        "shares": -100.0,
        "side": "Short",
        "priceAvg": 150.00,
        "priceClose": 145.00
    }])";

    bool result = client.parse_positions(json, positions);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(positions.size() == 1);
    ASSERT_TRUE(positions[0].quantity == -100);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    // JSON parsing tests
    RUN_TEST(parse_empty_positions);
    RUN_TEST(parse_single_position);
    RUN_TEST(parse_multiple_positions);
    RUN_TEST(parse_empty_orders);
    RUN_TEST(parse_single_order);
    RUN_TEST(parse_order_status_pending);
    RUN_TEST(parse_order_status_partial);
    RUN_TEST(parse_order_status_cancelled);
    RUN_TEST(parse_malformed_json);
    RUN_TEST(parse_position_with_short_shares);

    // Client initialization test
    RUN_TEST(client_initialization);

    test_summary();
    return 0;
}
