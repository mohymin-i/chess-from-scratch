# Roadmap

_The planned pull requests for this project, in the order they should land. Written 2026-11-23._

**Where things stand:** `chess_core` has bitboards for all 12 piece types, square helpers, place/remove/lookup and a
35-test suite. There are no moves, rules, graphics or engine yet. The CLI version has been scrapped, so the game
will be graphical.

**How to read this:**
- PRs are numbered in a workable order. "Needs" lists the PRs that must land first.
- Size is relative: **S** is small and focused, **M** is a solid feature, **L** is big and could be split further.
- Every PR adds tests and keeps `ctest` passing in both the normal and the sanitizer build (`-DCHESS_SANITIZE=ON`).
- Milestones 3 and 4 (piece sets, fonts, themes) are the customization features. They only need the GUI basics (#22–#24), so they can be built alongside Milestone 5.

## Decisions to make first

| Decision | Needed by | Recommendation |
|---|---|---|
| Graphics library | #22 | **SFML 3** for drawing, PNG loading and TrueType text (FreeType is built in), plus **Dear ImGui** (via ImGui-SFML) for menus, settings and the theme editor. Both fit the spec's "graphics libraries" allowance and install through CMake `FetchContent`. Alternatives: raylib (simpler, C API) or SDL3 (needs separate image and font add-ons). |
| Theme and manifest file format | #33, #39 | Use a JSON library such as nlohmann/json, which the spec allows as a non-chess library, or a hand-written `key = value` parser. JSON handles nested data such as glyph maps better. |
| License | #5 | MIT unless you have a reason otherwise. Bundled fonts and images each need a license that permits redistribution. |
| Opening book | #50 | Defer. See #50 for how it interacts with the spec's rules. |

---

## Milestone 0: Fix and harden the existing code

**#1 Mailbox array for O(1) piece lookup** (S)
`pieceAt` currently scans up to 12 bitboards, and `removePiece` clears all 6 of a color's bitboards because it
doesn't know which piece is on the square. Keep a 64-entry piece array next to the bitboards, so a lookup is one read
and a removal clears exactly one bitboard. Move generation and the GUI both call these constantly.
_Done when:_ `isConsistent()` also checks the array against the bitboards and the random stress test still passes.

**#2 Unchecked fast paths** (S) · needs #1
The public functions throw on a bad square. That's right for external input but wasteful inside move generation,
which will call them millions of times per second. Add internal unchecked versions that assert in debug builds only,
and keep the checked ones as the public API.

**#3 Complete the game-state fields** (S)
`Board` has getters but no setters for side to move, castling rights and the en passant square, and it has no
half-move clock or full-move number at all. FEN (#6) and making moves (#11) need all of them.

**#4 Named square constants** (S)
`Square` is a plain `int`, so any integer compiles where a square is expected. A `Square` enum with named constants
(`A1` … `H8`) makes code and tests easier to read and catches mix-ups between square, file and rank.

**#5 Repo housekeeping** (S)
Add a `LICENSE`, finish or delete `notes.md` (it stops mid-sentence), and update the README's progress section. Add
CI on GitHub Actions that builds and runs the tests on macOS and Linux, normally and with sanitizers, so every later
PR shows pass/fail automatically. Add `.clang-format` and `.clang-tidy` configs and turn on format-on-save in
`.vscode/settings.json`.

---

## Milestone 1: The full rules of chess

**#6 FEN import and export** (M) · needs #3
`Board::fromFen` / `toFen`. Malformed FEN returns an error and never crashes or half-loads a board. Nearly every
later test sets up positions this way.
_Done when:_ known positions survive a round trip and every field rejects bad values.

**#7 Move encoding** (S)
A compact 16-bit move: from (6 bits), to (6 bits) and flags (4 bits) for captures, promotions, en passant, castling
and double pawn pushes. Include conversion to and from UCI strings (`e2e4`, `e7e8q`).

**#8 Attack tables for knights, kings and pawns** (S)
Precompute each piece's attacks from every square. Watch for the classic bitboard bug where a knight on the h-file
"wraps around" to the a-file. Test every edge and corner square.

**#9 Rook, bishop and queen attacks, simple version** (M) · needs #8
Ray loops that stop at the first blocker. Correctness comes first; #19 replaces this with a faster method.

**#10 Pseudo-legal move generation** (L) · needs #7, #9
All piece moves, double pushes, all four promotion choices, en passant and castling. At this stage moves may still
leave your own king in check; #12 filters those out.

**#11 Make and unmake moves** (L) · needs #10
Apply a move, updating bitboards, the mailbox, castling rights, the en passant square, the clocks and side to move.
Undo it from a stack of saved state. Common bugs to test: castling rights must be lost when a rook is *captured* on
its home square, and the en passant square is only set after a double push.
_Done when:_ for every move in many positions, make followed by unmake leaves the board exactly as it was.

**#12 Check detection and legal moves** (M) · needs #11
`isSquareAttacked`, `inCheck`, and removing moves that leave the king in check. Edge cases: you can't castle out
of, through or into check, and an en passant capture can expose your king along the rank when both pawns leave it.

**#13 Perft test suite** (M) · needs #12
Count every position reachable to depth N from the standard test positions (the start position, "Kiwipete" and
positions 3–6 from the Chess Programming Wiki) and compare against the published counts. This is the most effective
way to find move-generation bugs. Include a "divide" mode that splits the count by first move so you can find the
move that's wrong. Shallow depths run in `ctest`; deep ones run under a separate label.

**#14 Zobrist hashing** (M) · needs #11
A 64-bit position fingerprint updated with every move, from random keys generated by your own seeded random number
generator. Repetition detection and the transposition table depend on it.
_Done when:_ after long random games, the incrementally updated hash matches one recomputed from scratch.

**#15 Game-end detection** (M) · needs #12, #14
Checkmate, stalemate, the 50-move rule, threefold repetition (using the hash history) and insufficient material
(K vs K, K + one minor piece vs K, and K+B vs K+B with both bishops on the same color).

**#16 `Game` class with history** (M) · needs #15
Wraps `Board` with a move list, undo/redo, the result, draw offers and resignation. The GUI only ever talks to this.

**#17 Standard algebraic notation (SAN)** (M) · needs #12
Read and write `Nf3`, `exd5`, `O-O`, `e8=Q+`, including disambiguation (`Nbd2`, `R1e1`) and `+`/`#` suffixes.

**#18 PGN import and export** (M) · needs #16, #17
Save and load full games with their tags.

**#19 Magic bitboards (algorithm upgrade)** (L) · needs #13
Replace the slider ray loops (#9) with magic-bitboard lookups. Find your own magic numbers with a search program,
which stays within the spec. (PEXT is the other fast option, but it's an x86-only instruction and Apple Silicon
lacks it.)
_Done when:_ every perft count still matches and a benchmark shows the speedup.

**#20 Fully legal move generation with pin and check masks** (M) · needs #13
An optimization of #12. Compute pinned pieces and the squares that block or capture a checking piece up front, then
generate only legal moves, instead of making and unmaking every pseudo-legal move to test it. Perft must match
exactly, and it should run faster.

---

## Milestone 2: The graphical game

**#21 Pick and integrate the graphics library** (M) · decision above
Opens a window and draws an empty board. Put rendering in a separate `gui` target so `chess_core` stays
graphics-free and testable without a window.

**#22 Board rendering and layout** (M) · needs #21, #16
Squares, pieces drawn with placeholder art, file and rank labels, board flip, resizing with the window, and sharp
rendering on Retina/high-DPI screens.

**#23 Moving pieces with the mouse** (M) · needs #22
Click-to-move and drag-and-drop. Highlights for the selected piece, legal destinations, the last move and a king in
check. Illegal moves snap back.

**#24 Promotion picker and game-over dialog** (S) · needs #23
Choose the promotion piece, plus a dialog showing checkmate, stalemate, draw or resignation with a "new game" button.

**#25 Move list and controls** (M) · needs #23, #17
A move list in SAN where clicking a move jumps to that position, plus buttons for undo/redo, new game, flip board
and resign/offer draw.

**#26 Save, load and share positions** (S) · needs #18
Open and save PGN files, paste a FEN to set up a position, and copy the current FEN to the clipboard.

**#27 Clocks, animation and sound** (M) · needs #23
Optional time controls with increment, pieces that slide instead of jumping, and move/capture/check sounds.

---

## Milestone 3: Piece sets, chess fonts and custom PNG sets

**#28 Piece-set system** (M) · needs #22
A `PieceSet` interface (`draw(piece, rectangle)`) that the board renderer uses for all pieces, so PNG sets and font
sets work the same way. The app looks for sets in two places: bundled `assets/pieces/` and a per-user folder
(`~/Library/Application Support/<app>/pieces` on macOS, `~/.config/<app>/pieces` on Linux). Each set has a
manifest with its name, author, license and type. A broken or missing set falls back to the bundled default with a
warning and never crashes the app.

**#29 PNG piece sets** (M) · needs #28
A folder with 12 PNGs named `wK.png` … `bP.png` plus a manifest. Downscale with smoothing so pieces stay clean at
small sizes, and keep each image's aspect ratio, centered on its square. Validation messages name the exact
problem: a missing file, an image that isn't square, or one without transparency. Loader and validation logic get
tests that don't need a window.

**#30 Sprite-sheet piece sets** (S) · needs #29
The same thing from a single PNG laid out as a 6×2 grid, cut into pieces on load.

**#31 Chess font support (TTF/OTF)** (L) · needs #28
Load chess fonts, whose letters draw pieces instead of characters.
- **Glyph map:** fonts disagree on which character is which piece, so each font's manifest maps its characters to the 12 pieces.
- **The fill problem:** in most chess fonts the white pieces are only outlines, so the square color shows through them. Draw a filled "mask" glyph in the piece color first, then the outline glyph on top, so the map needs both glyphs.
- **Crisp at any size:** font glyphs are vectors, so render each size once and cache it.
- **Licensing:** only bundle fonts whose license allows redistribution, and record the license in the manifest.

**#32 Piece-set picker** (S) · needs #29, #31
A settings screen with a preview of every installed set. Choosing one switches immediately, with no restart.

**#33 Custom piece-set creator** (L) · needs #29, #30
An in-app wizard that builds a piece set from your own PNGs:
- Pick 12 images or one sprite sheet. Pieces are assigned automatically from file names, and you can drag to fix any that land in the wrong place.
- Automatically trim transparent borders, center each piece, pad it to a square and normalize the sizes.
- Preview the set live on a board, on both light and dark squares.
- Get warnings for likely mistakes: very low resolution, or white and black pieces that look nearly identical.
- Name it, add an author, and save it to your user folder as a normal PNG set.
- Import and export sets as a folder or `.zip`, so they can be shared. (`.zip` needs a small library.)

**#34 Export a custom set as a real font file** (L, stretch) · needs #33
Optional. A **color bitmap font** can embed the PNGs as they are, through OpenType's `sbix` table (Apple) or `CBDT`
table (Google), with no image tracing. A traditional vector font would need each PNG traced into outlines, which is
much harder and not recommended. Only worth doing if people want to use the sets outside this app, because PNG sets
already cover in-app use.

---

## Milestone 4: Themes and customization

**#35 Theme model and built-in presets** (M) · needs #22
A `Theme` that holds every color the UI uses:
- light and dark squares, and the highlights for the selected square, last move and a king in check
- legal-move dots, capture rings and arrows
- coordinate labels, the background, panels and text

Each theme also names a default piece set. Ship several presets: classic green, brown/wood, blue, high-contrast and
dark mode. Replace every hard-coded color in the GUI with a theme lookup.

**#36 Theme files** (M) · needs #35, file-format decision
Themes stored as files in the user folder. Unknown keys are ignored, missing keys fall back to the defaults, and
invalid values are reported, never crashed on. The parser gets its own tests and fuzzing (#52).

**#37 Image-based boards** (S) · needs #35, #29
Wood or marble boards from PNGs, either one whole-board image or tiling light and dark square textures. Reuses the
PNG loader from #29.

**#38 Theme editor** (M) · needs #36
Color pickers with a live board preview. Duplicate a preset, edit it, save it under a new name, reset it, and
import or export theme files.

**#39 Settings that persist** (S) · needs #35, #32
Remember the theme, piece set, board orientation, window size and sound between launches. Smaller options belong
here too: coordinates on/off and inside/outside the board, showing legal moves on/off, animation speed, and
drag-and-drop versus click-to-move.

**#40 Accessibility** (S) · needs #38
Color-blind-friendly presets, so highlights don't rely on telling red from green. The theme editor warns when a
highlight has too little contrast against the squares. Add an adjustable UI scale.

---

## Milestone 5: Evaluation engine and bots

**#41 Evaluation v1** (M) · needs #12
Material plus your own piece-square tables, blended between middlegame and endgame values as pieces come off.
_Key test:_ a position and its color-flipped mirror must score exactly opposite, which catches many sign and
orientation bugs.

**#42 Search v1** (M) · needs #41, #15
Negamax with alpha-beta pruning, iterative deepening and quiescence search (so the engine doesn't stop calculating in
the middle of a capture sequence). Score mates by distance so the engine plays the fastest mate.

**#43 Move ordering** (M) · needs #42
Search likely-best moves first: the transposition-table move, then captures ordered by most valuable victim and least
valuable attacker, then killer moves and history scores. Measure the drop in positions searched.

**#44 Transposition table** (M) · needs #42, #14
Cache search results by Zobrist hash. Common bug: mate scores need adjusting for distance from the root when they're
stored and when they're read back.

**#45 Search improvements** (L, one PR each) · needs #43, #44, #48
Null-move pruning, late move reductions, aspiration windows and check extensions. Each one must show a measured gain
in #48 before it's merged.

**#46 Evaluation v2** (L) · needs #41, #48
Pawn structure (doubled, isolated and passed pawns), king safety, mobility, the bishop pair and rooks on open files.
Each term is checked against #48.

**#47 Thinking in the background, with time management** (M) · needs #42, #21
The engine searches on its own thread so the GUI never freezes, can be stopped instantly, and splits its time
sensibly under a clock.

**#48 Engine testing harness** (M) · needs #42
Engine-vs-engine matches from varied openings that report wins, draws and losses with error margins, plus a `bench`
command that measures positions searched per second. Every strength PR has to prove its gain here.

**#49 Difficulty levels and bot personalities** (M) · needs #47
Limit depth, time or nodes, and add controlled randomness and deliberate mistakes at lower levels. Give the bots
names and playing styles, and make the strongest genuinely hard, per the spec's goal.

**#50 Opening book** (M, decision)
Variety in the opening. Options: a small book you build by hand, or one your own code extracts from public game
databases. The spec forbids other people's chess tools, so a book built by your own code from public games is
probably fine, but that's your call.

**#51 Analysis mode** (M) · needs #47, #25
An evaluation bar, the engine's best line, and a hint button during play.

---

## Tooling (any time after #5)

**#52 Fuzz the parsers** (S) · needs #6, #18, #36
Feed random input to the FEN, PGN and theme parsers with libFuzzer and sanitizers to prove bad files can never
crash the app.

**#53 Coverage and benchmarks** (S)
A code-coverage report, plus perft and search speed tracked over time so slowdowns get noticed.

**#54 Packaging and releases** (M) · needs #22
A macOS `.app` with its assets bundled, a Linux build, and a GitHub Actions workflow that publishes releases.

---

## Suggested order

1. **#1–#5:** fix and harden what exists.
2. **#6–#13:** the rules up to passing perft. This is the hardest correctness work and everything depends on it.
3. **#14–#18:** the rest of the rules, plus notation and saved games.
4. **#21–#24:** a playable two-player graphical game, which is the README's current goal. Then **#25–#27**.
5. **#28–#40:** piece sets, chess fonts, custom PNG sets and themes, alongside **#41–#48** for the engine.
6. **#19, #20, #45, #46, #49–#51:** performance work, bot strength and analysis.
7. **#52–#54:** tooling, any time after #5, and releases once the GUI exists.
