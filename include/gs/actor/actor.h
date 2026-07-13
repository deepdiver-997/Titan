#pragma once

#include "gs/actor/mailbox.h"
#include "gs/common/types.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace gs::debug {
class SnapshotWriter;
class SnapshotReader;
}  // namespace gs::debug

namespace gs {

// Inter-Actor message buffered in outbox. After process_all() finishes,
// ActorSystem automatically routes each pending message to the target
// Actor's _next_mailbox.
struct PendingMsg {
    ActorId target_actor;
    std::unique_ptr<Message> msg;
};

class Actor {
public:
    explicit Actor(ActorId id, std::string name = "");
    virtual ~Actor() = default;

    Actor(const Actor&) = delete;
    Actor& operator=(const Actor&) = delete;

    ActorId id() const { return _id; }
    const std::string& name() const { return _name; }
    void set_name(const std::string& n) { _name = n; }
    bool is_active() const { return _active; }
    void set_active(bool a) { _active = a; }

    // Draining: stop accepting new work, finish pending, then shutdown.
    bool is_draining() const { return _draining; }
    void set_draining(bool d) { _draining = d; }

    // Immediate send to another Actor's _next_mailbox (mid-tick inter-Actor).
    void send(ActorId target, std::unique_ptr<Message> msg);

    // Deferred send — queued in outbox, auto-routed by ActorSystem after
    // process_all() completes. Use this for messages generated during
    // on_message() that should reach the target in the next tick.
    void send_deferred(ActorId target, std::unique_ptr<Message> msg);

    // For TCP-parsed input (same-tick consumption).
    void push_now(std::unique_ptr<Message> msg);

    // Mailbox swap + process. Called by ActorSystem.
    void swap_mailboxes();
    void process_all();

    // Drain outbox — called by ActorSystem after process_all().
    std::vector<PendingMsg> drain_outbox();

    // ---- Debug: snapshot / restore ---------------------------------------
    // Override to include custom state in debug snapshots.
    // Default no-op — zero overhead when not used.
    virtual void capture_state(debug::SnapshotWriter& w) { (void)w; }
    virtual void restore_state(debug::SnapshotReader& r) { (void)r; }

protected:
    virtual void on_message(Message& msg) = 0;

private:
    ActorId _id;
    std::string _name;
    Mailbox _next_mailbox;
    std::deque<std::unique_ptr<Message>> _cur_msgs;
    std::vector<PendingMsg> _outbox;
    bool _active = true;
    bool _draining = false;
};

}  // namespace gs
