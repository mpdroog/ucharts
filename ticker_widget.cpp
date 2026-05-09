// ticker_widget.cpp - Ticker window implementation
#include "ticker_widget.h"
#include "chart_widget.h"  // For make_color helper
#include "logger.h"
#include <cstring>
#include <cstdio>

TickerWidget::TickerWidget()
    : m_selected(false)
    , m_market(nullptr)
    , m_order_mgr(nullptr)
    , m_order_qty(100)
    , m_order_price(0.0f)
    , m_editing_symbol(false)
    , m_edit_frames(0)
    , m_best_bid(0.0f)
    , m_best_ask(0.0f)
{
    m_symbol[0] = '\0';
    m_qty_input[0] = '\0';
    m_price_input[0] = '\0';
    m_symbol_input[0] = '\0';
    m_error_msg[0] = '\0';
}

void TickerWidget::set_market_data(MarketData* market) {
    m_market = market;
}

void TickerWidget::set_order_manager(OrderManager* order_mgr) {
    m_order_mgr = order_mgr;
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

void TickerWidget::update_market_data() {
    if (m_market == nullptr || m_symbol[0] == '\0') {
        m_bids.clear();
        m_asks.clear();
        m_time_sales.clear();
        m_best_bid = 0.0f;
        m_best_ask = 0.0f;
        return;
    }

    m_market->get_level2(m_symbol, m_bids, m_asks, m_best_bid, m_best_ask);
    m_market->get_time_sales(m_symbol, m_time_sales, MAX_TIME_SALES_ROWS);

    // Update default order price to best ask if not set
    if (m_order_price <= 0.0f && m_best_ask > 0.0f) {
        set_order_price(m_best_ask);
    }
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

    if (ImGui::BeginChild("TickerContent", size, true)) {
        // Check for click
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            clicked = true;
        }

        ImVec2 content_size = ImGui::GetContentRegionAvail();
        // Symbol header / input
        float header_height = 25.0f;
        ImGui::PushItemWidth(content_size.x - 10);

        if (m_editing_symbol) {
            // Edit mode - show input field
            // Set focus for first few frames to ensure it takes
            if (m_edit_frames < 3) {
                ImGui::SetKeyboardFocusHere();
                m_edit_frames++;
            }
            if (ImGui::InputText("##SymbolEdit", m_symbol_input, sizeof(m_symbol_input),
                                 ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                // Enter pressed - validate and accept
                if (validate_symbol(m_symbol_input)) {
                    // Start async load of symbol data
                    LOG_D("ticker", "Starting async load for symbol: %s", m_symbol_input);
                    bool started = true;
                    if (m_market != nullptr) {
                        started = m_market->load_symbol(m_symbol_input);
                    }
                    if (started) {
                        LOG_I("ticker", "Async load started for: %s", m_symbol_input);
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

            if (ImGui::Button(display_text, ImVec2(content_size.x - 10, header_height))) {
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
            if (state == MarketData::LOAD_PENDING) {
                ImGui::PushStyleColor(ImGuiCol_Text, make_color(255, 255, 0, 255));
                ImGui::TextUnformatted("Loading...");
                ImGui::PopStyleColor();
            } else if (state == MarketData::LOAD_ERROR) {
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

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Calculate remaining height for Level 2 and T&S
        float remaining_height = content_size.y - ImGui::GetCursorPosY() - 80.0f; // Reserve 80 for order entry
        float level2_height = remaining_height * 0.6f;
        float ts_height = remaining_height * 0.4f;

        // Level 2 display
        render_level2(ImVec2(content_size.x, level2_height));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Time & Sales display
        render_time_sales(ImVec2(content_size.x, ts_height));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Order entry
        render_order_entry(content_size.x);
    }
    ImGui::EndChild();

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::PopID();

    return clicked;
}

void TickerWidget::render_level2(ImVec2 size) {
    ImGui::TextUnformatted("Level 2");

    if (ImGui::BeginChild("Level2", size, false)) {
        float col_width = size.x / 6.0f;

        // Headers
        ImGui::PushStyleColor(ImGuiCol_Text, make_color(150, 150, 150, 255));
        ImGui::Columns(6, "L2Header", false);
        ImGui::SetColumnWidth(0, col_width);
        ImGui::SetColumnWidth(1, col_width);
        ImGui::SetColumnWidth(2, col_width);
        ImGui::SetColumnWidth(3, col_width);
        ImGui::SetColumnWidth(4, col_width);
        ImGui::SetColumnWidth(5, col_width);

        ImGui::Text("Exch");
        ImGui::NextColumn();
        ImGui::Text("Bid");
        ImGui::NextColumn();
        ImGui::Text("Size");
        ImGui::NextColumn();
        ImGui::Text("Ask");
        ImGui::NextColumn();
        ImGui::Text("Size");
        ImGui::NextColumn();
        ImGui::Text("Exch");
        ImGui::NextColumn();
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Data rows
        size_t max_rows = MAX_LEVEL2_ROWS;
        for (size_t i = 0; i < max_rows; ++i) {
            // Bid side
            if (i < m_bids.size()) {
                const Level2Entry& bid = m_bids[i];
                ImGui::PushStyleColor(ImGuiCol_Text, bid.color);
                ImGui::Text("%s", bid.exchange);
                ImGui::NextColumn();
                ImGui::Text("%.2f", static_cast<double>(bid.price));
                ImGui::NextColumn();
                ImGui::Text("%.1f", static_cast<double>(bid.size) / 1000.0);
                ImGui::NextColumn();
                ImGui::PopStyleColor();
            } else {
                ImGui::Text("-");
                ImGui::NextColumn();
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
                ImGui::Text("%.1f", static_cast<double>(ask.size) / 1000.0);
                ImGui::NextColumn();
                ImGui::Text("%s", ask.exchange);
                ImGui::NextColumn();
                ImGui::PopStyleColor();
            } else {
                ImGui::Text("-");
                ImGui::NextColumn();
                ImGui::Text("-");
                ImGui::NextColumn();
                ImGui::Text("-");
                ImGui::NextColumn();
            }
        }

        ImGui::Columns(1);
    }
    ImGui::EndChild();
}

void TickerWidget::render_time_sales(ImVec2 size) {
    ImGui::TextUnformatted("Time & Sales");

    if (ImGui::BeginChild("TimeSales", size, false)) {
        float col_width = size.x / 3.0f;

        // Headers
        ImGui::PushStyleColor(ImGuiCol_Text, make_color(150, 150, 150, 255));
        ImGui::Columns(3, "TSHeader", false);
        ImGui::SetColumnWidth(0, col_width);
        ImGui::SetColumnWidth(1, col_width);
        ImGui::SetColumnWidth(2, col_width);

        ImGui::Text("Time");
        ImGui::NextColumn();
        ImGui::Text("Price");
        ImGui::NextColumn();
        ImGui::Text("Size");
        ImGui::NextColumn();
        ImGui::PopStyleColor();

        ImGui::Separator();

        // Data rows (newest first)
        for (size_t i = 0; i < m_time_sales.size() && i < MAX_TIME_SALES_ROWS; ++i) {
            const TimeSalesEntry& entry = m_time_sales[i];

            // Color based on direction
            ImU32 color;
            switch (entry.direction) {
                case DIR_UP:
                    color = make_color(0, 255, 0, 255);  // Green
                    break;
                case DIR_DOWN:
                    color = make_color(255, 0, 0, 255);  // Red
                    break;
                case DIR_SAME:
                    color = make_color(255, 255, 0, 255);  // Yellow
                    break;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::Text("%s", entry.timestamp);
            ImGui::NextColumn();
            ImGui::Text("%.2f", static_cast<double>(entry.price));
            ImGui::NextColumn();
            ImGui::Text("%d", entry.size);
            ImGui::NextColumn();
            ImGui::PopStyleColor();
        }

        ImGui::Columns(1);

        // Auto-scroll to show newest entries
        if (ImGui::GetScrollY() < ImGui::GetScrollMaxY()) {
            ImGui::SetScrollHereY(0.0f);
        }
    }
    ImGui::EndChild();
}

void TickerWidget::render_order_entry(float width) {
    float field_width = (width - 20) / 2.0f;

    // Quantity and Price inputs
    ImGui::PushItemWidth(field_width);

    ImGui::Text("Qty:");
    ImGui::SameLine();
    if (ImGui::InputText("##Qty", m_qty_input, sizeof(m_qty_input), ImGuiInputTextFlags_CharsDecimal)) {
        m_order_qty = atoi(m_qty_input);
        if (m_order_qty < 0) m_order_qty = 0;
    }

    ImGui::SameLine();
    ImGui::Text("Price:");
    ImGui::SameLine();
    if (ImGui::InputText("##Price", m_price_input, sizeof(m_price_input), ImGuiInputTextFlags_CharsDecimal)) {
        m_order_price = static_cast<float>(atof(m_price_input));
        if (m_order_price < 0.0f) m_order_price = 0.0f;
    }

    ImGui::PopItemWidth();

    // Buy and Sell buttons
    float button_width = (width - 15) / 2.0f;
    float button_height = 25.0f;

    ImGui::PushStyleColor(ImGuiCol_Button, make_color(0, 100, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, make_color(0, 150, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, make_color(0, 200, 0, 255));

    if (ImGui::Button("BUY", ImVec2(button_width, button_height))) {
        if (m_order_mgr != nullptr && m_symbol[0] != '\0' && m_order_qty > 0 && m_order_price > 0.0f) {
            LOG_I("ticker", "BUY order: %s qty=%d price=%.2f", m_symbol, m_order_qty, static_cast<double>(m_order_price));
            m_order_mgr->buy(m_symbol, m_order_qty, m_order_price);
        }
    }

    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Button, make_color(150, 0, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, make_color(200, 0, 0, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, make_color(255, 0, 0, 255));

    if (ImGui::Button("SELL", ImVec2(button_width, button_height))) {
        if (m_order_mgr != nullptr && m_symbol[0] != '\0' && m_order_qty > 0 && m_order_price > 0.0f) {
            LOG_I("ticker", "SELL order: %s qty=%d price=%.2f", m_symbol, m_order_qty, static_cast<double>(m_order_price));
            m_order_mgr->sell(m_symbol, m_order_qty, m_order_price);
        }
    }

    ImGui::PopStyleColor(3);
}
