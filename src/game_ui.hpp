#pragma once
#include "input.hpp"

namespace bb {
class Runtime;

class GameUi {
public:
    explicit GameUi(bool startInGame);
    bool update(Runtime& runtime, const RawInput& input);
    void draw(Runtime& runtime) const;

private:
    enum class Page { Title, Loadout, Playing, Paused };
    Page page;
    StagePoint pointer;
    bool mouseHeld{}, waitForMouseRelease{};
};

bool drawMatchPanel(const Runtime& runtime, std::string_view role, std::string_view costume,
                    std::string_view info, int digit, StagePoint position);
void drawGameHud(const Runtime& runtime);
}
