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
        for (int t = 0; t < NUM_PIECE_TYPES; ++t) occupancy_[c] |= pieces_[c][t];
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
    const Bitboard bit = squareBit(sq);
    for (int c = 0; c < NUM_COLORS; ++c) {
        if (!(occupancy_[c] & bit)) continue; // skip a whole color with one test
        for (int t = 0; t < NUM_PIECE_TYPES; ++t) {
            if (pieces_[c][t] & bit) return Piece{static_cast<PieceType>(t), static_cast<Color>(c)};
        }
    }
    return std::nullopt;
}

void Board::placePiece(Piece piece, Square sq) {
    removePiece(sq); // also validates sq
    const Bitboard bit = squareBit(sq);
    pieces_[idx(piece.color)][idx(piece.type)] |= bit;
    occupancy_[idx(piece.color)] |= bit;
}

void Board::removePiece(Square sq) {
    checkSquare(sq);
    const Bitboard keep = ~squareBit(sq);
    for (int c = 0; c < NUM_COLORS; ++c) {
        if ((occupancy_[c] & keep) == occupancy_[c]) continue; // this color has nothing on sq
        for (int t = 0; t < NUM_PIECE_TYPES; ++t) pieces_[c][t] &= keep;
        occupancy_[c] &= keep;
    }
}

std::string Board::toString() const {
    std::string out;
    out.reserve(9 * 18);
    for (int rank = 7; rank >= 0; --rank) {
        out += static_cast<char>('1' + rank);
        for (int file = 0; file < 8; ++file) {
            const auto piece = pieceAt(makeSquare(file, rank));
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
    if (enPassant_ != NO_SQUARE && !isValidSquare(enPassant_)) return false;
    if (castlingRights_ & ~ALL_CASTLING) return false;
    return true;
}
