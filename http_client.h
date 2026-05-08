// http_client.h - Simple HTTP client for fetching data from iqfeed API
#ifndef HTTP_CLIENT_H
#define HTTP_CLIENT_H

#include <string>

// HTTP response structure
struct HttpResponse {
    int status_code;
    std::string body;
    std::string error;
    bool success;

    HttpResponse() : status_code(0), success(false) {}
};

// HTTP client for making requests to the iqfeed API
class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Set base URL (e.g., "http://localhost:8080")
    void set_base_url(const char* url);

    // Set timeout in seconds (default: 10)
    void set_timeout(int seconds);

    // GET request to a path (e.g., "/ohlc?asset=AAPL&range=DAILY&datapoints=200")
    HttpResponse get(const char* path);

    // Get the full URL for a path
    std::string build_url(const char* path) const;

private:
    std::string m_base_url;
    int m_timeout;
};

// Global HTTP client instance
HttpClient& get_http_client();

#endif // HTTP_CLIENT_H
