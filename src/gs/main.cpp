#include "gs/actor/actor_system.h"
#include "gs/common/config.h"
#include "gs/common/types.h"
#include "gs/entity/player.h"
#include "gs/net/connection_manager.h"
#include "gs/net/message.h"
#include "gs/net/tcp_server.h"
#include "gs/scene/scene_manager.h"
#include "timer.h"

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <mutex>

using namespace gs;

// ---- Global state ---------------------------------------------------------
static std::atomic<bool> g_running{true};
static std::mutex g_players_mutex;
static std::unordered_map<EntityId, std::shared_ptr<Player>> g_players;
static std::atomic<EntityId> g_next_entity_id{1};

static ConnectionManager g_conn_mgr;
static bthread_timer::Timer g_conn_timer;

// ---- Signal handler -------------------------------------------------------
static void sig_handler(int sig) {
    if (sig == SIGINT) g_running.store(false);
}

// ---- Parse raw TCP data into messages, push to Scene Actor ---------------
// Parses length-prefixed messages from a raw byte stream. Any incomplete
// message at the end is left in `data` for the next tick.
static void parse_and_dispatch(const std::vector<uint8_t>& raw,
                               EntityId player_id,
                               ActorSystem& actor_system,
                               SceneManager& scene_mgr) {
    size_t offset = 0;
    while (offset + 4 <= raw.size()) {
        uint32_t body_len =
            (static_cast<uint32_t>(raw[offset]) << 24) |
            (static_cast<uint32_t>(raw[offset + 1]) << 16) |
            (static_cast<uint32_t>(raw[offset + 2]) << 8) |
            static_cast<uint32_t>(raw[offset + 3]);
        if (offset + 4 + body_len > raw.size()) break;  // incomplete
        offset += 4;

        if (body_len == 0) continue;

        uint8_t msg_type = raw[offset];
        if (msg_type == static_cast<uint8_t>(MsgType::Move) && body_len >= 9) {
            Vec2 pos;
            std::memcpy(&pos.x, raw.data() + offset + 1, 4);
            std::memcpy(&pos.y, raw.data() + offset + 5, 4);

            // Find which scene this player is in and push move message.
            // For simplicity, push to all scenes (each Scene checks if it
            // owns the player).
            for (auto sid : scene_mgr.scene_ids()) {
                auto msg = std::make_unique<MoveMessage>();
                msg->player_id = player_id;
                msg->new_pos = pos;
                actor_system.push_now(static_cast<ActorId>(sid), std::move(msg));
            }
        }

        offset += body_len;
    }
}

// ---- Per-connection timeout callback -------------------------------------
struct ConnTimeoutCtx {
    std::shared_ptr<TcpConnection> conn;
    EntityId player_id;
    std::atomic<int64_t> last_activity_ms{0};
};

static void on_conn_timeout(void* arg) {
    auto* ctx = static_cast<ConnTimeoutCtx*>(arg);
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    if (now - ctx->last_activity_ms.load() > 5000) {
        std::cout << "[timeout] player " << ctx->player_id << " idle, closing"
                  << std::endl;
        ctx->conn->close();
    }
}

// ---- Main game tick (driven by TimingWheel self-reschedule) --------------
static void game_tick(SceneManager& scene_mgr, ActorSystem& actor_system) {
    // ---- Phase 1: Collect input -----------------------------------------
    // Swap all TCP recv buffers → get byte streams keyed by player_id.
    auto buffers = g_conn_mgr.swap_all_buffers();

    // Parse each buffer and push move events to Scene Actors' cur_msgs.
    for (auto& [player_id, raw] : buffers) {
        parse_and_dispatch(raw, player_id, actor_system, scene_mgr);
    }

    // ---- Phase 2: Swap mailboxes ---------------------------------------
    // Move mid-tick inter-actor messages (from previous tick) into cur_msgs.
    actor_system.swap_all();

    // ---- Phase 3: Tick game systems -------------------------------------
    // Drive TimingWheel (per-entity timers) & TickManager (layered ticks).
    for (auto sid : scene_mgr.scene_ids()) {
        Scene* scene = scene_mgr.get_scene(sid);
        if (scene) {
            scene->tick_manager().tick();  // fires timers@60Hz, move@30Hz, etc.
        }
    }

    // ---- Phase 4: Execute Actor logic ----------------------------------
    // Each Scene Actor serial-processes its cur_msgs (move events, etc.).
    actor_system.process_all();

    // ---- Phase 5: Sync responses to clients ----------------------------
    // Send accumulated AOI events back to each player.
    // (In a full impl, the Scene would have queued AoiSyncMessages in the
    //  Actor, and we'd drain them here.)
}

int main() {
    std::cout << "=== Titan Game Server v1.0.0 ===" << std::endl;
    signal(SIGINT, sig_handler);

    ServerConfig config;

    // ---- 1. bthread_timer (network timeouts) -----------------------------
    bthread_timer::TimerOptions bt_opts;
    bt_opts.num_buckets = config.bt_num_buckets;
    bt_opts.task_pool_size = config.bt_task_pool_size;
    if (g_conn_timer.start(bt_opts) != 0) {
        std::cerr << "Failed to start bthread_timer" << std::endl;
        return 1;
    }

    // ---- 2. Actor System + Scene Manager ---------------------------------
    ActorSystem actor_system;
    SceneManager scene_mgr(actor_system, config);
    SceneId default_scene = scene_mgr.create_scene();
    std::cout << "[main] scene created, id=" << default_scene << std::endl;

    // ---- 3. Network layer (io_context on background thread) --------------
    boost::asio::io_context io_context;
    TcpServer server(io_context, config);

    server.set_connection_callback(
        [&](std::shared_ptr<TcpConnection> conn) {
            EntityId player_id = g_next_entity_id.fetch_add(1);
            std::cout << "[main] player " << player_id << " connected: "
                      << conn->remote_addr() << std::endl;

            // Create Player entity.
            Vec2 spawn(100.0f + player_id * 50.0f, 100.0f);
            auto player = std::make_shared<Player>(
                player_id, "P" + std::to_string(player_id), spawn);

            player->set_send_callback(
                [conn](const std::string& data) {
                    std::vector<uint8_t> bytes(data.begin(), data.end());
                    conn->send(encode_message(bytes));
                });

            // Connection timeout tracking.
            auto ctx = std::make_shared<ConnTimeoutCtx>();
            ctx->conn = conn;
            ctx->player_id = player_id;
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now()
                                  .time_since_epoch())
                              .count();
            ctx->last_activity_ms.store(now_ms);

            // Schedule periodic timeout check.
            g_conn_timer.schedule(
                on_conn_timeout, ctx.get(),
                std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(config.conn_timeout_ms));

            conn->set_close_callback([player_id]() {
                std::cout << "[main] player " << player_id << " disconnected"
                          << std::endl;
                g_conn_mgr.remove(player_id);
                std::lock_guard<std::mutex> lk(g_players_mutex);
                g_players.erase(player_id);
            });

            // Register.
            g_conn_mgr.add(player_id, conn);
            scene_mgr.add_player(player.get());
            {
                std::lock_guard<std::mutex> lk(g_players_mutex);
                g_players[player_id] = player;
            }
        });

    server.start();

    // ---- 4. TimingWheel demo timer (NPC respawn) -------------------------
    Scene* scene = scene_mgr.get_scene(default_scene);
    if (scene) {
        auto counter = std::make_shared<int>(0);
        auto cb = std::make_shared<std::function<void()>>();
        *cb = [scene, counter, cb]() {
            ++(*counter);
            std::cout << "[timing_wheel] NPC respawn #" << *counter
                      << std::endl;
            scene->timing_wheel().addTask(1000, *cb);
        };
        scene->timing_wheel().addTask(1000, *cb);
    }

    // ---- 5. steady_timer drives the game tick at 60Hz --------------------
    // Being "the battery for the clock": every 16ms, advance the TimingWheel
    // by one tick, which fires expired per-entity timers. Then run game_tick
    // to swap buffers, parse input, and process Actor mailboxes.
    std::cout << "[main] steady_timer @ 60Hz, io_context.run()" << std::endl;

    int tick_counter = 0;
    auto tick_interval = std::chrono::milliseconds(config.tw_tick_interval_ms);
    auto tick_timer = std::make_shared<boost::asio::steady_timer>(io_context);

    std::function<void(const boost::system::error_code&)> on_tick;
    on_tick = [&, tick_counter, tick_timer](const boost::system::error_code& ec) mutable {
        if (ec || !g_running.load()) return;

        // Advance the clock: fire expired TimingWheel callbacks.
        scene->timing_wheel().tick();
        for (auto sid : scene_mgr.scene_ids()) {
            if (sid == default_scene) continue;
            Scene* s = scene_mgr.get_scene(sid);
            if (s) s->timing_wheel().tick();
        }

        // Run the game tick.
        game_tick(scene_mgr, actor_system);

        ++tick_counter;
        if (tick_counter % 60 == 0) {
            std::cout << "[tick " << tick_counter
                      << "] conns=" << g_conn_mgr.size()
                      << " timers=" << scene->timing_wheel().pendingTaskCount()
                      << std::endl;
        }

        // Reschedule.
        tick_timer->expires_after(tick_interval);
        tick_timer->async_wait(on_tick);
    };

    tick_timer->expires_after(tick_interval);
    tick_timer->async_wait(on_tick);

    // io_context.run() blocks the main thread — network I/O + game tick
    // all on the same event loop.
    io_context.run();

    // ---- Cleanup ---------------------------------------------------------
    g_conn_timer.stop_and_join();
    std::cout << "[main] server stopped." << std::endl;
    return 0;
}
