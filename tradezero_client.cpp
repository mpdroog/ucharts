// tradezero_client.cpp - TradeZero REST API client implementation
#include "tradezero_client.h"
#include "logger.h"
#include <curl/curl.h>
#include <cstring>
#include <cstdio>
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
            order.side = OrderSide::BUY;
            order.status = OrderStatus::PENDING;
            order.client_order_id[0] = '\0';
            order.filled = 0;

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

            if (order_json.contains("executed")) {
                if (order_json["executed"].is_number_integer()) {
                    order.filled = order_json["executed"].get<int>();
                } else if (order_json["executed"].is_number_float()) {
                    order.filled = static_cast<int>(order_json["executed"].get<float>());
                }
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

std::vector<ClosedPosition> TradeZeroClient::get_executions() {
    std::vector<ClosedPosition> executions;

    char endpoint[128];
    std::snprintf(endpoint, sizeof(endpoint), "/accounts/%s/executions", m_account_id);

    TZResponse resp = make_request("GET", endpoint, nullptr);
    if (!resp.success) {
        LOG_W("tradezero", "Failed to fetch executions: HTTP %d", resp.status_code);
        return executions;
    }

    try {
        auto j = json::parse(resp.body);

        // API returns {"executions":[...]} wrapper
        if (j.is_object() && j.contains("executions") && j["executions"].is_array()) {
            j = j["executions"];
        } else if (!j.is_array()) {
            LOG_E("tradezero", "Executions response not array: %.200s", resp.body.c_str());
            return executions;
        }

        for (const auto& exec_json : j) {
            ClosedPosition closed;
            closed.symbol[0] = '\0';
            closed.quantity = 0;
            closed.entry_price = 0.0f;
            closed.exit_price = 0.0f;
            closed.entry_time = 0;
            closed.exit_time = 0;

            if (exec_json.contains("symbol") && exec_json["symbol"].is_string()) {
                std::string symbol = exec_json["symbol"].get<std::string>();
                std::strncpy(closed.symbol, symbol.c_str(), sizeof(closed.symbol) - 1);
                closed.symbol[sizeof(closed.symbol) - 1] = '\0';
            }

            if (exec_json.contains("quantity")) {
                if (exec_json["quantity"].is_number_integer()) {
                    closed.quantity = exec_json["quantity"].get<int>();
                } else if (exec_json["quantity"].is_number_float()) {
                    closed.quantity = static_cast<int>(exec_json["quantity"].get<float>());
                }
            }

            // Entry price (average fill price for the opening trade)
            if (exec_json.contains("entryPrice")) {
                closed.entry_price = exec_json["entryPrice"].get<float>();
            } else if (exec_json.contains("priceAvg")) {
                closed.entry_price = exec_json["priceAvg"].get<float>();
            }

            // Exit price (average fill price for the closing trade)
            if (exec_json.contains("exitPrice")) {
                closed.exit_price = exec_json["exitPrice"].get<float>();
            } else if (exec_json.contains("price")) {
                closed.exit_price = exec_json["price"].get<float>();
            }

            // Parse timestamps if available
            if (exec_json.contains("entryTime") && exec_json["entryTime"].is_number()) {
                closed.entry_time = exec_json["entryTime"].get<int64_t>();
            }
            if (exec_json.contains("exitTime") && exec_json["exitTime"].is_number()) {
                closed.exit_time = exec_json["exitTime"].get<int64_t>();
            } else if (exec_json.contains("timestamp") && exec_json["timestamp"].is_number()) {
                closed.exit_time = exec_json["timestamp"].get<int64_t>();
            }

            if (closed.symbol[0] != '\0' && closed.quantity > 0) {
                executions.push_back(closed);
            }
        }

        LOG_I("tradezero", "Fetched %zu executions", executions.size());

    } catch (const json::exception& e) {
        LOG_E("tradezero", "Executions JSON parse failed: %s (body: %.200s)", e.what(), resp.body.c_str());
    }

    return executions;
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

const char* TradeZeroClient::last_error() const {
    return m_error;
}
