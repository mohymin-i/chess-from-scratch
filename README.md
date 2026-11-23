# Chess from Scratch

[![CI](https://github.com/mohymin-i/chess-from-scratch/actions/workflows/ci.yml/badge.svg)](https://github.com/mohymin-i/chess-from-scratch/actions/workflows/ci.yml)

Built entirely by Mohymin Islam, computer science student at the University of Michigan. 

## Progress

The chess engine is in place; the graphical game comes next.

- **Rules:** bitboard board representation, FEN import/export, legal move generation (verified with perft against
  published counts), make/unmake, Zobrist hashing, and every way a game can end (checkmate, stalemate, the 50-move
  rule, threefold repetition, insufficient material).
- **Engine:** alpha-beta search with quiescence, a transposition table, move ordering and pruning, and a hand-tuned
  evaluation.
- **Bots:** six difficulty levels with different playing styles, from a beginner-friendly but still tricky level 1
  to full strength. See [BOTS.md](BOTS.md).

See [ROADMAP.md](ROADMAP.md) for the full plan.

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

Deep perft and the bot-vs-bot strength games are labeled `slow` and take about a minute. Skip them with
`ctest --test-dir build -LE slow`.

Every push and pull request runs the tests on macOS and Linux, with and without sanitizers, through GitHub Actions.

