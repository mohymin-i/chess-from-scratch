// Static evaluation: how good a position is, in centipawns, from the side to move's point of view.
//
// Every term has a middlegame and an endgame value. They're blended by game phase (how much non-pawn material is
// left), so for example the king is told to hide early and walk to the center late.
//
// All piece-square tables and weights here are this project's own, tuned by hand.
#ifndef CHESS_FROM_SCRATCH_EVAL_H
#define CHESS_FROM_SCRATCH_EVAL_H

#include "board.h"

// Percentages that scale groups of evaluation terms. 100 everywhere is the default, balanced style; bots use
// other values for personality (an attacker cares more about king safety and mobility, and so on).
struct EvalWeights {
    int material = 100;
    int pieceSquare = 100;
    int mobility = 100;
    int kingSafety = 100;
    int pawnStructure = 100;
    int passedPawns = 100;

    bool operator==(const EvalWeights&) const = default;
};

namespace eval {
// Middlegame and endgame piece values, also used by search for move ordering and pruning margins.
constexpr int PIECE_VALUE_MG[6] = {100, 320, 335, 480, 950, 0};
constexpr int PIECE_VALUE_EG[6] = {125, 300, 320, 530, 980, 0};
// Phase contribution of each piece type; the opening position adds up to MAX_PHASE.
constexpr int PHASE_WEIGHT[6] = {0, 1, 1, 2, 4, 0};
constexpr int MAX_PHASE = 24;
} // namespace eval

// Evaluates from the side to move's perspective: positive means the side to move is better.
int evaluate(const Board& board, const EvalWeights& weights = {});

// 0 (only kings and pawns) to MAX_PHASE (all pieces on the board).
int gamePhase(const Board& board);

// The same position with colors swapped and the board flipped top to bottom. A correct evaluation scores it
// exactly the same (both are from the side to move's point of view), which catches sign and orientation bugs.
Board mirrored(const Board& board);

#endif // CHESS_FROM_SCRATCH_EVAL_H
