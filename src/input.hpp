#pragma once
#include "coordinates.hpp"
#include <array>
#include <bitset>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>

namespace bb {
enum class Key : unsigned short {
    Unknown = 0, Space = 32,
    A = 'a', D = 'd', R = 'r', S = 's', W = 'w',
    Up = 256, Down, Left, Right, Enter, Count
};
constexpr std::size_t keyCount = static_cast<std::size_t>(Key::Count);

struct ButtonState { bool held{}, pressed{}, released{}; };
struct RawInput {
    std::bitset<keyCount> keys;
    bool primaryMouse{};
    StagePoint pointer;
    int wheel{};
    bool anyKeyHeld{}, anyKeyPressed{};
};

// Replay wire format stays unchanged; strings are decoded only at the boundary.
struct InputFrame {
    std::set<std::string> keys;
    bool fire{};
    double aimX{}, aimY{};
    int wheel{};
};

enum class Action : unsigned char {
    Jump, MoveLeft, MoveRight, Crouch, UseItem, CrawlDive, Reload, Chat, Map,
    PreviousSlot, NextSlot, PrimarySlot, SecondarySlot, GadgetSlot, ExitHelicopter, Count
};
constexpr std::size_t actionCount = static_cast<std::size_t>(Action::Count);
enum class BindingKind : unsigned char { None, Keyboard, PrimaryMouse, WheelUp, WheelDown };
struct InputBinding { BindingKind kind{BindingKind::None}; Key key{Key::Unknown}; };
struct ControlBindings {
    std::array<InputBinding, actionCount> actions;
    static ControlBindings fromLegacy(std::span<const std::string> controls);
};

struct PlayerInput {
    std::array<ButtonState, actionCount> actions;
    double horizontal{};
    StagePoint aimStage;
    WorldPoint aimWorld;
    int wheel{};
    const ButtonState& action(Action action) const;
};

std::optional<Key> legacyKey(std::string_view name);
RawInput decodeInput(const InputFrame& frame);
InputFrame encodeInput(const RawInput& input);

class InputSystem {
public:
    void reset();
    void submit(const RawInput& frame);
    ButtonState key(Key key) const;
    ButtonState primaryMouse() const;
    bool anyHeld() const;
    bool anyPressed() const;
    bool legacyKeyState(std::string_view name, bool pressed = false) const;
    const RawInput& raw() const { return current; }
    PlayerInput mapPlayer(const ControlBindings& bindings, WorldPoint aimWorld) const;

private:
    RawInput current, previous;
};
void testInputSystem();
}
