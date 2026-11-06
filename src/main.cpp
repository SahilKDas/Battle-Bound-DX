#include "runtime.hpp"
#include "assets.hpp"
#include "graphics.hpp"
#include <iostream>
#include <fstream>

int main(int argc, char** argv) {
    try {
        std::filesystem::path root = std::filesystem::current_path();
        bool headless = false, game = false, unmuted = false, renderSmoke = false, exercise = false;
        int ticks = 300;
        std::string username = "DX_Player";
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--self-test") return bb::selfTest();
            if (arg == "--root" && i + 1 < argc) root = argv[++i];
            else if (arg == "--headless") headless = true;
            else if (arg == "--game") game = true;
            else if (arg == "--audio") unmuted = true;
            else if (arg == "--render-smoke") renderSmoke = true;
            else if (arg == "--exercise") { exercise = true; game = true; }
            else if (arg == "--ticks" && i + 1 < argc) ticks = std::stoi(argv[++i]);
            else if (arg == "--username" && i + 1 < argc) username = argv[++i];
        }
        SetTraceLogLevel(LOG_WARNING);
        bb::openAssetPackage(root);
        for (int i = 1; i < argc; ++i) if (std::string(argv[i]) == "--credits") {
            auto credits = bb::readAsset("CREDITS.md");
            std::cout.write(reinterpret_cast<const char*>(credits.data()), static_cast<std::streamsize>(credits.size()));
            return 0;
        }
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
            runtime.greenFlag();
            if (game) runtime.startGame();
            if (headless || renderSmoke) {
                double minimumX = 1e30, maximumX = -1e30;
                for (int i = 0; i < ticks; ++i) {
                    if (exercise) {
                        runtime.scriptedInput = true;
                        runtime.scriptedKeys.clear();
                        if (i < 80) runtime.scriptedKeys.insert("d");
                        if (i >= 80 && i < 160) runtime.scriptedKeys.insert("a");
                        if (i >= 30 && i < 35) runtime.scriptedKeys.insert("w");
                        if (i >= 180 && i < 210) runtime.scriptedKeys.insert("s");
                        if (i >= 210 && i < 220) runtime.scriptedKeys.insert("r");
                        runtime.scriptedMouse = i >= 60 && i < 180;
                    }
                    runtime.tick(1.0 / 30);
                    if (i > 5) { double x = runtime.namedValue("::Player X").number(); minimumX = std::min(minimumX, x); maximumX = std::max(maximumX, x); }
                    if (renderSmoke) { bb::gfx::beginFrame(); runtime.draw(); if (i == ticks - 1) bb::gfx::requestCapture(); bb::gfx::endFrame(); }
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
                while (!bb::gfx::shouldClose()) {
                    if (bb::gfx::keyPressed(KEY_F5)) runtime.greenFlag();
                    if (bb::gfx::keyPressed(KEY_F6)) runtime.startGame();
                    runtime.tick(1.0 / 30);
                    bb::gfx::beginFrame();
                    runtime.draw();
                    bb::gfx::endFrame();
                }
                std::cout << runtime.diagnostics() << '\n';
            }
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
