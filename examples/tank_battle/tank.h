#pragma once

#include "gs/entity/entity.h"

namespace tb {

// func_id mapping:
//   0 = move  (args: float dx, float dy)
//   1 = fire  (args: float dx, float dy) — direction
class Tank : public gs::Entity {
public:
    Tank(gs::EntityId id, const gs::Vec2& pos);

    gs::Vec2 facing() const { return _facing; }

    void exec(int func_id, const void* args, size_t len,
              gs::Actor& self) override;

    gs::Vec2 apply_move(const gs::Vec2& delta);

private:
    gs::Vec2 _facing = {0, -1};
};

}  // namespace tb
