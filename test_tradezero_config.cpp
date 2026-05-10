// test_tradezero_config.cpp - Tests for TradeZero INI config parsing
// Compile: See Makefile test target

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

// Test helper macros
#define TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL: %s (line %d): %s\n", __func__, __LINE__, message); \
            return false; \
        } \
    } while(0)

#define TEST_PASS() \
    do { \
        std::printf("PASS: %s\n", __func__); \
        return true; \
    } while(0)

// ============================================================================
// Copy of TZConfig and INI parser from main.cpp
// ============================================================================

struct TZConfig {
    char api_key_id[128];
    char api_secret_key[128];
    char account_id[32];
    bool enabled;

    TZConfig() : enabled(false) {
        api_key_id[0] = '\0';
        api_secret_key[0] = '\0';
        account_id[0] = '\0';
    }
};

static bool load_tradezero_config(const char* ini_path, TZConfig& config) {
    FILE* file = std::fopen(ini_path, "r");
    if (file == nullptr) {
        return false;
    }

    char line[512];
    bool in_tradezero_section = false;

    while (std::fgets(line, sizeof(line), file) != nullptr) {
        // Remove newline
        size_t len = std::strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        if (len > 0 && line[len - 1] == '\r') {
            line[len - 1] = '\0';
            len--;
        }

        // Skip empty lines and comments
        char* start = line;
        while (*start == ' ' || *start == '\t') start++;
        if (*start == '\0' || *start == ';' || *start == '#') continue;

        // Check for section header
        if (*start == '[') {
            in_tradezero_section = (std::strncmp(start, "[tradezero]", 11) == 0);
            continue;
        }

        if (in_tradezero_section) {
            char* equals = std::strchr(start, '=');
            if (equals == nullptr) continue;

            // Extract key
            *equals = '\0';
            char* key = start;
            char* key_end = equals - 1;
            while (key_end > key && (*key_end == ' ' || *key_end == '\t')) {
                *key_end = '\0';
                key_end--;
            }

            // Extract value
            char* value = equals + 1;
            while (*value == ' ' || *value == '\t') value++;

            // Store values
            if (std::strcmp(key, "api_key_id") == 0) {
                std::strncpy(config.api_key_id, value, sizeof(config.api_key_id) - 1);
            } else if (std::strcmp(key, "api_secret_key") == 0) {
                std::strncpy(config.api_secret_key, value, sizeof(config.api_secret_key) - 1);
            } else if (std::strcmp(key, "account_id") == 0) {
                std::strncpy(config.account_id, value, sizeof(config.account_id) - 1);
            } else if (std::strcmp(key, "enabled") == 0) {
                config.enabled = (std::strcmp(value, "1") == 0 || std::strcmp(value, "true") == 0);
            }
        }
    }

    std::fclose(file);

    // Validate required fields if enabled
    if (config.enabled) {
        if (config.api_key_id[0] == '\0' || config.api_secret_key[0] == '\0' || config.account_id[0] == '\0') {
            return false;
        }
    }

    return true;
}

// ============================================================================
// Tests
// ============================================================================

bool test_load_nonexistent_file() {
    TZConfig config;
    bool result = load_tradezero_config("/tmp/nonexistent_file_12345.ini", config);

    TEST_ASSERT(result == false, "Should fail for nonexistent file");
    TEST_PASS();
}

bool test_parse_valid_config() {
    // Create a temporary config file
    const char* temp_file = "/tmp/test_tz_config.ini";
    FILE* f = std::fopen(temp_file, "w");
    TEST_ASSERT(f != nullptr, "Should create temp file");

    std::fprintf(f, "[tradezero]\n");
    std::fprintf(f, "api_key_id = test_key_123\n");
    std::fprintf(f, "api_secret_key = test_secret_456\n");
    std::fprintf(f, "account_id = ACC789\n");
    std::fprintf(f, "enabled = 1\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    TEST_ASSERT(result == true, "Should parse valid config");
    TEST_ASSERT(std::strcmp(config.api_key_id, "test_key_123") == 0, "api_key_id should match");
    TEST_ASSERT(std::strcmp(config.api_secret_key, "test_secret_456") == 0, "api_secret_key should match");
    TEST_ASSERT(std::strcmp(config.account_id, "ACC789") == 0, "account_id should match");
    TEST_ASSERT(config.enabled == true, "enabled should be true");

    std::remove(temp_file);
    TEST_PASS();
}

bool test_parse_disabled_config() {
    const char* temp_file = "/tmp/test_tz_config_disabled.ini";
    FILE* f = std::fopen(temp_file, "w");
    TEST_ASSERT(f != nullptr, "Should create temp file");

    std::fprintf(f, "[tradezero]\n");
    std::fprintf(f, "enabled = 0\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    TEST_ASSERT(result == true, "Should parse disabled config");
    TEST_ASSERT(config.enabled == false, "enabled should be false");

    std::remove(temp_file);
    TEST_PASS();
}

bool test_parse_config_with_whitespace() {
    const char* temp_file = "/tmp/test_tz_config_ws.ini";
    FILE* f = std::fopen(temp_file, "w");
    TEST_ASSERT(f != nullptr, "Should create temp file");

    std::fprintf(f, "  [tradezero]  \n");
    std::fprintf(f, "  api_key_id  =  test_key  \n");
    std::fprintf(f, "  api_secret_key=test_secret\n");
    std::fprintf(f, "account_id   =   ACC123   \n");
    std::fprintf(f, "enabled=1\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    TEST_ASSERT(result == true, "Should parse config with whitespace");
    TEST_ASSERT(std::strcmp(config.api_key_id, "test_key  ") == 0, "api_key_id should preserve trailing space");
    TEST_ASSERT(std::strcmp(config.account_id, "ACC123   ") == 0, "account_id should preserve trailing space");

    std::remove(temp_file);
    TEST_PASS();
}

bool test_parse_config_with_comments() {
    const char* temp_file = "/tmp/test_tz_config_comments.ini";
    FILE* f = std::fopen(temp_file, "w");
    TEST_ASSERT(f != nullptr, "Should create temp file");

    std::fprintf(f, "# This is a comment\n");
    std::fprintf(f, "[tradezero]\n");
    std::fprintf(f, "; Another comment\n");
    std::fprintf(f, "api_key_id = test_key\n");
    std::fprintf(f, "# Comment in middle\n");
    std::fprintf(f, "api_secret_key = test_secret\n");
    std::fprintf(f, "account_id = ACC123\n");
    std::fprintf(f, "enabled = 1\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    TEST_ASSERT(result == true, "Should parse config with comments");
    TEST_ASSERT(std::strcmp(config.api_key_id, "test_key") == 0, "api_key_id should match");

    std::remove(temp_file);
    TEST_PASS();
}

bool test_parse_config_with_other_sections() {
    const char* temp_file = "/tmp/test_tz_config_sections.ini";
    FILE* f = std::fopen(temp_file, "w");
    TEST_ASSERT(f != nullptr, "Should create temp file");

    std::fprintf(f, "[other_section]\n");
    std::fprintf(f, "other_key = other_value\n");
    std::fprintf(f, "[tradezero]\n");
    std::fprintf(f, "api_key_id = test_key\n");
    std::fprintf(f, "api_secret_key = test_secret\n");
    std::fprintf(f, "account_id = ACC123\n");
    std::fprintf(f, "enabled = 1\n");
    std::fprintf(f, "[another_section]\n");
    std::fprintf(f, "another_key = another_value\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    TEST_ASSERT(result == true, "Should parse config with other sections");
    TEST_ASSERT(std::strcmp(config.api_key_id, "test_key") == 0, "api_key_id should match");

    std::remove(temp_file);
    TEST_PASS();
}

bool test_parse_incomplete_enabled_config() {
    const char* temp_file = "/tmp/test_tz_config_incomplete.ini";
    FILE* f = std::fopen(temp_file, "w");
    TEST_ASSERT(f != nullptr, "Should create temp file");

    std::fprintf(f, "[tradezero]\n");
    std::fprintf(f, "api_key_id = test_key\n");
    std::fprintf(f, "enabled = 1\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    TEST_ASSERT(result == false, "Should fail for incomplete enabled config");

    std::remove(temp_file);
    TEST_PASS();
}

bool test_parse_empty_file() {
    const char* temp_file = "/tmp/test_tz_config_empty.ini";
    FILE* f = std::fopen(temp_file, "w");
    TEST_ASSERT(f != nullptr, "Should create temp file");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    TEST_ASSERT(result == true, "Should parse empty file");
    TEST_ASSERT(config.enabled == false, "enabled should be false");

    std::remove(temp_file);
    TEST_PASS();
}

bool test_config_initialization() {
    TZConfig config;

    TEST_ASSERT(config.enabled == false, "enabled should be false");
    TEST_ASSERT(config.api_key_id[0] == '\0', "api_key_id should be empty");
    TEST_ASSERT(config.api_secret_key[0] == '\0', "api_secret_key should be empty");
    TEST_ASSERT(config.account_id[0] == '\0', "account_id should be empty");
    TEST_PASS();
}

// ============================================================================
// Main Test Runner
// ============================================================================

int main() {
    int passed = 0;
    int failed = 0;

    std::printf("Running TradeZero Config Tests...\n\n");

    if (test_config_initialization()) passed++; else failed++;
    if (test_load_nonexistent_file()) passed++; else failed++;
    if (test_parse_valid_config()) passed++; else failed++;
    if (test_parse_disabled_config()) passed++; else failed++;
    if (test_parse_config_with_whitespace()) passed++; else failed++;
    if (test_parse_config_with_comments()) passed++; else failed++;
    if (test_parse_config_with_other_sections()) passed++; else failed++;
    if (test_parse_incomplete_enabled_config()) passed++; else failed++;
    if (test_parse_empty_file()) passed++; else failed++;

    std::printf("\n========================================\n");
    std::printf("Test Results: %d passed, %d failed\n", passed, failed);
    std::printf("========================================\n");

    return (failed == 0) ? 0 : 1;
}
