#include "battle_scene.h"

#include "gs/common/logger.h"
#include <iostream>

namespace tb {

BattleScene::BattleScene(gs::ActorId id, gs::SceneId scene_id,
                         const gs::ServerConfig& config)
    : gs::Scene(id, scene_id, config)
{
    set_aoi_callback([this](gs::EntityId viewer, const gs::AoiDiff& diff) {
        const auto& send_cb = send_callback();
        if (!send_cb) return;
        auto send_str = [&](gs::EntityId target, const std::string& s) {
            std::vector<uint8_t> data(s.begin(), s.end());
            send_cb(target, data);
        };
        for (auto eid : diff.entered) {
            auto* ent = aoi().get_entity(eid);
            float x = ent ? ent->position.x : 0;
            float y = ent ? ent->position.y : 0;
            send_str(viewer, "ENTER " + std::to_string(eid) + " " +
                     std::to_string((int)x) + " " + std::to_string((int)y));
        }
        for (auto eid : diff.moved) {
            auto* ent = aoi().get_entity(eid);
            float x = ent ? ent->position.x : 0;
            float y = ent ? ent->position.y : 0;
            send_str(viewer, "MOVE " + std::to_string(eid) + " " +
                     std::to_string((int)x) + " " + std::to_string((int)y));
        }
        for (auto eid : diff.left) {
            send_str(viewer, "LEAVE " + std::to_string(eid));
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
    _bullets[bid] = std::make_shared<Bullet>(bid, owner, pos, vel, 3000);
    register_in_aoi(bid, pos, gs::EntityType::Bullet);
    LOG_SCENE_INFO("player {} fired bullet {} at ({},{})", owner, bid, pos.x, pos.y);
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

        // ---- Collision detection ------------------------------------------
        // Brute-force check against all tanks.  In a production server this
        // would use AOI::query() for spatial filtering — the AOI grid already
        // partitions the world into 16m cells, so query(np, radius) returns
        // only entities in neighbouring cells.
        // See NineGridAoi for the query interface.
        bool hit = false;
        gs::EntityId hit_target = 0;
        for (auto& [tid, tank] : _tanks) {
            if (tid == bullet->owner()) continue;  // skip shooter
            float dx = np.x - tank.position().x;
            float dy = np.y - tank.position().y;
            if (dx * dx + dy * dy < 64.0f) {  // radius 8.0f
                hit = true;
                hit_target = tid;
                break;
            }
        }
        if (hit) {
            LOG_SCENE_INFO("bullet {} hit tank {}!", bid, hit_target);
            aoi().remove_entity(bid);
            it = _bullets.erase(it);
            // TODO: apply damage, spawn explosion effect, etc.
            continue;
        }
        ++it;
    }
}

}  // namespace tb
