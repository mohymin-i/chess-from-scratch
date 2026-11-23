#include "bot.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace {

BotSettings make(int level, std::string name, std::string description, int rating) {
    BotSettings s;
    s.level = level;
    s.name = std::move(name);
    s.description = std::move(description);
    s.approxRating = rating;
    return s;
}

std::vector<BotSettings> makeBuiltIns() {
    std::vector<BotSettings> bots;

    // Level 1. Three plies plus full capture resolution: it spots forks, pins and hanging pieces, and won't give
    // its own pieces away. It loses on purpose only by settling for decent moves over the best ones.
    BotSettings pebble = make(1, "Pebble",
                              "Thinks three moves ahead and punishes loose pieces, but often settles for a decent "
                              "move instead of the best one. A real game for new players.",
                              1100);
    pebble.maxDepth = 3;
    pebble.maxNodes = 80'000;
    pebble.randomMargin = 70;
    pebble.temperature = 35;
    pebble.mistakeChance = 0.07;
    pebble.mistakeMargin = 180;
    pebble.search.weights.mobility = 60;
    pebble.search.weights.kingSafety = 60;
    pebble.search.weights.pawnStructure = 60;
    pebble.hashMegabytes = 4;
    bots.push_back(pebble);

    BotSettings juniper = make(2, "Juniper",
                               "Sees four moves ahead and plays sensible, developing chess. Makes the occasional "
                               "inaccuracy you can take advantage of.",
                               1350);
    juniper.maxDepth = 4;
    juniper.maxNodes = 200'000;
    juniper.randomMargin = 45;
    juniper.temperature = 25;
    juniper.mistakeChance = 0.04;
    juniper.mistakeMargin = 140;
    juniper.search.weights.mobility = 80;
    juniper.hashMegabytes = 8;
    bots.push_back(juniper);

    BotSettings corvid = make(3, "Corvid",
                              "A sharp tactician that loves active pieces. Calculates six moves deep and rarely "
                              "slips.",
                              1600);
    corvid.maxDepth = 6;
    corvid.maxNodes = 600'000;
    corvid.randomMargin = 25;
    corvid.temperature = 14;
    corvid.mistakeChance = 0.015;
    corvid.mistakeMargin = 100;
    corvid.search.weights.mobility = 125;
    corvid.search.contempt = 5;
    bots.push_back(corvid);

    BotSettings bastion = make(4, "Bastion",
                               "A patient positional player: solid pawns, a safe king, and slow squeezes. Very hard "
                               "to trick.",
                               1850);
    bastion.moveTimeMs = 500;
    bastion.maxNodes = 3'000'000;
    bastion.randomMargin = 10;
    bastion.temperature = 6;
    bastion.candidateDepth = 7;
    bastion.search.weights.pawnStructure = 135;
    bastion.search.weights.kingSafety = 120;
    bastion.search.weights.passedPawns = 120;
    bastion.search.contempt = 10;
    bastion.hashMegabytes = 32;
    bots.push_back(bastion);

    BotSettings tempest = make(5, "Tempest",
                               "Aggressive and relentless. Goes after your king and avoids quiet draws. Plays its "
                               "best move every time.",
                               2100);
    tempest.moveTimeMs = 1200;
    tempest.openingVarietyPlies = 8;
    tempest.openingVarietyMargin = 15;
    tempest.candidateDepth = 8;
    tempest.search.weights.kingSafety = 150;
    tempest.search.weights.mobility = 130;
    tempest.search.contempt = 30;
    tempest.hashMegabytes = 64;
    bots.push_back(tempest);

    BotSettings zenith = make(6, "Zenith",
                              "Full strength: the deepest search and the most time. Varies its first few moves, then "
                              "plays the best move it can find.",
                              2300);
    zenith.moveTimeMs = 3000;
    zenith.openingVarietyPlies = 6;
    zenith.openingVarietyMargin = 10;
    zenith.candidateDepth = 9;
    zenith.search.contempt = 15;
    zenith.hashMegabytes = 128;
    bots.push_back(zenith);

    return bots;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

} // namespace

BotSettings BotSettings::validated() const {
    BotSettings s = *this;
    s.maxDepth = std::clamp(s.maxDepth, 1, search::MAX_PLY - 8);
    s.moveTimeMs = std::max(0, s.moveTimeMs);
    s.randomMargin = std::clamp(s.randomMargin, 0, 1000);
    s.temperature = std::clamp(s.temperature, 1, 1000);
    s.mistakeChance = std::clamp(s.mistakeChance, 0.0, 1.0);
    s.mistakeMargin = std::clamp(s.mistakeMargin, 0, 2000);
    s.candidateDepth = std::clamp(s.candidateDepth, 0, s.maxDepth);
    s.openingVarietyPlies = std::clamp(s.openingVarietyPlies, 0, 40);
    s.openingVarietyMargin = std::clamp(s.openingVarietyMargin, 0, 200);
    s.hashMegabytes = std::clamp(s.hashMegabytes, 1, 1024);
    s.search.contempt = std::clamp(s.search.contempt, -200, 200);
    EvalWeights& w = s.search.weights;
    for (int* weight : {&w.material, &w.pieceSquare, &w.mobility, &w.kingSafety, &w.pawnStructure, &w.passedPawns}) {
        *weight = std::clamp(*weight, 0, 300);
    }
    // With no limit at all the bot would think forever.
    if (s.maxNodes == 0 && s.moveTimeMs == 0 && s.maxDepth > 12) s.moveTimeMs = 1000;
    return s;
}

const std::vector<BotSettings>& builtInBots() {
    static const std::vector<BotSettings> bots = makeBuiltIns();
    return bots;
}

std::optional<BotSettings> findBot(std::string_view nameOrLevel) {
    const std::string key = lower(nameOrLevel);
    for (const BotSettings& b : builtInBots()) {
        if (lower(b.name) == key || std::to_string(b.level) == key) return b;
    }
    return std::nullopt;
}

int timeForMove(int remainingMs, int incrementMs, int movesToGo) {
    if (remainingMs <= 0) return 1;
    const int divisor = movesToGo > 0 ? movesToGo + 1 : 30;
    const int share = remainingMs / divisor + incrementMs * 3 / 4;
    return std::max(1, std::min(share, remainingMs / 4));
}

Bot::Bot(const BotSettings& settings, std::optional<std::uint64_t> seed)
    : settings_(settings.validated()), searcher_(settings_.hashMegabytes),
      rng_(seed ? *seed : std::random_device{}()) {}

SearchLimits Bot::limits(std::optional<int> clockMs, int incrementMs) const {
    SearchLimits l;
    l.maxDepth = settings_.maxDepth;
    l.maxNodes = settings_.maxNodes;
    l.moveTimeMs = settings_.moveTimeMs;
    l.stop = stop_;
    if (clockMs) {
        const int budget = timeForMove(*clockMs, incrementMs);
        l.moveTimeMs = l.moveTimeMs > 0 ? std::min(l.moveTimeMs, budget) : budget;
    }
    return l;
}

Move Bot::chooseMove(const Game& game, std::optional<int> clockMs, int incrementMs) {
    last_ = SearchResult{};
    if (game.status().isOver() || game.legalMoves().empty()) return Move();
    const Board& board = game.board();
    const SearchLimits l = limits(clockMs, incrementMs);

    // How far into the game we are, counted from the actual start position.
    const int ply = (board.fullmoveNumber() - 1) * 2 + (board.sideToMove() == Color::Black ? 1 : 0);
    int margin = settings_.randomMargin;
    if (settings_.mistakeChance > 0 && std::uniform_real_distribution<double>(0, 1)(rng_) < settings_.mistakeChance) {
        margin = std::max(margin, settings_.mistakeMargin);
    }
    if (ply < settings_.openingVarietyPlies) margin = std::max(margin, settings_.openingVarietyMargin);

    if (margin == 0) {
        last_ = searcher_.search(board, game.positionHistory(), l, settings_.search);
        return last_.bestMove;
    }

    const int depth = settings_.candidateDepth > 0 ? settings_.candidateDepth : settings_.maxDepth;
    const std::vector<ScoredMove> candidates =
        searcher_.candidateMoves(board, game.positionHistory(), depth, margin, l, settings_.search, &last_);
    if (candidates.empty()) return last_.bestMove;

    // With a forced mate on the board, just play it.
    const int best = candidates.front().score;
    if (search::isMateScore(best)) return candidates.front().move;

    std::vector<double> weights;
    weights.reserve(candidates.size());
    for (const ScoredMove& c : candidates) {
        weights.push_back(std::exp(-static_cast<double>(best - c.score) / settings_.temperature));
    }
    std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
    return candidates[pick(rng_)].move;
}
