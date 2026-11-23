#include <atomic>
#include <chrono>
#include <cstdlib>
#include <random>
#include <set>
#include <string>

#include "bot.h"
#include "eval.h"
#include "search.h"
#include "test_framework.h"

namespace {
Board fen(const char* text) { return Board::fromFen(text); }

SearchResult searchDepth(const char* position, int depth, const SearchSettings& settings = {}) {
    Searcher searcher(4);
    SearchLimits limits;
    limits.maxDepth = depth;
    return searcher.search(fen(position), {}, limits, settings);
}

// Random positions from random legal games, for property tests.
std::vector<Board> randomPositions(int count, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::vector<Board> out;
    while (static_cast<int>(out.size()) < count) {
        Board board;
        const int plies = std::uniform_int_distribution<int>(0, 80)(rng);
        for (int i = 0; i < plies; ++i) {
            MoveList moves;
            generateLegal(board, moves);
            if (moves.empty()) break;
            board.makeMove(moves[std::uniform_int_distribution<int>(0, moves.size() - 1)(rng)]);
        }
        out.push_back(board);
    }
    return out;
}

// A copy of a built-in bot with any time limit shortened, so tests run quickly. Levels 1-3 have no time limit
// (only depth and nodes), so they behave exactly as in real play.
BotSettings quick(int level) {
    BotSettings s = *findBot(std::to_string(level));
    if (s.moveTimeMs > 0) {
        s.moveTimeMs = 0;
        s.maxDepth = 4;
        s.candidateDepth = std::min(s.candidateDepth, 3);
        s.maxNodes = 20'000;
    }
    return s;
}
} // namespace

// ---------- Evaluation ----------

// The key evaluation test: swapping colors and flipping the board must give exactly the same score.
TEST(eval_is_color_symmetric) {
    EvalWeights unusual;
    unusual.kingSafety = 170;
    unusual.mobility = 40;
    unusual.passedPawns = 135;
    for (const Board& board : randomPositions(300, 99)) {
        const Board flip = mirrored(board);
        CHECK(flip.isConsistent());
        CHECK_EQ(evaluate(board), evaluate(flip));
        CHECK_EQ(evaluate(board, unusual), evaluate(flip, unusual));
        CHECK(mirrored(flip) == board);
    }
}

TEST(eval_is_from_side_to_move_perspective) {
    // Same position, other side to move: the scores mirror each other, apart from the constant tempo bonus.
    for (const Board& board : randomPositions(50, 5)) {
        if (inCheck(board)) continue;
        Board other = board;
        other.setSideToMove(opposite(board.sideToMove()));
        other.setEnPassantSquare(NO_SQUARE);
        if (moverLeftInCheck(other)) continue;
        // evaluate(white to move) + evaluate(black to move) is twice the tempo bonus for every position.
        Board start;
        Board startBlack = start;
        startBlack.setSideToMove(Color::Black);
        CHECK_EQ(evaluate(board) + evaluate(other), evaluate(start) + evaluate(startBlack));
    }
}

TEST(eval_basic_judgments) {
    CHECK(std::abs(evaluate(Board())) < 40); // the start position is roughly equal
    // An extra queen is a big advantage for whoever has it.
    CHECK(evaluate(fen("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1")) > 700);
    CHECK(evaluate(fen("rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR b KQkq - 0 1")) < -700);
    // Dead draws score near zero even with a piece up.
    CHECK(std::abs(evaluate(fen("4k3/8/8/8/8/8/8/3NKN2 w - - 0 1"))) < 60);
    CHECK(std::abs(evaluate(fen("4k3/8/8/8/8/8/8/4KB2 w - - 0 1"))) < 120);
    // A won endgame scores clearly, and more so with the defending king on the edge.
    const int center = evaluate(fen("8/8/8/4k3/8/8/8/3QK3 w - - 0 1"));
    const int edge = evaluate(fen("4k3/8/8/8/8/8/8/3QK3 w - - 0 1"));
    CHECK(center > 800);
    CHECK(edge > center);
    // A far-advanced passed pawn is worth more than one at home.
    CHECK(evaluate(fen("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1")) > evaluate(fen("4k3/8/8/8/8/8/1P6/4K3 w - - 0 1")));
}

TEST(eval_phase) {
    CHECK_EQ(gamePhase(Board()), eval::MAX_PHASE);
    CHECK_EQ(gamePhase(fen("4k3/pppppppp/8/8/8/8/PPPPPPPP/4K3 w - - 0 1")), 0);
    CHECK_EQ(gamePhase(fen("3qk3/8/8/8/8/8/8/3QK3 w - - 0 1")), 8);
}

// ---------- Static exchange evaluation ----------

TEST(search_static_exchange) {
    const auto see = [](const char* position, const char* uci) {
        const Board board = fen(position);
        return search::staticExchange(board, *moveFromUci(board, uci));
    };
    CHECK_EQ(see("4k3/8/8/3r4/8/8/8/3RK3 w - - 0 1", "d1d5"), 500);       // free rook
    CHECK_EQ(see("4k3/8/4p3/3n4/4P3/8/8/4K3 w - - 0 1", "e4d5"), 220);    // pawn takes a knight defended by a pawn
    CHECK_EQ(see("4k3/8/4p3/3p4/8/8/8/3QK3 w - - 0 1", "d1d5"), -850);    // queen takes a defended pawn
    CHECK_EQ(see("4k3/3r4/8/3p4/8/8/3R4/3RK3 w - - 0 1", "d2d5"), 100);   // doubled rooks win the pawn
    CHECK_EQ(see("3rk3/3r4/8/3p4/8/8/3R4/3RK3 w - - 0 1", "d2d5"), -400); // three defenders: it loses the rook
    CHECK_EQ(see("4k3/8/8/3p4/4K3/8/8/8 w - - 0 1", "e4d5"), 100);        // the king may take an undefended pawn
    CHECK(see("4k3/8/2p5/3p4/4K3/8/8/8 w - - 0 1", "e4d5") < -1000);  // into a defended square: illegal, and
                                                                       // scored as losing the king
    CHECK_EQ(see("4k3/8/8/8/8/8/3n4/3RK3 w - - 0 1", "d1d2"), 320);
}

// ---------- Search ----------

TEST(search_finds_mate_in_one) {
    const SearchResult r = searchDepth("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1", 4);
    CHECK_EQ(toUci(r.bestMove), std::string("a1a8"));
    CHECK(search::isMateScore(r.score));
    CHECK_EQ(search::mateInMoves(r.score), 1);
}

TEST(search_finds_mate_in_two) {
    // Rook ladder: Ra7 cuts the king off on the back rank, then Rb8 mates.
    const SearchResult r = searchDepth("6k1/8/8/8/8/8/R7/1R4K1 w - - 0 1", 6);
    CHECK(search::isMateScore(r.score));
    CHECK_EQ(search::mateInMoves(r.score), 2);
}

TEST(search_knows_when_it_is_mated) {
    // Black to move can only step along the back rank, and Rb8 mates next.
    const SearchResult r = searchDepth("6k1/R7/8/8/8/8/8/1R4K1 b - - 0 1", 4);
    CHECK(!r.bestMove.isNull());
    CHECK(search::isMateScore(r.score));
    CHECK_EQ(search::mateInMoves(r.score), -1);
}

TEST(search_handles_no_legal_moves) {
    const SearchResult mated = searchDepth("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3", 5);
    CHECK(mated.bestMove.isNull());
    CHECK(mated.score < 0);
    const SearchResult stalemate = searchDepth("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 5);
    CHECK(stalemate.bestMove.isNull());
    CHECK_EQ(stalemate.score, 0);
}

TEST(search_wins_material) {
    // The queen on d5 is attacked by the queen on d1 and defended by nothing.
    const SearchResult r = searchDepth("r3k2r/ppp2ppp/2n5/3q4/8/2N5/PPP2PPP/R2QK2R w KQkq - 0 1", 5);
    CHECK(r.bestMove.to() == D5);
    CHECK(r.score > 700);
    // A knight fork that wins the queen: Nf7+ is a quiet move, so a capture-only "greedy" player would miss it.
    const SearchResult fork = searchDepth("3q3k/pp4pp/8/6N1/8/8/PP4PP/6K1 w - - 0 1", 5);
    CHECK_EQ(toUci(fork.bestMove), std::string("g5f7"));
    CHECK(fork.score > 200); // from -645 before the fork: winning the queen leaves white a knight up
}

TEST(search_does_not_take_poisoned_material) {
    // Qxd5 looks like it wins a pawn but the pawn is defended by e6; the queen would be lost.
    const SearchResult r = searchDepth("4k3/8/4p3/3p4/8/8/8/3QK3 w - - 0 1", 4);
    CHECK(!(r.bestMove == *moveFromUci(fen("4k3/8/4p3/3p4/8/8/8/3QK3 w - - 0 1"), "d1d5")));
}

TEST(search_avoids_stalemating_a_won_ending) {
    // Many queen moves here stalemate black (Qg6, Qf7...). The engine must keep winning.
    const Board board = fen("7k/8/6K1/8/8/8/8/5Q2 w - - 0 1");
    Searcher searcher(4);
    SearchLimits limits;
    limits.maxDepth = 6;
    const SearchResult r = searcher.search(board, {}, limits);
    Board after = board;
    after.makeMove(r.bestMove);
    MoveList replies;
    generateLegal(after, replies);
    CHECK(!replies.empty() || inCheck(after)); // not stalemate
    CHECK(r.score > 500);
}

TEST(search_finds_perpetual_check_when_losing) {
    // Black is about to promote three pawns. White can only save the game with endless checks (Qe8+ Kh7 Qh5+ ...),
    // so the engine has to recognize the repetition as a draw.
    const SearchResult r = searchDepth("6k1/5pp1/8/8/8/8/ppp5/4Q2K w - - 0 1", 10);
    CHECK_EQ(toUci(r.bestMove), std::string("e1e8"));
    CHECK(std::abs(r.score) < 50);
}

TEST(search_respects_repetitions_in_game_history) {
    // Shuffle knights: playing g8 now repeats the start position a third time, so its score is a draw.
    Game game;
    for (const char* m : {"g1f3", "g8f6", "f3g1", "f6g8", "g1f3", "g8f6", "f3g1"}) CHECK(game.playUci(m));
    Searcher searcher(4);
    SearchSettings hatesDraws;
    hatesDraws.contempt = 300; // a draw is worth -300 to black here, so black must avoid f6g8
    SearchLimits limits;
    limits.maxDepth = 4;
    const SearchResult r = searcher.search(game.board(), game.positionHistory(), limits, hatesDraws);
    CHECK(!(toUci(r.bestMove) == "f6g8"));
}

TEST(search_limits_are_respected) {
    Searcher searcher(4);
    SearchLimits nodes;
    nodes.maxNodes = 5'000;
    const SearchResult r = searcher.search(Board(), {}, nodes);
    CHECK(!r.bestMove.isNull());
    CHECK(r.nodes < 5'000 + 2'000); // a little overshoot to finish the node in progress is fine
    SearchLimits depth;
    depth.maxDepth = 3;
    CHECK_EQ(searcher.search(Board(), {}, depth).depth, 3);
    // A stop flag that's already set still returns a legal move from the first iteration.
    std::atomic<bool> stop{true};
    SearchLimits stopped;
    stopped.stop = &stop;
    const SearchResult s = searcher.search(Board(), {}, stopped);
    CHECK(!s.bestMove.isNull());
    CHECK(s.depth >= 1);
}

TEST(search_pv_is_a_legal_line) {
    for (const Board& start : randomPositions(15, 31)) {
        Searcher searcher(4);
        SearchLimits limits;
        limits.maxDepth = 5;
        const SearchResult r = searcher.search(start, {}, limits);
        if (r.bestMove.isNull()) continue;
        CHECK(r.pv.empty() || r.pv[0] == r.bestMove);
        Board b = start;
        for (const Move m : r.pv) {
            MoveList legal;
            generateLegal(b, legal);
            CHECK(legal.contains(m));
            b.makeMove(m);
        }
    }
}

// Each pruning feature is an optimization: with it off the search reaches the same depth with more nodes.
TEST(search_pruning_reduces_work) {
    SearchSettings plain;
    plain.nullMovePruning = false;
    plain.lateMoveReductions = false;
    plain.futilityPruning = false;
    const char* position = "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4";
    const SearchResult pruned = searchDepth(position, 6);
    const SearchResult full = searchDepth(position, 6, plain);
    CHECK(pruned.nodes < full.nodes);
}

TEST(search_candidate_moves) {
    Searcher searcher(4);
    SearchLimits limits;
    // With a free queen available, nothing else is within 100 centipawns.
    const Board board = fen("r3k2r/ppp2ppp/2n5/3q4/8/2N5/PPP2PPP/R2QK2R w KQkq - 0 1");
    const auto onlyGood = searcher.candidateMoves(board, {}, 4, 100, limits);
    CHECK(!onlyGood.empty());
    for (const ScoredMove& c : onlyGood) CHECK(c.move.to() == D5);
    // In the quiet start position many moves are close.
    const auto many = searcher.candidateMoves(Board(), {}, 3, 60, limits);
    CHECK(many.size() >= 4);
    for (std::size_t i = 1; i < many.size(); ++i) CHECK(many[i].score <= many[i - 1].score);
    CHECK(many.front().score - many.back().score <= 60);
}

// ---------- Bots ----------

TEST(bot_roster) {
    const auto& bots = builtInBots();
    CHECK_EQ(static_cast<int>(bots.size()), 6);
    std::set<std::string> names;
    for (std::size_t i = 0; i < bots.size(); ++i) {
        CHECK_EQ(bots[i].level, static_cast<int>(i) + 1);
        CHECK(!bots[i].description.empty());
        names.insert(bots[i].name);
        if (i > 0) CHECK(bots[i].approxRating > bots[i - 1].approxRating);
        CHECK(bots[i].validated().level == bots[i].level);
    }
    CHECK_EQ(static_cast<int>(names.size()), 6);
    CHECK(findBot("pebble").has_value());
    CHECK(findBot("ZENITH").has_value());
    CHECK_EQ(findBot("3")->name, bots[2].name);
    CHECK(!findBot("nobody").has_value());
}

TEST(bot_settings_are_validated) {
    BotSettings wild;
    wild.maxDepth = -4;
    wild.temperature = 0;
    wild.mistakeChance = 3.0;
    wild.randomMargin = -10;
    wild.hashMegabytes = 0;
    wild.search.weights.mobility = 9999;
    const BotSettings v = wild.validated();
    CHECK_EQ(v.maxDepth, 1);
    CHECK_EQ(v.temperature, 1);
    CHECK(v.mistakeChance == 1.0);
    CHECK_EQ(v.randomMargin, 0);
    CHECK_EQ(v.hashMegabytes, 1);
    CHECK_EQ(v.search.weights.mobility, 300);
    BotSettings forever; // no limits at all would think forever
    CHECK(forever.validated().moveTimeMs > 0);
}

TEST(bot_time_allocation) {
    CHECK_EQ(timeForMove(60'000, 0), 2'000);
    CHECK_EQ(timeForMove(60'000, 1'000), 2'750);
    CHECK_EQ(timeForMove(1'000, 10'000), 250); // never more than a quarter of the clock
    CHECK_EQ(timeForMove(0, 0), 1);
    CHECK_EQ(timeForMove(10'000, 0, 4), 2'000);
}

TEST(bot_every_level_plays_legal_moves) {
    for (int level = 1; level <= 6; ++level) {
        Bot bot(quick(level), 7);
        Game game;
        for (int ply = 0; ply < 8 && !game.status().isOver(); ++ply) {
            const Move m = bot.chooseMove(game);
            CHECK(game.legalMoves().contains(m));
            CHECK(game.play(m));
        }
    }
}

TEST(bot_returns_null_move_when_game_over) {
    Game game;
    for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) CHECK(game.playUci(m));
    Bot bot(quick(1), 1);
    CHECK(bot.chooseMove(game).isNull());
}

TEST(bot_is_reproducible_with_a_seed) {
    for (int level : {1, 2}) {
        Game a;
        Game b;
        Bot botA(quick(level), 42);
        Bot botB(quick(level), 42);
        for (int ply = 0; ply < 16; ++ply) {
            const Move ma = botA.chooseMove(a);
            const Move mb = botB.chooseMove(b);
            CHECK(ma == mb);
            a.play(ma);
            b.play(mb);
        }
    }
}

TEST(bot_weak_levels_vary_their_play) {
    // Different seeds should not all play the same game: that's what keeps a beatable bot interesting.
    std::set<std::string> openings;
    for (std::uint64_t seed = 0; seed < 8; ++seed) {
        Bot bot(quick(1), seed);
        Game game;
        std::string line;
        for (int ply = 0; ply < 6; ++ply) {
            const Move m = bot.chooseMove(game);
            line += toUci(m) + " ";
            game.play(m);
        }
        openings.insert(line);
    }
    CHECK(openings.size() >= 3);
}

// The weakest bot still has to be a real opponent: it isn't a greedy capture-grabber.
TEST(bot_weakest_level_is_still_a_challenge) {
    const BotSettings pebble = quick(1);
    for (std::uint64_t seed = 0; seed < 10; ++seed) {
        Bot bot(pebble, seed);
        // Always takes a free queen.
        Game freeQueen(fen("r3k2r/ppp2ppp/2n5/3q4/8/2N5/PPP2PPP/R2QK2R w KQkq - 0 1"));
        CHECK(bot.chooseMove(freeQueen).to() == D5);
        // Always plays mate in one.
        Game mateInOne(fen("6k1/5ppp/8/8/8/8/5PPP/R5K1 w - - 0 1"));
        CHECK_EQ(toUci(bot.chooseMove(mateInOne)), std::string("a1a8"));
        // Never grabs a pawn that costs the queen.
        Game poisoned(fen("4k3/8/4p3/3p4/8/8/8/3QK3 w - - 0 1"));
        CHECK(!(toUci(bot.chooseMove(poisoned)) == "d1d5"));
        // Finds a quiet knight fork that wins the queen.
        Game fork(fen("3q3k/pp4pp/8/6N1/8/8/PP4PP/6K1 w - - 0 1"));
        CHECK_EQ(toUci(bot.chooseMove(fork)), std::string("g5f7"));
        // Saves its own attacked queen instead of leaving it.
        Game attacked(fen("4k3/8/8/3q4/8/2N5/8/4K3 b - - 0 1"));
        const Move save = bot.chooseMove(attacked);
        Board after = attacked.board();
        after.makeMove(save);
        CHECK(after.pieces(Color::Black, PieceType::Queen) != 0);
        CHECK(search::staticExchange(attacked.board(), save) >= 0);
    }
}

TEST(search_candidate_moves_share_one_time_budget) {
    Searcher searcher(4);
    SearchLimits limits;
    limits.moveTimeMs = 150;
    const auto start = std::chrono::steady_clock::now();
    const auto moves = searcher.candidateMoves(Board(), {}, 30, 50, limits);
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK(!moves.empty());
    CHECK(ms < 150 + 75); // both phases share the 150 ms; restarting the clock for phase two allowed ~300
}

TEST(bot_stop_flag_still_returns_a_move) {
    std::atomic<bool> stop{true};
    Bot bot(*findBot("Zenith"), 3);
    bot.setStopFlag(&stop);
    Game game;
    const Move m = bot.chooseMove(game);
    CHECK(game.legalMoves().contains(m));
}

TEST(bot_clock_limits_thinking_time) {
    Bot bot(*findBot("Zenith"), 3); // normally 3 seconds a move
    Game game;
    const auto start = std::chrono::steady_clock::now();
    const Move m = bot.chooseMove(game, 2'000); // 2 seconds left on the clock: about 66 ms for this move
    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK(game.legalMoves().contains(m));
    CHECK(ms < 1'500);
}
