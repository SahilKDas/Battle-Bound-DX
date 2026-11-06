#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace bb {
void openAssetPackage(const std::filesystem::path& fallbackRoot);
std::vector<unsigned char> readAsset(const std::string& name);
std::size_t packagedAssetCount();
void showStartupError(const std::string& message);
struct FontAtlas {
    int width{1024}, height{384};
    std::vector<unsigned char> pixels;
    std::vector<int> advances;
};
FontAtlas makeFontAtlas();
}
