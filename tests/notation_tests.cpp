#include <algorithm>
#include <random>
#include <sstream>
#include <string>

#include "pgn.h"
#include "san.h"
#include "test_framework.h"

namespace {
Board fen(const char* text) { return Board::fromFen(text); }

std::string san(const char* position, const char* uci) {
    const Board board = fen(position);
    return toSan(board, *moveFromUci(board, uci));
}

std::string parsed(const char* position, const char* text) {
    const Board board = fen(position);
    const std::optional<Move> m = moveFromSan(board, text);
    return m ? toUci(*m) : "none";
}

// A random legal game, optionally with think times on every move.
Game randomGame(std::mt19937& rng, int maxPlies, bool times) {
    Game game;
    for (int i = 0; i < maxPlies && !game.status().isOver(); ++i) {
        const MoveList& legal = game.legalMoves();
        const Move m = legal[std::uniform_int_distribution<int>(0, legal.size() - 1)(rng)];
        std::optional<int> ms;
        if (times) ms = std::uniform_int_distribution<int>(0, 400'000)(rng);
        game.play(m, ms);
    }
    return game;
}
} // namespace

// ---------- Writing SAN ----------

TEST(san_writes_basic_moves) {
    const char* start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    CHECK_EQ(san(start, "e2e4"), std::string("e4"));
    CHECK_EQ(san(start, "g1f3"), std::string("Nf3"));
    CHECK_EQ(san(start, "b1a3"), std::string("Na3"));
    const char* open = "rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2";
    CHECK_EQ(san(open, "e4d5"), std::string("exd5"));
    CHECK_EQ(san(open, "f1b5"), std::string("Bb5+"));
}

TEST(san_disambiguates_only_as_needed) {
    // Knights on b1 and f3 can both reach d2: the file is enough.
    CHECK_EQ(san("4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1", "b1d2"), std::string("Nbd2"));
    // Rooks on a1 and a5 share a file, so the rank is used.
    CHECK_EQ(san("4k3/8/8/R7/8/8/8/R3K3 w - - 0 1", "a1a3"), std::string("R1a3"));
    // Queens on a1, c1 and a3 all reach b2: a1 shares a file with a3 and a rank with c1, so both are needed.
    CHECK_EQ(san("4k3/8/8/8/8/Q7/8/Q1Q1K3 w - - 0 1", "a1b2"), std::string("Qa1b2"));
    // A pinned knight doesn't count as a second candidate.
    CHECK_EQ(san("4k3/8/8/8/1b6/8/3N4/4K1N1 w - - 0 1", "g1f3"), std::string("Nf3"));
}

TEST(san_writes_special_moves) {
    CHECK_EQ(san("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1", "e1g1"), std::string("O-O"));
    CHECK_EQ(san("r3k2r/8/8/8/8/8/8/R3K2R b KQkq - 0 1", "e8c8"), std::string("O-O-O"));
    CHECK_EQ(san("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7b8q"), std::string("b8=Q+"));
    CHECK_EQ(san("2r1k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7c8n"), std::string("bxc8=N"));
    CHECK_EQ(san("4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1", "e5d6"), std::string("exd6"));
    CHECK_EQ(san("rnbqkbnr/pppp1ppp/8/4p3/6P1/5P2/PPPPP2P/RNBQKBNR b KQkq - 0 2", "d8h4"), std::string("Qh4#"));
}

// ---------- Reading SAN ----------

TEST(san_reads_what_it_writes) {
    std::mt19937 rng(2024);
    for (int g = 0; g < 40; ++g) {
        Board board;
        for (int ply = 0; ply < 120; ++ply) {
            MoveList legal;
            generateLegal(board, legal);
            if (legal.empty()) break;
            for (const Move m : legal) {
                const std::string text = toSan(board, m);
                const std::optional<Move> back = moveFromSan(board, text);
                CHECK(back.has_value() && *back == m);
            }
            board.makeMove(legal[std::uniform_int_distribution<int>(0, legal.size() - 1)(rng)]);
        }
    }
}

TEST(san_reading_is_lenient_like_real_files) {
    const char* start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    CHECK_EQ(parsed(start, "Nf3!?"), std::string("g1f3"));
    CHECK_EQ(parsed(start, "e4!!"), std::string("e2e4"));
    CHECK_EQ(parsed(start, "Ng1f3"), std::string("g1f3"));
    CHECK_EQ(parsed(start, "Ng1-f3"), std::string("g1f3"));
    const char* castle = "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1";
    CHECK_EQ(parsed(castle, "0-0"), std::string("e1g1"));
    CHECK_EQ(parsed(castle, "O-O-O+"), std::string("e1c1"));
    CHECK_EQ(parsed(castle, "Kg1"), std::string("e1g1"));
    const char* promo = "4k3/1P6/8/8/8/8/8/4K3 w - - 0 1";
    CHECK_EQ(parsed(promo, "b8Q"), std::string("b7b8q"));
    CHECK_EQ(parsed(promo, "b8=q"), std::string("b7b8q"));
    CHECK_EQ(parsed(promo, "b8=N+"), std::string("b7b8n"));
    const char* ep = "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1";
    CHECK_EQ(parsed(ep, "exd6 e.p."), std::string("e5d6"));
    CHECK_EQ(parsed(ep, "ed6"), std::string("e5d6"));
    CHECK_EQ(parsed(ep, "exd6"), std::string("e5d6"));
}

TEST(san_rejects_bad_or_ambiguous_moves) {
    const char* twoKnights = "4k3/8/8/8/8/5N2/8/1N2K3 w - - 0 1";
    CHECK_EQ(parsed(twoKnights, "Nd2"), std::string("none")); // ambiguous
    CHECK_EQ(parsed(twoKnights, "Nbd2"), std::string("b1d2"));
    const char* start = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    for (const char* bad : {"", "+", "Ke2", "Qh5", "e5", "nf3", "Nf6", "O-O", "e4=Q", "Zf3", "Nf9", "N", "xx"}) {
        CHECK_EQ(parsed(start, bad), std::string("none"));
    }
    CHECK_EQ(parsed("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b8=K"), std::string("none"));
    CHECK_EQ(parsed("4k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b8"), std::string("none")); // promotion piece missing
    // "b" is a file, "B" is a bishop.
    const char* bishopAndPawn = "4k3/8/8/8/8/1p6/P1B5/4K3 w - - 0 1";
    CHECK_EQ(parsed(bishopAndPawn, "axb3"), std::string("a2b3"));
    CHECK_EQ(parsed(bishopAndPawn, "Bxb3"), std::string("c2b3"));
}

// ---------- Writing PGN ----------

TEST(pgn_writes_a_game) {
    Game game;
    for (const char* m : {"e2e4", "e7e5", "g1f3", "b8c6", "f1b5"}) CHECK(game.playUci(m));
    const std::string pgn = toPgn(game, {{"White", "Pebble"}, {"Black", "Me \"the\" human"}, {"Annotator", "x"}});
    const std::string expected = "[Event \"?\"]\n"
                                 "[Site \"?\"]\n"
                                 "[Date \"????.??.??\"]\n"
                                 "[Round \"?\"]\n"
                                 "[White \"Pebble\"]\n"
                                 "[Black \"Me \\\"the\\\" human\"]\n"
                                 "[Result \"*\"]\n"
                                 "[Annotator \"x\"]\n"
                                 "\n"
                                 "1. e4 e5 2. Nf3 Nc6 3. Bb5 *\n";
    CHECK_EQ(pgn, expected);
}

TEST(pgn_writes_custom_start_and_result) {
    Game game(fen("k7/8/1K6/8/8/8/8/7R b - - 0 40"));
    CHECK(game.playUci("a8b8"));
    CHECK(game.playUci("h1h8"));
    const std::string pgn = toPgn(game);
    CHECK(pgn.find("[SetUp \"1\"]") != std::string::npos);
    CHECK(pgn.find("[FEN \"k7/8/1K6/8/8/8/8/7R b - - 0 40\"]") != std::string::npos);
    CHECK(pgn.find("40... Kb8 41. Rh8#") != std::string::npos);
    CHECK(pgn.find("[Result \"1-0\"]") != std::string::npos);
    CHECK(pgn.find("Rh8# 1-0") != std::string::npos);
}

TEST(pgn_lines_stay_within_80_columns) {
    std::mt19937 rng(3);
    const Game game = randomGame(rng, 200, true);
    std::istringstream lines(toPgn(game));
    std::string line;
    while (std::getline(lines, line)) CHECK(line.size() <= 80);
}

TEST(pgn_move_times) {
    Game game;
    CHECK(game.playUci("e2e4", 5'000));
    CHECK(game.playUci("e7e5", 61'250));
    CHECK(game.playUci("g1f3", 3'723'000));
    CHECK(game.playUci("b8c6")); // no time recorded
    std::string pgn = toPgn(game);
    std::replace(pgn.begin(), pgn.end(), '\n', ' '); // compare without line wrapping
    CHECK(pgn.find("1. e4 { [%emt 0:00:05] } e5 { [%emt 0:01:01.25] } 2. Nf3 { [%emt 1:02:03] } Nc6 *") !=
          std::string::npos);
    const PgnGame back = parsePgn(pgn).at(0);
    CHECK(back.game.moveTimes() == game.moveTimes());
}

TEST(game_move_times_follow_undo_and_redo) {
    Game game;
    CHECK(game.playUci("e2e4", 100));
    CHECK(game.playUci("e7e5", 200));
    CHECK(!game.playUci("e2e4", 5)); // illegal moves don't record anything
    CHECK_EQ(static_cast<int>(game.moveTimes().size()), 2);
    CHECK(game.undo());
    CHECK_EQ(static_cast<int>(game.moveTimes().size()), 1);
    CHECK(game.redo());
    CHECK(game.moveTimes().back() == std::optional<int>(200));
    game.setMoveTime(0, -5); // negative times aren't kept
    CHECK(!game.moveTimes()[0].has_value());
    game.setMoveTime(99, 1); // out of range: ignored
    CHECK(!game.playUci("g1f3", -1) || !game.moveTimes().back().has_value());
}

// ---------- Reading PGN ----------

TEST(pgn_round_trips_random_games) {
    std::mt19937 rng(11);
    for (int i = 0; i < 40; ++i) {
        const Game game = randomGame(rng, 250, i % 2 == 0);
        const std::vector<PgnGame> games = parsePgn(toPgn(game, {{"Event", "Test " + std::to_string(i)}}));
        CHECK_EQ(static_cast<int>(games.size()), 1);
        const PgnGame& back = games[0];
        CHECK(back.game.moves() == game.moves());
        CHECK(back.game.board() == game.board());
        CHECK(back.game.moveTimes() == game.moveTimes());
        CHECK(back.game.status().result == game.status().result);
        CHECK_EQ(back.tag("Event"), "Test " + std::to_string(i));
        CHECK_EQ(back.result, resultToken(game.status()));
    }
}

TEST(pgn_reads_real_world_syntax) {
    const std::string text = "% exported by some tool, ignore this line\n"
                             "[Event \"Club \\\"Open\\\"\"]\n"
                             "[White \"A\"] [Black \"B\"]\n"
                             "[Result \"1-0\"]\n"
                             "\n"
                             "1.e4 e5 {Open game} 2. Nf3 $1 Nc6 (2... d6 3. d4 (3. Bc4 {Philidor}) exd4) 3.Bb5 a6\n"
                             "; a comment to the end of the line\n"
                             "4. Ba4 4... Nf6 5. O-O!? Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O 1-0\n"
                             "\n"
                             "[Event \"Second\"]\n"
                             "[SetUp \"1\"]\n"
                             "[FEN \"4k3/8/8/8/8/8/8/R3K3 w Q - 0 1\"]\n"
                             "\n"
                             "1. Ra8+ Kd7 2. Ra7+ Kc6 *\n"
                             "[Event \"Third, no result token\"]\n"
                             "[Result \"1/2-1/2\"]\n"
                             "1. d4 d5\n";
    const std::vector<PgnGame> games = parsePgn(text);
    CHECK_EQ(static_cast<int>(games.size()), 3);

    CHECK_EQ(games[0].tag("Event"), std::string("Club \"Open\""));
    CHECK_EQ(games[0].tag("Black"), std::string("B"));
    CHECK_EQ(static_cast<int>(games[0].game.moves().size()), 16);
    CHECK_EQ(games[0].result, std::string("1-0"));
    CHECK(games[0].game.status() == (GameStatus{GameResult::WhiteWins, GameEndReason::Resignation}));

    CHECK_EQ(games[1].game.startingBoard().toFen(), std::string("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1"));
    CHECK_EQ(static_cast<int>(games[1].game.moves().size()), 4);
    CHECK_EQ(games[1].result, std::string("*"));
    CHECK(!games[1].game.status().isOver());

    CHECK_EQ(games[2].result, std::string("1/2-1/2"));
    CHECK(games[2].game.status().reason == GameEndReason::DrawAgreed);
    CHECK(parsePgn("").empty());
    CHECK(parsePgn("   \n\n").empty());
}

TEST(pgn_reports_errors_with_location) {
    const auto error = [](const std::string& text) -> std::string {
        try {
            parsePgn(text);
        } catch (const PgnError& e) {
            return e.what();
        }
        return "";
    };
    const std::string illegal = error("[Event \"x\"]\n\n1. e4 e5\n2. Ke3 *\n");
    CHECK(illegal.find("line 4") != std::string::npos);
    CHECK(illegal.find("Ke3") != std::string::npos);
    CHECK(error("1. e4 {never closed").find("never closed") != std::string::npos);
    CHECK(error("1. e4 (1. d4 d5 *").find("never closed") != std::string::npos);
    CHECK(error("1. e4 e5 ) *").find("')'") != std::string::npos);
    CHECK(error("[FEN \"not a fen\"]\n1. e4 *").find("bad FEN") != std::string::npos);
    CHECK(error("[Event \"open\n1. e4 *").find("never closed") != std::string::npos);
    CHECK(error("[ \"x\"]").find("without a name") != std::string::npos);
    CHECK(error("1. e4 } e5 *").find("'}'") != std::string::npos);
    CHECK(error("1. e4 ] e5 *").find("']'") != std::string::npos);
    CHECK(error("1. f3 e5 2. g4 Qh4# 3. a3 0-1").find("already over") != std::string::npos);
    CHECK(error("[Event \"a\"] 1. e4 *\n[Event \"b\"] 1. e5 *").find("game 2") != std::string::npos);
}

// Random damage to a valid PGN either parses or throws PgnError: never a crash or another exception.
TEST(pgn_damaged_input_never_crashes) {
    std::mt19937 rng(77);
    const Game game = randomGame(rng, 60, true);
    const std::string original = toPgn(game, {{"White", "W"}});
    const std::string alphabet = "[]{}()\";%$.!?-=+#x0123456789abcdefghNBRQKO \n";
    std::uniform_int_distribution<int> letter(0, static_cast<int>(alphabet.size()) - 1);
    int rejected = 0;
    for (int round = 0; round < 3000; ++round) {
        std::string text = original;
        const int edits = 1 + round % 5;
        for (int e = 0; e < edits; ++e) {
            std::uniform_int_distribution<int> at(0, static_cast<int>(text.size()) - 1);
            const auto p = static_cast<std::size_t>(at(rng));
            switch (round % 3) {
                case 0: text[p] = alphabet[letter(rng)]; break;
                case 1: text.erase(p, 1); break;
                default: text.insert(p, 1, alphabet[letter(rng)]); break;
            }
            if (text.empty()) text = "*";
        }
        try {
            for (const PgnGame& g : parsePgn(text)) CHECK(g.game.board().isConsistent());
        } catch (const PgnError&) {
            ++rejected;
        }
    }
    CHECK(rejected > 0); // the damage really does break some files
}
