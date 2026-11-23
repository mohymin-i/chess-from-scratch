#include "analysis.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double INACCURACY = 5.0; // win-chance points lost
constexpr double MISTAKE = 10.0;
constexpr double BLUNDER = 18.0;

void summarize(const std::vector<MoveAnalysis>& moves, Color color, Color firstMover, PlayerSummary& out) {
    std::vector<double> accuracies;
    long long totalLoss = 0;
    int matches = 0;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const Color mover = (i % 2 == 0) ? firstMover : opposite(firstMover);
        if (mover != color) continue;
        const MoveAnalysis& m = moves[i];
        if (m.quality == MoveQuality::Forced) continue;
        accuracies.push_back(m.accuracy);
        totalLoss += std::min(m.centipawnLoss, 1000); // one lost mate shouldn't swamp the average
        if (m.played == m.best) ++matches;
        if (m.quality == MoveQuality::Inaccuracy) ++out.inaccuracies;
        if (m.quality == MoveQuality::Mistake) ++out.mistakes;
        if (m.quality == MoveQuality::Blunder) ++out.blunders;
    }
    out.movesAnalyzed = static_cast<int>(accuracies.size());
    if (accuracies.empty()) return;
    // The average of the arithmetic and harmonic means: the harmonic mean makes a single blunder count for more
    // than a plain average would, which matches how a game "feels".
    double sum = 0;
    double inverseSum = 0;
    for (const double a : accuracies) {
        sum += a;
        inverseSum += 1.0 / std::max(a, 1.0);
    }
    const double n = static_cast<double>(accuracies.size());
    out.accuracy = std::clamp((sum / n + n / inverseSum) / 2.0, 0.0, 100.0);
    out.averageCentipawnLoss = static_cast<int>(totalLoss / static_cast<long long>(accuracies.size()));
    out.engineMatchRate = matches / n;
}
} // namespace

const char* toString(MoveQuality quality) {
    switch (quality) {
        case MoveQuality::Forced: return "forced";
        case MoveQuality::Best: return "best";
        case MoveQuality::Excellent: return "excellent";
        case MoveQuality::Good: return "good";
        case MoveQuality::Inaccuracy: return "inaccuracy";
        case MoveQuality::Mistake: return "mistake";
        case MoveQuality::Blunder: return "blunder";
    }
    return "?";
}

double analysis::winChance(int centipawns) {
    const int cp = std::clamp(centipawns, -1000, 1000);
    return 50.0 + 50.0 * (2.0 / (1.0 + std::exp(-0.00368208 * cp)) - 1.0);
}

double analysis::moveAccuracy(double before, double after) {
    const double lost = before - after;
    if (lost <= 0) return 100.0;
    return std::clamp(103.1668 * std::exp(-0.04354 * lost) - 3.1669, 0.0, 100.0);
}

int analysis::mateToCentipawns(int score) {
    if (score >= search::MATE_BOUND) return 10000 - (search::MATE - score);
    if (score <= -search::MATE_BOUND) return -10000 + (search::MATE + score);
    return score;
}

MoveQuality analysis::classify(double winChanceLost, bool playedBest) {
    if (playedBest) return MoveQuality::Best;
    if (winChanceLost < 2.0) return MoveQuality::Excellent;
    if (winChanceLost < INACCURACY) return MoveQuality::Good;
    if (winChanceLost < MISTAKE) return MoveQuality::Inaccuracy;
    if (winChanceLost < BLUNDER) return MoveQuality::Mistake;
    return MoveQuality::Blunder;
}

GameAnalysis analyzeGame(const Game& game, const AnalysisSettings& settings) {
    GameAnalysis result;
    const std::vector<Move>& moves = game.moves();
    const int total = static_cast<int>(moves.size());
    if (total == 0) return result;

    Searcher searcher(settings.hashMegabytes);
    SearchLimits limits;
    limits.maxDepth = std::max(1, settings.depth);
    limits.maxNodes = settings.nodesPerPosition;

    // Score every position from the start to after the last move (from its side to move's point of view).
    // Position i's score is "eval before move i"; minus position i+1's score is "eval after move i".
    std::vector<int> scores(total + 1, 0);
    std::vector<Move> best(total);
    std::vector<std::vector<Move>> lines(total);
    std::vector<bool> forced(total, false);
    std::vector<std::uint64_t> history;

    Game replay(game.startingBoard());
    int analyzed = 0; // moves with both a "before" and an "after" score
    bool cancelled = false;
    for (int i = 0; i <= total; ++i) {
        history.push_back(replay.board().hash());
        const GameStatus status = replay.status();
        if (status.isOver() && status.reason != GameEndReason::Resignation &&
            status.reason != GameEndReason::DrawAgreed) {
            // Ended by the rules: the score is known exactly.
            scores[i] = status.reason == GameEndReason::Checkmate ? -search::MATE : 0;
        } else {
            const SearchResult r = searcher.search(replay.board(), history, limits);
            scores[i] = r.score;
            if (i < total) {
                best[i] = r.bestMove;
                lines[i] = r.pv;
                forced[i] = replay.legalMoves().size() == 1;
            }
        }
        analyzed = i;
        if (i == total || cancelled) break;
        // Resignations and agreed draws only happen after the last move, so the replay never refuses a move.
        if (!replay.play(moves[i])) break;
        if (settings.progress && !settings.progress(i + 1, total)) cancelled = true; // finish this move first
    }

    const Color firstMover = game.startingBoard().sideToMove();
    for (int i = 0; i < analyzed; ++i) {
        MoveAnalysis m;
        m.played = moves[i];
        m.best = best[i];
        m.bestLine = lines[i];
        m.thinkTimeMs = game.moveTimes()[i];
        m.evalBefore = analysis::mateToCentipawns(scores[i]);
        m.evalAfter = -analysis::mateToCentipawns(scores[i + 1]);
        const bool playedBest = m.played == m.best;
        m.centipawnLoss = playedBest ? 0 : std::max(0, m.evalBefore - m.evalAfter);
        m.winChanceBefore = analysis::winChance(m.evalBefore);
        m.winChanceAfter = analysis::winChance(m.evalAfter);
        const double lost = playedBest ? 0.0 : std::max(0.0, m.winChanceBefore - m.winChanceAfter);
        if (forced[i]) {
            m.quality = MoveQuality::Forced;
            m.accuracy = 100;
            m.centipawnLoss = 0;
        } else {
            m.quality = analysis::classify(lost, playedBest);
            m.accuracy = analysis::moveAccuracy(m.winChanceBefore, m.winChanceBefore - lost);
        }
        result.moves.push_back(std::move(m));
    }
    summarize(result.moves, Color::White, firstMover, result.white);
    summarize(result.moves, Color::Black, firstMover, result.black);
    return result;
}

std::vector<TrainingPosition> mistakesToPractice(const Game& game, const GameAnalysis& review, Color player) {
    std::vector<TrainingPosition> out;
    Board board = game.startingBoard();
    for (std::size_t i = 0; i < review.moves.size(); ++i) {
        const MoveAnalysis& m = review.moves[i];
        const bool bad = m.quality == MoveQuality::Mistake || m.quality == MoveQuality::Blunder;
        if (bad && board.sideToMove() == player && !m.best.isNull() && !(m.best == m.played)) {
            out.push_back({board.toFen(), m.played, m.best, m.quality, m.centipawnLoss, static_cast<int>(i)});
        }
        board.makeMove(game.moves()[i]);
    }
    return out;
}
