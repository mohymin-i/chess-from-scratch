#include "pgn.h"

#include <cctype>
#include <optional>

#include "san.h"

namespace {

constexpr const char* ROSTER[] = {"Event", "Site", "Date", "Round", "White", "Black", "Result"};
constexpr const char* ROSTER_DEFAULT[] = {"?", "?", "????.??.??", "?", "?", "?", "*"};

std::string escape(const std::string& value) {
    std::string out;
    for (const char c : value) {
        if (c == '\\' || c == '"') out += '\\';
        out += c;
    }
    return out;
}

// Elapsed move time in the "[%emt H:MM:SS]" comment format, with milliseconds only when needed.
std::string formatEmt(int ms) {
    const int h = ms / 3'600'000;
    const int m = ms / 60'000 % 60;
    const int sec = ms / 1000 % 60;
    const int frac = ms % 1000;
    std::string out = std::to_string(h) + ":" + (m < 10 ? "0" : "") + std::to_string(m) + ":" + (sec < 10 ? "0" : "") +
                      std::to_string(sec);
    if (frac) {
        std::string f = std::to_string(frac);
        f.insert(0, 3 - f.size(), '0');
        while (f.back() == '0') f.pop_back();
        out += "." + f;
    }
    return out;
}

// Parses the H:MM:SS(.fff) after "[%emt " in a comment. Returns nullopt if there isn't a well-formed one.
std::optional<int> parseEmt(std::string_view comment) {
    const std::size_t at = comment.find("[%emt ");
    if (at == std::string_view::npos) return std::nullopt;
    std::string_view rest = comment.substr(at + 6);
    const std::size_t end = rest.find(']');
    if (end == std::string_view::npos) return std::nullopt;
    rest = rest.substr(0, end);
    long long parts[3] = {0, 0, 0};
    int part = 0;
    int fracMs = 0;
    int fracDigits = 0;
    bool inFraction = false;
    bool sawDigit = false;
    for (const char c : rest) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            sawDigit = true;
            if (inFraction) {
                if (fracDigits < 3) {
                    fracMs = fracMs * 10 + (c - '0');
                    ++fracDigits;
                }
            } else {
                parts[part] = parts[part] * 10 + (c - '0');
                if (parts[part] > 1'000'000) return std::nullopt;
            }
        } else if (c == ':' && !inFraction && part < 2) {
            ++part;
        } else if (c == '.' && !inFraction && part == 2) {
            inFraction = true;
        } else {
            return std::nullopt;
        }
    }
    if (!sawDigit || part != 2) return std::nullopt;
    while (fracDigits < 3) {
        fracMs *= 10;
        ++fracDigits;
    }
    const long long total = ((parts[0] * 60 + parts[1]) * 60 + parts[2]) * 1000 + fracMs;
    if (total > 1'000'000'000) return std::nullopt;
    return static_cast<int>(total);
}

bool isResult(std::string_view token) {
    return token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::vector<PgnGame> run() {
        while (true) {
            skipSpace();
            if (pos_ >= text_.size()) break;
            const char c = text_[pos_];
            if (c == '[') {
                if (hasMoves_) finishGame(""); // a tag after moves starts the next game
                readTag();
            } else if (c == '{') {
                const std::string_view body = readComment();
                if (hasMoves_) {
                    if (const std::optional<int> ms = parseEmt(body)) {
                        current_.game.setMoveTime(current_.game.moves().size() - 1, ms);
                    }
                }
            } else if (c == ';' || (c == '%' && atLineStart())) {
                skipLine(); // a ';' comment, or a '%' escape line: both run to the end of the line
            } else if (c == '(') {
                skipVariation();
            } else if (c == ')') {
                fail("')' without a matching '('");
            } else if (c == '}') {
                fail("'}' without a matching '{'");
            } else if (c == ']') {
                fail("']' outside a tag");
            } else if (c == '$') {
                ++pos_;
                while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) ++pos_;
            } else {
                readToken();
            }
        }
        if (hasMoves_ || !current_.tags.empty()) finishGame("");
        return std::move(games_);
    }

private:
    [[noreturn]] void fail(const std::string& why) const {
        throw PgnError("PGN game " + std::to_string(games_.size() + 1) + ", line " + std::to_string(line_) + ": " +
                       why);
    }

    void advance() {
        if (text_[pos_] == '\n') ++line_;
        ++pos_;
    }

    bool atLineStart() const { return pos_ == 0 || text_[pos_ - 1] == '\n'; }

    void skipSpace() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) advance();
    }

    void skipLine() {
        while (pos_ < text_.size() && text_[pos_] != '\n') ++pos_;
    }

    // Reads a {...} comment and returns what's inside the braces.
    std::string_view readComment() {
        const int startLine = line_;
        const std::size_t start = ++pos_;
        while (pos_ < text_.size() && text_[pos_] != '}') advance();
        if (pos_ >= text_.size()) {
            line_ = startLine;
            fail("comment '{' is never closed");
        }
        return text_.substr(start, pos_++ - start);
    }

    void skipVariation() {
        const int startLine = line_;
        int depth = 0;
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == '{') {
                readComment();
                continue;
            }
            if (c == ';') {
                skipLine();
                continue;
            }
            if (c == '(') ++depth;
            if (c == ')' && --depth == 0) {
                ++pos_;
                return;
            }
            advance();
        }
        line_ = startLine;
        fail("variation '(' is never closed");
    }

    void readTag() {
        ++pos_; // '['
        skipSpace();
        std::string name;
        while (pos_ < text_.size() && (std::isalnum(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '_')) {
            name += text_[pos_++];
        }
        if (name.empty()) fail("tag without a name");
        skipSpace();
        if (pos_ >= text_.size() || text_[pos_] != '"') fail("tag " + name + " has no quoted value");
        ++pos_;
        std::string value;
        while (true) {
            if (pos_ >= text_.size() || text_[pos_] == '\n') fail("tag " + name + " value is never closed");
            const char c = text_[pos_++];
            if (c == '"') break;
            if (c == '\\' && pos_ < text_.size()) {
                value += text_[pos_++];
            } else {
                value += c;
            }
        }
        skipSpace();
        if (pos_ >= text_.size() || text_[pos_] != ']') fail("tag " + name + " is missing ']'");
        ++pos_;
        current_.tags.emplace_back(name, value);
    }

    void readToken() {
        const std::size_t start = pos_;
        while (pos_ < text_.size() && !std::isspace(static_cast<unsigned char>(text_[pos_])) &&
               std::string_view("{}()[];$]").find(text_[pos_]) == std::string_view::npos) {
            ++pos_;
        }
        if (pos_ == start) fail(std::string("unexpected '") + text_[pos_] + "'"); // never loop on a stray character
        std::string_view token = text_.substr(start, pos_ - start);
        if (isResult(token)) {
            finishGame(std::string(token));
            return;
        }
        // Move numbers: "12." and "12..." on their own, or glued to the move ("12.Nf3").
        std::size_t digits = 0;
        while (digits < token.size() && std::isdigit(static_cast<unsigned char>(token[digits]))) ++digits;
        if (digits > 0 && digits < token.size() && token[digits] == '.') {
            token.remove_prefix(digits);
            while (!token.empty() && token.front() == '.') token.remove_prefix(1);
        } else if (digits == token.size()) {
            return; // a bare number, as some exporters write
        }
        while (!token.empty() && token.front() == '.') token.remove_prefix(1); // "..." on its own
        if (token.empty()) return;
        if (token.find_first_not_of("!?") == std::string_view::npos) return; // annotation like "!?" on its own

        Game& game = startedGame();
        if (game.status().isOver()) fail("move " + std::string(token) + " after the game is already over");
        const std::optional<Move> move = moveFromSan(game.board(), token);
        if (!move) {
            const Board& b = game.board();
            fail("\"" + std::string(token) + "\" is not a legal move at move " + std::to_string(b.fullmoveNumber()) +
                 (b.sideToMove() == Color::White ? " for white" : " for black"));
        }
        game.play(*move);
        hasMoves_ = true;
    }

    Game& startedGame() {
        if (!started_) {
            Board start;
            for (const auto& [name, value] : current_.tags) {
                if (name == "FEN") {
                    try {
                        start = Board::fromFen(value);
                    } catch (const FenError& e) {
                        fail(std::string("bad FEN tag: ") + e.what());
                    }
                }
            }
            current_.game = Game(start);
            started_ = true;
        }
        return current_.game;
    }

    void finishGame(const std::string& token) {
        Game& game = startedGame();
        std::string result = token;
        if (result.empty()) result = current_.tag("Result");
        if (!isResult(result)) result = "*";
        current_.result = result;
        if (!game.status().isOver()) {
            if (result == "1-0") game.resign(Color::Black);
            if (result == "0-1") game.resign(Color::White);
            if (result == "1/2-1/2") game.agreeDraw();
        }
        games_.push_back(std::move(current_));
        current_ = PgnGame{};
        started_ = false;
        hasMoves_ = false;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    int line_ = 1;
    std::vector<PgnGame> games_;
    PgnGame current_;
    bool started_ = false;
    bool hasMoves_ = false;
};

} // namespace

std::vector<PgnGame> parsePgn(std::string_view text) { return Parser(text).run(); }

std::string PgnGame::tag(std::string_view name) const {
    for (const auto& [n, v] : tags)
        if (n == name) return v;
    return "";
}

std::string resultToken(const GameStatus& status) {
    switch (status.result) {
        case GameResult::WhiteWins: return "1-0";
        case GameResult::BlackWins: return "0-1";
        case GameResult::Draw: return "1/2-1/2";
        case GameResult::Ongoing: return "*";
    }
    return "*";
}

std::string toPgn(const Game& game, const PgnTags& extra) {
    PgnTags tags;
    for (int i = 0; i < 7; ++i) tags.emplace_back(ROSTER[i], ROSTER_DEFAULT[i]);
    tags[6].second = resultToken(game.status());
    if (!(game.startingBoard() == Board())) {
        tags.emplace_back("SetUp", "1");
        tags.emplace_back("FEN", game.startingBoard().toFen());
    }
    for (const auto& [name, value] : extra) {
        bool replaced = false;
        for (auto& [n, v] : tags) {
            if (n == name) {
                v = value;
                replaced = true;
            }
        }
        if (!replaced) tags.emplace_back(name, value);
    }

    std::string out;
    for (const auto& [name, value] : tags) out += "[" + name + " \"" + escape(value) + "\"]\n";
    out += '\n';

    std::vector<std::string> words;
    Board board = game.startingBoard();
    for (std::size_t i = 0; i < game.moves().size(); ++i) {
        const Move m = game.moves()[i];
        if (board.sideToMove() == Color::White) {
            words.push_back(std::to_string(board.fullmoveNumber()) + ".");
        } else if (i == 0) {
            words.push_back(std::to_string(board.fullmoveNumber()) + "...");
        }
        words.push_back(toSan(board, m));
        if (const std::optional<int> ms = game.moveTimes()[i]) words.push_back("{[%emt " + formatEmt(*ms) + "]}");
        board.makeMove(m);
    }
    words.push_back(tags[6].second); // the result, even if a caller overrode it

    std::size_t lineLength = 0;
    for (const std::string& w : words) {
        if (lineLength > 0 && lineLength + 1 + w.size() > 80) {
            out += '\n';
            lineLength = 0;
        } else if (lineLength > 0) {
            out += ' ';
            ++lineLength;
        }
        out += w;
        lineLength += w.size();
    }
    out += '\n';
    return out;
}
