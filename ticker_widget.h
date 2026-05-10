// ticker_widget.h - Ticker window with Level 2, Time & Sales, and order entry
#ifndef TICKER_WIDGET_H
#define TICKER_WIDGET_H

#include "imgui.h"
#include "types.h"
#include "market_data.h"
#include "order_manager.h"
#include "iqfeed_tcp.h"
#include <string>

// Ticker widget for displaying Level 2, Time & Sales, and order entry
class TickerWidget {
public:
    TickerWidget();

    // Set dependencies
    void set_market_data(MarketData* market);
    void set_order_manager(OrderManager* order_mgr);

    // Set/get symbol
    void set_symbol(const char* symbol);
    const char* get_symbol() const;

    // Selection state
    void set_selected(bool selected);
    bool is_selected() const;

    // Error display
    void set_error(const char* msg);
    void clear_error();

    // Order entry state
    void set_order_quantity(int qty);
    void set_order_price(float price);
    int get_order_quantity() const;
    float get_order_price() const;

    // Render the ticker widget
    // Returns true if clicked (for selection)
    bool render(ImVec2 size);

    // Get best bid/ask for hotkey orders
    float get_best_bid() const;
    float get_best_ask() const;

private:
    char m_symbol[MAX_SYMBOL_LEN];
    bool m_selected;
    MarketData* m_market;
    OrderManager* m_order_mgr;

    // Order entry state
    int m_order_qty;
    float m_order_price;
    char m_qty_input[16];
    char m_price_input[16];
    char m_symbol_input[MAX_SYMBOL_LEN];
    bool m_editing_symbol;
    int m_edit_frames;  // Frame counter since editing started
    char m_error_msg[64];

    // Cached Level 1 data
    float m_last;
    int m_last_size;
    float m_high;
    float m_low;
    float m_open;
    float m_close;
    int64_t m_volume;
    char m_last_time[16];

    // Cached Level 2 data
    std::vector<Level2Entry> m_bids;
    std::vector<Level2Entry> m_asks;
    float m_best_bid;
    float m_best_ask;

    // Cached Time & Sales (streaming from L1 updates)
    std::vector<TimeSalesEntry> m_time_sales;
    float m_prev_last;  // For detecting trade direction

    // Helper functions
    void render_level1(float width);
    void render_level2(ImVec2 size, int max_rows);
    void render_time_sales(ImVec2 size, int max_rows);
    void render_order_entry(float width);
    void update_market_data();
    void update_time_sales_from_l1();
    bool validate_symbol(const char* symbol);
};

#endif // TICKER_WIDGET_H
