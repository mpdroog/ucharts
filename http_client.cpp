// http_client.cpp - HTTP client implementation using libcurl
#include "http_client.h"
#include <curl/curl.h>
#include <cstring>

// Global instance
static HttpClient g_http_client;

HttpClient& get_http_client() {
    return g_http_client;
}

// Callback for curl to write received data
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* response = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    response->append(ptr, total);
    return total;
}

HttpClient::HttpClient()
    : m_base_url("http://localhost:8080"), m_timeout(10) {
    // Initialize curl globally (once per application)
    static bool curl_initialized = false;
    if (!curl_initialized) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_initialized = true;
    }
}

HttpClient::~HttpClient() {
    // Note: curl_global_cleanup() should be called at program exit
    // but we leave it for static destruction order issues
}

void HttpClient::set_base_url(const char* url) {
    if (url != nullptr) {
        m_base_url = url;
        // Remove trailing slash if present
        if (!m_base_url.empty() && m_base_url.back() == '/') {
            m_base_url.pop_back();
        }
    }
}

void HttpClient::set_timeout(int seconds) {
    m_timeout = seconds;
}

std::string HttpClient::build_url(const char* path) const {
    std::string url = m_base_url;
    if (path != nullptr && path[0] != '\0') {
        if (path[0] != '/') {
            url += '/';
        }
        url += path;
    }
    return url;
}

HttpResponse HttpClient::get(const char* path) {
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.error = "Failed to initialize curl";
        return response;
    }

    std::string url = build_url(path);
    std::string body;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(m_timeout));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        response.error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        return response;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    response.status_code = static_cast<int>(http_code);
    response.body = body;
    response.success = (http_code >= 200 && http_code < 300);

    if (!response.success) {
        response.error = "HTTP " + std::to_string(http_code);
    }

    curl_easy_cleanup(curl);
    return response;
}
