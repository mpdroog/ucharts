// tradezero_client.h - REST API client for TradeZero
#ifndef TRADEZERO_CLIENT_H
#define TRADEZERO_CLIENT_H

#include "types.h"
#include <string>
#include <vector>

// TradeZero API response structure
struct TZResponse {
    int status_code;
    std::string body;
    std::string error;
    bool success;

    TZResponse() : status_code(0), success(false) {}
};

// TradeZero account info (from GET /accounts)
struct TZAccount {
    char account_id[32];
    char account_type[16];  // "Live", "Demo", etc.
    char status[16];        // "Active", "Restricted"

    TZAccount() {
        account_id[0] = '\0';
        account_type[0] = '\0';
        status[0] = '\0';
    }
};

// Order types supported by a route
struct TZRouteOrderTypes {
    bool market;
    bool limit;
    bool stop;
    bool stop_limit;
    bool range_order;
    bool market_on_open;
    bool limit_on_open;
    bool trail_stop;
    bool market_on_close;
    bool limit_on_close;
    bool pegged;

    TZRouteOrderTypes() : market(false), limit(false), stop(false), stop_limit(false),
        range_order(false), market_on_open(false), limit_on_open(false),
        trail_stop(false), market_on_close(false), limit_on_close(false), pegged(false) {}
};

// TradeZero trading route (from GET /accounts/:accountId/routes)
struct TZRoute {
    char route_name[32];
    TZRouteOrderTypes order_types;
    bool use_display_qty;

    TZRoute() {
        route_name[0] = '\0';
        use_display_qty = false;
    }
};

// TradeZero REST API client
// Purpose: Order placement/cancellation and initial data sync
// Account data comes from P&L WebSocket stream
class TradeZeroClient {
public:
    TradeZeroClient();
    ~TradeZeroClient();

    // Configuration
    void set_credentials(const char* api_key_id, const char* api_secret_key, const char* account_id);
    void set_api_keys(const char* api_key_id, const char* api_secret_key);  // Set only API keys (before account selection)
    void set_account_id(const char* account_id);  // Set account after selection
    void set_base_url(const char* url);  // Override API base URL (for testing with mock server)
    bool is_configured() const;
    bool has_api_keys() const;  // Check if API keys are set (but maybe no account yet)

    // Account discovery
    std::vector<TZAccount> get_accounts();  // Get all accounts for API credentials

    // Trading routes
    std::vector<TZRoute> get_routes();  // Get available trading routes for account

    // Initial data sync (before WebSocket subscription)
    // Returns parsed data directly; empty vector on failure (errors logged internally)
    std::vector<Position> get_positions();
    std::vector<Order> get_orders();
    std::vector<ClosedPosition> get_executions();  // Today's executed trades

    // Order operations (no WebSocket equivalent)
    TZResponse place_order(const char* symbol, int quantity, const char* side,
                          const char* order_type, float limit_price, float stop_price,
                          const char* route = nullptr);
    TZResponse cancel_order(const char* client_order_id);
    TZResponse cancel_all_orders();

    // Get last error
    const char* last_error() const;

private:
    char m_api_key_id[128];
    char m_api_secret_key[128];
    char m_account_id[32];
    char m_error[256];
    char m_base_url[256];  // Custom base URL (empty = production)
    int m_timeout;  // Request timeout in seconds

    // Build URL for API endpoint
    std::string build_url(const char* endpoint) const;

    // Make HTTP request with authentication headers
    TZResponse make_request(const char* method, const char* endpoint, const char* body = nullptr);
    TZResponse make_request_internal(const char* method, const char* endpoint, const char* body = nullptr);
};

// Global TradeZero client instance
TradeZeroClient& get_tradezero_client();

#endif // TRADEZERO_CLIENT_H
