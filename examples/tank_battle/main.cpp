#include "battle_scene.h"
#include "gs/common/config.h"
#include "gs/net/i_connection.h"
#include "gs/net/message.h"
#include "gs/net/actor/net_sync.h"
#include "gs/net/tcp/server.h"
#include "gs/server/titan_server.h"
#include "third_party/bthread_timer/timer.h"

#include "gs/common/logger.h"
#include <memory>

using namespace gs;

// ---- Application state ---------------------------------------------------
static bthread_timer::Timer g_conn_timer;
static std::atomic<EntityId> g_next_player_id{1};

// ---- Protocol handler registry -------------------------------------------
using Handler = std::function<void(EntityId player_id,
                                   const uint8_t* body, size_t body_len,
                                   ActorSystem& sys, ActorId scene_aid)>;
static std::unordered_map<uint8_t, Handler> _handlers;

static void parse_input(
    const std::unordered_map<EntityId, std::vector<uint8_t>>& buffers,
    ActorSystem& sys, ActorId scene_aid) {
    static int call_count = 0;
    for (const auto& [player_id, raw] : buffers) {
        size_t off = 0;
        while (off + 4 <= raw.size()) {
            uint32_t body_len = (static_cast<uint32_t>(raw[off]) << 24) |
                           (static_cast<uint32_t>(raw[off+1]) << 16) |
                           (static_cast<uint32_t>(raw[off+2]) << 8) |
                           static_cast<uint32_t>(raw[off+3]);
            if (off + 4 + body_len > raw.size()) break;
            off += 4;
            if (body_len == 0) continue;
            uint8_t type = raw[off];
            const uint8_t* body = raw.data() + off + 1;
            size_t blen = body_len - 1;

            // Debug: print every message received.
            const char* tname = type == 0x02 ? "MOVE" :
                                type == 0x06 ? "FIRE" : "???";
            LOG_SCENE_DEBUG("[parse#{}] player={} type=0x{:x}({}) body_len={}",
                              call_count, player_id, (int)type, tname, blen);
            if (type == 0x02 && blen >= 8) {
                float x, y;
                std::memcpy(&x, body, 4);
                std::memcpy(&y, body + 4, 4);
                LOG_SCENE_DEBUG(" pos=({},{})", x, y);
            } else if (type == 0x06 && blen >= 8) {
                float dx, dy;
                std::memcpy(&dx, body, 4);
                std::memcpy(&dy, body + 4, 4);
                LOG_SCENE_DEBUG(" dir=({},{})", dx, dy);
            }


            auto it = _handlers.find(type);
            if (it != _handlers.end()) {
                it->second(player_id, body, blen, sys, scene_aid);
            }
            off += body_len;
        }
    }
    ++call_count;
}

// ---- main ----------------------------------------------------------------
int main() {
    Logger::instance().init("titan", 0);
    LOG_MAIN_INFO("=== Titan Tank Battle Server ===");

    ServerConfig config;

    TitanServer server(config);
    server.init();

    auto& sys = server.actor_system();
    auto grp_move   = sys.create_tick_group("tank_move", 30);
    auto grp_bullet = sys.create_tick_group("bullet", 125);
    auto grp_aoi    = sys.create_tick_group("aoi", 10);

    auto scene = std::make_unique<tb::BattleScene>(0, 1, config);
    tb::BattleScene* battle_ptr = scene.get();
    ActorId scene_aid = sys.spawn(std::move(scene), grp_move);

    // Transport — swap TcpServer ↔ QuicServer ↔ etc. here.
    auto transport = std::make_unique<TcpServer>(server.io_context(), config);
    auto grp_net = sys.create_tick_group("net_sync", 30);
    auto net_sync = std::make_unique<NetSyncActor>(0, transport.get());
    net_sync->set_name("net_sync");
    ActorId net_aid = sys.spawn(std::move(net_sync), grp_net);
    battle_ptr->set_net_sync_target(net_aid);

    transport->set_connection_callback(
        [&](std::shared_ptr<IConnection> conn) {
            EntityId pid = g_next_player_id.fetch_add(1);
            // Create tank entity — spawn at (100, 100).
            auto tank = std::make_shared<tb::Tank>(pid, gs::Vec2(100, 100));
            battle_ptr->add_entity(pid, tank);
            battle_ptr->register_in_aoi(pid, gs::Vec2(100, 100),
                                        gs::EntityType::Player);
            // Send the player their own entity_id so the client sets _player_id.
        {
            std::string welcome_text = "ENTER " + std::to_string(pid) + " 100 100";
            std::vector<uint8_t> welcome(welcome_text.begin(), welcome_text.end());
            conn->send(encode_message(welcome));
        }

        conn->set_close_callback([&, pid]() {
                transport->unregister_conn(pid);
                battle_ptr->aoi().remove_entity(pid);
            });
            transport->register_conn(pid, conn);
        });
    transport->start();
    server.on_stop([&]{ transport->close(); });
    // Debug API is provided programmatically (no stdin console).
    // To drain connections: call transport->drain(ip, port) directly.

    // Protocol handlers.
    _handlers[0x02] = [](EntityId pid, const uint8_t* b, size_t len,
                         ActorSystem& s, ActorId aid) {
        // Move → ExecCmd: entity=pid, func_id=0, args=[dx 4B][dy 4B]
        auto msg = std::make_unique<ExecCmdMessage>();
        msg->entity_id = pid;
        msg->func_id = 0;
        msg->args.assign(b, b + len);
        s.send(aid, std::move(msg));
    };
    _handlers[0x06] = [](EntityId pid, const uint8_t* b, size_t len,
                         ActorSystem& s, ActorId aid) {
        auto msg = std::make_unique<ExecCmdMessage>();
        msg->entity_id = pid;
        msg->func_id = 1;
        msg->args.assign(b, b + len);
        s.send(aid, std::move(msg));
    };

    // Tick callbacks.
    server.schedule_tick(16, [&]() {
        auto buffers = transport->swap_all_buffers();
        if (!buffers.empty()) {
            static int tick_no = 0;
            LOG_SCENE_DEBUG("[input tick#{}] {} connections have data, total conns={}",
                              ++tick_no, buffers.size(), transport->conn_count());
        }
        parse_input(buffers, sys, scene_aid);
        sys.swap_all();
    });

    server.schedule_tick(33, [&]{ sys.process_group(grp_move); });
    server.schedule_tick(8,  [&]{ battle_ptr->tick_bullets(8); });
    server.schedule_tick(100,[&]{ sys.process_group(grp_aoi); });
    server.schedule_tick(33, [&]{ sys.process_group(grp_net); });

    server.run();
    LOG_MAIN_INFO("stopped.");
    Logger::instance().destroy();    return 0;
}
