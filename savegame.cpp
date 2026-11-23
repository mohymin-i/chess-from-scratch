#include "savegame.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace fs = std::filesystem;

namespace {
std::string envOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

// A name for the temporary file that won't collide with another save in progress.
std::string tempSuffix() {
    static std::atomic<unsigned> counter{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return ".tmp-" + std::to_string(ticks) + "-" + std::to_string(counter++);
}
} // namespace

fs::path appDataDirectory() {
#if defined(_WIN32)
    const std::string appData = envOrEmpty("APPDATA");
    if (appData.empty()) return {};
    return fs::path(appData) / "Chess from Scratch";
#elif defined(__APPLE__)
    const std::string home = envOrEmpty("HOME");
    if (home.empty()) return {};
    return fs::path(home) / "Library" / "Application Support" / "Chess from Scratch";
#else
    const std::string xdg = envOrEmpty("XDG_DATA_HOME");
    if (!xdg.empty() && fs::path(xdg).is_absolute()) return fs::path(xdg) / "chess-from-scratch";
    const std::string home = envOrEmpty("HOME");
    if (home.empty()) return {};
    return fs::path(home) / ".local" / "share" / "chess-from-scratch";
#endif
}

fs::path autosavePath() {
    const fs::path dir = appDataDirectory();
    return dir.empty() ? fs::path() : dir / "autosave.pgn";
}

bool writeFileAtomically(const fs::path& path, std::string_view contents, std::string* error) {
    const auto failWith = [error](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    if (path.empty()) return failWith("no file name given");
    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
        if (ec) return failWith("can't create folder " + path.parent_path().string() + ": " + ec.message());
    }

    fs::path temp = path;
    temp += tempSuffix();
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return failWith("can't create " + temp.string());
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temp, ec);
            return failWith("couldn't write all of " + temp.string() + " (disk full?)");
        }
    }
    fs::rename(temp, path, ec); // replaces the target in one step on macOS and Linux
    if (ec) {
        // Windows won't rename over an existing file. Remove it first; this isn't atomic, but the new file is
        // complete before the old one goes away.
        std::error_code removeError;
        fs::remove(path, removeError);
        ec.clear();
        fs::rename(temp, path, ec);
    }
    if (ec) {
        std::error_code cleanup;
        fs::remove(temp, cleanup);
        return failWith("can't replace " + path.string() + ": " + ec.message());
    }
    return true;
}

std::optional<std::string> readWholeFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) return std::nullopt;
    return buffer.str();
}

bool saveGame(const Game& game, const PgnTags& tags, const fs::path& path, std::string* error) {
    return writeFileAtomically(path, toPgn(game, tags), error);
}

std::optional<SavedGame> loadSavedGame(const fs::path& path, std::string* error) {
    const auto failWith = [error](const std::string& why) -> std::optional<SavedGame> {
        if (error) *error = why;
        return std::nullopt;
    };
    const std::optional<std::string> text = readWholeFile(path);
    if (!text) return failWith("can't read " + path.string());
    try {
        std::vector<PgnGame> games = parsePgn(*text);
        if (games.empty()) return failWith(path.string() + " has no game in it");
        PgnGame& first = games.front();
        return SavedGame{std::move(first.game), std::move(first.tags)};
    } catch (const PgnError& e) {
        return failWith(path.string() + ": " + e.what());
    }
}
