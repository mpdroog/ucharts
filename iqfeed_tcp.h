// iqfeed_tcp.h - TCP client for IQFeed protocol
#ifndef IQFEED_TCP_H
#define IQFEED_TCP_H

#include "types.h"
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

// Default ports for IQFeed
static const int IQFEED_LOOKUP_PORT = 9100;   // Historical data
static const int IQFEED_LEVEL1_PORT = 5009;   // L1 quotes
static const int IQFEED_LEVEL2_PORT = 9200;   // L2 order book

// ============================================================================
// Historical Data Client (Port 9100) - Async only
// ============================================================================

// Async fetch request types
enum LookupRequestType {
    LOOKUP_REQ_DAILY,
    LOOKUP_REQ_INTERVAL
};

// Async fetch result
struct LookupResult {
    bool success;
    char symbol[32];
    LookupRequestType type;
    int interval_secs;
    std::vector<Candle> candles;
    char error[256];
    void* user_data;
};

// Callback for async results
using LookupCallback = std::function<void(const LookupResult&)>;

class IQFeedLookup {
public:
    IQFeedLookup();
    ~IQFeedLookup();

    // Non-copyable (owns thread, socket, mutex)
    IQFeedLookup(const IQFeedLookup&) = delete;
    IQFeedLookup& operator=(const IQFeedLookup&) = delete;

    // Connect to lookup port and start worker thread
    bool connect(const char* host = "127.0.0.1", int port = IQFEED_LOOKUP_PORT);
    void disconnect();
    bool is_connected() const;

    // Async fetch - queues request and returns immediately
    // Results delivered via callback on background thread
    void fetch_daily(const char* symbol, int datapoints, void* user_data = nullptr);
    void fetch_interval(const char* symbol, int interval_secs, int datapoints, void* user_data = nullptr);

    // Set callback for async results (call before fetch operations)
    void set_callback(LookupCallback callback);

    // Check if async requests are pending
    bool has_pending_requests() const;

    // Get last error
    const char* last_error() const;

private:
    // Async request
    struct Request {
        LookupRequestType type;
        char symbol[32];
        int datapoints;
        int interval_secs;
        void* user_data;
    };

    int m_socket;
    char m_error[256];
    char m_host[64];
    int m_port;

    // Async support
    std::atomic<bool> m_running;
    std::thread m_thread;
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    std::vector<Request> m_requests;
    LookupCallback m_callback;

    bool send_command(const char* cmd);
    bool read_until_endmsg(std::string& response, int expected_lines = 0);
    bool parse_historical_response(const std::string& response, std::vector<Candle>& candles);
    bool set_protocol();
    bool ensure_connected();

    // Background thread
    void worker_thread();
    void process_request(const Request& req);
};

// ============================================================================
// Level 1 Client (Port 5009) - Streaming Quotes
// ============================================================================

// L1 quote data
struct L1Quote {
    char symbol[16];
    float bid;
    float ask;
    float last;
    int bid_size;
    int ask_size;
    int last_size;
    float high;
    float low;
    float open;
    float close;
    int64_t volume;
    char last_time[16];

    L1Quote() : bid(0), ask(0), last(0), bid_size(0), ask_size(0), last_size(0),
                high(0), low(0), open(0), close(0), volume(0) {
        symbol[0] = '\0';
        last_time[0] = '\0';
    }
};

// Callback for L1 updates
using L1Callback = std::function<void(const L1Quote&)>;

class IQFeedLevel1 {
public:
    IQFeedLevel1();
    ~IQFeedLevel1();

    // Non-copyable (owns thread, socket, mutex)
    IQFeedLevel1(const IQFeedLevel1&) = delete;
    IQFeedLevel1& operator=(const IQFeedLevel1&) = delete;

    // Connect and start streaming thread
    bool connect(const char* host = "127.0.0.1", int port = IQFEED_LEVEL1_PORT);
    void disconnect();
    bool is_connected() const;

    // Watch/unwatch symbols
    bool watch(const char* symbol);
    bool unwatch(const char* symbol);

    // Set callback for quote updates
    void set_callback(L1Callback callback);

    // Get current quote (thread-safe)
    bool get_quote(const char* symbol, L1Quote& quote);

    const char* last_error() const;

private:
    int m_socket;
    std::atomic<bool> m_running;
    std::thread m_thread;
    std::mutex m_mutex;
    std::map<std::string, L1Quote> m_quotes;
    L1Callback m_callback;
    char m_error[256];

    void stream_thread();
    bool send_command(const char* cmd);
    void parse_l1_message(const std::string& line);
    void parse_summary_message(const char* data);
    void parse_update_message(const char* data);
    bool set_protocol();
};

// ============================================================================
// Level 2 Client (Port 9200) - Order Book
// ============================================================================

// L2 price level
struct L2Level {
    float price;
    int size;
    int order_count;
    bool is_bid;

    L2Level() : price(0), size(0), order_count(0), is_bid(true) {}
};

// Callback for L2 updates
using L2Callback = std::function<void(const char* symbol, const std::vector<L2Level>& bids, const std::vector<L2Level>& asks)>;

class IQFeedLevel2 {
public:
    IQFeedLevel2();
    ~IQFeedLevel2();

    // Non-copyable (owns thread, socket, mutex)
    IQFeedLevel2(const IQFeedLevel2&) = delete;
    IQFeedLevel2& operator=(const IQFeedLevel2&) = delete;

    // Connect and start streaming thread
    bool connect(const char* host = "127.0.0.1", int port = IQFEED_LEVEL2_PORT);
    void disconnect();
    bool is_connected() const;

    // Watch/unwatch symbols (max_levels typically 10)
    bool watch(const char* symbol, int max_levels = 10);
    bool unwatch(const char* symbol);

    // Set callback for book updates
    void set_callback(L2Callback callback);

    // Get current book (thread-safe)
    bool get_book(const char* symbol, std::vector<L2Level>& bids, std::vector<L2Level>& asks);

    const char* last_error() const;

private:
    struct BookData {
        std::vector<L2Level> bids;
        std::vector<L2Level> asks;
    };

    int m_socket;
    std::atomic<bool> m_running;
    std::thread m_thread;
    std::mutex m_mutex;
    std::map<std::string, BookData> m_books;
    L2Callback m_callback;
    char m_error[256];

    void stream_thread();
    bool send_command(const char* cmd);
    void parse_l2_message(const std::string& line);
    void parse_level_summary(const char* data);
    void parse_level_update(const char* data);
    void parse_level_delete(const char* data);
    bool set_protocol();
};

// ============================================================================
// Global instances
// ============================================================================

IQFeedLookup& get_iqfeed_lookup();
IQFeedLevel1& get_iqfeed_level1();
IQFeedLevel2& get_iqfeed_level2();

#endif // IQFEED_TCP_H
