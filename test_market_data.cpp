// test_market_data.cpp - Unit tests for market data module
// Compile: clang++ -std=c++11 -o test_market_data test_market_data.cpp market_data.cpp

#include "market_data.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <sys/stat.h>
#include <unistd.h>

// ============================================================================
// Test helpers
// ============================================================================

#include "test_common.h"
static const char* TEST_DATA_DIR = "test_data";

// Create test data directory and files
static void create_test_data() {
    mkdir(TEST_DATA_DIR, 0755);

    // Create level2 test file
    {
        char filepath[256];
        std::snprintf(filepath, sizeof(filepath), "%s/level2_TEST.csv", TEST_DATA_DIR);
        FILE* f = std::fopen(filepath, "w");
        if (f) {
            std::fprintf(f, "timestamp,symbol,side,exchange,price,size\n");
            std::fprintf(f, "09:30:00.000,TEST,BID,NYSE,100.00,1000\n");
            std::fprintf(f, "09:30:00.000,TEST,BID,ARCA,99.95,500\n");
            std::fprintf(f, "09:30:00.000,TEST,BID,BATS,99.90,800\n");
            std::fprintf(f, "09:30:00.000,TEST,ASK,NYSE,100.05,600\n");
            std::fprintf(f, "09:30:00.000,TEST,ASK,ARCA,100.10,400\n");
            std::fprintf(f, "09:30:00.000,TEST,ASK,BATS,100.15,700\n");
            std::fclose(f);
        }
    }

    // Create timesales test file
    {
        char filepath[256];
        std::snprintf(filepath, sizeof(filepath), "%s/timesales_TEST.csv", TEST_DATA_DIR);
        FILE* f = std::fopen(filepath, "w");
        if (f) {
            std::fprintf(f, "timestamp,symbol,price,size,direction\n");
            std::fprintf(f, "09:30:00.100,TEST,100.00,100,SAME\n");
            std::fprintf(f, "09:30:00.200,TEST,100.05,200,UP\n");
            std::fprintf(f, "09:30:00.300,TEST,100.02,150,DOWN\n");
            std::fprintf(f, "09:30:00.400,TEST,100.02,100,SAME\n");
            std::fprintf(f, "09:30:00.500,TEST,100.08,250,UP\n");
            std::fclose(f);
        }
    }

    // Create daily candles test file
    {
        char filepath[256];
        std::snprintf(filepath, sizeof(filepath), "%s/candles_TEST_daily.csv", TEST_DATA_DIR);
        FILE* f = std::fopen(filepath, "w");
        if (f) {
            std::fprintf(f, "timestamp,open,high,low,close,volume\n");
            std::fprintf(f, "2024-01-02,100.00,102.50,99.50,101.75,15000\n");
            std::fprintf(f, "2024-01-03,101.75,103.00,100.50,102.25,12500\n");
            std::fprintf(f, "2024-01-04,102.25,104.00,101.00,103.50,18000\n");
            std::fprintf(f, "2024-01-05,103.50,105.25,102.75,104.00,14200\n");
            std::fprintf(f, "2024-01-08,104.00,104.50,101.00,101.50,22000\n");
            std::fclose(f);
        }
    }

    // Create 1m candles test file
    {
        char filepath[256];
        std::snprintf(filepath, sizeof(filepath), "%s/candles_TEST_1m.csv", TEST_DATA_DIR);
        FILE* f = std::fopen(filepath, "w");
        if (f) {
            std::fprintf(f, "timestamp,open,high,low,close,volume\n");
            std::fprintf(f, "09:30,100.00,100.10,99.95,100.05,1000\n");
            std::fprintf(f, "09:31,100.05,100.20,100.00,100.15,1200\n");
            std::fprintf(f, "09:32,100.15,100.25,100.10,100.20,800\n");
            std::fclose(f);
        }
    }

    // Create 5m candles test file
    {
        char filepath[256];
        std::snprintf(filepath, sizeof(filepath), "%s/candles_TEST_5m.csv", TEST_DATA_DIR);
        FILE* f = std::fopen(filepath, "w");
        if (f) {
            std::fprintf(f, "timestamp,open,high,low,close,volume\n");
            std::fprintf(f, "09:30,100.00,100.30,99.90,100.20,5000\n");
            std::fprintf(f, "09:35,100.20,100.50,100.15,100.40,6000\n");
            std::fclose(f);
        }
    }
}

static void cleanup_test_data() {
    // Remove test files
    char filepath[256];
    std::snprintf(filepath, sizeof(filepath), "%s/level2_TEST.csv", TEST_DATA_DIR);
    remove(filepath);
    std::snprintf(filepath, sizeof(filepath), "%s/timesales_TEST.csv", TEST_DATA_DIR);
    remove(filepath);
    std::snprintf(filepath, sizeof(filepath), "%s/candles_TEST_daily.csv", TEST_DATA_DIR);
    remove(filepath);
    std::snprintf(filepath, sizeof(filepath), "%s/candles_TEST_1m.csv", TEST_DATA_DIR);
    remove(filepath);
    std::snprintf(filepath, sizeof(filepath), "%s/candles_TEST_5m.csv", TEST_DATA_DIR);
    remove(filepath);
    rmdir(TEST_DATA_DIR);
}

// ============================================================================
// Tests
// ============================================================================

TEST(has_symbol_empty) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);

    ASSERT_FALSE(md.has_symbol(nullptr));
    ASSERT_FALSE(md.has_symbol(""));
    ASSERT_FALSE(md.has_symbol("NONEXISTENT"));
}

TEST(has_symbol_exists) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);

    ASSERT_TRUE(md.has_symbol("TEST"));
}

TEST(load_symbol_nonexistent) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);

    ASSERT_FALSE(md.load_symbol("NONEXISTENT"));
}

TEST(load_symbol_success) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);

    ASSERT_TRUE(md.load_symbol("TEST"));
}

TEST(get_level2) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<Level2Entry> bids, asks;
    float best_bid, best_ask;

    ASSERT_TRUE(md.get_level2("TEST", bids, asks, best_bid, best_ask));

    ASSERT_EQ(bids.size(), 3u);
    ASSERT_EQ(asks.size(), 3u);

    // Bids should be sorted descending (highest first)
    ASSERT_FLOAT_EQ(bids[0].price, 100.00f, 0.01f);
    ASSERT_FLOAT_EQ(bids[1].price, 99.95f, 0.01f);
    ASSERT_FLOAT_EQ(bids[2].price, 99.90f, 0.01f);

    // Asks should be sorted ascending (lowest first)
    ASSERT_FLOAT_EQ(asks[0].price, 100.05f, 0.01f);
    ASSERT_FLOAT_EQ(asks[1].price, 100.10f, 0.01f);
    ASSERT_FLOAT_EQ(asks[2].price, 100.15f, 0.01f);

    ASSERT_FLOAT_EQ(best_bid, 100.00f, 0.01f);
    ASSERT_FLOAT_EQ(best_ask, 100.05f, 0.01f);
}

TEST(get_level2_exchange_names) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<Level2Entry> bids, asks;
    float best_bid, best_ask;

    ASSERT_TRUE(md.get_level2("TEST", bids, asks, best_bid, best_ask));

    ASSERT_STREQ(bids[0].exchange, "NYSE");
    ASSERT_STREQ(bids[1].exchange, "ARCA");
    ASSERT_STREQ(bids[2].exchange, "BATS");
}

TEST(get_level2_sizes) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<Level2Entry> bids, asks;
    float best_bid, best_ask;

    ASSERT_TRUE(md.get_level2("TEST", bids, asks, best_bid, best_ask));

    ASSERT_EQ(bids[0].size, 1000);
    ASSERT_EQ(bids[1].size, 500);
    ASSERT_EQ(bids[2].size, 800);
}

TEST(get_level2_colors) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<Level2Entry> bids, asks;
    float best_bid, best_ask;

    ASSERT_TRUE(md.get_level2("TEST", bids, asks, best_bid, best_ask));

    // Each level should have a different color
    ASSERT_TRUE(bids[0].color != 0);
    ASSERT_TRUE(asks[0].color != 0);
    ASSERT_TRUE(bids[0].color != asks[0].color);
}

TEST(get_time_sales) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<TimeSalesEntry> entries;
    ASSERT_TRUE(md.get_time_sales("TEST", entries, 15));

    ASSERT_EQ(entries.size(), 5u);
}

TEST(time_sales_direction) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<TimeSalesEntry> entries;
    ASSERT_TRUE(md.get_time_sales("TEST", entries, 15));

    ASSERT_EQ(entries[0].direction, TradeDirection::SAME);
    ASSERT_EQ(entries[1].direction, TradeDirection::UP);
    ASSERT_EQ(entries[2].direction, TradeDirection::DOWN);
    ASSERT_EQ(entries[3].direction, TradeDirection::SAME);
    ASSERT_EQ(entries[4].direction, TradeDirection::UP);
}

TEST(time_sales_prices) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<TimeSalesEntry> entries;
    ASSERT_TRUE(md.get_time_sales("TEST", entries, 15));

    ASSERT_FLOAT_EQ(entries[0].price, 100.00f, 0.01f);
    ASSERT_FLOAT_EQ(entries[1].price, 100.05f, 0.01f);
    ASSERT_FLOAT_EQ(entries[4].price, 100.08f, 0.01f);
}

TEST(time_sales_timestamps) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<TimeSalesEntry> entries;
    ASSERT_TRUE(md.get_time_sales("TEST", entries, 15));

    ASSERT_STREQ(entries[0].timestamp, "09:30:00.100");
    ASSERT_STREQ(entries[4].timestamp, "09:30:00.500");
}

TEST(get_candles_daily) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<Candle> candles;
    ASSERT_TRUE(md.get_candles("TEST", Timeframe::DAILY, candles, 200));

    ASSERT_EQ(candles.size(), 5u);

    ASSERT_FLOAT_EQ(candles[0].open, 100.00f, 0.01f);
    ASSERT_FLOAT_EQ(candles[0].close, 101.75f, 0.01f);
    ASSERT_FLOAT_EQ(candles[4].close, 101.50f, 0.01f);
}

TEST(get_candles_1m) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<Candle> candles;
    ASSERT_TRUE(md.get_candles("TEST", Timeframe::M1, candles, 200));

    ASSERT_EQ(candles.size(), 3u);
}

TEST(get_candles_5m) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<Candle> candles;
    ASSERT_TRUE(md.get_candles("TEST", Timeframe::M5, candles, 200));

    ASSERT_EQ(candles.size(), 2u);
}

TEST(get_candles_limit) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<Candle> candles;
    ASSERT_TRUE(md.get_candles("TEST", Timeframe::DAILY, candles, 3));

    ASSERT_EQ(candles.size(), 3u);
    // Should get the last 3 candles
    ASSERT_STREQ(candles[2].timestamp, "2024-01-08");
}

TEST(get_current_price) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    float price = md.get_current_price("TEST");
    ASSERT_TRUE(price > 0);
}

TEST(unload_symbol) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);
    ASSERT_TRUE(md.load_symbol("TEST"));

    std::vector<Candle> candles;
    ASSERT_TRUE(md.get_candles("TEST", Timeframe::DAILY, candles, 200));
    ASSERT_TRUE(candles.size() > 0);

    md.unload_symbol("TEST");

    candles.clear();
    ASSERT_FALSE(md.get_candles("TEST", Timeframe::DAILY, candles, 200));
}

TEST(simulation_control) {
    MarketData md;
    ASSERT_FALSE(md.is_running());

    md.start_simulation();
    ASSERT_TRUE(md.is_running());

    md.stop_simulation();
    ASSERT_FALSE(md.is_running());
}

TEST(empty_symbol_returns_false) {
    MarketData md;
    md.set_data_source(DataSourceMode::FILE);
    md.set_data_dir(TEST_DATA_DIR);

    std::vector<Level2Entry> bids, asks;
    float best_bid, best_ask;
    ASSERT_FALSE(md.get_level2("", bids, asks, best_bid, best_ask));
    ASSERT_FALSE(md.get_level2(nullptr, bids, asks, best_bid, best_ask));

    std::vector<TimeSalesEntry> ts;
    ASSERT_FALSE(md.get_time_sales("", ts));

    std::vector<Candle> candles;
    ASSERT_FALSE(md.get_candles("", Timeframe::DAILY, candles));
}

// ============================================================================
// IQFeed TCP format tests
// ============================================================================

// Test helper: parse IQFeed historical response format
static bool test_parse_iqfeed_response(const char* response, std::vector<Candle>& candles) {
    candles.clear();
    const char* p = response;
    const char* end = p + std::strlen(response);

    while (p < end) {
        const char* line_end = p;
        while (line_end < end && *line_end != '\n') line_end++;

        if (line_end > p) {
            char msg_id[8] = "";
            char ts[32] = "";
            float high = 0, low = 0, open = 0, close = 0;
            float volume = 0;

            int parsed = std::sscanf(p, "%7[^,],%31[^,],%f,%f,%f,%f,%f",
                                    msg_id, ts, &high, &low, &open, &close, &volume);

            if (parsed >= 6 && std::strcmp(msg_id, "LH") == 0) {
                Candle c;
                safe_strcpy(c.timestamp, ts, sizeof(c.timestamp));
                c.open = open;
                c.high = high;
                c.low = low;
                c.close = close;
                c.volume = volume;
                candles.push_back(c);
            }
        }
        p = line_end + 1;
    }
    return !candles.empty();
}

TEST(iqfeed_parse_daily_format) {
    // Real IQFeed format: LH,DATE,HIGH,LOW,OPEN,CLOSE,VOLUME,OPENINTEREST
    const char* response = "LH,2024-01-15,155.50,150.25,152.00,154.75,1000000,0,\n";

    std::vector<Candle> candles;
    ASSERT_TRUE(test_parse_iqfeed_response(response, candles));
    ASSERT_EQ(candles.size(), 1u);

    // Verify fields are mapped correctly
    ASSERT_STREQ(candles[0].timestamp, "2024-01-15");
    ASSERT_FLOAT_EQ(candles[0].high, 155.50f, 0.01f);   // HIGH is position 3
    ASSERT_FLOAT_EQ(candles[0].low, 150.25f, 0.01f);    // LOW is position 4
    ASSERT_FLOAT_EQ(candles[0].open, 152.00f, 0.01f);   // OPEN is position 5
    ASSERT_FLOAT_EQ(candles[0].close, 154.75f, 0.01f);  // CLOSE is position 6
    ASSERT_FLOAT_EQ(candles[0].volume, 1000000.0f, 1.0f);
}

TEST(iqfeed_parse_interval_format) {
    // Real IQFeed interval format: LH,DATETIME,HIGH,LOW,OPEN,CLOSE,TOTALVOL,PERIODVOL,TRADES
    const char* response = "LH,2024-05-10 09:30:00,184.76,184.50,184.55,184.70,32048,250,15,\n";

    std::vector<Candle> candles;
    ASSERT_TRUE(test_parse_iqfeed_response(response, candles));
    ASSERT_EQ(candles.size(), 1u);

    ASSERT_STREQ(candles[0].timestamp, "2024-05-10 09:30:00");
    ASSERT_FLOAT_EQ(candles[0].high, 184.76f, 0.01f);
    ASSERT_FLOAT_EQ(candles[0].low, 184.50f, 0.01f);
    ASSERT_FLOAT_EQ(candles[0].open, 184.55f, 0.01f);
    ASSERT_FLOAT_EQ(candles[0].close, 184.70f, 0.01f);
}

TEST(iqfeed_parse_multiple_candles) {
    const char* response =
        "LH,2024-01-15,155.50,150.25,152.00,154.75,1000000,0,\n"
        "LH,2024-01-16,156.00,153.00,154.75,155.25,900000,0,\n"
        "LH,2024-01-17,157.50,154.50,155.25,157.00,1100000,0,\n";

    std::vector<Candle> candles;
    ASSERT_TRUE(test_parse_iqfeed_response(response, candles));
    ASSERT_EQ(candles.size(), 3u);

    // First candle
    ASSERT_FLOAT_EQ(candles[0].open, 152.00f, 0.01f);
    ASSERT_FLOAT_EQ(candles[0].close, 154.75f, 0.01f);

    // Last candle
    ASSERT_FLOAT_EQ(candles[2].open, 155.25f, 0.01f);
    ASSERT_FLOAT_EQ(candles[2].close, 157.00f, 0.01f);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    create_test_data();

    RUN_TEST(has_symbol_empty);
    RUN_TEST(has_symbol_exists);
    RUN_TEST(load_symbol_nonexistent);
    RUN_TEST(load_symbol_success);
    RUN_TEST(get_level2);
    RUN_TEST(get_level2_exchange_names);
    RUN_TEST(get_level2_sizes);
    RUN_TEST(get_level2_colors);
    RUN_TEST(get_time_sales);
    RUN_TEST(time_sales_direction);
    RUN_TEST(time_sales_prices);
    RUN_TEST(time_sales_timestamps);
    RUN_TEST(get_candles_daily);
    RUN_TEST(get_candles_1m);
    RUN_TEST(get_candles_5m);
    RUN_TEST(get_candles_limit);
    RUN_TEST(get_current_price);
    RUN_TEST(unload_symbol);
    RUN_TEST(simulation_control);
    RUN_TEST(empty_symbol_returns_false);

    // IQFeed TCP format tests
    RUN_TEST(iqfeed_parse_daily_format);
    RUN_TEST(iqfeed_parse_interval_format);
    RUN_TEST(iqfeed_parse_multiple_candles);

    cleanup_test_data();
    test_summary();

    // Use _Exit to avoid static destruction order issues with global instances
    std::fflush(stdout);
    _Exit(0);
}
