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
static const int MAX_CANDLES = 200;          // Maximum candles per chart
static const int NUM_TICKERS = 4;            // Number of ticker windows
static const float ORDER_OFFSET = 0.05f;     // $0.05 offset for market orders
static const int MAX_SYMBOL_LEN = 8;         // Maximum symbol length
static const int MAX_LEVEL2_ROWS = 10;       // Max level 2 rows to display
static const int MAX_TIME_SALES_ROWS = 15;   // Max time & sales rows to display

// Line styles
enum LineStyle {
    STYLE_SOLID = 0,
    STYLE_DASHED = 1,
    STYLE_DOTTED = 2
};

// Chart timeframes
enum Timeframe {
    TF_1MIN = 0,
    TF_5MIN = 1,
    TF_DAILY = 2
};

// Order side
enum OrderSide {
    SIDE_BUY = 'B',
    SIDE_SELL = 'S'
};

// Order status
enum OrderStatus {
    STATUS_PENDING = 'P',
    STATUS_FILLED = 'F',
    STATUS_PARTIAL = 'A',  // Partially filled
    STATUS_CANCELLED = 'X'
};

// Time & Sales direction
enum TradeDirection {
    DIR_DOWN = -1,
    DIR_SAME = 0,
    DIR_UP = 1
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

    TimeSalesEntry() : price(0), size(0), direction(DIR_SAME) {
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
    OrderSide side;
    int quantity;
    int filled;
    float price;
    OrderStatus status;
    int64_t created_at;   // Unix timestamp

    Order() : id(0), side(SIDE_BUY), quantity(0), filled(0),
              price(0), status(STATUS_PENDING), created_at(0) {
        symbol[0] = '\0';
    }

    int pending_qty() const { return quantity - filled; }
    bool is_complete() const { return filled >= quantity || status == STATUS_CANCELLED; }
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
struct ClosedPosition {
    char symbol[8];
    int quantity;
    float entry_price;
    float exit_price;
    int64_t entry_time;   // Unix timestamp
    int64_t exit_time;    // Unix timestamp

    ClosedPosition() : quantity(0), entry_price(0), exit_price(0),
                       entry_time(0), exit_time(0) {
        symbol[0] = '\0';
    }

    float pnl_usd() const {
        return static_cast<float>(quantity) * (exit_price - entry_price);
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

    HLine() : price(0), color(0), style(STYLE_SOLID), source_tf(TF_DAILY), selected(false) {}
    HLine(float p, ImU32 c, LineStyle s, Timeframe tf = TF_DAILY)
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
                  color(0), style(STYLE_SOLID), source_tf(TF_DAILY), selected(false) {}
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

    IndicatorSettings() : sma_enabled(false), sma_period(20),
                          ema_enabled(false), ema_period(9),
                          boll_enabled(false), boll_period(20),
                          volume_enabled(true) {}
};

// ============================================================================
// Widget State Structures
// ============================================================================

// Chart zoom/pan state
struct ChartViewState {
    float zoom;
    float pan_x;

    ChartViewState() : zoom(1.0f), pan_x(0.0f) {}

    void reset() { zoom = 1.0f; pan_x = 0.0f; }
};

// Per-symbol chart state (persisted)
struct SymbolChartState {
    std::vector<HLine> hlines;
    std::vector<TrendLine> trendlines;
    IndicatorSettings indicators;
    ChartViewState view_1m;
    ChartViewState view_5m;
    ChartViewState view_daily;
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

    FullscreenState() : active(false), timeframe(TF_1MIN) {}
};

// Global application state
struct AppState {
    // Ticker windows
    TickerState tickers[NUM_TICKERS];
    int selected_ticker;

    // Chart data per symbol
    std::map<std::string, std::vector<Candle>> candles_1m;
    std::map<std::string, std::vector<Candle>> candles_5m;
    std::map<std::string, std::vector<Candle>> candles_daily;
    std::map<std::string, SymbolChartState> chart_states;

    // Positions
    std::vector<Position> open_positions;
    std::vector<ClosedPosition> closed_positions;
    std::vector<Order> pending_orders;

    // UI state
    FullscreenState fullscreen;
    int64_t current_sim_time;  // Current simulation timestamp

    AppState() : selected_ticker(0), current_sim_time(0) {
        tickers[0].selected = true;
    }

    const char* selected_symbol() const {
        if (selected_ticker >= 0 && selected_ticker < NUM_TICKERS) {
            return tickers[selected_ticker].symbol;
        }
        return "";
    }

    void select_ticker(int idx) {
        if (idx >= 0 && idx < NUM_TICKERS) {
            for (int i = 0; i < NUM_TICKERS; i++) {
                tickers[i].selected = (i == idx);
            }
            selected_ticker = idx;
        }
    }

    Position* find_position(const char* symbol) {
        for (auto& pos : open_positions) {
            if (std::strcmp(pos.symbol, symbol) == 0) {
                return &pos;
            }
        }
        return nullptr;
    }

    SymbolChartState& get_chart_state(const char* symbol) {
        return chart_states[std::string(symbol)];
    }
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

#endif // TYPES_H
