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
    enum class Page { Title, Playing, Paused };
    Page page;
    StagePoint pointer;
    bool mouseHeld{}, waitForMouseRelease{};
};

void drawGameHud(const Runtime& runtime);
}
