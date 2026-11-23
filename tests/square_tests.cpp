#include <climits>
#include <stdexcept>

#include "board.h"
#include "test_framework.h"

TEST(square_corners_use_little_endian_rank_file_mapping) {
    CHECK_EQ(squareFromString("a1"), 0);
    CHECK_EQ(squareFromString("h1"), 7);
    CHECK_EQ(squareFromString("a2"), 8);
    CHECK_EQ(squareFromString("e4"), 28);
    CHECK_EQ(squareFromString("a8"), 56);
    CHECK_EQ(squareFromString("h8"), 63);
}

TEST(square_make_square_round_trips_through_file_and_rank) {
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            const Square sq = makeSquare(file, rank);
            CHECK(isValidSquare(sq));
            CHECK_EQ(fileOf(sq), file);
            CHECK_EQ(rankOf(sq), rank);
        }
    }
}

TEST(square_every_name_round_trips) {
    for (Square sq = 0; sq < NUM_SQUARES; ++sq) {
        CHECK_EQ(squareFromString(squareToString(sq)), sq);
    }
}

TEST(square_bits_are_unique_single_bits) {
    Bitboard all = 0;
    for (Square sq = 0; sq < NUM_SQUARES; ++sq) {
        CHECK_EQ(popCount(squareBit(sq)), 1);
        CHECK((all & squareBit(sq)) == 0);
        all |= squareBit(sq);
    }
    CHECK_EQ(all, ~Bitboard{0});
}

TEST(square_malformed_names_are_rejected) {
    for (const char* bad : {"", "e", "e44", "i1", "e0", "e9", "E4", "4e", " e4", "e4 ", "`1", "a:"}) {
        CHECK_EQ(squareFromString(bad), NO_SQUARE);
    }
}

TEST(square_validity_bounds) {
    CHECK(!isValidSquare(INT_MIN));
    CHECK(!isValidSquare(-1));
    CHECK(isValidSquare(0));
    CHECK(isValidSquare(63));
    CHECK(!isValidSquare(64));
    CHECK(!isValidSquare(INT_MAX));
}

TEST(square_to_string_rejects_invalid_squares) {
    CHECK_THROWS(squareToString(NO_SQUARE), std::out_of_range);
    CHECK_THROWS(squareToString(64), std::out_of_range);
    CHECK_THROWS(squareToString(INT_MAX), std::out_of_range);
}

TEST(square_pop_count) {
    CHECK_EQ(popCount(0), 0);
    CHECK_EQ(popCount(~Bitboard{0}), 64);
    CHECK_EQ(popCount(0x8100000000000081ULL), 4);
}
