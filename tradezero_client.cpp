// tradezero_client.cpp - TradeZero REST API client implementation
#include "tradezero_client.h"
#include "logger.h"
#include <curl/curl.h>
#include <cstring>
#include <cstdio>

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

    // Initialize curl globally
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
    }
}

TradeZeroClient::~TradeZeroClient() {
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

bool TradeZeroClient::is_configured() const {
    return m_api_key_id[0] != '\0' && m_api_secret_key[0] != '\0' && m_account_id[0] != '\0';
}

std::string TradeZeroClient::build_url(const char* endpoint) const {
    // Base URL: https://webapi.tradezero.com/v1/api/
    std::string url = "https://webapi.tradezero.com/v1/api";
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

    if (!is_configured()) {
        std::snprintf(m_error, sizeof(m_error), "TradeZero client not configured");
        response.error = m_error;
        LOG_E("tradezero", "%s", m_error);
        return response;
    }

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
        std::snprintf(m_error, sizeof(m_error), "HTTP %d", static_cast<int>(http_code));
        response.error = m_error;
        LOG_E("tradezero", "HTTP %d: %s (URL: %s)", static_cast<int>(http_code),
              response_body.c_str(), url.c_str());
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

TZResponse TradeZeroClient::get_positions() {
    char endpoint[128];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/positions", m_account_id);
    return make_request("GET", endpoint, nullptr);
}

TZResponse TradeZeroClient::get_orders() {
    char endpoint[128];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/orders", m_account_id);
    return make_request("GET", endpoint, nullptr);
}

TZResponse TradeZeroClient::place_order(const char* symbol, int quantity, const char* side,
                                        const char* order_type, float limit_price, float stop_price) {
    if (symbol == nullptr || side == nullptr || order_type == nullptr) {
        TZResponse response;
        std::snprintf(m_error, sizeof(m_error), "Invalid order parameters");
        response.error = m_error;
        return response;
    }

    // Build JSON body
    char body[512];
    int len = std::snprintf(body, sizeof(body),
        "{\"symbol\":\"%s\",\"quantity\":%d,\"side\":\"%s\",\"orderType\":\"%s\"",
        symbol, quantity, side, order_type);

    if (limit_price > 0.0f) {
        len += std::snprintf(body + len, sizeof(body) - static_cast<size_t>(len), ",\"limitPrice\":%.2f", static_cast<double>(limit_price));
    }

    if (stop_price > 0.0f) {
        len += std::snprintf(body + len, sizeof(body) - static_cast<size_t>(len), ",\"stopPrice\":%.2f", static_cast<double>(stop_price));
    }

    std::snprintf(body + len, sizeof(body) - static_cast<size_t>(len), ",\"timeInForce\":\"day\"}");

    char endpoint[128];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/order", m_account_id);

    LOG_I("tradezero", "Placing order: %s %d %s @ %.2f (%s)",
          side, quantity, symbol, static_cast<double>(limit_price), order_type);

    return make_request("POST", endpoint, body);
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

// Simple JSON parser for positions
bool TradeZeroClient::parse_positions(const std::string& json, std::vector<Position>& positions) {
    positions.clear();

    // Expecting JSON array: [{...}, {...}, ...]
    if (json.empty() || json[0] != '[') {
        LOG_E("tradezero", "Invalid positions JSON (expected array)");
        return false;
    }

    // Simple state machine parser
    size_t pos = 1;  // Skip opening [
    while (pos < json.length()) {
        // Skip whitespace
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\t')) {
            pos++;
        }

        if (pos >= json.length() || json[pos] == ']') break;

        // Expecting object
        if (json[pos] != '{') {
            LOG_E("tradezero", "Expected '{' in positions array at pos %zu", pos);
            return false;
        }

        // Find end of object
        size_t start = pos;
        int brace_count = 0;
        while (pos < json.length()) {
            if (json[pos] == '{') brace_count++;
            else if (json[pos] == '}') {
                brace_count--;
                if (brace_count == 0) {
                    pos++;  // Include closing }
                    break;
                }
            }
            pos++;
        }

        std::string obj_json = json.substr(start, pos - start);

        // Parse position object (simplified - just extract key fields)
        Position position;
        position.symbol[0] = '\0';
        position.quantity = 0;
        position.avg_price = 0.0f;
        position.current_price = 0.0f;

        // Extract symbol
        size_t symbol_pos = obj_json.find("\"symbol\":");
        if (symbol_pos != std::string::npos) {
            size_t quote1 = obj_json.find('"', symbol_pos + 9);
            size_t quote2 = obj_json.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                std::string symbol = obj_json.substr(quote1 + 1, quote2 - quote1 - 1);
                std::strncpy(position.symbol, symbol.c_str(), sizeof(position.symbol) - 1);
                position.symbol[sizeof(position.symbol) - 1] = '\0';
            }
        }

        // Extract shares
        size_t shares_pos = obj_json.find("\"shares\":");
        if (shares_pos != std::string::npos) {
            position.quantity = std::atoi(obj_json.c_str() + shares_pos + 9);
        }

        // Extract priceAvg
        size_t avg_pos = obj_json.find("\"priceAvg\":");
        if (avg_pos != std::string::npos) {
            position.avg_price = static_cast<float>(std::atof(obj_json.c_str() + avg_pos + 11));
        }

        // Extract priceClose (current price)
        size_t close_pos = obj_json.find("\"priceClose\":");
        if (close_pos != std::string::npos) {
            position.current_price = static_cast<float>(std::atof(obj_json.c_str() + close_pos + 13));
        }

        if (position.symbol[0] != '\0') {
            positions.push_back(position);
        }

        // Skip comma
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n' || json[pos] == '\t')) {
            pos++;
        }
    }

    LOG_I("tradezero", "Parsed %zu positions", positions.size());
    return true;
}

// Simple JSON parser for orders
bool TradeZeroClient::parse_orders(const std::string& json, std::vector<Order>& orders) {
    orders.clear();

    // Expecting JSON array: [{...}, {...}, ...]
    if (json.empty() || json[0] != '[') {
        LOG_E("tradezero", "Invalid orders JSON (expected array)");
        return false;
    }

    // Simple state machine parser
    size_t pos = 1;  // Skip opening [
    while (pos < json.length()) {
        // Skip whitespace
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '\n' || json[pos] == '\t')) {
            pos++;
        }

        if (pos >= json.length() || json[pos] == ']') break;

        // Expecting object
        if (json[pos] != '{') {
            LOG_E("tradezero", "Expected '{' in orders array at pos %zu", pos);
            return false;
        }

        // Find end of object
        size_t start = pos;
        int brace_count = 0;
        while (pos < json.length()) {
            if (json[pos] == '{') brace_count++;
            else if (json[pos] == '}') {
                brace_count--;
                if (brace_count == 0) {
                    pos++;  // Include closing }
                    break;
                }
            }
            pos++;
        }

        std::string obj_json = json.substr(start, pos - start);

        // Parse order object (simplified)
        Order order;
        order.id = 0;
        order.symbol[0] = '\0';
        order.quantity = 0;
        order.price = 0.0f;
        order.side = OrderSide::BUY;
        order.status = OrderStatus::PENDING;
        order.client_order_id[0] = '\0';

        // Extract symbol
        size_t symbol_pos = obj_json.find("\"symbol\":");
        if (symbol_pos != std::string::npos) {
            size_t quote1 = obj_json.find('"', symbol_pos + 9);
            size_t quote2 = obj_json.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                std::string symbol = obj_json.substr(quote1 + 1, quote2 - quote1 - 1);
                std::strncpy(order.symbol, symbol.c_str(), sizeof(order.symbol) - 1);
                order.symbol[sizeof(order.symbol) - 1] = '\0';
            }
        }

        // Extract clientOrderId
        size_t cid_pos = obj_json.find("\"clientOrderId\":");
        if (cid_pos != std::string::npos) {
            size_t quote1 = obj_json.find('"', cid_pos + 16);
            size_t quote2 = obj_json.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                std::string cid = obj_json.substr(quote1 + 1, quote2 - quote1 - 1);
                std::strncpy(order.client_order_id, cid.c_str(), sizeof(order.client_order_id) - 1);
                order.client_order_id[sizeof(order.client_order_id) - 1] = '\0';
            }
        }

        // Extract orderQuantity
        size_t qty_pos = obj_json.find("\"orderQuantity\":");
        if (qty_pos != std::string::npos) {
            order.quantity = std::atoi(obj_json.c_str() + qty_pos + 16);
        }

        // Extract limitPrice
        size_t price_pos = obj_json.find("\"limitPrice\":");
        if (price_pos != std::string::npos) {
            order.price = static_cast<float>(std::atof(obj_json.c_str() + price_pos + 13));
        }

        // Extract executed (filled quantity)
        size_t exec_pos = obj_json.find("\"executed\":");
        if (exec_pos != std::string::npos) {
            order.filled = std::atoi(obj_json.c_str() + exec_pos + 11);
        }

        // Extract side
        size_t side_pos = obj_json.find("\"side\":");
        if (side_pos != std::string::npos) {
            size_t quote1 = obj_json.find('"', side_pos + 7);
            size_t quote2 = obj_json.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                std::string side = obj_json.substr(quote1 + 1, quote2 - quote1 - 1);
                if (side == "buy" || side == "Buy") {
                    order.side = OrderSide::BUY;
                } else if (side == "sell" || side == "Sell") {
                    order.side = OrderSide::SELL;
                }
            }
        }

        // Extract orderStatus
        size_t status_pos = obj_json.find("\"orderStatus\":");
        if (status_pos != std::string::npos) {
            size_t quote1 = obj_json.find('"', status_pos + 14);
            size_t quote2 = obj_json.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                std::string status = obj_json.substr(quote1 + 1, quote2 - quote1 - 1);
                if (status == "new" || status == "Accepted") {
                    order.status = OrderStatus::PENDING;
                } else if (status == "filled" || status == "Filled") {
                    order.status = OrderStatus::FILLED;
                } else if (status == "canceled" || status == "Canceled" || status == "Cancelled") {
                    order.status = OrderStatus::CANCELLED;
                } else if (status == "PartiallyFilled" || status == "Partially Filled") {
                    order.status = OrderStatus::PARTIAL;
                }
            }
        }

        if (order.symbol[0] != '\0') {
            orders.push_back(order);
        }

        // Skip comma
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == ',' || json[pos] == '\n' || json[pos] == '\t')) {
            pos++;
        }
    }

    LOG_I("tradezero", "Parsed %zu orders", orders.size());
    return true;
}

const char* TradeZeroClient::last_error() const {
    return m_error;
}
