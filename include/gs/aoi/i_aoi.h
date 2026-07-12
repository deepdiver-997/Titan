#pragma once

#include "gs/aoi/aoi_entity.h"
#include "gs/common/types.h"

#include <functional>
#include <vector>

namespace gs {

// Result of an AOI operation: which entities entered, left, or moved.
struct AoiDiff {
    std::vector<EntityId> entered;
    std::vector<EntityId> left;
    std::vector<EntityId> moved;
};

using AoiCallback = std::function<void(EntityId, const AoiDiff&)>;

// Abstract AOI algorithm. Implementations: NineGridAoi, CrossLinkAoi, etc.
class IAoi {
public:
    virtual ~IAoi() = default;

    virtual void add_entity(const AoiEntity& entity) = 0;
    virtual void remove_entity(EntityId id) = 0;

    // Move entity. Returns true if the grid cell changed.
    virtual bool move_entity(EntityId id, const Vec2& new_pos) = 0;

    virtual void set_callback(AoiCallback cb) = 0;
    virtual const AoiEntity* get_entity(EntityId id) const = 0;
};

}  // namespace gs
