#pragma once

#include "gs/common/types.h"

#include <unordered_set>

namespace gs {

// An entity tracked by the AOI system.
struct AoiEntity {
    EntityId id = INVALID_ENTITY_ID;
    EntityType type = EntityType::Player;
    Vec2 position;
    GridPos grid;
    int view_radius = 2;  // grid cells of visibility

    // Set of other entity IDs currently visible to this entity.
    std::unordered_set<EntityId> visible_set;
};

}  // namespace gs
