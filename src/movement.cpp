#include "runtime.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace bb {
namespace {
enum class Hitbox { Standing, Crouching, Compact };

struct Movement {
    WorldPoint point;
    double vx, vy, facing, air, ladder, animation, dive, jump;
    bool crouch, crawl;

    Hitbox hitbox() const {
        if (ladder > 0) return Hitbox::Standing;
        if (animation > 6) return Hitbox::Compact;
        return crouch ? Hitbox::Crouching : Hitbox::Standing;
    }

    void beginDive() {
        animation = 7;
        vx += std::sin((90 - vy * 1.8) * facing * (std::numbers::pi / 180)) * 2;
    }

    void steer(const PlayerInput& input, double run, double jumpSpeed,
               auto selectHitbox, auto blocked) {
        if (animation >= 13) return;
        const auto held = [&](Action action) { return input.action(action).held; };
        if (held(Action::MoveRight)) { vx += run; facing = 1; }
        if (held(Action::MoveLeft)) { vx -= run; facing = -1; }
        const bool grounded = air < 4;

        if (held(Action::Crouch) && !crawl) {
            if (grounded) { dive = 0; crouch = true; }
            else if (dive == 0) dive = 1;
            if (dive == 2) beginDive();
        } else {
            selectHitbox(Hitbox::Standing);
            crawl = false;
            crouch = blocked();
            if (crouch) {
                selectHitbox(Hitbox::Crouching);
                crawl = blocked();
                if (crawl) selectHitbox(Hitbox::Compact);
            } else {
                selectHitbox(hitbox());
            }
            if (grounded) dive = 0;
            if (dive == 1) dive = 2;
        }
        if (held(Action::CrawlDive)) {
            if (grounded) {
                selectHitbox(Hitbox::Compact);
                crouch = crawl = true;
            } else if (animation < 7) {
                beginDive();
            }
        }
        if (held(Action::Jump) && !crawl) {
            if (grounded && !crouch) { vy = jumpSpeed; jump = 1; }
            if (jump == 0 && vy > -20) { jump = 2; animation = 8; vy = jumpSpeed; }
        } else if (jump < 2) {
            jump = 0;
        }
    }
};

double roundPixel(double value) { return std::floor(value + 0.5); }
double fallDamage(double velocity) {
    return velocity < -20 ? -roundPixel(velocity * 3 + 60) : 0;
}
double doorOffset(double offset, bool occupied) {
    return offset + ((occupied ? 60.0 : 0.0) - offset) / (occupied ? 20.0 : 10.0);
}
}

void testMovement() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    Movement state{{}, 0, 0, 1, 0, 0, 0, 0, 0, false, false};
    PlayerInput input{};
    Hitbox selected = Hitbox::Standing;
    const auto select = [&](Hitbox hitbox) { selected = hitbox; };
    const auto blocked = [&] { return selected != Hitbox::Compact; };
    const auto clear = [] { return false; };
    const auto hold = [&](Action action, bool held) {
        input.actions[static_cast<std::size_t>(action)].held = held;
    };

    hold(Action::Jump, true);
    state.steer(input, 0.5, 10, select, clear);
    require(state.vy == 10 && state.jump == 1, "Ground jump launches");
    hold(Action::Jump, false);
    state.air = 5;
    state.steer(input, 0.5, 10, select, clear);
    hold(Action::Jump, true);
    state.steer(input, 0.5, 10, select, clear);
    require(state.jump == 2 && state.animation == 8, "Air jump requires release");
    state.animation = 0;
    hold(Action::Jump, false);
    hold(Action::CrawlDive, true);
    state.steer(input, 0.5, 10, select, clear);
    require(state.animation == 7 && state.vx > 0, "Airborne crawl input dives");
    state.animation = 0;
    state.air = 0;
    state.steer(input, 0.5, 10, select, clear);
    require(state.crawl && selected == Hitbox::Compact, "Ground crawl uses compact hitbox");
    hold(Action::CrawlDive, false);
    state.steer(input, 0.5, 10, select, blocked);
    require(state.crawl && selected == Hitbox::Compact, "Low ceiling keeps compact hitbox");
    state.steer(input, 0.5, 10, select, clear);
    require(!state.crawl && selected == Hitbox::Standing, "Clearance restores standing hitbox");
    require(doorOffset(0, true) == 3 && doorOffset(60, false) == 54, "Door easing");
    require(fallDamage(-20) == 0 && fallDamage(-30) == 30, "Fall damage threshold and scale");
}

bool Runtime::runNativeMovement(Task& task, const Block& block, const Frame& frame) {
    if (!nativeMovement) return false;
    auto& actor = *task.actor;
    const auto& definition = definitions[actor.type];
    const auto local = [&](const char* label) -> std::string& {
        for (const auto& [id, name] : definition.variableNames)
            if (name == label) return variableRef(actor, id);
        throw std::runtime_error(std::string("Missing movement state ") + label);
    };
    if (definition.name == "[PLATFORM]" && block.id == "aW") {
        auto& offset = local("_door.y.offset");
        const bool occupied = touching(actor, "[Player]") || touching(actor, "[Opponents]");
        offset = Value(doorOffset(Value(offset).number(), occupied)).text;
        return true;
    }
    const bool horizontal = block.procedure == "change player x by %s";
    const bool vertical = block.procedure == "change player y by %s";
    const bool stance = block.id == "r";
    const bool steer = block.id == "b6";
    const bool friction = block.id == "n;";
    const bool gravity = block.id == "x";
    if (definition.name != "[Player]" ||
        !(horizontal || vertical || stance || steer || friction || gravity)) return false;

    const auto global = [&](const char* name) { return namedValue(name).number(); };
    const auto get = [&](const char* name) { return Value(local(name)).number(); };
    const auto put = [&](const char* name, double value) { local(name) = Value(value).text; };
    const auto argument = [&](const char* name) {
        if (const auto it = frame.arguments.find(name); it != frame.arguments.end())
            return Value(it->second).number();
        for (auto scope = task.frames.rbegin(); scope != task.frames.rend(); ++scope)
            if (const auto it = scope->arguments.find(name); it != scope->arguments.end())
                return Value(it->second).number();
        return 0.0;
    };
    Movement move{{global("::Player X"), global("::Player Y")}, global("::sx"), global("::sy"),
                  global("::Høyre/Venstre"), get("in_air"), global("STIGE"), global("::Animation"),
                  get("dive"), get("double jump"), global("::Krypegå") == 1, get("_Crawling") == 1};
    const auto select = [&](Hitbox hitbox) {
        switch (hitbox) {
        case Hitbox::Standing: costume(actor, "HITBOX"); break;
        case Hitbox::Crouching: costume(actor, "HITBOX CROUCH"); break;
        case Hitbox::Compact: costume(actor, "HITBOX FLIP / DIVE"); break;
        }
    };
    const auto blocked = [&] { return touching(actor, "[PLATFORM]"); };
    const auto save = [&] {
        setNamed("::sx", move.vx);
        setNamed("::sy", move.vy);
        setNamed("::Høyre/Venstre", move.facing);
        setNamed("::Animation", move.animation);
        setNamed("::Krypegå", move.crouch ? 1 : 0);
        put("_Crawling", move.crawl ? 1 : 0);
        put("in_air", move.air);
        put("dive", move.dive);
        put("double jump", move.jump);
    };
    if (stance) { select(move.hitbox()); return true; }
    if (steer) { move.steer(playerInput(), argument("run"), argument("jump"), select, blocked); save(); return true; }
    if (friction) { setNamed("::sx", move.vx * argument("friction")); return true; }
    if (gravity) {
        // Preserve the observed ladder and terminal-speed rules of the original port.
        if (move.ladder > 0 && move.animation < 13) {
            setNamed("::sy", 0.0);
            put("in_air", 0);
        } else if (move.vy > -50) {
            setNamed("::sy", move.vy + argument("gravity"));
        }
        return true;
    }

    const WorldPoint camera{global(";CAM X"), global(";CAM Y")};
    const auto project = [&] { position(actor, move.point.x - camera.x, move.point.y - camera.y); };
    const auto dust = [&] {
        setNamed("*Dust x", move.point.x);
        setNamed("*Dust y", move.point.y - 12);
    };
    const auto damage = [&](const char* name, double amount, const char* source) {
        const auto before = namedValue(name);
        const Value after(before.number() - amount);
        setNamed(name, after);
        if (captureEvents && before.text != after.text && std::string_view(name) == "::Spiller-liv")
            gameplayEvents.push_back({name, definition.name, source, before.text, after.text});
    };
    const auto healthTimer = [&](double adjustment) { setNamed("Health Timer", now - timerOrigin + adjustment); };

    if (horizontal) {
        if (std::abs(move.point.x - camera.x) >= global("&Screen Length x")) return true;
        const auto delta = argument("sx");
        move.point.x += delta;
        project();
        if (blocked()) {
            if (namedValue("::[Spiller-action]").text == "heli" && std::abs(move.vx) > 13) {
                damage("::Helicopter HP", 250, "TH");
                healthTimer(0);
                playActorSound(actor, "Negative Hitmark");
                move.vx = -move.vx;
            }
            const auto initialY = move.point.y;
            for (int step = 0; step < 8 && blocked(); ++step) {
                ++move.point.y;
                project();
            }
            if (blocked()) {
                move.point.y = initialY;
                int steps = 0;
                while (steps < 201 && blocked()) {
                    if (std::abs(move.vx) < 16) move.vx *= 0.8;
                    move.point.x += delta > 0 ? -1 : 1;
                    ++steps;
                    project();
                }
                setNamed("loop", steps);
            }
        }
        setNamed("::sx", move.vx);
        setNamed("::Player X", move.point.x);
        setNamed("::Player Y", move.point.y);
        return true;
    }

    if (std::abs(move.point.y - camera.y) >= global("&Screen Length y")) return true;
    const auto delta = argument("sy");
    move.point.y += delta;
    ++move.air;
    project();
    bool landed = get("_landed") == 1;
    int steps = 0;
    while (steps < 201 && blocked()) {
        ++steps;
        move.point.y = roundPixel(move.point.y) + (delta < 0 ? 1 : -1);
        if (delta < 0) move.air = 0;
        project();
        if (steps == 1 && move.vy < -20) {
            playActorSound(actor, "Negative Hitmark");
            playActorSound(actor, "Land");
            if (namedValue(";SCENE").text == "game") {
                damage("::Spiller-liv", fallDamage(move.vy), "nG");
                healthTimer(-3);
            }
            const auto shake = roundPixel(std::abs(move.vy / 2));
            setNamed(";Skjerm-risting", shake);
            cameraSystem.setShakeMagnitude(shake);
            if (move.animation == 7 && !move.crawl) {
                move.jump = 1;
                move.animation = 8;
                move.vx += 4 * move.facing;
            }
            landed = true;
        }
        move.vy = 0;
        if (move.animation == 7 && !move.crawl && !move.crouch &&
            !playerInput().action(Action::Crouch).held && !playerInput().action(Action::CrawlDive).held) {
            move.vx += 0.5 * move.facing;
            move.vy = 8;
            dust();
        }
    }
    if (landed) dust();
    put("_landed", 0);
    setNamed("loop", steps);
    setNamed("::Player Y", move.point.y);
    save();
    return true;
}
}
