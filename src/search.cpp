#include "search.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "game.h"

namespace {

using search::INF;
using search::MATE;
using search::MATE_BOUND;
using search::MAX_PLY;

enum Bound : std::uint8_t { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };

// Piece values for exchange evaluation and move ordering. The king's is large so it's always "most valuable".
constexpr int SEE_VALUE[6] = {100, 320, 330, 500, 950, 20000};

// Late move reductions by depth and move number, a smooth log-log curve.
const auto LMR = [] {
    std::array<std::array<int, 64>, 64> table{};
    for (int d = 1; d < 64; ++d)
        for (int m = 1; m < 64; ++m) table[d][m] = static_cast<int>(0.75 + std::log(d) * std::log(m) / 2.25);
    return table;
}();

int capturedValue(const Board& board, Move move) {
    if (move.isEnPassant()) return SEE_VALUE[0];
    const std::optional<Piece> victim = board.pieceAtUnchecked(move.to());
    return victim ? SEE_VALUE[static_cast<int>(victim->type)] : 0;
}

bool hasNonPawnMaterial(const Board& board, Color c) {
    return board.occupancy(c) & ~(board.pieces(c, PieceType::Pawn) | board.pieces(c, PieceType::King));
}

// Mate scores are stored relative to the node (mate in N from here), not the root, so the same entry is right
// wherever the position turns up in the tree.
int scoreToTable(int score, int ply) {
    if (score >= MATE_BOUND) return score + ply;
    if (score <= -MATE_BOUND) return score - ply;
    return score;
}
int scoreFromTable(int score, int ply) {
    if (score >= MATE_BOUND) return score - ply;
    if (score <= -MATE_BOUND) return score + ply;
    return score;
}

} // namespace

int search::staticExchange(const Board& board, Move move) {
    const Square to = move.to();
    const Square from = move.from();
    const Color us = board.sideToMove();
    int gain[32];
    int d = 0;
    Bitboard occupied = board.occupancy();
    gain[0] = capturedValue(board, move);
    if (move.isEnPassant()) occupied ^= squareBit(static_cast<Square>(us == Color::White ? to - 8 : to + 8));

    int attackerValue = SEE_VALUE[static_cast<int>(board.pieceAtUnchecked(from)->type)];
    if (move.isPromotion()) {
        gain[0] += SEE_VALUE[static_cast<int>(move.promotionType())] - SEE_VALUE[0];
        attackerValue = SEE_VALUE[static_cast<int>(move.promotionType())];
    }
    Bitboard fromBit = squareBit(from);
    Color side = us;

    while (true) {
        ++d;
        side = opposite(side);
        gain[d] = attackerValue - gain[d - 1]; // what `side` gains by recapturing, if it can
        occupied ^= fromBit;
        const Bitboard attackers = attackersTo(board, to, occupied) & occupied;
        const Bitboard ours = attackers & board.occupancy(side);
        if (!ours) break;
        // Least valuable attacker.
        int type = 0;
        Bitboard candidates = 0;
        for (; type < 6; ++type) {
            candidates = ours & board.pieces(side, static_cast<PieceType>(type));
            if (candidates) break;
        }
        // A king can only recapture if nothing can take it back.
        if (type == 5 && (attackers & board.occupancy(opposite(side)))) break;
        fromBit = candidates & (~candidates + 1);
        attackerValue = SEE_VALUE[type];
        if (d >= 31) break;
    }
    while (--d) gain[d - 1] = -std::max(-gain[d - 1], gain[d]);
    return gain[0];
}

Searcher::Searcher(int hashMegabytes) { setHashSize(hashMegabytes); }

void Searcher::setHashSize(int megabytes) {
    const std::size_t bytes = static_cast<std::size_t>(std::max(1, megabytes)) * 1024 * 1024;
    std::size_t entries = 1;
    while (entries * 2 * sizeof(TTEntry) <= bytes) entries *= 2;
    table_.assign(entries, TTEntry{});
    tableMask_ = entries - 1;
}

void Searcher::clear() {
    std::fill(table_.begin(), table_.end(), TTEntry{});
    for (auto& k : killers_) k[0] = k[1] = Move();
    for (auto& c : history_)
        for (auto& f : c)
            for (int& h : f) h = 0;
}

Searcher::TTEntry* Searcher::probe(std::uint64_t key) {
    TTEntry& e = table_[key & tableMask_];
    return e.bound != BOUND_NONE && e.key == key ? &e : nullptr;
}

void Searcher::store(std::uint64_t key, int depth, int score, int bound, Move move, int ply) {
    TTEntry& e = table_[key & tableMask_];
    // Keep deeper results for the same position; always replace a different position.
    if (e.key == key && e.bound != BOUND_NONE && depth < e.depth - 2 && bound != BOUND_EXACT) return;
    if (e.key != key || !move.isNull()) e.move = move.raw();
    e.key = key;
    e.score = static_cast<std::int16_t>(scoreToTable(score, ply));
    e.depth = static_cast<std::uint8_t>(std::clamp(depth, 0, 255));
    e.bound = static_cast<std::uint8_t>(bound);
}

int Searcher::drawScore() const {
    // From the side to move's view: a draw is bad for the side that started the search (when contempt > 0).
    return board_.sideToMove() == rootSide_ ? -settings_.contempt : settings_.contempt;
}

bool Searcher::isRepetition() const {
    // Any earlier occurrence (two-fold) inside the window since the last irreversible move counts as a draw:
    // if repeating once is good, repeating again is too.
    const int current = static_cast<int>(hashes_.size()) - 1;
    const int oldest = std::max(0, current - board_.halfmoveClock());
    for (int i = current - 4; i >= oldest; i -= 2) {
        if (hashes_[i] == hashes_[current]) return true;
    }
    return false;
}

bool Searcher::shouldStop() {
    if (stopped_) return true;
    if (!canStop_) return false;
    if (limits_.maxNodes && nodes_ >= limits_.maxNodes) stopped_ = true;
    if ((nodes_ & 1023) == 0) {
        if (limits_.stop && limits_.stop->load(std::memory_order_relaxed)) stopped_ = true;
        if (limits_.moveTimeMs > 0) {
            const auto elapsed = std::chrono::steady_clock::now() - start_;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= limits_.moveTimeMs) {
                stopped_ = true;
            }
        }
    }
    return stopped_;
}

void Searcher::orderMoves(MoveList& moves, int* scores, Move ttMove, int ply) const {
    const Color us = board_.sideToMove();
    for (int i = 0; i < moves.size(); ++i) {
        const Move m = moves[i];
        int s = 0;
        if (m == ttMove) {
            s = 10'000'000;
        } else if (m.isCapture() || m.isPromotion()) {
            const int attacker = static_cast<int>(board_.pieceAtUnchecked(m.from())->type);
            const int mvvLva = capturedValue(board_, m) * 10 - attacker;
            if (m.isPromotion() && m.promotionType() != PieceType::Queen) {
                s = -1'000'000 + mvvLva; // underpromotions almost never matter
            } else {
                s = search::staticExchange(board_, m) >= 0 ? 5'000'000 + mvvLva : -500'000 + mvvLva;
            }
        } else if (ply < MAX_PLY && m == killers_[ply][0]) {
            s = 4'000'000;
        } else if (ply < MAX_PLY && m == killers_[ply][1]) {
            s = 3'900'000;
        } else {
            s = history_[static_cast<int>(us)][m.from()][m.to()];
        }
        scores[i] = s;
    }
}

// Selection sort step: swaps the best remaining move into position i. Usually cheaper than a full sort,
// because a cutoff often comes from one of the first few moves.
static void pickNext(MoveList& moves, int* scores, int i) {
    int best = i;
    for (int j = i + 1; j < moves.size(); ++j)
        if (scores[j] > scores[best]) best = j;
    if (best != i) {
        std::swap(moves[i], moves[best]);
        std::swap(scores[i], scores[best]);
    }
}

int Searcher::quiesce(int alpha, int beta, int ply) {
    ++nodes_;
    if (shouldStop()) return 0;
    if (ply >= MAX_PLY - 1) return evaluateHere();

    const bool checked = inCheck(board_);
    int best = -INF;
    int standPat = 0;
    MoveList moves;
    if (checked) {
        // In check every evasion has to be considered, or quiescence would miss mates.
        generatePseudoLegal(board_, moves);
        best = -MATE + ply;
    } else {
        standPat = evaluateHere();
        if (standPat >= beta) return standPat;
        alpha = std::max(alpha, standPat);
        best = standPat;
        generatePseudoLegal(board_, moves, GenType::Noisy);
    }

    int scores[MoveList::CAPACITY];
    orderMoves(moves, scores, Move(), MAX_PLY);
    for (int i = 0; i < moves.size(); ++i) {
        pickNext(moves, scores, i);
        const Move m = moves[i];
        if (!checked) {
            if (m.isPromotion() && m.promotionType() != PieceType::Queen) continue;
            // Delta pruning: even winning this piece for free can't raise alpha.
            const int promoBonus = m.isPromotion() ? SEE_VALUE[4] - SEE_VALUE[0] : 0;
            if (standPat + capturedValue(board_, m) + promoBonus + 200 < alpha) continue;
            if (search::staticExchange(board_, m) < 0) continue; // losing capture
        }
        const UndoInfo undo = board_.makeMove(m);
        if (moverLeftInCheck(board_)) {
            board_.unmakeMove(m, undo);
            continue;
        }
        const int score = -quiesce(-beta, -alpha, ply + 1);
        board_.unmakeMove(m, undo);
        if (stopped_) return 0;
        if (score > best) {
            best = score;
            if (score > alpha) {
                alpha = score;
                if (score >= beta) break;
            }
        }
    }
    return best;
}

int Searcher::negamax(int depth, int alpha, int beta, int ply, bool pvNode, bool allowNull) {
    pvLength_[ply] = ply;
    if (ply > 0) {
        if (isRepetition() || isInsufficientMaterial(board_)) return drawScore();
        // The 50-move rule draws only if the last move wasn't checkmate: a mate on the 100th half-move still wins.
        if (board_.halfmoveClock() >= 100) {
            if (!inCheck(board_)) return drawScore();
            MoveList replies;
            generateLegal(board_, replies);
            return replies.empty() ? -MATE + ply : drawScore();
        }
        // Mate distance pruning: no line from here can beat a mate already found closer to the root.
        alpha = std::max(alpha, -MATE + ply);
        beta = std::min(beta, MATE - ply - 1);
        if (alpha >= beta) return alpha;
    }

    const bool checked = inCheck(board_);
    if (checked && ply < MAX_PLY / 2) ++depth; // check extension
    if (depth <= 0) return quiesce(alpha, beta, ply);

    ++nodes_;
    if (shouldStop()) return 0;
    if (ply >= MAX_PLY - 1) return evaluateHere();

    const std::uint64_t key = board_.hash();
    Move ttMove;
    if (const TTEntry* e = probe(key)) {
        ttMove = Move::fromRaw(e->move);
        if (!pvNode && e->depth >= depth && ply > 0) {
            const int s = scoreFromTable(e->score, ply);
            if (e->bound == BOUND_EXACT || (e->bound == BOUND_LOWER && s >= beta) ||
                (e->bound == BOUND_UPPER && s <= alpha)) {
                return s;
            }
        }
    }

    const Color us = board_.sideToMove();
    const int staticEval = checked ? -INF : evaluateHere();

    if (!pvNode && !checked && ply > 0) {
        // Reverse futility: far enough above beta that a shallow search won't bring it back down.
        if (settings_.futilityPruning && depth <= 6 && staticEval - 85 * depth >= beta && !search::isMateScore(beta)) {
            return staticEval;
        }
        // Null move: if passing still keeps us above beta, a real move will too (except in zugzwang, which is
        // why it's skipped when only pawns are left).
        if (settings_.nullMovePruning && allowNull && depth >= 3 && staticEval >= beta &&
            hasNonPawnMaterial(board_, us)) {
            const int reduction = 3 + depth / 6;
            const UndoInfo undo = board_.makeNullMove();
            hashes_.push_back(board_.hash());
            const int score = -negamax(depth - 1 - reduction, -beta, -beta + 1, ply + 1, false, false);
            hashes_.pop_back();
            board_.unmakeNullMove(undo);
            if (stopped_) return 0;
            if (score >= beta) return search::isMateScore(score) ? beta : score;
        }
    }

    MoveList moves;
    generatePseudoLegal(board_, moves);
    int scores[MoveList::CAPACITY];
    orderMoves(moves, scores, ttMove, ply);

    int legal = 0;
    int quietsTried = 0;
    int best = -INF;
    Move bestMove;
    int bound = BOUND_UPPER;
    Move quietsSearched[MoveList::CAPACITY];

    for (int i = 0; i < moves.size(); ++i) {
        pickNext(moves, scores, i);
        const Move m = moves[i];
        const bool quiet = !m.isCapture() && !m.isPromotion();

        const UndoInfo undo = board_.makeMove(m);
        if (moverLeftInCheck(board_)) {
            board_.unmakeMove(m, undo);
            continue;
        }
        ++legal;
        const bool givesCheck = inCheck(board_);

        // Prune late quiet moves near the leaves once a reasonable move has been found.
        if (settings_.futilityPruning && !pvNode && !checked && quiet && !givesCheck && legal > 1 &&
            best > -MATE_BOUND) {
            const bool futile = depth <= 2 && staticEval + 100 + 100 * depth <= alpha;
            const bool tooLate = depth <= 3 && quietsTried >= 3 + 2 * depth * depth;
            if (futile || tooLate) {
                board_.unmakeMove(m, undo);
                continue;
            }
        }
        if (quiet) quietsSearched[quietsTried++] = m;

        hashes_.push_back(board_.hash());
        int score;
        if (legal == 1) {
            score = -negamax(depth - 1, -beta, -alpha, ply + 1, pvNode, true);
        } else {
            int reduction = 0;
            if (settings_.lateMoveReductions && depth >= 3 && legal > 3 && quiet && !checked && !givesCheck) {
                reduction = LMR[std::min(depth, 63)][std::min(legal, 63)];
                if (pvNode) --reduction;
                if (m == killers_[ply][0] || m == killers_[ply][1]) --reduction;
                reduction = std::clamp(reduction, 0, depth - 2);
            }
            score = -negamax(depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, false, true);
            if (score > alpha && reduction > 0) score = -negamax(depth - 1, -alpha - 1, -alpha, ply + 1, false, true);
            if (score > alpha && score < beta) score = -negamax(depth - 1, -beta, -alpha, ply + 1, true, true);
        }
        hashes_.pop_back();
        board_.unmakeMove(m, undo);
        if (stopped_) return 0;

        if (score > best) {
            best = score;
            bestMove = m;
            if (score > alpha) {
                alpha = score;
                bound = BOUND_EXACT;
                pv_[ply][ply] = m;
                for (int j = ply + 1; j < pvLength_[ply + 1]; ++j) pv_[ply][j] = pv_[ply + 1][j];
                pvLength_[ply] = std::max(pvLength_[ply + 1], ply + 1);
                if (score >= beta) {
                    bound = BOUND_LOWER;
                    if (quiet) {
                        if (killers_[ply][0] != m) {
                            killers_[ply][1] = killers_[ply][0];
                            killers_[ply][0] = m;
                        }
                        // Reward the move that cut off and penalize the quiet moves tried before it.
                        int (&h)[NUM_SQUARES][NUM_SQUARES] = history_[static_cast<int>(us)];
                        const int bonus = std::min(depth * depth, 400);
                        for (int q = 0; q < quietsTried; ++q) {
                            const Move qm = quietsSearched[q];
                            int& entry = h[qm.from()][qm.to()];
                            const int delta = qm == m ? bonus : -bonus;
                            entry += delta - entry * std::abs(delta) / 16384; // stays within about ±16384
                        }
                    }
                    break;
                }
            }
        }
    }

    if (legal == 0) return checked ? -MATE + ply : drawScore();
    store(key, depth, best, bound, bestMove, ply);
    return best;
}

void Searcher::begin(const Board& board, const std::vector<std::uint64_t>& history, const SearchLimits& limits,
                     const SearchSettings& settings) {
    board_ = board;
    hashes_ = history;
    if (hashes_.empty() || hashes_.back() != board.hash()) hashes_.push_back(board.hash());
    limits_ = limits;
    settings_ = settings;
    rootSide_ = board.sideToMove();
    start_ = std::chrono::steady_clock::now();
    nodes_ = 0;
    stopped_ = false;
    canStop_ = false;
    for (auto& k : killers_) k[0] = k[1] = Move();
    // Age the history scores so old games don't dominate.
    for (auto& c : history_)
        for (auto& f : c)
            for (int& h : f) h /= 4;
}

SearchResult Searcher::search(const Board& board, const std::vector<std::uint64_t>& history,
                              const SearchLimits& limits, const SearchSettings& settings) {
    begin(board, history, limits, settings);
    SearchResult result;

    MoveList legal;
    generateLegal(board, legal);
    if (legal.empty()) {
        result.score = inCheck(board) ? -MATE : 0;
        return result;
    }
    result.bestMove = legal[0];
    if (legal.size() == 1) { // nothing to think about
        result.depth = 0;
        result.pv = {legal[0]};
        result.score = evaluate(board, settings.weights);
        return result;
    }

    int previous = 0;
    const int maxDepth = std::clamp(limits.maxDepth, 1, MAX_PLY - 8);
    for (int depth = 1; depth <= maxDepth; ++depth) {
        // Aspiration window: expect a score near the last one and only widen if that guess was wrong.
        int window = depth >= 5 ? 30 : INF;
        int alpha = depth >= 5 ? std::max(-INF, previous - window) : -INF;
        int beta = depth >= 5 ? std::min(INF, previous + window) : INF;
        int score = 0;
        while (true) {
            score = negamax(depth, alpha, beta, 0, true, false);
            if (stopped_) break;
            if (score <= alpha) {
                alpha = std::max(-INF, alpha - window);
            } else if (score >= beta) {
                beta = std::min(INF, beta + window);
            } else {
                break;
            }
            window *= 2;
        }
        canStop_ = true; // from here on there's a completed iteration to fall back on
        if (stopped_) break;

        previous = score;
        result.score = score;
        result.depth = depth;
        result.pv.assign(pv_[0], pv_[0] + pvLength_[0]);
        if (!result.pv.empty()) result.bestMove = result.pv[0];

        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_).count();
        // A found mate won't get any shorter by searching deeper than its length.
        if (search::isMateScore(score) && depth > 2 * std::abs(search::mateInMoves(score)) + 2) break;
        // The next iteration usually takes several times longer than this one, so don't start it if it likely
        // can't finish.
        if (limits.moveTimeMs > 0 && elapsed * 2 > limits.moveTimeMs) break;
        if (limits.maxNodes && nodes_ >= limits.maxNodes) break;
    }

    result.nodes = nodes_;
    result.timeMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_)
                        .count();
    return result;
}

std::vector<ScoredMove> Searcher::candidateMoves(const Board& board, const std::vector<std::uint64_t>& history,
                                                 int depth, int margin, const SearchLimits& limits,
                                                 const SearchSettings& settings, SearchResult* mainResult) {
    std::vector<ScoredMove> out;
    SearchLimits main = limits;
    main.maxDepth = depth;
    const SearchResult best = search(board, history, main, settings);
    if (mainResult) *mainResult = best;
    if (best.bestMove.isNull()) return out;
    out.push_back({best.bestMove, best.score});
    if (best.depth == 0) return out; // only one legal move

    // Score the rest with the same depth. The window is [best - margin, best + 1]: anything that fails low is
    // more than `margin` worse and gets dropped cheaply.
    // The time and node budget covers both phases, so keep the clock and node count running.
    const auto started = start_;
    const std::uint64_t spent = nodes_;
    begin(board, history, limits, settings);
    start_ = started;
    nodes_ = spent;
    canStop_ = true;
    const int searchDepth = best.depth;
    const int floor = best.score - margin;
    MoveList legal;
    generateLegal(board, legal);
    for (const Move m : legal) {
        if (m == best.bestMove) continue;
        const UndoInfo undo = board_.makeMove(m);
        hashes_.push_back(board_.hash());
        const int score = -negamax(searchDepth - 1, -(best.score + 1), -(floor - 1), 1, true, true);
        hashes_.pop_back();
        board_.unmakeMove(m, undo);
        if (stopped_) break;
        if (score >= floor) out.push_back({m, std::min(score, best.score)});
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const ScoredMove& a, const ScoredMove& b) { return a.score > b.score; });
    return out;
}
