#include "scene.h"
#include "gs/net/message.h"

#include <iostream>

namespace gs {

Scene::Scene(ActorId id, SceneId scene_id, const ServerConfig& config)
    : Actor(id, "scene_" + std::to_string(scene_id))
    , _scene_id(scene_id)
    , _config(config)
    , _world_x_min(0)
    , _world_x_max(static_cast<float>(config.scene_width))
    , _aoi_world(config.scene_width, config.scene_height,
                  static_cast<float>(config.grid_cell_size),
                  config.aoi_radius)
{
    _aoi_world.set_callback([this](EntityId id, const AoiDiff& diff) {
        if (_aoi_cb) _aoi_cb(id, diff);
    });
}

void Scene::add_entity(EntityId id, std::shared_ptr<Entity> e) {
    _entities[id] = std::move(e);
}

void Scene::remove_entity(EntityId id) {
    _entities.erase(id);
}

Entity* Scene::get_entity(EntityId id) {
    auto it = _entities.find(id);
    return it != _entities.end() ? it->second.get() : nullptr;
}

void Scene::register_in_aoi(EntityId id, const Vec2& pos, EntityType type) {
    AoiEntity ae;
    ae.id = id;
    ae.type = type;
    ae.position = pos;
    ae.grid = world_to_grid(pos);
    ae.view_radius = _config.aoi_radius;
    _aoi_world.add_entity(ae);
}

void Scene::move_in_aoi(EntityId id, const Vec2& new_pos) {
    _aoi_world.move_entity(id, new_pos);
}

void Scene::on_message(Message& msg) {
    auto* cmd = dynamic_cast<ExecCmdMessage*>(&msg);
    if (cmd) {
        auto* e = get_entity(cmd->entity_id);
        if (e) e->exec(cmd->func_id, cmd->args.data(), cmd->args.size(), *this);
    }
}

}  // namespace gs
