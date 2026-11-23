#include "movegen.h"

#include "attacks.h"

namespace {

constexpr PieceType PROMOTION_PIECES[] = {PieceType::Queen, PieceType::Knight, PieceType::Rook, PieceType::Bishop};

void addPromotions(MoveList& list, Square from, Square to, bool capture) {
    for (const PieceType type : PROMOTION_PIECES) list.push(Move(from, to, promotionFlag(type, capture)));
}

void generatePawnMoves(const Board& board, MoveList& list, GenType type) {
    const Color us = board.sideToMove();
    const Color them = opposite(us);
    const Bitboard pawns = board.pieces(us, PieceType::Pawn);
    const Bitboard enemies = board.occupancy(them);
    const Bitboard empty = ~board.occupancy();
    const int up = us == Color::White ? 8 : -8;
    const Bitboard lastRank = us == Color::White ? RANK_8 : RANK_1;
    const Bitboard doublePushRank = us == Color::White ? rankMask(3) : rankMask(4); // where a double push lands
    const bool noisy = type != GenType::Quiet;
    const bool quiet = type != GenType::Noisy;

    const auto shiftUp = [us](Bitboard b) { return us == Color::White ? b << 8 : b >> 8; };

    // Pushes. Promotions count as noisy even without a capture, because they change the material balance.
    const Bitboard single = shiftUp(pawns) & empty;
    for (Bitboard b = single & lastRank; b && noisy;) {
        const Square to = popLsb(b);
        addPromotions(list, static_cast<Square>(to - up), to, false);
    }
    if (quiet) {
        for (Bitboard b = single & ~lastRank; b;) {
            const Square to = popLsb(b);
            list.push(Move(static_cast<Square>(to - up), to));
        }
        for (Bitboard b = shiftUp(single) & empty & doublePushRank; b;) {
            const Square to = popLsb(b);
            list.push(Move(static_cast<Square>(to - 2 * up), to, MoveFlag::DoublePawnPush));
        }
    }

    if (!noisy) return;

    // Captures.
    for (Bitboard b = pawns; b;) {
        const Square from = popLsb(b);
        for (Bitboard targets = pawnAttacks(us, from) & enemies; targets;) {
            const Square to = popLsb(targets);
            if (squareBit(to) & lastRank) {
                addPromotions(list, from, to, true);
            } else {
                list.push(Move(from, to, MoveFlag::Capture));
            }
        }
    }

    // En passant: our pawns that attack the en passant square are exactly those a pawn of theirs on that square
    // would attack.
    const Square ep = board.enPassantSquare();
    if (ep != NO_SQUARE) {
        for (Bitboard b = pawns & pawnAttacks(them, ep); b;) list.push(Move(popLsb(b), ep, MoveFlag::EnPassant));
    }
}

void generatePieceMoves(const Board& board, MoveList& list, GenType type, PieceType pieceType) {
    const Color us = board.sideToMove();
    const Bitboard occupied = board.occupancy();
    Bitboard targetMask = 0;
    if (type != GenType::Quiet) targetMask |= board.occupancy(opposite(us));
    if (type != GenType::Noisy) targetMask |= ~occupied;

    for (Bitboard pieces = board.pieces(us, pieceType); pieces;) {
        const Square from = popLsb(pieces);
        for (Bitboard targets = pieceAttacks(pieceType, from, occupied) & targetMask; targets;) {
            const Square to = popLsb(targets);
            list.push(Move(from, to, (squareBit(to) & occupied) ? MoveFlag::Capture : MoveFlag::Quiet));
        }
    }
}

void generateCastling(const Board& board, MoveList& list) {
    const Color us = board.sideToMove();
    const Color them = opposite(us);
    const std::uint8_t rights = board.castlingRights();
    const bool white = us == Color::White;
    const std::uint8_t kingside = white ? WHITE_KINGSIDE : BLACK_KINGSIDE;
    const std::uint8_t queenside = white ? WHITE_QUEENSIDE : BLACK_QUEENSIDE;
    if (!(rights & (kingside | queenside))) return;

    const Square king = white ? E1 : E8;
    const Bitboard occupied = board.occupancy();
    if (isSquareAttacked(board, king, them)) return; // can't castle out of check

    if (rights & kingside) {
        const auto f = static_cast<Square>(king + 1);
        const auto g = static_cast<Square>(king + 2);
        if (!(occupied & (squareBit(f) | squareBit(g))) && !isSquareAttacked(board, f, them) &&
            !isSquareAttacked(board, g, them)) {
            list.push(Move(king, g, MoveFlag::KingCastle));
        }
    }
    if (rights & queenside) {
        const auto d = static_cast<Square>(king - 1);
        const auto c = static_cast<Square>(king - 2);
        const auto b = static_cast<Square>(king - 3); // must be empty, but may be attacked
        if (!(occupied & (squareBit(d) | squareBit(c) | squareBit(b))) && !isSquareAttacked(board, d, them) &&
            !isSquareAttacked(board, c, them)) {
            list.push(Move(king, c, MoveFlag::QueenCastle));
        }
    }
}

} // namespace

void generatePseudoLegal(const Board& board, MoveList& list, GenType type) {
    generatePawnMoves(board, list, type);
    for (const PieceType pieceType :
         {PieceType::Knight, PieceType::Bishop, PieceType::Rook, PieceType::Queen, PieceType::King}) {
        generatePieceMoves(board, list, type, pieceType);
    }
    if (type != GenType::Noisy) generateCastling(board, list);
}

bool isSquareAttacked(const Board& board, Square sq, Color by, Bitboard occupied) {
    // A pawn of `by` attacks sq exactly when a pawn of the other color on sq would attack it.
    if (pawnAttacks(opposite(by), sq) & board.pieces(by, PieceType::Pawn)) return true;
    if (knightAttacks(sq) & board.pieces(by, PieceType::Knight)) return true;
    if (kingAttacks(sq) & board.pieces(by, PieceType::King)) return true;
    const Bitboard queens = board.pieces(by, PieceType::Queen);
    if (bishopAttacks(sq, occupied) & (board.pieces(by, PieceType::Bishop) | queens) & occupied) return true;
    if (rookAttacks(sq, occupied) & (board.pieces(by, PieceType::Rook) | queens) & occupied) return true;
    return false;
}

Bitboard attackersTo(const Board& board, Square sq, Bitboard occupied) {
    Bitboard result = 0;
    for (const Color c : {Color::White, Color::Black}) {
        result |= pawnAttacks(opposite(c), sq) & board.pieces(c, PieceType::Pawn);
        result |= knightAttacks(sq) & board.pieces(c, PieceType::Knight);
        result |= kingAttacks(sq) & board.pieces(c, PieceType::King);
        const Bitboard queens = board.pieces(c, PieceType::Queen);
        result |= bishopAttacks(sq, occupied) & (board.pieces(c, PieceType::Bishop) | queens);
        result |= rookAttacks(sq, occupied) & (board.pieces(c, PieceType::Rook) | queens);
    }
    return result & occupied;
}

bool isLegal(const Board& board, Move move) {
    Board copy = board;
    copy.makeMove(move);
    return !moverLeftInCheck(copy);
}

void generateLegal(const Board& board, MoveList& list) {
    MoveList pseudo;
    generatePseudoLegal(board, pseudo);
    Board scratch = board;
    for (const Move move : pseudo) {
        const UndoInfo undo = scratch.makeMove(move);
        if (!moverLeftInCheck(scratch)) list.push(move);
        scratch.unmakeMove(move, undo);
    }
}

std::uint64_t perft(Board& board, int depth) {
    if (depth == 0) return 1;
    MoveList moves;
    generatePseudoLegal(board, moves);
    std::uint64_t nodes = 0;
    for (const Move move : moves) {
        const UndoInfo undo = board.makeMove(move);
        if (!moverLeftInCheck(board)) nodes += depth == 1 ? 1 : perft(board, depth - 1);
        board.unmakeMove(move, undo);
    }
    return nodes;
}

std::vector<std::pair<Move, std::uint64_t>> perftDivide(Board& board, int depth) {
    std::vector<std::pair<Move, std::uint64_t>> result;
    if (depth < 1) return result;
    MoveList moves;
    generateLegal(board, moves);
    for (const Move move : moves) {
        const UndoInfo undo = board.makeMove(move);
        result.emplace_back(move, perft(board, depth - 1));
        board.unmakeMove(move, undo);
    }
    return result;
}
