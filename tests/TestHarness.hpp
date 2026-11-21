#pragma once

#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace test {

class Failure : public std::runtime_error {
public:
    Failure(const char* file, int line, const std::string& message)
        : std::runtime_error(std::string(file) + ":" + std::to_string(line) + ": " + message) {}
};

using TestFunction = void (*)();

struct TestCase {
    std::string Name;
    TestFunction Function = nullptr;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> registry;
    return registry;
}

class Registrar {
public:
    Registrar(const char* name, TestFunction function) {
        Registry().push_back(TestCase{name, function});
    }
};

inline int RunAll() {
    int failures = 0;
    std::size_t passed = 0;
    for (const auto& test : Registry()) {
        try {
            test.Function();
            ++passed;
            std::cout << "[PASS] " << test.Name << '\n';
        } catch (const Failure& failure) {
            ++failures;
            std::cout << "[FAIL] " << test.Name << "\n       " << failure.what() << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "[FAIL] " << test.Name << "\n       unexpected exception: "
                      << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cout << "[FAIL] " << test.Name << "\n       unknown exception\n";
        }
    }
    std::cout << "\n" << passed << " passed, " << failures << " failed, "
              << Registry().size() << " total\n";
    return failures == 0 ? 0 : 1;
}

} // namespace test

#define TEST_CASE(name)                              \
    static void name();                              \
    static ::test::Registrar name##_registrar(#name, &name); \
    static void name()

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            throw ::test::Failure(__FILE__, __LINE__,                        \
                                  "CHECK failed: " #condition);              \
        }                                                                    \
    } while (false)

#define CHECK_EQ(expected, actual)                                           \
    do {                                                                     \
        const auto expectedValue = (expected);                               \
        const auto actualValue = (actual);                                   \
        if (!(expectedValue == actualValue)) {                               \
            throw ::test::Failure(__FILE__, __LINE__,                        \
                                  "CHECK_EQ failed: " #expected " != " #actual); \
        }                                                                    \
    } while (false)

#define CHECK_NEAR(expected, actual, tolerance)                              \
    do {                                                                     \
        const double expectedValue = (expected);                             \
        const double actualValue = (actual);                                 \
        const double toleranceValue = (tolerance);                           \
        if (!(actualValue >= expectedValue - toleranceValue &&               \
              actualValue <= expectedValue + toleranceValue)) {              \
            throw ::test::Failure(__FILE__, __LINE__,                        \
                                  "CHECK_NEAR failed: " #expected " vs " #actual); \
        }                                                                    \
    } while (false)

#define CHECK_THROWS(expression)                                             \
    do {                                                                     \
        bool thrown = false;                                                 \
        try {                                                                \
            (void)(expression);                                              \
        } catch (...) {                                                      \
            thrown = true;                                                   \
        }                                                                    \
        if (!thrown) {                                                       \
            throw ::test::Failure(__FILE__, __LINE__,                        \
                                  "CHECK_THROWS failed: " #expression);      \
        }                                                                    \
    } while (false)

#define CHECK_THROWS_AS(expression, ExceptionType)                           \
    do {                                                                     \
        bool thrown = false;                                                 \
        try {                                                                \
            (void)(expression);                                              \
        } catch (const ExceptionType&) {                                     \
            thrown = true;                                                   \
        } catch (...) {                                                      \
            throw ::test::Failure(__FILE__, __LINE__,                        \
                                  "CHECK_THROWS_AS caught wrong type: " #expression); \
        }                                                                    \
        if (!thrown) {                                                       \
            throw ::test::Failure(__FILE__, __LINE__,                        \
                                  "CHECK_THROWS_AS failed: " #expression);   \
        }                                                                    \
    } while (false)
