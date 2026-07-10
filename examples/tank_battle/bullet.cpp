#include "bullet.h"

namespace tb {

Bullet::Bullet(gs::EntityId id, const gs::Vec2& pos, const gs::Vec2& velocity,
               int lifetime_ms)
    : gs::Entity(id, gs::EntityType::Bullet),
      _velocity(velocity),
      _lifetime_ms(lifetime_ms) {
    set_position(pos);
}

gs::Vec2 Bullet::tick(float speed, int dt_ms) {
    _lifetime_ms -= dt_ms;
    gs::Vec2 new_pos(position().x + _velocity.x * speed,
                     position().y + _velocity.y * speed);
    set_position(new_pos);
    return new_pos;
}

}  // namespace tb
