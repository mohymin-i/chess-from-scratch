#include "piece.h"

namespace {
// Indexed by PieceType.
constexpr char WHITE_LETTERS[NUM_PIECE_TYPES + 1] = "PNBRQK";

constexpr char toLower(char c) { return static_cast<char>(c - 'A' + 'a'); }
}

char pieceToChar(Piece piece) {
    const char letter = WHITE_LETTERS[static_cast<int>(piece.type)];
    return piece.color == Color::White ? letter : toLower(letter);
}

std::optional<Piece> pieceFromChar(char c) {
    for (int i = 0; i < NUM_PIECE_TYPES; ++i) {
        const auto type = static_cast<PieceType>(i);
        if (c == WHITE_LETTERS[i]) return Piece{type, Color::White};
        if (c == toLower(WHITE_LETTERS[i])) return Piece{type, Color::Black};
    }
    return std::nullopt;
}
