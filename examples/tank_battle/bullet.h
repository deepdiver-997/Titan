#pragma once

#include "gs/entity/entity.h"

namespace tb {

// Fast-moving projectile. Updated at bullet tick frequency (125Hz).
// Lifetime managed by countdown — each tick decrements, explodes at 0.
class Bullet : public gs::Entity {
public:
    Bullet(gs::EntityId id, gs::EntityId owner,
           const gs::Vec2& pos, const gs::Vec2& velocity,
           int lifetime_ms);

    // Move + decrement lifetime. Returns new position.
    gs::Vec2 tick(float speed, int dt_ms);

    bool is_alive() const { return _lifetime_ms > 0; }
    gs::EntityId owner() const { return _owner; }

private:
    gs::EntityId _owner;
    gs::Vec2 _velocity;
    int _lifetime_ms;
};

}  // namespace tb
