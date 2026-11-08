#include "runtime.hpp"
#include "content.hpp"
#include <stdexcept>
#include <numbers>

namespace bb {
namespace {
struct Firearm {
    content::ItemId item;
    double cooldown, recoil, kick;
    const char* payload;
    const char* actionBlock;
};
using content::ItemId;
constexpr Firearm firearms[]{
    {ItemId::Pistol, .3, 5, 1, "bullet", "5q"},
    {ItemId::Smg, .08, .5, 1, "bullet", "5u"},
    {ItemId::Shotgun, 1, 6, 2, "shotgun", "5C"},
    {ItemId::Sniper, 1.6, 4, 1, "bullet", "5E"},
    {ItemId::RocketLauncher, .5, 5, 5, "rpg", "5M"},
    {ItemId::MachineGun, .05, 0, 1, "bullet", "5V"},
    {ItemId::AssaultRifle, .09, 0, 1, "bullet", "5X"},
    {ItemId::HeavyPistol, .8, 10, 1, "bullet", "5Z"},
    {ItemId::SemiAutoRifle, .35, 3, 1, "bullet", "53"},
    {ItemId::LightSmg, .04, 0, 1, "bullet", "55"},
    {ItemId::MedicGun, .4, 6, 1, "bullet", "5%"},
    {ItemId::Helicopter, 1.5, 0, 0, "rpg", "5m"},
};
}

std::string& Runtime::weaponVariable(Actor& actor, std::string_view name) {
    const auto& definition = definitions[actor.type];
    // Several retired Scratch variables share the active variables' display names.
    if (definition.name == "[Bullet]") {
        constexpr std::pair<std::string_view, const char*> active[]{
            {"_timer", "YmsD]KxZO#z#_!O3{8FL"}, {"__ID", "vTP5kt-zPx;x`)_p#02i"},
            {"_sin", "fU+N*,wPb%sG7_mSq|j["}, {"_cos", "fA_L)?Z.vx|@RXoo2o.+"},
            {"_bullet.hit", "1?W379dctsZwRGddh48K"}, {"_arrow speed", "V@0(Wm4E{lN:{?e+{n$p"},
            {"_arrow G", "Mk-@6ne}ifK0(`5R.a+W"}, {"_grenade speed x", "S6.Q]$]zQ{]Q3hkI*r6d"},
            {"_grenade speed y", "%p*6Mm1^w@1Z]i8Qc{}b"},
        };
        for (const auto& [label, id] : active)
            if (name == label) return variableRef(actor, id);
    }
    for (const auto& [id, label] : definition.variableNames)
        if (name == label) return variableRef(actor, id);
    throw std::runtime_error("Missing weapon state " + std::string(name));
}

bool Runtime::runNativeWeaponAction(Task& task, const Block& block) {
    if (!nativeCombat || (block.id != "-" && block.id != "d?" && block.id != "6h")) return false;
    auto& actor = *task.actor;
    const auto& definition = definitions[actor.type];
    if (definition.name != "[Bullet]") return false;

    const auto local = [&](std::string_view label) -> std::string& { return weaponVariable(actor, label); };
    const auto number = [&](const char* name) { return namedValue(name).number(); };
    const auto readLocal = [&](const char* name) { return Value(local(name)).number(); };
    const auto writeLocal = [&](const char* name, double value) { local(name) = Value(value).text; };
    if (block.id == "d?") {
        task.frames.back().pc = -1;
        if (actor.size < 400) {
            const auto size = actor.size + randomBetween(actor, "aJK", 14, 22);
            costume(actor, size > 100 ? "SIZE2" : "SIZE");
            actor.size = size;
        }
        position(actor, readLocal("_,x") - number(";CAM X"), readLocal("_,y") - number(";CAM Y"));
        costume(actor, "fire");
        const auto age = readLocal("_bullet.hit") + 1;
        writeLocal("_bullet.hit", age);
        actor.effects["brightness"] -= 4;
        actor.effects["ghost"] += 2;
        const auto retire = [&] { if (actor.clone) actor.alive = task.alive = false; };
        if (age > 50) { retire(); return true; }
        if (touching(actor, "[PLATFORM]")) {
            writeLocal("_,x", readLocal("_,x") + randomBetween(actor, "aJP", -1, 1));
            writeLocal("_,y", readLocal("_,y") + randomBetween(actor, "aJQ", -1, 1));
        } else {
            const auto spread = std::floor(actor.size / 200 + .5);
            writeLocal("_,x", readLocal("_,x") + (readLocal("_sin") + randomBetween(actor, "sS", -spread, spread)));
            writeLocal("_,y", readLocal("_,y") + (readLocal("_cos") + randomBetween(actor, "sT", -spread, spread)));
        }
        if (std::abs(actor.x) > number("&Screen Length x") + 5 ||
            std::abs(actor.y) > number("&Screen Length y") + 5) retire();
        return true;
    }

    const auto set = [&](const char* name, Value value, const char* source = "") {
        const auto before = namedValue(name);
        setNamed(name, value);
        if (captureEvents && *source && before.text != value.text)
            gameplayEvents.push_back({name, definition.name, source, before.text, value.text});
    };
    const auto action = [&](Value value, const char* source) { set("::[Spiller-action]", value, source); };
    const auto stageList = [&](std::string_view label) -> std::vector<Value>& {
        for (const auto& [id, name] : definitions[0].listNames)
            if (name == label) return list(actor, id);
        throw std::runtime_error("Missing weapon list " + std::string(label));
    };
    if (block.id == "6h") {
        const auto payload = local("__ID");
        const bool bullet = payload == "bullet";
        const bool rocket = payload == "rpg";
        const bool arrow = payload == "arrow";
        const bool shuriken = payload == "shuriken";
        const bool flame = payload == "fire";
        const bool thrown = payload == "grenade" || payload == "rock";
        if (!(bullet || rocket || arrow || shuriken || flame || thrown)) return false;

        task.frames.back().pc = -1;
        writeLocal("__-helicopter.timer", 0);
        actor.visible = true;
        actor.rotation = "all around";
        const auto held = namedValue("::Våpen holdt");
        if (!flame) {
            const auto& settings = stageList(":SETTINGS:");
            actor.volume = settings.size() > 1 ? std::clamp(settings[1].number(), 0.0, 100.0) : 0;
            if (shuriken) { actor.effects.erase("pitch"); actor.effects.erase("pan"); }
            if (bullet) {
                const auto code = held.text;
                if (code == "b" || code == "p" || code == "s") playActorSound(actor, "Pistol");
                else if (code == "d") playActorSound(actor, "Gun sound");
                else if (code == "g") playActorSound(actor, "Shotgun");
                else if (code == "h" || code == "r") playActorSound(actor, "Sniper");
                else if (code == "n" || code == "o") playActorSound(actor, "M16");
                else if (code == "u") playActorSound(actor, "healing gun like what bro dood cool damm");
                if (code == "h") playActorSound(actor, "Bolt action after bang");
            } else if (rocket) playActorSound(actor, "Grenade Launcher ");
            else if (arrow) playActorSound(actor, "Bow");
            else playActorSound(actor, "Punch");
        }
        if (bullet || rocket || shuriken) {
            auto& statistics = stageList("Statistics");
            if (statistics.size() > 7) statistics[7] = statistics[7].number() + 1;
        }
        const auto layer = [&](bool front) {
            int value = actor.layer;
            for (const auto& other : actors)
                value = front ? std::max(value, other->layer + 1) : std::min(value, other->layer - 1);
            actor.layer = value;
        };
        constexpr auto radians = std::numbers::pi / 180;
        const auto round = [](double value) { return std::floor(value + .5); };
        const auto facing = number("::Høyre/Venstre");
        const auto aim = readLocal("_direction");
        const auto scale = number(";SIZE");
        const WorldPoint camera{number(";CAM X"), number(";CAM Y")};
        const WorldPoint arm{number(".Point arm X"), number(".Point arm Y")};
        WorldPoint origin{arm.x + camera.x, arm.y + camera.y};

        if (thrown) {
            layer(true);
            actor.effects.clear();
            const auto angle = round(aim + 30 * facing) * radians;
            const auto speed = payload == "grenade" ? 15 : 17;
            writeLocal("_grenade speed x", round(std::sin(angle) * speed));
            writeLocal("_grenade speed y", round(std::cos(angle) * speed));
        } else {
            double angle = aim;
            if (bullet || rocket) angle += randomBetween(actor, bullet ? "aL," : "aMp", 1, 3) * facing;
            if (shuriken) angle += 30 * facing;
            if (std::isfinite(angle)) actor.direction = std::remainder(angle, 360);
            const auto sine = std::sin(actor.direction * radians);
            const auto cosine = std::cos(actor.direction * radians);
            const auto speed = rocket ? 4 : arrow ? readLocal("_arrow speed") / 2 :
                               shuriken ? std::abs(sine) + 2 : 6;
            writeLocal("_sin", sine * speed);
            writeLocal("_cos", cosine * speed);
            if (bullet || rocket || flame) {
                const bool helicopter = rocket && namedValue("::[Spiller-action]").text.find("heli") != std::string::npos;
                const auto offset = helicopter ? 0 : rocket ? 4 : (flame ? 3 : 4) * (scale * .02);
                const auto shifted = (actor.direction - 90) * radians;
                origin = {round((arm.x + std::sin(shifted) * offset * facing) + camera.x),
                          round((arm.y + std::cos(shifted) * offset * facing) + camera.y)};
                if (helicopter) layer(true);
                if (!flame) {
                    setNamed("*Gun Blast Gun", helicopter ? Value("heli") : held);
                    setNamed("*Gun blast direction", round(actor.direction));
                    setNamed("*Gun Blast x", origin.x);
                    setNamed("*Gun Blast y", origin.y);
                } else {
                    origin.x += readLocal("_sin") * 3;
                    origin.y += readLocal("_cos") * 3;
                }
            }
            writeLocal("_bullet.hit", 0);
            if (arrow || shuriken || flame) writeLocal("_arrow G", 0);
        }
        writeLocal("_,x", origin.x);
        writeLocal("_,y", origin.y);
        position(actor, origin.x - camera.x, origin.y - camera.y);
        costume(actor, scale > 100 ? "SIZE2" : "SIZE");
        if (std::isfinite(scale)) actor.size = std::clamp(scale, .01, 10000.0);
        actor.effects.clear();
        if (bullet) {
            costume(actor, "Bullet");
            if (held.text == "u") actor.effects["color"] = -18;
        } else if (rocket) costume(actor, "RPG");
        else if (arrow) costume(actor, "Arrow");
        else if (shuriken) {
            writeLocal("_timer", std::abs(actor.direction) / actor.direction);
            writeLocal("_shuriken load", randomBetween(actor, "aMT", 25, 45));
            costume(actor, "shuriken" + local("_timer"));
        } else if (flame) {
            actor.effects["brightness"] = 90;
            costume(actor, "fire");
            layer(false);
        } else costume(actor, payload == "grenade" ? "Grenade" : "rock");
        if (multiplayerEnabled) {
            const auto kind = bullet ? RemoteAttack::Bullet : rocket ? RemoteAttack::Rocket :
                arrow ? RemoteAttack::Arrow : shuriken ? RemoteAttack::Shuriken :
                flame ? RemoteAttack::Flame : payload == "grenade" ? RemoteAttack::Grenade : RemoteAttack::Rock;
            const WorldVelocity velocity = thrown ? WorldVelocity{readLocal("_grenade speed x"), readLocal("_grenade speed y")}
                                                  : WorldVelocity{readLocal("_sin"), readLocal("_cos")};
            const auto weapon = rocket && namedValue("::[Spiller-action]").text.find("heli") != std::string::npos
                ? "heli" : bullet ? actor.networkWeapon : rocket ? "j" : arrow ? "e" : shuriken ? "k" :
                  flame ? "y" : payload == "grenade" ? "f" : "i";
            const auto charge = arrow ? readLocal("_arrow speed") : thrown ? readLocal("_bullet.hit") : 0;
            queueNetworkAttack(kind, origin, velocity, actor.direction, charge, weapon);
        }
        return true;
    }
    const auto slot = [&](const Value& weapon) {
        const auto& inventory = stageList("-INVENTORY");
        return static_cast<std::size_t>(std::find_if(inventory.begin(), inventory.end(),
            [&](const Value& entry) { return equal(entry, weapon); }) - inventory.begin());
    };
    const auto decrementAmmo = [&] {
        auto& magazines = stageList("Inventory Magazines");
        const auto index = slot(namedValue("::Våpen holdt"));
        if (index < magazines.size()) magazines[index] = magazines[index].number() - 1;
    };
    Value held;
    for (auto frame = task.frames.rbegin(); frame != task.frames.rend(); ++frame) {
        const auto item = frame->arguments.find("item held");
        if (item != frame->arguments.end()) { held = item->second; break; }
    }
    const auto emit = [&](const char* kind) {
        if (multiplayerEnabled) actor.networkWeapon = held.text;
        auto& cloneId = local("__ID");
        auto& direction = local("_direction");
        const auto aim = namedValue(".ARM Direction");
        direction = aim.text;
        if (std::string_view(kind) == "shotgun") {
            constexpr int spread[]{5, 3, 1, -1, -3, -5};
            constexpr const char* sources[]{"t_", "t{", "t}", "ua", "uc", "ue"};
            cloneId = "bullet";
            for (int pellet = 0; pellet < 6; ++pellet) {
                direction = Value(aim.number() + spread[pellet]).text;
                cloneActor(actor, definition.name, sources[pellet]);
            }
        } else {
            cloneId = kind;
            cloneActor(actor, definition.name, "uf");
        }
        cloneId.clear();
        actor.networkWeapon.clear();
    };
    const auto releaseBow = [&] {
        if (number("Bow Load") <= 1) return;
        writeLocal("_arrow speed", std::floor(number("Bow Load") + 0.5));
        auto& statistics = stageList("Statistics");
        if (statistics.size() > 7) statistics[7] = statistics[7].number() + 1;
        emit("arrow");
    };

    const auto mode = namedValue("::[Spiller-action]").text;
    const bool helicopter = mode.find("heli") != std::string::npos;
    const auto timer = now - timerOrigin;
    const auto ready = [&](double seconds) { return timer > readLocal("_timer") + seconds; };
    const auto magazine = slot(held);
    const auto& magazines = stageList("Inventory Magazines");
    const auto ammo = magazine < magazines.size() ? magazines[magazine] : Value("");
    const auto& controls = stageList(":CONTROLS:");
    const auto binding = controls.size() > 4 ? controls[4].text : std::string{};
    const bool trigger = binding == "mouse" ? inputSystem.raw().primaryMouse :
        binding.find("scroll") == std::string::npos && key(binding);
    const auto animation = number("::Animation");
    if ((multiplayerEnabled && (matchView.phase != MatchPhase::Playing || !matchView.ticksLeft)) ||
        (number(":Kart Åpent") >= 1 && !helicopter) || namedValue(";SCENE").text != "game" ||
        number("Mouse over chat.page") >= 1 || mode == "spectator" || !trigger ||
        animation >= 7 || (!ammo.text.empty() && ammo.number() <= 0)) {
        writeLocal("_medkit", 0);
        if (timer > readLocal("_clicktimer") + .1) {
            if (helicopter) action("heli", "aLI");
            else if (mode != "sp" && mode != "di" && mode != "spectator") action("0", "aLC");
        }
        writeLocal("_Mousedown", 1);
        releaseBow();
        set("Bow Load", 0.0);
        if (number("Nunchuks") > 0) set("Nunchuks", number("Nunchuks") - 1);
        return true;
    }

    writeLocal("_clicktimer", timer);
    const auto item = helicopter ? std::optional(ItemId::Helicopter)
                                 : content::findItem(held.text.empty() ? "a" : held.text);
    const auto gun = std::find_if(std::begin(firearms), std::end(firearms),
                                  [&](const Firearm& profile) { return profile.item == item; });
    const auto sendContactAttack = [&](RemoteAttack kind) {
        const auto aim = number(".ARM Direction");
        constexpr auto radians = std::numbers::pi / 180;
        queueNetworkAttack(kind, {number(".Point arm X") + number(";CAM X"),
                                  number(".Point arm Y") + number(";CAM Y")},
                           {std::sin(aim * radians) * 3, std::cos(aim * radians) * 3},
                           aim, 0, held.text.empty() ? "a" : held.text);
    };
    if (gun != std::end(firearms)) {
        action(helicopter ? "helis" : "1", gun->actionBlock);
        if (ready(gun->cooldown)) {
            writeLocal("_timer", timer);
            set("::Rekyl", number("::Rekyl") + gun->recoil);
            if (!helicopter) decrementAmmo();
            emit(gun->payload);
            if (number("::Krypegå") == 0) {
                const auto direction = number(".ARM Direction");
                const auto kick = std::abs(direction) / direction * gun->kick;
                // Scratch casts the intermediate NaN to zero before multiplying by -1.
                set("::sx", number("::sx") - (std::isnan(kick) ? 0 : kick));
            }
            set("crosshair grow", "1");
        }
    } else if (item) {
        const auto distance = number(".ARM Distance");
        const auto swing = [&](bool allowed, const char* active, const char* idle, double cooldown,
                               double amount, double offset, const char* payload) {
            if (!allowed) { action("0", idle); return; }
            action("1", active);
            if (!ready(cooldown)) return;
            set("crosshair grow", "1");
            writeLocal("_timer", timer);
            set("::Slåing", amount);
            if (offset) set("Player ARM Direction offset", offset * number("::Høyre/Venstre"));
            set("::Rekyl", 1.0);
            if (payload) emit(payload);
            else {
                playActorSound(actor, "Punch");
                sendContactAttack(RemoteAttack::Melee);
            }
        };
        switch (*item) {
        case ItemId::Hands: swing(distance > 15, "5o", "aKx", .3, -5, 0, nullptr); break;
        case ItemId::Axe: swing(distance > 15, "5s", "aKD", .8, -15, 40, nullptr); break;
        case ItemId::Knife: swing(distance > 5, "5T", "aK:", .25, -15, 50, nullptr); break;
        case ItemId::Katana: swing(true, "51", "", .4, -15, 100, nullptr); break;
        case ItemId::Grenade: swing(distance > 15 && animation != 2, "5z", "aKU", 1.5, -5, 0, "grenade"); break;
        case ItemId::PetCube: swing(distance > 15 && animation != 2, "5G", "aK3", .4, -5, 0, "rock"); break;
        case ItemId::Bow:
            set("::Rekyl", .1);
            if (distance > 22) set("Bow Load", number("Bow Load") < 10 ? number("Bow Load") + .5 : 10);
            else { releaseBow(); set("Bow Load", 0.0); }
            action(std::floor(number("Bow Load") + .5), "s[");
            set("crosshair grow", "b");
            break;
        case ItemId::Shurikens:
            set("::Rekyl", 1.0);
            action(distance > 14 ? "1" : "0", distance > 14 ? "5P" : "aK)");
            if (distance > 14 && ready(.4)) {
                set("crosshair grow", "1");
                writeLocal("_timer", timer);
                set("::Slåing", -5.0);
                emit("shuriken");
            }
            break;
        case ItemId::Taser:
            action("1", "57");
            set("crosshair grow", "1");
            set("::Rekyl", .1);
            playActorSound(actor, "taser boi");
            sendContactAttack(RemoteAttack::Taser);
            break;
        case ItemId::Medkit:
            action("0", "5)");
            if (std::abs(number("::sx")) > 1 || std::abs(number("::sy")) > 2) writeLocal("_medkit", 0);
            else {
                writeLocal("_medkit", readLocal("_medkit") + 1);
                if (readLocal("_medkit") > 40) {
                    writeLocal("_medkit", 0);
                    set("::Spiller-liv", number("::Spiller-liv") + 10, "aLk");
                }
            }
            break;
        case ItemId::Nunchaku:
            action("1", "5.");
            set("crosshair grow", "1");
            set("::Rekyl", .1);
            if (number("Nunchuks") < 50) set("Nunchuks", number("Nunchuks") + 2);
            if (number("Nunchuks") >= 50) sendContactAttack(RemoteAttack::Melee);
            break;
        case ItemId::SmokeBomb:
            action("1", "5=");
            if (ready(3)) {
                writeLocal("_timer", timer);
                set("::Slåing", -5.0);
                set("crosshair grow", "1");
                set("*Smoke X", namedValue("::Player X"));
                set("*Smoke Y", namedValue("::Player Y"));
                queueNetworkAttack(RemoteAttack::Smoke, {number("::Player X"), number("::Player Y")}, {}, 0, 0, "x");
            }
            break;
        case ItemId::Flamethrower:
            action("1", "5^");
            emit("fire");
            if (ready(.2)) { writeLocal("_timer", timer); decrementAmmo(); }
            set("crosshair grow", "1");
            break;
        default: break;
        }
    }
    writeLocal("_Mousedown", 0);
    return true;
}
}
