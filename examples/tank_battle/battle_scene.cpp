#include "battle_scene.h"
#include "gs/net/message.h"
#include "gs/net/actor/net_sync.h"

#include "gs/common/logger.h"
#include <iostream>

namespace tb {

BattleScene::BattleScene(gs::ActorId id, gs::SceneId scene_id,
                         const gs::ServerConfig& config)
    : gs::Scene(id, scene_id, config)
{
    set_aoi_callback([this](gs::EntityId viewer, const gs::AoiDiff& diff) {
        auto aid = net_sync_target();
        if (aid == gs::INVALID_ACTOR_ID) return;
        for (auto eid : diff.entered) {
            auto* ent = aoi().get_entity(eid);
            float x = ent ? ent->position.x : 0;
            float y = ent ? ent->position.y : 0;
            auto msg = std::make_unique<gs::ClientBoundMsg>();
            msg->target_player = viewer;
            msg->data = "ENTER " + std::to_string(eid) + " " +
                        std::to_string((int)x) + " " + std::to_string((int)y);
            send_deferred(aid, std::move(msg));
        }
        for (auto eid : diff.moved) {
            auto* ent = aoi().get_entity(eid);
            float x = ent ? ent->position.x : 0;
            float y = ent ? ent->position.y : 0;
            auto msg = std::make_unique<gs::ClientBoundMsg>();
            msg->target_player = viewer;
            msg->data = "MOVE " + std::to_string(eid) + " " +
                        std::to_string((int)x) + " " + std::to_string((int)y);
            send_deferred(aid, std::move(msg));
        }
        for (auto eid : diff.left) {
            auto msg = std::make_unique<gs::ClientBoundMsg>();
            msg->target_player = viewer;
            msg->data = "LEAVE " + std::to_string(eid);
            send_deferred(aid, std::move(msg));
        }
    });
}

void BattleScene::on_message(gs::Message& msg) {
    gs::Scene::on_message(msg);
}

void BattleScene::spawn_bullet(gs::EntityId owner, const gs::Vec2& pos,
                                const gs::Vec2& dir) {
    gs::EntityId bid = _next_bullet_id++;
    gs::Vec2 vel(dir.x * 300.0f, dir.y * 300.0f);
    _bullets[bid] = std::make_shared<Bullet>(bid, pos, vel, 3000);
    register_in_aoi(bid, pos, gs::EntityType::Bullet);
    std::cout << "[battle] player " << owner << " fired bullet "
              << bid << std::endl;
}

void BattleScene::tick_bullets(int dt_ms) {
    const float bs = 16.0f;
    for (auto it = _bullets.begin(); it != _bullets.end();) {
        auto& [bid, bullet] = *it;
        if (!bullet->is_alive()) {
            aoi().remove_entity(bid);
            it = _bullets.erase(it);
            LOG_SCENE_TRACE("bullet {} expired", bid);
            continue;
        }
        gs::Vec2 np = bullet->tick(bs, dt_ms);
        move_in_aoi(bid, np);
        ++it;
    }
}

}  // namespace tb
