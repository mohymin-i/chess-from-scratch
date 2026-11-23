// FEN (Forsyth-Edwards Notation) import and export.
//
// A FEN string has six space-separated fields:
//   rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1
//   placement               side  castling  en passant  halfmove clock  fullmove number
// Placement lists ranks 8 down to 1, each from the a-file to the h-file. Letters are pieces and digits are runs
// of empty squares.
#include <charconv>
#include <cstdlib>
#include <string>
#include <vector>

#include "board.h"
#include "movegen.h"

namespace {

[[noreturn]] void reject(const std::string& why) { throw FenError("FEN: " + why); }

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

// Splits on runs of whitespace, so stray spaces in pasted text don't matter.
std::vector<std::string_view> splitFields(std::string_view text) {
    std::vector<std::string_view> fields;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isSpace(text[i])) ++i;
        const std::size_t start = i;
        while (i < text.size() && !isSpace(text[i])) ++i;
        if (i > start) fields.push_back(text.substr(start, i - start));
    }
    return fields;
}

// Digits only: no sign, no spaces, and it has to fit in an int.
int parseCount(std::string_view field, const char* name) {
    int value = 0;
    const char* first = field.data();
    const char* last = field.data() + field.size();
    if (field.empty() || field[0] < '0' || field[0] > '9') {
        reject(std::string(name) + " must be a non-negative whole number, got \"" + std::string(field) + "\"");
    }
    const auto [end, error] = std::from_chars(first, last, value);
    if (error == std::errc::result_out_of_range) reject(std::string(name) + " is too large: " + std::string(field));
    if (error != std::errc() || end != last) {
        reject(std::string(name) + " must be a non-negative whole number, got \"" + std::string(field) + "\"");
    }
    return value;
}

constexpr char CASTLING_LETTERS[] = {'K', 'Q', 'k', 'q'};
constexpr CastlingRight CASTLING_FLAGS[] = {WHITE_KINGSIDE, WHITE_QUEENSIDE, BLACK_KINGSIDE, BLACK_QUEENSIDE};

} // namespace

Board Board::fromFen(std::string_view fen) {
    const std::vector<std::string_view> fields = splitFields(fen);
    if (fields.size() != 4 && fields.size() != 6) {
        reject("expected 6 fields (or 4 without the move counters), got " + std::to_string(fields.size()));
    }

    Board board = Board::empty();

    // ---- 1. Piece placement ----
    const std::string_view placement = fields[0];
    int rank = 7;
    int file = 0;
    bool lastWasDigit = false;
    const auto rankName = [&rank] { return "rank " + std::to_string(rank + 1); };
    for (const char c : placement) {
        if (c == '/') {
            if (file != 8) reject(rankName() + " has " + std::to_string(file) + " squares instead of 8");
            if (rank == 0) reject("more than 8 ranks in the piece placement (or a '/' after rank 1)");
            --rank;
            file = 0;
            lastWasDigit = false;
        } else if (c >= '1' && c <= '8') {
            // "44" adds up to 8, but no correct FEN writer produces it, so it's probably a typo.
            if (lastWasDigit) reject(rankName() + " has two digits in a row");
            file += c - '0';
            if (file > 8) reject(rankName() + " has more than 8 squares");
            lastWasDigit = true;
        } else if (const std::optional<Piece> piece = pieceFromChar(c)) {
            if (file >= 8) reject(rankName() + " has more than 8 squares");
            if (piece->type == PieceType::Pawn && (rank == 0 || rank == 7)) {
                reject("pawn on " + rankName() + "; pawns can't stand on the first or last rank");
            }
            board.placePieceUnchecked(*piece, makeSquare(file, rank));
            ++file;
            lastWasDigit = false;
        } else {
            reject(std::string("unexpected character '") + c + "' in the piece placement");
        }
    }
    if (rank != 0) reject("piece placement has " + std::to_string(8 - rank) + " ranks instead of 8");
    if (file != 8) reject("rank 1 has " + std::to_string(file) + " squares instead of 8");

    Square kings[NUM_COLORS] = {NO_SQUARE, NO_SQUARE};
    for (const Color color : {Color::White, Color::Black}) {
        const Bitboard king = board.pieces(color, PieceType::King);
        const char* name = color == Color::White ? "white" : "black";
        if (popCount(king) != 1) {
            reject(std::string(name) + " has " + std::to_string(popCount(king)) + " kings instead of 1");
        }
        kings[idx(color)] = static_cast<Square>(std::countr_zero(king));
    }
    const int fileGap = std::abs(fileOf(kings[0]) - fileOf(kings[1]));
    const int rankGap = std::abs(rankOf(kings[0]) - rankOf(kings[1]));
    if (fileGap <= 1 && rankGap <= 1) reject("the kings are on neighboring squares");

    // ---- 2. Side to move ----
    if (fields[1] == "w") {
        board.sideToMove_ = Color::White;
    } else if (fields[1] == "b") {
        board.sideToMove_ = Color::Black;
    } else {
        reject("side to move must be 'w' or 'b', got \"" + std::string(fields[1]) + "\"");
    }

    // ---- 3. Castling rights ----
    const std::string_view castling = fields[2];
    std::uint8_t rights = 0;
    if (castling != "-") {
        for (const char c : castling) {
            int i = 0;
            while (i < 4 && CASTLING_LETTERS[i] != c) ++i;
            if (i == 4) reject(std::string("unexpected character '") + c + "' in castling rights");
            if (rights & CASTLING_FLAGS[i]) reject(std::string("castling right '") + c + "' appears twice");
            rights |= CASTLING_FLAGS[i];
        }
    }
    // A castling right only makes sense if the king and that rook haven't left their home squares. Move generation
    // will trust this field, so a stale right here would later turn into an illegal castling move.
    struct CastlingHome {
        CastlingRight right;
        char letter;
        Color color;
        Square king;
        Square rook;
    };
    constexpr CastlingHome HOMES[] = {
        {WHITE_KINGSIDE, 'K', Color::White, E1, H1},
        {WHITE_QUEENSIDE, 'Q', Color::White, E1, A1},
        {BLACK_KINGSIDE, 'k', Color::Black, E8, H8},
        {BLACK_QUEENSIDE, 'q', Color::Black, E8, A8},
    };
    for (const CastlingHome& home : HOMES) {
        if (!(rights & home.right)) continue;
        if (board.mailbox_[home.king] != Piece{PieceType::King, home.color}) {
            reject(std::string("castling right '") + home.letter + "' but the king isn't on " +
                   squareToString(home.king));
        }
        if (board.mailbox_[home.rook] != Piece{PieceType::Rook, home.color}) {
            reject(std::string("castling right '") + home.letter + "' but there's no rook on " +
                   squareToString(home.rook));
        }
    }
    board.castlingRights_ = rights;

    // ---- 4. En passant square ----
    const std::string_view enPassant = fields[3];
    if (enPassant != "-") {
        const Square sq = squareFromString(enPassant);
        if (sq == NO_SQUARE) {
            reject("en passant square must be '-' or a square like e3, got \"" + std::string(enPassant) + "\"");
        }
        // The side that just moved pushed a pawn two squares, skipping over sq.
        const Color mover = opposite(board.sideToMove_);
        const int expectedRank = mover == Color::White ? 2 : 5;
        if (rankOf(sq) != expectedRank) {
            reject("en passant square " + std::string(enPassant) + " must be on rank " +
                   std::to_string(expectedRank + 1) + " when " +
                   (board.sideToMove_ == Color::White ? "white" : "black") + " is to move");
        }
        const int forward = mover == Color::White ? 8 : -8;
        const auto pawnSquare = static_cast<Square>(sq + forward);
        const auto startSquare = static_cast<Square>(sq - forward);
        if (board.mailbox_[pawnSquare] != Piece{PieceType::Pawn, mover}) {
            reject("en passant square " + std::string(enPassant) + " but no pawn on " + squareToString(pawnSquare) +
                   " that could have just moved past it");
        }
        if (board.mailbox_[sq] || board.mailbox_[startSquare]) {
            reject("en passant square " + std::string(enPassant) + " but " + squareToString(sq) + " or " +
                   squareToString(startSquare) + " is occupied, so no pawn just moved past it");
        }
        board.enPassant_ = sq;
    }

    // ---- 5 and 6. Move counters (optional) ----
    if (fields.size() == 6) {
        board.halfmoveClock_ = parseCount(fields[4], "halfmove clock");
        board.fullmoveNumber_ = parseCount(fields[5], "fullmove number");
        if (board.fullmoveNumber_ < 1) reject("fullmove number must be at least 1");
    }

    board.hash_ = board.computeHash(); // the fields above were set directly, bypassing the hash updates

    // The side that just moved can't have left its own king in check; the side to move could capture it.
    if (moverLeftInCheck(board)) {
        reject(std::string(board.sideToMove_ == Color::White ? "black" : "white") +
               " is in check but it's not their move");
    }
    return board;
}

std::string Board::toFen() const {
    std::string out;
    out.reserve(90);
    for (int rank = 7; rank >= 0; --rank) {
        int emptyRun = 0;
        for (int file = 0; file < 8; ++file) {
            const std::optional<Piece>& piece = mailbox_[makeSquare(file, rank)];
            if (!piece) {
                ++emptyRun;
                continue;
            }
            if (emptyRun > 0) out += static_cast<char>('0' + emptyRun);
            emptyRun = 0;
            out += pieceToChar(*piece);
        }
        if (emptyRun > 0) out += static_cast<char>('0' + emptyRun);
        if (rank > 0) out += '/';
    }

    out += sideToMove_ == Color::White ? " w " : " b ";

    if (castlingRights_ == 0) {
        out += '-';
    } else {
        for (int i = 0; i < 4; ++i) {
            if (castlingRights_ & CASTLING_FLAGS[i]) out += CASTLING_LETTERS[i];
        }
    }

    out += ' ';
    out += enPassant_ == NO_SQUARE ? "-" : squareToString(enPassant_);
    out += ' ' + std::to_string(halfmoveClock_) + ' ' + std::to_string(fullmoveNumber_);
    return out;
}
