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

    // ---- Output -----------------------------------------------------------
    // Application-level send callback: (target_entity, data)
    // Called by AOI / game logic to push data toward a client.
    using SendCb = std::function<void(EntityId, const std::vector<uint8_t>&)>;
    void set_send_callback(SendCb cb) { _send_cb = std::move(cb); }
    const SendCb& send_callback() const { return _send_cb; }

protected:
    using AoiCb = std::function<void(EntityId, const AoiDiff&)>;
    void set_aoi_callback(AoiCb cb) { _aoi_cb = std::move(cb); }
    const ServerConfig& config() const { return _config; }
    void on_message(Message& msg) override;

private:
    SceneId _scene_id;
    const ServerConfig& _config;
    float _world_x_min, _world_x_max;
    SendCb _send_cb;

    std::unique_ptr<IAoi> _aoi;
    std::unordered_map<EntityId, std::shared_ptr<Entity>> _entities;
    AoiCb _aoi_cb;
};

}  // namespace gs
