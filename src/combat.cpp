#include "runtime.hpp"
#include "content.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

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

std::optional<Value> Runtime::nativeProjectileDamage(Actor& actor, const Block& block) {
    if (!nativeCombat || definitions[actor.type].name != "[Opponents]") return std::nullopt;
    DamageKind kind;
    if (block.id == "A7" || block.id == "//" || block.id == "/:") kind = DamageKind::Direct;
    else if (block.id == "/1") kind = DamageKind::Headshot;
    else if (block.id == "A=" || block.id == "/*") kind = DamageKind::Shield;
    else return std::nullopt;
    const auto& locals = definitions[actor.type].variableNames;
    const auto type = std::find_if(locals.begin(), locals.end(), [](const auto& item) {
        return item.second == "_bullet type";
    });
    const auto& lists = definitions[0].listNames;
    const auto damage = std::find_if(lists.begin(), lists.end(), [](const auto& item) {
        return item.second == ".Bullet Damages";
    });
    if (type == locals.end() || damage == lists.end())
        throw std::runtime_error("Missing projectile damage data");
    auto& values = list(actor, damage->first);
    const auto index = listIndex(variable(actor, type->first), values.size());
    return Value(projectileDamage(index < 0 ? 0 : values[index].number(), kind));
}

bool Runtime::runNativeCombat(Task& task, const Block& block, Frame& call) {
    if (!nativeCombat) return false;
    auto& actor = *task.actor;
    const auto& definition = definitions[actor.type];
    if (definition.name != "[Bullet]") return false;
    const bool fire = block.procedure == "Shoot Timer %s Recoil %s backpower %s ID %s";
    const bool axe = block.procedure == "Axe timer %s direction offset %s";
    const bool punch = block.procedure == "Punch timer %s";
    const bool grenade = block.procedure == "grenade timer %s";
    const bool reload = block.procedure == "Inventory and magazines %s";
    const bool move = block.procedure == "Precision move %s %s %s %s";
    const bool impact = block.procedure == "Bullet hit %s";
    if (!(fire || axe || punch || grenade || reload || move || impact)) return false;

    const auto local = [&](std::string_view label) -> std::string& {
        for (const auto& [id, name] : definition.variableNames)
            if (name == label) return variableRef(actor, id);
        throw std::runtime_error("Missing combat state " + std::string(label));
    };
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

    WorldPoint point{get("_,x"), get("_,y")};
    const WorldPoint camera{global(";CAM X"), global(";CAM Y")};
    if (move) {
        // The source VM's boolean reporter is nonnumeric in this multiplication.
        put("_bullet.collision=true", 0);
        const ProjectileStep step{{argument("sin").number(), argument("cos").number()},
                                  argument("G").number()};
        const auto mask = argument("costume");
        for (int sample = 0; sample < 8; ++sample) {
            if (sample != 0) costume(actor, mask);
            if (touching(actor, "[PLATFORM]")) {
                put("_bullet.hit", get("_bullet.hit") + 1);
                return true;
            }
            step.advance(point);
            put("_,x", point.x);
            put("_,y", point.y);
            costume(actor, "SIZE");
            position(actor, point.x - camera.x, point.y - camera.y);
        }
        costume(actor, mask);
        return true;
    }

    const auto retire = [&] {
        if (actor.clone) actor.alive = task.alive = false;
    };
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
