// test_tradezero_config.cpp - Tests for TradeZero INI config parsing
// Compile: See Makefile test target

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include "test_common.h"

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

TEST(config_initialization) {
    TZConfig config;
    ASSERT_TRUE(config.enabled == false);
    ASSERT_TRUE(config.api_key_id[0] == '\0');
    ASSERT_TRUE(config.api_secret_key[0] == '\0');
    ASSERT_TRUE(config.account_id[0] == '\0');
}

TEST(load_nonexistent_file) {
    TZConfig config;
    bool result = load_tradezero_config("/tmp/nonexistent_file_12345.ini", config);
    ASSERT_TRUE(result == false);
}

TEST(parse_valid_config) {
    const char* temp_file = "/tmp/test_tz_config.ini";
    FILE* f = std::fopen(temp_file, "w");
    ASSERT_TRUE(f != nullptr);

    std::fprintf(f, "[tradezero]\n");
    std::fprintf(f, "api_key_id = test_key_123\n");
    std::fprintf(f, "api_secret_key = test_secret_456\n");
    std::fprintf(f, "account_id = ACC789\n");
    std::fprintf(f, "enabled = 1\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    ASSERT_TRUE(result == true);
    ASSERT_STREQ(config.api_key_id, "test_key_123");
    ASSERT_STREQ(config.api_secret_key, "test_secret_456");
    ASSERT_STREQ(config.account_id, "ACC789");
    ASSERT_TRUE(config.enabled == true);

    std::remove(temp_file);
}

TEST(parse_disabled_config) {
    const char* temp_file = "/tmp/test_tz_config_disabled.ini";
    FILE* f = std::fopen(temp_file, "w");
    ASSERT_TRUE(f != nullptr);

    std::fprintf(f, "[tradezero]\n");
    std::fprintf(f, "enabled = 0\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(config.enabled == false);

    std::remove(temp_file);
}

TEST(parse_config_with_whitespace) {
    const char* temp_file = "/tmp/test_tz_config_ws.ini";
    FILE* f = std::fopen(temp_file, "w");
    ASSERT_TRUE(f != nullptr);

    std::fprintf(f, "  [tradezero]  \n");
    std::fprintf(f, "  api_key_id  =  test_key  \n");
    std::fprintf(f, "  api_secret_key=test_secret\n");
    std::fprintf(f, "account_id   =   ACC123   \n");
    std::fprintf(f, "enabled=1\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    ASSERT_TRUE(result == true);
    ASSERT_STREQ(config.api_key_id, "test_key  ");
    ASSERT_STREQ(config.account_id, "ACC123   ");

    std::remove(temp_file);
}

TEST(parse_config_with_comments) {
    const char* temp_file = "/tmp/test_tz_config_comments.ini";
    FILE* f = std::fopen(temp_file, "w");
    ASSERT_TRUE(f != nullptr);

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

    ASSERT_TRUE(result == true);
    ASSERT_STREQ(config.api_key_id, "test_key");

    std::remove(temp_file);
}

TEST(parse_config_with_other_sections) {
    const char* temp_file = "/tmp/test_tz_config_sections.ini";
    FILE* f = std::fopen(temp_file, "w");
    ASSERT_TRUE(f != nullptr);

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

    ASSERT_TRUE(result == true);
    ASSERT_STREQ(config.api_key_id, "test_key");

    std::remove(temp_file);
}

TEST(parse_incomplete_enabled_config) {
    const char* temp_file = "/tmp/test_tz_config_incomplete.ini";
    FILE* f = std::fopen(temp_file, "w");
    ASSERT_TRUE(f != nullptr);

    std::fprintf(f, "[tradezero]\n");
    std::fprintf(f, "api_key_id = test_key\n");
    std::fprintf(f, "enabled = 1\n");
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    ASSERT_TRUE(result == false);

    std::remove(temp_file);
}

TEST(parse_empty_file) {
    const char* temp_file = "/tmp/test_tz_config_empty.ini";
    FILE* f = std::fopen(temp_file, "w");
    ASSERT_TRUE(f != nullptr);
    std::fclose(f);

    TZConfig config;
    bool result = load_tradezero_config(temp_file, config);

    ASSERT_TRUE(result == true);
    ASSERT_TRUE(config.enabled == false);

    std::remove(temp_file);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    test_init(argc, argv);

    RUN_TEST(config_initialization);
    RUN_TEST(load_nonexistent_file);
    RUN_TEST(parse_valid_config);
    RUN_TEST(parse_disabled_config);
    RUN_TEST(parse_config_with_whitespace);
    RUN_TEST(parse_config_with_comments);
    RUN_TEST(parse_config_with_other_sections);
    RUN_TEST(parse_incomplete_enabled_config);
    RUN_TEST(parse_empty_file);

    test_summary();
    return 0;
}
