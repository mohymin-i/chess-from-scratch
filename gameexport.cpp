#include "gameexport.h"

#include <cstdio>
#include <cstdlib>

#include "san.h"

namespace {

constexpr int MATE_CP = 10000;              // see analysis::mateToCentipawns
constexpr int MATE_CP_FLOOR = MATE_CP - 200; // anything this big encodes a forced mate

std::string twoDigits(unsigned v) { return (v < 10 ? "0" : "") + std::to_string(v); }

// The value of an EPD operation or CSV cell, quoted when needed.
std::string epdValue(const std::string& value) {
    if (value.find_first_of(" ;\"") == std::string::npos && !value.empty()) return value;
    std::string out = "\"";
    for (const char c : value) out += c == '"' ? '\'' : c; // EPD strings can't contain double quotes
    return out + "\"";
}

std::string csvCell(const std::string& value) {
    if (value.find_first_of(",\"\n\r") == std::string::npos) return value;
    std::string out = "\"";
    for (const char c : value) {
        if (c == '"') out += '"';
        out += c;
    }
    return out + "\"";
}

std::string tagValue(const PgnTags& tags, const std::string& name, const std::string& fallback = "") {
    for (const auto& [n, v] : tags)
        if (n == name) return v;
    return fallback;
}

std::string fixed(double value, int decimals) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

// Seconds per move for one side, or "" if no times were recorded.
std::string averageSeconds(const Game& game, Color side) {
    Color mover = game.startingBoard().sideToMove();
    long long total = 0;
    int count = 0;
    for (const std::optional<int>& t : game.moveTimes()) {
        if (mover == side && t) {
            total += *t;
            ++count;
        }
        mover = opposite(mover);
    }
    return count ? fixed(static_cast<double>(total) / count / 1000.0, 1) : "";
}

} // namespace

std::string formatEval(int whiteCentipawns) {
    const int magnitude = std::abs(whiteCentipawns);
    if (magnitude >= MATE_CP_FLOOR) {
        const int plies = MATE_CP - magnitude;
        if (plies <= 0) return ""; // already checkmate: nothing to evaluate
        const int moves = (plies + 1) / 2;
        return whiteCentipawns > 0 ? "#" + std::to_string(moves) : "#-" + std::to_string(moves);
    }
    return fixed(whiteCentipawns / 100.0, 2);
}

PgnTags dateTags(std::chrono::system_clock::time_point when) {
    using namespace std::chrono;
    const auto day = floor<days>(when);
    const year_month_day ymd{day};
    const hh_mm_ss<seconds> time{floor<seconds>(when - day)};
    const std::string date = std::to_string(static_cast<int>(ymd.year())) + "." +
                             twoDigits(static_cast<unsigned>(ymd.month())) + "." +
                             twoDigits(static_cast<unsigned>(ymd.day()));
    const std::string clock = twoDigits(static_cast<unsigned>(time.hours().count())) + ":" +
                              twoDigits(static_cast<unsigned>(time.minutes().count())) + ":" +
                              twoDigits(static_cast<unsigned>(time.seconds().count()));
    return {{"Date", date}, {"UTCDate", date}, {"UTCTime", clock}};
}

std::string exportGamePgn(const Game& game, const GameExportOptions& options) {
    PgnWriteOptions pgn;
    pgn.tags = options.tags;
    pgn.timeControl = options.timeControl;
    pgn.clocks = options.clocks;
    pgn.elapsed = options.elapsed;

    if (options.analysis) {
        const GameAnalysis& review = *options.analysis;
        bool hasAnnotator = false;
        for (const auto& [n, v] : pgn.tags) hasAnnotator |= n == "Annotator";
        if (!hasAnnotator) pgn.tags.emplace_back("Annotator", "Chess from Scratch");

        Board board = game.startingBoard();
        const std::size_t count = std::min(review.moves.size(), game.moves().size());
        for (std::size_t i = 0; i < count; ++i) {
            const MoveAnalysis& m = review.moves[i];
            MoveAnnotation note;
            const bool whiteMoved = board.sideToMove() == Color::White;
            if (options.evals) note.eval = formatEval(whiteMoved ? m.evalAfter : -m.evalAfter);
            if (options.judgments && !(m.best == m.played) && !m.best.isNull()) {
                const char* label = nullptr;
                if (m.quality == MoveQuality::Inaccuracy) {
                    note.nag = 6; // ?!
                    label = "Inaccuracy.";
                } else if (m.quality == MoveQuality::Mistake) {
                    note.nag = 2; // ?
                    label = "Mistake.";
                } else if (m.quality == MoveQuality::Blunder) {
                    note.nag = 4; // ??
                    label = "Blunder.";
                }
                if (label) {
                    note.comment = std::string(label) + " " + toSan(board, m.best) + " was best.";
                    const std::size_t plies = std::min(m.bestLine.size(),
                                                       static_cast<std::size_t>(std::max(0, options.variationPlies)));
                    note.variation.assign(m.bestLine.begin(), m.bestLine.begin() + static_cast<long>(plies));
                    if (note.variation.empty()) note.variation = {m.best};
                }
            }
            pgn.annotations.push_back(std::move(note));
            board.makeMove(game.moves()[i]);
        }
    }
    return toPgn(game, pgn);
}

std::vector<std::string> positionFens(const Game& game) {
    std::vector<std::string> out;
    Board board = game.startingBoard();
    out.push_back(board.toFen());
    for (const Move m : game.moves()) {
        board.makeMove(m);
        out.push_back(board.toFen());
    }
    return out;
}

std::string toEpd(const Board& board, const EpdOperations& operations) {
    const std::string fen = board.toFen();
    // The first four FEN fields: everything before the halfmove clock.
    std::size_t cut = fen.size();
    for (int spaces = 0, i = 0; i < static_cast<int>(fen.size()); ++i) {
        if (fen[i] == ' ' && ++spaces == 4) {
            cut = static_cast<std::size_t>(i);
            break;
        }
    }
    std::string out = fen.substr(0, cut);
    for (const auto& [opcode, value] : operations) {
        out += " " + opcode;
        if (!value.empty()) out += " " + epdValue(value);
        out += ";";
    }
    return out;
}

std::string exportPositionsEpd(const Game& game, const EpdExportOptions& options) {
    std::string out;
    Board board = game.startingBoard();
    const std::vector<Move>& moves = game.moves();
    for (std::size_t i = 0; i <= moves.size(); ++i) {
        const bool hasMove = i < moves.size();
        const MoveAnalysis* m =
            options.analysis && i < options.analysis->moves.size() ? &options.analysis->moves[i] : nullptr;
        const bool mistake = m && (m->quality == MoveQuality::Mistake || m->quality == MoveQuality::Blunder);
        const bool wanted = (!options.player || board.sideToMove() == *options.player) &&
                            (!options.onlyMistakes || mistake);
        if (wanted && (hasMove || !options.onlyMistakes)) {
            EpdOperations ops;
            if (m && !m->best.isNull()) {
                ops.emplace_back("bm", toSan(board, m->best));
                if (mistake) ops.emplace_back("am", toSan(board, m->played));
                if (std::abs(m->evalBefore) >= MATE_CP_FLOOR) {
                    const int moves = (MATE_CP - std::abs(m->evalBefore) + 1) / 2;
                    ops.emplace_back("dm", std::to_string(m->evalBefore > 0 ? moves : -moves));
                } else {
                    ops.emplace_back("ce", std::to_string(m->evalBefore)); // side to move's point of view
                }
            }
            ops.emplace_back("hmvc", std::to_string(board.halfmoveClock()));
            ops.emplace_back("fmvn", std::to_string(board.fullmoveNumber()));
            const std::string where = std::to_string(board.fullmoveNumber()) +
                                      (board.sideToMove() == Color::White ? "." : "...");
            ops.emplace_back("id", options.id + " " + where);
            if (hasMove) {
                std::string note = "played " + toSan(board, moves[i]);
                if (m) note += std::string(" (") + toString(m->quality) + ")";
                ops.emplace_back("c0", note);
            }
            out += toEpd(board, ops) + "\n";
        }
        if (hasMove) board.makeMove(moves[i]);
    }
    return out;
}

std::string terminationText(const GameStatus& status) {
    switch (status.reason) {
        case GameEndReason::None: return "";
        case GameEndReason::Checkmate: return "checkmate";
        case GameEndReason::Stalemate: return "stalemate";
        case GameEndReason::FiftyMoveRule: return "fifty-move rule";
        case GameEndReason::ThreefoldRepetition: return "threefold repetition";
        case GameEndReason::InsufficientMaterial: return "insufficient material";
        case GameEndReason::Resignation: return "resignation";
        case GameEndReason::DrawAgreed: return "draw agreed";
        case GameEndReason::Timeout: return "time forfeit";
    }
    return "";
}

std::string exportSummaryCsv(const std::vector<GameSummaryInput>& games) {
    const char* header[] = {"Event",
                            "Date",
                            "White",
                            "Black",
                            "Result",
                            "Termination",
                            "TimeControl",
                            "Moves",
                            "StartFEN",
                            "WhiteAccuracy",
                            "BlackAccuracy",
                            "WhiteACPL",
                            "BlackACPL",
                            "WhiteInaccuracies",
                            "WhiteMistakes",
                            "WhiteBlunders",
                            "BlackInaccuracies",
                            "BlackMistakes",
                            "BlackBlunders",
                            "WhiteSecondsPerMove",
                            "BlackSecondsPerMove"};
    std::string out;
    for (std::size_t i = 0; i < std::size(header); ++i) out += (i ? "," : "") + std::string(header[i]);
    out += "\r\n"; // RFC 4180 line endings, which every spreadsheet accepts

    for (const GameSummaryInput& in : games) {
        if (!in.game) continue;
        const Game& g = *in.game;
        const GameStatus status = g.status();
        const int plies = static_cast<int>(g.moves().size());
        const bool blackFirst = g.startingBoard().sideToMove() == Color::Black;
        const int fullMoves = (plies + (blackFirst ? 1 : 0) + 1) / 2;
        std::vector<std::string> row = {
            tagValue(in.tags, "Event", "?"),
            tagValue(in.tags, "Date", "????.??.??"),
            tagValue(in.tags, "White", "?"),
            tagValue(in.tags, "Black", "?"),
            resultToken(status),
            terminationText(status),
            in.timeControl ? in.timeControl->toPgnTag() : "-",
            std::to_string(fullMoves),
            g.startingBoard() == Board() ? "" : g.startingBoard().toFen(),
        };
        if (in.analysis) {
            const PlayerSummary& w = in.analysis->white;
            const PlayerSummary& b = in.analysis->black;
            for (const std::string& cell :
                 {fixed(w.accuracy, 1), fixed(b.accuracy, 1), std::to_string(w.averageCentipawnLoss),
                  std::to_string(b.averageCentipawnLoss), std::to_string(w.inaccuracies), std::to_string(w.mistakes),
                  std::to_string(w.blunders), std::to_string(b.inaccuracies), std::to_string(b.mistakes),
                  std::to_string(b.blunders)}) {
                row.push_back(cell);
            }
        } else {
            row.insert(row.end(), 10, "");
        }
        row.push_back(averageSeconds(g, Color::White));
        row.push_back(averageSeconds(g, Color::Black));
        for (std::size_t i = 0; i < row.size(); ++i) out += (i ? "," : "") + csvCell(row[i]);
        out += "\r\n";
    }
    return out;
}
