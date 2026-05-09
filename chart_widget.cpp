// chart_widget.cpp - Candlestick chart widget implementation
#include "chart_widget.h"
#include <cmath>
#include <cstdio>
#include <algorithm>

ChartWidget::ChartWidget()
    : m_current_price(0),
      m_hlines(nullptr), m_trendlines(nullptr), m_settings(nullptr), m_view(nullptr),
      m_draw_mode(ChartDrawMode::NONE), m_draw_color(g_chart_colors[0]), m_draw_style(LineStyle::SOLID),
      m_timeframe(Timeframe::DAILY),
      m_hovered_candle(-1), m_is_panning(false), m_pan_start_x(0), m_pan_start_pan(0),
      m_dragging_hline(-1), m_dragging_trendline(-1), m_dragging_trendline_point(-1),
      m_trendline_drawing(false), m_trendline_start_candle(-1.0f), m_trendline_start_price(0),
      m_indicators_dirty(true), m_daily_candles(nullptr), m_sr_dirty(true), m_prev_daily_size(0) {
}

void ChartWidget::set_symbol(const char* symbol) {
    if (symbol == nullptr) symbol = "";
    if (m_symbol != symbol) {
        m_symbol = symbol;
        m_indicators_dirty = true;
        m_sr_dirty = true;  // Recalculate S/R for new symbol
    }
}

void ChartWidget::set_candles(const std::vector<Candle>& candles) {
    // Only update if dirty (symbol changed) or data actually different
    if (m_indicators_dirty || candles.size() != m_candles.size()) {
        m_candles = candles;
    }
}

void ChartWidget::set_daily_candles(const std::vector<Candle>* daily_candles) {
    size_t new_size = daily_candles ? daily_candles->size() : 0;

    // Update the pointer
    m_daily_candles = daily_candles;

    // Only recalculate if size actually changed and we have enough data
    if (new_size != m_prev_daily_size && new_size >= 3) {
        m_sr_dirty = true;
        m_prev_daily_size = new_size;
    }
}

void ChartWidget::set_title(const char* title) {
    m_title = title ? title : "";
}

void ChartWidget::set_current_price(float price) {
    m_current_price = price;
}

void ChartWidget::set_drawings(std::vector<HLine>* hlines, std::vector<TrendLine>* trendlines) {
    m_hlines = hlines;
    m_trendlines = trendlines;
}

void ChartWidget::set_indicator_settings(IndicatorSettings* settings) {
    m_settings = settings;
    m_indicators_dirty = true;
}

void ChartWidget::set_view_state(ChartViewState* state) {
    m_view = state;
}

void ChartWidget::reset_view() {
    if (m_view != nullptr) {
        m_view->reset();
    } else {
        m_default_view.reset();
    }
}

void ChartWidget::set_draw_mode(ChartDrawMode mode) {
    m_draw_mode = mode;
    if (mode != ChartDrawMode::TRENDLINE) {
        m_trendline_drawing = false;
        m_trendline_start_candle = -1.0f;
    }
}

void ChartWidget::set_draw_color(ImU32 color) {
    m_draw_color = color;
}

void ChartWidget::set_draw_style(LineStyle style) {
    m_draw_style = style;
}

void ChartWidget::set_timeframe(Timeframe tf) {
    m_timeframe = tf;
}

// Calculate thickness multiplier based on source timeframe vs current timeframe
// Daily lines are 2x thicker on 5min/1min
// 5min lines are 1.5x thicker on 1min
static float get_thickness_multiplier(Timeframe source_tf, Timeframe current_tf) {
    if (source_tf == Timeframe::DAILY) {
        if (current_tf == Timeframe::M5 || current_tf == Timeframe::M1) {
            return 2.0f;  // Daily lines are 2x thicker on lower timeframes
        }
    } else if (source_tf == Timeframe::M5) {
        if (current_tf == Timeframe::M1) {
            return 1.5f;  // 5min lines are 1.5x thicker on 1min
        }
    }
    return 1.0f;  // Same timeframe or lower to higher - normal thickness
}

int ChartWidget::get_hovered_candle() const {
    return m_hovered_candle;
}

const Candle* ChartWidget::get_candle(int index) const {
    if (index < 0 || index >= static_cast<int>(m_candles.size())) return nullptr;
    return &m_candles[static_cast<size_t>(index)];
}

void ChartWidget::recalculate_indicators() {
    m_sma_values.clear();
    m_ema_values.clear();
    m_boll_upper.clear();
    m_boll_lower.clear();

    if (m_settings == nullptr || m_candles.empty()) return;

    if (m_settings->sma_enabled) {
        calculate_sma(m_settings->sma_period);
    }
    if (m_settings->ema_enabled) {
        calculate_ema(m_settings->ema_period);
    }
    if (m_settings->boll_enabled) {
        calculate_bollinger(m_settings->boll_period, 2.0f);
    }
}

void ChartWidget::calculate_sma(int period) {
    m_sma_values.resize(m_candles.size(), 0.0f);

    for (size_t i = 0; i < m_candles.size(); i++) {
        if (i < static_cast<size_t>(period - 1)) {
            m_sma_values[i] = 0.0f;
            continue;
        }
        float sum = 0.0f;
        for (int j = 0; j < period; j++) {
            sum += m_candles[i - static_cast<size_t>(j)].close;
        }
        m_sma_values[i] = sum / static_cast<float>(period);
    }
}

void ChartWidget::calculate_ema(int period) {
    m_ema_values.resize(m_candles.size(), 0.0f);

    if (m_candles.empty()) return;

    float k = 2.0f / (static_cast<float>(period) + 1.0f);

    float sum = 0.0f;
    for (int i = 0; i < period && i < static_cast<int>(m_candles.size()); i++) {
        sum += m_candles[static_cast<size_t>(i)].close;
    }
    m_ema_values[static_cast<size_t>(period - 1)] = sum / static_cast<float>(period);

    for (size_t i = static_cast<size_t>(period); i < m_candles.size(); i++) {
        m_ema_values[i] = m_candles[i].close * k + m_ema_values[i - 1] * (1.0f - k);
    }
}

void ChartWidget::calculate_bollinger(int period, float mult) {
    std::vector<float> sma;
    sma.resize(m_candles.size(), 0.0f);

    for (size_t i = 0; i < m_candles.size(); i++) {
        if (i < static_cast<size_t>(period - 1)) continue;
        float sum = 0.0f;
        for (int j = 0; j < period; j++) {
            sum += m_candles[i - static_cast<size_t>(j)].close;
        }
        sma[i] = sum / static_cast<float>(period);
    }

    m_boll_upper.resize(m_candles.size(), 0.0f);
    m_boll_lower.resize(m_candles.size(), 0.0f);

    for (size_t i = static_cast<size_t>(period - 1); i < m_candles.size(); i++) {
        float sum_sq = 0.0f;
        for (int j = 0; j < period; j++) {
            float diff = m_candles[i - static_cast<size_t>(j)].close - sma[i];
            sum_sq += diff * diff;
        }
        float stddev = std::sqrt(sum_sq / static_cast<float>(period));
        m_boll_upper[i] = sma[i] + mult * stddev;
        m_boll_lower[i] = sma[i] - mult * stddev;
    }
}

MarketSession ChartWidget::get_session_from_timestamp(const char* timestamp) {
    if (timestamp == nullptr || timestamp[0] == '\0') {
        return MarketSession::REGULAR;
    }

    // Find time part (after space if present)
    const char* time_part = timestamp;
    const char* space = std::strchr(timestamp, ' ');
    if (space != nullptr) {
        time_part = space + 1;
    }

    int hour = 0, minute = 0;
    if (std::sscanf(time_part, "%d:%d", &hour, &minute) < 2) {
        return MarketSession::REGULAR;
    }

    int time_mins = hour * 60 + minute;

    // Pre-market: 04:00 - 09:30 (240 - 570)
    if (time_mins >= 240 && time_mins < 570) {
        return MarketSession::PRE_MARKET;
    }
    // Regular hours: 09:30 - 16:00 (570 - 960)
    if (time_mins >= 570 && time_mins < 960) {
        return MarketSession::REGULAR;
    }
    // After-hours: 16:00 - 20:00 (960 - 1200)
    if (time_mins >= 960 && time_mins < 1200) {
        return MarketSession::AFTER_HOURS;
    }

    return MarketSession::REGULAR;
}

void ChartWidget::calculate_auto_sr() {
    m_auto_sr_levels.clear();

    if (m_daily_candles == nullptr || m_daily_candles->size() < 3) {
        return;
    }

    const std::vector<Candle>& candles = *m_daily_candles;
    float current_price = candles.back().close;

    // Calculate average volume for filtering
    float total_volume = 0.0f;
    int volume_count = 0;
    for (const auto& c : candles) {
        if (c.volume > 0) {
            total_volume += c.volume;
            volume_count++;
        }
    }
    float avg_volume = (volume_count > 0) ? (total_volume / static_cast<float>(volume_count)) : 0.0f;
    float min_volume = avg_volume * 0.5f;

    // Collect all swing highs (potential resistance) and swing lows (potential support)
    std::vector<AutoSRLevel> all_resistance;
    std::vector<AutoSRLevel> all_support;

    for (size_t i = 1; i < candles.size() - 1; i++) {
        const Candle& prev = candles[i - 1];
        const Candle& curr = candles[i];
        const Candle& next = candles[i + 1];

        bool has_significant_volume = (avg_volume <= 0) || (curr.volume >= min_volume);
        if (!has_significant_volume) continue;

        // Swing high -> potential resistance (only if ABOVE current price)
        if (curr.high > prev.high && curr.high > next.high) {
            if (curr.high > current_price) {
                all_resistance.emplace_back(curr.high, true, static_cast<int>(i));
            }
        }

        // Swing low -> potential support (only if BELOW current price)
        if (curr.low < prev.low && curr.low < next.low) {
            if (curr.low < current_price) {
                all_support.emplace_back(curr.low, false, static_cast<int>(i));
            }
        }
    }

    // Sort resistance by price (ascending - closest to current price first)
    std::sort(all_resistance.begin(), all_resistance.end(),
              [](const AutoSRLevel& a, const AutoSRLevel& b) { return a.price < b.price; });

    // Sort support by price (descending - closest to current price first)
    std::sort(all_support.begin(), all_support.end(),
              [](const AutoSRLevel& a, const AutoSRLevel& b) { return a.price > b.price; });

    // Take up to 3 closest resistance levels
    const size_t MAX_LEVELS = 3;
    for (size_t i = 0; i < std::min(all_resistance.size(), MAX_LEVELS); i++) {
        m_auto_sr_levels.push_back(all_resistance[i]);
    }

    // Take up to 3 closest support levels
    for (size_t i = 0; i < std::min(all_support.size(), MAX_LEVELS); i++) {
        m_auto_sr_levels.push_back(all_support[i]);
    }
}

void ChartWidget::draw_dashed_line(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, float dash_size) {
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.001f) return;

    dx /= len;
    dy /= len;

    float pos = 0.0f;
    bool draw = true;
    while (pos < len) {
        float segment = std::min(dash_size, len - pos);
        if (draw) {
            dl->AddLine(
                ImVec2(p1.x + dx * pos, p1.y + dy * pos),
                ImVec2(p1.x + dx * (pos + segment), p1.y + dy * (pos + segment)),
                color, thickness);
        }
        pos += dash_size;
        draw = !draw;
    }
}

void ChartWidget::draw_styled_line(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, LineStyle style) {
    switch (style) {
        case LineStyle::DASHED:
            draw_dashed_line(dl, p1, p2, color, thickness, 8.0f);
            break;
        case LineStyle::DOTTED:
            draw_dashed_line(dl, p1, p2, color, thickness, 3.0f);
            break;
        case LineStyle::SOLID:
            dl->AddLine(p1, p2, color, thickness);
            break;
    }
}

bool ChartWidget::render(ImVec2 size) {
    // Recalculate indicators only when data or settings changed
    if (m_indicators_dirty) {
        recalculate_indicators();
        m_indicators_dirty = false;
    }

    // Recalculate auto S/R from daily candles (only when dirty flag is set)
    if (m_sr_dirty) {
        calculate_auto_sr();
        m_sr_dirty = false;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;

    if (canvas_size.x <= 0.0f) canvas_size.x = ImGui::GetContentRegionAvail().x;
    if (canvas_size.y <= 0.0f) canvas_size.y = ImGui::GetContentRegionAvail().y;

    // Get view state
    ChartViewState* view = m_view ? m_view : &m_default_view;
    const float ZOOM_MIN = 0.1f;
    const float ZOOM_MAX = 30.0f;

    // Layout constants
    const float padding_left = 10.0f;
    const float padding_right = 60.0f;
    const float padding_top = 10.0f;
    const float time_axis_height = 20.0f;
    const float title_height = m_title.empty() ? 0.0f : 18.0f;

    // Calculate volume panel height
    float volume_height = 0.0f;
    bool show_volume = m_settings && m_settings->volume_enabled && !m_candles.empty() && m_candles[0].volume > 0;
    if (show_volume) {
        volume_height = canvas_size.y * 0.15f;
    }

    float main_chart_height = canvas_size.y - time_axis_height - volume_height - title_height;

    // Draw background
    draw_list->AddRectFilled(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        make_color(20, 20, 25));
    draw_list->AddRect(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        make_color(60, 60, 60));

    // Draw title
    if (!m_title.empty()) {
        draw_list->AddText(ImVec2(canvas_pos.x + 5, canvas_pos.y + 2), make_color(150, 150, 150), m_title.c_str());
    }

    // Adjust canvas pos for title
    ImVec2 chart_pos = ImVec2(canvas_pos.x, canvas_pos.y + title_height);

    // Create invisible button for interaction
    ImGui::SetCursorScreenPos(canvas_pos);
    char id[64];
    std::snprintf(id, sizeof(id), "##chart_%p", static_cast<void*>(this));
    ImGui::InvisibleButton(id, canvas_size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    bool is_hovered = ImGui::IsItemHovered();
    bool is_active = ImGui::IsItemActive();
    bool double_clicked = ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && is_hovered;
    const ImGuiIO& io = ImGui::GetIO();

    if (m_candles.empty()) {
        draw_list->AddText(ImVec2(chart_pos.x + 10, chart_pos.y + 20),
            make_color(100, 100, 100), "No data");
        return double_clicked;
    }

    // Calculate visible range
    float total_candles = static_cast<float>(m_candles.size());
    float visible_candles = total_candles / view->zoom;
    // Allow scrolling 30% past the right edge for some breathing room
    float extra_scroll = visible_candles * 0.3f;
    float rightmost_pan = std::max(0.0f, total_candles - visible_candles);
    float max_pan = rightmost_pan + extra_scroll;

    // Default to showing latest candles (pan_x < 0 means "show latest")
    if (view->pan_x < 0.0f) {
        view->pan_x = rightmost_pan;  // Position so last candle is at right edge
    }
    view->pan_x = std::max(0.0f, std::min(view->pan_x, max_pan));

    float start_candle = view->pan_x;
    float end_candle = std::min(start_candle + visible_candles, total_candles);

    int start_idx = static_cast<int>(start_candle);
    int end_idx = std::min(static_cast<int>(std::ceil(end_candle)), static_cast<int>(m_candles.size()));

    // Find price range
    float min_price = m_candles[static_cast<size_t>(start_idx)].low;
    float max_price = m_candles[static_cast<size_t>(start_idx)].high;
    for (int i = start_idx; i < end_idx; i++) {
        const Candle& c = m_candles[static_cast<size_t>(i)];
        if (c.low < min_price) min_price = c.low;
        if (c.high > max_price) max_price = c.high;
    }

    // Include indicators in price range
    if (m_settings && m_settings->sma_enabled && !m_sma_values.empty()) {
        for (int i = start_idx; i < end_idx; i++) {
            float v = m_sma_values[static_cast<size_t>(i)];
            if (v > 0) {
                if (v < min_price) min_price = v;
                if (v > max_price) max_price = v;
            }
        }
    }
    if (m_settings && m_settings->boll_enabled && !m_boll_upper.empty()) {
        for (int i = start_idx; i < end_idx; i++) {
            float u = m_boll_upper[static_cast<size_t>(i)];
            float l = m_boll_lower[static_cast<size_t>(i)];
            if (u > 0) {
                if (l < min_price) min_price = l;
                if (u > max_price) max_price = u;
            }
        }
    }

    // Include current price in range so the price line is always visible
    if (m_current_price > 0.0f) {
        if (m_current_price < min_price) min_price = m_current_price;
        if (m_current_price > max_price) max_price = m_current_price;
    }

    // Include auto S/R levels in price range so they're always visible
    for (const auto& sr : m_auto_sr_levels) {
        if (sr.price < min_price) min_price = sr.price;
        if (sr.price > max_price) max_price = sr.price;
    }

    // Add padding to price range
    float price_range = max_price - min_price;
    if (price_range < 0.01f) price_range = 0.01f;
    min_price -= price_range * 0.05f;
    max_price += price_range * 0.05f;
    price_range = max_price - min_price;

    float chart_width = canvas_size.x - padding_left - padding_right;
    float chart_height = main_chart_height - padding_top * 2.0f;
    float candle_width = chart_width / visible_candles;
    float body_width = std::max(1.0f, candle_width * 0.7f);

    // Coordinate conversion lambdas
    auto priceToY = [&](float price) -> float {
        return chart_pos.y + padding_top + chart_height - ((price - min_price) / price_range * chart_height);
    };

    auto yToPrice = [&](float y) -> float {
        float rel_y = (y - chart_pos.y - padding_top);
        return max_price - (rel_y / chart_height) * price_range;
    };

    auto candleToX = [&](float candle_idx) -> float {
        float offset = candle_idx - start_candle;
        return chart_pos.x + padding_left + offset * candle_width + candle_width / 2.0f;
    };

    auto xToCandle = [&](float x) -> float {
        // Account for candle centering (candleToX adds candle_width/2)
        return start_candle + (x - chart_pos.x - padding_left - candle_width / 2.0f) / candle_width;
    };

    // Handle keyboard navigation
    if (is_hovered || ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) view->pan_x -= visible_candles * 0.1f;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) view->pan_x += visible_candles * 0.1f;
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_Equal)) {
            view->zoom = std::min(view->zoom * 1.2f, ZOOM_MAX);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_Minus)) {
            view->zoom = std::max(view->zoom * 0.8f, ZOOM_MIN);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) view->reset();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) && m_hlines && m_trendlines) {
            m_hlines->erase(std::remove_if(m_hlines->begin(), m_hlines->end(),
                [](const HLine& h) { return h.selected; }), m_hlines->end());
            m_trendlines->erase(std::remove_if(m_trendlines->begin(), m_trendlines->end(),
                [](const TrendLine& t) { return t.selected; }), m_trendlines->end());
        }
    }

    // Handle zoom with mouse wheel
    if (is_hovered && std::fabs(io.MouseWheel) > 0.0f) {
        float mouse_candle = xToCandle(io.MousePos.x);
        view->zoom *= (io.MouseWheel > 0) ? 1.1f : 0.9f;
        view->zoom = std::max(ZOOM_MIN, std::min(view->zoom, ZOOM_MAX));
        float new_visible = total_candles / view->zoom;
        float mouse_ratio = (io.MousePos.x - chart_pos.x - padding_left) / chart_width;
        view->pan_x = mouse_candle - mouse_ratio * new_visible;
    }

    // Handle pan with left drag
    if (m_draw_mode == ChartDrawMode::NONE && m_dragging_hline < 0 && m_dragging_trendline < 0) {
        if (is_active && !m_is_panning && !double_clicked) {
            // Start panning
            m_is_panning = true;
            m_pan_start_x = io.MousePos.x;
            m_pan_start_pan = view->pan_x;
        }
        if (m_is_panning) {
            if (is_active) {
                float delta = (m_pan_start_x - io.MousePos.x) / candle_width;
                view->pan_x = m_pan_start_pan + delta;
            } else {
                m_is_panning = false;
            }
        }
    } else if (!is_active) {
        m_is_panning = false;
    }

    // Determine hovered candle
    m_hovered_candle = -1;
    if (is_hovered && io.MousePos.y < chart_pos.y + main_chart_height) {
        float hover_candle = xToCandle(io.MousePos.x);
        int idx = static_cast<int>(hover_candle);
        if (idx >= 0 && idx < static_cast<int>(m_candles.size())) {
            m_hovered_candle = idx;
        }
    }

    // Draw grid
    for (int i = 0; i <= 5; i++) {
        float price = min_price + (price_range * static_cast<float>(i) / 5.0f);
        float y = priceToY(price);
        draw_list->AddLine(
            ImVec2(chart_pos.x + padding_left, y),
            ImVec2(chart_pos.x + canvas_size.x - padding_right, y),
            make_color(40, 40, 45));

        char label[32];
        std::snprintf(label, sizeof(label), "%.2f", static_cast<double>(price));
        draw_list->AddText(ImVec2(chart_pos.x + canvas_size.x - padding_right + 5, y - 6), make_color(120, 120, 120), label);
    }

    // Draw extended hours background shading (pre-market and after-hours)
    // Only for intraday timeframes (1m, 5m)
    if (m_timeframe == Timeframe::M1 || m_timeframe == Timeframe::M5) {
        ImU32 extended_hours_color = make_color(35, 35, 42);  // Slightly lighter than background
        MarketSession prev_session = MarketSession::REGULAR;
        float region_start_x = -1.0f;

        for (int i = start_idx; i <= end_idx; i++) {
            MarketSession session = MarketSession::REGULAR;
            float x = 0.0f;

            if (i < end_idx) {
                const Candle& c = m_candles[static_cast<size_t>(i)];
                session = get_session_from_timestamp(c.timestamp);
                x = candleToX(static_cast<float>(i));
            }

            // Check for session transition or end of range
            bool is_extended = (session == MarketSession::PRE_MARKET || session == MarketSession::AFTER_HOURS);
            bool was_extended = (prev_session == MarketSession::PRE_MARKET || prev_session == MarketSession::AFTER_HOURS);

            if (was_extended && (!is_extended || i == end_idx)) {
                // End of extended hours region - draw it
                if (region_start_x >= 0.0f) {
                    float region_end_x = (i < end_idx) ? x - candle_width / 2.0f : candleToX(static_cast<float>(i - 1)) + candle_width / 2.0f;
                    draw_list->AddRectFilled(
                        ImVec2(std::max(region_start_x, chart_pos.x + padding_left), chart_pos.y + padding_top),
                        ImVec2(std::min(region_end_x, chart_pos.x + canvas_size.x - padding_right), chart_pos.y + main_chart_height),
                        extended_hours_color);
                }
                region_start_x = -1.0f;
            }

            if (is_extended && !was_extended) {
                // Start of extended hours region
                region_start_x = x - candle_width / 2.0f;
            }

            prev_session = session;
        }
    }

    // Draw Bollinger bands
    if (m_settings && m_settings->boll_enabled && !m_boll_upper.empty()) {
        for (int i = start_idx + 1; i < end_idx; i++) {
            float u1 = m_boll_upper[static_cast<size_t>(i - 1)];
            float u2 = m_boll_upper[static_cast<size_t>(i)];
            float l1 = m_boll_lower[static_cast<size_t>(i - 1)];
            float l2 = m_boll_lower[static_cast<size_t>(i)];
            if (u1 > 0 && u2 > 0) {
                float x1 = candleToX(static_cast<float>(i - 1));
                float x2 = candleToX(static_cast<float>(i));
                ImVec2 pts[4] = {
                    ImVec2(x1, priceToY(u1)), ImVec2(x2, priceToY(u2)),
                    ImVec2(x2, priceToY(l2)), ImVec2(x1, priceToY(l1))
                };
                draw_list->AddConvexPolyFilled(pts, 4, make_color(100, 100, 150, 30));
                draw_list->AddLine(ImVec2(x1, priceToY(u1)), ImVec2(x2, priceToY(u2)), make_color(150, 150, 150), 1.0f);
                draw_list->AddLine(ImVec2(x1, priceToY(l1)), ImVec2(x2, priceToY(l2)), make_color(150, 150, 150), 1.0f);
            }
        }
    }

    // Draw SMA
    if (m_settings && m_settings->sma_enabled && !m_sma_values.empty()) {
        for (int i = start_idx + 1; i < end_idx; i++) {
            float v1 = m_sma_values[static_cast<size_t>(i - 1)];
            float v2 = m_sma_values[static_cast<size_t>(i)];
            if (v1 > 0 && v2 > 0) {
                draw_list->AddLine(
                    ImVec2(candleToX(static_cast<float>(i - 1)), priceToY(v1)),
                    ImVec2(candleToX(static_cast<float>(i)), priceToY(v2)),
                    make_color(255, 150, 0), 1.5f);
            }
        }
    }

    // Draw EMA
    if (m_settings && m_settings->ema_enabled && !m_ema_values.empty()) {
        for (int i = start_idx + 1; i < end_idx; i++) {
            float v1 = m_ema_values[static_cast<size_t>(i - 1)];
            float v2 = m_ema_values[static_cast<size_t>(i)];
            if (v1 > 0 && v2 > 0) {
                draw_list->AddLine(
                    ImVec2(candleToX(static_cast<float>(i - 1)), priceToY(v1)),
                    ImVec2(candleToX(static_cast<float>(i)), priceToY(v2)),
                    make_color(50, 200, 255), 1.5f);
            }
        }
    }

    // Draw candles
    for (int i = start_idx; i < end_idx; i++) {
        const Candle& c = m_candles[static_cast<size_t>(i)];
        float x = candleToX(static_cast<float>(i));

        if (x < chart_pos.x + padding_left || x > chart_pos.x + canvas_size.x - padding_right) continue;

        float open_y = priceToY(c.open);
        float close_y = priceToY(c.close);
        float high_y = priceToY(c.high);
        float low_y = priceToY(c.low);

        bool bullish = c.close >= c.open;
        ImU32 color = bullish ? make_color(38, 166, 91) : make_color(214, 69, 65);

        draw_list->AddLine(ImVec2(x, high_y), ImVec2(x, low_y), color, 1.0f);

        float body_top = bullish ? close_y : open_y;
        float body_bottom = bullish ? open_y : close_y;

        if (body_bottom - body_top < 1.0f) {
            draw_list->AddLine(ImVec2(x - body_width / 2, open_y), ImVec2(x + body_width / 2, open_y), color, 2.0f);
        } else {
            draw_list->AddRectFilled(ImVec2(x - body_width / 2, body_top), ImVec2(x + body_width / 2, body_bottom), color);
        }
    }

    // Draw current price line (use m_current_price if set, otherwise fall back to last candle)
    float current_price = (m_current_price > 0.0f) ? m_current_price : m_candles.back().close;
    float price_y = priceToY(current_price);

    // Clamp Y position to chart bounds for the label
    float label_y = price_y;
    float min_y = chart_pos.y + padding_top;
    float max_y = chart_pos.y + main_chart_height - padding_top;
    bool price_in_range = (price_y >= min_y && price_y <= max_y);

    if (price_in_range) {
        // Draw price line if in visible range
        draw_dashed_line(draw_list,
            ImVec2(chart_pos.x + padding_left, price_y),
            ImVec2(chart_pos.x + canvas_size.x - padding_right, price_y),
            make_color(50, 150, 255), 1.0f, 4.0f);
    } else {
        // Clamp label position to edge of chart
        label_y = (price_y < min_y) ? min_y : max_y;
    }

    // Always draw the price label box
    char price_label[32];
    std::snprintf(price_label, sizeof(price_label), "%.2f", static_cast<double>(current_price));
    draw_list->AddRectFilled(
        ImVec2(chart_pos.x + canvas_size.x - padding_right + 2, label_y - 8),
        ImVec2(chart_pos.x + canvas_size.x - 2, label_y + 8),
        make_color(50, 150, 255));
    draw_list->AddText(ImVec2(chart_pos.x + canvas_size.x - padding_right + 5, label_y - 6), make_color(255, 255, 255), price_label);

    // Draw horizontal lines
    if (m_hlines) {
        for (size_t i = 0; i < m_hlines->size(); i++) {
            HLine& hl = (*m_hlines)[i];
            float y = priceToY(hl.price);
            if (y >= chart_pos.y + padding_top && y <= chart_pos.y + main_chart_height - padding_top) {
                float base_thickness = hl.selected ? 3.0f : 2.0f;
                float thickness = base_thickness * get_thickness_multiplier(hl.source_tf, m_timeframe);
                draw_styled_line(draw_list,
                    ImVec2(chart_pos.x + padding_left, y),
                    ImVec2(chart_pos.x + canvas_size.x - padding_right, y),
                    hl.color, thickness, hl.style);

                char label[32];
                std::snprintf(label, sizeof(label), "%.2f", static_cast<double>(hl.price));
                draw_list->AddText(ImVec2(chart_pos.x + canvas_size.x - padding_right + 5, y - 6), hl.color, label);

                // Check for hover/drag
                if (is_hovered && m_draw_mode == ChartDrawMode::NONE && !m_is_panning) {
                    float dist = std::fabs(io.MousePos.y - y);
                    if (dist < 5.0f) {
                        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            m_dragging_hline = static_cast<int>(i);
                            m_is_panning = false;
                        }
                    }
                }
            }
        }

        // Handle hline dragging
        if (m_dragging_hline >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                (*m_hlines)[static_cast<size_t>(m_dragging_hline)].price = yToPrice(io.MousePos.y);
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            } else {
                m_dragging_hline = -1;
            }
        }
    }

    // Draw automatic support/resistance levels (from daily candles)
    if (!m_auto_sr_levels.empty()) {
        ImU32 resistance_color = make_color(180, 80, 80, 180);   // Red-ish
        ImU32 support_color = make_color(80, 180, 80, 180);      // Green-ish

        for (const AutoSRLevel& level : m_auto_sr_levels) {
            float y = priceToY(level.price);
            if (y >= chart_pos.y + padding_top && y <= chart_pos.y + main_chart_height - padding_top) {
                ImU32 color = level.is_resistance ? resistance_color : support_color;
                draw_dashed_line(draw_list,
                    ImVec2(chart_pos.x + padding_left, y),
                    ImVec2(chart_pos.x + canvas_size.x - padding_right, y),
                    color, 1.0f, 6.0f);

                // Draw price label
                char label[32];
                std::snprintf(label, sizeof(label), "%.2f", static_cast<double>(level.price));
                draw_list->AddText(ImVec2(chart_pos.x + padding_left + 5, y - 12), color, label);
            }
        }
    }

    // Draw trend lines (only on their source timeframe)
    if (m_trendlines) {
        for (size_t i = 0; i < m_trendlines->size(); i++) {
            TrendLine& tl = (*m_trendlines)[i];
            // Only show trendlines on the timeframe where they were drawn
            if (tl.source_tf != m_timeframe) continue;

            float x1 = candleToX(tl.candle_start);
            float y1 = priceToY(tl.price_start);
            float x2 = candleToX(tl.candle_end);
            float y2 = priceToY(tl.price_end);

            float base_thickness = tl.selected ? 3.0f : 2.0f;
            float thickness = base_thickness * get_thickness_multiplier(tl.source_tf, m_timeframe);
            draw_styled_line(draw_list, ImVec2(x1, y1), ImVec2(x2, y2), tl.color, thickness, tl.style);

            if (tl.selected) {
                draw_list->AddCircleFilled(ImVec2(x1, y1), 5.0f, tl.color);
                draw_list->AddCircleFilled(ImVec2(x2, y2), 5.0f, tl.color);
            }

            // Check for drag on endpoints
            if (is_hovered && m_draw_mode == ChartDrawMode::NONE && !m_is_panning && m_dragging_hline < 0) {
                float dist1 = std::sqrt((io.MousePos.x - x1) * (io.MousePos.x - x1) + (io.MousePos.y - y1) * (io.MousePos.y - y1));
                float dist2 = std::sqrt((io.MousePos.x - x2) * (io.MousePos.x - x2) + (io.MousePos.y - y2) * (io.MousePos.y - y2));
                if (dist1 < 8.0f || dist2 < 8.0f) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        m_dragging_trendline = static_cast<int>(i);
                        m_dragging_trendline_point = (dist1 < dist2) ? 0 : 1;
                        m_is_panning = false;
                    }
                }
            }
        }

        if (m_dragging_trendline >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                TrendLine& tl = (*m_trendlines)[static_cast<size_t>(m_dragging_trendline)];
                float candle_f = xToCandle(io.MousePos.x);
                // Keep exact position for dragging
                float max_candle = static_cast<float>(m_candles.size()) - 1.0f;
                candle_f = std::max(0.0f, std::min(candle_f, max_candle));
                float price = yToPrice(io.MousePos.y);

                if (m_dragging_trendline_point == 0) {
                    tl.candle_start = candle_f;
                    tl.price_start = price;
                } else {
                    tl.candle_end = candle_f;
                    tl.price_end = price;
                }
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            } else {
                m_dragging_trendline = -1;
            }
        }
    }

    // Draw trend line preview
    if (m_trendline_drawing && m_trendline_start_candle >= 0.0f) {
        float x1 = candleToX(m_trendline_start_candle);
        float y1 = priceToY(m_trendline_start_price);
        draw_styled_line(draw_list, ImVec2(x1, y1), io.MousePos, m_draw_color, 2.0f, m_draw_style);
    }

    // Handle drawing
    if (m_draw_mode == ChartDrawMode::HLINE && is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_hlines) {
        float click_y = io.MousePos.y;
        if (click_y >= chart_pos.y + padding_top && click_y <= chart_pos.y + main_chart_height - padding_top) {
            HLine new_line(yToPrice(click_y), m_draw_color, m_draw_style, m_timeframe);
            m_hlines->push_back(new_line);
        }
    }

    if (m_draw_mode == ChartDrawMode::TRENDLINE && is_hovered && m_trendlines) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float candle_f = xToCandle(io.MousePos.x);
            // Clamp to valid range but keep as float for exact positioning
            float max_candle = static_cast<float>(m_candles.size()) - 1.0f;
            candle_f = std::max(0.0f, std::min(candle_f, max_candle));

            if (!m_trendline_drawing) {
                m_trendline_drawing = true;
                m_trendline_start_candle = candle_f;
                m_trendline_start_price = yToPrice(io.MousePos.y);
            } else {
                TrendLine tl;
                tl.candle_start = m_trendline_start_candle;
                tl.candle_end = candle_f;
                tl.price_start = m_trendline_start_price;
                tl.price_end = yToPrice(io.MousePos.y);
                tl.color = m_draw_color;
                tl.style = m_draw_style;
                tl.source_tf = m_timeframe;
                tl.selected = false;
                m_trendlines->push_back(tl);
                m_trendline_drawing = false;
                m_trendline_start_candle = -1.0f;
            }
        }
    }

    // Right-click to cancel or select
    if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        if (m_trendline_drawing) {
            m_trendline_drawing = false;
            m_trendline_start_candle = -1.0f;
        } else {
            if (m_hlines) for (auto& h : *m_hlines) h.selected = false;
            if (m_trendlines) for (auto& t : *m_trendlines) t.selected = false;

            if (m_hlines) {
                float click_price = yToPrice(io.MousePos.y);
                float threshold = price_range * 0.02f;
                int nearest = -1;
                float nearest_dist = threshold;

                for (size_t i = 0; i < m_hlines->size(); i++) {
                    float dist = std::fabs((*m_hlines)[i].price - click_price);
                    if (dist < nearest_dist) {
                        nearest_dist = dist;
                        nearest = static_cast<int>(i);
                    }
                }
                if (nearest >= 0) {
                    (*m_hlines)[static_cast<size_t>(nearest)].selected = true;
                }
            }
        }
    }

    // Right-click context menu
    char popup_id[64];
    std::snprintf(popup_id, sizeof(popup_id), "chart_context_%p", static_cast<void*>(this));
    if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        ImGui::OpenPopup(popup_id);
    }
    if (ImGui::BeginPopup(popup_id)) {
        // Check if any line is selected
        bool has_selected = false;
        if (m_hlines) {
            for (const auto& h : *m_hlines) {
                if (h.selected) { has_selected = true; break; }
            }
        }
        if (!has_selected && m_trendlines) {
            for (const auto& t : *m_trendlines) {
                if (t.selected) { has_selected = true; break; }
            }
        }

        if (has_selected) {
            if (ImGui::MenuItem("Delete Selected")) {
                if (m_hlines) {
                    m_hlines->erase(std::remove_if(m_hlines->begin(), m_hlines->end(),
                        [](const HLine& h) { return h.selected; }), m_hlines->end());
                }
                if (m_trendlines) {
                    m_trendlines->erase(std::remove_if(m_trendlines->begin(), m_trendlines->end(),
                        [](const TrendLine& t) { return t.selected; }), m_trendlines->end());
                }
            }
        }

        if (ImGui::MenuItem("Clear Lines (This Timeframe)")) {
            // Only clear lines drawn on this timeframe
            if (m_hlines) {
                m_hlines->erase(std::remove_if(m_hlines->begin(), m_hlines->end(),
                    [this](const HLine& h) { return h.source_tf == m_timeframe; }), m_hlines->end());
            }
            if (m_trendlines) {
                m_trendlines->erase(std::remove_if(m_trendlines->begin(), m_trendlines->end(),
                    [this](const TrendLine& t) { return t.source_tf == m_timeframe; }), m_trendlines->end());
            }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Reset Zoom")) {
            if (m_view) m_view->reset();
        }

        ImGui::EndPopup();
    }

    // Show crosshair
    if (is_hovered && !m_is_panning && m_dragging_hline < 0 && m_dragging_trendline < 0) {
        float hover_y = io.MousePos.y;
        if (hover_y >= chart_pos.y + padding_top && hover_y <= chart_pos.y + main_chart_height - padding_top) {
            draw_list->AddLine(
                ImVec2(chart_pos.x + padding_left, hover_y),
                ImVec2(chart_pos.x + canvas_size.x - padding_right, hover_y),
                make_color(80, 80, 80, 150), 1.0f);
        }
        draw_list->AddLine(
            ImVec2(io.MousePos.x, chart_pos.y + padding_top),
            ImVec2(io.MousePos.x, chart_pos.y + main_chart_height - padding_top),
            make_color(80, 80, 80, 150), 1.0f);

        float hover_price = yToPrice(io.MousePos.y);
        char label[32];
        std::snprintf(label, sizeof(label), "%.2f", static_cast<double>(hover_price));
        draw_list->AddRectFilled(
            ImVec2(chart_pos.x + canvas_size.x - padding_right + 2, hover_y - 8),
            ImVec2(chart_pos.x + canvas_size.x - 2, hover_y + 8),
            make_color(60, 60, 60));
        draw_list->AddText(ImVec2(chart_pos.x + canvas_size.x - padding_right + 5, hover_y - 6), make_color(200, 200, 200), label);
    }

    // Draw OHLC info box for hovered candle
    if (m_hovered_candle >= 0 && m_hovered_candle < static_cast<int>(m_candles.size())) {
        const Candle& hc = m_candles[static_cast<size_t>(m_hovered_candle)];
        bool bullish = hc.close >= hc.open;
        ImU32 price_color = bullish ? make_color(38, 166, 91) : make_color(214, 69, 65);

        char ohlc_text[128];
        std::snprintf(ohlc_text, sizeof(ohlc_text), "O: %.2f  H: %.2f  L: %.2f  C: %.2f  V: %.0f",
            static_cast<double>(hc.open), static_cast<double>(hc.high),
            static_cast<double>(hc.low), static_cast<double>(hc.close),
            static_cast<double>(hc.volume));

        ImVec2 text_size = ImGui::CalcTextSize(ohlc_text);
        float box_x = chart_pos.x + padding_left + 5;
        float box_y = chart_pos.y + padding_top + 5;

        draw_list->AddRectFilled(
            ImVec2(box_x - 3, box_y - 2),
            ImVec2(box_x + text_size.x + 3, box_y + text_size.y + 2),
            make_color(30, 30, 35, 220));
        draw_list->AddText(ImVec2(box_x, box_y), price_color, ohlc_text);
    }

    // Draw volume panel
    if (show_volume) {
        float vol_top = chart_pos.y + main_chart_height;
        float vol_bottom = vol_top + volume_height - 5.0f;

        float max_vol = 0;
        for (int i = start_idx; i < end_idx; i++) {
            if (m_candles[static_cast<size_t>(i)].volume > max_vol) {
                max_vol = m_candles[static_cast<size_t>(i)].volume;
            }
        }

        draw_list->AddLine(
            ImVec2(chart_pos.x + padding_left, vol_top),
            ImVec2(chart_pos.x + canvas_size.x - padding_right, vol_top),
            make_color(60, 60, 60));

        if (max_vol > 0) {
            for (int i = start_idx; i < end_idx; i++) {
                const Candle& c = m_candles[static_cast<size_t>(i)];
                float x = candleToX(static_cast<float>(i));
                if (x < chart_pos.x + padding_left || x > chart_pos.x + canvas_size.x - padding_right) continue;

                float bar_height = (c.volume / max_vol) * (vol_bottom - vol_top - 5.0f);
                bool bullish = c.close >= c.open;
                ImU32 color = bullish ? make_color(38, 166, 91, 150) : make_color(214, 69, 65, 150);

                draw_list->AddRectFilled(
                    ImVec2(x - body_width / 2, vol_bottom - bar_height),
                    ImVec2(x + body_width / 2, vol_bottom), color);
            }
        }
    }

    // Draw time axis
    float axis_y = chart_pos.y + canvas_size.y - time_axis_height + 5.0f - title_height;
    int label_interval = std::max(1, static_cast<int>(visible_candles / 6.0f));
    for (int i = start_idx; i < end_idx; i += label_interval) {
        float x = candleToX(static_cast<float>(i));
        if (x >= chart_pos.x + padding_left && x <= chart_pos.x + canvas_size.x - padding_right) {
            draw_list->AddLine(
                ImVec2(x, chart_pos.y + main_chart_height + volume_height),
                ImVec2(x, axis_y - 2), make_color(60, 60, 60));

            const char* ts = m_candles[static_cast<size_t>(i)].timestamp;
            char label[16];
            if (m_timeframe == Timeframe::DAILY) {
                // For daily: show MM-DD (extract from "YYYY-MM-DD" or "YYYY-MM-DD HH:MM:SS")
                if (std::strlen(ts) >= 10) {
                    std::snprintf(label, sizeof(label), "%.5s", ts + 5);  // "MM-DD"
                } else {
                    std::snprintf(label, sizeof(label), "%s", ts);
                }
            } else {
                // For intraday: show HH:MM (extract time part)
                const char* time_part = std::strchr(ts, ' ');
                if (time_part != nullptr) {
                    time_part++;  // Skip space
                } else {
                    time_part = ts;  // No space, assume it's just time
                }
                // Copy just HH:MM (5 chars)
                std::snprintf(label, sizeof(label), "%.5s", time_part);
            }
            ImVec2 text_size = ImGui::CalcTextSize(label);
            draw_list->AddText(ImVec2(x - text_size.x / 2, axis_y), make_color(120, 120, 120), label);
        }
    }

    return double_clicked;
}
