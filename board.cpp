#include "board.h"

#include <stdexcept>

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
    for (Square sq = 0; sq < NUM_SQUARES; ++sq) {
        const Bitboard bit = squareBit(sq);
        const std::optional<Piece>& piece = mailbox_[sq];
        if (piece) {
            if (!(pieces_[idx(piece->color)][idx(piece->type)] & bit)) return false;
        } else if (seen & bit) {
            return false;
        }
    }
    if (enPassant_ != NO_SQUARE && !isValidSquare(enPassant_)) return false;
    if (castlingRights_ & ~ALL_CASTLING) return false;
    return true;
}
