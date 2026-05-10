// order_manager.h - Order execution and position management
#ifndef ORDER_MANAGER_H
#define ORDER_MANAGER_H

#include "types.h"
#include "database.h"
#include "market_data.h"
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

// Order manager for executing orders and tracking positions
class OrderManager {
public:
    OrderManager();
    ~OrderManager();

    // Initialize with database and market data
    void init(Database* db, MarketData* market);

    // Place orders
    int64_t buy(const char* symbol, int quantity, float price);
    int64_t sell(const char* symbol, int quantity, float price);

    // Cancel orders
    bool cancel_order(int64_t order_id);
    bool cancel_all_orders(const char* symbol = nullptr);

    // Get orders
    const std::vector<Order>& get_pending_orders() const;
    Order* find_order(int64_t order_id);

    // Get positions
    const std::vector<Position>& get_open_positions() const;
    const std::vector<ClosedPosition>& get_closed_positions() const;
    Position* find_position(const char* symbol);

    // Update positions with current market prices
    void update_prices();

    // Calculate position quantity for percentage sell
    int calculate_sell_quantity(const char* symbol, int percentage);

    // Set callbacks
    void set_order_callback(OrderCallback cb);
    void set_position_callback(PositionCallback cb);

    // Load from database
    void load_from_database();

    // Save to database
    void save_to_database();

    // TradeZero integration
    void set_tradezero_client(TradeZeroClient* client);
    void on_tradezero_order_update(const TZOrderUpdate& update);
    void on_tradezero_position_update(const TZPositionUpdate& update);
    void on_tradezero_pnl_snapshot(const TZPnLSnapshot& snapshot);
    const std::vector<ClosedPosition>& get_todays_closed_positions() const;

    // Load initial data from TradeZero REST API
    void load_tradezero_positions(const std::vector<Position>& positions);
    void load_tradezero_orders(const std::vector<Order>& orders);

    // Helper to find order by client_order_id
    Order* find_order_by_client_id(const char* client_order_id);

private:
    Database* m_db;
    MarketData* m_market;
    TradeZeroClient* m_tradezero_client;
    std::vector<Order> m_pending_orders;
    std::vector<Position> m_open_positions;
    std::vector<ClosedPosition> m_closed_positions;
    int64_t m_next_order_id;
    OrderCallback m_order_callback;
    PositionCallback m_position_callback;

    void update_position_on_buy(const char* symbol, int quantity, float price);
    void update_position_on_sell(const char* symbol, int quantity, float price);
    int64_t get_current_timestamp() const;

    // Helper to parse TradeZero order status
    OrderStatus parse_tz_order_status(const char* status_str);
};

// Global order manager instance
OrderManager& get_order_manager();

#endif // ORDER_MANAGER_H
