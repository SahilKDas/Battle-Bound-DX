#include "input.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace bb {
namespace {
std::string normalized(std::string_view name) {
    std::string result(name);
    for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
ButtonState edges(bool held, bool previous) { return {held, held && !previous, !held && previous}; }
}

std::optional<Key> legacyKey(std::string_view name) {
    auto label = normalized(name);
    if (label.size() == 1 && label[0] >= '!' && label[0] <= '~') {
        return static_cast<Key>(static_cast<unsigned char>(label[0]));
    }
    if (label == "space") return Key::Space;
    if (label == "up arrow") return Key::Up;
    if (label == "down arrow") return Key::Down;
    if (label == "left arrow") return Key::Left;
    if (label == "right arrow") return Key::Right;
    if (label == "enter") return Key::Enter;
    return std::nullopt;
}

RawInput decodeInput(const InputFrame& frame) {
    RawInput result;
    for (const auto& name : frame.keys) {
        if (auto key = legacyKey(name)) result.keys.set(static_cast<std::size_t>(*key));
    }
    result.primaryMouse = frame.fire;
    result.pointer = {frame.aimX, frame.aimY};
    result.wheel = frame.wheel;
    return result;
}

InputFrame encodeInput(const RawInput& input) {
    InputFrame result;
    for (std::size_t key = 33; key <= 126; ++key) {
        if (input.keys[key]) result.keys.insert(std::string(1, static_cast<char>(key)));
    }
    for (auto [key, name] : {std::pair{Key::Space, "space"}, {Key::Up, "up arrow"},
                             {Key::Down, "down arrow"}, {Key::Left, "left arrow"},
                             {Key::Right, "right arrow"}, {Key::Enter, "enter"}}) {
        if (input.keys[static_cast<std::size_t>(key)]) result.keys.insert(name);
    }
    result.fire = input.primaryMouse;
    result.aimX = input.pointer.x; result.aimY = input.pointer.y;
    result.wheel = input.wheel;
    return result;
}

ControlBindings ControlBindings::fromLegacy(std::span<const std::string> controls) {
    ControlBindings result{};
    for (std::size_t i = 0; i < std::min(controls.size(), actionCount); ++i) {
        auto& binding = result.actions[i];
        const auto label = normalized(controls[i]);
        if (auto key = legacyKey(label)) binding = {BindingKind::Keyboard, *key};
        else if (label == "mouse") binding.kind = BindingKind::PrimaryMouse;
        else if (label == "scroll wheel up") binding.kind = BindingKind::WheelUp;
        else if (label == "scroll wheel down") binding.kind = BindingKind::WheelDown;
    }
    return result;
}

const ButtonState& PlayerInput::action(Action action) const {
    return actions.at(static_cast<std::size_t>(action));
}

void InputSystem::reset() { current = {}; previous = {}; }
void InputSystem::submit(const RawInput& frame) { previous = current; current = frame; }
ButtonState InputSystem::key(Key key) const {
    const auto index = static_cast<std::size_t>(key);
    if (key == Key::Unknown || index >= keyCount) return {};
    return edges(current.keys[index], previous.keys[index]);
}
ButtonState InputSystem::primaryMouse() const {
    return edges(current.primaryMouse, previous.primaryMouse);
}
bool InputSystem::anyHeld() const { return current.anyKeyHeld || current.keys.any(); }
bool InputSystem::anyPressed() const {
    return current.anyKeyPressed || (current.keys & ~previous.keys).any();
}
bool InputSystem::legacyKeyState(std::string_view name, bool pressed) const {
    if (normalized(name) == "any") return pressed ? anyPressed() : anyHeld();
    const auto code = legacyKey(name);
    if (!code) return false;
    const auto state = key(*code);
    return pressed ? state.pressed : state.held;
}

PlayerInput InputSystem::mapPlayer(const ControlBindings& bindings, WorldPoint aimWorld) const {
    PlayerInput result{};
    for (std::size_t i = 0; i < actionCount; ++i) {
        const auto binding = bindings.actions[i];
        switch (binding.kind) {
        case BindingKind::Keyboard: result.actions[i] = key(binding.key); break;
        case BindingKind::PrimaryMouse: result.actions[i] = primaryMouse(); break;
        case BindingKind::WheelUp: result.actions[i] = {current.wheel > 0, current.wheel > 0, false}; break;
        case BindingKind::WheelDown: result.actions[i] = {current.wheel < 0, current.wheel < 0, false}; break;
        case BindingKind::None: break;
        }
    }
    result.horizontal = static_cast<double>(result.action(Action::MoveRight).held) -
                        static_cast<double>(result.action(Action::MoveLeft).held);
    result.aimStage = current.pointer;
    result.aimWorld = aimWorld;
    result.wheel = current.wheel;
    return result;
}

void testInputSystem() {
    auto require = [](bool condition, const char* label) {
        if (!condition) throw std::runtime_error(label);
    };
    InputSystem input;
    InputFrame frame;
    frame.keys = {"D"}; frame.fire = true; frame.aimX = 12.25;
    input.submit(decodeInput(frame));
    require(input.key(Key::D).pressed && input.anyPressed() && input.primaryMouse().pressed,
            "First sample produces key/mouse edges");
    input.submit(decodeInput(frame));
    require(input.key(Key::D).held && !input.key(Key::D).pressed && !input.anyPressed(),
            "Held keys must not repeat pressed edges");
    const std::array<std::string, 5> defaults{"w", "a", "d", "s", "mouse"};
    auto player = input.mapPlayer(ControlBindings::fromLegacy(defaults), {100, 200});
    require(player.horizontal == 1 && player.action(Action::UseItem).held &&
            player.aimStage.x == 12.25 && player.aimWorld.x == 100, "Typed player mapping");
    auto remapped = defaults;
    remapped[2] = "left arrow";
    require(input.mapPlayer(ControlBindings::fromLegacy(remapped), {}).horizontal == 0,
            "Remapping does not leave old bindings active");
    frame.keys.clear(); frame.fire = false; frame.wheel = -1;
    input.submit(decodeInput(frame));
    require(input.key(Key::D).released && input.primaryMouse().released, "Release edges");
    input.reset();
    require(!input.anyHeld() && !input.primaryMouse().held, "Reset clears physical state");
    require(!legacyKey("invalid") && !input.legacyKeyState("invalid"), "Unknown key isolation");
}
}
