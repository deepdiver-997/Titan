#include "game_client.h"
#include "renderer.h"
#include "raylib.h"

#include <cmath>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "Usage: titan_tank_client <host> <port>" << std::endl;
        std::cout << "  WASD / Arrow keys to move" << std::endl;
        return 1;
    }

    std::string host = argv[1];
    uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));

    // ---- Renderer (window first, so it opens even if server is down) ------
    tank::Renderer renderer(800, 600, "Titan Tank Battle");

    // ---- Connect to Titan server -----------------------------------------
    tank::GameClient client;
    try {
        client.connect(host, port);
    } catch (const std::exception& e) {
        std::cerr << "Connect failed: " << e.what()
                  << " — running in offline mode\n";
    }

    client.set_aoi_callback([](const tank::AoiEvent& ev) {
        const char* type = ev.type == tank::EventType::Enter   ? "ENTER"
                           : ev.type == tank::EventType::Leave ? "LEAVE"
                                                               : "MOVE";
        std::cout << "  [" << type << "] entity=" << ev.entity_id << std::endl;
    });

    // ---- Game state ------------------------------------------------------
    float player_x = 100.0f, player_y = 100.0f;
    float last_dir_x = 0.0f, last_dir_y = -1.0f;
    float last_sent_x = player_x, last_sent_y = player_y;
    const float move_speed = 150.0f;

    std::cout << "[tank_client] WASD move, SPACE fire, ESC quit" << std::endl;

    while (!renderer.should_close()) {
        float dt = GetFrameTime();
        if (dt > 0.1f) dt = 0.016f;

        // ---- Input -------------------------------------------------------
        float ix = renderer.input_x();
        float iy = renderer.input_y();

        if (ix != 0.0f || iy != 0.0f) {
            float mag = sqrtf(ix * ix + iy * iy);
            player_x += (ix / mag) * move_speed * dt;
            player_y += (iy / mag) * move_speed * dt;
            last_dir_x = ix / mag;
            last_dir_y = iy / mag;
        }

        // Clamp to screen.
        if (player_x < 16) player_x = 16;
        if (player_y < 16) player_y = 16;
        if (player_x > renderer.screen_w() - 16) player_x = renderer.screen_w() - 16;
        if (player_y > renderer.screen_h() - 16) player_y = renderer.screen_h() - 16;

        // ---- Send move delta when moving ---------------------------------
        if (ix != 0.0f || iy != 0.0f) {
            float mag = sqrtf(ix * ix + iy * iy);
            float dx = (ix / mag) * move_speed * dt;
            float dy = (iy / mag) * move_speed * dt;
            client.send_move(dx, dy);
        }

        // ---- Fire on spacebar --------------------------------------------
        if (IsKeyPressed(KEY_SPACE)) {
            client.send_fire(last_dir_x, last_dir_y);
            std::cout << "[tank_client] fired!" << std::endl;
        }

        // ---- Poll network ------------------------------------------------
        client.poll();

        // ---- Render ------------------------------------------------------
        renderer.begin_frame();
        renderer.draw_entities(client.entities(), client.player_id(),
                               player_x, player_y, true);
        renderer.end_frame();
    }

    client.disconnect();
    return 0;
}
