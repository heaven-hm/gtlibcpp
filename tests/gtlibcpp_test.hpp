// Minimal test harness for the gtlibcpp regression suite. Each test
// runs in its own try/catch and reports a per-test pass / fail so
// ctest sees the actual count, not a single "tests: N passed" string.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

namespace gtlibcpp_test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> r;
    return r;
}

inline int& exit_code() {
    static int e = 0;
    return e;
}

inline int run_all() {
    int passed = 0;
    int failed = 0;
    for (const auto& c : registry()) {
        try {
            c.fn();
            std::printf("  [PASS] %s\n", c.name);
            ++passed;
        } catch (const std::exception& e) {
            std::printf("  [FAIL] %s: %s\n", c.name, e.what());
            ++failed;
            exit_code() = 1;
        } catch (...) {
            std::printf("  [FAIL] %s: unknown exception\n", c.name);
            ++failed;
            exit_code() = 1;
        }
    }
    std::printf("%d passed, %d failed\n", passed, failed);
    return exit_code();
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        registry().push_back({name, fn});
    }
};

inline int require_failed(const char* expr, const char* file, int line) {
    std::printf("    assertion failed: %s (%s:%d)\n", expr, file, line);
    throw std::runtime_error(std::string("assertion failed: ") + expr);
}

} // namespace gtlibcpp_test

#define GTLIBCPP_TEST(name)                                                 \
    static void gtlibcpp_test_##name();                                      \
    static ::gtlibcpp_test::Registrar gtlibcpp_test_reg_##name(              \
        #name, &gtlibcpp_test_##name);                                       \
    static void gtlibcpp_test_##name()

#define GTLIBCPP_REQUIRE(cond)                                               \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ::gtlibcpp_test::require_failed(#cond, __FILE__, __LINE__);      \
        }                                                                    \
    } while (0)

#define GTLIBCPP_REQUIRE_EQ(a, b) GTLIBCPP_REQUIRE((a) == (b))
#define GTLIBCPP_REQUIRE_NE(a, b) GTLIBCPP_REQUIRE((a) != (b))
#define GTLIBCPP_REQUIRE_TRUE(cond) GTLIBCPP_REQUIRE(cond)
#define GTLIBCPP_REQUIRE_FALSE(cond) GTLIBCPP_REQUIRE(!(cond))

#define GTLIBCPP_RUN_ALL() ::gtlibcpp_test::run_all()
