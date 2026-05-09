// Professional Trading Platform
// Uses Dear ImGui with GLFW + OpenGL3 backend

// Silence OpenGL deprecation warnings on macOS
#define GL_SILENCE_DEPRECATION

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include "types.h"
#include "database.h"
#include "market_data.h"
#include "order_manager.h"
#include "chart_widget.h"
#include "ticker_widget.h"
#include "positions_widget.h"
#include "iqfeed_tcp.h"
#include "logger.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <vector>
#include <algorithm>
#include <string>
#include <thread>
#include <chrono>

// Global state
static Database g_database;
// MarketData is a singleton - use get_market_data()
static OrderManager g_order_manager;

// Widgets
static TickerWidget g_ticker_widgets[NUM_TICKERS];
static PositionsWidget g_positions_widget;
static ChartWidget g_chart_1m;
static ChartWidget g_chart_5m;
static ChartWidget g_chart_daily;

// Application state
static int g_selected_ticker = 0;
static bool g_fullscreen_chart = false;
static int g_fullscreen_chart_idx = -1;  // 0=1m, 1=5m, 2=daily

// Shared drawing state per symbol
struct SymbolState {
    char symbol[MAX_SYMBOL_LEN];
    std::vector<HLine> hlines;
    std::vector<TrendLine> trendlines;
    IndicatorSettings indicators;
    ChartViewState view_1m;
    ChartViewState view_5m;
    ChartViewState view_daily;

    SymbolState() {
        symbol[0] = '\0';
        // Zoom in on intraday charts by default
        view_1m.zoom = 18.0f;
        view_5m.zoom = 12.0f;
        view_daily.zoom = 1.0f;
    }
};
static SymbolState g_symbol_states[NUM_TICKERS];

// Undo/redo for drawings
struct DrawingSnapshot {
    std::vector<HLine> hlines;
    std::vector<TrendLine> trendlines;
};
static const size_t MAX_UNDO_HISTORY = 50;
static std::vector<DrawingSnapshot> g_undo_stack[NUM_TICKERS];
static std::vector<DrawingSnapshot> g_redo_stack[NUM_TICKERS];

static void undo_drawing(int ticker_idx) {
    if (ticker_idx < 0 || ticker_idx >= NUM_TICKERS) return;
    if (g_undo_stack[ticker_idx].empty()) return;

    // Save current state to redo
    DrawingSnapshot current;
    current.hlines = g_symbol_states[ticker_idx].hlines;
    current.trendlines = g_symbol_states[ticker_idx].trendlines;
    g_redo_stack[ticker_idx].push_back(current);

    // Restore previous state
    DrawingSnapshot& prev = g_undo_stack[ticker_idx].back();
    g_symbol_states[ticker_idx].hlines = prev.hlines;
    g_symbol_states[ticker_idx].trendlines = prev.trendlines;
    g_undo_stack[ticker_idx].pop_back();
}

static void redo_drawing(int ticker_idx) {
    if (ticker_idx < 0 || ticker_idx >= NUM_TICKERS) return;
    if (g_redo_stack[ticker_idx].empty()) return;

    // Save current state to undo
    DrawingSnapshot current;
    current.hlines = g_symbol_states[ticker_idx].hlines;
    current.trendlines = g_symbol_states[ticker_idx].trendlines;
    g_undo_stack[ticker_idx].push_back(current);

    // Restore redo state
    DrawingSnapshot& next = g_redo_stack[ticker_idx].back();
    g_symbol_states[ticker_idx].hlines = next.hlines;
    g_symbol_states[ticker_idx].trendlines = next.trendlines;
    g_redo_stack[ticker_idx].pop_back();
}

// Drawing tool state
static ChartDrawMode g_draw_mode = ChartDrawMode::NONE;
static int g_current_color_idx = 0;
static LineStyle g_current_style = LineStyle::SOLID;

// Get current NYC time string
static void get_nyc_time(char* buf, size_t buf_size) {
    time_t now = time(nullptr);

    // Get UTC time
    struct tm* utc = gmtime(&now);
    if (utc == nullptr) {
        snprintf(buf, buf_size, "--:--:--");
        return;
    }

    // Convert to NYC (EST = UTC-5, EDT = UTC-4)
    // Simplified: assume EDT (UTC-4) from March to November
    int hour = utc->tm_hour - 4;
    if (hour < 0) hour += 24;

    snprintf(buf, buf_size, "%02d:%02d:%02d (NYSE)", hour, utc->tm_min, utc->tm_sec);
}

// Handle hotkey orders
static void process_hotkeys() {
    if (g_selected_ticker < 0 || g_selected_ticker >= NUM_TICKERS) return;

    const char* symbol = g_ticker_widgets[g_selected_ticker].get_symbol();
    if (symbol == nullptr || symbol[0] == '\0') return;

    float best_bid = g_ticker_widgets[g_selected_ticker].get_best_bid();
    float best_ask = g_ticker_widgets[g_selected_ticker].get_best_ask();

    // Check for Shift+number keys (buy orders)
    if (ImGui::GetIO().KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) {
            g_order_manager.buy(symbol, 100, best_ask + 0.05f);
        } else if (ImGui::IsKeyPressed(ImGuiKey_2)) {
            g_order_manager.buy(symbol, 200, best_ask + 0.05f);
        } else if (ImGui::IsKeyPressed(ImGuiKey_3)) {
            g_order_manager.buy(symbol, 500, best_ask + 0.05f);
        } else if (ImGui::IsKeyPressed(ImGuiKey_4)) {
            g_order_manager.buy(symbol, 1000, best_ask + 0.05f);
        }
    }

    // Check for Ctrl+number keys (sell orders) - also support Command on macOS
    if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper) {
        if (ImGui::IsKeyPressed(ImGuiKey_1)) {
            int qty = g_order_manager.calculate_sell_quantity(symbol, 25);
            if (qty > 0) g_order_manager.sell(symbol, qty, best_bid - 0.05f);
        } else if (ImGui::IsKeyPressed(ImGuiKey_2)) {
            int qty = g_order_manager.calculate_sell_quantity(symbol, 50);
            if (qty > 0) g_order_manager.sell(symbol, qty, best_bid - 0.05f);
        } else if (ImGui::IsKeyPressed(ImGuiKey_3)) {
            int qty = g_order_manager.calculate_sell_quantity(symbol, 75);
            if (qty > 0) g_order_manager.sell(symbol, qty, best_bid - 0.05f);
        } else if (ImGui::IsKeyPressed(ImGuiKey_4)) {
            int qty = g_order_manager.calculate_sell_quantity(symbol, 100);
            if (qty > 0) g_order_manager.sell(symbol, qty, best_bid - 0.05f);
        } else if (ImGui::IsKeyPressed(ImGuiKey_C)) {
            // Sell all at bid-5c
            int qty = g_order_manager.calculate_sell_quantity(symbol, 100);
            if (qty > 0) g_order_manager.sell(symbol, qty, best_bid - 0.05f);
        } else if (ImGui::IsKeyPressed(ImGuiKey_X)) {
            // Cancel all pending orders (Ctrl+X)
            g_order_manager.cancel_all_orders(nullptr);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
            // Undo drawing (Ctrl+Z)
            undo_drawing(g_selected_ticker);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
            // Redo drawing (Ctrl+Y)
            redo_drawing(g_selected_ticker);
        }
    }

    // Escape exits fullscreen
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        g_fullscreen_chart = false;
        g_fullscreen_chart_idx = -1;
    }
}

// Update charts for selected ticker
static void update_charts_for_selected_ticker() {
    if (g_selected_ticker < 0 || g_selected_ticker >= NUM_TICKERS) return;

    const char* symbol = g_ticker_widgets[g_selected_ticker].get_symbol();
    if (symbol == nullptr || symbol[0] == '\0') return;

    SymbolState& state = g_symbol_states[g_selected_ticker];

    // Get candle data from market data (empty is OK if data not yet loaded)
    // Use static for daily candles so chart widgets can maintain a stable pointer for S/R calculation
    std::vector<Candle> candles_1m, candles_5m;
    static std::vector<Candle> s_candles_daily;
    (void)get_market_data().get_candles(symbol, Timeframe::M1, candles_1m, MAX_CANDLES);
    (void)get_market_data().get_candles(symbol, Timeframe::M5, candles_5m, MAX_CANDLES);
    (void)get_market_data().get_candles(symbol, Timeframe::DAILY, s_candles_daily, MAX_CANDLES);
    const std::vector<Candle>& candles_daily = s_candles_daily;

    // Log data availability issues (helps catch bugs early)
    // Use static to only log once per symbol to avoid spam
    static char s_last_warned_symbol[MAX_SYMBOL_LEN] = "";
    static MarketData::LoadingState s_last_warned_state = MarketData::LoadingState::IDLE;

    auto load_state = get_market_data().get_loading_state(symbol);
    bool state_changed = (std::strcmp(s_last_warned_symbol, symbol) != 0 ||
                          s_last_warned_state != load_state);

    if (state_changed) {
        if (load_state == MarketData::LoadingState::ERROR) {
            LOG_E("main", "Symbol '%s' failed to load: %s",
                  symbol, get_market_data().get_loading_error(symbol));
        } else if (load_state == MarketData::LoadingState::COMPLETE) {
            if (candles_1m.empty() && candles_5m.empty() && candles_daily.empty()) {
                LOG_W("main", "Symbol '%s' COMPLETE but no candle data available!", symbol);
            } else {
                // Log which timeframes are missing (useful for debugging IQFeed issues)
                if (candles_1m.empty() || candles_5m.empty() || candles_daily.empty()) {
                    LOG_D("main", "Symbol '%s' partial data: 1m=%zu 5m=%zu daily=%zu",
                          symbol, candles_1m.size(), candles_5m.size(), candles_daily.size());
                }
            }
        } else if (load_state == MarketData::LoadingState::IDLE) {
            LOG_W("main", "Symbol '%s' selected but not loading (state=IDLE)", symbol);
        }

        safe_strcpy(s_last_warned_symbol, symbol, sizeof(s_last_warned_symbol));
        s_last_warned_state = load_state;
    }

    // Update charts (set_symbol first to track ticker changes for dirty flag)
    g_chart_1m.set_symbol(symbol);
    g_chart_1m.set_candles(candles_1m);
    g_chart_1m.set_daily_candles(&s_candles_daily);  // For S/R calculation
    g_chart_1m.set_title("1-Min");
    g_chart_1m.set_drawings(&state.hlines, &state.trendlines);
    g_chart_1m.set_indicator_settings(&state.indicators);
    g_chart_1m.set_view_state(&state.view_1m);

    g_chart_5m.set_symbol(symbol);
    g_chart_5m.set_candles(candles_5m);
    g_chart_5m.set_daily_candles(&s_candles_daily);  // For S/R calculation
    g_chart_5m.set_title("5-Min");
    g_chart_5m.set_drawings(&state.hlines, &state.trendlines);
    g_chart_5m.set_indicator_settings(&state.indicators);
    g_chart_5m.set_view_state(&state.view_5m);

    g_chart_daily.set_symbol(symbol);
    g_chart_daily.set_candles(candles_daily);
    g_chart_daily.set_daily_candles(&s_candles_daily);  // For S/R calculation
    g_chart_daily.set_title("Daily");
    g_chart_daily.set_drawings(&state.hlines, &state.trendlines);
    g_chart_daily.set_indicator_settings(&state.indicators);
    g_chart_daily.set_view_state(&state.view_daily);

    // Set timeframes for line thickness calculation
    g_chart_1m.set_timeframe(Timeframe::M1);
    g_chart_5m.set_timeframe(Timeframe::M5);
    g_chart_daily.set_timeframe(Timeframe::DAILY);

    // Set current price (same across all timeframes)
    float current_price = get_market_data().get_current_price(symbol);
    g_chart_1m.set_current_price(current_price);
    g_chart_5m.set_current_price(current_price);
    g_chart_daily.set_current_price(current_price);

    // Set drawing mode
    g_chart_1m.set_draw_mode(g_draw_mode);
    g_chart_5m.set_draw_mode(g_draw_mode);
    g_chart_daily.set_draw_mode(g_draw_mode);

    g_chart_1m.set_draw_color(g_chart_colors[g_current_color_idx]);
    g_chart_5m.set_draw_color(g_chart_colors[g_current_color_idx]);
    g_chart_daily.set_draw_color(g_chart_colors[g_current_color_idx]);

    g_chart_1m.set_draw_style(g_current_style);
    g_chart_5m.set_draw_style(g_current_style);
    g_chart_daily.set_draw_style(g_current_style);
}

// Load session state from database
static void load_session() {
    if (!g_database.is_open()) return;

    // Load tickers
    char symbols[NUM_TICKERS][MAX_SYMBOL_LEN];
    if (g_database.load_tickers(symbols)) {
        for (int i = 0; i < NUM_TICKERS; ++i) {
            g_ticker_widgets[i].set_symbol(symbols[i]);
            safe_strcpy(g_symbol_states[i].symbol, symbols[i], sizeof(g_symbol_states[i].symbol));

            // Load market data and drawings for each symbol
            if (symbols[i][0] != '\0') {
                if (!get_market_data().load_symbol(symbols[i])) {
                    LOG_W("session", "Failed to load %s: %s", symbols[i], get_market_data().last_error());
                    g_ticker_widgets[i].set_error(get_market_data().last_error());
                }
                g_database.load_hlines(symbols[i], g_symbol_states[i].hlines);
                g_database.load_trendlines(symbols[i], g_symbol_states[i].trendlines);
                g_database.load_indicator_settings(symbols[i], g_symbol_states[i].indicators);
            }
        }
    }

    // Load orders and positions
    g_order_manager.load_from_database();
}

// Save session state to database
static void save_session() {
    if (!g_database.is_open()) return;

    // Save tickers
    char symbol_storage[NUM_TICKERS][MAX_SYMBOL_LEN];
    const char* symbols[NUM_TICKERS];
    for (int i = 0; i < NUM_TICKERS; ++i) {
        const char* sym = g_ticker_widgets[i].get_symbol();
        if (sym != nullptr) {
            safe_strcpy(symbol_storage[i], sym, sizeof(symbol_storage[i]));
        } else {
            symbol_storage[i][0] = '\0';
        }
        symbols[i] = symbol_storage[i];
    }
    g_database.save_tickers(symbols);

    // Save drawings and indicators for each symbol
    for (int i = 0; i < NUM_TICKERS; ++i) {
        if (g_symbol_states[i].symbol[0] != '\0') {
            g_database.save_hlines(g_symbol_states[i].symbol, g_symbol_states[i].hlines);
            g_database.save_trendlines(g_symbol_states[i].symbol, g_symbol_states[i].trendlines);
            g_database.save_indicator_settings(g_symbol_states[i].symbol, g_symbol_states[i].indicators);
        }
    }

    // Save positions
    g_order_manager.save_to_database();
}

static void glfw_error_callback(int error, const char* description) {
    // Always log GLFW errors regardless of verbose mode
    std::fprintf(stderr, "[GLFW] Error %d: %s\n", error, description);
}

int main(int argc, char** argv) {
    // Parse CLI arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-v") == 0 || std::strcmp(argv[i], "--verbose") == 0) {
            set_log_level(LOG_DEBUG);
            LOG_I("main", "Verbose logging enabled");
        }
    }

    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() == GLFW_FALSE) {
        return 1;
    }

    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "Trading Platform", nullptr, nullptr);
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

    // Initialize database
    if (!g_database.init("trading.db")) {
        LOG_E("main", "Failed to initialize database");
    }

    // Initialize order manager
    g_order_manager.init(&g_database, &get_market_data());

    // Initialize ticker widgets
    for (int i = 0; i < NUM_TICKERS; ++i) {
        g_ticker_widgets[i].set_market_data(&get_market_data());
        g_ticker_widgets[i].set_order_manager(&g_order_manager);
    }

    // Initialize positions widget
    g_positions_widget.set_order_manager(&g_order_manager);

    // Load session
    load_session();

    // Set first ticker as selected by default
    g_ticker_widgets[0].set_selected(true);
    g_selected_ticker = 0;

    constexpr auto TARGET_FRAME_TIME = std::chrono::milliseconds(16);  // ~60 FPS

    // CPU profiling counters
    int frame_count = 0;
    auto last_profile_time = std::chrono::steady_clock::now();

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        auto frame_start = std::chrono::steady_clock::now();

        // Log frame rate every 5 seconds
        frame_count++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_profile_time).count();
        if (elapsed >= 5) {
            float fps = static_cast<float>(frame_count) / static_cast<float>(elapsed);
            LOG_I("perf", "Main loop: %.1f FPS (%d frames in %lld sec)",
                  static_cast<double>(fps), frame_count, static_cast<long long>(elapsed));
            frame_count = 0;
            last_profile_time = now;
        }

        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Process order fills
        g_order_manager.process_fills();
        g_order_manager.update_prices();

        // Process hotkeys
        process_hotkeys();

        // Track drawing state for undo (save before any modification)
        static size_t prev_hlines_count[NUM_TICKERS] = {0};
        static size_t prev_trendlines_count[NUM_TICKERS] = {0};
        for (int i = 0; i < NUM_TICKERS; ++i) {
            size_t hcount = g_symbol_states[i].hlines.size();
            size_t tcount = g_symbol_states[i].trendlines.size();
            // Save state if lines were added (new drawing)
            if (hcount > prev_hlines_count[i] || tcount > prev_trendlines_count[i]) {
                // Save the state BEFORE the new line was added
                DrawingSnapshot snapshot;
                if (hcount > prev_hlines_count[i]) {
                    snapshot.hlines.assign(g_symbol_states[i].hlines.begin(),
                        g_symbol_states[i].hlines.begin() + static_cast<long>(prev_hlines_count[i]));
                } else {
                    snapshot.hlines = g_symbol_states[i].hlines;
                }
                if (tcount > prev_trendlines_count[i]) {
                    snapshot.trendlines.assign(g_symbol_states[i].trendlines.begin(),
                        g_symbol_states[i].trendlines.begin() + static_cast<long>(prev_trendlines_count[i]));
                } else {
                    snapshot.trendlines = g_symbol_states[i].trendlines;
                }
                g_undo_stack[i].push_back(snapshot);
                if (g_undo_stack[i].size() > MAX_UNDO_HISTORY) {
                    g_undo_stack[i].erase(g_undo_stack[i].begin());
                }
                g_redo_stack[i].clear();
            }
            prev_hlines_count[i] = hcount;
            prev_trendlines_count[i] = tcount;
        }

        // Update charts for selected ticker
        update_charts_for_selected_ticker();

        // Main window
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("TradingPlatform", nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        float window_width = io.DisplaySize.x;
        float window_height = io.DisplaySize.y;

        // Top bar with clock
        char time_str[32];
        get_nyc_time(time_str, sizeof(time_str));

        float text_width = ImGui::CalcTextSize(time_str).x;
        ImGui::SetCursorPosX((window_width - text_width) / 2.0f);
        ImGui::Text("%s", time_str);

        ImGui::Separator();

        // Toolbar for drawing tools and indicators
        SymbolState* current_state = nullptr;
        if (g_selected_ticker >= 0 && g_selected_ticker < NUM_TICKERS) {
            current_state = &g_symbol_states[g_selected_ticker];
        }

        // Drawing tools section
        ImGui::Text("Draw:");
        ImGui::SameLine();

        if (ImGui::RadioButton("None", g_draw_mode == ChartDrawMode::NONE)) {
            g_draw_mode = ChartDrawMode::NONE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("HLine", g_draw_mode == ChartDrawMode::HLINE)) {
            g_draw_mode = ChartDrawMode::HLINE;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Trend", g_draw_mode == ChartDrawMode::TRENDLINE)) {
            g_draw_mode = ChartDrawMode::TRENDLINE;
        }

        ImGui::SameLine();
        ImGui::Text("  |  Color:");
        ImGui::SameLine();

        // Color buttons
        const char* color_names[] = {"Yellow", "Red", "Green", "Blue", "White", "Purple"};
        for (int i = 0; i < g_num_chart_colors; ++i) {
            ImGui::PushID(i + 100);
            ImVec4 col = ImGui::ColorConvertU32ToFloat4(g_chart_colors[i]);
            if (g_current_color_idx == i) {
                ImGui::PushStyleColor(ImGuiCol_Button, col);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
            } else {
                ImVec4 dim = ImVec4(col.x * 0.5f, col.y * 0.5f, col.z * 0.5f, col.w);
                ImGui::PushStyleColor(ImGuiCol_Button, dim);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
            }
            if (ImGui::SmallButton(color_names[i])) {
                g_current_color_idx = i;
            }
            ImGui::PopStyleColor(3);
            ImGui::PopID();
            if (i < g_num_chart_colors - 1) ImGui::SameLine();
        }

        ImGui::SameLine();
        ImGui::Text("  |  Style:");
        ImGui::SameLine();

        if (ImGui::RadioButton("Solid", g_current_style == LineStyle::SOLID)) {
            g_current_style = LineStyle::SOLID;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Dashed", g_current_style == LineStyle::DASHED)) {
            g_current_style = LineStyle::DASHED;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Dotted", g_current_style == LineStyle::DOTTED)) {
            g_current_style = LineStyle::DOTTED;
        }

        // Clear lines button
        ImGui::SameLine();
        ImGui::Text("  |");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, make_color(150, 50, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, make_color(200, 50, 50, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, make_color(255, 50, 50, 255));
        if (ImGui::SmallButton("Clear Lines") && current_state != nullptr) {
            current_state->hlines.clear();
            current_state->trendlines.clear();
        }
        ImGui::PopStyleColor(3);

        // Indicators section (new line)
        ImGui::Text("Indicators:");
        ImGui::SameLine();

        if (current_state != nullptr) {
            // SMA
            bool sma_changed = ImGui::Checkbox("SMA", &current_state->indicators.sma_enabled);
            ImGui::SameLine();
            ImGui::PushItemWidth(50);
            sma_changed |= ImGui::InputInt("##SMA_Period", &current_state->indicators.sma_period, 0, 0);
            ImGui::PopItemWidth();
            if (current_state->indicators.sma_period < 2) current_state->indicators.sma_period = 2;
            if (current_state->indicators.sma_period > 200) current_state->indicators.sma_period = 200;

            ImGui::SameLine();
            ImGui::Text("  ");
            ImGui::SameLine();

            // EMA
            bool ema_changed = ImGui::Checkbox("EMA", &current_state->indicators.ema_enabled);
            ImGui::SameLine();
            ImGui::PushItemWidth(50);
            ema_changed |= ImGui::InputInt("##EMA_Period", &current_state->indicators.ema_period, 0, 0);
            ImGui::PopItemWidth();
            if (current_state->indicators.ema_period < 2) current_state->indicators.ema_period = 2;
            if (current_state->indicators.ema_period > 200) current_state->indicators.ema_period = 200;

            ImGui::SameLine();
            ImGui::Text("  ");
            ImGui::SameLine();

            // Bollinger Bands
            bool boll_changed = ImGui::Checkbox("Bollinger", &current_state->indicators.boll_enabled);
            ImGui::SameLine();
            ImGui::PushItemWidth(50);
            boll_changed |= ImGui::InputInt("##Boll_Period", &current_state->indicators.boll_period, 0, 0);
            ImGui::PopItemWidth();
            if (current_state->indicators.boll_period < 2) current_state->indicators.boll_period = 2;
            if (current_state->indicators.boll_period > 200) current_state->indicators.boll_period = 200;

            // Recalculate indicators if changed
            if (sma_changed || ema_changed || boll_changed) {
                g_chart_1m.recalculate_indicators();
                g_chart_5m.recalculate_indicators();
                g_chart_daily.recalculate_indicators();
            }
        } else {
            ImGui::TextDisabled("(Select a ticker first)");
        }

        ImGui::Separator();

        // Calculate layout dimensions
        float content_height = window_height - ImGui::GetCursorPosY() - 10.0f;

        // Layout proportions: 15% / 55% / 30%
        float left_width = window_width * 0.15f;
        float center_width = window_width * 0.55f;
        float right_width = window_width * 0.30f - 10.0f;

        // Handle fullscreen chart
        if (g_fullscreen_chart && g_fullscreen_chart_idx >= 0) {
            ChartWidget* fullscreen_widget = nullptr;
            switch (g_fullscreen_chart_idx) {
                case 0: fullscreen_widget = &g_chart_1m; break;
                case 1: fullscreen_widget = &g_chart_5m; break;
                case 2: fullscreen_widget = &g_chart_daily; break;
                default: break;
            }

            if (fullscreen_widget != nullptr) {
                if (fullscreen_widget->render(ImVec2(window_width - 20.0f, content_height))) {
                    // Double-click to exit fullscreen
                    g_fullscreen_chart = false;
                    g_fullscreen_chart_idx = -1;
                }
            }
        } else {
            // Normal layout

            // Left panel - Positions
            ImGui::BeginChild("LeftPanel", ImVec2(left_width, content_height), false);
            {
                float half_height = content_height / 2.0f;

                // Open positions (top half)
                g_positions_widget.render_open_positions(ImVec2(left_width - 10.0f, half_height - 10.0f));

                ImGui::Spacing();

                // Closed positions (bottom half)
                g_positions_widget.render_closed_positions(ImVec2(left_width - 10.0f, half_height - 10.0f));
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Center panel - Tickers and Daily chart
            ImGui::BeginChild("CenterPanel", ImVec2(center_width, content_height), false);
            {
                float ticker_height = content_height * 0.60f;
                float daily_height = content_height * 0.40f - 10.0f;

                // Ticker grid (2x2)
                float ticker_width = (center_width - 20.0f) / 2.0f;
                float ticker_h = (ticker_height - 10.0f) / 2.0f;

                ImGui::BeginChild("TickerGrid", ImVec2(center_width - 10.0f, ticker_height), false);
                {
                    for (int row = 0; row < 2; ++row) {
                        for (int col = 0; col < 2; ++col) {
                            int idx = row * 2 + col;

                            if (col > 0) ImGui::SameLine();

                            ImGui::PushID(idx);
                            if (g_ticker_widgets[idx].render(ImVec2(ticker_width - 5.0f, ticker_h - 5.0f))) {
                                // Clicked - select this ticker
                                for (int i = 0; i < NUM_TICKERS; ++i) {
                                    g_ticker_widgets[i].set_selected(i == idx);
                                }
                                g_selected_ticker = idx;
                            }
                            ImGui::PopID();
                        }
                        if (row == 0) ImGui::Spacing();
                    }
                }
                ImGui::EndChild();

                ImGui::Spacing();

                // Daily chart
                if (g_chart_daily.render(ImVec2(center_width - 10.0f, daily_height))) {
                    // Double-click - go fullscreen
                    g_fullscreen_chart = true;
                    g_fullscreen_chart_idx = 2;
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            // Right panel - 1min and 5min charts
            ImGui::BeginChild("RightPanel", ImVec2(right_width, content_height), false);
            {
                float chart_height = (content_height - 10.0f) / 2.0f;

                // 1-min chart
                if (g_chart_1m.render(ImVec2(right_width - 10.0f, chart_height - 5.0f))) {
                    g_fullscreen_chart = true;
                    g_fullscreen_chart_idx = 0;
                }

                ImGui::Spacing();

                // 5-min chart
                if (g_chart_5m.render(ImVec2(right_width - 10.0f, chart_height - 5.0f))) {
                    g_fullscreen_chart = true;
                    g_fullscreen_chart_idx = 1;
                }
            }
            ImGui::EndChild();
        }

        ImGui::End();

        ImGui::Render();
        int display_w = 0;
        int display_h = 0;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);

        // Limit frame rate to reduce CPU usage
        auto frame_end = std::chrono::steady_clock::now();
        auto frame_duration = frame_end - frame_start;
        if (frame_duration < TARGET_FRAME_TIME) {
            std::this_thread::sleep_for(TARGET_FRAME_TIME - frame_duration);
        }
    }

    // Save session before exit
    save_session();

    // Disconnect IQFeed connections BEFORE static destruction
    // This ensures background threads are stopped cleanly
    get_iqfeed_lookup().disconnect();
    get_iqfeed_level1().disconnect();
    get_iqfeed_level2().disconnect();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
