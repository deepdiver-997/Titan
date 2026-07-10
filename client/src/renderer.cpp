#include "renderer.h"
#include "raylib.h"

namespace tank {

Renderer::Renderer(int screen_w, int screen_h, const char* title)
    : _screen_w(screen_w), _screen_h(screen_h) {
    InitWindow(screen_w, screen_h, title);
    SetTargetFPS(60);
}

bool Renderer::should_close() const {
    return WindowShouldClose();
}

void Renderer::begin_frame() {
    BeginDrawing();
    ClearBackground(DARKGRAY);
    // Debug: center welcome text.
    DrawText("Titan Tank Battle - Connected",
             _screen_w / 2 - 150, _screen_h / 2 - 10,
             20, RAYWHITE);
}

void Renderer::end_frame() {
    DrawFPS(10, 10);
    EndDrawing();
}

void Renderer::draw_entities(
    const std::unordered_map<uint64_t, EntityState>& entities,
    uint64_t player_id, float local_x, float local_y, bool connected) {
    // Debug info.
    DrawText(TextFormat("entities:%d pid:%llu pos:(%.0f,%.0f) %s",
                        (int)entities.size(), (unsigned long long)player_id,
                        local_x, local_y,
                        connected ? "connected" : "offline"),
             10, 30, 16, YELLOW);

    // Always draw the local player (client-side prediction).
    DrawRectangle((int)local_x - 16, (int)local_y - 16, 32, 32,
                  connected ? GREEN : GRAY);
    DrawRectangle((int)local_x - 2, (int)local_y - 16, 4, 8, DARKGREEN);

    // Draw server-confirmed entities.
    for (const auto& [id, es] : entities) {
        if (id == player_id) continue;  // tank already drawn above
        // Bullets are small circles, other entities are rectangles.
        if (id >= 10000) {
            DrawCircle((int)es.x, (int)es.y, 4, RED);
        } else {
            DrawRectangle((int)es.x - 16, (int)es.y - 16, 32, 32, RED);
        }
    }
}

float Renderer::input_x() const {
    float dir = 0.0f;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) dir += 1.0f;
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) dir -= 1.0f;
    return dir;
}

float Renderer::input_y() const {
    float dir = 0.0f;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) dir += 1.0f;
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) dir -= 1.0f;
    return dir;
}

}  // namespace tank
