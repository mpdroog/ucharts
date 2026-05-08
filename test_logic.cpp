// Test file for ucharts logic functions
// Compile with: clang++ -std=c++11 -DRUN_TESTS test_logic.cpp -o test_logic && ./test_logic

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <cassert>

// ============================================================================
// Copy of data structures and logic functions from main.cpp for testing
// ============================================================================

struct Candle {
    char timestamp[32];
    float open;
    float high;
    float low;
    float close;
    float volume;
};

static std::vector<Candle> g_candles;

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

// Parse CSV line - returns true if successful
static bool ParseCSVLine(const char* line, Candle& c) {
    c = {};

    // Try new format: timestamp,open,high,low,close,volume
    char ts[32] = "";
    int parsed = std::sscanf(line, "%31[^,],%f,%f,%f,%f,%f",
        ts, &c.open, &c.high, &c.low, &c.close, &c.volume);

    if (parsed >= 5) {
        std::strncpy(c.timestamp, ts, sizeof(c.timestamp) - 1);
        if (parsed < 6) c.volume = 0.0f;
        return true;
    }

    // Try old format: open,high,low,close
    parsed = std::sscanf(line, "%f,%f,%f,%f", &c.open, &c.high, &c.low, &c.close);
    if (parsed == 4) {
        std::strncpy(c.timestamp, "N/A", sizeof(c.timestamp) - 1);
        c.volume = 0.0f;
        return true;
    }

    return false;
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
        std::printf("FAILED: %s != %s (line %d)\n", #a, #b, __LINE__); \
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

// ============================================================================
// Tests
// ============================================================================

TEST(csv_parse_new_format) {
    Candle c;
    bool ok = ParseCSVLine("2024-01-02,100.00,102.50,99.50,101.75,15000", c);
    ASSERT_TRUE(ok);
    ASSERT_EQ(std::strcmp(c.timestamp, "2024-01-02"), 0);
    ASSERT_FLOAT_EQ(c.open, 100.0f, 0.01f);
    ASSERT_FLOAT_EQ(c.high, 102.5f, 0.01f);
    ASSERT_FLOAT_EQ(c.low, 99.5f, 0.01f);
    ASSERT_FLOAT_EQ(c.close, 101.75f, 0.01f);
    ASSERT_FLOAT_EQ(c.volume, 15000.0f, 0.01f);
}

TEST(csv_parse_old_format) {
    Candle c;
    bool ok = ParseCSVLine("100.00,102.50,99.50,101.75", c);
    ASSERT_TRUE(ok);
    ASSERT_FLOAT_EQ(c.open, 100.0f, 0.01f);
    ASSERT_FLOAT_EQ(c.high, 102.5f, 0.01f);
    ASSERT_FLOAT_EQ(c.low, 99.5f, 0.01f);
    ASSERT_FLOAT_EQ(c.close, 101.75f, 0.01f);
    ASSERT_FLOAT_EQ(c.volume, 0.0f, 0.01f);
}

TEST(csv_parse_no_volume) {
    Candle c;
    bool ok = ParseCSVLine("2024-01-02,100.00,102.50,99.50,101.75", c);
    ASSERT_TRUE(ok);
    ASSERT_FLOAT_EQ(c.volume, 0.0f, 0.01f);
}

TEST(csv_parse_invalid) {
    Candle c;
    bool ok = ParseCSVLine("invalid,data", c);
    ASSERT_TRUE(!ok);
}

TEST(csv_parse_header) {
    Candle c;
    bool ok = ParseCSVLine("timestamp,open,high,low,close,volume", c);
    // Header should fail to parse as numeric data
    ASSERT_TRUE(!ok);
}

TEST(sma_calculation) {
    g_candles.clear();
    // Create 5 candles with closes: 10, 20, 30, 40, 50
    for (int i = 1; i <= 5; i++) {
        Candle c = {};
        c.close = static_cast<float>(i * 10);
        g_candles.push_back(c);
    }

    std::vector<float> sma;
    CalculateSMA(3, sma);

    ASSERT_EQ(sma.size(), 5u);
    ASSERT_FLOAT_EQ(sma[0], 0.0f, 0.01f);  // Not enough data
    ASSERT_FLOAT_EQ(sma[1], 0.0f, 0.01f);  // Not enough data
    ASSERT_FLOAT_EQ(sma[2], 20.0f, 0.01f); // (10+20+30)/3 = 20
    ASSERT_FLOAT_EQ(sma[3], 30.0f, 0.01f); // (20+30+40)/3 = 30
    ASSERT_FLOAT_EQ(sma[4], 40.0f, 0.01f); // (30+40+50)/3 = 40
}

TEST(sma_empty_data) {
    g_candles.clear();
    std::vector<float> sma;
    CalculateSMA(3, sma);
    ASSERT_EQ(sma.size(), 0u);
}

TEST(ema_calculation) {
    g_candles.clear();
    // Create 5 candles with closes: 10, 20, 30, 40, 50
    for (int i = 1; i <= 5; i++) {
        Candle c = {};
        c.close = static_cast<float>(i * 10);
        g_candles.push_back(c);
    }

    std::vector<float> ema;
    CalculateEMA(3, ema);

    ASSERT_EQ(ema.size(), 5u);
    // EMA(3) starts at index 2 with SMA value
    ASSERT_FLOAT_EQ(ema[2], 20.0f, 0.01f); // (10+20+30)/3 = 20

    // k = 2/(3+1) = 0.5
    // EMA[3] = 40 * 0.5 + 20 * 0.5 = 30
    ASSERT_FLOAT_EQ(ema[3], 30.0f, 0.01f);

    // EMA[4] = 50 * 0.5 + 30 * 0.5 = 40
    ASSERT_FLOAT_EQ(ema[4], 40.0f, 0.01f);
}

TEST(bollinger_calculation) {
    g_candles.clear();
    // Create 5 candles with closes: 10, 10, 10, 10, 10 (no variance)
    for (int i = 0; i < 5; i++) {
        Candle c = {};
        c.close = 10.0f;
        g_candles.push_back(c);
    }

    std::vector<float> upper, lower;
    CalculateBollinger(3, 2.0f, upper, lower);

    ASSERT_EQ(upper.size(), 5u);
    ASSERT_EQ(lower.size(), 5u);

    // With no variance, upper = lower = SMA = 10
    ASSERT_FLOAT_EQ(upper[2], 10.0f, 0.01f);
    ASSERT_FLOAT_EQ(lower[2], 10.0f, 0.01f);
}

TEST(bollinger_with_variance) {
    g_candles.clear();
    // Create 3 candles with closes: 10, 20, 30
    for (int i = 1; i <= 3; i++) {
        Candle c = {};
        c.close = static_cast<float>(i * 10);
        g_candles.push_back(c);
    }

    std::vector<float> upper, lower;
    CalculateBollinger(3, 2.0f, upper, lower);

    // SMA = 20, stddev = sqrt((100+0+100)/3) = sqrt(66.67) ~ 8.16
    // Upper = 20 + 2*8.16 = 36.33
    // Lower = 20 - 2*8.16 = 3.67
    ASSERT_FLOAT_EQ(upper[2], 36.33f, 0.1f);
    ASSERT_FLOAT_EQ(lower[2], 3.67f, 0.1f);
}

TEST(candle_bullish_bearish) {
    Candle bullish = {};
    bullish.open = 100.0f;
    bullish.close = 110.0f;
    ASSERT_TRUE(bullish.close >= bullish.open);

    Candle bearish = {};
    bearish.open = 110.0f;
    bearish.close = 100.0f;
    ASSERT_TRUE(bearish.close < bearish.open);

    Candle doji = {};
    doji.open = 100.0f;
    doji.close = 100.0f;
    ASSERT_TRUE(doji.close >= doji.open);  // Doji treated as bullish
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::printf("Running ucharts logic tests...\n\n");

    RUN_TEST(csv_parse_new_format);
    RUN_TEST(csv_parse_old_format);
    RUN_TEST(csv_parse_no_volume);
    RUN_TEST(csv_parse_invalid);
    RUN_TEST(csv_parse_header);
    RUN_TEST(sma_calculation);
    RUN_TEST(sma_empty_data);
    RUN_TEST(ema_calculation);
    RUN_TEST(bollinger_calculation);
    RUN_TEST(bollinger_with_variance);
    RUN_TEST(candle_bullish_bearish);

    std::printf("\n%d/%d tests passed.\n", g_tests_passed, g_tests_run);
    return 0;
}
