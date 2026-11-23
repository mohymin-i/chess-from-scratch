#include <initializer_list>
#include <stdexcept>
#include <string>

#include "game.h"
#include "test_framework.h"

namespace {
Game fromFen(const char* fen) { return Game(Board::fromFen(fen)); }

void playAll(Game& game, std::initializer_list<const char*> moves) {
    for (const char* m : moves) {
        if (!game.playUci(m)) throw std::runtime_error(std::string("could not play ") + m);
    }
}
} // namespace

// ---------- Endings by rule ----------

TEST(game_checkmate) {
    Game game;
    playAll(game, {"f2f3", "e7e5", "g2g4"});
    CHECK(!game.status().isOver());
    CHECK(game.playUci("d8h4"));
    CHECK(game.status() == (GameStatus{GameResult::BlackWins, GameEndReason::Checkmate}));
    CHECK(game.legalMoves().empty());
    CHECK(!game.playUci("a2a3")); // nothing can be played after the game ends
}

TEST(game_stalemate) {
    Game game = fromFen("7k/8/6Q1/8/8/8/8/6K1 w - - 0 1");
    CHECK(game.playUci("g6f7"));
    CHECK(game.status() == (GameStatus{GameResult::Draw, GameEndReason::Stalemate}));
}

TEST(game_threefold_repetition) {
    Game game;
    playAll(game, {"g1f3", "g8f6", "f3g1", "f6g8"}); // start position, second time
    CHECK_EQ(game.repetitionCount(), 2);
    playAll(game, {"g1f3", "g8f6", "f3g1"});
    CHECK(!game.status().isOver());
    CHECK(game.playUci("f6g8")); // third time
    CHECK_EQ(game.repetitionCount(), 3);
    CHECK(game.status() == (GameStatus{GameResult::Draw, GameEndReason::ThreefoldRepetition}));
}

// Same piece placement but different castling rights is a different position.
TEST(game_repetition_respects_castling_rights) {
    Game game;
    playAll(game, {"e2e4", "e7e5", "e1e2", "e8e7", "e2e1", "e7e8"});
    CHECK_EQ(game.repetitionCount(), 1); // the identical-looking position after 2...e5 still had castling rights
    playAll(game, {"e1e2", "e8e7", "e2e1", "e7e8"});
    CHECK_EQ(game.repetitionCount(), 2);
    // After 1. e1e2 in the first lap black could still castle, so this position has only been seen once before.
    playAll(game, {"e1e2"});
    CHECK_EQ(game.repetitionCount(), 2);
    CHECK(!game.status().isOver());
    // King on e2, king on e7, no rights: seen in every lap, so this is the third time.
    playAll(game, {"e8e7"});
    CHECK_EQ(game.repetitionCount(), 3);
    CHECK(game.status().reason == GameEndReason::ThreefoldRepetition);
}

TEST(game_fifty_move_rule) {
    Game game = fromFen("4k3/8/8/8/8/8/8/R3K3 w - - 99 70");
    CHECK(!game.status().isOver());
    CHECK(game.playUci("a1a2"));
    CHECK(game.status() == (GameStatus{GameResult::Draw, GameEndReason::FiftyMoveRule}));
}

TEST(game_mate_on_the_hundredth_half_move_still_wins) {
    Game game = fromFen("7k/8/6K1/8/8/8/8/R7 w - - 99 80");
    CHECK(game.playUci("a1a8"));
    CHECK_EQ(game.board().halfmoveClock(), 100);
    CHECK(game.status() == (GameStatus{GameResult::WhiteWins, GameEndReason::Checkmate}));
}

TEST(game_insufficient_material) {
    const auto dead = [](const char* fen) { return isInsufficientMaterial(Board::fromFen(fen)); };
    CHECK(dead("4k3/8/8/8/8/8/8/4K3 w - - 0 1"));           // K v K
    CHECK(dead("4k3/8/8/8/8/8/8/4KN2 w - - 0 1"));          // K+N v K
    CHECK(dead("4k3/8/8/8/8/8/8/4KB2 w - - 0 1"));          // K+B v K
    CHECK(dead("4kb2/8/8/8/8/8/8/2B1K3 w - - 0 1"));        // c1 and f8 are both dark
    CHECK(dead("4k3/8/8/8/8/8/B7/B3K3 w - - 0 1") == false); // a1 dark, a2 light
    CHECK(!dead("4k3/8/8/8/8/8/8/2B1KB2 w - - 0 1"));       // c1 dark, f1 light
    CHECK(!dead("4kb2/8/8/8/8/8/8/3BK3 w - - 0 1"));        // d1 light, f8 dark
    CHECK(!dead("4k3/8/8/8/8/8/8/3NKN2 w - - 0 1"));        // two knights can mate if the defender helps
    CHECK(!dead("4k3/8/8/8/8/8/8/3NKB2 w - - 0 1"));
    CHECK(!dead("4k3/8/8/8/8/8/4P3/4K3 w - - 0 1"));
    CHECK(!dead("4k3/8/8/8/8/8/8/R3K3 w - - 0 1"));
    CHECK(!dead(std::string(START_FEN).c_str()));
}

TEST(game_insufficient_material_after_capture) {
    Game game = fromFen("4k3/8/8/8/8/8/4r3/4KN2 w - - 0 1");
    CHECK(!game.status().isOver());
    CHECK(game.playUci("e1e2"));
    CHECK(game.status() == (GameStatus{GameResult::Draw, GameEndReason::InsufficientMaterial}));
}

// ---------- Playing, undo, redo ----------

TEST(game_rejects_illegal_moves) {
    Game game;
    CHECK(!game.playUci("e2e5"));
    CHECK(!game.playUci("e7e5")); // black's move, white to play
    CHECK(!game.playUci("junk"));
    CHECK(!game.play(Move()));
    CHECK(game.moves().empty());
    CHECK(game.board() == Board());
}

TEST(game_undo_and_redo) {
    Game game;
    playAll(game, {"e2e4", "e7e5", "g1f3"});
    Board afterOne;
    afterOne.makeMove(*moveFromUci(afterOne, "e2e4"));

    CHECK(game.undo());
    CHECK(game.undo());
    CHECK(game.board() == afterOne);
    CHECK_EQ(static_cast<int>(game.moves().size()), 1);
    CHECK_EQ(static_cast<int>(game.positionHistory().size()), 2);
    CHECK(game.canRedo());

    CHECK(game.redo());
    CHECK(game.redo());
    CHECK(!game.redo());
    CHECK_EQ(toUci(game.moves().back()), std::string("g1f3"));

    CHECK(game.undo());
    CHECK(game.playUci("b1c3")); // a new move drops the redo list
    CHECK(!game.canRedo());

    while (game.undo()) {}
    CHECK(game.board() == Board());
    CHECK(!game.undo());
}

TEST(game_undo_reopens_a_finished_game) {
    Game game;
    playAll(game, {"f2f3", "e7e5", "g2g4", "d8h4"});
    CHECK(game.status().isOver());
    CHECK(game.undo());
    CHECK(!game.status().isOver());
    CHECK_EQ(game.legalMoves().size(), 30); // black's options before the mate (20 start moves + 10 more after e5)
}

TEST(game_resign_and_draw_offer) {
    Game game;
    game.resign(Color::White);
    CHECK(game.status() == (GameStatus{GameResult::BlackWins, GameEndReason::Resignation}));
    CHECK(!game.playUci("e2e4"));
    game.agreeDraw(); // ignored once the game is over
    CHECK(game.status().reason == GameEndReason::Resignation);

    Game other;
    CHECK(other.playUci("e2e4"));
    other.agreeDraw();
    CHECK(other.status() == (GameStatus{GameResult::Draw, GameEndReason::DrawAgreed}));
    CHECK(other.undo());
    CHECK(!other.status().isOver());
}
