#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace bb {
void openAssetPackage(const std::filesystem::path& fallbackRoot);
std::vector<unsigned char> readAsset(const std::string& name);
std::size_t packagedAssetCount();
void showStartupError(const std::string& message);
constexpr int fontAtlasColumns = 16;
constexpr int fontAtlasPadding = 2;
constexpr int fontAtlasCell(int glyphSize) { return glyphSize * 4 / 3 + 2 * fontAtlasPadding; }
struct FontAtlas {
    int width{}, height{};
    std::vector<unsigned char> pixels;
    std::vector<int> advances;
};
FontAtlas makeFontAtlas(int glyphSize);
}
