// database.h - SQLite database wrapper for persistence
#ifndef DATABASE_H
#define DATABASE_H

#include "types.h"
#include <vector>
#include <string>

// Database class for managing persistent storage
class Database {
public:
    Database();
    ~Database();

    // Initialize database (create tables if needed)
    bool init(const char* db_path = "ucharts.db");
    void close();
    bool is_open() const;

    // Session tickers (remember last 4 symbols)
    bool save_tickers(const char* symbols[NUM_TICKERS]);
    bool load_tickers(char symbols[NUM_TICKERS][8]);

    // Orders
    int64_t save_order(const Order& order);
    bool update_order(const Order& order);
    bool load_pending_orders(std::vector<Order>& orders);
    bool load_order_history(std::vector<Order>& orders, int limit = 100);

    // Positions
    bool save_position(const Position& pos);
    bool update_position(const Position& pos);
    bool delete_position(const char* symbol);
    bool load_open_positions(std::vector<Position>& positions);

    // Closed positions
    bool save_closed_position(const ClosedPosition& pos);
    bool load_closed_positions(std::vector<ClosedPosition>& positions, int limit = 100);

    // Chart drawings (per symbol)
    bool save_hlines(const char* symbol, const std::vector<HLine>& lines);
    bool load_hlines(const char* symbol, std::vector<HLine>& lines);
    bool save_trendlines(const char* symbol, const std::vector<TrendLine>& lines);
    bool load_trendlines(const char* symbol, std::vector<TrendLine>& lines);

    // Indicator settings (per symbol)
    bool save_indicator_settings(const char* symbol, const IndicatorSettings& settings);
    bool load_indicator_settings(const char* symbol, IndicatorSettings& settings);

    // Get last error message
    const char* last_error() const;

private:
    void* m_db;  // sqlite3* pointer (void* to avoid header dependency)
    char m_error[256];

    bool execute(const char* sql);
    bool create_tables();
};

// Global database instance
Database& get_database();

#endif // DATABASE_H
