// iqfeed_tcp.cpp - TCP client implementation for IQFeed protocol
#include "iqfeed_tcp.h"
#include "logger.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <thread>
#include <chrono>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <errno.h>

// Global instances
static IQFeedLookup g_lookup;
static IQFeedLevel1 g_level1;
static IQFeedLevel2 g_level2;

// Helper: convert symbol to uppercase (IQFeed requires uppercase)
static void to_uppercase(char* dest, const char* src, size_t dest_size) {
    size_t i = 0;
    while (i < dest_size - 1 && src[i] != '\0') {
        char c = src[i];
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 32);
        }
        dest[i] = c;
        i++;
    }
    dest[i] = '\0';
}

IQFeedLookup& get_iqfeed_lookup() { return g_lookup; }
IQFeedLevel1& get_iqfeed_level1() { return g_level1; }
IQFeedLevel2& get_iqfeed_level2() { return g_level2; }

// ============================================================================
// Helper functions
// ============================================================================

static int connect_tcp(const char* host, int port, char* error, size_t error_size) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::snprintf(error, error_size, "Failed to create socket: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        std::snprintf(error, error_size, "Invalid address: %s", host);
        close(sock);
        return -1;
    }

    // Set connection timeout
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::snprintf(error, error_size, "Failed to connect to %s:%d: %s", host, port, strerror(errno));
        close(sock);
        return -1;
    }

    return sock;
}

// Returns: 1 = got line, 0 = timeout, -1 = error/disconnected
static int read_line_ex(int sock, std::string& line, int timeout_ms = 5000) {
    line.clear();
    char buf[1];

    struct pollfd pfd;
    pfd.fd = sock;
    pfd.events = POLLIN;

    while (true) {
        int ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0) return -1;  // Error
        if (ret == 0) return 0;   // Timeout (expected)

        // Check for disconnect/error events
        if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
            return -1;
        }

        ssize_t n = recv(sock, buf, 1, 0);
        if (n <= 0) return -1;  // Disconnected

        if (buf[0] == '\n') {
            // Remove trailing \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return 1;  // Success
        }
        line += buf[0];
    }
}

static bool read_line(int sock, std::string& line, int timeout_ms = 5000) {
    return read_line_ex(sock, line, timeout_ms) == 1;
}

// ============================================================================
// IQFeedLookup Implementation (Async)
// ============================================================================

IQFeedLookup::IQFeedLookup() : m_socket(-1), m_port(IQFEED_LOOKUP_PORT), m_running(false) {
    m_error[0] = '\0';
    m_host[0] = '\0';
}

IQFeedLookup::~IQFeedLookup() {
    disconnect();
}

bool IQFeedLookup::connect(const char* host, int port) {
    if (m_running) {
        disconnect();
    }

    safe_strcpy(m_host, host, sizeof(m_host));
    m_port = port;

    // Start worker thread (will connect when first request arrives)
    m_running = true;
    m_thread = std::thread(&IQFeedLookup::worker_thread, this);

    LOG_I("iqfeed", "Lookup worker started for %s:%d", host, port);
    return true;
}

void IQFeedLookup::disconnect() EXCLUDES(m_mutex) {
    // Check if already disconnected
    bool was_running = m_running.exchange(false);
    if (!was_running) {
        return;
    }

    // Wake up worker thread
    {
        MutexLock lock(m_mutex);
        m_cond.notify_all();
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_socket >= 0) {
        close(m_socket);
        m_socket = -1;
    }

    {
        MutexLock lock(m_mutex);
        m_requests.clear();
    }
}

bool IQFeedLookup::is_connected() const {
    return m_socket >= 0 && m_running;
}

bool IQFeedLookup::ensure_connected() {
    if (m_socket >= 0) {
        return true;
    }

    LOG_D("iqfeed", "Connecting to lookup server %s:%d", m_host, m_port);
    m_socket = connect_tcp(m_host, m_port, m_error, sizeof(m_error));
    if (m_socket < 0) {
        LOG_E("iqfeed", "Lookup connection failed: %s", m_error);
        return false;
    }

    if (!set_protocol()) {
        close(m_socket);
        m_socket = -1;
        return false;
    }

    LOG_I("iqfeed", "Connected to lookup server");
    return true;
}

const char* IQFeedLookup::last_error() const {
    return m_error;
}

bool IQFeedLookup::set_protocol() {
    LOG_D("iqfeed", "Setting protocol 6.2...");
    if (!send_command("S,SET PROTOCOL,6.2\r\n")) {
        return false;
    }

    // Read response - may get multiple lines, consume all until we see CURRENT PROTOCOL
    std::string line;
    int attempts = 0;
    while (attempts < 10 && read_line(m_socket, line, 5000)) {
        if (line.find("CURRENT PROTOCOL") != std::string::npos) {
            LOG_I("iqfeed", "Protocol set: %s", line.c_str());
            return true;
        }
        LOG_W("iqfeed", "Lookup: Unexpected protocol response[%d]: %s", attempts, line.c_str());
        attempts++;
    }

    std::snprintf(m_error, sizeof(m_error), "No protocol response");
    LOG_E("iqfeed", "No protocol response from server after %d attempts", attempts);
    return false;
}

bool IQFeedLookup::send_command(const char* cmd) {
    if (m_socket < 0) {
        std::snprintf(m_error, sizeof(m_error), "Not connected");
        return false;
    }

    size_t len = std::strlen(cmd);
    ssize_t sent = send(m_socket, cmd, len, 0);
    if (sent != static_cast<ssize_t>(len)) {
        std::snprintf(m_error, sizeof(m_error), "Send failed: %s", strerror(errno));
        return false;
    }
    return true;
}

bool IQFeedLookup::read_until_endmsg(std::string& response, int expected_lines) {
    response.clear();
    std::string line;
    int line_count = 0;


    // Use shorter timeout for "no more data" detection since proxy may not send ENDMSG
    while (read_line(m_socket, line, 2000)) {  // 2 second timeout between lines
        line_count++;

        if (line.find("!ENDMSG!") != std::string::npos) {
            LOG_D("iqfeed", "Got ENDMSG after %d lines", line_count);
            return true;
        }
        if (line.find("E,") == 0) {
            const char* err_msg = line.c_str() + 2;
            // Check for known transient errors
            if (std::strstr(err_msg, "admin not ready") != nullptr) {
                std::snprintf(m_error, sizeof(m_error), "IQFeed not ready - wait a moment and retry");
                LOG_W("iqfeed", "IQFeed not ready yet (admin not ready)");
            } else {
                std::snprintf(m_error, sizeof(m_error), "API error: %s", err_msg);
                LOG_E("iqfeed", "API error: %s", err_msg);
            }
            return false;
        }
        response += line;
        response += '\n';

        // If we got expected lines, stop (proxy may not send ENDMSG)
        if (expected_lines > 0 && line_count >= expected_lines) {
            // Drain any remaining data including !ENDMSG! to avoid corrupting next request
            std::string drain;
            while (read_line(m_socket, drain, 100)) {  // Short timeout
                if (drain.find("!ENDMSG!") != std::string::npos) break;
            }
            return true;
        }
    }

    // If we received data but timed out waiting for more, that's okay
    // (docker-iqfeed proxy doesn't forward ENDMSG)
    if (line_count > 0) {
        LOG_D("iqfeed", "No more data after %d lines, assuming complete", line_count);
        return true;
    }

    LOG_E("iqfeed", "Timeout with no data received");
    std::snprintf(m_error, sizeof(m_error), "Timeout waiting for response");
    return false;
}

void IQFeedLookup::set_callback(LookupCallback callback) EXCLUDES(m_mutex) {
    MutexLock lock(m_mutex);
    m_callback = callback;
}

bool IQFeedLookup::has_pending_requests() const EXCLUDES(m_mutex) {
    MutexLock lock(m_mutex);
    return !m_requests.empty();
}

void IQFeedLookup::fetch_daily(const char* symbol, int datapoints, void* user_data) EXCLUDES(m_mutex) {
    Request req;
    req.type = LookupRequestType::DAILY;
    to_uppercase(req.symbol, symbol, sizeof(req.symbol));
    req.datapoints = datapoints;
    req.interval_secs = 0;
    req.user_data = user_data;

    {
        MutexLock lock(m_mutex);
        m_requests.push_back(req);
    }
    m_cond.notify_one();

    LOG_D("iqfeed", "Queued async daily request for %s", req.symbol);
}

void IQFeedLookup::fetch_interval(const char* symbol, int interval_secs, int datapoints, void* user_data) EXCLUDES(m_mutex) {
    Request req;
    req.type = LookupRequestType::INTERVAL;
    to_uppercase(req.symbol, symbol, sizeof(req.symbol));
    req.datapoints = datapoints;
    req.interval_secs = interval_secs;
    req.user_data = user_data;

    {
        MutexLock lock(m_mutex);
        m_requests.push_back(req);
    }
    m_cond.notify_one();

    LOG_D("iqfeed", "Queued async interval request for %s (%ds)", req.symbol, interval_secs);
}

// Uses condition_variable which requires std::unique_lock
void IQFeedLookup::worker_thread() {
    LOG_D("iqfeed", "Lookup worker thread started");

    while (m_running) {
        Request req;

        // Wait for a request
        {
            std::unique_lock<std::mutex> lock(m_mutex.native());
            // Lambda is called with lock held, but analyzer can't prove it
            m_cond.wait(lock, [this]() NO_THREAD_SAFETY_ANALYSIS { return !m_requests.empty() || !m_running; });

            if (!m_running) break;
            if (m_requests.empty()) continue;

            req = m_requests.front();
            m_requests.erase(m_requests.begin());
        }

        process_request(req);
    }

    LOG_D("iqfeed", "Lookup worker thread exiting");
}

void IQFeedLookup::process_request(const Request& req) EXCLUDES(m_mutex) {
    LookupResult result;
    result.success = false;
    safe_strcpy(result.symbol, req.symbol, sizeof(result.symbol));
    result.type = req.type;
    result.interval_secs = req.interval_secs;
    result.user_data = req.user_data;
    result.error[0] = '\0';

    // Ensure connected
    if (!ensure_connected()) {
        safe_strcpy(result.error, m_error, sizeof(result.error));
        // Get callback outside lock to avoid deadlock
        LookupCallback cb;
        {
            MutexLock lock(m_mutex);
            cb = m_callback;
        }
        if (cb) {
            cb(result);
        }
        return;
    }

    char cmd[128];
    int expected_lines = req.datapoints;

    if (req.type == LookupRequestType::DAILY) {
        std::snprintf(cmd, sizeof(cmd), "HDX,%s,%d\r\n", req.symbol, req.datapoints);
        LOG_D("iqfeed", "Sending: HDX,%s,%d", req.symbol, req.datapoints);
    } else {
        std::snprintf(cmd, sizeof(cmd), "HIX,%s,%d,%d,,,1,s\r\n", req.symbol, req.interval_secs, req.datapoints);
        LOG_D("iqfeed", "Sending: HIX,%s,%d,%d", req.symbol, req.interval_secs, req.datapoints);
    }

    if (!send_command(cmd)) {
        std::snprintf(result.error, sizeof(result.error), "Failed to send command: %s", m_error);
        LOG_E("iqfeed", "%s", result.error);
        close(m_socket);
        m_socket = -1;
    } else {
        std::string response;
        if (!read_until_endmsg(response, expected_lines)) {
            std::snprintf(result.error, sizeof(result.error), "No response: %s", m_error);
            LOG_E("iqfeed", "%s", result.error);
            close(m_socket);
            m_socket = -1;
        } else {
            if (parse_historical_response(response, result.candles)) {
                result.success = true;
            } else {
                safe_strcpy(result.error, "Failed to parse response", sizeof(result.error));
            }
        }
    }

    // Deliver result via callback - get callback outside lock to avoid deadlock
    LookupCallback cb;
    {
        MutexLock lock(m_mutex);
        cb = m_callback;
    }
    if (cb) {
        cb(result);
    }
}

bool IQFeedLookup::parse_historical_response(const std::string& response, std::vector<Candle>& candles) {
    // Response format: LH,DATE,HIGH,LOW,OPEN,CLOSE,VOLUME,OPENINTEREST,
    // For intervals: LH,DATETIME,HIGH,LOW,OPEN,CLOSE,TOTALVOLUME,PERIODVOLUME,NUMTRADES,

    const char* p = response.c_str();
    const char* end = p + response.size();

    while (p < end) {
        // Find end of line
        const char* line_end = p;
        while (line_end < end && *line_end != '\n') {
            line_end++;
        }

        if (line_end > p) {
            char msg_id[8] = "";
            char ts[32] = "";
            float high = 0, low = 0, open = 0, close = 0;
            float volume = 0;

            // Parse: LH,DATETIME,HIGH,LOW,OPEN,CLOSE,VOLUME,...
            int parsed = std::sscanf(p, "%7[^,],%31[^,],%f,%f,%f,%f,%f",
                                    msg_id, ts, &high, &low, &open, &close, &volume);

            if (parsed >= 6 && (std::strcmp(msg_id, "LH") == 0)) {
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

    // Data comes newest first, reverse for chronological order
    std::reverse(candles.begin(), candles.end());

    return !candles.empty();
}

// ============================================================================
// IQFeedLevel1 Implementation
// ============================================================================

IQFeedLevel1::IQFeedLevel1() : m_socket(-1), m_running(false), m_port(0) {
    m_error[0] = '\0';
    m_host[0] = '\0';
}

IQFeedLevel1::~IQFeedLevel1() {
    disconnect();
}

bool IQFeedLevel1::connect(const char* host, int port) {
    if (m_socket >= 0) {
        disconnect();
    }

    // Store for reconnection
    safe_strcpy(m_host, host, sizeof(m_host));
    m_port = port;

    m_socket = connect_tcp(host, port, m_error, sizeof(m_error));
    if (m_socket < 0) {
        return false;
    }

    if (!set_protocol()) {
        disconnect();
        return false;
    }

    // Start streaming thread
    m_running = true;
    m_thread = std::thread(&IQFeedLevel1::stream_thread, this);

    return true;
}

bool IQFeedLevel1::reconnect() {
    // Close existing socket
    if (m_socket >= 0) {
        close(m_socket);
        m_socket = -1;
    }

    if (m_host[0] == '\0' || m_port == 0) {
        return false;
    }

    LOG_I("iqfeed", "L1 reconnecting to %s:%d", m_host, m_port);

    m_socket = connect_tcp(m_host, m_port, m_error, sizeof(m_error));
    if (m_socket < 0) {
        LOG_W("iqfeed", "L1 reconnect failed: %s", m_error);
        return false;
    }

    if (!set_protocol()) {
        close(m_socket);
        m_socket = -1;
        return false;
    }

    // Re-watch all symbols
    {
        MutexLock lock(m_mutex);
        for (const auto& kv : m_quotes) {
            char cmd[64];
            std::snprintf(cmd, sizeof(cmd), "w%s\r\n", kv.first.c_str());
            send_command(cmd);
        }
    }

    LOG_I("iqfeed", "L1 reconnected successfully");
    return true;
}

void IQFeedLevel1::disconnect() EXCLUDES(m_mutex) {
    m_running = false;

    if (m_socket >= 0) {
        shutdown(m_socket, SHUT_RDWR);
        close(m_socket);
        m_socket = -1;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    MutexLock lock(m_mutex);
    m_quotes.clear();
}

bool IQFeedLevel1::is_connected() const {
    return m_socket >= 0 && m_running;
}

const char* IQFeedLevel1::last_error() const {
    return m_error;
}

bool IQFeedLevel1::set_protocol() {
    if (!send_command("S,SET PROTOCOL,6.2\r\n")) {
        return false;
    }

    // Read initial messages until we see CURRENT PROTOCOL or SERVER CONNECTED
    std::string line;
    int attempts = 0;
    while (attempts < 10 && read_line(m_socket, line, 2000)) {
        if (line.find("CURRENT PROTOCOL") != std::string::npos ||
            line.find("SERVER CONNECTED") != std::string::npos) {
            return true;
        }
        // S,KEY, S,IP, S,CUST are normal IQFeed init messages - not warnings
        if (line.find("S,KEY") == std::string::npos &&
            line.find("S,IP") == std::string::npos &&
            line.find("S,CUST") == std::string::npos) {
            LOG_W("iqfeed", "L1: Unexpected protocol response[%d]: %s", attempts, line.c_str());
        }
        attempts++;
    }

    std::snprintf(m_error, sizeof(m_error), "Protocol setup failed");
    return false;
}

bool IQFeedLevel1::send_command(const char* cmd) {
    if (m_socket < 0) {
        std::snprintf(m_error, sizeof(m_error), "Not connected");
        return false;
    }

    size_t len = std::strlen(cmd);
    ssize_t sent = send(m_socket, cmd, len, 0);
    return sent == static_cast<ssize_t>(len);
}

bool IQFeedLevel1::watch(const char* symbol) {
    char upper_symbol[32];
    to_uppercase(upper_symbol, symbol, sizeof(upper_symbol));
    char cmd[48];
    std::snprintf(cmd, sizeof(cmd), "w%s\r\n", upper_symbol);
    return send_command(cmd);
}

bool IQFeedLevel1::unwatch(const char* symbol) EXCLUDES(m_mutex) {
    // Per API spec (level1.txt), symbols must be uppercase
    char upper_symbol[32];
    to_uppercase(upper_symbol, symbol, sizeof(upper_symbol));
    char cmd[48];
    std::snprintf(cmd, sizeof(cmd), "r%s\r\n", upper_symbol);

    MutexLock lock(m_mutex);
    m_quotes.erase(upper_symbol);

    return send_command(cmd);
}

void IQFeedLevel1::set_callback(L1Callback callback) EXCLUDES(m_mutex) {
    MutexLock lock(m_mutex);
    m_callback = callback;
}

bool IQFeedLevel1::get_quote(const char* symbol, L1Quote& quote) EXCLUDES(m_mutex) {
    MutexLock lock(m_mutex);
    auto it = m_quotes.find(symbol);
    if (it == m_quotes.end()) {
        return false;
    }
    quote = it->second;
    return true;
}

void IQFeedLevel1::stream_thread() {
    std::string line;
    int msg_count = 0;
    int total_msgs = 0;
    int loop_count = 0;
    auto last_log = std::chrono::steady_clock::now();

    while (m_running && m_socket >= 0) {
        loop_count++;

        // Log stats every 5 seconds
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count();
        if (elapsed >= 5) {
            LOG_I("perf", "L1 thread: %d loops, %d msgs in %lld sec", loop_count, total_msgs, static_cast<long long>(elapsed));
            loop_count = 0;
            total_msgs = 0;
            last_log = now;
        }

        int result = read_line_ex(m_socket, line, 1000);
        if (result == 1) {
            // Got a line
            if (!line.empty()) {
                parse_l1_message(line);
                msg_count++;
                total_msgs++;
                // Yield CPU every 100 messages to prevent hogging when data streams fast
                if (msg_count >= 100) {
                    msg_count = 0;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        } else if (result == -1) {
            // Error/disconnect - keep trying to reconnect
            while (m_running && m_socket < 0) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (m_running) {
                    reconnect();
                    if (m_socket < 0) {
                        std::this_thread::sleep_for(std::chrono::seconds(3));
                    }
                }
            }
        } else {
            // result == 0 is timeout - sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    LOG_I("iqfeed", "L1 stream thread exiting (socket=%d, running=%d)", m_socket, m_running.load());
}

void IQFeedLevel1::parse_l1_message(const std::string& line) {
    if (line.empty()) return;

    char msg_type = line[0];
    if (line.size() < 3 || line[1] != ',') return;

    const char* data = line.c_str() + 2;

    switch (msg_type) {
        case 'P':  // Summary message
            parse_summary_message(data);
            break;
        case 'Q':  // Update message
            parse_update_message(data);
            break;
        case 'T':  // Timestamp - ignore
        case 'S':  // System message - ignore
        case 'F':  // Fundamental - ignore for now
            break;
        default:
            break;
    }
}

void IQFeedLevel1::parse_summary_message(const char* data) EXCLUDES(m_mutex) {
    // Format: SYMBOL,FIELD1,FIELD2,...
    // Default fields (protocol 6.2): Symbol,Exchange ID,Last,Change,Percent Change,
    // Total Volume,Incremental Volume,High,Low,Bid,Ask,Bid Size,Ask Size,Tick,
    // Bid Tick,Range,Last Trade Time,Open Interest,Open,Close,...

    char symbol[16] = "";
    char exchange[8] = "";
    float last = 0, change = 0, pct_change = 0;
    int64_t total_vol = 0, inc_vol = 0;
    float high = 0, low = 0, bid = 0, ask = 0;
    int bid_size = 0, ask_size = 0;
    char tick[4] = "";
    char bid_tick[4] = "";
    float range = 0;
    char last_time[16] = "";
    int open_interest = 0;
    float open = 0, close_price = 0;

    int parsed = std::sscanf(data, "%15[^,],%7[^,],%f,%f,%f,%lld,%lld,%f,%f,%f,%f,%d,%d,%3[^,],%3[^,],%f,%15[^,],%d,%f,%f",
                            symbol, exchange, &last, &change, &pct_change,
                            &total_vol, &inc_vol, &high, &low, &bid, &ask,
                            &bid_size, &ask_size, tick, bid_tick, &range,
                            last_time, &open_interest, &open, &close_price);

    if (parsed >= 11 && symbol[0] != '\0') {
        L1Quote quote_copy;
        L1Callback callback_copy;

        {
            MutexLock lock(m_mutex);
            L1Quote& quote = m_quotes[symbol];
            safe_strcpy(quote.symbol, symbol, sizeof(quote.symbol));
            quote.last = last;
            quote.high = high;
            quote.low = low;
            quote.bid = bid;
            quote.ask = ask;
            quote.bid_size = bid_size;
            quote.ask_size = ask_size;
            quote.volume = total_vol;
            if (parsed >= 19) {
                quote.open = open;
            }
            if (parsed >= 20) {
                quote.close = close_price;
            }
            if (last_time[0] != '\0') {
                safe_strcpy(quote.last_time, last_time, sizeof(quote.last_time));
            }
            quote_copy = quote;
            callback_copy = m_callback;
        }

        if (callback_copy) {
            callback_copy(quote_copy);
        }
    }
}

void IQFeedLevel1::parse_update_message(const char* data) EXCLUDES(m_mutex) {
    // Q messages only contain changed fields, but we'll parse what we can
    // This is a simplified version - full implementation would track field positions
    parse_summary_message(data);
}

// ============================================================================
// IQFeedLevel2 Implementation
// ============================================================================

IQFeedLevel2::IQFeedLevel2() : m_socket(-1), m_running(false), m_port(0) {
    m_error[0] = '\0';
    m_host[0] = '\0';
}

IQFeedLevel2::~IQFeedLevel2() {
    disconnect();
}

bool IQFeedLevel2::connect(const char* host, int port) {
    if (m_socket >= 0) {
        disconnect();
    }

    // Store for reconnection
    safe_strcpy(m_host, host, sizeof(m_host));
    m_port = port;

    m_socket = connect_tcp(host, port, m_error, sizeof(m_error));
    if (m_socket < 0) {
        return false;
    }

    if (!set_protocol()) {
        disconnect();
        return false;
    }

    // Start streaming thread
    m_running = true;
    m_thread = std::thread(&IQFeedLevel2::stream_thread, this);

    return true;
}

bool IQFeedLevel2::reconnect() {
    // Close existing socket
    if (m_socket >= 0) {
        close(m_socket);
        m_socket = -1;
    }

    if (m_host[0] == '\0' || m_port == 0) {
        return false;
    }

    LOG_I("iqfeed", "L2 reconnecting to %s:%d", m_host, m_port);

    m_socket = connect_tcp(m_host, m_port, m_error, sizeof(m_error));
    if (m_socket < 0) {
        LOG_W("iqfeed", "L2 reconnect failed: %s", m_error);
        return false;
    }

    if (!set_protocol()) {
        close(m_socket);
        m_socket = -1;
        return false;
    }

    // Re-watch all symbols
    {
        MutexLock lock(m_mutex);
        for (const auto& kv : m_books) {
            char cmd[64];
            std::snprintf(cmd, sizeof(cmd), "w%s\r\n", kv.first.c_str());
            send_command(cmd);
        }
    }

    LOG_I("iqfeed", "L2 reconnected successfully");
    return true;
}

void IQFeedLevel2::disconnect() EXCLUDES(m_mutex) {
    m_running = false;

    if (m_socket >= 0) {
        shutdown(m_socket, SHUT_RDWR);
        close(m_socket);
        m_socket = -1;
    }

    if (m_thread.joinable()) {
        m_thread.join();
    }

    MutexLock lock(m_mutex);
    m_books.clear();
}

bool IQFeedLevel2::is_connected() const {
    return m_socket >= 0 && m_running;
}

const char* IQFeedLevel2::last_error() const {
    return m_error;
}

bool IQFeedLevel2::set_protocol() {
    if (!send_command("S,SET PROTOCOL,6.2\r\n")) {
        return false;
    }

    // Read until we see protocol confirmation or server connected
    std::string line;
    int attempts = 0;
    while (attempts < 10 && read_line(m_socket, line, 2000)) {
        if (line.find("CURRENT PROTOCOL") != std::string::npos ||
            line.find("SERVER CONNECTED") != std::string::npos) {
            return true;
        }
        LOG_W("iqfeed", "L2: Unexpected protocol response[%d]: %s", attempts, line.c_str());
        attempts++;
    }

    std::snprintf(m_error, sizeof(m_error), "Protocol setup failed");
    return false;
}

bool IQFeedLevel2::send_command(const char* cmd) {
    if (m_socket < 0) {
        std::snprintf(m_error, sizeof(m_error), "Not connected");
        return false;
    }

    size_t len = std::strlen(cmd);
    ssize_t sent = send(m_socket, cmd, len, 0);
    return sent == static_cast<ssize_t>(len);
}

bool IQFeedLevel2::watch(const char* symbol, int max_levels) {
    char upper_symbol[32];
    to_uppercase(upper_symbol, symbol, sizeof(upper_symbol));
    // WPL - Watch Price Levels
    char cmd[64];
    std::snprintf(cmd, sizeof(cmd), "WPL,%s,%d\r\n", upper_symbol, max_levels);
    return send_command(cmd);
}

bool IQFeedLevel2::unwatch(const char* symbol) EXCLUDES(m_mutex) {
    // Per API spec (l2.txt), symbols must be uppercase
    char upper_symbol[32];
    to_uppercase(upper_symbol, symbol, sizeof(upper_symbol));
    char cmd[64];
    std::snprintf(cmd, sizeof(cmd), "RPL,%s\r\n", upper_symbol);

    MutexLock lock(m_mutex);
    m_books.erase(upper_symbol);

    return send_command(cmd);
}

void IQFeedLevel2::set_callback(L2Callback callback) EXCLUDES(m_mutex) {
    MutexLock lock(m_mutex);
    m_callback = callback;
}

bool IQFeedLevel2::get_book(const char* symbol, std::vector<L2Level>& bids, std::vector<L2Level>& asks) EXCLUDES(m_mutex) {
    bids.clear();
    asks.clear();

    MutexLock lock(m_mutex);
    auto it = m_books.find(symbol);
    if (it == m_books.end()) {
        return false;
    }

    bids = it->second.bids;
    asks = it->second.asks;
    return true;
}

void IQFeedLevel2::stream_thread() {
    std::string line;
    int msg_count = 0;
    int total_msgs = 0;
    int loop_count = 0;
    auto last_log = std::chrono::steady_clock::now();

    while (m_running && m_socket >= 0) {
        loop_count++;

        // Log stats every 5 seconds
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_log).count();
        if (elapsed >= 5) {
            LOG_I("perf", "L2 thread: %d loops, %d msgs in %lld sec", loop_count, total_msgs, static_cast<long long>(elapsed));
            loop_count = 0;
            total_msgs = 0;
            last_log = now;
        }

        int result = read_line_ex(m_socket, line, 1000);
        if (result == 1) {
            // Got a line
            if (!line.empty()) {
                parse_l2_message(line);
                msg_count++;
                total_msgs++;
                // Yield CPU every 100 messages to prevent hogging when data streams fast
                if (msg_count >= 100) {
                    msg_count = 0;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        } else if (result == -1) {
            // Error/disconnect - keep trying to reconnect
            while (m_running && m_socket < 0) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                if (m_running) {
                    reconnect();
                    if (m_socket < 0) {
                        std::this_thread::sleep_for(std::chrono::seconds(3));
                    }
                }
            }
        } else {
            // result == 0 is timeout - sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    LOG_I("iqfeed", "L2 stream thread exiting (socket=%d, running=%d)", m_socket, m_running.load());
}

void IQFeedLevel2::parse_l2_message(const std::string& line) {
    if (line.empty()) return;

    char msg_type = line[0];

    switch (msg_type) {
        case '7':  // Price Level Summary
            parse_level_summary(line.c_str());
            break;
        case '8':  // Price Level Update
            parse_level_update(line.c_str());
            break;
        case '9':  // Price Level Delete
            parse_level_delete(line.c_str());
            break;
        case 'T':  // Timestamp - ignore
        case 'S':  // System message - ignore
            break;
        default:
            break;
    }
}

void IQFeedLevel2::parse_level_summary(const char* data) EXCLUDES(m_mutex) {
    // Format: 7,SYMBOL,SIDE,PRICE,SIZE,ORDERCOUNT,PRECISION,TIME,DATE
    char msg_id[4] = "";
    char symbol[16] = "";
    char side[4] = "";
    float price = 0;
    int size = 0;
    int order_count = 0;

    int parsed = std::sscanf(data, "%3[^,],%15[^,],%3[^,],%f,%d,%d",
                            msg_id, symbol, side, &price, &size, &order_count);

    if (parsed >= 5 && symbol[0] != '\0') {
        L2Level level;
        level.price = price;
        level.size = size;
        level.order_count = order_count;
        level.is_bid = (side[0] == 'B' || side[0] == 'b');

        std::string symbol_copy;
        std::vector<L2Level> bids_copy, asks_copy;
        L2Callback callback_copy;

        {
            MutexLock lock(m_mutex);
            BookData& book = m_books[symbol];
            std::vector<L2Level>& levels = level.is_bid ? book.bids : book.asks;

            // Add or update level
            bool found = false;
            for (auto& l : levels) {
                if (std::abs(l.price - price) < 0.0001f) {
                    l = level;
                    found = true;
                    break;
                }
            }
            if (!found) {
                levels.push_back(level);
            }

            // Sort: bids descending, asks ascending
            if (level.is_bid) {
                std::sort(book.bids.begin(), book.bids.end(),
                         [](const L2Level& a, const L2Level& b) { return a.price > b.price; });
            } else {
                std::sort(book.asks.begin(), book.asks.end(),
                         [](const L2Level& a, const L2Level& b) { return a.price < b.price; });
            }

            symbol_copy = symbol;
            bids_copy = book.bids;
            asks_copy = book.asks;
            callback_copy = m_callback;
        }

        if (callback_copy) {
            callback_copy(symbol_copy.c_str(), bids_copy, asks_copy);
        }
    }
}

void IQFeedLevel2::parse_level_update(const char* data) EXCLUDES(m_mutex) {
    // Same format as summary
    parse_level_summary(data);
}

void IQFeedLevel2::parse_level_delete(const char* data) EXCLUDES(m_mutex) {
    // Format: 9,SYMBOL,SIDE,PRICE,TIME,DATE
    char msg_id[4] = "";
    char symbol[16] = "";
    char side[4] = "";
    float price = 0;

    int parsed = std::sscanf(data, "%3[^,],%15[^,],%3[^,],%f",
                            msg_id, symbol, side, &price);

    if (parsed >= 4 && symbol[0] != '\0') {
        bool is_bid = (side[0] == 'B' || side[0] == 'b');

        std::string symbol_copy;
        std::vector<L2Level> bids_copy, asks_copy;
        L2Callback callback_copy;

        {
            MutexLock lock(m_mutex);
            BookData& book = m_books[symbol];
            std::vector<L2Level>& levels = is_bid ? book.bids : book.asks;

            // Remove the level
            levels.erase(
                std::remove_if(levels.begin(), levels.end(),
                              [price](const L2Level& l) { return std::abs(l.price - price) < 0.0001f; }),
                levels.end());

            symbol_copy = symbol;
            bids_copy = book.bids;
            asks_copy = book.asks;
            callback_copy = m_callback;
        }

        if (callback_copy) {
            callback_copy(symbol_copy.c_str(), bids_copy, asks_copy);
        }
    }
}
