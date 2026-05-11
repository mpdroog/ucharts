// Common test infrastructure with quiet/verbose support
// Usage: test binaries are quiet by default, pass -v for verbose output
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>

static int g_tests_run = 0;
static int g_tests_passed = 0;
static bool g_verbose = false;

static void test_init(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "-v") == 0) {
            g_verbose = true;
        }
    }
}

#define TEST(name) static void test_##name()

#define RUN_TEST(name) do { \
    g_tests_run++; \
    if (g_verbose) std::printf("Running %s... ", #name); \
    test_##name(); \
    g_tests_passed++; \
    if (g_verbose) std::printf("PASSED\n"); \
} while(0)

// Use _Exit to skip static destructor calls on assertion failure
// This avoids mutex crashes from WebSocket cleanup during error exit
#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::printf("FAILED: %s != %s (line %d)\n", #a, #b, __LINE__); \
        std::fflush(stdout); \
        _Exit(1); \
    } \
} while(0)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (std::fabs((a) - (b)) > (eps)) { \
        std::printf("FAILED: %s (%.4f) != %s (%.4f) (line %d)\n", #a, (double)(a), #b, (double)(b), __LINE__); \
        std::fflush(stdout); \
        _Exit(1); \
    } \
} while(0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::printf("FAILED: %s is false (line %d)\n", #cond, __LINE__); \
        std::fflush(stdout); \
        _Exit(1); \
    } \
} while(0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

#define ASSERT_STREQ(a, b) do { \
    if (std::strcmp((a), (b)) != 0) { \
        std::printf("FAILED: \"%s\" != \"%s\" (line %d)\n", (a), (b), __LINE__); \
        std::fflush(stdout); \
        _Exit(1); \
    } \
} while(0)

static void test_summary() {
    if (g_verbose) {
        std::printf("\n%d/%d tests passed.\n", g_tests_passed, g_tests_run);
    }
}
