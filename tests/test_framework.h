// A tiny self-contained test framework, so the project needs no outside testing library.
//
//   TEST(some_name) { CHECK(x); CHECK_EQ(actual, expected); CHECK_THROWS(expr, std::out_of_range); }
//
// A failed check stops that test and is reported; the other tests still run.
#ifndef CHESS_TEST_FRAMEWORK_H
#define CHESS_TEST_FRAMEWORK_H

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace chess_test {

struct TestCase {
    const char* name;
    void (*fn)();
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

struct Failure {
    std::string message;
};

[[noreturn]] inline void fail(const char* file, int line, const std::string& what) {
    throw Failure{std::string(file) + ":" + std::to_string(line) + ": " + what};
}

template <typename T>
std::string describe(const T& value) {
    std::ostringstream out;
    if constexpr (std::is_same_v<T, std::uint64_t>) {
        out << "0x" << std::hex << value; // bitboards are easier to read in hex
    } else if constexpr (std::is_enum_v<T>) {
        out << static_cast<long long>(value);
    } else if constexpr (std::is_same_v<T, char>) {
        out << '\'' << value << '\'';
    } else if constexpr (std::is_arithmetic_v<T> || std::is_convertible_v<const T&, std::string_view>) {
        out << value;
    } else {
        out << "<value>";
    }
    return out.str();
}

} // namespace chess_test

#define TEST(name)                                                       \
    static void name();                                                  \
    static const ::chess_test::Registrar name##_registrar(#name, &name); \
    static void name()

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) ::chess_test::fail(__FILE__, __LINE__, "CHECK(" #cond ") failed"); \
    } while (0)

#define CHECK_EQ(actual, expected)                                                             \
    do {                                                                                       \
        const auto& actual_ = (actual);                                                        \
        const auto& expected_ = (expected);                                                    \
        if (!(actual_ == expected_)) {                                                         \
            ::chess_test::fail(__FILE__, __LINE__,                                             \
                               "CHECK_EQ(" #actual ", " #expected ") failed\n         got:      " + \
                                   ::chess_test::describe(actual_) +                           \
                                   "\n         expected: " + ::chess_test::describe(expected_)); \
        }                                                                                      \
    } while (0)

#define CHECK_THROWS(expr, ExceptionType)                                                                \
    do {                                                                                                 \
        bool thrown_ = false;                                                                            \
        try {                                                                                            \
            (void)(expr);                                                                                \
        } catch (const ExceptionType&) {                                                                 \
            thrown_ = true;                                                                              \
        }                                                                                                \
        if (!thrown_)                                                                                    \
            ::chess_test::fail(__FILE__, __LINE__, "CHECK_THROWS(" #expr ") did not throw " #ExceptionType); \
    } while (0)

#endif // CHESS_TEST_FRAMEWORK_H
