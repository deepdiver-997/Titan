#pragma once

#include "gs/actor/actor.h"
#include "gs/aoi/aoi_world.h"
#include "gs/common/config.h"
#include "gs/common/types.h"
#include "gs/entity/entity.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace gs {

// Generic spatial-simulation Actor. Provides AOI + per-entity TimingWheel +
// entity storage. Subclasses specialize for specific game logic.
//
// Lives in examples/common/ — NOT part of the core framework. The framework
// only provides Actor + ActorSystem + TitanServer.
class Scene : public Actor {
public:
    Scene(ActorId id, SceneId scene_id, const ServerConfig& config);

    SceneId scene_id() const { return _scene_id; }

    // ---- Entity management (subclasses build their world here) ------------
    void add_entity(EntityId id, std::shared_ptr<Entity> e);
    void remove_entity(EntityId id);
    Entity* get_entity(EntityId id);
    size_t entity_count() const { return _entities.size(); }

    // ---- AOI --------------------------------------------------------------
    AoiWorld& aoi_world() { return _aoi_world; }

    // Register / move an entity in the AOI grid.
    void register_in_aoi(EntityId id, const Vec2& pos, EntityType type);
    void move_in_aoi(EntityId id, const Vec2& new_pos);


    // ---- NetSync ----------------------------------------------------------
    void set_net_sync_target(ActorId aid) { _net_sync_aid = aid; }
    ActorId net_sync_target() const { return _net_sync_aid; }

protected:
    // Subclasses set this to receive AOI diffs.
    using AoiCb = std::function<void(EntityId, const AoiDiff&)>;
    void set_aoi_callback(AoiCb cb) { _aoi_cb = std::move(cb); }

    const ServerConfig& config() const { return _config; }

    // Default message dispatch: routes ExecCmdMessage → entity->exec().
    // Subclasses override and call Scene::on_message first for ExecCmd.
    void on_message(Message& msg) override;

private:
    SceneId _scene_id;
    const ServerConfig& _config;
    float _world_x_min, _world_x_max;
    ActorId _net_sync_aid = INVALID_ACTOR_ID;

    AoiWorld _aoi_world;
    std::unordered_map<EntityId, std::shared_ptr<Entity>> _entities;
    AoiCb _aoi_cb;
};

}  // namespace gs
