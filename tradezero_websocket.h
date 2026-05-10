// tradezero_websocket.h - TradeZero WebSocket streaming client
#ifndef TRADEZERO_WEBSOCKET_H
#define TRADEZERO_WEBSOCKET_H

#include "iqfeed_tcp.h"  // For thread safety annotations (Mutex, GUARDED_BY, etc.)
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>

// Forward declaration for libwebsockets context
struct lws_context;
struct lws;

// WebSocket streams
enum class TZStream {
    PNL,        // wss://.../stream/pnl - Account + position P&L
    PORTFOLIO   // wss://.../stream/portfolio - Orders + position changes
};

// ============================================================================
// P&L Stream Data Structures
// ============================================================================

// Position P&L information from P&L stream
struct TZPositionPnL {
    char position_id[32];           // positionId
    char symbol[16];                // symbol
    float unrealized_pnl;           // pnlCalc.unrealizedPnL
    float day_unrealized_pnl;       // pnlCalc.dayUnrealizedPnL
    float pct_pnl_move;             // pnlCalc.pctPnLMove
    float day_pct_pnl_move;         // pnlCalc.dayPctPnLMove
    float exposure;                 // pnlCalc.exposure
    float realized_pnl;             // realizedPnl
    float day_realized_pnl;         // dayRealizedPnl

    TZPositionPnL();
};

// P&L Stream: Initial snapshot + account-level updates
struct TZPnLSnapshot {
    float account_value;            // accountValue
    float available_cash;           // availableCash
    float buying_power;             // Calculate from allowedLeverage * equity
    float day_unrealized;           // dayUnrealized
    float day_realized;             // dayRealized
    float day_pnl;                  // dayPnl
    float total_unrealized;         // totalUnrealized
    int day_trades_remaining;       // Derive from account type (future)
    std::vector<TZPositionPnL> positions;  // positions array

    TZPnLSnapshot();
};

// P&L Stream: Real-time aggregate updates
struct TZAggUpdate {
    float account_value;
    float exposure;
    float day_unrealized;
    float day_pnl;
    float total_unrealized;
    float equity_ratio;

    TZAggUpdate();
};

// ============================================================================
// Portfolio Stream Data Structures
// ============================================================================

// Portfolio Stream: Order updates (from WebSocket)
struct TZOrderUpdate {
    char account_id[32];            // accountId
    char client_order_id[64];       // clientOrderId
    char symbol[16];                // symbol
    char side[16];                  // side ("Buy" or "Sell")
    char order_status[32];          // orderStatus ("Filled", "Canceled", "Accepted", etc.)
    char order_type[16];            // orderType ("Limit", "Market", etc.)
    int order_quantity;             // orderQuantity
    int executed;                   // executed
    int leaves_quantity;            // leavesQuantity
    float limit_price;              // limitPrice
    float price_avg;                // priceAvg
    float last_price;               // lastPrice
    int last_quantity;              // lastQuantity
    char start_time[32];            // startTime (ISO 8601)
    char last_updated[32];          // lastUpdated (ISO 8601)

    TZOrderUpdate();
};

// Portfolio Stream: Position updates (from order fills)
struct TZPositionUpdate {
    char id[32];                    // id
    char account_id[32];            // accountId
    char symbol[16];                // symbol
    float shares;                   // shares (positive=long, negative=short)
    char side[16];                  // side ("Long" or "Short")
    float price_avg;                // priceAvg
    float price_open;               // priceOpen
    float price_close;              // priceClose (current price)
    char day_overnight[16];         // dayOvernight ("Day" or "Overnight")
    char created_date[32];          // createdDate (ISO 8601)
    char updated_date[32];          // updatedDate (ISO 8601)

    TZPositionUpdate();
};

// ============================================================================
// Callbacks
// ============================================================================

using TZPnLSnapshotCallback = std::function<void(const TZPnLSnapshot&)>;
using TZAggUpdateCallback = std::function<void(const TZAggUpdate&)>;
using TZPositionPnLCallback = std::function<void(const TZPositionPnL&)>;
using TZOrderCallback = std::function<void(const TZOrderUpdate&)>;
using TZPositionCallback = std::function<void(const TZPositionUpdate&)>;
using TZConnectionCallback = std::function<void(bool connected)>;

// ============================================================================
// TradeZero WebSocket Client
// ============================================================================

class TradeZeroWebSocket {
public:
    TradeZeroWebSocket();
    ~TradeZeroWebSocket();

    // Non-copyable (owns thread, socket, mutex)
    TradeZeroWebSocket(const TradeZeroWebSocket&) = delete;
    TradeZeroWebSocket& operator=(const TradeZeroWebSocket&) = delete;

    // Configuration
    void set_credentials(const char* api_key_id, const char* api_secret_key, const char* account_id);

    // Connection management
    bool connect(TZStream stream) EXCLUDES(m_mutex);
    void disconnect() EXCLUDES(m_mutex);
    bool is_connected() const;

    // Set callbacks (call before connect)
    void set_pnl_snapshot_callback(TZPnLSnapshotCallback callback) EXCLUDES(m_mutex);
    void set_agg_update_callback(TZAggUpdateCallback callback) EXCLUDES(m_mutex);
    void set_position_pnl_callback(TZPositionPnLCallback callback) EXCLUDES(m_mutex);
    void set_order_callback(TZOrderCallback callback) EXCLUDES(m_mutex);
    void set_position_callback(TZPositionCallback callback) EXCLUDES(m_mutex);
    void set_connection_callback(TZConnectionCallback callback) EXCLUDES(m_mutex);

    // Get last error
    const char* last_error() const;

    // Message handling (public for callback access)
    void handle_message(const std::string& message) EXCLUDES(m_mutex);

    // Message queue access (public for callback access)
    void queue_message(const std::string& message) EXCLUDES(m_mutex);
    bool has_queued_messages() const EXCLUDES(m_mutex);
    std::string dequeue_message() EXCLUDES(m_mutex);
    void send_auth_message();
    void send_subscribe_message();

private:
    char m_api_key_id[128];
    char m_api_secret_key[128];
    char m_account_id[32];
    char m_error[256];
    TZStream m_stream;

    // Thread management
    std::atomic<bool> m_running;
    std::atomic<bool> m_connected;
    std::atomic<bool> m_authenticated;
    std::thread m_thread;
    mutable Mutex m_mutex;

    // libwebsockets context
    struct lws_context* m_lws_context;
    struct lws* m_lws_connection;

    // Message queue for outgoing messages (guarded by mutex)
    std::vector<std::string> m_outgoing_queue GUARDED_BY(m_mutex);

    // Reconnection state
    std::atomic<int> m_reconnect_attempts;
    std::atomic<bool> m_should_reconnect;
    static constexpr int MAX_RECONNECT_ATTEMPTS = 10;
    static constexpr int INITIAL_RECONNECT_DELAY_MS = 1000;
    static constexpr int MAX_RECONNECT_DELAY_MS = 60000;

    // Callbacks (guarded by mutex)
    TZPnLSnapshotCallback m_pnl_snapshot_callback GUARDED_BY(m_mutex);
    TZAggUpdateCallback m_agg_update_callback GUARDED_BY(m_mutex);
    TZPositionPnLCallback m_position_pnl_callback GUARDED_BY(m_mutex);
    TZOrderCallback m_order_callback GUARDED_BY(m_mutex);
    TZPositionCallback m_position_callback GUARDED_BY(m_mutex);
    TZConnectionCallback m_connection_callback GUARDED_BY(m_mutex);

    // Background thread
    void worker_thread() NO_THREAD_SAFETY_ANALYSIS;

    // Message parsing (now using nlohmann/json)
    void parse_pnl_snapshot(const std::string& json) EXCLUDES(m_mutex);
    void parse_agg_update(const std::string& json) EXCLUDES(m_mutex);
    void parse_position_pnl(const std::string& json) EXCLUDES(m_mutex);
    void parse_order_update(const std::string& json) EXCLUDES(m_mutex);
    void parse_position_update(const std::string& json) EXCLUDES(m_mutex);
    void parse_system_message(const std::string& json);

    // Connection helpers (private)
    bool reconnect();
    int get_reconnect_delay_ms() const;
};

// ============================================================================
// Global instances (one for each stream)
// ============================================================================

TradeZeroWebSocket& get_tradezero_pnl();       // P&L stream
TradeZeroWebSocket& get_tradezero_portfolio(); // Order/position stream

#endif // TRADEZERO_WEBSOCKET_H
