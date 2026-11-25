#include "eval.h"

#include <algorithm>
#include <array>
#include <cstdlib>

#include "attacks.h"
#include "movegen.h"

namespace {

// A middlegame/endgame pair, added up separately and blended at the end.
struct Score {
    int mg = 0;
    int eg = 0;
    Score& operator+=(Score o) {
        mg += o.mg;
        eg += o.eg;
        return *this;
    }
    Score& operator-=(Score o) {
        mg -= o.mg;
        eg -= o.eg;
        return *this;
    }
    friend Score operator*(Score s, int n) { return {s.mg * n, s.eg * n}; }
};

Score scaled(Score s, int percent) { return {s.mg * percent / 100, s.eg * percent / 100}; }

// ---------- Piece-square tables ----------
// Written the way a board is printed, from white's side: the first row is rank 8, the last is rank 1.
// For white, index = (7 - rank) * 8 + file. Black uses the same table flipped vertically.

// clang-format off
constexpr int PAWN_MG[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
     30,  30,  30,  32,  32,  30,  30,  30,
     10,  12,  18,  24,  24,  18,  12,  10,
      4,   6,  10,  20,  20,  10,   6,   4,
      0,   0,   6,  16,  16,   4,   0,   0,
      2,  -2,  -4,   4,   4,  -6,  -2,   2,
      4,   8,   8, -16, -16,   8,   8,   4,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr int PAWN_EG[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
     60,  60,  60,  60,  60,  60,  60,  60,
     35,  35,  35,  35,  35,  35,  35,  35,
     18,  18,  18,  18,  18,  18,  18,  18,
      8,   8,   8,   8,   8,   8,   8,   8,
      2,   2,   2,   2,   2,   2,   2,   2,
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr int KNIGHT_MG[64] = {
    -50, -35, -25, -20, -20, -25, -35, -50,
    -35, -15,   0,   5,   5,   0, -15, -35,
    -25,   5,  12,  16,  16,  12,   5, -25,
    -20,   5,  16,  22,  22,  16,   5, -20,
    -20,   0,  14,  20,  20,  14,   0, -20,
    -25,   2,  10,  12,  12,  10,   2, -25,
    -35, -15,   0,   4,   4,   0, -15, -35,
    -50, -30, -25, -20, -20, -25, -30, -50,
};
constexpr int KNIGHT_EG[64] = {
    -40, -28, -20, -15, -15, -20, -28, -40,
    -28, -10,   0,   4,   4,   0, -10, -28,
    -20,   0,   8,  12,  12,   8,   0, -20,
    -15,   4,  12,  16,  16,  12,   4, -15,
    -15,   4,  12,  16,  16,  12,   4, -15,
    -20,   0,   8,  12,  12,   8,   0, -20,
    -28, -10,   0,   4,   4,   0, -10, -28,
    -40, -28, -20, -15, -15, -20, -28, -40,
};
constexpr int BISHOP_MG[64] = {
    -20, -10, -10, -10, -10, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   6,  10,  10,   6,   0, -10,
    -10,   6,   6,  12,  12,   6,   6, -10,
    -10,   0,  12,  12,  12,  12,   0, -10,
    -10,  10,  10,  10,  10,  10,  10, -10,
    -10,   8,   0,   2,   2,   0,   8, -10,
    -20, -10, -12, -10, -10, -12, -10, -20,
};
constexpr int BISHOP_EG[64] = {
    -15, -10,  -8,  -6,  -6,  -8, -10, -15,
    -10,  -4,   0,   2,   2,   0,  -4, -10,
     -8,   0,   4,   6,   6,   4,   0,  -8,
     -6,   2,   6,  10,  10,   6,   2,  -6,
     -6,   2,   6,  10,  10,   6,   2,  -6,
     -8,   0,   4,   6,   6,   4,   0,  -8,
    -10,  -4,   0,   2,   2,   0,  -4, -10,
    -15, -10,  -8,  -6,  -6,  -8, -10, -15,
};
constexpr int ROOK_MG[64] = {
      0,   0,   0,   4,   4,   0,   0,   0,
     12,  16,  16,  16,  16,  16,  16,  12,
     -4,   0,   0,   0,   0,   0,   0,  -4,
     -4,   0,   0,   0,   0,   0,   0,  -4,
     -4,   0,   0,   0,   0,   0,   0,  -4,
     -4,   0,   0,   0,   0,   0,   0,  -4,
     -6,   0,   0,   0,   0,   0,   0,  -6,
     -2,   0,   2,   8,   8,   4,   0,  -2,
};
constexpr int ROOK_EG[64] = {
      6,   6,   6,   6,   6,   6,   6,   6,
     10,  10,  10,  10,  10,  10,  10,  10,
      2,   2,   2,   2,   2,   2,   2,   2,
      0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,   0,   0,   0,
     -2,  -2,  -2,  -2,  -2,  -2,  -2,  -2,
     -4,  -4,  -4,  -4,  -4,  -4,  -4,  -4,
     -4,  -2,   0,   0,   0,   0,  -2,  -4,
};
constexpr int QUEEN_MG[64] = {
    -20, -10, -10,  -5,  -5, -10, -10, -20,
    -10,   0,   0,   0,   0,   0,   0, -10,
    -10,   0,   4,   4,   4,   4,   0, -10,
     -5,   0,   4,   6,   6,   4,   0,  -5,
     -5,   0,   4,   6,   6,   4,   0,  -5,
    -10,   2,   4,   4,   4,   4,   0, -10,
    -10,   0,   2,   0,   0,   0,   0, -10,
    -20, -10, -10,  -2,  -5, -10, -10, -20,
};
constexpr int QUEEN_EG[64] = {
    -20, -12,  -8,  -6,  -6,  -8, -12, -20,
    -12,  -4,   0,   2,   2,   0,  -4, -12,
     -8,   0,   6,   8,   8,   6,   0,  -8,
     -6,   2,   8,  12,  12,   8,   2,  -6,
     -6,   2,   8,  12,  12,   8,   2,  -6,
     -8,   0,   6,   8,   8,   6,   0,  -8,
    -12,  -4,   0,   2,   2,   0,  -4, -12,
    -20, -12,  -8,  -6,  -6,  -8, -12, -20,
};
constexpr int KING_MG[64] = {
    -40, -45, -45, -50, -50, -45, -45, -40,
    -35, -40, -40, -45, -45, -40, -40, -35,
    -30, -35, -35, -40, -40, -35, -35, -30,
    -25, -30, -30, -35, -35, -30, -30, -25,
    -15, -20, -20, -25, -25, -20, -20, -15,
     -5, -10, -12, -15, -15, -12, -10,  -5,
     12,  12,  -4,  -8,  -8,  -4,  14,  14,
     18,  28,  10,  -4,   0,   6,  30,  20,
};
constexpr int KING_EG[64] = {
    -45, -30, -22, -18, -18, -22, -30, -45,
    -25, -10,  -2,   2,   2,  -2, -10, -25,
    -20,   0,  12,  18,  18,  12,   0, -20,
    -18,   4,  18,  26,  26,  18,   4, -18,
    -18,   2,  16,  24,  24,  16,   2, -18,
    -20,  -4,   8,  14,  14,   8,  -4, -20,
    -28, -12,  -4,   0,   0,  -4, -12, -28,
    -45, -32, -25, -20, -20, -25, -32, -45,
};
// clang-format on

constexpr const int* PST_MG[6] = {PAWN_MG, KNIGHT_MG, BISHOP_MG, ROOK_MG, QUEEN_MG, KING_MG};
constexpr const int* PST_EG[6] = {PAWN_EG, KNIGHT_EG, BISHOP_EG, ROOK_EG, QUEEN_EG, KING_EG};

constexpr int pstIndex(Color color, Square sq) {
    return color == Color::White ? (7 - rankOf(sq)) * 8 + fileOf(sq) : rankOf(sq) * 8 + fileOf(sq);
}

// ---------- Other weights ----------

// Mobility: bonus per reachable square beyond a typical count, for knight, bishop, rook, queen.
constexpr Score MOBILITY_PER_SQUARE[4] = {{4, 4}, {5, 5}, {2, 4}, {1, 2}};
constexpr int MOBILITY_TYPICAL[4] = {4, 6, 7, 13};

constexpr Score DOUBLED_PAWN = {-10, -20};
constexpr Score ISOLATED_PAWN = {-12, -14};
// Passed pawn bonus by how far it has advanced (relative rank 0-7).
constexpr Score PASSED_PAWN[8] = {{0, 0}, {5, 10}, {8, 15}, {15, 30}, {30, 55}, {50, 90}, {80, 140}, {0, 0}};
constexpr Score BISHOP_PAIR = {30, 50};
constexpr Score ROOK_OPEN_FILE = {25, 10};
constexpr Score ROOK_SEMI_OPEN_FILE = {12, 6};
constexpr int TEMPO = 12;

// King safety: each enemy piece type adds "attack units" per square of our king zone it hits.
constexpr int KING_ATTACK_UNITS[6] = {0, 2, 2, 3, 5, 0};
constexpr int SHIELD_PAWN_MISSING = 14;
constexpr int OPEN_FILE_NEAR_KING = 18;

constexpr Bitboard adjacentFiles(int file) {
    return (file > 0 ? fileMask(file - 1) : 0) | (file < 7 ? fileMask(file + 1) : 0);
}

int relativeRank(Color color, Square sq) { return color == Color::White ? rankOf(sq) : 7 - rankOf(sq); }

// Squares in front of a pawn on its own and the two neighboring files, up to the last rank. Precomputed: the
// evaluation asks for it for every pawn of every position.
constexpr std::array<std::array<Bitboard, NUM_SQUARES>, NUM_COLORS> makePassedSpans() {
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_COLORS> spans{};
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        const int file = sq % 8;
        const int rank = sq / 8;
        const Bitboard files = fileMask(file) | adjacentFiles(file);
        Bitboard whiteAhead = 0;
        Bitboard blackAhead = 0;
        for (int r = rank + 1; r < 8; ++r) whiteAhead |= rankMask(r);
        for (int r = rank - 1; r >= 0; --r) blackAhead |= rankMask(r);
        spans[0][sq] = files & whiteAhead;
        spans[1][sq] = files & blackAhead;
    }
    return spans;
}
constexpr auto PASSED_SPANS = makePassedSpans();

Bitboard passedSpan(Color color, Square sq) { return PASSED_SPANS[static_cast<int>(color)][sq]; }

Bitboard pawnAttacksAll(Color color, Bitboard pawns) {
    if (color == Color::White) return ((pawns & ~FILE_A) << 7) | ((pawns & ~FILE_H) << 9);
    return ((pawns & ~FILE_A) >> 9) | ((pawns & ~FILE_H) >> 7);
}

int chebyshev(Square a, Square b) {
    return std::max(std::abs(fileOf(a) - fileOf(b)), std::abs(rankOf(a) - rankOf(b)));
}

int centerDistance(Square sq) {
    // 0 for the four center squares, up to 6 for the corners.
    const int f = fileOf(sq) < 4 ? 3 - fileOf(sq) : fileOf(sq) - 4;
    const int r = rankOf(sq) < 4 ? 3 - rankOf(sq) : rankOf(sq) - 4;
    return f + r;
}

struct Terms {
    Score material;
    Score pieceSquare;
    Score mobility;
    Score kingSafety;
    Score pawnStructure;
    Score passedPawns;
};

// Adds everything for one side to `t` (white adds, black subtracts afterwards).
void evaluateSide(const Board& board, Color us, Terms& t) {
    const Color them = opposite(us);
    const Bitboard occupied = board.occupancy();
    const Bitboard own = board.occupancy(us);
    const Bitboard ourPawns = board.pieces(us, PieceType::Pawn);
    const Bitboard theirPawns = board.pieces(them, PieceType::Pawn);
    const Bitboard theirPawnAttacks = pawnAttacksAll(them, theirPawns);
    const Square theirKing = static_cast<Square>(std::countr_zero(board.pieces(them, PieceType::King)));
    const Bitboard theirKingZone = kingAttacks(theirKing) | squareBit(theirKing);

    int attackUnits = 0;
    int attackers = 0;

    for (int t6 = 0; t6 < 6; ++t6) {
        const auto type = static_cast<PieceType>(t6);
        for (Bitboard b = board.pieces(us, type); b;) {
            const Square sq = popLsb(b);
            t.material += Score{eval::PIECE_VALUE_MG[t6], eval::PIECE_VALUE_EG[t6]};
            t.pieceSquare += Score{PST_MG[t6][pstIndex(us, sq)], PST_EG[t6][pstIndex(us, sq)]};

            if (type == PieceType::Pawn || type == PieceType::King) continue;
            const Bitboard attacks = pieceAttacks(type, sq, occupied);
            const int reachable = popCount(attacks & ~own & ~theirPawnAttacks);
            t.mobility += MOBILITY_PER_SQUARE[t6 - 1] * (reachable - MOBILITY_TYPICAL[t6 - 1]);
            if (const int hits = popCount(attacks & theirKingZone)) {
                ++attackers;
                attackUnits += KING_ATTACK_UNITS[t6] * hits;
            }
            if (type == PieceType::Rook) {
                const Bitboard file = fileMask(fileOf(sq));
                if (!(file & ourPawns)) t.pieceSquare += (file & theirPawns) ? ROOK_SEMI_OPEN_FILE : ROOK_OPEN_FILE;
            }
        }
    }

    if (popCount(board.pieces(us, PieceType::Bishop)) >= 2) t.material += BISHOP_PAIR;

    // Attacking their king. Grows with the square of the pressure, and only counts with two or more attackers
    // (one piece alone rarely mates). It's a middlegame term: without queens there's little danger.
    if (attackers >= 2 && board.pieces(us, PieceType::Queen)) {
        t.kingSafety.mg += std::min(attackUnits * attackUnits, 500) / 2;
    }

    // Our own king's pawn shield, for a king still near its back rank.
    const Square ourKing = static_cast<Square>(std::countr_zero(board.pieces(us, PieceType::King)));
    if (relativeRank(us, ourKing) <= 1) {
        const int kf = fileOf(ourKing);
        for (int f = std::max(0, kf - 1); f <= std::min(7, kf + 1); ++f) {
            const Bitboard filePawns = ourPawns & fileMask(f);
            // A shield pawn counts if it's on the first two ranks in front of the king.
            const int forward = us == Color::White ? 1 : -1;
            const int r1 = rankOf(ourKing) + forward;
            const int r2 = rankOf(ourKing) + 2 * forward;
            Bitboard shieldRanks = 0;
            if (r1 >= 0 && r1 < 8) shieldRanks |= rankMask(r1);
            if (r2 >= 0 && r2 < 8) shieldRanks |= rankMask(r2);
            if (!(filePawns & shieldRanks)) t.kingSafety.mg -= SHIELD_PAWN_MISSING;
            if (!filePawns) t.kingSafety.mg -= OPEN_FILE_NEAR_KING;
        }
    }

    // Pawn structure, with whole-board bit operations instead of a loop over files. Folding the ranks together gives
    // one bit per file that has a pawn. Every pawn beyond the first on its file is doubled, so they number pawns minus
    // occupied files; a pawn is isolated when neither neighboring file has one.
    Bitboard fold = ourPawns | (ourPawns >> 32);
    fold |= fold >> 16;
    fold |= fold >> 8;
    const Bitboard pawnFiles = fold & 0xFF;
    t.pawnStructure += DOUBLED_PAWN * (popCount(ourPawns) - popCount(pawnFiles));
    const Bitboard isolatedFiles = pawnFiles & ~((pawnFiles << 1) | (pawnFiles >> 1));
    t.pawnStructure += ISOLATED_PAWN * popCount(ourPawns & (isolatedFiles * FILE_A));
    for (Bitboard b = ourPawns; b;) {
        const Square sq = popLsb(b);
        if (passedSpan(us, sq) & theirPawns) continue;
        const int rr = relativeRank(us, sq);
        t.passedPawns += PASSED_PAWN[rr];
        // In the endgame, kings matter for passed pawns: ours should be close, theirs far from where it promotes.
        const Square promotion = makeSquare(fileOf(sq), us == Color::White ? 7 : 0);
        t.passedPawns.eg += (chebyshev(theirKing, promotion) - chebyshev(ourKing, promotion)) * rr;
    }
}

int nonPawnMaterial(const Board& board, Color c) {
    int total = 0;
    for (int t = 1; t < 5; ++t) {
        total += popCount(board.pieces(c, static_cast<PieceType>(t))) * eval::PIECE_VALUE_MG[t];
    }
    return total;
}

} // namespace

int gamePhase(const Board& board) {
    int phase = 0;
    for (int t = 1; t < 5; ++t) {
        const auto type = static_cast<PieceType>(t);
        phase += eval::PHASE_WEIGHT[t] *
                 (popCount(board.pieces(Color::White, type)) + popCount(board.pieces(Color::Black, type)));
    }
    return std::min(phase, eval::MAX_PHASE);
}

int evaluate(const Board& board, const EvalWeights& w) {
    Terms white;
    Terms black;
    evaluateSide(board, Color::White, white);
    evaluateSide(board, Color::Black, black);

    Score total;
    total += scaled(white.material, w.material);
    total -= scaled(black.material, w.material);
    total += scaled(white.pieceSquare, w.pieceSquare);
    total -= scaled(black.pieceSquare, w.pieceSquare);
    total += scaled(white.mobility, w.mobility);
    total -= scaled(black.mobility, w.mobility);
    total += scaled(white.kingSafety, w.kingSafety);
    total -= scaled(black.kingSafety, w.kingSafety);
    total += scaled(white.pawnStructure, w.pawnStructure);
    total -= scaled(black.pawnStructure, w.pawnStructure);
    total += scaled(white.passedPawns, w.passedPawns);
    total -= scaled(black.passedPawns, w.passedPawns);

    const int phase = gamePhase(board);
    int score = (total.mg * phase + total.eg * (eval::MAX_PHASE - phase)) / eval::MAX_PHASE;

    // Endgame knowledge. "Strong" is decided by material, not by the score, so both colors are treated alike.
    const int whiteNonPawn = nonPawnMaterial(board, Color::White);
    const int blackNonPawn = nonPawnMaterial(board, Color::Black);
    const int whitePawns = popCount(board.pieces(Color::White, PieceType::Pawn));
    const int blackPawns = popCount(board.pieces(Color::Black, PieceType::Pawn));
    const int whiteTotal = whiteNonPawn + 100 * whitePawns;
    const int blackTotal = blackNonPawn + 100 * blackPawns;
    if (whiteTotal != blackTotal) {
        const Color strong = whiteTotal > blackTotal ? Color::White : Color::Black;
        const bool white = strong == Color::White;
        const int strongMaterial = white ? whiteNonPawn : blackNonPawn;
        const int weakMaterial = white ? blackNonPawn : whiteNonPawn;
        const int strongPawns = white ? whitePawns : blackPawns;
        const int weakPawns = white ? blackPawns : whitePawns;
        const Bitboard strongKnights = board.pieces(strong, PieceType::Knight);
        const bool onlyKnights = strongMaterial == popCount(strongKnights) * eval::PIECE_VALUE_MG[1];

        if (strongPawns == 0) {
            // Without pawns, being up less than about a rook rarely wins (K+R v K+B, K+R+B v K+R, ...),
            // and two knights can't force mate at all.
            if (strongMaterial - weakMaterial < 400) {
                score /= 4;
            } else if (onlyKnights && popCount(strongKnights) <= 2) {
                score /= 16;
            }
        }

        // "Mop-up": against a bare king, drive it to the edge and bring our king closer, so the bots actually
        // finish won endgames instead of shuffling until the 50-move rule.
        if (weakMaterial == 0 && weakPawns == 0 && strongMaterial >= eval::PIECE_VALUE_MG[3] && !onlyKnights) {
            const Square strongKing = kingSquare(board, strong);
            const Square weakKing = kingSquare(board, opposite(strong));
            const int bonus = 10 * centerDistance(weakKing) + 4 * (7 - chebyshev(strongKing, weakKing));
            score += white ? bonus : -bonus;
        }
    }

    score += board.sideToMove() == Color::White ? TEMPO : -TEMPO;
    return board.sideToMove() == Color::White ? score : -score;
}

Board mirrored(const Board& board) {
    Board out = Board::empty();
    for (Square sq = A1; sq <= H8; ++sq) {
        if (const std::optional<Piece> p = board.pieceAt(sq)) {
            out.placePiece({p->type, opposite(p->color)}, makeSquare(fileOf(sq), 7 - rankOf(sq)));
        }
    }
    out.setSideToMove(opposite(board.sideToMove()));
    const std::uint8_t r = board.castlingRights();
    out.setCastlingRights(static_cast<std::uint8_t>(((r & 3) << 2) | ((r >> 2) & 3)));
    const Square ep = board.enPassantSquare();
    out.setEnPassantSquare(ep == NO_SQUARE ? NO_SQUARE : makeSquare(fileOf(ep), 7 - rankOf(ep)));
    out.setHalfmoveClock(board.halfmoveClock());
    out.setFullmoveNumber(board.fullmoveNumber());
    return out;
}
