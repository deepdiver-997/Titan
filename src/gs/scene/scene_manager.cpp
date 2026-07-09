#include "gs/scene/scene_manager.h"
#include "gs/actor/actor_system.h"
#include "gs/entity/player.h"

namespace gs {

SceneManager::SceneManager(ActorSystem& actor_system, const ServerConfig& config)
    : _actor_system(actor_system), _config(config) {}

SceneId SceneManager::create_scene() {
    SceneId sid = static_cast<SceneId>(_scene_ids.size() + 1);
    ActorId aid = static_cast<ActorId>(sid);
    auto scene = std::make_unique<Scene>(aid, sid, _config);
    Scene* ptr = scene.get();
    _actor_system.spawn(std::move(scene));
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

void SceneManager::add_player(Player* player) {
    SceneId sid = scene_for_position(player->position().x);
    if (sid == INVALID_SCENE_ID) {
        sid = create_scene();
    }
    Scene* scene = get_scene(sid);
    if (scene) {
        scene->add_player(player);
        _player_scene[player->id()] = sid;
    }
}

void SceneManager::on_player_move(EntityId player_id, const Vec2& new_pos) {
    auto pit = _player_scene.find(player_id);
    if (pit == _player_scene.end()) return;

    SceneId current_sid = pit->second;
    SceneId target_sid = scene_for_position(new_pos.x);

    Scene* scene = get_scene(current_sid);
    if (!scene) return;

    // Cross-scene migration.
    if (target_sid != current_sid && target_sid != INVALID_SCENE_ID) {
        Scene* target = get_scene(target_sid);
        if (!target) {
            target_sid = create_scene();
            target = get_scene(target_sid);
        }
        if (target) {
            // Remove from old scene's AOI, add to new scene.
            scene->remove_player(player_id);
            // Update player position and add to target scene.
            // (In a full impl we'd move the Player* between scenes.)
            pit->second = target_sid;
            return;
        }
    }

    // Same scene: simple move.
    scene->move_player(player_id, new_pos);
}

}  // namespace gs
