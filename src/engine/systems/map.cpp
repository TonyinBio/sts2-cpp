#include "sts2/state/map.hpp"
#include "sts2/core/sort.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace sts2::engine {
bool MapEdges::contains(MapNodeId node) const noexcept {
    for (unsigned i = 0; i < used; ++i) if (slots[i] == node) return true;
    return false;
}
bool MapEdges::add(MapNodeId node) {
    if (node == no_node || contains(node)) return false;
    if (free_count) slots[free[--free_count]] = node;
    else {
        if (used == slots.size()) throw std::length_error("Map degree exceeds native seven-column bounds");
        slots[used++] = node;
    }
    return true;
}
bool MapEdges::remove(MapNodeId node) noexcept {
    for (std::uint8_t i = 0; i < used; ++i) if (slots[i] == node) {
        slots[i] = no_node; free[free_count++] = i; return true;
    }
    return false;
}
std::vector<MapNodeId> MapEdges::values() const {
    std::vector<MapNodeId> result; result.reserve(size());
    for (unsigned i = 0; i < used; ++i) if (slots[i] != no_node) result.push_back(slots[i]);
    return result;
}
MapNodeId MapState::at(int col, int row) const noexcept {
    if (col < 0 || col >= width || row < 0 || row >= height) return no_node;
    return grid[static_cast<std::size_t>(col * max_height + row)];
}
std::vector<MapNodeId> MapState::points() const {
    std::vector<MapNodeId> result;
    for (int col = 0; col < width; ++col) for (int row = 0; row < height; ++row)
        if (const auto id = at(col, row); id != no_node) result.push_back(id);
    return result;
}
namespace {
struct Builder {
    MapState m;
    GameRng rng;
    int rests, unknowns, elites;
    explicit Builder(std::uint64_t seed) : rng(seed) {}
    MapNode& node(MapNodeId id) { return m.nodes[id]; }
    MapNodeId create(int col, int row) {
        if (m.count == m.max_nodes) throw std::length_error("Map storage exhausted");
        const auto id = m.count++; auto& n = node(id); n.col = col; n.row = row; n.active = true;
        return id;
    }
    MapNodeId get(int col, int row) {
        auto& cell = m.grid[static_cast<std::size_t>(col * m.max_height + row)];
        if (cell == no_node) cell = create(col, row);
        return cell;
    }
    void link(MapNodeId parent, MapNodeId child) { node(parent).children.add(child); node(child).parents.add(parent); }
    void unlink(MapNodeId parent, MapNodeId child) { node(parent).children.remove(child); node(child).parents.remove(parent); }
    int integer(int min, int max) { int result; if (!rng.next_int(min, max, result)) throw std::invalid_argument("Map random bound"); return result; }
    int gaussian(int mean, int min, int max) {
        for (unsigned tries = 0; tries < 100000; ++tries) {
            const double u = 1.0 - rng.next_double(), v = 1.0 - rng.next_double();
            const double sample = static_cast<double>(mean) + std::sqrt(-2.0 * std::log(u)) * std::sin(6.283185307179586 * v);
            const auto low = std::floor(sample), fraction = sample - low;
            const auto rounded = fraction < .5 ? low : fraction > .5 ? low + 1 : (std::fmod(low, 2) == 0 ? low : low + 1);
            if (rounded >= min && rounded <= max) return static_cast<int>(rounded);
        }
        throw std::runtime_error("Gaussian map sampler did not terminate");
    }
    void shuffle(std::vector<MapNodeId>& ids) {
        game_sort<MapNodeId>(ids, [&](auto a, auto b) {
            const auto& x = node(a); const auto& y = node(b);
            return x.col == y.col ? x.row < y.row : x.col < y.col;
        });
        rng.shuffle<MapNodeId>(ids);
    }
    bool valid(MapKind kind, MapNodeId id) {
        auto& n = node(id);
        if (n.row < 6 && (kind == MapKind::rest || kind == MapKind::elite)) return false;
        if (n.row >= m.height - 3 && kind == MapKind::rest) return false;
        if (kind == MapKind::elite || kind == MapKind::rest || kind == MapKind::treasure || kind == MapKind::shop) {
            for (auto p : n.parents.values()) if (node(p).kind == kind) return false;
            for (auto c : n.children.values()) if (node(c).kind == kind) return false;
        }
        if (kind == MapKind::rest || kind == MapKind::monster || kind == MapKind::unknown || kind == MapKind::elite || kind == MapKind::shop)
            for (auto p : n.parents.values()) for (auto sibling : node(p).children.values())
                if (sibling != id && node(sibling).kind == kind) return false;
        return true;
    }
    void paths() {

        for (int iteration = 0; iteration < 7; ++iteration) {
            auto current = get(integer(0, 7), 1);
            if (iteration == 1) while (m.starts.contains(current)) current = get(integer(0, 7), 1);
            m.starts.add(current);
            while (node(current).row < m.height - 1) {
                const int col = node(current).col, row = node(current).row;
                std::array<int, 3> directions{-1, 0, 1}; rng.shuffle<int>(directions);
                bool found = false;
                for (auto direction : directions) {
                    const int next_col = std::clamp(col + direction, 0, 6);
                    const int delta = next_col - col;
                    bool crossing = false;
                    if (delta != 0) {
                        const auto neighbor = m.at(next_col, row);
                        if (neighbor != no_node) for (auto c : node(neighbor).children.values())
                            if (node(c).col - next_col == -delta) crossing = true;
                    }
                    if (!crossing) { const auto child = get(next_col, row + 1); link(current, child); current = child; found = true; break; }
                }
                if (!found) throw std::runtime_error("No valid map path");
            }
        }
        for (int col = 0; col < 7; ++col) {
            if (auto id = m.at(col, m.height - 1); id != no_node) link(id, m.boss);
            if (auto id = m.at(col, 1); id != no_node) link(m.start, id);
        }
        if (m.second_boss != no_node) link(m.boss, m.second_boss);
    }
    void assign() {
        for (auto id : m.points()) {
            auto& n = node(id);
            if (n.row == m.height - 1) { n.kind = MapKind::rest; n.can_modify = false; }
            if (n.row == m.height - 7) { n.kind = MapKind::treasure; n.can_modify = false; }
            if (n.row == 1) { n.kind = MapKind::monster; n.can_modify = false; }
        }
        std::vector<MapKind> queue;
        queue.insert(queue.end(), static_cast<std::size_t>(rests), MapKind::rest);
        queue.insert(queue.end(), 3, MapKind::shop);
        queue.insert(queue.end(), static_cast<std::size_t>(elites), MapKind::elite);
        queue.insert(queue.end(), static_cast<std::size_t>(unknowns), MapKind::unknown);
        for (int pass = 0; pass < 3 && !queue.empty(); ++pass) {
            auto ids = m.points(); std::erase_if(ids, [&](auto id) { return node(id).kind != MapKind::unassigned; }); shuffle(ids);
            for (auto id : ids) {
                if (queue.empty()) break;
                const auto count = queue.size();
                for (std::size_t attempt = 0; attempt < count; ++attempt) {
                    const auto kind = queue.front(); queue.erase(queue.begin());
                    if (valid(kind, id)) { node(id).kind = kind; break; }
                    queue.push_back(kind);
                }
            }
        }
        for (auto id : m.points()) if (node(id).kind == MapKind::unassigned) node(id).kind = MapKind::monster;
        node(m.start).kind = MapKind::ancient; node(m.boss).kind = MapKind::boss;
        if (m.second_boss != no_node) node(m.second_boss).kind = MapKind::boss;
    }
    using Path = std::vector<MapNodeId>;
    using Matches = std::vector<Path>;
    void all_paths(MapNodeId id, Path& path, std::vector<Path>& result) {
        path.push_back(id);
        if (node(id).kind == MapKind::boss) result.push_back(path);
        else for (auto c : node(id).children.values()) all_paths(c, path, result);
        path.pop_back();
        if (result.size() > 100000) throw std::length_error("Map path limit exceeded");
    }
    std::vector<Matches> matching() {
        std::vector<Path> paths; Path path; all_paths(m.start, path, paths);
        std::map<std::string, Matches> segments;
        for (const auto& route : paths) for (std::size_t i = 0; i + 1 < route.size(); ++i) {
            const auto& start = node(route[i]); if (start.children.size() <= 1 && start.row != 0) continue;
            for (std::size_t length = 2; i + length < route.size(); ++length) {
                const auto& end = node(route[i + length]); if (end.parents.size() < 2) continue;
                Path segment(route.begin() + static_cast<std::ptrdiff_t>(i), route.begin() + static_cast<std::ptrdiff_t>(i + length + 1));
                std::string key = start.row == 0 ? "0-" : std::to_string(start.col) + "," + std::to_string(start.row) + "-";
                key += std::to_string(end.col) + "," + std::to_string(end.row) + "-";
                for (auto p : segment) { if (p != segment.front()) key += ','; key += std::to_string(static_cast<int>(node(p).kind)); }
                auto& matches = segments[key];
                const bool overlaps = std::any_of(matches.begin(), matches.end(), [&](const auto& old) {
                    for (std::size_t j = 1; j + 1 < old.size(); ++j) if (old[j] == segment[j]) return true;
                    return false;
                });
                if (!overlaps) matches.push_back(std::move(segment));
            }
        }
        std::vector<Matches> result;
        for (auto& [key, matches] : segments) if (matches.size() > 1) result.push_back(std::move(matches));
        return result;
    }
    bool removed(MapNodeId id) { const auto& n = node(id); return m.at(n.col, n.row) == no_node; }
    bool in_map(MapNodeId id) { return !removed(id) || node(id).kind == MapKind::ancient || node(id).kind == MapKind::boss; }
    void remove(MapNodeId id) {
        auto& n = node(id); m.grid[static_cast<std::size_t>(n.col * m.max_height + n.row)] = no_node; n.active = false; m.starts.remove(id);
        for (auto c : n.children.values()) unlink(id, c);
        for (auto p : n.parents.values()) unlink(p, id);
    }
    bool prune_segment(const Path& segment) {
        bool changed = false;
        for (std::size_t i = 0; i + 1 < segment.size(); ++i) {
            const auto id = segment[i]; auto& n = node(id);
            if (!in_map(id)) return true;
            if (n.children.size() > 1 || n.parents.size() > 1) continue;
            bool necessary_parent = false;
            for (auto p : n.parents.values()) if (node(p).children.size() == 1 && !removed(p)) necessary_parent = true;
            if (necessary_parent) continue;
            bool necessary_child = false;
            for (std::size_t j = i; j < segment.size(); ++j) if (node(segment[j]).children.size() > 1 && node(segment[j]).parents.size() == 1) necessary_child = true;
            if (necessary_child) continue;
            if (node(segment.back()).parents.size() == 1) return false;
            bool separate_child = false;
            for (auto c : n.children.values()) if (std::find(segment.begin(), segment.end(), c) == segment.end() && node(c).parents.size() == 1) separate_child = true;
            if (!separate_child) { remove(id); changed = true; }
        }
        return changed;
    }
    bool prune() {
        auto groups = matching();
        for (auto& group : groups) {
            rng.shuffle<Path>(group);
            std::size_t pruned = 0;
            for (const auto& segment : group) { if (pruned == group.size() - 1) break; if (prune_segment(segment)) ++pruned; }
            if (pruned) return true;
            for (const auto& segment : group) {
                bool changed = false;
                for (std::size_t i = 0; i + 1 < segment.size(); ++i) if (node(segment[i]).children.size() >= 2 && node(segment[i + 1]).parents.size() != 1) {
                    unlink(segment[i], segment[i + 1]); changed = true;
                }
                if (changed) return true;
            }
        }
        return false;
    }
    bool repair(MapKind kind, int target) {
        int existing = 0; auto ids = m.points();
        for (auto id : ids) if (node(id).kind == kind) ++existing;
        int remaining = target - existing; if (remaining <= 0) return false;
        std::erase_if(ids, [&](auto id) { return node(id).kind != MapKind::monster || !node(id).can_modify; }); shuffle(ids);
        bool changed = false;
        for (auto id : ids) { if (!remaining) break; if (valid(kind, id)) { node(id).kind = kind; --remaining; changed = true; } }
        return changed;
    }
    void prune_repair() {
        for (int iteration = 0; iteration < 3; ++iteration) {
            unsigned passes = 0; while (prune()) if (++passes > 50) throw std::runtime_error("Map pruning did not settle");
            bool changed = repair(MapKind::shop, 3); changed |= repair(MapKind::elite, elites);
            changed |= repair(MapKind::rest, rests); changed |= repair(MapKind::unknown, unknowns);
            if (!changed) break;
        }
    }
    void shift(MapNodeId id, int col) {
        auto& n = node(id); m.grid[static_cast<std::size_t>(n.col * m.max_height + n.row)] = no_node;
        n.col = col; m.grid[static_cast<std::size_t>(col * m.max_height + n.row)] = id;
    }
    void postprocess() {
        auto empty = [&](int col) { for (int r = 0; r < m.height; ++r) if (m.at(col, r) != no_node) return false; return true; };
        const bool left = empty(0) && empty(1), right = empty(6) && empty(5);
        const int delta = left && !right ? -1 : !left && right ? 1 : 0;
        if (delta) for (int row = 0; row < m.height; ++row) for (int i = 0; i < 7; ++i) {
            const int col = delta > 0 ? 6 - i : i;
            if (const auto id = m.at(col, row); id != no_node) shift(id, col + delta);
        }
        for (int row = 0; row < m.height; ++row) {
            std::vector<MapNodeId> ids; for (int col = 0; col < 7; ++col) if (auto id = m.at(col, row); id != no_node) ids.push_back(id);
            bool changed;
            do {
                changed = false;
                for (auto id : ids) {
                    auto& n = node(id);
                    auto gap = [&](int col) { int distance = INT32_MAX; for (auto other : ids) if (id != other) distance = std::min(distance, std::abs(col - node(other).col)); return distance; };
                    int best = n.col, best_gap = gap(n.col);
                    for (int col = 0; col < 7; ++col) {
                        if (col == n.col || m.at(col, row) != no_node) continue;
                        bool allowed = true;
                        for (auto p : n.parents.values()) if (std::abs(col - node(p).col) > 1) allowed = false;
                        for (auto c : n.children.values()) if (std::abs(col - node(c).col) > 1) allowed = false;
                        if (allowed && gap(col) > best_gap) { best = col; best_gap = gap(col); }
                    }
                    if (best != n.col) { shift(id, best); changed = true; }
                }
            } while (changed);
        }
        for (int row = 0; row < m.height; ++row) for (int col = 0; col < 7; ++col) {
            const auto id = m.at(col, row); if (id == no_node) continue; auto& n = node(id);
            if (n.parents.size() != 1 || n.children.size() != 1) continue;
            const auto parent = n.parents.values()[0], child = n.children.values()[0];
            const bool smaller = n.col < node(parent).col && n.col < node(child).col;
            const bool larger = n.col > node(parent).col && n.col > node(child).col;
            if (smaller && col < 6 && m.at(col + 1, row) == no_node) shift(id, col + 1);
            if (larger && col > 0 && m.at(col - 1, row) == no_node) shift(id, col - 1);
        }
    }
};
}
bool generate_map(MapState& destination, std::string_view act, std::uint64_t seed, std::uint8_t ascension) {
    if (ascension > 10) return false;
    const int index = act == "ACT.OVERGROWTH" || act == "ACT.UNDERDOCKS" ? 0 : act == "ACT.HIVE" ? 1 : act == "ACT.GLORY" ? 2 : -1;
    if (index < 0) return false;
    Builder b(seed + game_hash("act_" + std::to_string(index + 1) + "_map"));
    b.m.height = index == 0 ? 16 : index == 1 ? 15 : 14;
    b.rests = index == 0 ? b.gaussian(7, 6, 7) : index == 1 ? b.gaussian(6, 6, 7) : b.integer(5, 7);
    b.unknowns = b.gaussian(12, 10, 14) - (index > 0 ? 1 : 0); b.elites = ascension >= 1 ? 8 : 5;
    b.m.boss = b.create(3, b.m.height); b.m.start = b.create(3, 0);
    if (index == 2 && ascension >= 10) b.m.second_boss = b.create(3, b.m.height + 1);
    b.paths(); b.assign(); b.prune_repair(); b.postprocess(); b.m.generator_state = b.rng.state();
    if (!valid_map(b.m)) throw std::runtime_error("Invalid generated map");
    destination = std::move(b.m); return true;
}
bool valid_map(const MapState& m) noexcept {
    if (m.height < 2 || m.height > m.max_height || m.count > m.max_nodes || m.start >= m.count || m.boss >= m.count ||
        (m.second_boss != no_node && m.second_boss >= m.count)) return false;
    if (m.start == m.boss || (m.second_boss != no_node && (m.second_boss == m.start || m.second_boss == m.boss)) ||
        m.nodes[m.start].kind != MapKind::ancient || m.nodes[m.start].row != 0 || !m.nodes[m.start].active ||
        m.nodes[m.boss].kind != MapKind::boss || m.nodes[m.boss].row != m.height || !m.nodes[m.boss].active) return false;
    if (m.second_boss != no_node && (m.nodes[m.second_boss].kind != MapKind::boss || m.nodes[m.second_boss].row != m.height + 1 || !m.nodes[m.second_boss].active)) return false;
    for (std::size_t i = m.count; i < m.nodes.size(); ++i) if (m.nodes[i] != MapNode{}) return false;
    const auto valid_edges = [&](const MapEdges& edges) {
        if (edges.used > edges.slots.size() || edges.free_count > edges.used) return false;
        unsigned holes = 0;
        for (unsigned i = 0; i < edges.used; ++i) {
            const auto id = edges.slots[i]; if (id == no_node) ++holes;
            else {
                if (id >= m.count) return false;
                for (unsigned j = 0; j < i; ++j) if (edges.slots[j] == id) return false;
            }
        }
        if (holes != edges.free_count) return false;
        for (unsigned i = 0; i < edges.free_count; ++i) {
            if (edges.free[i] >= edges.used || edges.slots[edges.free[i]] != no_node) return false;
            for (unsigned j = 0; j < i; ++j) if (edges.free[j] == edges.free[i]) return false;
        }
        for (unsigned i = edges.used; i < edges.slots.size(); ++i) if (edges.slots[i] != no_node) return false;
        return true;
    };
    if (!valid_edges(m.starts)) return false;
    // Validate every degree before relationship checks traverse another node's
    // edges; malformed checkpoints must not make contains() read past its slots.
    for (unsigned i = 0; i < m.count; ++i)
        if (!valid_edges(m.nodes[i].children) || !valid_edges(m.nodes[i].parents)) return false;
    for (unsigned i = 0; i < m.starts.used; ++i) if (const auto node = m.starts.slots[i]; node != no_node)
        if (!m.nodes[node].active || m.nodes[node].row != 1 || !m.nodes[m.start].children.contains(node)) return false;
    for (unsigned i = 0; i < m.grid.size(); ++i) if (const auto id = m.grid[i]; id != no_node) {
        if (id >= m.count || !m.nodes[id].active || m.nodes[id].col != static_cast<int>(i / m.max_height) ||
            m.nodes[id].row != static_cast<int>(i % m.max_height) || m.nodes[id].row >= m.height) return false;
    }
    for (unsigned i = 0; i < m.count; ++i) {
        const auto& n = m.nodes[i];
        if (!valid_edges(n.children) || !valid_edges(n.parents) || static_cast<unsigned>(n.kind) > static_cast<unsigned>(MapKind::ancient)) return false;
        if (n.col < 0 || n.col >= 7 || n.row < 0 || n.row > m.height + 1) return false;
        for (unsigned j = 0; j < n.children.used; ++j) if (auto c = n.children.slots[j]; c != no_node) {
            if (c >= m.count || m.nodes[c].row != n.row + 1 || !m.nodes[c].parents.contains(static_cast<MapNodeId>(i))) return false;
        }
        for (unsigned j = 0; j < n.parents.used; ++j) if (auto p = n.parents.slots[j]; p != no_node) {
            if (p >= m.count || m.nodes[p].row != n.row - 1 || !m.nodes[p].children.contains(static_cast<MapNodeId>(i))) return false;
        }
        if (n.active && i != m.start && i != m.boss && i != m.second_boss && m.at(n.col, n.row) != i) return false;
    }
    return true;
}
} // namespace sts2::engine
