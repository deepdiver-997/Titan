#include "gs/common/config.h"
#include "gs/common/logger.h"
#include "gs/entity/player.h"
#include "gs/net/channel.h"
#include "gs/net/i_connection.h"
#include "gs/net/message.h"
#include "gs/net/protocol/session_manager.h"
#include "gs/net/tcp/server.h"
#include "gs/scene/scene_manager.h"
#include "gs/server/titan_server.h"

#include <iostream>
#include <memory>

using namespace gs;

// ---- Application state ---------------------------------------------------
static std::unordered_map<EntityId, std::shared_ptr<Channel>> g_channels;
static std::unordered_map<SessionId, EntityId> g_session_to_entity;
static std::atomic<EntityId> g_next_player_id{1};

// ---- Parse session packets → push to Scene Actors ------------------------
static void parse_packet(EntityId player_id,
                         const std::vector<uint8_t>& payload,
                         ActorSystem& sys, SceneManager& scene_mgr) {
    if (payload.empty()) return;
    MsgType type = static_cast<MsgType>(payload[0]);
    const uint8_t* body = payload.data() + 1;
    size_t body_len = payload.size() - 1;

    if (type == MsgType::Move && body_len >= 8) {
        Vec2 pos;
        std::memcpy(&pos.x, body, 4);
        std::memcpy(&pos.y, body + 4, 4);
        for (auto sid : scene_mgr.scene_ids()) {
            auto msg = std::make_unique<MoveMessage>();
            msg->player_id = player_id;
            msg->new_pos = pos;
            sys.send(static_cast<ActorId>(sid), std::move(msg));
        }
    }
}

// ---- main ----------------------------------------------------------------
int main() {
    Logger::instance().init("titan", 0);
    LOG_MAIN_INFO("=== Titan Game Server v1.0.0 ===");

    ServerConfig config;

    // 1. Framework.
    TitanServer server(config);
    server.init();

    // 2. Session management + IO frequency group for channel flushing.
    SessionManager session_mgr;
    auto io_grp = server.create_io_group(33);  // ~30 Hz flush

    // 3. Tick groups.
    auto& sys = server.actor_system();
    auto grp_move  = sys.create_tick_group("move", 30);
    auto grp_skill = sys.create_tick_group("skill", 60);
    auto grp_aoi   = sys.create_tick_group("aoi", 10);

    // 4. Scene.
    SceneManager scene_mgr(sys, config, grp_move);
    SceneId sid = scene_mgr.create_scene(grp_move);
    Scene* default_scene = scene_mgr.get_scene(sid);

    // 5. Transport.
    auto transport = std::make_unique<TcpServer>(server.io_context(), config);
    transport->set_connection_callback([&](std::shared_ptr<IConnection> conn) {
        session_mgr.add_connection(conn);
    });
    transport->start();
    server.on_stop([&]{ transport->close(); });

    // 6. Session lifecycle: create entity + channel on new session.
    session_mgr.set_session_callback([&](std::shared_ptr<Session> session) {
        EntityId pid = g_next_player_id.fetch_add(1);
        auto player = std::make_shared<Player>(
            pid, "P"+std::to_string(pid), Vec2(100.f + pid*50.f, 100.f));

        // Create reliable channel bound to this session.
        auto ch = std::make_shared<Channel>(session, 0);
        server.add_to_io_group(io_grp, ch);
        g_channels[pid] = ch;
        g_session_to_entity[session->id()] = pid;

        scene_mgr.add_entity(pid, player->position(), EntityType::Player);
    });

    session_mgr.set_close_callback([&](SessionId sid) {
        auto it = g_session_to_entity.find(sid);
        if (it != g_session_to_entity.end()) {
            EntityId pid = it->second;
            if (default_scene) default_scene->remove_entity(pid);
            g_channels.erase(pid);
            g_session_to_entity.erase(it);
        }
    });

    // 7. Tick callbacks.
    server.schedule_tick(16, [&]() {
        // Bind new connections and drain received packets.
        session_mgr.bind_pending_framed();
        session_mgr.for_each([&](Session& session) {
            auto pkts = session.drain_framed();
            for (auto& pkt : pkts) {
                auto it = g_session_to_entity.find(session.id());
                if (it == g_session_to_entity.end()) continue;
                parse_packet(it->second, pkt.payload, sys, scene_mgr);
            }
        });
        sys.swap_all();
    });

    server.schedule_tick(16,  [&]{ sys.process_group(grp_skill); });
    server.schedule_tick(33,  [&]{ sys.process_group(grp_move);  });
    server.schedule_tick(100, [&]{ sys.process_group(grp_aoi);   });

    // 8. Run.
    server.run();
    LOG_MAIN_INFO("stopped.");
    Logger::instance().destroy();
    return 0;
}
