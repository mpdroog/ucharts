// TradingView-like Candlestick Chart Application
// Uses Dear ImGui with GLFW + OpenGL3 backend

// Silence OpenGL deprecation warnings on macOS
#define GL_SILENCE_DEPRECATION

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <string>

// Helper to create ImGui color without C-style casts
static constexpr ImU32 MakeColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255) {
    return (static_cast<ImU32>(a) << IM_COL32_A_SHIFT) |
           (static_cast<ImU32>(b) << IM_COL32_B_SHIFT) |
           (static_cast<ImU32>(g) << IM_COL32_G_SHIFT) |
           (static_cast<ImU32>(r) << IM_COL32_R_SHIFT);
}

// Debug helpers to track style push/pop balance
#ifndef NDEBUG
static int g_style_color_depth = 0;
static int g_style_var_depth = 0;

static void DbgPushStyleColor(ImGuiCol idx, ImU32 col) {
    g_style_color_depth++;
    ImGui::PushStyleColor(idx, col);
}

static void DbgPushStyleColor(ImGuiCol idx, const ImVec4& col) {
    g_style_color_depth++;
    ImGui::PushStyleColor(idx, col);
}

static void DbgPopStyleColor(int count = 1) {
    g_style_color_depth -= count;
    assert(g_style_color_depth >= 0 && "PopStyleColor called too many times!");
    ImGui::PopStyleColor(count);
}

static void DbgPushStyleVar(ImGuiStyleVar idx, float val) {
    g_style_var_depth++;
    ImGui::PushStyleVar(idx, val);
}

static void DbgPopStyleVar(int count = 1) {
    g_style_var_depth -= count;
    assert(g_style_var_depth >= 0 && "PopStyleVar called too many times!");
    ImGui::PopStyleVar(count);
}

static void DbgCheckStyleBalance() {
    assert(g_style_color_depth == 0 && "Unbalanced PushStyleColor/PopStyleColor!");
    assert(g_style_var_depth == 0 && "Unbalanced PushStyleVar/PopStyleVar!");
}

#define PUSH_STYLE_COLOR DbgPushStyleColor
#define POP_STYLE_COLOR DbgPopStyleColor
#define PUSH_STYLE_VAR DbgPushStyleVar
#define POP_STYLE_VAR DbgPopStyleVar
#define CHECK_STYLE_BALANCE() DbgCheckStyleBalance()
#else
#define PUSH_STYLE_COLOR ImGui::PushStyleColor
#define POP_STYLE_COLOR ImGui::PopStyleColor
#define PUSH_STYLE_VAR ImGui::PushStyleVar
#define POP_STYLE_VAR ImGui::PopStyleVar
#define CHECK_STYLE_BALANCE() ((void)0)
#endif

// Line styles
enum LineStyle { STYLE_SOLID = 0, STYLE_DASHED = 1, STYLE_DOTTED = 2 };

// Drawing modes
enum DrawMode { DRAW_NONE = 0, DRAW_HLINE = 1, DRAW_TRENDLINE = 2 };

// Preset colors
static const ImU32 g_preset_colors[] = {
    MakeColor(255, 200, 0),    // Yellow
    MakeColor(220, 60, 60),    // Red
    MakeColor(0, 200, 100),    // Green
    MakeColor(50, 150, 255),   // Blue
    MakeColor(255, 255, 255),  // White
    MakeColor(200, 100, 255),  // Purple
};
static const int g_num_colors = 6;

// OHLC data structure with timestamp and volume
struct Candle {
    char timestamp[32];
    float open;
    float high;
    float low;
    float close;
    float volume;
};

// Horizontal line structure
struct HLine {
    float price;
    ImU32 color;
    LineStyle style;
    bool selected;
};

// Trend line structure
struct TrendLine {
    int candle_start;
    int candle_end;
    float price_start;
    float price_end;
    ImU32 color;
    LineStyle style;
    bool selected;
};

// Indicator data
struct IndicatorData {
    bool enabled;
    int period;
    ImU32 color;
    std::vector<float> values;
};

// Global data
static std::vector<Candle> g_candles;
static std::vector<HLine> g_hlines;
static std::vector<TrendLine> g_trendlines;
static char g_csv_path[256] = "sample.csv";

// Zoom and pan state
static float g_zoom = 1.0f;
static float g_pan_x = 0.0f;
static constexpr float ZOOM_MIN = 0.1f;
static constexpr float ZOOM_MAX = 10.0f;

// Drawing state
static DrawMode g_draw_mode = DRAW_NONE;
static int g_current_color_idx = 0;
static LineStyle g_current_style = STYLE_SOLID;
static int g_hovered_candle = -1;

// Trend line drawing state
static bool g_trendline_drawing = false;
static int g_trendline_start_candle = -1;
static float g_trendline_start_price = 0.0f;

// Dragging state
static int g_dragging_hline = -1;
static int g_dragging_trendline = -1;
static int g_dragging_trendline_point = -1;  // 0=start, 1=end

// Indicators
static IndicatorData g_sma = {false, 20, MakeColor(255, 150, 0), {}};
static IndicatorData g_ema = {false, 9, MakeColor(50, 200, 255), {}};
static IndicatorData g_boll_upper = {false, 20, MakeColor(150, 150, 150), {}};
static IndicatorData g_boll_lower = {false, 20, MakeColor(150, 150, 150), {}};
static bool g_show_volume = true;

// Helper to draw dashed line
static void DrawDashedLine(ImDrawList* draw_list, ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, float dash_size = 5.0f) {
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
            draw_list->AddLine(
                ImVec2(p1.x + dx * pos, p1.y + dy * pos),
                ImVec2(p1.x + dx * (pos + segment), p1.y + dy * (pos + segment)),
                color, thickness);
        }
        pos += dash_size;
        draw = !draw;
    }
}

// Helper to draw styled line
static void DrawStyledLine(ImDrawList* draw_list, ImVec2 p1, ImVec2 p2, ImU32 color, float thickness, LineStyle style) {
    switch (style) {
        case STYLE_DASHED:
            DrawDashedLine(draw_list, p1, p2, color, thickness, 8.0f);
            break;
        case STYLE_DOTTED:
            DrawDashedLine(draw_list, p1, p2, color, thickness, 3.0f);
            break;
        case STYLE_SOLID:
            draw_list->AddLine(p1, p2, color, thickness);
            break;
    }
}

// Calculate SMA
static void CalculateSMA(int period, std::vector<float>& out) {
    out.clear();
    out.resize(g_candles.size(), 0.0f);

    for (std::size_t i = 0; i < g_candles.size(); i++) {
        if (i < static_cast<std::size_t>(period - 1)) {
            out[i] = 0.0f;
            continue;
        }
        float sum = 0.0f;
        for (int j = 0; j < period; j++) {
            sum += g_candles[i - static_cast<std::size_t>(j)].close;
        }
        out[i] = sum / static_cast<float>(period);
    }
}

// Calculate EMA
static void CalculateEMA(int period, std::vector<float>& out) {
    out.clear();
    out.resize(g_candles.size(), 0.0f);

    if (g_candles.empty()) return;

    float k = 2.0f / (static_cast<float>(period) + 1.0f);

    // Start with SMA for first value
    float sum = 0.0f;
    for (int i = 0; i < period && i < static_cast<int>(g_candles.size()); i++) {
        sum += g_candles[static_cast<std::size_t>(i)].close;
    }
    out[static_cast<std::size_t>(period - 1)] = sum / static_cast<float>(period);

    for (std::size_t i = static_cast<std::size_t>(period); i < g_candles.size(); i++) {
        out[i] = g_candles[i].close * k + out[i - 1] * (1.0f - k);
    }
}

// Calculate Bollinger Bands
static void CalculateBollinger(int period, float mult, std::vector<float>& upper, std::vector<float>& lower) {
    std::vector<float> sma;
    CalculateSMA(period, sma);

    upper.clear();
    lower.clear();
    upper.resize(g_candles.size(), 0.0f);
    lower.resize(g_candles.size(), 0.0f);

    for (std::size_t i = static_cast<std::size_t>(period - 1); i < g_candles.size(); i++) {
        float sum_sq = 0.0f;
        for (int j = 0; j < period; j++) {
            float diff = g_candles[i - static_cast<std::size_t>(j)].close - sma[i];
            sum_sq += diff * diff;
        }
        float stddev = std::sqrt(sum_sq / static_cast<float>(period));
        upper[i] = sma[i] + mult * stddev;
        lower[i] = sma[i] - mult * stddev;
    }
}

// Recalculate all indicators
static void RecalculateIndicators() {
    if (g_sma.enabled) {
        CalculateSMA(g_sma.period, g_sma.values);
    }
    if (g_ema.enabled) {
        CalculateEMA(g_ema.period, g_ema.values);
    }
    if (g_boll_upper.enabled) {
        CalculateBollinger(g_boll_upper.period, 2.0f, g_boll_upper.values, g_boll_lower.values);
    }
}

// Parse CSV file with timestamp,open,high,low,close,volume format
static bool LoadCSV(const char* filename) {
    FILE* file = std::fopen(filename, "r");
    if (file == nullptr) {
        std::fprintf(stderr, "Failed to open file: %s\n", filename);
        return false;
    }

    g_candles.clear();
    g_hlines.clear();
    g_trendlines.clear();
    g_zoom = 1.0f;
    g_pan_x = 0.0f;

    char line[512];
    int line_num = 0;

    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        line_num++;

        // Skip header line
        if (line_num == 1 && (std::strstr(line, "timestamp") != nullptr ||
                              std::strstr(line, "open") != nullptr ||
                              std::strstr(line, "Open") != nullptr)) {
            continue;
        }

        Candle c = {};

        // Try new format: timestamp,open,high,low,close,volume
        char ts[32] = "";
        int parsed = std::sscanf(line, "%31[^,],%f,%f,%f,%f,%f",
            ts, &c.open, &c.high, &c.low, &c.close, &c.volume);

        if (parsed >= 5) {
            std::strncpy(c.timestamp, ts, sizeof(c.timestamp) - 1);
            if (parsed < 6) c.volume = 0.0f;
            g_candles.push_back(c);
            continue;
        }

        // Try old format: open,high,low,close
        parsed = std::sscanf(line, "%f,%f,%f,%f", &c.open, &c.high, &c.low, &c.close);
        if (parsed == 4) {
            std::snprintf(c.timestamp, sizeof(c.timestamp), "%d", line_num);
            c.volume = 0.0f;
            g_candles.push_back(c);
        }
    }

    std::fclose(file);
    std::printf("Loaded %zu candles from %s\n", g_candles.size(), filename);

    RecalculateIndicators();
    return !g_candles.empty();
}

// Draw the main chart
static void DrawCandlestickChart(ImVec2 size, float volume_height) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = size;

    if (canvas_size.x <= 0.0f) {
        canvas_size.x = ImGui::GetContentRegionAvail().x;
    }
    if (canvas_size.y <= 0.0f) {
        canvas_size.y = ImGui::GetContentRegionAvail().y - 10.0f;
    }

    // Reserve space for volume panel and time axis
    const float time_axis_height = 25.0f;
    float main_chart_height = canvas_size.y - time_axis_height;
    if (g_show_volume && !g_candles.empty() && g_candles[0].volume > 0.0f) {
        main_chart_height -= volume_height;
    }

    // Draw background
    draw_list->AddRectFilled(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        MakeColor(20, 20, 25));
    draw_list->AddRect(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        MakeColor(60, 60, 60));

    // Create invisible button for interaction
    ImGui::InvisibleButton("chart_area", canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool is_hovered = ImGui::IsItemHovered();
    const ImGuiIO& io = ImGui::GetIO();

    if (g_candles.empty()) {
        return;
    }

    // Calculate dimensions
    const float padding_left = 10.0f;
    const float padding_right = 60.0f;
    const float padding_top = 10.0f;
    const float chart_width = canvas_size.x - padding_left - padding_right;
    const float chart_height = main_chart_height - padding_top * 2.0f;

    // Calculate visible range based on zoom and pan
    const float total_candles = static_cast<float>(g_candles.size());
    const float visible_candles = total_candles / g_zoom;
    const float max_pan = std::max(0.0f, total_candles - visible_candles);

    g_pan_x = std::max(0.0f, std::min(g_pan_x, max_pan));

    const float start_candle = g_pan_x;
    const float end_candle = std::min(start_candle + visible_candles, total_candles);

    int start_idx = static_cast<int>(start_candle);
    int end_idx = static_cast<int>(std::ceil(end_candle));
    end_idx = std::min(end_idx, static_cast<int>(g_candles.size()));

    // Find price range for visible candles
    float min_price = g_candles[static_cast<std::size_t>(start_idx)].low;
    float max_price = g_candles[static_cast<std::size_t>(start_idx)].high;
    for (int i = start_idx; i < end_idx; i++) {
        const Candle& c = g_candles[static_cast<std::size_t>(i)];
        if (c.low < min_price) min_price = c.low;
        if (c.high > max_price) max_price = c.high;
    }

    // Include indicators in price range
    if (g_sma.enabled) {
        for (int i = start_idx; i < end_idx; i++) {
            float v = g_sma.values[static_cast<std::size_t>(i)];
            if (v > 0.0f) {
                if (v < min_price) min_price = v;
                if (v > max_price) max_price = v;
            }
        }
    }
    if (g_boll_upper.enabled) {
        for (int i = start_idx; i < end_idx; i++) {
            float u = g_boll_upper.values[static_cast<std::size_t>(i)];
            float l = g_boll_lower.values[static_cast<std::size_t>(i)];
            if (u > 0.0f) {
                if (l < min_price) min_price = l;
                if (u > max_price) max_price = u;
            }
        }
    }

    // Add padding to price range
    float price_range = max_price - min_price;
    if (price_range < 0.01f) price_range = 0.01f;
    min_price -= price_range * 0.05f;
    max_price += price_range * 0.05f;
    price_range = max_price - min_price;

    const float candle_width = chart_width / visible_candles;
    const float body_width = std::max(1.0f, candle_width * 0.7f);

    // Helper to convert price to Y coordinate
    auto priceToY = [&](float price) -> float {
        return canvas_pos.y + padding_top + chart_height - ((price - min_price) / price_range * chart_height);
    };

    // Helper to convert Y coordinate to price
    auto yToPrice = [&](float y) -> float {
        float rel_y = (y - canvas_pos.y - padding_top);
        return max_price - (rel_y / chart_height) * price_range;
    };

    // Helper to convert candle index to X coordinate
    auto candleToX = [&](float candle_idx) -> float {
        float offset = candle_idx - start_candle;
        return canvas_pos.x + padding_left + offset * candle_width + candle_width / 2.0f;
    };

    // Helper to convert X to candle index
    auto xToCandle = [&](float x) -> float {
        return start_candle + (x - canvas_pos.x - padding_left) / candle_width;
    };

    // Handle keyboard navigation
    if (is_hovered || ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            g_pan_x -= visible_candles * 0.1f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            g_pan_x += visible_candles * 0.1f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) || ImGui::IsKeyPressed(ImGuiKey_Equal)) {
            g_zoom *= 1.2f;
            g_zoom = std::min(g_zoom, ZOOM_MAX);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_Minus)) {
            g_zoom *= 0.8f;
            g_zoom = std::max(g_zoom, ZOOM_MIN);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            g_zoom = 1.0f;
            g_pan_x = 0.0f;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            // Delete selected items
            g_hlines.erase(std::remove_if(g_hlines.begin(), g_hlines.end(),
                [](const HLine& h) { return h.selected; }), g_hlines.end());
            g_trendlines.erase(std::remove_if(g_trendlines.begin(), g_trendlines.end(),
                [](const TrendLine& t) { return t.selected; }), g_trendlines.end());
        }
    }

    // Handle zoom with mouse wheel
    if (is_hovered && std::fabs(io.MouseWheel) > 0.0f) {
        float mouse_candle = xToCandle(io.MousePos.x);
        g_zoom *= (io.MouseWheel > 0) ? 1.1f : 0.9f;
        g_zoom = std::max(ZOOM_MIN, std::min(g_zoom, ZOOM_MAX));

        float new_visible = total_candles / g_zoom;
        float mouse_ratio = (io.MousePos.x - canvas_pos.x - padding_left) / chart_width;
        g_pan_x = mouse_candle - mouse_ratio * new_visible;
    }

    // Handle pan with left mouse drag (when not in draw mode)
    static bool is_panning = false;
    static float pan_start_x = 0.0f;
    static float pan_start_pan = 0.0f;

    if (g_draw_mode == DRAW_NONE && g_dragging_hline < 0 && g_dragging_trendline < 0) {
        if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            is_panning = true;
            pan_start_x = io.MousePos.x;
            pan_start_pan = g_pan_x;
        }
    }
    if (is_panning) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float delta = (pan_start_x - io.MousePos.x) / candle_width;
            g_pan_x = pan_start_pan + delta;
        } else {
            is_panning = false;
        }
    }

    // Determine hovered candle
    g_hovered_candle = -1;
    if (is_hovered && io.MousePos.y < canvas_pos.y + main_chart_height) {
        float hover_candle = xToCandle(io.MousePos.x);
        int idx = static_cast<int>(hover_candle);
        if (idx >= 0 && idx < static_cast<int>(g_candles.size())) {
            g_hovered_candle = idx;
        }
    }

    // Draw grid lines
    const int num_grid_lines = 5;
    for (int i = 0; i <= num_grid_lines; i++) {
        float price = min_price + (price_range * static_cast<float>(i) / static_cast<float>(num_grid_lines));
        float y = priceToY(price);
        draw_list->AddLine(
            ImVec2(canvas_pos.x + padding_left, y),
            ImVec2(canvas_pos.x + canvas_size.x - padding_right, y),
            MakeColor(40, 40, 45));

        char price_label[32];
        std::snprintf(price_label, sizeof(price_label), "%.2f", static_cast<double>(price));
        draw_list->AddText(ImVec2(canvas_pos.x + canvas_size.x - padding_right + 5.0f, y - 6.0f),
            MakeColor(120, 120, 120), price_label);
    }

    // Draw Bollinger Bands (fill area)
    if (g_boll_upper.enabled && g_boll_upper.values.size() == g_candles.size()) {
        for (int i = start_idx + 1; i < end_idx; i++) {
            float u1 = g_boll_upper.values[static_cast<std::size_t>(i - 1)];
            float u2 = g_boll_upper.values[static_cast<std::size_t>(i)];
            float l1 = g_boll_lower.values[static_cast<std::size_t>(i - 1)];
            float l2 = g_boll_lower.values[static_cast<std::size_t>(i)];
            if (u1 > 0.0f && u2 > 0.0f) {
                float x1 = candleToX(static_cast<float>(i - 1));
                float x2 = candleToX(static_cast<float>(i));
                // Draw band as quad
                ImVec2 points[4] = {
                    ImVec2(x1, priceToY(u1)),
                    ImVec2(x2, priceToY(u2)),
                    ImVec2(x2, priceToY(l2)),
                    ImVec2(x1, priceToY(l1))
                };
                draw_list->AddConvexPolyFilled(points, 4, MakeColor(100, 100, 150, 30));
            }
        }
        // Draw lines
        for (int i = start_idx + 1; i < end_idx; i++) {
            float v1 = g_boll_upper.values[static_cast<std::size_t>(i - 1)];
            float v2 = g_boll_upper.values[static_cast<std::size_t>(i)];
            if (v1 > 0.0f && v2 > 0.0f) {
                draw_list->AddLine(
                    ImVec2(candleToX(static_cast<float>(i - 1)), priceToY(v1)),
                    ImVec2(candleToX(static_cast<float>(i)), priceToY(v2)),
                    g_boll_upper.color, 1.0f);
            }
            v1 = g_boll_lower.values[static_cast<std::size_t>(i - 1)];
            v2 = g_boll_lower.values[static_cast<std::size_t>(i)];
            if (v1 > 0.0f && v2 > 0.0f) {
                draw_list->AddLine(
                    ImVec2(candleToX(static_cast<float>(i - 1)), priceToY(v1)),
                    ImVec2(candleToX(static_cast<float>(i)), priceToY(v2)),
                    g_boll_lower.color, 1.0f);
            }
        }
    }

    // Draw SMA
    if (g_sma.enabled && g_sma.values.size() == g_candles.size()) {
        for (int i = start_idx + 1; i < end_idx; i++) {
            float v1 = g_sma.values[static_cast<std::size_t>(i - 1)];
            float v2 = g_sma.values[static_cast<std::size_t>(i)];
            if (v1 > 0.0f && v2 > 0.0f) {
                draw_list->AddLine(
                    ImVec2(candleToX(static_cast<float>(i - 1)), priceToY(v1)),
                    ImVec2(candleToX(static_cast<float>(i)), priceToY(v2)),
                    g_sma.color, 1.5f);
            }
        }
    }

    // Draw EMA
    if (g_ema.enabled && g_ema.values.size() == g_candles.size()) {
        for (int i = start_idx + 1; i < end_idx; i++) {
            float v1 = g_ema.values[static_cast<std::size_t>(i - 1)];
            float v2 = g_ema.values[static_cast<std::size_t>(i)];
            if (v1 > 0.0f && v2 > 0.0f) {
                draw_list->AddLine(
                    ImVec2(candleToX(static_cast<float>(i - 1)), priceToY(v1)),
                    ImVec2(candleToX(static_cast<float>(i)), priceToY(v2)),
                    g_ema.color, 1.5f);
            }
        }
    }

    // Draw candles
    for (int i = start_idx; i < end_idx; i++) {
        const Candle& c = g_candles[static_cast<std::size_t>(i)];
        float x = candleToX(static_cast<float>(i));

        if (x < canvas_pos.x + padding_left || x > canvas_pos.x + canvas_size.x - padding_right) {
            continue;
        }

        float open_y = priceToY(c.open);
        float close_y = priceToY(c.close);
        float high_y = priceToY(c.high);
        float low_y = priceToY(c.low);

        bool bullish = c.close >= c.open;
        ImU32 color = bullish ? MakeColor(38, 166, 91) : MakeColor(214, 69, 65);

        // Draw wick
        draw_list->AddLine(ImVec2(x, high_y), ImVec2(x, low_y), color, 1.0f);

        // Draw body
        float body_top = bullish ? close_y : open_y;
        float body_bottom = bullish ? open_y : close_y;

        if (body_bottom - body_top < 1.0f) {
            draw_list->AddLine(
                ImVec2(x - body_width / 2.0f, open_y),
                ImVec2(x + body_width / 2.0f, open_y),
                color, 2.0f);
        } else {
            draw_list->AddRectFilled(
                ImVec2(x - body_width / 2.0f, body_top),
                ImVec2(x + body_width / 2.0f, body_bottom),
                color);
        }
    }

    // Draw current price line (last close)
    {
        float last_close = g_candles.back().close;
        float y = priceToY(last_close);
        if (y >= canvas_pos.y + padding_top && y <= canvas_pos.y + main_chart_height - padding_top) {
            DrawDashedLine(draw_list,
                ImVec2(canvas_pos.x + padding_left, y),
                ImVec2(canvas_pos.x + canvas_size.x - padding_right, y),
                MakeColor(50, 150, 255), 1.0f, 4.0f);

            char price_label[32];
            std::snprintf(price_label, sizeof(price_label), "%.2f", static_cast<double>(last_close));
            draw_list->AddRectFilled(
                ImVec2(canvas_pos.x + canvas_size.x - padding_right + 2.0f, y - 8.0f),
                ImVec2(canvas_pos.x + canvas_size.x - 2.0f, y + 8.0f),
                MakeColor(50, 150, 255));
            draw_list->AddText(
                ImVec2(canvas_pos.x + canvas_size.x - padding_right + 5.0f, y - 6.0f),
                MakeColor(255, 255, 255), price_label);
        }
    }

    // Draw horizontal lines
    for (std::size_t i = 0; i < g_hlines.size(); i++) {
        HLine& hl = g_hlines[i];
        float y = priceToY(hl.price);
        if (y >= canvas_pos.y + padding_top && y <= canvas_pos.y + main_chart_height - padding_top) {
            float thickness = hl.selected ? 3.0f : 2.0f;
            DrawStyledLine(draw_list,
                ImVec2(canvas_pos.x + padding_left, y),
                ImVec2(canvas_pos.x + canvas_size.x - padding_right, y),
                hl.color, thickness, hl.style);

            char hl_label[32];
            std::snprintf(hl_label, sizeof(hl_label), "%.2f", static_cast<double>(hl.price));
            draw_list->AddText(
                ImVec2(canvas_pos.x + canvas_size.x - padding_right + 5.0f, y - 6.0f),
                hl.color, hl_label);

            // Check for hover/drag
            if (is_hovered && g_draw_mode == DRAW_NONE && !is_panning) {
                float dist = std::fabs(io.MousePos.y - y);
                if (dist < 5.0f) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        g_dragging_hline = static_cast<int>(i);
                        is_panning = false;
                    }
                }
            }
        }
    }

    // Handle horizontal line dragging
    if (g_dragging_hline >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            g_hlines[static_cast<std::size_t>(g_dragging_hline)].price = yToPrice(io.MousePos.y);
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        } else {
            g_dragging_hline = -1;
        }
    }

    // Draw trend lines
    for (std::size_t i = 0; i < g_trendlines.size(); i++) {
        TrendLine& tl = g_trendlines[i];
        float x1 = candleToX(static_cast<float>(tl.candle_start));
        float y1 = priceToY(tl.price_start);
        float x2 = candleToX(static_cast<float>(tl.candle_end));
        float y2 = priceToY(tl.price_end);

        float thickness = tl.selected ? 3.0f : 2.0f;
        DrawStyledLine(draw_list, ImVec2(x1, y1), ImVec2(x2, y2), tl.color, thickness, tl.style);

        // Draw handles if selected
        if (tl.selected) {
            draw_list->AddCircleFilled(ImVec2(x1, y1), 5.0f, tl.color);
            draw_list->AddCircleFilled(ImVec2(x2, y2), 5.0f, tl.color);
        }

        // Check for hover/drag on endpoints
        if (is_hovered && g_draw_mode == DRAW_NONE && !is_panning && g_dragging_hline < 0) {
            float dist1 = std::sqrt((io.MousePos.x - x1) * (io.MousePos.x - x1) +
                                    (io.MousePos.y - y1) * (io.MousePos.y - y1));
            float dist2 = std::sqrt((io.MousePos.x - x2) * (io.MousePos.x - x2) +
                                    (io.MousePos.y - y2) * (io.MousePos.y - y2));
            if (dist1 < 8.0f || dist2 < 8.0f) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    g_dragging_trendline = static_cast<int>(i);
                    g_dragging_trendline_point = (dist1 < dist2) ? 0 : 1;
                    is_panning = false;
                }
            }
        }
    }

    // Handle trend line dragging
    if (g_dragging_trendline >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            TrendLine& tl = g_trendlines[static_cast<std::size_t>(g_dragging_trendline)];
            float candle_f = xToCandle(io.MousePos.x);
            int candle_idx = static_cast<int>(std::round(candle_f));
            candle_idx = std::max(0, std::min(candle_idx, static_cast<int>(g_candles.size()) - 1));
            float price = yToPrice(io.MousePos.y);

            if (g_dragging_trendline_point == 0) {
                tl.candle_start = candle_idx;
                tl.price_start = price;
            } else {
                tl.candle_end = candle_idx;
                tl.price_end = price;
            }
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        } else {
            g_dragging_trendline = -1;
        }
    }

    // Draw trend line preview while drawing
    if (g_trendline_drawing && g_trendline_start_candle >= 0) {
        float x1 = candleToX(static_cast<float>(g_trendline_start_candle));
        float y1 = priceToY(g_trendline_start_price);
        DrawStyledLine(draw_list, ImVec2(x1, y1), io.MousePos,
            g_preset_colors[g_current_color_idx], 2.0f, g_current_style);
    }

    // Handle drawing
    if (g_draw_mode == DRAW_HLINE && is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        float click_y = io.MousePos.y;
        if (click_y >= canvas_pos.y + padding_top && click_y <= canvas_pos.y + main_chart_height - padding_top) {
            HLine new_line = {yToPrice(click_y), g_preset_colors[g_current_color_idx], g_current_style, false};
            g_hlines.push_back(new_line);
        }
    }

    if (g_draw_mode == DRAW_TRENDLINE && is_hovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            float candle_f = xToCandle(io.MousePos.x);
            int candle_idx = static_cast<int>(std::round(candle_f));
            candle_idx = std::max(0, std::min(candle_idx, static_cast<int>(g_candles.size()) - 1));

            if (!g_trendline_drawing) {
                g_trendline_drawing = true;
                g_trendline_start_candle = candle_idx;
                g_trendline_start_price = yToPrice(io.MousePos.y);
            } else {
                TrendLine tl = {
                    g_trendline_start_candle, candle_idx,
                    g_trendline_start_price, yToPrice(io.MousePos.y),
                    g_preset_colors[g_current_color_idx], g_current_style, false
                };
                g_trendlines.push_back(tl);
                g_trendline_drawing = false;
                g_trendline_start_candle = -1;
            }
        }
    }

    // Right-click to delete nearest line
    if (is_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        // Cancel trend line drawing
        if (g_trendline_drawing) {
            g_trendline_drawing = false;
            g_trendline_start_candle = -1;
        } else {
            // Deselect all
            for (auto& h : g_hlines) h.selected = false;
            for (auto& t : g_trendlines) t.selected = false;

            // Find nearest line
            float click_price = yToPrice(io.MousePos.y);
            float threshold = price_range * 0.02f;
            int nearest_hline = -1;
            float nearest_dist = threshold;

            for (std::size_t i = 0; i < g_hlines.size(); i++) {
                float dist = std::fabs(g_hlines[i].price - click_price);
                if (dist < nearest_dist) {
                    nearest_dist = dist;
                    nearest_hline = static_cast<int>(i);
                }
            }

            if (nearest_hline >= 0) {
                g_hlines[static_cast<std::size_t>(nearest_hline)].selected = true;
            }
        }
    }

    // Show crosshair
    if (is_hovered && !is_panning && g_dragging_hline < 0 && g_dragging_trendline < 0) {
        float hover_price = yToPrice(io.MousePos.y);
        float hover_y = io.MousePos.y;

        if (hover_y >= canvas_pos.y + padding_top && hover_y <= canvas_pos.y + main_chart_height - padding_top) {
            draw_list->AddLine(
                ImVec2(canvas_pos.x + padding_left, hover_y),
                ImVec2(canvas_pos.x + canvas_size.x - padding_right, hover_y),
                MakeColor(80, 80, 80, 150), 1.0f);
        }

        draw_list->AddLine(
            ImVec2(io.MousePos.x, canvas_pos.y + padding_top),
            ImVec2(io.MousePos.x, canvas_pos.y + main_chart_height - padding_top),
            MakeColor(80, 80, 80, 150), 1.0f);

        // Price label on crosshair
        char hover_label[32];
        std::snprintf(hover_label, sizeof(hover_label), "%.2f", static_cast<double>(hover_price));
        draw_list->AddRectFilled(
            ImVec2(canvas_pos.x + canvas_size.x - padding_right + 2.0f, hover_y - 8.0f),
            ImVec2(canvas_pos.x + canvas_size.x - 2.0f, hover_y + 8.0f),
            MakeColor(60, 60, 60));
        draw_list->AddText(
            ImVec2(canvas_pos.x + canvas_size.x - padding_right + 5.0f, hover_y - 6.0f),
            MakeColor(200, 200, 200), hover_label);
    }

    // Draw volume panel
    if (g_show_volume && !g_candles.empty() && g_candles[0].volume > 0.0f) {
        float vol_top = canvas_pos.y + main_chart_height;
        float vol_bottom = vol_top + volume_height - 5.0f;

        // Find max volume in visible range
        float max_vol = 0.0f;
        for (int i = start_idx; i < end_idx; i++) {
            if (g_candles[static_cast<std::size_t>(i)].volume > max_vol) {
                max_vol = g_candles[static_cast<std::size_t>(i)].volume;
            }
        }

        // Draw separator
        draw_list->AddLine(
            ImVec2(canvas_pos.x + padding_left, vol_top),
            ImVec2(canvas_pos.x + canvas_size.x - padding_right, vol_top),
            MakeColor(60, 60, 60));

        // Draw volume bars
        if (max_vol > 0.0f) {
            for (int i = start_idx; i < end_idx; i++) {
                const Candle& c = g_candles[static_cast<std::size_t>(i)];
                float x = candleToX(static_cast<float>(i));

                if (x < canvas_pos.x + padding_left || x > canvas_pos.x + canvas_size.x - padding_right) {
                    continue;
                }

                float bar_height = (c.volume / max_vol) * (vol_bottom - vol_top - 5.0f);
                bool bullish = c.close >= c.open;
                ImU32 color = bullish ? MakeColor(38, 166, 91, 150) : MakeColor(214, 69, 65, 150);

                draw_list->AddRectFilled(
                    ImVec2(x - body_width / 2.0f, vol_bottom - bar_height),
                    ImVec2(x + body_width / 2.0f, vol_bottom),
                    color);
            }
        }
    }

    // Draw time axis
    {
        float axis_y = canvas_pos.y + canvas_size.y - time_axis_height + 5.0f;

        // Draw time labels at intervals
        int label_interval = std::max(1, static_cast<int>(visible_candles / 8.0f));
        for (int i = start_idx; i < end_idx; i += label_interval) {
            float x = candleToX(static_cast<float>(i));
            if (x >= canvas_pos.x + padding_left && x <= canvas_pos.x + canvas_size.x - padding_right) {
                draw_list->AddLine(
                    ImVec2(x, canvas_pos.y + main_chart_height + (g_show_volume ? volume_height : 0.0f)),
                    ImVec2(x, axis_y - 2.0f),
                    MakeColor(60, 60, 60));

                const char* label = g_candles[static_cast<std::size_t>(i)].timestamp;
                ImVec2 text_size = ImGui::CalcTextSize(label);
                draw_list->AddText(
                    ImVec2(x - text_size.x / 2.0f, axis_y),
                    MakeColor(120, 120, 120), label);
            }
        }
    }
}

static void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

int main(int argc, char** argv) {
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() == GLFW_FALSE) {
        return 1;
    }

    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1400, 800, "TradingView-like Chart", nullptr, nullptr);
    if (window == nullptr) {
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.FrameRounding = 2.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    if (argc > 1) {
        std::strncpy(g_csv_path, argv[1], sizeof(g_csv_path) - 1);
        g_csv_path[sizeof(g_csv_path) - 1] = '\0';
    }
    LoadCSV(g_csv_path);

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Chart", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        // Toolbar row 1
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("##csvpath", g_csv_path, sizeof(g_csv_path));
        ImGui::SameLine();
        if (ImGui::Button("Load")) {
            LoadCSV(g_csv_path);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            g_zoom = 1.0f;
            g_pan_x = 0.0f;
        }

        ImGui::SameLine();
        ImGui::Text(" | ");
        ImGui::SameLine();

        // Drawing mode selector
        const char* mode_names[] = { "Pan", "H-Line", "Trend" };
        for (int i = 0; i < 3; i++) {
            if (i > 0) ImGui::SameLine();
            bool was_active = (g_draw_mode == i);
            if (was_active) {
                PUSH_STYLE_COLOR(ImGuiCol_Button, MakeColor(80, 80, 120));
            }
            if (ImGui::Button(mode_names[i])) {
                g_draw_mode = static_cast<DrawMode>(i);
                g_trendline_drawing = false;
                g_trendline_start_candle = -1;
            }
            if (was_active) {
                POP_STYLE_COLOR();
            }
        }

        ImGui::SameLine();
        ImGui::Text(" | ");
        ImGui::SameLine();

        // Color selector
        ImGui::Text("Color:");
        ImGui::SameLine();
        for (int i = 0; i < g_num_colors; i++) {
            ImGui::SameLine();
            ImGui::PushID(i);
            ImVec4 col = ImGui::ColorConvertU32ToFloat4(g_preset_colors[i]);
            bool was_selected = (g_current_color_idx == i);
            if (was_selected) {
                PUSH_STYLE_VAR(ImGuiStyleVar_FrameBorderSize, 2.0f);
                PUSH_STYLE_COLOR(ImGuiCol_Border, ImVec4(1, 1, 1, 1));
            }
            if (ImGui::ColorButton("##col", col, ImGuiColorEditFlags_NoTooltip, ImVec2(20, 20))) {
                g_current_color_idx = i;
            }
            if (was_selected) {
                POP_STYLE_COLOR();
                POP_STYLE_VAR();
            }
            ImGui::PopID();
        }

        ImGui::SameLine();
        ImGui::Text(" | ");
        ImGui::SameLine();

        // Style selector
        const char* style_names[] = { "Solid", "Dashed", "Dotted" };
        ImGui::Text("Style:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80);
        if (ImGui::BeginCombo("##style", style_names[g_current_style])) {
            for (int i = 0; i < 3; i++) {
                if (ImGui::Selectable(style_names[i], g_current_style == i)) {
                    g_current_style = static_cast<LineStyle>(i);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        if (ImGui::Button("Clear All")) {
            g_hlines.clear();
            g_trendlines.clear();
        }

        // Toolbar row 2 - Indicators
        bool sma_changed = ImGui::Checkbox("SMA", &g_sma.enabled);
        ImGui::SameLine();
        if (g_sma.enabled) {
            ImGui::SetNextItemWidth(50);
            sma_changed |= ImGui::InputInt("##sma_period", &g_sma.period, 0);
            g_sma.period = std::max(2, std::min(200, g_sma.period));
            ImGui::SameLine();
        }

        bool ema_changed = ImGui::Checkbox("EMA", &g_ema.enabled);
        ImGui::SameLine();
        if (g_ema.enabled) {
            ImGui::SetNextItemWidth(50);
            ema_changed |= ImGui::InputInt("##ema_period", &g_ema.period, 0);
            g_ema.period = std::max(2, std::min(200, g_ema.period));
            ImGui::SameLine();
        }

        bool boll_changed = ImGui::Checkbox("Bollinger", &g_boll_upper.enabled);
        g_boll_lower.enabled = g_boll_upper.enabled;
        ImGui::SameLine();
        if (g_boll_upper.enabled) {
            ImGui::SetNextItemWidth(50);
            boll_changed |= ImGui::InputInt("##boll_period", &g_boll_upper.period, 0);
            g_boll_upper.period = std::max(2, std::min(200, g_boll_upper.period));
            g_boll_lower.period = g_boll_upper.period;
            ImGui::SameLine();
        }

        ImGui::Checkbox("Volume", &g_show_volume);

        if (sma_changed || ema_changed || boll_changed) {
            RecalculateIndicators();
        }

        ImGui::Separator();

        // OHLC Info Panel
        if (g_hovered_candle >= 0 && g_hovered_candle < static_cast<int>(g_candles.size())) {
            const Candle& c = g_candles[static_cast<std::size_t>(g_hovered_candle)];
            float prev_close = (g_hovered_candle > 0) ?
                g_candles[static_cast<std::size_t>(g_hovered_candle - 1)].close : c.open;
            float change = c.close - prev_close;
            float change_pct = (prev_close > 0.0f) ? (change / prev_close * 100.0f) : 0.0f;

            ImU32 change_color = (change >= 0.0f) ? MakeColor(38, 166, 91) : MakeColor(214, 69, 65);
            char sign = (change >= 0.0f) ? '+' : '-';

            ImGui::Text("%s  ", c.timestamp);
            ImGui::SameLine();
            ImGui::Text("O: %.2f  H: %.2f  L: %.2f  C: %.2f",
                static_cast<double>(c.open), static_cast<double>(c.high),
                static_cast<double>(c.low), static_cast<double>(c.close));
            ImGui::SameLine();
            PUSH_STYLE_COLOR(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(change_color));
            ImGui::Text(" %c%.2f (%.2f%%)", sign, static_cast<double>(std::fabs(change)),
                static_cast<double>(std::fabs(change_pct)));
            POP_STYLE_COLOR();
            if (c.volume > 0.0f) {
                ImGui::SameLine();
                ImGui::Text("  Vol: %.0f", static_cast<double>(c.volume));
            }
        } else {
            ImGui::Text("Hover over chart to see OHLC data");
        }

        ImGui::Separator();

        // Draw chart
        float volume_height = g_show_volume ? 80.0f : 0.0f;
        DrawCandlestickChart(ImVec2(0, 0), volume_height);

        ImGui::End();

        CHECK_STYLE_BALANCE();
        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
