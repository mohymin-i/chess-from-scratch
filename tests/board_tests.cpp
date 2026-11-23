#include <climits>
#include <optional>
#include <random>
#include <stdexcept>

#include "board.h"
#include "test_framework.h"

namespace {
constexpr Color COLORS[] = {Color::White, Color::Black};
constexpr PieceType TYPES[] = {PieceType::Pawn, PieceType::Knight, PieceType::Bishop,
                               PieceType::Rook, PieceType::Queen,  PieceType::King};

Piece piece(PieceType type, Color color) { return Piece{type, color}; }

// Mirrors a bitboard top-to-bottom (rank 1 <-> rank 8). Written independently of the Board code.
Bitboard flipVertical(Bitboard b) {
    Bitboard result = 0;
    for (int rank = 0; rank < 8; ++rank) {
        result |= ((b >> (8 * rank)) & 0xFFULL) << (8 * (7 - rank));
    }
    return result;
}

bool sameBoard(const Board& a, const Board& b) {
    for (Color c : COLORS)
        for (PieceType t : TYPES)
            if (a.pieces(c, t) != b.pieces(c, t)) return false;
    return a.occupancy(Color::White) == b.occupancy(Color::White) &&
           a.occupancy(Color::Black) == b.occupancy(Color::Black);
}
} // namespace

// ---------- Starting position ----------

TEST(board_starting_bitboards_match_documented_values) {
    const Board board;
    CHECK_EQ(board.pieces(Color::White, PieceType::Pawn),   0x000000000000FF00ULL);
    CHECK_EQ(board.pieces(Color::White, PieceType::Knight), 0x0000000000000042ULL);
    CHECK_EQ(board.pieces(Color::White, PieceType::Bishop), 0x0000000000000024ULL);
    CHECK_EQ(board.pieces(Color::White, PieceType::Rook),   0x0000000000000081ULL);
    CHECK_EQ(board.pieces(Color::White, PieceType::Queen),  0x0000000000000008ULL);
    CHECK_EQ(board.pieces(Color::White, PieceType::King),   0x0000000000000010ULL);
    CHECK_EQ(board.pieces(Color::Black, PieceType::Pawn),   0x00FF000000000000ULL);
    CHECK_EQ(board.pieces(Color::Black, PieceType::Knight), 0x4200000000000000ULL);
    CHECK_EQ(board.pieces(Color::Black, PieceType::Bishop), 0x2400000000000000ULL);
    CHECK_EQ(board.pieces(Color::Black, PieceType::Rook),   0x8100000000000000ULL);
    CHECK_EQ(board.pieces(Color::Black, PieceType::Queen),  0x0800000000000000ULL);
    CHECK_EQ(board.pieces(Color::Black, PieceType::King),   0x1000000000000000ULL);
}

TEST(board_starting_piece_counts) {
    const Board board;
    for (Color c : COLORS) {
        CHECK_EQ(popCount(board.pieces(c, PieceType::Pawn)), 8);
        CHECK_EQ(popCount(board.pieces(c, PieceType::Knight)), 2);
        CHECK_EQ(popCount(board.pieces(c, PieceType::Bishop)), 2);
        CHECK_EQ(popCount(board.pieces(c, PieceType::Rook)), 2);
        CHECK_EQ(popCount(board.pieces(c, PieceType::Queen)), 1);
        CHECK_EQ(popCount(board.pieces(c, PieceType::King)), 1);
        CHECK_EQ(popCount(board.occupancy(c)), 16);
    }
}

TEST(board_starting_occupancy) {
    const Board board;
    CHECK_EQ(board.occupancy(Color::White), 0x000000000000FFFFULL);
    CHECK_EQ(board.occupancy(Color::Black), 0xFFFF000000000000ULL);
    CHECK_EQ(board.occupancy(), 0xFFFF00000000FFFFULL);
    CHECK_EQ(board.occupancy(Color::White) & board.occupancy(Color::Black), 0ULL);
}

TEST(board_starting_piece_bitboards_never_overlap) {
    const Board board;
    for (Color c1 : COLORS)
        for (PieceType t1 : TYPES)
            for (Color c2 : COLORS)
                for (PieceType t2 : TYPES)
                    if (c1 != c2 || t1 != t2) CHECK((board.pieces(c1, t1) & board.pieces(c2, t2)) == 0);
}

TEST(board_black_setup_mirrors_white) {
    const Board board;
    for (PieceType t : TYPES) {
        CHECK_EQ(board.pieces(Color::Black, t), flipVertical(board.pieces(Color::White, t)));
    }
}

TEST(board_starting_pieces_on_named_squares) {
    const Board board;
    // Queen on her own color, king on e-file: the usual setup mistakes.
    CHECK(board.pieceAt(D1) == piece(PieceType::Queen, Color::White));
    CHECK(board.pieceAt(E1) == piece(PieceType::King, Color::White));
    CHECK(board.pieceAt(D8) == piece(PieceType::Queen, Color::Black));
    CHECK(board.pieceAt(E8) == piece(PieceType::King, Color::Black));

    CHECK(board.pieceAt(A1) == piece(PieceType::Rook, Color::White));
    CHECK(board.pieceAt(B1) == piece(PieceType::Knight, Color::White));
    CHECK(board.pieceAt(C1) == piece(PieceType::Bishop, Color::White));
    CHECK(board.pieceAt(F1) == piece(PieceType::Bishop, Color::White));
    CHECK(board.pieceAt(G1) == piece(PieceType::Knight, Color::White));
    CHECK(board.pieceAt(H1) == piece(PieceType::Rook, Color::White));
    CHECK(board.pieceAt(A8) == piece(PieceType::Rook, Color::Black));
    CHECK(board.pieceAt(G8) == piece(PieceType::Knight, Color::Black));

    for (int file = 0; file < 8; ++file) {
        CHECK(board.pieceAt(makeSquare(file, 1)) == piece(PieceType::Pawn, Color::White));
        CHECK(board.pieceAt(makeSquare(file, 6)) == piece(PieceType::Pawn, Color::Black));
    }
}

TEST(board_middle_ranks_start_empty_and_back_ranks_full) {
    const Board board;
    for (Square s = A1; s <= H8; ++s) {
        const bool shouldBeEmpty = rankOf(s) >= 2 && rankOf(s) <= 5;
        CHECK_EQ(board.pieceAt(s).has_value(), !shouldBeEmpty);
    }
}

TEST(board_starting_game_state) {
    const Board board;
    CHECK(board.sideToMove() == Color::White);
    CHECK_EQ(board.castlingRights(), ALL_CASTLING);
    CHECK_EQ(board.enPassantSquare(), NO_SQUARE);
    CHECK(board.isConsistent());
}

TEST(board_to_string_renders_starting_position) {
    const Board board;
    CHECK_EQ(board.toString(), std::string("8 r n b q k b n r\n"
                                           "7 p p p p p p p p\n"
                                           "6 . . . . . . . .\n"
                                           "5 . . . . . . . .\n"
                                           "4 . . . . . . . .\n"
                                           "3 . . . . . . . .\n"
                                           "2 P P P P P P P P\n"
                                           "1 R N B Q K B N R\n"
                                           "  a b c d e f g h\n"));
}

// ---------- Empty board ----------

TEST(board_empty_has_no_pieces) {
    const Board board = Board::empty();
    CHECK_EQ(board.occupancy(), 0ULL);
    for (Color c : COLORS)
        for (PieceType t : TYPES) CHECK_EQ(board.pieces(c, t), 0ULL);
    for (Square s = A1; s <= H8; ++s) CHECK(!board.pieceAt(s).has_value());
    CHECK(board.sideToMove() == Color::White);
    CHECK_EQ(board.castlingRights(), 0);
    CHECK_EQ(board.enPassantSquare(), NO_SQUARE);
    CHECK(board.isConsistent());
}

TEST(board_to_string_renders_empty_board) {
    const Board board = Board::empty();
    std::string expected;
    for (int rank = 8; rank >= 1; --rank) expected += std::to_string(rank) + " . . . . . . . .\n";
    expected += "  a b c d e f g h\n";
    CHECK_EQ(board.toString(), expected);
}

// ---------- Placing and removing ----------

TEST(board_place_piece_on_empty_square) {
    Board board = Board::empty();
    board.placePiece(piece(PieceType::Knight, Color::White), E4);
    CHECK(board.pieceAt(E4) == piece(PieceType::Knight, Color::White));
    CHECK_EQ(board.pieces(Color::White, PieceType::Knight), squareBit(E4));
    CHECK_EQ(board.occupancy(Color::White), squareBit(E4));
    CHECK_EQ(board.occupancy(Color::Black), 0ULL);
    CHECK(board.isConsistent());
}

TEST(board_place_piece_on_every_square) {
    for (Square s = A1; s <= H8; ++s) {
        Board board = Board::empty();
        board.placePiece(piece(PieceType::Bishop, Color::Black), s);
        CHECK(board.pieceAt(s) == piece(PieceType::Bishop, Color::Black));
        CHECK_EQ(board.occupancy(), squareBit(s));
        CHECK(board.isConsistent());
    }
}

TEST(board_placing_on_occupied_square_replaces_the_piece) {
    Board board;
    board.placePiece(piece(PieceType::Queen, Color::White), D8); // "captures" the black queen
    CHECK(board.pieceAt(D8) == piece(PieceType::Queen, Color::White));
    CHECK_EQ(board.pieces(Color::Black, PieceType::Queen), 0ULL);
    CHECK_EQ(popCount(board.pieces(Color::White, PieceType::Queen)), 2);
    CHECK_EQ(popCount(board.occupancy(Color::White)), 17);
    CHECK_EQ(popCount(board.occupancy(Color::Black)), 15);
    CHECK(board.isConsistent());
}

TEST(board_placing_same_piece_twice_is_idempotent) {
    Board board = Board::empty();
    board.placePiece(piece(PieceType::Rook, Color::White), A1);
    board.placePiece(piece(PieceType::Rook, Color::White), A1);
    CHECK_EQ(popCount(board.occupancy()), 1);
    CHECK(board.isConsistent());
}

TEST(board_remove_piece_clears_square) {
    Board board;
    board.removePiece(E2);
    CHECK(!board.pieceAt(E2).has_value());
    CHECK_EQ(popCount(board.pieces(Color::White, PieceType::Pawn)), 7);
    CHECK_EQ(popCount(board.occupancy(Color::White)), 15);
    CHECK_EQ(popCount(board.occupancy(Color::Black)), 16);
    CHECK(board.isConsistent());
}

TEST(board_removing_from_empty_square_changes_nothing) {
    Board board;
    const Board before = board;
    board.removePiece(E4);
    CHECK(sameBoard(board, before));
}

TEST(board_remove_every_piece_leaves_empty_board) {
    Board board;
    for (Square s = A1; s <= H8; ++s) board.removePiece(s);
    CHECK_EQ(board.occupancy(), 0ULL);
    CHECK(sameBoard(board, Board::empty()));
    CHECK(board.isConsistent());
}

TEST(board_copies_are_independent) {
    const Board original;
    Board copy = original;
    copy.removePiece(E1);
    copy.placePiece(piece(PieceType::Queen, Color::Black), E4);
    CHECK(original.pieceAt(E1) == piece(PieceType::King, Color::White));
    CHECK(!original.pieceAt(E4).has_value());
    CHECK(sameBoard(original, Board()));
}

// ---------- Bad input must throw, not crash or corrupt the board ----------

TEST(board_invalid_squares_throw_and_leave_board_unchanged) {
    Board board;
    for (int raw : {-1, 64, 100, -100, INT_MIN, INT_MAX}) {
        const auto bad = static_cast<Square>(raw);
        CHECK_THROWS(board.pieceAt(bad), std::out_of_range);
        CHECK_THROWS(board.placePiece(piece(PieceType::Pawn, Color::White), bad), std::out_of_range);
        CHECK_THROWS(board.removePiece(bad), std::out_of_range);
    }
    CHECK(sameBoard(board, Board()));
    CHECK(board.isConsistent());
}

// ---------- Stress test ----------

// Thousands of random place/remove operations, checked against a simple 64-square array
// that is obviously correct. Catches bookkeeping bugs that hand-written cases miss.
TEST(board_random_operations_match_simple_model) {
    std::mt19937 rng(20261123); // fixed seed so failures are reproducible
    std::uniform_int_distribution<int> squareDist(0, NUM_SQUARES - 1);
    std::uniform_int_distribution<int> typeDist(0, NUM_PIECE_TYPES - 1);
    std::uniform_int_distribution<int> colorDist(0, NUM_COLORS - 1);
    std::uniform_int_distribution<int> actionDist(0, 9);

    Board board = Board::empty();
    std::optional<Piece> model[NUM_SQUARES];

    for (int step = 0; step < 20000; ++step) {
        const auto s = static_cast<Square>(squareDist(rng));
        if (actionDist(rng) < 6) {
            const Piece p{static_cast<PieceType>(typeDist(rng)), static_cast<Color>(colorDist(rng))};
            board.placePiece(p, s);
            model[s] = p;
        } else {
            board.removePiece(s);
            model[s].reset();
        }

        CHECK(board.pieceAt(s) == model[s]);
        CHECK(board.isConsistent());

        if (step % 250 == 0) {
            Bitboard expected[NUM_COLORS] = {};
            for (Square t = A1; t <= H8; ++t) {
                CHECK(board.pieceAt(t) == model[t]);
                if (model[t]) expected[static_cast<int>(model[t]->color)] |= squareBit(t);
            }
            CHECK_EQ(board.occupancy(Color::White), expected[0]);
            CHECK_EQ(board.occupancy(Color::Black), expected[1]);
        }
    }
}

// ---------- Square lookup vs bitboards ----------

namespace {
// Checks pieceAt() against the bitboards directly, without trusting isConsistent().
bool lookupMatchesBitboards(const Board& board) {
    for (Square s = A1; s <= H8; ++s) {
        std::optional<Piece> fromBitboards;
        for (Color c : COLORS)
            for (PieceType t : TYPES)
                if (board.pieces(c, t) & squareBit(s)) fromBitboards = Piece{t, c};
        if (board.pieceAt(s) != fromBitboards) return false;
    }
    return true;
}
} // namespace

TEST(board_lookup_matches_bitboards_after_every_kind_of_change) {
    Board board;
    CHECK(lookupMatchesBitboards(board));
    board.removePiece(E2);                                         // remove
    CHECK(lookupMatchesBitboards(board));
    board.placePiece(piece(PieceType::Pawn, Color::White), E4);    // place on empty
    CHECK(lookupMatchesBitboards(board));
    board.placePiece(piece(PieceType::Knight, Color::Black), E4);  // replace other color
    CHECK(lookupMatchesBitboards(board));
    board.placePiece(piece(PieceType::Bishop, Color::Black), E4);  // replace same color
    CHECK(lookupMatchesBitboards(board));
    CHECK(lookupMatchesBitboards(Board::empty()));
}

// ---------- Unchecked fast paths ----------

TEST(board_unchecked_operations_match_checked_ones) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> squareDist(0, NUM_SQUARES - 1);
    std::uniform_int_distribution<int> typeDist(0, NUM_PIECE_TYPES - 1);
    std::uniform_int_distribution<int> colorDist(0, NUM_COLORS - 1);
    std::uniform_int_distribution<int> actionDist(0, 1);

    Board checked;
    Board unchecked;
    for (int step = 0; step < 5000; ++step) {
        const auto s = static_cast<Square>(squareDist(rng));
        if (actionDist(rng) == 0) {
            const Piece p{static_cast<PieceType>(typeDist(rng)), static_cast<Color>(colorDist(rng))};
            checked.placePiece(p, s);
            unchecked.placePieceUnchecked(p, s);
        } else {
            checked.removePiece(s);
            unchecked.removePieceUnchecked(s);
        }
        CHECK(unchecked.pieceAtUnchecked(s) == checked.pieceAt(s));
        CHECK(unchecked.isConsistent());
    }
    CHECK(sameBoard(checked, unchecked));
}

// ---------- Game-state fields ----------

TEST(board_move_counters_start_at_fen_defaults) {
    for (const Board& board : {Board(), Board::empty()}) {
        CHECK_EQ(board.halfmoveClock(), 0);
        CHECK_EQ(board.fullmoveNumber(), 1);
    }
}

TEST(board_state_setters_store_values) {
    Board board;
    board.setSideToMove(Color::Black);
    board.setCastlingRights(WHITE_KINGSIDE | BLACK_QUEENSIDE);
    board.setEnPassantSquare(E3);
    board.setHalfmoveClock(37);
    board.setFullmoveNumber(52);
    CHECK(board.sideToMove() == Color::Black);
    CHECK_EQ(board.castlingRights(), WHITE_KINGSIDE | BLACK_QUEENSIDE);
    CHECK_EQ(board.enPassantSquare(), E3);
    CHECK_EQ(board.halfmoveClock(), 37);
    CHECK_EQ(board.fullmoveNumber(), 52);
    CHECK(board.isConsistent());

    board.setEnPassantSquare(NO_SQUARE);
    CHECK_EQ(board.enPassantSquare(), NO_SQUARE);
}

TEST(board_every_castling_combination_is_accepted) {
    Board board;
    for (int rights = 0; rights <= ALL_CASTLING; ++rights) {
        board.setCastlingRights(static_cast<std::uint8_t>(rights));
        CHECK_EQ(board.castlingRights(), rights);
        CHECK(board.isConsistent());
    }
}

TEST(board_unknown_castling_flags_are_rejected) {
    Board board;
    for (int bad : {16, 31, 128, 255}) {
        CHECK_THROWS(board.setCastlingRights(static_cast<std::uint8_t>(bad)), std::invalid_argument);
        CHECK_EQ(board.castlingRights(), ALL_CASTLING); // unchanged
    }
}

TEST(board_en_passant_square_must_be_on_rank_3_or_6) {
    Board board;
    for (Square s = A1; s <= H8; ++s) {
        if (rankOf(s) == 2 || rankOf(s) == 5) {
            board.setEnPassantSquare(s);
            CHECK_EQ(board.enPassantSquare(), s);
        } else {
            board.setEnPassantSquare(NO_SQUARE);
            CHECK_THROWS(board.setEnPassantSquare(s), std::invalid_argument);
            CHECK_EQ(board.enPassantSquare(), NO_SQUARE); // unchanged
        }
    }
    for (int raw : {-2, 64, INT_MIN, INT_MAX}) {
        CHECK_THROWS(board.setEnPassantSquare(static_cast<Square>(raw)), std::out_of_range);
    }
    CHECK(board.isConsistent());
}

TEST(board_move_counters_reject_impossible_values) {
    Board board;
    board.setHalfmoveClock(0);
    board.setHalfmoveClock(150);
    CHECK_THROWS(board.setHalfmoveClock(-1), std::invalid_argument);
    CHECK_THROWS(board.setHalfmoveClock(INT_MIN), std::invalid_argument);
    CHECK_EQ(board.halfmoveClock(), 150);

    board.setFullmoveNumber(1);
    board.setFullmoveNumber(INT_MAX);
    CHECK_THROWS(board.setFullmoveNumber(0), std::invalid_argument);
    CHECK_THROWS(board.setFullmoveNumber(-5), std::invalid_argument);
    CHECK_EQ(board.fullmoveNumber(), INT_MAX);
}

TEST(board_copies_keep_game_state) {
    Board board;
    board.setSideToMove(Color::Black);
    board.setEnPassantSquare(D6);
    board.setHalfmoveClock(3);
    board.setFullmoveNumber(9);
    const Board copy = board;
    CHECK(copy.sideToMove() == Color::Black);
    CHECK_EQ(copy.enPassantSquare(), D6);
    CHECK_EQ(copy.halfmoveClock(), 3);
    CHECK_EQ(copy.fullmoveNumber(), 9);
}
