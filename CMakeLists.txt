cmake_minimum_required(VERSION 4.2)
project(chess_from_scratch)

set(CMAKE_CXX_STANDARD 14)

include_directories(.)

add_executable(chess_from_scratch
        board.cpp
        main.cpp
        board.h
        piece.cpp)
