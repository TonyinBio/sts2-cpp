#include "sts2/rules/ruleset.hpp"

namespace sts2::engine {
namespace {
constexpr std::array strike = {Instruction{Opcode::damage, 6, 3}, Instruction{Opcode::finish}};
constexpr std::array defend = {Instruction{Opcode::block, 5, 3}, Instruction{Opcode::finish}};
constexpr std::array bash = {Instruction{Opcode::damage, 8, 2}, Instruction{Opcode::vulnerable, 2, 1}, Instruction{Opcode::finish}};
constexpr std::array survivor = {Instruction{Opcode::block, 8, 3}, Instruction{Opcode::discard_choice}, Instruction{Opcode::finish}};
constexpr std::array definitions = {
    CardDefinition{"CARD.STRIKE_IRONCLAD", "Strike", 1, true, strike},
    CardDefinition{"CARD.DEFEND_IRONCLAD", "Defend", 1, false, defend},
    CardDefinition{"CARD.BASH", "Bash", 2, true, bash},
    CardDefinition{"CARD.SURVIVOR", "Survivor", 1, false, survivor}};
}
const CardDefinition* Ruleset::card(CardId id) const noexcept {
    const auto index = static_cast<std::size_t>(id);
    return index < definitions.size() ? &definitions[index] : nullptr;
}
std::span<const CardDefinition> Ruleset::cards() const noexcept { return definitions; }
const Ruleset& Ruleset::pinned() noexcept { static constexpr Ruleset rules; return rules; }
} // namespace sts2::engine
