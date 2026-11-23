// Move generation, check detection and perft.
#ifndef CHESS_FROM_SCRATCH_MOVEGEN_H
#define CHESS_FROM_SCRATCH_MOVEGEN_H

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

#include "board.h"
#include "move.h"

// A fixed-size list, so generating moves never allocates. No legal chess position has more than 218 moves.
class MoveList {
public:
    static constexpr int CAPACITY = 256;

    void push(Move move) {
        assert(size_ < CAPACITY);
        moves_[size_++] = move;
    }
    int size() const { return size_; }
    bool empty() const { return size_ == 0; }
    void clear() { size_ = 0; }
    Move operator[](int i) const { return moves_[i]; }
    Move& operator[](int i) { return moves_[i]; }
    const Move* begin() const { return moves_.data(); }
    const Move* end() const { return moves_.data() + size_; }
    Move* begin() { return moves_.data(); }
    Move* end() { return moves_.data() + size_; }
    bool contains(Move move) const {
        for (const Move m : *this)
            if (m == move) return true;
        return false;
    }

private:
    std::array<Move, CAPACITY> moves_;
    int size_ = 0;
};

enum class GenType : std::uint8_t {
    All,
    Noisy, // captures (including en passant) and every promotion: what quiescence search looks at
    Quiet, // everything else, including castling
};

// Moves that follow each piece's movement rules but may leave the mover's own king in check. Search uses these
// and rejects illegal ones after making them, which is cheaper than filtering every move up front.
// Castling is only generated when it's fully legal (not out of, through or into check).
void generatePseudoLegal(const Board& board, MoveList& list, GenType type = GenType::All);

// Exactly the legal moves.
void generateLegal(const Board& board, MoveList& list);

// True if a pseudo-legal move is legal, meaning it doesn't leave the mover's king in check.
bool isLegal(const Board& board, Move move);

// Is sq attacked by any piece of color `by`? `occupied` lets callers pretend pieces are elsewhere.
bool isSquareAttacked(const Board& board, Square sq, Color by, Bitboard occupied);
inline bool isSquareAttacked(const Board& board, Square sq, Color by) {
    return isSquareAttacked(board, sq, by, board.occupancy());
}

// Every piece of either color that attacks sq, given `occupied`.
Bitboard attackersTo(const Board& board, Square sq, Bitboard occupied);

inline Square kingSquare(const Board& board, Color color) {
    return static_cast<Square>(std::countr_zero(board.pieces(color, PieceType::King)));
}

// Is the side to move in check?
inline bool inCheck(const Board& board) {
    const Color us = board.sideToMove();
    return isSquareAttacked(board, kingSquare(board, us), opposite(us));
}

// Did the move just made leave the side that made it in check? Call it right after makeMove.
inline bool moverLeftInCheck(const Board& board) {
    const Color mover = opposite(board.sideToMove());
    return isSquareAttacked(board, kingSquare(board, mover), board.sideToMove());
}

// Perft ("performance test"): the number of leaf positions reachable in exactly `depth` moves. The counts for
// standard positions are published, so a mismatch proves a move generation bug.
std::uint64_t perft(Board& board, int depth);
// The same, split by first move, to find which move's subtree is wrong.
std::vector<std::pair<Move, std::uint64_t>> perftDivide(Board& board, int depth);

#endif // CHESS_FROM_SCRATCH_MOVEGEN_H
