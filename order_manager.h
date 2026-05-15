// order_manager.h - Order execution and position management
#ifndef ORDER_MANAGER_H
#define ORDER_MANAGER_H

#include "types.h"
#include "database.h"
#include "market_data.h"
#include "iqfeed_tcp.h"  // For Mutex, MutexLock, GUARDED_BY, EXCLUDES
#include <vector>
#include <functional>

// Forward declarations
class TradeZeroClient;
struct TZOrderUpdate;
struct TZPositionUpdate;
struct TZPnLSnapshot;

// Callback for order events
typedef std::function<void(const Order&)> OrderCallback;
typedef std::function<void(const Position&)> PositionCallback;
// Callback for order errors (symbol, error message)
typedef std::function<void(const char* symbol, const char* error)> OrderErrorCallback;

// Order manager for executing orders and tracking positions
class OrderManager {
public:
    OrderManager();
    ~OrderManager();

    // Initialize with database and market data
    void init(Database* db, MarketData* market);

    // Reset to clean state (for testing)
    void reset() EXCLUDES(m_mutex);

    // Place orders (route is optional, nullptr uses default/SMART routing)
    int64_t buy(const char* symbol, int quantity, float price, const char* route = nullptr) EXCLUDES(m_mutex);
    int64_t sell(const char* symbol, int quantity, float price, const char* route = nullptr) EXCLUDES(m_mutex);
    int64_t sell_market(const char* symbol, int quantity, const char* route = nullptr) EXCLUDES(m_mutex);

    // Cancel orders
    bool cancel_order(int64_t order_id) EXCLUDES(m_mutex);
    bool cancel_all_orders(const char* symbol = nullptr) EXCLUDES(m_mutex);

    // Get orders (returns copy for thread safety)
    std::vector<Order> get_pending_orders() const EXCLUDES(m_mutex);
    Order* find_order(int64_t order_id) EXCLUDES(m_mutex);

    // Get positions (returns copy for thread safety)
    std::vector<Position> get_open_positions() const EXCLUDES(m_mutex);
    std::vector<ClosedPosition> get_closed_positions() const EXCLUDES(m_mutex);
    Position* find_position(const char* symbol) EXCLUDES(m_mutex);

    // Update positions with current market prices
    void update_prices() EXCLUDES(m_mutex);

    // Calculate position quantity for percentage sell
    int calculate_sell_quantity(const char* symbol, int percentage) EXCLUDES(m_mutex);

    // Set callbacks
    void set_order_callback(OrderCallback cb) EXCLUDES(m_mutex);
    void set_position_callback(PositionCallback cb) EXCLUDES(m_mutex);
    void set_error_callback(OrderErrorCallback cb) EXCLUDES(m_mutex);

    // Load from database
    void load_from_database() EXCLUDES(m_mutex);

    // Save to database
    void save_to_database() EXCLUDES(m_mutex);

    // TradeZero integration
    void set_tradezero_client(TradeZeroClient* client);
    void on_tradezero_order_update(const TZOrderUpdate& update) EXCLUDES(m_mutex);
    void on_tradezero_position_update(const TZPositionUpdate& update) EXCLUDES(m_mutex);
    void on_tradezero_pnl_snapshot(const TZPnLSnapshot& snapshot) EXCLUDES(m_mutex);
    std::vector<ClosedPosition> get_todays_closed_positions() const EXCLUDES(m_mutex);

    // Load initial data from TradeZero REST API
    void load_tradezero_positions(const std::vector<Position>& positions) EXCLUDES(m_mutex);
    void load_tradezero_orders(const std::vector<Order>& orders) EXCLUDES(m_mutex);
    void load_tradezero_order_history(const std::vector<Order>& orders) EXCLUDES(m_mutex);  // Build closed positions from filled orders

    // Helper to find order by client_order_id
    Order* find_order_by_client_id(const char* client_order_id) EXCLUDES(m_mutex);

    // Get last error message (for UI display)
    const char* last_error() const;

private:
    mutable Mutex m_mutex;  // Protects all member data below
    Database* m_db;
    MarketData* m_market;
    TradeZeroClient* m_tradezero_client;
    std::vector<Order> m_pending_orders GUARDED_BY(m_mutex);
    std::vector<Position> m_open_positions GUARDED_BY(m_mutex);
    std::vector<ClosedPosition> m_closed_positions GUARDED_BY(m_mutex);
    int64_t m_next_order_id GUARDED_BY(m_mutex);
    OrderCallback m_order_callback GUARDED_BY(m_mutex);
    PositionCallback m_position_callback GUARDED_BY(m_mutex);
    OrderErrorCallback m_error_callback GUARDED_BY(m_mutex);
    char m_error[256];  // Last error message for UI display

    void update_position_on_buy(const char* symbol, int quantity, float price);
    void update_position_on_sell(const char* symbol, int quantity, float price);
    int64_t get_current_timestamp() const;

    // Helper to parse TradeZero order status
    OrderStatus parse_tz_order_status(const char* status_str);

    // Internal helper methods that require m_mutex to be held
    Position* find_position_locked(const char* symbol);
    Order* find_order_locked(int64_t order_id);
    Order* find_order_by_client_id_locked(const char* client_order_id);
};

// Global order manager instance
OrderManager& get_order_manager();

#endif // ORDER_MANAGER_H
