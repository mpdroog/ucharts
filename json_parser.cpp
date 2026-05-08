// json_parser.cpp - Simple JSON parser implementation
#include "json_parser.h"
#include <cstring>
#include <cstdlib>
#include <algorithm>

// Skip whitespace and return pointer to next non-whitespace char
static const char* skip_ws(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
        p++;
    }
    return p;
}

// Find the value for a given key within a JSON object
// Returns pointer to the start of the value (after the colon)
static const char* find_key_value(const char* json, size_t len, const char* key) {
    const char* end = json + len;
    size_t key_len = std::strlen(key);

    const char* p = json;
    while (p < end) {
        // Find the key (quoted string)
        const char* quote = std::strchr(p, '"');
        if (quote == nullptr || quote >= end) return nullptr;

        const char* key_start = quote + 1;
        const char* key_end = std::strchr(key_start, '"');
        if (key_end == nullptr || key_end >= end) return nullptr;

        size_t found_len = static_cast<size_t>(key_end - key_start);

        if (found_len == key_len && std::strncmp(key_start, key, key_len) == 0) {
            // Found the key, now find the colon and value
            p = key_end + 1;
            p = skip_ws(p, end);
            if (p < end && *p == ':') {
                p++;
                p = skip_ws(p, end);
                return p;
            }
        }

        p = key_end + 1;
    }

    return nullptr;
}

bool json_get_string(const char* json, size_t len, const char* key, std::string& value) {
    const char* val = find_key_value(json, len, key);
    if (val == nullptr) return false;

    const char* end = json + len;

    // Value should start with a quote
    if (val >= end || *val != '"') return false;

    const char* str_start = val + 1;
    const char* str_end = std::strchr(str_start, '"');
    if (str_end == nullptr || str_end >= end) return false;

    value.assign(str_start, static_cast<size_t>(str_end - str_start));
    return true;
}

bool json_get_number_as_string(const char* json, size_t len, const char* key, float& value) {
    std::string str_val;
    if (!json_get_string(json, len, key, str_val)) {
        return false;
    }

    char* endptr = nullptr;
    float f = std::strtof(str_val.c_str(), &endptr);
    if (endptr == str_val.c_str()) {
        return false;
    }

    value = f;
    return true;
}

bool parse_iqfeed_ohlc_json(const std::string& json, std::vector<Candle>& candles) {
    candles.clear();

    if (json.empty()) return false;

    const char* p = json.c_str();
    const char* end = p + json.size();

    // Skip to opening bracket
    p = skip_ws(p, end);
    if (p >= end || *p != '[') return false;
    p++;

    // Parse each object in the array
    while (p < end) {
        p = skip_ws(p, end);
        if (p >= end) break;

        // Check for end of array
        if (*p == ']') break;

        // Skip comma between objects
        if (*p == ',') {
            p++;
            continue;
        }

        // Find object start
        if (*p != '{') {
            p++;
            continue;
        }

        // Find object end
        const char* obj_start = p;
        int brace_count = 1;
        p++;
        while (p < end && brace_count > 0) {
            if (*p == '{') brace_count++;
            else if (*p == '}') brace_count--;
            p++;
        }
        const char* obj_end = p;
        size_t obj_len = static_cast<size_t>(obj_end - obj_start);

        // Parse this object
        Candle c;
        std::string datetime;

        bool ok = true;
        ok = ok && json_get_string(obj_start, obj_len, "Datetime", datetime);
        ok = ok && json_get_number_as_string(obj_start, obj_len, "Open", c.open);
        ok = ok && json_get_number_as_string(obj_start, obj_len, "High", c.high);
        ok = ok && json_get_number_as_string(obj_start, obj_len, "Low", c.low);
        ok = ok && json_get_number_as_string(obj_start, obj_len, "Close", c.close);

        if (ok) {
            safe_strcpy(c.timestamp, datetime.c_str(), sizeof(c.timestamp));
            c.volume = 0.0f;  // API doesn't provide volume in basic OHLC
            candles.push_back(c);
        }
    }

    // The API returns candles in reverse order (newest first), so reverse them
    std::reverse(candles.begin(), candles.end());

    return !candles.empty();
}
