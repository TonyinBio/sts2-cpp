#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace sts2::engine {
inline constexpr std::string_view game_build = "0.111.0+41cef1ea";
inline constexpr std::string_view game_digest = "0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9";
inline constexpr std::uint32_t rules_revision = 1;
enum class CardId : std::uint8_t { strike, defend, bash, survivor, inflame, shockwave, tremble, uppercut, purity, armaments, headbutt, burning_pact, true_grit, count };
enum class Opcode : std::uint8_t { damage, block, vulnerable, discard_choice, finish, strength, weak, exhaust_choice, upgrade_choice, topdeck_choice, exhaust_one, draw, grit_exhaust };
struct Instruction { Opcode opcode; std::int32_t base = 0; std::int32_t upgrade = 0; };
struct CardDefinition {
    std::string_view model_id, name;
    std::int32_t cost;
    bool enemy_target;
    std::span<const Instruction> program;
};
// Static programs/English data live once, outside every simulation's mutable state.
class Ruleset {
public:
    [[nodiscard]] const CardDefinition* card(CardId id) const noexcept;
    [[nodiscard]] std::span<const CardDefinition> cards() const noexcept;
    [[nodiscard]] static const Ruleset& pinned() noexcept;
};
} // namespace sts2::engine
