// tradezero_client.cpp - TradeZero REST API client implementation
#include "tradezero_client.h"
#include "logger.h"
#include <curl/curl.h>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Global instance
static TradeZeroClient g_tradezero_client;

TradeZeroClient& get_tradezero_client() {
    return g_tradezero_client;
}

// Callback for curl to write received data
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* response = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    response->append(ptr, total);
    return total;
}

TradeZeroClient::TradeZeroClient() : m_timeout(30) {
    m_api_key_id[0] = '\0';
    m_api_secret_key[0] = '\0';
    m_account_id[0] = '\0';
    m_error[0] = '\0';
    m_base_url[0] = '\0';

    // Initialize curl globally
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
    }
}

TradeZeroClient::~TradeZeroClient() {
    // Securely zero credentials to prevent memory disclosure
    volatile char* p;
    p = m_api_key_id;
    for (size_t i = 0; i < sizeof(m_api_key_id); ++i) p[i] = 0;
    p = m_api_secret_key;
    for (size_t i = 0; i < sizeof(m_api_secret_key); ++i) p[i] = 0;
    p = m_account_id;
    for (size_t i = 0; i < sizeof(m_account_id); ++i) p[i] = 0;

    // curl_global_cleanup() left for static destruction
}

void TradeZeroClient::set_credentials(const char* api_key_id, const char* api_secret_key, const char* account_id) {
    std::strncpy(m_api_key_id, api_key_id, sizeof(m_api_key_id) - 1);
    m_api_key_id[sizeof(m_api_key_id) - 1] = '\0';

    std::strncpy(m_api_secret_key, api_secret_key, sizeof(m_api_secret_key) - 1);
    m_api_secret_key[sizeof(m_api_secret_key) - 1] = '\0';

    std::strncpy(m_account_id, account_id, sizeof(m_account_id) - 1);
    m_account_id[sizeof(m_account_id) - 1] = '\0';
}

void TradeZeroClient::set_api_keys(const char* api_key_id, const char* api_secret_key) {
    std::strncpy(m_api_key_id, api_key_id, sizeof(m_api_key_id) - 1);
    m_api_key_id[sizeof(m_api_key_id) - 1] = '\0';

    std::strncpy(m_api_secret_key, api_secret_key, sizeof(m_api_secret_key) - 1);
    m_api_secret_key[sizeof(m_api_secret_key) - 1] = '\0';

    m_account_id[0] = '\0';  // Clear account until selected
}

void TradeZeroClient::set_account_id(const char* account_id) {
    std::strncpy(m_account_id, account_id, sizeof(m_account_id) - 1);
    m_account_id[sizeof(m_account_id) - 1] = '\0';
}

void TradeZeroClient::set_base_url(const char* url) {
    if (url != nullptr) {
        std::strncpy(m_base_url, url, sizeof(m_base_url) - 1);
        m_base_url[sizeof(m_base_url) - 1] = '\0';
    } else {
        m_base_url[0] = '\0';
    }
}

bool TradeZeroClient::is_configured() const {
    return m_api_key_id[0] != '\0' && m_api_secret_key[0] != '\0' && m_account_id[0] != '\0';
}

bool TradeZeroClient::has_api_keys() const {
    return m_api_key_id[0] != '\0' && m_api_secret_key[0] != '\0';
}

std::string TradeZeroClient::build_url(const char* endpoint) const {
    // Use custom base URL if set (for testing), otherwise production URL
    std::string url;
    if (m_base_url[0] != '\0') {
        url = m_base_url;
    } else {
        url = "https://webapi.tradezero.com/v1/api";
    }
    if (endpoint != nullptr && endpoint[0] != '\0') {
        if (endpoint[0] != '/') {
            url += '/';
        }
        url += endpoint;
    }
    return url;
}

TZResponse TradeZeroClient::make_request(const char* method, const char* endpoint, const char* body) {
    TZResponse response;

    // Most requests need full configuration (API keys + account)
    // get_accounts() uses make_request_with_keys_only() which only needs API keys
    if (!is_configured()) {
        std::snprintf(m_error, sizeof(m_error), "TradeZero client not configured");
        response.error = m_error;
        LOG_E("tradezero", "%s", m_error);
        return response;
    }

    return make_request_internal(method, endpoint, body);
}

TZResponse TradeZeroClient::make_request_internal(const char* method, const char* endpoint, const char* body) {
    TZResponse response;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        std::snprintf(m_error, sizeof(m_error), "Failed to initialize curl");
        response.error = m_error;
        LOG_E("tradezero", "%s", m_error);
        return response;
    }

    std::string url = build_url(endpoint);
    std::string response_body;

    // Set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Set authentication headers
    struct curl_slist* headers = nullptr;

    char key_header[256];
    std::snprintf(key_header, sizeof(key_header), "TZ-API-KEY-ID: %s", m_api_key_id);
    headers = curl_slist_append(headers, key_header);

    char secret_header[256];
    std::snprintf(secret_header, sizeof(secret_header), "TZ-API-SECRET-KEY: %s", m_api_secret_key);
    headers = curl_slist_append(headers, secret_header);

    headers = curl_slist_append(headers, "Accept: application/json");

    if (body != nullptr) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Auto-decompress gzip/deflate responses
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    // Set HTTP method
    if (std::strcmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (body != nullptr) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        }
    } else if (std::strcmp(method, "DELETE") == 0) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    }
    // Default is GET

    // Set response callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    // Set timeouts
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(m_timeout));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

    // Follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Enable verbose logging for debugging (can be disabled later)
    // curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Request failed: %s", curl_easy_strerror(res));
        response.error = m_error;
        LOG_E("tradezero", "%s (URL: %s)", m_error, url.c_str());
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return response;
    }

    // Get HTTP status code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    response.status_code = static_cast<int>(http_code);
    response.body = response_body;
    response.success = (http_code >= 200 && http_code < 300);

    if (!response.success) {
        // Try to parse error message from JSON response body
        bool parsed_error = false;
        if (!response_body.empty()) {
            try {
                auto err_json = json::parse(response_body);
                // Try common error message fields
                if (err_json.contains("message") && err_json["message"].is_string()) {
                    safe_strcpy(m_error, err_json["message"].get<std::string>().c_str(), sizeof(m_error));
                    parsed_error = true;
                } else if (err_json.contains("error") && err_json["error"].is_string()) {
                    safe_strcpy(m_error, err_json["error"].get<std::string>().c_str(), sizeof(m_error));
                    parsed_error = true;
                } else if (err_json.contains("errorMessage") && err_json["errorMessage"].is_string()) {
                    safe_strcpy(m_error, err_json["errorMessage"].get<std::string>().c_str(), sizeof(m_error));
                    parsed_error = true;
                }
            } catch (const json::exception&) {
                // Not valid JSON, use raw body if short enough
                if (response_body.size() < sizeof(m_error) - 1) {
                    safe_strcpy(m_error, response_body.c_str(), sizeof(m_error));
                    parsed_error = true;
                }
            }
        }
        if (!parsed_error) {
            std::snprintf(m_error, sizeof(m_error), "HTTP %d", static_cast<int>(http_code));
        }
        response.error = m_error;
        LOG_E("tradezero", "HTTP %d: %s (URL: %s)", static_cast<int>(http_code),
              response_body.c_str(), url.c_str());
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

std::vector<TZAccount> TradeZeroClient::get_accounts() {
    std::vector<TZAccount> accounts;

    // Only need API keys for this endpoint (no account_id required)
    if (!has_api_keys()) {
        LOG_E("tradezero", "API keys not configured");
        return accounts;
    }

    TZResponse resp = make_request_internal("GET", "/accounts", nullptr);
    if (!resp.success) {
        LOG_W("tradezero", "Failed to fetch accounts: HTTP %d", resp.status_code);
        return accounts;
    }

    try {
        auto j = json::parse(resp.body);

        // Production API returns: {"accounts": [{"account": ..., "accountStatus": ...}, ...]}
        if (!j.is_object() || !j.contains("accounts") || !j["accounts"].is_array()) {
            LOG_E("tradezero", "Accounts response missing 'accounts' array: %.200s", resp.body.c_str());
            return accounts;
        }

        for (const auto& acc_json : j["accounts"]) {
            TZAccount account;

            // Log raw JSON keys for debugging
            LOG_D("tradezero", "Account JSON keys:");
            for (auto& [key, val] : acc_json.items()) {
                LOG_D("tradezero", "  key: %s", key.c_str());
            }

            if (acc_json.contains("account") && acc_json["account"].is_string()) {
                std::string id = acc_json["account"].get<std::string>();
                std::strncpy(account.account_id, id.c_str(), sizeof(account.account_id) - 1);
                account.account_id[sizeof(account.account_id) - 1] = '\0';
            }

            if (acc_json.contains("accountType") && acc_json["accountType"].is_string()) {
                std::string type = acc_json["accountType"].get<std::string>();
                std::strncpy(account.account_type, type.c_str(), sizeof(account.account_type) - 1);
                account.account_type[sizeof(account.account_type) - 1] = '\0';
            }

            if (acc_json.contains("accountStatus") && acc_json["accountStatus"].is_string()) {
                std::string status = acc_json["accountStatus"].get<std::string>();
                std::strncpy(account.status, status.c_str(), sizeof(account.status) - 1);
                account.status[sizeof(account.status) - 1] = '\0';
            }

            LOG_I("tradezero", "Parsed account: id='%s' type='%s' status='%s'",
                  account.account_id, account.account_type, account.status);
            if (account.account_id[0] != '\0') {
                accounts.push_back(account);
            }
        }

        LOG_I("tradezero", "Fetched %zu accounts", accounts.size());

    } catch (const json::exception& e) {
        LOG_E("tradezero", "Accounts JSON parse failed: %s (body: %.200s)", e.what(), resp.body.c_str());
    }

    return accounts;
}

std::vector<TZRoute> TradeZeroClient::get_routes() {
    std::vector<TZRoute> routes;

    if (!is_configured()) {
        LOG_E("tradezero", "Client not configured for get_routes");
        return routes;
    }

    char endpoint[128];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/routes", m_account_id);

    TZResponse resp = make_request("GET", endpoint, nullptr);
    if (!resp.success) {
        LOG_W("tradezero", "Failed to fetch routes: HTTP %d", resp.status_code);
        return routes;
    }

    try {
        auto j = json::parse(resp.body);

        // API returns {"routes": [...]}
        if (!j.is_object() || !j.contains("routes") || !j["routes"].is_array()) {
            LOG_E("tradezero", "Routes response missing 'routes' array: %.200s", resp.body.c_str());
            return routes;
        }

        for (const auto& route_json : j["routes"]) {
            TZRoute route;

            if (route_json.contains("routeName") && route_json["routeName"].is_string()) {
                std::string name = route_json["routeName"].get<std::string>();
                std::strncpy(route.route_name, name.c_str(), sizeof(route.route_name) - 1);
                route.route_name[sizeof(route.route_name) - 1] = '\0';
            }

            if (route_json.contains("useDisplayQty") && route_json["useDisplayQty"].is_boolean()) {
                route.use_display_qty = route_json["useDisplayQty"].get<bool>();
            }

            // Parse orderTypes array
            if (route_json.contains("orderTypes") && route_json["orderTypes"].is_array()) {
                for (const auto& ot : route_json["orderTypes"]) {
                    if (!ot.is_string()) continue;
                    std::string order_type = ot.get<std::string>();
                    if (order_type == "Market") route.order_types.market = true;
                    else if (order_type == "Limit") route.order_types.limit = true;
                    else if (order_type == "Stop") route.order_types.stop = true;
                    else if (order_type == "StopLimit") route.order_types.stop_limit = true;
                    else if (order_type == "RangeOrder") route.order_types.range_order = true;
                    else if (order_type == "MarketOnOpen") route.order_types.market_on_open = true;
                    else if (order_type == "LimitOnOpen") route.order_types.limit_on_open = true;
                    else if (order_type == "TrailStop") route.order_types.trail_stop = true;
                    else if (order_type == "MarketOnClose") route.order_types.market_on_close = true;
                    else if (order_type == "LimitOnClose") route.order_types.limit_on_close = true;
                    else if (order_type == "Pegged") route.order_types.pegged = true;
                }
            }

            if (route.route_name[0] != '\0') {
                LOG_I("tradezero", "Route: %s (limit=%d, market=%d)",
                      route.route_name, route.order_types.limit, route.order_types.market);
                routes.push_back(route);
            }
        }

        LOG_I("tradezero", "Fetched %zu routes", routes.size());

    } catch (const json::exception& e) {
        LOG_E("tradezero", "Routes JSON parse failed: %s (body: %.200s)", e.what(), resp.body.c_str());
    }

    return routes;
}

std::vector<Position> TradeZeroClient::get_positions() {
    std::vector<Position> positions;

    char endpoint[128];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/positions", m_account_id);

    TZResponse resp = make_request("GET", endpoint, nullptr);
    if (!resp.success) {
        LOG_W("tradezero", "Failed to fetch positions: HTTP %d", resp.status_code);
        return positions;
    }

    try {
        auto j = json::parse(resp.body);

        // API returns {"positions":[...]} not raw array
        if (j.is_object() && j.contains("positions") && j["positions"].is_array()) {
            j = j["positions"];
        } else if (!j.is_array()) {
            LOG_E("tradezero", "Positions response not array: %.200s", resp.body.c_str());
            return positions;
        }

        for (const auto& pos_json : j) {
            Position position;
            position.symbol[0] = '\0';
            position.quantity = 0;
            position.avg_price = 0.0f;
            position.current_price = 0.0f;

            if (pos_json.contains("symbol") && pos_json["symbol"].is_string()) {
                std::string symbol = pos_json["symbol"].get<std::string>();
                std::strncpy(position.symbol, symbol.c_str(), sizeof(position.symbol) - 1);
                position.symbol[sizeof(position.symbol) - 1] = '\0';
            }

            if (pos_json.contains("shares")) {
                if (pos_json["shares"].is_number_integer()) {
                    position.quantity = pos_json["shares"].get<int>();
                } else if (pos_json["shares"].is_number_float()) {
                    position.quantity = static_cast<int>(pos_json["shares"].get<float>());
                }
            }

            if (pos_json.contains("priceAvg")) {
                position.avg_price = pos_json["priceAvg"].get<float>();
            }

            if (pos_json.contains("priceClose")) {
                position.current_price = pos_json["priceClose"].get<float>();
            }

            if (position.symbol[0] != '\0') {
                positions.push_back(position);
            }
        }

        LOG_I("tradezero", "Fetched %zu positions", positions.size());

    } catch (const json::exception& e) {
        LOG_E("tradezero", "Positions JSON parse failed: %s (body: %.200s)", e.what(), resp.body.c_str());
    }

    return positions;
}

std::vector<Order> TradeZeroClient::get_orders() {
    std::vector<Order> orders;

    char endpoint[128];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/orders", m_account_id);

    TZResponse resp = make_request("GET", endpoint, nullptr);
    if (!resp.success) {
        LOG_W("tradezero", "Failed to fetch orders: HTTP %d", resp.status_code);
        return orders;
    }

    try {
        auto j = json::parse(resp.body);

        // API returns {"orders":[...]} not raw array
        if (j.is_object() && j.contains("orders") && j["orders"].is_array()) {
            j = j["orders"];
        } else if (!j.is_array()) {
            LOG_E("tradezero", "Orders response not array: %.200s", resp.body.c_str());
            return orders;
        }

        for (const auto& order_json : j) {
            Order order;
            order.id = 0;
            order.symbol[0] = '\0';
            order.quantity = 0;
            order.price = 0.0f;
            order.avg_price = 0.0f;
            order.side = OrderSide::BUY;
            order.status = OrderStatus::PENDING;
            order.client_order_id[0] = '\0';
            order.executed = 0;
            order.canceled = 0;
            order.leaves = 0;

            if (order_json.contains("symbol") && order_json["symbol"].is_string()) {
                std::string symbol = order_json["symbol"].get<std::string>();
                std::strncpy(order.symbol, symbol.c_str(), sizeof(order.symbol) - 1);
                order.symbol[sizeof(order.symbol) - 1] = '\0';
            }

            if (order_json.contains("clientOrderId") && order_json["clientOrderId"].is_string()) {
                std::string cid = order_json["clientOrderId"].get<std::string>();
                std::strncpy(order.client_order_id, cid.c_str(), sizeof(order.client_order_id) - 1);
                order.client_order_id[sizeof(order.client_order_id) - 1] = '\0';
            }

            if (order_json.contains("orderQuantity")) {
                if (order_json["orderQuantity"].is_number_integer()) {
                    order.quantity = order_json["orderQuantity"].get<int>();
                } else if (order_json["orderQuantity"].is_number_float()) {
                    order.quantity = static_cast<int>(order_json["orderQuantity"].get<float>());
                }
            }

            if (order_json.contains("limitPrice")) {
                order.price = order_json["limitPrice"].get<float>();
            }

            if (order_json.contains("priceAvg")) {
                order.avg_price = order_json["priceAvg"].get<float>();
            }

            if (order_json.contains("executed")) {
                if (order_json["executed"].is_number_integer()) {
                    order.executed = order_json["executed"].get<int>();
                } else if (order_json["executed"].is_number_float()) {
                    order.executed = static_cast<int>(order_json["executed"].get<float>());
                }
            }

            if (order_json.contains("canceledQuantity")) {
                order.canceled = order_json["canceledQuantity"].get<int>();
            }

            if (order_json.contains("leavesQuantity")) {
                order.leaves = order_json["leavesQuantity"].get<int>();
            }

            if (order_json.contains("side") && order_json["side"].is_string()) {
                std::string side = order_json["side"].get<std::string>();
                if (side == "buy" || side == "Buy") {
                    order.side = OrderSide::BUY;
                } else if (side == "sell" || side == "Sell") {
                    order.side = OrderSide::SELL;
                }
            }

            if (order_json.contains("orderStatus") && order_json["orderStatus"].is_string()) {
                std::string status = order_json["orderStatus"].get<std::string>();
                std::transform(status.begin(), status.end(), status.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                LOG_D("tradezero", "Order %s status from API: %s", order.symbol, status.c_str());
                if (status == "new" || status == "accepted" || status == "pendingnew") {
                    order.status = OrderStatus::PENDING;
                } else if (status == "filled") {
                    order.status = OrderStatus::FILLED;
                } else if (status == "canceled" || status == "cancelled") {
                    order.status = OrderStatus::CANCELLED;
                } else if (status == "rejected") {
                    order.status = OrderStatus::REJECTED;
                } else if (status == "partiallyfilled" || status == "partially filled") {
                    order.status = OrderStatus::PARTIAL;
                }
            }

            if (order.symbol[0] != '\0') {
                orders.push_back(order);
            }
        }

        LOG_I("tradezero", "Fetched %zu orders", orders.size());

    } catch (const json::exception& e) {
        LOG_E("tradezero", "Orders JSON parse failed: %s (body: %.200s)", e.what(), resp.body.c_str());
    }

    return orders;
}

std::vector<Order> TradeZeroClient::get_order_history(const char* start_date) {
    std::vector<Order> orders;

    if (start_date == nullptr || start_date[0] == '\0') {
        LOG_E("tradezero", "get_order_history: start_date required");
        return orders;
    }

    char endpoint[128];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/orders/start-date/%s", m_account_id, start_date);

    TZResponse resp = make_request("GET", endpoint, nullptr);
    if (!resp.success) {
        LOG_W("tradezero", "Failed to fetch order history: HTTP %d", resp.status_code);
        return orders;
    }

    try {
        auto j = json::parse(resp.body);

        // API returns {"orders":[...]} wrapper
        if (j.is_object() && j.contains("orders") && j["orders"].is_array()) {
            j = j["orders"];
        } else if (!j.is_array()) {
            LOG_E("tradezero", "Order history response not array: %.200s", resp.body.c_str());
            return orders;
        }

        for (const auto& order_json : j) {
            Order order;
            order.id = 0;
            order.symbol[0] = '\0';
            order.quantity = 0;
            order.price = 0.0f;
            order.avg_price = 0.0f;
            order.side = OrderSide::BUY;
            order.status = OrderStatus::PENDING;
            order.client_order_id[0] = '\0';
            order.executed = 0;
            order.canceled = 0;
            order.leaves = 0;
            order.created_at = 0;

            if (order_json.contains("symbol") && order_json["symbol"].is_string()) {
                std::string symbol = order_json["symbol"].get<std::string>();
                std::strncpy(order.symbol, symbol.c_str(), sizeof(order.symbol) - 1);
                order.symbol[sizeof(order.symbol) - 1] = '\0';
            }

            if (order_json.contains("clientOrderId") && order_json["clientOrderId"].is_string()) {
                std::string cid = order_json["clientOrderId"].get<std::string>();
                std::strncpy(order.client_order_id, cid.c_str(), sizeof(order.client_order_id) - 1);
                order.client_order_id[sizeof(order.client_order_id) - 1] = '\0';
            }

            if (order_json.contains("orderQuantity")) {
                if (order_json["orderQuantity"].is_number_integer()) {
                    order.quantity = order_json["orderQuantity"].get<int>();
                } else if (order_json["orderQuantity"].is_number_float()) {
                    order.quantity = static_cast<int>(order_json["orderQuantity"].get<float>());
                }
            }

            if (order_json.contains("limitPrice")) {
                order.price = order_json["limitPrice"].get<float>();
            }

            if (order_json.contains("priceAvg")) {
                order.avg_price = order_json["priceAvg"].get<float>();
            }

            if (order_json.contains("executed")) {
                if (order_json["executed"].is_number_integer()) {
                    order.executed = order_json["executed"].get<int>();
                } else if (order_json["executed"].is_number_float()) {
                    order.executed = static_cast<int>(order_json["executed"].get<float>());
                }
            }

            if (order_json.contains("canceledQuantity")) {
                order.canceled = order_json["canceledQuantity"].get<int>();
            }

            if (order_json.contains("leavesQuantity")) {
                order.leaves = order_json["leavesQuantity"].get<int>();
            }

            if (order_json.contains("side") && order_json["side"].is_string()) {
                std::string side = order_json["side"].get<std::string>();
                if (side == "buy" || side == "Buy") {
                    order.side = OrderSide::BUY;
                } else if (side == "sell" || side == "Sell") {
                    order.side = OrderSide::SELL;
                }
            }

            if (order_json.contains("orderStatus") && order_json["orderStatus"].is_string()) {
                std::string status = order_json["orderStatus"].get<std::string>();
                std::transform(status.begin(), status.end(), status.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (status == "new" || status == "accepted" || status == "pendingnew") {
                    order.status = OrderStatus::PENDING;
                } else if (status == "filled") {
                    order.status = OrderStatus::FILLED;
                } else if (status == "canceled" || status == "cancelled") {
                    order.status = OrderStatus::CANCELLED;
                } else if (status == "rejected") {
                    order.status = OrderStatus::REJECTED;
                } else if (status == "partiallyfilled" || status == "partially filled") {
                    order.status = OrderStatus::PARTIAL;
                }
            }

            if (order.symbol[0] != '\0') {
                orders.push_back(order);
            }
        }

        LOG_I("tradezero", "Fetched %zu orders from history (start=%s)", orders.size(), start_date);

    } catch (const json::exception& e) {
        LOG_E("tradezero", "Order history JSON parse failed: %s (body: %.200s)", e.what(), resp.body.c_str());
    }

    return orders;
}

TZResponse TradeZeroClient::place_order(const char* symbol, int quantity, const char* side,
                                        const char* order_type, float limit_price, float stop_price,
                                        const char* route) {
    if (symbol == nullptr || side == nullptr || order_type == nullptr) {
        TZResponse response;
        std::snprintf(m_error, sizeof(m_error), "Invalid order parameters");
        response.error = m_error;
        return response;
    }

    // Uppercase symbol (TradeZero API requires uppercase)
    std::string upper_symbol(symbol);
    std::transform(upper_symbol.begin(), upper_symbol.end(), upper_symbol.begin(), ::toupper);

    // Capitalize side: "buy" -> "Buy", "sell" -> "Sell"
    std::string cap_side(side);
    if (!cap_side.empty()) {
        cap_side[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cap_side[0])));
    }

    // Capitalize orderType: "limit" -> "Limit", "market" -> "Market"
    std::string cap_order_type(order_type);
    if (!cap_order_type.empty()) {
        cap_order_type[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cap_order_type[0])));
    }

    // Generate unique clientOrderId
    char client_order_id[64];
    std::snprintf(client_order_id, sizeof(client_order_id), "DT-%lld",
                  static_cast<long long>(std::time(nullptr)));

    // Build JSON body per API reference
    json j;
    j["clientOrderId"] = client_order_id;
    j["symbol"] = upper_symbol;
    j["orderQuantity"] = quantity;
    j["side"] = cap_side;
    j["orderType"] = cap_order_type;
    j["securityType"] = "Stock";
    if (limit_price > 0.0f) {
        j["limitPrice"] = limit_price;
    }
    if (stop_price > 0.0f) {
        j["stopPrice"] = stop_price;
    }
    j["timeInForce"] = "Day";
    if (route != nullptr && route[0] != '\0') {
        j["route"] = route;
    }

    std::string body = j.dump();

    char endpoint[128];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/order", m_account_id);

    LOG_I("tradezero", "Placing order: %s %d %s @ %.2f (%s) route=%s",
          side, quantity, symbol, static_cast<double>(limit_price), order_type,
          (route && route[0]) ? route : "default");
    LOG_D("tradezero", "Order body: %s", body.c_str());

    return make_request("POST", endpoint, body.c_str());
}

TZResponse TradeZeroClient::cancel_order(const char* client_order_id) {
    if (client_order_id == nullptr) {
        TZResponse response;
        std::snprintf(m_error, sizeof(m_error), "Invalid client_order_id");
        response.error = m_error;
        return response;
    }

    char endpoint[256];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/orders/%s", m_account_id, client_order_id);

    LOG_I("tradezero", "Canceling order: %s", client_order_id);

    return make_request("DELETE", endpoint, nullptr);
}

TZResponse TradeZeroClient::cancel_all_orders() {
    LOG_I("tradezero", "Canceling all orders");
    return make_request("DELETE", "/accounts/orders", nullptr);
}

const char* TradeZeroClient::last_error() const {
    return m_error;
}
