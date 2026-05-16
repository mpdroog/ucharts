// signal_detector.cpp - Detect 5-min resistance breakouts on 1-min chart
#include "signal_detector.h"
#include <algorithm>
#include <cstring>
#include <cmath>

SignalDetector::SignalDetector()
    : m_min_rr(2.0f)
    , m_min_volume_ratio(1.2f)   // 20% above average volume
{}

EntrySignal SignalDetector::detect_breakout(
    const std::vector<Candle>& candles_1m,
    const std::vector<SRLevel>& sr_levels,
    int candle_idx
) {
    EntrySignal signal;

    if (candle_idx < 2 || candle_idx >= static_cast<int>(candles_1m.size())) {
        return signal;
    }

    const Candle& current = candles_1m[static_cast<size_t>(candle_idx)];
    const Candle& prev = candles_1m[static_cast<size_t>(candle_idx - 1)];

    // Check for breakout: previous close below resistance, current close above
    // Look for resistance levels that were broken
    float tolerance = 0.005f;  // 0.5% tolerance for "near" the level

    std::vector<float> broken_levels;
    for (const auto& level : sr_levels) {
        if (level.type != SRType::RESISTANCE) continue;

        float res = level.price;
        // Previous candle was below resistance (or just touching it)
        // Current candle closed above it
        if (prev.close <= res * (1.0f + tolerance) && current.close > res) {
            broken_levels.push_back(res);
        }
    }

    if (broken_levels.empty()) {
        return signal;  // No breakout detected
    }

    // Use the highest broken level (most significant breakout)
    float broken_level = *std::max_element(broken_levels.begin(), broken_levels.end());

    // Check volume - current candle should have above-average volume
    float vol_ratio = calc_volume_ratio(candles_1m, candle_idx, 20);
    if (vol_ratio < m_min_volume_ratio) {
        return signal;  // Not enough volume to confirm breakout
    }

    // Current candle should be bullish
    if (!is_bullish(current)) {
        return signal;  // Bearish candle breaking resistance is suspicious
    }

    // Calculate stop loss: just below the broken level
    float stop = broken_level * 0.995f;  // 0.5% below the level

    // Find target: next resistance above current price
    float target = find_next_resistance(sr_levels, current.close);
    if (target <= 0 || target <= current.close) {
        // No higher resistance found, use a default target (e.g., 5% above)
        target = current.close * 1.05f;
    }

    // Calculate R:R
    float risk = current.close - stop;
    float reward = target - current.close;

    if (risk <= 0) {
        return signal;  // Invalid risk
    }

    float rr = reward / risk;

    // Build signal
    signal.type = SignalType::BREAKOUT;
    signal.direction = SignalDirection::LONG;
    signal.entry_price = current.close;
    signal.stop_loss = stop;
    signal.target = target;
    signal.risk_reward = rr;
    signal.candle_idx = candle_idx;
    signal.broken_level = broken_level;
    signal.volume_ratio = vol_ratio;

    copy_timestamp(signal.timestamp, current.timestamp, sizeof(signal.timestamp));

    // Validate R:R
    signal.is_valid = (rr >= m_min_rr);

    return signal;
}

std::vector<float> SignalDetector::find_resistance_levels_near(
    const std::vector<SRLevel>& levels, float price, float tolerance_pct
) {
    std::vector<float> result;
    float tolerance = price * tolerance_pct;

    for (const auto& level : levels) {
        if (level.type == SRType::RESISTANCE) {
            float distance = std::fabs(level.price - price);
            if (distance <= tolerance) {
                result.push_back(level.price);
            }
        }
    }

    return result;
}

float SignalDetector::find_next_resistance(const std::vector<SRLevel>& levels, float price) {
    float nearest = 0;
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

float SignalDetector::calc_volume_ratio(const std::vector<Candle>& candles, int candle_idx, int avg_period) {
    if (candle_idx < avg_period) {
        return 0;
    }

    // Calculate average volume over avg_period (excluding current candle)
    float avg_vol = 0;
    int start = candle_idx - avg_period;
    if (start < 0) start = 0;

    for (int i = start; i < candle_idx; i++) {
        avg_vol += candles[static_cast<size_t>(i)].volume;
    }
    int count = candle_idx - start;
    if (count > 0) {
        avg_vol /= static_cast<float>(count);
    }

    if (avg_vol <= 0) {
        return 0;
    }

    // Current candle's volume vs average
    return candles[static_cast<size_t>(candle_idx)].volume / avg_vol;
}

void SignalDetector::copy_timestamp(char* dest, const char* src, size_t dest_size) {
    if (dest_size > 0) {
        std::strncpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
    }
}
