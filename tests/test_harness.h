// test_harness.h — a minimal Catch2-style harness with no external dependency.
//
// The workspace already standardizes on this (ra9fael.volume, cursorwrap, bright)
// instead of pulling in Catch2 or gtest: the plugin's own rule is that nothing
// beyond Win32 and the C++ standard library leaks into the build, and a second
// test framework would fork the CI and local commands. One process runs every
// test; failures print and count.
#pragma once

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace rename_test {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> tests;
    return tests;
}

inline int& failures()
{
    static int count = 0;
    return count;
}

template <typename Fn>
inline bool registerTest(const char* name, Fn fn)
{
    registry().push_back({name, std::function<void()>(fn)});
    return true;
}

inline void reportFailure(const char* file, int line, const char* expr)
{
    ++failures();
    std::printf("[ FAIL ] %s:%d: %s\n", file, line, expr);
}

inline int runAll()
{
    for (const TestCase& test : registry())
    {
        std::printf("[ RUN  ] %s\n", test.name);
        std::fflush(stdout);
        const int before = failures();
        test.fn();
        if (failures() == before)
            std::printf("[  OK  ] %s\n", test.name);
    }
    const int total = failures();
    if (total == 0)
        std::printf("[ PASS ] all %zu tests\n", registry().size());
    else
        std::printf("[ FAIL ] %d failed check(s) across %zu tests\n", total, registry().size());
    return total == 0 ? 0 : 1;
}

} // namespace rename_test

#define TEST_CASE_IMPL2(name, counter)                                                        \
    static void name##_##counter();                                                           \
    static const bool name##_##counter##_registered =                                         \
        ::rename_test::registerTest(#name " #" #counter, name##_##counter);                    \
    static void name##_##counter()

#define TEST_CASE_IMPL(name, counter) TEST_CASE_IMPL2(name, counter)
#define TEST_CASE(name) TEST_CASE_IMPL(name, __COUNTER__)

#define CHECK(expr)                                                                           \
    do {                                                                                      \
        if (!(expr))                                                                          \
            ::rename_test::reportFailure(__FILE__, __LINE__, #expr);                           \
    } while (false)

#define CHECK_EQ(actual, expected)                                                            \
    do {                                                                                      \
        const auto& a_ = (actual);                                                            \
        const auto& e_ = (expected);                                                          \
        if (!(a_ == e_))                                                                      \
            ::rename_test::reportFailure(__FILE__, __LINE__, #actual " == " #expected);        \
    } while (false)

#define CHECK_NE(actual, unexpected)                                                          \
    do {                                                                                      \
        const auto& a_ = (actual);                                                            \
        const auto& u_ = (unexpected);                                                        \
        if (a_ == u_)                                                                         \
            ::rename_test::reportFailure(__FILE__, __LINE__, #actual " != " #unexpected);      \
    } while (false)
