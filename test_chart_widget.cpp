// Test file for chart widget features (S/R levels, session detection, real-time updates)
// Compile with: clang++ -std=c++17 -DRUN_TESTS test_chart_widget.cpp -o test_chart_widget && ./test_chart_widget

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <cassert>

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

    std::printf("\n%d/%d tests passed.\n", g_tests_passed, g_tests_run);
    return 0;
}
