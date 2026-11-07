#include "runtime.hpp"
#include <array>
#include <string_view>
#include <stdexcept>
#include <iostream>

namespace bb {
namespace {
enum class CameraOperation {
    AimX, AimY, FollowX, FollowY, HelicopterX, HelicopterY, PassengerX, PassengerY,
    PanX, PanY, GridX, GridY, SpawnX, SpawnY, SpectatorClampX, SpectatorMinY,
    SpectatorMaxY, PlayerClampX, ShakeX, ShakeY
};
struct Mapping { std::string_view block; CameraOperation operation; };

// Source mapping belongs to the compatibility adapter, not the camera system.
constexpr std::array<Mapping, 26> mappings{{
    Mapping{"ou", CameraOperation::AimX}, {"U3", CameraOperation::AimY},
    {"ox", CameraOperation::FollowX}, {"U?", CameraOperation::FollowY},
    {"ow", CameraOperation::HelicopterX}, {"U+", CameraOperation::HelicopterY},
    {"r,", CameraOperation::PassengerX}, {"r-", CameraOperation::PassengerY},
    {"rf", CameraOperation::PanX}, {"rg", CameraOperation::PanY},
    {"ot", CameraOperation::GridX}, {"rh", CameraOperation::GridX}, {"r/", CameraOperation::GridX},
    {"oA", CameraOperation::GridY}, {"0~", CameraOperation::GridY}, {"r:", CameraOperation::GridY},
    {"1+", CameraOperation::SpawnX}, {"1{", CameraOperation::SpawnY},
    {"0_", CameraOperation::SpectatorClampX}, {"aE,", CameraOperation::SpectatorClampX},
    {"0{", CameraOperation::SpectatorMinY}, {"0}", CameraOperation::SpectatorMaxY},
    {"aF_", CameraOperation::PlayerClampX}, {"aF{", CameraOperation::PlayerClampX},
    {"q-", CameraOperation::ShakeX}, {"0n", CameraOperation::ShakeY}
}};

bool cameraOwned(std::string_view name) {
    return name == ";CAM X" || name == ";CAM Y" || name == "Grid x" ||
           name == "Grid y" || name == ";Skjerm-risting";
}
}

void Runtime::resetCameraFromLegacy() {
    // The existing VM converts boolean reporter strings to numeric zero. Preserve
    // its observed camera profile here; fixing that VM bug is a separate change.
    cameraSystem.configure({5, 10, 10, 4, 0});
    cameraSystem.reset({{namedValue(";CAM X").number(), namedValue(";CAM Y").number()},
                        {namedValue("Grid x").number(), namedValue("Grid y").number()},
                        namedValue(";Skjerm-risting").number()});
}

void Runtime::syncCameraField(const std::string& id, double value) {
    const auto found = definitions[0].variableNames.find(id);
    if (found == definitions[0].variableNames.end()) return;
    const auto& name = found->second;
    if (name == ";CAM X") cameraSystem.setHorizontal(value);
    else if (name == ";CAM Y") cameraSystem.setVertical(value);
    else if (name == "Grid x") cameraSystem.setGridHorizontal(value);
    else if (name == "Grid y") cameraSystem.setGridVertical(value);
    else if (name == ";Skjerm-risting") cameraSystem.setShakeMagnitude(value);
}

bool Runtime::writeNativeCamera(Task& task, const Block& block, const std::string& id,
                                std::string& result) {
    if (!nativeCamera || definitions[task.actor->type].name != "[Player]") return false;
    const auto field = definitions[0].variableNames.find(id);
    if (field == definitions[0].variableNames.end() || !cameraOwned(field->second)) return false;
    const auto mapping = std::find_if(mappings.begin(), mappings.end(), [&](const auto& value) {
        return value.block == block.id;
    });
    if (mapping == mappings.end()) return false;

    const auto number = [&](const char* name) { return namedValue(name).number(); };
    const auto local = [&](std::string_view name) {
        for (const auto& [localId, label] : definitions[task.actor->type].variableNames) {
            if (label == name) return variable(*task.actor, localId).number();
        }
        return 0.0;
    };
    const auto parameter = [&](const char* name) {
        for (auto frame = task.frames.rbegin(); frame != task.frames.rend(); ++frame) {
            auto value = frame->arguments.find(name);
            if (value != frame->arguments.end()) return Value(value->second).number();
        }
        return 0.0;
    };
    const auto shakeNoise = [&] {
        // Evaluate the original RNG reporter to retain its draw order and trace ID.
        const auto& blocks = definitions[task.actor->type].blocks;
        const auto& add = blocks.at(block.inputs.at("VALUE").block);
        const auto& multiply = blocks.at(add.inputs.at("NUM2").block);
        const int randomBlock = multiply.inputs.at("NUM2").block;
        if (blocks.at(randomBlock).op != "operator_random") {
            throw std::runtime_error("Camera shake source mapping no longer matches the project");
        }
        return eval(task, randomBlock).number();
    };

    switch (mapping->operation) {
    case CameraOperation::AimX: cameraSystem.followHorizontal(number("::Player X"), mx() / 5); break;
    case CameraOperation::AimY: cameraSystem.followVertical(number("::Player Y"), number("::sy"), my() / 5); break;
    case CameraOperation::FollowX: cameraSystem.followHorizontal(number("::Player X")); break;
    case CameraOperation::FollowY: cameraSystem.followVertical(number("::Player Y"), number("::sy")); break;
    case CameraOperation::HelicopterX: cameraSystem.followHelicopterHorizontal(number("::Player X")); break;
    case CameraOperation::HelicopterY: cameraSystem.followHelicopterVertical(number("::Player Y"), local("in_air")); break;
    case CameraOperation::PassengerX: cameraSystem.followPassengerHorizontal(number("::Player X"), parameter("d"), parameter("rl")); break;
    case CameraOperation::PassengerY: cameraSystem.followPassengerVertical(number("::Player Y")); break;
    case CameraOperation::PanX: cameraSystem.panHorizontal(number("::sx")); break;
    case CameraOperation::PanY: cameraSystem.panVertical(number("::sy")); break;
    case CameraOperation::GridX: cameraSystem.snapGridHorizontal(number("(grid x *)")); break;
    case CameraOperation::GridY: cameraSystem.snapGridVertical(number("(grid y *)")); break;
    case CameraOperation::SpawnX: cameraSystem.setHorizontal(number("::Player X")); break;
    case CameraOperation::SpawnY: cameraSystem.setVertical(number("::Player Y")); break;
    case CameraOperation::SpectatorClampX: cameraSystem.clampHorizontal(0, number("(grid x *)") * 12); break;
    case CameraOperation::SpectatorMinY: cameraSystem.clampVerticalMinimum(number("(grid y *)") * -1); break;
    case CameraOperation::SpectatorMaxY: cameraSystem.clampVerticalMaximum(number("(grid y *)") * 2); break;
    case CameraOperation::PlayerClampX: cameraSystem.clampHorizontal(-114, 8755); break;
    case CameraOperation::ShakeX: cameraSystem.shakeHorizontal(parameter("shaky variable"), shakeNoise()); break;
    case CameraOperation::ShakeY: cameraSystem.shakeVertical(parameter("shaky variable"), shakeNoise()); break;
    }
    const auto& state = cameraSystem.state();
    const auto& name = field->second;
    if (name == ";CAM X") result = Value(state.position.x).text;
    else if (name == ";CAM Y") result = Value(state.position.y).text;
    else if (name == "Grid x") result = Value(state.gridOrigin.x).text;
    else if (name == "Grid y") result = Value(state.gridOrigin.y).text;
    return true;
}

int testCameraBridge(const std::filesystem::path& root) {
    Runtime native(root, true), legacy(root, true);
    native.load(); legacy.load();
    legacy.useLegacyCamera(true);
    std::size_t checks = 0;
    for (const auto& mapping : mappings) {
        for (int variant = 0; variant < 4; ++variant) {
            auto prepare = [&](Runtime& runtime) {
                runtime.setDeterministic(3005 + variant, 1793966460);
                runtime.captureEvents = true;
                runtime.greenFlag();
                runtime.setNamed("::Player X", "350.75");
                runtime.setNamed("::Player Y", variant % 2 ? "700" : "680");
                runtime.setNamed("::sx", "-0.5");
                runtime.setNamed("::sy", variant < 2 ? "-21" : "-20");
                runtime.setNamed(";CAM X", "100.25");
                runtime.setNamed(";CAM Y", "-20.5");
                runtime.setNamed("(grid x *)", "720");
                runtime.setNamed("(grid y *)", "540");
                runtime.setNamed(";Skjerm-risting", "1.25");
                if (mapping.operation == CameraOperation::SpectatorClampX) {
                    runtime.setNamed(";CAM X", mapping.block == "0_" ? "10000" : "-50");
                }
                if (mapping.operation == CameraOperation::PlayerClampX) {
                    runtime.setNamed(";CAM X", mapping.block == "aF_" ? "9999" : "-200");
                }
                if (mapping.operation == CameraOperation::SpectatorMinY) runtime.setNamed(";CAM Y", "-1000");
                if (mapping.operation == CameraOperation::SpectatorMaxY) runtime.setNamed(";CAM Y", "2000");
                auto actor = *std::find_if(runtime.actors.begin(), runtime.actors.end(), [&](const auto& value) {
                    return runtime.definitions[value->type].name == "[Player]";
                });
                for (const auto& [id, name] : runtime.definitions[actor->type].variableNames) {
                    if (name == "in_air") actor->variables[id] = variant % 2 ? "1" : "2";
                }
                runtime.resetCameraFromLegacy();
                InputFrame input;
                input.aimX = 87.25; input.aimY = -43.75;
                runtime.applyInput(input);
                Task task;
                task.actor = actor;
                Frame frame;
                frame.arguments = {{"d", "90"}, {"rl", "-1"}, {"shaky variable", "1.25"}};
                task.frames.push_back(frame);
                return task;
            };
            auto actualTask = prepare(native), expectedTask = prepare(legacy);
            auto& blocks = native.definitions[actualTask.actor->type].blocks;
            auto source = std::find_if(blocks.begin(), blocks.end(), [&](const Block& value) {
                return value.id == mapping.block;
            });
            if (source == blocks.end()) throw std::runtime_error("Camera source block missing");
            const auto& id = source->fields.at("VARIABLE_ID");
            auto& actual = native.variableRef(*actualTask.actor, id);
            auto expected = legacy.variable(*expectedTask.actor, id);
            auto value = legacy.input(expectedTask, *source, "VALUE");
            expected = source->op == "data_changevariableby" ?
                Value(expected.number() + value.number()) : value;
            if (!native.writeNativeCamera(actualTask, *source, id, actual) || actual != expected.text ||
                native.randomDrawCount() != legacy.randomDrawCount() ||
                native.gameplayEvents != legacy.gameplayEvents) {
                throw std::runtime_error("Camera parity failed at source block " + source->id +
                                         " variant " + std::to_string(variant) + ": " +
                                         actual + " != " + expected.text);
            }
            ++checks;
        }
    }
    std::cout << "Camera bridge: " << checks << " original-expression comparisons passed\n";
    return 0;
}
}
