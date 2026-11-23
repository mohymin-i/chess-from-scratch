#include <initializer_list>
#include <random>

#include "attacks.h"
#include "test_framework.h"

namespace {
// Reference implementation on a 0x88 board, written independently of attacks.h. In 0x88 a square is
// rank * 16 + file, and (sq & 0x88) != 0 exactly when a step has left the board, so wrap-around can't happen.
int to0x88(Square sq) { return rankOf(sq) * 16 + fileOf(sq); }
Square from0x88(int sq) { return makeSquare(sq & 7, sq >> 4); }

Bitboard walk(Square start, const int* offsets, int count, bool slide, Bitboard occupied) {
    Bitboard result = 0;
    for (int i = 0; i < count; ++i) {
        int sq = to0x88(start);
        while (true) {
            sq += offsets[i];
            if (sq & 0x88) break;
            result |= squareBit(from0x88(sq));
            if (!slide || (occupied & squareBit(from0x88(sq)))) break;
        }
    }
    return result;
}

constexpr int KNIGHT_0X88[] = {33, 31, 18, 14, -14, -18, -31, -33};
constexpr int KING_0X88[] = {1, -1, 16, -16, 15, 17, -15, -17};
constexpr int ROOK_0X88[] = {1, -1, 16, -16};
constexpr int BISHOP_0X88[] = {15, 17, -15, -17};

Bitboard refKnight(Square sq) { return walk(sq, KNIGHT_0X88, 8, false, 0); }
Bitboard refKing(Square sq) { return walk(sq, KING_0X88, 8, false, 0); }
Bitboard refRook(Square sq, Bitboard occ) { return walk(sq, ROOK_0X88, 4, true, occ); }
Bitboard refBishop(Square sq, Bitboard occ) { return walk(sq, BISHOP_0X88, 4, true, occ); }
Bitboard refPawn(Color color, Square sq) {
    const int offsets[2] = {color == Color::White ? 15 : -15, color == Color::White ? 17 : -17};
    return walk(sq, offsets, 2, false, 0);
}

// Steps one square at a time from a toward b. Only called for aligned squares.
Bitboard refBetween(Square a, Square b) {
    const int df = (fileOf(b) > fileOf(a)) - (fileOf(b) < fileOf(a));
    const int dr = (rankOf(b) > rankOf(a)) - (rankOf(b) < rankOf(a));
    Bitboard result = 0;
    for (int f = fileOf(a) + df, r = rankOf(a) + dr; makeSquare(f, r) != b; f += df, r += dr) {
        result |= squareBit(makeSquare(f, r));
    }
    return result;
}

Bitboard bits(std::initializer_list<Square> squares) {
    Bitboard b = 0;
    for (const Square sq : squares) b |= squareBit(sq);
    return b;
}
} // namespace

// ---------- Leapers ----------

TEST(attacks_leapers_match_reference_on_every_square) {
    for (Square sq = A1; sq <= H8; ++sq) {
        CHECK_EQ(knightAttacks(sq), refKnight(sq));
        CHECK_EQ(kingAttacks(sq), refKing(sq));
        CHECK_EQ(pawnAttacks(Color::White, sq), refPawn(Color::White, sq));
        CHECK_EQ(pawnAttacks(Color::Black, sq), refPawn(Color::Black, sq));
    }
}

TEST(attacks_leapers_known_squares) {
    CHECK_EQ(knightAttacks(A1), bits({B3, C2}));
    CHECK_EQ(knightAttacks(H8), bits({G6, F7}));
    CHECK_EQ(knightAttacks(H4), bits({G2, F3, F5, G6}));
    CHECK_EQ(popCount(knightAttacks(D4)), 8);
    CHECK_EQ(kingAttacks(A1), bits({A2, B1, B2}));
    CHECK_EQ(kingAttacks(H5), bits({H4, H6, G4, G5, G6}));
    CHECK_EQ(popCount(kingAttacks(E4)), 8);
    CHECK_EQ(pawnAttacks(Color::White, E4), bits({D5, F5}));
    CHECK_EQ(pawnAttacks(Color::Black, E4), bits({D3, F3}));
    CHECK_EQ(pawnAttacks(Color::White, A2), bits({B3}));
    CHECK_EQ(pawnAttacks(Color::White, H2), bits({G3}));
    CHECK_EQ(pawnAttacks(Color::Black, H7), bits({G6}));
    CHECK_EQ(pawnAttacks(Color::White, C8), Bitboard{0}); // nothing past the last rank
    CHECK_EQ(pawnAttacks(Color::Black, C1), Bitboard{0});
}

// The classic bug: a shift-based knight on the h-file "attacks" the a- and b-files.
TEST(attacks_never_wrap_around_the_board_edge) {
    const Bitboard leftTwoFiles = fileMask(0) | fileMask(1);
    const Bitboard rightTwoFiles = fileMask(6) | fileMask(7);
    for (int rank = 0; rank < 8; ++rank) {
        const Square h = makeSquare(7, rank);
        const Square a = makeSquare(0, rank);
        CHECK_EQ(knightAttacks(h) & leftTwoFiles, Bitboard{0});
        CHECK_EQ(knightAttacks(a) & rightTwoFiles, Bitboard{0});
        CHECK_EQ(kingAttacks(h) & FILE_A, Bitboard{0});
        CHECK_EQ(kingAttacks(a) & FILE_H, Bitboard{0});
        for (const Color c : {Color::White, Color::Black}) {
            CHECK_EQ(pawnAttacks(c, h) & FILE_A, Bitboard{0});
            CHECK_EQ(pawnAttacks(c, a) & FILE_H, Bitboard{0});
        }
        CHECK_EQ(rookAttacks(h, 0) & ~rankMask(rank) & ~FILE_H, Bitboard{0});
    }
}

// isSquareAttacked works by asking "what would a piece on the target attack?", which only works because these
// relations are symmetric (and mirrored by color for pawns).
TEST(attacks_are_symmetric) {
    for (Square a = A1; a <= H8; ++a) {
        for (Square b = A1; b <= H8; ++b) {
            CHECK_EQ((knightAttacks(a) & squareBit(b)) != 0, (knightAttacks(b) & squareBit(a)) != 0);
            CHECK_EQ((kingAttacks(a) & squareBit(b)) != 0, (kingAttacks(b) & squareBit(a)) != 0);
            CHECK_EQ((pawnAttacks(Color::White, a) & squareBit(b)) != 0,
                     (pawnAttacks(Color::Black, b) & squareBit(a)) != 0);
        }
    }
}

// ---------- Sliders ----------

TEST(attacks_sliders_on_empty_board) {
    for (Square sq = A1; sq <= H8; ++sq) {
        CHECK_EQ(popCount(rookAttacks(sq, 0)), 14);
        CHECK_EQ(rookAttacks(sq, 0), (fileMask(fileOf(sq)) | rankMask(rankOf(sq))) & ~squareBit(sq));
    }
    CHECK_EQ(popCount(bishopAttacks(D4, 0)), 13);
    CHECK_EQ(popCount(bishopAttacks(A1, 0)), 7);
    CHECK_EQ(popCount(queenAttacks(D4, 0)), 27);
    CHECK_EQ(popCount(queenAttacks(A1, 0)), 21);
}

TEST(attacks_sliders_stop_at_and_include_the_first_blocker) {
    // Rook on d4, blockers on d6, d7, b4 and g4.
    const Bitboard occ = bits({D6, D7, B4, G4});
    CHECK_EQ(rookAttacks(D4, occ), bits({D5, D6, D3, D2, D1, C4, B4, E4, F4, G4}));
    // Bishop on c1 with a blocker on e3: b2, a3, d2, e3.
    CHECK_EQ(bishopAttacks(C1, bits({E3, F4})), bits({B2, A3, D2, E3}));
    // The slider's own square doesn't count as a blocker.
    CHECK_EQ(rookAttacks(A1, bits({A1})), rookAttacks(A1, 0));
}

TEST(attacks_sliders_match_reference_with_random_occupancy) {
    std::mt19937_64 rng(808);
    for (int round = 0; round < 3000; ++round) {
        // Mix sparse and dense boards: AND-ing random words thins them out.
        Bitboard occ = rng();
        for (int thin = round % 4; thin > 0; --thin) occ &= rng();
        for (Square sq = A1; sq <= H8; ++sq) {
            CHECK_EQ(rookAttacks(sq, occ), refRook(sq, occ));
            CHECK_EQ(bishopAttacks(sq, occ), refBishop(sq, occ));
            CHECK_EQ(queenAttacks(sq, occ), refRook(sq, occ) | refBishop(sq, occ));
        }
    }
}

TEST(attacks_piece_attacks_dispatches_by_type) {
    const Bitboard occ = bits({E6, B2});
    CHECK_EQ(pieceAttacks(PieceType::Knight, E4, occ), knightAttacks(E4));
    CHECK_EQ(pieceAttacks(PieceType::Bishop, E4, occ), bishopAttacks(E4, occ));
    CHECK_EQ(pieceAttacks(PieceType::Rook, E4, occ), rookAttacks(E4, occ));
    CHECK_EQ(pieceAttacks(PieceType::Queen, E4, occ), queenAttacks(E4, occ));
    CHECK_EQ(pieceAttacks(PieceType::King, E4, occ), kingAttacks(E4));
    CHECK_EQ(pieceAttacks(PieceType::Pawn, E4, occ), Bitboard{0});
}

// ---------- Geometry and bit helpers ----------

TEST(attacks_between_and_line) {
    CHECK_EQ(between(A1, H8), bits({B2, C3, D4, E5, F6, G7}));
    CHECK_EQ(between(H8, A1), between(A1, H8));
    CHECK_EQ(between(E1, E4), bits({E2, E3}));
    CHECK_EQ(between(E1, F1), Bitboard{0}); // neighbors
    CHECK_EQ(between(A1, B3), Bitboard{0}); // not on a shared line
    CHECK_EQ(between(E4, E4), Bitboard{0});
    CHECK_EQ(line(A1, C3), bits({A1, B2, C3, D4, E5, F6, G7, H8}));
    CHECK_EQ(line(C1, C5), fileMask(2));
    CHECK_EQ(line(A1, B3), Bitboard{0});
    for (Square a = A1; a <= H8; ++a) {
        for (Square b = A1; b <= H8; ++b) {
            if (a == b) continue;
            const bool aligned = (refRook(a, 0) | refBishop(a, 0)) & squareBit(b);
            CHECK_EQ(line(a, b) != 0, aligned);
            CHECK_EQ(between(a, b), aligned ? refBetween(a, b) : Bitboard{0});
            if (aligned) CHECK(line(a, b) & squareBit(a) && line(a, b) & squareBit(b));
        }
    }
}

TEST(attacks_bit_scans) {
    CHECK_EQ(lsb(bits({C3, F7})), C3);
    CHECK_EQ(msb(bits({C3, F7})), F7);
    CHECK_EQ(lsb(squareBit(H8)), H8);
    CHECK_EQ(msb(squareBit(A1)), A1);
    Bitboard b = bits({A1, D4, H8});
    CHECK_EQ(popLsb(b), A1);
    CHECK_EQ(popLsb(b), D4);
    CHECK_EQ(popLsb(b), H8);
    CHECK_EQ(b, Bitboard{0});
}
