#include "runtime.hpp"
#include "replay.hpp"
#include "content.hpp"
#include <iostream>
#include <stdexcept>

namespace bb {
int testArsenalCombat(const std::filesystem::path& root) {
    Runtime native(root, true), legacy(root, true);
    native.load();
    legacy.load();
    legacy.useLegacyCombat(true);
    const auto tape = exerciseTape(115, 3005, 1793966460);
    const auto prepare = [&](Runtime& runtime, const content::ItemDefinition& item, std::size_t slot) {
        runtime.loadoutSelection.fill(std::nullopt);
        runtime.setDeterministic(tape.seed, tape.epoch);
        runtime.captureEvents = true;
        runtime.greenFlag();
        constexpr std::array lowCost{"e", "a", "i"};
        for (std::size_t index = 0; index < lowCost.size(); ++index) {
            const auto code = index == slot ? item.legacyCode : lowCost[index];
            for (int attempt = 0; attempt < 26 && runtime.loadoutCode(index) != code; ++attempt)
                runtime.cycleLoadout(index);
            if (runtime.loadoutCode(index) != code)
                throw std::runtime_error("Weapon is missing from loadout: " + std::string(code));
        }
        runtime.startGame();
    };
    int checked = 0;
    for (const auto& item : content::items()) {
        if (item.category == content::ItemCategory::Vehicle) continue;
        const auto slot = static_cast<std::size_t>(item.category);
        prepare(native, item, slot);
        prepare(legacy, item, slot);
        bool equipped = false;
        for (std::size_t tick = 0; tick < tape.frames.size(); ++tick) {
            auto input = tape.frames[tick];
            input.keys.clear();
            if (tick == 15) input.keys.insert(std::to_string(slot + 1));
            input.fire = tick >= 22 && tick < 70;
            for (auto* runtime : {&native, &legacy}) {
                runtime->applyInput(input);
                runtime->tick(1.0 / 30);
            }
            equipped = equipped || native.namedValue("::Våpen holdt").text == item.legacyCode;
            const auto fail = [&](std::string_view detail) {
                throw std::runtime_error(std::string(item.name) + " replay tick " +
                                         std::to_string(tick) + " differed in " + std::string(detail));
            };
            for (const auto* field : {"::Våpen holdt", "::Ammonisjon", "::Spiller-liv", "::Player X",
                                      "::Player Y", "::sx", "::sy", "::[Spiller-action]", "::Slåing",
                                      "::Rekyl", "Bow Load", "Nunchuks", "Reload Spin"})
                if (native.namedValue(field).text != legacy.namedValue(field).text) fail(field);
            if (native.bulletClones != legacy.bulletClones || native.actors.size() != legacy.actors.size() ||
                native.taskCount() != legacy.taskCount() || native.randomDrawCount() != legacy.randomDrawCount() ||
                native.gameplayEvents != legacy.gameplayEvents) fail("events or actor lifecycle");
        }
        if (!equipped) throw std::runtime_error("Replay did not equip " + std::string(item.name));
        ++checked;
    }
    std::cout << checked << " loadout items matched Scratch replay state and events.\n";
    return 0;
}

int testOpponentCombat(const std::filesystem::path& root) {
    Runtime runtime(root, true);
    runtime.load();
    runtime.setDeterministic(3005, 1793966460);
    runtime.greenFlag();
    runtime.startGame();
    const auto tape = exerciseTape(22, 3005, 1793966460);
    for (const auto& input : tape.frames) {
        runtime.applyInput(input);
        runtime.tick(1.0 / 30);
    }

    for (int tick = 0; tick < 150 &&
         runtime.simulationTime() - runtime.timerOrigin < runtime.namedValue("SPAWN PROTECTION").number();
         ++tick) {
        runtime.applyInput({});
        runtime.tick(1.0 / 30);
    }
    if (runtime.simulationTime() - runtime.timerOrigin < runtime.namedValue("SPAWN PROTECTION").number())
        throw std::runtime_error("Opponent fixture is still spawn protected");

    const auto opponent = std::find_if(runtime.actors.begin(), runtime.actors.end(), [&](const auto& actor) {
        return !actor->clone && runtime.definitions[actor->type].name == "[Opponents]";
    });
    if (opponent == runtime.actors.end()) throw std::runtime_error("Missing opponent definition");
    const auto& definition = runtime.definitions[(*opponent)->type];
    const auto set = [&](std::string_view name, const Value& value) {
        for (const auto& [id, label] : definition.variableNames) {
            if (name == label) { runtime.variableRef(**opponent, id) = value.text; return; }
        }
        throw std::runtime_error("Missing opponent fixture field " + std::string(name));
    };
    const auto x = runtime.namedValue("::Player X").number();
    const auto y = runtime.namedValue("::Player Y").number();
    const auto opponentType = (*opponent)->type;
    set("_name", "ReplayOpponent");
    set("_ride.username", "");
    set("_item.held", "d");
    set("_ARM.Start.x", x - 140);
    set("_ARM.Start.y", y + 23);
    set("_ARM.direction", 90);
    set("_top.arm.Direction", 90);
    set("_RL", 1.0);
    set("_timer", 0.0);
    const auto& shoot = definition.blocks.at(definition.procedures.at("Skyte timer %s Type %s Clone ID %s"));
    const auto task = runtime.spawn(*opponent, shoot.next);
    task->frames.back().procedure = true;
    task->frames.back().warp = true;
    task->frames.back().arguments = {{"timer", "0.07"}, {"type", "2"}, {"clone id", "bullet"}};
    runtime.captureEvents = true;
    int hits = 0, contacts = 0;
    double damage = 0;
    std::string flight;
    for (int tick = 0; tick < 20; ++tick) {
        runtime.applyInput({});
        runtime.tick(1.0 / 30);
        for (auto& candidate : runtime.actors) {
            if (candidate->type != opponentType || !candidate->clone ||
                runtime.weaponVariable(*candidate, "___ID") != "bullet") continue;
            if (tick < 8) flight += " [" + runtime.weaponVariable(*candidate, "_x") + "," +
                runtime.weaponVariable(*candidate, "_y") + " hit=" +
                runtime.weaponVariable(*candidate, "_bullet.hit") + "]";
        }
        for (const auto& event : runtime.gameplayEvents) {
            if (event.kind == "projectile-contact") {
                flight += " " + event.after;
                if (event.after == "player") ++contacts;
            }
            if (event.kind == "opponent-impact" || event.kind == "::Spiller-liv")
                flight += " " + event.kind + "=" + event.before + ">" + event.after;
            if (event.kind == "::Spiller-liv" && event.sprite == "[Opponents]") {
                ++hits;
                damage += Value(event.before).number() - Value(event.after).number();
            }
        }
    }
    if (hits != 1 || contacts != 1 || (damage != 7 && damage != 14) ||
        runtime.namedValue("::Spiller-liv").number() != 100 - damage ||
        runtime.namedValue("::Spiller-truffet-av").text != "ReplayOpponent")
        throw std::runtime_error("Opponent SMG shot failed the damage/attribution check, health=" +
                                 runtime.namedValue("::Spiller-liv").text + " target=" + Value(x).text + "," + Value(y).text + flight);
    std::cout << "Opponent SMG fired through the live simulation and dealt " << damage << " damage once.\n";
    return 0;
}
}
