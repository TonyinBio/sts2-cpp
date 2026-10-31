#pragma once
#include "sts2/core/storage.hpp"
#include "sts2/state/combat_types.hpp"
#include <algorithm>
#include <cstdint>

namespace sts2::engine {
struct CreatureState {
    std::int32_t hp = 0, max_hp = 0, block = 0;
    std::uint64_t positive_unblocked_hits = 0;
    // Provisional inline ordered instances; this access contract does not select
    // a final dense/sparse layout. Capacity is a storage optimization, not a limit.
    InlineSequence<PowerInstance, 6> powers {};
    auto power_instances(OverflowStorage& owner) noexcept { return SequenceView(powers, owner); }
    auto power_instances(const OverflowStorage& owner) const noexcept { return SequenceView(powers, owner); }
    PowerInstance* find_power(PowerId id, OverflowStorage& owner) noexcept {
        for (auto& p : powers.values(owner)) if (p.definition == id && p.active) return &p;
        return nullptr;
    }
    const PowerInstance* find_power(PowerId id, const OverflowStorage& owner) const noexcept {
        for (const auto& p : powers.values(owner)) if (p.definition == id && p.active) return &p;
        return nullptr;
    }
    PowerInstance* find_power_instance(std::uint32_t id, OverflowStorage& owner) noexcept {
        for (auto& p : powers.values(owner)) if (p.instance == id && p.active) return &p;
        return nullptr;
    }
    bool has_power(PowerId id, const OverflowStorage& owner) const noexcept { return find_power(id, owner) != nullptr; }
    std::int32_t power_amount(PowerId id, const OverflowStorage& owner) const noexcept {
        std::int64_t total = 0;
        for (const auto& p : powers.values(owner)) if (p.definition == id && p.active) total += p.amount;
        return static_cast<std::int32_t>(std::clamp<std::int64_t>(total, INT32_MIN, INT32_MAX));
    }
    void clear_powers() noexcept { powers.clear(); }
    void assign_from(const CreatureState& other, OverflowStorage& owner) {
        auto retained = powers;
        *this = other; powers = retained;
        power_instances(owner).assign(other.power_instances(owner).begin(), other.power_instances(owner).end());
    }
    void clear() noexcept {
        auto retained = powers; *this = {}; powers = retained; powers.clear();
    }
    bool equals(const OverflowStorage& owner, const CreatureState& other, const OverflowStorage& other_owner) const {
        return hp == other.hp && max_hp == other.max_hp && block == other.block && positive_unblocked_hits == other.positive_unblocked_hits && powers.equals(owner, other.powers, other_owner);
    }
};
}
