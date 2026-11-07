#include "platform_input.hpp"
#include "graphics.hpp"

namespace bb {
RawInput capturePlatformInput(const Viewport& viewport, bool recording) {
    RawInput result;
    for (int code = 33; code <= 126; ++code) {
        if ((code >= 'a' && code <= 'z') || (recording && code == ';')) continue;
        if (gfx::keyDown(code)) {
            const auto key = code >= 'A' && code <= 'Z' ? code + 32 : code;
            result.keys.set(static_cast<std::size_t>(key));
        }
    }
    for (auto [code, key] : {std::pair{KEY_SPACE, Key::Space}, {KEY_UP, Key::Up},
                            {KEY_DOWN, Key::Down}, {KEY_LEFT, Key::Left},
                            {KEY_RIGHT, Key::Right}, {KEY_ENTER, Key::Enter}}) {
        if (gfx::keyDown(code)) result.keys.set(static_cast<std::size_t>(key));
    }
    for (int code = 32; code < 350; ++code) {
        result.anyKeyHeld = result.anyKeyHeld || gfx::keyDown(code);
        result.anyKeyPressed = result.anyKeyPressed || gfx::keyPressed(code);
    }
    result.primaryMouse = gfx::mouseDown(MOUSE_BUTTON_LEFT);
    result.pointer = viewport.pixelToStage({gfx::mouseX(), gfx::mouseY()}, recording);
    result.wheel = gfx::mouseWheel() > 0 ? 1 : gfx::mouseWheel() < 0 ? -1 : 0;
    return result;
}
}
