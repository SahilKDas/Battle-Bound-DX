#include "preferences.hpp"
#include "input.hpp"
#include <cstdlib>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace bb {
std::filesystem::path preferencesPath() {
    if (const auto* local = _wgetenv(L"LOCALAPPDATA"); local && *local)
        return std::filesystem::path(local) / "Battle Bound DX" / "settings.txt";
    return {};
}

Preferences loadPreferences(const std::filesystem::path& path) {
    Preferences values;
    std::ifstream file(path);
    std::string version;
    if (!(file >> version) || version != "BBDX_SETTINGS_1") return values;
    Preferences saved;
    if (!(file >> saved.fps >> saved.vsync >> saved.samples >> saved.maximized >> saved.volume) ||
        saved.fps < 0 || saved.fps > 1000 || saved.volume < 0 || saved.volume > 100 ||
        (saved.samples != 1 && saved.samples != 2 && saved.samples != 4)) return values;
    for (std::size_t i = 0; i < saved.controls.size(); ++i) {
        if (!(file >> std::quoted(saved.controls[i]))) return values;
        if (i == 9 || i == 10) {
            if (saved.controls[i] != values.controls[i]) return values;
        } else if (!legacyKey(saved.controls[i]) && !(i == 4 && saved.controls[i] == "mouse")) {
            return values;
        }
    }
    return saved;
}

void savePreferences(const std::filesystem::path& path, const Preferences& values) {
    if (path.empty()) throw std::runtime_error("Settings folder is unavailable");
    std::filesystem::create_directories(path.parent_path());
    auto temporary = path;
    temporary += ".tmp";
    std::ofstream file(temporary, std::ios::trunc);
    file << "BBDX_SETTINGS_1\n" << values.fps << ' ' << values.vsync << ' ' << values.samples
         << ' ' << values.maximized << ' ' << values.volume << '\n';
    for (const auto& control : values.controls) file << std::quoted(control) << '\n';
    file.close();
    if (!file) throw std::runtime_error("Could not write settings");
    std::filesystem::rename(temporary, path);
}
void testPreferences() {
    const auto token = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto directory = std::filesystem::temp_directory_path() / ("bbdx-settings-test-" + std::to_string(token));
    if (!std::filesystem::create_directory(directory)) throw std::runtime_error("Cannot create settings test directory");
    const auto path = directory / "settings.txt";
    const auto cleanup = [&] {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        std::filesystem::remove(directory / "settings.txt.tmp", ignored);
        std::filesystem::remove(directory, ignored);
    };
    try {
        Preferences expected;
        if (loadPreferences(path) != expected) throw std::runtime_error("Missing settings did not use defaults");
        expected.fps = 144;
        expected.vsync = expected.maximized = true;
        expected.samples = 2;
        expected.volume = 40;
        expected.controls[0] = "up arrow";
        savePreferences(path, expected);
        if (loadPreferences(path) != expected) throw std::runtime_error("Settings round-trip failed");
        expected.volume = 80;
        savePreferences(path, expected);
        if (loadPreferences(path) != expected) throw std::runtime_error("Settings replacement failed");
        std::ofstream(path, std::ios::trunc) << "BBDX_SETTINGS_1\n144 1 18";
        if (loadPreferences(path) != Preferences{}) throw std::runtime_error("Corrupt settings did not use defaults");
    } catch (...) {
        cleanup();
        throw;
    }
    cleanup();
}

}
