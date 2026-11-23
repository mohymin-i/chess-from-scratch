// Deep perft: millions of positions per case, against published counts. A mismatch anywhere means a move
// generation, make/unmake or legality bug. Runs as its own ctest entry labeled "slow".
//
// Sources: the Chess Programming Wiki "Perft Results" page (the six standard positions) and the widely used
// collection of tricky positions that each target one rule (en passant pins, castling through check, and so on).
#include <iterator>
#include <string>

#include "movegen.h"
#include "test_framework.h"

namespace {
struct DeepCase {
    const char* fen;
    int depth;
    std::uint64_t count;
};

void runAll(const DeepCase* begin, const DeepCase* end) {
    for (const DeepCase* c = begin; c != end; ++c) {
        Board board = Board::fromFen(c->fen);
        const std::uint64_t got = perft(board, c->depth);
        if (got != c->count) {
            ::chess_test::fail(__FILE__, __LINE__,
                               std::string(c->fen) + " depth " + std::to_string(c->depth) + ": got " +
                                   std::to_string(got) + ", expected " + std::to_string(c->count));
        }
    }
}
} // namespace

TEST(perft_deep_standard_positions) {
    const DeepCase cases[] = {
        {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609},
        {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603},
        {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, 674624},
        {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, 422333},
        {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487},
        {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594},
    };
    runAll(std::begin(cases), std::end(cases));
}

TEST(perft_deep_tricky_positions) {
    const DeepCase cases[] = {
        {"3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1134888},          // illegal en passant (pin)
        {"8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1015133},         // illegal en passant (bishop)
        {"8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467},        // en passant gives check
        {"5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661072},              // short castle gives check
        {"3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803711},              // long castle gives check
        {"r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206},  // castling rights
        {"r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1720476},   // castling prevented
        {"2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3821001},          // promote out of check
        {"8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1004658},        // discovered check
        {"4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217342},              // promote to give check
        {"8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683},                // underpromote to give check
        {"K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2217},                 // self stalemate
        {"8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567584},              // stalemate and checkmate
        {"8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23527},            // double check
        {"r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1", 4, 422333}, // mirrored position 4
    };
    runAll(std::begin(cases), std::end(cases));
}
