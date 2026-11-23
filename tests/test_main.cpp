// Runs every registered test. Pass a word to run only the tests whose names contain it:
//   ./chess_tests            run everything
//   ./chess_tests square     run only tests with "square" in the name
#include <cstring>
#include <exception>
#include <iostream>

#include "test_framework.h"

int main(int argc, char* argv[]) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0;
    int failed = 0;

    for (const auto& test : chess_test::registry()) {
        if (filter && std::strstr(test.name, filter) == nullptr) continue;
        try {
            test.fn();
            ++passed;
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const chess_test::Failure& failure) {
            ++failed;
            std::cout << "[FAIL] " << test.name << "\n       " << failure.message << '\n';
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[FAIL] " << test.name << "\n       unexpected exception: " << e.what() << '\n';
        } catch (...) {
            ++failed;
            std::cout << "[FAIL] " << test.name << "\n       unexpected non-standard exception\n";
        }
    }

    std::cout << '\n' << passed << " passed, " << failed << " failed\n";
    if (passed + failed == 0) {
        std::cout << "No tests matched.\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
