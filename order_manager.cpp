// order_manager.cpp - Order execution and position management implementation
#include "order_manager.h"
#include <cstring>
#include <ctime>
#include <algorithm>

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

    Order order;
    order.id = m_next_order_id++;
    safe_strcpy(order.symbol, symbol, sizeof(order.symbol));
    order.side = SIDE_BUY;
    order.quantity = quantity;
    order.filled = 0;
    order.price = price;
    order.status = STATUS_PENDING;
    order.created_at = get_current_timestamp();

    m_pending_orders.push_back(order);

    // Save to database
    if (m_db != nullptr && m_db->is_open()) {
        m_db->save_order(order);
    }

    if (m_order_callback) {
        m_order_callback(order);
    }

    return order.id;
}

int64_t OrderManager::sell(const char* symbol, int quantity, float price) {
    if (symbol == nullptr || symbol[0] == '\0' || quantity <= 0 || price <= 0) {
        return -1;
    }

    // Check if we have enough position to sell
    Position* pos = find_position(symbol);
    if (pos == nullptr || pos->quantity < quantity) {
        return -1;  // Not enough shares to sell
    }

    Order order;
    order.id = m_next_order_id++;
    safe_strcpy(order.symbol, symbol, sizeof(order.symbol));
    order.side = SIDE_SELL;
    order.quantity = quantity;
    order.filled = 0;
    order.price = price;
    order.status = STATUS_PENDING;
    order.created_at = get_current_timestamp();

    m_pending_orders.push_back(order);

    // Save to database
    if (m_db != nullptr && m_db->is_open()) {
        m_db->save_order(order);
    }

    if (m_order_callback) {
        m_order_callback(order);
    }

    return order.id;
}

bool OrderManager::cancel_order(int64_t order_id) {
    for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ++it) {
        if (it->id == order_id) {
            it->status = STATUS_CANCELLED;

            // Update in database
            if (m_db != nullptr && m_db->is_open()) {
                m_db->update_order(*it);
            }

            if (m_order_callback) {
                m_order_callback(*it);
            }

            m_pending_orders.erase(it);
            return true;
        }
    }
    return false;
}

bool OrderManager::cancel_all_orders(const char* symbol) {
    bool cancelled_any = false;

    for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ) {
        if (symbol == nullptr || symbols_equal(it->symbol, symbol)) {
            it->status = STATUS_CANCELLED;

            // Update in database
            if (m_db != nullptr && m_db->is_open()) {
                m_db->update_order(*it);
            }

            if (m_order_callback) {
                m_order_callback(*it);
            }

            it = m_pending_orders.erase(it);
            cancelled_any = true;
        } else {
            ++it;
        }
    }

    return cancelled_any;
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

void OrderManager::process_fills() {
    if (m_market == nullptr) return;

    for (auto it = m_pending_orders.begin(); it != m_pending_orders.end(); ) {
        Order& order = *it;
        bool filled = false;

        // Get current bid/ask
        std::vector<Level2Entry> bids, asks;
        float best_bid = 0, best_ask = 0;
        m_market->get_level2(order.symbol, bids, asks, best_bid, best_ask);

        if (order.side == SIDE_BUY) {
            // Buy order fills if our price >= best ask
            if (best_ask > 0 && order.price >= best_ask) {
                // Calculate fill quantity (simplified - fill entirely)
                int fill_qty = order.quantity - order.filled;
                order.filled = order.quantity;
                order.status = STATUS_FILLED;

                // Update position
                update_position_on_buy(order.symbol, fill_qty, order.price);
                filled = true;
            }
        } else {
            // Sell order fills if our price <= best bid
            if (best_bid > 0 && order.price <= best_bid) {
                int fill_qty = order.quantity - order.filled;
                order.filled = order.quantity;
                order.status = STATUS_FILLED;

                // Update position
                update_position_on_sell(order.symbol, fill_qty, order.price);
                filled = true;
            }
        }

        if (filled) {
            // Update in database
            if (m_db != nullptr && m_db->is_open()) {
                m_db->update_order(order);
            }

            if (m_order_callback) {
                m_order_callback(order);
            }

            it = m_pending_orders.erase(it);
        } else {
            ++it;
        }
    }
}

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
