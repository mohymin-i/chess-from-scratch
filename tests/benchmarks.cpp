// Performance benchmarks for the engine's hot paths, plus a deterministic "bench signature".
//
//   ./chess_bench            run every benchmark and print a table
//   ./chess_bench search     run only benchmarks whose name contains "search"
//   ./chess_bench --check    also fail if a benchmark is far below its floor (see below)
//
// Each benchmark repeats its work until about BENCH_SECONDS have passed and reports operations per second.
//
// Floors. The floors are deliberately about 10x below what an optimized build does on a 2020 laptop, so they only
// catch real regressions (an accidental O(n^2), a debug-only check left in a hot loop), never a slow CI machine.
// They're only enforced in an optimized build without sanitizers; elsewhere --check just runs everything once, so
// the benchmarks themselves stay tested.
//
// Signature. The total node count of a fixed-depth search over the bench positions. Search is deterministic, so this
// changes only when the search or evaluation changes. A change that's meant to be a pure speed-up must leave it
// alone; compare it before and after.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "analysis.h"
#include "bot.h"
#include "eval.h"
#include "movegen.h"
#include "pgn.h"
#include "san.h"
#include "search.h"

#if defined(__SANITIZE_ADDRESS__)
#define CHESS_BENCH_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#define CHESS_BENCH_SANITIZED 1
#endif
#endif
#if defined(__OPTIMIZE__) && !defined(CHESS_BENCH_SANITIZED)
constexpr bool FLOORS_APPLY = true;
#else
constexpr bool FLOORS_APPLY = false;
#endif

namespace {

constexpr double BENCH_SECONDS = 0.4;

// A spread of positions: opening, middlegames with tactics, endgames. The perft ones are the Chess Programming Wiki
// test positions; the rest are ordinary positions from play.
const char* const POSITIONS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bq1rk1/pp2bppp/2n1pn2/3p4/2PP4/2N1PN2/PP1B1PPP/R2QKB1R w KQ - 2 8",
    "2r2rk1/pp1bqppp/2n1pn2/3p4/3P4/2PBPN2/P1Q2PPP/R1B2RK1 w - - 4 13",
    "r1b2rk1/2q1bppp/p2p1n2/np2p3/3PP3/5N1P/PPBN1PP1/R1BQR1K1 w - - 1 13",
    "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    "8/8/4kpp1/3p4/p6P/2B4b/6P1/6K1 w - - 0 1",
    "8/5k2/8/3K4/8/8/2P5/8 w - - 0 1",
};

std::vector<Board> positions() {
    std::vector<Board> out;
    for (const char* fen : POSITIONS) out.push_back(Board::fromFen(fen));
    return out;
}

// A fixed 60-ply game (the engine against itself at depth 4), for the notation and analysis benchmarks.
const Game& sampleGame() {
    static const Game game = [] {
        Game g;
        Searcher searcher(4);
        SearchLimits limits;
        limits.maxDepth = 4;
        while (!g.status().isOver() && g.moves().size() < 60) {
            g.play(searcher.search(g.board(), g.positionHistory(), limits).bestMove, 1000);
        }
        return g;
    }();
    return game;
}

struct Benchmark {
    const char* name;
    const char* unit;
    double floor; // minimum units per second in an optimized build; 0 for none
    // Does one batch of work and returns how many units it did.
    std::function<std::uint64_t()> run;
};

volatile std::uint64_t sink = 0; // keeps the optimizer from deleting work whose result isn't otherwise used

std::vector<Benchmark> benchmarks() {
    std::vector<Benchmark> list;

    list.push_back({"perft_start_d4", "nodes", 5e6, [] {
                        Board b;
                        return perft(b, 4);
                    }});
    list.push_back({"perft_kiwipete_d3", "nodes", 5e6, [] {
                        Board b = Board::fromFen(POSITIONS[1]);
                        return perft(b, 3);
                    }});
    list.push_back({"generate_legal", "positions", 5e5, [] {
                        static const std::vector<Board> ps = positions();
                        std::uint64_t n = 0;
                        for (const Board& b : ps) {
                            MoveList l;
                            generateLegal(b, l);
                            sink = sink + l.size();
                            ++n;
                        }
                        return n;
                    }});
    list.push_back({"generate_pseudo_legal", "positions", 2e6, [] {
                        static const std::vector<Board> ps = positions();
                        std::uint64_t n = 0;
                        for (const Board& b : ps) {
                            MoveList l;
                            generatePseudoLegal(b, l);
                            sink = sink + l.size();
                            ++n;
                        }
                        return n;
                    }});
    list.push_back({"make_unmake", "moves", 1e7, [] {
                        static const std::vector<Board> ps = positions();
                        std::uint64_t n = 0;
                        for (Board b : ps) {
                            MoveList l;
                            generatePseudoLegal(b, l);
                            for (const Move m : l) {
                                const UndoInfo u = b.makeMove(m);
                                sink = sink + b.hash();
                                b.unmakeMove(m, u);
                                ++n;
                            }
                        }
                        return n;
                    }});
    list.push_back({"evaluate", "evals", 1e6, [] {
                        static const std::vector<Board> ps = positions();
                        std::uint64_t n = 0;
                        for (const Board& b : ps) {
                            sink = sink + static_cast<std::uint64_t>(evaluate(b));
                            ++n;
                        }
                        return n;
                    }});
    list.push_back({"static_exchange", "captures", 2e6, [] {
                        static const std::vector<Board> ps = positions();
                        std::uint64_t n = 0;
                        for (const Board& b : ps) {
                            MoveList l;
                            generatePseudoLegal(b, l, GenType::Noisy);
                            for (const Move m : l) {
                                sink = sink + static_cast<std::uint64_t>(search::staticExchange(b, m));
                                ++n;
                            }
                        }
                        return n;
                    }});
    list.push_back({"search_d7", "nodes", 3e5, [] {
                        static const std::vector<Board> ps = positions();
                        Searcher searcher(16);
                        SearchLimits limits;
                        limits.maxDepth = 7;
                        std::uint64_t n = 0;
                        for (const Board& b : ps) n += searcher.search(b, {}, limits).nodes;
                        return n;
                    }});
    list.push_back({"bot_pebble_move", "moves", 20, [] {
                        Bot bot(*findBot("Pebble"), 1);
                        const Game& g = sampleGame();
                        Game replay(g.startingBoard());
                        std::uint64_t n = 0;
                        for (std::size_t i = 0; i < 20 && i < g.moves().size(); ++i) {
                            sink = sink + bot.chooseMove(replay).raw();
                            replay.play(g.moves()[i]);
                            ++n;
                        }
                        return n;
                    }});
    list.push_back({"to_san", "moves", 2e5, [] {
                        const Game& g = sampleGame();
                        Board b = g.startingBoard();
                        std::uint64_t n = 0;
                        for (const Move m : g.moves()) {
                            sink = sink + toSan(b, m).size();
                            b.makeMove(m);
                            ++n;
                        }
                        return n;
                    }});
    list.push_back({"from_san", "moves", 2e5, [] {
                        static const std::vector<std::string> sans = [] {
                            const Game& g = sampleGame();
                            Board b = g.startingBoard();
                            std::vector<std::string> out;
                            for (const Move m : g.moves()) {
                                out.push_back(toSan(b, m));
                                b.makeMove(m);
                            }
                            return out;
                        }();
                        Board b = sampleGame().startingBoard();
                        for (const std::string& s : sans) b.makeMove(*moveFromSan(b, s));
                        return static_cast<std::uint64_t>(sans.size());
                    }});
    list.push_back({"pgn_write", "games", 2e3, [] {
                        PgnWriteOptions o;
                        o.timeControl = TimeControl{300'000, 2'000};
                        sink = sink + toPgn(sampleGame(), o).size();
                        return std::uint64_t{1};
                    }});
    list.push_back({"pgn_parse", "games", 2e3, [] {
                        static const std::string text = toPgn(sampleGame());
                        sink = sink + parsePgn(text).size();
                        return std::uint64_t{1};
                    }});
    list.push_back({"game_play_and_undo", "moves", 1e5, [] {
                        const Game& g = sampleGame();
                        Game replay(g.startingBoard());
                        for (const Move m : g.moves()) replay.play(m);
                        while (replay.undo()) {
                        }
                        return 2 * static_cast<std::uint64_t>(g.moves().size());
                    }});
    list.push_back({"fen_round_trip", "positions", 5e5, [] {
                        std::uint64_t n = 0;
                        for (const char* fen : POSITIONS) {
                            sink = sink + Board::fromFen(fen).toFen().size();
                            ++n;
                        }
                        return n;
                    }});
    list.push_back({"analyze_game_d6", "positions", 50, [] {
                        AnalysisSettings s;
                        s.depth = 6;
                        s.hashMegabytes = 16;
                        sink = sink + analyzeGame(sampleGame(), s).moves.size();
                        return static_cast<std::uint64_t>(sampleGame().moves().size()) + 1;
                    }});
    return list;
}

std::uint64_t benchSignature() {
    Searcher searcher(16);
    SearchLimits limits;
    limits.maxDepth = 7;
    std::uint64_t nodes = 0;
    for (const char* fen : POSITIONS) nodes += searcher.search(Board::fromFen(fen), {}, limits).nodes;
    return nodes;
}

} // namespace

int main(int argc, char* argv[]) {
    bool check = false;
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--check") == 0) {
            check = true;
        } else {
            filter = argv[i];
        }
    }
    // Under --check in an unoptimized or sanitized build, run each benchmark once: it still proves the code paths
    // work, without taking minutes.
    const bool quick = check && !FLOORS_APPLY;

    std::printf("%-24s %16s  %-10s %s\n", "benchmark", "per second", "unit", "floor");
    int failures = 0;
    int ran = 0;
    for (const Benchmark& b : benchmarks()) {
        if (filter && std::strstr(b.name, filter) == nullptr) continue;
        ++ran;
        b.run(); // warm-up: first-touch allocations, the sample game, static tables
        const auto start = std::chrono::steady_clock::now();
        std::uint64_t units = 0;
        double seconds = 0;
        do {
            units += b.run();
            seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        } while (!quick && seconds < BENCH_SECONDS);
        const double rate = static_cast<double>(units) / seconds;
        const bool belowFloor = FLOORS_APPLY && b.floor > 0 && rate < b.floor;
        std::printf("%-24s %16.0f  %-10s %.0f%s\n", b.name, rate, b.unit, b.floor, belowFloor ? "  BELOW FLOOR" : "");
        if (belowFloor) ++failures;
    }
    if (ran == 0) {
        std::printf("No benchmarks matched.\n");
        return 1;
    }
    if (!filter) std::printf("\nbench signature: %llu nodes\n", static_cast<unsigned long long>(benchSignature()));
    if (!FLOORS_APPLY) std::printf("(floors not enforced: unoptimized or sanitized build)\n");
    return check && failures > 0 ? 1 : 0;
}
