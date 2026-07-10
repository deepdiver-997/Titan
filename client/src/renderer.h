#pragma once

#include "game_client.h"

#include <unordered_map>

namespace tank {

// Draws entities using raylib.
class Renderer {
public:
    Renderer(int screen_w, int screen_h, const char* title);

    bool should_close() const;
    void begin_frame();
    void end_frame();

    void draw_entities(const std::unordered_map<uint64_t, EntityState>& entities,
                       uint64_t player_id, float local_x, float local_y,
                       bool connected);

    // Get keyboard input direction (normalized).
    float input_x() const;
    float input_y() const;

    int screen_w() const { return _screen_w; }
    int screen_h() const { return _screen_h; }

private:
    int _screen_w, _screen_h;
};

}  // namespace tank
