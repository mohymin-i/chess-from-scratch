#include <cstdlib>
#include <random>
#include <string>

#include "board.h"
#include "movegen.h"
#include "test_framework.h"

namespace {
// The standard perft positions from the Chess Programming Wiki plus a few with en passant squares.
// All of them are already in canonical form, so toFen must give back exactly the same text.
constexpr const char* KNOWN_POSITIONS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", // "Kiwipete"
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",   // after 1. e4
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq c6 0 2", // after 1. e4 c5
    "4k3/8/8/8/8/8/8/4K3 b - - 99 300",
};

// Returns the FenError message, or "" if the FEN was accepted.
std::string fenError(const std::string& fen) {
    try {
        (void)Board::fromFen(fen);
    } catch (const FenError& e) {
        return e.what();
    }
    return "";
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }
} // namespace

// ---------- Reading and writing ----------

TEST(fen_start_position_matches_default_board) {
    CHECK(Board::fromFen(START_FEN) == Board());
    CHECK_EQ(Board().toFen(), std::string(START_FEN));
}

TEST(fen_known_positions_round_trip_exactly) {
    for (const char* fen : KNOWN_POSITIONS) {
        const Board board = Board::fromFen(fen);
        CHECK(board.isConsistent());
        CHECK_EQ(board.toFen(), std::string(fen));
    }
}

TEST(fen_reads_every_field) {
    const Board board = Board::fromFen("rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w Kq c6 3 17");
    CHECK(board.pieceAt(E4) == (Piece{PieceType::Pawn, Color::White}));
    CHECK(board.pieceAt(C5) == (Piece{PieceType::Pawn, Color::Black}));
    CHECK(board.pieceAt(E2) == std::nullopt);
    CHECK(board.pieceAt(A8) == (Piece{PieceType::Rook, Color::Black}));
    CHECK_EQ(popCount(board.occupancy()), 32);
    CHECK_EQ(board.sideToMove(), Color::White);
    CHECK_EQ(board.castlingRights(), static_cast<std::uint8_t>(WHITE_KINGSIDE | BLACK_QUEENSIDE));
    CHECK_EQ(board.enPassantSquare(), C6);
    CHECK_EQ(board.halfmoveClock(), 3);
    CHECK_EQ(board.fullmoveNumber(), 17);
}

TEST(fen_writes_board_built_by_hand) {
    Board board = Board::empty();
    board.placePiece({PieceType::King, Color::White}, G1);
    board.placePiece({PieceType::Rook, Color::White}, F1);
    board.placePiece({PieceType::King, Color::Black}, B8);
    board.placePiece({PieceType::Queen, Color::Black}, H8);
    board.placePiece({PieceType::Pawn, Color::Black}, D4);
    board.setSideToMove(Color::Black);
    board.setHalfmoveClock(12);
    board.setFullmoveNumber(40);
    CHECK_EQ(board.toFen(), std::string("1k5q/8/8/8/3p4/8/8/5RK1 b - - 12 40"));
}

TEST(fen_castling_letters_are_written_in_standard_order) {
    const Board board = Board::fromFen("r3k2r/8/8/8/8/8/8/R3K2R w qkQK - 0 1");
    CHECK_EQ(board.castlingRights(), static_cast<std::uint8_t>(ALL_CASTLING));
    CHECK_EQ(board.toFen(), std::string("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"));
}

TEST(fen_move_counters_are_optional) {
    const Board board = Board::fromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -");
    CHECK_EQ(board.halfmoveClock(), 0);
    CHECK_EQ(board.fullmoveNumber(), 1);
    CHECK(board == Board());
}

TEST(fen_extra_whitespace_is_ignored) {
    const Board board = Board::fromFen("  rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR   w\tKQkq  -  0  1 \r\n");
    CHECK(board == Board());
}

TEST(fen_board_equality_compares_game_state) {
    Board a;
    Board b;
    CHECK(a == b);
    b.setHalfmoveClock(1);
    CHECK(!(a == b));
    b.setHalfmoveClock(0);
    b.setCastlingRights(WHITE_KINGSIDE);
    CHECK(!(a == b));
    b.setCastlingRights(ALL_CASTLING);
    b.removePiece(A2);
    CHECK(!(a == b));
}

// ---------- Rejecting bad input ----------

TEST(fen_rejects_malformed_text) {
    const char* bad[] = {
        "",
        "   ",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR",               // one field
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq",        // three fields
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0",    // five fields
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1 x", // seven fields
        // Placement
        "rnbqkbnr/pppppppp/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",      // 7 ranks
        "rnbqkbnr/pppppppp/8/8/8/8/8/8/RNBQKBNR w KQkq - 0 1",         // 9 ranks
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR/ w KQkq - 0 1",   // trailing slash
        "rnbqkbnr/pppppppp/8//8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",     // empty rank
        "rnbqkbnr/ppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",     // 7 squares
        "rnbqkbnr/ppppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",   // 9 squares
        "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",    // digit 9
        "rnbqkbnr/pppppppp/0/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",    // digit 0
        "rnbqkbnr/pppppppp/44/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",   // two digits in a row
        "rnbqkbnr/pppppppp/7p1/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",  // too many after a digit
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNX w KQkq - 0 1",    // unknown letter
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBN1 w KQkq - 0 1",    // h1 rook missing but 'K'
        // Side to move
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR W KQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR wb KQkq - 0 1",
        // Castling
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkx - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KKQkq - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w -K - 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w -- - 0 1",
        // En passant
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e9 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq E6 0 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq e 0 1",
        // Counters
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - -1 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - +1 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 1x 1",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 0",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1.5",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 99999999999999999999 1",
    };
    for (const char* fen : bad) {
        CHECK_THROWS(Board::fromFen(fen), FenError);
    }
}

TEST(fen_rejects_positions_the_engine_cannot_handle) {
    const char* bad[] = {
        "8/8/8/8/8/8/8/4K3 w - - 0 1",                // no black king
        "4k3/8/8/8/8/8/8/8 w - - 0 1",                // no white king
        "4k3/8/8/8/8/8/8/3KK3 w - - 0 1",             // two white kings
        "8/8/8/8/8/8/3k4/4K3 w - - 0 1",              // kings touching diagonally
        "8/8/8/8/8/8/8/3kK3 w - - 0 1",               // kings side by side
        "P3k3/8/8/8/8/8/8/4K3 w - - 0 1",             // white pawn on rank 8
        "4k3/8/8/8/8/8/8/p3K3 w - - 0 1",             // black pawn on rank 1
        "4k3/8/8/8/8/8/8/R4K1R w K - 0 1",            // 'K' but king not on e1
        "4k3/8/8/8/8/8/8/R3K3 w K - 0 1",             // 'K' but no rook on h1
        "4k3/8/8/8/8/8/8/r3K2R w Q - 0 1",            // 'Q' but a black rook on a1
        "r3k3/8/8/8/8/8/8/4K3 w k - 0 1",             // 'k' but no rook on h8
        "4k3/8/8/8/4P3/8/8/4K3 w - e3 0 1",           // e3 but white is to move
        "4k3/8/8/8/8/8/8/4K3 b - e3 0 1",             // e3 but no pawn on e4
        "4k3/8/8/8/4p3/8/8/4K3 b - e3 0 1",           // e3 but the pawn on e4 is black's
        "4k3/8/8/8/4P3/4N3/8/4K3 b - e3 0 1",         // e3 occupied
        "4k3/8/8/8/4P3/8/4N3/4K3 b - e3 0 1",         // e2 occupied, so the pawn didn't come from there
        "4k3/8/8/4p3/8/8/8/4K3 w - e5 0 1",           // en passant square on the wrong rank
        "4k3/8/8/8/8/8/8/4K3 w - a1 0 1",
    };
    for (const char* fen : bad) {
        CHECK_THROWS(Board::fromFen(fen), FenError);
    }
}

TEST(fen_error_messages_name_the_problem) {
    CHECK(contains(fenError("4k3/8/8/8/8/8/8/R3K3 w K - 0 1"), "no rook on h1"));
    CHECK(contains(fenError("4k3/8/8/8/8/8/8/4K3 x - - 0 1"), "side to move"));
    CHECK(contains(fenError("4k3/8/8/8/8/8/8/3KK3 w - - 0 1"), "white has 2 kings"));
    CHECK(contains(fenError("4k3/8/8/8/8/8/8/4K3 w - - 0 0"), "fullmove number"));
    CHECK(contains(fenError("4k3/8/8/8/8/8/7/4K3 w - - 0 1"), "rank 2 has 7 squares"));
    CHECK(contains(fenError("4k3/8/8/8/8/8/8/8/4K3 w - - 0 1"), "more than 8 ranks"));
    CHECK_EQ(fenError(std::string(START_FEN)), std::string(""));
}

TEST(fen_error_is_an_invalid_argument) {
    CHECK_THROWS(Board::fromFen("nonsense"), std::invalid_argument);
}

// ---------- Randomized ----------

// Random legal-looking boards survive toFen -> fromFen unchanged.
TEST(fen_random_boards_round_trip) {
    std::mt19937 rng(6061);
    std::uniform_int_distribution<int> squareDist(0, NUM_SQUARES - 1);
    std::uniform_int_distribution<int> typeDist(0, NUM_PIECE_TYPES - 2); // everything but the king
    std::uniform_int_distribution<int> colorDist(0, 1);
    std::uniform_int_distribution<int> countDist(0, 30);
    std::uniform_int_distribution<int> clockDist(0, 200);

    int checked = 0;
    for (int round = 0; round < 2000; ++round) {
        Board board = Board::empty();
        Square whiteKing;
        Square blackKing;
        do {
            whiteKing = static_cast<Square>(squareDist(rng));
            blackKing = static_cast<Square>(squareDist(rng));
        } while (std::abs(fileOf(whiteKing) - fileOf(blackKing)) <= 1 &&
                 std::abs(rankOf(whiteKing) - rankOf(blackKing)) <= 1);
        board.placePiece({PieceType::King, Color::White}, whiteKing);
        board.placePiece({PieceType::King, Color::Black}, blackKing);

        const int extras = countDist(rng);
        for (int i = 0; i < extras; ++i) {
            const auto sq = static_cast<Square>(squareDist(rng));
            const auto type = static_cast<PieceType>(typeDist(rng));
            if (board.pieceAt(sq)) continue;
            if (type == PieceType::Pawn && (rankOf(sq) == 0 || rankOf(sq) == 7)) continue;
            board.placePiece({type, static_cast<Color>(colorDist(rng))}, sq);
        }
        board.setSideToMove(static_cast<Color>(colorDist(rng)));
        board.setHalfmoveClock(clockDist(rng));
        board.setFullmoveNumber(1 + clockDist(rng));
        if (moverLeftInCheck(board)) continue; // not a reachable position, and fromFen rightly rejects it
        ++checked;

        const std::string fen = board.toFen();
        const Board parsed = Board::fromFen(fen);
        CHECK(parsed == board);
        CHECK_EQ(parsed.toFen(), fen);
    }
    CHECK(checked > 1000); // most random boards are still usable
}

// Randomly damaged FEN strings either throw FenError or give a consistent board. Never anything else.
TEST(fen_damaged_input_never_half_loads) {
    std::mt19937 rng(1123);
    const std::string alphabet = "pnbrqkPNBRQK12345678/ wb-KQkqabcdefgh0123456789x";
    std::uniform_int_distribution<int> letterDist(0, static_cast<int>(alphabet.size()) - 1);
    std::uniform_int_distribution<int> actionDist(0, 2);
    std::uniform_int_distribution<int> editsDist(1, 4);

    int accepted = 0;
    int rejected = 0;
    for (int round = 0; round < 20000; ++round) {
        std::string fen = KNOWN_POSITIONS[round % std::size(KNOWN_POSITIONS)];
        const int edits = editsDist(rng);
        for (int e = 0; e < edits; ++e) {
            std::uniform_int_distribution<int> posDist(0, static_cast<int>(fen.size()) - 1);
            const auto pos = static_cast<std::size_t>(posDist(rng));
            const char letter = alphabet[letterDist(rng)];
            switch (actionDist(rng)) {
                case 0: fen[pos] = letter; break;
                case 1: fen.erase(pos, 1); break;
                default: fen.insert(pos, 1, letter); break;
            }
            if (fen.empty()) fen = "x";
        }

        try {
            const Board board = Board::fromFen(fen);
            ++accepted;
            CHECK(board.isConsistent());
            CHECK(Board::fromFen(board.toFen()) == board);
        } catch (const FenError&) {
            ++rejected; // expected for most damaged strings
        }
    }
    // Both outcomes must actually happen, or the test isn't exercising anything. Some edits (a changed counter,
    // a swapped piece) still leave valid FEN.
    CHECK(accepted > 0);
    CHECK(rejected > 0);
}
