# Chess from Scratch

[![CI](https://github.com/mohymin-i/chess-from-scratch/actions/workflows/ci.yml/badge.svg)](https://github.com/mohymin-i/chess-from-scratch/actions/workflows/ci.yml)

Built entirely by Mohymin Islam, computer science student at the University of Michigan. 

## Progress

As of right now, this project is in its infancy. I am currently working on implementing the core game with two players first.

The board representation (bitboards for all 12 piece types) and its test suite are in place, and positions can be loaded from and saved to FEN. Move generation and the rest of the rules come next; see [ROADMAP.md](ROADMAP.md) for the full plan.

## Running the tests

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

To also catch memory errors and undefined behavior, build a second copy with sanitizers:

```sh
cmake -S . -B build-asan -DCHESS_SANITIZE=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

To run a subset, pass part of a test name: `./build/chess_tests board_place`.

Every push and pull request runs the tests on macOS and Linux, with and without sanitizers, through GitHub Actions.

