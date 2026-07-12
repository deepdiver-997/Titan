#pragma once

#include "gs/actor/actor_system.h"
#include "gs/common/config.h"
#include "gs/common/types.h"
#include "gs/scene/scene.h"

#include <unordered_map>
#include <vector>

namespace gs {

// Manages multiple Scenes and entity migration between them.
// Entity-based (no Player dependency).
class SceneManager {
public:
    SceneManager(ActorSystem& actor_system, const ServerConfig& config,
                 ActorSystem::GroupId default_tick_group);

    SceneId create_scene(ActorSystem::GroupId tick_group);
    Scene* get_scene(SceneId id);

    // Register an entity in the appropriate scene by position.
    void add_entity(EntityId eid, const Vec2& pos, EntityType type);

    // Move an entity. Handles cross-scene migration.
    void move_entity(EntityId eid, const Vec2& new_pos);

    SceneId scene_for_position(float world_x) const;

    size_t scene_count() const { return _scene_ids.size(); }
    const std::vector<SceneId>& scene_ids() const { return _scene_ids; }

private:
    ActorSystem& _actor_system;
    const ServerConfig& _config;
    ActorSystem::GroupId _default_group;

    std::vector<SceneId> _scene_ids;
    std::unordered_map<SceneId, Scene*> _scenes;
    std::unordered_map<EntityId, SceneId> _entity_scene;
};

}  // namespace gs
