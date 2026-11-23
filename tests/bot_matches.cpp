// Full games between bots, checking that each difficulty level really is stronger than the one below it.
// Levels 1-3 are limited by depth and nodes (not time) and the bots are seeded, so these games are the same on
// every run on a given platform. Runs as its own ctest entry labeled "slow".
#include <string>

#include "bot.h"
#include "test_framework.h"

namespace {
const char* OPENINGS[] = {
    "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1",    // 1. e4
    "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2", // 1. d4 d5
};

// Plays one game and returns the result from `first`'s point of view: 1 win, 0.5 draw, 0 loss.
double playGame(const BotSettings& first, const BotSettings& second, const char* opening, bool firstIsWhite,
                std::uint64_t seed) {
    Game game(Board::fromFen(opening));
    Bot a(first, seed);
    Bot b(second, seed + 1);
    const Color firstColor = firstIsWhite ? Color::White : Color::Black;
    while (!game.status().isOver() && game.moves().size() < 300) {
        Bot& mover = game.board().sideToMove() == firstColor ? a : b;
        const Move m = mover.chooseMove(game);
        if (!game.play(m)) ::chess_test::fail(__FILE__, __LINE__, "bot played an illegal move: " + toUci(m));
    }
    const GameResult r = game.status().result;
    if (r == GameResult::WhiteWins) return firstIsWhite ? 1.0 : 0.0;
    if (r == GameResult::BlackWins) return firstIsWhite ? 0.0 : 1.0;
    return 0.5; // draws, and games stopped at the move cap
}

void checkStronger(int strongLevel, int weakLevel) {
    const BotSettings strong = *findBot(std::to_string(strongLevel));
    const BotSettings weak = *findBot(std::to_string(weakLevel));
    double score = 0;
    int games = 0;
    for (const char* opening : OPENINGS) {
        for (const bool white : {true, false}) {
            score += playGame(strong, weak, opening, white, 100 + games);
            ++games;
        }
    }
    if (score * 2 <= games) {
        ::chess_test::fail(__FILE__, __LINE__,
                           strong.name + " scored only " + std::to_string(score) + "/" + std::to_string(games) +
                               " against " + weak.name);
    }
}
} // namespace

TEST(bot_matches_level_2_beats_level_1) { checkStronger(2, 1); }
TEST(bot_matches_level_3_beats_level_2) { checkStronger(3, 2); }
