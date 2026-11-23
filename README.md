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
- **Saved games and review:** SAN and PGN (with per-move think times), and a post-game review with accuracy scores,
  move labels from "best" to "blunder", and your mistakes turned into practice positions.

- **Export for analysis:** games, positions and comparison tables in the formats other chess software reads (see
  below).

See [ROADMAP.md](ROADMAP.md) for the full plan.

## Export formats

Games can be exported for analysis in Lichess, ChessBase, En Croissant, Scid or any other chess program, and
compared side by side in a spreadsheet:

| What | Format | Details |
|---|---|---|
| Games | PGN | Standard tags, `TimeControl` (`"300+3"`), `Termination` (`"normal"`, `"time forfeit"`), `Date`/`UTCDate`/`UTCTime`; `SetUp`/`FEN` for custom starts; many games in one file |
| Time | PGN comments | `[%clk 0:04:58]` clock after each move and `[%emt 0:00:05]` time spent; both are read back on import, and Lichess-style files with only `[%clk]` get their move times worked out |
| Analysis | PGN | `[%eval 0.35]` / `[%eval #-3]` from white's side, `$6` `$2` `$4` for ?! ? ??, a comment like "Mistake. Nf3 was best." and the better line as a variation |
| Positions | FEN, EPD | FEN for any move; EPD sets with `bm`, `am`, `ce`, `dm`, `id`, `c0`, `hmvc`, `fmvn` (for example, just the positions before your mistakes) |
| Comparison | CSV | One row per game: players, result, how it ended, time control, length, accuracy, average centipawn loss, mistakes and seconds per move for each side |

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

