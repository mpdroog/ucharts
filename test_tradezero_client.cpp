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

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    RUN_TEST(client_initialization);
    RUN_TEST(client_base_url_override);

    test_summary();
    return 0;
}
