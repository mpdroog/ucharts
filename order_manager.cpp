// order_manager.cpp - Order execution and position management implementation
#include "order_manager.h"
#include "tradezero_client.h"
#include "tradezero_websocket.h"
#include "logger.h"
#include <cstring>
#include <ctime>
#include <cctype>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Global order manager instance
static OrderManager g_order_manager;

OrderManager& get_order_manager() {
    return g_order_manager;
}

OrderManager::OrderManager()
    : m_db(nullptr), m_market(nullptr), m_tradezero_client(nullptr), m_next_order_id(1) {
    m_error[0] = '\0';
}

OrderManager::~OrderManager() {
}

void OrderManager::init(Database* db, MarketData* market) {
    m_db = db;
    m_market = market;
}

void OrderManager::reset() {
    MutexLock lock(m_mutex);
    m_pending_orders.clear();
    m_open_positions.clear();
    m_closed_positions.clear();
    m_next_order_id = 1;
    m_order_callback = nullptr;
    m_position_callback = nullptr;
    m_error_callback = nullptr;
    m_error[0] = '\0';
}

int64_t OrderManager::get_current_timestamp() const {
    return static_cast<int64_t>(std::time(nullptr));
}

int64_t OrderManager::buy(const char* symbol, int quantity, float price, const char* route) {
    if (symbol == nullptr || symbol[0] == '\0' || quantity <= 0 || price <= 0) {
        return -1;
    }

    MutexLock lock(m_mutex);

    // Place order via TradeZero REST API
    if (m_tradezero_client != nullptr && m_tradezero_client->is_configured()) {
        // Create local pending order FIRST with temporary client_order_id
        // This prevents race condition where WebSocket callback creates duplicate order
        Order order;
        order.id = m_next_order_id++;
        safe_strcpy(order.symbol, symbol, sizeof(order.symbol));
        // Temporary client_order_id - will be updated when server responds
        std::snprintf(order.client_order_id, sizeof(order.client_order_id), "PENDING_BUY_%lld",
                     static_cast<long long>(order.id));
        order.side = OrderSide::BUY;
        order.quantity = quantity;
        order.executed = 0;
        order.canceled = 0;
        order.leaves = quantity;
        order.price = price;
        order.avg_price = 0;
        order.status = OrderStatus::PENDING;
        order.created_at = get_current_timestamp();

        m_pending_orders.push_back(order);
        int64_t local_order_id = order.id;

        // Now send REST request to broker
        TZResponse resp = m_tradezero_client->place_order(
            symbol, quantity, "buy", "limit", price, 0.0f, route
        );

        if (!resp.success) {
            LOG_E("orders", "TradeZero buy failed: %s", resp.error.c_str());
            // Store error for UI display
            safe_strcpy(m_error, resp.error.c_str(), sizeof(m_error));
            // Remove the pending order we added
            for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ++it) {
                if (it->id == local_order_id) {
                    m_pending_orders.erase(it);
                    break;
                }
            }
            return -1;
        }
        m_error[0] = '\0';  // Clear error on success

        // Extract clientOrderId from response JSON and update our local order
        char client_order_id[64];
        client_order_id[0] = '\0';
        try {
            auto j = json::parse(resp.body);
            if (j.contains("clientOrderId") && j["clientOrderId"].is_string()) {
                std::string cid = j["clientOrderId"].get<std::string>();
                safe_strcpy(client_order_id, cid.c_str(), sizeof(client_order_id));
            }
        } catch (const json::exception& e) {
            LOG_E("orders", "Failed to parse place_order response: %s", e.what());
        }

        // Fallback: generate our own ID if server didn't provide one
        if (client_order_id[0] == '\0') {
            std::snprintf(client_order_id, sizeof(client_order_id), "BUY_%s_%lld",
                         symbol, static_cast<long long>(get_current_timestamp()));
            LOG_W("orders", "Server didn't return clientOrderId, generated: %s", client_order_id);
        }

        // Update the local order with the real client_order_id
        for (auto& o : m_pending_orders) {
            if (o.id == local_order_id) {
                safe_strcpy(o.client_order_id, client_order_id, sizeof(o.client_order_id));
                break;
            }
        }

        LOG_I("orders", "BUY order placed via TradeZero: id=%lld %s qty=%d @ %.2f clientOrderId=%s",
              static_cast<long long>(local_order_id), symbol, quantity, static_cast<double>(price), client_order_id);

        if (m_order_callback) {
            for (const auto& o : m_pending_orders) {
                if (o.id == local_order_id) {
                    m_order_callback(o);
                    break;
                }
            }
        }

        return local_order_id;
    } else {
        LOG_E("orders", "TradeZero client not configured");
        return -1;
    }
}

int64_t OrderManager::sell(const char* symbol, int quantity, float price, const char* route) {
    if (symbol == nullptr || symbol[0] == '\0' || quantity <= 0 || price <= 0) {
        return -1;
    }

    MutexLock lock(m_mutex);

    // Check if we have enough position to sell
    Position* pos = find_position_locked(symbol);
    if (pos == nullptr || pos->quantity < quantity) {
        LOG_W("orders", "SELL rejected: %s qty=%d - insufficient position", symbol, quantity);
        return -1;  // Not enough shares to sell
    }

    // Place order via TradeZero REST API
    if (m_tradezero_client != nullptr && m_tradezero_client->is_configured()) {
        // Create local pending order FIRST with temporary client_order_id
        Order order;
        order.id = m_next_order_id++;
        safe_strcpy(order.symbol, symbol, sizeof(order.symbol));
        std::snprintf(order.client_order_id, sizeof(order.client_order_id), "PENDING_SELL_%lld",
                     static_cast<long long>(order.id));
        order.side = OrderSide::SELL;
        order.quantity = quantity;
        order.executed = 0;
        order.canceled = 0;
        order.leaves = quantity;
        order.price = price;
        order.avg_price = 0;
        order.status = OrderStatus::PENDING;
        order.created_at = get_current_timestamp();

        m_pending_orders.push_back(order);
        int64_t local_order_id = order.id;

        // Now send REST request to broker
        TZResponse resp = m_tradezero_client->place_order(
            symbol, quantity, "sell", "limit", price, 0.0f, route
        );

        if (!resp.success) {
            LOG_E("orders", "TradeZero sell failed: %s", resp.error.c_str());
            // Store error for UI display
            safe_strcpy(m_error, resp.error.c_str(), sizeof(m_error));
            // Remove the pending order we added
            for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ++it) {
                if (it->id == local_order_id) {
                    m_pending_orders.erase(it);
                    break;
                }
            }
            return -1;
        }
        m_error[0] = '\0';  // Clear error on success

        // Extract clientOrderId from response JSON
        char client_order_id[64];
        client_order_id[0] = '\0';
        try {
            auto j = json::parse(resp.body);
            if (j.contains("clientOrderId") && j["clientOrderId"].is_string()) {
                std::string cid = j["clientOrderId"].get<std::string>();
                safe_strcpy(client_order_id, cid.c_str(), sizeof(client_order_id));
            }
        } catch (const json::exception& e) {
            LOG_E("orders", "Failed to parse place_order response: %s", e.what());
        }

        // Fallback: generate our own ID if server didn't provide one
        if (client_order_id[0] == '\0') {
            std::snprintf(client_order_id, sizeof(client_order_id), "SELL_%s_%lld",
                         symbol, static_cast<long long>(get_current_timestamp()));
            LOG_W("orders", "Server didn't return clientOrderId, generated: %s", client_order_id);
        }

        // Update the local order with the real client_order_id
        for (auto& o : m_pending_orders) {
            if (o.id == local_order_id) {
                safe_strcpy(o.client_order_id, client_order_id, sizeof(o.client_order_id));
                break;
            }
        }

        LOG_I("orders", "SELL order placed via TradeZero: id=%lld %s qty=%d @ %.2f",
              static_cast<long long>(local_order_id), symbol, quantity, static_cast<double>(price));

        if (m_order_callback) {
            for (const auto& o : m_pending_orders) {
                if (o.id == local_order_id) {
                    m_order_callback(o);
                    break;
                }
            }
        }

        return local_order_id;
    } else {
        LOG_E("orders", "TradeZero client not configured");
        return -1;
    }
}

bool OrderManager::cancel_order(int64_t order_id) {
    MutexLock lock(m_mutex);

    Order* order = find_order_locked(order_id);
    if (order == nullptr) return false;

    // Cancel via TradeZero REST API
    if (m_tradezero_client != nullptr && m_tradezero_client->is_configured()) {
        TZResponse resp = m_tradezero_client->cancel_order(order->client_order_id);
        if (!resp.success) {
            LOG_E("orders", "TradeZero cancel failed: %s", resp.error.c_str());
            safe_strcpy(m_error, resp.error.c_str(), sizeof(m_error));
            if (m_error_callback) {
                m_error_callback(order->symbol, resp.error.c_str());
            }
            return false;
        }

        LOG_I("orders", "Order cancel requested via TradeZero: id=%lld %s",
              static_cast<long long>(order->id), order->symbol);

        // WebSocket will confirm cancellation
        // For now, keep order in pending state until WebSocket confirms
        return true;
    } else {
        LOG_E("orders", "TradeZero client not configured");
        if (m_error_callback) {
            m_error_callback("", "TradeZero client not configured");
        }
        return false;
    }
}

bool OrderManager::cancel_all_orders(const char* symbol) {
    MutexLock lock(m_mutex);

    if (m_tradezero_client == nullptr || !m_tradezero_client->is_configured()) {
        LOG_E("orders", "TradeZero client not configured");
        return false;
    }

    if (symbol == nullptr) {
        // Cancel all orders via TradeZero API
        TZResponse resp = m_tradezero_client->cancel_all_orders();
        if (!resp.success) {
            LOG_E("orders", "TradeZero cancel all failed: %s", resp.error.c_str());
            return false;
        }

        LOG_I("orders", "Cancel all orders requested via TradeZero");
        // WebSocket will confirm cancellations
        return true;
    } else {
        // Cancel orders for specific symbol (iterate and cancel individually)
        bool cancelled_any = false;
        for (const auto& order : m_pending_orders) {
            if (symbols_equal(order.symbol, symbol)) {
                TZResponse resp = m_tradezero_client->cancel_order(order.client_order_id);
                if (resp.success) {
                    cancelled_any = true;
                    LOG_I("orders", "Cancel requested for order: %s", order.client_order_id);
                } else {
                    LOG_E("orders", "Failed to cancel order %s: %s",
                          order.client_order_id, resp.error.c_str());
                }
            }
        }

        // WebSocket will confirm cancellations
        return cancelled_any;
    }
}

std::vector<Order> OrderManager::get_pending_orders() const {
    MutexLock lock(m_mutex);
    return m_pending_orders;  // Returns copy for thread safety
}

Order* OrderManager::find_order(int64_t order_id) {
    MutexLock lock(m_mutex);
    return find_order_locked(order_id);
}

// Internal helper - requires m_mutex to be held
Order* OrderManager::find_order_locked(int64_t order_id) {
    for (auto& order : m_pending_orders) {
        if (order.id == order_id) {
            return &order;
        }
    }
    return nullptr;
}

std::vector<Position> OrderManager::get_open_positions() const {
    MutexLock lock(m_mutex);
    return m_open_positions;  // Returns copy for thread safety
}

std::vector<ClosedPosition> OrderManager::get_closed_positions() const {
    MutexLock lock(m_mutex);
    return m_closed_positions;  // Returns copy for thread safety
}

Position* OrderManager::find_position(const char* symbol) {
    MutexLock lock(m_mutex);
    return find_position_locked(symbol);
}

// Internal helper - requires m_mutex to be held
Position* OrderManager::find_position_locked(const char* symbol) {
    if (symbol == nullptr) return nullptr;

    for (auto& pos : m_open_positions) {
        if (symbols_equal(pos.symbol, symbol)) {
            return &pos;
        }
    }
    return nullptr;
}

void OrderManager::update_prices() {
    if (m_market == nullptr) return;

    MutexLock lock(m_mutex);
    for (auto& pos : m_open_positions) {
        float price = m_market->get_current_price(pos.symbol);
        if (price > 0) {
            pos.current_price = price;
        }
    }
}

// NOTE: process_fills() removed - TradeZero integration
// Order fills now come from TradeZero WebSocket callbacks (on_tradezero_order_update)
// instead of CSV simulation

int OrderManager::calculate_sell_quantity(const char* symbol, int percentage) {
    MutexLock lock(m_mutex);

    Position* pos = find_position_locked(symbol);
    if (pos == nullptr || pos->quantity <= 0 || percentage <= 0) {
        return 0;
    }

    int qty = (pos->quantity * percentage) / 100;
    return (qty > 0) ? qty : 1;  // At least 1 share
}

void OrderManager::update_position_on_buy(const char* symbol, int quantity, float price) {
    // Note: Caller must hold m_mutex
    Position* pos = find_position_locked(symbol);

    LOG_I("orders", "update_position_on_buy: %s qty=%d @ %.2f existing_pos=%s",
          symbol, quantity, static_cast<double>(price), pos ? "yes" : "no");

    if (pos != nullptr) {
        // Update existing position with weighted average price
        float total_cost = (pos->avg_price * static_cast<float>(pos->quantity)) +
                          (price * static_cast<float>(quantity));
        pos->quantity += quantity;
        pos->avg_price = total_cost / static_cast<float>(pos->quantity);
    } else {
        // Create new position
        Position new_pos;
        safe_strcpy(new_pos.symbol, symbol, sizeof(new_pos.symbol));
        new_pos.quantity = quantity;
        new_pos.avg_price = price;
        new_pos.current_price = price;
        m_open_positions.push_back(new_pos);
        pos = &m_open_positions.back();
    }

    // Update current price
    if (m_market != nullptr) {
        float market_price = m_market->get_current_price(symbol);
        if (market_price > 0) {
            pos->current_price = market_price;
        }
    }

    // Save to database
    if (m_db != nullptr && m_db->is_open() && pos != nullptr) {
        m_db->save_position(*pos);
    }

    if (m_position_callback && pos != nullptr) {
        m_position_callback(*pos);
    }
}

void OrderManager::update_position_on_sell(const char* symbol, int quantity, float price) {
    // Note: Caller must hold m_mutex
    Position* pos = find_position_locked(symbol);
    if (pos == nullptr) {
        LOG_W("orders", "update_position_on_sell: No position found for %s", symbol);
        return;
    }
    LOG_I("orders", "Closing %d shares of %s @ %.2f (pos qty=%d avg=%.2f)",
          quantity, symbol, static_cast<double>(price), pos->quantity, static_cast<double>(pos->avg_price));

    // Record closed position
    ClosedPosition closed;
    safe_strcpy(closed.symbol, symbol, sizeof(closed.symbol));
    closed.quantity = quantity;
    closed.entry_price = pos->avg_price;
    closed.exit_price = price;
    closed.entry_time = 0;  // Would need to track this
    closed.exit_time = get_current_timestamp();
    m_closed_positions.push_back(closed);

    // Save closed position to database
    if (m_db != nullptr && m_db->is_open()) {
        m_db->save_closed_position(closed);
    }

    // Update or remove position
    pos->quantity -= quantity;

    if (pos->quantity <= 0) {
        // Remove position
        if (m_db != nullptr && m_db->is_open()) {
            m_db->delete_position(symbol);
        }

        // Remove from vector
        for (auto it = m_open_positions.begin(); it != m_open_positions.end(); ++it) {
            if (symbols_equal(it->symbol, symbol)) {
                m_open_positions.erase(it);
                break;
            }
        }
    } else {
        // Update in database
        if (m_db != nullptr && m_db->is_open()) {
            m_db->save_position(*pos);
        }

        if (m_position_callback) {
            m_position_callback(*pos);
        }
    }
}

void OrderManager::set_order_callback(OrderCallback cb) {
    MutexLock lock(m_mutex);
    m_order_callback = cb;
}

void OrderManager::set_position_callback(PositionCallback cb) {
    MutexLock lock(m_mutex);
    m_position_callback = cb;
}

void OrderManager::set_error_callback(OrderErrorCallback cb) {
    MutexLock lock(m_mutex);
    m_error_callback = cb;
}

void OrderManager::load_from_database() {
    // Orders come from TradeZero API (source of truth), not local database
    // But positions are persisted locally for session continuity
    if (m_db) {
        m_db->load_open_positions(m_open_positions);
        LOG_I("orders", "Loaded %zu positions from database", m_open_positions.size());
    }
}

void OrderManager::save_to_database() {
    // Orders come from TradeZero API (source of truth), not local database
    // But positions are persisted locally for session continuity
    if (!m_db) return;

    // Save all open positions
    for (const auto& pos : m_open_positions) {
        m_db->save_position(pos);
    }
    LOG_I("orders", "Saved %zu positions to database", m_open_positions.size());
}

// ============================================================================
// TradeZero Integration
// ============================================================================

void OrderManager::set_tradezero_client(TradeZeroClient* client) {
    m_tradezero_client = client;
}

Order* OrderManager::find_order_by_client_id(const char* client_order_id) {
    MutexLock lock(m_mutex);
    return find_order_by_client_id_locked(client_order_id);
}

// Internal helper - requires m_mutex to be held
Order* OrderManager::find_order_by_client_id_locked(const char* client_order_id) {
    if (client_order_id == nullptr) return nullptr;

    for (auto& order : m_pending_orders) {
        if (std::strcmp(order.client_order_id, client_order_id) == 0) {
            return &order;
        }
    }
    return nullptr;
}

void OrderManager::on_tradezero_order_update(const TZOrderUpdate& update) {
    MutexLock lock(m_mutex);

    bool was_rejected = false;  // Track if order was rejected (for error callback)
    int new_fills = 0;          // New fills since last update

    // Find order by client_order_id
    Order* order = find_order_by_client_id_locked(update.client_order_id);

    LOG_D("orders", "Looking for order with clientOrderId=%s, found=%s",
          update.client_order_id, order ? "yes" : "no");

    // If not found, look for a pending order with temporary PENDING_ prefix
    // This handles the case where WebSocket callback runs before buy()/sell()
    // has received the server's client_order_id
    if (order == nullptr) {
        OrderSide expected_side = (std::strcmp(update.side, "Buy") == 0) ? OrderSide::BUY : OrderSide::SELL;
        for (auto& o : m_pending_orders) {
            // Match on symbol, quantity, side, and pending status with temp ID
            if (std::strcmp(o.symbol, update.symbol) == 0 &&
                o.quantity == update.order_quantity &&
                o.side == expected_side &&
                o.status == OrderStatus::PENDING &&
                std::strncmp(o.client_order_id, "PENDING_", 8) == 0) {
                // Found matching pending order - update its client_order_id
                safe_strcpy(o.client_order_id, update.client_order_id, sizeof(o.client_order_id));
                order = &o;
                LOG_I("orders", "Matched pending order id=%lld to %s",
                      static_cast<long long>(o.id), update.client_order_id);
                break;
            }
        }
    }

    if (order == nullptr) {
        // Order not in local state (e.g., placed in another app)
        // Create new order from update
        Order new_order;
        new_order.id = m_next_order_id++;
        safe_strcpy(new_order.symbol, update.symbol, sizeof(new_order.symbol));
        safe_strcpy(new_order.client_order_id, update.client_order_id, sizeof(new_order.client_order_id));
        new_order.side = (std::strcmp(update.side, "Buy") == 0) ? OrderSide::BUY : OrderSide::SELL;
        new_order.quantity = update.order_quantity;
        new_order.executed = update.executed;
        new_order.canceled = update.canceled_quantity;
        new_order.leaves = update.leaves_quantity;
        new_order.price = update.limit_price;
        new_order.avg_price = update.price_avg;
        new_order.created_at = get_current_timestamp();

        // Parse order status (case-insensitive)
        std::string status_str(update.order_status);
        std::transform(status_str.begin(), status_str.end(), status_str.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // Trust API status completely
        if (status_str == "filled") {
            new_order.status = OrderStatus::FILLED;
        } else if (status_str == "partiallyfilled") {
            new_order.status = OrderStatus::PARTIAL;
        } else if (status_str == "canceled" || status_str == "cancelled") {
            new_order.status = OrderStatus::CANCELLED;
        } else if (status_str == "rejected") {
            new_order.status = OrderStatus::REJECTED;
            was_rejected = true;
            LOG_W("orders", "New order REJECTED: %s %s qty=%d text=%s",
                  update.side, update.symbol, update.order_quantity, update.text);
        } else {
            // Pending, Accepted, PendingNew, etc.
            new_order.status = OrderStatus::PENDING;
        }

        m_pending_orders.push_back(new_order);
        order = &m_pending_orders.back();

        // For new orders that already have fills, process them
        new_fills = update.executed;

        LOG_I("orders", "New order from TradeZero: %s %s qty=%d exec=%d leaves=%d status=%s",
              update.side, update.symbol, update.order_quantity,
              update.executed, update.leaves_quantity, update.order_status);
    } else {
        // Calculate new fills since last update
        new_fills = update.executed - order->executed;

        // Update order fields from API
        order->executed = update.executed;
        order->canceled = update.canceled_quantity;
        order->leaves = update.leaves_quantity;
        if (update.price_avg > 0) {
            order->avg_price = update.price_avg;
        }

        // Validate API invariant: quantity == executed + canceled + leaves
        int sum = order->executed + order->canceled + order->leaves;
        if (order->quantity != sum) {
            LOG_E("orders", "API invariant broken for order %s: total quantity (%d) should equal executed (%d) + canceled (%d) + leaves (%d) = %d",
                  update.symbol, order->quantity, order->executed, order->canceled, order->leaves, sum);
        }

        // Parse order status (case-insensitive)
        std::string status_str(update.order_status);
        std::transform(status_str.begin(), status_str.end(), status_str.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        // Trust API status completely
        if (status_str == "filled") {
            order->status = OrderStatus::FILLED;
        } else if (status_str == "partiallyfilled") {
            order->status = OrderStatus::PARTIAL;
        } else if (status_str == "canceled" || status_str == "cancelled") {
            order->status = OrderStatus::CANCELLED;
        } else if (status_str == "rejected") {
            order->status = OrderStatus::REJECTED;
            was_rejected = true;
            LOG_W("orders", "Order REJECTED: %s %s qty=%d text=%s",
                  update.side, update.symbol, update.order_quantity, update.text);
        } else {
            // Pending, Accepted, PendingNew, etc.
            order->status = OrderStatus::PENDING;
        }

        LOG_I("orders", "Order updated: id=%lld %s status=%s exec=%d leaves=%d canceled=%d new_fills=%d",
              static_cast<long long>(order->id), order->symbol,
              update.order_status, update.executed, update.leaves_quantity, update.canceled_quantity, new_fills);
    }

    // Process new fills into positions (for both PARTIAL and FILLED)
    if (new_fills > 0) {
        float fill_price = (update.price_avg > 0) ? update.price_avg : update.limit_price;

        LOG_I("orders", "Processing %d new fills for %s @ %.2f",
              new_fills, order->symbol, static_cast<double>(fill_price));

        if (order->side == OrderSide::BUY) {
            update_position_on_buy(order->symbol, new_fills, fill_price);
        } else {
            update_position_on_sell(order->symbol, new_fills, fill_price);
        }
    }

    // Copy order for callback (before potential erase that would invalidate pointer)
    Order order_copy = *order;

    // Remove from pending when order is complete
    if (order->status == OrderStatus::FILLED ||
        order->status == OrderStatus::CANCELLED ||
        order->status == OrderStatus::REJECTED) {

        // Add rejected orders to closed positions for visibility
        if (was_rejected || order->status == OrderStatus::REJECTED) {
            ClosedPosition rejected;
            safe_strcpy(rejected.symbol, order->symbol, sizeof(rejected.symbol));
            rejected.quantity = order->quantity;
            rejected.entry_price = order->price;
            rejected.exit_price = order->price;
            rejected.entry_time = order->created_at;
            rejected.exit_time = get_current_timestamp();
            rejected.status = ClosedPositionStatus::REJECTED;
            m_closed_positions.push_back(rejected);
        }

        // Remove from pending orders
        for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ++it) {
            if (std::strcmp(it->client_order_id, update.client_order_id) == 0) {
                m_pending_orders.erase(it);
                break;
            }
        }

        // Notify error callback for rejected orders
        if (was_rejected && m_error_callback) {
            const char* reason = (update.text[0] != '\0') ? update.text : "Order rejected";
            m_error_callback(update.symbol, reason);
        }
    }

    // Notify UI (use copy since order pointer may be invalid after erase)
    if (m_order_callback) {
        m_order_callback(order_copy);
    }
}

void OrderManager::on_tradezero_position_update(const TZPositionUpdate& update) {
    MutexLock lock(m_mutex);

    // Update local position state from Portfolio stream
    Position* pos = find_position_locked(update.symbol);

    int new_qty = static_cast<int>(update.shares);

    if (pos != nullptr) {
        pos->quantity = new_qty;
        pos->avg_price = update.price_avg;
        pos->current_price = update.price_close;

        LOG_I("positions", "Position updated: %s qty=%d avg=%.2f current=%.2f",
              update.symbol, new_qty, static_cast<double>(update.price_avg),
              static_cast<double>(update.price_close));

        // Update in database
        if (m_db != nullptr && m_db->is_open()) {
            m_db->save_position(*pos);
        }

        // Notify UI
        if (m_position_callback) {
            m_position_callback(*pos);
        }
    } else if (new_qty != 0) {
        // New position (e.g., from another app)
        Position new_pos;
        safe_strcpy(new_pos.symbol, update.symbol, sizeof(new_pos.symbol));
        new_pos.quantity = new_qty;
        new_pos.avg_price = update.price_avg;
        new_pos.current_price = update.price_close;
        m_open_positions.push_back(new_pos);

        LOG_I("positions", "New position from TradeZero: %s qty=%d avg=%.2f",
              update.symbol, new_qty, static_cast<double>(update.price_avg));

        // Save to database
        if (m_db != nullptr && m_db->is_open()) {
            m_db->save_position(new_pos);
        }

        // Notify UI
        if (m_position_callback) {
            m_position_callback(new_pos);
        }
    }
}

void OrderManager::on_tradezero_pnl_snapshot(const TZPnLSnapshot& snapshot) {
    MutexLock lock(m_mutex);

    // Initial position sync from P&L stream
    // Note: P&L stream has position-level P&L but not full position details
    // Full position details come from Portfolio stream or REST API

    LOG_I("tradezero", "P&L snapshot: account_value=%.2f day_pnl=%.2f positions=%zu",
          static_cast<double>(snapshot.account_value),
          static_cast<double>(snapshot.day_pnl),
          snapshot.positions.size());

    // We don't clear positions here because Portfolio stream provides more complete data
    // Just log the snapshot for now
    for (const auto& pos_pnl : snapshot.positions) {
        LOG_I("tradezero", "  Position P&L: %s unrealized=%.2f day_unrealized=%.2f",
              pos_pnl.symbol,
              static_cast<double>(pos_pnl.unrealized_pnl),
              static_cast<double>(pos_pnl.day_unrealized_pnl));
    }
}

std::vector<ClosedPosition> OrderManager::get_todays_closed_positions() const {
    MutexLock lock(m_mutex);
    // For now, return all closed positions
    // TODO: Filter to only today's trades when we have proper timestamps
    return m_closed_positions;  // Returns copy for thread safety
}

const char* OrderManager::last_error() const {
    return m_error;
}

void OrderManager::load_tradezero_positions(const std::vector<Position>& positions) {
    MutexLock lock(m_mutex);

    // Merge with existing positions (prefer TradeZero data)
    for (const auto& tz_pos : positions) {
        Position* existing = find_position_locked(tz_pos.symbol);
        if (existing != nullptr) {
            // Update existing position
            existing->quantity = tz_pos.quantity;
            existing->avg_price = tz_pos.avg_price;
            existing->current_price = tz_pos.current_price;

            LOG_I("tradezero", "Updated position from REST: %s qty=%d avg=%.2f",
                  tz_pos.symbol, tz_pos.quantity,
                  static_cast<double>(tz_pos.avg_price));
        } else {
            // Add new position
            m_open_positions.push_back(tz_pos);

            LOG_I("tradezero", "Loaded position from REST: %s qty=%d avg=%.2f",
                  tz_pos.symbol, tz_pos.quantity,
                  static_cast<double>(tz_pos.avg_price));
        }

        // Save to database
        if (m_db != nullptr && m_db->is_open()) {
            m_db->save_position(tz_pos);
        }
    }
}

void OrderManager::load_tradezero_orders(const std::vector<Order>& orders) {
    MutexLock lock(m_mutex);

    for (const auto& tz_order : orders) {
        if (tz_order.status == OrderStatus::PENDING || tz_order.status == OrderStatus::PARTIAL) {
            // Add to pending orders
            Order* existing = find_order_by_client_id_locked(tz_order.client_order_id);
            if (existing == nullptr) {
                m_pending_orders.push_back(tz_order);

                LOG_I("tradezero", "Loaded order from REST: %s %s qty=%d @ %.2f status=%c",
                      tz_order.side == OrderSide::BUY ? "BUY" : "SELL",
                      tz_order.symbol, tz_order.quantity,
                      static_cast<double>(tz_order.price),
                      static_cast<char>(tz_order.status));
            }
        } else if (tz_order.status == OrderStatus::REJECTED) {
            // Add rejected orders to closed positions for visibility
            ClosedPosition rejected;
            safe_strcpy(rejected.symbol, tz_order.symbol, sizeof(rejected.symbol));
            rejected.quantity = tz_order.quantity;
            rejected.entry_price = tz_order.price;
            rejected.exit_price = tz_order.price;  // No fill, so same price
            rejected.entry_time = tz_order.created_at;
            rejected.exit_time = tz_order.created_at;
            rejected.status = ClosedPositionStatus::REJECTED;

            m_closed_positions.push_back(rejected);

            LOG_I("tradezero", "Loaded rejected order from REST: %s %s qty=%d @ %.2f",
                  tz_order.side == OrderSide::BUY ? "BUY" : "SELL",
                  tz_order.symbol, tz_order.quantity,
                  static_cast<double>(tz_order.price));
        }
    }
}

void OrderManager::load_tradezero_executions(const std::vector<ClosedPosition>& executions) {
    MutexLock lock(m_mutex);

    // Load today's executions (closed trades) from TradeZero
    for (const auto& exec : executions) {
        // Check if we already have this execution (by symbol + exit_time)
        bool already_exists = false;
        for (const auto& existing : m_closed_positions) {
            if (symbols_equal(existing.symbol, exec.symbol) &&
                existing.exit_time == exec.exit_time &&
                existing.quantity == exec.quantity) {
                already_exists = true;
                break;
            }
        }

        if (!already_exists) {
            m_closed_positions.push_back(exec);

            LOG_I("tradezero", "Loaded execution from REST: %s qty=%d entry=%.2f exit=%.2f pnl=%.2f",
                  exec.symbol, exec.quantity,
                  static_cast<double>(exec.entry_price),
                  static_cast<double>(exec.exit_price),
                  static_cast<double>(exec.pnl_usd()));

            // Save to database
            if (m_db != nullptr && m_db->is_open()) {
                m_db->save_closed_position(exec);
            }
        }
    }

    LOG_I("tradezero", "Loaded %zu executions from TradeZero", executions.size());
}
