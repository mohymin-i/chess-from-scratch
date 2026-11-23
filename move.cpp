#include "move.h"

#include <cstdlib>

namespace {
// Promotion letters in UCI are lowercase, indexed by PieceType.
constexpr char PROMOTION_LETTERS[NUM_PIECE_TYPES + 1] = "pnbrqk";

std::optional<PieceType> promotionFromLetter(char c) {
    switch (c) {
        case 'n': return PieceType::Knight;
        case 'b': return PieceType::Bishop;
        case 'r': return PieceType::Rook;
        case 'q': return PieceType::Queen;
        default: return std::nullopt;
    }
}
} // namespace

std::string toUci(Move move) {
    if (move.isNull()) return "0000";
    std::string out = squareToString(move.from()) + squareToString(move.to());
    if (move.isPromotion()) out += PROMOTION_LETTERS[static_cast<int>(move.promotionType())];
    return out;
}

std::optional<Move> moveFromUci(const Board& board, std::string_view text) {
    if (text == "0000") return Move();
    if (text.size() != 4 && text.size() != 5) return std::nullopt;

    const Square from = squareFromString(text.substr(0, 2));
    const Square to = squareFromString(text.substr(2, 2));
    if (from == NO_SQUARE || to == NO_SQUARE || from == to) return std::nullopt;

    std::optional<PieceType> promotion;
    if (text.size() == 5) {
        promotion = promotionFromLetter(text[4]);
        if (!promotion) return std::nullopt;
    }

    const std::optional<Piece> mover = board.pieceAt(from);
    if (!mover || mover->color != board.sideToMove()) return std::nullopt;

    const std::optional<Piece> target = board.pieceAt(to);
    if (target && target->color == mover->color) return std::nullopt;
    const bool capture = target.has_value();

    if (mover->type == PieceType::Pawn) {
        const int lastRank = mover->color == Color::White ? 7 : 0;
        const bool reachesLastRank = rankOf(to) == lastRank;
        if (reachesLastRank != promotion.has_value()) return std::nullopt;
        if (promotion) return Move(from, to, promotionFlag(*promotion, capture));

        if (to == board.enPassantSquare() && fileOf(from) != fileOf(to)) return Move(from, to, MoveFlag::EnPassant);
        if (std::abs(rankOf(to) - rankOf(from)) == 2) return Move(from, to, MoveFlag::DoublePawnPush);
        return Move(from, to, capture ? MoveFlag::Capture : MoveFlag::Quiet);
    }

    if (promotion) return std::nullopt; // only pawns promote

    // UCI writes castling as the king's two-square move (e1g1, e1c1, e8g8, e8c8).
    if (mover->type == PieceType::King && !capture) {
        const Square home = mover->color == Color::White ? E1 : E8;
        if (from == home && to == home + 2) return Move(from, to, MoveFlag::KingCastle);
        if (from == home && to == home - 2) return Move(from, to, MoveFlag::QueenCastle);
    }

    return Move(from, to, capture ? MoveFlag::Capture : MoveFlag::Quiet);
}
