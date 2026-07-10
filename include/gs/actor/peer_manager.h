#pragma once

#include "gs/actor/actor.h"
#include "gs/net/i_peer.h"

#include <boost/asio.hpp>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gs {

// Distributed Actor routing — gossip discovery + position registry.
//
// Each node runs one PeerManager. It:
//  - Listens on a TCP port for incoming peer connections
//  - Connects to known peers to join the cluster
//  - Maintains _routes: ActorId → node address (who owns which Actor)
//  - Gossips: NEW_NODE / REGISTER_ACTOR / UNREGISTER_ACTOR
//  - Forwards remote Actor messages transparently
//
// Wire format (length-prefixed, same pattern as Titan messages):
//   [4B big-endian length][1B msg_type][payload]
//   msg_type: 0xE0 = REGISTER_ACTOR, 0xE1 = UNREGISTER_ACTOR,
//             0xE2 = NEW_NODE,       0xE3 = ACTOR_MSG
//
// Thread safety:
//  - _peers: mutex protected (inserts rare, lookup for broadcast)
//  - _routes: each peer's connection callback runs on a single io_context
//    thread, so route updates for a given peer are single-writer.
//    Reads from `send_to()` are lock-free (stale reads are benign —
//    worst case a message is dropped, not corrupted).
class PeerManager {
public:
    // Called when a remote Actor message arrives for a local Actor.
    using ActorMsgCallback =
        std::function<void(ActorId target, std::unique_ptr<Message> msg)>;

    // Called when a new peer connects. ActorSystem provides this to sync
    // all local Actors to the new peer under its own lock.
    using NewPeerCallback = std::function<void(const std::string& peer_addr)>;

    // @param io          the io_context shared with the rest of Titan
    // @param listen_addr bind address, e.g. "0.0.0.0"
    // @param listen_port gossip port
    PeerManager(boost::asio::io_context& io,
                const std::string& listen_addr, uint16_t listen_port);

    // ---- Callbacks --------------------------------------------------------

    // Set handler for incoming remote Actor messages.
    void set_actor_msg_callback(ActorMsgCallback cb) { _actor_cb = std::move(cb); }

    // Register callback invoked when a new peer connects (accepted or
    // actively connected). ActorSystem uses this to sync its local Actors
    // to the new peer.
    void on_new_peer(NewPeerCallback cb) { _new_peer_cb = std::move(cb); }

    // ---- Cluster management -----------------------------------------------

    // Start accepting incoming peer connections (non-blocking).
    void start_accept();

    // Actively connect to a peer to join an existing cluster.
    // After connecting, pushes local Actor list to the peer
    // and gossips the new node to all other peers.
    void connect_to_peer(const std::string& ip, uint16_t port);

    // ---- Actor registration (called by ActorSystem) -----------------------

    // Broadcast "Actor <aid> is on this node" to all connected peers.
    void broadcast_register(ActorId aid);

    // Broadcast "Actor <aid> no longer exists" to all connected peers.
    void broadcast_unregister(ActorId aid);

    // Send a REGISTER_ACTOR to a single peer (used during initial sync).
    void send_register_to(const std::string& addr, ActorId aid);

    // ---- Message routing --------------------------------------------------

    // Forward an Actor message to a remote node.
    void send_to(ActorId target, std::unique_ptr<Message> msg);

    // ---- Stats ------------------------------------------------------------
    size_t peer_count() const { return _peers.size(); }
    size_t route_count() const { return _routes.size(); }

private:
    void do_accept();
    void handle_peer_data(const std::string& peer_addr,
                          const std::vector<uint8_t>& data);
    void send_to_peer(const std::string& addr,
                      const std::vector<uint8_t>& data);
    void gossip_new_node(const std::string& new_addr);

    // Incoming message handlers.
    void handle_register(const std::string& peer_addr, ActorId aid);
    void handle_unregister(const std::string& peer_addr, ActorId aid);
    void handle_new_node(const std::string& peer_addr,
                         const std::string& new_node_addr);
    void handle_actor_msg(const std::vector<uint8_t>& data);

    // Wire encoding.
    static std::vector<uint8_t> make_frame(uint8_t type,
                                           const std::string& payload);
    static std::string encode_register(ActorId aid);
    static std::string encode_unregister(ActorId aid);
    static std::string encode_new_node(const std::string& addr);
    static std::string actor_msg_to_wire(ActorId target, Message& msg);

    boost::asio::io_context& _io;
    std::string _my_addr;
    boost::asio::ip::tcp::acceptor _acceptor;

    // ip → peer connection. Mutex for insert; broadcast iterates under lock.
    mutable std::mutex _peer_mutex;
    std::unordered_map<std::string, std::shared_ptr<IPeer>> _peers;

    // ActorId → owning node ip. Single-writer per ip (connection callback
    // thread). Lock-free reads from send_to().
    std::unordered_map<ActorId, std::string> _routes;

    // Set of all known node addresses (avoids duplicate connections).
    std::unordered_set<std::string> _known_nodes;

    ActorMsgCallback _actor_cb;
    NewPeerCallback _new_peer_cb;
};

}  // namespace gs
