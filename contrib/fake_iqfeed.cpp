// fake_iqfeed.cpp - Mock IQFeed server for integration testing
// Simulates IQFeed Lookup (port 9100), Level1 (port 5009), and Level2 (port 9200) servers
//
// Usage: ./fake_iqfeed [--lookup-port 9100] [--level1-port 5009] [--level2-port 9200]
//
// Protocol simulation:
// - Lookup: Responds to HDX (daily) and HIX (interval) commands with fake candle data
// - Level1: Responds to w<symbol> (watch) and r<symbol> (unwatch) with fake L1 quotes
// - Level2: Responds to w<symbol> (watch) and r<symbol> (unwatch) with fake order book

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <set>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

static std::atomic<bool> g_running{true};

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
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        close(sock);
        return -1;
    }

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }

    if (listen(sock, 5) < 0) {
        perror("listen");
        close(sock);
        return -1;
    }

    return sock;
}

// ============================================================================
// Fake Lookup Server (Port 9100)
// ============================================================================

static void handle_lookup_client(int client_sock) {
    log_info("Lookup client connected");

    char buffer[4096];
    std::string line_buffer;

    while (g_running.load()) {
        struct pollfd pfd = {client_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) break;
        if (ret == 0) continue;

        ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        line_buffer += buffer;

        // Process complete lines
        size_t pos;
        while ((pos = line_buffer.find('\n')) != std::string::npos) {
            std::string line = line_buffer.substr(0, pos);
            line_buffer = line_buffer.substr(pos + 1);

            // Remove \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) continue;

            log_info("Lookup: Received '%s'", line.c_str());

            // Parse command
            if (line.rfind("HDX,", 0) == 0) {
                // Daily data: HDX,SYMBOL,DATAPOINTS
                char symbol[32];
                int datapoints;
                if (std::sscanf(line.c_str(), "HDX,%31[^,],%d", symbol, &datapoints) == 2) {
                    log_info("Lookup: HDX for %s, %d datapoints", symbol, datapoints);

                    // Send fake daily candles
                    for (int i = datapoints; i > 0; i--) {
                        time_t t = time(nullptr) - (i * 86400);
                        struct tm* tm = localtime(&t);
                        char date[16];
                        strftime(date, sizeof(date), "%Y-%m-%d", tm);

                        float base = 100.0f + (i % 10);
                        char candle[256];
                        std::snprintf(candle, sizeof(candle),
                            "%s,%.2f,%.2f,%.2f,%.2f,100000,0\r\n",
                            date, base, base + 2.0f, base - 1.0f, base + 1.0f);
                        send(client_sock, candle, strlen(candle), 0);
                    }

                    // End marker
                    const char* end = "!ENDMSG!\r\n";
                    send(client_sock, end, strlen(end), 0);
                }
            }
            else if (line.rfind("HIX,", 0) == 0) {
                // Interval data: HIX,SYMBOL,INTERVAL,DATAPOINTS,,,1,s
                char symbol[32];
                int interval, datapoints;
                if (std::sscanf(line.c_str(), "HIX,%31[^,],%d,%d", symbol, &interval, &datapoints) == 3) {
                    log_info("Lookup: HIX for %s, %ds interval, %d datapoints", symbol, interval, datapoints);

                    // Send fake interval candles
                    for (int i = datapoints; i > 0; i--) {
                        time_t t = time(nullptr) - (i * interval);
                        struct tm* tm = localtime(&t);
                        char datetime[32];
                        strftime(datetime, sizeof(datetime), "%Y-%m-%d %H:%M:%S", tm);

                        float base = 100.0f + (i % 10);
                        char candle[256];
                        std::snprintf(candle, sizeof(candle),
                            "%s,%.2f,%.2f,%.2f,%.2f,100000,0\r\n",
                            datetime, base, base + 2.0f, base - 1.0f, base + 1.0f);
                        send(client_sock, candle, strlen(candle), 0);
                    }

                    // End marker
                    const char* end = "!ENDMSG!\r\n";
                    send(client_sock, end, strlen(end), 0);
                }
            }
        }
    }

    close(client_sock);
    log_info("Lookup client disconnected");
}

static void lookup_server(int port) {
    int server_sock = create_server_socket(port);
    if (server_sock < 0) {
        log_info("Failed to create lookup server on port %d", port);
        return;
    }

    log_info("Lookup server listening on port %d", port);

    while (g_running.load()) {
        struct pollfd pfd = {server_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) break;
        if (ret == 0) continue;

        int client_sock = accept(server_sock, nullptr, nullptr);
        if (client_sock >= 0) {
            std::thread(handle_lookup_client, client_sock).detach();
        }
    }

    close(server_sock);
    log_info("Lookup server stopped");
}

// ============================================================================
// Fake Level1 Server (Port 5009)
// ============================================================================

static void handle_level1_client(int client_sock) {
    log_info("Level1 client connected");

    char buffer[4096];
    std::string line_buffer;
    std::set<std::string> watched_symbols;

    while (g_running.load()) {
        struct pollfd pfd = {client_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) break;
        if (ret == 0) {
            // Send periodic updates for watched symbols
            for (const auto& symbol : watched_symbols) {
                time_t now = time(nullptr);
                struct tm* tm = localtime(&now);
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M:%S", tm);

                float base = 100.0f + (rand() % 10);
                char update[512];
                std::snprintf(update, sizeof(update),
                    "Q,%s,%.2f,%d,%.2f,%d,%.2f,%d,%.2f,%.2f,%.2f,%.2f,%s,100000\r\n",
                    symbol.c_str(), base - 0.05f, 100, base + 0.05f, 100, base, 50,
                    base + 1.0f, base - 1.0f, base, base, time_str);
                send(client_sock, update, strlen(update), 0);
            }
            continue;
        }

        ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        line_buffer += buffer;

        // Process complete lines
        size_t pos;
        while ((pos = line_buffer.find('\n')) != std::string::npos) {
            std::string line = line_buffer.substr(0, pos);
            line_buffer = line_buffer.substr(pos + 1);

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) continue;

            log_info("Level1: Received '%s'", line.c_str());

            // Parse command
            if (line == "S,SET PROTOCOL,6.2") {
                const char* ack = "S,CURRENT PROTOCOL,6.2\r\n";
                send(client_sock, ack, strlen(ack), 0);
            }
            else if (line.rfind("w", 0) == 0 && line.length() > 1) {
                // Watch symbol
                std::string symbol = line.substr(1);
                watched_symbols.insert(symbol);
                log_info("Level1: Watching %s", symbol.c_str());

                // Send initial summary
                time_t now = time(nullptr);
                struct tm* tm = localtime(&now);
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M:%S", tm);

                float base = 100.0f;
                char summary[512];
                std::snprintf(summary, sizeof(summary),
                    "P,%s,%.2f,%d,%.2f,%d,%.2f,%d,%.2f,%.2f,%.2f,%.2f,%s,100000\r\n",
                    symbol.c_str(), base - 0.05f, 100, base + 0.05f, 100, base, 50,
                    base + 1.0f, base - 1.0f, base, base, time_str);
                send(client_sock, summary, strlen(summary), 0);
            }
            else if (line.rfind("r", 0) == 0 && line.length() > 1) {
                // Unwatch symbol
                std::string symbol = line.substr(1);
                watched_symbols.erase(symbol);
                log_info("Level1: Unwatching %s", symbol.c_str());
            }
        }
    }

    close(client_sock);
    log_info("Level1 client disconnected");
}

static void level1_server(int port) {
    int server_sock = create_server_socket(port);
    if (server_sock < 0) {
        log_info("Failed to create level1 server on port %d", port);
        return;
    }

    log_info("Level1 server listening on port %d", port);

    while (g_running.load()) {
        struct pollfd pfd = {server_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) break;
        if (ret == 0) continue;

        int client_sock = accept(server_sock, nullptr, nullptr);
        if (client_sock >= 0) {
            std::thread(handle_level1_client, client_sock).detach();
        }
    }

    close(server_sock);
    log_info("Level1 server stopped");
}

// ============================================================================
// Fake Level2 Server (Port 9200)
// ============================================================================

static void handle_level2_client(int client_sock) {
    log_info("Level2 client connected");

    char buffer[4096];
    std::string line_buffer;
    std::set<std::string> watched_symbols;

    while (g_running.load()) {
        struct pollfd pfd = {client_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) break;
        if (ret == 0) continue;

        ssize_t n = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) break;

        buffer[n] = '\0';
        line_buffer += buffer;

        size_t pos;
        while ((pos = line_buffer.find('\n')) != std::string::npos) {
            std::string line = line_buffer.substr(0, pos);
            line_buffer = line_buffer.substr(pos + 1);

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            if (line.empty()) continue;

            log_info("Level2: Received '%s'", line.c_str());

            if (line == "S,SET PROTOCOL,6.2") {
                const char* ack = "S,CURRENT PROTOCOL,6.2\r\n";
                send(client_sock, ack, strlen(ack), 0);
            }
            else if (line.rfind("w", 0) == 0 && line.length() > 1) {
                std::string symbol = line.substr(1);
                watched_symbols.insert(symbol);
                log_info("Level2: Watching %s", symbol.c_str());

                // Send fake order book (5 levels each side)
                float mid = 100.0f;
                for (int i = 0; i < 5; i++) {
                    // Bid side
                    char bid[256];
                    std::snprintf(bid, sizeof(bid), "Z,%s,%.2f,%d,B\r\n",
                        symbol.c_str(), mid - 0.05f * (i + 1), 100 * (i + 1));
                    send(client_sock, bid, strlen(bid), 0);

                    // Ask side
                    char ask[256];
                    std::snprintf(ask, sizeof(ask), "Z,%s,%.2f,%d,A\r\n",
                        symbol.c_str(), mid + 0.05f * (i + 1), 100 * (i + 1));
                    send(client_sock, ask, strlen(ask), 0);
                }
            }
            else if (line.rfind("r", 0) == 0 && line.length() > 1) {
                std::string symbol = line.substr(1);
                watched_symbols.erase(symbol);
                log_info("Level2: Unwatching %s", symbol.c_str());
            }
        }
    }

    close(client_sock);
    log_info("Level2 client disconnected");
}

static void level2_server(int port) {
    int server_sock = create_server_socket(port);
    if (server_sock < 0) {
        log_info("Failed to create level2 server on port %d", port);
        return;
    }

    log_info("Level2 server listening on port %d", port);

    while (g_running.load()) {
        struct pollfd pfd = {server_sock, POLLIN, 0};
        int ret = poll(&pfd, 1, 100);

        if (ret < 0) break;
        if (ret == 0) continue;

        int client_sock = accept(server_sock, nullptr, nullptr);
        if (client_sock >= 0) {
            std::thread(handle_level2_client, client_sock).detach();
        }
    }

    close(server_sock);
    log_info("Level2 server stopped");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    int lookup_port = 9100;
    int level1_port = 5009;
    int level2_port = 9200;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--lookup-port") == 0 && i + 1 < argc) {
            lookup_port = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--level1-port") == 0 && i + 1 < argc) {
            level1_port = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--level2-port") == 0 && i + 1 < argc) {
            level2_port = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("Usage: %s [options]\n", argv[0]);
            std::printf("Options:\n");
            std::printf("  --lookup-port PORT   Lookup server port (default: 9100)\n");
            std::printf("  --level1-port PORT   Level1 server port (default: 5009)\n");
            std::printf("  --level2-port PORT   Level2 server port (default: 9200)\n");
            std::printf("  --help               Show this help\n");
            return 0;
        }
    }

    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    log_info("Starting fake IQFeed server");
    log_info("Lookup port: %d", lookup_port);
    log_info("Level1 port: %d", level1_port);
    log_info("Level2 port: %d", level2_port);
    log_info("Press Ctrl+C to stop");

    // Start servers in separate threads
    std::thread t1(lookup_server, lookup_port);
    std::thread t2(level1_server, level1_port);
    std::thread t3(level2_server, level2_port);

    // Wait for Ctrl+C
    std::printf("\nRunning... Press Ctrl+C to stop\n");
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    log_info("Shutting down...");
    t1.join();
    t2.join();
    t3.join();

    return 0;
}
