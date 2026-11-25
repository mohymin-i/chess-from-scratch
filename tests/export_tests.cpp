#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>

#include "gameexport.h"
#include "san.h"
#include "test_framework.h"

namespace {
bool has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

// Joins wrapped lines so tests can look for text regardless of where the 80-column wrap falls.
std::string flat(std::string text) {
    std::replace(text.begin(), text.end(), '\n', ' ');
    return text;
}

Game timedGame(std::initializer_list<std::pair<const char*, int>> moves) {
    Game game;
    for (const auto& [uci, ms] : moves) {
        if (!game.playUci(uci, ms)) throw std::runtime_error(std::string("bad test move ") + uci);
    }
    return game;
}

std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return out;
}
} // namespace

// ---------- Time notation ----------

TEST(export_time_control_tag) {
    CHECK(TimeControl::fromPgnTag("300+3") == (TimeControl{300'000, 3'000}));
    CHECK(TimeControl::fromPgnTag("600") == (TimeControl{600'000, 0}));
    for (const char* bad : {"", "-", "?", "0", "40/7200", "300+", "+3", "abc", "300+x", "*60", "99999999"}) {
        CHECK(!TimeControl::fromPgnTag(bad).has_value());
    }
    CHECK_EQ((TimeControl{180'000, 2'000}).toPgnTag(), std::string("180+2"));
    CHECK_EQ((TimeControl{900'000, 0}).toPgnTag(), std::string("900"));
}

TEST(export_clock_format) {
    CHECK_EQ(formatClock(0), std::string("0:00:00"));
    CHECK_EQ(formatClock(298'300), std::string("0:04:58.3"));
    CHECK_EQ(formatClock(3'723'004), std::string("1:02:03.004"));
    CHECK_EQ(formatClock(-50), std::string("0:00:00"));
    CHECK(parseClock("0:04:58.3") == std::optional<int>(298'300));
    CHECK(parseClock("1:02:03") == std::optional<int>(3'723'000));
    CHECK(parseClock("12:00:00.25") == std::optional<int>(43'200'250));
    for (const char* bad : {"", "1:2", "::", "0:60:00", "0:00:61", "a:00:00", "0:00:00.", "1:00:00:00", " 0:01:00"}) {
        CHECK(!parseClock(bad).has_value());
    }
    for (int ms : {0, 1, 999, 1000, 59'999, 61'250, 3'599'999, 36'000'000}) {
        CHECK(parseClock(formatClock(ms)) == std::optional<int>(ms));
    }
}

TEST(export_writes_clocks_from_think_times) {
    const Game game = timedGame({{"e2e4", 2'000}, {"e7e5", 5'000}, {"g1f3", 1'500}, {"b8c6", 61'000}});
    PgnWriteOptions options;
    options.timeControl = TimeControl{60'000, 1'000};
    const std::string pgn = flat(toPgn(game, options));
    CHECK(has(pgn, "[TimeControl \"60+1\"]"));
    // 60 - 2 + 1 = 59, 60 - 5 + 1 = 56, 59 - 1.5 + 1 = 58.5, and black's clock can't go below zero before the
    // increment: 0 + 1 = 1.
    CHECK(has(pgn, "1. e4 { [%clk 0:00:59] [%emt 0:00:02] } e5 { [%clk 0:00:56] [%emt 0:00:05] }"));
    CHECK(has(pgn, "2. Nf3 { [%clk 0:00:58.5] [%emt 0:00:01.5] } Nc6 { [%clk 0:00:01] [%emt 0:01:01] }"));
    options.elapsed = false;
    CHECK(!has(toPgn(game, options), "%emt"));
    options.clocks = false;
    CHECK(!has(toPgn(game, options), "%clk"));
}

TEST(export_clocks_stop_where_times_are_unknown) {
    Game game;
    CHECK(game.playUci("e2e4", 1'000));
    CHECK(game.playUci("e7e5"));        // black's time unknown
    CHECK(game.playUci("g1f3", 1'000));
    CHECK(game.playUci("b8c6", 1'000)); // black's clock is still unknown after a missing time
    PgnWriteOptions options;
    options.timeControl = TimeControl{60'000, 0};
    const std::string pgn = flat(toPgn(game, options));
    CHECK(has(pgn, "e4 { [%clk 0:00:59] [%emt 0:00:01] } e5 2. Nf3 { [%clk 0:00:58] [%emt 0:00:01] }"));
    CHECK(has(pgn, "Nc6 { [%emt 0:00:01] }"));
}

TEST(export_reads_lichess_style_clocks) {
    // Only [%clk], the way Lichess exports: think times are worked out from the clocks and the increment.
    const std::string text = "[White \"a\"]\n[TimeControl \"60+1\"]\n\n"
                             "1. e4 { [%clk 0:01:00] } e5 { [%clk 0:00:58] } 2. Nf3 { [%clk 0:00:59] }\n"
                             "Nc6 {[%clk\n0:00:30.5]} *\n";
    const PgnGame g = parsePgn(text).at(0);
    CHECK(g.timeControl == (TimeControl{60'000, 1'000}));
    CHECK(g.clocks == (std::vector<std::optional<int>>{60'000, 58'000, 59'000, 30'500}));
    CHECK(g.game.moveTimes() == (std::vector<std::optional<int>>{1'000, 3'000, 2'000, 28'500}));
    // [%emt] wins over a clock difference when both are there.
    const PgnGame both = parsePgn("[TimeControl \"60\"]\n1. e4 { [%clk 0:00:50] [%emt 0:00:07] } *").at(0);
    CHECK(both.game.moveTimes()[0] == std::optional<int>(7'000));
    // Without a time control, a side's first move has nothing to compare with.
    const PgnGame noTc = parsePgn("1. e4 { [%clk 0:00:50] } e5 { [%clk 0:00:40] } 2. d4 { [%clk 0:00:45] } *").at(0);
    CHECK(noTc.game.moveTimes() == (std::vector<std::optional<int>>{std::nullopt, std::nullopt, 5'000}));
}

TEST(export_round_trips_clocks_and_times) {
    const Game game = timedGame({{"d2d4", 3'100}, {"d7d5", 900}, {"c2c4", 12'000}, {"e7e6", 4'400}});
    PgnWriteOptions options;
    options.timeControl = TimeControl{180'000, 2'000};
    const PgnGame back = parsePgn(toPgn(game, options)).at(0);
    CHECK(back.game.moveTimes() == game.moveTimes());
    CHECK(back.timeControl == options.timeControl);
    CHECK(back.clocks[0] == std::optional<int>(180'000 - 3'100 + 2'000));
    CHECK(back.clocks[3] == std::optional<int>(180'000 - 900 + 2'000 - 4'400 + 2'000));
}

// ---------- Timeouts ----------

TEST(export_timeout_rules) {
    Game lost;
    CHECK(lost.playUci("e2e4"));
    lost.timeout(Color::Black);
    CHECK(lost.status() == (GameStatus{GameResult::WhiteWins, GameEndReason::Timeout}));
    lost.timeout(Color::White); // ignored once the game is over
    CHECK(lost.status().result == GameResult::WhiteWins);

    // Flagging against a bare king, or a king and one minor piece, is a draw: they could never checkmate.
    for (const char* position : {"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1", "4k3/8/8/8/8/8/4P3/2N1K3 w - - 0 1"}) {
        Game drawn(Board::fromFen(position));
        drawn.timeout(Color::White);
        CHECK(drawn.status() == (GameStatus{GameResult::Draw, GameEndReason::Timeout}));
    }
    Game rook(Board::fromFen("4k2r/8/8/8/8/8/4P3/4K3 w - - 0 1"));
    rook.timeout(Color::White);
    CHECK(rook.status().result == GameResult::BlackWins);
    CHECK(rook.undo() == false); // nothing to undo, and the timeout stays
    CHECK(!isRuleEnding(GameEndReason::Timeout));
    CHECK(isRuleEnding(GameEndReason::Checkmate));
}

TEST(export_timeout_in_pgn) {
    Game game;
    CHECK(game.playUci("e2e4"));
    game.timeout(Color::Black);
    const std::string pgn = toPgn(game);
    CHECK(has(pgn, "[Termination \"time forfeit\"]"));
    CHECK(has(pgn, "[Result \"1-0\"]"));
    const PgnGame back = parsePgn(pgn).at(0);
    CHECK(back.game.status() == (GameStatus{GameResult::WhiteWins, GameEndReason::Timeout}));
    // A normal ending is labeled too, and unfinished games get no Termination tag.
    Game mate;
    for (const char* m : {"f2f3", "e7e5", "g2g4", "d8h4"}) CHECK(mate.playUci(m));
    CHECK(has(toPgn(mate), "[Termination \"normal\"]"));
    CHECK(!has(toPgn(Game()), "Termination"));
}

// ---------- Analysis in PGN ----------

TEST(export_eval_format) {
    CHECK_EQ(formatEval(35), std::string("0.35"));
    CHECK_EQ(formatEval(-120), std::string("-1.20"));
    CHECK_EQ(formatEval(0), std::string("0.00"));
    CHECK_EQ(formatEval(-5), std::string("-0.05"));
    CHECK_EQ(formatEval(analysis::mateToCentipawns(search::MATE - 1)), std::string("#1"));
    CHECK_EQ(formatEval(analysis::mateToCentipawns(search::MATE - 5)), std::string("#3"));
    CHECK_EQ(formatEval(analysis::mateToCentipawns(-(search::MATE - 4))), std::string("#-2"));
    CHECK_EQ(formatEval(10000), std::string("")); // already mate
}

TEST(export_annotated_game) {
    Game game;
    for (const char* m : {"e2e4", "e7e5", "d1h5", "b8c6", "f1c4", "g8f6", "h5f7"}) CHECK(game.playUci(m, 2'000));
    AnalysisSettings settings;
    settings.depth = 6;
    settings.hashMegabytes = 8;
    const GameAnalysis review = analyzeGame(game, settings);
    GameExportOptions options;
    options.analysis = &review;
    options.timeControl = TimeControl{300'000, 0};
    const std::string pgn = flat(exportGamePgn(game, options));

    Board beforeBlunder = Board::fromFen("r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq - 3 3");
    const std::string bestSan = toSan(beforeBlunder, review.moves[5].best);
    CHECK(has(pgn, "[Annotator \"Chess from Scratch\"]"));
    CHECK(has(pgn, "Nf6 $4 { [%eval #1] [%clk 0:04:54] [%emt 0:00:02] } { Blunder. " + bestSan + " was best. } (3... " +
                       bestSan));
    CHECK(has(pgn, "4. Qxf7# { [%clk 0:04:52] [%emt 0:00:02] } 1-0")); // no eval on the mating move
    CHECK(has(pgn, "1. e4 $") == false);                                // good moves get no glyph

    // It still reads back as the same game.
    const PgnGame back = parsePgn(exportGamePgn(game, options)).at(0);
    CHECK(back.game.moves() == game.moves());
    CHECK(back.game.moveTimes() == game.moveTimes());

    options.evals = false;
    CHECK(!has(exportGamePgn(game, options), "%eval"));
    options.judgments = false;
    const std::string bare = exportGamePgn(game, options);
    CHECK(!has(bare, "$4"));
    CHECK(!has(bare, "Blunder"));
    CHECK(!has(bare, "("));
    options.tags = {{"Annotator", "Someone else"}};
    CHECK(has(exportGamePgn(game, options), "[Annotator \"Someone else\"]"));
}

TEST(export_custom_annotations_are_safe) {
    Game game;
    CHECK(game.playUci("e2e4"));
    CHECK(game.playUci("e7e5"));
    PgnWriteOptions options;
    MoveAnnotation tricky;
    tricky.comment = "a } inside { and a very long comment that must wrap onto more lines than one because it is long";
    tricky.variation = {*moveFromUci(Board(), "d2d4"), *moveFromUci(Board(), "a2a3")}; // second move illegal: dropped
    options.annotations = {tricky};
    const std::string pgn = toPgn(game, options);
    for (const std::string& line : lines(pgn)) CHECK(line.size() <= 80);
    CHECK(has(flat(pgn), "(1. d4)"));
    const PgnGame back = parsePgn(pgn).at(0); // the stray '}' was neutralized, so this parses
    CHECK(back.game.moves() == game.moves());
}

TEST(export_many_games_in_one_file) {
    Game a;
    CHECK(a.playUci("e2e4"));
    Game b(Board::fromFen("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1"));
    CHECK(b.playUci("a1a8"));
    const std::string file = joinPgn({toPgn(a, {{"Round", "1"}}), toPgn(b, {{"Round", "2"}})});
    const std::vector<PgnGame> games = parsePgn(file);
    CHECK_EQ(static_cast<int>(games.size()), 2);
    CHECK_EQ(games[1].tag("Round"), std::string("2"));
    CHECK(games[1].game.startingBoard() == b.startingBoard());
    CHECK(joinPgn({}).empty());
}

// ---------- Dates ----------

TEST(export_date_tags) {
    using namespace std::chrono;
    const PgnTags epoch = dateTags(system_clock::time_point(seconds(0)));
    CHECK(epoch == (PgnTags{{"Date", "1970.01.01"}, {"UTCDate", "1970.01.01"}, {"UTCTime", "00:00:00"}}));
    const PgnTags later = dateTags(system_clock::time_point(seconds(1'795'464'299)));
    CHECK(later == (PgnTags{{"Date", "2026.11.23"}, {"UTCDate", "2026.11.23"}, {"UTCTime", "20:04:59"}}));
    const PgnTags leap = dateTags(system_clock::time_point(seconds(951'825'600))); // 2000-02-29 12:00:00
    CHECK(leap[0].second == "2000.02.29");
}

// ---------- Positions ----------

TEST(export_fen_list) {
    Game game;
    for (const char* m : {"e2e4", "c7c5", "g1f3"}) CHECK(game.playUci(m));
    const std::vector<std::string> fens = positionFens(game);
    CHECK_EQ(static_cast<int>(fens.size()), 4);
    CHECK_EQ(fens[0], std::string(START_FEN));
    CHECK_EQ(fens[3], game.board().toFen());
    for (const std::string& f : fens) CHECK(Board::fromFen(f).isConsistent());
}

TEST(export_epd) {
    CHECK_EQ(toEpd(Board()), std::string("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -"));
    CHECK_EQ(toEpd(Board(), {{"bm", "e4"}, {"id", "start position"}, {"c0", "a;b"}, {"noop", ""}}),
             std::string("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - bm e4; id \"start position\"; "
                         "c0 \"a;b\"; noop;"));
    // An EPD position is a valid FEN once the counters are added back.
    CHECK(Board::fromFen(toEpd(Board()) + " 0 1") == Board());
}

TEST(export_positions_as_epd) {
    Game game;
    for (const char* m : {"e2e4", "e7e5", "d1h5", "b8c6", "f1c4", "g8f6", "h5f7"}) CHECK(game.playUci(m));
    const std::vector<std::string> all = lines(exportPositionsEpd(game));
    CHECK_EQ(static_cast<int>(all.size()), 8); // start plus one per move
    CHECK(has(all[0], "c0 \"played e4\";"));
    CHECK(has(all[0], "id \"game 1.\";"));
    CHECK(!has(all[7], "c0")); // the final position has no move played from it

    AnalysisSettings settings;
    settings.depth = 6;
    settings.hashMegabytes = 8;
    const GameAnalysis review = analyzeGame(game, settings);
    EpdExportOptions options;
    options.analysis = &review;
    options.onlyMistakes = true;
    options.id = "practice";
    const std::vector<std::string> mistakes = lines(exportPositionsEpd(game, options));
    CHECK_EQ(static_cast<int>(mistakes.size()), 1);
    CHECK(has(mistakes[0], "r1bqkbnr/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR b KQkq -"));
    CHECK(has(mistakes[0], "am Nf6;"));
    CHECK(has(mistakes[0], "bm "));
    CHECK(has(mistakes[0], "id \"practice 3...\";"));
    CHECK(has(mistakes[0], "c0 \"played Nf6 (blunder)\";"));

    options.onlyMistakes = false;
    options.player = Color::White;
    const std::vector<std::string> white = lines(exportPositionsEpd(game, options));
    CHECK_EQ(static_cast<int>(white.size()), 4); // white to move before moves 1-4
    for (const std::string& line : white) CHECK(has(line, " w "));
    CHECK(has(white[3], "dm 1;")); // before Qxf7#: mate in one
}

// ---------- Comparison table ----------

TEST(export_summary_csv) {
    Game first;
    for (const auto& [m, ms] : {std::pair{"e2e4", 1'000}, {"e7e5", 3'000}, {"g1f3", 2'000}, {"b8c6", 5'000}}) {
        CHECK(first.playUci(m, ms));
    }
    first.resign(Color::Black);
    Game second(Board::fromFen("rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"));
    CHECK(second.playUci("e7e5"));
    AnalysisSettings settings;
    settings.depth = 4;
    settings.hashMegabytes = 8;
    const GameAnalysis review = analyzeGame(first, settings);

    const std::string csv = exportSummaryCsv({
        {&first, {{"White", "Smith, Jo"}, {"Black", "Pebble \"L1\""}, {"Date", "2026.11.23"}}, TimeControl{60'000, 0},
         &review},
        {&second, {}, std::nullopt, nullptr},
        {nullptr, {}, std::nullopt, nullptr}, // skipped
    });
    const std::vector<std::string> rows = lines(csv);
    CHECK_EQ(static_cast<int>(rows.size()), 3);
    CHECK(has(csv, "\r\n"));
    CHECK_EQ(static_cast<int>(std::count(rows[0].begin(), rows[0].end(), ',')), 20); // 21 columns
    CHECK(has(rows[1], "?,2026.11.23,\"Smith, Jo\",\"Pebble \"\"L1\"\"\",1-0,resignation,60,2,,"));
    CHECK(has(rows[1], ",1.5,4.0")); // seconds per move for white and black
    const std::string blackFirstFen = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1";
    CHECK(has(rows[2], "?,????.??.??,?,?,*,,-,1," + blackFirstFen + ",,,,,,,,,,,,"));
}

TEST(export_termination_text) {
    CHECK_EQ(terminationText({}), std::string(""));
    CHECK_EQ(terminationText({GameResult::Draw, GameEndReason::ThreefoldRepetition}),
             std::string("threefold repetition"));
    CHECK_EQ(terminationText({GameResult::WhiteWins, GameEndReason::Timeout}), std::string("time forfeit"));
    CHECK_EQ(terminationText({GameResult::BlackWins, GameEndReason::Checkmate}), std::string("checkmate"));
}

TEST(export_timeout_wording_from_other_programs) {
    for (const char* termination : {"Time forfeit", "TIME FORFEIT", "Hikaru won on time"}) {
        const std::string pgn =
            std::string("[Result \"1-0\"]\n[Termination \"") + termination + "\"]\n\n1. e4 e5 1-0\n";
        CHECK(parsePgn(pgn).at(0).game.status() == (GameStatus{GameResult::WhiteWins, GameEndReason::Timeout}));
    }
    // A drawn flag (the opponent couldn't mate) stays a timeout through PGN, instead of becoming an agreed draw.
    Game drawn(Board::fromFen("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
    drawn.timeout(Color::White);
    CHECK(parsePgn(toPgn(drawn)).at(0).game.status() == drawn.status());
    const std::string chessCom = "[Result \"1/2-1/2\"]\n[FEN \"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1\"]\n"
                                 "[Termination \"Game drawn by timeout vs insufficient material\"]\n\n1/2-1/2\n";
    CHECK(parsePgn(chessCom).at(0).game.status() == drawn.status());
    // A decisive flag the rules would call a draw keeps the file's result.
    const std::string odd = "[Result \"0-1\"]\n[FEN \"4k3/8/8/8/8/8/4P3/4K3 w - - 0 1\"]\n"
                            "[Termination \"time forfeit\"]\n\n0-1\n";
    CHECK(parsePgn(odd).at(0).game.status().result == GameResult::BlackWins);
}
