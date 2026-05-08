// json_parser.h - Simple JSON parser for iqfeed OHLC responses
#ifndef JSON_PARSER_H
#define JSON_PARSER_H

#include "types.h"
#include <string>
#include <vector>

// Parse JSON array of OHLC candles from iqfeed API
// Expected format:
// [{"Close": "111.1100", "Datetime": "2023-05-26", "High": "111.1000", "Low": "111.1000", "Open": "111.1000"}, ...]
bool parse_iqfeed_ohlc_json(const std::string& json, std::vector<Candle>& candles);

// Parse a single string value from JSON (finds "key": "value" and returns value)
bool json_get_string(const char* json, size_t len, const char* key, std::string& value);

// Parse a numeric value from JSON string (the value is a string like "111.1100")
bool json_get_number_as_string(const char* json, size_t len, const char* key, float& value);

#endif // JSON_PARSER_H
