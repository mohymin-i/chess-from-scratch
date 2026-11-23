// Zobrist hashing: a 64-bit fingerprint of a position.
//
// Every (piece, square) pair, every castling-rights combination, every en passant file and "black to move" gets a
// random 64-bit key. A position's hash is the XOR of the keys for everything in it. XOR is its own inverse, so a
// move updates the hash by XOR-ing out what left and XOR-ing in what arrived, without rescanning the board.
//
// The keys come from our own seeded generator (SplitMix64) at compile time, so hashes are the same on every run
// and every machine, which keeps tests and engine behavior reproducible.
#ifndef CHESS_FROM_SCRATCH_ZOBRIST_H
#define CHESS_FROM_SCRATCH_ZOBRIST_H

#include <cstdint>

namespace zobrist {

struct Keys {
    std::uint64_t piece[2][6][64]; // [color][piece type][square]
    std::uint64_t castling[16];    // indexed by the whole castling-rights byte
    std::uint64_t enPassantFile[8];
    std::uint64_t blackToMove;
};

constexpr std::uint64_t splitMix64(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

constexpr Keys makeKeys() {
    Keys keys{};
    std::uint64_t state = 0x43484553534B4559ULL; // "CHESSKEY"
    for (auto& color : keys.piece)
        for (auto& type : color)
            for (auto& key : type) key = splitMix64(state);
    // No castling rights hashes to 0 so an empty board with white to move has hash 0.
    for (int i = 1; i < 16; ++i) keys.castling[i] = splitMix64(state);
    for (auto& key : keys.enPassantFile) key = splitMix64(state);
    keys.blackToMove = splitMix64(state);
    return keys;
}

inline constexpr Keys KEYS = makeKeys();

} // namespace zobrist

#endif // CHESS_FROM_SCRATCH_ZOBRIST_H
