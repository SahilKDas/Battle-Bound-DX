#include "content.hpp"
#include "generated_content.hpp"
#include <cmath>
#include <set>
#include <stdexcept>

namespace bb::content {
std::span<const ItemDefinition> items() { return generated::items; }
std::span<const MatchDefinition> matches() { return generated::matches; }
std::string_view sourceFingerprint() { return generated::sourceSha256; }

std::optional<ItemId> findItem(std::string_view legacyCode) {
    for (const auto& definition : items()) {
        if (definition.legacyCode == legacyCode) return definition.id;
    }
    return std::nullopt;
}

const ItemDefinition& item(ItemId id) {
    for (const auto& definition : items()) {
        if (definition.id == id) return definition;
    }
    throw std::out_of_range("Unknown item ID");
}

void validate() {
    std::set<std::string_view> codes;
    std::set<ItemId> ids;
    for (const auto& definition : items()) {
        if (!codes.insert(definition.legacyCode).second || !ids.insert(definition.id).second ||
            definition.name.empty() || definition.sourceReference.empty() ||
            (definition.gearPoints && *definition.gearPoints < 0) ||
            (definition.reloadSeconds && (!std::isfinite(*definition.reloadSeconds) ||
                                           *definition.reloadSeconds <= 0))) {
            throw std::runtime_error("Invalid generated content definition");
        }
    }
}
}
