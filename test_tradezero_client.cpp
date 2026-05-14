// test_tradezero_client.cpp - Tests for TradeZero REST API client
// Compile: See Makefile test target
//
// Note: JSON parsing is tested via integration tests with the mock server.
// See contrib/fake_tradezero.go and test_integration.cpp for full tests.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include "test_common.h"
#include "tradezero_client.h"
#include "types.h"

// ============================================================================
// Tests for Client Configuration
// ============================================================================

TEST(client_initialization) {
    TradeZeroClient client;

    ASSERT_TRUE(!client.is_configured());

    client.set_credentials("test_key_id", "test_secret_key", "test_account_id");

    ASSERT_TRUE(client.is_configured());
}

TEST(client_base_url_override) {
    TradeZeroClient client;
    client.set_credentials("key", "secret", "account");
    client.set_base_url("http://localhost:8080/v1/api");

    ASSERT_TRUE(client.is_configured());
}

TEST(client_get_executions_returns_vector) {
    // This test verifies the get_executions() method exists and returns a vector
    // Full integration test with mock server is in test_integration.cpp
    TradeZeroClient client;
    client.set_credentials("test_key", "test_secret", "test_account");
    client.set_base_url("http://localhost:8080/v1/api");

    // Call get_executions - should return empty vector if mock server not running
    // (won't crash, just returns empty on connection failure)
    std::vector<ClosedPosition> executions = client.get_executions();
    // No assertion needed - just verifying it compiles and doesn't crash
    (void)executions;
}

TEST(client_api_keys_only) {
    TradeZeroClient client;

    ASSERT_TRUE(!client.has_api_keys());
    ASSERT_TRUE(!client.is_configured());

    client.set_api_keys("test_key_id", "test_secret_key");

    ASSERT_TRUE(client.has_api_keys());
    ASSERT_TRUE(!client.is_configured());  // Still not configured (no account)
}

TEST(client_set_account_after_keys) {
    TradeZeroClient client;

    client.set_api_keys("test_key_id", "test_secret_key");
    ASSERT_TRUE(!client.is_configured());

    client.set_account_id("test_account");
    ASSERT_TRUE(client.is_configured());
}

TEST(client_get_accounts_returns_vector) {
    // This test verifies the get_accounts() method exists and returns a vector
    // Full integration test with mock server is in test_integration.cpp
    TradeZeroClient client;
    client.set_api_keys("test_key", "test_secret");
    client.set_base_url("http://localhost:8080/v1/api");

    // Call get_accounts - should return accounts from mock server
    std::vector<TZAccount> accounts = client.get_accounts();
    // No assertion here - mock server may or may not be running
    (void)accounts;
}

TEST(tzaccount_struct_initialization) {
    TZAccount account;
    ASSERT_TRUE(account.account_id[0] == '\0');
    ASSERT_TRUE(account.account_type[0] == '\0');
    ASSERT_TRUE(account.status[0] == '\0');
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    RUN_TEST(client_initialization);
    RUN_TEST(client_base_url_override);
    RUN_TEST(client_get_executions_returns_vector);
    RUN_TEST(client_api_keys_only);
    RUN_TEST(client_set_account_after_keys);
    RUN_TEST(client_get_accounts_returns_vector);
    RUN_TEST(tzaccount_struct_initialization);

    test_summary();
    return 0;
}
