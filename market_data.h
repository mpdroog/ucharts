// market_data.h - Market data loading and simulation
#ifndef MARKET_DATA_H
#define MARKET_DATA_H

#include "types.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>

// Data source mode
enum class DataSourceMode {
    FILE = 0,          // Load from CSV files (original behavior)
    IQFEED_HTTP = 1,   // Fetch from iqfeed HTTP API (deprecated)
    IQFEED = 2         // Fetch from iqfeed TCP (faster)
};


// Market data manager for loading and streaming simulation data
// SINGLETON: Use get_market_data() to access the instance
class MarketData {
public:
    // Loading state for async operations
    enum class LoadingState {
        IDLE = 0,
        PENDING,      // Request queued
        COMPLETE,     // All data loaded
        ERROR         // Loading failed
    };

    ~MarketData();

    // Non-copyable, non-movable (singleton)
    MarketData(const MarketData&) = delete;
    MarketData& operator=(const MarketData&) = delete;
    MarketData(MarketData&&) = delete;
    MarketData& operator=(MarketData&&) = delete;

#ifdef MARKET_DATA_TEST_MODE
public:
    // Public constructor for testing only
    MarketData();
#else
private:
    // Private constructor - use get_market_data() instead
    MarketData();
    friend MarketData& get_market_data();
#endif

public:

    // Set the data directory (for file-based loading)
    void set_data_dir(const char* dir);

    // Set data source mode (file or iqfeed API)
    void set_data_source(DataSourceMode mode);

    // Set iqfeed API base URL (e.g., "http://localhost:8080") - for HTTP mode
    void set_api_url(const char* url);

    // Set iqfeed TCP host (default: "127.0.0.1")
    void set_tcp_host(const char* host);

    // Connect to iqfeed TCP streams (L1 and L2)
    [[nodiscard]] bool connect_streams();
    void disconnect_streams();
    [[nodiscard]] bool streams_connected() const;

    // Subscribe to L1/L2 updates for a symbol
    [[nodiscard]] bool subscribe_quotes(const char* symbol);
    [[nodiscard]] bool unsubscribe_quotes(const char* symbol);

    // Update L1/L2 data from TCP streams (call periodically from main loop)
    void update_from_streams();

    // Check if data exists for a symbol
    [[nodiscard]] bool has_symbol(const char* symbol) const;

    // Load all data for a symbol (async - returns immediately)
    // Returns true if loading started, false if already loaded/loading
    [[nodiscard]] bool load_symbol(const char* symbol);

    // Check if a symbol is currently loading
    [[nodiscard]] bool is_loading(const char* symbol) const;

    // Get loading state for a symbol
    [[nodiscard]] LoadingState get_loading_state(const char* symbol) const;

    // Get loading error message (if state is LoadingState::ERROR)
    [[nodiscard]] const char* get_loading_error(const char* symbol) const;

    // Unload symbol data
    void unload_symbol(const char* symbol);

    // Force reload symbol data (clears cache and re-fetches)
    [[nodiscard]] bool reload_symbol(const char* symbol);

    // Check if a specific timeframe has data
    [[nodiscard]] bool has_timeframe_data(const char* symbol, Timeframe tf) const;

    // Get current Level 2 book (aggregated, 10 levels per side)
    [[nodiscard]] bool get_level2(const char* symbol, std::vector<Level2Entry>& bids,
                    std::vector<Level2Entry>& asks, float& best_bid, float& best_ask);

    // Get recent Time & Sales entries
    [[nodiscard]] bool get_time_sales(const char* symbol, std::vector<TimeSalesEntry>& entries, int count = TIME_SALES_ROWS);

    // Get candle data for a timeframe
    [[nodiscard]] bool get_candles(const char* symbol, Timeframe tf, std::vector<Candle>& candles, int limit = MAX_CANDLES);

    // Get current price (last trade price or mid of bid/ask)
    [[nodiscard]] float get_current_price(const char* symbol) const;

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
        LoadingState loading_state;
        int pending_requests;   // Number of async requests still pending
        char load_error[128];   // Error message if loading failed

        SymbolData() : current_price(0), loaded(false), loading_state(LoadingState::IDLE), pending_requests(0) {
            load_error[0] = '\0';
        }
    };

    std::string m_data_dir;
    std::string m_api_url;
    std::string m_tcp_host;
    DataSourceMode m_source_mode;
    bool m_streams_connected;
    mutable std::mutex m_mutex;  // Protects m_symbols for thread-safe access
    std::map<std::string, SymbolData> m_symbols;
    bool m_running;
    size_t m_sim_index;
    char m_current_time[16];
    int64_t m_current_timestamp;
    char m_error[256];

    // File-based parsers
    bool parse_level2_csv(const char* filepath, const char* symbol);
    bool parse_timesales_csv(const char* filepath, const char* symbol);
    bool parse_candles_csv(const char* filepath, std::vector<Candle>& candles);

    // HTTP API-based loading (deprecated, slower)
    bool load_symbol_from_http(const char* symbol);
    bool fetch_daily_candles_http(const char* symbol, std::vector<Candle>& candles);
    bool fetch_minute_candles_http(const char* symbol, int interval_secs, std::vector<Candle>& candles);
    bool parse_csv_response(const std::string& csv, std::vector<Candle>& candles);

    // TCP-based loading (async)
    bool load_symbol_from_tcp(const char* symbol);
    void on_lookup_result(const struct LookupResult& result);
    void update_l1_quote(const char* symbol);
    void update_l2_book(const char* symbol);

    // Level 2 color assignment
    static ImU32 get_level_color(int level_index, bool is_bid);
};

// Global market data instance
MarketData& get_market_data();

#endif // MARKET_DATA_H
