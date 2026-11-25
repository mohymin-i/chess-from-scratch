#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>

#include "savegame.h"
#include "test_framework.h"

namespace fs = std::filesystem;

namespace {
// A fresh folder under the system temp directory, removed when the test ends.
class TempDir {
public:
    TempDir() {
        std::random_device rd;
        path_ = fs::temp_directory_path() / ("chess-save-test-" + std::to_string(rd()) + std::to_string(rd()));
        fs::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

int filesIn(const fs::path& dir) {
    int n = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(dir)) ++n;
    return n;
}

#if !defined(_WIN32)
// Sets an environment variable for the rest of the scope, then puts the old value back.
class EnvOverride {
public:
    EnvOverride(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name)) old_ = old;
        if (value) {
            setenv(name, value, 1);
        } else {
            unsetenv(name);
        }
    }
    ~EnvOverride() {
        if (old_) {
            setenv(name_, old_->c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }
    EnvOverride(const EnvOverride&) = delete;
    EnvOverride& operator=(const EnvOverride&) = delete;

private:
    const char* name_;
    std::optional<std::string> old_;
};
#endif
} // namespace

TEST(savegame_round_trip) {
    TempDir dir;
    Game game(Board::fromFen("r3k2r/pppq1ppp/2n5/3p4/3P4/2N5/PPPQ1PPP/R3K2R w KQkq - 4 9"));
    CHECK(game.playUci("e1c1", 2'500));
    CHECK(game.playUci("e8g8", 400));
    const fs::path file = dir.path() / "nested" / "folder" / "game.pgn"; // missing folders get created
    std::string error;
    CHECK(saveGame(game, {{"White", "Me"}, {"Black", "Pebble"}, {"BotLevel", "1"}}, file, &error));
    CHECK(error.empty());

    const std::optional<SavedGame> loaded = loadSavedGame(file, &error);
    CHECK(loaded.has_value());
    CHECK(loaded->game.board() == game.board());
    CHECK(loaded->game.startingBoard() == game.startingBoard());
    CHECK(loaded->game.moves() == game.moves());
    CHECK(loaded->game.moveTimes() == game.moveTimes());
    bool foundLevel = false;
    for (const auto& [name, value] : loaded->tags) foundLevel |= name == "BotLevel" && value == "1";
    CHECK(foundLevel);
}

TEST(savegame_keeps_finished_games_finished) {
    TempDir dir;
    Game resigned;
    CHECK(resigned.playUci("e2e4"));
    resigned.resign(Color::White);
    const fs::path file = dir.path() / "resigned.pgn";
    CHECK(saveGame(resigned, {}, file));
    const std::optional<SavedGame> loaded = loadSavedGame(file);
    CHECK(loaded && loaded->game.status() == resigned.status());

    Game ongoing;
    CHECK(ongoing.playUci("d2d4"));
    CHECK(saveGame(ongoing, {}, file)); // overwrite
    const std::optional<SavedGame> again = loadSavedGame(file);
    CHECK(again && !again->game.status().isOver());
    CHECK_EQ(static_cast<int>(again->game.moves().size()), 1);
}

TEST(savegame_writes_are_atomic) {
    TempDir dir;
    const fs::path file = dir.path() / "save.pgn";
    for (int i = 0; i < 20; ++i) {
        CHECK(writeFileAtomically(file, "version " + std::to_string(i)));
    }
    CHECK_EQ(*readWholeFile(file), std::string("version 19"));
    CHECK_EQ(filesIn(dir.path()), 1); // no temporary files left behind
    CHECK(writeFileAtomically(file, ""));
    CHECK_EQ(*readWholeFile(file), std::string(""));
}

TEST(savegame_failures_are_reported_not_thrown) {
    TempDir dir;
    std::string error;
    CHECK(!loadSavedGame(dir.path() / "missing.pgn", &error).has_value());
    CHECK(error.find("can't read") != std::string::npos);

    const fs::path corrupt = dir.path() / "corrupt.pgn";
    CHECK(writeFileAtomically(corrupt, "[Event \"x\"]\n1. e4 e5 2. Kxe8 *\n"));
    CHECK(!loadSavedGame(corrupt, &error).has_value());
    CHECK(error.find("not a legal move") != std::string::npos);

    const fs::path empty = dir.path() / "empty.pgn";
    CHECK(writeFileAtomically(empty, "\n\n"));
    CHECK(!loadSavedGame(empty, &error).has_value());
    CHECK(error.find("no game") != std::string::npos);

    // A folder can't be written over as if it were a file.
    const fs::path folder = dir.path() / "a-folder";
    fs::create_directories(folder / "inside");
    CHECK(!writeFileAtomically(folder, "data", &error));
    CHECK(!error.empty());
    CHECK(!writeFileAtomically("", "data", &error));
    // The failed write didn't leave a temp file next to the folder.
    for (const auto& entry : fs::directory_iterator(dir.path())) {
        CHECK(entry.path().filename().string().find(".tmp-") == std::string::npos);
    }
}

#if !defined(_WIN32)
TEST(savegame_app_data_directory) {
    {
        EnvOverride home("HOME", "/Users/someone");
        EnvOverride xdg("XDG_DATA_HOME", nullptr);
#if defined(__APPLE__)
        CHECK_EQ(appDataDirectory(), fs::path("/Users/someone/Library/Application Support/Chess from Scratch"));
#else
        CHECK_EQ(appDataDirectory(), fs::path("/Users/someone/.local/share/chess-from-scratch"));
        EnvOverride xdgSet("XDG_DATA_HOME", "/data");
        CHECK_EQ(appDataDirectory(), fs::path("/data/chess-from-scratch"));
        EnvOverride xdgRelative("XDG_DATA_HOME", "relative/path"); // the spec says to ignore relative paths
        CHECK_EQ(appDataDirectory(), fs::path("/Users/someone/.local/share/chess-from-scratch"));
#endif
        CHECK_EQ(autosavePath().filename(), fs::path("autosave.pgn"));
    }
    {
        EnvOverride noHome("HOME", nullptr);
        EnvOverride noXdg("XDG_DATA_HOME", nullptr);
        CHECK(appDataDirectory().empty());
        CHECK(autosavePath().empty());
    }
}
#endif

// The app restores the autosave on launch and keeps passing its tags (player names, bot) to later saves.
TEST(savegame_tags_from_a_loaded_save_are_safe_to_reuse) {
    TempDir dir;
    const fs::path file = dir.path() / "autosave.pgn";
    Game game;
    CHECK(game.playUci("e2e4"));
    CHECK(saveGame(game, {{"White", "Me"}}, file));
    std::optional<SavedGame> loaded = loadSavedGame(file);
    CHECK(loaded.has_value());
    loaded->game.resign(Color::Black); // the stale Result "*" in the tags must not bring this back to life
    CHECK(saveGame(loaded->game, loaded->tags, file));
    const std::optional<SavedGame> again = loadSavedGame(file);
    CHECK(again && again->game.status() == (GameStatus{GameResult::WhiteWins, GameEndReason::Resignation}));
    bool keptName = false;
    for (const auto& [name, value] : again->tags) keptName |= name == "White" && value == "Me";
    CHECK(keptName);

    // Tags from a game that started from a custom position, reused for a normal game: no stale FEN.
    Game custom(Board::fromFen("4k3/8/8/8/8/8/8/R3K3 w Q - 0 1"));
    CHECK(custom.playUci("a1a7"));
    CHECK(saveGame(custom, {}, file));
    const std::optional<SavedGame> fromCustom = loadSavedGame(file);
    Game fresh;
    CHECK(fresh.playUci("d2d4"));
    CHECK(saveGame(fresh, fromCustom->tags, file));
    const std::optional<SavedGame> freshBack = loadSavedGame(file);
    CHECK(freshBack && freshBack->game.startingBoard() == Board() && freshBack->game.moves() == fresh.moves());
}
