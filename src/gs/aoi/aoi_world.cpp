#include "gs/aoi/aoi_world.h"

#include <algorithm>
#include <cmath>

namespace gs {

NineGridAoi::NineGridAoi(float world_width, float world_height, float cell_size,
                   int default_view_radius)
    : _grid_cols(static_cast<int>(std::ceil(world_width / cell_size)))
    , _grid_rows(static_cast<int>(std::ceil(world_height / cell_size)))
    , _default_view_radius(default_view_radius)
{
    _grids.resize(_grid_cols * _grid_rows);
}

int NineGridAoi::cell_index(const GridPos& gp) const {
    return gp.y * _grid_cols + gp.x;
}

bool NineGridAoi::valid_grid(const GridPos& gp) const {
    return gp.x >= 0 && gp.x < _grid_cols && gp.y >= 0 && gp.y < _grid_rows;
}

void NineGridAoi::add_entity(const AoiEntity& entity) {
    EntityId id = entity.id;
    _entities[id] = entity;

    if (valid_grid(entity.grid)) {
        _grids[cell_index(entity.grid)].add(id);
    }

    // Compute initial visible set for the new entity.
    std::unordered_set<EntityId> new_visible;
    collect_visible(entity.grid, entity.view_radius, new_visible);

    // Notify the new entity about what it sees.
    AoiDiff diff;
    for (auto vid : new_visible) {
        if (vid != id) diff.entered.push_back(vid);
    }
    _entities[id].visible_set = std::move(new_visible);
    if (_callback && !diff.entered.empty()) {
        _callback(id, diff);
    }

    // Notify existing entities that the new entity entered THEIR view.
    for (int dy = -entity.view_radius; dy <= entity.view_radius; ++dy) {
        for (int dx = -entity.view_radius; dx <= entity.view_radius; ++dx) {
            GridPos gp{entity.grid.x + dx, entity.grid.y + dy};
            if (!valid_grid(gp)) continue;
            for (const auto& [eid, _] : _grids[cell_index(gp)].entities()) {
                if (eid == id) continue;
                auto& other = _entities[eid];
                // Check if the new entity is within other's view.
                int gdx = entity.grid.x - other.grid.x;
                int gdy = entity.grid.y - other.grid.y;
                if (std::abs(gdx) <= other.view_radius &&
                    std::abs(gdy) <= other.view_radius) {
                    other.visible_set.insert(id);
                    if (_callback) {
                        AoiDiff other_diff;
                        other_diff.entered.push_back(id);
                        _callback(eid, other_diff);
                    }
                }
            }
        }
    }
}

void NineGridAoi::remove_entity(EntityId id) {
    auto it = _entities.find(id);
    if (it == _entities.end()) return;

    const auto& entity = it->second;

    // Remove from grid.
    if (valid_grid(entity.grid)) {
        _grids[cell_index(entity.grid)].remove(id);
    }

    // Notify other entities that this entity left their view.
    // (In a full implementation, we would walk each visible entity
    // and remove `id` from their visible_set + fire Leave callback.)

    _entities.erase(it);
}

bool NineGridAoi::move_entity(EntityId id, const Vec2& new_pos) {
    auto it = _entities.find(id);
    if (it == _entities.end()) return false;

    AoiEntity& entity = it->second;
    GridPos old_grid = entity.grid;
    GridPos new_grid = world_to_grid(new_pos);

    entity.position = new_pos;
    entity.grid = new_grid;

    // Update grid registration if cell changed.
    if (old_grid != new_grid) {
        if (valid_grid(old_grid)) {
            _grids[cell_index(old_grid)].remove(id);
        }
        if (valid_grid(new_grid)) {
            _grids[cell_index(new_grid)].add(id);
        }
    }

    // Compute new visible set.
    std::unordered_set<EntityId> new_visible;
    collect_visible(new_grid, entity.view_radius, new_visible);

    // Compute diff against old visible set.
    AoiDiff diff = compute_diff(entity.visible_set, new_visible, id);

    entity.visible_set = std::move(new_visible);

    if (_callback && (!diff.entered.empty() || !diff.left.empty())) {
        _callback(id, diff);
    }

    return old_grid != new_grid;
}

void NineGridAoi::collect_visible(const GridPos& center, int radius,
                               std::unordered_set<EntityId>& out) const {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            GridPos gp{center.x + dx, center.y + dy};
            if (!valid_grid(gp)) continue;
            const auto& cell = _grids[cell_index(gp)].entities();
            for (const auto& [eid, _] : cell) {
                out.insert(eid);
            }
        }
    }
}

AoiDiff NineGridAoi::compute_diff(const std::unordered_set<EntityId>& old_set,
                               const std::unordered_set<EntityId>& new_set,
                               EntityId self_id) const {
    AoiDiff diff;
    for (auto id : new_set) {
        if (id == self_id) continue;
        if (old_set.find(id) == old_set.end()) {
            diff.entered.push_back(id);
        } else {
            diff.moved.push_back(id);  // in both → moved
        }
    }
    for (auto id : old_set) {
        if (id != self_id && new_set.find(id) == new_set.end()) {
            diff.left.push_back(id);
        }
    }
    return diff;
}

const AoiEntity* NineGridAoi::get_entity(EntityId id) const {
    auto it = _entities.find(id);
    return it != _entities.end() ? &it->second : nullptr;
}

const std::unordered_map<EntityId, bool>* NineGridAoi::entities_in_cell(
    const GridPos& gp) const {
    if (!valid_grid(gp)) return nullptr;
    return &_grids[cell_index(gp)].entities();
}

}  // namespace gs
