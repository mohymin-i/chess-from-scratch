// Standard algebraic notation (SAN), the move format people read and write: e4, Nf3, exd5, O-O, e8=Q+, Nbd2,
// R1e1, Qxf7#.
#ifndef CHESS_FROM_SCRATCH_SAN_H
#define CHESS_FROM_SCRATCH_SAN_H

#include <optional>
#include <string>
#include <string_view>

#include "board.h"
#include "move.h"

// Writes a legal move in SAN. It disambiguates only as much as needed (file first, then rank, then both) and
// appends + for check and # for checkmate.
std::string toSan(const Board& board, Move move);

// Reads SAN and returns the matching legal move, or nullopt if no legal move matches or more than one does.
// It's lenient in the ways real-world files are: check and mate marks, annotations (!, ?, !?), "e.p.",
// zeros for castling (0-0), a missing "x" or "=", an explicit pawn letter (Pe4) and extra disambiguation (Ng1f3,
// Ng1-f3) are all accepted.
// Piece letters must be uppercase, because "b" is the b-file.
std::optional<Move> moveFromSan(const Board& board, std::string_view san);

#endif // CHESS_FROM_SCRATCH_SAN_H
