#pragma once

#include "gs/common/types.h"

#include <unordered_map>

namespace gs {

// A single grid cell in the AOI world.
// Holds the set of entities whose center is inside this cell.
class AoiGrid {
public:
    // Register an entity in this cell.
    void add(EntityId id);

    // Remove an entity from this cell.
    void remove(EntityId id);

    // Check if an entity is in this cell.
    bool contains(EntityId id) const;

    // Get all entity IDs in this cell.
    const std::unordered_map<EntityId, bool>& entities() const { return _entities; }

    size_t size() const { return _entities.size(); }

private:
    // value is unused (bool placeholder); we use unordered_map for O(1) lookup.
    std::unordered_map<EntityId, bool> _entities;
};

}  // namespace gs
