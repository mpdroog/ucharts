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
#include "tradezero_client.h"
#include "tradezero_websocket.h"
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
#include <mutex>

// TradeZero configuration
struct TZConfig {
    char api_key_id[128];
    char api_secret_key[128];
    char account_id[32];
    bool enabled;

    TZConfig() : enabled(false) {
        api_key_id[0] = '\0';
        api_secret_key[0] = '\0';
        account_id[0] = '\0';
    }
};

// TradeZero initialization thread (stored for proper cleanup)
static std::thread g_tradezero_init_thread;

// Simple INI parser for TradeZero config
[[maybe_unused]] static bool load_tradezero_config(const char* ini_path, TZConfig& config) {
    FILE* file = std::fopen(ini_path, "r");
    if (file == nullptr) {
        LOG_W("config", "Config file not found: %s", ini_path);
        return false;
    }

    char line[512];
    bool in_tradezero_section = false;

    while (std::fgets(line, sizeof(line), file)) {
        // Remove leading/trailing whitespace
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        char* end = start + std::strlen(start) - 1;
        while (end > start && (*end == '\n' || *end == '\r' || *end == ' ' || *end == '\t')) {
            *end = '\0';
            end--;
        }

        // Skip empty lines and comments
        if (*start == '\0' || *start == '#' || *start == ';') {
            continue;
        }

        // Check for [tradezero] section
        if (*start == '[') {
            in_tradezero_section = (std::strcmp(start, "[tradezero]") == 0);
            continue;
        }

        // Parse key=value pairs in [tradezero] section
        if (in_tradezero_section) {
            char* equals = std::strchr(start, '=');
            if (equals == nullptr) continue;

            // Extract key
            *equals = '\0';
            char* key = start;
            char* key_end = equals - 1;
            while (key_end > key && (*key_end == ' ' || *key_end == '\t')) {
                *key_end = '\0';
                key_end--;
            }

            // Extract value
            char* value = equals + 1;
            while (*value == ' ' || *value == '\t') value++;

            // Store values
            if (std::strcmp(key, "api_key_id") == 0) {
                std::strncpy(config.api_key_id, value, sizeof(config.api_key_id) - 1);
                config.api_key_id[sizeof(config.api_key_id) - 1] = '\0';
            } else if (std::strcmp(key, "api_secret_key") == 0) {
                std::strncpy(config.api_secret_key, value, sizeof(config.api_secret_key) - 1);
                config.api_secret_key[sizeof(config.api_secret_key) - 1] = '\0';
            } else if (std::strcmp(key, "account_id") == 0) {
                std::strncpy(config.account_id, value, sizeof(config.account_id) - 1);
                config.account_id[sizeof(config.account_id) - 1] = '\0';
            } else if (std::strcmp(key, "enabled") == 0) {
                config.enabled = (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0);
            }
        }
    }

    std::fclose(file);

    // Validate required fields if enabled
    if (config.enabled) {
        if (config.api_key_id[0] == '\0' || config.api_secret_key[0] == '\0' || config.account_id[0] == '\0') {
            LOG_E("config", "TradeZero enabled but missing required credentials");
            return false;
        }
    }

    LOG_I("config", "TradeZero config loaded: enabled=%d, account_id=%s",
          config.enabled, config.enabled ? config.account_id : "N/A");

    return true;
}

// TradeZero account info structure
struct TZAccountInfo {
    float equity;
    float buying_power;
    float cash_balance;
    float day_pnl;
    int day_trades_remaining;

    TZAccountInfo() : equity(0), buying_power(0), cash_balance(0),
                      day_pnl(0), day_trades_remaining(0) {}
};

// Global TradeZero account info cache (updated by WebSocket callbacks)
static TZAccountInfo g_tz_account_info;
static std::mutex g_tz_account_mutex;

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
// Mutex for g_symbol_states - currently only accessed from main UI thread,
// but mutex is here for safety if future callbacks need access
static std::mutex g_symbol_states_mutex;

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
        std::fprintf(stderr, "[FATAL] Failed to initialize GLFW\n");
        return 1;
    }

    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

    GLFWwindow* window = glfwCreateWindow(1600, 900, "Trading Platform", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "[FATAL] Failed to create GLFW window\n");
        glfwTerminate();
        return 1;
    }
    glfwMaximizeWindow(window);  // Maximize to use full screen
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

    // Initialize TradeZero integration
    TZConfig tz_config;
    if (load_tradezero_config("config.ini", tz_config) && tz_config.enabled) {
        LOG_I("tradezero", "Initializing TradeZero integration...");

        // Configure REST client
        get_tradezero_client().set_credentials(
            tz_config.api_key_id,
            tz_config.api_secret_key,
            tz_config.account_id
        );

        // Configure WebSocket clients
        get_tradezero_pnl().set_credentials(
            tz_config.api_key_id,
            tz_config.api_secret_key,
            tz_config.account_id
        );

        get_tradezero_portfolio().set_credentials(
            tz_config.api_key_id,
            tz_config.api_secret_key,
            tz_config.account_id
        );

        // Set TradeZero client in order manager
        g_order_manager.set_tradezero_client(&get_tradezero_client());

        // Set WebSocket callbacks BEFORE connecting
        get_tradezero_pnl().set_pnl_snapshot_callback([](const TZPnLSnapshot& snapshot) {
            std::lock_guard<std::mutex> lock(g_tz_account_mutex);
            g_tz_account_info.equity = snapshot.account_value;
            g_tz_account_info.buying_power = snapshot.buying_power;
            g_tz_account_info.cash_balance = snapshot.available_cash;
            g_tz_account_info.day_pnl = snapshot.day_pnl;
            g_tz_account_info.day_trades_remaining = snapshot.day_trades_remaining;

            // Also update order manager with initial positions
            g_order_manager.on_tradezero_pnl_snapshot(snapshot);
        });

        get_tradezero_pnl().set_agg_update_callback([](const TZAggUpdate& update) {
            std::lock_guard<std::mutex> lock(g_tz_account_mutex);
            g_tz_account_info.equity = update.account_value;
            g_tz_account_info.day_pnl = update.day_pnl;
        });

        get_tradezero_portfolio().set_order_callback([](const TZOrderUpdate& update) {
            g_order_manager.on_tradezero_order_update(update);
        });

        get_tradezero_portfolio().set_position_callback([](const TZPositionUpdate& update) {
            g_order_manager.on_tradezero_position_update(update);
        });

        // Initialize in background thread (per TradeZero docs)
        // Store thread for proper cleanup on exit
        g_tradezero_init_thread = std::thread([]{
            LOG_I("tradezero", "Connecting TradeZero WebSocket streams...");

            // Step 1: Start Portfolio WebSocket (starts buffering)
            if (!get_tradezero_portfolio().connect(TZStream::PORTFOLIO)) {
                LOG_E("tradezero", "Failed to connect Portfolio stream: %s",
                      get_tradezero_portfolio().last_error());
                return;
            }

            // Step 2: Fetch REST snapshot (positions, orders)
            TZResponse resp = get_tradezero_client().get_positions();
            if (resp.success) {
                std::vector<Position> positions;
                if (get_tradezero_client().parse_positions(resp.body, positions)) {
                    LOG_I("tradezero", "Fetched %zu positions from REST API", positions.size());
                    // Load positions into order manager
                    g_order_manager.load_tradezero_positions(positions);
                } else {
                    LOG_W("tradezero", "Failed to parse positions from REST API");
                }
            } else {
                LOG_W("tradezero", "Failed to fetch initial positions: %s", resp.error.c_str());
            }

            resp = get_tradezero_client().get_orders();
            if (resp.success) {
                std::vector<Order> orders;
                if (get_tradezero_client().parse_orders(resp.body, orders)) {
                    LOG_I("tradezero", "Fetched %zu orders from REST API", orders.size());
                    // Load orders into order manager
                    g_order_manager.load_tradezero_orders(orders);
                } else {
                    LOG_W("tradezero", "Failed to parse orders from REST API");
                }
            } else {
                LOG_W("tradezero", "Failed to fetch initial orders: %s", resp.error.c_str());
            }

            // Step 3: Portfolio WebSocket is already connected and subscribed
            // It's now processing buffered + new messages

            // Step 4: Connect P&L stream (has initial snapshot)
            if (!get_tradezero_pnl().connect(TZStream::PNL)) {
                LOG_E("tradezero", "Failed to connect P&L stream: %s",
                      get_tradezero_pnl().last_error());
                return;
            }

            LOG_I("tradezero", "TradeZero connected successfully");
        });
    } else {
        LOG_I("tradezero", "TradeZero integration disabled or not configured");
    }

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

            // Retry missing timeframes every 5 seconds
            for (int i = 0; i < NUM_TICKERS; ++i) {
                const char* sym = g_symbol_states[i].symbol;
                if (sym[0] != '\0') {
                    // Check if any timeframe is missing and retry
                    auto state = get_market_data().get_loading_state(sym);
                    if (state == MarketData::LoadingState::COMPLETE) {
                        bool has_daily = get_market_data().has_timeframe_data(sym, Timeframe::DAILY);
                        bool has_1m = get_market_data().has_timeframe_data(sym, Timeframe::M1);
                        bool has_5m = get_market_data().has_timeframe_data(sym, Timeframe::M5);
                        if (!has_daily || !has_1m || !has_5m) {
                            LOG_I("main", "Retrying missing data for %s (daily=%d, 1m=%d, 5m=%d)",
                                  sym, has_daily ? 1 : 0, has_1m ? 1 : 0, has_5m ? 1 : 0);
                            if (!get_market_data().load_symbol(sym)) {
                                LOG_W("main", "Retry failed for %s - will try again later", sym);
                            }
                        }
                    }
                }
            }
        }

        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // TradeZero: Order fills come from WebSocket callbacks
        // CSV simulation process_fills() removed
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

        // Top bar with connection status and clock
        // Connection status indicators (left side)
        bool l1_ok = get_iqfeed_level1().is_connected();
        bool l2_ok = get_iqfeed_level2().is_connected();
        bool lu_ok = get_iqfeed_lookup().is_connected();

        ImVec4 col_green(0.0f, 0.8f, 0.0f, 1.0f);
        ImVec4 col_red(0.8f, 0.0f, 0.0f, 1.0f);

        ImGui::TextColored(l1_ok ? col_green : col_red, "L1");
        ImGui::SameLine();
        ImGui::TextColored(l2_ok ? col_green : col_red, "L2");
        ImGui::SameLine();
        ImGui::TextColored(lu_ok ? col_green : col_red, "LU");
        ImGui::SameLine();

        // TradeZero status indicator
        bool tz_rest_ok = get_tradezero_client().is_configured();
        bool tz_ws_ok = get_tradezero_portfolio().is_connected() && get_tradezero_pnl().is_connected();
        bool tz_ok = tz_rest_ok && tz_ws_ok;
        ImGui::TextColored(tz_ok ? col_green : col_red, "TZ");
        ImGui::SameLine();

        // Clock (centered)
        char time_str[32];
        get_nyc_time(time_str, sizeof(time_str));

        float text_width = ImGui::CalcTextSize(time_str).x;
        ImGui::SetCursorPosX((window_width - text_width) / 2.0f);
        ImGui::Text("%s", time_str);

        // Account info (right-aligned) - only show if TradeZero is connected
        if (tz_ok) {
            TZAccountInfo account;
            {
                std::lock_guard<std::mutex> lock(g_tz_account_mutex);
                account = g_tz_account_info;
            }

            // Build account info string
            char account_text[256];
            std::snprintf(account_text, sizeof(account_text),
                         "Equity: $%.2f | BP: $%.0f | Cash: $%.2f | P&L: %+.2f | DT: %d",
                         static_cast<double>(account.equity),
                         static_cast<double>(account.buying_power),
                         static_cast<double>(account.cash_balance),
                         static_cast<double>(account.day_pnl),
                         account.day_trades_remaining);

            // Calculate position for right alignment
            float account_text_width = ImGui::CalcTextSize(account_text).x;
            float cursor_y = ImGui::GetCursorPosY();
            ImGui::SetCursorPosY(cursor_y - ImGui::GetTextLineHeight());
            ImGui::SetCursorPosX(window_width - account_text_width - 10.0f);

            // Render with color coding for P&L and day trades
            ImGui::Text("Equity: $%.2f | BP: $%.0f | Cash: $%.2f | ",
                       static_cast<double>(account.equity),
                       static_cast<double>(account.buying_power),
                       static_cast<double>(account.cash_balance));
            ImGui::SameLine();

            ImVec4 pnl_color = (account.day_pnl >= 0) ? col_green : col_red;
            ImGui::TextColored(pnl_color, "P&L: %+.2f", static_cast<double>(account.day_pnl));
            ImGui::SameLine();

            ImVec4 dt_color = col_green;
            if (account.day_trades_remaining == 0) {
                dt_color = col_red;
            } else if (account.day_trades_remaining <= 2) {
                dt_color = ImVec4(0.8f, 0.8f, 0.0f, 1.0f);  // Yellow
            }
            ImGui::TextColored(dt_color, " | DT: %d", account.day_trades_remaining);
        }

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

        // Push zero spacing for content area only
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

        // Calculate layout dimensions - account for BeginChild overhead and nested panels
        const float TOTAL_OVERHEAD = 12.0f;  // Accumulated overhead from nested BeginChild panels
        float content_height = window_height - ImGui::GetCursorPosY() - TOTAL_OVERHEAD;

        // Layout proportions: 15% / 55% / 30%
        float left_width = window_width * 0.15f;
        float center_width = window_width * 0.55f;
        float right_width = window_width * 0.30f;

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
                if (fullscreen_widget->render(ImVec2(window_width, content_height))) {
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
                g_positions_widget.render_open_positions(ImVec2(left_width, half_height));

                // Closed positions (bottom half)
                g_positions_widget.render_closed_positions(ImVec2(left_width, half_height));
            }
            ImGui::EndChild();

            ImGui::SameLine(0, 0);

            // Center panel - Tickers and Daily chart
            ImGui::BeginChild("CenterPanel", ImVec2(center_width, content_height), false);
            {
                // Account for TickerGrid BeginChild overhead
                const float GRID_OVERHEAD = 6.0f;
                float ticker_height = (content_height * 0.60f) - GRID_OVERHEAD;
                float daily_height = content_height * 0.40f;

                // Ticker grid (2x2)
                float ticker_width = center_width / 2.0f;
                float ticker_h = ticker_height / 2.0f;

                ImGui::BeginChild("TickerGrid", ImVec2(center_width, ticker_height), false);
                {
                    for (int row = 0; row < 2; ++row) {
                        for (int col = 0; col < 2; ++col) {
                            int idx = row * 2 + col;

                            if (col > 0) ImGui::SameLine(0, 0);

                            ImGui::PushID(idx);
                            if (g_ticker_widgets[idx].render(ImVec2(ticker_width, ticker_h))) {
                                // Clicked - select this ticker
                                for (int i = 0; i < NUM_TICKERS; ++i) {
                                    g_ticker_widgets[i].set_selected(i == idx);
                                }
                                g_selected_ticker = idx;
                            }
                            ImGui::PopID();
                        }
                    }
                }
                ImGui::EndChild();

                // Daily chart
                if (g_chart_daily.render(ImVec2(center_width, daily_height))) {
                    // Double-click - go fullscreen
                    g_fullscreen_chart = true;
                    g_fullscreen_chart_idx = 2;
                }
            }
            ImGui::EndChild();

            ImGui::SameLine(0, 0);

            // Right panel - 1min and 5min charts
            ImGui::BeginChild("RightPanel", ImVec2(right_width, content_height), false);
            {
                float chart_height = content_height / 2.0f;

                // 1-min chart
                if (g_chart_1m.render(ImVec2(right_width, chart_height))) {
                    g_fullscreen_chart = true;
                    g_fullscreen_chart_idx = 0;
                }

                // 5-min chart
                if (g_chart_5m.render(ImVec2(right_width, chart_height))) {
                    g_fullscreen_chart = true;
                    g_fullscreen_chart_idx = 1;
                }
            }
            ImGui::EndChild();
        }

        ImGui::PopStyleVar();  // Pop ItemSpacing

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

        // Limit frame rate to ~60 FPS to reduce CPU usage
        // Not a busy-wait risk: if we skip sleep, rendering still takes time
        auto frame_end = std::chrono::steady_clock::now();
        auto frame_duration = frame_end - frame_start;
        if (frame_duration < TARGET_FRAME_TIME) {
            auto sleep_ms = std::chrono::duration_cast<std::chrono::milliseconds>(TARGET_FRAME_TIME - frame_duration).count();
            if (sleep_ms > 0) {
                safe_sleep_ms(static_cast<int>(sleep_ms));
            }
        }
    }

    // Save session before exit
    save_session();

    // Wait for TradeZero initialization thread to complete
    if (g_tradezero_init_thread.joinable()) {
        g_tradezero_init_thread.join();
    }

    // Disconnect IQFeed connections BEFORE static destruction
    // This ensures background threads are stopped cleanly
    get_iqfeed_lookup().disconnect();
    get_iqfeed_level1().disconnect();
    get_iqfeed_level2().disconnect();

    // Disconnect TradeZero WebSocket streams
    get_tradezero_pnl().disconnect();
    get_tradezero_portfolio().disconnect();
    LOG_I("tradezero", "TradeZero disconnected");

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
