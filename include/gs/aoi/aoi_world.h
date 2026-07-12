#pragma once

#include "gs/aoi/aoi_entity.h"
#include "gs/aoi/aoi_grid.h"
#include "gs/aoi/i_aoi.h"
#include "gs/common/types.h"

#include <functional>
#include <unordered_map>
#include <vector>

namespace gs {

// Nine-grid AOI implementation of IAoi.
//
// Entities are registered into fixed-size grid cells. When an entity moves,
// the system computes the symmetric difference between its old and new
// visible sets, producing Enter/Leave/Move events.
//
// Thread-safety: NOT thread-safe. Single-Actor access only.
class NineGridAoi : public IAoi {
public:
    NineGridAoi(float world_width, float world_height, float cell_size,
                int default_view_radius = 2);

    void add_entity(const AoiEntity& entity) override;
    void remove_entity(EntityId id) override;
    bool move_entity(EntityId id, const Vec2& new_pos) override;
    void set_callback(AoiCallback cb) override { _callback = std::move(cb); }
    const AoiEntity* get_entity(EntityId id) const override;

    // Grid-specific queries (not in IAoi).
    const std::unordered_map<EntityId, bool>* entities_in_cell(const GridPos&) const;
    int grid_cols() const { return _grid_cols; }
    int grid_rows() const { return _grid_rows; }

private:
    void collect_visible(const GridPos& center, int radius,
                         std::unordered_set<EntityId>& out) const;
    AoiDiff compute_diff(const std::unordered_set<EntityId>& old_set,
                         const std::unordered_set<EntityId>& new_set,
                         EntityId self_id) const;
    int cell_index(const GridPos& gp) const;
    bool valid_grid(const GridPos& gp) const;

    int _grid_cols;
    int _grid_rows;
    int _default_view_radius;
    std::vector<AoiGrid> _grids;
    std::unordered_map<EntityId, AoiEntity> _entities;
    AoiCallback _callback;
};

// Factory: create the default AOI implementation (NineGridAoi).
inline std::unique_ptr<IAoi> make_default_aoi(float w, float h, float cell,
                                               int radius = 2) {
    return std::make_unique<NineGridAoi>(w, h, cell, radius);
}

}  // namespace gs
