// signal_detector.h - Detect 5-min resistance breakouts on 1-min chart
#ifndef SIGNAL_DETECTOR_H
#define SIGNAL_DETECTOR_H

#include "types.h"
#include "sr_calculator.h"
#include <vector>
#include <cmath>

// ============================================================================
// Signal Types
// ============================================================================

enum class SignalType {
    NONE = 0,
    BREAKOUT = 1    // 1-min candle breaking through 5-min resistance
};

enum class SignalDirection {
    LONG = 1,
    SHORT = -1
};

// ============================================================================
// Entry Signal Structure
// ============================================================================

struct EntrySignal {
    SignalType type;
    SignalDirection direction;
    float entry_price;
    float stop_loss;        // Below the broken level
    float target;           // Next resistance level
    float risk_reward;      // Calculated R:R ratio
    int candle_idx;         // Which candle triggered the signal
    char timestamp[32];
    bool is_valid;          // R:R >= minimum threshold

    // Breakout context
    float broken_level;     // The resistance level that was broken
    float volume_ratio;     // Current candle volume vs average

    EntrySignal() : type(SignalType::NONE), direction(SignalDirection::LONG),
                    entry_price(0), stop_loss(0), target(0), risk_reward(0),
                    candle_idx(-1), is_valid(false),
                    broken_level(0), volume_ratio(0) {
        timestamp[0] = '\0';
    }

    bool is_long() const { return direction == SignalDirection::LONG; }
    bool is_short() const { return direction == SignalDirection::SHORT; }

    const char* type_name() const {
        switch (type) {
            case SignalType::NONE: return "NONE";
            case SignalType::BREAKOUT: return "BREAKOUT";
	    default:
	        LOG_W("signal", "type_name: unhandled SignalType %d", static_cast<int>(type));
	        break;
        }
        return "NONE";
    }

    const char* direction_name() const {
        return is_long() ? "LONG" : "SHORT";
    }
};

// ============================================================================
// Signal Detector Class
// ============================================================================

class SignalDetector {
public:
    SignalDetector();

    // Set minimum R:R ratio for valid signals (default 2.0)
    void set_min_rr(float min_rr) { m_min_rr = min_rr; }
    float get_min_rr() const { return m_min_rr; }

    // Set minimum volume ratio (default 1.2 = 20% above average)
    void set_min_volume_ratio(float ratio) { m_min_volume_ratio = ratio; }

    // Detect breakout at a specific candle
    // sr_levels should be calculated based on PREVIOUS candle's price
    // so resistance levels are still above the breakout point
    EntrySignal detect_breakout(
        const std::vector<Candle>& candles_1m,
        const std::vector<SRLevel>& sr_levels,
        int candle_idx
    );

private:
    float m_min_rr;           // Minimum R:R ratio (default 2.0)
    float m_min_volume_ratio; // Minimum volume vs average (default 1.2)

    // Helper: find resistance levels near a price (within tolerance)
    std::vector<float> find_resistance_levels_near(const std::vector<SRLevel>& levels, float price, float tolerance_pct);

    // Helper: find next resistance above price (for target)
    float find_next_resistance(const std::vector<SRLevel>& levels, float price);

    // Helper: calculate volume ratio vs average
    float calc_volume_ratio(const std::vector<Candle>& candles, int candle_idx, int avg_period);

    // Helper: check if candle is bullish
    static bool is_bullish(const Candle& c) { return c.close > c.open; }

    // Helper: copy timestamp
    static void copy_timestamp(char* dest, const char* src, size_t dest_size);
};

#endif // SIGNAL_DETECTOR_H
