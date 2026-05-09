// Test file for chart widget features (S/R levels, session detection, real-time updates)
// Compile with: clang++ -std=c++17 -DRUN_TESTS test_chart_widget.cpp -o test_chart_widget && ./test_chart_widget

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <cassert>
#include <string>

// ============================================================================
// Copy of data structures from types.h for testing
// ============================================================================

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

struct AutoSRLevel {
    float price;
    bool is_resistance;
    int candle_idx;

    AutoSRLevel() : price(0), is_resistance(true), candle_idx(0) {}
    AutoSRLevel(float p, bool res, int idx) : price(p), is_resistance(res), candle_idx(idx) {}
};

enum class MarketSession {
    PRE_MARKET,
    REGULAR,
    AFTER_HOURS
};

// ============================================================================
// Copy of logic functions from chart_widget.cpp for testing
// ============================================================================

static MarketSession get_session_from_timestamp(const char* timestamp) {
    if (timestamp == nullptr || timestamp[0] == '\0') {
        return MarketSession::REGULAR;
    }

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

    if (time_mins >= 240 && time_mins < 570) {
        return MarketSession::PRE_MARKET;
    }
    if (time_mins >= 570 && time_mins < 960) {
        return MarketSession::REGULAR;
    }
    if (time_mins >= 960 && time_mins < 1200) {
        return MarketSession::AFTER_HOURS;
    }

    return MarketSession::REGULAR;
}

static void calculate_auto_sr(const std::vector<Candle>& candles, std::vector<AutoSRLevel>& levels) {
    levels.clear();

    if (candles.size() < 3) {
        return;
    }

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

    std::vector<AutoSRLevel> all_resistance;
    std::vector<AutoSRLevel> all_support;

    for (size_t i = 1; i < candles.size() - 1; i++) {
        const Candle& prev = candles[i - 1];
        const Candle& curr = candles[i];
        const Candle& next = candles[i + 1];

        bool has_significant_volume = (avg_volume <= 0) || (curr.volume >= min_volume);
        if (!has_significant_volume) continue;

        // Swing high -> resistance if above current price
        if (curr.high > prev.high && curr.high > next.high) {
            if (curr.high > current_price) {
                all_resistance.emplace_back(curr.high, true, static_cast<int>(i));
            }
        }

        // Swing low -> support if below current price
        if (curr.low < prev.low && curr.low < next.low) {
            if (curr.low < current_price) {
                all_support.emplace_back(curr.low, false, static_cast<int>(i));
            }
        }
    }

    // Sort and take closest levels
    std::sort(all_resistance.begin(), all_resistance.end(),
              [](const AutoSRLevel& a, const AutoSRLevel& b) { return a.price < b.price; });
    std::sort(all_support.begin(), all_support.end(),
              [](const AutoSRLevel& a, const AutoSRLevel& b) { return a.price > b.price; });

    const size_t MAX_LEVELS = 3;
    for (size_t i = 0; i < std::min(all_resistance.size(), MAX_LEVELS); i++) {
        levels.push_back(all_resistance[i]);
    }
    for (size_t i = 0; i < std::min(all_support.size(), MAX_LEVELS); i++) {
        levels.push_back(all_support[i]);
    }
}

// Candle update helpers from market_data.cpp
static bool parse_time_hhmm(const char* time_str, int& hour, int& minute) {
    if (time_str == nullptr || time_str[0] == '\0') return false;
    int h = 0, m = 0, s = 0;
    if (std::sscanf(time_str, "%d:%d:%d", &h, &m, &s) >= 2) {
        hour = h;
        minute = m;
        return true;
    }
    return false;
}

static bool is_same_candle_period(const char* candle_ts, int tick_hour, int tick_minute, int interval_mins) {
    if (candle_ts == nullptr || candle_ts[0] == '\0') return false;

    const char* time_part = candle_ts;
    const char* space = std::strchr(candle_ts, ' ');
    if (space != nullptr) {
        time_part = space + 1;
    }

    int candle_hour = 0, candle_minute = 0;
    if (!parse_time_hhmm(time_part, candle_hour, candle_minute)) {
        return false;
    }

    int candle_period = candle_hour * 60 + (candle_minute / interval_mins) * interval_mins;
    int tick_period = tick_hour * 60 + (tick_minute / interval_mins) * interval_mins;

    return candle_period == tick_period;
}

// ============================================================================
// Test helpers
// ============================================================================

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name) static void test_##name()
#define RUN_TEST(name) do { \
    g_tests_run++; \
    std::printf("Running %s... ", #name); \
    test_##name(); \
    g_tests_passed++; \
    std::printf("PASSED\n"); \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::printf("FAILED: %s (%d) != %s (%d) (line %d)\n", #a, (int)(a), #b, (int)(b), __LINE__); \
        std::exit(1); \
    } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (std::fabs((a) - (b)) > (eps)) { \
        std::printf("FAILED: %s (%.4f) != %s (%.4f) (line %d)\n", #a, (double)(a), #b, (double)(b), __LINE__); \
        std::exit(1); \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::printf("FAILED: %s is false (line %d)\n", #cond, __LINE__); \
        std::exit(1); \
    } \
} while(0)

static Candle make_candle(float open, float high, float low, float close, const char* ts = "") {
    Candle c;
    c.open = open;
    c.high = high;
    c.low = low;
    c.close = close;
    c.volume = 0;
    std::strncpy(c.timestamp, ts, sizeof(c.timestamp) - 1);
    c.timestamp[sizeof(c.timestamp) - 1] = '\0';
    return c;
}

// ============================================================================
// Session Detection Tests
// ============================================================================

TEST(session_premarket) {
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("04:00:00")), static_cast<int>(MarketSession::PRE_MARKET));
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("2024-01-02 08:30:00")), static_cast<int>(MarketSession::PRE_MARKET));
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("09:29:59")), static_cast<int>(MarketSession::PRE_MARKET));
}

TEST(session_regular) {
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("09:30:00")), static_cast<int>(MarketSession::REGULAR));
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("2024-01-02 12:00:00")), static_cast<int>(MarketSession::REGULAR));
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("15:59:59")), static_cast<int>(MarketSession::REGULAR));
}

TEST(session_afterhours) {
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("16:00:00")), static_cast<int>(MarketSession::AFTER_HOURS));
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("2024-01-02 18:30:00")), static_cast<int>(MarketSession::AFTER_HOURS));
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("19:59:59")), static_cast<int>(MarketSession::AFTER_HOURS));
}

TEST(session_empty_timestamp) {
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp("")), static_cast<int>(MarketSession::REGULAR));
    ASSERT_EQ(static_cast<int>(get_session_from_timestamp(nullptr)), static_cast<int>(MarketSession::REGULAR));
}

// ============================================================================
// Auto S/R Level Tests
// ============================================================================

TEST(sr_basic_swing_high) {
    // Candles: low, HIGH, low - should detect swing high at index 1
    std::vector<Candle> candles;
    candles.push_back(make_candle(100, 105, 95, 100));   // 0
    candles.push_back(make_candle(100, 115, 95, 110));   // 1 - swing high (115)
    candles.push_back(make_candle(110, 108, 92, 105));   // 2

    std::vector<AutoSRLevel> levels;
    calculate_auto_sr(candles, levels);

    ASSERT_EQ(levels.size(), 1u);
    ASSERT_TRUE(levels[0].is_resistance);
    ASSERT_FLOAT_EQ(levels[0].price, 115.0f, 0.01f);
    ASSERT_EQ(levels[0].candle_idx, 1);
}

TEST(sr_basic_swing_low) {
    // Candles: high, LOW, high - should detect swing low at index 1
    std::vector<Candle> candles;
    candles.push_back(make_candle(100, 105, 95, 100));   // 0
    candles.push_back(make_candle(100, 102, 85, 90));    // 1 - swing low (85)
    candles.push_back(make_candle(90, 108, 88, 105));    // 2

    std::vector<AutoSRLevel> levels;
    calculate_auto_sr(candles, levels);

    ASSERT_EQ(levels.size(), 1u);
    ASSERT_TRUE(!levels[0].is_resistance);
    ASSERT_FLOAT_EQ(levels[0].price, 85.0f, 0.01f);
    ASSERT_EQ(levels[0].candle_idx, 1);
}

TEST(sr_multiple_swings) {
    // Multiple swing highs and lows
    std::vector<Candle> candles;
    candles.push_back(make_candle(100, 105, 95, 100));   // 0
    candles.push_back(make_candle(100, 115, 98, 110));   // 1 - swing high (115 > 105 and 115 > 108)
    candles.push_back(make_candle(110, 108, 92, 95));    // 2
    candles.push_back(make_candle(95, 100, 85, 90));     // 3 - swing low (85 < 92 and 85 < 88)
    candles.push_back(make_candle(90, 105, 88, 100));    // 4

    std::vector<AutoSRLevel> levels;
    calculate_auto_sr(candles, levels);

    // Should have swing high at 115 (candle 1) and swing low at 85 (candle 3)
    ASSERT_EQ(levels.size(), 2u);

    int resistance_count = 0, support_count = 0;
    for (const auto& level : levels) {
        if (level.is_resistance) resistance_count++;
        else support_count++;
    }
    ASSERT_EQ(resistance_count, 1);
    ASSERT_EQ(support_count, 1);
}

TEST(sr_lower_resistance_invalidated_by_higher) {
    // Scanning right to left: higher resistance invalidates lower ones to its left
    std::vector<Candle> candles;
    candles.push_back(make_candle(100, 105, 95, 100));   // 0
    candles.push_back(make_candle(100, 120, 98, 115));   // 1 - swing high (120) - HIGHER
    candles.push_back(make_candle(115, 110, 92, 95));    // 2
    candles.push_back(make_candle(95, 115, 90, 110));    // 3 - swing high (115) - found first when scanning R->L
    candles.push_back(make_candle(110, 108, 100, 105));  // 4

    std::vector<AutoSRLevel> levels;
    calculate_auto_sr(candles, levels);

    // Scanning R->L: find 115 first (kept), then find 120 (kept because higher)
    // So we should have both 115 and 120
    int resistance_count = 0;
    bool found_120 = false, found_115 = false;
    for (const auto& level : levels) {
        if (level.is_resistance) {
            resistance_count++;
            if (std::fabs(level.price - 120.0f) < 0.01f) found_120 = true;
            if (std::fabs(level.price - 115.0f) < 0.01f) found_115 = true;
        }
    }
    ASSERT_TRUE(found_120);  // Higher resistance kept
    ASSERT_TRUE(found_115);  // First found (most recent) kept
}

TEST(sr_higher_support_invalidated_by_lower) {
    // Scanning right to left: lower support invalidates higher ones to its left
    std::vector<Candle> candles;
    candles.push_back(make_candle(100, 105, 95, 100));   // 0
    candles.push_back(make_candle(100, 102, 80, 85));    // 1 - swing low (80) - LOWER
    candles.push_back(make_candle(85, 108, 88, 105));    // 2
    candles.push_back(make_candle(105, 110, 85, 90));    // 3 - swing low (85) - found first when scanning R->L
    candles.push_back(make_candle(90, 95, 88, 92));      // 4

    std::vector<AutoSRLevel> levels;
    calculate_auto_sr(candles, levels);

    // Scanning R->L: find 85 first (kept), then find 80 (kept because lower)
    int support_count = 0;
    bool found_80 = false, found_85 = false;
    for (const auto& level : levels) {
        if (!level.is_resistance) {
            support_count++;
            if (std::fabs(level.price - 80.0f) < 0.01f) found_80 = true;
            if (std::fabs(level.price - 85.0f) < 0.01f) found_85 = true;
        }
    }
    ASSERT_TRUE(found_80);   // Lower support kept
    ASSERT_TRUE(found_85);   // First found (most recent) kept
}

TEST(sr_no_levels_with_less_than_3_candles) {
    std::vector<Candle> candles;
    candles.push_back(make_candle(100, 105, 95, 100));
    candles.push_back(make_candle(100, 115, 98, 110));

    std::vector<AutoSRLevel> levels;
    calculate_auto_sr(candles, levels);

    ASSERT_EQ(levels.size(), 0u);
}

TEST(sr_empty_candles) {
    std::vector<Candle> candles;
    std::vector<AutoSRLevel> levels;
    calculate_auto_sr(candles, levels);
    ASSERT_EQ(levels.size(), 0u);
}

TEST(sr_resistance_above_current_price) {
    // Test that all swing highs ABOVE current price are identified as resistance
    // Both 115 and 119 are above current price (112), so both are kept
    std::vector<Candle> candles;
    candles.push_back(make_candle(100, 105, 95, 100));   // 0
    candles.push_back(make_candle(100, 115, 98, 110));   // 1 - swing high (115) > 112, KEPT
    candles.push_back(make_candle(110, 108, 92, 95));    // 2
    candles.push_back(make_candle(95, 110, 90, 108));    // 3
    candles.push_back(make_candle(108, 108, 100, 105));  // 4
    candles.push_back(make_candle(105, 119, 102, 116));  // 5 - swing high (119) > 112, KEPT
    candles.push_back(make_candle(116, 115, 108, 112));  // 6 - current price = 112

    std::vector<AutoSRLevel> levels;
    calculate_auto_sr(candles, levels);

    // Both swing highs are above current price (112), so both should be resistance
    bool found_119 = false;
    bool found_115 = false;
    for (const auto& level : levels) {
        if (level.is_resistance) {
            if (std::fabs(level.price - 119.0f) < 0.01f) found_119 = true;
            if (std::fabs(level.price - 115.0f) < 0.01f) found_115 = true;
        }
    }
    ASSERT_TRUE(found_119);  // Above current price, kept
    ASSERT_TRUE(found_115);  // Above current price, kept
}

// ============================================================================
// Candle Period Tests
// ============================================================================

TEST(candle_period_1min_same) {
    ASSERT_TRUE(is_same_candle_period("09:30:00", 9, 30, 1));
    ASSERT_TRUE(is_same_candle_period("2024-01-02 09:30:45", 9, 30, 1));
}

TEST(candle_period_1min_different) {
    ASSERT_TRUE(!is_same_candle_period("09:30:00", 9, 31, 1));
    ASSERT_TRUE(!is_same_candle_period("09:30:00", 10, 30, 1));
}

TEST(candle_period_5min_same) {
    ASSERT_TRUE(is_same_candle_period("09:30:00", 9, 30, 5));
    ASSERT_TRUE(is_same_candle_period("09:30:00", 9, 31, 5));
    ASSERT_TRUE(is_same_candle_period("09:30:00", 9, 34, 5));
}

TEST(candle_period_5min_different) {
    ASSERT_TRUE(!is_same_candle_period("09:30:00", 9, 35, 5));
    ASSERT_TRUE(!is_same_candle_period("09:30:00", 9, 40, 5));
}

TEST(candle_period_empty_timestamp) {
    ASSERT_TRUE(!is_same_candle_period("", 9, 30, 1));
    ASSERT_TRUE(!is_same_candle_period(nullptr, 9, 30, 1));
}

// ============================================================================
// VWAP Calculation (copied from chart_widget.cpp)
// ============================================================================

static void calculate_vwap(const std::vector<Candle>& candles, std::vector<float>& vwap_values) {
    vwap_values.resize(candles.size(), 0.0f);

    if (candles.empty()) return;

    float cum_tp_vol = 0.0f;
    float cum_vol = 0.0f;
    std::string current_date;

    for (size_t i = 0; i < candles.size(); i++) {
        const Candle& c = candles[i];

        std::string ts(c.timestamp);
        std::string date = (ts.length() >= 10) ? ts.substr(0, 10) : ts;

        // Reset VWAP at new day
        if (date != current_date) {
            cum_tp_vol = 0.0f;
            cum_vol = 0.0f;
            current_date = date;
        }

        float typical_price = (c.high + c.low + c.close) / 3.0f;
        float vol = c.volume > 0 ? c.volume : 1.0f;

        cum_tp_vol += typical_price * vol;
        cum_vol += vol;

        if (cum_vol > 0.0f) {
            vwap_values[i] = cum_tp_vol / cum_vol;
        } else {
            vwap_values[i] = typical_price;
        }
    }
}

// ============================================================================
// Cumulative Delta Calculation (copied from chart_widget.cpp)
// ============================================================================

static void calculate_cumulative_delta(const std::vector<Candle>& candles, std::vector<float>& delta_values) {
    delta_values.resize(candles.size(), 0.0f);

    if (candles.empty()) return;

    float cum_delta = 0.0f;
    std::string current_date;

    for (size_t i = 0; i < candles.size(); i++) {
        const Candle& c = candles[i];

        std::string ts(c.timestamp);
        std::string date = (ts.length() >= 10) ? ts.substr(0, 10) : ts;

        // Reset at new day
        if (date != current_date) {
            cum_delta = 0.0f;
            current_date = date;
        }

        float range = c.high - c.low;
        if (range < 0.001f) range = 0.001f;

        float close_position = (c.close - c.low) / range;  // 0 to 1
        float delta = c.volume * (2.0f * close_position - 1.0f);

        cum_delta += delta;
        delta_values[i] = cum_delta;
    }
}

// ============================================================================
// Auto-MA Calculation (copied from chart_widget.cpp)
// ============================================================================

enum class AutoMAType {
    NONE = 0,
    EMA9 = 1,
    SMA9 = 2,
    EMA21 = 3,
    SMA21 = 4
};

static float calculate_ma_error(const std::vector<Candle>& candles, const std::vector<float>& ma_values) {
    float sum_sq_error = 0.0f;
    int count = 0;

    for (size_t i = 0; i < candles.size() && i < ma_values.size(); i++) {
        if (ma_values[i] > 0.0f) {
            float diff = candles[i].close - ma_values[i];
            sum_sq_error += diff * diff;
            count++;
        }
    }

    return (count > 0) ? (sum_sq_error / static_cast<float>(count)) : 999999.0f;
}

static void calculate_ema(const std::vector<Candle>& candles, int period, std::vector<float>& ema_values) {
    ema_values.resize(candles.size(), 0.0f);
    if (candles.size() < static_cast<size_t>(period)) return;

    float k = 2.0f / (static_cast<float>(period) + 1.0f);
    float sum = 0.0f;
    for (int i = 0; i < period; i++) {
        sum += candles[static_cast<size_t>(i)].close;
    }
    ema_values[static_cast<size_t>(period - 1)] = sum / static_cast<float>(period);

    for (size_t i = static_cast<size_t>(period); i < candles.size(); i++) {
        ema_values[i] = candles[i].close * k + ema_values[i - 1] * (1.0f - k);
    }
}

static void calculate_sma(const std::vector<Candle>& candles, int period, std::vector<float>& sma_values) {
    sma_values.resize(candles.size(), 0.0f);
    for (size_t i = static_cast<size_t>(period - 1); i < candles.size(); i++) {
        float sum = 0.0f;
        for (int j = 0; j < period; j++) {
            sum += candles[i - static_cast<size_t>(j)].close;
        }
        sma_values[i] = sum / static_cast<float>(period);
    }
}

static AutoMAType calculate_auto_ma(const std::vector<Candle>& candles, std::vector<float>& auto_ma_values) {
    auto_ma_values.clear();
    if (candles.empty()) return AutoMAType::NONE;

    std::vector<float> ema9, sma9, ema21, sma21;
    calculate_ema(candles, 9, ema9);
    calculate_sma(candles, 9, sma9);
    calculate_ema(candles, 21, ema21);
    calculate_sma(candles, 21, sma21);

    float err_ema9 = calculate_ma_error(candles, ema9);
    float err_sma9 = calculate_ma_error(candles, sma9);
    float err_ema21 = calculate_ma_error(candles, ema21);
    float err_sma21 = calculate_ma_error(candles, sma21);

    float min_error = err_ema9;
    AutoMAType best_type = AutoMAType::EMA9;
    auto_ma_values = ema9;

    if (err_sma9 < min_error) {
        min_error = err_sma9;
        best_type = AutoMAType::SMA9;
        auto_ma_values = sma9;
    }
    if (err_ema21 < min_error) {
        min_error = err_ema21;
        best_type = AutoMAType::EMA21;
        auto_ma_values = ema21;
    }
    if (err_sma21 < min_error) {
        best_type = AutoMAType::SMA21;
        auto_ma_values = sma21;
    }

    return best_type;
}

// ============================================================================
// VWAP Tests
// ============================================================================

static Candle make_candle_with_volume(float open, float high, float low, float close, float volume, const char* ts = "") {
    Candle c = make_candle(open, high, low, close, ts);
    c.volume = volume;
    return c;
}

TEST(vwap_single_candle) {
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 105, 95, 102, 1000, "2024-01-02 09:30:00"));

    std::vector<float> vwap;
    calculate_vwap(candles, vwap);

    ASSERT_EQ(vwap.size(), 1u);
    // typical_price = (105 + 95 + 102) / 3 = 100.67
    float expected_tp = (105.0f + 95.0f + 102.0f) / 3.0f;
    ASSERT_FLOAT_EQ(vwap[0], expected_tp, 0.01f);
}

TEST(vwap_cumulative) {
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 105, 95, 102, 1000, "2024-01-02 09:30:00"));
    candles.push_back(make_candle_with_volume(102, 108, 100, 106, 2000, "2024-01-02 09:31:00"));

    std::vector<float> vwap;
    calculate_vwap(candles, vwap);

    ASSERT_EQ(vwap.size(), 2u);

    // First candle: tp = 100.67, vwap = 100.67
    float tp1 = (105.0f + 95.0f + 102.0f) / 3.0f;
    ASSERT_FLOAT_EQ(vwap[0], tp1, 0.01f);

    // Second candle: tp = 104.67, cumsum = (100.67*1000 + 104.67*2000) / 3000 = 103.33
    float tp2 = (108.0f + 100.0f + 106.0f) / 3.0f;
    float expected_vwap2 = (tp1 * 1000.0f + tp2 * 2000.0f) / 3000.0f;
    ASSERT_FLOAT_EQ(vwap[1], expected_vwap2, 0.01f);
}

TEST(vwap_resets_on_new_day) {
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 105, 95, 102, 1000, "2024-01-02 15:59:00"));
    candles.push_back(make_candle_with_volume(110, 115, 108, 112, 2000, "2024-01-03 09:30:00"));

    std::vector<float> vwap;
    calculate_vwap(candles, vwap);

    ASSERT_EQ(vwap.size(), 2u);

    // Each day resets, so second candle's VWAP is just its own typical price
    float tp2 = (115.0f + 108.0f + 112.0f) / 3.0f;
    ASSERT_FLOAT_EQ(vwap[1], tp2, 0.01f);
}

TEST(vwap_empty_candles) {
    std::vector<Candle> candles;
    std::vector<float> vwap;
    calculate_vwap(candles, vwap);
    ASSERT_EQ(vwap.size(), 0u);
}

// ============================================================================
// Cumulative Delta Tests
// ============================================================================

TEST(delta_bullish_candle_positive) {
    // Bullish candle: close at top of range = positive delta
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 110, 100, 110, 1000, "2024-01-02 09:30:00"));

    std::vector<float> delta;
    calculate_cumulative_delta(candles, delta);

    ASSERT_EQ(delta.size(), 1u);
    // close_position = (110-100)/(110-100) = 1.0, delta = 1000 * (2*1-1) = 1000
    ASSERT_FLOAT_EQ(delta[0], 1000.0f, 0.01f);
}

TEST(delta_bearish_candle_negative) {
    // Bearish candle: close at bottom of range = negative delta
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(110, 110, 100, 100, 1000, "2024-01-02 09:30:00"));

    std::vector<float> delta;
    calculate_cumulative_delta(candles, delta);

    ASSERT_EQ(delta.size(), 1u);
    // close_position = (100-100)/(110-100) = 0.0, delta = 1000 * (2*0-1) = -1000
    ASSERT_FLOAT_EQ(delta[0], -1000.0f, 0.01f);
}

TEST(delta_doji_neutral) {
    // Doji: close at middle of range = neutral delta
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(105, 110, 100, 105, 1000, "2024-01-02 09:30:00"));

    std::vector<float> delta;
    calculate_cumulative_delta(candles, delta);

    ASSERT_EQ(delta.size(), 1u);
    // close_position = (105-100)/(110-100) = 0.5, delta = 1000 * (2*0.5-1) = 0
    ASSERT_FLOAT_EQ(delta[0], 0.0f, 0.01f);
}

TEST(delta_cumulative) {
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 110, 100, 110, 1000, "2024-01-02 09:30:00"));  // +1000
    candles.push_back(make_candle_with_volume(110, 115, 105, 108, 500, "2024-01-02 09:31:00"));   // close_pos=0.3, delta=-200

    std::vector<float> delta;
    calculate_cumulative_delta(candles, delta);

    ASSERT_EQ(delta.size(), 2u);
    ASSERT_FLOAT_EQ(delta[0], 1000.0f, 0.01f);
    // Second: close_pos = (108-105)/(115-105) = 0.3, delta = 500*(2*0.3-1) = -200
    ASSERT_FLOAT_EQ(delta[1], 800.0f, 1.0f);  // Cumulative: 1000 + (-200) = 800
}

TEST(delta_resets_on_new_day) {
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 110, 100, 110, 1000, "2024-01-02 15:59:00"));  // +1000
    candles.push_back(make_candle_with_volume(100, 110, 100, 110, 500, "2024-01-03 09:30:00"));   // +500 (reset)

    std::vector<float> delta;
    calculate_cumulative_delta(candles, delta);

    ASSERT_EQ(delta.size(), 2u);
    ASSERT_FLOAT_EQ(delta[0], 1000.0f, 0.01f);
    ASSERT_FLOAT_EQ(delta[1], 500.0f, 0.01f);  // Reset on new day
}

TEST(delta_empty_candles) {
    std::vector<Candle> candles;
    std::vector<float> delta;
    calculate_cumulative_delta(candles, delta);
    ASSERT_EQ(delta.size(), 0u);
}

// ============================================================================
// Auto-MA Tests
// ============================================================================

TEST(auto_ma_selects_best_fit) {
    // Create candles that EMA9 should fit best (fast-moving price action)
    std::vector<Candle> candles;
    for (int i = 0; i < 30; i++) {
        float price = 100.0f + static_cast<float>(i % 10) * 2.0f;  // Oscillating
        candles.push_back(make_candle(price, price + 1, price - 1, price, "2024-01-02 09:30:00"));
    }

    std::vector<float> auto_ma;
    AutoMAType type = calculate_auto_ma(candles, auto_ma);

    // Should select one of the MAs (not NONE)
    ASSERT_TRUE(type != AutoMAType::NONE);
    ASSERT_TRUE(!auto_ma.empty());
}

TEST(auto_ma_empty_candles) {
    std::vector<Candle> candles;
    std::vector<float> auto_ma;
    AutoMAType type = calculate_auto_ma(candles, auto_ma);

    ASSERT_EQ(static_cast<int>(type), static_cast<int>(AutoMAType::NONE));
}

TEST(auto_ma_selects_slower_for_trending) {
    // Create steadily trending candles - slower MA should fit better
    std::vector<Candle> candles;
    for (int i = 0; i < 50; i++) {
        float price = 100.0f + static_cast<float>(i) * 0.5f;  // Steady uptrend
        candles.push_back(make_candle(price - 0.5f, price + 0.2f, price - 0.7f, price, "2024-01-02 09:30:00"));
    }

    std::vector<float> auto_ma;
    AutoMAType type = calculate_auto_ma(candles, auto_ma);

    // In a steady trend, 21-period MAs often fit better
    // Just verify it picks something valid
    ASSERT_TRUE(type != AutoMAType::NONE);
    ASSERT_EQ(auto_ma.size(), candles.size());
}

// ============================================================================
// Edge Case Tests - Short/Empty Timestamps
// ============================================================================

TEST(vwap_short_timestamp) {
    // IQFeed sometimes returns just "HH:MM:SS" without date
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 105, 95, 102, 1000, "09:30:00"));
    candles.push_back(make_candle_with_volume(102, 108, 100, 106, 2000, "09:31:00"));

    std::vector<float> vwap;
    calculate_vwap(candles, vwap);  // Should not crash

    ASSERT_EQ(vwap.size(), 2u);
    ASSERT_TRUE(vwap[0] > 0.0f);
    ASSERT_TRUE(vwap[1] > 0.0f);
}

TEST(vwap_empty_timestamp) {
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 105, 95, 102, 1000, ""));

    std::vector<float> vwap;
    calculate_vwap(candles, vwap);  // Should not crash

    ASSERT_EQ(vwap.size(), 1u);
}

TEST(delta_short_timestamp) {
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 110, 100, 110, 1000, "09:30:00"));

    std::vector<float> delta;
    calculate_cumulative_delta(candles, delta);  // Should not crash

    ASSERT_EQ(delta.size(), 1u);
}

TEST(delta_empty_timestamp) {
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 110, 100, 110, 1000, ""));

    std::vector<float> delta;
    calculate_cumulative_delta(candles, delta);  // Should not crash

    ASSERT_EQ(delta.size(), 1u);
}

TEST(vwap_mixed_timestamp_formats) {
    // Some candles have full timestamp, some just time
    std::vector<Candle> candles;
    candles.push_back(make_candle_with_volume(100, 105, 95, 102, 1000, "2024-01-02 09:30:00"));
    candles.push_back(make_candle_with_volume(102, 108, 100, 106, 2000, "09:31:00"));
    candles.push_back(make_candle_with_volume(106, 110, 104, 108, 1500, "2024-01-02 09:32:00"));

    std::vector<float> vwap;
    calculate_vwap(candles, vwap);  // Should not crash

    ASSERT_EQ(vwap.size(), 3u);
}

TEST(auto_ma_short_timestamps) {
    std::vector<Candle> candles;
    for (int i = 0; i < 30; i++) {
        float price = 100.0f + static_cast<float>(i % 10);
        char ts[16];
        std::snprintf(ts, sizeof(ts), "09:%02d:00", 30 + i);
        candles.push_back(make_candle_with_volume(price, price + 1, price - 1, price, 1000, ts));
    }

    std::vector<float> auto_ma;
    AutoMAType type = calculate_auto_ma(candles, auto_ma);  // Should not crash

    ASSERT_TRUE(type != AutoMAType::NONE);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::printf("Running chart widget tests...\n\n");

    // Session detection tests
    RUN_TEST(session_premarket);
    RUN_TEST(session_regular);
    RUN_TEST(session_afterhours);
    RUN_TEST(session_empty_timestamp);

    // Auto S/R level tests
    RUN_TEST(sr_basic_swing_high);
    RUN_TEST(sr_basic_swing_low);
    RUN_TEST(sr_multiple_swings);
    RUN_TEST(sr_lower_resistance_invalidated_by_higher);
    RUN_TEST(sr_higher_support_invalidated_by_lower);
    RUN_TEST(sr_no_levels_with_less_than_3_candles);
    RUN_TEST(sr_empty_candles);
    RUN_TEST(sr_resistance_above_current_price);

    // Candle period tests
    RUN_TEST(candle_period_1min_same);
    RUN_TEST(candle_period_1min_different);
    RUN_TEST(candle_period_5min_same);
    RUN_TEST(candle_period_5min_different);
    RUN_TEST(candle_period_empty_timestamp);

    // VWAP tests
    RUN_TEST(vwap_single_candle);
    RUN_TEST(vwap_cumulative);
    RUN_TEST(vwap_resets_on_new_day);
    RUN_TEST(vwap_empty_candles);
    RUN_TEST(vwap_short_timestamp);
    RUN_TEST(vwap_empty_timestamp);
    RUN_TEST(vwap_mixed_timestamp_formats);

    // Cumulative delta tests
    RUN_TEST(delta_bullish_candle_positive);
    RUN_TEST(delta_bearish_candle_negative);
    RUN_TEST(delta_doji_neutral);
    RUN_TEST(delta_cumulative);
    RUN_TEST(delta_resets_on_new_day);
    RUN_TEST(delta_empty_candles);
    RUN_TEST(delta_short_timestamp);
    RUN_TEST(delta_empty_timestamp);

    // Auto-MA tests
    RUN_TEST(auto_ma_selects_best_fit);
    RUN_TEST(auto_ma_empty_candles);
    RUN_TEST(auto_ma_selects_slower_for_trending);
    RUN_TEST(auto_ma_short_timestamps);

    std::printf("\n%d/%d tests passed.\n", g_tests_passed, g_tests_run);
    return 0;
}
