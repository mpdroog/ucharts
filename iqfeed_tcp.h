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
#include <chrono>

// ============================================================================
// Thread Safety Annotations (Clang)
// Enable with: -Wthread-safety
// ============================================================================
#if defined(__clang__)
#define CAPABILITY(x) __attribute__((capability(x)))
#define SCOPED_CAPABILITY __attribute__((scoped_lockable))
#define GUARDED_BY(x) __attribute__((guarded_by(x)))
#define PT_GUARDED_BY(x) __attribute__((pt_guarded_by(x)))
#define REQUIRES(...) __attribute__((requires_capability(__VA_ARGS__)))
#define REQUIRES_SHARED(...) __attribute__((requires_shared_capability(__VA_ARGS__)))
#define ACQUIRE(...) __attribute__((acquire_capability(__VA_ARGS__)))
#define RELEASE(...) __attribute__((release_capability(__VA_ARGS__)))
#define EXCLUDES(...) __attribute__((locks_excluded(__VA_ARGS__)))
#else
#define CAPABILITY(x)
#define SCOPED_CAPABILITY
#define GUARDED_BY(x)
#define PT_GUARDED_BY(x)
#define REQUIRES(...)
#define REQUIRES_SHARED(...)
#define ACQUIRE(...)
#define RELEASE(...)
#define EXCLUDES(...)
#endif

// Mutex wrapper with thread safety annotations
class CAPABILITY("mutex") Mutex {
public:
    void lock() ACQUIRE() { m_mutex.lock(); }
    void unlock() RELEASE() { m_mutex.unlock(); }
    bool try_lock() ACQUIRE() { return m_mutex.try_lock(); }
    std::mutex& native() { return m_mutex; }
private:
    std::mutex m_mutex;
};

// RAII lock guard with annotations
class SCOPED_CAPABILITY MutexLock {
public:
    explicit MutexLock(Mutex& m) ACQUIRE(m) : m_mutex(m) { m_mutex.lock(); }
    ~MutexLock() RELEASE() { m_mutex.unlock(); }
    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;
private:
    Mutex& m_mutex;
};

// Simple counting semaphore for thread signaling
// Uses its own internal mutex (not annotated), so it's outside the thread safety annotation system
class Semaphore {
public:
    explicit Semaphore(int initial = 0) : m_count(initial) {}

    void post() {
        std::unique_lock<std::mutex> lock(m_mutex);
        ++m_count;
        m_cond.notify_one();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this]() { return m_count > 0; });
        --m_count;
    }

    // Non-blocking wait with timeout for clean shutdown
    template<typename Rep, typename Period>
    bool wait_for(const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        if (!m_cond.wait_for(lock, timeout, [this]() { return m_count > 0; })) {
            return false;  // Timeout
        }
        --m_count;
        return true;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cond;
    int m_count;
};

// Default ports for IQFeed
static const int IQFEED_LOOKUP_PORT = 9100;   // Historical data
static const int IQFEED_LEVEL1_PORT = 5009;   // L1 quotes
static const int IQFEED_LEVEL2_PORT = 9200;   // L2 order book

// ============================================================================
// Historical Data Client (Port 9100) - Async only
// ============================================================================

// Async fetch request types
enum class LookupRequestType {
    DAILY,
    INTERVAL
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
    [[nodiscard]] bool connect(const char* host = "127.0.0.1", int port = IQFEED_LOOKUP_PORT);
    void disconnect() EXCLUDES(m_mutex);
    [[nodiscard]] bool is_connected() const;

    // Async fetch - queues request and returns immediately
    // Results delivered via callback on background thread
    void fetch_daily(const char* symbol, int datapoints, void* user_data = nullptr) EXCLUDES(m_mutex);
    void fetch_interval(const char* symbol, int interval_secs, int datapoints, void* user_data = nullptr) EXCLUDES(m_mutex);

    // Set callback for async results (call before fetch operations)
    void set_callback(LookupCallback callback) EXCLUDES(m_mutex);

    // Check if async requests are pending
    [[nodiscard]] bool has_pending_requests() const EXCLUDES(m_mutex);

    // Get last error
    [[nodiscard]] const char* last_error() const;

private:
    // Async request
    struct Request {
        LookupRequestType type;
        char symbol[32];
        int datapoints;
        int interval_secs;
        void* user_data;
    };

    std::atomic<int> m_socket;  // Atomic for thread-safe is_connected() checks
    char m_error[256];
    char m_host[64];
    int m_port;

    // Async support
    std::atomic<bool> m_running;
    std::thread m_thread;
    mutable Mutex m_mutex;
    Semaphore m_sem;
    std::vector<Request> m_requests GUARDED_BY(m_mutex);
    LookupCallback m_callback GUARDED_BY(m_mutex);

    // Retry backoff (starts at 1 second, increases 5x on each failure, resets on success)
    std::atomic<int> m_retry_delay_ms{1000};
    static constexpr int RETRY_DELAY_INITIAL_MS = 1000;
    static constexpr int RETRY_DELAY_MAX_MS = 60000;  // Cap at 1 minute
    static constexpr int RETRY_BACKOFF_MULTIPLIER = 5;

    bool send_command(const char* cmd);
    bool read_until_endmsg(std::string& response, int expected_lines = 0);
    bool parse_historical_response(const std::string& response, std::vector<Candle>& candles);
    bool set_protocol();
    bool ensure_connected();

    // Background thread
    void worker_thread();
    void process_request(const Request& req) EXCLUDES(m_mutex);
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
    [[nodiscard]] bool connect(const char* host = "127.0.0.1", int port = IQFEED_LEVEL1_PORT);
    void disconnect() EXCLUDES(m_mutex);
    [[nodiscard]] bool is_connected() const;

    // Watch/unwatch symbols
    [[nodiscard]] bool watch(const char* symbol);
    [[nodiscard]] bool unwatch(const char* symbol) EXCLUDES(m_mutex);

    // Set callback for quote updates
    void set_callback(L1Callback callback) EXCLUDES(m_mutex);

    // Get current quote (thread-safe)
    [[nodiscard]] bool get_quote(const char* symbol, L1Quote& quote) EXCLUDES(m_mutex);

    [[nodiscard]] const char* last_error() const;

private:
    std::atomic<int> m_socket;  // Atomic for thread-safe is_connected() checks
    std::atomic<bool> m_running;
    std::thread m_thread;
    Mutex m_mutex;
    std::map<std::string, L1Quote> m_quotes GUARDED_BY(m_mutex);
    L1Callback m_callback GUARDED_BY(m_mutex);
    char m_error[256];
    char m_host[64];
    int m_port;

    void stream_thread();
    bool send_command(const char* cmd);
    bool reconnect();  // Attempt to reconnect
    void parse_l1_message(const std::string& line);
    // IMPORTANT: These methods must NOT invoke callbacks while holding m_mutex
    void parse_summary_message(const char* data) EXCLUDES(m_mutex);
    void parse_update_message(const char* data) EXCLUDES(m_mutex);
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
    [[nodiscard]] bool connect(const char* host = "127.0.0.1", int port = IQFEED_LEVEL2_PORT);
    void disconnect() EXCLUDES(m_mutex);
    [[nodiscard]] bool is_connected() const;

    // Watch/unwatch symbols (max_levels typically 10)
    [[nodiscard]] bool watch(const char* symbol, int max_levels = 10);
    [[nodiscard]] bool unwatch(const char* symbol) EXCLUDES(m_mutex);

    // Set callback for book updates
    void set_callback(L2Callback callback) EXCLUDES(m_mutex);

    // Get current book (thread-safe)
    [[nodiscard]] bool get_book(const char* symbol, std::vector<L2Level>& bids, std::vector<L2Level>& asks) EXCLUDES(m_mutex);

    [[nodiscard]] const char* last_error() const;

private:
    struct BookData {
        std::vector<L2Level> bids;
        std::vector<L2Level> asks;
    };

    std::atomic<int> m_socket;  // Atomic for thread-safe is_connected() checks
    std::atomic<bool> m_running;
    std::thread m_thread;
    Mutex m_mutex;
    std::map<std::string, BookData> m_books GUARDED_BY(m_mutex);
    L2Callback m_callback GUARDED_BY(m_mutex);
    char m_error[256];
    char m_host[64];
    int m_port;

    void stream_thread();
    bool send_command(const char* cmd);
    bool reconnect();  // Attempt to reconnect
    void parse_l2_message(const std::string& line);
    // IMPORTANT: These methods must NOT invoke callbacks while holding m_mutex
    void parse_level_summary(const char* data) EXCLUDES(m_mutex);
    void parse_level_update(const char* data) EXCLUDES(m_mutex);
    void parse_level_delete(const char* data) EXCLUDES(m_mutex);
    bool set_protocol();
};

// ============================================================================
// Global instances
// ============================================================================

IQFeedLookup& get_iqfeed_lookup();
IQFeedLevel1& get_iqfeed_level1();
IQFeedLevel2& get_iqfeed_level2();

#endif // IQFEED_TCP_H
