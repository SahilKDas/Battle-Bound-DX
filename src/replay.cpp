#include "replay.hpp"
#include "content.hpp"
#include "graphics.hpp"
#include "runtime.hpp"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace bb {
namespace {
constexpr std::size_t maxFrames = 10000;
constexpr std::size_t maxTraceBytes = 16'000'000;
constexpr std::size_t maxReplayBytes = 2'000'000;

void reserveOutput(const std::filesystem::path& path, const std::filesystem::path& workspace,
                   std::uintmax_t reservation) {
    auto root = std::filesystem::weakly_canonical(workspace);
    auto target = std::filesystem::weakly_canonical(path);
    auto relative = target.lexically_relative(root);
    if (!relative.empty() && *relative.begin() != "..") {
        std::uintmax_t total = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
            if (entry.is_regular_file()) total += entry.file_size();
            if (total + reservation >= 450'000'000) {
                throw std::runtime_error("Workspace reservation would exceed 450 MB guard");
            }
        }
    }
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
}

std::string json(std::string_view value) {
    std::ostringstream result;
    result << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') result << '\\' << c;
        else if (c < 32) result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
        else result << c;
    }
    result << '"';
    return result.str();
}

bool validKey(const std::string& key) {
    return (key.size() == 1 && key[0] >= '!' && key[0] <= '~' && key[0] != ';') ||
           key == "space" || key == "up arrow" || key == "down arrow" ||
           key == "left arrow" || key == "right arrow" || key == "enter";
}

std::string keyString(const InputFrame& frame) {
    std::string text;
    for (const auto& key : frame.keys) {
        if (!text.empty()) text += ';';
        text += key;
    }
    return text;
}

void validate(const InputFrame& frame) {
    if (!std::isfinite(frame.aimX) || !std::isfinite(frame.aimY) ||
        std::abs(frame.aimX) > 320 || std::abs(frame.aimY) > 180 ||
        frame.wheel < -1 || frame.wheel > 1) {
        throw std::runtime_error("Invalid recorded pointer input");
    }
    for (const auto& key : frame.keys) {
        if (!validKey(key)) throw std::runtime_error("Unsupported replay key: " + key);
    }
}

void checkExtension(const std::filesystem::path& path, const char* required) {
    if (path.extension() != required) {
        throw std::runtime_error(std::string("Output must have extension ") + required);
    }
}
}

ReplayTape exerciseTape(int ticks, std::uint32_t seed, std::int64_t epoch) {
    if (ticks < 1 || ticks > static_cast<int>(maxFrames)) {
        throw std::runtime_error("Replay length must be 1..10000 frames");
    }
    ReplayTape tape;
    tape.seed = seed; tape.epoch = epoch;
    for (int tick = 0; tick < ticks; ++tick) {
        InputFrame frame;
        if (tick < 80) frame.keys.insert("d");
        if (tick >= 80 && tick < 160) frame.keys.insert("a");
        if (tick >= 30 && tick < 35) frame.keys.insert("w");
        if (tick >= 180 && tick < 210) frame.keys.insert("s");
        if (tick >= 210 && tick < 220) frame.keys.insert("r");
        frame.fire = tick >= 60 && tick < 180;
        frame.aimX = 120;
        tape.frames.push_back(frame);
    }
    return tape;
}

ReplayTape readReplay(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > maxReplayBytes) {
        throw std::runtime_error("Replay exceeds 2 MB limit");
    }
    std::ifstream input(path);
    input.imbue(std::locale::classic());
    std::string magic, label, source;
    unsigned int version{};
    ReplayTape tape;
    std::uint64_t seed{};
    std::size_t frames{};
    if (!(input >> magic >> version) || magic != "BBDX_REPLAY" || version != 1 ||
        !(input >> label >> seed) || label != "seed" || seed > UINT32_MAX ||
        !(input >> label >> tape.epoch) || label != "epoch" ||
        !(input >> label >> std::quoted(tape.username)) || label != "username" ||
        !(input >> label >> source) || label != "source" || source != content::sourceFingerprint() ||
        !(input >> label >> frames) || label != "frames" || frames == 0 || frames > maxFrames) {
        throw std::runtime_error("Invalid or incompatible replay header");
    }
    tape.seed = static_cast<std::uint32_t>(seed);
    for (std::size_t index = 0; index < frames; ++index) {
        InputFrame frame;
        std::size_t tick{};
        int fire{};
        std::string keys;
        if (!(input >> tick >> std::quoted(keys) >> fire >> frame.aimX >> frame.aimY >> frame.wheel) ||
            tick != index || (fire != 0 && fire != 1)) {
            throw std::runtime_error("Invalid replay frame " + std::to_string(index));
        }
        frame.fire = fire != 0;
        std::istringstream list(keys);
        for (std::string key; std::getline(list, key, ';');) {
            if (!frame.keys.insert(key).second) throw std::runtime_error("Duplicate replay key");
        }
        validate(frame);
        tape.frames.push_back(std::move(frame));
    }
    input >> std::ws;
    if (!input.eof()) throw std::runtime_error("Unexpected data after replay frames");
    return tape;
}

void writeReplay(const std::filesystem::path& path, const ReplayTape& tape,
                 const std::filesystem::path& workspace) {
    checkExtension(path, ".replay");
    if (tape.frames.empty() || tape.frames.size() > maxFrames) {
        throw std::runtime_error("Recording length must be 1..10000 frames");
    }
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(17) << "BBDX_REPLAY 1\nseed " << tape.seed << "\nepoch " << tape.epoch
         << "\nusername " << std::quoted(tape.username) << "\nsource " << content::sourceFingerprint()
         << "\nframes " << tape.frames.size() << '\n';
    for (std::size_t i = 0; i < tape.frames.size(); ++i) {
        const auto& frame = tape.frames[i];
        validate(frame);
        text << i << '\t' << std::quoted(keyString(frame)) << '\t' << frame.fire << '\t'
             << frame.aimX << '\t' << frame.aimY << '\t' << frame.wheel << '\n';
    }
    auto data = text.str();
    if (data.size() > maxReplayBytes) throw std::runtime_error("Recording exceeds 2 MB limit");
    reserveOutput(path, workspace, maxReplayBytes);
    std::ofstream output(path, std::ios::binary);
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!output) throw std::runtime_error("Cannot write replay");
}

TraceWriter::TraceWriter(const std::filesystem::path& path, const ReplayTape& tape,
                         const std::filesystem::path& workspace) {
    checkExtension(path, ".jsonl");
    reserveOutput(path, workspace, maxTraceBytes);
    output.open(path, std::ios::binary);
    if (!output) throw std::runtime_error("Cannot create gameplay trace");
    append("{\"type\":\"header\",\"schema\":1,\"stepHz\":30,\"seed\":" +
           std::to_string(tape.seed) + ",\"epoch\":" + std::to_string(tape.epoch) +
           ",\"username\":" + json(tape.username) + ",\"sourceSha256\":" +
           json(content::sourceFingerprint()) + "}");
}

void TraceWriter::append(const std::string& line) {
    if (written + line.size() + 1 > maxTraceBytes) throw std::runtime_error("Trace exceeds 16 MB limit");
    output << line << '\n';
    written += line.size() + 1;
    if (!output) throw std::runtime_error("Cannot write gameplay trace");
}

void TraceWriter::write(std::size_t tick, const InputFrame& input, const Runtime& runtime) {
    std::ostringstream line;
    line.imbue(std::locale::classic());
    line << std::setprecision(17) << "{\"type\":\"tick\",\"tick\":" << tick
         << ",\"time\":" << runtime.simulationTime() << ",\"keys\":" << json(keyString(input))
         << ",\"fire\":" << (input.fire ? "true" : "false")
         << ",\"aim\":[" << input.aimX << ',' << input.aimY << "],\"wheel\":" << input.wheel;
    for (const auto& [label, original] : {
             std::pair{"x", "::Player X"}, {"y", "::Player Y"}, {"vx", "::sx"}, {"vy", "::sy"},
             {"health", "::Spiller-liv"}, {"ammo", "::Ammonisjon"}, {"animation", "::Animation"},
             {"action", "::[Spiller-action]"}, {"weapon", "::Våpen holdt"}, {"scene", ";SCENE"},
             {"mode", ";Spill-Modus"}, {"team", ";Ditt Lag"}, {"points", "::Player game-points"}}) {
        line << ',' << json(label) << ':' << json(runtime.namedValue(original).text);
    }
    if (const auto* match = runtime.networkMatch()) {
        const char* winner = match->phase != MatchPhase::Finished ? "" :
            match->hostScore == match->guestScore ? "draw" : match->hostScore > match->guestScore ? "host" : "guest";
        line << ",\"match\":{\"round\":" << match->round << ",\"map\":" << match->map
             << ",\"phase\":" << static_cast<unsigned>(match->phase) << ",\"ticksLeft\":" << match->ticksLeft
             << ",\"hostScore\":" << match->hostScore << ",\"guestScore\":" << match->guestScore
             << ",\"hostReady\":" << (match->hostReady ? "true" : "false")
             << ",\"guestReady\":" << (match->guestReady ? "true" : "false")
             << ",\"winner\":" << json(winner) << '}';
    }
    auto item = content::findItem(runtime.namedValue("::Våpen holdt").text);
    line << ",\"itemName\":" << json(item ? content::item(*item).name : "unknown")
         << ",\"rngDraws\":" << runtime.randomDrawCount() << ",\"bulletClones\":"
         << runtime.bulletClones << ",\"actors\":" << runtime.actors.size()
         << ",\"tasks\":" << runtime.taskCount() << ",\"events\":[";
    bool first = true;
    for (const auto& event : runtime.gameplayEvents) {
        if (!first) line << ',';
        first = false;
        line << "{\"kind\":" << json(event.kind) << ",\"sprite\":" << json(event.sprite)
             << ",\"block\":" << json(event.block) << ",\"before\":" << json(event.before)
             << ",\"after\":" << json(event.after) << '}';
    }
    line << "]}";
    append(line.str());
}

void TraceWriter::writeSystems(std::size_t tick, const Runtime& runtime) {
    const auto& camera = runtime.camera().state();
    const auto& input = runtime.playerInput();
    std::ostringstream line;
    line.imbue(std::locale::classic());
    line << std::setprecision(17) << "{\"type\":\"systems-tick\",\"tick\":" << tick
         << ",\"camera\":[" << camera.position.x << ',' << camera.position.y
         << "],\"grid\":[" << camera.gridOrigin.x << ',' << camera.gridOrigin.y
         << "],\"shake\":" << camera.shakeMagnitude << ",\"horizontal\":" << input.horizontal
         << ",\"aimStage\":[" << input.aimStage.x << ',' << input.aimStage.y
         << "],\"aimWorld\":[" << input.aimWorld.x << ',' << input.aimWorld.y << "],\"actions\":[";
    bool first = true;
    for (const auto& action : input.actions) {
        if (!first) line << ',';
        first = false;
        line << '[' << action.held << ',' << action.pressed << ',' << action.released << ']';
    }
    line << "]}";
    append(line.str());
}
}
