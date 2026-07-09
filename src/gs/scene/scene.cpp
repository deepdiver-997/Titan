#include "gs/scene/scene.h"
#include "gs/entity/player.h"
#include "gs/net/message.h"

#include <iostream>

namespace gs {

Scene::Scene(ActorId id, SceneId scene_id, const ServerConfig& config)
    : Actor(id)
    , _scene_id(scene_id)
    , _config(config)
    , _world_x_min(0)
    , _world_x_max(static_cast<float>(config.scene_width))
    , _aoi_world(config.scene_width, config.scene_height,
                  static_cast<float>(config.grid_cell_size),
                  config.aoi_radius)
    , _tick_manager(60)
    , _timing_wheel(config.tw_tick_interval_ms, config.tw_wheel_size)
{
    // Register layered tick callbacks.
    // Note: TimingWheel.tick() is driven by the main loop, NOT by TickManager,
    // to avoid double-ticking. TickManager handles game-logic layers only.
    _tick_manager.add_layer("move", config.tick_move,
        [this](int64_t) { /* NPC movement processing */ });
    _tick_manager.add_layer("skill", config.tick_skill,
        [this](int64_t) { /* Skill execution processing */ });
    _tick_manager.add_layer("aoi", config.tick_aoi,
        [this](int64_t) { /* AOI sync batching */ });

    _aoi_world.set_callback([this](EntityId id, const AoiDiff& diff) {
        this->on_aoi_change(id, diff);
    });
}

void Scene::add_player(Player* player) {
    EntityId id = player->id();
    _players[id] = player;

    AoiEntity ae;
    ae.id = id;
    ae.type = EntityType::Player;
    ae.position = player->position();
    ae.grid = world_to_grid(ae.position);
    ae.view_radius = _config.aoi_radius;
    _aoi_world.add_entity(ae);
}

void Scene::remove_player(EntityId id) {
    _aoi_world.remove_entity(id);
    _players.erase(id);
}

void Scene::move_player(EntityId id, const Vec2& new_pos) {
    auto it = _players.find(id);
    if (it == _players.end()) return;
    it->second->set_position(new_pos);
    _aoi_world.move_entity(id, new_pos);
}

void Scene::on_aoi_change(EntityId id, const AoiDiff& diff) {
    auto it = _players.find(id);
    if (it == _players.end()) return;

    for (auto eid : diff.entered) {
        it->second->on_entity_enter_view(eid);
    }
    for (auto eid : diff.left) {
        it->second->on_entity_leave_view(eid);
    }
}

void Scene::on_message(Message& msg) {
    // Dispatch concrete message types.
    auto* move_msg = dynamic_cast<MoveMessage*>(&msg);
    if (move_msg) {
        move_player(move_msg->player_id, move_msg->new_pos);
        return;
    }
    // Future: handle other message types (skill, chat, etc.)
}

}  // namespace gs
