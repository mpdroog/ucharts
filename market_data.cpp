// market_data.cpp - Market data loading and simulation implementation
#include "market_data.h"
#include "http_client.h"
#include "json_parser.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

// Global market data instance
static MarketData g_market_data;

MarketData& get_market_data() {
    return g_market_data;
}

// Preset colors for Level 2 display (distinguishable shades)
static const ImU32 g_bid_colors[] = {
    0xFF00AA00,  // Bright green
    0xFF009900,
    0xFF008800,
    0xFF007700,
    0xFF006600,
    0xFF005500,
    0xFF004400,
    0xFF003300,
    0xFF002200,
    0xFF001100,
};

static const ImU32 g_ask_colors[] = {
    0xFF0000AA,  // Bright red (note: ImU32 is ABGR)
    0xFF0000BB,
    0xFF0000CC,
    0xFF0000DD,
    0xFF0000EE,
    0xFF0000FF,
    0xFF1111FF,
    0xFF2222FF,
    0xFF3333FF,
    0xFF4444FF,
};

MarketData::MarketData()
    : m_data_dir("data"), m_api_url("http://localhost:8080"), m_source_mode(SOURCE_IQFEED),
      m_running(false), m_sim_index(0), m_current_timestamp(0) {
    m_current_time[0] = '\0';
    m_error[0] = '\0';
}

MarketData::~MarketData() {
    stop_simulation();
}

void MarketData::set_data_dir(const char* dir) {
    m_data_dir = dir;
}

void MarketData::set_data_source(DataSourceMode mode) {
    m_source_mode = mode;
}

void MarketData::set_api_url(const char* url) {
    if (url != nullptr) {
        m_api_url = url;
        get_http_client().set_base_url(url);
    }
}

const char* MarketData::last_error() const {
    return m_error;
}

bool MarketData::has_symbol(const char* symbol) const {
    if (symbol == nullptr || symbol[0] == '\0') return false;

    // For API mode, assume any symbol might be valid (API will error if not)
    if (m_source_mode == SOURCE_IQFEED) {
        return true;
    }

    // For file mode, check if data files exist
    char filepath[512];
    std::snprintf(filepath, sizeof(filepath), "%s/candles_%s_daily.csv",
                 m_data_dir.c_str(), symbol);

    FILE* f = std::fopen(filepath, "r");
    if (f != nullptr) {
        std::fclose(f);
        return true;
    }
    return false;
}

bool MarketData::load_symbol(const char* symbol) {
    if (symbol == nullptr || symbol[0] == '\0') {
        std::snprintf(m_error, sizeof(m_error), "Empty symbol");
        return false;
    }

    // Use API if configured
    if (m_source_mode == SOURCE_IQFEED) {
        return load_symbol_from_api(symbol);
    }

    // File-based loading (original behavior)
    std::string sym(symbol);
    SymbolData& data = m_symbols[sym];
    data.loaded = false;

    char filepath[512];
    bool any_loaded = false;

    // Load Level 2 data
    std::snprintf(filepath, sizeof(filepath), "%s/level2_%s.csv",
                 m_data_dir.c_str(), symbol);
    if (parse_level2_csv(filepath, symbol)) {
        any_loaded = true;
    }

    // Load Time & Sales data
    std::snprintf(filepath, sizeof(filepath), "%s/timesales_%s.csv",
                 m_data_dir.c_str(), symbol);
    if (parse_timesales_csv(filepath, symbol)) {
        any_loaded = true;
    }

    // Load candle data (1m, 5m, daily)
    std::snprintf(filepath, sizeof(filepath), "%s/candles_%s_1m.csv",
                 m_data_dir.c_str(), symbol);
    if (parse_candles_csv(filepath, data.candles_1m)) {
        any_loaded = true;
    }

    std::snprintf(filepath, sizeof(filepath), "%s/candles_%s_5m.csv",
                 m_data_dir.c_str(), symbol);
    if (parse_candles_csv(filepath, data.candles_5m)) {
        any_loaded = true;
    }

    std::snprintf(filepath, sizeof(filepath), "%s/candles_%s_daily.csv",
                 m_data_dir.c_str(), symbol);
    if (parse_candles_csv(filepath, data.candles_daily)) {
        any_loaded = true;
    }

    if (!any_loaded) {
        std::snprintf(m_error, sizeof(m_error), "No data files found for %s", symbol);
        return false;
    }

    // Set initial price from daily candles if available
    if (!data.candles_daily.empty()) {
        data.current_price = data.candles_daily.back().close;
    } else if (!data.candles_1m.empty()) {
        data.current_price = data.candles_1m.back().close;
    }

    data.loaded = true;
    return true;
}

void MarketData::unload_symbol(const char* symbol) {
    if (symbol == nullptr) return;
    m_symbols.erase(std::string(symbol));
}

ImU32 MarketData::get_level_color(int level_index, bool is_bid) {
    if (level_index < 0) level_index = 0;
    if (level_index >= LEVEL2_DEPTH) level_index = LEVEL2_DEPTH - 1;
    return is_bid ? g_bid_colors[level_index] : g_ask_colors[level_index];
}

bool MarketData::get_level2(const char* symbol, std::vector<Level2Entry>& bids,
                            std::vector<Level2Entry>& asks, float& best_bid, float& best_ask) {
    bids.clear();
    asks.clear();
    best_bid = 0;
    best_ask = 0;

    if (symbol == nullptr || symbol[0] == '\0') return false;

    auto it = m_symbols.find(std::string(symbol));
    if (it == m_symbols.end() || !it->second.loaded) return false;

    const SymbolData& data = it->second;

    // Copy Level 2 data with color assignment
    bids.reserve(LEVEL2_DEPTH);
    asks.reserve(LEVEL2_DEPTH);

    for (size_t i = 0; i < data.level2_bids.size() && i < LEVEL2_DEPTH; i++) {
        Level2Entry entry = data.level2_bids[i];
        entry.color = get_level_color(static_cast<int>(i), true);
        bids.push_back(entry);
        if (i == 0) best_bid = entry.price;
    }

    for (size_t i = 0; i < data.level2_asks.size() && i < LEVEL2_DEPTH; i++) {
        Level2Entry entry = data.level2_asks[i];
        entry.color = get_level_color(static_cast<int>(i), false);
        asks.push_back(entry);
        if (i == 0) best_ask = entry.price;
    }

    return true;
}

bool MarketData::get_time_sales(const char* symbol, std::vector<TimeSalesEntry>& entries, int count) {
    entries.clear();

    if (symbol == nullptr || symbol[0] == '\0') return false;

    auto it = m_symbols.find(std::string(symbol));
    if (it == m_symbols.end() || !it->second.loaded) return false;

    const SymbolData& data = it->second;
    const std::vector<TimeSalesEntry>& ts = data.time_sales;

    // Get the last 'count' entries
    size_t start = (ts.size() > static_cast<size_t>(count)) ? ts.size() - static_cast<size_t>(count) : 0;
    for (size_t i = start; i < ts.size(); i++) {
        entries.push_back(ts[i]);
    }

    return true;
}

bool MarketData::get_candles(const char* symbol, Timeframe tf, std::vector<Candle>& candles, int limit) {
    candles.clear();

    if (symbol == nullptr || symbol[0] == '\0') return false;

    auto it = m_symbols.find(std::string(symbol));
    if (it == m_symbols.end() || !it->second.loaded) return false;

    const SymbolData& data = it->second;
    const std::vector<Candle>* src = nullptr;

    switch (tf) {
        case TF_1MIN: src = &data.candles_1m; break;
        case TF_5MIN: src = &data.candles_5m; break;
        case TF_DAILY: src = &data.candles_daily; break;
    }

    if (src == nullptr || src->empty()) return false;

    // Get the last 'limit' candles
    size_t start = (src->size() > static_cast<size_t>(limit)) ? src->size() - static_cast<size_t>(limit) : 0;
    for (size_t i = start; i < src->size(); i++) {
        candles.push_back((*src)[i]);
    }

    return true;
}

float MarketData::get_current_price(const char* symbol) const {
    if (symbol == nullptr || symbol[0] == '\0') return 0;

    auto it = m_symbols.find(std::string(symbol));
    if (it == m_symbols.end() || !it->second.loaded) return 0;

    return it->second.current_price;
}

void MarketData::start_simulation() {
    m_running = true;
    m_sim_index = 0;
}

void MarketData::stop_simulation() {
    m_running = false;
}

void MarketData::step_simulation() {
    if (!m_running) return;
    m_sim_index++;
    // In a real implementation, this would advance through time-sorted data
}

bool MarketData::is_running() const {
    return m_running;
}

const char* MarketData::get_current_time() const {
    return m_current_time;
}

int64_t MarketData::get_current_timestamp() const {
    return m_current_timestamp;
}

// ============================================================================
// Parsers
// ============================================================================

bool MarketData::parse_level2_csv(const char* filepath, const char* symbol) {
    FILE* file = std::fopen(filepath, "r");
    if (file == nullptr) return false;

    std::string sym(symbol);
    SymbolData& data = m_symbols[sym];
    data.level2_bids.clear();
    data.level2_asks.clear();

    // Temporary storage for aggregation
    std::map<float, Level2Entry> bid_levels;
    std::map<float, Level2Entry> ask_levels;

    char line[512];
    int line_num = 0;

    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        line_num++;

        // Skip header
        if (line_num == 1 && (std::strstr(line, "timestamp") != nullptr ||
                              std::strstr(line, "side") != nullptr)) {
            continue;
        }

        // Parse: timestamp,symbol,side,exchange,price,size
        char ts[16], sym_col[8], side[4], exchange[8];
        float price;
        int size;

        int parsed = std::sscanf(line, "%15[^,],%7[^,],%3[^,],%7[^,],%f,%d",
                                 ts, sym_col, side, exchange, &price, &size);
        if (parsed < 6) continue;

        Level2Entry entry;
        safe_strcpy(entry.exchange, exchange, sizeof(entry.exchange));
        entry.price = price;
        entry.size = size;
        entry.color = 0;

        if (side[0] == 'B' || side[0] == 'b') {
            // Aggregate bids at same price
            auto it = bid_levels.find(price);
            if (it != bid_levels.end()) {
                it->second.size += size;
            } else {
                bid_levels[price] = entry;
            }
        } else if (side[0] == 'A' || side[0] == 'a') {
            // Aggregate asks at same price
            auto it = ask_levels.find(price);
            if (it != ask_levels.end()) {
                it->second.size += size;
            } else {
                ask_levels[price] = entry;
            }
        }

        // Store timestamp from first entry
        if (line_num == 2) {
            safe_strcpy(m_current_time, ts, sizeof(m_current_time));
        }
    }

    std::fclose(file);

    // Convert maps to vectors (bids descending, asks ascending)
    for (auto it = bid_levels.rbegin(); it != bid_levels.rend() && data.level2_bids.size() < LEVEL2_DEPTH; ++it) {
        data.level2_bids.push_back(it->second);
    }

    for (auto it = ask_levels.begin(); it != ask_levels.end() && data.level2_asks.size() < LEVEL2_DEPTH; ++it) {
        data.level2_asks.push_back(it->second);
    }

    return !data.level2_bids.empty() || !data.level2_asks.empty();
}

bool MarketData::parse_timesales_csv(const char* filepath, const char* symbol) {
    FILE* file = std::fopen(filepath, "r");
    if (file == nullptr) return false;

    std::string sym(symbol);
    SymbolData& data = m_symbols[sym];
    data.time_sales.clear();

    char line[512];
    int line_num = 0;
    float last_price = 0;

    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        line_num++;

        // Skip header
        if (line_num == 1 && (std::strstr(line, "timestamp") != nullptr ||
                              std::strstr(line, "direction") != nullptr)) {
            continue;
        }

        // Parse: timestamp,symbol,price,size,direction
        char ts[16], sym_col[8], dir[8];
        float price;
        int size;

        int parsed = std::sscanf(line, "%15[^,],%7[^,],%f,%d,%7s",
                                 ts, sym_col, &price, &size, dir);
        if (parsed < 4) continue;

        TimeSalesEntry entry;
        safe_strcpy(entry.timestamp, ts, sizeof(entry.timestamp));
        entry.price = price;
        entry.size = size;

        // Determine direction from string or calculate from price change
        if (parsed >= 5) {
            if (std::strcmp(dir, "UP") == 0 || dir[0] == 'U' || dir[0] == '+') {
                entry.direction = DIR_UP;
            } else if (std::strcmp(dir, "DOWN") == 0 || dir[0] == 'D' || dir[0] == '-') {
                entry.direction = DIR_DOWN;
            } else {
                entry.direction = DIR_SAME;
            }
        } else {
            // Calculate from price change
            if (price > last_price) {
                entry.direction = DIR_UP;
            } else if (price < last_price) {
                entry.direction = DIR_DOWN;
            } else {
                entry.direction = DIR_SAME;
            }
        }

        data.time_sales.push_back(entry);
        last_price = price;
        data.current_price = price;
    }

    std::fclose(file);
    return !data.time_sales.empty();
}

bool MarketData::parse_candles_csv(const char* filepath, std::vector<Candle>& candles) {
    FILE* file = std::fopen(filepath, "r");
    if (file == nullptr) return false;

    candles.clear();

    char line[512];
    int line_num = 0;

    while (std::fgets(line, static_cast<int>(sizeof(line)), file) != nullptr) {
        line_num++;

        // Skip header
        if (line_num == 1 && (std::strstr(line, "timestamp") != nullptr ||
                              std::strstr(line, "open") != nullptr ||
                              std::strstr(line, "Open") != nullptr)) {
            continue;
        }

        Candle c;

        // Try new format: timestamp,open,high,low,close,volume
        char ts[32] = "";
        int parsed = std::sscanf(line, "%31[^,],%f,%f,%f,%f,%f",
                                 ts, &c.open, &c.high, &c.low, &c.close, &c.volume);

        if (parsed >= 5) {
            safe_strcpy(c.timestamp, ts, sizeof(c.timestamp));
            if (parsed < 6) c.volume = 0.0f;
            candles.push_back(c);
            continue;
        }

        // Try old format: open,high,low,close
        parsed = std::sscanf(line, "%f,%f,%f,%f", &c.open, &c.high, &c.low, &c.close);
        if (parsed == 4) {
            std::snprintf(c.timestamp, sizeof(c.timestamp), "%d", line_num);
            c.volume = 0.0f;
            candles.push_back(c);
        }
    }

    std::fclose(file);

    // Limit to MAX_CANDLES
    if (candles.size() > MAX_CANDLES) {
        candles.erase(candles.begin(), candles.begin() + static_cast<long>(candles.size() - MAX_CANDLES));
    }

    return !candles.empty();
}

// ============================================================================
// API-based loading (iqfeed HTTP API)
// ============================================================================

bool MarketData::load_symbol_from_api(const char* symbol) {
    std::string sym(symbol);
    SymbolData& data = m_symbols[sym];
    data.loaded = false;
    data.level2_bids.clear();
    data.level2_asks.clear();
    data.time_sales.clear();
    data.candles_1m.clear();
    data.candles_5m.clear();
    data.candles_daily.clear();

    bool any_loaded = false;

    // Fetch daily candles
    if (fetch_daily_candles(symbol, data.candles_daily)) {
        any_loaded = true;
    }

    // Fetch 1-minute candles (interval=60 seconds)
    if (fetch_minute_candles(symbol, 60, data.candles_1m)) {
        any_loaded = true;
    }

    // Fetch 5-minute candles (interval=300 seconds)
    if (fetch_minute_candles(symbol, 300, data.candles_5m)) {
        any_loaded = true;
    }

    if (!any_loaded) {
        std::snprintf(m_error, sizeof(m_error), "Failed to fetch data for %s from API", symbol);
        return false;
    }

    // Set current price from most recent candle
    if (!data.candles_1m.empty()) {
        data.current_price = data.candles_1m.back().close;
    } else if (!data.candles_daily.empty()) {
        data.current_price = data.candles_daily.back().close;
    }

    data.loaded = true;
    return true;
}

bool MarketData::fetch_daily_candles(const char* symbol, std::vector<Candle>& candles) {
    candles.clear();

    // Build URL: /ohlc?asset=SYMBOL&range=DAILY&datapoints=200
    char path[256];
    std::snprintf(path, sizeof(path), "/ohlc?asset=%s&range=DAILY&datapoints=%d",
                 symbol, MAX_CANDLES);

    HttpClient& client = get_http_client();
    HttpResponse resp = client.get(path);

    if (!resp.success) {
        std::snprintf(m_error, sizeof(m_error), "API error fetching daily candles: %s",
                     resp.error.c_str());
        return false;
    }

    // Parse JSON response
    if (!parse_iqfeed_ohlc_json(resp.body, candles)) {
        std::snprintf(m_error, sizeof(m_error), "Failed to parse daily candles JSON");
        return false;
    }

    return !candles.empty();
}

bool MarketData::fetch_minute_candles(const char* symbol, int interval_secs, std::vector<Candle>& candles) {
    candles.clear();

    // Build URL: /ohlc-intervals?asset=SYMBOL&interval=60&datapoints=200
    char path[256];
    std::snprintf(path, sizeof(path), "/ohlc-intervals?asset=%s&interval=%d&datapoints=%d",
                 symbol, interval_secs, MAX_CANDLES);

    HttpClient& client = get_http_client();
    HttpResponse resp = client.get(path);

    if (!resp.success) {
        std::snprintf(m_error, sizeof(m_error), "API error fetching %d-sec candles: %s",
                     interval_secs, resp.error.c_str());
        return false;
    }

    // The interval endpoint returns JSON in the same format
    if (!parse_iqfeed_ohlc_json(resp.body, candles)) {
        // Try CSV format (when using Accept: text/csv header)
        if (!parse_csv_response(resp.body, candles)) {
            std::snprintf(m_error, sizeof(m_error), "Failed to parse %d-sec candles",
                         interval_secs);
            return false;
        }
    }

    // Limit to MAX_CANDLES
    if (candles.size() > MAX_CANDLES) {
        candles.erase(candles.begin(), candles.begin() + static_cast<long>(candles.size() - MAX_CANDLES));
    }

    return !candles.empty();
}

bool MarketData::parse_csv_response(const std::string& csv, std::vector<Candle>& candles) {
    candles.clear();

    if (csv.empty()) return false;

    // Parse CSV format from iqfeed:
    // MessageID, TimeStamp, High, Low, Open, Close, TotalVolume, PeriodVolume, NumberofTrades,
    // LH,2024-05-10 05:32:00,184.7600,184.7500,184.7500,184.7600,32048,250,0,

    const char* p = csv.c_str();
    const char* end = p + csv.size();

    // Process line by line
    while (p < end) {
        // Find end of line
        const char* line_end = p;
        while (line_end < end && *line_end != '\n' && *line_end != '\r') {
            line_end++;
        }

        // Parse this line
        if (line_end > p) {
            char msg_id[8] = "";
            char ts[32] = "";
            float high = 0, low = 0, open = 0, close = 0;
            float total_vol = 0;

            // Format: MessageID,TimeStamp,High,Low,Open,Close,TotalVolume,...
            int parsed = std::sscanf(p, "%7[^,],%31[^,],%f,%f,%f,%f,%f",
                                    msg_id, ts, &high, &low, &open, &close, &total_vol);

            // Skip header line
            if (parsed >= 6 && std::strcmp(msg_id, "Message") != 0 &&
                std::strcmp(msg_id, "LH") == 0) {
                Candle c;
                safe_strcpy(c.timestamp, ts, sizeof(c.timestamp));
                c.open = open;
                c.high = high;
                c.low = low;
                c.close = close;
                c.volume = total_vol;
                candles.push_back(c);
            }
        }

        // Move to next line
        p = line_end;
        while (p < end && (*p == '\n' || *p == '\r')) {
            p++;
        }
    }

    // Reverse to get chronological order (API returns newest first)
    std::reverse(candles.begin(), candles.end());

    return !candles.empty();
}
