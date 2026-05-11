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

#include <nlohmann/json.hpp>
#include <cstring>
#include <cstdio>
#include <new>  // For placement new

using json = nlohmann::json;

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

// Forward declaration
static int websocket_callback(struct lws* wsi, enum lws_callback_reasons reason,
                              void* user, void* in, size_t len);

// Simple approach: track active client (only one at a time for now)
static TradeZeroWebSocket* g_active_ws_client = nullptr;

// Simplified approach: don't use libwebsockets per-session data
// Just use the global g_active_ws_client pointer directly
// This avoids memory corruption issues with placement new

// libwebsockets protocols array - minimal per-session data
static struct lws_protocols protocols[] = {
    {
        "tradezero-protocol",       // protocol name
        websocket_callback,          // callback
        0,                           // NO per-session data
        4096,                        // rx buffer size
        0,                           // id (unused)
        nullptr,                     // user pointer
        0                            // tx packet size
    },
    { nullptr, nullptr, 0, 0, 0, nullptr, 0 }  // terminator
};

// libwebsockets callback - uses g_active_ws_client directly
static int websocket_callback(struct lws* wsi, enum lws_callback_reasons reason,
                              void* user, void* in, size_t len) {
    (void)user;  // Unused - we use g_active_ws_client instead

    TradeZeroWebSocket* client = g_active_ws_client;

    if (reason == LWS_CALLBACK_CLIENT_ESTABLISHED) {
        LOG_I("tradezero_ws", "WebSocket connection established");
        if (client) {
            client->send_auth_message();
        }
        return 0;
    }

    else if (reason == LWS_CALLBACK_CLIENT_RECEIVE) {
        if (client && in && len > 0) {
            std::string message(static_cast<const char*>(in), len);
            client->handle_message(message);
        }
        return 0;
    }

    else if (reason == LWS_CALLBACK_CLIENT_WRITEABLE) {
        if (!client) {
            return 0;
        }

        // Check if there are messages to send
        if (!client->has_queued_messages()) {
            return 0;
        }

        // Get next message from queue
        std::string msg = client->dequeue_message();
        if (msg.empty()) {
            return 0;
        }

        // Allocate buffer with LWS_PRE bytes padding before the message
        size_t msg_len = msg.size();
        std::vector<uint8_t> write_buffer(LWS_PRE + msg_len);

        // Copy message after LWS_PRE padding
        std::memcpy(&write_buffer[LWS_PRE], msg.c_str(), msg_len);

        // Write message (text mode for JSON)
        int written = lws_write(wsi, &write_buffer[LWS_PRE], msg_len, LWS_WRITE_TEXT);

        if (written < 0) {
            LOG_E("tradezero_ws", "Error writing to WebSocket");
            return -1;
        }

        if (static_cast<size_t>(written) < msg_len) {
            LOG_W("tradezero_ws", "Partial write: %d/%zu bytes", written, msg_len);
        } else {
            LOG_D("tradezero_ws", "Sent: %s", msg.c_str());
        }

        // Request another callback if more messages are queued
        if (client->has_queued_messages()) {
            lws_callback_on_writable(wsi);
        }

        return 0;
    }

    else if (reason == LWS_CALLBACK_CLIENT_CONNECTION_ERROR) {
        const char* error_msg = in ? static_cast<const char*>(in) : "unknown error";
        LOG_E("tradezero_ws", "Connection error: %s", error_msg);
        return -1;
    }

    else if (reason == LWS_CALLBACK_CLIENT_CLOSED) {
        LOG_W("tradezero_ws", "WebSocket connection closed - will attempt reconnect");
        return 0;
    }

    // Ignore all other callbacks
    return 0;
}

TradeZeroWebSocket::TradeZeroWebSocket()
    : m_port(0), m_use_ssl(true), m_stream(TZStream::PNL), m_running(false), m_connected(false),
      m_authenticated(false), m_lws_context(nullptr), m_lws_connection(nullptr),
      m_reconnect_attempts(0), m_should_reconnect(true) {
    m_api_key_id[0] = '\0';
    m_api_secret_key[0] = '\0';
    m_account_id[0] = '\0';
    m_error[0] = '\0';
    m_host[0] = '\0';
}

TradeZeroWebSocket::~TradeZeroWebSocket() {
    disconnect();

    // Securely zero credentials to prevent memory disclosure
    volatile char* p;
    p = m_api_key_id;
    for (size_t i = 0; i < sizeof(m_api_key_id); ++i) p[i] = 0;
    p = m_api_secret_key;
    for (size_t i = 0; i < sizeof(m_api_secret_key); ++i) p[i] = 0;
    p = m_account_id;
    for (size_t i = 0; i < sizeof(m_account_id); ++i) p[i] = 0;
}

void TradeZeroWebSocket::set_credentials(const char* api_key_id, const char* api_secret_key, const char* account_id) {
    std::strncpy(m_api_key_id, api_key_id, sizeof(m_api_key_id) - 1);
    m_api_key_id[sizeof(m_api_key_id) - 1] = '\0';

    std::strncpy(m_api_secret_key, api_secret_key, sizeof(m_api_secret_key) - 1);
    m_api_secret_key[sizeof(m_api_secret_key) - 1] = '\0';

    std::strncpy(m_account_id, account_id, sizeof(m_account_id) - 1);
    m_account_id[sizeof(m_account_id) - 1] = '\0';
}

void TradeZeroWebSocket::set_url(const char* host, int port, bool use_ssl) {
    if (host != nullptr) {
        std::strncpy(m_host, host, sizeof(m_host) - 1);
        m_host[sizeof(m_host) - 1] = '\0';
    } else {
        m_host[0] = '\0';
    }
    m_port = port;
    m_use_ssl = use_ssl;
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
    // Set active client for callback access
    g_active_ws_client = this;

    // Determine endpoint
    const char* path = (m_stream == TZStream::PNL) ? "/stream/pnl" : "/stream/portfolio";

    // libwebsockets setup
    struct lws_context_creation_info info;
    memset(&info, 0, sizeof(info));
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;  // Use our protocols array
    info.gid = static_cast<gid_t>(-1);
    info.uid = static_cast<uid_t>(-1);
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    // Keepalive configuration to prevent idle disconnects
    info.ka_time = 30;       // Start keepalive after 30 seconds idle
    info.ka_probes = 3;      // Send 3 probes before giving up
    info.ka_interval = 10;   // 10 seconds between probes

    m_lws_context = lws_create_context(&info);
    if (m_lws_context == nullptr) {
        std::snprintf(m_error, sizeof(m_error), "Failed to create lws context");
        LOG_E("tradezero_ws", "%s", m_error);
        m_running.store(false);
        return;
    }

    // Connection info - use custom URL if set, otherwise production
    struct lws_client_connect_info ccinfo;
    memset(&ccinfo, 0, sizeof(ccinfo));
    ccinfo.context = m_lws_context;

    if (m_host[0] != '\0') {
        // Use custom URL (for testing with mock server)
        ccinfo.address = m_host;
        ccinfo.port = m_port;
        ccinfo.ssl_connection = m_use_ssl ?
            (LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK) : 0;
    } else {
        // Production URL
        ccinfo.address = "webapi.tradezero.com";
        ccinfo.port = 443;
        ccinfo.ssl_connection = LCCSCF_USE_SSL | LCCSCF_ALLOW_SELFSIGNED | LCCSCF_SKIP_SERVER_CERT_HOSTNAME_CHECK;
    }

    ccinfo.path = path;
    ccinfo.host = ccinfo.address;
    ccinfo.origin = ccinfo.address;
    ccinfo.local_protocol_name = protocols[0].name;  // Use our protocol
    ccinfo.userdata = this;  // Pass client pointer as userdata

    const char* scheme = m_use_ssl ? "wss" : "ws";
    LOG_I("tradezero_ws", "Connecting to %s://%s:%d%s...", scheme, ccinfo.address, ccinfo.port, path);

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

    // Clear active client BEFORE destroying context
    // This prevents callbacks from accessing 'this' during destruction
    if (g_active_ws_client == this) {
        g_active_ws_client = nullptr;
    }

    // Now safe to destroy context - callbacks will see g_active_ws_client == nullptr
    if (m_lws_context) {
        lws_context_destroy(m_lws_context);
        m_lws_context = nullptr;
    }
}

void TradeZeroWebSocket::handle_message(const std::string& message) {
    // Parse JSON to determine message type
    try {
        auto j = json::parse(message);

        // Check if it's a system message
        if (j.contains("@system") && j["@system"].get<bool>()) {
            parse_system_message(message);
            return;
        }

        // Check for P&L stream messages
        if (j.contains("action") && j["action"].get<std::string>() == "init") {
            parse_pnl_snapshot(message);
        }
        else if (j.contains("target")) {
            std::string target = j["target"].get<std::string>();
            if (target == "aggCalcs") {
                parse_agg_update(message);
            }
            else if (target == "position") {
                parse_position_pnl(message);
            }
        }
        // Check for Portfolio stream messages
        else if (j.contains("subscription")) {
            std::string subscription = j["subscription"].get<std::string>();
            if (subscription == "Order") {
                parse_order_update(message);
            }
            else if (subscription == "Position") {
                parse_position_update(message);
            }
        }
    } catch (const json::exception& e) {
        LOG_E("tradezero_ws", "Failed to route message: %s", e.what());
    }
}

void TradeZeroWebSocket::parse_system_message(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);

        if (!j.contains("status")) {
            return;
        }

        std::string status = j["status"];

        if (status == "PENDING_AUTH") {
            // Auth message already sent in ESTABLISHED callback
        }
        else if (status == "CONNECTED") {
            m_authenticated.store(true);
            m_reconnect_attempts.store(0);  // Reset reconnect counter on success
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
        else if (status == "FAILED_AUTH") {
            LOG_E("tradezero_ws", "Authentication failed - check credentials");
            m_running.store(false);
            m_should_reconnect.store(false);  // Don't retry on auth failure
        }
        else {
            LOG_W("tradezero_ws", "Unknown system status: %s", status.c_str());
        }
    } catch (const json::exception& e) {
        LOG_E("tradezero_ws", "Failed to parse system message: %s", e.what());
    }
}

void TradeZeroWebSocket::send_auth_message() {
    try {
        json auth_msg = {
            {"key", m_api_key_id},
            {"secret", m_api_secret_key}
        };
        queue_message(auth_msg.dump());
    } catch (const std::exception& e) {
        LOG_E("tradezero_ws", "Failed to create auth message: %s", e.what());
    }
}

void TradeZeroWebSocket::send_subscribe_message() {
    json sub_msg;

    if (m_stream == TZStream::PNL) {
        sub_msg = {{"account", m_account_id}};
    } else {
        sub_msg = {
            {"accountId", m_account_id},
            {"subscriptions", {"Order", "Position"}}
        };
    }

    queue_message(sub_msg.dump());
    m_connected.store(true);
}

void TradeZeroWebSocket::parse_pnl_snapshot(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);

        TZPnLSnapshot snapshot;

        // Parse account-level fields
        if (j.contains("accountValue")) snapshot.account_value = j["accountValue"].get<float>();
        if (j.contains("availableCash")) snapshot.available_cash = j["availableCash"].get<float>();
        if (j.contains("dayUnrealized")) snapshot.day_unrealized = j["dayUnrealized"].get<float>();
        if (j.contains("dayRealized")) snapshot.day_realized = j["dayRealized"].get<float>();
        if (j.contains("dayPnl")) snapshot.day_pnl = j["dayPnl"].get<float>();
        if (j.contains("totalUnrealized")) snapshot.total_unrealized = j["totalUnrealized"].get<float>();

        // Calculate buying power from allowedLeverage if available
        if (j.contains("allowedLeverage")) {
            float leverage = j["allowedLeverage"].get<float>();
            snapshot.buying_power = snapshot.account_value * leverage;
        } else {
            snapshot.buying_power = snapshot.account_value * 4.0f;  // Default 4x leverage
        }

        // Parse positions array if present
        if (j.contains("positions") && j["positions"].is_array()) {
            for (const auto& pos_json : j["positions"]) {
                TZPositionPnL pos;

                if (pos_json.contains("positionId")) safe_strcpy(pos.position_id, pos_json["positionId"].get<std::string>().c_str(), sizeof(pos.position_id));
                if (pos_json.contains("symbol")) safe_strcpy(pos.symbol, pos_json["symbol"].get<std::string>().c_str(), sizeof(pos.symbol));

                // Parse pnlCalc nested object
                if (pos_json.contains("pnlCalc") && pos_json["pnlCalc"].is_object()) {
                    const auto& pnl_calc = pos_json["pnlCalc"];
                    if (pnl_calc.contains("unrealizedPnL")) pos.unrealized_pnl = pnl_calc["unrealizedPnL"].get<float>();
                    if (pnl_calc.contains("dayUnrealizedPnL")) pos.day_unrealized_pnl = pnl_calc["dayUnrealizedPnL"].get<float>();
                    if (pnl_calc.contains("pctPnLMove")) pos.pct_pnl_move = pnl_calc["pctPnLMove"].get<float>();
                    if (pnl_calc.contains("dayPctPnLMove")) pos.day_pct_pnl_move = pnl_calc["dayPctPnLMove"].get<float>();
                    if (pnl_calc.contains("exposure")) pos.exposure = pnl_calc["exposure"].get<float>();
                }

                if (pos_json.contains("realizedPnl")) pos.realized_pnl = pos_json["realizedPnl"].get<float>();
                if (pos_json.contains("dayRealizedPnl")) pos.day_realized_pnl = pos_json["dayRealizedPnl"].get<float>();

                snapshot.positions.push_back(pos);
            }
        }

        LOG_I("tradezero_ws", "P&L Snapshot: equity=%.2f, cash=%.2f, pnl=%.2f, positions=%zu",
              static_cast<double>(snapshot.account_value), static_cast<double>(snapshot.available_cash),
              static_cast<double>(snapshot.day_pnl), snapshot.positions.size());

        // Invoke callback
        TZPnLSnapshotCallback cb;
        {
            MutexLock lock(m_mutex);
            cb = m_pnl_snapshot_callback;
        }
        if (cb) {
            cb(snapshot);
        }
    } catch (const json::exception& e) {
        LOG_E("tradezero_ws", "Failed to parse P&L snapshot: %s", e.what());
    }
}

void TradeZeroWebSocket::parse_agg_update(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);

        TZAggUpdate update;

        // Parse aggregate update fields
        if (j.contains("accountValue")) update.account_value = j["accountValue"].get<float>();
        if (j.contains("exposure")) update.exposure = j["exposure"].get<float>();
        if (j.contains("dayUnrealized")) update.day_unrealized = j["dayUnrealized"].get<float>();
        if (j.contains("dayPnl")) update.day_pnl = j["dayPnl"].get<float>();
        if (j.contains("totalUnrealized")) update.total_unrealized = j["totalUnrealized"].get<float>();
        if (j.contains("equityRatio")) update.equity_ratio = j["equityRatio"].get<float>();

        // Invoke callback
        TZAggUpdateCallback cb;
        {
            MutexLock lock(m_mutex);
            cb = m_agg_update_callback;
        }
        if (cb) {
            cb(update);
        }
    } catch (const json::exception& e) {
        LOG_E("tradezero_ws", "Failed to parse aggregate update: %s", e.what());
    }
}

void TradeZeroWebSocket::parse_position_pnl(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);

        TZPositionPnL pos_pnl;

        // Parse position P&L fields
        if (j.contains("positionId")) safe_strcpy(pos_pnl.position_id, j["positionId"].get<std::string>().c_str(), sizeof(pos_pnl.position_id));
        if (j.contains("symbol")) safe_strcpy(pos_pnl.symbol, j["symbol"].get<std::string>().c_str(), sizeof(pos_pnl.symbol));

        // Parse pnlCalc nested object
        if (j.contains("pnlCalc") && j["pnlCalc"].is_object()) {
            const auto& pnl_calc = j["pnlCalc"];
            if (pnl_calc.contains("unrealizedPnL")) pos_pnl.unrealized_pnl = pnl_calc["unrealizedPnL"].get<float>();
            if (pnl_calc.contains("dayUnrealizedPnL")) pos_pnl.day_unrealized_pnl = pnl_calc["dayUnrealizedPnL"].get<float>();
            if (pnl_calc.contains("pctPnLMove")) pos_pnl.pct_pnl_move = pnl_calc["pctPnLMove"].get<float>();
            if (pnl_calc.contains("dayPctPnLMove")) pos_pnl.day_pct_pnl_move = pnl_calc["dayPctPnLMove"].get<float>();
            if (pnl_calc.contains("exposure")) pos_pnl.exposure = pnl_calc["exposure"].get<float>();
        }

        if (j.contains("realizedPnl")) pos_pnl.realized_pnl = j["realizedPnl"].get<float>();
        if (j.contains("dayRealizedPnl")) pos_pnl.day_realized_pnl = j["dayRealizedPnl"].get<float>();

        LOG_I("tradezero_ws", "Position P&L: %s unrealized=%.2f day_pnl=%.2f",
              pos_pnl.symbol, static_cast<double>(pos_pnl.unrealized_pnl), static_cast<double>(pos_pnl.day_unrealized_pnl));

        // Invoke callback
        TZPositionPnLCallback cb;
        {
            MutexLock lock(m_mutex);
            cb = m_position_pnl_callback;
        }
        if (cb && pos_pnl.symbol[0] != '\0') {
            cb(pos_pnl);
        }
    } catch (const json::exception& e) {
        LOG_E("tradezero_ws", "Failed to parse position P&L: %s", e.what());
    }
}

void TradeZeroWebSocket::parse_order_update(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);

        // Per API spec (tradezero-websocket.txt), order data is nested in "order" object
        // Format: {"ts": ..., "accountId": ..., "action": "update", "subscription": "Order", "order": {...}}
        json order_json;
        if (j.contains("order") && j["order"].is_object()) {
            order_json = j["order"];
        } else {
            // Fallback: order fields at top level (for backwards compatibility)
            order_json = j;
        }

        TZOrderUpdate order;

        // Parse all order fields from the order object
        if (order_json.contains("accountId")) safe_strcpy(order.account_id, order_json["accountId"].get<std::string>().c_str(), sizeof(order.account_id));
        if (order_json.contains("clientOrderId")) safe_strcpy(order.client_order_id, order_json["clientOrderId"].get<std::string>().c_str(), sizeof(order.client_order_id));
        if (order_json.contains("symbol")) safe_strcpy(order.symbol, order_json["symbol"].get<std::string>().c_str(), sizeof(order.symbol));
        if (order_json.contains("side")) safe_strcpy(order.side, order_json["side"].get<std::string>().c_str(), sizeof(order.side));
        if (order_json.contains("orderStatus")) safe_strcpy(order.order_status, order_json["orderStatus"].get<std::string>().c_str(), sizeof(order.order_status));
        if (order_json.contains("orderType")) safe_strcpy(order.order_type, order_json["orderType"].get<std::string>().c_str(), sizeof(order.order_type));
        if (order_json.contains("orderQuantity")) order.order_quantity = order_json["orderQuantity"];
        if (order_json.contains("executed")) order.executed = order_json["executed"];
        if (order_json.contains("leavesQuantity")) order.leaves_quantity = order_json["leavesQuantity"];
        if (order_json.contains("limitPrice")) order.limit_price = order_json["limitPrice"];
        if (order_json.contains("priceAvg")) order.price_avg = order_json["priceAvg"];
        if (order_json.contains("lastPrice")) order.last_price = order_json["lastPrice"];
        if (order_json.contains("lastQuantity")) order.last_quantity = order_json["lastQuantity"];
        if (order_json.contains("startTime")) safe_strcpy(order.start_time, order_json["startTime"].get<std::string>().c_str(), sizeof(order.start_time));
        if (order_json.contains("lastUpdated")) safe_strcpy(order.last_updated, order_json["lastUpdated"].get<std::string>().c_str(), sizeof(order.last_updated));

        LOG_I("tradezero_ws", "Order update: %s %s qty=%d status=%s",
              order.symbol, order.side, order.order_quantity, order.order_status);

        // Invoke callback
        TZOrderCallback cb;
        {
            MutexLock lock(m_mutex);
            cb = m_order_callback;
        }
        if (cb && order.symbol[0] != '\0') {
            cb(order);
        }
    } catch (const json::exception& e) {
        LOG_E("tradezero_ws", "Failed to parse order update: %s", e.what());
    }
}

void TradeZeroWebSocket::parse_position_update(const std::string& json_str) {
    try {
        auto j = json::parse(json_str);

        // Per API spec (tradezero-websocket.txt), position data is nested in "position" object
        // Format: {"ts": ..., "accountId": ..., "action": "update", "subscription": "Position", "position": {...}}
        json pos_json;
        if (j.contains("position") && j["position"].is_object()) {
            pos_json = j["position"];
        } else {
            // Fallback: position fields at top level (for backwards compatibility)
            pos_json = j;
        }

        TZPositionUpdate position;

        // Parse position fields from the position object
        if (pos_json.contains("id")) safe_strcpy(position.id, pos_json["id"].get<std::string>().c_str(), sizeof(position.id));
        if (pos_json.contains("accountId")) safe_strcpy(position.account_id, pos_json["accountId"].get<std::string>().c_str(), sizeof(position.account_id));
        if (pos_json.contains("symbol")) safe_strcpy(position.symbol, pos_json["symbol"].get<std::string>().c_str(), sizeof(position.symbol));
        if (pos_json.contains("shares")) position.shares = pos_json["shares"].get<float>();
        if (pos_json.contains("side")) safe_strcpy(position.side, pos_json["side"].get<std::string>().c_str(), sizeof(position.side));
        if (pos_json.contains("priceAvg")) position.price_avg = pos_json["priceAvg"].get<float>();
        if (pos_json.contains("priceOpen")) position.price_open = pos_json["priceOpen"].get<float>();
        if (pos_json.contains("priceClose")) position.price_close = pos_json["priceClose"].get<float>();
        if (pos_json.contains("dayOvernight")) safe_strcpy(position.day_overnight, pos_json["dayOvernight"].get<std::string>().c_str(), sizeof(position.day_overnight));
        if (pos_json.contains("createdDate")) safe_strcpy(position.created_date, pos_json["createdDate"].get<std::string>().c_str(), sizeof(position.created_date));
        if (pos_json.contains("updatedDate")) safe_strcpy(position.updated_date, pos_json["updatedDate"].get<std::string>().c_str(), sizeof(position.updated_date));

        LOG_I("tradezero_ws", "Position update: %s shares=%.0f avg_price=%.2f",
              position.symbol, static_cast<double>(position.shares), static_cast<double>(position.price_avg));

        // Invoke callback
        TZPositionCallback cb;
        {
            MutexLock lock(m_mutex);
            cb = m_position_callback;
        }
        if (cb && position.symbol[0] != '\0') {
            cb(position);
        }
    } catch (const json::exception& e) {
        LOG_E("tradezero_ws", "Failed to parse position update: %s", e.what());
    }
}

// ============================================================================
// Message Queue Methods
// ============================================================================

void TradeZeroWebSocket::queue_message(const std::string& message) {
    MutexLock lock(m_mutex);
    m_outgoing_queue.push_back(message);

    // Request callback to send message
    if (m_lws_connection) {
        lws_callback_on_writable(m_lws_connection);
    }
}

bool TradeZeroWebSocket::has_queued_messages() const {
    MutexLock lock(m_mutex);
    return !m_outgoing_queue.empty();
}

std::string TradeZeroWebSocket::dequeue_message() {
    MutexLock lock(m_mutex);
    if (m_outgoing_queue.empty()) {
        return "";
    }

    std::string msg = m_outgoing_queue.front();
    m_outgoing_queue.erase(m_outgoing_queue.begin());
    return msg;
}

// ============================================================================
// Reconnection Logic
// ============================================================================

int TradeZeroWebSocket::get_reconnect_delay_ms() const {
    int attempts = m_reconnect_attempts.load();
    // Exponential backoff: 1s, 2s, 4s, 8s, 16s, 32s, 60s (capped)
    int delay = INITIAL_RECONNECT_DELAY_MS * (1 << attempts);
    return (delay > MAX_RECONNECT_DELAY_MS) ? MAX_RECONNECT_DELAY_MS : delay;
}

bool TradeZeroWebSocket::reconnect() {
    if (!m_should_reconnect.load()) {
        LOG_I("tradezero_ws", "Reconnection disabled");
        return false;
    }

    int attempts = m_reconnect_attempts.load();
    if (attempts >= MAX_RECONNECT_ATTEMPTS) {
        LOG_E("tradezero_ws", "Max reconnection attempts reached (%d)", MAX_RECONNECT_ATTEMPTS);
        m_should_reconnect.store(false);
        return false;
    }

    m_reconnect_attempts.fetch_add(1);
    int delay_ms = get_reconnect_delay_ms();

    LOG_I("tradezero_ws", "Reconnecting in %dms (attempt %d/%d)...",
          delay_ms, attempts + 1, MAX_RECONNECT_ATTEMPTS);

    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

    // Reset connection state
    m_connected.store(false);
    m_authenticated.store(false);

    // Reconnection will be handled by worker thread restarting the connection
    return true;
}
