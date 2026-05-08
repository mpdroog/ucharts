// market_data.h - Market data loading and simulation
#ifndef MARKET_DATA_H
#define MARKET_DATA_H

#include "types.h"
#include <string>
#include <vector>
#include <map>

// Market data manager for loading and streaming simulation data
class MarketData {
public:
    MarketData();
    ~MarketData();

    // Set the data directory
    void set_data_dir(const char* dir);

    // Check if data exists for a symbol
    bool has_symbol(const char* symbol) const;

    // Load all data for a symbol
    bool load_symbol(const char* symbol);

    // Unload symbol data
    void unload_symbol(const char* symbol);

    // Get current Level 2 book (aggregated, 10 levels per side)
    bool get_level2(const char* symbol, std::vector<Level2Entry>& bids,
                    std::vector<Level2Entry>& asks, float& best_bid, float& best_ask);

    // Get recent Time & Sales entries
    bool get_time_sales(const char* symbol, std::vector<TimeSalesEntry>& entries, int count = TIME_SALES_ROWS);

    // Get candle data for a timeframe
    bool get_candles(const char* symbol, Timeframe tf, std::vector<Candle>& candles, int limit = MAX_CANDLES);

    // Get current price (last trade price or mid of bid/ask)
    float get_current_price(const char* symbol) const;

    // Simulation control
    void start_simulation();
    void stop_simulation();
    void step_simulation();  // Advance one tick
    bool is_running() const;

    // Get current simulation timestamp
    const char* get_current_time() const;
    int64_t get_current_timestamp() const;

    // Get last error
    const char* last_error() const;

private:
    // Per-symbol data storage
    struct SymbolData {
        std::vector<Level2Entry> level2_bids;
        std::vector<Level2Entry> level2_asks;
        std::vector<TimeSalesEntry> time_sales;
        std::vector<Candle> candles_1m;
        std::vector<Candle> candles_5m;
        std::vector<Candle> candles_daily;
        float current_price;
        bool loaded;

        SymbolData() : current_price(0), loaded(false) {}
    };

    std::string m_data_dir;
    std::map<std::string, SymbolData> m_symbols;
    bool m_running;
    size_t m_sim_index;
    char m_current_time[16];
    int64_t m_current_timestamp;
    char m_error[256];

    // Parsers
    bool parse_level2_csv(const char* filepath, const char* symbol);
    bool parse_timesales_csv(const char* filepath, const char* symbol);
    bool parse_candles_csv(const char* filepath, std::vector<Candle>& candles);

    // Level 2 color assignment
    static ImU32 get_level_color(int level_index, bool is_bid);
};

// Global market data instance
MarketData& get_market_data();

#endif // MARKET_DATA_H
