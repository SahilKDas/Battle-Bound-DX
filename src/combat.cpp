#include "runtime.hpp"
#include "content.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <numbers>

namespace bb {
namespace {
struct FireCycle {
    double lastShot;
    bool ready(double now, double interval) const { return now > lastShot + interval; }
};

enum class DamageKind { Direct, Headshot, Shield };

double projectileDamage(double base, DamageKind kind) {
    switch (kind) {
    case DamageKind::Direct: return base;
    case DamageKind::Headshot: return base * 2;
    case DamageKind::Shield: return std::floor(base / 3 + 0.5);
    }
    return base;
}

struct ProjectileStep {
    WorldVelocity velocity;
    double gravity;
    void advance(WorldPoint& point) const {
        point.x += velocity.x;
        point.y += velocity.y;
        point.y += gravity;
    }
};
}

void testCombat() {
    if (FireCycle{1}.ready(1.08, 0.08) || !FireCycle{1}.ready(1.1, 0.08))
        throw std::runtime_error("SMG cadence uses a strict cooldown");
    if (projectileDamage(-7, DamageKind::Direct) != -7 ||
        projectileDamage(-7, DamageKind::Headshot) != -14 ||
        projectileDamage(-7, DamageKind::Shield) != -2 ||
        projectileDamage(8, DamageKind::Direct) != 8)
        throw std::runtime_error("Projectile damage and medic healing");
}

bool Runtime::runNativeCombat(Task& task, const Block& block, Frame& call) {
    if (!nativeCombat) return false;
    auto& actor = *task.actor;
    const auto& definition = definitions[actor.type];
    const bool remote = definition.name == "[Opponents]";
    if (definition.name != "[Bullet]" && !remote) return false;
    const bool fire = block.procedure == "Shoot Timer %s Recoil %s backpower %s ID %s";
    const bool axe = block.procedure == "Axe timer %s direction offset %s";
    const bool punch = block.procedure == "Punch timer %s";
    const bool grenade = block.procedure == "grenade timer %s";
    const bool reload = block.procedure == "Inventory and magazines %s";
    const bool move = block.procedure == (remote ? "kule presisjon bevegelse %s %s %s %s"
                                                 : "Precision move %s %s %s %s");
    const bool trajectory = block.procedure == "Calculate sin cos %s %s";
    const bool remoteImpact = remote && block.procedure == "Kule truffet %s";
    if (remote && !move && !trajectory && !remoteImpact) return false;
    const bool bounce = block.procedure == "Grenade";
    const bool explosion = block.procedure == "Explosion";
    const bool arrowImpact = block.procedure == "Arrow Hit %s";
    const bool impact = block.procedure == "Bullet hit %s";
    if (!(fire || axe || punch || grenade || reload || move || impact || trajectory || remoteImpact || bounce || explosion || arrowImpact)) return false;

    const auto local = [&](std::string_view label) -> std::string& { return weaponVariable(actor, label); };
    const auto get = [&](std::string_view name) { return Value(local(name)).number(); };
    const auto put = [&](std::string_view name, double value) { local(name) = Value(value).text; };
    const auto argument = [&](const char* label) { return Value(call.arguments.at(label)); };
    const auto global = [&](const char* name) { return namedValue(name).number(); };
    const auto stageList = [&](std::string_view label) -> std::vector<Value>& {
        for (const auto& [id, name] : definitions[0].listNames)
            if (name == label) return list(actor, id);
        for (const auto& [id, name] : definition.listNames)
            if (name == label) return list(actor, id);
        throw std::runtime_error("Missing combat list " + std::string(label));
    };
    const auto find = [](const std::vector<Value>& values, const Value& wanted) {
        return std::find_if(values.begin(), values.end(), [&](const Value& entry) {
            return equal(entry, wanted);
        }) - values.begin();
    };
    const auto entry = [](const std::vector<Value>& values, std::size_t index) {
        return index < values.size() ? values[index] : Value("");
    };
    const auto replace = [](std::vector<Value>& values, std::size_t index, Value value) {
        if (index < values.size()) values[index] = std::move(value);
    };
    const double timer = now - timerOrigin;

    const auto continueAt = [&](std::string_view id) {
        const auto next = std::find_if(definition.blocks.begin(), definition.blocks.end(),
                                      [&](const Block& source) { return source.id == id; });
        if (next == definition.blocks.end()) throw std::runtime_error("Missing combat continuation");
        call.pc = static_cast<int>(next - definition.blocks.begin());
    };
    if (axe || punch || grenade) {
        if (!FireCycle{get("_timer")}.ready(timer, argument("timer").number())) return true;
        setNamed("crosshair grow", 1.0);
        put("_timer", timer);
        setNamed("::Slåing", axe ? -15.0 : -5.0);
        if (axe) setNamed("Player ARM Direction offset", argument("offset").number() * global("::Høyre/Venstre"));
        setNamed("::Rekyl", 1.0);
        if (grenade) {
            continueAt("aQC");
            return false;
        }
        playActorSound(actor, "Punch");
        return true;
    }
    if (fire) {
        if (!FireCycle{get("_timer")}.ready(timer, argument("timer").number())) return true;
        put("_timer", timer);
        setNamed("::Rekyl", global("::Rekyl") + argument("recoil").number());
        if (namedValue("::[Spiller-action]").text.find("heli") == std::string::npos) {
            const auto slot = find(stageList("-INVENTORY"), namedValue("::Våpen holdt"));
            auto& magazines = stageList("Inventory Magazines");
            replace(magazines, slot, entry(magazines, slot).number() - 1);
        }
        // The existing clone entry point schedules effects and weapon-specific payloads.
        continueAt("s8");
        return false;
    }

    if (reload) {
        const auto held = argument("item held like hwat");
        const auto& inventory = stageList("-INVENTORY");
        const auto& items = stageList("ITEMS");
        const auto& capacities = stageList("magazine size");
        auto& magazines = stageList("Inventory Magazines");
        const auto slot = find(inventory, held);
        const auto capacity = entry(capacities, find(items, held));
        setNamed("Magazine size", capacity);
        auto& reloadId = local("_reload ID");
        double elapsed = get("_reloaded");
        const auto readAmmo = [&](const Value& item) { return entry(magazines, find(inventory, item)); };
        if (playerInput().action(Action::Reload).held &&
            std::string_view("bdghjnoprsuy").find(held.text) != std::string_view::npos &&
            entry(magazines, slot).number() < capacity.number()) {
            put("_timer", timer);
            replace(magazines, slot, "0");
        }
        if (equal(readAmmo(reloadId), "0") || equal(entry(magazines, slot), "0")) {
            if (!equal(reloadId, held) && equal(entry(magazines, slot), "0")) {
                elapsed = 0;
                reloadId = held.text;
            }
            ++elapsed;
            if (elapsed == 1) {
                reloadId = held.text;
                actor.effects.erase("pitch");
                actor.effects.erase("pan");
                if (held.text == "r") playActorSound(actor, "Eject");
                if (held.text != "j") playActorSound(actor, "pistol reload");
            }
            const auto duration = [&](const std::string& code) {
                const auto item = content::findItem(code);
                return item ? content::item(*item).reloadSeconds.value_or(0) * 30 : 0;
            };
            if (elapsed > duration(reloadId)) {
                const auto reloadSlot = find(inventory, reloadId);
                replace(magazines, reloadSlot, entry(capacities, reloadSlot));
                put("_timer", 0);
                elapsed = 0;
                reloadId.clear();
            }
            setNamed("Reload Spin", elapsed * (360.0 / duration(reloadId)));
        } else {
            elapsed = 0;
            setNamed("Reload Spin", 0.0);
            reloadId.clear();
        }
        put("_reloaded", elapsed);
        const auto ammo = entry(magazines, slot);
        // Empty magazines for melee and utility items compare lexically in the source.
        const bool overCapacity = capacity.text.empty() ? !ammo.text.empty() : ammo.number() > capacity.number();
        if ((std::string_view("bdghjpnorsuy").find(held.text) != std::string_view::npos &&
             (ammo.text.empty() || ammo.number() < 0)) || overCapacity)
            replace(magazines, slot, capacity);
        const auto before = namedValue("::Ammonisjon");
        const auto after = entry(magazines, slot);
        setNamed("::Ammonisjon", after);
        if (captureEvents && before.text != after.text)
            gameplayEvents.push_back({"::Ammonisjon", definition.name, "!J", before.text, after.text});
        return true;
    }

    const char* x = remote ? "_x" : "_,x";
    const char* y = remote ? "_y" : "_,y";
    if (trajectory) {
        const double radians = argument("direction").number() * (std::numbers::pi / 180);
        const double speed = argument("speed").number();
        put("_sin", std::sin(radians) * speed);
        put("_cos", std::cos(radians) * speed);
        return true;
    }
    WorldPoint point{get(x), get(y)};
    const WorldPoint camera{global(";CAM X"), global(";CAM Y")};
    if (move) {
        actor.projectileContact = ProjectileContact::None;
        const bool helicopter = namedValue("::[Spiller-action]").text.find("heli") != std::string::npos;
        const bool passenger = global("::Helicopter passenger") == 1;
        const bool canHit = remote
            ? !equal(local("_name"), namedValue("::Helicopter.ride Username")) &&
              !equal(local("_ride.username"), username)
            : (!(helicopter || passenger) || get("__-helicopter.timer") > 4);
        put(remote ? "_bullet.no.hit" : "_bullet.collision=true", canHit ? 1 : 0);
        const char* target = remote ? "[Player]" : "[Opponents]";
        const ProjectileStep step{{argument("sin").number(), argument("cos").number()},
                                  argument("G").number()};
        const auto mask = argument("costume");
        for (int sample = 0; sample < 8; ++sample) {
            if (sample != 0) costume(actor, mask);
            const bool terrain = touching(actor, "[PLATFORM]");
            if (terrain || (canHit && touching(actor, target))) {
                actor.projectileContact = terrain ? ProjectileContact::Terrain : ProjectileContact::Actor;
                if (remote && captureEvents)
                    gameplayEvents.push_back({"projectile-contact", definition.name, block.id, "",
                                              terrain ? "terrain" : "player"});
                put("_bullet.hit", get("_bullet.hit") + 1);
                return true;
            }
            step.advance(point);
            put(x, point.x);
            put(y, point.y);
            costume(actor, "SIZE");
            position(actor, point.x - camera.x, point.y - camera.y);
        }
        costume(actor, mask);
        if (remote && (std::abs(point.x - camera.x) > global("&Screen Length x") + 50 ||
                       std::abs(point.y - camera.y) > global("&Screen Length y") + 50))
            put("_bullet.hit", get("_bullet.hit") + 1);
        return true;
    }

    const auto retire = [&] {
        if (actor.clone) actor.alive = task.alive = false;
    };
    if (remoteImpact) {
        const auto age = argument("e").number();
        if (age <= 0 && actor.projectileContact == ProjectileContact::None) return true;
        actor.effects.erase("pitch");
        actor.effects.erase("pan");
        if (actor.projectileContact == ProjectileContact::Terrain) {
            costume(actor, "B" + argument("e").text);
            if (age > 6) retire();
            return true;
        }
        if (actor.projectileContact != ProjectileContact::Actor) {
            if (!touching(actor, "_edge_") || age > 10) retire();
            return true;
        }

        const auto type = get("_bullet type");
        const auto& damages = stageList(".Bullet Damages");
        const int index = listIndex(Value(type), damages.size());
        const auto base = index < 0 ? 0 : damages[index].number();
        if (captureEvents) gameplayEvents.push_back({"opponent-impact", definition.name, block.id,
                                                      Value(type).text, Value(base).text});
        const auto mode = global(";Spill-Modus");
        const bool teams = mode == 1 || mode == 3;
        const bool teammate = teams && equal(namedValue("::Farge"), local("_color"));
        const auto apply = [&](const char* name, double amount, const char* source) {
            const auto before = namedValue(name);
            const Value after(before.number() + amount);
            setNamed(name, after);
            if (captureEvents && std::string_view(name) == "::Spiller-liv" && before.text != after.text)
                gameplayEvents.push_back({name, definition.name, source, before.text, after.text});
        };
        const auto shake = [&] {
            setNamed(";Skjerm-risting", 3.0);
            cameraSystem.setShakeMagnitude(3);
        };
        const auto direction = actor.direction;
        const auto push = [&](double scale) {
            setNamed("::sx", global("::sx") + std::sin(direction * (std::numbers::pi / 180)) * scale);
        };
        if (type == 10 && global("::Spiller-liv") < 1) broadcast("medic-gun shorten death timer");
        if (teammate) {
            if (base > 1) { apply("::Spiller-liv", base, "A7"); shake(); push(2); }
        } else if (!(teams && base > 1)) {
            setNamed("::Spiller-truffet-av", local("_name"));
            setNamed("Health Timer", timer);
            shake();
            const bool helicopter = namedValue("::[Spiller-action]").text.find("heli") != std::string::npos;
            const auto crouch = global("::Krypegå");
            const auto scale = global(";SIZE") * .02;
            const bool headshot = !helicopter &&
                std::abs((global("::Player Y") + (29 - crouch * 15) * scale) - point.y) < 6 &&
                std::abs((global("::Player X") + (crouch * (3 * global("::Høyre/Venstre"))) * scale) - point.x) < 7;
            const auto shieldAngle = global(".Cloud.ARM.Direction");
            const auto opposite = shieldAngle == 0 ? 0 : shieldAngle - 180 * (std::abs(shieldAngle) / shieldAngle);
            const bool shield = !helicopter && namedValue("::Våpen holdt").text == "m" &&
                global("::Animation") < 7 && std::abs(opposite - direction) < 40;
            playActorSound(actor, headshot ? "Headshot" : "Negative Hitmark");
            push(shield ? 1 : 2);
            if (shield) {
                playActorSound(actor, "shield");
                setNamed("*Spark X", point.x);
                setNamed("*Spark Y", point.y);
                setNamed("*Spark Direction", direction);
                apply("::Spiller-liv", projectileDamage(base, DamageKind::Shield), headshot ? "A=" : "/*");
            } else if (helicopter) {
                apply("::Helicopter HP", base, "//");
            } else {
                apply("::Spiller-liv", projectileDamage(base, headshot ? DamageKind::Headshot : DamageKind::Direct),
                      headshot ? "/1" : "/:");
            }
        }
        retire();
        return true;
    }

    const auto outside = [&](double margin) {
        return std::abs(point.x - camera.x) > global("&Screen Length x") + margin ||
               std::abs(point.y - camera.y) > global("&Screen Length y") + margin;
    };
    if (bounce) {
        const bool grenadePayload = local("__ID") == "grenade";
        const auto sound = grenadePayload ? "Grenade Bounce" : "rock";
        costume(actor, "Grenade HITBOX");
        if (std::abs(point.x - camera.x) < global("&Screen Length x") + 4 &&
            std::abs(point.y - camera.y) < global("&Screen Length y") + 4) {
            actor.visible = true;
            WorldVelocity velocity{get("_grenade speed x"), get("_grenade speed y")};
            if (point.y < -570) { velocity.x *= .8; velocity.y *= .5; }
            const auto contact = [&] {
                return touching(actor, "[PLATFORM]") ||
                       (!grenadePayload && touching(actor, "[Opponents]"));
            };
            point.x += velocity.x;
            position(actor, point.x - camera.x, actor.y);
            if (contact()) {
                if (std::abs(velocity.x) > 1) playActorSound(actor, sound);
                velocity.x *= -.8;
                point.x += velocity.x * 1.4;
                position(actor, point.x - camera.x, actor.y);
            }
            velocity.y += global("G");
            point.y += velocity.y;
            position(actor, actor.x, point.y - camera.y);
            if (contact()) {
                if (std::abs(velocity.y) > 2) playActorSound(actor, sound);
                velocity.y *= -.5;
                point.y += velocity.y * 2;
                velocity.x *= .75;
                position(actor, actor.x, point.y - camera.y);
            }
            const auto angle = actor.direction + velocity.x;
            if (std::isfinite(angle)) actor.direction = std::remainder(angle, 360);
            put("_grenade speed x", velocity.x);
            put("_grenade speed y", velocity.y);
            put(x, point.x);
            put(y, point.y);
            costume(actor, grenadePayload ? "Grenade" : "rock");
        } else {
            position(actor, point.x - camera.x, point.y - camera.y);
            actor.visible = false;
        }
        const auto age = get("_bullet.hit") + 1;
        put("_bullet.hit", age);
        if (!grenadePayload && age > 40) actor.effects["ghost"] += 10;
        if (age > 50) {
            if (grenadePayload) { continueAt("aQq"); return false; }
            retire();
        }
        return true;
    }

    if (explosion) {
        if (!touching(actor, "_edge_")) {
            actor.effects["ghost"] = 100;
            actor.size = 100;
            const bool helicopter = namedValue("::[Spiller-action]").text.find("heli") != std::string::npos;
            const bool grenadePayload = local("__ID") == "grenade";
            const auto player = std::find_if(actors.begin(), actors.end(), [&](const auto& target) {
                return !target->clone && definitions[target->type].name == "[Player]";
            });
            const auto px = player == actors.end() ? mx() : (*player)->x;
            const auto py = player == actors.end() ? my() : (*player)->y;
            const auto radius = global("Grenade Radius") * (helicopter && !grenadePayload ? 2 : 1);
            const auto force = radius - std::floor(std::hypot(px - actor.x, py - actor.y) + .5);
            put("_clicktimer", force);
            costume(actor, "Range");
            if (force > 0) {
                const auto animation = global("::Animation");
                const bool protectedPose = animation == 9 || animation == 10;
                // The source converts its direction comparison to zero before arithmetic.
                setNamed("::sx", global("::sx") - force / (helicopter || protectedPose ? 3 : 1.5));
                if (helicopter) {
                    setNamed("::Helicopter HP", global("::Helicopter HP") + force * (grenadePayload ? -.8 : -1));
                } else {
                    if (!protectedPose) setNamed("::sy", global("::sy") + force / 6);
                    const auto before = namedValue("::Spiller-liv");
                    const auto scale = protectedPose ? (grenadePayload ? -.8 : -.5) :
                                                       (grenadePayload ? -1 : -.7);
                    const Value after(before.number() + force * scale);
                    setNamed("::Spiller-liv", after);
                    if (captureEvents && before.text != after.text)
                        gameplayEvents.push_back({"::Spiller-liv", definition.name,
                            protectedPose ? (grenadePayload ? "3_" : "3`") : (grenadePayload ? "4a" : "4b"),
                            before.text, after.text});
                }
                setNamed("Health Timer", timer);
            }
            if (touching(actor, "[Opponents]")) {
                playActorSound(actor, "Positive Hitmark");
                if (grenadePayload && !equal(namedValue(";Spill-Modus"), "3"))
                    setNamed("::Player.INFO", global("::Player.INFO") + std::floor(force * .7 + .5));
            }
            setNamed(";Skjerm-risting", 15.0);
            cameraSystem.setShakeMagnitude(15);
            if (multiplayerEnabled && captureEvents && grenadePayload)
                gameplayEvents.push_back({"effect", definition.name, "grenade-blast",
                                          Value(point.x).text, Value(point.y).text});
            setNamed("*Explosion X", point.x);
            setNamed("*Explosion Y", point.y);
        }
        retire();
        return true;
    }

    if (arrowImpact) {
        const auto age = argument("smn").number();
        if (age > 0) {
            actor.effects.erase("pitch");
            actor.effects.erase("pan");
            if (touching(actor, "[Opponents]")) {
                auto& statistics = stageList("Statistics");
                replace(statistics, 6, entry(statistics, 6).number() + 1);
                playActorSound(actor, "Positive Hitmark");
                retire();
            } else {
                if (age == 1 && local("__ID") == "shuriken") playActorSound(actor, "shuriken");
                if (age > 50) actor.effects["ghost"] += 5;
                if (age > 70) retire();
            }
        }
        if (outside(10)) retire();
        return true;
    }
    const auto hitAge = argument("hi");
    if (hitAge.number() > 0) {
        actor.effects.erase("pitch");
        actor.effects.erase("pan");
        if (touching(actor, "[PLATFORM]")) {
            costume(actor, "B" + hitAge.text);
            if (hitAge.number() > 6) retire();
        } else {
            if (touching(actor, "[Opponents]")) {
                auto& statistics = stageList("Statistics");
                replace(statistics, 6, entry(statistics, 6).number() + 1);
                playActorSound(actor, "Positive Hitmark");
                if (!equal(namedValue(";Spill-Modus"), "3"))
                    setNamed("::Player.INFO", global("::Player.INFO") + 5);
            }
            retire();
        }
    }
    if (std::abs(point.x - camera.x) > global("&Screen Length x") + 30 ||
        std::abs(point.y - camera.y) > global("&Screen Length y") + 30)
        retire();
    return true;
}
}
