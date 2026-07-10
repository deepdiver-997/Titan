#pragma once

#include "gs/entity/entity.h"

#include <string>
#include <vector>

namespace gs {

// Player entity — represents a connected client.
//
// Does NOT own network I/O. AOI events are formatted by Scene and pushed
// to the Actor outbox, which the net-sync group drains and sends.
class Player : public Entity {
public:
    Player(EntityId id, const std::string& name, const Vec2& pos);

    // Track visible entities (for game logic queries, not for sending).
    void track_enter(EntityId other_id);
    void track_leave(EntityId other_id);

    const std::vector<EntityId>& visible_entities() const { return _visible_ids; }

private:
    std::vector<EntityId> _visible_ids;
};

}  // namespace gs
