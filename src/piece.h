#ifndef CHESS_FROM_SCRATCH_PIECE_H
#define CHESS_FROM_SCRATCH_PIECE_H

#include <cstdint>
#include <optional>

enum class Color : std::uint8_t { White = 0, Black = 1 };
enum class PieceType : std::uint8_t { Pawn = 0, Knight, Bishop, Rook, Queen, King };

constexpr int NUM_COLORS = 2;
constexpr int NUM_PIECE_TYPES = 6;

constexpr Color opposite(Color color) {
    return color == Color::White ? Color::Black : Color::White;
}

// A piece is just "what" and "whose". Where it stands is the board's job (the bitboards),
// so position, captured state, en passant and castling rights are not stored here.
struct Piece {
    PieceType type;
    Color color;

    constexpr bool operator==(const Piece&) const = default;
};

// Material value in centipawns (1 pawn = 100). The king is never captured, so it has no material value.
constexpr int pieceValue(PieceType type) {
    switch (type) {
        case PieceType::Pawn:   return 100;
        case PieceType::Knight: return 300;
        case PieceType::Bishop: return 300;
        case PieceType::Rook:   return 500;
        case PieceType::Queen:  return 900;
        case PieceType::King:   return 0;
    }
    return 0;
}

// FEN letters: P N B R Q K for white, p n b r q k for black.
char pieceToChar(Piece piece);
std::optional<Piece> pieceFromChar(char c);

#endif //CHESS_FROM_SCRATCH_PIECE_H
