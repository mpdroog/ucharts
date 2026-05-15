// ticker_widget.cpp - Ticker window implementation
#include "ticker_widget.h"
#include "chart_widget.h"  // For make_color helper
#include "logger.h"
#include <cstring>
#include <cstdio>
#include <cmath>

TickerWidget::TickerWidget()
    : m_selected(false)
    , m_market(nullptr)
    , m_order_mgr(nullptr)
    , m_route_getter(nullptr)
    , m_order_qty(100)
    , m_order_price(0.0f)
    , m_focus_qty(false)
    , m_editing_symbol(false)
    , m_edit_frames(0)
    , m_last(0.0f)
    , m_last_size(0)
    , m_high(0.0f)
    , m_low(0.0f)
    , m_open(0.0f)
    , m_close(0.0f)
    , m_volume(0)
    , m_best_bid(0.0f)
    , m_best_ask(0.0f)
    , m_prev_last(0.0f)
{
    m_symbol[0] = '\0';
    m_qty_input[0] = '\0';
    m_price_input[0] = '\0';
    m_symbol_input[0] = '\0';
    m_error_msg[0] = '\0';
    m_last_time[0] = '\0';
}

void TickerWidget::set_market_data(MarketData* market) {
    m_market = market;
}

void TickerWidget::set_order_manager(OrderManager* order_mgr) {
    m_order_mgr = order_mgr;
}

void TickerWidget::set_route_getter(RouteGetter getter) {
    m_route_getter = getter;
}

void TickerWidget::set_symbol(const char* symbol) {
    if (symbol != nullptr) {
        safe_strcpy(m_symbol, symbol, sizeof(m_symbol));
        safe_strcpy(m_symbol_input, symbol, sizeof(m_symbol_input));
        m_error_msg[0] = '\0';
    } else {
        m_symbol[0] = '\0';
        m_symbol_input[0] = '\0';
    }
}

const char* TickerWidget::get_symbol() const {
    return m_symbol;
}

void TickerWidget::set_selected(bool selected) {
    m_selected = selected;
}

bool TickerWidget::is_selected() const {
    return m_selected;
}

void TickerWidget::set_error(const char* msg) {
    if (msg != nullptr) {
        safe_strcpy(m_error_msg, msg, sizeof(m_error_msg));
    }
}

void TickerWidget::clear_error() {
    m_error_msg[0] = '\0';
}

void TickerWidget::focus_order_entry() {
    m_focus_qty = true;
}

void TickerWidget::set_order_quantity(int qty) {
    m_order_qty = qty;
    snprintf(m_qty_input, sizeof(m_qty_input), "%d", qty);
}

void TickerWidget::set_order_price(float price) {
    m_order_price = price;
    snprintf(m_price_input, sizeof(m_price_input), "%.2f", static_cast<double>(price));
}

int TickerWidget::get_order_quantity() const {
    return m_order_qty;
}

float TickerWidget::get_order_price() const {
    return m_order_price;
}

float TickerWidget::get_best_bid() const {
    return m_best_bid;
}

float TickerWidget::get_best_ask() const {
    return m_best_ask;
}

void TickerWidget::clear_market_data() {
    // Clear L2 data
    m_bids.clear();
    m_asks.clear();

    // Clear T&S data
    m_time_sales.clear();
    m_prev_last = 0.0f;

    // Clear L1 data
    m_best_bid = 0.0f;
    m_best_ask = 0.0f;
    m_last = 0.0f;
    m_last_size = 0;
    m_high = 0.0f;
    m_low = 0.0f;
    m_open = 0.0f;
    m_close = 0.0f;
    m_volume = 0;
    m_last_time[0] = '\0';
}

void TickerWidget::update_market_data() {
    if (m_market == nullptr || m_symbol[0] == '\0') {
        m_bids.clear();
        m_asks.clear();
        m_time_sales.clear();
        m_best_bid = 0.0f;
        m_best_ask = 0.0f;
        m_last = 0.0f;
        m_high = 0.0f;
        m_low = 0.0f;
        m_volume = 0;
        return;
    }

    // Get Level 1 quote data (only if L1 is connected)
    bool l1_connected = get_iqfeed_level1().is_connected();
    if (l1_connected) {
        L1Quote quote;
        bool got_quote = get_iqfeed_level1().get_quote(m_symbol, quote);
        static int s_ticker_log_count = 0;
        if (s_ticker_log_count++ % 300 == 0) {
            LOG_D("ticker", "get_quote('%s') = %d, last=%.2f", m_symbol, got_quote, static_cast<double>(got_quote ? quote.last : 0.0f));
        }
        if (got_quote) {
            m_best_bid = quote.bid;
            m_best_ask = quote.ask;
            m_last = quote.last;
            m_last_size = quote.last_size;
            m_high = quote.high;
            m_low = quote.low;
            m_open = quote.open;
            m_close = quote.close;
            m_volume = quote.volume;
            safe_strcpy(m_last_time, quote.last_time, sizeof(m_last_time));

            // Update Time & Sales from L1 trade updates
            update_time_sales_from_l1();
        }
    }

    // Get Level 2 data
    float bid_tmp = 0.0f, ask_tmp = 0.0f;
    (void)m_market->get_level2(m_symbol, m_bids, m_asks, bid_tmp, ask_tmp);

    // Use L2 bid/ask if L1 not available
    if (m_best_bid <= 0.0f) m_best_bid = bid_tmp;
    if (m_best_ask <= 0.0f) m_best_ask = ask_tmp;

    // Fall back to stored T&S if streaming is empty
    if (m_time_sales.empty()) {
        (void)m_market->get_time_sales(m_symbol, m_time_sales, MAX_TIME_SALES_ROWS);
    }

}

void TickerWidget::update_time_sales_from_l1() {
    // Only add entry if we have a trade (last price > 0) and it changed
    if (m_last <= 0.0f) return;

    // Check if this is a new trade (price changed)
    bool is_new_trade = false;
    if (m_time_sales.empty()) {
        is_new_trade = true;
    } else {
        const TimeSalesEntry& latest = m_time_sales.front();
        // New trade if price changed significantly
        if (std::abs(latest.price - m_last) > 0.001f) {
            is_new_trade = true;
        }
    }

    if (is_new_trade && m_last_size > 0) {
        TimeSalesEntry entry;
        // Use last_time if available, otherwise leave empty
        if (m_last_time[0] != '\0') {
            safe_strcpy(entry.timestamp, m_last_time, sizeof(entry.timestamp));
        } else {
            entry.timestamp[0] = '\0';
        }
        entry.price = m_last;
        entry.size = m_last_size;

        // Determine direction based on previous last price
        if (m_prev_last > 0.0f) {
            if (m_last > m_prev_last) {
                entry.direction = TradeDirection::UP;
            } else if (m_last < m_prev_last) {
                entry.direction = TradeDirection::DOWN;
            } else {
                entry.direction = TradeDirection::SAME;
            }
        } else {
            entry.direction = TradeDirection::SAME;
        }

        // Insert at front (newest first)
        m_time_sales.insert(m_time_sales.begin(), entry);

        // Limit size
        while (m_time_sales.size() > MAX_TIME_SALES_ROWS) {
            m_time_sales.pop_back();
        }

        m_prev_last = m_last;
    }
}

void TickerWidget::render_level1(float width) {
    // Compact Level 1 display: Bid | Last | Ask on first row, H/L/Vol on second
    float col_width = width / 3.0f;

    // Row 1: Bid | Last (with change indicator) | Ask
    ImGui::PushStyleColor(ImGuiCol_Text, make_color(0, 200, 0, 255));  // Green for bid
    ImGui::Text("B: %.2f", static_cast<double>(m_best_bid));
    ImGui::PopStyleColor();

    ImGui::SameLine(col_width);

    // Last price - color based on direction from close
    ImU32 last_color = make_color(255, 255, 255, 255);  // White default
    if (m_close > 0.0f) {
        if (m_last > m_close) {
            last_color = make_color(0, 255, 0, 255);  // Green if up
        } else if (m_last < m_close) {
            last_color = make_color(255, 0, 0, 255);  // Red if down
        }
    }
    ImGui::PushStyleColor(ImGuiCol_Text, last_color);
    ImGui::Text("%.2f", static_cast<double>(m_last));
    ImGui::PopStyleColor();

    ImGui::SameLine(col_width * 2.0f);

    ImGui::PushStyleColor(ImGuiCol_Text, make_color(255, 80, 80, 255));  // Red for ask
    ImGui::Text("A: %.2f", static_cast<double>(m_best_ask));
    ImGui::PopStyleColor();
}

bool TickerWidget::validate_symbol(const char* symbol) {
    if (symbol == nullptr || symbol[0] == '\0') {
        return false;
    }

    // Check if market data has this symbol
    if (m_market != nullptr) {
        return m_market->has_symbol(symbol);
    }

    // If no market data, accept any non-empty symbol
    return true;
}

bool TickerWidget::render(ImVec2 size) {
    bool clicked = false;

    // Update market data
    update_market_data();

    ImGui::PushID(this);

    // Use BeginChild with border flag for proper layout
    ImU32 border_color = m_selected ? make_color(255, 200, 0, 255) : make_color(80, 80, 80, 255);

    // Push border color style
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(border_color));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, m_selected ? 2.0f : 1.0f);

    // Remove all spacing/padding for maximum data density - push BEFORE BeginChild
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));

    if (ImGui::BeginChild("TickerContent", size, true)) {
        // Check for click
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            clicked = true;
        }

        ImVec2 content_size = ImGui::GetContentRegionAvail();
        // Symbol header / input
        float header_height = 18.0f;
        ImGui::PushItemWidth(content_size.x);

        if (m_editing_symbol) {
            // Edit mode - show input field
            // Set focus for first few frames to ensure it takes
            if (m_edit_frames < 3) {
                ImGui::SetKeyboardFocusHere();
                m_edit_frames++;
            }
            if (ImGui::InputText("##SymbolEdit", m_symbol_input, sizeof(m_symbol_input),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                // Enter pressed - check for empty (clear) or validate symbol
                if (m_symbol_input[0] == '\0') {
                    // Empty input - clear the ticker completely
                    LOG_I("ticker", "Clearing ticker (was: %s)", m_symbol);

                    // Unwatch old symbol's L1/L2 if we had one
                    if (m_symbol[0] != '\0' && m_market != nullptr) {
                        if (!m_market->unsubscribe_quotes(m_symbol)) {
                            LOG_W("ticker", "Failed to unsubscribe quotes for %s", m_symbol);
                        }
                    }

                    // Clear symbol
                    m_symbol[0] = '\0';

                    // Clear all market data
                    clear_market_data();

                    // Clear order entry fields
                    m_order_qty = 100;
                    m_order_price = 0.0f;
                    m_qty_input[0] = '\0';
                    m_price_input[0] = '\0';

                    m_error_msg[0] = '\0';
                    m_editing_symbol = false;
                } else if (validate_symbol(m_symbol_input)) {
                    // Start async load of symbol data
                    LOG_D("ticker", "Starting async load for symbol: %s", m_symbol_input);
                    bool started = true;
                    if (m_market != nullptr) {
                        started = m_market->load_symbol(m_symbol_input);
                    }
                    if (started) {
                        LOG_I("ticker", "Async load started for: %s", m_symbol_input);

                        // Clear old market data before setting new symbol
                        clear_market_data();

                        safe_strcpy(m_symbol, m_symbol_input, sizeof(m_symbol));
                        m_error_msg[0] = '\0';
                        m_editing_symbol = false;
                    } else {
                        // Failed to even start loading
                        if (m_market != nullptr) {
                            LOG_W("ticker", "Failed to start load for %s: %s", m_symbol_input, m_market->last_error());
                            safe_strcpy(m_error_msg, m_market->last_error(), sizeof(m_error_msg));
                        } else {
                            LOG_W("ticker", "Failed to load %s: no market data", m_symbol_input);
                            safe_strcpy(m_error_msg, "Failed to load", sizeof(m_error_msg));
                        }
                    }
                } else {
                    LOG_W("ticker", "Invalid symbol: %s", m_symbol_input);
                    safe_strcpy(m_error_msg, "Invalid symbol", sizeof(m_error_msg));
                }
            }

            // Check for focus loss (only after enough frames for focus to settle)
            if (m_edit_frames >= 3 && !ImGui::IsItemActive() && !ImGui::IsItemFocused()) {
                m_editing_symbol = false;
                safe_strcpy(m_symbol_input, m_symbol, sizeof(m_symbol_input));
            }
        } else {
            // Display mode - show symbol or placeholder
            const char* display_text = (m_symbol[0] != '\0') ? m_symbol : "Enter Symbol";
            ImU32 text_color = (m_symbol[0] != '\0') ? make_color(255, 255, 255, 255) : make_color(128, 128, 128, 255);

            ImGui::PushStyleColor(ImGuiCol_Text, text_color);
            ImGui::PushStyleColor(ImGuiCol_Button, make_color(40, 40, 40, 255));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, make_color(60, 60, 60, 255));

            if (ImGui::Button(display_text, ImVec2(content_size.x, header_height))) {
                m_editing_symbol = true;
                m_edit_frames = 0;  // Reset frame counter for focus
                safe_strcpy(m_symbol_input, m_symbol, sizeof(m_symbol_input));
            }

            ImGui::PopStyleColor(3);
        }

        ImGui::PopItemWidth();

        // Show loading state or error message
        if (m_market != nullptr && m_symbol[0] != '\0') {
            MarketData::LoadingState state = m_market->get_loading_state(m_symbol);
            if (state == MarketData::LoadingState::PENDING) {
                ImGui::PushStyleColor(ImGuiCol_Text, make_color(255, 255, 0, 255));
                ImGui::TextUnformatted("Loading...");
                ImGui::PopStyleColor();
            } else if (state == MarketData::LoadingState::ERROR) {
                ImGui::PushStyleColor(ImGuiCol_Text, make_color(255, 100, 100, 255));
                const char* err = m_market->get_loading_error(m_symbol);
                ImGui::Text("Error: %s", err[0] != '\0' ? err : "Unknown error");
                ImGui::PopStyleColor();
            }
        }
        if (m_error_msg[0] != '\0') {
            ImGui::PushStyleColor(ImGuiCol_Text, make_color(255, 100, 100, 255));
            ImGui::TextUnformatted(m_error_msg);
            ImGui::PopStyleColor();
        }

        // Level 1 display (compact quote data)
        render_level1(content_size.x);

        // Calculate remaining height with proper overhead budgeting
        float used_height = ImGui::GetCursorPosY();

        // Budget for overhead
        const float ORDER_ENTRY_HEIGHT = 16.0f; // Order entry buttons (matches button_height)

        // Use ImGui's actual text line height for accurate row calculation
        float row_height = ImGui::GetTextLineHeight();

        // Calculate available space for L2/TS content
        float available_for_panels = content_size.y - used_height - ORDER_ENTRY_HEIGHT;
        float panel_width = content_size.x / 2.0f;

        // Calculate max rows that fit
        int max_l2_rows = static_cast<int>(available_for_panels / row_height);
        int max_ts_rows = static_cast<int>(available_for_panels / row_height);

        // Clamp to reasonable minimums and maximums
        max_l2_rows = std::max(5, std::min(max_l2_rows, 25));
        max_ts_rows = std::max(5, std::min(max_ts_rows, 40));

        // Render L2 and TS directly side-by-side
        render_level2(ImVec2(panel_width, available_for_panels), max_l2_rows);
        ImGui::SameLine(0, 0);
        render_time_sales(ImVec2(panel_width, available_for_panels), max_ts_rows);

        // Order entry directly after panels (no SetCursorPosY to see natural position)
        render_order_entry(content_size.x);
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(4);  // Pop ItemSpacing, FramePadding, WindowPadding, ChildBorderSize
    ImGui::PopStyleColor();  // Border color
    ImGui::PopID();

    return clicked;
}

void TickerWidget::render_level2(ImVec2 size, int /* max_rows */) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (ImGui::BeginChild("Level2", size, false, ImGuiWindowFlags_NoScrollbar)) {
        // Ultra-compact 4-column layout: Price | Size | Price | Size
        float col_width = size.x / 4.0f;

        ImGui::Columns(4, "L2Cols", false);
        ImGui::SetColumnWidth(0, col_width);
        ImGui::SetColumnWidth(1, col_width);
        ImGui::SetColumnWidth(2, col_width);
        ImGui::SetColumnWidth(3, col_width);

        // Render all available data - child window clips overflow
        size_t display_rows = std::max(m_bids.size(), m_asks.size());
        for (size_t i = 0; i < display_rows; ++i) {
            // Bid side
            if (i < m_bids.size()) {
                const Level2Entry& bid = m_bids[i];
                ImGui::PushStyleColor(ImGuiCol_Text, bid.color);
                ImGui::Text("%.2f", static_cast<double>(bid.price));
                ImGui::NextColumn();
                ImGui::Text("%.0f", static_cast<double>(bid.size) / 100.0);  // In lots
                ImGui::NextColumn();
                ImGui::PopStyleColor();
            } else {
                ImGui::Text("-");
                ImGui::NextColumn();
                ImGui::Text("-");
                ImGui::NextColumn();
            }

            // Ask side
            if (i < m_asks.size()) {
                const Level2Entry& ask = m_asks[i];
                ImGui::PushStyleColor(ImGuiCol_Text, ask.color);
                ImGui::Text("%.2f", static_cast<double>(ask.price));
                ImGui::NextColumn();
                ImGui::Text("%.0f", static_cast<double>(ask.size) / 100.0);  // In lots
                ImGui::NextColumn();
                ImGui::PopStyleColor();
            } else {
                ImGui::Text("-");
                ImGui::NextColumn();
                ImGui::Text("-");
                ImGui::NextColumn();
            }
        }
        // Don't call Columns(1) - it may add spacing
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);  // WindowPadding, ItemSpacing for Level2
}

void TickerWidget::render_time_sales(ImVec2 size, int /* max_rows */) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    if (ImGui::BeginChild("TimeSales", size, false, ImGuiWindowFlags_NoScrollbar)) {
        // Ultra-compact 2-column layout: Price | Size (right-aligned)
        float col_width = size.x / 2.0f;

        ImGui::Columns(2, "TSCols", false);
        ImGui::SetColumnWidth(0, col_width);
        ImGui::SetColumnWidth(1, col_width);

        // Render all available data - child window clips overflow
        size_t display_rows = m_time_sales.size();
        for (size_t i = 0; i < display_rows; ++i) {
            const TimeSalesEntry& entry = m_time_sales[i];

            // Color based on direction
            ImU32 color;
            switch (entry.direction) {
                case TradeDirection::UP:
                    color = make_color(0, 255, 0, 255);  // Green
                    break;
                case TradeDirection::DOWN:
                    color = make_color(255, 0, 0, 255);  // Red
                    break;
                case TradeDirection::SAME:
                    color = make_color(255, 255, 0, 255);  // Yellow
                    break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);

            // Right-align price
            char price_buf[16];
            snprintf(price_buf, sizeof(price_buf), "%.2f", static_cast<double>(entry.price));
            float price_width = ImGui::CalcTextSize(price_buf).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + col_width - price_width - 4.0f);
            ImGui::TextUnformatted(price_buf);
            ImGui::NextColumn();

            // Right-align size
            char size_buf[16];
            if (entry.size >= 1000) {
                snprintf(size_buf, sizeof(size_buf), "%.1fK", static_cast<double>(entry.size) / 1000.0);
            } else {
                snprintf(size_buf, sizeof(size_buf), "%d", entry.size);
            }
            float size_width = ImGui::CalcTextSize(size_buf).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + col_width - size_width - 4.0f);
            ImGui::TextUnformatted(size_buf);
            ImGui::NextColumn();

            ImGui::PopStyleColor();
        }
        // Don't call Columns(1) - it may add spacing
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);  // WindowPadding, ItemSpacing
}

void TickerWidget::render_order_entry(float width) {
    // Ultra-compact order entry: Qty + Price inputs + BUY/SELL buttons on one line
    float qty_width = width * 0.18f;
    float price_width = width * 0.22f;
    float button_width = (width - qty_width - price_width) / 2.0f;
    float button_height = 16.0f;

    // Quantity input with "Qty" placeholder
    if (m_focus_qty) {
        ImGui::SetKeyboardFocusHere();
        m_focus_qty = false;
    }
    ImGui::PushItemWidth(qty_width);
    if (ImGui::InputTextWithHint("##Qty", "Qty", m_qty_input, sizeof(m_qty_input), ImGuiInputTextFlags_CharsDecimal)) {
        m_order_qty = atoi(m_qty_input);
        if (m_order_qty < 0) m_order_qty = 0;
    }
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 0);

    // Price input with placeholder showing ask+0.05
    char price_hint[16];
    if (m_best_ask > 0.0f) {
        snprintf(price_hint, sizeof(price_hint), "%.2f", static_cast<double>(m_best_ask + ORDER_OFFSET));
    } else {
        price_hint[0] = '\0';
    }
    ImGui::PushItemWidth(price_width);
    if (ImGui::InputTextWithHint("##Price", price_hint, m_price_input, sizeof(m_price_input), ImGuiInputTextFlags_CharsDecimal)) {
        m_order_price = static_cast<float>(atof(m_price_input));
        if (m_order_price < 0.0f) m_order_price = 0.0f;
    }
    ImGui::PopItemWidth();

    ImGui::SameLine(0, 0);

    ImGui::PushStyleColor(ImGuiCol_Button, make_color(0, 100, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, make_color(0, 150, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, make_color(0, 200, 0, 255));

    if (ImGui::Button("BUY", ImVec2(button_width, button_height))) {
        // Use manual price if entered, otherwise ask+offset
        float buy_price = (m_order_price > 0.0f) ? m_order_price : (m_best_ask + ORDER_OFFSET);
        if (m_order_mgr == nullptr) {
            set_error("Order manager not configured");
        } else if (m_symbol[0] == '\0') {
            set_error("Enter a symbol first");
        } else if (m_order_qty <= 0) {
            set_error("Enter quantity");
        } else if (buy_price <= 0.0f) {
            set_error("Enter price (no ask data)");
        } else {
            const char* route = m_route_getter ? m_route_getter() : nullptr;
            LOG_I("ticker", "BUY order: %s qty=%d price=%.2f route=%s", m_symbol, m_order_qty, static_cast<double>(buy_price), route ? route : "default");
            int64_t result = m_order_mgr->buy(m_symbol, m_order_qty, buy_price, route);
            if (result < 0) {
                set_error(m_order_mgr->last_error());
            } else {
                clear_error();
            }
        }
    }

    ImGui::PopStyleColor(3);

    ImGui::SameLine(0, 0);

    ImGui::PushStyleColor(ImGuiCol_Button, make_color(150, 0, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, make_color(200, 0, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, make_color(255, 0, 0, 255));

    if (ImGui::Button("SELL", ImVec2(button_width, button_height))) {
        // Use manual price if entered, otherwise bid-offset
        float sell_price = (m_order_price > 0.0f) ? m_order_price : (m_best_bid - ORDER_OFFSET);
        if (m_order_mgr == nullptr) {
            set_error("Order manager not configured");
        } else if (m_symbol[0] == '\0') {
            set_error("Enter a symbol first");
        } else if (m_order_qty <= 0) {
            set_error("Enter quantity");
        } else if (sell_price <= 0.0f) {
            set_error("Enter price (no bid data)");
        } else {
            const char* route = m_route_getter ? m_route_getter() : nullptr;
            LOG_I("ticker", "SELL order: %s qty=%d price=%.2f route=%s", m_symbol, m_order_qty, static_cast<double>(sell_price), route ? route : "default");
            int64_t result = m_order_mgr->sell(m_symbol, m_order_qty, sell_price, route);
            if (result < 0) {
                set_error(m_order_mgr->last_error());
            } else {
                clear_error();
            }
        }
    }

    ImGui::PopStyleColor(3);
}
