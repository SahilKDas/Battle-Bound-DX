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
constexpr Rectangle loadoutBack{34, 285, 185, 33};
constexpr Rectangle loadoutRow(int row) { return {34, static_cast<float>(95 + row * 59), 572, 48}; }

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
    if (page == Page::Loadout) {
        if (gfx::keyPressed(KEY_ESCAPE) || (clicked && over(pointer, loadoutBack))) {
            page = Page::Title;
        } else {
            for (int slot = 0; slot < 3; ++slot)
                if ((clicked && over(pointer, loadoutRow(slot))) || gfx::keyPressed(KEY_ONE + slot))
                    runtime.cycleLoadout(slot);
        }
        return false;
    }
    if (page == Page::Title && clicked && over(pointer, restart)) {
        page = Page::Loadout;
        return false;
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
    if (page == Page::Loadout) {
        gfx::rectangle(0, 0, 640, 360, {9, 16, 29, 255});
        label("OFFLINE LOADOUT", 34, 32, 28);
        label("Click a slot or press 1, 2 or 3 to cycle its gear.", 34, 68, 11, muted);
        constexpr const char* labels[]{"PRIMARY", "SECONDARY", "GADGET"};
        for (int slot = 0; slot < 3; ++slot) {
            const auto row = loadoutRow(slot);
            const auto item = content::findItem(runtime.loadoutCode(slot));
            gfx::rectangle(row.x, row.y, row.width, row.height, over(pointer, row) ? track : surface);
            label(labels[slot], row.x + 14, row.y + 6, 9, accent);
            label(item ? std::string(content::item(*item).name) : "Empty", row.x + 14, row.y + 20, 15);
            label("NEXT >", row.x + row.width - 66, row.y + 17, 12, muted);
        }
        button(loadoutBack, "Back  /  Esc", pointer, false);
        return;
    }
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
    button(restart, page == Page::Paused ? "Restart match" : "Loadout", pointer, false);
}

bool drawMatchPanel(const Runtime& runtime, std::string_view role, std::string_view costume,
                    std::string_view info, int digit, StagePoint position) {
    const auto x = static_cast<float>(320 + position.x);
    const auto y = static_cast<float>(180 - position.y);
    if (role == "chat" && costume.starts_with("Chat-")) {
        constexpr const char* phrases[4][9] = {
            {"Hello", "Hi", "I have to go", "Bye", "No problem", "Thanks!", "No", "Yes", "Okay"},
            {"Follow me", "Heal me please", "Want to team?", "We are on the same team", "I go solo",
             "Let's attack!", "Go defend!", "I'll cover you", "Don't attack"},
            {"Hop in!", "Leave the helicopter", "Peace?", "Let me join", "Cool look",
             "lol", "Nice", "Darn gosh", "You're good!"},
            {"Vote TDM", "Vote GG", "Vote CTF", "Vote FFA", "Misclick, sorry", "Wrong chat",
             "Melee only?", "1v1?", "You killed me"}
        };
        const int page = std::clamp(static_cast<int>(runtime.namedValue(":Chat.Page").number()), 1, 4);
        gfx::rectangle(x - 60, y - 100, 120, 200, surface);
        label("QUICK CHAT", x - 52, y - 94, 9, accent);
        for (int row = 0; row < 9; ++row)
            label(std::to_string(row + 1) + "  " + phrases[page - 1][row], x - 52, y - 75 + row * 16, 9);
        label("CLICK TO PAGE  " + std::to_string(page) + "/4", x - 52, y + 76, 8, muted);
        return true;
    }
    if (role == "vote" && digit >= 1 && digit <= 4) {
        const auto& mode = content::matches()[digit - 1];
        const bool selected = runtime.namedValue("::VALG").number() == digit;
        gfx::rectangle(x - 48, y - 24, 96, 48, selected ? track : surface);
        label(std::string(mode.name), x - 41, y - 16, 10);
        label(selected ? "SELECTED" : "VOTE", x - 41, y + 3, 8, accent);
        return true;
    }
    if (role == "game result") {
        gfx::rectangle(208, 130, 224, 82, surface);
        label("MATCH COMPLETE", 227, 144, 11, accent);
        const char* result = info == "1w" ? "Team 1 wins" : info == "2w" ? "Team 2 wins" :
                             info == "e" ? "Draw" : info == "mg" ? "Match ended" : "Round ended";
        label(result, 227, 167, 16);
        return true;
    }
    if (role == "gear") return true;
    return false;
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

    if (number(":Kart Åpent") > 0) {
        gfx::rectangle(220, 55, 200, 22, surface);
        label("TACTICAL MAP", 232, 58, 10, accent);
        label("M  CLOSE", 361, 59, 8, muted);
        return;
    }
    constexpr const char* slots[]{"PRIMARY", "SECONDARY", "GADGET"};
    const int activeSlot = static_cast<int>(number("inventory scroll"));
    for (int slot = 0; slot < 3; ++slot) {
        const auto left = static_cast<float>(206 + slot * 77);
        gfx::rectangle(left, 321, 73, 25, activeSlot == slot + 1 ? track : surface);
        label(std::to_string(slot + 1) + "  " + slots[slot], left + 6, 326, 9,
              activeSlot == slot + 1 ? accent : muted);
    }

    const auto aim = runtime.playerInput().aimStage;
    const auto x = static_cast<float>(aim.x + 320);
    const auto y = static_cast<float>(180 - aim.y);
    gfx::rectangle(x - 6, y, 4, 1, ink);
    gfx::rectangle(x + 3, y, 4, 1, ink);
    gfx::rectangle(x, y - 6, 1, 4, ink);
    gfx::rectangle(x, y + 3, 1, 4, ink);
}
}
