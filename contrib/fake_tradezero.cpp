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

static std::atomic<bool> g_running{true};

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

static std::string read_http_request(int sock) {
    char buffer[4096];
    std::string request;

    while (true) {
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
        std::lock_guard<std::mutex> lock(g_data_mutex);
        MockOrder order;
        char order_id[64];
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

        char json[256];
        std::snprintf(json, sizeof(json),
            "{\"clientOrderId\":\"%s\",\"orderStatus\":\"Accepted\"}", order_id);
        send_http_response(client_sock, 200, "OK", json);
    }
    else if (method == "DELETE" && path.find("/v1/api/accounts/") == 0 && path.find("/orders/") != std::string::npos) {
        // DELETE order
        std::lock_guard<std::mutex> lock(g_data_mutex);
        // Extract order ID from path
        size_t last_slash = path.rfind('/');
        if (last_slash != std::string::npos) {
            std::string order_id = path.substr(last_slash + 1);

            // Find and cancel order
            for (auto& ord : g_orders) {
                if (ord.client_order_id == order_id) {
                    ord.order_status = "Canceled";
                    break;
                }
            }
        }

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
// WebSocket Handler (Simplified - just sends JSON over TCP)
// ============================================================================

static void handle_ws_client(int client_sock, const std::string& stream_path) {
    log_info("WebSocket client connected to %s", stream_path.c_str());

    char buffer[4096];
    std::string line_buffer;
    bool authenticated = false;
    bool subscribed = false;

    // Send initial system message
    const char* pending_auth = "{\"@system\":true,\"status\":\"PENDING_AUTH\"}\n";
    send(client_sock, pending_auth, strlen(pending_auth), 0);

    while (g_running.load()) {
        struct pollfd pfd = {client_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 1000);

        if (ret < 0) break;

        if (ret > 0) {
            ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) break;

            buffer[n] = '\0';
            line_buffer += buffer;

            // Process complete lines (JSON messages)
            size_t pos;
            while ((pos = line_buffer.find('\n')) != std::string::npos) {
                std::string line = line_buffer.substr(0, pos);
                line_buffer = line_buffer.substr(pos + 1);

                if (line.empty()) continue;

                log_info("WS %s: Received '%s'", stream_path.c_str(), line.c_str());

                // Check for auth message
                if (!authenticated && line.find("\"key\":") != std::string::npos) {
                    authenticated = true;
                    const char* connected = "{\"@system\":true,\"status\":\"CONNECTED\"}\n";
                    send(client_sock, connected, strlen(connected), 0);
                    log_info("WS %s: Authenticated", stream_path.c_str());
                }
                // Check for subscribe message
                else if (authenticated && !subscribed && line.find("\"account") != std::string::npos) {
                    subscribed = true;
                    log_info("WS %s: Subscribed", stream_path.c_str());

                    // Send initial snapshot for P&L stream
                    if (stream_path == "/stream/pnl") {
                        const char* snapshot =
                            "{\"action\":\"init\",\"accountValue\":50000.00,"
                            "\"availableCash\":25000.00,\"dayPnl\":250.50,"
                            "\"dayUnrealized\":150.25,\"dayRealized\":100.25,"
                            "\"totalUnrealized\":300.00,\"allowedLeverage\":4.0,"
                            "\"positions\":[{\"positionId\":\"pos1\",\"symbol\":\"AAPL\","
                            "\"pnlCalc\":{\"unrealizedPnL\":200.0,\"dayUnrealizedPnL\":50.0,"
                            "\"exposure\":15000.0},\"realizedPnl\":100.0,\"dayRealizedPnl\":50.0}]}\n";
                        send(client_sock, snapshot, strlen(snapshot), 0);
                    }
                }
            }
        }

        // Send periodic updates if subscribed
        if (subscribed) {
            if (stream_path == "/stream/pnl") {
                // Send aggregate update
                const char* update =
                    "{\"target\":\"aggCalcs\",\"accountValue\":50100.00,"
                    "\"exposure\":15000.0,\"dayUnrealized\":160.00,"
                    "\"dayPnl\":260.00,\"totalUnrealized\":310.00,"
                    "\"equityRatio\":0.85}\n";
                send(client_sock, update, strlen(update), 0);
            }
            else if (stream_path == "/stream/portfolio") {
                // Send order update
                std::lock_guard<std::mutex> lock(g_data_mutex);
                if (!g_orders.empty()) {
                    const auto& ord = g_orders[0];
                    char update[512];
                    std::snprintf(update, sizeof(update),
                        "{\"subscription\":\"Order\",\"symbol\":\"%s\","
                        "\"clientOrderId\":\"%s\",\"side\":\"%s\","
                        "\"orderStatus\":\"%s\",\"orderQuantity\":%d,"
                        "\"executed\":%d,\"limitPrice\":%.2f}\n",
                        ord.symbol.c_str(), ord.client_order_id.c_str(),
                        ord.side.c_str(), ord.order_status.c_str(),
                        ord.order_quantity, ord.executed, ord.limit_price);
                    send(client_sock, update, strlen(update), 0);
                }
            }

            // Slow down updates
            std::this_thread::sleep_for(std::chrono::seconds(2));
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
            // Read first line to determine stream path
            char buffer[1024];
            ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
            if (n > 0) {
                buffer[n] = '\0';
                std::string first_line(buffer);

                // Detect stream from first message or default to pnl
                std::string stream_path = "/stream/pnl";
                if (first_line.find("portfolio") != std::string::npos) {
                    stream_path = "/stream/portfolio";
                }

                std::thread(handle_ws_client, client_sock, stream_path).detach();
            } else {
                close(client_sock);
            }
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
