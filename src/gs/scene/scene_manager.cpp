#include "gs/scene/scene_manager.h"

namespace gs {

SceneManager::SceneManager(ActorSystem& actor_system, const ServerConfig& config,
                           ActorSystem::GroupId default_tick_group)
    : _actor_system(actor_system), _config(config),
      _default_group(default_tick_group) {}

SceneId SceneManager::create_scene(ActorSystem::GroupId tick_group) {
    SceneId sid = static_cast<SceneId>(_scene_ids.size() + 1);
    ActorId aid = static_cast<ActorId>(sid);
    auto scene = std::make_unique<Scene>(aid, sid, _config);
    Scene* ptr = scene.get();
    _actor_system.spawn(std::move(scene), tick_group);
    _scene_ids.push_back(sid);
    _scenes[sid] = ptr;
    return sid;
}

Scene* SceneManager::get_scene(SceneId id) {
    auto it = _scenes.find(id);
    return it != _scenes.end() ? it->second : nullptr;
}

SceneId SceneManager::scene_for_position(float world_x) const {
    int idx = static_cast<int>(world_x / _config.scene_width);
    if (idx < 0) idx = 0;
    auto sid = static_cast<SceneId>(idx + 1);
    if (_scenes.find(sid) != _scenes.end()) return sid;
    return INVALID_SCENE_ID;
}

void SceneManager::add_entity(EntityId eid, const Vec2& pos, EntityType type) {
    SceneId sid = scene_for_position(pos.x);
    if (sid == INVALID_SCENE_ID) sid = create_scene(_default_group);
    Scene* scene = get_scene(sid);
    if (scene) {
        scene->register_in_aoi(eid, pos, type);
        _entity_scene[eid] = sid;
    }
}

void SceneManager::move_entity(EntityId eid, const Vec2& new_pos) {
    auto pit = _entity_scene.find(eid);
    if (pit == _entity_scene.end()) return;

    SceneId sid = pit->second;
    Scene* scene = get_scene(sid);
    if (!scene) return;

    SceneId target = scene_for_position(new_pos.x);
    if (target != sid && target != INVALID_SCENE_ID) {
        if (!get_scene(target)) create_scene(_default_group);
        scene->aoi().remove_entity(eid);
        pit->second = target;
        return;
    }

    scene->move_in_aoi(eid, new_pos);
}

}  // namespace gs
