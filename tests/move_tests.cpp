#include <string>

#include "board.h"
#include "move.h"
#include "test_framework.h"

namespace {
constexpr MoveFlag ALL_FLAGS[] = {
    MoveFlag::Quiet,
    MoveFlag::DoublePawnPush,
    MoveFlag::KingCastle,
    MoveFlag::QueenCastle,
    MoveFlag::Capture,
    MoveFlag::EnPassant,
    MoveFlag::KnightPromotion,
    MoveFlag::BishopPromotion,
    MoveFlag::RookPromotion,
    MoveFlag::QueenPromotion,
    MoveFlag::KnightPromotionCapture,
    MoveFlag::BishopPromotionCapture,
    MoveFlag::RookPromotionCapture,
    MoveFlag::QueenPromotionCapture,
};

std::optional<MoveFlag> uciFlag(const Board& board, const char* text) {
    const std::optional<Move> move = moveFromUci(board, text);
    if (!move) return std::nullopt;
    return move->flag();
}

// Kings on e1 and e8 plus whatever the test adds.
Board kingsOnly() {
    Board board = Board::empty();
    board.placePiece({PieceType::King, Color::White}, E1);
    board.placePiece({PieceType::King, Color::Black}, E8);
    return board;
}
} // namespace

// ---------- Packing ----------

TEST(move_is_two_bytes_and_default_is_null) {
    static_assert(sizeof(Move) == 2);
    constexpr Move none;
    static_assert(none.isNull());
    static_assert(none.raw() == 0);
    CHECK(!Move(E2, E4, MoveFlag::DoublePawnPush).isNull());
}

// Every square pair and flag packs and unpacks to the same values, and the raw bits round-trip.
TEST(move_every_field_combination_round_trips) {
    for (Square from = A1; from <= H8; ++from) {
        for (Square to = A1; to <= H8; ++to) {
            for (const MoveFlag flag : ALL_FLAGS) {
                const Move move(from, to, flag);
                CHECK_EQ(move.from(), from);
                CHECK_EQ(move.to(), to);
                CHECK_EQ(move.flag(), flag);
                CHECK(Move::fromRaw(move.raw()) == move);
            }
        }
    }
}

TEST(move_bit_layout_matches_documentation) {
    // from e2 (12) in bits 0-5, to e4 (28) in bits 6-11, flag 1 in bits 12-15.
    CHECK_EQ(Move(E2, E4, MoveFlag::DoublePawnPush).raw(), static_cast<std::uint16_t>(12 | (28 << 6) | (1 << 12)));
    CHECK_EQ(Move(H7, H8, MoveFlag::QueenPromotionCapture).raw(),
             static_cast<std::uint16_t>(55 | (63 << 6) | (15 << 12)));
}

TEST(move_flag_queries) {
    for (const MoveFlag flag : ALL_FLAGS) {
        const Move move(B7, B8, flag);
        const int f = static_cast<int>(flag);
        CHECK_EQ(move.isCapture(), f == 4 || f == 5 || f >= 12);
        CHECK_EQ(move.isPromotion(), f >= 8);
        CHECK_EQ(move.isEnPassant(), flag == MoveFlag::EnPassant);
        CHECK_EQ(move.isCastle(), flag == MoveFlag::KingCastle || flag == MoveFlag::QueenCastle);
        CHECK_EQ(move.isDoublePawnPush(), flag == MoveFlag::DoublePawnPush);
    }
}

TEST(move_promotion_types) {
    for (const PieceType type : {PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen}) {
        for (const bool capture : {false, true}) {
            const Move move(G7, G8, promotionFlag(type, capture));
            CHECK(move.isPromotion());
            CHECK_EQ(move.isCapture(), capture);
            CHECK_EQ(move.promotionType(), type);
        }
    }
    CHECK_EQ(promotionFlag(PieceType::Queen, true), MoveFlag::QueenPromotionCapture);
    CHECK_EQ(promotionFlag(PieceType::Knight, false), MoveFlag::KnightPromotion);
}

// ---------- UCI output ----------

TEST(move_to_uci) {
    CHECK_EQ(toUci(Move(E2, E4, MoveFlag::DoublePawnPush)), std::string("e2e4"));
    CHECK_EQ(toUci(Move(G1, F3)), std::string("g1f3"));
    CHECK_EQ(toUci(Move(E1, G1, MoveFlag::KingCastle)), std::string("e1g1"));
    CHECK_EQ(toUci(Move(E7, E8, MoveFlag::QueenPromotion)), std::string("e7e8q"));
    CHECK_EQ(toUci(Move(B2, A1, MoveFlag::KnightPromotionCapture)), std::string("b2a1n"));
    CHECK_EQ(toUci(Move(C7, C8, MoveFlag::RookPromotion)), std::string("c7c8r"));
    CHECK_EQ(toUci(Move(C7, D8, MoveFlag::BishopPromotionCapture)), std::string("c7d8b"));
    CHECK_EQ(toUci(Move()), std::string("0000"));
}

// ---------- UCI input ----------

TEST(move_from_uci_start_position) {
    const Board board;
    CHECK(uciFlag(board, "e2e4") == MoveFlag::DoublePawnPush);
    CHECK(uciFlag(board, "e2e3") == MoveFlag::Quiet);
    CHECK(uciFlag(board, "g1f3") == MoveFlag::Quiet);
    const std::optional<Move> move = moveFromUci(board, "b1c3");
    CHECK(move.has_value());
    CHECK_EQ(move->from(), B1);
    CHECK_EQ(move->to(), C3);
}

TEST(move_from_uci_captures_and_en_passant) {
    Board board = kingsOnly();
    board.placePiece({PieceType::Pawn, Color::White}, E5);
    board.placePiece({PieceType::Pawn, Color::Black}, D5);
    board.placePiece({PieceType::Knight, Color::Black}, F6);
    board.placePiece({PieceType::Rook, Color::White}, A1);
    board.placePiece({PieceType::Rook, Color::Black}, A7);
    board.setEnPassantSquare(D6);

    CHECK(uciFlag(board, "e5d6") == MoveFlag::EnPassant);
    CHECK(uciFlag(board, "e5f6") == MoveFlag::Capture);
    CHECK(uciFlag(board, "e5e6") == MoveFlag::Quiet);
    CHECK(uciFlag(board, "a1a7") == MoveFlag::Capture);
    CHECK(uciFlag(board, "a1a5") == MoveFlag::Quiet);
}

TEST(move_from_uci_castling) {
    Board board = kingsOnly();
    for (const Square sq : {A1, H1}) board.placePiece({PieceType::Rook, Color::White}, sq);
    for (const Square sq : {A8, H8}) board.placePiece({PieceType::Rook, Color::Black}, sq);
    board.setCastlingRights(ALL_CASTLING);

    CHECK(uciFlag(board, "e1g1") == MoveFlag::KingCastle);
    CHECK(uciFlag(board, "e1c1") == MoveFlag::QueenCastle);
    CHECK(uciFlag(board, "e1f1") == MoveFlag::Quiet);
    CHECK(uciFlag(board, "e1d2") == MoveFlag::Quiet);

    board.setSideToMove(Color::Black);
    CHECK(uciFlag(board, "e8g8") == MoveFlag::KingCastle);
    CHECK(uciFlag(board, "e8c8") == MoveFlag::QueenCastle);
}

TEST(move_from_uci_two_square_king_step_off_home_is_not_castling) {
    Board board = Board::empty();
    board.placePiece({PieceType::King, Color::White}, D4);
    board.placePiece({PieceType::King, Color::Black}, H8);
    CHECK(uciFlag(board, "d4f4") == MoveFlag::Quiet);
}

TEST(move_from_uci_promotions) {
    Board board = kingsOnly();
    board.placePiece({PieceType::Pawn, Color::White}, B7);
    board.placePiece({PieceType::Rook, Color::Black}, A8);
    board.placePiece({PieceType::Pawn, Color::Black}, G2);

    CHECK(uciFlag(board, "b7b8q") == MoveFlag::QueenPromotion);
    CHECK(uciFlag(board, "b7b8n") == MoveFlag::KnightPromotion);
    CHECK(uciFlag(board, "b7a8r") == MoveFlag::RookPromotionCapture);
    CHECK(uciFlag(board, "b7a8b") == MoveFlag::BishopPromotionCapture);

    board.setSideToMove(Color::Black);
    CHECK(uciFlag(board, "g2g1q") == MoveFlag::QueenPromotion);
}

TEST(move_from_uci_rejects_bad_input) {
    Board board = kingsOnly();
    board.placePiece({PieceType::Pawn, Color::White}, B7);
    board.placePiece({PieceType::Pawn, Color::White}, C2);
    board.placePiece({PieceType::Knight, Color::White}, G1);
    board.placePiece({PieceType::Knight, Color::Black}, B8);

    const char* bad[] = {
        "", "e2", "e2e", "e2e4qq", "i2i4", "e0e4", "E2E4", // malformed
        "c2c2",                                            // same square
        "d4d5",                                            // nothing on d4
        "b8c6",                                            // black piece, white to move
        "g1e1",                                            // captures own king
        "b7b8",                                            // promotion letter missing
        "b7b8k", "b7b8p", "b7b8Q",                         // not a promotion piece
        "c2c3q",                                           // promotion letter on a normal pawn move
        "g1f3q",                                           // only pawns promote
    };
    for (const char* text : bad) {
        CHECK(!moveFromUci(board, text).has_value());
    }
}

TEST(move_from_uci_null_move) {
    const std::optional<Move> move = moveFromUci(Board(), "0000");
    CHECK(move.has_value());
    CHECK(move->isNull());
}

// Text written by toUci reads back as the same move on a board where that move makes sense.
TEST(move_uci_round_trip) {
    Board board = kingsOnly();
    board.placePiece({PieceType::Pawn, Color::White}, E5);
    board.placePiece({PieceType::Pawn, Color::Black}, D5);
    board.placePiece({PieceType::Pawn, Color::White}, H7);
    board.placePiece({PieceType::Knight, Color::Black}, G8);
    board.placePiece({PieceType::Rook, Color::White}, H1);
    board.setCastlingRights(WHITE_KINGSIDE);
    board.setEnPassantSquare(D6);

    const Move moves[] = {
        Move(E5, D6, MoveFlag::EnPassant),
        Move(E5, E6),
        Move(H7, G8, MoveFlag::QueenPromotionCapture),
        Move(H7, H8, MoveFlag::KnightPromotion),
        Move(E1, G1, MoveFlag::KingCastle),
        Move(H1, H5),
    };
    for (const Move move : moves) {
        const std::optional<Move> parsed = moveFromUci(board, toUci(move));
        CHECK(parsed.has_value());
        CHECK(*parsed == move);
    }
}

// Only a diagonal pawn move onto the en passant square is an en passant capture. moveFromUci doesn't check
// geometry, so a nonsense straight move there must at least not be mislabeled.
TEST(move_from_uci_straight_move_onto_en_passant_square_is_not_en_passant) {
    Board board = kingsOnly();
    board.placePiece({PieceType::Pawn, Color::White}, D4);
    board.placePiece({PieceType::Pawn, Color::Black}, C5);
    board.setEnPassantSquare(D6);
    CHECK(uciFlag(board, "d4d6") != MoveFlag::EnPassant);
}
