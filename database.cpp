// database.cpp - SQLite database implementation
#include "database.h"
#include <sqlite3.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Global database instance
static Database g_database;

Database& get_database() {
    return g_database;
}

Database::Database() : m_db(nullptr) {
    m_error[0] = '\0';
}

Database::~Database() {
    close();
}

bool Database::init(const char* db_path) {
    if (m_db != nullptr) {
        close();
    }

    int rc = sqlite3_open(db_path, reinterpret_cast<sqlite3**>(&m_db));
    if (rc != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Cannot open database: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        sqlite3_close(static_cast<sqlite3*>(m_db));
        m_db = nullptr;
        return false;
    }

    // Enable foreign keys
    execute("PRAGMA foreign_keys = ON");

    return create_tables();
}

void Database::close() {
    if (m_db != nullptr) {
        sqlite3_close(static_cast<sqlite3*>(m_db));
        m_db = nullptr;
    }
}

bool Database::is_open() const {
    return m_db != nullptr;
}

const char* Database::last_error() const {
    return m_error;
}

bool Database::execute(const char* sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(static_cast<sqlite3*>(m_db), sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "SQL error: %s", err_msg);
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool Database::create_tables() {
    // Session tickers table
    const char* sql_tickers = R"(
        CREATE TABLE IF NOT EXISTS session_tickers (
            slot INTEGER PRIMARY KEY,
            symbol TEXT NOT NULL
        )
    )";

    // Orders table
    const char* sql_orders = R"(
        CREATE TABLE IF NOT EXISTS orders (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            symbol TEXT NOT NULL,
            side TEXT NOT NULL,
            quantity INTEGER NOT NULL,
            filled INTEGER NOT NULL DEFAULT 0,
            price REAL NOT NULL,
            status TEXT NOT NULL,
            created_at INTEGER NOT NULL
        )
    )";

    // Positions table
    const char* sql_positions = R"(
        CREATE TABLE IF NOT EXISTS positions (
            symbol TEXT PRIMARY KEY,
            quantity INTEGER NOT NULL,
            avg_price REAL NOT NULL
        )
    )";

    // Closed positions table
    const char* sql_closed = R"(
        CREATE TABLE IF NOT EXISTS closed_positions (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            symbol TEXT NOT NULL,
            quantity INTEGER NOT NULL,
            entry_price REAL NOT NULL,
            exit_price REAL NOT NULL,
            entry_time INTEGER NOT NULL,
            exit_time INTEGER NOT NULL
        )
    )";

    // Horizontal lines table
    const char* sql_hlines = R"(
        CREATE TABLE IF NOT EXISTS hlines (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            symbol TEXT NOT NULL,
            price REAL NOT NULL,
            color INTEGER NOT NULL,
            style INTEGER NOT NULL
        )
    )";

    // Trend lines table
    const char* sql_trendlines = R"(
        CREATE TABLE IF NOT EXISTS trendlines (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            symbol TEXT NOT NULL,
            candle_start INTEGER NOT NULL,
            candle_end INTEGER NOT NULL,
            price_start REAL NOT NULL,
            price_end REAL NOT NULL,
            color INTEGER NOT NULL,
            style INTEGER NOT NULL
        )
    )";

    // Indicator settings table
    const char* sql_indicators = R"(
        CREATE TABLE IF NOT EXISTS indicator_settings (
            symbol TEXT PRIMARY KEY,
            sma_enabled INTEGER NOT NULL DEFAULT 0,
            sma_period INTEGER NOT NULL DEFAULT 20,
            ema_enabled INTEGER NOT NULL DEFAULT 0,
            ema_period INTEGER NOT NULL DEFAULT 9,
            boll_enabled INTEGER NOT NULL DEFAULT 0,
            boll_period INTEGER NOT NULL DEFAULT 20,
            volume_enabled INTEGER NOT NULL DEFAULT 1
        )
    )";

    return execute(sql_tickers) &&
           execute(sql_orders) &&
           execute(sql_positions) &&
           execute(sql_closed) &&
           execute(sql_hlines) &&
           execute(sql_trendlines) &&
           execute(sql_indicators);
}

// ============================================================================
// Session Tickers
// ============================================================================

bool Database::save_tickers(const char* symbols[NUM_TICKERS]) {
    if (m_db == nullptr) return false;

    // Clear existing
    if (!execute("DELETE FROM session_tickers")) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO session_tickers (slot, symbol) VALUES (?, ?)";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    bool success = true;
    for (int i = 0; i < NUM_TICKERS; i++) {
        if (symbols[i] == nullptr || symbols[i][0] == '\0') continue;

        sqlite3_reset(stmt);
        sqlite3_bind_int(stmt, 1, i);
        sqlite3_bind_text(stmt, 2, symbols[i], -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::snprintf(m_error, sizeof(m_error), "Insert failed: %s",
                         sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return success;
}

bool Database::load_tickers(char symbols[NUM_TICKERS][8]) {
    if (m_db == nullptr) return false;

    // Initialize to empty
    for (int i = 0; i < NUM_TICKERS; i++) {
        symbols[i][0] = '\0';
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT slot, symbol FROM session_tickers ORDER BY slot";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int slot = sqlite3_column_int(stmt, 0);
        const char* sym = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (slot >= 0 && slot < NUM_TICKERS && sym != nullptr) {
            safe_strcpy(symbols[slot], sym, 8);
        }
    }

    sqlite3_finalize(stmt);
    return true;
}

// ============================================================================
// Orders
// ============================================================================

int64_t Database::save_order(const Order& order) {
    if (m_db == nullptr) return -1;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO orders (symbol, side, quantity, filled, price, status, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)
    )";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return -1;
    }

    char side_str[2] = {static_cast<char>(order.side), '\0'};
    char status_str[2] = {static_cast<char>(order.status), '\0'};

    sqlite3_bind_text(stmt, 1, order.symbol, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, side_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, order.quantity);
    sqlite3_bind_int(stmt, 4, order.filled);
    sqlite3_bind_double(stmt, 5, static_cast<double>(order.price));
    sqlite3_bind_text(stmt, 6, status_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, order.created_at);

    int64_t result = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        result = sqlite3_last_insert_rowid(static_cast<sqlite3*>(m_db));
    } else {
        std::snprintf(m_error, sizeof(m_error), "Insert failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool Database::update_order(const Order& order) {
    if (m_db == nullptr) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE orders SET filled = ?, status = ? WHERE id = ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    char status_str[2] = {static_cast<char>(order.status), '\0'};

    sqlite3_bind_int(stmt, 1, order.filled);
    sqlite3_bind_text(stmt, 2, status_str, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, order.id);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) {
        std::snprintf(m_error, sizeof(m_error), "Update failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
    }

    sqlite3_finalize(stmt);
    return success;
}

bool Database::load_pending_orders(std::vector<Order>& orders) {
    if (m_db == nullptr) return false;

    orders.clear();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, symbol, side, quantity, filled, price, status, created_at "
                      "FROM orders WHERE status IN ('P', 'A') ORDER BY created_at DESC";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Order order;
        order.id = sqlite3_column_int64(stmt, 0);
        safe_strcpy(order.symbol, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)), sizeof(order.symbol));
        const char* side = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        order.side = (side && side[0] == 'S') ? SIDE_SELL : SIDE_BUY;
        order.quantity = sqlite3_column_int(stmt, 3);
        order.filled = sqlite3_column_int(stmt, 4);
        order.price = static_cast<float>(sqlite3_column_double(stmt, 5));
        const char* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (status) {
            switch (status[0]) {
                case 'F': order.status = STATUS_FILLED; break;
                case 'A': order.status = STATUS_PARTIAL; break;
                case 'X': order.status = STATUS_CANCELLED; break;
                default: order.status = STATUS_PENDING; break;
            }
        }
        order.created_at = sqlite3_column_int64(stmt, 7);
        orders.push_back(order);
    }

    sqlite3_finalize(stmt);
    return true;
}

bool Database::load_order_history(std::vector<Order>& orders, int limit) {
    if (m_db == nullptr) return false;

    orders.clear();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT id, symbol, side, quantity, filled, price, status, created_at "
                      "FROM orders ORDER BY created_at DESC LIMIT ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Order order;
        order.id = sqlite3_column_int64(stmt, 0);
        safe_strcpy(order.symbol, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)), sizeof(order.symbol));
        const char* side = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        order.side = (side && side[0] == 'S') ? SIDE_SELL : SIDE_BUY;
        order.quantity = sqlite3_column_int(stmt, 3);
        order.filled = sqlite3_column_int(stmt, 4);
        order.price = static_cast<float>(sqlite3_column_double(stmt, 5));
        const char* status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (status) {
            switch (status[0]) {
                case 'F': order.status = STATUS_FILLED; break;
                case 'A': order.status = STATUS_PARTIAL; break;
                case 'X': order.status = STATUS_CANCELLED; break;
                default: order.status = STATUS_PENDING; break;
            }
        }
        order.created_at = sqlite3_column_int64(stmt, 7);
        orders.push_back(order);
    }

    sqlite3_finalize(stmt);
    return true;
}

// ============================================================================
// Positions
// ============================================================================

bool Database::save_position(const Position& pos) {
    if (m_db == nullptr) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR REPLACE INTO positions (symbol, quantity, avg_price)
        VALUES (?, ?, ?)
    )";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, pos.symbol, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, pos.quantity);
    sqlite3_bind_double(stmt, 3, static_cast<double>(pos.avg_price));

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) {
        std::snprintf(m_error, sizeof(m_error), "Insert failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
    }

    sqlite3_finalize(stmt);
    return success;
}

bool Database::update_position(const Position& pos) {
    return save_position(pos);  // Same as save with INSERT OR REPLACE
}

bool Database::delete_position(const char* symbol) {
    if (m_db == nullptr) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM positions WHERE symbol = ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, symbol, -1, SQLITE_TRANSIENT);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return success;
}

bool Database::load_open_positions(std::vector<Position>& positions) {
    if (m_db == nullptr) return false;

    positions.clear();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT symbol, quantity, avg_price FROM positions ORDER BY symbol";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Position pos;
        safe_strcpy(pos.symbol, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), sizeof(pos.symbol));
        pos.quantity = sqlite3_column_int(stmt, 1);
        pos.avg_price = static_cast<float>(sqlite3_column_double(stmt, 2));
        positions.push_back(pos);
    }

    sqlite3_finalize(stmt);
    return true;
}

// ============================================================================
// Closed Positions
// ============================================================================

bool Database::save_closed_position(const ClosedPosition& pos) {
    if (m_db == nullptr) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO closed_positions (symbol, quantity, entry_price, exit_price, entry_time, exit_time)
        VALUES (?, ?, ?, ?, ?, ?)
    )";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, pos.symbol, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, pos.quantity);
    sqlite3_bind_double(stmt, 3, static_cast<double>(pos.entry_price));
    sqlite3_bind_double(stmt, 4, static_cast<double>(pos.exit_price));
    sqlite3_bind_int64(stmt, 5, pos.entry_time);
    sqlite3_bind_int64(stmt, 6, pos.exit_time);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) {
        std::snprintf(m_error, sizeof(m_error), "Insert failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
    }

    sqlite3_finalize(stmt);
    return success;
}

bool Database::load_closed_positions(std::vector<ClosedPosition>& positions, int limit) {
    if (m_db == nullptr) return false;

    positions.clear();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT symbol, quantity, entry_price, exit_price, entry_time, exit_time "
                      "FROM closed_positions ORDER BY exit_time DESC LIMIT ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ClosedPosition pos;
        safe_strcpy(pos.symbol, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)), sizeof(pos.symbol));
        pos.quantity = sqlite3_column_int(stmt, 1);
        pos.entry_price = static_cast<float>(sqlite3_column_double(stmt, 2));
        pos.exit_price = static_cast<float>(sqlite3_column_double(stmt, 3));
        pos.entry_time = sqlite3_column_int64(stmt, 4);
        pos.exit_time = sqlite3_column_int64(stmt, 5);
        positions.push_back(pos);
    }

    sqlite3_finalize(stmt);
    return true;
}

// ============================================================================
// Chart Drawings
// ============================================================================

bool Database::save_hlines(const char* symbol, const std::vector<HLine>& lines) {
    if (m_db == nullptr) return false;

    // Delete existing lines for this symbol
    sqlite3_stmt* del_stmt = nullptr;
    sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), "DELETE FROM hlines WHERE symbol = ?", -1, &del_stmt, nullptr);
    sqlite3_bind_text(del_stmt, 1, symbol, -1, SQLITE_TRANSIENT);
    sqlite3_step(del_stmt);
    sqlite3_finalize(del_stmt);

    if (lines.empty()) return true;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO hlines (symbol, price, color, style) VALUES (?, ?, ?, ?)";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    bool success = true;
    for (const auto& line : lines) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, symbol, -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, static_cast<double>(line.price));
        sqlite3_bind_int(stmt, 3, static_cast<int>(line.color));
        sqlite3_bind_int(stmt, 4, static_cast<int>(line.style));

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return success;
}

bool Database::load_hlines(const char* symbol, std::vector<HLine>& lines) {
    if (m_db == nullptr) return false;

    lines.clear();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT price, color, style FROM hlines WHERE symbol = ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, symbol, -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        HLine line;
        line.price = static_cast<float>(sqlite3_column_double(stmt, 0));
        line.color = static_cast<ImU32>(sqlite3_column_int(stmt, 1));
        line.style = static_cast<LineStyle>(sqlite3_column_int(stmt, 2));
        line.selected = false;
        lines.push_back(line);
    }

    sqlite3_finalize(stmt);
    return true;
}

bool Database::save_trendlines(const char* symbol, const std::vector<TrendLine>& lines) {
    if (m_db == nullptr) return false;

    // Delete existing lines for this symbol
    sqlite3_stmt* del_stmt = nullptr;
    sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), "DELETE FROM trendlines WHERE symbol = ?", -1, &del_stmt, nullptr);
    sqlite3_bind_text(del_stmt, 1, symbol, -1, SQLITE_TRANSIENT);
    sqlite3_step(del_stmt);
    sqlite3_finalize(del_stmt);

    if (lines.empty()) return true;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO trendlines (symbol, candle_start, candle_end, price_start, price_end, color, style) "
                      "VALUES (?, ?, ?, ?, ?, ?, ?)";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    bool success = true;
    for (const auto& line : lines) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, symbol, -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, line.candle_start);
        sqlite3_bind_int(stmt, 3, line.candle_end);
        sqlite3_bind_double(stmt, 4, static_cast<double>(line.price_start));
        sqlite3_bind_double(stmt, 5, static_cast<double>(line.price_end));
        sqlite3_bind_int(stmt, 6, static_cast<int>(line.color));
        sqlite3_bind_int(stmt, 7, static_cast<int>(line.style));

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            success = false;
            break;
        }
    }

    sqlite3_finalize(stmt);
    return success;
}

bool Database::load_trendlines(const char* symbol, std::vector<TrendLine>& lines) {
    if (m_db == nullptr) return false;

    lines.clear();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT candle_start, candle_end, price_start, price_end, color, style FROM trendlines WHERE symbol = ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, symbol, -1, SQLITE_TRANSIENT);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        TrendLine line;
        line.candle_start = sqlite3_column_int(stmt, 0);
        line.candle_end = sqlite3_column_int(stmt, 1);
        line.price_start = static_cast<float>(sqlite3_column_double(stmt, 2));
        line.price_end = static_cast<float>(sqlite3_column_double(stmt, 3));
        line.color = static_cast<ImU32>(sqlite3_column_int(stmt, 4));
        line.style = static_cast<LineStyle>(sqlite3_column_int(stmt, 5));
        line.selected = false;
        lines.push_back(line);
    }

    sqlite3_finalize(stmt);
    return true;
}

// ============================================================================
// Indicator Settings
// ============================================================================

bool Database::save_indicator_settings(const char* symbol, const IndicatorSettings& settings) {
    if (m_db == nullptr) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR REPLACE INTO indicator_settings
        (symbol, sma_enabled, sma_period, ema_enabled, ema_period, boll_enabled, boll_period, volume_enabled)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, symbol, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, settings.sma_enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 3, settings.sma_period);
    sqlite3_bind_int(stmt, 4, settings.ema_enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 5, settings.ema_period);
    sqlite3_bind_int(stmt, 6, settings.boll_enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 7, settings.boll_period);
    sqlite3_bind_int(stmt, 8, settings.volume_enabled ? 1 : 0);

    bool success = (sqlite3_step(stmt) == SQLITE_DONE);
    if (!success) {
        std::snprintf(m_error, sizeof(m_error), "Insert failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
    }

    sqlite3_finalize(stmt);
    return success;
}

bool Database::load_indicator_settings(const char* symbol, IndicatorSettings& settings) {
    if (m_db == nullptr) return false;

    // Set defaults
    settings = IndicatorSettings();

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT sma_enabled, sma_period, ema_enabled, ema_period, boll_enabled, boll_period, volume_enabled "
                      "FROM indicator_settings WHERE symbol = ?";

    if (sqlite3_prepare_v2(static_cast<sqlite3*>(m_db), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::snprintf(m_error, sizeof(m_error), "Prepare failed: %s",
                     sqlite3_errmsg(static_cast<sqlite3*>(m_db)));
        return false;
    }

    sqlite3_bind_text(stmt, 1, symbol, -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        settings.sma_enabled = sqlite3_column_int(stmt, 0) != 0;
        settings.sma_period = sqlite3_column_int(stmt, 1);
        settings.ema_enabled = sqlite3_column_int(stmt, 2) != 0;
        settings.ema_period = sqlite3_column_int(stmt, 3);
        settings.boll_enabled = sqlite3_column_int(stmt, 4) != 0;
        settings.boll_period = sqlite3_column_int(stmt, 5);
        settings.volume_enabled = sqlite3_column_int(stmt, 6) != 0;
    }

    sqlite3_finalize(stmt);
    return true;
}
