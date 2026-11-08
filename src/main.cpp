#include "game_ui.hpp"
#include "runtime.hpp"
#include "assets.hpp"
#include "graphics.hpp"
#include "content.hpp"
#include "replay.hpp"
#include "platform_input.hpp"
#include <iostream>
#include <fstream>
#include <charconv>

namespace {
template<class Integer>
Integer parseInteger(const char* text) {
    const std::string_view value(text);
    Integer result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        throw std::runtime_error("Invalid integer argument: " + std::string(value));
    }
    return result;
}
}

int main(int argc, char** argv) {
    try {
        std::filesystem::path root = std::filesystem::current_path();
        bool headless = false, game = false, unmuted = false, renderSmoke = false, exercise = false;
        int ticks = 300;
        std::uint32_t seed = 3005;
        std::int64_t epoch = 1793966460;
        bool deterministic = false, clockOverride = false, showCredits = false;
        bool arsenalCheck = false, opponentCheck = false, legacyCombat = false, legacyMovement = false, legacyCamera = false, cameraCheck = false;
        std::filesystem::path replayPath, recordPath, tracePath, systemsPath;
        std::string username = "DX_Player";
        std::string peerHost, equip;
        unsigned peerPort = 0;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--self-test") return bb::selfTest();
            if (arg == "--root" && i + 1 < argc) root = argv[++i];
            else if (arg == "--headless") headless = true;
            else if (arg == "--game") game = true;
            else if ((arg == "--host" || arg == "--join") && i + 1 < argc) {
                if (peerPort) throw std::runtime_error("Choose one multiplayer endpoint");
                if (arg == "--join") {
                    if (i + 2 >= argc) throw std::runtime_error("Join requires an IPv4 address and port");
                    peerHost = argv[++i];
                    if (peerHost.empty()) throw std::runtime_error("Missing multiplayer host");
                }
                peerPort = parseInteger<unsigned>(argv[++i]);
                if (!peerPort || peerPort > 65535) throw std::runtime_error("Invalid multiplayer port");
                game = true;
            }
            else if (arg == "--equip" && i + 1 < argc) equip = argv[++i];
            else if (arg == "--audio") unmuted = true;
            else if (arg == "--render-smoke") renderSmoke = true;
            else if (arg == "--exercise") { exercise = true; game = true; }
            else if (arg == "--ticks" && i + 1 < argc) ticks = parseInteger<int>(argv[++i]);
            else if (arg == "--seed" && i + 1 < argc) {
                seed = parseInteger<std::uint32_t>(argv[++i]);
                deterministic = true; clockOverride = true;
            }
            else if (arg == "--epoch" && i + 1 < argc) { epoch = parseInteger<std::int64_t>(argv[++i]); deterministic = true; clockOverride = true; }
            else if (arg == "--replay" && i + 1 < argc) { replayPath = argv[++i]; headless = true; game = true; deterministic = true; }
            else if (arg == "--record" && i + 1 < argc) { recordPath = argv[++i]; game = true; deterministic = true; }
            else if (arg == "--trace" && i + 1 < argc) { tracePath = argv[++i]; game = true; deterministic = true; }
            else if (arg == "--username" && i + 1 < argc) username = argv[++i];
            else if (arg == "--credits") showCredits = true;
            else if (arg == "--legacy-combat") legacyCombat = true;
            else if (arg == "--legacy-movement") legacyMovement = true;
            else if (arg == "--legacy-camera") legacyCamera = true;
            else if (arg == "--arsenal-check") arsenalCheck = true;
            else if (arg == "--opponent-check") opponentCheck = true;
            else if (arg == "--camera-check") cameraCheck = true;
            else if (arg == "--systems-trace" && i + 1 < argc) { systemsPath = argv[++i]; deterministic = true; game = true; }
            else throw std::runtime_error("Unknown or incomplete option: " + arg);
        }
        if (ticks < 1 || ticks > 10000) throw std::runtime_error("Ticks must be 1..10000");
        if (username.empty() || username.size() > 128 || username.find_first_of("\r\n") != std::string::npos) {
            throw std::runtime_error("Invalid replay username");
        }
        bb::ReplayTape tape;
        if (!replayPath.empty()) {
            if (clockOverride) throw std::runtime_error("Replay files own their seed and clock");
            tape = bb::readReplay(replayPath);
            ticks = static_cast<int>(tape.frames.size());
            seed = tape.seed; epoch = tape.epoch; username = tape.username;
        } else if (exercise) {
            tape = bb::exerciseTape(ticks, seed, epoch);
            deterministic = true;
        }
        tape.seed = seed; tape.epoch = epoch; tape.username = username;
        if (tracePath == replayPath && !tracePath.empty()) throw std::runtime_error("Input and output paths must differ");
        if (recordPath == replayPath && !recordPath.empty()) throw std::runtime_error("Input and output paths must differ");
        if (!systemsPath.empty() && (systemsPath == tracePath || systemsPath == replayPath || systemsPath == recordPath)) {
            throw std::runtime_error("Trace outputs must have distinct paths");
        }
        bb::content::validate();
        SetTraceLogLevel(LOG_WARNING);
        bb::openAssetPackage(root);
        if (arsenalCheck) return bb::testArsenalCombat(root);
        if (opponentCheck) return bb::testOpponentCombat(root);
        if (cameraCheck) return bb::testCameraBridge(root);
        if (showCredits) {
            auto credits = bb::readAsset("CREDITS.md");
            std::cout.write(reinterpret_cast<const char*>(credits.data()), static_cast<std::streamsize>(credits.size()));
            return 0;
        }
        if (peerPort && legacyCombat) throw std::runtime_error("Multiplayer requires native combat");
        std::unique_ptr<bb::PeerConnection> connection;
        if (peerPort) connection = std::make_unique<bb::PeerConnection>(peerPort, peerHost);
        if (!headless) {
            bb::gfx::initialize(1280, 720, "Battle Bound DX | Vulkan", renderSmoke);
            bb::gfx::setFrameRate(renderSmoke ? 0 : 30);
            if (unmuted) InitAudioDevice();
        }
        int result = 0;
        {
            bb::Runtime runtime(root, headless);
            runtime.username = username;
            runtime.mute = !unmuted;
            runtime.load();
            runtime.useLegacyCamera(legacyCamera);
            runtime.useLegacyCombat(legacyCombat);
            runtime.useLegacyMovement(legacyMovement);
            if (deterministic) runtime.setDeterministic(seed, epoch);
            else if (connection) runtime.setDeterministic(
                static_cast<std::uint32_t>(connection->epoch()) ^ (peerHost.empty() ? 3005u : 9001u),
                connection->epoch());
            runtime.captureEvents = !tracePath.empty();
            runtime.greenFlag();
            if (!equip.empty()) {
                const auto id = bb::content::findItem(equip);
                if (!id || bb::content::item(*id).category == bb::content::ItemCategory::Vehicle)
                    throw std::runtime_error("Unknown loadout item");
                const auto slot = static_cast<std::size_t>(bb::content::item(*id).category);
                for (int attempt = 0; attempt < 26 && runtime.loadoutCode(slot) != equip; ++attempt)
                    runtime.cycleLoadout(slot);
                if (runtime.loadoutCode(slot) != equip) throw std::runtime_error("Item is missing from loadout");
            }
            if (game) runtime.startGame();
            if (connection) runtime.enableMultiplayer();
            const auto advance = [&] {
                if (connection) runtime.receiveNetworkFrame(connection->exchange(runtime.takeNetworkFrame()));
                runtime.tick(1.0 / 30);
            };
            std::unique_ptr<bb::TraceWriter> trace;
            if (!tracePath.empty()) trace = std::make_unique<bb::TraceWriter>(tracePath, tape, root);
            std::unique_ptr<bb::TraceWriter> systemsTrace;
            if (!systemsPath.empty()) systemsTrace = std::make_unique<bb::TraceWriter>(systemsPath, tape, root);
            bb::ReplayTape recording;
            recording.seed = seed; recording.epoch = epoch; recording.username = username;
            if (headless || renderSmoke) {
                const bb::GameUi previewUi(game);
                double minimumX = 1e30, maximumX = -1e30;
                for (int i = 0; i < ticks; ++i) {
                    bb::InputFrame frame;
                    if (!tape.frames.empty()) frame = tape.frames.at(i);
                    if (deterministic) runtime.applyInput(frame);
                    advance();
                    if (trace) trace->write(i, frame, runtime);
                    if (systemsTrace) systemsTrace->writeSystems(i, runtime);
                    if (!recordPath.empty()) recording.frames.push_back(frame);
                    if (i > 5) { double x = runtime.namedValue("::Player X").number(); minimumX = std::min(minimumX, x); maximumX = std::max(maximumX, x); }
                    if (renderSmoke) {
                        bb::gfx::beginFrame();
                        previewUi.draw(runtime);
                        if (i == ticks - 1) bb::gfx::requestCapture();
                        bb::gfx::endFrame();
                    }
                }
                if (renderSmoke) {
                    Image screenshot = bb::gfx::capture();
                    if (!screenshot.data) throw std::runtime_error("Native framebuffer capture failed");
                    ImageFormat(&screenshot, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                    std::ofstream output(root / "scratch/native-preview.ppm", std::ios::binary);
                    output << "P6\n" << screenshot.width << ' ' << screenshot.height << "\n255\n";
                    auto* colors = static_cast<const Color*>(screenshot.data);
                    for (int i = 0; i < screenshot.width * screenshot.height; ++i) { output.put(colors[i].r); output.put(colors[i].g); output.put(colors[i].b); }
                    UnloadImage(screenshot);
                    if (!output) throw std::runtime_error("Cannot save framebuffer capture");
                }
                std::cout << runtime.diagnostics() << '\n';
                if (!runtime.unsupported.empty()) result = 2;
                if (runtime.stopped()) result = 3;
                if (exercise) {
                    std::cout << "Scripted player X range: " << maximumX - minimumX << '\n';
                    if (maximumX - minimumX < 10 || runtime.bulletClones == 0) throw std::runtime_error("Scripted movement/shooting did not exercise the original gameplay");
                }
            } else {
                bb::GameUi ui(game || deterministic);
                std::size_t tick = 0;
                while (!bb::gfx::shouldClose()) {
                    if ((!recordPath.empty() || trace || systemsTrace) && tick >= static_cast<std::size_t>(ticks)) break;
                    bb::InputFrame frame;
                    const bb::Viewport viewport{static_cast<double>(bb::gfx::width()),
                                                  static_cast<double>(bb::gfx::height())};
                    const auto physical = bb::capturePlatformInput(viewport, deterministic);
                    if (!connection && !deterministic && !ui.update(runtime, physical)) {
                        bb::gfx::beginFrame();
                        ui.draw(runtime);
                        bb::gfx::endFrame();
                        continue;
                    }
                    frame = bb::encodeInput(physical);
                    if (deterministic) runtime.applyInput(frame);
                    else runtime.submitLiveInput(physical);
                    advance();
                    if (trace) trace->write(tick, frame, runtime);
                    if (systemsTrace) systemsTrace->writeSystems(tick, runtime);
                    if (!recordPath.empty()) recording.frames.push_back(frame);
                    ++tick;
                    bb::gfx::beginFrame();
                    ui.draw(runtime);
                    bb::gfx::endFrame();
                }
                std::cout << runtime.diagnostics() << '\n';
            }
            if (!recordPath.empty() && !recording.frames.empty()) bb::writeReplay(recordPath, recording, root);
        }
        if (!headless) {
            if (IsAudioDeviceReady()) CloseAudioDevice();
            bb::gfx::shutdown();
        }
        return result;
    } catch (const std::exception& error) {
        std::cerr << "Battle Bound DX: " << error.what() << '\n';
        bool diagnostic = false;
        for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--headless" || std::string(argv[i]) == "--self-test") diagnostic = true;
        if (!diagnostic) bb::showStartupError(error.what());
        return 1;
    }
}
