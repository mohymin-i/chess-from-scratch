# Bots

Six built-in computer opponents, from `src/bot.h`. All of them run the same engine: an alpha-beta search with
quiescence search, a transposition table and the usual pruning (see `src/search.h`), plus a hand-tuned evaluation
(`src/eval.h`). None of them is a "grab the biggest capture" greedy player. Even the weakest looks three moves ahead
and resolves every capture sequence, so it won't leave pieces hanging and will punish yours.

What makes the lower levels beatable is that they deliberately settle for a *good* move instead of the *best* one,
and they don't search as deep.

| Level | Name    | Style                                   | Thinking                           | Rough target |
|-------|---------|-----------------------------------------|------------------------------------|--------------|
| 1     | Pebble  | Solid and practical; punishes loose pieces | 3 plies, picks within ~70 cp of best | ~1100 |
| 2     | Juniper | Sensible development, small inaccuracies | 4 plies, within ~45 cp              | ~1350 |
| 3     | Corvid  | Tactical, values active pieces          | 6 plies, within ~25 cp              | ~1600 |
| 4     | Bastion | Positional: pawn structure and king safety | 0.5 s a move, within ~10 cp       | ~1850 |
| 5     | Tempest | Aggressive: attacks the king, avoids draws | 1.2 s a move, best move only      | ~2100 |
| 6     | Zenith  | Full strength                           | 3 s a move, best move only          | ~2300 |

The ratings are targets for tuning, not measurements against people. What *has* been measured: in bot-vs-bot
games from varied openings, every level beats the one below it (`tests/bot_matches.cpp` checks levels 1 to 3 on
every CI run; levels 4 to 6 were checked by hand at reduced time controls).

## Why the weakest bot still gives new players a game

- **It searches, it doesn't guess.** Three plies plus quiescence means it sees "if I take, you take back", forks,
  pins, back-rank mates and most two-move tactics.
- **It never blunders material on purpose.** Its randomness only chooses among moves within about 70 centipawns of
  the best, occasionally (7% of moves) up to 180. That's at most "a bit worse", never "hangs a piece", which costs
  300 or more.
- **It always plays a forced mate** when it finds one, and always takes a free piece.
- **Its imperfect moves look human.** Research on human-like engines found that a strong engine weakened with
  random noise doesn't play like a weaker person, and players complain about bots that "blunder out of nothing".
  So when Pebble, Juniper and Corvid pick a near-best move, they favor natural ones. Measured over 30 seeded
  Pebble openings, this halves king walks and edge-pawn pushes and adds more development and castling, with no
  change in strength (10/20 against the plain version).

`tests/engine_tests.cpp` (`bot_weakest_level_is_still_a_challenge`) checks this across ten random seeds: Pebble
always takes a free queen, always mates in one, finds a quiet knight fork, never grabs a pawn that loses its queen,
and saves its own attacked queen.

## Settings

Every bot is just a `BotSettings` struct, so a custom bot is a built-in one with a few fields changed:

```cpp
BotSettings mine = *findBot("Corvid");
mine.name = "Corvid (no randomness)";
mine.randomMargin = 0;
mine.search.weights.kingSafety = 150;
Bot bot(mine);
Move m = bot.chooseMove(game);
```

| Field | Meaning |
|---|---|
| `maxDepth`, `maxNodes`, `moveTimeMs` | Search limits. Whichever is hit first stops the search; `0` means no limit for nodes and time. |
| `randomMargin`, `temperature` | Candidate moves are those within `randomMargin` centipawns of the best. Each is picked with weight `exp(-(best - score) / temperature)`, so better moves are still more likely. `0` always plays the best move. |
| `mistakeChance`, `mistakeMargin` | With this probability the margin widens for one move, like a human lapse. |
| `candidateDepth` | Search depth used when choosing among candidates (defaults to `maxDepth`). |
| `humanLikeChoice` | When settling for a less-than-best move, prefer natural ones (recaptures, development, castling, checks) over aimless king walks, early edge-pawn pushes, early queen trips and moving a piece twice. On for levels 1-3. |
| `openingVarietyPlies`, `openingVarietyMargin` | Lets even full-strength bots vary their first few moves so games don't repeat. |
| `search.weights` | Percent scales for evaluation terms: `material`, `pieceSquare`, `mobility`, `kingSafety`, `pawnStructure`, `passedPawns`. This is where the personalities come from. |
| `search.contempt` | How many centipawns worse than equal a draw is for the bot. Positive values make it avoid repetitions. |
| `search.nullMovePruning`, `lateMoveReductions`, `futilityPruning` | Search features. They're on by default and only worth turning off for testing. |
| `hashMegabytes` | Transposition table size. |

`BotSettings::validated()` clamps every field into a working range, so a hand-edited bot can't break the engine.
A bot with no limits at all gets a 1-second time limit.

## Playing with a clock

`bot.chooseMove(game, remainingMs, incrementMs)` keeps the bot within a sensible share of its clock
(`timeForMove`: about 1/30 of the remaining time plus most of the increment, never more than a quarter of what's
left). A bot never thinks longer than its own `moveTimeMs`.

The lower levels answer almost instantly. A GUI should add a short artificial delay so they feel natural.

## Using it from a GUI

- `Bot::setStopFlag(&flag)` lets another thread end a search early. The bot then plays the best move found so far.
  The first search iteration always finishes, so there is always a legal move to play.
- `Bot::lastSearch()` gives the score, depth, node count and expected line, for an evaluation bar or hint display.
- `Bot::newGame()` clears what the bot learned from the previous game.
