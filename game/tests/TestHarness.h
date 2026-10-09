#pragma once
#include <cstdio>
#include <functional>
#include <vector>

namespace test
{
    struct Case { const char* name; std::function<void()> body; };
    inline std::vector<Case>& Registry() { static std::vector<Case> cases; return cases; }
    inline int& Failures() { static int failures = 0; return failures; }
    // Optional process-wide setup (engine init) run once before the first case.
    inline std::function<void()>& GlobalSetup() { static std::function<void()> setup; return setup; }
    struct SetupRegistrar { explicit SetupRegistrar(std::function<void()> f) { GlobalSetup() = std::move(f); } };
    struct Registrar { Registrar(const char* n, std::function<void()> b) { Registry().push_back({ n, std::move(b) }); } };
}

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST_CASE(name) \
    static void TEST_CAT(test_fn_, __LINE__)(); \
    static test::Registrar TEST_CAT(test_reg_, __LINE__)(name, TEST_CAT(test_fn_, __LINE__)); \
    static void TEST_CAT(test_fn_, __LINE__)()

#define CHECK(expr) \
    do { if (!(expr)) { ++test::Failures(); std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while (0)
