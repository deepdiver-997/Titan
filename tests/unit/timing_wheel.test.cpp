#include <catch2/catch_test_macros.hpp>

#include "third_party/timing_wheel/timing_wheel.h"

#include <atomic>


TEST_CASE("TimingWheel: add and fire task", "[timing_wheel]") {
    TimingWheel wheel(10, 8);  // 10ms per tick, 8 slots per level
    std::atomic<int> fired{0};

    wheel.addTask(10, [&]() { fired++; });  // fires in 1 tick
    REQUIRE(wheel.pendingTaskCount() == 1);

    wheel.tick();
    REQUIRE(fired == 1);
    REQUIRE(wheel.pendingTaskCount() == 0);
}

TEST_CASE("TimingWheel: task at multiples of tick", "[timing_wheel]") {
    TimingWheel wheel(10, 8);
    int count = 0;

    wheel.addTask(30, [&]() { count++; });  // fires after 3 ticks

    wheel.tick();  // tick 1
    REQUIRE(count == 0);
    wheel.tick();  // tick 2
    REQUIRE(count == 0);
    wheel.tick();  // tick 3
    REQUIRE(count == 1);
}

TEST_CASE("TimingWheel: cancel task", "[timing_wheel]") {
    TimingWheel wheel(10, 8);
    int count = 0;

    auto id = wheel.addTask(10, [&]() { count++; });
    REQUIRE(wheel.cancelTask(id));

    wheel.tick();
    REQUIRE(count == 0);  // was cancelled, never fires
}

TEST_CASE("TimingWheel: cascade across levels", "[timing_wheel]") {
    // With wheel_size=4, level-0 covers 4 ticks.
    // A task at 20ms (2 ticks) stays in level-0.
    // A task at 50ms (5 ticks) overflows to level-1,
    // cascades back to level-0 when level-0 wraps.
    TimingWheel wheel(10, 4);

    // Fire at tick 5 (50ms). Level-0 has 4 slots (ticks 0-3),
    // tick 5 wraps to slot 1 of level-1.
    std::atomic<int> fired{0};
    wheel.addTask(50, [&]() { fired++; });

    for (int i = 0; i < 5; ++i) wheel.tick();
    REQUIRE(fired == 1);
}

TEST_CASE("TimingWheel: multiple tasks same slot", "[timing_wheel]") {
    TimingWheel wheel(10, 8);
    std::atomic<int> count{0};

    wheel.addTask(10, [&]() { count++; });  // tick 1
    wheel.addTask(10, [&]() { count++; });  // tick 1
    wheel.addTask(10, [&]() { count++; });  // tick 1

    wheel.tick();
    REQUIRE(count == 3);
}

TEST_CASE("TimingWheel: gc does not remove pending tasks", "[timing_wheel]") {
    TimingWheel wheel(10, 8);
    wheel.addTask(10, []() {});
    REQUIRE(wheel.pendingTaskCount() == 1);
    wheel.gc();
    // gc only cleans cancelled_/processed_ metadata, not pending tasks
    REQUIRE(wheel.pendingTaskCount() == 1);
}
