#include "runtime.hpp"
#include "content.hpp"
#include <numbers>
#include <stdexcept>
#include <utility>

namespace bb {
namespace {
constexpr double radians = std::numbers::pi / 180;
struct ProjectileRule { const char* costume; int lifetime; };
constexpr ProjectileRule rules[]{
    {"Bullet", 90}, {"Arrow", 90}, {"shuriken1", 90}, {"RPG", 90},
    {"Grenade", 51}, {"rock", 51}, {"fire", 50}, {"Bullet", 1},
    {"Bullet", 1}, {"Bullet", 1}
};

double bulletDamage(content::ItemId item) {
    using content::ItemId;
    switch (item) {
    case ItemId::Sniper: return 60;
    case ItemId::MachineGun: return 3.5;
    case ItemId::AssaultRifle: return 8;
    case ItemId::HeavyPistol: return 17;
    case ItemId::SemiAutoRifle: return 23;
    case ItemId::LightSmg: return 6;
    case ItemId::MedicGun: return -8;
    default: return 7;
    }
}

double meleeDamage(content::ItemId item) {
    using content::ItemId;
    switch (item) {
    case ItemId::Hands: return 3;
    case ItemId::Axe: return 21;
    case ItemId::Knife: return 20;
    case ItemId::Katana: return 25;
    case ItemId::Nunchaku: return 2;
    default: return 0;
    }
}
}

void Runtime::enableMultiplayer(bool host, unsigned roundSeconds) {
    matchHost = host;
    roundLength = roundSeconds * 30;
    matchView = nextMatch = {};
    localDeaths = 0;
    localRematch = false;
    multiplayerEnabled = true;
    for (const auto& task : tasks)
        if (definitions[task->actor->type].name == "☁") task->alive = false;
}

void Runtime::queueNetworkAttack(RemoteAttack kind, WorldPoint origin, WorldVelocity velocity,
                                 double direction, double charge, std::string_view weapon) {
    if (!multiplayerEnabled || matchView.phase != MatchPhase::Playing || !matchView.ticksLeft) return;
    if (const auto item = content::findItem(weapon))
        outgoingAttacks.push_back({kind, origin, velocity, direction, charge, static_cast<unsigned>(*item)});
}

NetworkFrame Runtime::takeNetworkFrame() {
    for (const auto& actor : actors) {
        if (!actor->clone && definitions[actor->type].name == "[Player]") {
            return {{namedValue("::Player X").number(), namedValue("::Player Y").number()},
                    actor->direction, actor->size, actor->costume, actor->visible, username,
                    std::exchange(outgoingAttacks, {}), matchHost ? nextMatch : MatchState{},
                    matchView.round, localDeaths, localRematch};
        }
    }
    throw std::runtime_error("Missing multiplayer player actor");
}

void Runtime::receiveNetworkFrame(NetworkFrame frame) {
    if (peerFrame && peerFrame->name != frame.name) throw std::runtime_error("Multiplayer peer identity changed");
    const auto shared = matchHost ? nextMatch : frame.match;
    if (shared.round != matchView.round) {
        if (shared.round != matchView.round + 1 || shared.phase != MatchPhase::Countdown)
            throw std::runtime_error("Unexpected multiplayer round transition");
        multiplayerEnabled = false;
        greenFlag();
        startGame();
        multiplayerEnabled = true;
        for (const auto& task : tasks)
            if (definitions[task->actor->type].name == "☁") task->alive = false;
        localDeaths = 0;
        localRematch = false;
        frame.attacks.clear();
    }
    matchView = shared;
    setNamed(";KART", static_cast<double>(matchView.map));
    if (matchHost) {
        if (nextMatch.phase == MatchPhase::Playing) {
            nextMatch.guestScore = localDeaths;
            if (frame.reportedRound == nextMatch.round) nextMatch.hostScore = frame.deaths;
            // Exchange one final report after the last combat tick before publishing the winner.
            if (nextMatch.ticksLeft) --nextMatch.ticksLeft;
            else nextMatch.phase = MatchPhase::Finished;
        } else if (nextMatch.phase == MatchPhase::Countdown) {
            if (--nextMatch.ticksLeft == 0) {
                nextMatch.phase = MatchPhase::Playing;
                nextMatch.ticksLeft = roundLength;
            }
        } else {
            nextMatch.hostReady = localRematch;
            nextMatch.guestReady = frame.reportedRound == nextMatch.round && frame.rematch;
            if (nextMatch.hostReady && nextMatch.guestReady) {
                const auto round = nextMatch.round + 1;
                nextMatch = {};
                nextMatch.round = round;
            }
        }
    }
    if (frame.reportedRound != matchView.round) frame.attacks.clear();
    peerFrame = std::move(frame);
}

void Runtime::simulateNetworkCombat() {
    if (!peerFrame) return;
    if (matchView.phase == MatchPhase::Finished && inputSystem.key(Key::Enter).pressed)
        localRematch = true;
    if (matchView.phase != MatchPhase::Playing || !matchView.ticksLeft) {
        peerFrame->attacks.clear();
        outgoingAttacks.clear();
        for (auto& projectile : peerProjectiles) projectile.sprite->alive = false;
        peerProjectiles.clear();
    }
    const auto findActor = [&](std::string_view name) -> std::shared_ptr<Actor> {
        for (const auto& actor : actors)
            if (!actor->clone && definitions[actor->type].name == name) return actor;
        throw std::runtime_error("Missing multiplayer sprite definition");
    };
    const auto player = findActor("[Player]");
    if (!peerAvatar) {
        peerAvatar = std::make_shared<Actor>(*player);
        peerAvatar->networkReplica = peerAvatar->clone = true;
        peerAvatar->bubble = peerFrame->name;
        actors.push_back(peerAvatar);
    }
    const auto cameraX = namedValue(";CAM X").number();
    const auto cameraY = namedValue(";CAM Y").number();
    const auto project = [&](Actor& actor, WorldPoint point) {
        actor.x = point.x - cameraX;
        actor.y = point.y - cameraY;
    };
    if (static_cast<std::size_t>(peerFrame->costume) >= definitions[player->type].costumes.size())
        throw std::runtime_error("Invalid multiplayer player costume");
    peerAvatar->costume = peerFrame->costume;
    peerAvatar->direction = peerFrame->direction;
    peerAvatar->size = peerFrame->size;
    peerAvatar->visible = peerFrame->visible;
    project(*peerAvatar, peerFrame->position);
    setNamed("#PlayersOnline", 2.0);

    const auto timer = now - timerOrigin;
    const auto dealDamage = [&](double damage, const char* source, double angle, bool canHeadshot,
                                WorldPoint impact) {
        if (damage == 0 || timer < namedValue("SPAWN PROTECTION").number() ||
            namedValue(";SCENE").text != "game") return;
        const auto action = namedValue("::[Spiller-action]").text;
        if (action == "spectator" || action == "di" || action == "sp") return;
        const bool helicopter = action.find("heli") != std::string::npos;
        const auto health = helicopter ? "::Helicopter HP" : "::Spiller-liv";
        const auto maximum = helicopter ? 500.0 : 100.0;
        const auto kind = std::string_view(source);
        const bool contact = kind == "melee" || kind == "taser";
        if (helicopter && (kind == "flame" || contact)) return;
        const auto before = namedValue(health);
        if (before.number() <= 0) return;
        if (damage > 0) {
            const auto aim = namedValue(".ARM Direction").number();
            const auto shieldArc = kind == "explosion" ? 80 : 40;
            const auto weapon = namedValue("::Våpen holdt").text;
            const bool shield = !helicopter && (weapon == "m" || (contact && weapon == "q")) &&
                namedValue("::Animation").number() < 7 &&
                std::abs(std::remainder(aim - angle - 180, 360)) < shieldArc;
            const auto crouch = namedValue("::Krypegå").number();
            const auto scale = namedValue(";SIZE").number() * .02;
            const auto headY = namedValue("::Player Y").number() + (29 - crouch * 15) * scale;
            const bool headshot = canHeadshot && !helicopter &&
                std::abs(headY - impact.y) < 6 &&
                std::abs(namedValue("::Player X").number() - impact.x) < 7;
            if (shield) {
                if (kind == "flame" || contact) return;
                damage = kind == "explosion" ? damage * .5 : -std::floor(-damage / 3 + .5);
            }
            else if (headshot) damage *= 2;
            setNamed("::Spiller-truffet-av", peerFrame->name);
            setNamed("Health Timer", timer);
            const auto shake = kind == "flame" ? 2.0 : 3.0;
            setNamed(";Skjerm-risting", shake);
            cameraSystem.setShakeMagnitude(shake);
            if (kind != "flame")
                setNamed("::sx", namedValue("::sx").number() + std::sin(angle * radians) * (shield ? 1 : 2));
        }
        const Value after(std::clamp(before.number() - damage, 0.0, maximum));
        setNamed(health, after);
        if (after.number() == 0 && before.number() > 0) ++localDeaths;
        if (captureEvents && before.text != after.text)
            gameplayEvents.push_back({health, "[Network Peer]", source, before.text, after.text});
    };

    for (const auto& attack : peerFrame->attacks) {
        if (attack.kind == RemoteAttack::Smoke) {
            auto smoke = *findActor("[Particles]");
            const auto& definition = definitions[smoke.type];
            for (const auto& [id, name] : definition.variableNames) {
                if (id == "o=?mfRl2n4uLEcMh!~:N") smoke.variables[id] = "smokebomb";
                else if (name == "_x") smoke.variables[id] = Value(attack.origin.x).text;
                else if (name == "_y") smoke.variables[id] = Value(attack.origin.y).text;
            }
            smoke.networkSmoke = true;
            cloneActor(smoke, "[Network Peer]", "smoke");
            continue;
        }
        if (peerProjectiles.size() >= 256) throw std::runtime_error("Multiplayer projectile limit exceeded");
        const auto& rule = rules[static_cast<unsigned>(attack.kind)];
        auto sprite = std::make_shared<Actor>(*findActor("[Bullet]"));
        sprite->networkReplica = sprite->clone = true;
        sprite->visible = attack.kind != RemoteAttack::Melee && attack.kind != RemoteAttack::Taser;
        sprite->direction = attack.angle;
        sprite->rotation = "all around";
        sprite->size = std::max(1.0, namedValue(";SIZE").number());
        sprite->effects.clear();
        costume(*sprite, rule.costume);
        project(*sprite, attack.origin);
        actors.push_back(sprite);
        const bool thrown = attack.kind == RemoteAttack::Grenade || attack.kind == RemoteAttack::Rock;
        const int age = thrown ? static_cast<int>(std::clamp(attack.charge, 0.0, 50.0)) : 0;
        peerProjectiles.push_back({attack, std::move(sprite), age});
    }
    peerFrame->attacks.clear();

    for (auto& projectile : peerProjectiles) {
        auto& [attack, sprite, age, gravity] = projectile;
        const auto kind = attack.kind;
        const auto item = static_cast<content::ItemId>(attack.item);
        const auto explode = [&] {
            const auto radius = namedValue("Grenade Radius").number() *
                (item == content::ItemId::Helicopter ? 2 : 1);
            const auto distance = std::hypot(sprite->x - player->x, sprite->y - player->y);
            const auto force = std::max(0.0, radius - std::floor(distance + .5));
            const auto pose = namedValue("::Animation").number();
            const bool dive = pose == 9 || pose == 10;
            const bool helicopter = namedValue("::[Spiller-action]").text.find("heli") != std::string::npos;
            const auto scale = kind == RemoteAttack::Rocket ? (helicopter ? 1 : dive ? .5 : .7)
                                                            : (helicopter ? .5 : dive ? .8 : 1);
            dealDamage(force * scale, "explosion", attack.angle, false, attack.origin);
            if (captureEvents)
                gameplayEvents.push_back({"effect", "[Network Peer]",
                    kind == RemoteAttack::Grenade ? "grenade-blast" : "rocket-blast",
                    Value(attack.origin.x).text, Value(attack.origin.y).text});
            setNamed("*Explosion X", attack.origin.x);
            setNamed("*Explosion Y", attack.origin.y);
        };
        if (kind == RemoteAttack::Grenade || kind == RemoteAttack::Rock) {
            auto& point = attack.origin;
            auto& velocity = attack.velocity;
            costume(*sprite, "Grenade HITBOX");
            if (point.y < -570) { velocity.x *= .8; velocity.y *= .5; }
            point.x += velocity.x;
            project(*sprite, point);
            if (touching(*sprite, "[PLATFORM]")) {
                velocity.x *= -.8;
                point.x += velocity.x * 1.4;
                project(*sprite, point);
            }
            velocity.y += namedValue("G").number();
            point.y += velocity.y;
            project(*sprite, point);
            if (touching(*sprite, "[PLATFORM]")) {
                velocity.y *= -.5;
                point.y += velocity.y * 2;
                velocity.x *= .75;
            } else if (kind == RemoteAttack::Rock && touching(*sprite, "[Player]")) {
                dealDamage(15, "rock", attack.angle, true, point);
                sprite->alive = false;
            }
            project(*sprite, point);
            costume(*sprite, kind == RemoteAttack::Grenade ? "Grenade" : "rock");
            if (++age >= 51) {
                if (kind == RemoteAttack::Grenade) explode();
                sprite->alive = false;
            }
            continue;
        }

        if (kind == RemoteAttack::Flame) {
            project(*sprite, attack.origin);
            sprite->size = std::min(400.0, sprite->size + 18);
            sprite->effects["brightness"] = 90.0 - age * 4;
            sprite->effects["ghost"] = age * 2;
            ++age;
            if (!touching(*sprite, "[PLATFORM]")) {
                if (age < 40 && touching(*sprite, "[Player]"))
                    dealDamage(age < 10 ? .3 : age < 28 ? .2 : .05, "flame",
                               attack.angle, false, attack.origin);
                attack.origin.x += attack.velocity.x;
                attack.origin.y += attack.velocity.y;
            }
            if (age >= rules[static_cast<unsigned>(kind)].lifetime) sprite->alive = false;
            continue;
        }

        const bool melee = kind == RemoteAttack::Melee || kind == RemoteAttack::Taser;
        const bool arcing = kind == RemoteAttack::Arrow || kind == RemoteAttack::Shuriken;
        const auto steps = melee ? 12 : 8;
        for (int sample = 0; sample < steps; ++sample) {
            project(*sprite, attack.origin);
            if (touching(*sprite, "[PLATFORM]")) {
                if (kind == RemoteAttack::Rocket) explode();
                sprite->alive = false;
                break;
            }
            if (touching(*sprite, "[Player]")) {
                if (kind == RemoteAttack::Rocket) explode();
                else if (kind == RemoteAttack::Bullet) dealDamage(bulletDamage(item), "bullet", attack.angle, true, attack.origin);
                else if (kind == RemoteAttack::Arrow) dealDamage(attack.charge * 3, "arrow", attack.angle, true, attack.origin);
                else if (kind == RemoteAttack::Shuriken) dealDamage(15, "shuriken", attack.angle, true, attack.origin);
                else if (kind == RemoteAttack::Taser) dealDamage(2.5, "taser", attack.angle, false, attack.origin);
                else dealDamage(meleeDamage(item), "melee", attack.angle,
                                item != content::ItemId::Axe, attack.origin);
                sprite->alive = false;
                break;
            }
            attack.origin.x += attack.velocity.x;
            attack.origin.y += attack.velocity.y + gravity / 8;
        }
        if (arcing) gravity -= 1.2;
        if (++age >= rules[static_cast<unsigned>(kind)].lifetime) sprite->alive = false;
    }
    std::erase_if(peerProjectiles, [](const auto& projectile) { return !projectile.sprite->alive; });
}
}
