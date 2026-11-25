// Property tests: instead of checking hand-picked cases, generate many positions (seeded random games, so every run
// sees the same ones) and check rules that must hold in all of them. They catch the bugs nobody thought to write a
// case for, and they're what guards the speed-ups in movegen and search: each fast path is compared against the
// simple, obviously correct way of doing the same thing.
//
// CHESS_PROPERTY_SCALE multiplies the amount of work. The unit suite uses 1; the chess_fuzz target (ctest label
// "slow") builds this same file with a larger scale.
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "analysis.h"
#include "eval.h"
#include "pgn.h"
#include "san.h"
#include "search.h"
#include "test_framework.h"

#ifndef CHESS_PROPERTY_SCALE
#define CHESS_PROPERTY_SCALE 1
#endif

namespace {
constexpr int SCALE = CHESS_PROPERTY_SCALE;
// The exchange values the search uses (search.cpp): pawn, knight, bishop, rook, queen.
constexpr int VALUE[5] = {100, 320, 330, 500, 950};

// A random but legal game. Captures are preferred some of the time so the walks also reach endgames, promotions
// and the drawing rules, not just crowded middlegames.
Game randomGame(std::mt19937_64& rng, int maxPlies) {
    Game game;
    for (int ply = 0; ply < maxPlies && !game.status().isOver(); ++ply) {
        const MoveList& legal = game.legalMoves();
        Move pick = legal[static_cast<int>(rng() % static_cast<std::uint64_t>(legal.size()))];
        if (rng() % 3 == 0) {
            for (const Move m : legal) {
                if (m.isCapture() || m.isPromotion()) {
                    pick = m;
                    break;
                }
            }
        }
        game.play(pick, static_cast<int>(rng() % 20'000));
    }
    return game;
}

// Calls `check` on every position of `games` random games.
template <typename Check>
void forEachPosition(std::uint64_t seed, int games, Check check) {
    std::mt19937_64 rng(seed);
    for (int g = 0; g < games; ++g) {
        const Game game = randomGame(rng, 30 + static_cast<int>(rng() % 170));
        Board board = game.startingBoard();
        check(board);
        for (const Move m : game.moves()) {
            board.makeMove(m);
            check(board);
        }
    }
}

// Legal moves the slow, obvious way: play every pseudo-legal move and see whether the king is left in check.
std::vector<Move> legalByBruteForce(const Board& board) {
    MoveList pseudo;
    generatePseudoLegal(board, pseudo);
    std::vector<Move> out;
    for (const Move m : pseudo) {
        Board copy = board;
        copy.makeMove(m);
        if (!moverLeftInCheck(copy)) out.push_back(m);
    }
    return out;
}

std::vector<std::uint16_t> sortedRaw(const MoveList& list) {
    std::vector<std::uint16_t> out;
    for (const Move m : list) out.push_back(m.raw());
    std::sort(out.begin(), out.end());
    return out;
}
} // namespace

TEST(property_legal_moves_match_brute_force) {
    forEachPosition(1, 12 * SCALE, [](const Board& board) {
        MoveList fast;
        generateLegal(board, fast);
        const std::vector<Move> slow = legalByBruteForce(board);
        CHECK_EQ(fast.size(), static_cast<int>(slow.size()));
        for (const Move m : slow) CHECK(fast.contains(m));
        // The legality shortcut may only ever say "legal" for a move that is.
        const LegalityShortcut shortcut(board, inCheck(board));
        MoveList pseudo;
        generatePseudoLegal(board, pseudo);
        for (const Move m : pseudo) {
            if (shortcut.certainlyLegal(m)) CHECK(std::find(slow.begin(), slow.end(), m) != slow.end());
        }
    });
}

TEST(property_noisy_and_quiet_moves_split_all_moves) {
    forEachPosition(2, 10 * SCALE, [](const Board& board) {
        MoveList all;
        MoveList noisy;
        MoveList quiet;
        generatePseudoLegal(board, all);
        generatePseudoLegal(board, noisy, GenType::Noisy);
        generatePseudoLegal(board, quiet, GenType::Quiet);
        for (const Move m : noisy) CHECK(m.isCapture() || m.isPromotion());
        for (const Move m : quiet) CHECK(!m.isCapture() && !m.isPromotion());
        MoveList both = noisy;
        for (const Move m : quiet) both.push(m);
        CHECK(sortedRaw(both) == sortedRaw(all));
    });
}

TEST(property_make_unmake_restores_everything) {
    forEachPosition(3, 10 * SCALE, [](const Board& board) {
        CHECK(board.isConsistent()); // includes the incremental hash against one computed from scratch
        MoveList pseudo;
        generatePseudoLegal(board, pseudo);
        Board scratch = board;
        for (const Move m : pseudo) {
            const UndoInfo undo = scratch.makeMove(m);
            CHECK(scratch.isConsistent());
            scratch.unmakeMove(m, undo);
            CHECK(scratch == board);
        }
        if (!inCheck(board)) {
            const UndoInfo undo = scratch.makeNullMove();
            CHECK(scratch.isConsistent());
            scratch.unmakeNullMove(undo);
            CHECK(scratch == board);
        }
    });
}

TEST(property_notation_round_trips) {
    forEachPosition(4, 8 * SCALE, [](const Board& board) {
        CHECK(Board::fromFen(board.toFen()) == board);
        MoveList legal;
        generateLegal(board, legal);
        for (const Move m : legal) {
            const std::optional<Move> fromSan = moveFromSan(board, toSan(board, m));
            CHECK(fromSan && *fromSan == m);
            const std::optional<Move> fromUci = moveFromUci(board, toUci(m));
            CHECK(fromUci && *fromUci == m);
        }
    });
}

TEST(property_evaluation_is_symmetric_and_bounded) {
    forEachPosition(5, 10 * SCALE, [](const Board& board) {
        const int score = evaluate(board);
        CHECK_EQ(evaluate(mirrored(board)), score);
        CHECK(!search::isMateScore(score)); // a static score must never look like a forced mate
        CHECK(gamePhase(board) >= 0 && gamePhase(board) <= eval::MAX_PHASE);
    });
}

TEST(property_attack_queries_agree) {
    forEachPosition(6, 6 * SCALE, [](const Board& board) {
        const Bitboard occupied = board.occupancy();
        for (Square sq = A1; sq <= H8; ++sq) {
            const Bitboard attackers = attackersTo(board, sq, occupied);
            for (const Color c : {Color::White, Color::Black}) {
                CHECK_EQ(isSquareAttacked(board, sq, c), (attackers & board.occupancy(c)) != 0);
            }
        }
    });
}

TEST(property_static_exchange_is_bounded) {
    forEachPosition(7, 8 * SCALE, [](const Board& board) {
        MoveList noisy;
        generatePseudoLegal(board, noisy, GenType::Noisy);
        for (const Move m : noisy) {
            // The opponent may recapture or not, so the exchange can never be worth more than what the first capture
            // takes, plus the promotion.
            const std::optional<Piece> victim = board.pieceAt(m.to());
            int firstGain = m.isEnPassant() ? VALUE[0] : victim ? VALUE[static_cast<int>(victim->type)] : 0;
            if (m.isPromotion()) firstGain += VALUE[static_cast<int>(m.promotionType())] - VALUE[0];
            CHECK(search::staticExchange(board, m) <= firstGain);
        }
    });
}

TEST(property_search_returns_legal_moves_and_lines) {
    std::mt19937_64 rng(8);
    Searcher searcher(4);
    for (int g = 0; g < 6 * SCALE; ++g) {
        const Game game = randomGame(rng, static_cast<int>(rng() % 120));
        if (game.status().isOver()) continue;
        SearchLimits limits;
        limits.maxDepth = 1 + static_cast<int>(rng() % 4);
        const SearchResult r = searcher.search(game.board(), game.positionHistory(), limits);
        CHECK(game.legalMoves().contains(r.bestMove));
        CHECK(!r.pv.empty() && r.pv[0] == r.bestMove);
        Game line = game;
        for (const Move m : r.pv) CHECK(line.play(m));
    }
}

TEST(property_games_survive_pgn) {
    std::mt19937_64 rng(9);
    for (int g = 0; g < 10 * SCALE; ++g) {
        Game game = randomGame(rng, 20 + static_cast<int>(rng() % 200));
        if (!game.status().isOver() && rng() % 3 == 0) game.resign(rng() % 2 ? Color::White : Color::Black);
        PgnWriteOptions options;
        if (rng() % 2) options.timeControl = TimeControl{180'000, 2'000};
        const std::vector<PgnGame> back = parsePgn(toPgn(game, options));
        CHECK_EQ(back.size(), std::size_t{1});
        CHECK(back[0].game.moves() == game.moves());
        CHECK(back[0].game.moveTimes() == game.moveTimes());
        CHECK(back[0].game.status() == game.status());
    }
}

TEST(property_damaged_pgn_is_rejected_cleanly) {
    std::mt19937_64 rng(10);
    const std::string symbols = "{}()[];$%\"\\.-01NBRQKxe8=+#\n ";
    for (int g = 0; g < 4 * SCALE; ++g) {
        const std::string pgn = toPgn(randomGame(rng, 60));
        for (int k = 0; k < 25; ++k) {
            std::string damaged = pgn;
            for (int e = 0; e < 1 + static_cast<int>(rng() % 4) && !damaged.empty(); ++e) {
                const std::size_t at = rng() % damaged.size();
                switch (rng() % 3) {
                    case 0: damaged[at] = symbols[rng() % symbols.size()]; break;
                    case 1: damaged.erase(at, 1 + rng() % 5); break;
                    default: damaged.insert(at, 1, static_cast<char>(rng() % 128)); break;
                }
            }
            // Either it reads, and every game in it is legal, or it throws PgnError. Nothing else.
            try {
                for (const PgnGame& game : parsePgn(damaged)) CHECK(game.game.board().isConsistent());
            } catch (const PgnError&) {
                // Rejected with a proper error: also fine.
            }
        }
    }
}

TEST(property_review_numbers_stay_in_range) {
    std::mt19937_64 rng(11);
    for (int g = 0; g < SCALE; ++g) {
        const Game game = randomGame(rng, 50);
        AnalysisSettings settings;
        settings.depth = 3;
        settings.hashMegabytes = 4;
        const GameAnalysis review = analyzeGame(game, settings);
        CHECK_EQ(review.moves.size(), game.moves().size());
        for (const MoveAnalysis& m : review.moves) {
            CHECK(m.centipawnLoss >= 0);
            CHECK(m.accuracy >= 0 && m.accuracy <= 100);
            CHECK(m.winChanceBefore >= 0 && m.winChanceBefore <= 100);
            CHECK(m.bestLine.empty() || m.bestLine[0] == m.best);
        }
        for (const PlayerSummary* p : {&review.white, &review.black}) {
            CHECK(p->accuracy >= 0 && p->accuracy <= 100);
            CHECK(p->engineMatchRate >= 0 && p->engineMatchRate <= 1);
        }
    }
}
