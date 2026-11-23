// PGN (Portable Game Notation): the standard text format for saving whole games.
//
//   [Event "Casual game"]
//   [White "Pebble"]
//   [Black "Me"]
//   [Result "0-1"]
//
//   1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 0-1
#ifndef CHESS_FROM_SCRATCH_PGN_H
#define CHESS_FROM_SCRATCH_PGN_H

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "game.h"

using PgnTags = std::vector<std::pair<std::string, std::string>>;

// Thrown by parsePgn. what() names the game number, the line and the problem.
class PgnError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct PgnGame {
    PgnTags tags;       // in file order
    Game game;          // every move replayed and checked for legality
    std::string result; // "1-0", "0-1", "1/2-1/2" or "*" (unknown or unfinished)

    // The value of a tag, or "" if it's missing.
    std::string tag(std::string_view name) const;
};

// Writes one game. The seven standard tags (Event, Site, Date, Round, White, Black, Result) always come first, filled
// with "?" when not given; `tags` adds to or overrides them. A game that didn't start from the normal position gets
// SetUp and FEN tags so it loads back correctly. Recorded think times are written as {[%emt H:MM:SS]} comments
// after each move. Lines are wrapped at 80 columns.
std::string toPgn(const Game& game, const PgnTags& tags = {});

// Reads every game in the text. Comments ({...} and ; to end of line), variations in parentheses, numeric
// annotation glyphs ($1) and move-number styles (1. / 1... / 1.e4) are all handled; variations are skipped.
// {[%emt H:MM:SS]} comments after a move set that move's think time.
// A decisive result on a game that isn't over by the rules (a resignation, a loss on time) is recorded with
// Game::resign, and "1/2-1/2" with Game::agreeDraw, so the Game's status matches the file.
// Throws PgnError on an illegal or unreadable move, a bad FEN tag or broken syntax.
std::vector<PgnGame> parsePgn(std::string_view text);

// The PGN result token for a game's current status.
std::string resultToken(const GameStatus& status);

#endif // CHESS_FROM_SCRATCH_PGN_H
