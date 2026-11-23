cmake_minimum_required(VERSION 3.20)
project(chess_from_scratch CXX)

set(CMAKE_CXX_STANDARD 20) # C++20 for <bit> (std::popcount, std::countr_zero)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # lets VS Code IntelliSense see the real compiler flags

option(CHESS_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if (CHESS_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Game logic lives in a library so anything built later and the tests share exactly the same code.
add_library(chess_core board.cpp fen.cpp piece.cpp)
target_include_directories(chess_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---- Tests: run with `ctest --test-dir <build dir> --output-on-failure` ----
enable_testing()

add_executable(chess_tests
        tests/test_main.cpp
        tests/square_tests.cpp
        tests/piece_tests.cpp
        tests/board_tests.cpp
        tests/fen_tests.cpp)
target_link_libraries(chess_tests PRIVATE chess_core)

add_test(NAME unit_tests COMMAND chess_tests)
