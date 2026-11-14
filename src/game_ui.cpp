#include "game_ui.hpp"
#include "content.hpp"
#include "graphics.hpp"
#include "runtime.hpp"
#include <algorithm>
#include <cmath>
#include <charconv>

namespace bb {
namespace {
constexpr Color ink{237, 242, 255, 255};
constexpr Color muted{167, 182, 212, 255};
constexpr Color accent{181, 255, 107, 255};
constexpr Color background{12, 16, 31, 255};
constexpr Color surface{24, 33, 55, 255};
constexpr Color track{51, 66, 97, 255};
constexpr Color outline{65, 81, 116, 255};
constexpr Rectangle primary{48, 222, 135, 33};
constexpr Rectangle restart{193, 222, 134, 33};
constexpr Rectangle hostButton{362, 155, 105, 33};
constexpr Rectangle joinButton{479, 155, 105, 33};
constexpr Rectangle lobbyAction{282, 252, 156, 33};
constexpr Rectangle lobbyBack{448, 252, 128, 33};
constexpr Rectangle loadoutBack{24, 67, 126, 33};
constexpr Rectangle loadoutRow(int slot) { return {static_cast<float>(24 + slot * 200), 140, 192, 150}; }

void label(const std::string& value, float x, float y, float size, Color color = ink) {
    gfx::text(value.c_str(), x, y, size, color);
}

bool over(StagePoint pointer, Rectangle box) {
    const auto x = pointer.x + 320;
    const auto y = 180 - pointer.y;
    return x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height;
}

void card(Rectangle box, Color fill = surface) {
    gfx::rectangle(box.x, box.y, box.width, box.height, outline);
    gfx::rectangle(box.x + 1, box.y + 1, box.width - 2, box.height - 2, fill);
}

void button(Rectangle box, const char* title, StagePoint pointer, bool main) {
    const bool hover = over(pointer, box);
    if (main) gfx::rectangle(box.x, box.y, box.width, box.height, hover ? ink : accent);
    else card(box, hover ? track : surface);
    label(title, box.x + 13, box.y + 8, 13, main ? background : ink);
}

void pageHeader(const char* section) {
    gfx::rectangle(0, 0, 640, 47, surface);
    gfx::rectangle(0, 46, 640, 1, outline);
    gfx::rectangle(24, 16, 22, 16, accent);
    label("DX", 27, 16, 11, background);
    label("Battle Bound", 54, 14, 14);
    label(section, 504, 16, 11, muted);
}

}

GameUi::GameUi(bool startInGame) : page(startInGame ? Page::Playing : Page::Title) {}

std::unique_ptr<PeerConnection> GameUi::takeLobbyConnection() {
    if (!lobby) return {};
    auto connection = lobby->takeConnection();
    if (connection) {
        lobby.reset();
        page = Page::Playing;
        waitForMouseRelease = mouseHeld;
    }
    return connection;
}

void GameUi::showConnectionError(std::string message) {
    lobby.reset();
    lobbyError = std::move(message);
    page = Page::Lobby;
}

bool GameUi::update(Runtime& runtime, const RawInput& input) {
    pointer = input.pointer;
    const bool clicked = input.primaryMouse && !mouseHeld;
    mouseHeld = input.primaryMouse;
    if (page == Page::Title && clicked && (over(pointer, hostButton) || over(pointer, joinButton))) {
        hostLobby = over(pointer, hostButton);
        endpoint = hostLobby ? "37026" : "127.0.0.1:37026";
        lobbyError.clear();
        while (gfx::nextCharacter() > 0) {}
        page = Page::Lobby;
        return false;
    }
    if (page == Page::Lobby) {
        if (gfx::keyPressed(KEY_ESCAPE) || (clicked && over(pointer, lobbyBack))) {
            lobby.reset();
            page = Page::Title;
            return false;
        }
        if (lobby) {
            const auto state = lobby->status();
            if (!state.error.empty()) showConnectionError(state.error);
            else if (!state.peer.empty() && (gfx::keyPressed(KEY_ENTER) || (clicked && over(pointer, lobbyAction))))
                lobby->ready();
            return false;
        }
        for (int character; (character = gfx::nextCharacter()) > 0;) {
            if (endpoint.size() < 21 && ((character >= '0' && character <= '9') ||
                                        (!hostLobby && (character == '.' || character == ':'))))
                endpoint += static_cast<char>(character);
        }
        if (gfx::keyPressed(KEY_BACKSPACE) && !endpoint.empty()) endpoint.pop_back();
        if (gfx::keyPressed(KEY_DELETE)) endpoint.clear();
        if (gfx::keyPressed(KEY_ENTER) || (clicked && over(pointer, lobbyAction))) {
            const auto colon = endpoint.find(':');
            const std::string_view value(endpoint);
            const auto portText = hostLobby ? value : colon == std::string::npos ? std::string_view{} : value.substr(colon + 1);
            unsigned port{};
            if (portText.empty()) {
                lobbyError = hostLobby ? "Enter a port from 1 to 65535." : "Enter an IPv4 address and port, like 192.168.1.20:37026.";
                return false;
            }
            const auto parsed = std::from_chars(portText.data(), portText.data() + portText.size(), port);
            if (parsed.ec != std::errc{} || parsed.ptr != portText.data() + portText.size() ||
                !port || port > 65535 || (!hostLobby && colon == 0)) {
                lobbyError = "Enter a valid address and a port from 1 to 65535.";
                return false;
            }
            lobbyError.clear();
            const auto name = runtime.username == "DX_Player" ? (hostLobby ? "Host" : "Guest") : runtime.username;
            lobby = std::make_unique<LobbySession>(port, hostLobby ? "" : endpoint.substr(0, colon), name);
        }
        return false;
    }
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
    if (page == Page::Playing) {
        runtime.draw();
        return;
    }
    if (page == Page::Paused) runtime.draw();
    gfx::rectangle(0, 0, 640, 360, page == Page::Paused ? Color{12, 16, 31, 215} : background);

    if (page == Page::Lobby) {
        pageHeader("Multiplayer");
        label(hostLobby ? "Host a match" : "Join a match", 24, 88, 30);
        label(hostLobby ? "Open a lobby on your local network." : "Connect to a lobby on your local network.", 24, 140, 12, muted);
        label("Both players must ready up.", 24, 164, 12, muted);
        card({264, 72, 332, 233});
        label(hostLobby ? "Listen port" : "Host IPv4 address and port", 282, 87, 12);
        card({282, 108, 294, 38}, background);
        label(endpoint.empty() ? "_" : endpoint, 294, 116, 16);
        const auto state = lobby ? lobby->status() : LobbyStatus{};
        if (!lobbyError.empty()) {
            const auto wrap = lobbyError.size() > 46 ? lobbyError.rfind(' ', 46) : std::string::npos;
            const auto message = wrap == std::string::npos ? lobbyError :
                lobbyError.substr(0, wrap) + "\n" + lobbyError.substr(wrap + 1);
            label(message, 282, 160, 11, {255, 149, 149, 255});
        } else if (!lobby) label("Type to edit. Delete clears the field.", 282, 160, 11, muted);
        else if (state.peer.empty())
            label(hostLobby ? "Waiting for a guest..." : "Connecting to host...", 282, 160, 13, accent);
        else {
            label(lobby->isReady() ? "You are ready" : "You are waiting to ready up", 282, 160, 13, accent);
            label(state.peer.substr(0, 28) + (state.peerReady ? " is ready" : " is waiting"), 282, 184, 13);
        }
        const auto action = !lobby ? (hostLobby ? "Open lobby" : "Connect") :
            state.peer.empty() ? "Waiting..." : lobby->isReady() ? "Ready" : "Ready up";
        button(lobbyAction, action, pointer, !lobby || (!state.peer.empty() && !lobby->isReady()));
        button(lobbyBack, "Back", pointer, false);
        return;
    }
    if (page == Page::Loadout) {
        pageHeader("Offline loadout");
        button(loadoutBack, "<  Back to title", pointer, false);
        label("Build your loadout", 224, 66, 27);
        label("Click a card or use 1, 2 and 3 to cycle gear.", 226, 102, 12, muted);
        constexpr const char* labels[]{"Primary", "Secondary", "Gadget"};
        for (int slot = 0; slot < 3; ++slot) {
            const auto row = loadoutRow(slot);
            const auto item = content::findItem(runtime.loadoutCode(slot));
            card(row, over(pointer, row) ? track : surface);
            label(labels[slot], row.x + 14, row.y + 14, 12, accent);
            label(item ? std::string(content::item(*item).name) : "Empty", row.x + 14, row.y + 49, 15);
            gfx::rectangle(row.x + 14, row.y + 93, row.width - 28, 1, outline);
            label("Change gear  >", row.x + 14, row.y + 108, 12, muted);
        }
        return;
    }

    const bool title = page == Page::Title;
    pageHeader(title ? "Play" : "Paused");
    card({24, 67, 320, 215});
    gfx::rectangle(48, 90, 36, 3, accent);
    label(title ? "Battle Bound DX" : "Match paused", 46, 101, 35);
    label(title ? "Play offline or start a two-player LAN match." : "Resume play or restart the match.", 48, 153, 12, muted);
    button(primary, title ? "Play offline" : "Resume", pointer, true);
    button(restart, title ? "Loadout" : "Restart match", pointer, false);
    if (title) {
        card({350, 67, 266, 141});
        label("Multiplayer", 362, 82, 21);
        label("Connect over your local network.", 362, 118, 12, muted);
        button(hostButton, "Host", pointer, false);
        button(joinButton, "Join", pointer, false);
    }
    const auto controlsY = title ? 218.0f : 67.0f;
    card({350, controlsY, 266, title ? 64.0f : 215.0f});
    label("Controls", 362, controlsY + 8, 13);
    if (title) {
        label("A / D  Move     W  Jump     Left click  Fire", 362, controlsY + 32, 11, muted);
    } else {
        constexpr const char* keys[]{"A / D", "W", "S", "Space", "Left click", "R", "Esc"};
        constexpr const char* actions[]{"Move", "Jump", "Crouch", "Crawl / Dive", "Fire", "Reload", "Resume"};
        for (int row = 0; row < 7; ++row) {
            const auto y = controlsY + 36 + row * 23;
            label(keys[row], 362, y, 12, accent);
            label(actions[row], 470, y, 12, muted);
        }
    }
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
        card({x - 60, y - 100, 120, 200});
        gfx::rectangle(x - 59, y - 77, 118, 1, outline);
        label("QUICK CHAT", x - 52, y - 94, 9, accent);
        for (int row = 0; row < 9; ++row)
            label(std::to_string(row + 1) + "  " + phrases[page - 1][row], x - 52, y - 75 + row * 16, 9);
        label("CLICK TO PAGE  " + std::to_string(page) + "/4", x - 52, y + 76, 8, muted);
        return true;
    }
    if (role == "vote" && digit >= 1 && digit <= 4) {
        const auto& mode = content::matches()[digit - 1];
        const bool selected = runtime.namedValue("::VALG").number() == digit;
        card({x - 48, y - 24, 96, 48}, selected ? track : surface);
        label(std::string(mode.name), x - 41, y - 16, 10);
        label(selected ? "SELECTED" : "VOTE", x - 41, y + 3, 8, accent);
        return true;
    }
    if (role == "game result") {
        card({208, 130, 224, 82});
        gfx::rectangle(209, 131, 222, 2, accent);
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
    card({14, 304, 142, 42});
    label(helicopter ? "AIRFRAME" : "HEALTH", 24, 307, 9, muted);
    label(std::to_string(static_cast<int>(std::ceil(health))), 24, 318, 16);
    gfx::rectangle(66, 327, 78, 4, track);
    gfx::rectangle(66, 327, static_cast<float>(78 * std::clamp(health / maximum, 0.0, 1.0)), 4,
                   health < maximum / 4 ? Color{255, 110, 115, 255} : accent);

    const auto id = content::findItem(runtime.namedValue("::Våpen holdt").text);
    card({484, 304, 142, 42});
    label(id ? std::string(content::item(*id).name) : "Weapon", 494, 307, 10, muted);
    const auto ammo = number("::Ammonisjon");
    const auto capacity = number("Magazine size");
    if (capacity > 0) {
        label(std::to_string(static_cast<int>(std::max(0.0, ammo))) + " / " +
              std::to_string(static_cast<int>(capacity)), 494, 320, 15);
    }
    if (number("Reload Spin") > 0) label("RELOAD", 572, 323, 8, accent);

    card({232, 12, 176, 37});
    const auto mode = number(";Spill-Modus");
    for (const auto& match : content::matches())
        if (mode == static_cast<int>(match.id)) label(std::string(match.name), 244, 16, 12);
    label(runtime.networkMatch() ? "LAN MATCH" : "KILLS  " +
          std::to_string(static_cast<int>(number("::draphehe"))), 244, 32, 9, accent);
    const auto time = runtime.matchSecondsRemaining();
    const auto seconds = time % 60;
    label(std::to_string(time / 60) + ":" + (seconds < 10 ? "0" : "") +
          std::to_string(seconds), 372, 27, 11, muted);
    label(number("#PlayersOnline") <= 1 ? "SOLO" : "PLAYERS  " +
          std::to_string(static_cast<int>(number("#PlayersOnline"))), 14, 15, 9, muted);
    if (const auto* match = runtime.networkMatch()) {
        card({478, 12, 148, 37});
        label("ROUND " + std::to_string(match->round) + "  /  MAP " + std::to_string(match->map), 488, 17, 10, muted);
        label("HOST " + std::to_string(match->hostScore) + "   GUEST " + std::to_string(match->guestScore),
              488, 31, 10, accent);
        if (match->phase == MatchPhase::Countdown) {
            card({250, 74, 140, 40});
            label("MATCH STARTS IN " + std::to_string(runtime.matchSecondsRemaining()), 263, 83, 13, accent);
        } else if (match->phase == MatchPhase::Finished) {
            const auto winner = match->hostScore == match->guestScore ? "Draw" :
                match->hostScore > match->guestScore ? "Host wins" : "Guest wins";
            card({189, 104, 262, 136});
            gfx::rectangle(190, 105, 260, 2, accent);
            label(winner, 208, 119, 24, accent);
            label("Enter to ready for a rematch", 208, 159, 14);
            label(std::string("Host ") + (match->hostReady ? "ready" : "waiting") +
                  "   /   Guest " + (match->guestReady ? "ready" : "waiting"), 208, 194, 12, muted);
            return;
        }
    } else {
        label("ESC  PAUSE", 554, 15, 9, muted);
    }

    if (number(":Kart Åpent") > 0) {
        card({220, 55, 200, 22});
        label("TACTICAL MAP", 232, 58, 10, accent);
        label("M  CLOSE", 361, 59, 8, muted);
        return;
    }
    constexpr const char* slots[]{"PRIMARY", "SECONDARY", "GADGET"};
    const int activeSlot = static_cast<int>(number("inventory scroll"));
    for (int slot = 0; slot < 3; ++slot) {
        const auto left = static_cast<float>(191 + slot * 87);
        card({left, 319, 83, 27}, activeSlot == slot + 1 ? track : surface);
        if (activeSlot == slot + 1) gfx::rectangle(left + 1, 320, 81, 2, accent);
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
