// types.h - Data structures for ucharts trading platform
#ifndef TYPES_H
#define TYPES_H

#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <map>

// Forward declare ImU32 to avoid ImGui dependency in pure data header
// When included after imgui.h, this will be the same type
#ifndef ImU32
typedef unsigned int ImU32;
#endif

// ============================================================================
// Constants
// ============================================================================

static const int LEVEL2_DEPTH = 10;          // Number of price levels per side
static const int TIME_SALES_ROWS = 15;       // Number of T&S rows to display
static const int MAX_CANDLES = 200;          // Maximum candles per chart (display)
static const int MAX_BACKTEST_CANDLES = 2000; // Maximum candles for backtesting (~4 trading days of 1-min)
static const int NUM_TICKERS = 4;            // Number of ticker windows
static const float ORDER_OFFSET = 0.05f;     // $0.05 offset for market orders
static const int MAX_SYMBOL_LEN = 8;         // Maximum symbol length
static const int MAX_LEVEL2_ROWS = 10;       // Max level 2 rows to display
static const int MAX_TIME_SALES_ROWS = 20;   // Max time & sales rows to store

// Callback type for getting the currently selected trading route
typedef const char* (*RouteGetter)();

// Line styles
enum class LineStyle {
    SOLID = 0,
    DASHED = 1,
    DOTTED = 2
};

// Chart timeframes
enum class Timeframe {
    M1 = 0,
    M5 = 1,
    DAILY = 2
};

// Order side (character values for database compatibility)
enum class OrderSide {
    BUY = 'B',
    SELL = 'S'
};

// Order status (character values for database compatibility)
enum class OrderStatus {
    PENDING = 'P',
    FILLED = 'F',
    PARTIAL = 'A',  // Partially filled
    CANCELLED = 'X',
    REJECTED = 'R'
};

// Time & Sales direction
enum class TradeDirection {
    DOWN = -1,
    SAME = 0,
    UP = 1
};

// ============================================================================
// Market Data Structures
// ============================================================================

// OHLCV candlestick data
struct Candle {
    char timestamp[32];
    float open;
    float high;
    float low;
    float close;
    float volume;

    Candle() : open(0), high(0), low(0), close(0), volume(0) {
        timestamp[0] = '\0';
    }
};

// Level 2 order book entry (aggregated by price)
struct Level2Entry {
    char exchange[8];     // Exchange name (NYSE, ARCA, BATS, etc.)
    float price;
    int size;             // Size in shares
    ImU32 color;          // Display color for this level

    Level2Entry() : price(0), size(0), color(0) {
        exchange[0] = '\0';
    }
};

// Time & Sales entry
struct TimeSalesEntry {
    char timestamp[16];   // HH:MM:SS.mmm format
    float price;
    int size;
    TradeDirection direction;

    TimeSalesEntry() : price(0), size(0), direction(TradeDirection::SAME) {
        timestamp[0] = '\0';
    }
};

// ============================================================================
// Order & Position Structures
// ============================================================================

// Order
struct Order {
    int64_t id;
    char symbol[8];
    char client_order_id[64];  // TradeZero client order ID
    OrderSide side;
    int quantity;              // Total order quantity
    int executed;              // Shares filled (from API)
    int canceled;              // Shares canceled (from API)
    int leaves;                // Shares remaining (from API)
    float price;               // Limit price
    float avg_price;           // Average execution price (from API)
    OrderStatus status;
    int64_t created_at;        // Unix timestamp

    Order() : id(0), side(OrderSide::BUY), quantity(0), executed(0),
              canceled(0), leaves(0), price(0), avg_price(0),
              status(OrderStatus::PENDING), created_at(0) {
        symbol[0] = '\0';
        client_order_id[0] = '\0';
    }

    bool is_complete() const { return leaves == 0 || status == OrderStatus::CANCELLED || status == OrderStatus::REJECTED; }
};

// Open position
struct Position {
    char symbol[8];
    int quantity;
    float avg_price;
    float current_price;

    Position() : quantity(0), avg_price(0), current_price(0) {
        symbol[0] = '\0';
    }

    float unrealized_pnl() const {
        return static_cast<float>(quantity) * (current_price - avg_price);
    }

    float pnl_percent() const {
        if (avg_price <= 0.0f) return 0.0f;
        return ((current_price - avg_price) / avg_price) * 100.0f;
    }
};

// Closed position (trade history)
enum class ClosedPositionStatus : char {
    FILLED = 'F',
    REJECTED = 'R'
};

struct ClosedPosition {
    char symbol[8];
    int quantity;
    float entry_price;
    float exit_price;
    int64_t entry_time;   // Unix timestamp
    int64_t exit_time;    // Unix timestamp
    ClosedPositionStatus status;

    ClosedPosition() : quantity(0), entry_price(0), exit_price(0),
                       entry_time(0), exit_time(0), status(ClosedPositionStatus::FILLED) {
        symbol[0] = '\0';
    }

    float pnl_usd() const {
        return static_cast<float>(quantity) * (exit_price - entry_price);
    }

    bool is_rejected() const {
        return status == ClosedPositionStatus::REJECTED;
    }
};

// ============================================================================
// Chart Drawing Structures
// ============================================================================

// Horizontal line
struct HLine {
    float price;
    ImU32 color;
    LineStyle style;
    Timeframe source_tf;  // Timeframe where line was drawn
    bool selected;

    HLine() : price(0), color(0), style(LineStyle::SOLID), source_tf(Timeframe::DAILY), selected(false) {}
    HLine(float p, ImU32 c, LineStyle s, Timeframe tf = Timeframe::DAILY)
        : price(p), color(c), style(s), source_tf(tf), selected(false) {}
};

// Trend line
struct TrendLine {
    float candle_start;  // Float for exact positioning between candles
    float candle_end;
    float price_start;
    float price_end;
    ImU32 color;
    LineStyle style;
    Timeframe source_tf;  // Timeframe where line was drawn
    bool selected;

    TrendLine() : candle_start(0), candle_end(0), price_start(0), price_end(0),
                  color(0), style(LineStyle::SOLID), source_tf(Timeframe::DAILY), selected(false) {}
};

// Automatic support/resistance level (calculated from daily highs/lows)
struct AutoSRLevel {
    float price;
    bool is_resistance;  // true = resistance (swing high), false = support (swing low)
    int candle_idx;      // Index in daily candles where this level was identified

    AutoSRLevel() : price(0), is_resistance(true), candle_idx(0) {}
    AutoSRLevel(float p, bool res, int idx) : price(p), is_resistance(res), candle_idx(idx) {}
};

// Auto-MA selection mode
enum class AutoMAType {
    NONE = 0,
    EMA9 = 1,
    SMA9 = 2,
    EMA21 = 3,
    SMA21 = 4
};

// Indicator settings
struct IndicatorSettings {
    bool sma_enabled;
    int sma_period;
    bool ema_enabled;
    int ema_period;
    bool boll_enabled;
    int boll_period;
    bool volume_enabled;
    bool vwap_enabled;           // VWAP indicator
    bool cumulative_delta_enabled; // Cumulative delta indicator
    bool auto_ma_enabled;        // Auto-select best MA
    AutoMAType auto_ma_type;     // Which MA was auto-selected

    IndicatorSettings() : sma_enabled(false), sma_period(20),
                          ema_enabled(false), ema_period(9),
                          boll_enabled(false), boll_period(20),
                          volume_enabled(true),
                          vwap_enabled(true),
                          cumulative_delta_enabled(true),
                          auto_ma_enabled(true),
                          auto_ma_type(AutoMAType::NONE) {}
};

// ============================================================================
// Widget State Structures
// ============================================================================

// Chart zoom/pan state
struct ChartViewState {
    float zoom;
    float pan_x;  // -1 means "show latest candles"

    ChartViewState() : zoom(1.0f), pan_x(-1.0f) {}

    void reset() { zoom = 1.0f; pan_x = -1.0f; }
};

// Ticker window state
struct TickerState {
    char symbol[8];
    char symbol_input[8];     // For editing
    bool has_error;
    char error_msg[64];
    float bid;
    float ask;
    std::vector<Level2Entry> bids;
    std::vector<Level2Entry> asks;
    std::vector<TimeSalesEntry> time_sales;
    bool selected;

    // Order entry fields
    int order_qty;
    float order_price;

    TickerState() : has_error(false), bid(0), ask(0), selected(false),
                    order_qty(100), order_price(0) {
        symbol[0] = '\0';
        symbol_input[0] = '\0';
        error_msg[0] = '\0';
        bids.reserve(LEVEL2_DEPTH);
        asks.reserve(LEVEL2_DEPTH);
        time_sales.reserve(TIME_SALES_ROWS);
    }

    bool is_empty() const { return symbol[0] == '\0'; }

    void set_symbol(const char* s) {
        std::strncpy(symbol, s, sizeof(symbol) - 1);
        symbol[sizeof(symbol) - 1] = '\0';
        std::strncpy(symbol_input, s, sizeof(symbol_input) - 1);
        symbol_input[sizeof(symbol_input) - 1] = '\0';
        has_error = false;
        error_msg[0] = '\0';
    }

    void set_error(const char* msg) {
        has_error = true;
        std::strncpy(error_msg, msg, sizeof(error_msg) - 1);
        error_msg[sizeof(error_msg) - 1] = '\0';
    }

    void clear_error() {
        has_error = false;
        error_msg[0] = '\0';
    }
};

// ============================================================================
// Application State
// ============================================================================

// Fullscreen chart state
struct FullscreenState {
    bool active;
    Timeframe timeframe;

    FullscreenState() : active(false), timeframe(Timeframe::M1) {}
};

// ============================================================================
// Utility functions
// ============================================================================

// Helper to safely copy strings
inline void safe_strcpy(char* dest, const char* src, size_t dest_size) {
    if (dest_size == 0) return;
    std::strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

// Compare symbols (case-insensitive)
inline bool symbols_equal(const char* a, const char* b) {
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? static_cast<char>(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? static_cast<char>(*b - 32) : *b;
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == *b;
}

// ============================================================================
// Safe sleep functions - crash on zero/negative duration to prevent busy waits
// ============================================================================

#include <thread>
#include <chrono>
#include <cstdio>
#include <cstdlib>

// Safe sleep (milliseconds) - crashes if duration <= 0
inline void safe_sleep_ms(int ms) {
    if (ms <= 0) {
        std::fprintf(stderr, "FATAL: safe_sleep_ms(%d) - zero/negative duration would cause busy wait\n", ms);
        std::abort();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// Safe sleep (seconds) - crashes if duration <= 0
inline void safe_sleep_s(int s) {
    if (s <= 0) {
        std::fprintf(stderr, "FATAL: safe_sleep_s(%d) - zero/negative duration would cause busy wait\n", s);
        std::abort();
    }
    std::this_thread::sleep_for(std::chrono::seconds(s));
}

// Safe sleep (microseconds) - crashes if duration <= 0
inline void safe_sleep_us(int us) {
    if (us <= 0) {
        std::fprintf(stderr, "FATAL: safe_sleep_us(%d) - zero/negative duration would cause busy wait\n", us);
        std::abort();
    }
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

#endif // TYPES_H
