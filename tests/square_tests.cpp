#include <climits>
#include <stdexcept>
#include <string>
#include <type_traits>

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
    for (Square sq = A1; sq <= H8; ++sq) {
        CHECK_EQ(squareFromString(squareToString(sq)), sq);
    }
}

TEST(square_bits_are_unique_single_bits) {
    Bitboard all = 0;
    for (Square sq = A1; sq <= H8; ++sq) {
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
    CHECK_THROWS(squareToString(static_cast<Square>(64)), std::out_of_range);
    CHECK_THROWS(squareToString(static_cast<Square>(INT_MAX)), std::out_of_range);
}

TEST(square_pop_count) {
    CHECK_EQ(popCount(0), 0);
    CHECK_EQ(popCount(~Bitboard{0}), 64);
    CHECK_EQ(popCount(0x8100000000000081ULL), 4);
}

// Compile-time checks: these fail the build, not a test, if Square loses its type safety.
static_assert(!std::is_convertible_v<int, Square>, "a plain int must not silently become a Square");
static_assert(std::is_convertible_v<Square, int>, "a Square must still work as an index and bit position");
static_assert(sizeof(Square) == sizeof(int));

TEST(square_named_constants_match_their_names) {
    CHECK_EQ(A1, squareFromString("a1"));
    CHECK_EQ(H1, squareFromString("h1"));
    CHECK_EQ(E4, squareFromString("e4"));
    CHECK_EQ(D5, squareFromString("d5"));
    CHECK_EQ(A8, squareFromString("a8"));
    CHECK_EQ(H8, squareFromString("h8"));
    // Every constant from A1 to H8, in order, names the square its string says.
    const char* files = "abcdefgh";
    for (Square sq = A1; sq <= H8; ++sq) {
        const std::string expected{files[sq % 8], static_cast<char>('1' + sq / 8)};
        CHECK_EQ(squareToString(sq), expected);
    }
}

TEST(square_increment_walks_the_board_in_order) {
    int count = 0;
    for (Square sq = A1; sq <= H8; ++sq) {
        CHECK_EQ(static_cast<int>(sq), count);
        ++count;
    }
    CHECK_EQ(count, NUM_SQUARES);
}
