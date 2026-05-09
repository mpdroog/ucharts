// chart_widget.h - Reusable candlestick chart widget
#ifndef CHART_WIDGET_H
#define CHART_WIDGET_H

#include "imgui.h"
#include "types.h"
#include <vector>
#include <string>

// Drawing mode
enum class ChartDrawMode {
    NONE = 0,
    HLINE = 1,
    TRENDLINE = 2
};

// Market session type (US Eastern Time)
enum class MarketSession {
    PRE_MARKET,     // 04:00 - 09:30 ET
    REGULAR,        // 09:30 - 16:00 ET
    AFTER_HOURS     // 16:00 - 20:00 ET
};

// Chart widget for rendering candlestick charts with indicators
class ChartWidget {
public:
    ChartWidget();

    // Set chart data
    void set_symbol(const char* symbol);
    void set_candles(const std::vector<Candle>& candles);
    void set_daily_candles(const std::vector<Candle>* daily_candles);  // For S/R calculation
    void set_title(const char* title);
    void set_current_price(float price);

    // Set drawing state (shared across charts)
    void set_drawings(std::vector<HLine>* hlines, std::vector<TrendLine>* trendlines);

    // Indicator settings
    void set_indicator_settings(IndicatorSettings* settings);
    void recalculate_indicators();

    // View state
    void reset_view();
    void set_view_state(ChartViewState* state);

    // Drawing tools
    void set_draw_mode(ChartDrawMode mode);
    void set_draw_color(ImU32 color);
    void set_draw_style(LineStyle style);
    void set_timeframe(Timeframe tf);

    // Render the chart
    // Returns true if double-clicked (for fullscreen toggle)
    bool render(ImVec2 size);

    // Get hovered candle index (-1 if none)
    int get_hovered_candle() const;

    // Get the candle data for OHLC display
    const Candle* get_candle(int index) const;

private:
    std::string m_title;
    std::vector<Candle> m_candles;
    float m_current_price;
    std::vector<HLine>* m_hlines;
    std::vector<TrendLine>* m_trendlines;
    IndicatorSettings* m_settings;
    ChartViewState* m_view;
    ChartViewState m_default_view;

    // Drawing state
    ChartDrawMode m_draw_mode;
    ImU32 m_draw_color;
    LineStyle m_draw_style;
    Timeframe m_timeframe;

    // Interaction state
    int m_hovered_candle;
    bool m_is_panning;
    float m_pan_start_x;
    float m_pan_start_pan;
    int m_dragging_hline;
    int m_dragging_trendline;
    int m_dragging_trendline_point;
    bool m_trendline_drawing;
    float m_trendline_start_candle;  // Float for exact positioning
    float m_trendline_start_price;

    // Calculated indicators
    std::vector<float> m_sma_values;
    std::vector<float> m_ema_values;
    std::vector<float> m_boll_upper;
    std::vector<float> m_boll_lower;
    bool m_indicators_dirty;  // Set when candles/settings change
    std::string m_symbol;     // Current symbol being displayed

    // Auto support/resistance levels (calculated from daily candles)
    const std::vector<Candle>* m_daily_candles;  // Pointer to daily candles for S/R calc
    std::vector<AutoSRLevel> m_auto_sr_levels;
    bool m_sr_dirty;
    size_t m_prev_daily_size;  // Track size changes to avoid recalc every frame

    // Helper functions
    void calculate_auto_sr();
    static MarketSession get_session_from_timestamp(const char* timestamp);
    void calculate_sma(int period);
    void calculate_ema(int period);
    void calculate_bollinger(int period, float mult);

    void draw_dashed_line(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, float dash_size);
    void draw_styled_line(ImDrawList* dl, ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, LineStyle style);
};

// Helper to create color without C-style casts
inline ImU32 make_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
    return (static_cast<ImU32>(a) << IM_COL32_A_SHIFT) |
           (static_cast<ImU32>(b) << IM_COL32_B_SHIFT) |
           (static_cast<ImU32>(g) << IM_COL32_G_SHIFT) |
           (static_cast<ImU32>(r) << IM_COL32_R_SHIFT);
}

// Preset colors
static const ImU32 g_chart_colors[] = {
    0xFF00C8FF,  // Yellow (ABGR)
    0xFF3C3CDC,  // Red
    0xFF64C800,  // Green
    0xFFFF9632,  // Blue
    0xFFFFFFFF,  // White
    0xFFFF64C8,  // Purple
};
static const int g_num_chart_colors = 6;

#endif // CHART_WIDGET_H
