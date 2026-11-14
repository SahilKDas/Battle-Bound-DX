#pragma once
#include <array>
#include <filesystem>
#include <string>

namespace bb {
struct Preferences {
    int fps{60};
    bool vsync{};
    int samples{4};
    bool maximized{};
    int volume{};
    // Match the original control-list indices for keyboard and mouse actions.
    std::array<std::string, 15> controls{
        "w", "a", "d", "s", "mouse", "space", "r", "t", "m",
        "scroll wheel up", "scroll wheel down", "1", "2", "3", "q"
    };
    bool operator==(const Preferences&) const = default;
};

std::filesystem::path preferencesPath();
Preferences loadPreferences(const std::filesystem::path& path);
void testPreferences();
void savePreferences(const std::filesystem::path& path, const Preferences& values);
}
