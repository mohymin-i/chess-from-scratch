#ifndef CHESS_FROM_SCRATCH_BOARD_H
#define CHESS_FROM_SCRATCH_BOARD_H

#include <bit>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "piece.h"

using Bitboard = std::uint64_t;

// Squares use little-endian rank-file mapping (see bitboards.md):
// a1 = 0, b1 = 1, ..., h1 = 7, a2 = 8, ..., h8 = 63.
// Square is its own type, so a file, rank or other plain int can't be passed where a square is expected
// without an explicit static_cast. Its value is still the bit index, so squareBit(E4) and arrays indexed
// by square work directly.
// clang-format off
enum Square : int {
    A1, B1, C1, D1, E1, F1, G1, H1,
    A2, B2, C2, D2, E2, F2, G2, H2,
    A3, B3, C3, D3, E3, F3, G3, H3,
    A4, B4, C4, D4, E4, F4, G4, H4,
    A5, B5, C5, D5, E5, F5, G5, H5,
    A6, B6, C6, D6, E6, F6, G6, H6,
    A7, B7, C7, D7, E7, F7, G7, H7,
    A8, B8, C8, D8, E8, F8, G8, H8,
    NO_SQUARE = -1,
};
// clang-format on
constexpr int NUM_SQUARES = 64;

// Allows `for (Square sq = A1; sq <= H8; ++sq)`.
constexpr Square& operator++(Square& sq) { return sq = static_cast<Square>(sq + 1); }

// Takes an int so it can check raw numbers before they become squares.
constexpr bool isValidSquare(int sq) { return sq >= 0 && sq < NUM_SQUARES; }
constexpr Square makeSquare(int file, int rank) { return static_cast<Square>(rank * 8 + file); } // file, rank 0-7
constexpr int fileOf(Square sq) { return sq & 7; }
constexpr int rankOf(Square sq) { return sq >> 3; }
constexpr Bitboard squareBit(Square sq) { return Bitboard{1} << sq; } // sq must be valid

inline int popCount(Bitboard b) { return std::popcount(b); }

// "e4" -> 28. Returns NO_SQUARE unless the name is a lowercase file a-h followed by a rank 1-8.
Square squareFromString(std::string_view name);
// 28 -> "e4". Throws std::out_of_range for invalid squares.
std::string squareToString(Square sq);

enum CastlingRight : std::uint8_t {
    WHITE_KINGSIDE = 1,
    WHITE_QUEENSIDE = 2,
    BLACK_KINGSIDE = 4,
    BLACK_QUEENSIDE = 8,
    ALL_CASTLING = 15,
};

class Board {
public:
    Board();              // standard starting position
    static Board empty(); // no pieces, white to move, no castling rights

    Bitboard pieces(Color color, PieceType type) const { return pieces_[idx(color)][idx(type)]; }
    Bitboard occupancy(Color color) const { return occupancy_[idx(color)]; }
    Bitboard occupancy() const { return occupancy_[0] | occupancy_[1]; }

    // Checked versions for squares that come from outside (files, the GUI, tests).
    // All three throw std::out_of_range for an invalid square.
    std::optional<Piece> pieceAt(Square sq) const;
    void placePiece(Piece piece, Square sq); // replaces whatever was on sq
    void removePiece(Square sq);             // does nothing if sq is empty

    // Unchecked versions for hot loops (move generation, search) where the square is known to be valid.
    // They skip the bounds check; an invalid square is a bug, caught by assert in debug builds.
    std::optional<Piece> pieceAtUnchecked(Square sq) const {
        assert(isValidSquare(sq));
        return mailbox_[sq];
    }
    void placePieceUnchecked(Piece piece, Square sq) {
        removePieceUnchecked(sq);
        const Bitboard bit = squareBit(sq);
        pieces_[idx(piece.color)][idx(piece.type)] |= bit;
        occupancy_[idx(piece.color)] |= bit;
        mailbox_[sq] = piece;
    }
    void removePieceUnchecked(Square sq) {
        assert(isValidSquare(sq));
        const std::optional<Piece> old = mailbox_[sq];
        if (!old) return;
        // The mailbox says exactly which bitboard holds this square, so only that one is touched.
        const Bitboard keep = ~squareBit(sq);
        pieces_[idx(old->color)][idx(old->type)] &= keep;
        occupancy_[idx(old->color)] &= keep;
        mailbox_[sq].reset();
    }

    Color sideToMove() const { return sideToMove_; }
    std::uint8_t castlingRights() const { return castlingRights_; }
    Square enPassantSquare() const { return enPassant_; } // square a pawn just skipped over, or NO_SQUARE
    int halfmoveClock() const { return halfmoveClock_; }   // moves since the last capture or pawn move (50-move rule)
    int fullmoveNumber() const { return fullmoveNumber_; } // starts at 1, goes up after each black move

    // Setters reject impossible values and leave the board unchanged when they throw.
    void setSideToMove(Color color) { sideToMove_ = color; }
    void setCastlingRights(std::uint8_t rights); // std::invalid_argument unless a combination of CastlingRight flags
    void setEnPassantSquare(Square sq);          // NO_SQUARE or a rank 3/6 square; std::out_of_range off the board,
                                                 // std::invalid_argument on any other rank
    void setHalfmoveClock(int clock);            // std::invalid_argument if negative
    void setFullmoveNumber(int number);          // std::invalid_argument if less than 1

    std::string toString() const;

    // True if no square holds two pieces, and the cached occupancy and mailbox both match the piece bitboards.
    bool isConsistent() const;

private:
    struct EmptyTag {};
    explicit Board(EmptyTag) {}

    static constexpr int idx(Color c) { return static_cast<int>(c); }
    static constexpr int idx(PieceType t) { return static_cast<int>(t); }
    static void checkSquare(Square sq);
    static bool isValidEnPassantSquare(Square sq);

    Bitboard pieces_[NUM_COLORS][NUM_PIECE_TYPES] = {};
    Bitboard occupancy_[NUM_COLORS] = {}; // kept up to date on every change so lookups are one AND
    // The same position indexed by square ("mailbox"): bitboards answer "where are the knights?",
    // this answers "what is on e4?" in one read. Every change updates both.
    std::optional<Piece> mailbox_[NUM_SQUARES] = {};
    Color sideToMove_ = Color::White;
    std::uint8_t castlingRights_ = 0;
    Square enPassant_ = NO_SQUARE;
    int halfmoveClock_ = 0;
    int fullmoveNumber_ = 1;
};

#endif //CHESS_FROM_SCRATCH_BOARD_H
