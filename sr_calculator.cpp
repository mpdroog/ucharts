// sr_calculator.cpp - Multi-timeframe Support/Resistance calculation
#include "sr_calculator.h"
#include <algorithm>
#include <cmath>

SRCalculator::SRCalculator() {}

std::vector<SRLevel> SRCalculator::calculate_levels(
    const std::vector<Candle>& daily_candles,
    const std::vector<Candle>& m5_candles,
    float current_price,
    int max_levels_per_side
) {
    std::vector<SRLevel> all_resistance;
    std::vector<SRLevel> all_support;

    // 1. Extract swing highs/lows from daily (3-candle pattern)
    if (daily_candles.size() >= 3) {
        detect_swings_3candle(daily_candles, current_price, SRTimeframe::DAILY,
                              all_resistance, all_support);
    }

    // 2. Extract swing highs/lows from 5-min (5-candle pattern, more strict)
    if (m5_candles.size() >= 5) {
        std::vector<SRLevel> m5_resistance;
        std::vector<SRLevel> m5_support;
        detect_swings_5candle(m5_candles, current_price, SRTimeframe::M5,
                              m5_resistance, m5_support);

        // Only add 5-min levels if not too close to existing daily levels
        for (const auto& level : m5_resistance) {
            if (!has_nearby_level(all_resistance, level.price, 0.005f)) {
                all_resistance.push_back(level);
            }
        }
        for (const auto& level : m5_support) {
            if (!has_nearby_level(all_support, level.price, 0.005f)) {
                all_support.push_back(level);
            }
        }
    }

    // 3. Calculate strength scores for all levels using 5-min data (more granular)
    const std::vector<Candle>& strength_candles = m5_candles.empty() ? daily_candles : m5_candles;
    for (auto& level : all_resistance) {
        calculate_level_strength(level, strength_candles);
    }
    for (auto& level : all_support) {
        calculate_level_strength(level, strength_candles);
    }

    // 4. Sort and filter to nearest levels
    return filter_nearest(all_resistance, all_support, current_price, max_levels_per_side);
}

std::vector<SRLevel> SRCalculator::calculate_daily_levels(
    const std::vector<Candle>& daily_candles,
    float current_price,
    int max_levels_per_side
) {
    std::vector<SRLevel> resistance;
    std::vector<SRLevel> support;

    if (daily_candles.size() >= 3) {
        detect_swings_3candle(daily_candles, current_price, SRTimeframe::DAILY,
                              resistance, support);

        for (auto& level : resistance) {
            calculate_level_strength(level, daily_candles);
        }
        for (auto& level : support) {
            calculate_level_strength(level, daily_candles);
        }
    }

    return filter_nearest(resistance, support, current_price, max_levels_per_side);
}

std::vector<SRLevel> SRCalculator::calculate_m5_levels(
    const std::vector<Candle>& m5_candles,
    float current_price,
    int max_levels_per_side
) {
    std::vector<SRLevel> resistance;
    std::vector<SRLevel> support;

    if (m5_candles.size() >= 5) {
        detect_swings_5candle(m5_candles, current_price, SRTimeframe::M5,
                              resistance, support);

        for (auto& level : resistance) {
            calculate_level_strength(level, m5_candles);
        }
        for (auto& level : support) {
            calculate_level_strength(level, m5_candles);
        }
    }

    return filter_nearest(resistance, support, current_price, max_levels_per_side);
}

float SRCalculator::find_nearest_support(const std::vector<SRLevel>& levels, float price) const {
    float nearest = 0.0f;
    float min_distance = 1e9f;

    for (const auto& level : levels) {
        if (level.type == SRType::SUPPORT && level.price < price) {
            float distance = price - level.price;
            if (distance < min_distance) {
                min_distance = distance;
                nearest = level.price;
            }
        }
    }

    return nearest;
}

float SRCalculator::find_nearest_resistance(const std::vector<SRLevel>& levels, float price) const {
    float nearest = 0.0f;
    float min_distance = 1e9f;

    for (const auto& level : levels) {
        if (level.type == SRType::RESISTANCE && level.price > price) {
            float distance = level.price - price;
            if (distance < min_distance) {
                min_distance = distance;
                nearest = level.price;
            }
        }
    }

    return nearest;
}

bool SRCalculator::has_nearby_level(const std::vector<SRLevel>& levels, float price, float tolerance_pct) const {
    float tolerance = price * tolerance_pct;

    for (const auto& level : levels) {
        if (std::fabs(level.price - price) <= tolerance) {
            return true;
        }
    }

    return false;
}

void SRCalculator::detect_swings_3candle(
    const std::vector<Candle>& candles,
    float current_price,
    SRTimeframe tf,
    std::vector<SRLevel>& resistance,
    std::vector<SRLevel>& support
) {
    if (candles.size() < 3) return;

    float avg_volume = calculate_avg_volume(candles);
    float min_volume = avg_volume * 0.5f;

    for (size_t i = 1; i < candles.size() - 1; i++) {
        const Candle& prev = candles[i - 1];
        const Candle& curr = candles[i];
        const Candle& next = candles[i + 1];

        // Volume filter: skip candles with low volume
        bool has_significant_volume = (avg_volume <= 0) || (curr.volume >= min_volume);
        if (!has_significant_volume) continue;

        // Swing high -> potential resistance (only if ABOVE current price)
        if (curr.high > prev.high && curr.high > next.high) {
            if (curr.high > current_price) {
                resistance.emplace_back(curr.high, SRType::RESISTANCE, tf, static_cast<int>(i));
            }
        }

        // Swing low -> potential support (only if BELOW current price)
        if (curr.low < prev.low && curr.low < next.low) {
            if (curr.low < current_price) {
                support.emplace_back(curr.low, SRType::SUPPORT, tf, static_cast<int>(i));
            }
        }
    }
}

void SRCalculator::detect_swings_5candle(
    const std::vector<Candle>& candles,
    float current_price,
    SRTimeframe tf,
    std::vector<SRLevel>& resistance,
    std::vector<SRLevel>& support
) {
    // Use 3-candle pattern for intraday (less strict, more levels)
    if (candles.size() < 3) return;

    float avg_volume = calculate_avg_volume(candles);
    float min_volume = avg_volume * 0.2f;  // Lower threshold for intraday

    for (size_t i = 1; i < candles.size() - 1; i++) {
        const Candle& prev = candles[i - 1];
        const Candle& curr = candles[i];
        const Candle& next = candles[i + 1];

        // Volume filter (relaxed for intraday)
        bool has_significant_volume = (avg_volume <= 0) || (curr.volume >= min_volume);
        if (!has_significant_volume) continue;

        // 3-candle swing high: curr.high > immediate neighbors
        bool is_swing_high = curr.high > prev.high && curr.high > next.high;

        if (is_swing_high && curr.high > current_price) {
            resistance.emplace_back(curr.high, SRType::RESISTANCE, tf, static_cast<int>(i));
        }

        // 3-candle swing low: curr.low < immediate neighbors
        bool is_swing_low = curr.low < prev.low && curr.low < next.low;

        if (is_swing_low && curr.low < current_price) {
            support.emplace_back(curr.low, SRType::SUPPORT, tf, static_cast<int>(i));
        }
    }
}

void SRCalculator::calculate_level_strength(SRLevel& level, const std::vector<Candle>& candles) {
    const float TOLERANCE = level.price * 0.002f;  // 0.2% tolerance

    level.touch_count = 0;
    level.bounce_count = 0;

    for (const auto& c : candles) {
        // Check if candle touched the level (wick or body crossed it)
        if (c.low <= level.price + TOLERANCE && c.high >= level.price - TOLERANCE) {
            level.touch_count++;

            // Check if it bounced (closed back away from level)
            bool bounced = false;
            if (level.type == SRType::SUPPORT) {
                // For support: bounced if close > level (held above support)
                bounced = (c.close > level.price);
            } else {
                // For resistance: bounced if close < level (held below resistance)
                bounced = (c.close < level.price);
            }

            if (bounced) {
                level.bounce_count++;
            }
        }
    }

    // Strength = bounce_rate * log(touch_count + 1)
    // Higher touch count with good bounce rate = stronger level
    if (level.touch_count > 0) {
        float bounce_rate = static_cast<float>(level.bounce_count) / static_cast<float>(level.touch_count);
        level.strength = bounce_rate * std::log(static_cast<float>(level.touch_count) + 1.0f);
    } else {
        level.strength = 0.0f;
    }
}

std::vector<SRLevel> SRCalculator::filter_nearest(
    std::vector<SRLevel>& resistance,
    std::vector<SRLevel>& support,
    float /* current_price */,
    int max_per_side
) {
    std::vector<SRLevel> result;

    // Sort resistance by price ascending (closest to current price first)
    std::sort(resistance.begin(), resistance.end(),
              [](const SRLevel& a, const SRLevel& b) { return a.price < b.price; });

    // Sort support by price descending (closest to current price first)
    std::sort(support.begin(), support.end(),
              [](const SRLevel& a, const SRLevel& b) { return a.price > b.price; });

    // Take up to max_per_side closest resistance levels
    for (size_t i = 0; i < resistance.size() && static_cast<int>(i) < max_per_side; i++) {
        result.push_back(resistance[i]);
    }

    // Take up to max_per_side closest support levels
    for (size_t i = 0; i < support.size() && static_cast<int>(i) < max_per_side; i++) {
        result.push_back(support[i]);
    }

    return result;
}

float SRCalculator::calculate_avg_volume(const std::vector<Candle>& candles) {
    float total = 0.0f;
    int count = 0;

    for (const auto& c : candles) {
        if (c.volume > 0) {
            total += c.volume;
            count++;
        }
    }

    return (count > 0) ? (total / static_cast<float>(count)) : 0.0f;
}
