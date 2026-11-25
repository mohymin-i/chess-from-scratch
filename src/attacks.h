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

namespace attacks_detail {

// The straightforward version: walk each ray to its first blocker. It's the reference the fast version below is
// built from and tested against.
constexpr Bitboard rookAttacksByRays(Square sq, Bitboard occupied) {
    return rayAttacks<NORTH>(sq, occupied) | rayAttacks<EAST>(sq, occupied) | rayAttacks<SOUTH>(sq, occupied) |
           rayAttacks<WEST>(sq, occupied);
}
constexpr Bitboard bishopAttacksByRays(Square sq, Bitboard occupied) {
    return rayAttacks<NORTH_EAST>(sq, occupied) | rayAttacks<NORTH_WEST>(sq, occupied) |
           rayAttacks<SOUTH_EAST>(sq, occupied) | rayAttacks<SOUTH_WEST>(sq, occupied);
}

// The fast version ("kindergarten bitboards", see chessprogramming.org). Along one line (a rank, a diagonal or an
// anti-diagonal), each file holds exactly one square, so a multiplication gathers the line's six inner squares (the
// b- to g-files; the edge squares can't block anything beyond them) into a 6-bit index. A small table then gives the
// attacked files for a slider on that file, and masking with the line turns files back into squares. Files work the
// same way after a different multiplication. No branches, and the tables total 8 KB.
inline constexpr Bitboard B_FILE = FILE_A << 1;
inline constexpr Bitboard FILE_GATHER = 0x0080402010080400ULL; // collects a2..a7 into the top six bits

struct LineMasks {
    std::array<Bitboard, NUM_SQUARES> rank{}, diagonal{}, antiDiagonal{}; // the line through sq, without sq
};
constexpr LineMasks makeLineMasks() {
    LineMasks m;
    for (int sq = 0; sq < NUM_SQUARES; ++sq) {
        m.rank[sq] = RAYS[EAST][sq] | RAYS[WEST][sq];
        m.diagonal[sq] = RAYS[NORTH_EAST][sq] | RAYS[SOUTH_WEST][sq];
        m.antiDiagonal[sq] = RAYS[NORTH_WEST][sq] | RAYS[SOUTH_EAST][sq];
    }
    return m;
}
inline constexpr LineMasks LINE_MASKS = makeLineMasks();

constexpr int lineIndex(Bitboard lineMask, Bitboard occupied) {
    return static_cast<int>(((lineMask & occupied) * B_FILE) >> 58);
}
constexpr int fileIndex(Square sq, Bitboard occupied) {
    return static_cast<int>(((FILE_A & (occupied >> fileOf(sq))) * FILE_GATHER) >> 58);
}

// FILE_FILL[file][index]: the files a slider on `file` attacks, copied onto every rank.
// A_FILE[rank][index]: the squares a slider on the a-file attacks along it.
// Both are filled by trying every blocker pattern with the ray version, so they agree with it by construction.
using LineTable = std::array<std::array<Bitboard, 64>, 8>;
constexpr LineTable makeFileFill() {
    LineTable table{};
    for (int file = 0; file < 8; ++file) {
        const auto sq = static_cast<Square>(file); // on rank 1
        for (int inner = 0; inner < 64; ++inner) {
            const Bitboard occupied = static_cast<Bitboard>(inner) << 1; // blockers on b1..g1
            const Bitboard onRank = rookAttacksByRays(sq, occupied) & RANK_1;
            table[file][lineIndex(LINE_MASKS.rank[sq], occupied)] = onRank * FILE_A;
        }
    }
    return table;
}
constexpr LineTable makeAFile() {
    LineTable table{};
    for (int rank = 0; rank < 8; ++rank) {
        const auto sq = static_cast<Square>(rank * 8); // on the a-file
        for (int inner = 0; inner < 64; ++inner) {
            Bitboard occupied = 0; // blockers on a2..a7
            for (int r = 1; r <= 6; ++r) {
                if (inner & (1 << (r - 1))) occupied |= Bitboard{1} << (8 * r);
            }
            table[rank][fileIndex(sq, occupied)] = rookAttacksByRays(sq, occupied) & FILE_A;
        }
    }
    return table;
}
inline constexpr LineTable FILE_FILL = makeFileFill();
inline constexpr LineTable A_FILE = makeAFile();

constexpr Bitboard lineAttacks(Bitboard lineMask, Square sq, Bitboard occupied) {
    return lineMask & FILE_FILL[fileOf(sq)][lineIndex(lineMask, occupied)];
}

} // namespace attacks_detail

constexpr Bitboard rookAttacks(Square sq, Bitboard occupied) {
    using namespace attacks_detail;
    return lineAttacks(LINE_MASKS.rank[sq], sq, occupied) | (A_FILE[rankOf(sq)][fileIndex(sq, occupied)] << fileOf(sq));
}
constexpr Bitboard bishopAttacks(Square sq, Bitboard occupied) {
    using namespace attacks_detail;
    return lineAttacks(LINE_MASKS.diagonal[sq], sq, occupied) | lineAttacks(LINE_MASKS.antiDiagonal[sq], sq, occupied);
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
