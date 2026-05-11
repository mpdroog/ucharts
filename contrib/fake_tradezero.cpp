// fake_tradezero.cpp - Mock TradeZero API server for integration testing
// Simulates TradeZero REST API and WebSocket streams
//
// Usage: ./fake_tradezero [--http-port 8080] [--ws-port 8081]
//
// REST API endpoints (HTTP):
// - GET  /v1/api/accounts/{accountId}/positions
// - GET  /v1/api/accounts/{accountId}/orders
// - POST /v1/api/accounts/{accountId}/order
// - DELETE /v1/api/accounts/{accountId}/orders/{orderId}
//
// WebSocket streams:
// - /stream/pnl - P&L and account updates
// - /stream/portfolio - Order and position updates

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

static std::atomic<bool> g_running{true};

// Signal handler for graceful shutdown
static void signal_handler(int sig) {
    (void)sig;
    g_running.store(false);
}

// ============================================================================
// Mock Data
// ============================================================================

struct MockOrder {
    std::string client_order_id;
    std::string symbol;
    std::string side;
    std::string order_status;
    int order_quantity;
    int executed;
    float limit_price;
    float price_avg;
};

struct MockPosition {
    std::string symbol;
    float shares;
    float price_avg;
    float price_close;
};

static std::mutex g_data_mutex;
static std::vector<MockOrder> g_orders;
static std::vector<MockPosition> g_positions;
static int g_next_order_id = 1000;

// Event queue for WebSocket notifications
struct OrderEvent {
    MockOrder order;
};
static std::mutex g_events_mutex;
static std::vector<OrderEvent> g_pending_events;

// ============================================================================
// Utilities
// ============================================================================

static void log_info(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char time_str[32];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);

    std::printf("[%s][INFO] %s\n", time_str, buf);
    std::fflush(stdout);
}

// Base64 encoding table
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64_encode(const unsigned char* data, size_t len) {
    std::string result;
    result.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int val = data[i] << 16;
        if (i + 1 < len) val |= data[i + 1] << 8;
        if (i + 2 < len) val |= data[i + 2];
        result += base64_chars[(val >> 18) & 0x3F];
        result += base64_chars[(val >> 12) & 0x3F];
        result += (i + 1 < len) ? base64_chars[(val >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? base64_chars[val & 0x3F] : '=';
    }
    return result;
}

// Compute SHA1 hash
static void sha1(const unsigned char* data, size_t len, unsigned char* hash) {
#ifdef __APPLE__
    CC_SHA1(data, static_cast<CC_LONG>(len), hash);
#else
    SHA1(data, len, hash);
#endif
}

// Compute WebSocket accept key
static std::string compute_ws_accept(const std::string& ws_key) {
    const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string combined = ws_key + magic;
    unsigned char hash[20];
    sha1(reinterpret_cast<const unsigned char*>(combined.c_str()), combined.size(), hash);
    return base64_encode(hash, 20);
}

// Send WebSocket frame (text)
static void ws_send_text(int sock, const std::string& msg) {
    size_t len = msg.size();
    std::vector<uint8_t> frame;

    // Opcode: 0x81 = final frame, text
    frame.push_back(0x81);

    // Length
    if (len <= 125) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len <= 65535) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
        }
    }

    // Payload
    frame.insert(frame.end(), msg.begin(), msg.end());

    send(sock, frame.data(), frame.size(), 0);
}

// Read WebSocket frame (simple implementation)
static std::string ws_recv_frame(int sock, int timeout_ms = 5000) {
    // Set socket timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint8_t header[2];
    ssize_t n = recv(sock, header, 2, 0);
    if (n <= 0) return "";

    // bool fin = (header[0] & 0x80) != 0;
    // uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    if (payload_len == 126) {
        uint8_t ext[2];
        recv(sock, ext, 2, 0);
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        recv(sock, ext, 8, 0);
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }

    uint8_t mask[4] = {0, 0, 0, 0};
    if (masked) {
        recv(sock, mask, 4, 0);
    }

    std::string payload(payload_len, '\0');
    size_t received = 0;
    while (received < payload_len) {
        n = recv(sock, &payload[received], payload_len - received, 0);
        if (n <= 0) break;
        received += static_cast<size_t>(n);
    }

    // Unmask if needed
    if (masked) {
        for (size_t i = 0; i < payload.size(); i++) {
            payload[i] ^= mask[i % 4];
        }
    }

    return payload;
}

// Perform WebSocket handshake
static bool ws_handshake(int sock, std::string& path) {
    char buffer[4096];
    std::string request;

    // Set socket timeout for handshake
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Read HTTP request
    while (true) {
        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) return false;
        buffer[n] = '\0';
        request += buffer;
        if (request.find("\r\n\r\n") != std::string::npos) break;
    }

    // Parse request line for path
    size_t method_end = request.find(' ');
    size_t path_end = request.find(' ', method_end + 1);
    if (method_end == std::string::npos || path_end == std::string::npos) return false;
    path = request.substr(method_end + 1, path_end - method_end - 1);

    // Find Sec-WebSocket-Key
    std::string ws_key;
    size_t key_pos = request.find("Sec-WebSocket-Key:");
    if (key_pos == std::string::npos) return false;
    size_t key_start = key_pos + 18;
    while (key_start < request.size() && request[key_start] == ' ') key_start++;
    size_t key_end = request.find("\r\n", key_start);
    ws_key = request.substr(key_start, key_end - key_start);

    // Compute accept key
    std::string accept = compute_ws_accept(ws_key);

    // Send response
    char response[512];
    std::snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept.c_str());

    send(sock, response, strlen(response), 0);
    return true;
}

static int create_server_socket(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(sock);
        return -1;
    }

    if (listen(sock, 5) < 0) {
        close(sock);
        return -1;
    }

    return sock;
}

static std::string read_http_request(int sock, int timeout_ms = 5000) {
    char buffer[4096];
    std::string request;

    // Set socket receive timeout
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (true) {
        // Use poll to wait for data with timeout
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        int poll_result = poll(&pfd, 1, timeout_ms);
        if (poll_result <= 0) break;  // Timeout or error

        ssize_t n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        request += buffer;

        // Check for end of HTTP headers
        if (request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }

    return request;
}

static void send_http_response(int sock, int status_code, const char* status_text,
                               const std::string& body, const char* content_type = "application/json") {
    char headers[512];
    std::snprintf(headers, sizeof(headers),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status_code, status_text, content_type, body.length());

    send(sock, headers, strlen(headers), 0);
    send(sock, body.c_str(), body.length(), 0);
}

// ============================================================================
// REST API Handler
// ============================================================================

static void handle_http_client(int client_sock) {
    log_info("HTTP client connected");

    std::string request = read_http_request(client_sock);
    if (request.empty()) {
        close(client_sock);
        return;
    }

    // Parse request line
    size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos) {
        close(client_sock);
        return;
    }

    std::string request_line = request.substr(0, line_end);
    log_info("HTTP: %s", request_line.c_str());

    // Parse method and path
    size_t method_end = request_line.find(' ');
    size_t path_end = request_line.find(' ', method_end + 1);

    if (method_end == std::string::npos || path_end == std::string::npos) {
        close(client_sock);
        return;
    }

    std::string method = request_line.substr(0, method_end);
    std::string path = request_line.substr(method_end + 1, path_end - method_end - 1);

    std::string response_body;

    // Route requests
    if (method == "GET" && path.find("/v1/api/accounts/") == 0 && path.find("/positions") != std::string::npos) {
        // GET positions
        std::lock_guard<std::mutex> lock(g_data_mutex);
        response_body = "[";
        for (size_t i = 0; i < g_positions.size(); i++) {
            const auto& pos = g_positions[i];
            char json[512];
            std::snprintf(json, sizeof(json),
                "{\"symbol\":\"%s\",\"shares\":%.1f,\"priceAvg\":%.2f,\"priceClose\":%.2f}",
                pos.symbol.c_str(), pos.shares, pos.price_avg, pos.price_close);
            response_body += json;
            if (i < g_positions.size() - 1) response_body += ",";
        }
        response_body += "]";
        send_http_response(client_sock, 200, "OK", response_body);
    }
    else if (method == "GET" && path.find("/v1/api/accounts/") == 0 && path.find("/orders") != std::string::npos) {
        // GET orders
        std::lock_guard<std::mutex> lock(g_data_mutex);
        response_body = "[";
        for (size_t i = 0; i < g_orders.size(); i++) {
            const auto& ord = g_orders[i];
            char json[512];
            std::snprintf(json, sizeof(json),
                "{\"clientOrderId\":\"%s\",\"symbol\":\"%s\",\"side\":\"%s\","
                "\"orderStatus\":\"%s\",\"orderQuantity\":%d,\"executed\":%d,"
                "\"limitPrice\":%.2f,\"priceAvg\":%.2f}",
                ord.client_order_id.c_str(), ord.symbol.c_str(), ord.side.c_str(),
                ord.order_status.c_str(), ord.order_quantity, ord.executed,
                ord.limit_price, ord.price_avg);
            response_body += json;
            if (i < g_orders.size() - 1) response_body += ",";
        }
        response_body += "]";
        send_http_response(client_sock, 200, "OK", response_body);
    }
    else if (method == "POST" && path.find("/v1/api/accounts/") == 0 && path.find("/order") != std::string::npos) {
        // POST order (extract body from request)
        size_t body_start = request.find("\r\n\r\n");
        std::string body = (body_start != std::string::npos) ? request.substr(body_start + 4) : "";

        log_info("HTTP: POST order body: %s", body.c_str());

        // Parse simple JSON (extract symbol, side, quantity, limitPrice)
        char symbol[32] = "AAPL";
        char side[16] = "buy";
        int quantity = 100;
        float limit_price = 150.0f;

        // Simple parsing
        size_t sym_pos = body.find("\"symbol\":\"");
        if (sym_pos != std::string::npos) {
            size_t start = sym_pos + 10;
            size_t end = body.find("\"", start);
            if (end != std::string::npos) {
                strncpy(symbol, body.substr(start, end - start).c_str(), sizeof(symbol) - 1);
            }
        }

        // Create order
        MockOrder order;
        char order_id[64];
        {
            std::lock_guard<std::mutex> lock(g_data_mutex);
            std::snprintf(order_id, sizeof(order_id), "ORD%d", g_next_order_id++);
            order.client_order_id = order_id;
            order.symbol = symbol;
            order.side = side;
            order.order_status = "Accepted";
            order.order_quantity = quantity;
            order.executed = 0;
            order.limit_price = limit_price;
            order.price_avg = 0.0f;
            g_orders.push_back(order);
        }

        // Queue event for WebSocket notification
        {
            std::lock_guard<std::mutex> lock(g_events_mutex);
            g_pending_events.push_back({order});
        }

        char json[256];
        std::snprintf(json, sizeof(json),
            "{\"clientOrderId\":\"%s\",\"orderStatus\":\"Accepted\"}", order_id);
        send_http_response(client_sock, 200, "OK", json);
    }
    else if (method == "DELETE" && path.find("/v1/api/accounts/") == 0 && path.find("/orders/") != std::string::npos) {
        // DELETE order
        MockOrder canceled_order;
        bool found = false;

        {
            std::lock_guard<std::mutex> lock(g_data_mutex);
            // Extract order ID from path
            size_t last_slash = path.rfind('/');
            if (last_slash != std::string::npos) {
                std::string order_id = path.substr(last_slash + 1);

                // Find and cancel order
                for (auto& ord : g_orders) {
                    if (ord.client_order_id == order_id) {
                        ord.order_status = "Canceled";
                        canceled_order = ord;
                        found = true;
                        break;
                    }
                }
            }
        }

        // Queue event for WebSocket notification
        if (found) {
            std::lock_guard<std::mutex> lock(g_events_mutex);
            g_pending_events.push_back({canceled_order});
        }

        send_http_response(client_sock, 200, "OK", "{\"status\":\"success\"}");
    }
    else if (method == "DELETE" && path.find("/accounts/orders") != std::string::npos && path.find("/orders/") == std::string::npos) {
        // DELETE all orders (cancel all)
        std::vector<MockOrder> canceled_orders;

        {
            std::lock_guard<std::mutex> lock(g_data_mutex);
            for (auto& ord : g_orders) {
                if (ord.order_status != "Canceled" && ord.order_status != "Filled") {
                    ord.order_status = "Canceled";
                    canceled_orders.push_back(ord);
                }
            }
        }

        // Queue events for WebSocket notification
        {
            std::lock_guard<std::mutex> lock(g_events_mutex);
            for (const auto& ord : canceled_orders) {
                g_pending_events.push_back({ord});
            }
        }

        log_info("HTTP: Cancel all orders - %zu orders canceled", canceled_orders.size());
        send_http_response(client_sock, 200, "OK", "{\"status\":\"success\"}");
    }
    else {
        send_http_response(client_sock, 404, "Not Found", "{\"error\":\"Not Found\"}");
    }

    close(client_sock);
    log_info("HTTP client disconnected");
}

static void http_server(int port) {
    int server_sock = create_server_socket(port);
    if (server_sock < 0) {
        log_info("Failed to create HTTP server on port %d", port);
        return;
    }

    log_info("HTTP server listening on port %d", port);

    // Add some initial mock data
    {
        std::lock_guard<std::mutex> lock(g_data_mutex);

        MockPosition pos1;
        pos1.symbol = "AAPL";
        pos1.shares = 100.0f;
        pos1.price_avg = 150.0f;
        pos1.price_close = 151.0f;
        g_positions.push_back(pos1);

        MockOrder ord1;
        ord1.client_order_id = "ORD999";
        ord1.symbol = "TSLA";
        ord1.side = "Buy";
        ord1.order_status = "Accepted";
        ord1.order_quantity = 50;
        ord1.executed = 0;
        ord1.limit_price = 200.0f;
        ord1.price_avg = 0.0f;
        g_orders.push_back(ord1);
    }

    while (g_running.load()) {
        struct pollfd pfd = {server_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) break;
        if (ret == 0) continue;

        int client_sock = accept(server_sock, nullptr, nullptr);
        if (client_sock >= 0) {
            std::thread(handle_http_client, client_sock).detach();
        }
    }

    close(server_sock);
    log_info("HTTP server stopped");
}

// ============================================================================
// WebSocket Handler (with proper WebSocket framing)
// ============================================================================

static void handle_ws_client(int client_sock, const std::string& stream_path) {
    log_info("WebSocket client connected to %s", stream_path.c_str());

    bool authenticated = false;
    bool subscribed = false;

    // Send initial system message using WebSocket frame
    ws_send_text(client_sock, "{\"@system\":true,\"status\":\"PENDING_AUTH\"}");

    while (g_running.load()) {
        struct pollfd pfd = {client_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);  // 100ms poll timeout

        if (ret < 0) break;

        if (ret > 0) {
            // Read WebSocket frame
            std::string msg = ws_recv_frame(client_sock, 1000);
            if (msg.empty()) {
                // Connection closed or error - exit loop to avoid busy-wait
                // (poll returns immediately on closed socket, ws_recv_frame returns empty)
                break;
            }

            log_info("WS %s: Received '%s'", stream_path.c_str(), msg.c_str());

            // Check for auth message
            if (!authenticated && msg.find("\"key\":") != std::string::npos) {
                authenticated = true;
                ws_send_text(client_sock, "{\"@system\":true,\"status\":\"CONNECTED\"}");
                log_info("WS %s: Authenticated", stream_path.c_str());
            }
            // Check for subscribe message
            else if (authenticated && !subscribed && msg.find("\"account") != std::string::npos) {
                subscribed = true;
                log_info("WS %s: Subscribed", stream_path.c_str());

                // Send initial snapshot for P&L stream
                if (stream_path == "/stream/pnl") {
                    ws_send_text(client_sock,
                        "{\"action\":\"init\",\"accountValue\":50000.00,"
                        "\"availableCash\":25000.00,\"dayPnl\":250.50,"
                        "\"dayUnrealized\":150.25,\"dayRealized\":100.25,"
                        "\"totalUnrealized\":300.00,\"allowedLeverage\":4.0,"
                        "\"positions\":[{\"positionId\":\"pos1\",\"symbol\":\"AAPL\","
                        "\"pnlCalc\":{\"unrealizedPnL\":200.0,\"dayUnrealizedPnL\":50.0,"
                        "\"exposure\":15000.0},\"realizedPnl\":100.0,\"dayRealizedPnl\":50.0}]}");
                }
            }
        }

        // Process pending events for portfolio stream (immediate notifications)
        if (subscribed && stream_path == "/stream/portfolio") {
            std::vector<OrderEvent> events_to_send;
            {
                std::lock_guard<std::mutex> lock(g_events_mutex);
                events_to_send = std::move(g_pending_events);
                g_pending_events.clear();
            }

            for (const auto& event : events_to_send) {
                char update[512];
                std::snprintf(update, sizeof(update),
                    "{\"subscription\":\"Order\",\"symbol\":\"%s\","
                    "\"clientOrderId\":\"%s\",\"side\":\"%s\","
                    "\"orderStatus\":\"%s\",\"orderQuantity\":%d,"
                    "\"executed\":%d,\"limitPrice\":%.2f}",
                    event.order.symbol.c_str(), event.order.client_order_id.c_str(),
                    event.order.side.c_str(), event.order.order_status.c_str(),
                    event.order.order_quantity, event.order.executed, event.order.limit_price);
                ws_send_text(client_sock, update);
                log_info("WS portfolio: Sent order event for %s (%s)",
                    event.order.client_order_id.c_str(), event.order.order_status.c_str());
            }
        }

        // Send periodic P&L updates (less frequent)
        if (subscribed && stream_path == "/stream/pnl") {
            static int pnl_counter = 0;
            if (++pnl_counter % 50 == 0) {  // Every 5 seconds (50 * 100ms poll)
                ws_send_text(client_sock,
                    "{\"target\":\"aggCalcs\",\"accountValue\":50100.00,"
                    "\"exposure\":15000.0,\"dayUnrealized\":160.00,"
                    "\"dayPnl\":260.00,\"totalUnrealized\":310.00,"
                    "\"equityRatio\":0.85}");
            }
        }
    }

    close(client_sock);
    log_info("WebSocket client disconnected from %s", stream_path.c_str());
}

static void ws_server(int port) {
    int server_sock = create_server_socket(port);
    if (server_sock < 0) {
        log_info("Failed to create WebSocket server on port %d", port);
        return;
    }

    log_info("WebSocket server listening on port %d", port);
    log_info("  /stream/pnl - P&L stream");
    log_info("  /stream/portfolio - Portfolio stream");

    while (g_running.load()) {
        struct pollfd pfd = {server_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) break;
        if (ret == 0) continue;

        int client_sock = accept(server_sock, nullptr, nullptr);
        if (client_sock >= 0) {
            // Perform WebSocket handshake
            std::string stream_path;
            if (!ws_handshake(client_sock, stream_path)) {
                log_info("WebSocket handshake failed");
                close(client_sock);
                continue;
            }

            log_info("WebSocket handshake successful for path: %s", stream_path.c_str());
            std::thread(handle_ws_client, client_sock, stream_path).detach();
        }
    }

    close(server_sock);
    log_info("WebSocket server stopped");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    int http_port = 8080;
    int ws_port = 8081;

    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--http-port") == 0 && i + 1 < argc) {
            http_port = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--ws-port") == 0 && i + 1 < argc) {
            ws_port = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("Usage: %s [options]\n", argv[0]);
            std::printf("Options:\n");
            std::printf("  --http-port PORT   HTTP REST API port (default: 8080)\n");
            std::printf("  --ws-port PORT     WebSocket server port (default: 8081)\n");
            std::printf("  --help             Show this help\n");
            return 0;
        }
    }

    // Setup signal handlers for graceful shutdown
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    log_info("Starting fake TradeZero server");
    log_info("HTTP REST API: http://localhost:%d", http_port);
    log_info("WebSocket: ws://localhost:%d", ws_port);
    log_info("Press Ctrl+C to stop");

    std::thread t1(http_server, http_port);
    std::thread t2(ws_server, ws_port);

    std::printf("\nRunning... Press Ctrl+C to stop\n");
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    log_info("Shutting down...");
    t1.join();
    t2.join();

    return 0;
}
