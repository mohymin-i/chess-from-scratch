// A game in progress: the current board plus everything needed for the rules that depend on history
// (threefold repetition) and for undo/redo. The GUI and the bots talk to this, not to Board directly.
#ifndef CHESS_FROM_SCRATCH_GAME_H
#define CHESS_FROM_SCRATCH_GAME_H

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "board.h"
#include "move.h"
#include "movegen.h"

enum class GameResult : std::uint8_t { Ongoing, WhiteWins, BlackWins, Draw };

enum class GameEndReason : std::uint8_t {
    None,
    Checkmate,
    Stalemate,
    FiftyMoveRule,        // 100 half-moves with no capture or pawn move
    ThreefoldRepetition,  // the same position, with the same side to move and the same rights, three times
    InsufficientMaterial, // neither side can possibly checkmate
    Resignation,
    DrawAgreed,
};

struct GameStatus {
    GameResult result = GameResult::Ongoing;
    GameEndReason reason = GameEndReason::None;

    bool isOver() const { return result != GameResult::Ongoing; }
    bool operator==(const GameStatus&) const = default;
};

// True when no sequence of legal moves can end in checkmate: K v K, K + one minor piece v K, or only bishops
// left besides the kings and all of them on squares of the same color.
bool isInsufficientMaterial(const Board& board);

class Game {
public:
    Game() : Game(Board()) {}
    explicit Game(const Board& start);

    const Board& board() const { return board_; }
    const Board& startingBoard() const { return start_; }
    const std::vector<Move>& moves() const { return moves_; }

    // Hashes of every position so far, oldest first, including the current one. Search uses this to spot
    // repetitions that reach back into the game.
    const std::vector<std::uint64_t>& positionHistory() const { return hashes_; }

    // Plays a move if it's legal and the game isn't over. Returns false (and changes nothing) otherwise.
    // Playing a move clears the redo list.
    bool play(Move move);
    bool playUci(std::string_view uci);

    bool canUndo() const { return !moves_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    bool undo(); // also clears a resignation or agreed draw
    bool redo();

    void resign(Color loser);
    void agreeDraw();

    // Computed fresh from the current position each time.
    GameStatus status() const;

    const MoveList& legalMoves() const { return legal_; }

    // How many times the current position has occurred, counting now.
    int repetitionCount() const;

private:
    void refresh(); // recompute legal moves after the board changes

    Board start_;
    Board board_;
    std::vector<Move> moves_;
    std::vector<UndoInfo> undos_;
    std::vector<std::uint64_t> hashes_;
    std::vector<Move> redo_;
    MoveList legal_;
    std::optional<GameStatus> manualEnd_; // resignation or agreed draw
};

#endif // CHESS_FROM_SCRATCH_GAME_H
