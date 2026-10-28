#include "sts2/rules/ruleset.hpp"

namespace sts2::engine {
namespace {
constexpr std::array strike = {Instruction{Opcode::damage, 6, 3}, Instruction{Opcode::finish}};
constexpr std::array defend = {Instruction{Opcode::block, 5, 3}, Instruction{Opcode::finish}};
constexpr std::array bash = {Instruction{Opcode::damage, 8, 2}, Instruction{Opcode::vulnerable, 2, 1}, Instruction{Opcode::finish}};
constexpr std::array survivor = {Instruction{Opcode::block, 8, 3}, Instruction{Opcode::discard_choice}, Instruction{Opcode::finish}};
constexpr std::array armaments = {Instruction{Opcode::block, 5}, Instruction{Opcode::upgrade_choice}, Instruction{Opcode::finish}};
constexpr std::array headbutt = {Instruction{Opcode::damage, 9, 3}, Instruction{Opcode::topdeck_choice}, Instruction{Opcode::finish}};
constexpr std::array burning_pact = {Instruction{Opcode::exhaust_one}, Instruction{Opcode::draw, 2, 1}, Instruction{Opcode::finish}};
constexpr std::array true_grit = {Instruction{Opcode::block, 7, 2}, Instruction{Opcode::grit_exhaust}, Instruction{Opcode::finish}};
constexpr std::array purity = {Instruction{Opcode::exhaust_choice, 3, 2}, Instruction{Opcode::finish}};
constexpr std::array definitions = {
    CardDefinition{"CARD.STRIKE_IRONCLAD", "Strike", 1, true, strike},
    CardDefinition{"CARD.DEFEND_IRONCLAD", "Defend", 1, false, defend},
    CardDefinition{"CARD.BASH", "Bash", 2, true, bash},
    CardDefinition{"CARD.SURVIVOR", "Survivor", 1, false, survivor},
    CardDefinition{"CARD.PURITY", "Purity", 0, false, purity, CardRarity::uncommon},
    CardDefinition{"CARD.ARMAMENTS", "Armaments", 1, false, armaments, CardRarity::common},
    CardDefinition{"CARD.HEADBUTT", "Headbutt", 1, true, headbutt, CardRarity::common},
    CardDefinition{"CARD.BURNING_PACT", "Burning Pact", 1, false, burning_pact, CardRarity::uncommon},
    CardDefinition{"CARD.TRUE_GRIT", "True Grit", 1, false, true_grit, CardRarity::common}};
}
const CardDefinition* Ruleset::card(CardId id) const noexcept {
    const auto index = static_cast<std::size_t>(id);
    return index < definitions.size() ? &definitions[index] : nullptr;
}
std::span<const CardDefinition> Ruleset::cards() const noexcept { return definitions; }
const Ruleset& Ruleset::pinned() noexcept { static constexpr Ruleset rules; return rules; }
} // namespace sts2::engine
