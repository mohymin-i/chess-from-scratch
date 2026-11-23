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
add_library(chess_core board.cpp fen.cpp move.cpp movegen.cpp game.cpp san.cpp pgn.cpp eval.cpp search.cpp bot.cpp analysis.cpp savegame.cpp gameexport.cpp piece.cpp)
target_include_directories(chess_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---- Tests: run with `ctest --test-dir <build dir> --output-on-failure` ----
enable_testing()

add_executable(chess_tests
        tests/test_main.cpp
        tests/square_tests.cpp
        tests/piece_tests.cpp
        tests/board_tests.cpp
        tests/fen_tests.cpp
        tests/move_tests.cpp
        tests/attack_tests.cpp
        tests/movegen_tests.cpp
        tests/game_tests.cpp
        tests/engine_tests.cpp
        tests/notation_tests.cpp
        tests/analysis_tests.cpp
        tests/savegame_tests.cpp
        tests/edge_case_tests.cpp
        tests/export_tests.cpp)
target_link_libraries(chess_tests PRIVATE chess_core)

add_test(NAME unit_tests COMMAND chess_tests)

# Deep perft takes a few seconds, so it's a separate test labeled "slow".
# Skip it with `ctest -LE slow`; run only it with `ctest -L slow`.
add_executable(chess_perft_deep tests/test_main.cpp tests/perft_deep.cpp)
target_link_libraries(chess_perft_deep PRIVATE chess_core)
add_test(NAME perft_deep COMMAND chess_perft_deep)
set_tests_properties(perft_deep PROPERTIES LABELS slow)

# Bot-vs-bot games that check each difficulty level beats the one below it. Also labeled "slow".
add_executable(chess_bot_matches tests/test_main.cpp tests/bot_matches.cpp)
target_link_libraries(chess_bot_matches PRIVATE chess_core)
add_test(NAME bot_matches COMMAND chess_bot_matches)
set_tests_properties(bot_matches PROPERTIES LABELS slow)
