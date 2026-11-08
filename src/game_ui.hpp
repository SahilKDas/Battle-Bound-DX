#pragma once
#include "input.hpp"
#include "lobby.hpp"

namespace bb {
class Runtime;

class GameUi {
public:
    explicit GameUi(bool startInGame);
    bool update(Runtime& runtime, const RawInput& input);
    void draw(Runtime& runtime) const;
    std::unique_ptr<PeerConnection> takeLobbyConnection();
    bool hosting() const { return hostLobby; }
    void showConnectionError(std::string message);

private:
    enum class Page { Title, Loadout, Lobby, Playing, Paused };
    Page page;
    StagePoint pointer;
    bool hostLobby{};
    std::string endpoint, lobbyError;
    std::unique_ptr<LobbySession> lobby;
    bool mouseHeld{}, waitForMouseRelease{};
};

bool drawMatchPanel(const Runtime& runtime, std::string_view role, std::string_view costume,
                    std::string_view info, int digit, StagePoint position);
void drawGameHud(const Runtime& runtime);
}
