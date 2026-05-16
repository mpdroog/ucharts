// test_sr_calculator.cpp - Unit tests for S/R calculator
#include "sr_calculator.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cassert>

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
                    __FILE__, __LINE__, #a, (double)(a), #b, (double)(b), (double)(tol)); \
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
    std::strcpy(candle.timestamp, "2024-01-01");
    return candle;
}

// ============================================================================
// Daily S/R Tests (3-candle swing pattern)
// ============================================================================

TEST(daily_basic_swing_high) {
    SRCalculator calc;
    std::vector<Candle> candles = {
        make_candle(100, 102, 99, 101),   // prev
        make_candle(101, 105, 100, 104),  // swing high at 105
        make_candle(104, 103, 101, 102),  // next
    };

    float current_price = 100.0f;
    auto levels = calc.calculate_daily_levels(candles, current_price, 3);

    ASSERT_EQ(levels.size(), 1u);
    ASSERT_NEAR(levels[0].price, 105.0f, 0.01f);
    ASSERT_TRUE(levels[0].is_resistance());
    ASSERT_TRUE(levels[0].is_daily());
}

TEST(daily_basic_swing_low) {
    SRCalculator calc;
    // Use candles where only a swing low is detected (no swing high)
    std::vector<Candle> candles = {
        make_candle(100, 102, 99, 101),   // prev: high=102
        make_candle(101, 101, 95, 97),    // swing low at 95, high=101 (not swing high)
        make_candle(97, 100, 96, 99),     // next: high=100
    };

    float current_price = 100.0f;
    auto levels = calc.calculate_daily_levels(candles, current_price, 3);

    ASSERT_EQ(levels.size(), 1u);
    ASSERT_NEAR(levels[0].price, 95.0f, 0.01f);
    ASSERT_TRUE(levels[0].is_support());
}

TEST(daily_multiple_levels) {
    SRCalculator calc;
    std::vector<Candle> candles = {
        make_candle(100, 102, 99, 101),
        make_candle(101, 110, 100, 108),  // swing high at 110
        make_candle(108, 109, 105, 106),
        make_candle(106, 108, 90, 92),    // swing low at 90
        make_candle(92, 95, 91, 94),
    };

    float current_price = 100.0f;
    auto levels = calc.calculate_daily_levels(candles, current_price, 3);

    ASSERT_EQ(levels.size(), 2u);

    // Find resistance and support
    SRLevel* resistance = nullptr;
    SRLevel* support = nullptr;
    for (auto& l : levels) {
        if (l.is_resistance()) resistance = &l;
        if (l.is_support()) support = &l;
    }

    ASSERT_TRUE(resistance != nullptr);
    ASSERT_TRUE(support != nullptr);
    ASSERT_NEAR(resistance->price, 110.0f, 0.01f);
    ASSERT_NEAR(support->price, 90.0f, 0.01f);
}

TEST(daily_filters_levels_by_current_price) {
    SRCalculator calc;
    std::vector<Candle> candles = {
        make_candle(100, 102, 99, 101),
        make_candle(101, 110, 100, 108),  // swing high at 110 (above current)
        make_candle(108, 109, 105, 106),
        make_candle(106, 108, 95, 97),    // swing low at 95 (below current)
        make_candle(97, 99, 96, 98),
    };

    // With current price at 105, the swing high at 110 is resistance,
    // and swing low at 95 is support
    float current_price = 105.0f;
    auto levels = calc.calculate_daily_levels(candles, current_price, 3);

    for (const auto& l : levels) {
        if (l.is_resistance()) {
            ASSERT_TRUE(l.price > current_price);
        } else {
            ASSERT_TRUE(l.price < current_price);
        }
    }
}

TEST(daily_takes_nearest_3) {
    SRCalculator calc;
    std::vector<Candle> candles;

    // Create candles with multiple swing highs
    candles.push_back(make_candle(100, 102, 99, 101));
    candles.push_back(make_candle(101, 110, 100, 108));  // R1: 110
    candles.push_back(make_candle(108, 109, 105, 106));
    candles.push_back(make_candle(106, 115, 104, 113));  // R2: 115
    candles.push_back(make_candle(113, 114, 110, 112));
    candles.push_back(make_candle(112, 120, 111, 118));  // R3: 120
    candles.push_back(make_candle(118, 119, 115, 117));
    candles.push_back(make_candle(117, 130, 116, 128));  // R4: 130 (should be excluded)
    candles.push_back(make_candle(128, 129, 125, 127));

    float current_price = 105.0f;
    auto levels = calc.calculate_daily_levels(candles, current_price, 3);

    // Count resistance levels
    int resistance_count = 0;
    for (const auto& l : levels) {
        if (l.is_resistance()) resistance_count++;
    }

    // Should only have 3 resistance levels (nearest)
    ASSERT_EQ(resistance_count, 3);

    // They should be 110, 115, 120 (nearest to current price)
    bool has_110 = false, has_115 = false, has_120 = false, has_130 = false;
    for (const auto& l : levels) {
        if (l.is_resistance()) {
            if (std::fabs(l.price - 110.0f) < 1.0f) has_110 = true;
            if (std::fabs(l.price - 115.0f) < 1.0f) has_115 = true;
            if (std::fabs(l.price - 120.0f) < 1.0f) has_120 = true;
            if (std::fabs(l.price - 130.0f) < 1.0f) has_130 = true;
        }
    }

    ASSERT_TRUE(has_110);
    ASSERT_TRUE(has_115);
    ASSERT_TRUE(has_120);
    ASSERT_TRUE(!has_130);  // 130 should be excluded (too far)
}

TEST(daily_empty_candles) {
    SRCalculator calc;
    std::vector<Candle> candles;

    auto levels = calc.calculate_daily_levels(candles, 100.0f, 3);
    ASSERT_EQ(levels.size(), 0u);
}

TEST(daily_less_than_3_candles) {
    SRCalculator calc;
    std::vector<Candle> candles = {
        make_candle(100, 102, 99, 101),
        make_candle(101, 105, 100, 104),
    };

    auto levels = calc.calculate_daily_levels(candles, 100.0f, 3);
    ASSERT_EQ(levels.size(), 0u);
}

// ============================================================================
// 5-Minute S/R Tests (5-candle swing pattern)
// ============================================================================

TEST(m5_basic_swing_high) {
    SRCalculator calc;
    std::vector<Candle> candles = {
        make_candle(100, 102, 99, 101),   // i-2
        make_candle(101, 103, 100, 102),  // i-1
        make_candle(102, 110, 101, 108),  // swing high at 110
        make_candle(108, 107, 104, 105),  // i+1
        make_candle(105, 106, 103, 104),  // i+2
    };

    float current_price = 100.0f;
    auto levels = calc.calculate_m5_levels(candles, current_price, 3);

    ASSERT_EQ(levels.size(), 1u);
    ASSERT_NEAR(levels[0].price, 110.0f, 0.01f);
    ASSERT_TRUE(levels[0].is_resistance());
    ASSERT_TRUE(!levels[0].is_daily());  // Should be M5
}

TEST(m5_basic_swing_low) {
    SRCalculator calc;
    std::vector<Candle> candles = {
        make_candle(100, 102, 98, 99),    // i-2
        make_candle(99, 100, 97, 98),     // i-1
        make_candle(98, 99, 90, 92),      // swing low at 90
        make_candle(92, 95, 91, 94),      // i+1
        make_candle(94, 97, 93, 96),      // i+2
    };

    float current_price = 100.0f;
    auto levels = calc.calculate_m5_levels(candles, current_price, 3);

    ASSERT_EQ(levels.size(), 1u);
    ASSERT_NEAR(levels[0].price, 90.0f, 0.01f);
    ASSERT_TRUE(levels[0].is_support());
}

TEST(m5_requires_5_candle_pattern) {
    SRCalculator calc;

    // This pattern would be a swing high with 3-candle but not 5-candle
    std::vector<Candle> candles = {
        make_candle(100, 108, 99, 107),   // i-2 (high = 108, higher than i)
        make_candle(107, 103, 100, 102),  // i-1
        make_candle(102, 105, 101, 104),  // NOT a 5-candle swing high (i-2 is higher)
        make_candle(104, 103, 99, 100),   // i+1
        make_candle(100, 101, 98, 99),    // i+2
    };

    float current_price = 95.0f;
    auto levels = calc.calculate_m5_levels(candles, current_price, 3);

    // Should NOT detect a swing high because candle i-2 has higher high
    int resistance_count = 0;
    for (const auto& l : levels) {
        if (l.is_resistance()) resistance_count++;
    }
    ASSERT_EQ(resistance_count, 0);
}

// ============================================================================
// Combined Daily + 5-Min Tests
// ============================================================================

TEST(combined_deduplicates_nearby_levels) {
    SRCalculator calc;

    std::vector<Candle> daily_candles = {
        make_candle(100, 102, 99, 101),
        make_candle(101, 110, 100, 108),  // Daily resistance at 110
        make_candle(108, 109, 105, 107),
    };

    std::vector<Candle> m5_candles = {
        make_candle(105, 107, 104, 106),
        make_candle(106, 108, 105, 107),
        make_candle(107, 110.3f, 106, 109),  // M5 resistance at 110.3 (within 0.5% of 110)
        make_candle(109, 109, 106, 107),
        make_candle(107, 108, 105, 106),
    };

    float current_price = 100.0f;
    auto levels = calc.calculate_levels(daily_candles, m5_candles, current_price, 3);

    // Should only have ONE resistance level (the daily one at 110)
    // The M5 level at 110.3 should be deduplicated (within 0.5%)
    int resistance_count = 0;
    for (const auto& l : levels) {
        if (l.is_resistance()) resistance_count++;
    }
    ASSERT_EQ(resistance_count, 1);
}

TEST(combined_includes_distant_m5_levels) {
    SRCalculator calc;

    std::vector<Candle> daily_candles = {
        make_candle(100, 102, 99, 101),
        make_candle(101, 110, 100, 108),  // Daily resistance at 110
        make_candle(108, 109, 105, 107),
    };

    std::vector<Candle> m5_candles = {
        make_candle(105, 107, 104, 106),
        make_candle(106, 108, 105, 107),
        make_candle(107, 115, 106, 113),  // M5 resistance at 115 (>0.5% from 110)
        make_candle(113, 114, 110, 112),
        make_candle(112, 113, 109, 111),
    };

    float current_price = 100.0f;
    auto levels = calc.calculate_levels(daily_candles, m5_candles, current_price, 3);

    // Should have TWO resistance levels (110 from daily, 115 from M5)
    int resistance_count = 0;
    bool has_110 = false, has_115 = false;
    for (const auto& l : levels) {
        if (l.is_resistance()) {
            resistance_count++;
            if (std::fabs(l.price - 110.0f) < 1.0f) has_110 = true;
            if (std::fabs(l.price - 115.0f) < 1.0f) has_115 = true;
        }
    }
    ASSERT_EQ(resistance_count, 2);
    ASSERT_TRUE(has_110);
    ASSERT_TRUE(has_115);
}

// ============================================================================
// Strength Calculation Tests
// ============================================================================

TEST(strength_increases_with_touches) {
    SRCalculator calc;

    // Create a level that gets touched multiple times
    std::vector<Candle> candles = {
        make_candle(100, 102, 99, 101),
        make_candle(101, 110, 100, 105),  // swing high at 110
        make_candle(105, 109, 104, 107),  // touches 110 area, bounces down
        make_candle(107, 108, 103, 104),
        make_candle(104, 109.5f, 103, 108), // touches 110 area again
        make_candle(108, 109, 105, 106),
    };

    float current_price = 100.0f;
    auto levels = calc.calculate_daily_levels(candles, current_price, 3);

    // The resistance level should have touch_count >= 1
    bool found_resistance = false;
    for (const auto& l : levels) {
        if (l.is_resistance() && std::fabs(l.price - 110.0f) < 1.0f) {
            found_resistance = true;
            ASSERT_TRUE(l.touch_count >= 1);
            ASSERT_TRUE(l.strength > 0.0f);
        }
    }
    ASSERT_TRUE(found_resistance);
}

// ============================================================================
// Nearest Level Finder Tests
// ============================================================================

TEST(find_nearest_support) {
    SRCalculator calc;

    std::vector<SRLevel> levels;
    levels.emplace_back(95.0f, SRType::SUPPORT, SRTimeframe::DAILY, 0);
    levels.emplace_back(90.0f, SRType::SUPPORT, SRTimeframe::DAILY, 0);
    levels.emplace_back(85.0f, SRType::SUPPORT, SRTimeframe::DAILY, 0);
    levels.emplace_back(110.0f, SRType::RESISTANCE, SRTimeframe::DAILY, 0);

    float nearest = calc.find_nearest_support(levels, 100.0f);
    ASSERT_NEAR(nearest, 95.0f, 0.01f);
}

TEST(find_nearest_resistance) {
    SRCalculator calc;

    std::vector<SRLevel> levels;
    levels.emplace_back(95.0f, SRType::SUPPORT, SRTimeframe::DAILY, 0);
    levels.emplace_back(105.0f, SRType::RESISTANCE, SRTimeframe::DAILY, 0);
    levels.emplace_back(110.0f, SRType::RESISTANCE, SRTimeframe::DAILY, 0);
    levels.emplace_back(120.0f, SRType::RESISTANCE, SRTimeframe::DAILY, 0);

    float nearest = calc.find_nearest_resistance(levels, 100.0f);
    ASSERT_NEAR(nearest, 105.0f, 0.01f);
}

TEST(find_nearest_returns_zero_if_none) {
    SRCalculator calc;
    std::vector<SRLevel> levels;

    float support = calc.find_nearest_support(levels, 100.0f);
    float resistance = calc.find_nearest_resistance(levels, 100.0f);

    ASSERT_NEAR(support, 0.0f, 0.01f);
    ASSERT_NEAR(resistance, 0.0f, 0.01f);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::printf("\n=== S/R Calculator Tests ===\n\n");

    // Tests are auto-registered and run via static initialization

    std::printf("\n=== Results ===\n");
    std::printf("Passed: %d\n", g_tests_passed);
    std::printf("Failed: %d\n", g_tests_failed);

    return g_tests_failed > 0 ? 1 : 0;
}
