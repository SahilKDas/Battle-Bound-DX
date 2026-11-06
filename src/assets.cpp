#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "assets.hpp"
#include <array>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <stdexcept>

namespace bb {
namespace {
struct Entry { std::uint64_t offset{}, size{}; };
std::map<std::string, Entry> entries;
std::filesystem::path executable, fallback;
template<class T> T read(std::ifstream& stream) {
    T value{};
    if (!stream.read(reinterpret_cast<char*>(&value), sizeof(value))) throw std::runtime_error("Truncated asset package");
    return value;
}
}
void openAssetPackage(const std::filesystem::path& fallbackRoot) {
    SetProcessDPIAware();
    fallback = fallbackRoot;
    std::wstring path(32768, L'\0');
    auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("Cannot locate executable");
    path.resize(length); executable = path; entries.clear();
    std::ifstream stream(executable, std::ios::binary | std::ios::ate);
    auto fileSize = static_cast<std::uint64_t>(stream.tellg());
    if (fileSize < 16) return;
    stream.seekg(static_cast<std::streamoff>(fileSize - 16));
    auto start = read<std::uint64_t>(stream);
    std::array<char, 8> magic{}; stream.read(magic.data(), magic.size());
    if (std::string(magic.data(), magic.size()) != "BBDXPAK1") return;
    if (start >= fileSize - 16) throw std::runtime_error("Invalid asset package offset");
    stream.seekg(static_cast<std::streamoff>(start));
    auto count = read<std::uint32_t>(stream);
    if (count > 10000) throw std::runtime_error("Invalid asset count");
    while (count--) {
        auto size = read<std::uint32_t>(stream);
        if (!size || size > 4096) throw std::runtime_error("Invalid asset name");
        std::string name(size, '\0'); stream.read(name.data(), size);
        auto bytes = read<std::uint64_t>(stream);
        auto offset = static_cast<std::uint64_t>(stream.tellg());
        if (offset > fileSize - 16 || bytes > fileSize - 16 - offset || bytes > 100'000'000) throw std::runtime_error("Invalid asset range");
        if (!entries.emplace(name, Entry{offset, bytes}).second) throw std::runtime_error("Duplicate packaged asset");
        stream.seekg(static_cast<std::streamoff>(bytes), std::ios::cur);
    }
}
std::vector<unsigned char> readAsset(const std::string& name) {
    auto found = entries.find(name);
    std::ifstream stream;
    std::uint64_t size{};
    if (found != entries.end()) {
        stream.open(executable, std::ios::binary);
        stream.seekg(static_cast<std::streamoff>(found->second.offset));
        size = found->second.size;
    } else {
        auto utf8 = std::u8string(reinterpret_cast<const char8_t*>(name.data()), name.size());
        stream.open(fallback / std::filesystem::path(utf8), std::ios::binary | std::ios::ate);
        if (!stream) throw std::runtime_error("Missing asset: " + name);
        size = static_cast<std::uint64_t>(stream.tellg()); stream.seekg(0);
    }
    if (size > 100'000'000) throw std::runtime_error("Asset exceeds runtime size limit");
    std::vector<unsigned char> result(static_cast<std::size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(result.data()), static_cast<std::streamsize>(size))) throw std::runtime_error("Cannot read asset: " + name);
    return result;
}
std::size_t packagedAssetCount() { return entries.size(); }
void showStartupError(const std::string& message) { MessageBoxA(nullptr, message.c_str(), "Battle Bound DX", MB_OK | MB_ICONERROR); }
FontAtlas makeFontAtlas() {
    FontAtlas atlas;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = atlas.width;
    info.bmiHeader.biHeight = -atlas.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap || !pixels) throw std::runtime_error("Cannot create font atlas");
    auto previousBitmap = SelectObject(dc, bitmap);
    HFONT font = CreateFontW(-48, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    auto previousFont = SelectObject(dc, font);
    SetBkColor(dc, RGB(0, 0, 0));
    SetTextColor(dc, RGB(255, 255, 255));
    std::fill_n(static_cast<unsigned char*>(pixels), atlas.width * atlas.height * 4, 0);
    for (wchar_t c = 32; c < 127; ++c) {
        int index = c - 32;
        TextOutW(dc, index % 16 * 64, index / 16 * 64, &c, 1);
        SIZE size{}; GetTextExtentPoint32W(dc, &c, 1, &size);
        atlas.advances.push_back(size.cx);
    }
    GdiFlush();
    atlas.pixels.resize(atlas.width * atlas.height * 4);
    auto* source = static_cast<unsigned char*>(pixels);
    for (std::size_t i = 0; i < atlas.pixels.size(); i += 4) {
        atlas.pixels[i] = atlas.pixels[i + 1] = atlas.pixels[i + 2] = 255;
        atlas.pixels[i + 3] = source[i];
    }
    SelectObject(dc, previousFont); SelectObject(dc, previousBitmap);
    DeleteObject(font); DeleteObject(bitmap); DeleteDC(dc);
    return atlas;
}
}
