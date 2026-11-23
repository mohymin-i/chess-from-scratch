// Exporting games and positions so players can analyze and compare them in other chess software, using the
// formats that software reads:
//
//   - Games: PGN, with [%clk] / [%emt] for time, [%eval] for engine evaluations, standard annotation glyphs
//     ($6 ?!, $2 ?, $4 ??), the engine's better line as a variation, and many games in one file.
//   - Positions: FEN for single positions and EPD for sets of positions, with the standard operations
//     (bm best move, am move to avoid, ce evaluation, dm mate distance, id, c0 comment).
//   - Comparison: a CSV table with one row per game (result, accuracy, mistakes, time used) for spreadsheets.
#ifndef CHESS_FROM_SCRATCH_GAMEEXPORT_H
#define CHESS_FROM_SCRATCH_GAMEEXPORT_H

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "analysis.h"
#include "pgn.h"

struct GameExportOptions {
    PgnTags tags;                          // player names, event, date (see dateTags), ...
    std::optional<TimeControl> timeControl; // adds the TimeControl tag and [%clk] clocks
    const GameAnalysis* analysis = nullptr; // a review from analyzeGame, to include its findings
    bool clocks = true;    // [%clk]
    bool elapsed = true;   // [%emt]
    bool evals = true;     // [%eval] after every move (needs analysis)
    bool judgments = true; // for inaccuracies and worse: the glyph, "Mistake. Nf3 was best." and the better line
    int variationPlies = 6; // how much of the better line to include
};

// One game as PGN, with whatever the options ask for.
std::string exportGamePgn(const Game& game, const GameExportOptions& options = {});

// An evaluation in the [%eval] format, from white's point of view: "0.35", "-1.20", "#3" (white mates in 3) or
// "#-2". Takes centipawns where forced mates are encoded as by analysis::mateToCentipawns. Empty for a
// position that's already checkmate.
std::string formatEval(int whiteCentipawns);

// The Date, UTCDate and UTCTime tags for a moment, e.g. a game's start: "2026.11.23", "2026.11.23", "18:04:59".
PgnTags dateTags(std::chrono::system_clock::time_point when);

// ---------- Positions ----------

// The FEN of the starting position and of the position after every move.
std::vector<std::string> positionFens(const Game& game);

using EpdOperations = std::vector<std::pair<std::string, std::string>>;

// One EPD line: the first four FEN fields followed by operations, e.g. `bm Nf3; id "game 1, move 12";`.
// Values that contain spaces or semicolons are quoted.
std::string toEpd(const Board& board, const EpdOperations& operations = {});

struct EpdExportOptions {
    std::string id = "game";                // prefix for each position's id
    const GameAnalysis* analysis = nullptr; // adds bm, ce / dm and, for mistakes, am
    bool onlyMistakes = false;              // just the positions before mistakes and blunders (needs analysis)
    std::optional<Color> player;            // only positions where this side was to move
};

// Positions from a game, one EPD line each, for position-based tools and training sets.
std::string exportPositionsEpd(const Game& game, const EpdExportOptions& options = {});

// ---------- Comparing games ----------

struct GameSummaryInput {
    const Game* game = nullptr;
    PgnTags tags;                           // Event, Date, White, Black, ...
    std::optional<TimeControl> timeControl;
    const GameAnalysis* analysis = nullptr; // optional: adds accuracy and mistake columns
};

// A CSV table (RFC 4180) with a header row and one row per game: event, date, players, result, how it ended,
// time control, length, and per player the accuracy, average centipawn loss, inaccuracies, mistakes,
// blunders and average seconds per move. Opens directly in any spreadsheet.
std::string exportSummaryCsv(const std::vector<GameSummaryInput>& games);

// A short description of how a game ended: "checkmate", "resignation", "time forfeit", "threefold
// repetition", ..., or "" if it's still going.
std::string terminationText(const GameStatus& status);

#endif // CHESS_FROM_SCRATCH_GAMEEXPORT_H
