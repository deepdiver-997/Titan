#include "timer.h"

#include <algorithm>
#include <cerrno>

namespace bthread_timer {

thread_local int64_t tls_compensation_us = 0;

// ============================================================================
// Portable thread hash (replaces macOS-only pthread_threadid_np)
// ============================================================================
static inline uint64_t thread_hash_id() {
    return std::hash<std::thread::id>{}(std::this_thread::get_id());
}

void Timer::set_thread_name() {
#if defined(__APPLE__)
    pthread_setname_np("timer");
#elif defined(__linux__)
    pthread_setname_np(pthread_self(), "timer");
#elif defined(_WIN32)
    // Windows 10 1607+: SetThreadDescription(GetCurrentThread(), L"timer");
    // For broader compatibility, use the win32 RaiseException trick or just skip.
    // The thread name is for debugging only; name_for_thread() is non-critical.
#endif
}

// ============================================================================
// TaskId encoding (same as brpc: version << 32 | slot)
// ============================================================================
inline TaskId Timer::make_task_id(uint32_t slot, uint32_t version) {
    return (static_cast<uint64_t>(version) << 32) | slot;
}

inline uint32_t Timer::slot_of_task_id(TaskId id) {
    return static_cast<uint32_t>(id & 0xFFFFFFFFULL);
}

inline uint32_t Timer::version_of_task_id(TaskId id) {
    return static_cast<uint32_t>(id >> 32);
}

// ============================================================================
// Task
// ============================================================================
struct Timer::Task {
    Task* next = nullptr;
    int64_t run_time_us;
    TimerCallback fn = nullptr;
    void* arg = nullptr;
    TaskId task_id = INVALID_TASK_ID;
    uint32_t flags = TASK_FLAG_NONE;

    // version states:
    //   initial_version:     pending
    //   initial_version + 1: running
    //   initial_version + 2: done / cancelled
    //
    // Atomic because unschedule() (any thread) and run_and_delete()
    // (timer thread) race on this field.
    std::atomic<uint32_t> version{2};

    bool run_and_delete() {
        const uint32_t id_version = version_of_task_id(task_id);
        uint32_t expected = id_version;
        if (version.compare_exchange_strong(
                expected, id_version + 1, std::memory_order_relaxed)) {
            if (flags & TASK_FLAG_DONT_COUNT_TIME) {
                int64_t start = now_us();
                fn(arg);
                tls_compensation_us += now_us() - start;
            } else {
                fn(arg);
            }
            version.store(id_version + 2, std::memory_order_release);
            return true;
        }
        return false;
    }

    bool try_delete() {
        return version.load(std::memory_order_relaxed) !=
               version_of_task_id(task_id);
    }
};

// ============================================================================
// Task pool: pre-allocated array + lock-free free list
// ============================================================================
bool Timer::init_task_pool(int size) {
    _task_pool.reset(new Task[size]);
    _task_pool_size = size;
    _free_list_head.store(0, std::memory_order_relaxed);
    for (int i = size - 1; i >= 1; --i) {
        _task_pool[i].version.store(2, std::memory_order_relaxed);
        uint64_t old_head = _free_list_head.load(std::memory_order_relaxed);
        uint64_t new_head;
        do {
            uint64_t aba = (old_head & 0xFFFF) + 1;
            _task_pool[i].next = reinterpret_cast<Task*>(old_head >> 16);
            new_head = (static_cast<uint64_t>(i) << 16) | aba;
        } while (!_free_list_head.compare_exchange_weak(
            old_head, new_head, std::memory_order_release,
            std::memory_order_relaxed));
    }
    return true;
}

Timer::Task* Timer::allocate_task() {
    while (true) {
        uint64_t head = _free_list_head.load(std::memory_order_acquire);
        uint32_t slot = static_cast<uint32_t>(head >> 16);
        if (slot == NULL_SLOT) return nullptr;
        Task* task = &_task_pool[slot];
        uint64_t next =
            (reinterpret_cast<uint64_t>(task->next) << 16) |
            ((head & 0xFFFF) + 1);
        if (_free_list_head.compare_exchange_weak(
                head, next, std::memory_order_release,
                std::memory_order_relaxed)) {
            task->next = nullptr;
            return task;
        }
    }
}

void Timer::return_task(Task* task) {
    uint32_t slot = static_cast<uint32_t>(task - _task_pool.get());
    task->version.store(2, std::memory_order_relaxed);
    uint64_t old_head = _free_list_head.load(std::memory_order_relaxed);
    uint64_t new_head;
    do {
        uint64_t aba = (old_head & 0xFFFF) + 1;
        task->next = reinterpret_cast<Task*>(old_head >> 16);
        new_head = (static_cast<uint64_t>(slot) << 16) | aba;
    } while (!_free_list_head.compare_exchange_weak(
        old_head, new_head, std::memory_order_release,
        std::memory_order_relaxed));
}

// ============================================================================
// Bucket
// ============================================================================
class Timer::Bucket {
public:
    Bucket()
        : _nearest_run_time(std::numeric_limits<int64_t>::max())
        , _task_head(nullptr) {}

    Bucket(const Bucket&) = delete;
    Bucket& operator=(const Bucket&) = delete;

    struct ScheduleResult {
        TaskId task_id;
        bool earlier;
    };

    ScheduleResult schedule(Timer* timer, TimerCallback fn, void* arg,
                            int64_t run_time_us,
                            uint32_t flags = TASK_FLAG_NONE) {
        Task* task = timer->allocate_task();
        if (task == nullptr) return {INVALID_TASK_ID, false};

        task->fn = fn;
        task->arg = arg;
        task->run_time_us = run_time_us;
        task->flags = flags;

        uint32_t version = task->version.load(std::memory_order_relaxed);
        if (version == 0) {
            task->version.fetch_add(2, std::memory_order_relaxed);
            version = 2;
        }
        uint32_t slot = static_cast<uint32_t>(task - timer->_task_pool.get());
        task->task_id = Timer::make_task_id(slot, version);

        bool earlier = false;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            task->next = _task_head;
            _task_head = task;
            if (run_time_us < _nearest_run_time) {
                _nearest_run_time = run_time_us;
                earlier = true;
            }
        }
        return {task->task_id, earlier};
    }

    Task* consume_tasks() {
        Task* head = nullptr;
        if (_task_head) {
            std::lock_guard<std::mutex> lk(_mutex);
            if (_task_head) {
                head = _task_head;
                _task_head = nullptr;
                _nearest_run_time = std::numeric_limits<int64_t>::max();
            }
        }
        return head;
    }

private:
    std::mutex _mutex;
    int64_t _nearest_run_time;
    Task* _task_head;
};

// ============================================================================
// Time helpers
// ============================================================================
int64_t Timer::now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// ============================================================================
// Timer — lifecycle
// ============================================================================
Timer::Timer() = default;

Timer::~Timer() {
    stop_and_join();
}

int Timer::start(const TimerOptions& options) {
    if (_started) return 0;
    if (options.num_buckets <= 0 || options.num_buckets > 1024)
        return EINVAL;
    if (options.task_pool_size < 2 || options.task_pool_size > (1 << 20))
        return EINVAL;

    _buckets.reset(new Bucket[options.num_buckets]);
    _num_buckets = options.num_buckets;

    if (!init_task_pool(options.task_pool_size)) {
        return ENOMEM;
    }

    _stop.store(false, std::memory_order_relaxed);
    _thread = std::thread(&Timer::run, this);
    _started = true;
    return 0;
}

void Timer::stop_and_join() {
    _stop.store(true, std::memory_order_relaxed);
    if (!_started) return;

    {
        std::lock_guard<std::mutex> lk(_mutex);
        _nearest_run_time_us = 0;
        _wake_signaled = true;
    }
    _wake_cv.notify_one();

    if (std::this_thread::get_id() != _thread.get_id()) {
        _thread.join();
    }
    _started = false;
}

// ============================================================================
// Timer — schedule / unschedule
// ============================================================================
TaskId Timer::schedule(TimerCallback fn, void* arg,
                       std::chrono::steady_clock::time_point at,
                       uint32_t flags) {
    if (_stop.load(std::memory_order_relaxed) || !_started)
        return INVALID_TASK_ID;

    int64_t run_time_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            at.time_since_epoch())
            .count();

    int bucket_idx = thread_hash_id() % _num_buckets;
    Bucket::ScheduleResult result =
        _buckets[bucket_idx].schedule(this, fn, arg, run_time_us, flags);

    if (result.task_id == INVALID_TASK_ID)
        return INVALID_TASK_ID;

    if (result.earlier) {
        bool need_wake = false;
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (run_time_us < _nearest_run_time_us) {
                _nearest_run_time_us = run_time_us;
                _wake_signaled = true;
                need_wake = true;
            }
        }
        if (need_wake) {
            _wake_cv.notify_one();
        }
    }
    return result.task_id;
}

int Timer::unschedule(TaskId task_id) {
    uint32_t slot = slot_of_task_id(task_id);
    if (slot == 0 || slot >= static_cast<uint32_t>(_task_pool_size))
        return -1;

    Task* task = &_task_pool[slot];
    uint32_t id_version = version_of_task_id(task_id);
    uint32_t expected = id_version;

    if (task->version.compare_exchange_strong(expected, id_version + 2,
                                              std::memory_order_acquire))
        return 0;
    if (expected == id_version + 1) return 1;
    return -1;
}

// ============================================================================
// Timer thread — main loop
// ============================================================================

bool Timer::task_run_time_greater(const Task* a, const Task* b) {
    return a->run_time_us > b->run_time_us;
}

void Timer::run() {
    set_thread_name();

    std::vector<Task*> tasks;
    tasks.reserve(4096);

    while (!_stop.load(std::memory_order_relaxed)) {
        // ---- Step 1: Reset nearest -----------------------------------------
        {
            std::lock_guard<std::mutex> lk(_mutex);
            if (_stop.load(std::memory_order_relaxed)) break;
            _nearest_run_time_us = std::numeric_limits<int64_t>::max();
        }

        // ---- Step 2: Pull all bucket lists into local min-heap -----------
        for (int i = 0; i < _num_buckets; ++i) {
            for (Task* p = _buckets[i].consume_tasks(); p != nullptr;) {
                Task* next = p->next;
                if (!p->try_delete()) {
                    tasks.push_back(p);
                    std::push_heap(tasks.begin(), tasks.end(),
                                   task_run_time_greater);
                } else {
                    return_task(p);
                }
                p = next;
            }
        }

        // ---- Step 3: Execute ready tasks --------------------------------
        bool pull_again = false;
        while (!tasks.empty()) {
            Task* task1 = tasks[0];
            if ((now_us() - tls_compensation_us) < task1->run_time_us) break;

            {
                std::lock_guard<std::mutex> lk(_mutex);
                if (task1->run_time_us > _nearest_run_time_us) {
                    pull_again = true;
                    break;
                }
            }

            std::pop_heap(tasks.begin(), tasks.end(), task_run_time_greater);
            tasks.pop_back();
            task1->run_and_delete();
            return_task(task1);
        }
        if (pull_again) continue;

        // ---- Step 4: Sleep via cond_var ----------------------------------
        int64_t next_run_time = std::numeric_limits<int64_t>::max();
        if (!tasks.empty()) next_run_time = tasks[0]->run_time_us;

        {
            std::unique_lock<std::mutex> lk(_mutex);
            if (next_run_time > _nearest_run_time_us) {
                continue;  // newer earlier task, re-pull buckets
            }
            _nearest_run_time_us = next_run_time;

            // Compute absolute timeout.
            if (next_run_time != std::numeric_limits<int64_t>::max()) {
                int64_t delta = next_run_time - (now_us() - tls_compensation_us);
                if (delta <= 0) continue;  // already overdue, re-check

                auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::microseconds(delta);

                // Wait until deadline OR a scheduling thread signals us.
                _wake_signaled = false;
                _wake_cv.wait_until(lk, deadline, [this]() {
                    return _stop.load(std::memory_order_relaxed) || _wake_signaled;
                });
            } else {
                // No pending tasks — wait indefinitely for a schedule() call.
                _wake_signaled = false;
                _wake_cv.wait(lk, [this]() {
                    return _stop.load(std::memory_order_relaxed) || _wake_signaled;
                });
            }
            // On wake: lk is re-acquired, scope ends → mutex released.
        }
    }
}

}  // namespace bthread_timer
