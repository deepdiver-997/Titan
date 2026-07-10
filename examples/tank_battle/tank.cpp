#include "tank.h"
#include "bullet.h"
#include "battle_scene.h"

#include <cmath>
#include <cstring>

namespace tb {

Tank::Tank(gs::EntityId id, const gs::Vec2& pos)
    : gs::Entity(id, gs::EntityType::Player) {
    set_position(pos);
}

gs::Vec2 Tank::apply_move(const gs::Vec2& delta) {
    if (delta.x != 0 || delta.y != 0) {
        float mag = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        _facing = {delta.x / mag, delta.y / mag};
    }
    gs::Vec2 new_pos(position().x + delta.x, position().y + delta.y);
    set_position(new_pos);
    return new_pos;
}

void Tank::exec(int func_id, const void* args, size_t len,
                gs::Actor& self) {
    auto* scene = dynamic_cast<BattleScene*>(&self);
    if (!scene) return;

    if (func_id == 0) {  // move (delta)
        if (len < 8) return;
        float dx, dy;
        std::memcpy(&dx, static_cast<const char*>(args), 4);
        std::memcpy(&dy, static_cast<const char*>(args) + 4, 4);
        auto np = apply_move({dx, dy});
        scene->move_in_aoi(id(), np);
    } else if (func_id == 1) {  // fire
        if (len < 8) return;
        float dx, dy;
        std::memcpy(&dx, static_cast<const char*>(args), 4);
        std::memcpy(&dy, static_cast<const char*>(args) + 4, 4);
        auto* scene = dynamic_cast<BattleScene*>(&self);
        if (scene) scene->spawn_bullet(id(), position(), {dx, dy});
    }
}

}  // namespace tb
