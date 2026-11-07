#include "game_ui.hpp"
#include "content.hpp"
#include "graphics.hpp"
#include "runtime.hpp"
#include <algorithm>
#include <cmath>

namespace bb {
namespace {
constexpr Color ink{240, 246, 255, 255};
constexpr Color muted{160, 179, 200, 255};
constexpr Color accent{99, 240, 196, 255};
constexpr Color surface{16, 27, 43, 245};
constexpr Color track{52, 70, 90, 255};
constexpr Rectangle primary{34, 233, 185, 39};
constexpr Rectangle restart{34, 280, 185, 33};

void label(const std::string& value, float x, float y, float size, Color color = ink) {
    gfx::text(value.c_str(), x, y, size, color);
}

bool over(StagePoint pointer, Rectangle box) {
    const auto x = pointer.x + 320;
    const auto y = 180 - pointer.y;
    return x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height;
}

void button(Rectangle box, const char* title, StagePoint pointer, bool main) {
    const bool hover = over(pointer, box);
    const Color fill = main ? (hover ? ink : accent) : (hover ? track : surface);
    gfx::rectangle(box.x, box.y, box.width, box.height, fill);
    label(title, box.x + 14, box.y + 7, 15, main ? surface : ink);
}
}

GameUi::GameUi(bool startInGame) : page(startInGame ? Page::Playing : Page::Title) {}

bool GameUi::update(Runtime& runtime, const RawInput& input) {
    pointer = input.pointer;
    const bool clicked = input.primaryMouse && !mouseHeld;
    mouseHeld = input.primaryMouse;
    if (page == Page::Playing) {
        if (waitForMouseRelease && mouseHeld) return false;
        waitForMouseRelease = false;
        if (gfx::keyPressed(KEY_ESCAPE)) page = Page::Paused;
        else if (gfx::keyPressed(KEY_F5)) page = Page::Title;
        return page == Page::Playing;
    }
    if (page == Page::Paused && clicked && over(pointer, restart)) {
        runtime.greenFlag();
        runtime.startGame();
        waitForMouseRelease = mouseHeld;
        page = Page::Playing;
        return false;
    }
    if (gfx::keyPressed(KEY_ENTER) || gfx::keyPressed(KEY_F6) ||
        (clicked && over(pointer, primary)) ||
        (page == Page::Paused && gfx::keyPressed(KEY_ESCAPE))) {
        if (page == Page::Title) {
            runtime.greenFlag();
            runtime.startGame();
        }
        waitForMouseRelease = mouseHeld;
        page = Page::Playing;
    }
    // Consume menu input so Play and Resume cannot fire a shot.
    return false;
}

void GameUi::draw(Runtime& runtime) const {
    if (page != Page::Title) runtime.draw();
    if (page == Page::Playing) return;
    gfx::rectangle(0, 0, 640, 360, page == Page::Title ? Color{9, 16, 29, 255} : Color{9, 16, 29, 237});
    gfx::rectangle(34, 42, 24, 3, accent);
    label("BATTLE BOUND / DX", 34, 55, 12, accent);
    label(page == Page::Title ? "Battle Bound" : "Paused", 31, 77, 44);
    label(page == Page::Title ? "Offline arena" : "Your match is on hold.", 34, 141, 17, muted);
    gfx::rectangle(354, 50, 252, 262, surface);
    label("CONTROLS", 374, 67, 12, accent);
    constexpr const char* keys[]{"A / D", "W", "S", "Space", "Left click", "R", "Esc"};
    constexpr const char* actions[]{"Move", "Jump", "Crouch", "Crawl / Dive", "Fire", "Reload", "Pause"};
    for (int row = 0; row < 7; ++row) {
        const auto y = static_cast<float>(99 + row * 27);
        label(keys[row], 374, y, 13);
        label(actions[row], 474, y, 13, muted);
    }
    button(primary, page == Page::Title ? "Play  /  Enter" : "Resume  /  Enter", pointer, true);
    if (page == Page::Paused) button(restart, "Restart match", pointer, false);
}

void drawGameHud(const Runtime& runtime) {
    if (runtime.namedValue(";SCENE").text != "game") return;
    const auto number = [&](const char* name) { return runtime.namedValue(name).number(); };
    const bool helicopter = runtime.namedValue("::[Spiller-action]").text == "heli";
    const auto health = std::max(0.0, number(helicopter ? "::Helicopter HP" : "::Spiller-liv"));
    const auto maximum = helicopter ? 500.0 : 100.0;
    gfx::rectangle(14, 299, 152, 47, surface);
    label(helicopter ? "AIRFRAME" : "HEALTH", 24, 304, 9, muted);
    label(std::to_string(static_cast<int>(std::ceil(health))), 24, 315, 16);
    gfx::rectangle(66, 324, 90, 4, track);
    gfx::rectangle(66, 324, static_cast<float>(90 * std::clamp(health / maximum, 0.0, 1.0)), 4,
                   health < maximum / 4 ? Color{255, 110, 115, 255} : accent);

    const auto id = content::findItem(runtime.namedValue("::Våpen holdt").text);
    gfx::rectangle(474, 299, 152, 47, surface);
    label(id ? std::string(content::item(*id).name) : "Weapon", 484, 304, 10, muted);
    const auto ammo = number("::Ammonisjon");
    const auto capacity = number("Magazine size");
    if (capacity > 0) {
        label(std::to_string(static_cast<int>(std::max(0.0, ammo))) + " / " +
              std::to_string(static_cast<int>(capacity)), 484, 317, 15);
    }
    if (number("Reload Spin") > 0) label("RELOAD", 572, 323, 8, accent);

    gfx::rectangle(253, 14, 134, 34, surface);
    const auto mode = number(";Spill-Modus");
    for (const auto& match : content::matches())
        if (mode == static_cast<int>(match.id)) label(std::string(match.name), 262, 17, 11);
    label("KILLS  " + std::to_string(static_cast<int>(number("::draphehe"))), 262, 31, 9, accent);
    const auto time = runtime.matchSecondsRemaining();
    const auto seconds = time % 60;
    label(std::to_string(time / 60) + ":" + (seconds < 10 ? "0" : "") +
          std::to_string(seconds), 350, 31, 9, muted);
    label(number("#PlayersOnline") <= 1 ? "SOLO" : "PLAYERS  " +
          std::to_string(static_cast<int>(number("#PlayersOnline"))), 14, 15, 9, muted);
    label("ESC  PAUSE", 554, 15, 9, muted);

    const auto aim = runtime.playerInput().aimStage;
    const auto x = static_cast<float>(aim.x + 320);
    const auto y = static_cast<float>(180 - aim.y);
    gfx::rectangle(x - 6, y, 4, 1, ink);
    gfx::rectangle(x + 3, y, 4, 1, ink);
    gfx::rectangle(x, y - 6, 1, 4, ink);
    gfx::rectangle(x, y + 3, 1, 4, ink);
}
}
