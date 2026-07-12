#pragma once

#include "gs/actor/actor.h"
#include "gs/aoi/i_aoi.h"
#include "gs/common/config.h"
#include "gs/common/types.h"
#include "gs/entity/entity.h"

#include <functional>
#include <memory>
#include <unordered_map>

namespace gs {

// Generic spatial-simulation Actor. Provides AOI + entity storage.
// Subclasses specialize for specific game logic.
//
// Default AOI: NineGridAoi. Pass a custom IAoi to the constructor for
// cross-linked-list, tower, or other AOI algorithms — Scene doesn't care.
//
// Part of the framework (include/gs/scene/). No coupling to Actor internals.
class Scene : public Actor {
public:
    Scene(ActorId id, SceneId scene_id, const ServerConfig& config,
          std::unique_ptr<IAoi> aoi = nullptr);

    SceneId scene_id() const { return _scene_id; }

    // ---- Entity management (subclasses build their world here) ------------
    void add_entity(EntityId id, std::shared_ptr<Entity> e);
    void remove_entity(EntityId id);
    Entity* get_entity(EntityId id);

    // ---- AOI --------------------------------------------------------------
    IAoi& aoi() { return *_aoi; }

    // Register / move an entity in the AOI system.
    void register_in_aoi(EntityId id, const Vec2& pos, EntityType type);
    void move_in_aoi(EntityId id, const Vec2& new_pos);

    // ---- NetSync ----------------------------------------------------------
    void set_net_sync_target(ActorId aid) { _net_sync_aid = aid; }
    ActorId net_sync_target() const { return _net_sync_aid; }

protected:
    using AoiCb = std::function<void(EntityId, const AoiDiff&)>;
    void set_aoi_callback(AoiCb cb) { _aoi_cb = std::move(cb); }
    const ServerConfig& config() const { return _config; }
    void on_message(Message& msg) override;

private:
    SceneId _scene_id;
    const ServerConfig& _config;
    float _world_x_min, _world_x_max;
    ActorId _net_sync_aid = INVALID_ACTOR_ID;

    std::unique_ptr<IAoi> _aoi;
    std::unordered_map<EntityId, std::shared_ptr<Entity>> _entities;
    AoiCb _aoi_cb;
};

}  // namespace gs
