#pragma once

#include "bullet.h"
#include "examples/common/scene/scene.h"
#include "tank.h"

#include <memory>
#include <unordered_map>

namespace tb {

// Tank battle Scene — extends the framework example Scene.
//
// Tick groups (registered in main.cpp):
//   tank_move@30Hz → process MoveMessages
//   bullet@125Hz  → tick_bullets()
//
// Per-entity timers via TimingWheel (inherited from Scene):
//   bullet.addTask(3000ms, explode)
class BattleScene : public gs::Scene {
public:
    BattleScene(gs::ActorId id, gs::SceneId scene_id,
                const gs::ServerConfig& config);

    void tick_bullets(int dt_ms);

    // Called by Tank::exec for fire.
    void spawn_bullet(gs::EntityId owner, const gs::Vec2& pos,
                      const gs::Vec2& dir);

protected:
    void on_message(gs::Message& msg) override;

    std::unordered_map<gs::EntityId, Tank> _tanks;
    std::unordered_map<gs::EntityId, std::shared_ptr<Bullet>> _bullets;
    gs::EntityId _next_bullet_id = 10000;
};

}  // namespace tb
