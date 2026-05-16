// sr_calculator.h - Multi-timeframe Support/Resistance calculation
#ifndef SR_CALCULATOR_H
#define SR_CALCULATOR_H

#include "types.h"
#include <vector>
#include <cmath>

// ============================================================================
// S/R Level Types
// ============================================================================

// Source timeframe for S/R level
enum class SRTimeframe {
    DAILY = 0,
    M5 = 1
};

// Level type
enum class SRType {
    SUPPORT = 0,
    RESISTANCE = 1
};

// Extended S/R level with strength metrics
struct SRLevel {
    float price;
    SRType type;
    SRTimeframe source_tf;
    int touch_count;        // How many times price touched this level
    int bounce_count;       // How many times price bounced (respected the level)
    float strength;         // Calculated strength score (0.0 - 1.0+)
    int candle_idx;         // Index in source candles where identified

    SRLevel() : price(0), type(SRType::SUPPORT), source_tf(SRTimeframe::DAILY),
                touch_count(0), bounce_count(0), strength(0), candle_idx(0) {}

    SRLevel(float p, SRType t, SRTimeframe tf, int idx)
        : price(p), type(t), source_tf(tf),
          touch_count(0), bounce_count(0), strength(0), candle_idx(idx) {}

    bool is_resistance() const { return type == SRType::RESISTANCE; }
    bool is_support() const { return type == SRType::SUPPORT; }
    bool is_daily() const { return source_tf == SRTimeframe::DAILY; }
};

// ============================================================================
// S/R Calculator Class
// ============================================================================

class SRCalculator {
public:
    SRCalculator();

    // Calculate S/R levels from both daily and 5-min candles
    // Returns up to max_levels_per_side levels for each side (support/resistance)
    std::vector<SRLevel> calculate_levels(
        const std::vector<Candle>& daily_candles,
        const std::vector<Candle>& m5_candles,
        float current_price,
        int max_levels_per_side = 3
    );

    // Calculate S/R from daily candles only (for backward compatibility)
    std::vector<SRLevel> calculate_daily_levels(
        const std::vector<Candle>& daily_candles,
        float current_price,
        int max_levels_per_side = 3
    );

    // Calculate S/R from 5-min candles only
    std::vector<SRLevel> calculate_m5_levels(
        const std::vector<Candle>& m5_candles,
        float current_price,
        int max_levels_per_side = 3
    );

    // Find nearest support below price
    float find_nearest_support(const std::vector<SRLevel>& levels, float price) const;

    // Find nearest resistance above price
    float find_nearest_resistance(const std::vector<SRLevel>& levels, float price) const;

    // Check if a level is near an existing level (for deduplication)
    bool has_nearby_level(const std::vector<SRLevel>& levels, float price, float tolerance_pct = 0.005f) const;

private:
    // Detect swing highs/lows using 3-candle pattern (for daily)
    void detect_swings_3candle(
        const std::vector<Candle>& candles,
        float current_price,
        SRTimeframe tf,
        std::vector<SRLevel>& resistance,
        std::vector<SRLevel>& support
    );

    // Detect swing highs/lows using 5-candle pattern (for 5-min, more strict)
    void detect_swings_5candle(
        const std::vector<Candle>& candles,
        float current_price,
        SRTimeframe tf,
        std::vector<SRLevel>& resistance,
        std::vector<SRLevel>& support
    );

    // Calculate strength score for a level based on historical touches
    void calculate_level_strength(SRLevel& level, const std::vector<Candle>& candles);

    // Sort and filter levels by proximity to current price
    std::vector<SRLevel> filter_nearest(
        std::vector<SRLevel>& resistance,
        std::vector<SRLevel>& support,
        float current_price,
        int max_per_side
    );

    // Calculate average volume for filtering
    float calculate_avg_volume(const std::vector<Candle>& candles);
};

#endif // SR_CALCULATOR_H
