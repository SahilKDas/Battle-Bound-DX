#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace bb::content {
enum class ItemId : std::uint8_t {
    Hands, Pistol, Axe, Smg, Bow, Grenade, Shotgun, Sniper, PetCube,
    RocketLauncher, Shurikens, Knife, Shield, MachineGun, AssaultRifle,
    HeavyPistol, Katana, SemiAutoRifle, LightSmg, Taser, MedicGun, Medkit,
    Nunchaku, SmokeBomb, Flamethrower, Helicopter
};

enum class ItemCategory : std::uint8_t { Primary, Secondary, Gadget, Vehicle };
enum class MatchMode : std::uint8_t { TeamDeathmatch = 1, GunGame, CaptureTheFlag, FreeForAll };

struct ItemDefinition {
    ItemId id;
    std::string_view legacyCode;
    std::string_view name;
    ItemCategory category;
    std::optional<int> gearPoints;
    std::optional<double> reloadSeconds;
    std::string_view iconPath;
    std::string_view sourceReference;
};

struct MatchDefinition {
    MatchMode id;
    std::string_view name;
    std::string_view sourceReference;
};

std::span<const ItemDefinition> items();
std::span<const MatchDefinition> matches();
std::optional<ItemId> findItem(std::string_view legacyCode);
const ItemDefinition& item(ItemId id);
std::string_view sourceFingerprint();
void validate();
}
