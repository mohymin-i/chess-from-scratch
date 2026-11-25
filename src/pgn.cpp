#include "pgn.h"

#include <algorithm>
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
        // A tag has to fit on one line, so a line break (from a pasted name, say) becomes a space.
        out += static_cast<unsigned char>(c) < 0x20 ? ' ' : c;
    }
    return out;
}

// The characters a tag name may use after the first, which must be a letter, digit or underscore (PGN 8.1.1).
bool isTagNameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '+' || c == '#' || c == '=' || c == ':' ||
           c == '-';
}

// A tag name readers accept: anything else becomes '_' ("Bot level" -> "Bot_level"). Empty if nothing is left.
std::string tagName(const std::string& name) {
    std::string out;
    for (const char c : name) {
        const bool first = out.empty();
        const bool ok = first ? std::isalnum(static_cast<unsigned char>(c)) || c == '_' : isTagNameChar(c);
        out += ok ? c : '_';
    }
    return out;
}

// The value of an embedded command like "[%clk 0:04:58]" inside a comment, or nullopt if it isn't there. Any
// whitespace (including a line break from wrapping) may separate the name from the value.
std::optional<std::string_view> commandValue(std::string_view comment, std::string_view name) {
    const auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    const std::string key = "[%" + std::string(name);
    for (std::size_t at = comment.find(key); at != std::string_view::npos; at = comment.find(key, at + 1)) {
        std::string_view rest = comment.substr(at + key.size());
        if (rest.empty() || !isSpace(rest.front())) continue; // "[%clkx" is a different command
        const std::size_t end = rest.find(']');
        if (end == std::string_view::npos) return std::nullopt;
        rest = rest.substr(0, end);
        while (!rest.empty() && isSpace(rest.front())) rest.remove_prefix(1);
        while (!rest.empty() && isSpace(rest.back())) rest.remove_suffix(1);
        return rest;
    }
    return std::nullopt;
}

bool isResult(std::string_view token) {
    return token == "1-0" || token == "0-1" || token == "1/2-1/2" || token == "*";
}

// The PGN standard writes "time forfeit", Lichess "Time forfeit", Chess.com "<player> won on time" and
// "Game drawn by timeout vs insufficient material".
bool endedOnTime(std::string termination) {
    for (char& c : termination) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return termination.find("time forfeit") != std::string::npos || termination.find("on time") != std::string::npos ||
           termination.find("timeout") != std::string::npos;
}

// Records a result the moves alone don't explain: a resignation, an agreed draw or a lost or drawn flag.
void recordResult(Game& game, const std::string& result, bool onTime) {
    const Board& board = game.board();
    if (result == "1-0" || result == "0-1") {
        const Color loser = result == "1-0" ? Color::Black : Color::White;
        // A flag against a side that can't mate is a draw, which would contradict the file's result.
        if (onTime && hasMatingMaterial(board, opposite(loser))) {
            game.timeout(loser);
        } else {
            game.resign(loser);
        }
    } else if (result == "1/2-1/2") {
        // A drawn flag: the opponent of whoever ran out couldn't mate. That's usually the side to move's clock.
        const Color toMove = board.sideToMove();
        if (onTime && !hasMatingMaterial(board, opposite(toMove))) {
            game.timeout(toMove);
        } else if (onTime && !hasMatingMaterial(board, toMove)) {
            game.timeout(opposite(toMove));
        } else {
            game.agreeDraw();
        }
    }
}

// Fills in missing think times from [%clk] values: what a player's clock lost on a move, plus the increment.
void deriveTimesFromClocks(PgnGame& g) {
    const std::size_t n = g.game.moves().size();
    if (g.clocks.size() != n) return;
    const int increment = g.timeControl ? g.timeControl->incrementMs : 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (g.game.moveTimes()[i] || !g.clocks[i]) continue;
        std::optional<int> before;
        if (i >= 2) {
            before = g.clocks[i - 2];
        } else if (g.timeControl) {
            before = g.timeControl->baseMs; // each side's first move starts from the full base time
        }
        if (!before) continue;
        const int spent = *before - *g.clocks[i] + increment;
        if (spent >= 0) g.game.setMoveTime(i, spent);
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::vector<PgnGame> run() {
        if (text_.starts_with("\xEF\xBB\xBF")) pos_ = 3; // a UTF-8 byte order mark, as some Windows tools write
        while (true) {
            skipSpace();
            if (pos_ >= text_.size()) break;
            const char c = text_[pos_];
            if (c == '[') {
                if (hasMoves_) finishGame(""); // a tag after moves starts the next game
                readTag();
            } else if (c == '{') {
                const std::string_view body = readComment();
                if (hasMoves_) applyComment(body);
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

    // [%emt] and [%clk] in a comment belong to the move just played.
    void applyComment(std::string_view body) {
        const std::size_t last = current_.game.moves().size() - 1;
        if (const auto emt = commandValue(body, "emt")) {
            if (const std::optional<int> ms = parseClock(*emt)) current_.game.setMoveTime(last, ms);
        }
        if (const auto clk = commandValue(body, "clk")) {
            if (const std::optional<int> ms = parseClock(*clk)) current_.clocks[last] = ms;
        }
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
        while (pos_ < text_.size() && isTagNameChar(text_[pos_])) name += text_[pos_++];
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
        // "exf6 e.p." written with a space, and evaluation symbols written out ("+-", "-+", "=", "+/-"). A lone "--"
        // is left alone: it's a null move, which can't be skipped without getting the side to move wrong.
        if (token == "e.p." || token == "ep") return;
        if (token != "--" && token.find_first_not_of("+-=/") == std::string_view::npos) return;

        Game& game = startedGame();
        if (game.status().isOver()) fail("move " + std::string(token) + " after the game is already over");
        const std::optional<Move> move = moveFromSan(game.board(), token);
        if (!move) {
            const Board& b = game.board();
            fail("\"" + std::string(token) + "\" is not a legal move at move " + std::to_string(b.fullmoveNumber()) +
                 (b.sideToMove() == Color::White ? " for white" : " for black"));
        }
        game.play(*move);
        current_.clocks.emplace_back();
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
        current_.timeControl = TimeControl::fromPgnTag(current_.tag("TimeControl"));
        deriveTimesFromClocks(current_);
        if (!game.status().isOver()) recordResult(game, result, endedOnTime(current_.tag("Termination")));
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

// SAN for a line of moves starting at `board`, with move numbers: "12... Nf6 13. Bxc6 dxc6". Stops at the first
// move that isn't legal, so a stale line can't produce a broken file.
std::string lineToSan(Board board, const std::vector<Move>& line) {
    std::string out;
    bool first = true;
    for (const Move m : line) {
        MoveList legal;
        generateLegal(board, legal);
        if (!legal.contains(m)) break;
        if (!out.empty()) out += ' ';
        if (board.sideToMove() == Color::White) {
            out += std::to_string(board.fullmoveNumber()) + ". ";
        } else if (first) {
            out += std::to_string(board.fullmoveNumber()) + "... ";
        }
        out += toSan(board, m);
        board.makeMove(m);
        first = false;
    }
    return out;
}

} // namespace

std::string TimeControl::toPgnTag() const {
    const std::string base = std::to_string(std::max(0, baseMs) / 1000);
    return incrementMs > 0 ? base + "+" + std::to_string(incrementMs / 1000) : base;
}

std::optional<TimeControl> TimeControl::fromPgnTag(std::string_view tag) {
    const auto number = [](std::string_view s) -> std::optional<int> {
        if (s.empty() || s.size() > 7) return std::nullopt;
        int v = 0;
        for (const char c : s) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
            v = v * 10 + (c - '0');
        }
        return v;
    };
    const std::size_t plus = tag.find('+');
    const std::optional<int> base = number(tag.substr(0, plus));
    if (!base || *base == 0) return std::nullopt;
    TimeControl tc{*base * 1000, 0};
    if (plus != std::string_view::npos) {
        const std::optional<int> inc = number(tag.substr(plus + 1));
        if (!inc) return std::nullopt;
        tc.incrementMs = *inc * 1000;
    }
    return tc;
}

std::string formatClock(int ms) {
    ms = std::max(0, ms);
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

std::optional<int> parseClock(std::string_view text) {
    long long parts[3] = {0, 0, 0};
    int part = 0;
    int fracMs = 0;
    int fracDigits = 0;
    bool inFraction = false;
    bool digitInPart = false;
    for (const char c : text) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digitInPart = true;
            if (inFraction) {
                if (fracDigits < 3) {
                    fracMs = fracMs * 10 + (c - '0');
                    ++fracDigits;
                }
            } else {
                parts[part] = parts[part] * 10 + (c - '0');
                if (parts[part] > 1'000'000) return std::nullopt;
            }
        } else if (c == ':' && !inFraction && part < 2 && digitInPart) {
            ++part;
            digitInPart = false;
        } else if (c == '.' && !inFraction && part == 2 && digitInPart) {
            inFraction = true;
            digitInPart = false; // the fraction needs its own digits
        } else {
            return std::nullopt;
        }
    }
    // Three parts, each with digits, and a fraction (if any) with at least one digit.
    if (part != 2 || !digitInPart) return std::nullopt;
    if (parts[1] > 59 || parts[2] > 59) return std::nullopt;
    while (fracDigits < 3) {
        fracMs *= 10;
        ++fracDigits;
    }
    const long long total = ((parts[0] * 60 + parts[1]) * 60 + parts[2]) * 1000 + fracMs;
    if (total > 1'000'000'000) return std::nullopt;
    return static_cast<int>(total);
}

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

std::string toPgn(const Game& game, const PgnTags& tags) {
    PgnWriteOptions options;
    options.tags = tags;
    return toPgn(game, options);
}

std::string toPgn(const Game& game, const PgnWriteOptions& options) {
    PgnTags tags;
    for (int i = 0; i < 7; ++i) tags.emplace_back(ROSTER[i], ROSTER_DEFAULT[i]);
    const GameStatus status = game.status();
    tags[6].second = resultToken(status);
    if (!(game.startingBoard() == Board())) {
        tags.emplace_back("SetUp", "1");
        tags.emplace_back("FEN", game.startingBoard().toFen());
    }
    if (options.timeControl) tags.emplace_back("TimeControl", options.timeControl->toPgnTag());
    if (status.isOver()) tags.emplace_back("Termination", status.reason == GameEndReason::Timeout ? "time forfeit"
                                                                                                    : "normal");
    for (const auto& [rawName, value] : options.tags) {
        const std::string name = tagName(rawName);
        if (name.empty()) continue;
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

    // Each side's clock, while it can still be worked out from the recorded think times.
    std::optional<int> clock[2];
    if (options.timeControl && options.clocks) clock[0] = clock[1] = options.timeControl->baseMs;

    std::vector<std::string> words;
    Board board = game.startingBoard();
    for (std::size_t i = 0; i < game.moves().size(); ++i) {
        const Move m = game.moves()[i];
        const int side = static_cast<int>(board.sideToMove());
        if (board.sideToMove() == Color::White) {
            words.push_back(std::to_string(board.fullmoveNumber()) + ".");
        } else if (i == 0) {
            words.push_back(std::to_string(board.fullmoveNumber()) + "...");
        }
        words.push_back(toSan(board, m));

        const MoveAnnotation* note = i < options.annotations.size() ? &options.annotations[i] : nullptr;
        if (note && note->nag > 0) words.push_back("$" + std::to_string(note->nag));

        const std::optional<int> spent = game.moveTimes()[i];
        std::string commands;
        if (note && !note->eval.empty()) commands += " [%eval " + note->eval + "]";
        if (clock[side]) {
            if (spent) {
                clock[side] = std::max(0, *clock[side] - *spent) + options.timeControl->incrementMs;
                commands += " [%clk " + formatClock(*clock[side]) + "]";
            } else {
                clock[side].reset(); // unknown from here on
            }
        }
        if (spent && options.elapsed) commands += " [%emt " + formatClock(*spent) + "]";
        if (!commands.empty()) words.push_back("{" + commands + " }");
        if (note && !note->comment.empty()) {
            std::string text = note->comment;
            std::replace(text.begin(), text.end(), '}', ')'); // a '}' would end the comment early
            words.push_back("{ " + text + " }");
        }
        if (note && !note->variation.empty()) {
            const std::string line = lineToSan(board, note->variation);
            if (!line.empty()) words.push_back("(" + line + ")");
        }
        board.makeMove(m);
    }
    words.push_back(tags[6].second); // the result, even if a caller overrode it

    // Wrap at spaces, including the spaces inside comments and variations: in PGN movetext a line break means the
    // same as a space, so long comments can't push a line past 80 columns. Embedded commands like
    // "[%clk 0:04:58]" are never split, because other programs often expect each one on a single line.
    std::vector<std::string> pieces;
    for (const std::string& word : words) {
        std::string piece;
        int brackets = 0;
        for (const char c : word) {
            if (c == '[') ++brackets;
            if (c == ']' && brackets > 0) --brackets;
            if (c == ' ' && brackets == 0) {
                if (!piece.empty()) pieces.push_back(piece);
                piece.clear();
            } else {
                piece += c;
            }
        }
        if (!piece.empty()) pieces.push_back(piece);
    }
    std::size_t lineLength = 0;
    for (const std::string& w : pieces) {
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

std::string joinPgn(const std::vector<std::string>& games) {
    std::string out;
    for (const std::string& g : games) {
        if (!out.empty()) out += '\n';
        out += g;
        if (!g.empty() && g.back() != '\n') out += '\n';
    }
    return out;
}
