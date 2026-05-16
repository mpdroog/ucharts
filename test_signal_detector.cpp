// test_signal_detector.cpp - Unit tests for signal detector
#include "signal_detector.h"
#include "sr_calculator.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cmath>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    static struct TestRegister_##name { \
        TestRegister_##name() { \
            std::printf("Running %s... ", #name); \
            test_##name(); \
            std::printf("PASSED\n"); \
            g_tests_passed++; \
        } \
    } g_test_register_##name; \
    static void test_##name()

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::printf("FAILED\n  %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, tol) do { \
    if (std::fabs((a) - (b)) > (tol)) { \
        std::printf("FAILED\n  %s:%d: %s (%.4f) != %s (%.4f) within %.4f\n", \
                    __FILE__, __LINE__, #a, static_cast<double>(a), #b, static_cast<double>(b), static_cast<double>(tol)); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        std::printf("FAILED\n  %s:%d: %s is false\n", __FILE__, __LINE__, #expr); \
        g_tests_failed++; \
        return; \
    } \
} while(0)

// Helper to create a candle
static Candle make_candle(float o, float h, float l, float c, float v = 1000.0f) {
    Candle candle;
    candle.open = o;
    candle.high = h;
    candle.low = l;
    candle.close = c;
    candle.volume = v;
    std::strcpy(candle.timestamp, "2024-01-01 09:30");
    return candle;
}

// Helper to create S/R levels
static std::vector<SRLevel> make_sr_levels(
    const std::vector<std::pair<float, SRType>>& level_data
) {
    std::vector<SRLevel> levels;
    for (const auto& pair : level_data) {
        levels.emplace_back(pair.first, pair.second, SRTimeframe::M5, 0);
    }
    return levels;
}

// ============================================================================
// Breakout Detection Tests
// ============================================================================

TEST(breakout_long_detected) {
    SignalDetector detector;
    detector.set_min_rr(1.0f);  // Lower threshold for testing
    detector.set_min_volume_ratio(0.0f);  // Disable volume check for unit test

    // Need enough candles for volume calculation (20+)
    std::vector<Candle> candles;
    for (int i = 0; i < 20; i++) {
        candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    }
    // idx 20: previous candle below resistance
    candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    // idx 21: breaks above 100 with bullish candle
    candles.push_back(make_candle(99.5f, 101, 99, 100.5f, 2000.0f));

    auto sr_levels = make_sr_levels({
        {100.0f, SRType::RESISTANCE},
        {95.0f, SRType::SUPPORT},
        {110.0f, SRType::RESISTANCE}  // Target
    });

    auto signal = detector.detect_breakout(candles, sr_levels, 21);

    ASSERT_EQ(signal.type, SignalType::BREAKOUT);
    ASSERT_TRUE(signal.is_long());
    ASSERT_NEAR(signal.entry_price, 100.5f, 0.01f);
    ASSERT_NEAR(signal.broken_level, 100.0f, 0.01f);
}

TEST(breakout_no_signal_when_no_break) {
    SignalDetector detector;
    detector.set_min_volume_ratio(0.0f);

    std::vector<Candle> candles;
    for (int i = 0; i < 20; i++) {
        candles.push_back(make_candle(99, 99.5f, 98, 99, 1000.0f));
    }
    // idx 20: below resistance
    candles.push_back(make_candle(99, 99.5f, 98, 99, 1000.0f));
    // idx 21: still below resistance
    candles.push_back(make_candle(99, 99.8f, 98.5f, 99.5f, 1000.0f));

    auto sr_levels = make_sr_levels({
        {100.0f, SRType::RESISTANCE},
        {95.0f, SRType::SUPPORT}
    });

    auto signal = detector.detect_breakout(candles, sr_levels, 21);

    ASSERT_EQ(signal.type, SignalType::NONE);
}

TEST(breakout_requires_bullish_candle_for_long) {
    SignalDetector detector;
    detector.set_min_volume_ratio(0.0f);

    std::vector<Candle> candles;
    for (int i = 0; i < 20; i++) {
        candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    }
    // idx 20: below resistance
    candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    // idx 21: Bearish candle breaking resistance (close < open)
    candles.push_back(make_candle(100.5f, 101, 99.5f, 100.2f, 2000.0f));

    auto sr_levels = make_sr_levels({
        {100.0f, SRType::RESISTANCE},
        {95.0f, SRType::SUPPORT},
        {110.0f, SRType::RESISTANCE}
    });

    auto signal = detector.detect_breakout(candles, sr_levels, 21);

    // Should not trigger because candle is bearish
    ASSERT_EQ(signal.type, SignalType::NONE);
}

TEST(breakout_requires_volume) {
    SignalDetector detector;
    detector.set_min_rr(1.0f);
    detector.set_min_volume_ratio(1.5f);  // Require 50% above average

    std::vector<Candle> candles;
    for (int i = 0; i < 20; i++) {
        candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    }
    // idx 20: below resistance
    candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    // idx 21: breaks above but LOW volume
    candles.push_back(make_candle(99.5f, 101, 99, 100.5f, 500.0f));

    auto sr_levels = make_sr_levels({
        {100.0f, SRType::RESISTANCE},
        {95.0f, SRType::SUPPORT},
        {110.0f, SRType::RESISTANCE}
    });

    auto signal = detector.detect_breakout(candles, sr_levels, 21);

    // Should not trigger due to low volume
    ASSERT_EQ(signal.type, SignalType::NONE);
}

TEST(breakout_with_good_volume) {
    SignalDetector detector;
    detector.set_min_rr(1.0f);
    detector.set_min_volume_ratio(1.2f);  // Require 20% above average

    std::vector<Candle> candles;
    for (int i = 0; i < 20; i++) {
        candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    }
    // idx 20: below resistance
    candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    // idx 21: breaks above with HIGH volume (2x average)
    candles.push_back(make_candle(99.5f, 101, 99, 100.5f, 2000.0f));

    auto sr_levels = make_sr_levels({
        {100.0f, SRType::RESISTANCE},
        {95.0f, SRType::SUPPORT},
        {110.0f, SRType::RESISTANCE}
    });

    auto signal = detector.detect_breakout(candles, sr_levels, 21);

    // Should trigger with good volume
    ASSERT_EQ(signal.type, SignalType::BREAKOUT);
    ASSERT_TRUE(signal.volume_ratio >= 1.2f);
}

// ============================================================================
// R:R Calculation Tests
// ============================================================================

TEST(rr_calculated_correctly) {
    SignalDetector detector;
    detector.set_min_rr(2.0f);
    detector.set_min_volume_ratio(0.0f);

    std::vector<Candle> candles;
    for (int i = 0; i < 20; i++) {
        candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    }
    candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    // Entry at 101, stop at 100*0.995=99.5, target at 110
    // Risk = 101 - 99.5 = 1.5, Reward = 110 - 101 = 9
    // R:R = 9 / 1.5 = 6.0
    candles.push_back(make_candle(99.5f, 101.5f, 99, 101.0f, 2000.0f));

    auto sr_levels = make_sr_levels({
        {100.0f, SRType::RESISTANCE},
        {95.0f, SRType::SUPPORT},
        {110.0f, SRType::RESISTANCE}  // Next target
    });

    auto signal = detector.detect_breakout(candles, sr_levels, 21);

    ASSERT_EQ(signal.type, SignalType::BREAKOUT);
    ASSERT_NEAR(signal.target, 110.0f, 0.01f);
    ASSERT_TRUE(signal.risk_reward >= 2.0f);
    ASSERT_TRUE(signal.is_valid);
}

TEST(signal_invalid_when_rr_below_minimum) {
    SignalDetector detector;
    detector.set_min_rr(10.0f);  // Very high threshold
    detector.set_min_volume_ratio(0.0f);

    std::vector<Candle> candles;
    for (int i = 0; i < 20; i++) {
        candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    }
    candles.push_back(make_candle(99, 100, 98, 99.5f, 1000.0f));
    candles.push_back(make_candle(99.5f, 101.5f, 99, 101.0f, 2000.0f));

    auto sr_levels = make_sr_levels({
        {100.0f, SRType::RESISTANCE},
        {95.0f, SRType::SUPPORT},
        {102.0f, SRType::RESISTANCE}  // Close target = poor R:R
    });

    auto signal = detector.detect_breakout(candles, sr_levels, 21);

    // Signal detected but not valid due to R:R
    ASSERT_EQ(signal.type, SignalType::BREAKOUT);
    ASSERT_TRUE(!signal.is_valid);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(handles_empty_candles) {
    SignalDetector detector;
    std::vector<Candle> candles;
    auto sr_levels = make_sr_levels({{100.0f, SRType::RESISTANCE}});

    auto signal = detector.detect_breakout(candles, sr_levels, 0);
    ASSERT_EQ(signal.type, SignalType::NONE);
}

TEST(handles_invalid_candle_idx) {
    SignalDetector detector;
    std::vector<Candle> candles = {make_candle(100, 101, 99, 100)};
    auto sr_levels = make_sr_levels({{100.0f, SRType::RESISTANCE}});

    auto signal = detector.detect_breakout(candles, sr_levels, 5);  // Out of bounds
    ASSERT_EQ(signal.type, SignalType::NONE);
}

TEST(handles_empty_sr_levels) {
    SignalDetector detector;
    detector.set_min_volume_ratio(0.0f);

    std::vector<Candle> candles;
    for (int i = 0; i < 22; i++) {
        candles.push_back(make_candle(99, 101, 98.5f, 101, 1000.0f));
    }
    std::vector<SRLevel> sr_levels;

    auto signal = detector.detect_breakout(candles, sr_levels, 21);
    ASSERT_EQ(signal.type, SignalType::NONE);  // No S/R to break
}

TEST(handles_no_resistance_above) {
    SignalDetector detector;
    detector.set_min_volume_ratio(0.0f);

    std::vector<Candle> candles;
    for (int i = 0; i < 22; i++) {
        candles.push_back(make_candle(99, 101, 98.5f, 101, 1000.0f));
    }

    // Only support levels, no resistance
    auto sr_levels = make_sr_levels({
        {95.0f, SRType::SUPPORT},
        {90.0f, SRType::SUPPORT}
    });

    auto signal = detector.detect_breakout(candles, sr_levels, 21);
    ASSERT_EQ(signal.type, SignalType::NONE);  // No resistance to break
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::printf("\n=== Signal Detector Tests ===\n\n");

    // Tests are auto-registered and run via static initialization

    std::printf("\n=== Results ===\n");
    std::printf("Passed: %d\n", g_tests_passed);
    std::printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
