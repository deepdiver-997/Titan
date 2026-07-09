#pragma once

#include "gs/entity/entity.h"

#include <string>

namespace gs {

// NPC/monster entity — AI-driven, no network connection.
class Npc : public Entity {
public:
    Npc(EntityId id, const std::string& name, const Vec2& pos);

    // Simple random walk behavior.
    void update(int64_t tick);
};

}  // namespace gs
