// order_manager.cpp - Order execution and position management implementation
#include "order_manager.h"
#include "tradezero_client.h"
#include "tradezero_websocket.h"
#include "logger.h"
#include <cstring>
#include <ctime>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// Global order manager instance
static OrderManager g_order_manager;

OrderManager& get_order_manager() {
    return g_order_manager;
}

OrderManager::OrderManager()
    : m_db(nullptr), m_market(nullptr), m_next_order_id(1) {
}

OrderManager::~OrderManager() {
}

void OrderManager::init(Database* db, MarketData* market) {
    m_db = db;
    m_market = market;
}

int64_t OrderManager::get_current_timestamp() const {
    return static_cast<int64_t>(std::time(nullptr));
}

int64_t OrderManager::buy(const char* symbol, int quantity, float price) {
    if (symbol == nullptr || symbol[0] == '\0' || quantity <= 0 || price <= 0) {
        return -1;
    }

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
        order.filled = 0;
        order.price = price;
        order.status = OrderStatus::PENDING;
        order.created_at = get_current_timestamp();

        m_pending_orders.push_back(order);
        int64_t local_order_id = order.id;

        // Now send REST request to broker
        TZResponse resp = m_tradezero_client->place_order(
            symbol, quantity, "buy", "limit", price, 0.0f
        );

        if (!resp.success) {
            LOG_E("orders", "TradeZero buy failed: %s", resp.error.c_str());
            // Remove the pending order we added
            for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ++it) {
                if (it->id == local_order_id) {
                    m_pending_orders.erase(it);
                    break;
                }
            }
            return -1;
        }

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

        LOG_I("orders", "BUY order placed via TradeZero: id=%lld %s qty=%d @ %.2f",
              static_cast<long long>(local_order_id), symbol, quantity, static_cast<double>(price));

        // Save to database
        if (m_db != nullptr && m_db->is_open()) {
            // Find the updated order to save
            for (const auto& o : m_pending_orders) {
                if (o.id == local_order_id) {
                    if (m_db->save_order(o) < 0) {
                        LOG_W("orders", "Failed to save order to database: %s", m_db->last_error());
                    }
                    break;
                }
            }
        }

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

int64_t OrderManager::sell(const char* symbol, int quantity, float price) {
    if (symbol == nullptr || symbol[0] == '\0' || quantity <= 0 || price <= 0) {
        return -1;
    }

    // Check if we have enough position to sell
    Position* pos = find_position(symbol);
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
        order.filled = 0;
        order.price = price;
        order.status = OrderStatus::PENDING;
        order.created_at = get_current_timestamp();

        m_pending_orders.push_back(order);
        int64_t local_order_id = order.id;

        // Now send REST request to broker
        TZResponse resp = m_tradezero_client->place_order(
            symbol, quantity, "sell", "limit", price, 0.0f
        );

        if (!resp.success) {
            LOG_E("orders", "TradeZero sell failed: %s", resp.error.c_str());
            // Remove the pending order we added
            for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ++it) {
                if (it->id == local_order_id) {
                    m_pending_orders.erase(it);
                    break;
                }
            }
            return -1;
        }

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

        // Save to database
        if (m_db != nullptr && m_db->is_open()) {
            for (const auto& o : m_pending_orders) {
                if (o.id == local_order_id) {
                    if (m_db->save_order(o) < 0) {
                        LOG_W("orders", "Failed to save order to database: %s", m_db->last_error());
                    }
                    break;
                }
            }
        }

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
    Order* order = find_order(order_id);
    if (order == nullptr) return false;

    // Cancel via TradeZero REST API
    if (m_tradezero_client != nullptr && m_tradezero_client->is_configured()) {
        TZResponse resp = m_tradezero_client->cancel_order(order->client_order_id);
        if (!resp.success) {
            LOG_E("orders", "TradeZero cancel failed: %s", resp.error.c_str());
            return false;
        }

        LOG_I("orders", "Order cancel requested via TradeZero: id=%lld %s",
              static_cast<long long>(order->id), order->symbol);

        // WebSocket will confirm cancellation
        // For now, keep order in pending state until WebSocket confirms
        return true;
    } else {
        LOG_E("orders", "TradeZero client not configured");
        return false;
    }
}

bool OrderManager::cancel_all_orders(const char* symbol) {
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

const std::vector<Order>& OrderManager::get_pending_orders() const {
    return m_pending_orders;
}

Order* OrderManager::find_order(int64_t order_id) {
    for (auto& order : m_pending_orders) {
        if (order.id == order_id) {
            return &order;
        }
    }
    return nullptr;
}

const std::vector<Position>& OrderManager::get_open_positions() const {
    return m_open_positions;
}

const std::vector<ClosedPosition>& OrderManager::get_closed_positions() const {
    return m_closed_positions;
}

Position* OrderManager::find_position(const char* symbol) {
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
    Position* pos = find_position(symbol);
    if (pos == nullptr || pos->quantity <= 0 || percentage <= 0) {
        return 0;
    }

    int qty = (pos->quantity * percentage) / 100;
    return (qty > 0) ? qty : 1;  // At least 1 share
}

void OrderManager::update_position_on_buy(const char* symbol, int quantity, float price) {
    Position* pos = find_position(symbol);

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
    Position* pos = find_position(symbol);
    if (pos == nullptr) return;

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
    m_order_callback = cb;
}

void OrderManager::set_position_callback(PositionCallback cb) {
    m_position_callback = cb;
}

void OrderManager::load_from_database() {
    if (m_db == nullptr || !m_db->is_open()) return;

    m_db->load_pending_orders(m_pending_orders);
    m_db->load_open_positions(m_open_positions);
    m_db->load_closed_positions(m_closed_positions);

    // Find max order ID
    m_next_order_id = 1;
    for (const auto& order : m_pending_orders) {
        if (order.id >= m_next_order_id) {
            m_next_order_id = order.id + 1;
        }
    }
}

void OrderManager::save_to_database() {
    if (m_db == nullptr || !m_db->is_open()) return;

    for (const auto& pos : m_open_positions) {
        m_db->save_position(pos);
    }
}

// ============================================================================
// TradeZero Integration
// ============================================================================

void OrderManager::set_tradezero_client(TradeZeroClient* client) {
    m_tradezero_client = client;
}

Order* OrderManager::find_order_by_client_id(const char* client_order_id) {
    if (client_order_id == nullptr) return nullptr;

    for (auto& order : m_pending_orders) {
        if (std::strcmp(order.client_order_id, client_order_id) == 0) {
            return &order;
        }
    }
    return nullptr;
}

void OrderManager::on_tradezero_order_update(const TZOrderUpdate& update) {
    // Find order by client_order_id
    Order* order = find_order_by_client_id(update.client_order_id);

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
        new_order.filled = update.executed;
        new_order.price = update.price_avg > 0 ? update.price_avg : update.limit_price;
        new_order.created_at = get_current_timestamp();

        // Parse order status
        if (std::strcmp(update.order_status, "Filled") == 0) {
            new_order.status = OrderStatus::FILLED;
        } else if (std::strcmp(update.order_status, "Canceled") == 0 ||
                   std::strcmp(update.order_status, "Cancelled") == 0) {
            // Accept both American (Canceled) and British (Cancelled) spelling
            new_order.status = OrderStatus::CANCELLED;
        } else if (update.executed > 0 && update.executed < update.order_quantity) {
            new_order.status = OrderStatus::PARTIAL;
        } else {
            new_order.status = OrderStatus::PENDING;
        }

        m_pending_orders.push_back(new_order);
        order = &m_pending_orders.back();

        LOG_I("orders", "New order from TradeZero: %s %s %d @ %.2f status=%s",
              update.side, update.symbol, update.order_quantity,
              static_cast<double>(update.limit_price), update.order_status);
    } else {
        // Update existing order
        order->filled = update.executed;
        order->price = update.price_avg > 0 ? update.price_avg : update.limit_price;

        // Parse order status
        if (std::strcmp(update.order_status, "Filled") == 0) {
            order->status = OrderStatus::FILLED;
        } else if (std::strcmp(update.order_status, "Canceled") == 0 ||
                   std::strcmp(update.order_status, "Cancelled") == 0) {
            // Accept both American (Canceled) and British (Cancelled) spelling
            order->status = OrderStatus::CANCELLED;
        } else if (update.executed > 0 && update.executed < update.order_quantity) {
            order->status = OrderStatus::PARTIAL;
        } else {
            order->status = OrderStatus::PENDING;
        }

        LOG_I("orders", "Order updated: id=%lld %s status=%s filled=%d/%d",
              static_cast<long long>(order->id), order->symbol,
              update.order_status, order->filled, order->quantity);
    }

    // Copy order for callback (before potential erase that would invalidate pointer)
    Order order_copy = *order;

    // Handle filled orders
    if (order->status == OrderStatus::FILLED) {
        int fill_qty = order->quantity;
        float fill_price = order->price;

        if (order->side == OrderSide::BUY) {
            update_position_on_buy(order->symbol, fill_qty, fill_price);
        } else {
            update_position_on_sell(order->symbol, fill_qty, fill_price);
        }

        // Remove from pending orders
        for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ++it) {
            if (std::strcmp(it->client_order_id, update.client_order_id) == 0) {
                m_pending_orders.erase(it);
                break;
            }
        }
    } else if (order->status == OrderStatus::CANCELLED) {
        // Remove cancelled order from pending
        for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ++it) {
            if (std::strcmp(it->client_order_id, update.client_order_id) == 0) {
                m_pending_orders.erase(it);
                break;
            }
        }
    }

    // Notify UI (use copy since order pointer may be invalid after erase)
    if (m_order_callback) {
        m_order_callback(order_copy);
    }
}

void OrderManager::on_tradezero_position_update(const TZPositionUpdate& update) {
    // Update local position state from Portfolio stream
    Position* pos = find_position(update.symbol);

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

const std::vector<ClosedPosition>& OrderManager::get_todays_closed_positions() const {
    // For now, return all closed positions
    // TODO: Filter to only today's trades when we have proper timestamps
    return m_closed_positions;
}

void OrderManager::load_tradezero_positions(const std::vector<Position>& positions) {
    // Merge with existing positions (prefer TradeZero data)
    for (const auto& tz_pos : positions) {
        Position* existing = find_position(tz_pos.symbol);
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
    // Load pending/active orders from TradeZero
    for (const auto& tz_order : orders) {
        // Only load orders that aren't filled or cancelled
        if (tz_order.status == OrderStatus::PENDING || tz_order.status == OrderStatus::PARTIAL) {
            // Check if we already have this order
            Order* existing = find_order_by_client_id(tz_order.client_order_id);
            if (existing == nullptr) {
                // Add new order
                m_pending_orders.push_back(tz_order);

                LOG_I("tradezero", "Loaded order from REST: %s %s qty=%d @ %.2f status=%c",
                      tz_order.side == OrderSide::BUY ? "BUY" : "SELL",
                      tz_order.symbol, tz_order.quantity,
                      static_cast<double>(tz_order.price),
                      static_cast<char>(tz_order.status));

                // Save to database
                if (m_db != nullptr && m_db->is_open()) {
                    if (m_db->save_order(tz_order) < 0) {
                        LOG_W("orders", "Failed to save order to database: %s", m_db->last_error());
                    }
                }
            }
        }
    }
}
