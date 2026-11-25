#include "board.h"

#include <array>
#include <climits>
#include <stdexcept>

#include "attacks.h"
#include "move.h"

Square squareFromString(std::string_view name) {
    if (name.size() != 2) return NO_SQUARE;
    const char file = name[0];
    const char rank = name[1];
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') return NO_SQUARE;
    return makeSquare(file - 'a', rank - '1');
}

std::string squareToString(Square sq) {
    if (!isValidSquare(sq)) {
        throw std::out_of_range("squareToString: invalid square " + std::to_string(sq));
    }
    return {static_cast<char>('a' + fileOf(sq)), static_cast<char>('1' + rankOf(sq))};
}

Board Board::empty() {
    return Board(EmptyTag{});
}

Board::Board() {
    // Starting bitboards, documented in bitboards.md.
    pieces_[idx(Color::White)][idx(PieceType::Pawn)]   = 0x000000000000FF00ULL;
    pieces_[idx(Color::White)][idx(PieceType::Knight)] = 0x0000000000000042ULL;
    pieces_[idx(Color::White)][idx(PieceType::Bishop)] = 0x0000000000000024ULL;
    pieces_[idx(Color::White)][idx(PieceType::Rook)]   = 0x0000000000000081ULL;
    pieces_[idx(Color::White)][idx(PieceType::Queen)]  = 0x0000000000000008ULL;
    pieces_[idx(Color::White)][idx(PieceType::King)]   = 0x0000000000000010ULL;
    pieces_[idx(Color::Black)][idx(PieceType::Pawn)]   = 0x00FF000000000000ULL;
    pieces_[idx(Color::Black)][idx(PieceType::Knight)] = 0x4200000000000000ULL;
    pieces_[idx(Color::Black)][idx(PieceType::Bishop)] = 0x2400000000000000ULL;
    pieces_[idx(Color::Black)][idx(PieceType::Rook)]   = 0x8100000000000000ULL;
    pieces_[idx(Color::Black)][idx(PieceType::Queen)]  = 0x0800000000000000ULL;
    pieces_[idx(Color::Black)][idx(PieceType::King)]   = 0x1000000000000000ULL;

    for (int c = 0; c < NUM_COLORS; ++c) {
        for (int t = 0; t < NUM_PIECE_TYPES; ++t) {
            occupancy_[c] |= pieces_[c][t];
            // Visit each set bit: countr_zero finds the lowest one, b &= b - 1 clears it.
            for (Bitboard b = pieces_[c][t]; b; b &= b - 1) {
                mailbox_[std::countr_zero(b)] = Piece{static_cast<PieceType>(t), static_cast<Color>(c)};
            }
        }
    }
    castlingRights_ = ALL_CASTLING;
    hash_ = computeHash();
}

void Board::checkSquare(Square sq) {
    if (!isValidSquare(sq)) {
        throw std::out_of_range("Board: invalid square " + std::to_string(sq));
    }
}

std::optional<Piece> Board::pieceAt(Square sq) const {
    checkSquare(sq);
    return pieceAtUnchecked(sq);
}

void Board::placePiece(Piece piece, Square sq) {
    checkSquare(sq);
    placePieceUnchecked(piece, sq);
}

void Board::removePiece(Square sq) {
    checkSquare(sq);
    removePieceUnchecked(sq);
}

std::string Board::toString() const {
    std::string out;
    out.reserve(9 * 18);
    for (int rank = 7; rank >= 0; --rank) {
        out += static_cast<char>('1' + rank);
        for (int file = 0; file < 8; ++file) {
            const auto piece = pieceAtUnchecked(makeSquare(file, rank));
            out += ' ';
            out += piece ? pieceToChar(*piece) : '.';
        }
        out += '\n';
    }
    out += "  a b c d e f g h\n";
    return out;
}

bool Board::isConsistent() const {
    Bitboard seen = 0;
    for (int c = 0; c < NUM_COLORS; ++c) {
        Bitboard colorUnion = 0;
        for (int t = 0; t < NUM_PIECE_TYPES; ++t) {
            if (pieces_[c][t] & seen) return false; // two pieces on one square
            seen |= pieces_[c][t];
            colorUnion |= pieces_[c][t];
        }
        if (colorUnion != occupancy_[c]) return false;
    }
    // With no overlaps (checked above), this makes the mailbox and the bitboards an exact match.
    for (Square sq = A1; sq <= H8; ++sq) {
        const Bitboard bit = squareBit(sq);
        const std::optional<Piece>& piece = mailbox_[sq];
        if (piece) {
            if (!(pieces_[idx(piece->color)][idx(piece->type)] & bit)) return false;
        } else if (seen & bit) {
            return false;
        }
    }
    if (enPassant_ != NO_SQUARE && !isValidEnPassantSquare(enPassant_)) return false;
    if (castlingRights_ & ~ALL_CASTLING) return false;
    if (halfmoveClock_ < 0 || fullmoveNumber_ < 1) return false;
    return hash_ == computeHash();
}

bool Board::isValidEnPassantSquare(Square sq) {
    // After a double push the skipped square is on rank 3 (white pushed) or rank 6 (black pushed).
    return isValidSquare(sq) && (rankOf(sq) == 2 || rankOf(sq) == 5);
}

void Board::setCastlingRights(std::uint8_t rights) {
    if (rights & ~ALL_CASTLING) {
        throw std::invalid_argument("setCastlingRights: unknown flags in " + std::to_string(rights));
    }
    hash_ ^= zobrist::KEYS.castling[castlingRights_] ^ zobrist::KEYS.castling[rights];
    castlingRights_ = rights;
}

void Board::setEnPassantSquare(Square sq) {
    if (sq != NO_SQUARE) {
        checkSquare(sq); // std::out_of_range for a square off the board
        if (!isValidEnPassantSquare(sq)) {
            throw std::invalid_argument("setEnPassantSquare: " + squareToString(sq) + " is not on rank 3 or 6");
        }
    }
    if (enPassant_ != NO_SQUARE) hash_ ^= zobrist::KEYS.enPassantFile[fileOf(enPassant_)];
    if (sq != NO_SQUARE) hash_ ^= zobrist::KEYS.enPassantFile[fileOf(sq)];
    enPassant_ = sq;
}

void Board::setHalfmoveClock(int clock) {
    if (clock < 0) throw std::invalid_argument("setHalfmoveClock: negative value " + std::to_string(clock));
    halfmoveClock_ = clock;
}

void Board::setFullmoveNumber(int number) {
    if (number < 1) throw std::invalid_argument("setFullmoveNumber: must be at least 1, got " + std::to_string(number));
    fullmoveNumber_ = number;
}

std::uint64_t Board::computeHash() const {
    std::uint64_t h = 0;
    for (Square sq = A1; sq <= H8; ++sq) {
        if (const std::optional<Piece>& p = mailbox_[sq]) h ^= zobrist::KEYS.piece[idx(p->color)][idx(p->type)][sq];
    }
    h ^= zobrist::KEYS.castling[castlingRights_];
    if (enPassant_ != NO_SQUARE) h ^= zobrist::KEYS.enPassantFile[fileOf(enPassant_)];
    if (sideToMove_ == Color::Black) h ^= zobrist::KEYS.blackToMove;
    return h;
}

namespace {
// castlingRights &= CASTLING_KEEP[from] & CASTLING_KEEP[to] after every move: moving the king or a rook off its
// home square, or capturing a rook on its home square, loses the matching rights.
constexpr std::array<std::uint8_t, NUM_SQUARES> makeCastlingKeep() {
    std::array<std::uint8_t, NUM_SQUARES> keep{};
    for (auto& k : keep) k = ALL_CASTLING;
    keep[E1] = static_cast<std::uint8_t>(ALL_CASTLING & ~(WHITE_KINGSIDE | WHITE_QUEENSIDE));
    keep[H1] = static_cast<std::uint8_t>(ALL_CASTLING & ~WHITE_KINGSIDE);
    keep[A1] = static_cast<std::uint8_t>(ALL_CASTLING & ~WHITE_QUEENSIDE);
    keep[E8] = static_cast<std::uint8_t>(ALL_CASTLING & ~(BLACK_KINGSIDE | BLACK_QUEENSIDE));
    keep[H8] = static_cast<std::uint8_t>(ALL_CASTLING & ~BLACK_KINGSIDE);
    keep[A8] = static_cast<std::uint8_t>(ALL_CASTLING & ~BLACK_QUEENSIDE);
    return keep;
}
constexpr auto CASTLING_KEEP = makeCastlingKeep();

// Where the rook goes from and to for a castling move whose king lands on `kingTo`.
struct RookHop {
    Square from;
    Square to;
};
constexpr RookHop rookHopFor(Square kingTo) {
    switch (kingTo) {
        case G1: return {H1, F1};
        case C1: return {A1, D1};
        case G8: return {H8, F8};
        default: return {A8, D8}; // C8
    }
}
} // namespace

UndoInfo Board::makeMove(Move move) {
    UndoInfo undo{std::nullopt, castlingRights_, enPassant_, halfmoveClock_, hash_, fullmoveNumber_};

    const Square from = move.from();
    const Square to = move.to();
    const Color us = sideToMove_;
    const Color them = opposite(us);
    assert(mailbox_[from] && mailbox_[from]->color == us);
    const Piece mover = *mailbox_[from];

    // Clear the old en passant square from the hash; a new one may be set below.
    if (enPassant_ != NO_SQUARE) hash_ ^= zobrist::KEYS.enPassantFile[fileOf(enPassant_)];
    enPassant_ = NO_SQUARE;

    // The counters stop at INT_MAX instead of overflowing (a FEN may hold any int); unmakeMove restores them exactly.
    if (halfmoveClock_ < INT_MAX) ++halfmoveClock_;
    if (mover.type == PieceType::Pawn) halfmoveClock_ = 0;

    if (move.isEnPassant()) {
        const auto capturedSquare = static_cast<Square>(us == Color::White ? to - 8 : to + 8);
        undo.captured = mailbox_[capturedSquare];
        removePieceUnchecked(capturedSquare);
    } else if (mailbox_[to]) {
        undo.captured = mailbox_[to];
        halfmoveClock_ = 0;
        removePieceUnchecked(to);
    }

    removePieceUnchecked(from);
    putPieceOnEmpty(move.isPromotion() ? Piece{move.promotionType(), us} : mover, to);

    if (move.isCastle()) {
        const RookHop hop = rookHopFor(to);
        removePieceUnchecked(hop.from);
        putPieceOnEmpty(Piece{PieceType::Rook, us}, hop.to);
    }

    if (move.isDoublePawnPush()) {
        // Only record the en passant square if an enemy pawn is actually next to the pushed pawn.
        const auto skipped = static_cast<Square>(us == Color::White ? from + 8 : from - 8);
        if (pawnAttacks(us, skipped) & pieces(them, PieceType::Pawn)) {
            enPassant_ = skipped;
            hash_ ^= zobrist::KEYS.enPassantFile[fileOf(skipped)];
        }
    }

    const std::uint8_t newRights = castlingRights_ & CASTLING_KEEP[from] & CASTLING_KEEP[to];
    if (newRights != castlingRights_) {
        hash_ ^= zobrist::KEYS.castling[castlingRights_] ^ zobrist::KEYS.castling[newRights];
        castlingRights_ = newRights;
    }

    if (us == Color::Black && fullmoveNumber_ < INT_MAX) ++fullmoveNumber_;
    sideToMove_ = them;
    hash_ ^= zobrist::KEYS.blackToMove;
    return undo;
}

void Board::unmakeMove(Move move, const UndoInfo& undo) {
    const Square from = move.from();
    const Square to = move.to();
    const Color us = opposite(sideToMove_); // the side that made the move
    sideToMove_ = us;

    if (move.isCastle()) {
        const RookHop hop = rookHopFor(to);
        removePieceUnchecked(hop.to);
        putPieceOnEmpty(Piece{PieceType::Rook, us}, hop.from);
    }

    const Piece moved = move.isPromotion() ? Piece{PieceType::Pawn, us} : *mailbox_[to];
    removePieceUnchecked(to);
    putPieceOnEmpty(moved, from);

    if (undo.captured) {
        const Square capturedSquare =
            move.isEnPassant() ? static_cast<Square>(us == Color::White ? to - 8 : to + 8) : to;
        putPieceOnEmpty(*undo.captured, capturedSquare);
    }

    castlingRights_ = undo.castlingRights;
    enPassant_ = undo.enPassant;
    halfmoveClock_ = undo.halfmoveClock;
    fullmoveNumber_ = undo.fullmoveNumber;
    hash_ = undo.hash; // cheaper than undoing each XOR, and exact by construction
}

UndoInfo Board::makeNullMove() {
    const UndoInfo undo{std::nullopt, castlingRights_, enPassant_, halfmoveClock_, hash_, fullmoveNumber_};
    if (enPassant_ != NO_SQUARE) hash_ ^= zobrist::KEYS.enPassantFile[fileOf(enPassant_)];
    enPassant_ = NO_SQUARE;
    if (halfmoveClock_ < INT_MAX) ++halfmoveClock_;
    sideToMove_ = opposite(sideToMove_);
    hash_ ^= zobrist::KEYS.blackToMove;
    return undo;
}

void Board::unmakeNullMove(const UndoInfo& undo) {
    sideToMove_ = opposite(sideToMove_);
    enPassant_ = undo.enPassant;
    halfmoveClock_ = undo.halfmoveClock;
    hash_ = undo.hash;
}
