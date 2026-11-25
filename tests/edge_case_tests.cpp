// Extreme and unusual situations that cut across modules.
#include <climits>
#include <string>

#include "bot.h"
#include "san.h"
#include "test_framework.h"

namespace {
Board fen(const char* text) { return Board::fromFen(text); }
} // namespace

// The position with the most legal moves known (218). MoveList must hold them all, and every one must be
// writable as SAN and readable back.
TEST(edge_maximum_number_of_moves) {
    const Board board = fen("R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w - - 0 1");
    MoveList legal;
    generateLegal(board, legal);
    CHECK_EQ(legal.size(), 218);
    MoveList pseudo;
    generatePseudoLegal(board, pseudo);
    CHECK(pseudo.size() <= MoveList::CAPACITY);
    for (const Move m : legal) CHECK(moveFromSan(board, toSan(board, m)) == m);
    Searcher searcher(4);
    SearchLimits limits;
    limits.maxDepth = 3;
    const SearchResult r = searcher.search(board, {}, limits);
    CHECK(legal.contains(r.bestMove));
    CHECK(search::isMateScore(r.score)); // with eight queens there's a quick mate
}

TEST(edge_clocks_from_fen_are_respected) {
    // Already past 100 half-moves: the game is a draw on load and nobody may move.
    Game game(fen("4k3/8/8/8/8/8/8/R3K3 w - - 150 200"));
    CHECK(game.status() == (GameStatus{GameResult::Draw, GameEndReason::FiftyMoveRule}));
    CHECK(!game.playUci("a1a2"));
    Bot bot(*findBot("Pebble"), 1);
    CHECK(bot.chooseMove(game).isNull());
    // A huge fullmove number round-trips.
    CHECK_EQ(fen("4k3/8/8/8/8/8/8/R3K3 w - - 0 2147483647").toFen(),
             std::string("4k3/8/8/8/8/8/8/R3K3 w - - 0 2147483647"));
}

TEST(edge_single_legal_move) {
    // Black is in check from the rook on a8 and Kxa8 is the only way out (a7, b7, c7 are covered by the king).
    Game game(fen("Rk6/8/1K6/8/8/8/8/8 b - - 0 1"));
    CHECK_EQ(game.legalMoves().size(), 1);
    for (int level = 1; level <= 6; ++level) {
        BotSettings s = *findBot(std::to_string(level));
        s.moveTimeMs = std::min(s.moveTimeMs, 50);
        Bot bot(s, 9);
        CHECK(bot.chooseMove(game) == game.legalMoves()[0]);
    }
}

TEST(edge_bare_kings_and_dead_positions) {
    Searcher searcher(4);
    SearchLimits limits;
    limits.maxDepth = 8;
    const SearchResult kk = searcher.search(fen("8/8/3k4/8/8/4K3/8/8 w - - 0 1"), {}, limits);
    CHECK(!kk.bestMove.isNull());
    CHECK(std::abs(kk.score) < 50);
    Game dead(fen("8/8/3k4/8/8/4K3/8/8 w - - 0 1"));
    CHECK(dead.status().reason == GameEndReason::InsufficientMaterial);
}

TEST(edge_lots_of_queens) {
    // Promotions can put up to nine queens on one side; nothing overflows or misbehaves. The black king is
    // walled in by its own bishop and pawns so the position is legal.
    const Board board = fen("6bk/6pp/8/QQQ5/QQQ5/QQQ5/8/K7 w - - 0 1");
    CHECK(board.isConsistent());
    const int score = evaluate(board);
    CHECK(score > 5000);
    CHECK(score < search::MATE_BOUND); // a static score must never look like a mate score
    CHECK_EQ(evaluate(board), evaluate(mirrored(board)));
    Searcher searcher(4);
    SearchLimits limits;
    limits.maxDepth = 4;
    const SearchResult r = searcher.search(board, {}, limits);
    CHECK(r.score > 3000);
}

TEST(edge_promotions_choose_the_right_piece) {
    Searcher searcher(4);
    SearchLimits limits;
    limits.maxDepth = 6;
    // Capturing the queen while promoting is simply best.
    const SearchResult take = searcher.search(fen("3q4/4P1k1/8/8/8/8/8/K7 w - - 0 1"), {}, limits);
    CHECK_EQ(toUci(take.bestMove), std::string("e7d8q"));
    // e8=N+ forks the king on g7 and the queen on d6; promoting to a queen would let black's queen take e8 or
    // trade. The extra h-pawn keeps the resulting knight ending winning.
    const SearchResult fork = searcher.search(fen("8/4P1k1/3q4/8/8/8/7P/K7 w - - 0 1"), {}, limits);
    CHECK_EQ(toUci(fork.bestMove), std::string("e7e8n"));
}

// A FEN may carry move counters up to INT_MAX. Playing on must not overflow them (undefined behavior, caught by
// the sanitizer build), and taking the move back restores them exactly.
TEST(edge_huge_move_counters_do_not_overflow) {
    Board board = Board::fromFen("4k3/8/8/8/8/8/8/R3K3 b - - 2147483647 2147483647");
    const Board before = board;
    const Move move = *moveFromUci(board, "e8d8");
    const UndoInfo undo = board.makeMove(move);
    CHECK_EQ(board.fullmoveNumber(), INT_MAX);
    CHECK_EQ(board.halfmoveClock(), INT_MAX);
    board.unmakeMove(move, undo);
    CHECK(board == before);
    const UndoInfo nullUndo = board.makeNullMove();
    board.unmakeNullMove(nullUndo);
    CHECK(board == before);
}
