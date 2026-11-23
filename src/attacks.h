// Which squares each piece attacks, as bitboards.
//
// Knights, kings and pawns ("leapers") always attack the same squares from a given square, so those are
// precomputed tables. Rooks, bishops and queens ("sliders") stop at the first piece in each direction, so their
// attacks depend on the occupancy. They use precomputed rays: take the full ray in a direction, find the
// nearest blocker on it with a single bit scan, and cut the ray off behind that blocker.
//
// Everything here is computed at compile time and has no state.
#ifndef CHESS_FROM_SCRATCH_ATTACKS_H
#define CHESS_FROM_SCRATCH_ATTACKS_H

#include <array>
#include <bit>
#include <cstdint>

#include "board.h"
#include "piece.h"

// ---------- Masks ----------

inline constexpr Bitboard FILE_A = 0x0101010101010101ULL;
inline constexpr Bitboard FILE_H = FILE_A << 7;
inline constexpr Bitboard RANK_1 = 0xFFULL;
inline constexpr Bitboard RANK_8 = RANK_1 << 56;

constexpr Bitboard fileMask(int file) { return FILE_A << file; }
constexpr Bitboard rankMask(int rank) { return RANK_1 << (8 * rank); }

// Index of the lowest set bit. b must not be empty.
constexpr Square lsb(Bitboard b) { return static_cast<Square>(std::countr_zero(b)); }
// Index of the highest set bit. b must not be empty.
constexpr Square msb(Bitboard b) { return static_cast<Square>(63 - std::countl_zero(b)); }
// Removes and returns the lowest set bit's square, for `while (b) { Square sq = popLsb(b); ... }` loops.
constexpr Square popLsb(Bitboard& b) {
    const Square sq = lsb(b);
    b &= b - 1;
    return sq;
}

namespace attacks_detail {

// Steps from sq by (df, dr) and returns the bit for the target, or 0 if it falls off the board.
// Working in file/rank coordinates is what prevents the classic wrap-around bug (h-file to a-file).
constexpr Bitboard step(int sq, int df, int dr) {
    const int file = sq % 8 + df;
    const int rank = sq / 8 + dr;
    if (file < 0 || file > 7 || rank < 0 || rank > 7) return 0;
    return Bitboard{1} << (rank * 8 + file);
}

template <std::size_t N>
constexpr std::array<Bitboard, NUM_SQUARES> leaperTable(const int (&deltas)[N][2]) {
    std::array<Bitboard, NUM_SQUARES> table{};
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        for (const auto& d : deltas) table[sq] |= step(sq, d[0], d[1]);
    }
    return table;
}

constexpr int KNIGHT_DELTAS[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int KING_DELTAS[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr int WHITE_PAWN_DELTAS[2][2] = {{-1, 1}, {1, 1}};
constexpr int BLACK_PAWN_DELTAS[2][2] = {{-1, -1}, {1, -1}};

inline constexpr auto KNIGHT = leaperTable(KNIGHT_DELTAS);
inline constexpr auto KING = leaperTable(KING_DELTAS);
inline constexpr std::array<std::array<Bitboard, NUM_SQUARES>, NUM_COLORS> PAWN = {
    leaperTable(WHITE_PAWN_DELTAS), leaperTable(BLACK_PAWN_DELTAS)};

// The 8 ray directions. The first four go toward higher square numbers, so the nearest blocker on them is the
// lowest set bit; the last four go toward lower numbers, so it's the highest set bit.
enum Direction : std::uint8_t {
    NORTH,
    EAST,
    NORTH_EAST,
    NORTH_WEST,
    SOUTH,
    WEST,
    SOUTH_WEST,
    SOUTH_EAST,
    NUM_DIRECTIONS,
};
constexpr int DIRECTION_DELTAS[NUM_DIRECTIONS][2] = {{0, 1},  {1, 0},  {1, 1},   {-1, 1},
                                                     {0, -1}, {-1, 0}, {-1, -1}, {1, -1}};

// RAYS[dir][sq]: every square from sq (exclusive) to the edge of the board in direction dir.
constexpr std::array<std::array<Bitboard, NUM_SQUARES>, NUM_DIRECTIONS> makeRays() {
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_DIRECTIONS> rays{};
    for (int dir = 0; dir < NUM_DIRECTIONS; ++dir) {
        for (int sq = 0; sq < NUM_SQUARES; ++sq) {
            for (int n = 1; n < 8; ++n) {
                const Bitboard bit = step(sq, DIRECTION_DELTAS[dir][0] * n, DIRECTION_DELTAS[dir][1] * n);
                if (!bit) break;
                rays[dir][sq] |= bit;
            }
        }
    }
    return rays;
}
inline constexpr auto RAYS = makeRays();

template <Direction dir>
constexpr Bitboard rayAttacks(Square sq, Bitboard occupied) {
    const Bitboard ray = RAYS[dir][sq];
    const Bitboard blockers = ray & occupied;
    if (!blockers) return ray;
    const Square blocker = dir < SOUTH ? lsb(blockers) : msb(blockers);
    // Everything past the blocker is in the blocker's own ray in the same direction. The blocker itself stays
    // in, because that's the piece being attacked (or defended).
    return ray ^ RAYS[dir][blocker];
}

// BETWEEN[a][b]: the squares strictly between a and b if they share a rank, file or diagonal, otherwise 0.
// LINE[a][b]: the whole line through a and b (edge to edge, including both) if they're aligned, otherwise 0.
constexpr std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SQUARES> makeBetween() {
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SQUARES> table{};
    for (int a = 0; a < NUM_SQUARES; ++a) {
        for (int dir = 0; dir < NUM_DIRECTIONS; ++dir) {
            Bitboard between = 0;
            for (int n = 1; n < 8; ++n) {
                const Bitboard bit = step(a, DIRECTION_DELTAS[dir][0] * n, DIRECTION_DELTAS[dir][1] * n);
                if (!bit) break;
                table[a][std::countr_zero(bit)] = between;
                between |= bit;
            }
        }
    }
    return table;
}
inline constexpr auto BETWEEN = makeBetween();

constexpr std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SQUARES> makeLines() {
    std::array<std::array<Bitboard, NUM_SQUARES>, NUM_SQUARES> table{};
    for (int a = 0; a < NUM_SQUARES; ++a) {
        for (int dir = 0; dir < NUM_DIRECTIONS; ++dir) {
            const int opposite = (dir + 4) % NUM_DIRECTIONS;
            const Bitboard line = RAYS[dir][a] | RAYS[opposite][a] | (Bitboard{1} << a);
            for (Bitboard b = RAYS[dir][a]; b; b &= b - 1) table[a][std::countr_zero(b)] = line;
        }
    }
    return table;
}
inline constexpr auto LINE = makeLines();

} // namespace attacks_detail

// ---------- Leapers ----------

constexpr Bitboard knightAttacks(Square sq) { return attacks_detail::KNIGHT[sq]; }
constexpr Bitboard kingAttacks(Square sq) { return attacks_detail::KING[sq]; }
// The two diagonal squares a pawn of `color` on sq attacks (one on the a- and h-files).
constexpr Bitboard pawnAttacks(Color color, Square sq) { return attacks_detail::PAWN[static_cast<int>(color)][sq]; }

// ---------- Sliders ----------

constexpr Bitboard rookAttacks(Square sq, Bitboard occupied) {
    using namespace attacks_detail;
    return rayAttacks<NORTH>(sq, occupied) | rayAttacks<EAST>(sq, occupied) | rayAttacks<SOUTH>(sq, occupied) |
           rayAttacks<WEST>(sq, occupied);
}
constexpr Bitboard bishopAttacks(Square sq, Bitboard occupied) {
    using namespace attacks_detail;
    return rayAttacks<NORTH_EAST>(sq, occupied) | rayAttacks<NORTH_WEST>(sq, occupied) |
           rayAttacks<SOUTH_EAST>(sq, occupied) | rayAttacks<SOUTH_WEST>(sq, occupied);
}
constexpr Bitboard queenAttacks(Square sq, Bitboard occupied) {
    return rookAttacks(sq, occupied) | bishopAttacks(sq, occupied);
}

// Attacks of any non-pawn piece type. For pawns use pawnAttacks, which needs the color.
constexpr Bitboard pieceAttacks(PieceType type, Square sq, Bitboard occupied) {
    switch (type) {
        case PieceType::Knight: return knightAttacks(sq);
        case PieceType::Bishop: return bishopAttacks(sq, occupied);
        case PieceType::Rook: return rookAttacks(sq, occupied);
        case PieceType::Queen: return queenAttacks(sq, occupied);
        case PieceType::King: return kingAttacks(sq);
        case PieceType::Pawn: return 0;
    }
    return 0;
}

// ---------- Geometry ----------

constexpr Bitboard between(Square a, Square b) { return attacks_detail::BETWEEN[a][b]; }
constexpr Bitboard line(Square a, Square b) { return attacks_detail::LINE[a][b]; }

#endif // CHESS_FROM_SCRATCH_ATTACKS_H
