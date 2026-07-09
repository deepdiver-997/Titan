#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>

namespace gs {

struct Message;

// Thread-safe message queue for Actors.
// Multiple producers → single consumer.
class Mailbox {
public:
    void push(std::unique_ptr<Message> msg);
    std::unique_ptr<Message> pop();
    bool try_pop(std::unique_ptr<Message>& out);
    bool empty() const;
    size_t size() const;

    // Atomically swap out all messages. Returns the deque contents,
    // leaving this mailbox empty.
    std::deque<std::unique_ptr<Message>> swap_all();

private:
    mutable std::mutex _mutex;
    std::deque<std::unique_ptr<Message>> _queue;
    std::condition_variable _cv;
};

}  // namespace gs
