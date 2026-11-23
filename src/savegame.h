// Saving and restoring games on disk, so a desktop app never loses a game: the game in progress is saved after
// every move and restored on the next launch, even after a crash.
//
// Saves are plain PGN (with think times), so they can also be opened by any other chess program.
#ifndef CHESS_FROM_SCRATCH_SAVEGAME_H
#define CHESS_FROM_SCRATCH_SAVEGAME_H

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "pgn.h"

// The per-user folder for the app's files:
//   macOS:   ~/Library/Application Support/Chess from Scratch
//   Linux:   $XDG_DATA_HOME/chess-from-scratch, or ~/.local/share/chess-from-scratch
//   Windows: %APPDATA%\Chess from Scratch
// Returns an empty path if the home folder can't be determined. Doesn't create the folder.
std::filesystem::path appDataDirectory();

// Writes the whole file or nothing: the data goes to a temporary file in the same folder, which then replaces the
// target. If the app crashes or the disk fills up halfway, the previous file is untouched. Creates missing parent
// folders. On failure returns false and, if `error` is given, says why.
bool writeFileAtomically(const std::filesystem::path& path, std::string_view contents, std::string* error = nullptr);

// The file's contents, or nullopt if it can't be read.
std::optional<std::string> readWholeFile(const std::filesystem::path& path);

struct SavedGame {
    Game game;
    PgnTags tags;
};

// Saves one game as PGN. `tags` can hold anything the app wants back later (player names, which bot, time
// control); they're stored as PGN tags.
bool saveGame(const Game& game, const PgnTags& tags, const std::filesystem::path& path, std::string* error = nullptr);

// Loads the first game in a PGN file. Returns nullopt for a missing, unreadable or corrupt file, with the reason
// in `error`. A corrupt save never throws, so a bad file can't stop the app from starting.
std::optional<SavedGame> loadSavedGame(const std::filesystem::path& path, std::string* error = nullptr);

// The autosave file inside appDataDirectory().
std::filesystem::path autosavePath();

#endif // CHESS_FROM_SCRATCH_SAVEGAME_H
