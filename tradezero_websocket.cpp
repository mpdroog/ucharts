// tradezero_websocket.cpp - TradeZero WebSocket client implementation
#include "tradezero_websocket.h"
#include "logger.h"

// Disable old-style-cast warnings for libwebsockets (uses OpenSSL headers with C-style casts)
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#endif
#include <libwebsockets.h>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <cstring>
#include <cstdio>

// Global instances
static TradeZeroWebSocket g_tradezero_pnl;
static TradeZeroWebSocket g_tradezero_portfolio;

TradeZeroWebSocket& get_tradezero_pnl() {
    return g_tradezero_pnl;
}

TradeZeroWebSocket& get_tradezero_portfolio() {
    return g_tradezero_portfolio;
}

// ============================================================================
// Data structure constructors
// ============================================================================

TZPositionPnL::TZPositionPnL()
    : unrealized_pnl(0), day_unrealized_pnl(0), pct_pnl_move(0),
      day_pct_pnl_move(0), exposure(0), realized_pnl(0), day_realized_pnl(0) {
    position_id[0] = '\0';
    symbol[0] = '\0';
}

TZPnLSnapshot::TZPnLSnapshot()
    : account_value(0), available_cash(0), buying_power(0),
      day_unrealized(0), day_realized(0), day_pnl(0),
      total_unrealized(0), day_trades_remaining(0) {}

TZAggUpdate::TZAggUpdate()
    : account_value(0), exposure(0), day_unrealized(0),
      day_pnl(0), total_unrealized(0), equity_ratio(0) {}

TZOrderUpdate::TZOrderUpdate()
    : order_quantity(0), executed(0), leaves_quantity(0),
      limit_price(0), price_avg(0), last_price(0), last_quantity(0) {
    account_id[0] = '\0';
    client_order_id[0] = '\0';
    symbol[0] = '\0';
    side[0] = '\0';
    order_status[0] = '\0';
    order_type[0] = '\0';
    start_time[0] = '\0';
    last_updated[0] = '\0';
}

TZPositionUpdate::TZPositionUpdate()
    : shares(0), price_avg(0), price_open(0), price_close(0) {
    id[0] = '\0';
    account_id[0] = '\0';
    symbol[0] = '\0';
    side[0] = '\0';
    day_overnight[0] = '\0';
    created_date[0] = '\0';
    updated_date[0] = '\0';
}

// ============================================================================
// TradeZeroWebSocket implementation
// ============================================================================

// Per-connection user data
struct ConnectionData {
    TradeZeroWebSocket* client;
    std::string message_buffer;
};

// libwebsockets callback
[[maybe_unused]] static int websocket_callback([[maybe_unused]] struct lws* wsi, enum lws_callback_reasons reason,
                              void* user, void* in, size_t len) {
    ConnectionData* conn_data = static_cast<ConnectionData*>(user);

    if (reason == LWS_CALLBACK_CLIENT_ESTABLISHED) {
        if (conn_data && conn_data->client) {
            LOG_I("tradezero_ws", "WebSocket connection established");
        }
    } else if (reason == LWS_CALLBACK_CLIENT_RECEIVE) {
        if (conn_data && conn_data->client && in && len > 0) {
            std::string message(static_cast<const char*>(in), len);
            conn_data->client->handle_message(message);
        }
    } else if (reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR) {
        if (conn_data && conn_data->client) {
            const char* error_msg = in ? static_cast<const char*>(in) : "unknown error";
            LOG_E("tradezero_ws", "Connection error: %s", error_msg);
        }
    } else if (reason == LWS_CALLBACK_CLIENT_CLOSED) {
        if (conn_data && conn_data->client) {
            LOG_W("tradezero_ws", "WebSocket connection closed");
        }
    }
    // Ignore all other callbacks

    return 0;
}

TradeZeroWebSocket::TradeZeroWebSocket()
    : m_stream(TZStream::PNL), m_running(false), m_connected(false),
      m_authenticated(false), m_lws_context(nullptr), m_lws_connection(nullptr) {
    m_api_key_id[0] = '\0';
    m_api_secret_key[0] = '\0';
    m_account_id[0] = '\0';
    m_error[0] = '\0';
}

TradeZeroWebSocket::~TradeZeroWebSocket() {
    disconnect();
}

void TradeZeroWebSocket::set_credentials(const char* api_key_id, const char* api_secret_key, const char* account_id) {
    std::strncpy(m_api_key_id, api_key_id, sizeof(m_api_key_id) - 1);
    m_api_key_id[sizeof(m_api_key_id) - 1] = '\0';

    std::strncpy(m_api_secret_key, api_secret_key, sizeof(m_api_secret_key) - 1);
    m_api_secret_key[sizeof(m_api_secret_key) - 1] = '\0';

    std::strncpy(m_account_id, account_id, sizeof(m_account_id) - 1);
    m_account_id[sizeof(m_account_id) - 1] = '\0';
}

bool TradeZeroWebSocket::connect(TZStream stream) {
    if (m_running.load()) {
        LOG_W("tradezero_ws", "Already connected");
        return true;
    }

    m_stream = stream;
    m_running.store(true);

    // Start worker thread
    try {
        m_thread = std::thread(&TradeZeroWebSocket::worker_thread, this);
    } catch (const std::exception& e) {
        std::snprintf(m_error, sizeof(m_error), "Failed to start thread: %s", e.what());
        LOG_E("tradezero_ws", "%s", m_error);
        m_running.store(false);
        return false;
    }

    // Wait a bit for connection (simple approach)
    for (int i = 0; i < 50 && !m_connected.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return m_connected.load();
}

void TradeZeroWebSocket::disconnect() {
    if (!m_running.load()) {
        return;
    }

    LOG_I("tradezero_ws", "Disconnecting...");
    m_running.store(false);

    if (m_thread.joinable()) {
        m_thread.join();
    }

    m_connected.store(false);
    m_authenticated.store(false);
}

bool TradeZeroWebSocket::is_connected() const {
    return m_connected.load() && m_authenticated.load();
}

void TradeZeroWebSocket::set_pnl_snapshot_callback(TZPnLSnapshotCallback callback) {
    MutexLock lock(m_mutex);
    m_pnl_snapshot_callback = callback;
}

void TradeZeroWebSocket::set_agg_update_callback(TZAggUpdateCallback callback) {
    MutexLock lock(m_mutex);
    m_agg_update_callback = callback;
}

void TradeZeroWebSocket::set_position_pnl_callback(TZPositionPnLCallback callback) {
    MutexLock lock(m_mutex);
    m_position_pnl_callback = callback;
}

void TradeZeroWebSocket::set_order_callback(TZOrderCallback callback) {
    MutexLock lock(m_mutex);
    m_order_callback = callback;
}

void TradeZeroWebSocket::set_position_callback(TZPositionCallback callback) {
    MutexLock lock(m_mutex);
    m_position_callback = callback;
}

void TradeZeroWebSocket::set_connection_callback(TZConnectionCallback callback) {
    MutexLock lock(m_mutex);
    m_connection_callback = callback;
}

const char* TradeZeroWebSocket::last_error() const {
    return m_error;
}

void TradeZeroWebSocket::worker_thread() {
    LOG_I("tradezero_ws", "Worker thread started");

    // Determine endpoint
    const char* path = (m_stream == TZStream::PNL) ? "/stream/pnl" : "/stream/portfolio";

    // libwebsockets setup
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = nullptr;  // Will be set dynamically
    info.gid = static_cast<gid_t>(-1);
    info.uid = static_cast<uid_t>(-1);
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    m_lws_context = lws_create_context(&info);
    if (m_lws_context == nullptr) {
        std::snprintf(m_error, sizeof(m_error), "Failed to create lws context");
        LOG_E("tradezero_ws", "%s", m_error);
        m_running.store(false);
        return;
    }

    // Connection info
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = m_lws_context;
    ccinfo.address = "webapi.tradezero.com";
    ccinfo.port = 443;
    ccinfo.path = path;
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.protocol = ""; // Empty for default
    ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;

    LOG_I("tradezero_ws", "Connecting to wss://%s%s...", ccinfo.address, path);

    // Attempt WebSocket connection
    struct lws* wsi = lws_client_connect_via_info(&ccinfo);
    if (wsi == nullptr) {
        std::snprintf(m_error, sizeof(m_error), "Failed to initiate WebSocket connection");
        LOG_E("tradezero_ws", "%s", m_error);
        m_running.store(false);
        lws_context_destroy(m_lws_context);
        m_lws_context = nullptr;
        return;
    }

    m_lws_connection = wsi;

    // Main event loop
    while (m_running.load()) {
        // Service libwebsockets (handles callbacks, I/O, etc.)
        if (m_lws_context) {
            int result = lws_service(m_lws_context, 50);  // 50ms timeout
            if (result < 0) {
                LOG_E("tradezero_ws", "lws_service error: %d", result);
                break;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Cleanup
    if (m_lws_context) {
        lws_context_destroy(m_lws_context);
        m_lws_context = nullptr;
    }

    LOG_I("tradezero_ws", "Worker thread stopped");
}

void TradeZeroWebSocket::handle_message(const std::string& message) {
    // Check if it's a system message
    if (message.find("\"@system\":true") != std::string::npos) {
        parse_system_message(message);
        return;
    }

    // Check message type based on stream
    if (m_stream == TZStream::PNL) {
        if (message.find("\"action\":\"init\"") != std::string::npos) {
            parse_pnl_snapshot(message);
        } else if (message.find("\"target\":\"aggCalcs\"") != std::string::npos) {
            parse_agg_update(message);
        } else if (message.find("\"target\":\"position\"") != std::string::npos) {
            parse_position_pnl(message);
        }
    } else if (m_stream == TZStream::PORTFOLIO) {
        if (message.find("\"subscription\":\"Order\"") != std::string::npos) {
            parse_order_update(message);
        } else if (message.find("\"subscription\":\"Position\"") != std::string::npos) {
            parse_position_update(message);
        }
    }
}

void TradeZeroWebSocket::parse_system_message(const std::string& json) {
    // Check for PENDING_AUTH
    if (json.find("\"status\":\"PENDING_AUTH\"") != std::string::npos) {
        LOG_I("tradezero_ws", "Auth required, sending credentials...");
        send_auth_message();
    }
    // Check for CONNECTED
    else if (json.find("\"status\":\"CONNECTED\"") != std::string::npos) {
        LOG_I("tradezero_ws", "Authenticated successfully");
        m_authenticated.store(true);
        send_subscribe_message();

        // Notify connection callback
        TZConnectionCallback cb;
        {
            MutexLock lock(m_mutex);
            cb = m_connection_callback;
        }
        if (cb) {
            cb(true);
        }
    }
    // Check for FAILED_AUTH
    else if (json.find("\"status\":\"FAILED_AUTH\"") != std::string::npos) {
        LOG_E("tradezero_ws", "Authentication failed");
        m_running.store(false);
    }
}

bool TradeZeroWebSocket::send_auth_message() {
    char auth_msg[512];
    std::snprintf(auth_msg, sizeof(auth_msg),
        "{\"key\":\"%s\",\"secret\":\"%s\"}",
        m_api_key_id, m_api_secret_key);

    // TODO: Use lws_write() to send message
    // This requires proper callback setup
    LOG_D("tradezero_ws", "Auth message: %s", auth_msg);

    return true;
}

bool TradeZeroWebSocket::send_subscribe_message() {
    char sub_msg[256];

    if (m_stream == TZStream::PNL) {
        std::snprintf(sub_msg, sizeof(sub_msg),
            "{\"account\":\"%s\"}", m_account_id);
    } else {
        std::snprintf(sub_msg, sizeof(sub_msg),
            "{\"accountId\":\"%s\",\"subscriptions\":[\"Order\",\"Position\"]}",
            m_account_id);
    }

    // TODO: Use lws_write() to send message
    LOG_D("tradezero_ws", "Subscribe message: %s", sub_msg);

    m_connected.store(true);
    return true;
}

// Simplified JSON parsing (similar to tradezero_client.cpp)
void TradeZeroWebSocket::parse_pnl_snapshot(const std::string& json) {
    TZPnLSnapshot snapshot;

    // Extract account values (simplified parsing)
    size_t pos;
    if ((pos = json.find("\"accountValue\":")) != std::string::npos) {
        snapshot.account_value = static_cast<float>(std::atof(json.c_str() + pos + 15));
    }
    if ((pos = json.find("\"availableCash\":")) != std::string::npos) {
        snapshot.available_cash = static_cast<float>(std::atof(json.c_str() + pos + 16));
    }
    if ((pos = json.find("\"dayPnl\":")) != std::string::npos) {
        snapshot.day_pnl = static_cast<float>(std::atof(json.c_str() + pos + 9));
    }

    // Calculate buying power (simplified)
    snapshot.buying_power = snapshot.account_value * 4.0f;  // Assume 4x leverage

    LOG_I("tradezero_ws", "P&L Snapshot: equity=%.2f, cash=%.2f, pnl=%.2f",
          static_cast<double>(snapshot.account_value), static_cast<double>(snapshot.available_cash), static_cast<double>(snapshot.day_pnl));

    // Invoke callback
    TZPnLSnapshotCallback cb;
    {
        MutexLock lock(m_mutex);
        cb = m_pnl_snapshot_callback;
    }
    if (cb) {
        cb(snapshot);
    }
}

void TradeZeroWebSocket::parse_agg_update(const std::string& json) {
    TZAggUpdate update;

    size_t pos;
    if ((pos = json.find("\"accountValue\":")) != std::string::npos) {
        update.account_value = static_cast<float>(std::atof(json.c_str() + pos + 15));
    }
    if ((pos = json.find("\"dayPnl\":")) != std::string::npos) {
        update.day_pnl = static_cast<float>(std::atof(json.c_str() + pos + 9));
    }

    // Invoke callback
    TZAggUpdateCallback cb;
    {
        MutexLock lock(m_mutex);
        cb = m_agg_update_callback;
    }
    if (cb) {
        cb(update);
    }
}

void TradeZeroWebSocket::parse_position_pnl(const std::string& json) {
    // Simplified implementation
    TZPositionPnL pos_pnl;

    // Extract symbol
    size_t pos = json.find("\"symbol\":");
    if (pos != std::string::npos) {
        size_t q1 = json.find('"', pos + 9);
        size_t q2 = json.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
            std::string symbol = json.substr(q1 + 1, q2 - q1 - 1);
            std::strncpy(pos_pnl.symbol, symbol.c_str(), sizeof(pos_pnl.symbol) - 1);
        }
    }

    // Invoke callback
    TZPositionPnLCallback cb;
    {
        MutexLock lock(m_mutex);
        cb = m_position_pnl_callback;
    }
    if (cb && pos_pnl.symbol[0] != '\0') {
        cb(pos_pnl);
    }
}

void TradeZeroWebSocket::parse_order_update(const std::string& json) {
    TZOrderUpdate order;

    // Extract order fields (simplified)
    size_t pos;
    if ((pos = json.find("\"symbol\":")) != std::string::npos) {
        size_t q1 = json.find('"', pos + 9);
        size_t q2 = json.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
            std::string symbol = json.substr(q1 + 1, q2 - q1 - 1);
            std::strncpy(order.symbol, symbol.c_str(), sizeof(order.symbol) - 1);
        }
    }

    if ((pos = json.find("\"orderStatus\":")) != std::string::npos) {
        size_t q1 = json.find('"', pos + 14);
        size_t q2 = json.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
            std::string status = json.substr(q1 + 1, q2 - q1 - 1);
            std::strncpy(order.order_status, status.c_str(), sizeof(order.order_status) - 1);
        }
    }

    LOG_I("tradezero_ws", "Order update: %s status=%s", order.symbol, order.order_status);

    // Invoke callback
    TZOrderCallback cb;
    {
        MutexLock lock(m_mutex);
        cb = m_order_callback;
    }
    if (cb && order.symbol[0] != '\0') {
        cb(order);
    }
}

void TradeZeroWebSocket::parse_position_update(const std::string& json) {
    TZPositionUpdate position;

    // Extract position fields (simplified)
    size_t pos;
    if ((pos = json.find("\"symbol\":")) != std::string::npos) {
        size_t q1 = json.find('"', pos + 9);
        size_t q2 = json.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
            std::string symbol = json.substr(q1 + 1, q2 - q1 - 1);
            std::strncpy(position.symbol, symbol.c_str(), sizeof(position.symbol) - 1);
        }
    }

    if ((pos = json.find("\"shares\":")) != std::string::npos) {
        position.shares = static_cast<float>(std::atof(json.c_str() + pos + 9));
    }

    if ((pos = json.find("\"priceAvg\":")) != std::string::npos) {
        position.price_avg = static_cast<float>(std::atof(json.c_str() + pos + 11));
    }

    LOG_I("tradezero_ws", "Position update: %s shares=%.0f", position.symbol, static_cast<double>(position.shares));

    // Invoke callback
    TZPositionCallback cb;
    {
        MutexLock lock(m_mutex);
        cb = m_position_callback;
    }
    if (cb && position.symbol[0] != '\0') {
        cb(position);
    }
}

bool TradeZeroWebSocket::reconnect() {
    LOG_I("tradezero_ws", "Attempting to reconnect...");
    // TODO: Implement reconnection logic
    return false;
}
