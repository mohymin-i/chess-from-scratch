// Post-game review: for every move, what the engine would have played, how much the actual move cost, and a
// label (best ... blunder). Plus an accuracy score per player and the positions worth practicing.
//
// Losses are measured in win probability, not raw centipawns, so dropping from +8 to +5 (still completely winning)
// isn't punished like dropping from +1 to -2. The win-probability curve and the accuracy formula follow the
// approach Lichess has published; the classification thresholds are this project's own.
//
// The per-move numbers (centipawn loss, whether the engine's first choice was played, think time) are kept in
// full because the planned lightweight fair-play check will be built on them.
#ifndef CHESS_FROM_SCRATCH_ANALYSIS_H
#define CHESS_FROM_SCRATCH_ANALYSIS_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "game.h"
#include "search.h"

enum class MoveQuality : std::uint8_t {
    Forced,     // the only legal move
    Best,       // the engine's choice
    Excellent,  // almost as good
    Good,
    Inaccuracy,
    Mistake,
    Blunder,
};

const char* toString(MoveQuality quality);

struct MoveAnalysis {
    Move played;
    Move best;                 // the engine's choice in this position
    std::vector<Move> bestLine; // the engine's expected continuation, starting with `best`
    // Evaluations in centipawns from the mover's point of view. Forced mates are converted to large values
    // (see analysis::mateToCentipawns) so the numbers stay comparable.
    int evalBefore = 0;   // with best play from here
    int evalAfter = 0;    // after the move actually played
    int centipawnLoss = 0; // evalBefore - evalAfter, never negative
    double winChanceBefore = 50; // 0-100, from the mover's point of view
    double winChanceAfter = 50;
    double accuracy = 100; // 0-100 for this move
    MoveQuality quality = MoveQuality::Best;
    std::optional<int> thinkTimeMs;
};

struct PlayerSummary {
    double accuracy = 100; // 0-100 over the whole game
    int averageCentipawnLoss = 0;
    int inaccuracies = 0;
    int mistakes = 0;
    int blunders = 0;
    int movesAnalyzed = 0;     // excluding forced moves
    double engineMatchRate = 0; // share of analyzed moves that matched the engine's first choice, 0-1
};

struct GameAnalysis {
    std::vector<MoveAnalysis> moves; // one per move of the game, in order
    PlayerSummary white;
    PlayerSummary black;

    const PlayerSummary& player(Color c) const { return c == Color::White ? white : black; }
};

struct AnalysisSettings {
    int depth = 12;
    std::uint64_t nodesPerPosition = 0; // 0 means no node limit
    int hashMegabytes = 64;
    // Called after each position is analyzed with (done, total). Return false to cancel; the moves analyzed so
    // far are returned.
    std::function<bool(int, int)> progress;
};

// A position to practice: one where a player made a mistake or blunder and a clearly better move existed.
struct TrainingPosition {
    std::string fen;
    Move played;
    Move best;
    MoveQuality quality;
    int centipawnLoss;
    int moveIndex; // into Game::moves()
};

namespace analysis {
// Win probability (0-100) for an evaluation in centipawns.
double winChance(int centipawns);
// Accuracy (0-100) of a move that took the mover's win chance from `before` to `after`.
double moveAccuracy(double before, double after);
// Search scores with forced mates become +/- (10000 - distance), so a mate is always worth more than any
// material advantage and a quicker mate more than a slower one.
int mateToCentipawns(int score);
// The label for a move, from the win chance it lost.
MoveQuality classify(double winChanceLost, bool playedBest);
} // namespace analysis

GameAnalysis analyzeGame(const Game& game, const AnalysisSettings& settings = {});

// Mistakes and blunders by `player` worth practicing, in game order.
std::vector<TrainingPosition> mistakesToPractice(const Game& game, const GameAnalysis& review, Color player);

#endif // CHESS_FROM_SCRATCH_ANALYSIS_H
