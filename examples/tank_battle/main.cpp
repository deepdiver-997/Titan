#include "battle_scene.h"
#include "gs/common/config.h"
#include "gs/net/channel.h"
#include "gs/net/i_connection.h"
#include "gs/net/message.h"
#include "gs/net/protocol/session_manager.h"
#include "gs/net/tcp/server.h"
#include "gs/server/titan_server.h"

#include "gs/common/logger.h"
#include <memory>

using namespace gs;

// ---- Application state ---------------------------------------------------
static std::unordered_map<EntityId, std::shared_ptr<Channel>> g_channels;
static std::unordered_map<SessionId, EntityId> g_session_to_entity;
static std::atomic<EntityId> g_next_player_id{1};

// ---- Protocol handler registry -------------------------------------------
using Handler = std::function<void(EntityId player_id,
                                   const uint8_t* body, size_t body_len,
                                   ActorSystem& sys, ActorId scene_aid)>;
static std::unordered_map<uint8_t, Handler> _handlers;

static void parse_packet(EntityId player_id,
                         const std::vector<uint8_t>& payload,
                         ActorSystem& sys, ActorId scene_aid) {
    static int call_count = 0;
    if (payload.empty()) return;
    uint8_t type = payload[0];
    const uint8_t* body = payload.data() + 1;
    size_t blen = payload.size() - 1;

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
    ++call_count;
}

// ---- main ----------------------------------------------------------------
int main() {
    Logger::instance().init("titan", 0);
    LOG_MAIN_INFO("=== Titan Tank Battle Server ===");

    ServerConfig config;

    TitanServer server(config);
    server.init();

    // Session management + network output.
    SessionManager session_mgr;
    auto* sink = server.net().get_sink(33);  // ~30 Hz flush group

    auto& sys = server.actor_system();
    auto grp_move   = sys.create_tick_group("tank_move", 30);
    auto grp_bullet = sys.create_tick_group("bullet", 125);
    auto grp_aoi    = sys.create_tick_group("aoi", 10);

    auto scene = std::make_unique<tb::BattleScene>(0, 1, config);
    tb::BattleScene* battle_ptr = scene.get();
    ActorId scene_aid = sys.spawn(std::move(scene), grp_move);

    // Send callback: writes directly to the Channel buffer (thread-safe).
    battle_ptr->set_send_callback([&](EntityId target,
                                       const std::vector<uint8_t>& data) {
        auto it = g_channels.find(target);
        if (it != g_channels.end()) {
            it->second->write(data);
        }
    });

    // Transport — swap TcpServer ↔ QuicServer ↔ etc. here.
    auto transport = std::make_unique<TcpServer>(server.io_context(), config);
    transport->set_connection_callback([&](std::shared_ptr<IConnection> conn) {
        session_mgr.add_connection(conn);
    });
    transport->start();
    server.on_stop([&]{ transport->close(); });

    // Session lifecycle: create tank entity + channel on new session.
    session_mgr.set_session_callback([&](std::shared_ptr<Session> session) {
        EntityId pid = g_next_player_id.fetch_add(1);

        // Create tank entity — spawn at (100, 100).
        auto tank = std::make_shared<tb::Tank>(pid, gs::Vec2(100, 100));
        battle_ptr->add_entity(pid, tank);
        battle_ptr->register_in_aoi(pid, gs::Vec2(100, 100),
                                    gs::EntityType::Player);

        // Create reliable channel bound to this session.
        auto ch = std::make_shared<Channel>(session, 0,
                                            Channel::WriteMode::Append, sink);
        g_channels[pid] = ch;
        g_session_to_entity[session->id()] = pid;

        // Welcome message — sent through Channel (buffered, flushed by IO group).
        std::string welcome = "ENTER " + std::to_string(pid) + " 100 100";
        std::vector<uint8_t> data(welcome.begin(), welcome.end());
        ch->write(data);
    });

    session_mgr.set_close_callback([&](SessionId sid) {
        auto it = g_session_to_entity.find(sid);
        if (it != g_session_to_entity.end()) {
            EntityId pid = it->second;
            battle_ptr->aoi().remove_entity(pid);
            g_channels.erase(pid);
            g_session_to_entity.erase(it);
        }
    });

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
        session_mgr.bind_pending_framed();
        session_mgr.for_each([&](Session& session) {
            auto pkts = session.drain_framed();
            if (pkts.empty()) return;
            static int tick_no = 0;
            auto it = g_session_to_entity.find(session.id());
            if (it == g_session_to_entity.end()) return;
            LOG_SCENE_DEBUG("[input tick#{}] session {} has {} packets, total sessions={}",
                              ++tick_no, session.id(), pkts.size(), g_session_to_entity.size());
            for (auto& pkt : pkts) {
                parse_packet(it->second, pkt.payload, sys, scene_aid);
            }
        });
        sys.swap_all();
    });

    server.schedule_tick(33, [&]{ sys.process_group(grp_move); });
    server.schedule_tick(8,  [&]{ battle_ptr->tick_bullets(8); });
    server.schedule_tick(100,[&]{ sys.process_group(grp_aoi); });

    server.run();
    LOG_MAIN_INFO("stopped.");
    Logger::instance().destroy();
    return 0;
}
