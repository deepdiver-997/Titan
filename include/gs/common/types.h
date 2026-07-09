#pragma once

#include <cstdint>
#include <string>

namespace gs {

// ---- IDs ---------------------------------------------------------------
using EntityId = uint64_t;
using ActorId = uint64_t;
using SceneId = uint32_t;
using TickId = uint64_t;

constexpr EntityId INVALID_ENTITY_ID = 0;
constexpr ActorId INVALID_ACTOR_ID = 0;
constexpr SceneId INVALID_SCENE_ID = 0;

// ---- 2D vector ---------------------------------------------------------
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}

    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }
};

// ---- Grid coordinate (integer cell index) ------------------------------
struct GridPos {
    int x = 0;
    int y = 0;

    GridPos() = default;
    GridPos(int x_, int y_) : x(x_), y(y_) {}

    bool operator==(const GridPos& o) const { return x == o.x && y == o.y; }
    bool operator!=(const GridPos& o) const { return !(*this == o); }

    struct Hash {
        size_t operator()(const GridPos& g) const {
            return (static_cast<uint64_t>(g.x) << 32) |
                   (static_cast<uint64_t>(g.y) & 0xFFFFFFFF);
        }
    };
};

// ---- World ↔ Grid conversion (grid cell size = 16m) -------------------
constexpr float GRID_CELL_SIZE = 16.0f;

inline GridPos world_to_grid(const Vec2& pos) {
    return {static_cast<int>(pos.x / GRID_CELL_SIZE),
            static_cast<int>(pos.y / GRID_CELL_SIZE)};
}

// ---- Entity type -------------------------------------------------------
enum class EntityType : uint8_t {
    Player = 0,
    Npc = 1,
    Bullet = 2,
};

// ---- AOI event types ---------------------------------------------------
enum class AoiEventType : uint8_t {
    Enter = 0,  // 实体进入视野
    Leave = 1,  // 实体离开视野
    Move = 2,   // 实体在视野内移动
};

// ---- Message base type -------------------------------------------------
struct Message {
    virtual ~Message() = default;
};

}  // namespace gs
