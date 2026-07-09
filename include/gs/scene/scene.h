#pragma once

#include "gs/actor/actor.h"
#include "gs/aoi/aoi_world.h"
#include "gs/common/config.h"
#include "gs/common/types.h"
#include "gs/tick/tick_manager.h"
#include "timing_wheel.h"

#include <memory>
#include <unordered_map>

namespace gs {

class Entity;
class Player;

// A Scene is an Actor that owns an AOI world, tick system, and entity
// collection. It represents one map shard (e.g. "map region 0-6400").
//
// Key design:
// - Scene IS an Actor → serial execution within its mailbox, no locks.
// - Owns a TimingWheel for per-entity timers (skill CDs, buffs).
// - Owns a TickManager for frame-level periodic logic (move, AOI sync).
// - Communicates with other Scenes via the SceneManager for cross-scene AOI.
class Scene : public Actor {
public:
    Scene(ActorId id, SceneId scene_id, const ServerConfig& config);

    SceneId scene_id() const { return _scene_id; }

    // Add a player entity into this scene.
    void add_player(Player* player);

    // Remove a player from this scene.
    void remove_player(EntityId id);

    // Move a player to a new position. Triggers AOI diff computation.
    void move_player(EntityId id, const Vec2& new_pos);

    // Process one tick of game logic (move + AOI + skill).
    void tick_game(int64_t tick);

    // Get the AOI world (for querying visible entities).
    const AoiWorld& aoi_world() const { return _aoi_world; }
    AoiWorld& aoi_world() { return _aoi_world; }

    // Get the TimingWheel (for per-entity timer scheduling).
    TimingWheel& timing_wheel() { return _timing_wheel; }

    const ServerConfig& config() const { return _config; }

    // Scene world boundaries.
    float world_x_min() const { return _world_x_min; }
    float world_x_max() const { return _world_x_max; }

    // Elapsed game ticks.
    int64_t tick_count() const { return _tick_manager.tick_count(); }

    // Tick manager access (for driving from main loop).
    TickManager& tick_manager() { return _tick_manager; }

protected:
    void on_message(Message& msg) override;

private:
    SceneId _scene_id;
    const ServerConfig& _config;

    float _world_x_min;
    float _world_x_max;

    AoiWorld _aoi_world;
    TickManager _tick_manager;
    TimingWheel _timing_wheel;

    std::unordered_map<EntityId, Player*> _players;

    // AOI callback: when an entity's view changes, route notifications to
    // the corresponding Player connections.
    void on_aoi_change(EntityId id, const AoiDiff& diff);
};

}  // namespace gs
