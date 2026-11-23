#include "game.h"

#include <algorithm>
#include <utility>

namespace {
constexpr Bitboard LIGHT_SQUARES = 0x55AA55AA55AA55AAULL; // b1, d1, ..., a2, c2, ...: squares where file + rank is odd
} // namespace

bool isInsufficientMaterial(const Board& board) {
    for (const Color c : {Color::White, Color::Black}) {
        const Bitboard heavy =
            board.pieces(c, PieceType::Pawn) | board.pieces(c, PieceType::Rook) | board.pieces(c, PieceType::Queen);
        if (heavy) return false;
    }
    const auto both = [&board](PieceType type) {
        return board.pieces(Color::White, type) | board.pieces(Color::Black, type);
    };
    const Bitboard knights = both(PieceType::Knight);
    const Bitboard bishops = both(PieceType::Bishop);
    const int minors = popCount(knights | bishops);
    if (minors <= 1) return true; // K v K, or K + one minor v K
    if (knights) return false;    // two or more minors including a knight can mate (with help)
    // Only bishops: mate needs bishops on both colors.
    return (bishops & LIGHT_SQUARES) == 0 || (bishops & ~LIGHT_SQUARES) == 0;
}

Game::Game(const Board& start) : start_(start), board_(start) {
    hashes_.push_back(board_.hash());
    refresh();
}

void Game::refresh() {
    legal_.clear();
    generateLegal(board_, legal_);
}

bool Game::play(Move move, std::optional<int> thinkTimeMs) {
    if (status().isOver() || !legal_.contains(move)) return false;
    undos_.push_back(board_.makeMove(move));
    moves_.push_back(move);
    times_.push_back(thinkTimeMs && *thinkTimeMs >= 0 ? thinkTimeMs : std::nullopt);
    hashes_.push_back(board_.hash());
    redo_.clear();
    refresh();
    return true;
}

bool Game::playUci(std::string_view uci, std::optional<int> thinkTimeMs) {
    const std::optional<Move> move = moveFromUci(board_, uci);
    return move && play(*move, thinkTimeMs);
}

void Game::setMoveTime(std::size_t index, std::optional<int> thinkTimeMs) {
    if (index < times_.size()) times_[index] = thinkTimeMs && *thinkTimeMs >= 0 ? thinkTimeMs : std::nullopt;
}

bool Game::undo() {
    if (moves_.empty()) return false;
    manualEnd_.reset();
    board_.unmakeMove(moves_.back(), undos_.back());
    redo_.emplace_back(moves_.back(), times_.back());
    moves_.pop_back();
    times_.pop_back();
    undos_.pop_back();
    hashes_.pop_back();
    refresh();
    return true;
}

bool Game::redo() {
    if (redo_.empty()) return false;
    const auto [move, time] = redo_.back();
    redo_.pop_back();
    auto rest = std::move(redo_);
    const bool ok = play(move, time); // clears redo_, so restore the rest afterwards
    redo_ = std::move(rest);
    return ok;
}

void Game::resign(Color loser) {
    if (status().isOver()) return;
    manualEnd_ = GameStatus{loser == Color::White ? GameResult::BlackWins : GameResult::WhiteWins,
                            GameEndReason::Resignation};
}

void Game::agreeDraw() {
    if (status().isOver()) return;
    manualEnd_ = GameStatus{GameResult::Draw, GameEndReason::DrawAgreed};
}

int Game::repetitionCount() const {
    // Only positions since the last capture or pawn move can repeat, and only every second one has the same
    // side to move.
    const int current = static_cast<int>(hashes_.size()) - 1;
    const int oldest = std::max(0, current - board_.halfmoveClock());
    int count = 1;
    for (int i = current - 2; i >= oldest; i -= 2) {
        if (hashes_[i] == hashes_[current]) ++count;
    }
    return count;
}

GameStatus Game::status() const {
    if (manualEnd_) return *manualEnd_;
    // Checkmate and stalemate come first: a mate delivered on the 100th half-move still wins.
    if (legal_.empty()) {
        if (inCheck(board_)) {
            return {board_.sideToMove() == Color::White ? GameResult::BlackWins : GameResult::WhiteWins,
                    GameEndReason::Checkmate};
        }
        return {GameResult::Draw, GameEndReason::Stalemate};
    }
    if (isInsufficientMaterial(board_)) return {GameResult::Draw, GameEndReason::InsufficientMaterial};
    if (board_.halfmoveClock() >= 100) return {GameResult::Draw, GameEndReason::FiftyMoveRule};
    if (repetitionCount() >= 3) return {GameResult::Draw, GameEndReason::ThreefoldRepetition};
    return {};
}
