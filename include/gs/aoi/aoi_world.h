#pragma once

#include "gs/aoi/aoi_entity.h"
#include "gs/aoi/aoi_grid.h"
#include "gs/common/types.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace gs {

// Result of an AOI operation: which entities entered, left, or moved
// within this entity's view.
struct AoiDiff {
    std::vector<EntityId> entered;   // entities that just entered view
    std::vector<EntityId> left;      // entities that just left view
    std::vector<EntityId> moved;     // entities that moved but stayed in view
};

// Callback invoked when an entity's visible set changes.
// Args: (entity_id, diff_result)
using AoiCallback = std::function<void(EntityId, const AoiDiff&)>;

// Nine-grid AOI world.
//
// Entities are registered into grid cells. When an entity moves, the system
// computes the symmetric difference between its old and new visible sets,
// producing Enter/Leave/Move events.
//
// Thread-safety: this class is NOT thread-safe. It should only be accessed
// from the owning Scene Actor's processing thread.
class AoiWorld {
public:
    AoiWorld(float world_width, float world_height, float cell_size,
             int default_view_radius = 2);

    // Register an entity. Must call before it can be tracked.
    void add_entity(const AoiEntity& entity);

    // Remove an entity. All other entities will receive Leave events for it.
    void remove_entity(EntityId id);

    // Move an entity to a new position. Computes diff and invokes callbacks.
    // Returns true if the entity's grid cell changed.
    bool move_entity(EntityId id, const Vec2& new_pos);

    // Set callback for AOI events.
    void set_callback(AoiCallback cb) { _callback = std::move(cb); }

    // Get entity by ID.
    const AoiEntity* get_entity(EntityId id) const;

    // Get all entity IDs in a given cell.
    const std::unordered_map<EntityId, bool>* entities_in_cell(const GridPos& gp) const;

    // Get grid dimensions.
    int grid_cols() const { return _grid_cols; }
    int grid_rows() const { return _grid_rows; }

private:
    // Collect all entity IDs visible from a given cell (including neighbors).
    void collect_visible(const GridPos& center, int radius,
                         std::unordered_set<EntityId>& out) const;

    // Compute the diff between two entity sets.
    AoiDiff compute_diff(const std::unordered_set<EntityId>& old_set,
                         const std::unordered_set<EntityId>& new_set,
                         EntityId self_id) const;

    // Get the 1D index of a grid cell.
    int cell_index(const GridPos& gp) const;

    // Check if a grid position is valid.
    bool valid_grid(const GridPos& gp) const;

    int _grid_cols;
    int _grid_rows;
    int _default_view_radius;

    std::vector<AoiGrid> _grids;
    std::unordered_map<EntityId, AoiEntity> _entities;
    AoiCallback _callback;
};

}  // namespace gs
