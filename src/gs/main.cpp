#include "gs/common/config.h"
#include "gs/entity/player.h"
#include "gs/net/i_connection.h"
#include "gs/net/message.h"
#include "gs/net/net_sync_actor.h"
#include "gs/net/tcp_server.h"
#include "gs/scene/scene_manager.h"
#include "gs/server/titan_server.h"
#include "third_party/bthread_timer/timer.h"

#include <iostream>
#include <memory>

using namespace gs;

// ---- Application state ---------------------------------------------------
static bthread_timer::Timer g_conn_timer;
static std::unordered_map<EntityId, std::shared_ptr<Player>> g_players;
static std::atomic<EntityId> g_next_player_id{1};

// ---- Parse TCP data → push to Scene Actors ------------------------------
static void parse_input(
    const std::unordered_map<EntityId, std::vector<uint8_t>>& buffers,
    ActorSystem& sys, SceneManager& scene_mgr) {
    for (const auto& [player_id, raw] : buffers) {
        size_t off = 0;
        while (off + 4 <= raw.size()) {
            uint32_t len = (static_cast<uint32_t>(raw[off]) << 24) |
                           (static_cast<uint32_t>(raw[off+1]) << 16) |
                           (static_cast<uint32_t>(raw[off+2]) << 8) |
                           static_cast<uint32_t>(raw[off+3]);
            if (off + 4 + len > raw.size()) break;
            off += 4;
            if (len == 0) continue;

            if (raw[off] == static_cast<uint8_t>(MsgType::Move) && len >= 9) {
                Vec2 pos;
                std::memcpy(&pos.x, raw.data() + off + 1, 4);
                std::memcpy(&pos.y, raw.data() + off + 5, 4);
                for (auto sid : scene_mgr.scene_ids()) {
                    auto msg = std::make_unique<MoveMessage>();
                    msg->player_id = player_id;
                    msg->new_pos = pos;
                    sys.push_now(static_cast<ActorId>(sid), std::move(msg));
                }
            }
            off += len;
        }
    }
}


// ---- main ----------------------------------------------------------------
int main() {
    std::cout << "=== Titan Game Server v1.0.0 ===" << std::endl;

    ServerConfig config;

    // 1. Framework.
    TitanServer server(config);
    server.init();

    // 2. Network timeout timer.
    bthread_timer::TimerOptions bt_opts;
    bt_opts.num_buckets = config.bt_num_buckets;
    bt_opts.task_pool_size = config.bt_task_pool_size;
    g_conn_timer.start(bt_opts);

    // 3. Tick groups.
    auto& sys = server.actor_system();
    auto grp_move  = sys.create_tick_group("move", 30);
    auto grp_skill = sys.create_tick_group("skill", 60);
    auto grp_aoi   = sys.create_tick_group("aoi", 10);

    // 4. Scene.
    SceneManager scene_mgr(sys, config, grp_move);
    SceneId sid = scene_mgr.create_scene(grp_move);
    Scene* default_scene = scene_mgr.get_scene(sid);

    // 4b. Transport — swap TcpServer ↔ QuicServer ↔ etc. here.
    auto transport = std::make_unique<TcpServer>(server.io_context(), config);

    // 4c. NetSyncActor — the only Actor that touches network output.
    auto grp_net = sys.create_tick_group("net_sync", 30);
    auto net_sync = std::make_unique<NetSyncActor>(0, transport.get());
    net_sync->set_name("net_sync");
    ActorId net_sync_aid = sys.spawn(std::move(net_sync), grp_net);
    if (default_scene) default_scene->set_net_sync_target(net_sync_aid);
    transport->set_connection_callback([&](std::shared_ptr<IConnection> conn) {
        EntityId pid = g_next_player_id.fetch_add(1);
        auto player = std::make_shared<Player>(
            pid, "P"+std::to_string(pid), Vec2(100.f + pid*50.f, 100.f));

        conn->set_close_callback([&, pid]() {
            transport->unregister_conn(pid);
            if (default_scene) default_scene->remove_entity(pid);
        });

        transport->register_conn(pid, conn);
        scene_mgr.add_entity(pid, player->position(), EntityType::Player);
        g_players[pid] = player;
    });
    transport->start();
    server.on_stop([&]{ transport->close(); });

    // 6. Register tick callbacks — each frequency gets its own wheel.
    //
    //    Input collection (60Hz):
    server.schedule_tick(16, [&]() {
        auto buffers = transport->swap_all_buffers();
        parse_input(buffers, sys, scene_mgr);
        sys.swap_all();
    });

    //    Tick groups — each at its own frequency, independent wheels.
    server.schedule_tick(16,  [&]{ sys.process_group(grp_skill); });
    server.schedule_tick(33,  [&]{ sys.process_group(grp_move);  });
    server.schedule_tick(100, [&]{ sys.process_group(grp_aoi);   });
    server.schedule_tick(33,  [&]{ sys.process_group(grp_net);   });

    // 7. Run.
    server.run();
    g_conn_timer.stop_and_join();
    std::cout << "[main] stopped." << std::endl;
    return 0;
}
