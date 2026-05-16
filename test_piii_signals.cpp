// Test signal detection with fixed PIII data
// Uses JSON test data from testdata/piii_*.json
#include "sr_calculator.h"
#include "signal_detector.h"
#include "types.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <sstream>
#include <string>

// Simple JSON array parser for candle data
static bool parse_candles_json(const char* filename, std::vector<Candle>& candles) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        printf("ERROR: Cannot open %s\n", filename);
        return false;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Parse JSON array of candles
    // Format: [{"Close":"8.15","Datetime":"2026-05-15 10:23:00","High":"8.27",...}, ...]
    const char* p = content.c_str();

    while (*p) {
        // Find next object
        const char* obj_start = std::strchr(p, '{');
        if (!obj_start) break;

        const char* obj_end = std::strchr(obj_start, '}');
        if (!obj_end) break;

        Candle c;
        c.open = c.high = c.low = c.close = c.volume = 0;
        c.timestamp[0] = '\0';

        // Extract fields (simple parsing)
        std::string obj(obj_start, static_cast<size_t>(obj_end - obj_start + 1));

        auto extract = [&obj](const char* key) -> std::string {
            std::string search = std::string("\"") + key + "\":";
            size_t pos = obj.find(search);
            if (pos == std::string::npos) return "";
            pos += search.length();
            while (pos < obj.length() && (obj[pos] == ' ' || obj[pos] == '"')) pos++;
            size_t end = pos;
            while (end < obj.length() && obj[end] != '"' && obj[end] != ',' && obj[end] != '}') end++;
            return obj.substr(pos, end - pos);
        };

        std::string ts = extract("Datetime");
        if (ts.empty()) {
            // Daily format uses different field name
            ts = extract("Date");
        }
        std::strncpy(c.timestamp, ts.c_str(), sizeof(c.timestamp) - 1);
        c.timestamp[sizeof(c.timestamp) - 1] = '\0';

        c.open = std::stof(extract("Open"));
        c.high = std::stof(extract("High"));
        c.low = std::stof(extract("Low"));
        c.close = std::stof(extract("Close"));
        c.volume = std::stof(extract("Volume"));

        candles.push_back(c);
        p = obj_end + 1;
    }

    // Data comes in reverse chronological order, so reverse it
    std::reverse(candles.begin(), candles.end());

    return !candles.empty();
}

static void run_signal_test(const char* symbol, const char* file_1m, const char* file_5m, const char* file_daily) {
    printf("=== %s Signal Detection Test ===\n\n", symbol);

    // Load test data
    std::vector<Candle> candles_1m, candles_5m, candles_daily;

    printf("Loading test data...\n");
    if (!parse_candles_json(file_1m, candles_1m)) {
        printf("ERROR: Failed to load 1m data from %s\n", file_1m);
        return;
    }
    if (!parse_candles_json(file_5m, candles_5m)) {
        printf("WARNING: Failed to load 5m data\n");
    }
    if (!parse_candles_json(file_daily, candles_daily)) {
        printf("WARNING: Failed to load daily data\n");
    }

    printf("  1-min candles: %zu\n", candles_1m.size());
    printf("  5-min candles: %zu\n", candles_5m.size());
    printf("  Daily candles: %zu\n", candles_daily.size());

    if (candles_1m.empty()) {
        printf("\nNo candles to test.\n");
        return;
    }

    // Show price range
    float min_price = 9999, max_price = 0;
    for (const auto& c : candles_1m) {
        if (c.low < min_price) min_price = c.low;
        if (c.high > max_price) max_price = c.high;
    }
    printf("\nPrice range: $%.2f - $%.2f\n",
           static_cast<double>(min_price), static_cast<double>(max_price));

    // Show last few candles
    printf("\nLast 5 1-min candles:\n");
    for (size_t i = candles_1m.size() > 5 ? candles_1m.size() - 5 : 0; i < candles_1m.size(); ++i) {
        const Candle& c = candles_1m[i];
        printf("  %s: O=%.2f H=%.2f L=%.2f C=%.2f V=%.0f\n",
               c.timestamp, static_cast<double>(c.open), static_cast<double>(c.high),
               static_cast<double>(c.low), static_cast<double>(c.close), static_cast<double>(c.volume));
    }

    // Calculate S/R levels
    printf("\n--- S/R LEVELS ---\n");
    SRCalculator sr_calc;
    float current_price = candles_1m.back().close;
    std::vector<SRLevel> sr_levels = sr_calc.calculate_levels(candles_daily, candles_5m, current_price, 5);

    printf("Current price: $%.2f\n", static_cast<double>(current_price));
    printf("Found %zu S/R levels:\n", sr_levels.size());
    for (const SRLevel& level : sr_levels) {
        const char* type = level.is_resistance() ? "RES" : "SUP";
        const char* tf = level.is_daily() ? "daily" : "5min";
        printf("  %s $%.2f (%s, strength=%.2f, touches=%d)\n",
               type, static_cast<double>(level.price), tf,
               static_cast<double>(level.strength), level.touch_count);
    }

    // Run signal detection - looking for 5-min resistance breakouts on 1-min
    printf("\n--- BREAKOUT DETECTION (R:R >= 2.0) ---\n");
    printf("(Detecting 1-min candles breaking through 5-min resistance with volume)\n\n");
    SignalDetector detector;
    detector.set_min_rr(2.0f);

    int signals_found = 0;
    int start_idx = 20;  // Need volume history

    for (int i = start_idx; i < static_cast<int>(candles_1m.size()) - 1; ++i) {
        // Calculate S/R at PREVIOUS candle's price (so resistance is still above)
        float prev_price = candles_1m[static_cast<size_t>(i - 1)].close;
        std::vector<SRLevel> sr_at_prev = sr_calc.calculate_levels(
            candles_daily, candles_5m, prev_price, 10);

        // Detect breakout at this candle
        EntrySignal signal = detector.detect_breakout(candles_1m, sr_at_prev, i);

        if (signal.is_valid) {
            signals_found++;

            printf("BREAKOUT #%d at candle %d (%s):\n", signals_found, i, candles_1m[static_cast<size_t>(i)].timestamp);
            printf("  Broken:  $%.2f (resistance)\n", static_cast<double>(signal.broken_level));
            printf("  Entry:   $%.2f\n", static_cast<double>(signal.entry_price));
            printf("  Stop:    $%.2f\n", static_cast<double>(signal.stop_loss));
            printf("  Target:  $%.2f\n", static_cast<double>(signal.target));
            printf("  R:R:     %.1f\n", static_cast<double>(signal.risk_reward));
            printf("  Volume:  %.1fx average\n", static_cast<double>(signal.volume_ratio));
            printf("\n");
        }
    }

    printf("--- SUMMARY ---\n");
    printf("Total breakouts found: %d\n", signals_found);

    if (signals_found == 0) {
        printf("\nNo valid breakouts found.\n");

        // Diagnostics
        printf("\nDiagnostics:\n");
        float nearest_sup = 0, nearest_res = 0;
        for (const SRLevel& level : sr_levels) {
            if (level.is_support() && level.price < current_price) {
                if (nearest_sup == 0 || level.price > nearest_sup) nearest_sup = level.price;
            }
            if (level.is_resistance() && level.price > current_price) {
                if (nearest_res == 0 || level.price < nearest_res) nearest_res = level.price;
            }
        }
        if (nearest_sup > 0) {
            printf("  - Nearest support: $%.2f (%.1f%% below)\n",
                   static_cast<double>(nearest_sup),
                   static_cast<double>((current_price - nearest_sup) / current_price * 100));
        }
        if (nearest_res > 0) {
            printf("  - Nearest resistance: $%.2f (%.1f%% above)\n",
                   static_cast<double>(nearest_res),
                   static_cast<double>((nearest_res - current_price) / current_price * 100));
        }
    }

    printf("\n");
}

int main() {
    // Test PIII (should find breakouts)
    run_signal_test("PIII",
                    "testdata/piii_1m.json",
                    "testdata/piii_5m.json",
                    "testdata/piii_daily.json");

    // Test AUUD (crashed stock - should find fewer/no signals with tight thresholds)
    run_signal_test("AUUD",
                    "testdata/auud_1m.json",
                    "testdata/auud_5m.json",
                    "testdata/auud_daily.json");

    return 0;
}
