#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace sts2::engine {
using ContentId = std::uint16_t;
inline constexpr ContentId no_content = UINT16_MAX;
enum class RelicTier : std::uint8_t { common, uncommon, rare, shop, event, ancient, starter, other };
struct RelicEntry { ContentId id; RelicTier tier; };
enum class ContentRarity : std::uint8_t { common, uncommon, rare, other };
struct CardEntry { ContentId id; ContentRarity rarity; bool upgradable; };
struct PotionEntry { ContentId id; ContentRarity rarity; };
struct EncounterEntry { ContentId id; std::uint64_t tags; };
struct ActDefinition {
    ContentId id;
    std::uint8_t index, rooms, weak_count;
    bool is_default;
    std::span<const ContentId> events, ancients;
    std::span<const EncounterEntry> weak, normal, elite, bosses;
};
struct CharacterDefinition {
    ContentId id;
    std::int32_t hp, gold, orb_slots, energy;
    std::span<const ContentId> deck, relics;
    std::span<const RelicEntry> relic_pool;
    ContentId card_pool_id;
    std::span<const CardEntry> card_pool;
    std::span<const PotionEntry> potion_pool;
};
class SetupRules {
public:
    [[nodiscard]] static std::string_view fingerprint() noexcept;
    [[nodiscard]] static std::string_view native_defaults() noexcept;
    [[nodiscard]] static std::span<const ContentId> neow_positive() noexcept;
    [[nodiscard]] static std::span<const ContentId> neow_curses() noexcept;
    [[nodiscard]] static std::span<const ActDefinition> acts() noexcept;
    [[nodiscard]] static std::span<const CharacterDefinition> characters() noexcept;
    [[nodiscard]] static std::span<const PotionEntry> shared_potions() noexcept;
    [[nodiscard]] static std::span<const RelicEntry> shared_relics() noexcept;
    [[nodiscard]] static std::span<const ContentId> shared_events() noexcept;
    [[nodiscard]] static std::span<const ContentId> shared_ancients() noexcept;
    [[nodiscard]] static std::string_view name(ContentId) noexcept;
    [[nodiscard]] static ContentId find(std::string_view) noexcept;
    [[nodiscard]] static const ActDefinition* act(ContentId) noexcept;
    [[nodiscard]] static const CharacterDefinition* character(ContentId) noexcept;
};
} // namespace sts2::engine
