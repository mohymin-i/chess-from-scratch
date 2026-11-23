#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "movegen.h"
#include "test_framework.h"

namespace {
struct PerftCase {
    const char* fen;
    std::vector<std::uint64_t> counts; // counts[d - 1] is perft(d)
};

// Published counts from the Chess Programming Wiki "Perft Results" page. Deeper levels are in perft_deep.cpp.
const PerftCase SHALLOW[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", {20, 400, 8902, 197281}},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", {48, 2039, 97862}},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", {14, 191, 2812, 43238}},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", {6, 264, 9467}},
    {"r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", {6, 264, 9467}}, // mirror of the above
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", {44, 1486, 62379}},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", {46, 2079, 89890}},
};

Board fen(const char* text) { return Board::fromFen(text); }

std::vector<std::string> legalUci(const Board& board) {
    MoveList moves;
    generateLegal(board, moves);
    std::vector<std::string> out;
    for (const Move m : moves) out.push_back(toUci(m));
    std::sort(out.begin(), out.end());
    return out;
}

bool hasMove(const Board& board, const char* uci) {
    const auto all = legalUci(board);
    return std::find(all.begin(), all.end(), uci) != all.end();
}

// Plays a move given in UCI text, which must be legal.
void play(Board& board, const char* uci) {
    const std::optional<Move> move = moveFromUci(board, uci);
    if (!move) throw std::runtime_error(std::string("bad test move ") + uci);
    MoveList legal;
    generateLegal(board, legal);
    if (!legal.contains(*move)) throw std::runtime_error(std::string("illegal test move ") + uci);
    board.makeMove(*move);
}
} // namespace

// ---------- Perft ----------

TEST(movegen_perft_matches_published_counts) {
    for (const PerftCase& c : SHALLOW) {
        Board board = fen(c.fen);
        for (std::size_t d = 0; d < c.counts.size(); ++d) {
            CHECK_EQ(perft(board, static_cast<int>(d + 1)), c.counts[d]);
        }
        CHECK(board == fen(c.fen)); // perft must leave the board as it found it
    }
}

TEST(movegen_perft_divide_adds_up) {
    Board board = fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    const auto divide = perftDivide(board, 2);
    CHECK_EQ(static_cast<int>(divide.size()), 48);
    std::uint64_t total = 0;
    for (const auto& [move, count] : divide) total += count;
    CHECK_EQ(total, std::uint64_t{2039});
    CHECK_EQ(perft(board, 0), std::uint64_t{1});
}

// ---------- Specific rules ----------

TEST(movegen_start_position_moves) {
    const auto moves = legalUci(Board());
    CHECK_EQ(static_cast<int>(moves.size()), 20);
    CHECK(hasMove(Board(), "e2e4"));
    CHECK(hasMove(Board(), "g1f3"));
    CHECK(!hasMove(Board(), "e1e2"));
}

TEST(movegen_castling_rules) {
    // Both sides free to castle.
    CHECK(hasMove(fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"), "e1g1"));
    CHECK(hasMove(fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1"), "e1c1"));
    CHECK(hasMove(fen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1"), "e8g8"));
    CHECK(hasMove(fen("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1"), "e8c8"));
    // No rights, no castling.
    CHECK(!hasMove(fen("r3k2r/8/8/8/8/8/8/R3K2R w kq - 0 1"), "e1g1"));
    // Through check: f1 attacked by a rook on f8.
    CHECK(!hasMove(fen("2k2r2/8/8/8/8/8/8/R3K2R w KQ - 0 1"), "e1g1"));
    CHECK(hasMove(fen("2k2r2/8/8/8/8/8/8/R3K2R w KQ - 0 1"), "e1c1"));
    // Into check: g1 attacked.
    CHECK(!hasMove(fen("2k3r1/8/8/8/8/8/8/R3K2R w KQ - 0 1"), "e1g1"));
    // Queenside: b1 may be attacked, but must be empty.
    CHECK(hasMove(fen("1r4k1/8/8/8/8/8/8/R3K2R w KQ - 0 1"), "e1c1"));
    CHECK(!hasMove(fen("6k1/8/8/8/8/8/8/RN2K2R w KQ - 0 1"), "e1c1"));
    // d1 attacked blocks queenside.
    CHECK(!hasMove(fen("3r2k1/8/8/8/8/8/8/R3K2R w KQ - 0 1"), "e1c1"));
    // Piece in the way.
    CHECK(!hasMove(fen("6k1/8/8/8/8/8/8/R3KB1R w KQ - 0 1"), "e1g1"));
}

TEST(movegen_cannot_castle_out_of_check) {
    const Board board = fen("4k3/8/8/8/1b6/8/8/R3K2R w KQ - 0 1"); // bishop b4 checks e1
    CHECK(inCheck(board));
    CHECK(!hasMove(board, "e1g1"));
    CHECK(!hasMove(board, "e1c1"));
}

TEST(movegen_en_passant) {
    Board board = fen("rnbqkbnr/pppppppp/8/4P3/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 2");
    play(board, "d7d5");
    CHECK_EQ(board.enPassantSquare(), D6);
    CHECK(hasMove(board, "e5d6"));
    Board after = board;
    play(after, "e5d6");
    CHECK(after.pieceAt(D5) == std::nullopt); // the captured pawn is gone
    CHECK(after.pieceAt(D6) == (Piece{PieceType::Pawn, Color::White}));
    CHECK(after.isConsistent());
    // The right expires after one move.
    play(board, "g1f3");
    play(board, "g8f6");
    CHECK(!hasMove(board, "e5d6"));
}

// The tricky one: capturing en passant removes two pawns from the same rank, which can expose the king.
TEST(movegen_en_passant_cannot_expose_king_along_rank) {
    const Board board = fen("8/8/8/KPp4r/8/8/8/4k3 w - c6 0 1");
    CHECK(!hasMove(board, "b5c6"));
    const Board free = fen("8/8/8/1Pp4r/K7/8/8/4k3 w - c6 0 1");
    CHECK(hasMove(free, "b5c6"));
}

TEST(movegen_en_passant_square_only_set_when_capturable) {
    Board board;
    play(board, "e2e4");
    CHECK_EQ(board.enPassantSquare(), NO_SQUARE); // no black pawn next to e4
    Board other = fen("rnbqkbnr/pppp1ppp/8/8/4p3/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    play(other, "d2d4");
    CHECK_EQ(other.enPassantSquare(), D3);
}

TEST(movegen_promotions) {
    const Board board = fen("1n5k/P7/8/8/8/8/8/7K w - - 0 1");
    for (const char* m : {"a7a8q", "a7a8r", "a7a8b", "a7a8n", "a7b8q", "a7b8r", "a7b8b", "a7b8n"}) {
        CHECK(hasMove(board, m));
    }
    CHECK(!hasMove(board, "a7a8")); // there is no "promote to nothing"
}

TEST(movegen_pinned_pieces_and_check_evasion) {
    // The knight on e2 is pinned by the rook on e8 and can't move at all.
    const Board pinned = fen("4r2k/8/8/8/8/8/4N3/4K3 w - - 0 1");
    for (const std::string& m : legalUci(pinned)) CHECK(m.rfind("e2", 0) != 0);
    // In double check only king moves are legal.
    const Board doubleCheck = fen("4r2k/8/8/8/8/5n2/3B4/4K3 w - - 0 1");
    CHECK(inCheck(doubleCheck));
    for (const std::string& m : legalUci(doubleCheck)) CHECK(m.rfind("e1", 0) == 0);
}

TEST(movegen_checkmate_and_stalemate_have_no_moves) {
    const Board foolsMate = fen("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
    CHECK(inCheck(foolsMate));
    CHECK(legalUci(foolsMate).empty());
    const Board stalemate = fen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
    CHECK(!inCheck(stalemate));
    CHECK(legalUci(stalemate).empty());
}

TEST(movegen_attack_detection) {
    const Board board = fen("4k3/8/8/3p4/8/2N5/8/R3K3 w - - 0 1");
    CHECK(isSquareAttacked(board, A8, Color::White));  // rook up the a-file
    CHECK(isSquareAttacked(board, D5, Color::White));  // knight c3
    CHECK(isSquareAttacked(board, E4, Color::Black));  // pawn d5
    CHECK(isSquareAttacked(board, C4, Color::Black));  // pawn d5
    CHECK(!isSquareAttacked(board, D4, Color::Black)); // pawns don't attack straight ahead
    CHECK(isSquareAttacked(board, D7, Color::Black));  // king e8
    CHECK(!isSquareAttacked(board, H8, Color::White));
    // Removing a blocker from `occupied` lets the attack through (x-ray).
    const Board xray = fen("4k3/8/8/8/8/8/R2n4/4K3 w - - 0 1");
    CHECK(!isSquareAttacked(xray, H2, Color::White));
    CHECK(isSquareAttacked(xray, H2, Color::White, xray.occupancy() & ~squareBit(D2)));
    const Bitboard attackers = attackersTo(board, D5, board.occupancy());
    CHECK_EQ(attackers, squareBit(C3));
}

TEST(movegen_fen_rejects_side_not_to_move_in_check) {
    CHECK_THROWS(Board::fromFen("4k3/8/8/8/8/8/8/4K2r b - - 0 1"), FenError); // white in check, black to move
    CHECK(Board::fromFen("4k3/8/8/8/8/8/8/4K2r w - - 0 1").sideToMove() == Color::White);
}

// ---------- Make / unmake ----------

namespace {
// Walks every move to `depth` plies and checks that make followed by unmake restores the exact board, and that
// the incremental hash always matches a full recompute.
void checkMakeUnmake(Board& board, int depth) {
    if (depth == 0) return;
    MoveList moves;
    generatePseudoLegal(board, moves);
    for (const Move move : moves) {
        const Board before = board;
        const UndoInfo undo = board.makeMove(move);
        CHECK(board.isConsistent()); // includes hash == computeHash()
        if (!moverLeftInCheck(board)) checkMakeUnmake(board, depth - 1);
        board.unmakeMove(move, undo);
        CHECK(board == before);
    }
}
} // namespace

TEST(movegen_make_unmake_restores_board_exactly) {
    for (const PerftCase& c : SHALLOW) {
        Board board = fen(c.fen);
        checkMakeUnmake(board, 3);
    }
}

TEST(movegen_null_move_round_trips) {
    Board board = fen("rnbqkbnr/pppp1ppp/8/8/3Pp3/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 1");
    const Board before = board;
    const UndoInfo undo = board.makeNullMove();
    CHECK_EQ(board.sideToMove(), Color::White);
    CHECK_EQ(board.enPassantSquare(), NO_SQUARE);
    CHECK(board.isConsistent());
    board.unmakeNullMove(undo);
    CHECK(board == before);
}

TEST(movegen_castling_rights_update) {
    // Moving the king loses both rights.
    Board board = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    play(board, "e1e2");
    CHECK_EQ(board.castlingRights(), static_cast<std::uint8_t>(BLACK_KINGSIDE | BLACK_QUEENSIDE));
    // Moving a rook loses that side only.
    board = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    play(board, "h1h5");
    CHECK_EQ(board.castlingRights(), static_cast<std::uint8_t>(WHITE_QUEENSIDE | BLACK_KINGSIDE | BLACK_QUEENSIDE));
    // Capturing a rook on its home square removes the victim's right.
    board = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    play(board, "a1a8");
    CHECK_EQ(board.castlingRights(), static_cast<std::uint8_t>(WHITE_KINGSIDE | BLACK_KINGSIDE));
    // Castling itself moves the rook.
    board = fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    play(board, "e1c1");
    CHECK(board.pieceAt(D1) == (Piece{PieceType::Rook, Color::White}));
    CHECK(board.pieceAt(A1) == std::nullopt);
    CHECK(board.pieceAt(C1) == (Piece{PieceType::King, Color::White}));
    play(board, "e8g8");
    CHECK(board.pieceAt(F8) == (Piece{PieceType::Rook, Color::Black}));
    CHECK(board.pieceAt(G8) == (Piece{PieceType::King, Color::Black}));
    CHECK_EQ(board.castlingRights(), std::uint8_t{0});
}

TEST(movegen_clocks_update) {
    Board board;
    play(board, "g1f3");
    CHECK_EQ(board.halfmoveClock(), 1);
    CHECK_EQ(board.fullmoveNumber(), 1);
    play(board, "g8f6");
    CHECK_EQ(board.halfmoveClock(), 2);
    CHECK_EQ(board.fullmoveNumber(), 2);
    play(board, "e2e4"); // pawn move resets
    CHECK_EQ(board.halfmoveClock(), 0);
    play(board, "f6e4"); // capture resets
    CHECK_EQ(board.halfmoveClock(), 0);
}

// ---------- Hashing ----------

TEST(movegen_hash_is_order_independent) {
    // The same position reached by different move orders has the same hash.
    Board a;
    for (const char* m : {"g1f3", "g8f6", "b1c3", "b8c6"}) play(a, m);
    Board b;
    for (const char* m : {"b1c3", "b8c6", "g1f3", "g8f6"}) play(b, m);
    CHECK_EQ(a.hash(), b.hash());
    // Knights out and back: same position as the start, except for the clocks, which aren't hashed.
    Board c;
    for (const char* m : {"g1f3", "g8f6", "f3g1", "f6g8"}) play(c, m);
    CHECK_EQ(c.hash(), Board().hash());
    CHECK(!(c == Board())); // halfmove clock and fullmove number differ
    // Side to move matters.
    Board d = Board();
    d.setSideToMove(Color::Black);
    CHECK(d.hash() != Board().hash());
    // Castling rights matter.
    Board e = Board();
    e.setCastlingRights(WHITE_KINGSIDE);
    CHECK(e.hash() != Board().hash());
    CHECK_EQ(Board::empty().hash(), std::uint64_t{0});
}

// Long random games: the incremental hash always matches a recompute, and the board stays consistent.
TEST(movegen_random_games_keep_board_consistent) {
    std::mt19937 rng(424242);
    for (int game = 0; game < 60; ++game) {
        Board board;
        for (int ply = 0; ply < 300; ++ply) {
            MoveList moves;
            generateLegal(board, moves);
            if (moves.empty()) break;
            const Move move = moves[std::uniform_int_distribution<int>(0, moves.size() - 1)(rng)];
            board.makeMove(move);
            CHECK(board.isConsistent());
            CHECK(!moverLeftInCheck(board));
            // Every board reached by play round-trips through FEN.
            CHECK(Board::fromFen(board.toFen()) == board);
        }
    }
}

// ---------- Move lists ----------

TEST(movegen_noisy_and_quiet_split_all_moves) {
    std::mt19937 rng(77);
    for (int game = 0; game < 20; ++game) {
        Board board = fen(SHALLOW[game % std::size(SHALLOW)].fen);
        for (int ply = 0; ply < 40; ++ply) {
            MoveList all;
            MoveList noisy;
            MoveList quiet;
            generatePseudoLegal(board, all);
            generatePseudoLegal(board, noisy, GenType::Noisy);
            generatePseudoLegal(board, quiet, GenType::Quiet);
            CHECK_EQ(noisy.size() + quiet.size(), all.size());
            for (const Move m : noisy) CHECK(all.contains(m) && (m.isCapture() || m.isPromotion()));
            for (const Move m : quiet) CHECK(all.contains(m) && !m.isCapture() && !m.isPromotion());

            MoveList legal;
            generateLegal(board, legal);
            if (legal.empty()) break;
            for (const Move m : legal) {
                CHECK(isLegal(board, m));
                // Every generated move is described correctly in UCI and parses back with the same flags.
                const std::optional<Move> parsed = moveFromUci(board, toUci(m));
                CHECK(parsed.has_value() && *parsed == m);
            }
            board.makeMove(legal[std::uniform_int_distribution<int>(0, legal.size() - 1)(rng)]);
        }
    }
}
