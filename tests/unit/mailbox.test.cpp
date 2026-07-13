#include <catch2/catch_test_macros.hpp>

#include "gs/actor/mailbox.h"
#include "gs/common/types.h"

#include <thread>

using namespace gs;

// Minimal Message subclass for testing.
struct TestMsg : public Message {
    int value = 0;
};

static std::unique_ptr<Message> make_msg(int v) {
    auto m = std::make_unique<TestMsg>();
    m->value = v;
    return m;
}

TEST_CASE("Mailbox: push and pop", "[actor][mailbox]") {
    Mailbox mb;
    REQUIRE(mb.empty());
    REQUIRE(mb.size() == 0);

    mb.push(make_msg(42));
    REQUIRE_FALSE(mb.empty());
    REQUIRE(mb.size() == 1);

    auto msg = mb.pop();
    REQUIRE(msg != nullptr);
    REQUIRE(static_cast<TestMsg*>(msg.get())->value == 42);

    REQUIRE(mb.empty());
    REQUIRE(mb.size() == 0);
}

TEST_CASE("Mailbox: try_pop on empty", "[actor][mailbox]") {
    Mailbox mb;
    std::unique_ptr<Message> out;
    REQUIRE_FALSE(mb.try_pop(out));
    REQUIRE(out == nullptr);
}

TEST_CASE("Mailbox: swap_all", "[actor][mailbox]") {
    Mailbox mb;
    mb.push(make_msg(1));
    mb.push(make_msg(2));
    mb.push(make_msg(3));

    auto all = mb.swap_all();
    REQUIRE(all.size() == 3);
    REQUIRE(mb.empty());

    int sum = 0;
    for (auto& m : all) sum += static_cast<TestMsg*>(m.get())->value;
    REQUIRE(sum == 6);
}

TEST_CASE("Mailbox: multi-producer thread safety", "[actor][mailbox]") {
    Mailbox mb;
    constexpr int N = 10000;

    std::thread t1([&]() {
        for (int i = 0; i < N; ++i) mb.push(make_msg(i));
    });
    std::thread t2([&]() {
        for (int i = 0; i < N; ++i) mb.push(make_msg(i));
    });
    t1.join();
    t2.join();

    REQUIRE(mb.size() == 2 * N);

    auto all = mb.swap_all();
    REQUIRE(all.size() == 2 * N);
}
