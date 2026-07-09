#include "gs/entity/npc.h"

#include <cstdlib>

namespace gs {

Npc::Npc(EntityId id, const std::string& name, const Vec2& pos)
    : Entity(id, EntityType::Npc) {
    set_name(name);
    set_position(pos);
}

void Npc::update(int64_t /*tick*/) {
    // Simple random walk — moves 0.5m in a random direction each update.
    float dx = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 1.0f;
    float dy = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 1.0f;
    Vec2 new_pos(position().x + dx, position().y + dy);
    set_position(new_pos);
}

}  // namespace gs
