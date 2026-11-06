#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace bb {
class Runtime;

struct InputFrame {
    std::set<std::string> keys;
    bool fire{};
    double aimX{}, aimY{};
    int wheel{};
};

struct ReplayTape {
    std::uint32_t seed{3005};
    std::int64_t epoch{1793966460};
    std::string username{"DX_Player"};
    std::vector<InputFrame> frames;
};

ReplayTape exerciseTape(int ticks, std::uint32_t seed, std::int64_t epoch);
ReplayTape readReplay(const std::filesystem::path& path);
void writeReplay(const std::filesystem::path& path, const ReplayTape& tape,
                 const std::filesystem::path& workspace);
InputFrame sampleInput();

class TraceWriter {
public:
    TraceWriter(const std::filesystem::path& path, const ReplayTape& tape,
                const std::filesystem::path& workspace);
    void write(std::size_t tick, const InputFrame& input, const Runtime& runtime);

private:
    std::ofstream output;
    std::size_t written{};
    void append(const std::string& line);
};
}
