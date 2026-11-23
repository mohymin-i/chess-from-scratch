#ifndef CHESS_FROM_SCRATCH_BOARD_H
#define CHESS_FROM_SCRATCH_BOARD_H

#include <bit>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "piece.h"

using Bitboard = std::uint64_t;

// Squares use little-endian rank-file mapping (see bitboards.md):
// a1 = 0, b1 = 1, ..., h1 = 7, a2 = 8, ..., h8 = 63.
using Square = int;
constexpr Square NO_SQUARE = -1;
constexpr int NUM_SQUARES = 64;

constexpr bool isValidSquare(Square sq) { return sq >= 0 && sq < NUM_SQUARES; }
constexpr Square makeSquare(int file, int rank) { return rank * 8 + file; } // file and rank are 0-7
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

    // All three throw std::out_of_range for an invalid square.
    std::optional<Piece> pieceAt(Square sq) const;
    void placePiece(Piece piece, Square sq); // replaces whatever was on sq
    void removePiece(Square sq);             // does nothing if sq is empty

    Color sideToMove() const { return sideToMove_; }
    std::uint8_t castlingRights() const { return castlingRights_; }
    Square enPassantSquare() const { return enPassant_; }

    std::string toString() const;

    // True if no square holds two pieces, and the cached occupancy and mailbox both match the piece bitboards.
    bool isConsistent() const;

private:
    struct EmptyTag {};
    explicit Board(EmptyTag) {}

    static constexpr int idx(Color c) { return static_cast<int>(c); }
    static constexpr int idx(PieceType t) { return static_cast<int>(t); }
    static void checkSquare(Square sq);

    Bitboard pieces_[NUM_COLORS][NUM_PIECE_TYPES] = {};
    Bitboard occupancy_[NUM_COLORS] = {}; // kept up to date on every change so lookups are one AND
    // The same position indexed by square ("mailbox"): bitboards answer "where are the knights?",
    // this answers "what is on e4?" in one read. Every change updates both.
    std::optional<Piece> mailbox_[NUM_SQUARES] = {};
    Color sideToMove_ = Color::White;
    std::uint8_t castlingRights_ = 0;
    Square enPassant_ = NO_SQUARE;
};

#endif //CHESS_FROM_SCRATCH_BOARD_H
