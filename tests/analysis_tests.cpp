#include <cmath>
#include <string>
#include <thread>

#include "analysis.h"
#include "bot.h"
#include "test_framework.h"

namespace {
Game playUci(std::initializer_list<const char*> moves, const char* start = nullptr) {
    Game game = start ? Game(Board::fromFen(start)) : Game();
    for (const char* m : moves) {
        if (!game.playUci(m)) throw std::runtime_error(std::string("bad test move ") + m);
    }
    return game;
}

AnalysisSettings shallow(int depth = 6) {
    AnalysisSettings s;
    s.depth = depth;
    s.hashMegabytes = 8;
    return s;
}

// 1. e4 e5 2. Qh5 Nc6 3. Bc4 Nf6?? 4. Qxf7#
Game scholarsMate() { return playUci({"e2e4", "e7e5", "d1h5", "b8c6", "f1c4", "g8f6", "h5f7"}); }
} // namespace

// ---------- Formulas ----------

TEST(analysis_win_chance_curve) {
    CHECK(std::abs(analysis::winChance(0) - 50.0) < 1e-9);
    for (int cp = -1200; cp <= 1200; cp += 50) {
        CHECK(std::abs(analysis::winChance(cp) + analysis::winChance(-cp) - 100.0) < 1e-9); // symmetric
        CHECK(analysis::winChance(cp + 50) >= analysis::winChance(cp));                      // monotonic
    }
    CHECK(analysis::winChance(300) > 70);
    CHECK(analysis::winChance(100000) < 100.0);
    CHECK(analysis::winChance(100000) == analysis::winChance(1000)); // clamped, so a mate isn't "infinitely" won
}

TEST(analysis_move_accuracy_curve) {
    CHECK(analysis::moveAccuracy(60, 60) == 100.0);
    CHECK(analysis::moveAccuracy(60, 70) == 100.0); // gaining never costs accuracy
    double previous = 100.0;
    for (int lost = 1; lost <= 100; ++lost) {
        const double a = analysis::moveAccuracy(100, 100 - lost);
        CHECK(a <= previous);
        CHECK(a >= 0.0);
        previous = a;
    }
    CHECK(analysis::moveAccuracy(100, 0) == 0.0);
}

TEST(analysis_mate_scores_rank_above_material) {
    const int mateIn1 = analysis::mateToCentipawns(search::MATE - 1);
    const int mateIn3 = analysis::mateToCentipawns(search::MATE - 5);
    CHECK(mateIn1 > mateIn3);
    CHECK(mateIn3 > 5000);
    CHECK_EQ(analysis::mateToCentipawns(-(search::MATE - 2)), -(10000 - 2));
    CHECK_EQ(analysis::mateToCentipawns(250), 250);
}

TEST(analysis_classification_thresholds) {
    CHECK(analysis::classify(40, true) == MoveQuality::Best); // the engine's own move is never punished
    CHECK(analysis::classify(1, false) == MoveQuality::Excellent);
    CHECK(analysis::classify(3, false) == MoveQuality::Good);
    CHECK(analysis::classify(7, false) == MoveQuality::Inaccuracy);
    CHECK(analysis::classify(12, false) == MoveQuality::Mistake);
    CHECK(analysis::classify(30, false) == MoveQuality::Blunder);
    CHECK_EQ(std::string(toString(MoveQuality::Blunder)), std::string("blunder"));
    CHECK_EQ(std::string(toString(MoveQuality::Forced)), std::string("forced"));
}

// ---------- Reviewing games ----------

TEST(analysis_finds_the_blunder) {
    const Game game = scholarsMate();
    const GameAnalysis review = analyzeGame(game, shallow());
    CHECK_EQ(static_cast<int>(review.moves.size()), 7);
    const MoveAnalysis& nf6 = review.moves[5];
    CHECK(nf6.quality == MoveQuality::Blunder);
    CHECK(nf6.evalAfter < -5000); // walks into mate
    CHECK(!(nf6.best == nf6.played));
    const MoveAnalysis& mate = review.moves[6];
    CHECK(mate.quality == MoveQuality::Best);
    CHECK_EQ(review.black.blunders, 1);
    CHECK_EQ(review.white.blunders, 0);
    CHECK(review.white.accuracy > review.black.accuracy);
    CHECK(review.black.accuracy < 70);
    CHECK(review.black.averageCentipawnLoss > review.white.averageCentipawnLoss);
    for (const MoveAnalysis& m : review.moves) {
        CHECK(m.centipawnLoss >= 0);
        CHECK(m.accuracy >= 0 && m.accuracy <= 100);
        CHECK(m.bestLine.empty() || m.bestLine[0] == m.best);
    }
}

TEST(analysis_practice_positions) {
    const Game game = scholarsMate();
    const GameAnalysis review = analyzeGame(game, shallow());
    const auto black = mistakesToPractice(game, review, Color::Black);
    CHECK_EQ(static_cast<int>(black.size()), 1);
    CHECK_EQ(black[0].fen, std::string("r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 3 3"));
    CHECK_EQ(toUci(black[0].played), std::string("g8f6"));
    CHECK_EQ(black[0].moveIndex, 5);
    // The better move it suggests actually stops the mate.
    Game retry(Board::fromFen(black[0].fen));
    CHECK(retry.play(black[0].best));
    MoveList replies;
    generateLegal(retry.board(), replies);
    for (const Move m : replies) {
        Board b = retry.board();
        b.makeMove(m);
        MoveList answers;
        generateLegal(b, answers);
        CHECK(!(answers.empty() && inCheck(b)));
    }
    CHECK(mistakesToPractice(game, review, Color::White).empty());
}

TEST(analysis_forced_moves_are_not_judged) {
    // Ra8+ leaves black exactly one legal move, Kxa8 (a7, b7 and c7 are covered by the king on b6).
    const Game game = playUci({"a1a8", "b8a8"}, "1k6/8/1K6/8/8/8/8/R7 w - - 0 1");
    const GameAnalysis review = analyzeGame(game, shallow(4));
    CHECK(review.moves[0].quality != MoveQuality::Forced);
    CHECK(review.moves[1].quality == MoveQuality::Forced);
    // The position after Ra8+ has only that one reply, and it leaves a dead draw (K v K). The review has to see
    // that through the forced move instead of taking the static evaluation (a rook up) at face value.
    CHECK_EQ(review.moves[0].evalAfter, 0);
    CHECK(review.moves[0].quality == MoveQuality::Blunder);
    CHECK_EQ(review.black.movesAnalyzed, 0);
    CHECK(review.black.accuracy == 100.0);
}

TEST(analysis_handles_short_and_unusual_games) {
    CHECK(analyzeGame(Game(), shallow()).moves.empty());
    // A game from a custom position where black moves first.
    const Game blackFirst = playUci({"e7e5", "g1f3"}, "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1");
    const GameAnalysis r = analyzeGame(blackFirst, shallow(4));
    CHECK_EQ(static_cast<int>(r.moves.size()), 2);
    CHECK_EQ(r.black.movesAnalyzed, 1);
    CHECK_EQ(r.white.movesAnalyzed, 1);
    // A game that ended by resignation is still reviewed move by move.
    Game resigned = playUci({"e2e4", "e7e5"});
    resigned.resign(Color::Black);
    CHECK_EQ(static_cast<int>(analyzeGame(resigned, shallow(4)).moves.size()), 2);
    // A game that ended in stalemate scores the final position as a draw.
    const Game stalemate = playUci({"g6f7"}, "7k/8/6Q1/8/8/8/8/6K1 w - - 0 1");
    const GameAnalysis s = analyzeGame(stalemate, shallow(4));
    CHECK_EQ(s.moves[0].evalAfter, 0);
    CHECK(s.moves[0].quality == MoveQuality::Blunder); // throwing away a won position
}

TEST(analysis_keeps_think_times_and_can_be_cancelled) {
    Game game;
    for (const auto& [m, ms] : {std::pair{"e2e4", 1200}, {"e7e5", 800}, {"g1f3", 300}, {"b8c6", 4000}}) {
        CHECK(game.playUci(m, ms));
    }
    const GameAnalysis full = analyzeGame(game, shallow(4));
    CHECK(full.moves[3].thinkTimeMs == std::optional<int>(4000));
    int calls = 0;
    AnalysisSettings cancelAfterTwo = shallow(4);
    cancelAfterTwo.progress = [&calls](int done, int total) {
        ++calls;
        CHECK_EQ(total, 4);
        return done < 2;
    };
    const GameAnalysis partial = analyzeGame(game, cancelAfterTwo);
    CHECK_EQ(static_cast<int>(partial.moves.size()), 2);
    CHECK_EQ(calls, 2);
}

// The numbers the future fair-play check will rely on: an engine playing its own first choice matches itself.
TEST(analysis_engine_match_rate) {
    Game game;
    Searcher searcher(8);
    SearchLimits limits;
    limits.maxDepth = 5;
    for (int ply = 0; ply < 12 && !game.status().isOver(); ++ply) {
        game.play(searcher.search(game.board(), game.positionHistory(), limits).bestMove);
    }
    const GameAnalysis review = analyzeGame(game, shallow(5));
    CHECK(review.white.engineMatchRate > 0.6);
    CHECK(review.black.engineMatchRate > 0.6);
    CHECK(review.white.accuracy > 85);
    CHECK(review.black.accuracy > 85);
}

// ---------- Human-like bots ----------

TEST(bot_naturalness_weights) {
    // Recapturing is favored.
    const Game exchange = playUci({"e2e4", "d7d5", "e4d5", "d8d5", "b1c3", "d5a5", "d2d4", "c8f5", "c1d2", "f5c2"});
    // Black just took on c2; Qxc2 (d1c2) recaptures.
    CHECK(moveNaturalness(exchange, *moveFromUci(exchange.board(), "d1c2")) > 1.5);
    // Developing and castling are favored; king walks and edge pawns aren't.
    const Game start;
    CHECK(moveNaturalness(start, *moveFromUci(start.board(), "g1f3")) > 1.0);
    CHECK(moveNaturalness(start, *moveFromUci(start.board(), "a2a4")) < 1.0);
    const Game open = playUci({"e2e4", "e7e5"});
    CHECK(moveNaturalness(open, *moveFromUci(open.board(), "e1e2")) < 0.5);
    CHECK(moveNaturalness(open, *moveFromUci(open.board(), "d1h5")) < 1.0);
    const Game castle = playUci({"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6"});
    CHECK(moveNaturalness(castle, *moveFromUci(castle.board(), "e1g1")) > 1.5);
}

TEST(bot_human_like_choice_reduces_odd_moves) {
    const auto oddMoves = [](bool humanLike) {
        int odd = 0;
        for (std::uint64_t seed = 0; seed < 12; ++seed) {
            BotSettings s = *findBot("Pebble");
            s.humanLikeChoice = humanLike;
            Bot white(s, seed);
            Bot black(s, seed + 500);
            Game game;
            for (int ply = 0; ply < 12 && !game.status().isOver(); ++ply) {
                const Move m = (game.board().sideToMove() == Color::White ? white : black).chooseMove(game);
                const Piece p = *game.board().pieceAt(m.from());
                const bool kingWalk = p.type == PieceType::King && !m.isCastle();
                const bool edgePawn = p.type == PieceType::Pawn && !m.isCapture() &&
                                      (fileOf(m.from()) == 0 || fileOf(m.from()) == 7);
                if (kingWalk || edgePawn) ++odd;
                game.play(m);
            }
        }
        return odd;
    };
    CHECK(oddMoves(true) < oddMoves(false));
    CHECK(findBot("Pebble")->humanLikeChoice);
    CHECK(!findBot("Zenith")->humanLikeChoice); // full strength always plays the best move it finds
}

TEST(analysis_on_several_threads) {
    const Game game = scholarsMate();
    AnalysisSettings settings = shallow();
    settings.threads = 3;
    const GameAnalysis a = analyzeGame(game, settings);
    const GameAnalysis b = analyzeGame(game, settings);
    CHECK_EQ(static_cast<int>(a.moves.size()), 7);
    // The same on every run with the same thread count.
    for (std::size_t i = 0; i < a.moves.size(); ++i) {
        CHECK(a.moves[i].best == b.moves[i].best);
        CHECK_EQ(a.moves[i].evalBefore, b.moves[i].evalBefore);
        CHECK_EQ(a.moves[i].evalAfter, b.moves[i].evalAfter);
    }
    // And it reaches the same verdicts as the one-thread review.
    CHECK(a.moves[5].quality == MoveQuality::Blunder);
    CHECK(a.moves[6].quality == MoveQuality::Best);
    CHECK_EQ(a.black.blunders, 1);
    CHECK_EQ(a.white.blunders, 0);
    // More threads than positions is fine.
    settings.threads = 64;
    CHECK_EQ(static_cast<int>(analyzeGame(game, settings).moves.size()), 7);

    // Progress is reported on the calling thread, and cancelling keeps only moves analyzed from the start on.
    settings.threads = 4;
    const std::thread::id caller = std::this_thread::get_id();
    bool sameThread = true;
    int calls = 0;
    settings.progress = [&](int done, int total) {
        sameThread &= std::this_thread::get_id() == caller;
        ++calls;
        CHECK(done >= 1 && done <= total);
        return false; // cancel at once
    };
    const GameAnalysis cancelled = analyzeGame(game, settings);
    CHECK(sameThread);
    CHECK(calls >= 1);
    CHECK(cancelled.moves.size() < 7);
    for (std::size_t i = 0; i < cancelled.moves.size(); ++i) CHECK(cancelled.moves[i].played == game.moves()[i]);
}
