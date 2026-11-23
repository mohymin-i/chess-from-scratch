#ifndef CHESS_FROM_SCRATCH_MOVE_H
#define CHESS_FROM_SCRATCH_MOVE_H

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "board.h"
#include "piece.h"

// What kind of move it is, beyond its from and to squares. The values follow the common 4-bit scheme
// (see chessprogramming.org "Encoding Moves"): bit 3 (8) marks a promotion, bit 2 (4) a capture, and for
// promotions the low two bits pick the piece (knight, bishop, rook, queen).
enum class MoveFlag : std::uint8_t {
    Quiet = 0,
    DoublePawnPush = 1,
    KingCastle = 2,
    QueenCastle = 3,
    Capture = 4,
    EnPassant = 5,
    KnightPromotion = 8,
    BishopPromotion = 9,
    RookPromotion = 10,
    QueenPromotion = 11,
    KnightPromotionCapture = 12,
    BishopPromotionCapture = 13,
    RookPromotionCapture = 14,
    QueenPromotionCapture = 15,
};

// A move packed into 16 bits: bits 0-5 are the from square, 6-11 the to square, 12-15 the MoveFlag.
// Small enough that move lists and the transposition table stay compact, and copying one is free.
//
// Move() is the "null move" (all zero bits). It never comes out of move generation, so it can mean "no move".
class Move {
public:
    constexpr Move() = default;
    constexpr Move(Square from, Square to, MoveFlag flag = MoveFlag::Quiet)
        : bits_(static_cast<std::uint16_t>(from | (to << 6) | (static_cast<int>(flag) << 12))) {}

    // The from and to squares must be valid; that's the caller's job, like the unchecked Board functions.
    static constexpr Move fromRaw(std::uint16_t bits) {
        Move m;
        m.bits_ = bits;
        return m;
    }
    constexpr std::uint16_t raw() const { return bits_; }

    constexpr Square from() const { return static_cast<Square>(bits_ & 0x3F); }
    constexpr Square to() const { return static_cast<Square>((bits_ >> 6) & 0x3F); }
    constexpr MoveFlag flag() const { return static_cast<MoveFlag>(bits_ >> 12); }

    constexpr bool isNull() const { return bits_ == 0; }
    constexpr bool isCapture() const { return flagBits() & 4; } // includes en passant and promotion captures
    constexpr bool isPromotion() const { return flagBits() & 8; }
    constexpr bool isEnPassant() const { return flag() == MoveFlag::EnPassant; }
    constexpr bool isCastle() const { return flag() == MoveFlag::KingCastle || flag() == MoveFlag::QueenCastle; }
    constexpr bool isDoublePawnPush() const { return flag() == MoveFlag::DoublePawnPush; }
    // Knight, bishop, rook or queen. Only meaningful when isPromotion().
    constexpr PieceType promotionType() const { return static_cast<PieceType>((flagBits() & 3) + 1); }

    constexpr bool operator==(const Move&) const = default;

private:
    constexpr int flagBits() const { return bits_ >> 12; }

    std::uint16_t bits_ = 0;
};

static_assert(sizeof(Move) == 2, "Move must stay 16 bits");

// The flag for promoting to `type` (knight, bishop, rook or queen), with or without a capture.
constexpr MoveFlag promotionFlag(PieceType type, bool capture) {
    return static_cast<MoveFlag>(8 + (capture ? 4 : 0) + (static_cast<int>(type) - 1));
}

// UCI long algebraic notation, as used by chess engines: "e2e4", "e7e8q", "e1g1" for castling.
// The null move is "0000".
std::string toUci(Move move);

// Reads UCI text and fills in the flags (capture, en passant, castling, double push) from the board, since the
// text alone doesn't say. Returns nullopt for bad text, an empty from square, a piece of the side not to move, a
// capture of your own piece, or a promotion letter that doesn't match whether a pawn reaches the last rank.
// "0000" gives the null move.
// It does not check that the piece can actually move that way; matching against generated legal moves (#12)
// does that.
std::optional<Move> moveFromUci(const Board& board, std::string_view text);

#endif // CHESS_FROM_SCRATCH_MOVE_H
