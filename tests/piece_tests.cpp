#include <set>

#include "piece.h"
#include "test_framework.h"

namespace {
constexpr Color COLORS[] = {Color::White, Color::Black};
constexpr PieceType TYPES[] = {PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
                               PieceType::Rook, PieceType::Queen,  PieceType::King};
}

TEST(piece_values_in_centipawns) {
    CHECK_EQ(pieceValue(PieceType::Pawn), 100);
    CHECK_EQ(pieceValue(PieceType::Knight), 300);
    CHECK_EQ(pieceValue(PieceType::Bishop), 300);
    CHECK_EQ(pieceValue(PieceType::Rook), 500);
    CHECK_EQ(pieceValue(PieceType::Queen), 900);
    CHECK_EQ(pieceValue(PieceType::King), 0);
}

TEST(piece_letters_match_fen) {
    const char* white = "PNBRQK";
    const char* black = "pnbrqk";
    for (int i = 0; i < NUM_PIECE_TYPES; ++i) {
        CHECK_EQ(pieceToChar(Piece{TYPES[i], Color::White}), white[i]);
        CHECK_EQ(pieceToChar(Piece{TYPES[i], Color::Black}), black[i]);
    }
}

TEST(piece_all_twelve_letters_are_distinct_and_round_trip) {
    std::set<char> letters;
    for (Color color : COLORS) {
        for (PieceType type : TYPES) {
            const Piece piece{type, color};
            const char letter = pieceToChar(piece);
            letters.insert(letter);
            const auto back = pieceFromChar(letter);
            CHECK(back.has_value());
            CHECK(*back == piece);
        }
    }
    CHECK_EQ(letters.size(), 12u);
}

TEST(piece_non_piece_characters_are_rejected) {
    for (char c : {'.', ' ', 'x', 'X', 'a', 'z', '0', '9', '\0', '\n'}) {
        CHECK(!pieceFromChar(c).has_value());
    }
}

TEST(piece_equality_compares_type_and_color) {
    const Piece whiteRook{PieceType::Rook, Color::White};
    const Piece blackRook{PieceType::Rook, Color::Black};
    const Piece whiteQueen{PieceType::Queen, Color::White};
    CHECK(whiteRook == whiteRook);
    CHECK(!(whiteRook == blackRook));
    CHECK(!(whiteRook == whiteQueen));
}

TEST(piece_opposite_color) {
    CHECK(opposite(Color::White) == Color::Black);
    CHECK(opposite(Color::Black) == Color::White);
    CHECK(opposite(opposite(Color::White)) == Color::White);
}
