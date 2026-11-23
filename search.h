// The engine's search: iterative-deepening negamax with alpha-beta pruning.
//
//   - Principal variation search (PVS): the first move gets a full window, the rest a null window, and only
//     moves that beat it are re-searched.
//   - Quiescence search: at the horizon, keep resolving captures so the evaluation never stops in the middle of
//     an exchange. Losing captures (by static exchange evaluation) are skipped there.
//   - Transposition table keyed by the Zobrist hash, with mate scores stored relative to the node.
//   - Move ordering: hash move, winning captures by most valuable victim / least valuable attacker, killer moves,
//     then quiet moves by history score, and losing captures last.
//   - Selectivity: check extension, null-move pruning, reverse futility pruning, futility pruning, late move
//     pruning and late move reductions. Mate scores count the distance, so the engine plays the fastest mate and
//     defends the longest.
#ifndef CHESS_FROM_SCRATCH_SEARCH_H
#define CHESS_FROM_SCRATCH_SEARCH_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "board.h"
#include "eval.h"
#include "move.h"
#include "movegen.h"

namespace search {
constexpr int INF = 32001;
constexpr int MATE = 32000;
constexpr int MAX_PLY = 128;
constexpr int MATE_BOUND = MATE - MAX_PLY; // any score at least this big is a forced mate

constexpr bool isMateScore(int score) { return score >= MATE_BOUND || score <= -MATE_BOUND; }
// Moves to mate (positive: we mate, negative: we get mated). Only meaningful for mate scores.
constexpr int mateInMoves(int score) { return score > 0 ? (MATE - score + 1) / 2 : -(MATE + score + 1) / 2; }

// Static exchange evaluation: the material result of the capture sequence on the move's target square, assuming
// both sides always recapture with their least valuable piece and may stop when it stops paying. In centipawns,
// from the mover's point of view. Non-captures score the risk of the moved piece being taken.
int staticExchange(const Board& board, Move move);
} // namespace search

// When to stop. Any limit that is reached stops the search; 0 means "no limit" for nodes and time.
struct SearchLimits {
    int maxDepth = 64;
    std::uint64_t maxNodes = 0;
    int moveTimeMs = 0;
    // Set from another thread to stop immediately (the GUI's "move now" button).
    const std::atomic<bool>* stop = nullptr;
};

// How the engine judges positions. Part of each bot's settings.
struct SearchSettings {
    EvalWeights weights;
    // A draw counts as this many centipawns worse than equal for the side that started the search, so a bot
    // with contempt plays on in equal positions instead of repeating moves.
    int contempt = 0;
    // Search features. Turning them off makes the engine weaker; tests use this to check that each one helps.
    bool nullMovePruning = true;
    bool lateMoveReductions = true;
    bool futilityPruning = true;
};

struct SearchResult {
    Move bestMove;        // the null move only if there are no legal moves
    int score = 0;        // centipawns, from the side to move's point of view (see search::isMateScore)
    int depth = 0;        // last fully completed iteration
    std::uint64_t nodes = 0;
    std::int64_t timeMs = 0;
    std::vector<Move> pv; // the expected line, starting with bestMove
};

struct ScoredMove {
    Move move;
    int score;
};

class Searcher {
public:
    explicit Searcher(int hashMegabytes = 16);

    // Resizes and clears the transposition table.
    void setHashSize(int megabytes);
    // Forgets everything learned (transposition table, killers, history). Call it between unrelated games.
    void clear();

    // `history` is the hashes of earlier positions in the game (Game::positionHistory()), so repetitions that
    // reach back before the search started are scored as draws.
    SearchResult search(const Board& board, const std::vector<std::uint64_t>& history, const SearchLimits& limits,
                        const SearchSettings& settings = {});

    // Every legal move that scores within `margin` centipawns of the best, searched to `depth`, best first.
    // Moves that are clearly worse are rejected with a cheap null-window search instead of being scored exactly.
    // The weaker bots use this to choose among good-enough moves instead of always playing the top one.
    // If mainResult is given, it receives the normal search result for the best move.
    std::vector<ScoredMove> candidateMoves(const Board& board, const std::vector<std::uint64_t>& history, int depth,
                                           int margin, const SearchLimits& limits,
                                           const SearchSettings& settings = {}, SearchResult* mainResult = nullptr);

    std::uint64_t nodes() const { return nodes_; }

private:
    struct TTEntry {
        std::uint64_t key = 0;
        std::int16_t score = 0;
        std::uint16_t move = 0;
        std::uint8_t depth = 0;
        std::uint8_t bound = 0; // 0 empty, 1 upper, 2 lower, 3 exact
    };

    int negamax(int depth, int alpha, int beta, int ply, bool pvNode, bool allowNull);
    int quiesce(int alpha, int beta, int ply);
    int drawScore() const;
    bool isRepetition() const;
    bool shouldStop();
    void begin(const Board& board, const std::vector<std::uint64_t>& history, const SearchLimits& limits,
               const SearchSettings& settings);
    int evaluateHere() const { return evaluate(board_, settings_.weights); }

    void orderMoves(MoveList& moves, int* scores, Move ttMove, int ply) const;
    TTEntry* probe(std::uint64_t key);
    void store(std::uint64_t key, int depth, int score, int bound, Move move, int ply);

    std::vector<TTEntry> table_;
    std::uint64_t tableMask_ = 0;

    Board board_;
    std::vector<std::uint64_t> hashes_; // game history followed by the current search path
    SearchLimits limits_;
    SearchSettings settings_;
    Color rootSide_ = Color::White;
    std::chrono::steady_clock::time_point start_;
    std::uint64_t nodes_ = 0;
    bool stopped_ = false;
    bool canStop_ = false; // the first iteration always finishes, so there is always a move to play

    Move killers_[search::MAX_PLY][2] = {};
    int history_[NUM_COLORS][NUM_SQUARES][NUM_SQUARES] = {};
    Move pv_[search::MAX_PLY][search::MAX_PLY] = {};
    int pvLength_[search::MAX_PLY] = {};
};

#endif // CHESS_FROM_SCRATCH_SEARCH_H
