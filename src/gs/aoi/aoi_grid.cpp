#include "gs/aoi/aoi_grid.h"

namespace gs {

void AoiGrid::add(EntityId id) {
    _entities[id] = true;
}

void AoiGrid::remove(EntityId id) {
    _entities.erase(id);
}

bool AoiGrid::contains(EntityId id) const {
    return _entities.find(id) != _entities.end();
}

}  // namespace gs
