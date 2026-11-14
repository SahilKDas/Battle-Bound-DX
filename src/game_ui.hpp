#pragma once
#include "input.hpp"
#include "lobby.hpp"
#include "preferences.hpp"

namespace bb {
class Runtime;

class GameUi {
public:
    explicit GameUi(bool startInGame, Preferences* preferences = nullptr);
    void setSettingsError(std::string message) { settingsError = std::move(message); }
    bool update(Runtime& runtime, const RawInput& input);
    void draw(Runtime& runtime) const;
    std::unique_ptr<PeerConnection> takeLobbyConnection();
    bool hosting() const { return hostLobby; }
    void showConnectionError(std::string message);
    void returnToLobby(bool host, std::string address, unsigned port, std::string message);

private:
    enum class Page { Title, Loadout, Lobby, Playing, Paused, Settings, Controls };
    Page page;
    Page settingsBack{Page::Title};
    Preferences* preferences{};
    int bindingSlot{-1};
    std::string settingsError;
    StagePoint pointer;
    bool hostLobby{};
    std::string endpoint, lobbyError;
    std::vector<std::string> hostAddresses;
    std::size_t hostAddressIndex{};
    bool addressCopied{};
    std::string hostInvite() const;
    std::unique_ptr<LobbySession> lobby;
    bool mouseHeld{}, waitForMouseRelease{};
};

bool drawMatchPanel(const Runtime& runtime, std::string_view role, std::string_view costume,
                    std::string_view info, int digit, StagePoint position);
void drawGameHud(const Runtime& runtime);
}
