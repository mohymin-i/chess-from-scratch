#include "game.h"

#include <algorithm>
#include <utility>

#include "attacks.h"

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

bool hasMatingMaterial(const Board& board, Color side) {
    const Color other = opposite(side);
    if (board.pieces(side, PieceType::Pawn) | board.pieces(side, PieceType::Rook) |
        board.pieces(side, PieceType::Queen)) {
        return true;
    }
    const Bitboard knights = board.pieces(side, PieceType::Knight);
    const Bitboard bishops = board.pieces(side, PieceType::Bishop);
    if (!knights && !bishops) return false; // a bare king
    if (knights && bishops) return true;
    if (popCount(knights) >= 2) return true; // two knights can't force mate, but a mate can still happen
    const Bitboard theirPawns = board.pieces(other, PieceType::Pawn);
    const Bitboard theirKnights = board.pieces(other, PieceType::Knight);
    const Bitboard theirBishops = board.pieces(other, PieceType::Bishop);
    if (knights) {
        // A lone knight mates only a king hemmed in by its own pieces. Like Lichess, queens alone don't count.
        return (theirPawns | theirKnights | theirBishops | board.pieces(other, PieceType::Rook)) != 0;
    }
    // Only bishops. On both colors they can mate; all on one color, the opponent must supply the blockers.
    const bool light = bishops & LIGHT_SQUARES;
    const bool dark = bishops & ~LIGHT_SQUARES;
    if (light && dark) return true;
    const Bitboard otherColorSquares = light ? ~LIGHT_SQUARES : LIGHT_SQUARES;
    return (theirPawns | theirKnights | (theirBishops & otherColorSquares)) != 0;
}

namespace {
Board withoutUselessEnPassant(Board board) {
    const Square ep = board.enPassantSquare();
    const Color us = board.sideToMove();
    if (ep != NO_SQUARE && !(pawnAttacks(opposite(us), ep) & board.pieces(us, PieceType::Pawn))) {
        board.setEnPassantSquare(NO_SQUARE);
    }
    return board;
}
} // namespace

Game::Game(const Board& start) : start_(withoutUselessEnPassant(start)), board_(start_) {
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

void Game::timeout(Color loser) {
    if (status().isOver()) return;
    const Color winner = opposite(loser);
    if (!hasMatingMaterial(board_, winner)) {
        manualEnd_ = GameStatus{GameResult::Draw, GameEndReason::Timeout};
    } else {
        manualEnd_ = GameStatus{winner == Color::White ? GameResult::WhiteWins : GameResult::BlackWins,
                                GameEndReason::Timeout};
    }
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
