#pragma once

#include "gs/common/config.h"
#include "gs/common/types.h"
#include "gs/scene/scene.h"

#include <memory>
#include <unordered_map>

namespace gs {

class ActorSystem;

// Manages multiple Scene Actors and handles cross-scene entity migration.
//
// Cross-scene design (double registration + hysteresis):
//   1. When an entity approaches a Scene boundary (distance < margin),
//      a mirror entity is registered in the neighboring Scene's AOI.
//   2. When the entity moves further past the boundary (distance > hysteresis),
//      it fully migrates to the neighboring Scene.
class SceneManager {
public:
    SceneManager(ActorSystem& actor_system, const ServerConfig& config);

    // Create a new Scene, register with ActorSystem, return its SceneId.
    SceneId create_scene();

    // Get the Scene for a SceneId. Returns nullptr if not found.
    Scene* get_scene(SceneId id);

    // Add a player to the appropriate scene based on position.
    void add_player(Player* player);

    // Handle a player move. Checks if cross-scene migration is needed.
    void on_player_move(EntityId player_id, const Vec2& new_pos);

    // Determine which Scene owns a world position.
    SceneId scene_for_position(float world_x) const;

    // Number of scenes.
    size_t scene_count() const { return _scene_ids.size(); }
    const std::vector<SceneId>& scene_ids() const { return _scene_ids; }

private:
    ActorSystem& _actor_system;
    const ServerConfig& _config;

    // ActorSystem owns the Scene Actors; we keep raw pointers.
    std::vector<SceneId> _scene_ids;
    std::unordered_map<SceneId, Scene*> _scenes;

    // Map player → current scene.
    std::unordered_map<EntityId, SceneId> _player_scene;
};

}  // namespace gs
