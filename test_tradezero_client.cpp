// test_tradezero_client.cpp - Tests for TradeZero REST API client
// Compile: See Makefile test target

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <vector>
#include "tradezero_client.h"
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
// Tests for JSON Parsing
// ============================================================================

bool test_parse_empty_positions() {
    TradeZeroClient client;
    std::vector<Position> positions;

    std::string json = "[]";
    bool result = client.parse_positions(json, positions);

    TEST_ASSERT(result == true, "Should parse empty array");
    TEST_ASSERT(positions.size() == 0, "Should have zero positions");
    TEST_PASS();
}

bool test_parse_single_position() {
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

    TEST_ASSERT(result == true, "Should parse valid position");
    TEST_ASSERT(positions.size() == 1, "Should have one position");
    TEST_ASSERT(std::strcmp(positions[0].symbol, "AAPL") == 0, "Symbol should be AAPL");
    TEST_ASSERT(positions[0].quantity == 100, "Quantity should be 100");
    TEST_ASSERT(positions[0].avg_price == 150.50f, "Avg price should be 150.50");
    TEST_ASSERT(positions[0].current_price == 151.00f, "Current price should be 151.00");
    TEST_PASS();
}

bool test_parse_multiple_positions() {
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

    TEST_ASSERT(result == true, "Should parse multiple positions");
    TEST_ASSERT(positions.size() == 2, "Should have two positions");
    TEST_ASSERT(std::strcmp(positions[0].symbol, "AAPL") == 0, "First symbol should be AAPL");
    TEST_ASSERT(std::strcmp(positions[1].symbol, "TSLA") == 0, "Second symbol should be TSLA");
    TEST_PASS();
}

bool test_parse_empty_orders() {
    TradeZeroClient client;
    std::vector<Order> orders;

    std::string json = "[]";
    bool result = client.parse_orders(json, orders);

    TEST_ASSERT(result == true, "Should parse empty array");
    TEST_ASSERT(orders.size() == 0, "Should have zero orders");
    TEST_PASS();
}

bool test_parse_single_order() {
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

    TEST_ASSERT(result == true, "Should parse valid order");
    TEST_ASSERT(orders.size() == 1, "Should have one order");
    TEST_ASSERT(std::strcmp(orders[0].symbol, "AAPL") == 0, "Symbol should be AAPL");
    TEST_ASSERT(std::strcmp(orders[0].client_order_id, "order123") == 0, "Client order ID should match");
    TEST_ASSERT(orders[0].side == OrderSide::BUY, "Side should be BUY");
    TEST_ASSERT(orders[0].quantity == 100, "Quantity should be 100");
    TEST_ASSERT(orders[0].filled == 100, "Filled should be 100");
    TEST_ASSERT(orders[0].status == OrderStatus::FILLED, "Status should be FILLED");
    TEST_PASS();
}

bool test_parse_order_status_pending() {
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

    TEST_ASSERT(result == true, "Should parse pending order");
    TEST_ASSERT(orders.size() == 1, "Should have one order");
    TEST_ASSERT(orders[0].side == OrderSide::SELL, "Side should be SELL");
    TEST_ASSERT(orders[0].status == OrderStatus::PENDING, "Status should be PENDING");
    TEST_ASSERT(orders[0].filled == 0, "Filled should be 0");
    TEST_PASS();
}

bool test_parse_order_status_partial() {
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

    TEST_ASSERT(result == true, "Should parse partial order");
    TEST_ASSERT(orders.size() == 1, "Should have one order");
    TEST_ASSERT(orders[0].status == OrderStatus::PARTIAL, "Status should be PARTIAL");
    TEST_ASSERT(orders[0].quantity == 100, "Quantity should be 100");
    TEST_ASSERT(orders[0].filled == 50, "Filled should be 50");
    TEST_PASS();
}

bool test_parse_order_status_cancelled() {
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

    TEST_ASSERT(result == true, "Should parse cancelled order");
    TEST_ASSERT(orders.size() == 1, "Should have one order");
    TEST_ASSERT(orders[0].status == OrderStatus::CANCELLED, "Status should be CANCELLED");
    TEST_PASS();
}

bool test_client_initialization() {
    TradeZeroClient client;

    TEST_ASSERT(!client.is_configured(), "Client should not be configured initially");

    client.set_credentials("test_key_id", "test_secret_key", "test_account_id");

    TEST_ASSERT(client.is_configured(), "Client should be configured after set_credentials");
    TEST_PASS();
}

bool test_parse_malformed_json() {
    TradeZeroClient client;
    std::vector<Position> positions;

    std::string json = "{invalid json";
    bool result = client.parse_positions(json, positions);

    TEST_ASSERT(result == false, "Should fail on malformed JSON");
    TEST_ASSERT(positions.size() == 0, "Should have zero positions");
    TEST_PASS();
}

bool test_parse_position_with_short_shares() {
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

    TEST_ASSERT(result == true, "Should parse short position");
    TEST_ASSERT(positions.size() == 1, "Should have one position");
    TEST_ASSERT(positions[0].quantity == -100, "Quantity should be -100 for short");
    TEST_PASS();
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    int passed = 0;
    int failed = 0;

    std::printf("Running TradeZero Client Tests...\n\n");

    // JSON parsing tests
    if (test_parse_empty_positions()) passed++; else failed++;
    if (test_parse_single_position()) passed++; else failed++;
    if (test_parse_multiple_positions()) passed++; else failed++;
    if (test_parse_empty_orders()) passed++; else failed++;
    if (test_parse_single_order()) passed++; else failed++;
    if (test_parse_order_status_pending()) passed++; else failed++;
    if (test_parse_order_status_partial()) passed++; else failed++;
    if (test_parse_order_status_cancelled()) passed++; else failed++;
    if (test_parse_malformed_json()) passed++; else failed++;
    if (test_parse_position_with_short_shares()) passed++; else failed++;

    // Client initialization test
    if (test_client_initialization()) passed++; else failed++;

    std::printf("\n========================================\n");
    std::printf("Test Results: %d passed, %d failed\n", passed, failed);
    std::printf("========================================\n");

    return (failed == 0) ? 0 : 1;
}
