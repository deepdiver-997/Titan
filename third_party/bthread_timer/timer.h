#pragma once

#include "timer_types.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>

namespace bthread_timer {

struct TimerOptions {
    // Scheduling requests are hashed into different buckets to reduce
    // contention. Larger num_buckets may NOT mean better scalability,
    // because more buckets are sparser and more likely to trigger
    // the global wake-up path.
    // Default: 13
    int num_buckets = 13;

    // Maximum number of in-flight tasks in the pre-allocated task pool.
    // When the pool is exhausted, schedule() returns INVALID_TASK_ID.
    // Default: 131072 (128K)
    int task_pool_size = 131072;
};

// A high-precision, high-performance timer backed by a dedicated
// thread. Cross-platform — uses std::condition_variable for sleep/wake
// (no epoll/kqueue), so it works on macOS, Linux, and Windows.
//
// Architecture overview (modeled after brpc's TimerThread):
// 1. Timer thread sleeps via condition_variable::wait_until() with
//    the computed next deadline.
// 2. Tasks are hashed by caller thread-id into N buckets, each with
//    its own mutex + linked list, to reduce contention.
// 3. Each bucket tracks its earliest deadline; the timer maintains a
//    global _nearest_run_time_us (under _mutex).  Scheduling threads
//    that lower it signal _wake_cv to wake the timer thread.
// 4. On wake-up the timer thread: swaps out every bucket's list,
//    pushes un-cancelled tasks into a min-heap, then pops and runs
//    every ready task (not cancelled + deadline reached).
class Timer {
public:
    Timer();
    ~Timer();

    // Non-copyable, non-movable
    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;

    // Start the timer thread.
    // Returns 0 on success, or an errno value on failure.
    int start(const TimerOptions& options = TimerOptions());

    // Stop the timer thread and join it.  After this call, schedule()
    // returns INVALID_TASK_ID.
    void stop_and_join();

    // Schedule |fn(arg)| to run at |at| (absolute steady_clock time).
    // Returns a TaskId for cancellation, or INVALID_TASK_ID on error.
    TaskId schedule(TimerCallback fn, void* arg,
                    std::chrono::steady_clock::time_point at,
                    uint32_t flags = TASK_FLAG_NONE);

    // Cancel a previously scheduled task.
    // Returns: 0=cancelled, 1=running, -1=invalid.
    int unschedule(TaskId task_id);

private:
    // ---- internal types ---------------------------------------------------
    struct Task;
    class Bucket;

    // ---- task pool --------------------------------------------------------
    bool init_task_pool(int size);
    Task* allocate_task();
    void return_task(Task* task);

    static TaskId make_task_id(uint32_t slot, uint32_t version);
    static uint32_t slot_of_task_id(TaskId id);
    static uint32_t version_of_task_id(TaskId id);

    // ---- timer thread -----------------------------------------------------
    void run();
    static bool task_run_time_greater(const Task* a, const Task* b);

    // ---- time helpers -----------------------------------------------------
    static int64_t now_us();
    static void set_thread_name();

    // ---- data members -----------------------------------------------------
    std::unique_ptr<Bucket[]> _buckets;
    int _num_buckets = 0;

    // Protects _nearest_run_time_us + _wake_cv sleep/wake sequencing.
    std::mutex _mutex;
    std::condition_variable _wake_cv;
    bool _wake_signaled = false;  // true → timer thread should re-check
    int64_t _nearest_run_time_us = std::numeric_limits<int64_t>::max();

    std::atomic<bool> _stop{false};
    bool _started = false;
    std::thread _thread;

    // Task pool -------------------------------------------------------------
    std::unique_ptr<Task[]> _task_pool;
    int _task_pool_size = 0;

    // Lock-free free list head.  Packed: (slot_index << 16) | aba_counter.
    static constexpr uint64_t NULL_SLOT = 0;
    std::atomic<uint64_t> _free_list_head{0};
};

}  // namespace bthread_timer
