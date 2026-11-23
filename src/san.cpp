#include "san.h"

#include "movegen.h"

namespace {

constexpr char PIECE_LETTERS[] = "PNBRQK"; // indexed by PieceType

std::optional<PieceType> pieceFromLetter(char c) {
    for (int i = 1; i < NUM_PIECE_TYPES; ++i) {
        if (PIECE_LETTERS[i] == c) return static_cast<PieceType>(i);
    }
    return std::nullopt;
}

// Promotion letters are accepted in either case ("e8=q"); there is no ambiguity after the destination square.
char upper(char c) { return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c; }

bool isFile(char c) { return c >= 'a' && c <= 'h'; }
bool isRank(char c) { return c >= '1' && c <= '8'; }

} // namespace

std::string toSan(const Board& board, Move move) {
    std::string out;
    const PieceType type = board.pieceAt(move.from())->type;

    if (move.isCastle()) {
        out = move.flag() == MoveFlag::KingCastle ? "O-O" : "O-O-O";
    } else {
        if (type == PieceType::Pawn) {
            if (move.isCapture()) out += static_cast<char>('a' + fileOf(move.from()));
        } else {
            out += PIECE_LETTERS[static_cast<int>(type)];
            // Other pieces of the same type that could also go to this square.
            MoveList legal;
            generateLegal(board, legal);
            bool ambiguous = false;
            bool sameFile = false;
            bool sameRank = false;
            for (const Move other : legal) {
                if (other == move || other.to() != move.to() || other.from() == move.from()) continue;
                if (board.pieceAt(other.from())->type != type) continue;
                ambiguous = true;
                if (fileOf(other.from()) == fileOf(move.from())) sameFile = true;
                if (rankOf(other.from()) == rankOf(move.from())) sameRank = true;
            }
            if (ambiguous) {
                if (!sameFile) {
                    out += static_cast<char>('a' + fileOf(move.from()));
                } else if (!sameRank) {
                    out += static_cast<char>('1' + rankOf(move.from()));
                } else {
                    out += squareToString(move.from());
                }
            }
        }
        if (move.isCapture()) out += 'x';
        out += squareToString(move.to());
        if (move.isPromotion()) {
            out += '=';
            out += PIECE_LETTERS[static_cast<int>(move.promotionType())];
        }
    }

    Board after = board;
    after.makeMove(move);
    if (inCheck(after)) {
        MoveList replies;
        generateLegal(after, replies);
        out += replies.empty() ? '#' : '+';
    }
    return out;
}

std::optional<Move> moveFromSan(const Board& board, std::string_view san) {
    std::string s(san);
    // Drop "e.p." and any trailing check, mate and annotation marks.
    if (const std::size_t ep = s.find("e.p."); ep != std::string::npos) s.erase(ep);
    while (!s.empty() && (s.back() == '+' || s.back() == '#' || s.back() == '!' || s.back() == '?' ||
                          s.back() == ' ')) {
        s.pop_back();
    }
    if (s.empty()) return std::nullopt;

    MoveList legal;
    generateLegal(board, legal);

    if (s == "O-O" || s == "0-0" || s == "O-O-O" || s == "0-0-0") {
        const MoveFlag want = s.size() == 3 ? MoveFlag::KingCastle : MoveFlag::QueenCastle;
        for (const Move m : legal)
            if (m.flag() == want) return m;
        return std::nullopt;
    }

    PieceType type = PieceType::Pawn;
    std::size_t i = 0;
    if (const std::optional<PieceType> p = pieceFromLetter(s[0])) {
        type = *p;
        i = 1;
    }

    // Promotion: "=Q", or just "Q" right after the destination rank.
    std::optional<PieceType> promotion;
    if (const std::size_t eq = s.find('='); eq != std::string::npos) {
        if (eq + 2 != s.size()) return std::nullopt;
        promotion = pieceFromLetter(upper(s[eq + 1]));
        if (!promotion || *promotion == PieceType::King) return std::nullopt;
        s.erase(eq);
    } else if (type == PieceType::Pawn && s.size() >= 3 && isRank(s[s.size() - 2])) {
        if (const std::optional<PieceType> p = pieceFromLetter(upper(s.back())); p && *p != PieceType::King) {
            promotion = p;
            s.pop_back();
        }
    }

    // The destination is the last two characters; everything between the piece letter and it is
    // disambiguation, an optional "x" and an optional "-".
    if (s.size() < i + 2) return std::nullopt;
    const Square to = squareFromString(std::string_view(s).substr(s.size() - 2));
    if (to == NO_SQUARE) return std::nullopt;
    int fromFile = -1;
    int fromRank = -1;
    for (std::size_t j = i; j < s.size() - 2; ++j) {
        const char c = s[j];
        if (isFile(c) && fromFile < 0) {
            fromFile = c - 'a';
        } else if (isRank(c) && fromRank < 0) {
            fromRank = c - '1';
        } else if (c != 'x' && c != '-' && c != ':') {
            return std::nullopt;
        }
    }

    std::optional<Move> found;
    for (const Move m : legal) {
        if (m.to() != to) continue; // "Kg1" for castling is accepted too: it's a king move to g1
        if (board.pieceAt(m.from())->type != type) continue;
        if (fromFile >= 0 && fileOf(m.from()) != fromFile) continue;
        if (fromRank >= 0 && rankOf(m.from()) != fromRank) continue;
        if (m.isPromotion() != promotion.has_value()) continue;
        if (promotion && m.promotionType() != *promotion) continue;
        if (found) return std::nullopt; // ambiguous
        found = m;
    }
    return found;
}
