// Computer opponents.
//
// Every bot runs the same real search (alpha-beta with quiescence, see search.h). None of them just grabs the
// biggest capture in front of it. They differ in:
//   - how deep and how long they think,
//   - how often they settle for a move that's merely good instead of the best one (and how much worse it may be),
//   - their evaluation style (an attacker weighs king safety and activity more than a positional player does),
//   - contempt: how much they avoid drawish repetitions.
//
// Even the weakest bot sees a few moves ahead and resolves every capture sequence, so it doesn't hang pieces or
// miss simple tactics. It plays slightly imprecise moves on purpose, which is what makes it beatable.
#ifndef CHESS_FROM_SCRATCH_BOT_H
#define CHESS_FROM_SCRATCH_BOT_H

#include <atomic>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "game.h"
#include "search.h"

struct BotSettings {
    std::string name;
    std::string description;
    int level = 0;        // 1 (weakest) to 6 (strongest) for the built-in bots
    int approxRating = 0; // rough intended strength, not measured against humans

    // Search limits. Any that is set applies; 0 means no limit for nodes and time.
    int maxDepth = 64;
    std::uint64_t maxNodes = 0;
    int moveTimeMs = 0;

    // Choosing among good moves instead of always the best: moves scoring within `randomMargin` centipawns of
    // the best are candidates, picked with probability proportional to exp(-(best - score) / temperature).
    // A margin of 0 always plays the best move.
    int randomMargin = 0;
    int temperature = 1;
    // Occasionally (probability mistakeChance) the margin widens to mistakeMargin, modeling a lapse.
    double mistakeChance = 0.0;
    int mistakeMargin = 0;
    // How deep the candidate search goes when choosing randomly. Defaults to maxDepth when 0.
    int candidateDepth = 0;
    // Even full-strength bots vary their first moves: for the first `openingVarietyPlies` plies, moves within
    // `openingVarietyMargin` centipawns of the best are allowed.
    int openingVarietyPlies = 0;
    int openingVarietyMargin = 0;

    SearchSettings search;
    int hashMegabytes = 16;

    // Clamps every field into a usable range, so a hand-edited or partly filled-in bot still works.
    BotSettings validated() const;
};

// The built-in bots, weakest first.
const std::vector<BotSettings>& builtInBots();
// Looks up a built-in bot by name (case-insensitive) or by level ("3").
std::optional<BotSettings> findBot(std::string_view nameOrLevel);

// Time to spend on a move under a chess clock: a slice of the remaining time plus most of the increment,
// never more than a quarter of what's left. All values in milliseconds.
int timeForMove(int remainingMs, int incrementMs, int movesToGo = 0);

class Bot {
public:
    // A fixed seed makes the bot's choices reproducible (tests rely on this); by default it's random.
    explicit Bot(const BotSettings& settings, std::optional<std::uint64_t> seed = std::nullopt);

    const BotSettings& settings() const { return settings_; }

    // Picks a move for the side to move. Returns the null move if the game is over or there are no legal moves.
    // If clockMs is given, the bot also stays within a sensible share of that remaining time.
    Move chooseMove(const Game& game, std::optional<int> clockMs = std::nullopt, int incrementMs = 0);

    // Details of the last search (score, depth, nodes, expected line), for an analysis display.
    const SearchResult& lastSearch() const { return last_; }

    // Lets another thread end the current search early; the bot then plays the best move found so far.
    void setStopFlag(const std::atomic<bool>* stop) { stop_ = stop; }

    // Forget what was learned from earlier positions. Call between games.
    void newGame() { searcher_.clear(); }

private:
    SearchLimits limits(std::optional<int> clockMs, int incrementMs) const;

    BotSettings settings_;
    Searcher searcher_;
    std::mt19937_64 rng_;
    SearchResult last_;
    const std::atomic<bool>* stop_ = nullptr;
};

#endif // CHESS_FROM_SCRATCH_BOT_H
