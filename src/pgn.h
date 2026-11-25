// PGN (Portable Game Notation): the standard text format for saving whole games.
//
//   [Event "Casual game"]
//   [White "Pebble"]
//   [Black "Me"]
//   [Result "0-1"]
//   [TimeControl "300+3"]
//
//   1. e4 { [%clk 0:05:01] [%emt 0:00:02] } e5 { [%clk 0:04:58] [%emt 0:00:05] } 2. Nf3 ... 0-1
//
// Time uses the conventions other chess software reads: the TimeControl tag ("seconds+increment"), [%clk] for
// the clock after each move (what Lichess and Chess.com export) and [%emt] for the time spent on it. Engine
// analysis uses [%eval] and the standard numeric annotation glyphs ($2 = ?, $4 = ??, $6 = ?!).
#ifndef CHESS_FROM_SCRATCH_PGN_H
#define CHESS_FROM_SCRATCH_PGN_H

#include <optional>
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

// A time control with a fixed base time and a per-move increment (Fischer), as written in the PGN TimeControl
// tag: "300+3" is 5 minutes plus 3 seconds a move, "600" is 10 minutes flat.
struct TimeControl {
    int baseMs = 0;
    int incrementMs = 0;

    std::string toPgnTag() const;
    // nullopt for "-", "?", anything malformed, and forms this app doesn't play (moves/seconds, sandglass).
    static std::optional<TimeControl> fromPgnTag(std::string_view tag);
    bool operator==(const TimeControl&) const = default;
};

// Extra information attached to one move when writing PGN.
struct MoveAnnotation {
    int nag = 0;                // numeric annotation glyph, e.g. 2 for "?", 0 for none
    std::string eval;           // the [%eval] value, e.g. "0.35" or "#-3"; empty for none
    std::string comment;        // free text shown after the move
    std::vector<Move> variation; // an alternative to this move, starting from the position before it
};

struct PgnWriteOptions {
    // Declared so that a braced list of tags, as in toPgn(game, {{"White", "Me"}}), can only mean the PgnTags
    // overload. Without it GCC finds the call ambiguous, since a braced list could also build these options.
    PgnWriteOptions() = default;

    PgnTags tags; // added to, or overriding, the standard tags
    // Writes the TimeControl tag and, from the recorded think times, the [%clk] clock after each move.
    std::optional<TimeControl> timeControl;
    bool clocks = true;  // [%clk], when there's a time control
    bool elapsed = true; // [%emt], when think times were recorded
    // One entry per move (or fewer; missing ones mean no annotation).
    std::vector<MoveAnnotation> annotations;
};

struct PgnGame {
    PgnTags tags;       // in file order
    Game game;          // every move replayed and checked for legality
    std::string result; // "1-0", "0-1", "1/2-1/2" or "*" (unknown or unfinished)
    std::optional<TimeControl> timeControl; // from the TimeControl tag
    // The [%clk] value after each move, parallel to game.moves(); nullopt where the file didn't say.
    std::vector<std::optional<int>> clocks;

    // The value of a tag, or "" if it's missing.
    std::string tag(std::string_view name) const;
};

// Writes one game. The seven standard tags (Event, Site, Date, Round, White, Black, Result) come first, filled
// with "?" when not given. A game that didn't start from the normal position gets SetUp and FEN tags; a finished
// game gets a Termination tag ("normal" or "time forfeit"). Lines are wrapped at 80 columns.
std::string toPgn(const Game& game, const PgnWriteOptions& options);
std::string toPgn(const Game& game, const PgnTags& tags = {});

// Several games as one PGN file (a "database"), separated by blank lines, ready for other chess software.
std::string joinPgn(const std::vector<std::string>& games);

// Reads every game in the text. Comments ({...} and ; to end of line), variations in parentheses, numeric
// annotation glyphs ($1) and move-number styles (1. / 1... / 1.e4) are all handled; variations are skipped.
// Think times come from [%emt] comments, or, if a game only has [%clk], from the change in the clock (plus the
// increment from the TimeControl tag).
// A decisive result on a game that isn't over by the rules is recorded with Game::resign, and "1/2-1/2" with
// Game::agreeDraw, so the Game's status matches. When the Termination tag says the game ended on time ("time
// forfeit", also Lichess's and Chess.com's wording) Game::timeout is used instead, for a drawn flag too.
// Throws PgnError on an illegal or unreadable move, a bad FEN tag or broken syntax.
std::vector<PgnGame> parsePgn(std::string_view text);

// The PGN result token for a game's current status.
std::string resultToken(const GameStatus& status);

// "H:MM:SS", with a fraction of a second only when needed ("0:04:58.3"): the format of [%clk] and [%emt].
std::string formatClock(int ms);
// Parses that format back. nullopt if malformed.
std::optional<int> parseClock(std::string_view text);

#endif // CHESS_FROM_SCRATCH_PGN_H
